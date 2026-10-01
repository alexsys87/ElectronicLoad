/** \file
  \brief What the PX-100 protocol (px100.c) needs from a load.

  Implemented twice:
    src/battery_emu.c   emulated battery + load (battery_emu firmware)
    load/load.c         the real electronic load (electronic_load firmware)

  Units are the protocol's: current setpoint A * 100, cutoff V * 100,
  timer in seconds, readings in mV, mA, mAh, mWh, degC and seconds.
*/

#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <stdbool.h>

/* Control. */
void dev_set_output(bool on);
void dev_set_current_ca(uint16_t amps_x100);
void dev_set_cutoff_cv(uint16_t volts_x100);
void dev_set_timer_s(uint16_t seconds);
void dev_reset_counters(void);

/* Setpoints as stored. */
uint16_t dev_current_setpoint_ca(void);
uint16_t dev_cutoff_setpoint_cv(void);
uint16_t dev_timer_setpoint_s(void);

/* Readings. */
bool     dev_output_on(void);
uint32_t dev_voltage_mv(void);
uint32_t dev_current_ma(void);
uint32_t dev_capacity_mah(void);
uint32_t dev_energy_mwh(void);
int32_t  dev_temperature_c(void);
uint32_t dev_test_time_s(void);

#endif /* DEVICE_H */
