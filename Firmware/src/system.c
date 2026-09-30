/** \file
  \brief Clocks, millisecond tick and short delays.

  Clock tree (RM0368 section 6):
    HSE 25 MHz / M 25 = 1 MHz * N 336 = 336 MHz VCO
      / P 4 = 84 MHz SYSCLK, AHB 84 MHz, APB1 42 MHz, APB2 84 MHz
      / Q 7 = 48 MHz for USB
  Flash: 2 wait states at 84 MHz and 2.7 .. 3.6 V.
*/

#include "system.h"

volatile uint32_t system_ms;

/// CMSIS expects this name.
uint32_t SystemCoreClock = 16000000UL;

static bool hse_ok;

#define PLL_N   336U
#define PLL_P   4U
#define PLL_Q   7U

#if BOARD_HSE_HZ % 1000000UL
  #error BOARD_HSE_HZ must be a whole number of MHz.
#endif

/// Called from the reset handler, before main().
void SystemInit(void) {
  // Nothing: the reset handler already enabled the FPU, clocks come in
  // system_init() so they run with .data and .bss set up.
}

static bool wait_bit(volatile uint32_t *reg, uint32_t mask, uint32_t value) {
  uint32_t n;

  // No timer yet: count loops. 200000 loops are > 20 ms at 16 MHz.
  for (n = 0; n < 200000UL; n++) {
    if ((*reg & mask) == value)
      return true;
  }
  return false;
}

static void clock_init(void) {
  uint32_t pll_m;

  // Crystal.
  RCC->CR |= RCC_CR_HSEON;
  hse_ok = wait_bit(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY);
  if ( ! hse_ok)
    RCC->CR &= ~RCC_CR_HSEON;

  // Voltage scale 2 is enough for 84 MHz (the reset value, but be sure).
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  (void)RCC->APB1ENR;
  PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;

  FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
               FLASH_ACR_LATENCY_2WS;

  // PLL input 1 MHz.
  pll_m = hse_ok ? (uint32_t)(BOARD_HSE_HZ / 1000000UL) : 16U;
  RCC->PLLCFGR = (pll_m << RCC_PLLCFGR_PLLM_Pos) |
                 (PLL_N << RCC_PLLCFGR_PLLN_Pos) |
                 (((PLL_P / 2U) - 1U) << RCC_PLLCFGR_PLLP_Pos) |
                 (PLL_Q << RCC_PLLCFGR_PLLQ_Pos) |
                 (hse_ok ? RCC_PLLCFGR_PLLSRC_HSE : RCC_PLLCFGR_PLLSRC_HSI);

  RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

  RCC->CR |= RCC_CR_PLLON;
  if ( ! wait_bit(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY))
    return;                               // Stay on HSI 16 MHz.

  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  if (wait_bit(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL))
    SystemCoreClock = F_CPU;
}

void system_init(void) {
  clock_init();

  // Cycle counter for delay_us() and the USB driver timeouts.
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  // Keep the debugger connected while the main loop sleeps in WFI.
  DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP;

  SysTick_Config(SystemCoreClock / 1000U);
  NVIC_SetPriority(SysTick_IRQn, IRQ_PRIO_SYSTICK);
}

bool system_hse_ok(void) {
  return hse_ok;
}

void delay_us(uint32_t us) {
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = us * (SystemCoreClock / 1000000UL);

  while (DWT->CYCCNT - start < cycles) {
  }
}

void SysTick_Handler(void) {
  system_ms++;
}
