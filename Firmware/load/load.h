/** \file
  \brief Electronic load controller: implements device.h for the real hardware.
*/

#ifndef LOAD_H
#define LOAD_H

#include <stdint.h>
#include <stdbool.h>

#include "device.h"

/// Why the load is off.
typedef enum {
  LOAD_STOP_NONE = 0,           ///< Running, or never started.
  LOAD_STOP_USER,               ///< Off command from the PC.
  LOAD_STOP_KEY,                ///< KEY button on the board.
  LOAD_STOP_CUTOFF,             ///< Battery reached the cutoff voltage.
  LOAD_STOP_TIMER,              ///< Discharge timer expired.
  LOAD_STOP_NO_BATTERY,         ///< "On" without a battery connected.
  // Faults from here on.
  LOAD_FAULT_ADC,               ///< ADS1115 doesn't answer / ACS712 supply missing.
  LOAD_FAULT_OVERVOLTAGE,
  LOAD_FAULT_OVERCURRENT,
  LOAD_FAULT_OVERPOWER,
  LOAD_FAULT_OVERTEMP,
  LOAD_FAULT_FAN,               ///< Fan doesn't turn.
  LOAD_FAULT_CURRENT_WHEN_OFF   ///< Current flows although the load is off.
} load_stop_t;

void load_init(void);

/// Call every main loop pass with what measure_poll() returned.
void load_task(uint32_t now_ms, uint8_t measure_flags);

/// KEY button: stop the load.
void load_key_stop(void);

load_stop_t load_stop_reason(void);

static inline bool load_is_fault(load_stop_t r) {
  return r >= LOAD_FAULT_ADC;
}

#endif /* LOAD_H */
