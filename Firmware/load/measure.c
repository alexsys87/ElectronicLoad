/** \file
  \brief Battery voltage and load current from the ADS1115.

  The ADS1115 has one converter and a multiplexer, so the channels are
  converted one after the other, 4 ms each (475 SPS plus margin):

    ACS712: VCC, V, I, V, I, ... and VCC again every 16th slot
    shunt:  V, I, V, I, ...

  V is AIN0 - AIN3 (Kelvin sense on both battery terminals) with the
  ACS712 and VSENSE_DIFFERENTIAL, else AIN0 against GND.

  That gives about 110 voltage and 110 current values per second.

  ACS712 current, ratiometric: the output is VCC/2 at zero current and the
  sensitivity scales with VCC / 5 V. Output and VCC go through the same
  divider, so with a = output and s = supply as read by the ADS1115:

      I = (a - s/2) * 5 V / (S * s)          S in V/A at 5 V

  The divider ratio cancels out; supply drift of the 5 V rail as well.
  The remaining offset (Hall sensor, divider mismatch) is tracked while
  the load is off: measure_set_zeroing().
*/

#include "measure.h"
#include "ads1115.h"
#include "i2c.h"
#include "load_config.h"

#include <math.h>

#define CONVERSION_MS     4U      ///< 2.1 ms conversion, ms tick granularity.
#define ERRORS_TO_FAIL    3U
#define RETRY_MS          100U

#define V_DIV             ((float)(VSENSE_RTOP_OHM + VSENSE_RBOT_OHM) / (float)VSENSE_RBOT_OHM)
#define V_GAIN            ((float)VSENSE_GAIN_PPM / 1000000.0f)
#define I_GAIN            ((float)ISENSE_GAIN_PPM / 1000000.0f)

#define ACS_DIV           ((float)ACS712_DIV_RBOT_OHM / (float)(ACS712_DIV_RTOP_OHM + ACS712_DIV_RBOT_OHM))
#define ACS_VCC_NOMINAL   (5.0f * ACS_DIV)    ///< Expected supply reading.

/* Filters: fast for control and protections, slow for the display. */
#define K_FAST            0.30f
#define K_SLOW            0.05f
#define K_VCC             0.20f
#define K_ZERO            0.01f

typedef enum { SLOT_V, SLOT_I, SLOT_VCC } slot_t;

static struct {
  bool ok;                ///< Last I2C transfers went fine.
  bool have_v, have_i;
  uint8_t errors;
  uint32_t retry_at;

  bool busy;              ///< A conversion is running.
  slot_t slot;
  uint32_t started;
  uint32_t count;

  float vcc;              ///< ACS712 supply as read (after divider), V.
  float v_fast, v_slow;   ///< Battery voltage, V.
  float i_fast, i_slow;   ///< Load current, A.

  bool zeroing;
  float zero;             ///< ACS712 offset, A.
  float zero_track;
} m;

void measure_init(void) {
  m.vcc = ACS_VCC_NOMINAL;
  m.ok = ads_probe();
  if ( ! m.ok) {
    i2c_recover();
    m.ok = ads_probe();
  }
}

bool measure_ok(void) {
  return m.ok && m.have_v && m.have_i;
}

float measure_voltage_v(void)      { return m.v_fast; }
float measure_current_a(void)      { return m.i_fast; }
float measure_voltage_slow_v(void) { return m.v_slow; }
float measure_current_slow_a(void) { return m.i_slow; }

void measure_set_zeroing(bool on) {
  if (on && ! m.zeroing)
    m.zero_track = m.zero;
  m.zeroing = on;
}

static slot_t next_slot(void) {
#if CURRENT_SENSOR == CURRENT_SENSOR_ACS712
  if (m.count % 16U == 0U)
    return SLOT_VCC;
#endif
  return (m.count & 1U) ? SLOT_I : SLOT_V;
}

static float filter(float acc, float x, float k, bool first) {
  return first ? x : acc + (x - acc) * k;
}

static void error(uint32_t now) {
  m.busy = false;
  if (++m.errors >= ERRORS_TO_FAIL) {
    m.ok = false;
    m.have_v = m.have_i = false;
    m.errors = 0;
    m.retry_at = now + RETRY_MS;
    i2c_recover();
  }
}

static uint8_t process(int16_t raw) {
  float fs = (float)ads_fullscale_uv(m.slot == SLOT_I &&
                                     CURRENT_SENSOR == CURRENT_SENSOR_SHUNT
                                     ? ADS_PGA_512 : ADS_PGA_4096);
  float volts = (float)raw * fs / 32768.0f / 1000000.0f;
  float i;

  switch (m.slot) {
    case SLOT_V:
      volts = volts * V_DIV * V_GAIN;
      if (volts < 0.0f)
        volts = 0.0f;
      m.v_fast = filter(m.v_fast, volts, K_FAST, ! m.have_v);
      m.v_slow = filter(m.v_slow, volts, K_SLOW, ! m.have_v);
      m.have_v = true;
      return MEAS_NEW_V;

    case SLOT_VCC:
      m.vcc = filter(m.vcc, volts, K_VCC, false);
      return 0;

    case SLOT_I:
    default:
#if CURRENT_SENSOR == CURRENT_SENSOR_ACS712
      // Without its 5 V supply the sensor output means nothing.
      if (m.vcc < 0.5f * ACS_VCC_NOMINAL) {
        m.have_i = false;
        return 0;
      }
      i = (volts - m.vcc / 2.0f) * 5.0f / ((float)ACS712_MV_PER_A / 1000.0f * m.vcc);
  #if ACS712_INVERT
      i = -i;
  #endif
      i *= I_GAIN;
      if (m.zeroing) {
        m.zero_track += (i - m.zero_track) * K_ZERO;
        if (fabsf(m.zero_track) * 1000.0f <= (float)ACS712_ZERO_MAX_MA)
          m.zero = m.zero_track;
      }
      i -= m.zero;
#else
      i = volts / ((float)RSHUNT_UOHM / 1000000.0f) * I_GAIN;
#endif
      m.i_fast = filter(m.i_fast, i, K_FAST, ! m.have_i);
      m.i_slow = filter(m.i_slow, i, K_SLOW, ! m.have_i);
      m.have_i = true;
      return MEAS_NEW_I;
  }
}

uint8_t measure_poll(uint32_t now) {
  int16_t raw;

  if ( ! m.ok && (int32_t)(now - m.retry_at) < 0)
    return 0;

  if ( ! m.busy) {
    uint16_t mux, pga = ADS_PGA_4096;

    m.slot = next_slot();
    switch (m.slot) {
      case SLOT_V:
#if VSENSE_DIFFERENTIAL && CURRENT_SENSOR == CURRENT_SENSOR_ACS712
        mux = ADS_MUX_AIN0_AIN3;
#else
        mux = ADS_MUX_AIN0;
#endif
        break;
      case SLOT_VCC: mux = ADS_MUX_AIN2; break;
      case SLOT_I:
      default:
#if CURRENT_SENSOR == CURRENT_SENSOR_ACS712
        mux = ADS_MUX_AIN1;
#else
        mux = ADS_MUX_AIN3;
        pga = ADS_PGA_512;
#endif
        break;
    }
    if ( ! ads_start(mux, pga)) {
      error(now);
      return 0;
    }
    m.busy = true;
    m.started = now;
    return 0;
  }

  if ((uint32_t)(now - m.started) < CONVERSION_MS)
    return 0;

  m.busy = false;
  if ( ! ads_read(&raw)) {
    error(now);
    return 0;
  }
  m.ok = true;
  m.errors = 0;
  m.count++;
  return process(raw);
}
