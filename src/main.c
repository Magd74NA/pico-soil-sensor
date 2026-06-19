/*
 * main.c — application entry point.
 *
 * Default example: blink the onboard LED (GP25). Replace with your own code.
 *
 * NOTE: clocks are NOT initialised, so clk_sys runs from the ROSC at an
 * unspecified frequency (a few MHz). That's fine for blinking; for a precise
 * 125 MHz core, start XOSC + PLL (see the SDK's clocks.c) and use the hardware
 * timer for accurate delays.
 */
#include "rp2040_helpers.h"

/* Onboard LED on the original Raspberry Pi Pico. GP25 is NOT broken out to the
 * 40-pin header — for an external LED use a header pin such as GP19 (pin 24):
 *   GP19 -> 220-330R -> LED anode ; LED cathode -> GND
 */
#define LED_PIN 25

/* Rough busy-wait half-period. Not cycle-accurate (ROSC clock); tune to taste. */
#define DELAY_ITERATIONS (1u << 24)

static void delay(void) {
    for (volatile uint32_t i = 0; i < DELAY_ITERATIONS; ++i) {
        __asm volatile ("nop");
    }
}

int main(void) {
    gpio_init(LED_PIN);
    gpio_set_dir_out(LED_PIN);

    for (;;) {
        gpio_xor(LED_PIN);
        delay();
    }
}
