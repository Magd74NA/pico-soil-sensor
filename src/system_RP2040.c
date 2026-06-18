/*
 * system_RP2040.c — CMSIS-Core(M) Device System File for the RP2040.
 *
 * Provides the two CMSIS entry points the startup + core expect:
 *
 *   SystemCoreClock   : global holding the current clk_sys frequency in Hz.
 *   SystemInit()      : called from Reset_Handler (startup.c) just before
 *                       main(). This is THE place to bring up clocks (XOSC +
 *                       PLL -> 125 MHz), then set SystemCoreClock accordingly.
 *
 * We replace the SDK's version (src/rp2_common/cmsis/.../system_RP2040.c),
 * which depends on the SDK's clock_get_hz() — not available bare-metal. This
 * file is SDK-free.
 *
 * This SystemInit() is a STRONG definition, so it overrides the weak default
 * in startup.c. Define your clocks here as the project grows.
 */

#include <stdint.h>
#include "RP2040.h"          /* pulls in core_cm0plus.h + system_RP2040.h     */

/*---------------------------------------------------------------------------
  System Core Clock Variable
    Holds clk_sys in Hz. At reset the RP2040 runs from the ROSC at an
    undefined frequency (a few MHz, not crystal-accurate), so we leave this
    at 0 until clock initialisation is implemented in SystemInit().
 *---------------------------------------------------------------------------*/
uint32_t SystemCoreClock = 0u;

/*---------------------------------------------------------------------------
  SystemCoreClockUpdate — refresh SystemCoreClock from the hardware.
    Full impl would read CLOCKS->CLK_SYS_CTRL etc. We keep it as a hook;
    for now it just preserves the stored value.
 *---------------------------------------------------------------------------*/
void SystemCoreClockUpdate(void) {
    /* TODO: read the clock tree registers and compute the real clk_sys. */
}

/*---------------------------------------------------------------------------
  SystemInit — early hardware init, runs before main().

  >>> YOUR TURN: bring up the clocks so clk_sys is a known 125 MHz <<<

  This body is a placeholder. Implement the sequence below, then set
  SystemCoreClock. Declared in system_RP2040.h; this strong definition
  overrides the weak one in startup.c.

  =========================================================================
  THE GOAL
  =========================================================================
  At reset clk_sys runs from the ROSC (ring oscillator) at an undefined ~few
  MHz — fine for blinking, useless for cycle-counted delays / baud rates.
  We want clk_sys = 125 MHz derived from the 12 MHz crystal (XOSC) via the
  system PLL. The RP2040 clock tree is:

      12 MHz crystal ──> XOSC ──> clk_ref ──> PLL_SYS ──> clk_sys (125 MHz)

  =========================================================================
  THE REGISTER ACCESS YOU'LL USE
  =========================================================================
  The CMSIS device header (RP2040.h) gives typed structs, so you write e.g.
      XOSC->CTRL     = ...;
      PLL_SYS->CS     = ...;
      CLOCKS->CLK_REF_CTRL = ...;

  BUT: the CMSIS header only has the struct *fields*, not the named *values*
  for each field (e.g. it has XOSC->CTRL but not XOSC_CTRL_ENABLE_VALUE_ENABLE).
  The named value macros live in the SDK's hardware/regs/ headers:
      hardware/regs/xosc.h   hardware/regs/pll.h   hardware/regs/clocks.h
  Two options: (a) copy those three headers into cmsis/ and #include them, or
  (b) use the raw magic numbers below. (a) is cleaner; the values are given
  here for reference either way.

  =========================================================================
  THE SEQUENCE (each step's register, value, and why)
  =========================================================================

  STEP 1 — Start the crystal oscillator (XOSC)
    XOSC->STARTUP = 47;
        // startup delay in cycles of clk_ref; 47 ≈ 0.5ms at 96MHz reset clk.
        // (SDK computes STARTUP_DELAY = ((XOSC_HZ/1000 + 128)/256) * 1 ≈ 47.)
    XOSC->CTRL = (0xAA0u)              // FREQUENCY_RANGE = 1..15 MHz
               | (0xFABu << 12);       // ENABLE = enable (magic trigger value)
    while (!(XOSC->STATUS & (1u<<31))) ;   // wait STATUS.STABLE before use

  STEP 2 — Route XOSC into clk_ref (the reference for the PLL)
    CLOCKS->CLK_REF_CTRL = 2;          // SRC = XOSC_CLKSRC (was ROSC at reset)
    // (value 2 == CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC)

  STEP 3 — Configure the system PLL for 1500 MHz VCO
    PLL math for 125 MHz from 12 MHz reference:
        VCO = ref / REFDIV * FBDIV   → must be 750..1600 MHz
        out = VCO / POSTDIV1 / POSTDIV2
      Pick REFDIV=1, FBDIV=125 → VCO = 12*125 = 1500 MHz (✓ in range)
      Pick POSTDIV1=6, POSTDIV2=2 → 1500/6/2 = 125 MHz
    Order matters (must power down before changing dividers):
      PLL_SYS->PWR       = (1u<<0)        // PD = power down while we reconfig
                          | (1u<<2);      // DSMPD = 1 (integer mode, no dither)
      PLL_SYS->FBDIV_INT = 125;           // feedback divider
      PLL_SYS->PRIM      = (6u<<16)       // POSTDIV1 (field at bits 19:16)
                          | (2u<<12);     // POSTDIV2 (field at bits 15:12)
      PLL_SYS->CS        = 1;             // REFDIV = 1 (field at bits 5:0)
      // Power back up: clear PD, VCOPD, POSTDIVPD; keep DSMPD set.
      PLL_SYS->PWR       = (1u<<2);       // DSMPD only
      while (!(PLL_SYS->CS & (1u<<31))) ; // wait CS.LOCK before switching to it

  STEP 4 — Switch clk_sys from clk_ref over to PLL_SYS
    The SRC mux has two levels; flipping directly glitches. Do it in two writes:
      CLOCKS->CLK_SYS_CTRL = 0;           // AUXSRC = clksrc_pll_sys (value 0)
      CLOCKS->CLK_SYS_CTRL = 1;           // SRC    = clksrc_clk_sys_aux (1)
    (You're reading the whole register and writing SRC; AUXSRC must already be 0.)

  STEP 5 — Record the result
    SystemCoreClock = 125000000u;
    // (clk_peri is derived from clk_sys and feeds UART/SPI/I2C; leave it as-is
    //  for now, or set CLOCKS->CLK_PERI_CTRL if you need a specific peri clock.)

  =========================================================================
  REFERENCE IMPLEMENTATIONS (read these)
  =========================================================================
  • pico-sdk/src/rp2_common/hardware_xosc/xosc.c        → xosc_init()
  • pico-sdk/src/rp2_common/hardware_pll/pll.c          → pll_init()
  • pico-sdk/src/rp2_common/hardware_clocks/clocks.c    → clocks_init()
  • RP2040 datasheet, Section 2 "Hardware": subsections on XOSC, PLLs, CLOCKS.

  =========================================================================
  SANITY CHECKS ONCE IT WORKS
  =========================================================================
  • main()'s busy-wait will now be ~125MHz/ROSC faster — re-tune any delay.
  • Use the hardware TIMER (TIMER->RAWL/H, or SysTick) for accurate delays.
  • ADC / UART baud rates will now be deterministic given clk_sys=125MHz.
 *---------------------------------------------------------------------------*/
void SystemInit(void) {
    /* TODO: implement steps 1-5 above. */

    SystemCoreClockUpdate();
}
