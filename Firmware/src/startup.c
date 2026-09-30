/** \file
  \brief Vector table and reset handler for the STM32F401.

  Plain C instead of ST's assembler startup file. Every interrupt without
  a handler of its own lands in Default_Handler, which stops there so a
  debugger shows the culprit.
*/

#include <stdint.h>

#include "stm32f4xx.h"

/* Linker script symbols. */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

int main(void);
void SystemInit(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Core exceptions and the interrupts we use; all others are aliases. */
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))

void NMI_Handler(void)          WEAK_DEFAULT;
void HardFault_Handler(void)    WEAK_DEFAULT;
void MemManage_Handler(void)    WEAK_DEFAULT;
void BusFault_Handler(void)     WEAK_DEFAULT;
void UsageFault_Handler(void)   WEAK_DEFAULT;
void SVC_Handler(void)          WEAK_DEFAULT;
void DebugMon_Handler(void)     WEAK_DEFAULT;
void PendSV_Handler(void)       WEAK_DEFAULT;
void SysTick_Handler(void)      WEAK_DEFAULT;
void OTG_FS_IRQHandler(void)    WEAK_DEFAULT;

typedef void (*vector_t)(void);

/// 16 core exceptions + 85 interrupts (up to SPI4_IRQn = 84).
#define VECTOR_COUNT (16 + SPI4_IRQn + 1)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

__attribute__((section(".isr_vector"), used))
const vector_t vector_table[VECTOR_COUNT] = {
  [0 ... VECTOR_COUNT - 1] = Default_Handler,

  [0]  = (vector_t)&_estack,
  [1]  = Reset_Handler,
  [2]  = NMI_Handler,
  [3]  = HardFault_Handler,
  [4]  = MemManage_Handler,
  [5]  = BusFault_Handler,
  [6]  = UsageFault_Handler,
  [7]  = 0,
  [8]  = 0,
  [9]  = 0,
  [10] = 0,
  [11] = SVC_Handler,
  [12] = DebugMon_Handler,
  [13] = 0,
  [14] = PendSV_Handler,
  [15] = SysTick_Handler,

  [16 + OTG_FS_IRQn] = OTG_FS_IRQHandler,
};

#pragma GCC diagnostic pop

void Reset_Handler(void) {
  uint32_t *src, *dst;

  // FPU on (CP10, CP11 full access) before any floating point instruction.
  SCB->CPACR |= (0xFUL << 20);
  __DSB();
  __ISB();

  // .data from flash, .bss zeroed.
  for (src = &_sidata, dst = &_sdata; dst < &_edata; )
    *dst++ = *src++;
  for (dst = &_sbss; dst < &_ebss; )
    *dst++ = 0;

  SystemInit();
  main();

  for (;;) {
  }
}

void Default_Handler(void) {
  for (;;) {
  }
}
