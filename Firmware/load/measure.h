/** \file
  \brief Battery voltage and load current from the ADS1115.
*/

#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>
#include <stdbool.h>

#define MEAS_NEW_V    0x01U
#define MEAS_NEW_I    0x02U

void measure_init(void);

/// Runs the conversion sequence, call often. Returns MEAS_NEW_V / MEAS_NEW_I
/// when a new value of that kind was just produced (about 100 per second each).
uint8_t measure_poll(uint32_t now_ms);

/// ADC answers and both values are valid.
bool measure_ok(void);

/// Fast filtered values (control, protections), and slow ones (display).
float measure_voltage_v(void);
float measure_current_a(void);
float measure_voltage_slow_v(void);
float measure_current_slow_a(void);

/// ACS712 only: track the zero point while true (load off, nothing flows).
void measure_set_zeroing(bool on);

#endif /* MEASURE_H */
