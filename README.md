# rp2040-baremetal-template

A minimal, template for the **Raspberry Pi Pico
(RP2040)**. No Pico SDK, no libraries, includes startup. **Includes copy of SDK's Boot 2**

- Builds under **CMake** (for CLion) **and** a plain **Makefile**.
- Flashes over **J-Link** (SWD).
- Default app blinks the onboard LED (GP25)

---

## Quick start

```sh
# 1. configure + build (CMake + Ninja)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=./arm-none-eabi-gcc.cmake -G Ninja
cmake --build build

# 2. flash over J-Link
cmake --build build --target flash
```

The onboard LED should now be blinking. (Makefile alternative: `make && make flash`.)

### Requirements

- `arm-none-eabi-gcc` / `-binutils` / `-gdb`  (GCC 12+)
- `cmake` 3.20+, `ninja`, `python3`
- `JLinkExe` + `JLinkGDBServer` (SEGGER tools)
- A J-Link wired to the Pico's SWD header (SWDIO, SWCLK, GND, VTref)

---

## Project layout

```
rp2040-baremetal-template/
├── CMakeLists.txt              build definition (boot2 pipeline + firmware + flash/disasm targets)
├── Makefile                    standalone build (no CMake needed)
├── arm-none-eabi-gcc.cmake     CMake toolchain file (Cortex-M0+, Thumb, freestanding)
├── flash.jlink                 J-Link Commander script (loads firmware.hex)
│
├── src/
│   ├── main.c                  <-- YOUR APPLICATION (default: blink GP25)
│   ├── startup.c                Cortex-M0+ C startup: vector table + Reset_Handler
│   └── system_RP2040.c          CMSIS system file: SystemInit() + SystemCoreClock
├── ld/
│   └── memmap.ld               linker script (boot2 @ 0x10000000, app @ 0x10000100)
├── include/
│   └── rp2040_helpers.h        GPIO helpers on top of CMSIS (SIO->, SCB->, ...)
│
├── cmsis/                      CMSIS-Core + RP2040 device header (vendor-blessed)
│   ├── Core/Include/            core_cm0plus.h, cmsis_gcc.h, ... (SCB/NVIC/SysTick)
│   └── Device/RP2040/Include/   RP2040.h (full peripheral map), system_RP2040.h
│
└── boot2/                      SECOND-STAGE BOOTLOADER, built from source
    ├── boot2_w25q080.S         readable source: configures W25Q080 QSPI XIP mode
    ├── boot2_helpers/          exit_from_boot2.S, wait_ssi_ready.S, read_flash_sreg.S
    ├── boot_stage2.ld          linker script (SRAM @ 0x20041f00, 252 bytes)
    ├── pad_checksum            python: pads to 256 B + appends bootrom CRC32
    └── include/                self-contained headers (no SDK config deps)
```

---

## Customizing for your project

1. **Edit `src/main.c`** — replace the blink example with your code.
2. **Rename the firmware** (optional). The output artifact defaults to `firmware`.
   To change it, update `FIRMWARE` in three places:
   - `set(FIRMWARE ...)` in `CMakeLists.txt`
   - `FIRMWARE ?= ...` at the top of `Makefile`
   - `loadfile firmware.hex` in `flash.jlink`
3. **Access hardware** via the CMSIS device header — `#include "RP2040.h"` (or
   `"rp2040_helpers.h"` for the helpers too). Every peripheral is a typed struct:
   `SIO->GPIO_OUT_XOR = (1u << 25);`, `SCB->VTOR = ...`, `NVIC_EnableIRQ(...)`.
   Add project-specific sugar to `include/rp2040_helpers.h`.
4. **Rename the firmware** (optional). The output artifact defaults to `firmware`.
   To change it, update `FIRMWARE` in three places:
   - `set(FIRMWARE ...)` in `CMakeLists.txt`
   - `FIRMWARE ?= ...` at the top of `Makefile`
   - `loadfile firmware.hex` in `flash.jlink`
5. **Rename the project** (optional) — change `project(rp2040_baremetal ...)` in
   `CMakeLists.txt`. CLion picks up the new name automatically; only the `.idea/`
   run config's `PROJECT_NAME` references the old name and is easy to recreate.

---

## The boot chain (what runs on power-up)

1. **bootrom** (mask ROM) copies the first 256 B of flash (`0x10000000`) into
   SRAM, checks the CRC32, jumps to it.
2. **boot2** (`boot2/boot2_w25q080.S`) — readable assembly that configures the
   QSPI/XIP interface so flash is executable, sets `VTOR = 0x10000100`, then
   loads SP + reset-handler from the vector table and jumps.
3. **startup.c** (`Reset_Handler`) re-sets VTOR via `SCB->VTOR`, copies `.data`
   flash→RAM, zeroes `.bss`, calls `SystemInit()` (from system_RP2040.c), then
   `main`.
