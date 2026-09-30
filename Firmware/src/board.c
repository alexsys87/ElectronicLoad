/** \file
  \brief WeAct BlackPill on board LED (PC13) and KEY button (PA0).

  The LED is wired from 3.3 V through a resistor to PC13: low = on.
  KEY connects PA0 to GND: needs the internal pull-up, low = pressed.
*/

#include "board.h"
#include "system.h"

#define KEY_DEBOUNCE_MS   30

static uint8_t key_stable;              ///< 1 = pressed.
static uint8_t key_count;
static bool key_event;

void board_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
  (void)RCC->AHB1ENR;

  // PC13: output, push-pull, low speed, LED off.
  GPIOC->BSRR = GPIO_BSRR_BS13;
  GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODER13) | GPIO_MODER_MODER13_0;
  GPIOC->OTYPER &= ~GPIO_OTYPER_OT13;

  // PA0: input with pull-up.
  GPIOA->MODER &= ~GPIO_MODER_MODER0;
  GPIOA->PUPDR = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPD0) | GPIO_PUPDR_PUPD0_0;
}

void board_led(bool on) {
  GPIOC->BSRR = on ? GPIO_BSRR_BR13 : GPIO_BSRR_BS13;
}

void board_key_poll(void) {
  uint8_t raw = (GPIOA->IDR & GPIO_IDR_ID0) ? 0 : 1;

  if (raw == key_stable) {
    key_count = 0;
    return;
  }
  if (++key_count >= KEY_DEBOUNCE_MS) {
    key_stable = raw;
    key_count = 0;
    if (raw)
      key_event = true;
  }
}

bool board_key_pressed(void) {
  bool e = key_event;

  key_event = false;
  return e;
}
