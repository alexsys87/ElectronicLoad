/** \file
  \brief I2C1 master, 400 kHz, PB6 = SCL, PB7 = SDA. Blocking, with timeouts.
*/

#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include <stdbool.h>

void i2c_init(void);

/// Writes len bytes. false on NACK, bus error or timeout.
bool i2c_write(uint8_t addr, const uint8_t *data, uint8_t len);

/// Reads 1 or 2 bytes (all the ADS1115 needs).
bool i2c_read(uint8_t addr, uint8_t *data, uint8_t len);

/// Frees a stuck bus (9 clocks + STOP) and re-initializes the peripheral.
void i2c_recover(void);

#endif /* I2C_H */
