/** \file
  \brief Build time configuration of the battery emulator.

  Everything here can also be overridden from the make command line, e.g.
    make CDEFS="-DEMU_TIME_SCALE=120 -DBAT_CAPACITY_MAH=3400"
*/

#ifndef CONFIG_H
#define CONFIG_H

/* ---------------------------------------------------------------------- */
/*  Board: WeAct Studio BlackPill STM32F401CCU6 / STM32F401CEU6           */
/* ---------------------------------------------------------------------- */

/// Crystal on the BlackPill (V1.3 and V3.0): 25 MHz.
#ifndef BOARD_HSE_HZ
  #define BOARD_HSE_HZ          25000000UL
#endif

/// Core clock: 84 MHz from the PLL, USB gets exactly 48 MHz from PLL Q.
#define F_CPU                   84000000UL

/// Interrupt priorities (0 = highest, 15 = lowest).
#define IRQ_PRIO_USB            6
#define IRQ_PRIO_SYSTICK        10

/* ---------------------------------------------------------------------- */
/*  USB                                                                    */
/* ---------------------------------------------------------------------- */

/// ST's VID/PID of the virtual COM port. Fine for a bench tool, needs a
/// PID of your own for anything that leaves the lab.
#ifndef USB_VID
  #define USB_VID               0x0483
#endif
#ifndef USB_PID
  #define USB_PID               0x5740
#endif
#ifndef USB_MANUFACTURER
  #define USB_MANUFACTURER      "ElectronicLoad"
#endif
#ifndef USB_PRODUCT
  #define USB_PRODUCT           "BlackPill Battery Emulator (PX-100 protocol)"
#endif

/* ---------------------------------------------------------------------- */
/*  Emulation                                                              */
/* ---------------------------------------------------------------------- */

/// Simulated seconds per real second. 1 = real time. With 60 a 2500 mAh
/// cell discharged at 1 A takes about 2.5 minutes instead of 2.5 hours.
/// The device timer, test time, mAh and mWh all run in simulated time.
#ifndef EMU_TIME_SCALE
  #define EMU_TIME_SCALE        60
#endif

/// Model step in real milliseconds.
#define EMU_STEP_MS             10

/// Cell chemistry: EMU_CHEM_LIION (NMC/NCA, 4.2 V) or EMU_CHEM_LIFEPO4.
#define EMU_CHEM_LIION          1
#define EMU_CHEM_LIFEPO4        2
#ifndef EMU_CHEMISTRY
  #define EMU_CHEMISTRY         EMU_CHEM_LIION
#endif

/// Cells in series (1 = single 18650).
#ifndef BAT_CELLS_SERIES
  #define BAT_CELLS_SERIES      1
#endif

/// Rated capacity of the emulated battery.
#ifndef BAT_CAPACITY_MAH
  #define BAT_CAPACITY_MAH      2500
#endif

/// Ohmic internal resistance per cell.
#ifndef BAT_R0_MOHM
  #define BAT_R0_MOHM           60
#endif

/// Polarization (RC branch) per cell: resistance and time constant. This
/// makes the voltage sag slowly after the load switches on and recover
/// slowly after it switches off, like a real cell.
#ifndef BAT_RP_MOHM
  #define BAT_RP_MOHM           40
#endif
#ifndef BAT_TAU_S
  #define BAT_TAU_S             40
#endif

/// Hard safety floor per cell. The load always switches off here, even if
/// the cutoff voltage from the PC is lower (or 0). The emulated cell also
/// never goes deeper than this.
#ifndef BAT_SAFE_MV
  #if EMU_CHEMISTRY == EMU_CHEM_LIFEPO4
    #define BAT_SAFE_MV         2000
  #else
    #define BAT_SAFE_MV         2500
  #endif
#endif

/// Load limits (the PX-100 handles 10 A).
#ifndef LOAD_MAX_CURRENT_MA
  #define LOAD_MAX_CURRENT_MA   10000
#endif

/// Heatsink model: ambient, thermal resistance, time constant and the
/// over temperature protection.
#ifndef LOAD_AMBIENT_C
  #define LOAD_AMBIENT_C        25
#endif
#ifndef LOAD_RTH_C_PER_W_X10
  #define LOAD_RTH_C_PER_W_X10  15      // 1.5 degC/W
#endif
#ifndef LOAD_THERMAL_TAU_S
  #define LOAD_THERMAL_TAU_S    180
#endif
#ifndef LOAD_OVERTEMP_C
  #define LOAD_OVERTEMP_C       80
#endif

/// Measurement noise (peak) added to the reported voltage and current.
#ifndef EMU_NOISE_MV
  #define EMU_NOISE_MV          2
#endif
#ifndef EMU_NOISE_MA
  #define EMU_NOISE_MA          3
#endif

/// Setpoints after power up (PX-100 units: A*100, V*100, seconds).
#define EMU_DEFAULT_CURRENT_CA  100     // 1.00 A
#define EMU_DEFAULT_CUTOFF_CV   300     // 3.00 V
#define EMU_DEFAULT_TIMER_S     0       // off

#endif /* CONFIG_H */
