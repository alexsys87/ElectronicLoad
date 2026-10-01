/** \file
  \brief Electronic load (up to 60 W) on the WeAct BlackPill STM32F401.

  Same USB CDC port and PX-100 protocol as the battery emulator, so the
  PC software in ../WpfApp works unchanged. Hardware: docs/hardware.md,
  settings: load_config.h.

  LED (PC13):
    on                  load on
    1 Hz blink          stopped by the cutoff voltage or the timer
    5 Hz blink          fault: ADC (also at idle), over voltage / current /
                        power / temperature, fan, current while off
    short flash / 2 s   idle
    fast blink          25 MHz crystal failed

  KEY (PA0): stops the load immediately.

  Independent watchdog: if the main loop hangs for about 0.5 s the MCU
  resets, and the hardware pull-ups / pull-downs hold the load off.
*/

#include "system.h"
#include "board.h"
#include "usb_cdc.h"
#include "px100.h"
#include "load.h"
#include "measure.h"
#include "output.h"
#include "analog.h"
#include "fan.h"
#include "i2c.h"

#define ANALOG_PERIOD_MS    100U

static void usb_write(const uint8_t *data, uint16_t len) {
  usb_cdc_write(data, len);
}

/// IWDG: LSI (~32 kHz) / 64, reload 250 -> about 0.5 s.
static void watchdog_init(void) {
  DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;   // Not while halted in the debugger.
  IWDG->KR = 0x5555U;
  IWDG->PR = 4U;
  IWDG->RLR = 250U;
  IWDG->KR = 0xAAAAU;
  IWDG->KR = 0xCCCCU;
}

static void watchdog_kick(void) {
  IWDG->KR = 0xAAAAU;
}

static void led_update(uint32_t now) {
  load_stop_t r = load_stop_reason();
  bool on;

  if ( ! system_hse_ok())
    on = (now / 50U) & 1U;
  else if (dev_output_on())
    on = true;
  else if (load_is_fault(r) || ( ! measure_ok() && now > 1000U))
    on = (now / 100U) & 1U;               // Also when the ADC is missing at idle.
  else if (r == LOAD_STOP_CUTOFF || r == LOAD_STOP_TIMER)
    on = (now / 500U) & 1U;
  else
    on = (now % 2000U) < 50U;
  board_led(on);
}

int main(void) {
  uint32_t last_ms, last_analog;

  system_init();
  output_init();                          // Load off before anything else.
  board_init();
  fan_init();
  analog_init();
  i2c_init();
  measure_init();
  load_init();
  px100_init(usb_write);
  usb_cdc_init();
  watchdog_init();

  analog_task();
  last_ms = last_analog = system_ms;

  for (;;) {
    uint32_t now;
    uint8_t flags;

    // Requests from the PC.
    while (usb_cdc_rxchars())
      px100_feed(usb_cdc_popchar());

    now = system_ms;
    flags = measure_poll(now);
    load_task(now, flags);

    if (now - last_analog >= ANALOG_PERIOD_MS) {
      last_analog = now;
      analog_task();
    }

    if (now != last_ms) {
      last_ms = now;
      fan_task(now);
      board_key_poll();
      if (board_key_pressed())
        load_key_stop();
      led_update(now);
    }

    watchdog_kick();
    __WFI();
  }
}
