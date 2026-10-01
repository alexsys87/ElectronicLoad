/** \file
  \brief Electronic load controller: implements device.h for the real hardware.

  The constant current loop itself is analog (op-amp, MOSFET, shunt). The
  firmware:
    - computes the target: setpoint, limited to LOAD_MAX_CURRENT_MA and to
      LOAD_MAX_POWER_MW / V (constant power limit);
    - ramps up to it (soft start);
    - adds a slow integral trim from the measured current, which removes the
      op-amp offset and resistor tolerances (bounded, so a fault can't wind
      it up);
    - integrates mAh, mWh and test time;
    - stops at the cutoff voltage or when the timer expires;
    - guards: over voltage, over current, over power, over temperature,
      fan failure, ADC failure, current flowing while off;
    - drives the fan from the dissipated power (and the NTC, if fitted).

  Control runs on every new current reading (about 110 per second).
*/

#include "load.h"
#include "load_config.h"
#include "system.h"
#include "measure.h"
#include "output.h"
#include "analog.h"
#include "fan.h"

#include <math.h>

#define ZEROING_DELAY_MS    2000U     ///< Off this long before tracking the zero.
#define OFF_CURRENT_DELAY   1000U     ///< Settle time after "off" before checking.
#define DISPLAY_ZERO_MA     30.0f     ///< Show 0 below this while off.

static struct {
  // Setpoints, protocol units.
  uint16_t set_ca;
  uint16_t cutoff_cv;
  uint16_t timer_s;

  bool on;
  load_stop_t stop;
  uint32_t on_since;
  uint32_t off_since;

  float target_ma;          ///< Ramped and limited setpoint.
  float trim_ma;            ///< Digital correction.
  uint32_t last_ctrl;

  // Counters.
  double cap_mah;
  double energy_mwh;
  uint32_t time_ms;

  // Debounce / fault timers, ms.
  uint32_t below_cutoff_ms;
  uint32_t oc_ms;
  uint32_t op_ms;
  uint32_t off_current_ms;
  uint32_t fan_low_since;

  uint8_t fan_on_duty;      ///< Fan duty while on, kept for the run-on.
} l;

/* ---------------------------------------------------------------------- */
/*  Helpers                                                                */
/* ---------------------------------------------------------------------- */

static float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

static void stop_load(load_stop_t why) {
  output_enable(false);
  l.on = false;
  l.stop = why;
  l.target_ma = 0.0f;
  l.off_since = system_ms;
  l.off_current_ms = 0;
}

/// Current the load should draw now: setpoint, current and power limits.
static float limit_ma(float v) {
  float lim = (float)l.set_ca * 10.0f;

  if (lim > (float)LOAD_MAX_CURRENT_MA)
    lim = (float)LOAD_MAX_CURRENT_MA;
  if (v > 0.1f && lim * v > (float)LOAD_MAX_POWER_MW)
    lim = (float)LOAD_MAX_POWER_MW / v;               // mW / V = mA
  return lim;
}

/* ---------------------------------------------------------------------- */
/*  Control, on every new current reading                                  */
/* ---------------------------------------------------------------------- */

