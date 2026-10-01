/** \file
  \brief Emulated battery plus constant current electronic load.

  Battery model (Thevenin equivalent circuit, per cell):

      OCV(SoC) --- R0 ---+--- Rp ---+--- terminal
                         |          |
                         +--- Cp ---+        tau = Rp * Cp

    - OCV(SoC): open circuit voltage from a lookup table, linear
      interpolation, SoC from -5 % (over discharged) to 100 %.
    - R0: ohmic drop, appears the moment the current changes.
    - Rp/Cp: polarization, the slow sag under load and the slow recovery
      after the load switches off.

  Load model: constant current while on, limited to LOAD_MAX_CURRENT_MA.
  The heatsink temperature follows the dissipated power with a first order
  lag. Counters (mAh, mWh, test time) run only while the load is on.

  Time runs EMU_TIME_SCALE times faster than real time. All model math is
  done in double: the counters add tiny steps to large sums for hours, and
  float would lose them. At 100 steps per second that's no load for the CPU.
*/

#include "battery_emu.h"
#include "config.h"

#include <math.h>

/* ---------------------------------------------------------------------- */
/*  Open circuit voltage tables, per cell, mV                              */
/* ---------------------------------------------------------------------- */

#define OCV_SOC_MIN     (-0.05)         // first table entry
#define OCV_SOC_STEP    (0.05)          // table spacing
#define OCV_POINTS      22              // -5 % .. 100 %

#if EMU_CHEMISTRY == EMU_CHEM_LIFEPO4
static const uint16_t ocv_table_mv[OCV_POINTS] = {
  1800, 2500, 3000, 3150, 3200, 3220, 3240, 3255, 3265, 3275, 3285,
  3290, 3295, 3300, 3305, 3310, 3320, 3330, 3340, 3350, 3360, 3450
};
#elif EMU_CHEMISTRY == EMU_CHEM_LIION
static const uint16_t ocv_table_mv[OCV_POINTS] = {
  2300, 3000, 3420, 3560, 3630, 3680, 3710, 3735, 3755, 3775, 3795,
  3815, 3840, 3870, 3905, 3945, 3985, 4025, 4065, 4105, 4150, 4200
};
#else
  #error Unknown EMU_CHEMISTRY.
#endif

#define CELLS           ((double)BAT_CELLS_SERIES)
#define R0_OHM          (BAT_R0_MOHM / 1000.0 * CELLS)
#define RP_OHM          (BAT_RP_MOHM / 1000.0 * CELLS)
#define SAFE_V          (BAT_SAFE_MV / 1000.0 * CELLS)
#define RTH_C_PER_W     (LOAD_RTH_C_PER_W_X10 / 10.0)

/* ---------------------------------------------------------------------- */
/*  State                                                                  */
/* ---------------------------------------------------------------------- */

static struct {
  // Setpoints, PX-100 units.
  uint16_t set_ca;              ///< Current, A * 100.
  uint16_t cutoff_cv;           ///< Cutoff voltage, V * 100.
  uint16_t timer_s;             ///< Timer, 0 = off.

  bool on;
  bool depleted;                ///< Last run ended at the cutoff.
  emu_stop_reason_t stop;

  // Battery and load.
  double charge_mah;            ///< Left in the cell, can go below 0.
  double v_rc;                  ///< Polarization voltage.
  double v_term;                ///< Terminal voltage, no noise.
  double i_load;                ///< Load current, no noise.
  double temp_c;                ///< Heatsink temperature.

  // Counters.
  double cap_mah;
  double energy_mwh;
  double test_time_s;
  double sim_time_s;

  // What the load "measures", with noise, updated every step.
  uint32_t meas_mv;
  uint32_t meas_ma;

  // Step size and first order filter factors, see set_time_scale().
  uint16_t scale;
  double dt;
  double k_rc;
  double k_th;

  uint32_t rng;
} s;

/* ---------------------------------------------------------------------- */
/*  Helpers                                                                */
/* ---------------------------------------------------------------------- */

/// Open circuit voltage of the whole battery at the given state of charge.
static double ocv(double soc) {
  double pos = (soc - OCV_SOC_MIN) / OCV_SOC_STEP;
  int i;
  double frac, mv;

  if (pos <= 0.0)
    return ocv_table_mv[0] / 1000.0 * CELLS;
  if (pos >= OCV_POINTS - 1)
    return ocv_table_mv[OCV_POINTS - 1] / 1000.0 * CELLS;

  i = (int)pos;
  frac = pos - i;
  mv = ocv_table_mv[i] + (ocv_table_mv[i + 1] - ocv_table_mv[i]) * frac;
  return mv / 1000.0 * CELLS;
}

/// Uniform noise in -peak .. +peak.
static int32_t noise(int32_t peak) {
  if (peak <= 0)
    return 0;
  s.rng = s.rng * 1664525UL + 1013904223UL;         // Numerical Recipes LCG
  return (int32_t)((s.rng >> 16) % (uint32_t)(2 * peak + 1)) - peak;
}

static void update_measurement(void) {
  int32_t mv = (int32_t)lround(s.v_term * 1000.0) + noise(EMU_NOISE_MV);
  int32_t ma = 0;

  if (s.i_load > 0.0)
    ma = (int32_t)lround(s.i_load * 1000.0) + noise(EMU_NOISE_MA);

  s.meas_mv = (uint32_t)(mv > 0 ? mv : 0);
  s.meas_ma = (uint32_t)(ma > 0 ? ma : 0);
}

