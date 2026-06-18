/*
 * startup.c — Cortex-M0+ C runtime startup for the RP2040 (bare metal)
 *
 * Boot up to here:
 *
 *   bootrom -> boot2 (configures XIP flash) -> Reset_Handler (here) -> main()
 *
 * The vector table MUST be the first thing in the application image: boot2 sets
 * VTOR = 0x10000100 and loads the initial SP + the reset-handler address from
 * it. memmap.ld enforces this with KEEP(*(.vectors)) right after .boot2.
 */

#include <stdint.h>
#include "RP2040.h"   /* CMSIS: SCB, NVIC, IRQn_Type, SystemInit, ... */

/* ----------------------------------------------------------------------- */
/*  Symbols provided by the linker script (ld/memmap.ld). These are the     */
/*  addresses of section boundaries, NOT values — always take "&" of them.  */
/* ----------------------------------------------------------------------- */
extern uint32_t __StackTop;      /* top of SRAM (0x20040000): initial SP      */
extern uint32_t __etext;         /* LMA in flash where .data's image lives    */
extern uint32_t __data_start__;  /* VMA in RAM where .data begins             */
extern uint32_t __data_end__;    /* VMA in RAM where .data ends               */
extern uint32_t __bss_start__;   /* RAM: start of zero-initialized section    */
extern uint32_t __bss_end__;     /* RAM: end of zero-initialized section      */

/* Provided by src/main.c */
int main(void);

/* Optional, overridable early-init hook (called before main, after .bss).
 * Weak + default-empty so main() runs even if you don't define it. The CMSIS
 * system file (system_RP2040.c) provides a STRONG SystemInit() that overrides
 * this one — add clock setup (XOSC/PLL -> 125 MHz) there. */
void SystemInit(void) __attribute__((weak));

/* ----------------------------------------------------------------------- */
/*  Default handler — any unexpected exception hangs here.                  */
/*  Declare first so the weak aliases below can target it.                  */
/* ----------------------------------------------------------------------- */
void Default_Handler(void) {
    while (1) {
        __asm volatile ("bkpt #0");  /* if a debugger is attached, stop here */
    }
}

/* ----------------------------------------------------------------------- */
/*  Exception handlers. Each is weak-aliased to Default_Handler, so you can */
/*  override any of them by simply defining a same-named function in your   */
/*  own code (e.g. `void HardFault_Handler(void) { ... }`).                 */
/* ----------------------------------------------------------------------- */
void Reset_Handler(void);
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/* The 26 RP2040 peripheral IRQs (vector indices 16..41). Names follow the
 * datasheet / SDK hardware/regs/intctrl.h. Override e.g.
 *     void TIMER_IRQ_0_Handler(void) { ... }
 * to use one; remember to also enable it in the NVIC + the peripheral. */
void TIMER_IRQ_0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER_IRQ_1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER_IRQ_2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER_IRQ_3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PWM_IRQ_WRAP_Handler(void)__attribute__((weak, alias("Default_Handler")));
void USBCTRL_IRQ_Handler(void) __attribute__((weak, alias("Default_Handler")));
void XIP_IRQ_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void PIO0_IRQ_0_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void PIO0_IRQ_1_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void PIO1_IRQ_0_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void PIO1_IRQ_1_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void DMA_IRQ_0_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void DMA_IRQ_1_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void IO_IRQ_BANK0_Handler(void)__attribute__((weak, alias("Default_Handler")));
void IO_IRQ_QSPI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SIO_IRQ_PROC0_Handler(void)__attribute__((weak, alias("Default_Handler")));
void SIO_IRQ_PROC1_Handler(void)__attribute__((weak, alias("Default_Handler")));
void CLOCKS_IRQ_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SPI0_IRQ_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void SPI1_IRQ_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void UART0_IRQ_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UART1_IRQ_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void ADC_IRQ_FIFO_Handler(void)__attribute__((weak, alias("Default_Handler")));
void I2C0_IRQ_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void I2C1_IRQ_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void RTC_IRQ_Handler(void)     __attribute__((weak, alias("Default_Handler")));