static void control(uint32_t now) {
  uint32_t dt = now - l.last_ctrl;
  float v = measure_voltage_v();
  float i_ma = measure_current_a() * 1000.0f;
  float lim, trim_max;

  l.last_ctrl = now;
  if (dt > 100U)
    dt = 100U;                            // After a stall, don't jump.

  if ( ! l.on) {
    // A shorted MOSFET (or wiring fault) lets current flow anyway. The
    // load can't stop that, but it must not pretend all is fine.
    if (now - l.off_since > OFF_CURRENT_DELAY && i_ma > (float)LOAD_OFF_CURRENT_MA) {
      l.off_current_ms += dt;
      if (l.off_current_ms >= 1000U)
        l.stop = LOAD_FAULT_CURRENT_WHEN_OFF;
    }
    else {
      l.off_current_ms = 0;
    }
    return;
  }

  // Target: ramp up, follow reductions immediately.
  lim = limit_ma(v);
  if (l.target_ma < lim) {
    l.target_ma += (float)LOAD_RAMP_MA_PER_S * (float)dt / 1000.0f;
    if (l.target_ma > lim)
      l.target_ma = lim;
  }
  else {
    l.target_ma = lim;
  }

  // Trim: slow integral of the error, at most 10 % of the target + 100 mA.
  trim_max = clampf(0.1f * l.target_ma + 100.0f, 0.0f, (float)LOAD_TRIM_MAX_MA);
  if (l.target_ma > 0.0f)
    l.trim_ma += LOAD_TRIM_KI * (l.target_ma - i_ma) * (float)dt / 1000.0f;
  l.trim_ma = clampf(l.trim_ma, -trim_max, trim_max);

  output_set_ma(l.target_ma > 0.0f ? l.target_ma + l.trim_ma : 0.0f);

  // Counters.
  if (i_ma > 0.0f) {
    double mah = (double)i_ma * (double)dt / 3600000.0;

    l.cap_mah += mah;
    l.energy_mwh += mah * (double)v;
  }
  l.time_ms += dt;

  // Cutoff, debounced.
  {
    float cutoff_mv = (float)l.cutoff_cv * 10.0f;

    if (cutoff_mv < (float)LOAD_MIN_CUTOFF_MV)
      cutoff_mv = (float)LOAD_MIN_CUTOFF_MV;
    if (v * 1000.0f <= cutoff_mv) {
      l.below_cutoff_ms += dt;
      if (l.below_cutoff_ms >= LOAD_CUTOFF_DEBOUNCE_MS) {
        stop_load(LOAD_STOP_CUTOFF);
        return;
      }
    }
    else {
      l.below_cutoff_ms = 0;
    }
  }

  if (v * 1000.0f > (float)LOAD_MAX_VOLTAGE_MV) {
    stop_load(LOAD_FAULT_OVERVOLTAGE);
    return;
  }

  // Over current: well above the target, or far above the maximum at once.
  if (i_ma > 1.2f * (float)LOAD_MAX_CURRENT_MA) {
    stop_load(LOAD_FAULT_OVERCURRENT);
    return;
  }
  if (i_ma > l.target_ma + (float)LOAD_OC_MARGIN_MA) {
    l.oc_ms += dt;
    if (l.oc_ms >= LOAD_OC_TIME_MS) {
      stop_load(LOAD_FAULT_OVERCURRENT);
      return;
    }
  }
  else {
    l.oc_ms = 0;
  }

  // Over power (the limit above should never let this happen).
  if (i_ma * v > 1.15f * (float)LOAD_MAX_POWER_MW) {
    l.op_ms += dt;
    if (l.op_ms >= LOAD_OP_TIME_MS) {
      stop_load(LOAD_FAULT_OVERPOWER);
      return;
    }
  }
  else {
    l.op_ms = 0;
  }
}

/* ---------------------------------------------------------------------- */
/*  Fan                                                                    */
/* ---------------------------------------------------------------------- */

static void fan_control(uint32_t now) {
  uint32_t duty;

  if (l.on) {
    float p = measure_voltage_v() * measure_current_a() * 1000.0f;   // mW

    if (p < 0.0f)
      p = 0.0f;
    duty = FAN_MIN_PERCENT +
           (uint32_t)((float)(100U - FAN_MIN_PERCENT) * p / (float)FAN_FULL_POWER_MW);
    if (duty > 100U)
      duty = 100U;
    if (duty > l.fan_on_duty || now - l.on_since < 1000U)
      l.fan_on_duty = (uint8_t)duty;
    else if (duty < l.fan_on_duty)
      l.fan_on_duty--;                    // Slow down gently.
    duty = l.fan_on_duty;
  }
  else if (l.fan_on_duty != 0U && now - l.off_since < FAN_RUNON_S * 1000UL) {
    duty = l.fan_on_duty;                 // Run-on: cool the heatsink down.
  }
  else {
    l.fan_on_duty = 0;
    duty = FAN_IDLE_PERCENT;
  }

#if TEMP_SOURCE == TEMP_SOURCE_NTC
  {
    // Heatsink temperature: FAN_MIN_PERCENT at 40 degC, 100 % at 70 degC.
    float t = analog_temperature_c();

    if (t > 40.0f) {
      uint32_t dt = FAN_MIN_PERCENT +
                    (uint32_t)((float)(100U - FAN_MIN_PERCENT) * (t - 40.0f) / 30.0f);

      if (dt > 100U)
        dt = 100U;
      if (dt > duty)
        duty = dt;
    }
  }
#endif

  fan_set_percent((uint8_t)duty);

#if FAN_TACH_CHECK
  if (l.on && duty > 0U && fan_rpm() < FAN_MIN_RPM) {
    if (l.fan_low_since == 0U)
      l.fan_low_since = now | 1U;
    else if (now - l.fan_low_since >= FAN_FAIL_MS)
      stop_load(LOAD_FAULT_FAN);
  }
  else {
    l.fan_low_since = 0;
  }
#endif
}

/* ---------------------------------------------------------------------- */
/*  Interface                                                              */
/* ---------------------------------------------------------------------- */

