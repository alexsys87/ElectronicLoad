/** \file
  \brief ADS1115 16 bit ADC on I2C, single-shot conversions.

  Config register (pointer 0x01):
    15 OS     write 1: start a single conversion
    14:12 MUX input
    11:9 PGA  full scale range
    8 MODE    1 = single-shot
    7:5 DR    110 = 475 samples per second
    1:0 COMP  11 = comparator off
*/

#include "ads1115.h"
#include "i2c.h"
#include "load_config.h"

#define REG_CONVERSION    0x00U
#define REG_CONFIG        0x01U

#define CFG_OS            0x8000U
#define CFG_SINGLE_SHOT   0x0100U
#define CFG_DR_475        0x00C0U
#define CFG_COMP_OFF      0x0003U

uint32_t ads_fullscale_uv(uint16_t pga) {
  switch (pga) {
    case ADS_PGA_2048: return 2048000UL;
    case ADS_PGA_512:  return 512000UL;
    default:           return 4096000UL;
  }
}

static bool read_reg(uint8_t reg, uint16_t *value) {
  uint8_t b[2];

  if ( ! i2c_write(ADS1115_ADDR, &reg, 1))
    return false;
  if ( ! i2c_read(ADS1115_ADDR, b, 2))
    return false;
  *value = (uint16_t)((b[0] << 8) | b[1]);
  return true;
}

bool ads_probe(void) {
  uint16_t cfg;

  // Any answer will do; the comparator bits are 11 unless someone changed them.
  return read_reg(REG_CONFIG, &cfg);
}

bool ads_start(uint16_t mux, uint16_t pga) {
  uint16_t cfg = CFG_OS | mux | pga | CFG_SINGLE_SHOT | CFG_DR_475 | CFG_COMP_OFF;
  uint8_t b[3];

  b[0] = REG_CONFIG;
  b[1] = (uint8_t)(cfg >> 8);
  b[2] = (uint8_t)cfg;
  return i2c_write(ADS1115_ADDR, b, 3);
}

bool ads_read(int16_t *raw) {
  uint16_t v;

  if ( ! read_reg(REG_CONVERSION, &v))
    return false;
  *raw = (int16_t)v;
  return true;
}