/* ----------------------------------------------------------------------- */
/*  Exception vector table                                                  */
/*  Index 0      = initial SP                                               */
/*  Index 1..15  = core exceptions (NMI, HardFault, SysTick, ...)           */
/*  Index 16..   = peripheral IRQs (RP2040 has 26 used, 6 reserved)         */
/*  Stored as uint32_t words so SP (an address) and handlers share a type.  */
/*  `used` prevents garbage collection; `aligned(128)` satisfies VTOR.       */
/* ----------------------------------------------------------------------- */
__attribute__((section(".vectors"), used, aligned(128)))
const uint32_t vector_table[] = {
    (uint32_t)&__StackTop,        /*  0:  Initial SP                        */
    (uint32_t)&Reset_Handler,     /*  1:  Reset                             */
    (uint32_t)&NMI_Handler,       /*  2:  NMI                               */
    (uint32_t)&HardFault_Handler, /*  3:  HardFault                         */
    0, 0, 0, 0, 0, 0, 0,          /*  4-10: Reserved (M0+ has no Mem/Bus/   */
                                  /*        UsageFault)                     */
    (uint32_t)&SVC_Handler,       /* 11: SVCall                             */
    0,                            /* 12: Reserved (no DebugMonitor on M0+)  */
    0,                            /* 13: Reserved                           */
    (uint32_t)&PendSV_Handler,    /* 14: PendSV                             */
    (uint32_t)&SysTick_Handler,   /* 15: SysTick                            */

    /* 16..41: RP2040 peripheral IRQs (in datasheet order) */
    (uint32_t)&TIMER_IRQ_0_Handler,  /* 16 */
    (uint32_t)&TIMER_IRQ_1_Handler,  /* 17 */
    (uint32_t)&TIMER_IRQ_2_Handler,  /* 18 */
    (uint32_t)&TIMER_IRQ_3_Handler,  /* 19 */
    (uint32_t)&PWM_IRQ_WRAP_Handler, /* 20 */
    (uint32_t)&USBCTRL_IRQ_Handler,  /* 21 */
    (uint32_t)&XIP_IRQ_Handler,      /* 22 */
    (uint32_t)&PIO0_IRQ_0_Handler,   /* 23 */
    (uint32_t)&PIO0_IRQ_1_Handler,   /* 24 */
    (uint32_t)&PIO1_IRQ_0_Handler,   /* 25 */
    (uint32_t)&PIO1_IRQ_1_Handler,   /* 26 */
    (uint32_t)&DMA_IRQ_0_Handler,    /* 27 */
    (uint32_t)&DMA_IRQ_1_Handler,    /* 28 */
    (uint32_t)&IO_IRQ_BANK0_Handler, /* 29 */
    (uint32_t)&IO_IRQ_QSPI_Handler,  /* 30 */
    (uint32_t)&SIO_IRQ_PROC0_Handler,/* 31 */
    (uint32_t)&SIO_IRQ_PROC1_Handler,/* 32 */
    (uint32_t)&CLOCKS_IRQ_Handler,   /* 33 */
    (uint32_t)&SPI0_IRQ_Handler,     /* 34 */
    (uint32_t)&SPI1_IRQ_Handler,     /* 35 */
    (uint32_t)&UART0_IRQ_Handler,    /* 36 */
    (uint32_t)&UART1_IRQ_Handler,    /* 37 */
    (uint32_t)&ADC_IRQ_FIFO_Handler, /* 38 */
    (uint32_t)&I2C0_IRQ_Handler,     /* 39 */
    (uint32_t)&I2C1_IRQ_Handler,     /* 40 */
    (uint32_t)&RTC_IRQ_Handler,      /* 41 */
    /* 42..47: IRQs 26..31 are unused on the RP2040 */
    0, 0, 0, 0, 0, 0,
};

/* ----------------------------------------------------------------------- */
/*  Reset_Handler — the C runtime entry point. Runs in Handler mode with a  */
/*  valid SP (set by hardware from vector_table[0]). Must not return.       */
/* ----------------------------------------------------------------------- */
void Reset_Handler(void) {
    /* Point VTOR at our vector table via the CMSIS SCB register (redundant
     * after boot2, but makes the image work when launched directly by gdb
     * without going through boot2). */
    SCB->VTOR = (uint32_t)vector_table;

    /* Copy .data initial values from flash (__etext) to RAM. */
    uint32_t *src = &__etext;
    uint32_t *dst = &__data_start__;
    while (dst < &__data_end__) {
        *dst++ = *src++;
    }

    /* Zero the .bss section in RAM. */
    for (dst = &__bss_start__; dst < &__bss_end__; ) {
        *dst++ = 0;
    }

    /* Optional early init (clocks, etc.). Default impl is empty. */
    SystemInit();

    /* Hand off to the application. */
    (void)main();

    /* main() should never return. Hang if it does. */
    while (1) {
        __asm__ volatile ("nop");
    }
}

/* Weak default SystemInit — overridden by system_RP2040.c's strong version. */
void SystemInit(void) {
}