void load_init(void) {
  l.set_ca = 100;                         // 1.00 A
  l.cutoff_cv = 300;                      // 3.00 V
  l.timer_s = 0;
  l.stop = LOAD_STOP_NONE;
  l.off_since = system_ms;
  l.last_ctrl = system_ms;
  output_enable(false);
}

void load_task(uint32_t now, uint8_t flags) {
  if (flags & MEAS_NEW_I)
    control(now);

  if (l.on) {
    if ( ! measure_ok())
      stop_load(LOAD_FAULT_ADC);
    else if (l.timer_s != 0U && l.time_ms >= (uint32_t)l.timer_s * 1000UL)
      stop_load(LOAD_STOP_TIMER);
    else if (analog_temperature_c() >= (float)TEMP_LIMIT_C)
      stop_load(LOAD_FAULT_OVERTEMP);
  }

  // Track the ACS712 zero while nothing can flow.
  measure_set_zeroing( ! l.on && now - l.off_since > ZEROING_DELAY_MS &&
                       l.stop != LOAD_FAULT_CURRENT_WHEN_OFF);

  fan_control(now);
}

void load_key_stop(void) {
  if (l.on)
    stop_load(LOAD_STOP_KEY);
}

load_stop_t load_stop_reason(void) {
  return l.stop;
}

void dev_set_output(bool on) {
  float v, i;

  if ( ! on) {
    if (l.on)
      stop_load(LOAD_STOP_USER);
    return;
  }
  if (l.on)
    return;

  // Refuse to start when something is wrong; the PC sees the load stay off.
  if ( ! measure_ok()) {
    l.stop = LOAD_FAULT_ADC;
    return;
  }
  v = measure_voltage_v() * 1000.0f;
  i = measure_current_a() * 1000.0f;
  if (v > (float)LOAD_MAX_VOLTAGE_MV) {
    l.stop = LOAD_FAULT_OVERVOLTAGE;
    return;
  }
  if (v < (float)LOAD_NO_BATTERY_MV) {
    l.stop = LOAD_STOP_NO_BATTERY;
    return;
  }
  // Right after "off" the filtered reading still decays: only judge it
  // after the same settle time as the check in control().
  if (system_ms - l.off_since > OFF_CURRENT_DELAY && i > (float)LOAD_OFF_CURRENT_MA) {
    l.stop = LOAD_FAULT_CURRENT_WHEN_OFF;
    return;
  }
  if (analog_temperature_c() >= (float)TEMP_LIMIT_C - 5.0f) {
    l.stop = LOAD_FAULT_OVERTEMP;         // Let it cool down a bit first.
    return;
  }

  measure_set_zeroing(false);
  l.on = true;
  l.stop = LOAD_STOP_NONE;
  l.on_since = system_ms;
  l.last_ctrl = system_ms;
  l.target_ma = 0.0f;
  l.below_cutoff_ms = l.oc_ms = l.op_ms = 0;
  l.fan_low_since = 0;
  output_enable(true);
}

void dev_set_current_ca(uint16_t amps_x100) { l.set_ca = amps_x100; }
void dev_set_cutoff_cv(uint16_t volts_x100) { l.cutoff_cv = volts_x100; }
void dev_set_timer_s(uint16_t seconds)      { l.timer_s = seconds; }

void dev_reset_counters(void) {
  l.cap_mah = 0.0;
  l.energy_mwh = 0.0;
  l.time_ms = 0;
}

uint16_t dev_current_setpoint_ca(void) { return l.set_ca; }
uint16_t dev_cutoff_setpoint_cv(void)  { return l.cutoff_cv; }
uint16_t dev_timer_setpoint_s(void)    { return l.timer_s; }

bool dev_output_on(void) { return l.on; }

uint32_t dev_voltage_mv(void) {
  float v = measure_voltage_slow_v() * 1000.0f;

  return measure_ok() && v > 0.0f ? (uint32_t)(v + 0.5f) : 0U;
}

uint32_t dev_current_ma(void) {
  float i = measure_current_slow_a() * 1000.0f;

  if ( ! measure_ok() || i < 0.0f || ( ! l.on && i < DISPLAY_ZERO_MA))
    return 0U;
  return (uint32_t)(i + 0.5f);
}

uint32_t dev_capacity_mah(void) { return (uint32_t)l.cap_mah; }
uint32_t dev_energy_mwh(void)   { return (uint32_t)l.energy_mwh; }
uint32_t dev_test_time_s(void)  { return l.time_ms / 1000U; }

int32_t dev_temperature_c(void) {
  return (int32_t)lroundf(analog_temperature_c());
}
