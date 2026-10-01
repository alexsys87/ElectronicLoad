/** \file
  \brief 4-pin PC fan: 25 kHz PWM on PA8, tach on PA1.

  PWM: PA8 = TIM1_CH1 (AF1), 84 MHz / 3360 = 25 kHz (Intel 4-wire fan
  spec). Open drain: the fan pulls its PWM input up itself, the pin only
  pulls it low. PA8 is 5 V tolerant. High = run, so duty = speed.

  Tach: open collector, 2 pulses per revolution, pull-up 10 kOhm to 3.3 V
  (never to 12 V: PA1 is not 5 V tolerant). PA1 = TIM2_CH2 (AF1) clocks
  TIM2 directly (external clock mode 1, input filter against PWM noise),
  so counting needs no interrupt. Pulses per second -> rpm.
*/

#include "fan.h"
#include "system.h"
#include "load_config.h"

#define FAN_ARR           (F_CPU / 25000U - 1U)

static uint8_t percent;
static uint32_t rpm;
static uint32_t last_count;
static uint32_t last_ms;

void fan_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
  (void)RCC->APB1ENR;

  // TIM1 CH1 PWM mode 1, 25 kHz, duty 0 (fan at its minimum or stopped).
  TIM1->PSC = 0;
  TIM1->ARR = FAN_ARR;
  TIM1->CCR1 = 0;
  TIM1->CCMR1 = (6UL << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
  TIM1->CCER = TIM_CCER_CC1E;
  TIM1->BDTR = TIM_BDTR_MOE;              // Advanced timer: main output enable.
  TIM1->EGR = TIM_EGR_UG;
  TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

  // PA8: AF1, open drain.
  GPIOA->OTYPER |= GPIO_OTYPER_OT8;
  GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(0xFUL << 0)) | (1UL << 0);
  GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER8) | GPIO_MODER_MODER8_1;

  // TIM2 counts rising edges on TI2 (PA1): CC2 = input TI2, filter
  // fDTS/32 with N = 8 (fDTS = 21 MHz -> about 12 us of stable level).
  TIM2->CR1 = TIM_CR1_CKD_1;              // fDTS = fCK / 4
  TIM2->PSC = 0;
  TIM2->ARR = 0xFFFFFFFFUL;
  TIM2->CCMR1 = (1UL << TIM_CCMR1_CC2S_Pos) | (0xFUL << TIM_CCMR1_IC2F_Pos);
  TIM2->CCER = 0;                         // Rising edge.
  TIM2->SMCR = (6UL << TIM_SMCR_TS_Pos) | (7UL << TIM_SMCR_SMS_Pos);  // TI2FP2, ext. clock 1
  TIM2->EGR = TIM_EGR_UG;
  TIM2->CR1 |= TIM_CR1_CEN;

  // PA1: AF1, pull-up (an external 10k is still recommended).
  GPIOA->PUPDR = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPD1) | GPIO_PUPDR_PUPD1_0;
  GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFUL << 4)) | (1UL << 4);
  GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER1) | GPIO_MODER_MODER1_1;

  last_count = TIM2->CNT;
  last_ms = system_ms;
}

void fan_set_percent(uint8_t p) {
  if (p > 100U)
    p = 100U;
  percent = p;
  TIM1->CCR1 = (FAN_ARR + 1U) * p / 100U;
}

uint8_t fan_percent(void) {
  return percent;
}

void fan_task(uint32_t now) {
  uint32_t dt = now - last_ms;

  if (dt >= 1000U) {
    uint32_t count = TIM2->CNT;
    uint32_t pulses = count - last_count;

    rpm = pulses * 60000U / (dt * FAN_PULSES_PER_REV);
    last_count = count;
    last_ms = now;
  }
}

uint32_t fan_rpm(void) {
  return rpm;
}
