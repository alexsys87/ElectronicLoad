/** \file
  \brief Configuration of the real electronic load (60 W, constant current).

  All values describe the hardware in docs/hardware.md. Change them here when
  your parts differ; calibrate with the *_GAIN_PPM trims after building.
  Any value can also be overridden from the command line / IAR defines.

  Signal chain:
    current setpoint  PWM (PA6, TIM3) -> RC filter -> divider -> op-amp + MOSFET
                      + shunt (analog constant current loop)
    voltage           battery Kelvin sense + / - -> dividers -> ADS1115
                      AIN0 - AIN3 (differential)
    current           ACS712 -> divider -> ADS1115 AIN1, ACS712 supply -> AIN2
                      (or the shunt voltage on AIN3, CURRENT_SENSOR_SHUNT)
    temperature       STM32 internal sensor (or NTC on PB0)
    fan               4-pin PC CPU cooler: PWM PA8 (TIM1), tach PA1
*/

#ifndef LOAD_CONFIG_H
#define LOAD_CONFIG_H

/* ---------------------------------------------------------------------- */
/*  Limits                                                                 */
/* ---------------------------------------------------------------------- */

/// Highest current the load regulates to (setpoints above are limited).
#ifndef LOAD_MAX_CURRENT_MA
  #define LOAD_MAX_CURRENT_MA       10000
#endif

/// Highest power. The current is reduced to P / V while the battery voltage
/// is high (constant power limit), so the heatsink never sees more.
#ifndef LOAD_MAX_POWER_MW
  #define LOAD_MAX_POWER_MW         60000
#endif

/// Input over voltage: the load refuses to start / switches off above this.
#ifndef LOAD_MAX_VOLTAGE_MV
  #define LOAD_MAX_VOLTAGE_MV       30000
#endif

/// Lowest cutoff: a cutoff setpoint below this (or 0) is raised to it.
#ifndef LOAD_MIN_CUTOFF_MV
  #define LOAD_MIN_CUTOFF_MV        500
#endif

/// Below this voltage at "on" there is no battery: the load stays off.
#ifndef LOAD_NO_BATTERY_MV
  #define LOAD_NO_BATTERY_MV        300
#endif

/* ---------------------------------------------------------------------- */
/*  Current setpoint: PWM -> RC filter -> divider -> op-amp (+) input      */
/* ---------------------------------------------------------------------- */

/// PWM period in timer counts (TIM3 at 84 MHz: 4096 counts = 20.5 kHz, 12 bit).
#define ISET_PWM_ARR                4095U

/// PWM high level (the MCU supply).
#ifndef ISET_VPWM_MV
  #define ISET_VPWM_MV              3300
#endif

/// Divider after the RC filter: Rtop from the filter, Rbot to GND.
#ifndef ISET_DIV_RTOP_OHM
  #define ISET_DIV_RTOP_OHM         5100
#endif
#ifndef ISET_DIV_RBOT_OHM
  #define ISET_DIV_RBOT_OHM         1000
#endif

/// Shunt resistance as the op-amp sees it, in micro ohm. Two MOSFET
/// channels with 0.1 Ohm each from the same reference behave like 0.05 Ohm.
#ifndef RSHUNT_UOHM
  #define RSHUNT_UOHM               50000
#endif

/* ---------------------------------------------------------------------- */
/*  Voltage measurement: ADS1115 AIN0                                      */
/* ---------------------------------------------------------------------- */

/// I2C address of the ADS1115 (ADDR pin to GND).
#define ADS1115_ADDR                0x48

/// Divider from the battery sense wires: Rtop to the battery +, Rbot to GND.
/// 100k / 10k: 30 V -> 2.73 V, range up to 45 V at the +-4.096 V scale.
#ifndef VSENSE_RTOP_OHM
  #define VSENSE_RTOP_OHM           100000
#endif
#ifndef VSENSE_RBOT_OHM
  #define VSENSE_RBOT_OHM           10000
#endif

/// 1: Kelvin sense on both battery terminals, the minus sense wire goes
/// through an identical divider to AIN3 and the ADS1115 measures AIN0 - AIN3.
/// The voltage drop on the minus power wire then doesn't count. Only with
/// the ACS712 (with CURRENT_SENSOR_SHUNT, AIN3 is the shunt input).
#ifndef VSENSE_DIFFERENTIAL
  #define VSENSE_DIFFERENTIAL       1
#endif

/// Calibration: measured / shown * 1000000 (1000000 = no correction).
#ifndef VSENSE_GAIN_PPM
  #define VSENSE_GAIN_PPM           1000000
#endif

/* ---------------------------------------------------------------------- */
/*  Current measurement                                                    */
/* ---------------------------------------------------------------------- */

#define CURRENT_SENSOR_ACS712       1   ///< ACS712 Hall sensor, AIN1 + AIN2.
#define CURRENT_SENSOR_SHUNT        2   ///< Shunt voltage on AIN3 (more accurate).

#ifndef CURRENT_SENSOR
  #define CURRENT_SENSOR            CURRENT_SENSOR_ACS712
#endif

/// ACS712 sensitivity at 5 V supply: 185 (5 A), 100 (20 A), 66 (30 A) mV/A.
#ifndef ACS712_MV_PER_A
  #define ACS712_MV_PER_A           100
#endif

