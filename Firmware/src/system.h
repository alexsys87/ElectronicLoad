/** \file
  \brief Clocks, millisecond tick and short delays.
*/

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <stdbool.h>

#include "stm32f4xx.h"
#include "config.h"

/// Milliseconds since reset, from SysTick.
extern volatile uint32_t system_ms;

/// HSE 25 MHz -> PLL -> 84 MHz core, 48 MHz USB. Falls back to the
/// internal 16 MHz oscillator if the crystal doesn't start (USB is then
/// out of spec and may not enumerate).
void system_init(void);

/// true if the crystal runs.
bool system_hse_ok(void);

/// Busy wait, DWT cycle counter based.
void delay_us(uint32_t us);

#endif /* SYSTEM_H */
