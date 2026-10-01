/** \file
  \brief ADS1115 16 bit ADC on I2C, single-shot conversions.
*/

#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>
#include <stdbool.h>

/* Input multiplexer: differential AIN0 - AIN3, or single ended against GND. */
#define ADS_MUX_AIN0_AIN3 0x1000U
#define ADS_MUX_AIN0      0x4000U
#define ADS_MUX_AIN1      0x5000U
#define ADS_MUX_AIN2      0x6000U
#define ADS_MUX_AIN3      0x7000U

/* Full scale ranges. */
#define ADS_PGA_4096      0x0200U         ///< +-4.096 V, 125 uV / LSB
#define ADS_PGA_2048      0x0400U         ///< +-2.048 V
#define ADS_PGA_512       0x0800U         ///< +-0.512 V, 15.6 uV / LSB

/// Full scale in microvolt for a PGA setting.
uint32_t ads_fullscale_uv(uint16_t pga);

/// true if an ADS1115 answers.
bool ads_probe(void);

/// Starts one conversion (475 SPS, done after about 2.1 ms).
bool ads_start(uint16_t mux, uint16_t pga);

/// Reads the last conversion result.
bool ads_read(int16_t *raw);

#endif /* ADS1115_H */
