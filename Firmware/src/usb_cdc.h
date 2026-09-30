/** \file
  \brief USB CDC ACM virtual COM port on the STM32F401 OTG_FS core.
*/

#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

/// Bring up the core and connect to the host (pull-up on DP).
void usb_cdc_init(void);

/// Received characters waiting in the buffer.
uint16_t usb_cdc_rxchars(void);

/// Next received character, 0 if there is none.
uint8_t usb_cdc_popchar(void);

/// Queue one character for sending. Waits (up to USB_TX_TIMEOUT_MS) while
/// the buffer is full and a host has the port open, drops it otherwise.
/// Call from thread mode only.
void usb_cdc_writechar(uint8_t data);

/// Queue a block of characters, see usb_cdc_writechar().
void usb_cdc_write(const uint8_t *data, uint16_t len);

/// Wait until everything queued is sent (up to 100 ms).
void usb_cdc_flush(void);

/// Poll the driver by hand, for use with interrupts disabled. Returns the
/// next received character or -1.
int16_t usb_cdc_rx_poll(void);

/// Configured, not suspended and DTR set (a terminal has the port open).
uint8_t usb_cdc_connected(void);

#endif /* USB_CDC_H */