4. **main** is your code.

### How boot2 is built (reproduces the SDK pipeline)

```
boot2_w25q080.S  --assemble+link (boot_stage2.ld)-->  boot2.elf
                                                       |
                              --objcopy -O binary-->   boot2.bin   (~240 B)
                                                       |
                              --pad_checksum-------->  boot2_padded_checksummed.S
                                                       |   (256 B: padded + CRC32)
                              --assemble-->            .boot2 section of firmware.elf
```

The output is **byte-identical** to the SDK's `bs2_default` for the Pico board.
The one board-specific knob, `PICO_FLASH_SPI_CLKDIV=2`, is set explicitly
(matches `boards/include/boards/pico.h`).

> **Clocks are not initialized** by this template, so `clk_sys` runs from the
> ROSC at an unspecified frequency (a few MHz). Fine for blinking/toggling; for
> a deterministic 125 MHz core, start XOSC + PLL (see the SDK's `clocks.c`) and
> use the hardware TIMER for accurate delays. `SystemInit()` in
> `src/system_RP2040.c` is the canonical place to do that (and to set
> `SystemCoreClock`).

---

## CMSIS headers

This project ships the **official CMSIS-Core** plus the **RP2040 device header**
(the same vendor-blessed set the SDK uses), under `cmsis/`. No SDK required.

- `cmsis/Core/Include/` — ARM's `core_cm0plus.h` + compiler shims. Provides the
  core register access: `SCB->VTOR`, `NVIC_EnableIRQ(...)`, `SysTick_Config(...)`,
  `__enable_irq()`, the `IRQn_Type`-aware interrupt API, etc.
- `cmsis/Device/RP2040/Include/RP2040.h` — SVD-generated full register map.
  Every peripheral is a typed struct with a base-pointer macro:
  `SIO`, `IO_BANK0`, `CLOCKS`, `RESETS`, `UART0`, `ADC`, `PIO0`, ... So you write
  `SIO->GPIO_OUT_XOR = (1u << 25);` instead of hand-computing addresses.
- `src/system_RP2040.c` — the CMSIS system file: owns `SystemInit()` and
  `SystemCoreClock`. We replace the SDK's version (which depends on `clock_get_hz`).

To use: `#include "RP2040.h"` (or `#include "rp2040_helpers.h"` for the GPIO helpers too).
   (The helper is named `rp2040_helpers.h`, not `rp2040.h`, so it doesn't collide
   with the device header `RP2040.h` on case-insensitive filesystems like macOS.)
The build adds both `cmsis/` include dirs as `-isystem` (so vendor headers don't
trigger `-Wall`).

> **Include-guard gotcha:** the device header guards with `#ifndef RP2040_H`, so
> any helper header must use a *different* guard (this project's `rp2040.h` uses
> `RP2040_HELPERS_H`) or it'll silently skip the whole CMSIS header.

---

## Building

### CMake (recommended, required for CLion)

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=./arm-none-eabi-gcc.cmake -G Ninja
cmake --build build                 # boot2 + firmware.elf/.hex/.bin
cmake --build build --target flash  # program over J-Link
cmake --build build --target disasm # write firmware.dis
```

### Makefile (standalone)

```sh
make          # boot2 from source + firmware.elf/.bin/.hex/.dis
make flash    # program over J-Link
make clean
```

Both produce identical images.

---

## Open in CLion

The `.idea/` directory is preconfigured.

1. **Settings → Build, Execution, Deployment → Toolchains** — ensure a toolchain
   named **`embedded`** exists: compiler `arm-none-eabi-gcc`, debugger
   `arm-none-eabi-gdb`. (This is the same toolchain used by typical STM32
   CLion setups.)
2. **File → Open** this directory. CLion loads the **`Debug-embedded`** profile,
   which passes `-DCMAKE_TOOLCHAIN_FILE=./arm-none-eabi-gcc.cmake`.
3. Build target **`firmware.elf`**; artifacts land in `cmake-build-debug-embedded/`.
4. Run config **`firmware (J-Link)`** downloads & debugs via the J-Link GDB server
   (`.idea/debugServers/SEGGER_J_Link.xml`, device `RP2040_M0_0`, SWD @ 4 MHz).
   If CLion can't resolve it on first open, create an **Embedded GDB Server**
   config pointing at the `SEGGER J-Link` debug target.
5. Flash from the terminal anytime: `cmake --build build --target flash`.

---

## Hardware

- J-Link EDU Mini wired to the Pico SWD header (SWDIO, SWCLK, GND, VTref).
- External LED (optional): GP19 (pin 24) → 220–330Ω → LED anode → GND (pin 23).
  (GP25, the onboard LED, is not broken out to the header.)
