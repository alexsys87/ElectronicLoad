/** \file
  \brief Battery emulator for the WeAct BlackPill STM32F401.

  The board shows up as a USB virtual COM port and answers the PX-100
  electronic load protocol. Behind it runs a model of a battery being
  discharged at constant current down to the cutoff voltage (and never
  below the safe limit, see config.h). The PC software in ../WpfApp
  connects to the COM port, sets current / cutoff / timer, switches the
  load on and draws the whole discharge.

  LED (PC13):
    on                  load on, discharging
    1 Hz blink          load switched itself off (cutoff, safe limit,
                        timer, over temperature)
    short flash / 2 s   idle
    fast blink          25 MHz crystal failed, USB won't work reliably

  KEY (PA0): insert a fresh, fully charged battery (load off, counters 0).
*/

#include "system.h"
#include "board.h"
#include "usb_cdc.h"
#include "px100.h"
#include "battery_emu.h"

static void usb_write(const uint8_t *data, uint16_t len) {
  usb_cdc_write(data, len);
}

static void led_update(uint32_t now) {
  bool on;

  if ( ! system_hse_ok()) {
    on = (now / 100U) & 1U;
  }
  else if (emu_output_on()) {
    on = true;
  }
  else {
    switch (emu_stop_reason()) {
      case EMU_STOP_CUTOFF:
      case EMU_STOP_SAFE_LIMIT:
      case EMU_STOP_TIMER:
      case EMU_STOP_OVERTEMP:
        on = (now / 500U) & 1U;
        break;
      default:
        on = (now % 2000U) < 50U;
        break;
    }
  }
  board_led(on);
}

int main(void) {
  uint32_t last_step, last_ms;

  system_init();
  board_init();
  emu_init();
  px100_init(usb_write);
  usb_cdc_init();

  last_step = last_ms = system_ms;

  for (;;) {
    uint32_t now;

    // Requests from the PC.
    while (usb_cdc_rxchars())
      px100_feed(usb_cdc_popchar());

    now = system_ms;

    // Model: fixed steps. After a long stall (debugger) skip ahead
    // instead of racing through the backlog.
    if ((uint32_t)(now - last_step) > 50U * EMU_STEP_MS)
      last_step = now - EMU_STEP_MS;
    while ((uint32_t)(now - last_step) >= EMU_STEP_MS) {
      emu_step();
      last_step += EMU_STEP_MS;
    }

    // Once per millisecond: button and LED.
    if (now != last_ms) {
      last_ms = now;
      board_key_poll();
      if (board_key_pressed())
        emu_new_battery();
      led_update(now);
    }

    // Sleep until the next SysTick or USB interrupt.
    __WFI();
  }
}
