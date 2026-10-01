/** \file
  \brief Current setpoint output: PWM on PA6 and the LOAD_OFF clamp on PB12.
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdbool.h>

/// PWM 0, clamp active. Call first thing after the clocks.
void output_init(void);

/// Current setpoint for the analog loop, clamped to 0 .. full scale.
void output_set_ma(float ma);

/// false: PWM 0 and the clamp pulls the op-amp input to GND.
void output_enable(bool on);

/// Current at 100 % PWM, from the divider and shunt in load_config.h.
float output_fullscale_ma(void);

#endif /* OUTPUT_H */
