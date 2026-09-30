/** \file
  \brief WeAct BlackPill on board LED (PC13) and KEY button (PA0).
*/

#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

void board_init(void);

void board_led(bool on);

/// Debounced KEY state, call board_key_poll() once per millisecond.
void board_key_poll(void);

/// true once per press (falling edge after debouncing).
bool board_key_pressed(void);

#endif /* BOARD_H */