static void switch_off(emu_stop_reason_t why) {
  s.on = false;
  s.stop = why;
  s.i_load = 0.0;
  if (why == EMU_STOP_CUTOFF || why == EMU_STOP_SAFE_LIMIT)
    s.depleted = true;
}

/* ---------------------------------------------------------------------- */
/*  Interface                                                              */
/* ---------------------------------------------------------------------- */

void emu_init(void) {
  s.set_ca = EMU_DEFAULT_CURRENT_CA;
  s.cutoff_cv = EMU_DEFAULT_CUTOFF_CV;
  s.timer_s = EMU_DEFAULT_TIMER_S;
  s.temp_c = LOAD_AMBIENT_C;
  s.sim_time_s = 0.0;
  s.rng = 0x1234567UL;
  emu_set_time_scale(EMU_TIME_SCALE);
  emu_new_battery();
}

void emu_set_time_scale(uint16_t scale) {
  if (scale == 0)
    scale = 1;
  s.scale = scale;
  s.dt = EMU_STEP_MS / 1000.0 * scale;
  s.k_rc = 1.0 - exp(-s.dt / BAT_TAU_S);
  s.k_th = 1.0 - exp(-s.dt / LOAD_THERMAL_TAU_S);
}

void emu_new_battery(void) {
  s.on = false;
  s.stop = EMU_STOP_NONE;
  s.depleted = false;
  s.charge_mah = BAT_CAPACITY_MAH;
  s.v_rc = 0.0;
  s.i_load = 0.0;
  s.v_term = ocv(1.0);
  dev_reset_counters();
  update_measurement();
}

void emu_step(void) {
  double dt = s.dt;
  double i = 0.0;
  double v;

  s.sim_time_s += dt;

  if (s.on) {
    uint32_t ma = (uint32_t)s.set_ca * 10U;

    if (ma > LOAD_MAX_CURRENT_MA)
      ma = LOAD_MAX_CURRENT_MA;
    i = ma / 1000.0;
  }
  s.i_load = i;

  // Terminal voltage with this step's current.
  s.v_rc += (i * RP_OHM - s.v_rc) * s.k_rc;
  v = ocv(s.charge_mah / BAT_CAPACITY_MAH) - i * R0_OHM - s.v_rc;
  if (v < 0.0)
    v = 0.0;
  s.v_term = v;

  // Charge taken out and counters.
  if (s.on) {
    double mah = i * 1000.0 * dt / 3600.0;

    s.charge_mah -= mah;
    s.cap_mah += mah;
    s.energy_mwh += v * mah;
    s.test_time_s += dt;
  }

  // Heatsink.
  s.temp_c += (LOAD_AMBIENT_C + v * i * RTH_C_PER_W - s.temp_c) * s.k_th;

  // Protections, in the order a user wants to read them.
  if (s.on) {
    double cutoff = s.cutoff_cv / 100.0;

    if (cutoff >= SAFE_V && v <= cutoff)
      switch_off(EMU_STOP_CUTOFF);
    else if (v <= SAFE_V)
      switch_off(EMU_STOP_SAFE_LIMIT);
    else if (s.timer_s != 0 && s.test_time_s >= s.timer_s)
      switch_off(EMU_STOP_TIMER);
    else if (s.temp_c >= LOAD_OVERTEMP_C)
      switch_off(EMU_STOP_OVERTEMP);
  }

  update_measurement();
}

void dev_set_output(bool on) {
  if (on) {
    if (s.on)
      return;
    // The previous discharge went all the way down: the user wants to
    // test the next cell. Insert a fresh one and start from zero.
    if (s.depleted)
      emu_new_battery();
    s.on = true;
    s.stop = EMU_STOP_NONE;
  }
  else if (s.on) {
    switch_off(EMU_STOP_USER);
    update_measurement();
  }
}

void dev_set_current_ca(uint16_t amps_x100) {
  s.set_ca = amps_x100;
}

void dev_set_cutoff_cv(uint16_t volts_x100) {
  s.cutoff_cv = volts_x100;
}

void dev_set_timer_s(uint16_t seconds) {
  s.timer_s = seconds;
}

void dev_reset_counters(void) {
  s.cap_mah = 0.0;
  s.energy_mwh = 0.0;
  s.test_time_s = 0.0;
}

uint16_t dev_current_setpoint_ca(void) { return s.set_ca; }
uint16_t dev_cutoff_setpoint_cv(void)  { return s.cutoff_cv; }
uint16_t dev_timer_setpoint_s(void)    { return s.timer_s; }

bool     dev_output_on(void)    { return s.on; }
uint32_t dev_voltage_mv(void)   { return s.meas_mv; }
uint32_t dev_current_ma(void)   { return s.meas_ma; }
uint32_t dev_capacity_mah(void) { return (uint32_t)s.cap_mah; }
uint32_t dev_energy_mwh(void)   { return (uint32_t)s.energy_mwh; }
int32_t  dev_temperature_c(void) { return (int32_t)lround(s.temp_c); }
uint32_t dev_test_time_s(void)  { return (uint32_t)s.test_time_s; }

emu_stop_reason_t emu_stop_reason(void) { return s.stop; }
float emu_state_of_charge(void) { return (float)(s.charge_mah / BAT_CAPACITY_MAH); }
float emu_sim_time_s(void)      { return (float)s.sim_time_s; }
