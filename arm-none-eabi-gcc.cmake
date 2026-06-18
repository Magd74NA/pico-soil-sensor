# arm-none-eabi-gcc.cmake — CMake toolchain file for the Raspberry Pi Pico (RP2040).
#
# Cross-compiles for the Cortex-M0+ core using the arm-none-eabi GCC toolchain,
# freestanding (no newlib runtime), and never tries to execute ARM test binaries
# on the host during try_compile().
#
# Consumed by the CLion "Debug-embedded" CMake profile via:
#     -DCMAKE_TOOLCHAIN_FILE="./arm-none-eabi-gcc.cmake"
#
# (Same convention as your cmake_template_stm32 project, but retuned for the
#  RP2040: Cortex-M0+, Thumb, no FPU. Compare with that project's STM32F4
#  Cortex-M4 + FPU variant.)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# We cannot run ARM binaries on the host, so tell CMake not to try.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Toolchain binaries
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP      arm-none-eabi-objdump)
set(CMAKE_SIZE         arm-none-eabi-size)

# RP2040: Cortex-M0+, Thumb, no FPU.
set(MCU_FLAGS "-mcpu=cortex-m0plus -mthumb")

# Baseline flags for every target in the project. Per-target options (optimisation,
# warnings, the linker script, ...) are added in CMakeLists.txt.
set(CMAKE_C_FLAGS_INIT        "${MCU_FLAGS} -ffreestanding -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT      "${MCU_FLAGS} -ffreestanding -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT      "${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -nostartfiles -nostdlib -Wl,--gc-sections")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
