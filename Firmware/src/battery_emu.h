/** \file
  \brief Emulated battery plus constant current electronic load.

  Hardware independent (also builds on the PC, see sim/). The load behaves
  like a PX-100: constant current while on, switches off on its own at the
  cutoff voltage, when the timer expires or on over temperature.
*/

#ifndef BATTERY_EMU_H
#define BATTERY_EMU_H

#include <stdint.h>
#include <stdbool.h>

/// Why the load switched off the last time.
typedef enum {
  EMU_STOP_NONE = 0,        ///< Still running, or never started.
  EMU_STOP_USER,            ///< Off command from the PC.
  EMU_STOP_CUTOFF,          ///< Voltage reached the cutoff setpoint.
  EMU_STOP_SAFE_LIMIT,      ///< Voltage reached the hard safety floor.
  EMU_STOP_TIMER,           ///< Discharge timer expired.
  EMU_STOP_OVERTEMP         ///< Heatsink too hot.
} emu_stop_reason_t;

void emu_init(void);

/// Advance the model by one step of EMU_STEP_MS real milliseconds.
void emu_step(void);

/// Simulated seconds per real second (default EMU_TIME_SCALE).
void emu_set_time_scale(uint16_t scale);

/// Replace the cell with a fully charged one: load off, counters cleared.
void emu_new_battery(void);

/* Control, PX-100 semantics. */
void emu_set_output(bool on);
void emu_set_current_ca(uint16_t amps_x100);
void emu_set_cutoff_cv(uint16_t volts_x100);
void emu_set_timer_s(uint16_t seconds);
void emu_reset_counters(void);

/* Setpoints as stored. */
uint16_t emu_current_setpoint_ca(void);
uint16_t emu_cutoff_setpoint_cv(void);
uint16_t emu_timer_setpoint_s(void);

/* Readings, as the load would measure them. */
bool     emu_output_on(void);
uint32_t emu_voltage_mv(void);
uint32_t emu_current_ma(void);
uint32_t emu_capacity_mah(void);
uint32_t emu_energy_mwh(void);
int32_t  emu_temperature_c(void);
uint32_t emu_test_time_s(void);

/* Extra state, for the LED and the PC simulator. */
emu_stop_reason_t emu_stop_reason(void);
float    emu_state_of_charge(void);       ///< 1.0 = full, may go below 0.
float    emu_sim_time_s(void);            ///< Simulated time since power up.

#endif /* BATTERY_EMU_H */
