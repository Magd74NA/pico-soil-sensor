# Bare-metal on the RP2040 — A Learning Guide

This document explains, from first principles, how this project boots a program
on the Raspberry Pi Pico's RP2040 chip **without the Pico SDK**. It's the
"why" behind every file in this repository. Read it alongside the source — each
section points at the exact file and lines it describes.

It assumes you understand C and basic embedded concepts (memory-mapped IO,
binary/hex), but no prior Cortex-M or RP2040 knowledge is required.

---

## Table of contents

1. [The mental model: what "bare metal" means here](#1-the-mental-model)
2. [The RP2040's memory map](#2-the-rp2040s-memory-map)
3. [The boot chain: how the CPU reaches `main()`](#3-the-boot-chain)
4. [`boot2/` — the second-stage bootloader](#4-boot2--the-second-stage-bootloader)
5. [`src/startup.c` — the C runtime startup](#5-srcstartupc--the-c-runtime-startup)
6. [`ld/memmap.ld` — the linker script](#6-ldmemmapld--the-linker-script)
7. [`cmsis/` — the CMSIS headers](#7-cmsis--the-cmsis-headers)
8. [The clock tree (the `SystemInit` TODO)](#8-the-clock-tree)
9. [The build pipeline](#9-the-build-pipeline)
10. [Glossary of concepts worth memorising](#10-glossary)

---

## 1. The mental model

<a id="1-the-mental-model"></a>

On your laptop, when you run a program, the **operating system** does a lot of
invisible work before `main()` runs: it loads the executable into memory, sets
up a stack, zero-initialises globals, wires up the C standard library, and
hands control to your code. You never think about it.

On a microcontroller there is **no operating system**. When you apply power,
the CPU wakes up knowing nothing — no stack, no initialised globals, no
libraries, not even a guarantee of which clock speed it's running at. *Your
code* (and the chip's built-in bootrom) must do all of that setup work before
`main()` can safely execute a single line.

> "Bare metal" means: **you** are responsible for everything the OS would
> normally do. There is no safety net. The reward is total control and a
> tiny, deterministic, instant-booting firmware.

So the central question of this project is: **what has to happen between
power-on and `main()`?** The answer is a *chain* of four programs, each
handing off to the next:

```
  bootrom   →   boot2   →   startup (Reset_Handler)   →   main()
  (in ROM)      (your code) (your code)                   (your code)
```

Every file in this repo exists to assemble that chain correctly. The rest of
this document walks through each link.

---

## 2. The RP2040's memory map

<a id="2-the-rp2040s-memory-map"></a>

Before anything else, you need a mental picture of *where things live* on this
chip. The RP2040 exposes a **single, flat 32-bit address space** — code, RAM,
and peripherals all have addresses, and the CPU accesses them all the same way
(via pointers in C).

The three regions that matter to us:

| Address range | Size | What's there | Key property |
|---|---|---|---|
| `0x10000000`–`0x11FFFFFF` | 2 MB | **External QSPI flash** (your firmware lives here) | Read at runtime (XIP); slow-ish but executable |
| `0x20000000`–`0x20041FFF` | 264 KB | **SRAM** (static RAM, 6 banks) | Fast, read/write; loses contents on power-off |
| `0x40000000`–`0x4007FFFF` | — | **Peripherals** (clocks, GPIO ctrl, UART, ...) | MMIO — talk to hardware here |
| `0xD0000000`–`0xD0000FFF` | — | **SIO** (single-cycle IO — fast GPIO) | Special: GPIO toggles in 1 cycle |
| `0xE0000000`+ | — | **Core private peripherals** (SCB, NVIC, SysTick) | Cortex-M system registers |

Two non-obvious facts that drive everything:

1. **Flash is executable.** This is called **XIP** (eXecute In Place). The CPU
   fetches instructions directly from the flash chip over QSPI — you don't
   have to copy your code to RAM first. *But* QSPI needs configuration first,
   which is boot2's job (see §4).

2. **SRAM has two ranges.** The main 256 KB is at `0x20000000`. There's also a
   4 KB "scratch" bank per core (`0x20040000`+). Our stack pointer tops out at
   `0x20040000` (end of the main bank) and grows *downward*.

Memorise these three addresses — they appear constantly:
```
0x10000000   flash start   (where boot2 and your image live)
0x10000100   = 0x10000000 + 256  (where your app image starts)
0x20040000   = 0x20000000 + 256K (top of RAM, = stack top)
```

---

## 3. The boot chain

<a id="3-the-boot-chain"></a>

This is the single most important concept in the whole project. Let's trace
what happens from the instant you apply power until `main()` runs.

### Stage 0 — the bootrom (out of your control)

When the RP2040 powers on, the Cortex-M0+ core fetches its initial stack
pointer and reset vector from address `0x00000000`. On this chip that's mapped
to a small **mask ROM** burned into silicon at the factory — the "bootrom".

The bootrom does several things, but the one that matters to us:

> It **copies the first 256 bytes of flash** (`0x10000000`) into SRAM,
> **verifies a CRC32 checksum** embedded in those bytes, and if valid,
> **jumps to them**.

If the checksum is wrong (or flash is empty), it drops into a USB "BOOTSEL"
mode instead — that's the mode you get by holding BOOTSEL while plugging in the
Pico to drag-drop a UF2 file.

**Implication #1:** the first 256 bytes of your flash image *must* be a valid,
checksummed bootloader. That's `boot2` (§4).

**Implication #2:** the bootrom runs off the **ROSC** (ring oscillator) — an
on-chip clock that's fast but inaccurate (± a few %, varies with temperature).
Until *you* start the crystal oscillator (§8), nothing is cycle-accurate.

### Stage 1 — boot2 (`boot2/boot2_w25q080.S`)

Those 256 bytes run in SRAM. Their job is the one thing the bootrom can't do:
**configure the QSPI flash interface** so the rest of flash is executable.
Then boot2 does the hand-off described in §4.2.

### Stage 2 — `Reset_Handler` (`src/startup.c`)

boot2 jumps to the second word of your vector table (at `0x10000100`+4), which
points at `Reset_Handler`. This function sets up the C runtime: copies
`.data`, zeroes `.bss`, calls `SystemInit()`, then `main()`. Full detail in §5.

### Stage 3 — `main` (`src/main.c`)

Finally, your application. By the time you get here the stack is valid, globals
have their values, the vector table is installed, and the hardware is in a
known (if minimal) state.

### Why this design?

You might wonder why it's a *chain* rather than one program. Each stage solves
a bootstrapping problem the previous one couldn't:

- The **bootrom** is fixed (in ROM) so it can't know about your specific flash
  chip — but it can run *anything* generic. So it just runs the first 256 bytes.
- **boot2** is small (256-byte limit, including checksum) because that's all the
  bootrom copies. But it's *your* code, so it knows how to talk to *your*
  specific flash (the W25Q080). Once XIP is up, the 256-byte limit is gone.
- **startup** has no size limit (runs from flash via XIP) and full SRAM, so it
  can do real work: C runtime setup, optionally clock configuration, etc.

Each stage "unlocks" the next stage's capabilities. Beautifully layered.

---

## 4. `boot2/` — the second-stage bootloader

<a id="4-boot2--the-second-stage-bootloader"></a>

`boot2/` is a self-contained subsystem you should rarely need to touch. It
reproduces the SDK's boot2 pipeline exactly — the resulting 256-byte blob is
**byte-identical** to the SDK's official `bs2_default`.

### 4.1 What boot2 actually does

Open `boot2/boot2_w25q080.S` and follow along. Despite looking intimidating,
it does five understandable things:

1. **Configure the QSPI pads** — drive strength on the clock pin, disable
   Schmitt triggers on data pins for speed.
2. **Disable & reconfigure the SSI controller** — the hardware block that
   serialises bits to the flash chip. Set baud rate, frame format (quad-SPI).
3. **Program the flash's status register** — send the W25Q080 a command
   (`0x06` write-enable, `0x01` write-status) to switch it into quad-SPI mode
   (turning the WP/HOLD pins into extra data lines IO2/IO3).
4. **Dummy read** — issue one Fast-Read-Quad command with "mode continuation
   bits" set, so the flash stops requiring a command prefix on subsequent reads.
5. **Hand off** — call the exit routine (below).

### 4.2 The hand-off: this is the crucial moment

The last thing boot2 does is in `boot2/boot2_helpers/exit_from_boot2.S`:

```asm
vector_into_flash:
    ldr r0, =(XIP_BASE + 0x100)     @ 0x10000100
    ldr r1, =(PPB_BASE + M0PLUS_VTOR_OFFSET)   @ 0xE000ED08 (the VTOR register)
    str r0, [r1]                    @ VTOR = 0x10000100
    ldmia r0, {r0, r1}             @ r0 = *(0x10000100), r1 = *(0x10000104)
    msr msp, r0                    @ set the stack pointer = first word
    bx r1                          @ jump to the address in the second word
```

Read this carefully — it defines a contract the rest of the system must obey:

> boot2 writes `0x10000100` into the **VTOR** register, then loads the first
> two 32-bit words at that address: **word 0 becomes the stack pointer**, and
> **word 1 becomes the address to jump to**.

So whatever sits at flash address `0x10000100` is interpreted as a **vector
table**. This is why our linker script (§6) places `vector_table` at exactly
`0x10000100`, and why the vector table's first two entries are `__StackTop` and
`Reset_Handler`. The whole system hinges on that one `+0x100` offset.

### 4.3 Why the 256-byte limit and the checksum

The bootrom copies exactly 256 bytes — that's a hardware limit, not a choice.
Of those 256 bytes, the **last 4 are a CRC32** the bootrom checks; the
remaining 252 are code+data. `boot2/pad_checksum` pads the assembled code to
252 bytes and appends that CRC32. If you ever modify boot2, it must not exceed
252 bytes of payload.

### 4.4 How boot2 is built

See §9. The pipeline is: assemble `boot2_w25q080.S` → link with its own linker
script (`boot_stage2.ld`, which places it at `0x20041f00` in SRAM where the
bootrom copies it) → objcopy to raw binary → `pad_checksum` → emit as a `.byte`
blob in a `.boot2` section → link that into `firmware.elf`.

---

## 5. `src/startup.c` — the C runtime startup

<a id="5-srcstartupc--the-c-runtime-startup"></a>

This file has three responsibilities, in order: provide the **vector table**,
provide **`Default_Handler`**, and provide **`Reset_Handler`**. Let's take each.

### 5.1 The vector table

```c
__attribute__((section(".vectors"), used, aligned(128)))
const uint32_t vector_table[] = {
    (uint32_t)&__StackTop,        /*  0:  Initial SP      → 0x20040000 */
    (uint32_t)&Reset_Handler,     /*  1:  Reset           → 0x100001c9 */
    ...
```

Three attributes matter:
- `section(".vectors")` — the linker script forces this section to address
  `0x10000100` (right after boot2). That's the address boot2 wrote into VTOR.
- `used` — prevents `--gc-sections` from deleting it. Nothing in C references
  `vector_table` by name; the hardware "uses" it, but the linker can't see that.
- `aligned(128)` — Cortex-M0+ requires the vector table to be 128-byte aligned
  (VTOR ignores the low 7 bits).

**Index 0 is the stack pointer**, not a handler. On exception entry the CPU
loads SP from here. `&__StackTop` resolves to `0x20040000` (top of 256 KB SRAM).

**Index 1 is the reset handler address.** Note the stored value is `0x100001c9`
but `Reset_Handler`'s symbol is `0x100001c8` — that **+1 is the Thumb bit**.
All Cortex-M code is Thumb, and handler addresses must have bit 0 set to tell
the core "switch to Thumb state when jumping here". GCC inserts it
automatically when you take the address of a Thumb function. Never hand-write
handler addresses as integers.

**Indices 2–15 are core exceptions** defined by ARM (same on every Cortex-M0+):
NMI, HardFault, then reserved slots (M0+ doesn't have MemManage/BusFault/
UsageFault — those exist on M3+ only), then SVCall, PendSV, SysTick.

**Indices 16+ are peripheral interrupts (IRQs).** This part is RP2040-specific.
The hardware assigns each interrupt a fixed number; when (say) the ADC fires
IRQ 22, the CPU computes `VTOR + (16 + 22) × 4`, reads the word there, and
jumps. **So the position of an entry in the array *is* its interrupt number —
order is sacred.** We list all 26 RP2040 IRQs in datasheet order.

### 5.2 The weak/alias pattern (how you extend the startup)

```c
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
...
void TIMER_IRQ_0_Handler(void) __attribute__((weak, alias("Default_Handler")));
```

Two attributes together:
- `weak` — "my definition can be overridden by a strong definition elsewhere."
- `alias("Default_Handler")` — "if not overridden, point this symbol at
  `Default_Handler`."

Effect: every exception defaults to the hang-in-`Default_Handler` behaviour,
but you can override any single one just by defining it normally:
```c
void TIMER_IRQ_0_Handler(void) { /* your ISR */ }
```
Your definition is strong, so the linker drops the alias and points
`TIMER_IRQ_0_Handler` at your function. **You never edit `startup.c` to add an
interrupt handler.** This is the standard Cortex-M pattern.

### 5.3 `Default_Handler` — the "something went wrong" trap

```c
void Default_Handler(void) {
    while (1) { __asm volatile ("bkpt #0"); }
}
```

Infinite loop with a software breakpoint. If a debugger is attached, you stop
*here* and immediately see "ah, I'm in Default_Handler because IRQ X fired and
I forgot to handle it." Without a debugger, `bkpt` faults → HardFault → which is
*also* aliased here → safe hang. Always define this *before* the alias
declarations so the alias target is visible.

### 5.4 `Reset_Handler` — the actual work

```c
void Reset_Handler(void) {
    SCB->VTOR = (uint32_t)vector_table;      // A: re-point VTOR
    uint32_t *src = &__etext;                // B: copy .data
    uint32_t *dst = &__data_start__;
    while (dst < &__data_end__) *dst++ = *src++;
    for (dst = &__bss_start__; dst < &__bss_end__; ) *dst++ = 0;  // C: zero .bss
    SystemInit();                            // D: optional hook
    (void)main();                            // E: your program
    while (1) { __asm__ volatile ("nop"); }  // F: hang if main returns
}
```

**A — VTOR.** Redundant after boot2, but makes the image work when a debugger
loads the ELF directly (skipping bootrom/boot2). `SCB->VTOR` is the CMSIS name
for the register at `0xE000ED08`.

**B — copy `.data`.** *This is the most important concept in the whole file.*
A global like `int counter = 42;` has two lives:
- Its **runtime location** (VMA) is in RAM (so you can write to it).
- Its **initial value** (`42`) is stored in flash (so it survives power-off).

RAM is garbage at power-up. So before `main` reads `counter`, someone must
copy `42` from flash to RAM. **That someone is this loop.** It walks from
`__etext` (the value's location in flash) to `__data_start__..__data_end__`
(the variable's location in RAM).

**C — zero `.bss`.** `.bss` is the C convention for "starts at zero" globals
(`int x;` with no initialiser, or `= 0`). To save flash the linker does *not*
store a block of zeros; it just remembers the address range. At startup we
zero it ourselves. The C standard requires this; skip it and your globals
contain random SRAM junk.

**D — `SystemInit`.** An optional hook for early hardware init (clocks, etc.).
It's declared `weak` here; `system_RP2040.c` overrides it with a strong
definition. See §8.

**E — `main`.** Finally.

**F — hang.** `main` shouldn't return; if it does, spin forever rather than run
off into undefined memory.

**Constraint on `Reset_Handler`:** it runs *before* `.data`/`.bss` are
populated, so it must not read any initialised global. It only uses stack
locals (which work because SP was set from the vector table) and linker symbols
by address. This is why it's written so plainly.

---

## 6. `ld/memmap.ld` — the linker script

<a id="6-ldmemmapld--the-linker-script"></a>

The linker's job is to decide *where in physical memory* each piece of your
program goes. `memmap.ld` is the script that tells it. Two parts:

### 6.1 The MEMORY block

```ld
MEMORY {
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2048K
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
```

Declares the two physical regions and their properties (rx = read+execute,
rwx = read+write+execute). The linker will refuse to place code in RAM-only
sections, etc.

### 6.2 The SECTIONS block — the heart of the file

Walk through it in order:

```ld
.boot2 : {
    __boot2_start__ = .;
    KEEP(*(.boot2))
    __boot2_end__ = .;
} > FLASH
ASSERT(__boot2_end__ - __boot2_start__ == 256, "...")
```
- `.boot2` goes first in flash → lands at `0x10000000`. `KEEP` prevents
  garbage collection. The `ASSERT` enforces the 256-byte hardware requirement —
  if boot2 ever grows past 256, the *link* fails with a clear message.

```ld
.text : {
    KEEP(*(.vectors))   /* vector_table lands here → 0x10000100 */
    *(.text*)
    *(.rodata*)
} > FLASH
```
- `.text` follows immediately. Because `.boot2` was exactly 256 bytes, `.text`
  starts at `0x10000100` — which is where boot2 expects the vector table.
  `KEEP(*(.vectors))` ensures `vector_table` is the very first thing.

```ld
.data : {
    __data_start__ = .;
    *(.data*)
    __data_end__ = .;
} > RAM AT > FLASH
__etext = LOADADDR(.data);
```
- This is the magic line. `> RAM AT > FLASH` says: this section's **VMA** is in
  RAM (where the code expects to find it at runtime), but its **LMA** (where the
  bytes physically sit in the image) is in flash, right after `.text`.
- `__etext = LOADADDR(.data)` captures the LMA — that's the source pointer
  `Reset_Handler` copies from.

```ld
.bss (NOLOAD) : {
    __bss_start__ = .;
    *(.bss*)
    __bss_end__ = .;
} > RAM
```
- `(NOLOAD)` means "don't emit this to the flash image" — it's pure RAM, zeroed
  by startup. Saves flash space.

```ld
__StackTop = ORIGIN(RAM) + LENGTH(RAM);   /* 0x20040000 */
```
- The linker computes the stack-top address and exposes it as a symbol. This is
  what `vector_table[0]` reads.

### 6.3 LMA vs VMA — the key abstraction

If you understand one thing about linker scripts, understand this:

- **VMA (Virtual Memory Address)** — where the code *expects* the data to be at
  runtime (e.g. `.data` in RAM).
- **LMA (Load Memory Address)** — where the bytes *physically sit* in the image
  file (e.g. `.data`'s initial values in flash).

For `.text` and `.rodata`, LMA = VMA (both in flash). For `.data`, they differ:
VMA in RAM, LMA in flash. The startup code bridges that gap by copying. The
`> RAM AT > FLASH` syntax is how you express it. `LOADADDR()` gets the LMA;
the bare `.` inside the section gets the VMA.

### 6.4 The symbols startup needs

These six linker-defined symbols are read *by name* from `startup.c`:

| Symbol | Defined as | Read by |
|---|---|---|
| `__StackTop` | `ORIGIN(RAM) + LENGTH(RAM)` | vector_table[0] |
| `__etext` | `LOADADDR(.data)` | Reset_Handler `.data` copy source |
| `__data_start__` / `__data_end__` | `.data` VMA range | Reset_Handler `.data` copy |
| `__bss_start__` / `__bss_end__` | `.bss` range | Reset_Handler `.bss` zero |

They're not variables — they're *addresses*. In C you always take `&__StackTop`
to get the address itself; writing `__StackTop` (no `&`) would dereference it
and read random bytes. This distinction is the source of ~90% of startup bugs.

---

## 7. `cmsis/` — the CMSIS headers

<a id="7-cmsis--the-cmsis-headers"></a>

CMSIS (**C**ortex-**M** **S**oftware **I**nterface **S**tandard) is ARM's
vendor-neutral API for Cortex-M chips. This project ships the official
CMSIS-Core plus the RP2040 device header — the same vendor-blessed set the SDK
uses. Three layers:

### 7.1 CMSIS-Core (`cmsis/Core/Include/`)

ARM's `core_cm0plus.h` and compiler shims. Gives you typed access to the **core
registers** (the ones inside the CPU itself, identical on every Cortex-M0+):
- `SCB->VTOR`, `SCB->ICSR`, ... (System Control Block)
- `NVIC_EnableIRQ(...)`, `NVIC_SetPriority(...)`, `NVIC_ClearPendingIRQ(...)`
- `SysTick_Config(ticks)` (set up the system tick timer)
- `__enable_irq()`, `__disable_irq()`, `__WFI()` (wait for interrupt)
- The `IRQn_Type` enum so the NVIC functions know which interrupt you mean.

### 7.2 The device header (`cmsis/Device/RP2040/Include/RP2040.h`)

A 2796-line, SVD-generated header (SVD = "System View Description", an XML
format chip vendors publish describing every register). It defines:

- **`IRQn_Type`** — the RP2040's interrupt numbers (`TIMER_IRQ_0_IRQn = 0`,
  `ADC_IRQ_FIFO_IRQn = 22`, etc.) — the same numbers that match vector-table
  positions.
- **Peripheral structs** — `SIO_Type`, `CLOCKS_Type`, `UART0_Type`, `ADC_Type`,
  ... one struct per peripheral, with every register as a typed field.
- **Instance macros** — `SIO`, `IO_BANK0`, `CLOCKS`, `RESETS`, `UART0`, `ADC`,
  `PIO0`, `TIMER`, etc. Each is `((Foo_Type*)BASE_ADDR)` — a pointer you
  dereference like a struct.

So instead of `(*(volatile uint32_t*)0xd000001c) = (1u << 25)` you write:
```c
SIO->GPIO_OUT_XOR = (1u << 25);     // toggle GP25
SCB->VTOR         = (uint32_t)vector_table;
NVIC_EnableIRQ(TIMER_IRQ_0_IRQn);
```
Type-checked, autocomplete-friendly, and matching every other ARM vendor's
toolchain. **Use these structs for all hardware access.**

### 7.3 The system file (`src/system_RP2040.c`)

CMSIS expects a per-device "system" file providing `SystemInit()` and the
`SystemCoreClock` global. The SDK ships one but it depends on
`clock_get_hz()` (SDK-only), so we replace it with a bare-metal version that
leaves clock setup as the TODO described in §8.

### 7.4 Gotcha: include-guard collision

The device header guards with `#ifndef RP2040_H`. Our helper header
`include/rp2040.h` originally used the *same* guard — so when it did
`#include "RP2040.h"`, the guard was already defined and **the entire CMSIS
header got silently skipped**, leaving `SIO`/`IO_BANK0_BASE` undeclared. The
helper now uses a unique guard (`RP2040_HELPERS_H`). **Rule: never reuse a
vendor's include guard in your own header.**

---

## 8. The clock tree

<a id="8-the-clock-tree"></a>

At reset, `clk_sys` runs from the **ROSC** — an on-chip ring oscillator. It's
fast enough to blink an LED but **inaccurate** (frequency varies with
temperature/voltage/process, ±several %). For anything timing-critical (UART
baud rates, cycle-counted delays, ADC sampling), you need the **crystal
oscillator (XOSC)** — a 12 MHz crystal on the Pico board — multiplied up to
125 MHz by a **PLL**.

The full TODO is in `src/system_RP2040.c::SystemInit()` with exact register
values. The shape of the clock tree:

```
   12 MHz crystal ──> XOSC ──> clk_ref ──> PLL_SYS ──> clk_sys (125 MHz)
                                              │
                                  VCO = 12 MHz × 125 = 1500 MHz
                                  out = 1500 / 6 / 2 = 125 MHz
```

The math: a PLL multiplies a reference by `FBDIV/REFDIV` to produce a high
"VCO" frequency (750–1600 MHz range), then divides by `POSTDIV1 × POSTDIV2`
to get the output. For 125 MHz: `REFDIV=1, FBDIV=125 → VCO 1500 MHz`,
`POSTDIV1=6, POSTDIV2=2 → 125 MHz`.

`SystemInit()` is the right place for this setup because it runs *after* the C
runtime is up (so you can use loops and locals) but *before* `main` (so your
application code sees a known clock). The CMSIS convention is that
`SystemCoreClock` always reflects the current `clk_sys` in Hz, so set it to
`125000000u` when done.

Until you implement it, treat all timing as approximate and use the hardware
TIMER (or SysTick, which CMSIS gives you via `SysTick_Config`) for accurate
delays rather than busy-wait loops.

---

## 9. The build pipeline

<a id="9-the-build-pipeline"></a>

When you run `make` or `cmake --build build`, this happens (7 steps):

```
STEP 1   boot2_w25q080.S  ──assemble+link (boot_stage2.ld)──▶  boot2.elf
STEP 2   boot2.elf        ──objcopy -O binary──▶              boot2.bin   (~240 B)
STEP 3   boot2.bin        ──python pad_checksum──▶            boot2_padded_checksummed.S
                                                              (256 B: padded + CRC32)
STEP 4   boot2_padded_checksummed.S ──assemble──▶             boot2_blob.obj
STEP 5   src/startup.c, src/system_RP2040.c, src/main.c ──▶   *.obj
STEP 6   link everything with ld/memmap.ld ──▶                firmware.elf
STEP 7   objcopy ──▶ firmware.hex (for J-Link), firmware.bin (raw)
```

Key points:
- Steps 1–3 turn the readable boot2 source into an embeddable blob.
- Step 4 compiles that blob; its `.boot2` section lands at flash start.
- Step 6 is where all the address contracts (§6) get resolved — the linker
  places `.boot2` at `0x10000000`, `vector_table` at `0x10000100`, etc.
- `firmware.hex` (Intel HEX, carries addresses) is what `flash.jlink` loads.
  `firmware.elf` (with debug symbols) is what CLion/GDB loads for stepping.

Both CMake+Ninja and the standalone Makefile produce **byte-identical** images
— they're two front-ends to the same `arm-none-eabi-gcc` pipeline.

---

## 10. Glossary

<a id="10-glossary"></a>

**VTOR** (Vector Table Offset Register) — the Cortex-M register (at
`0xE000ED08`) that says where the vector table lives. boot2 sets it to
`0x10000100`; `Reset_Handler` sets it again defensively.

**Vector table** — an array of 32-bit words at a 128-byte-aligned address.
Word 0 is the initial stack pointer; words 1+ are handler addresses in a fixed
order (reset, NMI, HardFault, ..., then IRQ 0, IRQ 1, ...).

**Thumb bit** — bit 0 of a handler address. Cortex-M code is all Thumb; bit 0
set tells the core to stay in Thumb state when jumping. GCC sets it for you
when you take a function's address.

**bootrom** — factory mask ROM that runs first on power-up. Copies boot2 to
SRAM, checks its CRC32, jumps to it.

**boot2** (second-stage bootloader) — your 256-byte program that configures
QSPI/XIP and hands off to the vector table.

**XIP** (eXecute In Place) — the CPU fetches instructions directly from QSPI
flash without copying to RAM first. Requires boot2 to configure the flash
interface.

**LMA / VMA** — Load vs Virtual Memory Address. Where bytes sit in the image
file vs where the code expects them at runtime. `.data` has VMA in RAM, LMA in
flash; startup copies between them.

**`.data`** — initialised globals/statics with a non-zero value. Lives in RAM
at runtime; initial values stored in flash and copied over by `Reset_Handler`.

**`.bss`** — zero-initialised globals/statics. Lives in RAM only (not in the
flash image); zeroed by `Reset_Handler`.

**NVIC** (Nested Vectored Interrupt Controller) — the Cortex-M interrupt
controller. `NVIC_EnableIRQ(IRQn)` turns on an interrupt; the vector table
slot at `16 + IRQn` holds its handler.

**SIO** (Single-cycle IO) — a special RP2040 peripheral that lets you toggle
GPIO in a single CPU cycle via atomic SET/CLR/XOR registers. Faster than the
regular IO_BANK0 path.

**CMSIS** — ARM's standard C API for Cortex-M. Gives you typed core-register
access (`SCB->...`, `NVIC->...`) and a per-device header (`RP2040.h`) with
typed peripheral structs.

**C runtime** — the state C assumes before `main`: valid stack, `.data`
populated, `.bss` zeroed. On a hosted OS the kernel provides it; bare metal,
`Reset_Handler` does.

---

## Further reading

- **RP2040 datasheet** — the canonical reference. Especially §2 "Hardware"
  (covers XOSC, PLLs, CLOCKS, resets, address map) and the bootrom section.
- **ARMv6-M Architecture Reference Manual** — the CPU itself: exception model,
  instruction set, VTOR/NVIC/SysTick details.
- **The Pico SDK source** (in `../picotime/pico/pico-sdk/`):
  - `src/rp2_common/pico_crt0/crt0.S` — the full-fat equivalent of our
    `startup.c` (core-1 handling, binary info, copy-to-RAM).
  - `src/rp2_common/hardware_xosc/xosc.c`, `hardware_pll/pll.c`,
    `hardware_clocks/clocks.c` — reference implementations for the clock TODO.
  - `src/rp2040/boot_stage2/boot2_w25q080.S` — the source our `boot2/` is built
    from.
- **CMSIS-Core documentation** — `arm-software.github.io/CMSIS_5/Core/` — covers
  the SCB/NVIC/SysTick APIs and the startup contract.