/// 1 if the sensor is wired so that the load current reads negative.
#ifndef ACS712_INVERT
  #define ACS712_INVERT             0
#endif

/// Divider between the ACS712 (output and its 5 V supply) and the ADS1115,
/// which runs from 3.3 V: 10k / 15k -> 5 V reads 3.0 V. Only used for the
/// first readings; the formula itself is ratiometric and needs no ratio.
#ifndef ACS712_DIV_RTOP_OHM
  #define ACS712_DIV_RTOP_OHM       10000
#endif
#ifndef ACS712_DIV_RBOT_OHM
  #define ACS712_DIV_RBOT_OHM       15000
#endif

/// Automatic zero of the ACS712 while the load is off. Offsets larger than
/// this are not taken as zero but as real current (shorted MOSFET).
#ifndef ACS712_ZERO_MAX_MA
  #define ACS712_ZERO_MAX_MA        400
#endif

/// Calibration of the current reading (1000000 = no correction).
#ifndef ISENSE_GAIN_PPM
  #define ISENSE_GAIN_PPM           1000000
#endif

/* ---------------------------------------------------------------------- */
/*  Control                                                                */
/* ---------------------------------------------------------------------- */

/// Setpoint ramp after "on" and on setpoint changes (soft start).
#ifndef LOAD_RAMP_MA_PER_S
  #define LOAD_RAMP_MA_PER_S        20000
#endif

/// Slow digital correction on top of the analog loop: removes op-amp
/// offset, divider and shunt tolerances. Integral gain in 1/s, and the
/// largest correction in mA.
#ifndef LOAD_TRIM_KI
  #define LOAD_TRIM_KI              2.0f
#endif
#ifndef LOAD_TRIM_MAX_MA
  #define LOAD_TRIM_MAX_MA          800
#endif

/* ---------------------------------------------------------------------- */
/*  Protections                                                            */
/* ---------------------------------------------------------------------- */

/// Voltage must stay at/below the cutoff this long before the load stops.
#ifndef LOAD_CUTOFF_DEBOUNCE_MS
  #define LOAD_CUTOFF_DEBOUNCE_MS   300
#endif

/// Over current: more than target + margin for this long (loop failure).
#ifndef LOAD_OC_MARGIN_MA
  #define LOAD_OC_MARGIN_MA         700
#endif
#ifndef LOAD_OC_TIME_MS
  #define LOAD_OC_TIME_MS           300
#endif

/// Current flowing although the load is off (shorted MOSFET).
#ifndef LOAD_OFF_CURRENT_MA
  #define LOAD_OFF_CURRENT_MA       500
#endif

/// Over power, for this long (the power limit should prevent it).
#ifndef LOAD_OP_TIME_MS
  #define LOAD_OP_TIME_MS           500
#endif

/* ---------------------------------------------------------------------- */
/*  Temperature                                                            */
/* ---------------------------------------------------------------------- */

#define TEMP_SOURCE_CHIP            1   ///< STM32 internal sensor (default).
#define TEMP_SOURCE_NTC             2   ///< 10k NTC on the heatsink, PB0.

#ifndef TEMP_SOURCE
  #define TEMP_SOURCE               TEMP_SOURCE_CHIP
#endif

/// Over temperature: load off above this.
#ifndef TEMP_LIMIT_C
  #if TEMP_SOURCE == TEMP_SOURCE_NTC
    #define TEMP_LIMIT_C            85    // heatsink
  #else
    #define TEMP_LIMIT_C            80    // chip
  #endif
#endif

/// NTC: R25, beta, and the pull-up from 3.3 V (NTC from PB0 to GND).
#ifndef NTC_R25_OHM
  #define NTC_R25_OHM               10000
#endif
#ifndef NTC_BETA
  #define NTC_BETA                  3950
#endif
#ifndef NTC_PULLUP_OHM
  #define NTC_PULLUP_OHM            10000
#endif

/* ---------------------------------------------------------------------- */
/*  Fan (4-pin PC CPU cooler, 12 V)                                        */
/* ---------------------------------------------------------------------- */

/// Duty while the load is on, from FAN_MIN_PERCENT at no power up to 100 %
/// at FAN_FULL_POWER_MW. With the NTC the temperature can raise it further.
#ifndef FAN_MIN_PERCENT
  #define FAN_MIN_PERCENT           35
#endif
#ifndef FAN_FULL_POWER_MW
  #define FAN_FULL_POWER_MW         40000
#endif

/// Duty while idle, and how long the fan keeps running after the load stops.
#ifndef FAN_IDLE_PERCENT
  #define FAN_IDLE_PERCENT          0
#endif
#ifndef FAN_RUNON_S
  #define FAN_RUNON_S               60
#endif

/// Fan check: switch the load off when the fan reports less than
/// FAN_MIN_RPM for FAN_FAIL_MS while it should run. 0 for a 3-pin fan
/// without a usable tach signal.
#ifndef FAN_TACH_CHECK
  #define FAN_TACH_CHECK            1
#endif
#ifndef FAN_PULSES_PER_REV
  #define FAN_PULSES_PER_REV        2
#endif
#ifndef FAN_MIN_RPM
  #define FAN_MIN_RPM               300
#endif
#ifndef FAN_FAIL_MS
  #define FAN_FAIL_MS               4000
#endif

#endif /* LOAD_CONFIG_H */
