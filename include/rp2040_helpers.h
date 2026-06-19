/*
 * rp2040.h — convenience helpers on top of the CMSIS device header.
 *
 * The full register map lives in the CMSIS device header `RP2040.h` (SVD-
 * generated, in cmsis/Device/RP2040/Include/). It exposes every peripheral as
 * a typed struct with a base-pointer macro, e.g.
 *
 *     SIO->GPIO_OUT_XOR  = (1u << 25);   // toggle GP25
 *     SCB->VTOR          = (uint32_t)vector_table;
 *     NVIC_EnableIRQ(TIMER_IRQ_0_IRQn);
 *
 * This file adds only the tiny bit of sugar not in CMSIS: friendly GPIO
 * helpers (gpio_init/put/get/...) so application code reads cleanly. For
 * anything else, just use the CMSIS peripheral structs directly.
 *
 * Include "RP2040.h" for the raw register access; include "rp2040.h" for that
 * plus the helpers.
 */
#ifndef RP2040_HELPERS_H
#define RP2040_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include "RP2040.h"   /* CMSIS device header (core + peripherals + NVIC/SCB) */

/* GPIO_FUNCSEL_SIO = 5: route the pin to the SIO block (plain GPIO).
 * The IO_BANK0 per-pin control registers are arrayed 8 bytes apart. */
#define GPIO_FUNCSEL_SIO  5u
#define IO_GPIO_CTRL(n)   (IO_BANK0_BASE + 0x004u + (uint32_t)(n) * 8u)

/* Connect a pin to the SIO GPIO controller (funcsel = SIO). Required before
 * using the gpio_* helpers below. Leaves the pad at its reset default. */
static inline void gpio_init(uint32_t pin) {
    *(volatile uint32_t *)IO_GPIO_CTRL(pin) = GPIO_FUNCSEL_SIO;
}

static inline void gpio_set_dir_out(uint32_t pin) { SIO->GPIO_OE_SET = 1u << pin; }
static inline void gpio_set_dir_in (uint32_t pin) { SIO->GPIO_OE_CLR = 1u << pin; }
static inline bool gpio_get        (uint32_t pin) { return (SIO->GPIO_IN >> pin) & 1u; }
static inline void gpio_put        (uint32_t pin, bool on) {
    if (on) SIO->GPIO_OUT_SET = 1u << pin;
    else    SIO->GPIO_OUT_CLR = 1u << pin;
}
static inline void gpio_xor (uint32_t pin) { SIO->GPIO_OUT_XOR = 1u << pin; }

#endif /* RP2040_HELPERS_H */
