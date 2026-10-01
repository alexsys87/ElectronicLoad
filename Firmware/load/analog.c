/** \file
  \brief Internal ADC: chip temperature, VDDA and the optional heatsink NTC.

  ADC1, 12 bit, ADCCLK = PCLK2 / 4 = 21 MHz, 480 cycles sampling (23 us,
  the temperature sensor needs at least 10 us).

    channel 18  temperature sensor (TSVREFE)
    channel 17  VREFINT, gives the real VDDA
    channel 8   PB0, NTC divider (TEMP_SOURCE_NTC)

  Factory calibration (datasheet DS9716, "Temperature sensor calibration
  values"): raw readings at VDDA = 3.3 V, 30 degC and 110 degC, and VREFINT.
  Readings are first scaled to VDDA = 3.3 V with VREFINT.
*/

#include "analog.h"
#include "system.h"
#include "load_config.h"

#include <math.h>

#define TS_CAL1           (*(const volatile uint16_t *)0x1FFF7A2CUL)   // 30 degC
#define TS_CAL2           (*(const volatile uint16_t *)0x1FFF7A2EUL)   // 110 degC
#define VREFINT_CAL       (*(const volatile uint16_t *)0x1FFF7A2AUL)
#define CAL_VDDA          3.3f

#define CH_NTC            8U
#define CH_VREFINT        17U
#define CH_TEMP           18U

#define K_FILTER          0.2f

static float chip_c = 25.0f;
static float ntc_c = 25.0f;
static float vdda = CAL_VDDA;
static bool first = true;

void analog_init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
  (void)RCC->APB2ENR;

#if TEMP_SOURCE == TEMP_SOURCE_NTC
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
  GPIOB->MODER |= GPIO_MODER_MODER0;      // PB0 analog
#endif

  ADC->CCR = ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;   // PCLK2 / 4, sensors on
  ADC1->CR1 = 0;                                   // 12 bit
  ADC1->CR2 = 0;
  // 480 cycles on all channels used here.
  ADC1->SMPR1 = (7UL << ADC_SMPR1_SMP17_Pos) | (7UL << ADC_SMPR1_SMP18_Pos);
  ADC1->SMPR2 = (7UL << ADC_SMPR2_SMP8_Pos);
  ADC1->SQR1 = 0;                                  // one conversion
  ADC1->CR2 = ADC_CR2_ADON;
  delay_us(20);                                    // ADC and sensor start up
}

static uint16_t convert(uint32_t channel) {
  uint32_t start = system_ms;

  ADC1->SQR3 = channel;
  ADC1->SR = 0;
  ADC1->CR2 |= ADC_CR2_SWSTART;
  while ( ! (ADC1->SR & ADC_SR_EOC)) {
    if ((uint32_t)(system_ms - start) > 2U)
      return 0;
  }
  return (uint16_t)ADC1->DR;
}

void analog_task(void) {
  uint16_t ref = convert(CH_VREFINT);
  float scale, t;

  if (ref == 0U)
    return;
  // Factor that turns a reading at the real VDDA into one at 3.3 V.
  scale = (float)VREFINT_CAL / (float)ref;
  vdda = CAL_VDDA * scale;

  t = (float)convert(CH_TEMP) * scale;
  t = 30.0f + (t - (float)TS_CAL1) * (110.0f - 30.0f) /
              (float)((int32_t)TS_CAL2 - (int32_t)TS_CAL1);
  chip_c = first ? t : chip_c + (t - chip_c) * K_FILTER;

#if TEMP_SOURCE == TEMP_SOURCE_NTC
  {
    // NTC from PB0 to GND, pull-up to 3.3 V (ratiometric, VDDA cancels).
    float r = (float)convert(CH_NTC);

    if (r >= 4090.0f)
      t = -40.0f;                         // Open.
    else if (r <= 5.0f)
      t = 150.0f;                         // Shorted.
    else {
      r = (float)NTC_PULLUP_OHM * r / (4095.0f - r);
      t = 1.0f / (1.0f / 298.15f + logf(r / (float)NTC_R25_OHM) / (float)NTC_BETA) - 273.15f;
    }
    ntc_c = first ? t : ntc_c + (t - ntc_c) * K_FILTER;
  }
#endif
  first = false;
}

float analog_chip_temp_c(void) { return chip_c; }
float analog_ntc_temp_c(void)  { return ntc_c; }
float analog_vdda_v(void)      { return vdda; }

float analog_temperature_c(void) {
#if TEMP_SOURCE == TEMP_SOURCE_NTC
  return ntc_c;
#else
  return chip_c;
#endif
}
