# CMakeLists.txt — bare-metal RP2040 template (no Pico SDK).
#
# Builds a flashable image from:
#   src/main.c      your application (default: blink GP25)
#   src/startup.c   Cortex-M0+ C startup: vector table + Reset_Handler
#   src/system_RP2040.c  CMSIS system file (SystemInit + SystemCoreClock)
#   ld/memmap.ld    linker script (boot2 @ 0x10000000, app @ 0x10000100)
#   boot2/          second-stage bootloader, built from readable source
#   cmsis/          CMSIS-Core + RP2040 device header (vendor-blessed)
#
# Configure with the arm-none-eabi toolchain:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=./arm-none-eabi-gcc.cmake -G Ninja
#   cmake --build build                 # firmware.elf + firmware.hex + firmware.bin
#   cmake --build build --target flash  # program over J-Link
#   cmake --build build --target disasm # write firmware.dis

cmake_minimum_required(VERSION 3.20)
project(rp2040_baremetal C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output artifact base name. flash.jlink loads "firmware.hex", so if you rename
# this, also update flash.jlink (and the FIRMWARE var in the Makefile).
set(FIRMWARE firmware)

# Keep all artifacts at the top of the build dir so flash.jlink's relative
# "loadfile firmware.hex" resolves from WORKING_DIRECTORY below.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---------------------------------------------------------------------------
# Boot stage 2 — assemble the readable source, pad+checksum to 256 bytes, and
# emit the padded blob as an assembly source compiled into the firmware.
# Reproduces src/rp2040/boot_stage2/CMakeLists.txt::pico_define_boot_stage2.
# Output is byte-identical to the SDK's official bs2_default for the Pico board.
# ---------------------------------------------------------------------------
set(BOOT2_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/boot2)
set(BOOT2_LD      ${BOOT2_DIR}/boot_stage2.ld)
set(BOOT2_SRC     ${BOOT2_DIR}/boot2_w25q080.S)
set(BOOT2_ELF     ${CMAKE_BINARY_DIR}/boot2.elf)
set(BOOT2_BIN     ${CMAKE_BINARY_DIR}/boot2.bin)
set(BOOT2_PAD_ASM ${CMAKE_BINARY_DIR}/boot2_padded_checksummed.S)

# PICO_FLASH_SPI_CLKDIV=2 matches boards/include/boards/pico.h (the only board-
# specific knob boot2 uses). Keeps the image byte-identical to the official Pico boot2.
set(BOOT2_DEFS -DPICO_FLASH_SPI_CLKDIV=2)

add_custom_command(
    OUTPUT  ${BOOT2_ELF}
    COMMAND ${CMAKE_C_COMPILER} ${BOOT2_DEFS} -ffreestanding -nostdlib
            "-I${BOOT2_DIR}/include" "-I${BOOT2_DIR}"
            -c ${BOOT2_SRC} -o ${CMAKE_BINARY_DIR}/boot2.o
    COMMAND ${CMAKE_C_COMPILER} -nostdlib -nostartfiles
            -T ${BOOT2_LD} ${CMAKE_BINARY_DIR}/boot2.o -o ${BOOT2_ELF}
    DEPENDS ${BOOT2_SRC} ${BOOT2_DIR}/boot2_helpers/exit_from_boot2.S
            ${BOOT2_DIR}/boot2_helpers/wait_ssi_ready.S
            ${BOOT2_DIR}/boot2_helpers/read_flash_sreg.S
            ${BOOT2_LD}
            ${BOOT2_DIR}/include/hardware/regs/ssi.h
            ${BOOT2_DIR}/include/hardware/regs/pads_qspi.h
            ${BOOT2_DIR}/include/hardware/regs/addressmap.h
            ${BOOT2_DIR}/include/hardware/regs/m0plus.h
            ${BOOT2_DIR}/include/hardware/platform_defs.h
            ${BOOT2_DIR}/include/pico/asm_helper.S
    COMMENT "Assembling+linking boot2 (readable source)"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${BOOT2_BIN}
    COMMAND ${CMAKE_OBJCOPY} -O binary ${BOOT2_ELF} ${BOOT2_BIN}
    DEPENDS ${BOOT2_ELF}
    COMMENT "objcopy boot2.elf -> boot2.bin"
)

add_custom_command(
    OUTPUT  ${BOOT2_PAD_ASM}
    COMMAND ${Python3_EXECUTABLE} ${BOOT2_DIR}/pad_checksum -s 0xffffffff
            ${BOOT2_BIN} ${BOOT2_PAD_ASM}
    DEPENDS ${BOOT2_BIN} ${BOOT2_DIR}/pad_checksum
    COMMENT "Padding+checksumming boot2 -> 256-byte embeddable blob"
)

# ---------------------------------------------------------------------------
# CMSIS — vendor Core + RP2040 device headers (system include, no warnings).
#   RP2040.h includes core_cm0plus.h, so both paths must be on the search list.
# ---------------------------------------------------------------------------
set(CMSIS_CORE_INC   ${CMAKE_CURRENT_SOURCE_DIR}/cmsis/Core/Include)
set(CMSIS_DEVICE_INC ${CMAKE_CURRENT_SOURCE_DIR}/cmsis/Device/RP2040/Include)

# ---------------------------------------------------------------------------
# Application image: startup + system + main + the embedded padded boot2 blob.
# ---------------------------------------------------------------------------
add_executable(${FIRMWARE}.elf
    src/startup.c
    src/system_RP2040.c
    src/main.c
    ${BOOT2_PAD_ASM}
)

target_include_directories(${FIRMWARE}.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${FIRMWARE}.elf SYSTEM PRIVATE
    ${CMSIS_CORE_INC} ${CMSIS_DEVICE_INC})
target_compile_options(${FIRMWARE}.elf PRIVATE -Os -g3 -Wall -Wextra -Wno-main)
target_link_options(${FIRMWARE}.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/ld/memmap.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/${FIRMWARE}.map
)
set_source_files_properties(${BOOT2_PAD_ASM} PROPERTIES GENERATED TRUE)

add_custom_command(TARGET ${FIRMWARE}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${FIRMWARE}.elf> ${CMAKE_BINARY_DIR}/${FIRMWARE}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${FIRMWARE}.elf> ${CMAKE_BINARY_DIR}/${FIRMWARE}.bin
    COMMAND ${CMAKE_SIZE}              $<TARGET_FILE:${FIRMWARE}.elf>
    COMMENT "Generating ${FIRMWARE}.hex / ${FIRMWARE}.bin; size report:"
)

# objdump writes to stdout; Ninja/Make run each command through /bin/sh, so the
# redirect is interpreted by the shell (NOT a VERBATIM command).
add_custom_target(disasm
    COMMAND ${CMAKE_OBJDUMP} -h -d $<TARGET_FILE:${FIRMWARE}.elf> > ${CMAKE_BINARY_DIR}/${FIRMWARE}.dis
    DEPENDS ${FIRMWARE}.elf
    COMMENT "Disassembling ${FIRMWARE}.elf -> ${FIRMWARE}.dis"
)

# Flash target: program firmware.hex over J-Link from the build dir.
find_program(JLINK_EXE JLinkExe)
if(JLINK_EXE)
    add_custom_target(flash
        COMMAND ${JLINK_EXE} -AutoConnect 1 -CommanderScript ${CMAKE_CURRENT_SOURCE_DIR}/flash.jlink
        DEPENDS ${FIRMWARE}.elf
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Flashing ${FIRMWARE}.hex via J-Link"
    )
else()
    message(WARNING "JLinkExe not found on PATH; the 'flash' target will be unavailable.")
endif()
