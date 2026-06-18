/*
 * rp2040.h — minimal bare-metal register definitions for the RP2040.
 *
 * Deliberately tiny: just enough to drive GPIO from `main.c` without dragging
 * in the full Pico SDK. Add more registers/blocks here as your project grows
 * (UART, timers, clocks, PIO, ...). For the complete set, see the SDK's
 * src/rp2040/hardware_regs/include/hardware/regs/ headers.
 *
 * No SDK, no libc beyond <stdint.h>/<stdbool.h>.
 */
#ifndef RP2040_H
#define RP2040_H

#include <stdint.h>
#include <stdbool.h>

/* Volatile 32-bit MMIO access. Use for every register read/write so the
 * compiler can't reorder or elide them. */
#define IO32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

/* ----------------------------------------------------------------------- */
/*  Peripheral base addresses                                              */
/* ----------------------------------------------------------------------- */
#define SIO_BASE        0xD0000000u   /* Single-cycle IO: GPIO, divider, ... */
#define IO_BANK0_BASE   0x40014000u   /* GPIO0..29 control (funcsel, IRQs)   */
#define PADS_BANK0_BASE 0x4001C000u   /* GPIO0..29 pad: IE, OE, drive, pull  */
#define XIP_BASE        0x10000000u   /* Execute-in-place flash window       */

/* ----------------------------------------------------------------------- */
/*  SIO — GPIO output / output-enable (single-cycle, atomic per register)  */
/*  Writing a bit to _SET/_CLR/_XOR sets/clears/toggles just that pin.     */
/* ----------------------------------------------------------------------- */
#define SIO_GPIO_IN      (SIO_BASE + 0x004u)
#define SIO_GPIO_OUT     (SIO_BASE + 0x010u)
#define SIO_GPIO_OUT_SET (SIO_BASE + 0x014u)
#define SIO_GPIO_OUT_CLR (SIO_BASE + 0x018u)
#define SIO_GPIO_OUT_XOR (SIO_BASE + 0x01Cu)
#define SIO_GPIO_OE      (SIO_BASE + 0x020u)
#define SIO_GPIO_OE_SET  (SIO_BASE + 0x024u)
#define SIO_GPIO_OE_CLR  (SIO_BASE + 0x028u)

/* ----------------------------------------------------------------------- */
/*  IO_BANK0 — per-pin function select                                     */
/* ----------------------------------------------------------------------- */
#define IO_GPIO_CTRL(n)  (IO_BANK0_BASE + 0x004u + (uint32_t)(n) * 8u)
#define GPIO_FUNCSEL_SIO 5u   /* connect the pin to the SIO block (plain GPIO) */

/* ----------------------------------------------------------------------- */
/*  Convenience helpers                                                    */
/* ----------------------------------------------------------------------- */

/* Connect a pin to the SIO GPIO controller (funcsel = SIO). Required before
 * using the gpio_* helpers below. Leaves pad at its reset default. */
static inline void gpio_init(uint32_t pin) {
    IO32(IO_GPIO_CTRL(pin)) = GPIO_FUNCSEL_SIO;
}

static inline void gpio_set_dir_out(uint32_t pin) { IO32(SIO_GPIO_OE_SET) = 1u << pin; }
static inline void gpio_set_dir_in (uint32_t pin) { IO32(SIO_GPIO_OE_CLR) = 1u << pin; }
static inline bool gpio_get (uint32_t pin)        { return (IO32(SIO_GPIO_IN) >> pin) & 1u; }
static inline void gpio_put (uint32_t pin, bool on) {
    IO32(on ? SIO_GPIO_OUT_SET : SIO_GPIO_OUT_CLR) = 1u << pin;
}
static inline void gpio_xor (uint32_t pin)        { IO32(SIO_GPIO_OUT_XOR) = 1u << pin; }

#endif /* RP2040_H */
