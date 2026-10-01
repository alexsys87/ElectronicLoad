/** \file
  \brief 4-pin PC fan: 25 kHz PWM on PA8, tach on PA1.
*/

#ifndef FAN_H
#define FAN_H

#include <stdint.h>

void fan_init(void);

void fan_set_percent(uint8_t percent);
uint8_t fan_percent(void);

/// Updates the speed measurement, call often.
void fan_task(uint32_t now_ms);

/// Revolutions per minute, updated once a second.
uint32_t fan_rpm(void);

#endif /* FAN_H */
