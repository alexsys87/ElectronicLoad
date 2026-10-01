/** \file
  \brief I2C1 master, 400 kHz, PB6 = SCL, PB7 = SDA. Blocking, with timeouts.

  The F4 I2C peripheral needs the exact event sequences of RM0368 section
  18.3.3, in particular for reads of one and two bytes, where STOP / ACK
  must be set at the right moment. Those few register accesses run with
  interrupts masked so the USB interrupt can't stretch them.

  Every wait has a timeout: a missing or hung ADS1115 never blocks the main
  loop for more than a few milliseconds. External pull-ups of 4.7 kOhm to
  3.3 V are required (the internal ones are only a fallback).
*/

#include "i2c.h"
#include "system.h"

#define I2C_TIMEOUT_MS    3U

/// APB1 clock in MHz (42 MHz, see system.c).
#define PCLK1_MHZ         (F_CPU / 2U / 1000000U)

static uint32_t irq_lock(void) {
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  return primask;
}

static void irq_unlock(uint32_t primask) {
  __set_PRIMASK(primask);
}

/// Waits for an SR1 flag; false on timeout or an error flag.
static bool wait_sr1(uint32_t flag) {
  uint32_t start = system_ms;

  while ( ! (I2C1->SR1 & flag)) {
    if (I2C1->SR1 & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO))
      return false;
    if ((uint32_t)(system_ms - start) > I2C_TIMEOUT_MS)
      return false;
  }
  return true;
}

/// Waits until a previous STOP is done and the bus is idle.
static bool wait_idle(void) {
  uint32_t start = system_ms;

  while ((I2C1->CR1 & I2C_CR1_STOP) || (I2C1->SR2 & I2C_SR2_BUSY)) {
    if ((uint32_t)(system_ms - start) > I2C_TIMEOUT_MS)
      return false;
  }
  return true;
}

static bool fail(void) {
  I2C1->CR1 |= I2C_CR1_STOP;
  I2C1->CR1 &= ~I2C_CR1_POS;
  I2C1->SR1 = 0;                          // Clear AF, BERR, ARLO.
  return false;
}

static void pins_af(void) {
  // PB6, PB7: AF4, open drain, pull-up, high speed.
  GPIOB->OTYPER |= GPIO_OTYPER_OT6 | GPIO_OTYPER_OT7;
  GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR6 | GPIO_OSPEEDER_OSPEEDR7;
  GPIOB->PUPDR = (GPIOB->PUPDR & ~(GPIO_PUPDR_PUPD6 | GPIO_PUPDR_PUPD7)) |
                 GPIO_PUPDR_PUPD6_0 | GPIO_PUPDR_PUPD7_0;
  GPIOB->AFR[0] = (GPIOB->AFR[0] & ~((0xFUL << 24) | (0xFUL << 28))) |
                  (4UL << 24) | (4UL << 28);
  GPIOB->MODER = (GPIOB->MODER & ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7)) |
                 GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1;
}

static void peripheral_init(void) {
  I2C1->CR1 = I2C_CR1_SWRST;
  I2C1->CR1 = 0;
  I2C1->CR2 = PCLK1_MHZ;
  // Fast mode, duty 2:1: period = 3 * CCR / PCLK1 -> 400 kHz.
  I2C1->CCR = I2C_CCR_FS | (PCLK1_MHZ * 1000U / (3U * 400U));
  // Max rise time 300 ns in fast mode.
  I2C1->TRISE = PCLK1_MHZ * 300U / 1000U + 1U;
  I2C1->CR1 = I2C_CR1_PE;
}

void i2c_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
  RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
  (void)RCC->APB1ENR;

  pins_af();
  peripheral_init();
}

void i2c_recover(void) {
  uint8_t i;

  I2C1->CR1 = 0;

  // SCL / SDA as open drain outputs, released (high).
  GPIOB->BSRR = GPIO_BSRR_BS6 | GPIO_BSRR_BS7;
  GPIOB->MODER = (GPIOB->MODER & ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7)) |
                 GPIO_MODER_MODER6_0 | GPIO_MODER_MODER7_0;

  // Clock out whatever a slave still wants to send.
  for (i = 0; i < 9U; i++) {
    GPIOB->BSRR = GPIO_BSRR_BR6;
    delay_us(5);
    GPIOB->BSRR = GPIO_BSRR_BS6;
    delay_us(5);
  }
  // STOP: SDA low -> high while SCL is high.
  GPIOB->BSRR = GPIO_BSRR_BR7;
  delay_us(5);
  GPIOB->BSRR = GPIO_BSRR_BS7;
  delay_us(5);

  pins_af();
  peripheral_init();
}

bool i2c_write(uint8_t addr, const uint8_t *data, uint8_t len) {
  uint8_t i;

  if ( ! wait_idle())
    return fail();

  I2C1->CR1 |= I2C_CR1_START;
  if ( ! wait_sr1(I2C_SR1_SB))
    return fail();
  I2C1->DR = (uint32_t)addr << 1;
  if ( ! wait_sr1(I2C_SR1_ADDR))
    return fail();
  (void)I2C1->SR2;                        // Clears ADDR.

  for (i = 0; i < len; i++) {
    if ( ! wait_sr1(I2C_SR1_TXE))
      return fail();
    I2C1->DR = data[i];
  }
  if ( ! wait_sr1(I2C_SR1_BTF))
    return fail();
  I2C1->CR1 |= I2C_CR1_STOP;
  return true;
}

bool i2c_read(uint8_t addr, uint8_t *data, uint8_t len) {
  uint32_t primask;

  if (len < 1U || len > 2U)
    return false;
  if ( ! wait_idle())
    return fail();

  // No ACK for the last (or only) byte. For two bytes POS makes the NACK
  // apply to the second one (RM0368, "2-byte reception").
  I2C1->CR1 &= ~I2C_CR1_ACK;
  if (len == 2U)
    I2C1->CR1 |= I2C_CR1_POS;

  I2C1->CR1 |= I2C_CR1_START;
  if ( ! wait_sr1(I2C_SR1_SB))
    return fail();
  I2C1->DR = ((uint32_t)addr << 1) | 1U;
  if ( ! wait_sr1(I2C_SR1_ADDR))
    return fail();

  if (len == 1U) {
    primask = irq_lock();
    (void)I2C1->SR2;                      // Clear ADDR ...
    I2C1->CR1 |= I2C_CR1_STOP;            // ... and STOP right after.
    irq_unlock(primask);
    if ( ! wait_sr1(I2C_SR1_RXNE))
      return fail();
    data[0] = (uint8_t)I2C1->DR;
  }
  else {
    (void)I2C1->SR2;                      // Clear ADDR.
    if ( ! wait_sr1(I2C_SR1_BTF))         // Both bytes received.
      return fail();
    primask = irq_lock();
    I2C1->CR1 |= I2C_CR1_STOP;
    data[0] = (uint8_t)I2C1->DR;
    irq_unlock(primask);
    data[1] = (uint8_t)I2C1->DR;
    I2C1->CR1 &= ~I2C_CR1_POS;
  }
  return true;
}
