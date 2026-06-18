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
│   └── startup.c                Cortex-M0+ C startup: vector table + Reset_Handler
├── ld/
│   └── memmap.ld               linker script (boot2 @ 0x10000000, app @ 0x10000100)
├── include/
│   └── rp2040.h                minimal register defs + GPIO helpers (grow this)
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
3. **Add registers** to `include/rp2040.h` as needed (UART, timers, clocks, PIO…).
4. **Rename the project** (optional) — change `project(rp2040_baremetal ...)` in
   `CMakeLists.txt`. CLion picks up the new name automatically; only the `.idea/`
   run config's `PROJECT_NAME` references the old name and is easy to recreate.

---

## The boot chain (what runs on power-up)

1. **bootrom** (mask ROM) copies the first 256 B of flash (`0x10000000`) into
   SRAM, checks the CRC32, jumps to it.
2. **boot2** (`boot2/boot2_w25q080.S`) — readable assembly that configures the
   QSPI/XIP interface so flash is executable, sets `VTOR = 0x10000100`, then
   loads SP + reset-handler from the vector table and jumps.
3. **startup.c** (`Reset_Handler`) re-sets VTOR, copies `.data` flash→RAM, zeroes
   `.bss`, calls `main`.
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
> use the hardware TIMER for accurate delays.

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
