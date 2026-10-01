/** \file
  \brief Current setpoint output: PWM on PA6 and the LOAD_OFF clamp on PB12.

  PA6 = TIM3_CH1 (AF2), 84 MHz / 4096 = 20.5 kHz, 12 bit. The RC filter
  turns it into 0 .. 3.3 V, the divider into 0 .. 0.54 V, which the op-amp
  makes the shunt voltage: I = Vref / Rshunt.

  Two independent ways to switch the load off:
    - PWM duty 0;
    - LOAD_OFF (PB12) high: a small N-MOSFET pulls the op-amp reference
      to GND. An external pull-up holds it high while the MCU is in reset
      or not yet configured, so the load is off at power-up regardless of
      what PA6 does. (Also add a pull-down on PA6.)
*/

#include "output.h"
#include "system.h"
#include "load_config.h"

static bool enabled;

float output_fullscale_ma(void) {
  float vref_mv = (float)ISET_VPWM_MV * (float)ISET_DIV_RBOT_OHM /
                  (float)(ISET_DIV_RTOP_OHM + ISET_DIV_RBOT_OHM);

  return vref_mv / ((float)RSHUNT_UOHM / 1000000.0f);   // mV / Ohm = mA
}

void output_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
  (void)RCC->APB1ENR;

  // PB12 LOAD_OFF: push-pull output, high = clamp.
  GPIOB->BSRR = GPIO_BSRR_BS12;
  GPIOB->MODER = (GPIOB->MODER & ~GPIO_MODER_MODER12) | GPIO_MODER_MODER12_0;

  // TIM3 CH1 PWM mode 1, duty 0.
  TIM3->PSC = 0;
  TIM3->ARR = ISET_PWM_ARR;
  TIM3->CCR1 = 0;
  TIM3->CCMR1 = (6UL << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
  TIM3->CCER = TIM_CCER_CC1E;
  TIM3->EGR = TIM_EGR_UG;
  TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

  // PA6: AF2, push-pull.
  GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFUL << 24)) | (2UL << 24);
  GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER6) | GPIO_MODER_MODER6_1;

  enabled = false;
}

void output_set_ma(float ma) {
  float fs = output_fullscale_ma();
  uint32_t ccr;

  if ( ! enabled || ma <= 0.0f) {
    TIM3->CCR1 = 0;
    return;
  }
  if (ma >= fs)
    ccr = ISET_PWM_ARR + 1U;              // 100 %
  else
    ccr = (uint32_t)(ma / fs * (float)(ISET_PWM_ARR + 1U) + 0.5f);
  TIM3->CCR1 = ccr;
}

void output_enable(bool on) {
  enabled = on;
  if (on) {
    GPIOB->BSRR = GPIO_BSRR_BR12;         // Release the clamp.
  }
  else {
    TIM3->CCR1 = 0;
    GPIOB->BSRR = GPIO_BSRR_BS12;
  }
}
