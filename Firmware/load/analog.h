/** \file
  \brief Internal ADC: chip temperature, VDDA and the optional heatsink NTC.
*/

#ifndef ANALOG_H
#define ANALOG_H

#include <stdint.h>

void analog_init(void);

/// Converts all channels, call every 100 ms (blocks for about 0.1 ms).
void analog_task(void);

float analog_chip_temp_c(void);
float analog_ntc_temp_c(void);      ///< NaN-free: -40 when open, 150 when shorted.
float analog_vdda_v(void);

/// The temperature selected by TEMP_SOURCE.
float analog_temperature_c(void);

#endif /* ANALOG_H */
