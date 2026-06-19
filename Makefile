# Makefile — standalone bare-metal build for the RP2040 (no Pico SDK, no CMake).
#
#   make          # builds boot2 from source, then firmware.elf/.bin/.hex/.dis
#   make flash    # programs firmware.hex over J-Link (needs JLinkExe)
#   make clean
#
# All build artifacts go into BUILD_DIR (default: build/). The source tree is
# never written to, so `make clean` is just `rm -rf build/`.
#
# Toolchain: arm-none-eabi-gcc (arm-none-eabi-binutils). Also needs python3 for
# the boot2 pad+checksum step.

ARM_PREFIX ?= arm-none-eabi
CC      := $(ARM_PREFIX)-gcc
OBJCOPY := $(ARM_PREFIX)-objcopy
OBJDUMP := $(ARM_PREFIX)-objdump
SIZE    := $(ARM_PREFIX)-size

# Output artifact base name. Keep in sync with CMakeLists.txt and flash.jlink.
FIRMWARE ?= firmware

# Where ALL build artifacts live. Layout produced:
#
#   $(BUILD_DIR)/
#   ├── boot2.o, boot2.elf, boot2.bin, boot2_padded_checksummed.S   (boot2 stage)
#   ├── src/startup.o, src/system_RP2040.o, src/main.o              (app objects)
#   └── firmware.elf, firmware.bin, firmware.hex, firmware.dis, firmware.map
#
# flash.jlink uses a relative "loadfile firmware.hex", so the flash target runs
# JLinkExe from inside $(BUILD_DIR).
BUILD_DIR ?= build
B         := $(BUILD_DIR)

ARCHFLAGS := -mcpu=cortex-m0plus -mthumb
CFLAGS   := $(ARCHFLAGS) -Os -g3 -std=c11 -ffreestanding \
            -ffunction-sections -fdata-sections \
            -Iinclude -isystem cmsis/Core/Include -isystem cmsis/Device/RP2040/Include \
            -Wall -Wextra -Wno-main -nostdlib
LDFLAGS  := $(ARCHFLAGS) -nostdlib -nostartfiles \
            -T ld/memmap.ld -Wl,--gc-sections -Wl,-Map=$(B)/$(FIRMWARE).map

# ---- boot2 built from readable source (same pipeline as the SDK) ----
BOOT2_DIR  := boot2
BOOT2_INC  := -I$(BOOT2_DIR)/include -I$(BOOT2_DIR)
BOOT2_DEFS := -DPICO_FLASH_SPI_CLKDIV=2   # matches boards/include/boards/pico.h

# ---- Build-dir output paths ----
BOOT2_O   := $(B)/boot2.o
BOOT2_ELF := $(B)/boot2.elf
BOOT2_BIN := $(B)/boot2.bin
BOOT2_PAD := $(B)/boot2_padded_checksummed.S
STARTUP_O := $(B)/src/startup.o
SYSTEM_O  := $(B)/src/system_RP2040.o
MAIN_O    := $(B)/src/main.o
APP_OBJS  := $(STARTUP_O) $(SYSTEM_O) $(MAIN_O)

FW_ELF := $(B)/$(FIRMWARE).elf
FW_BIN := $(B)/$(FIRMWARE).bin
FW_HEX := $(B)/$(FIRMWARE).hex
FW_DIS := $(B)/$(FIRMWARE).dis

.PHONY: all flash clean
all: $(FW_ELF) $(FW_BIN) $(FW_HEX)

# assemble+link boot2 with its own linker script (SRAM @ 0x20041f00, 252 bytes).
# $(@D) = the directory part of the target; mkdir -p creates it on demand.
$(BOOT2_ELF): $(BOOT2_DIR)/boot2_w25q080.S $(BOOT2_DIR)/boot_stage2.ld
	@mkdir -p $(@D)
	$(CC) $(ARCHFLAGS) $(BOOT2_DEFS) -ffreestanding -nostdlib $(BOOT2_INC) \
	  -c $(BOOT2_DIR)/boot2_w25q080.S -o $(BOOT2_O)
	$(CC) $(ARCHFLAGS) -nostdlib -nostartfiles -T $(BOOT2_DIR)/boot_stage2.ld \
	  $(BOOT2_O) -o $@

$(BOOT2_BIN): $(BOOT2_ELF)
	@mkdir -p $(@D)
	$(OBJCOPY) -O binary $< $@

# pad to 256 bytes + append the bootrom CRC32, emit as an embeddable .S blob
$(BOOT2_PAD): $(BOOT2_BIN) $(BOOT2_DIR)/pad_checksum
	@mkdir -p $(@D)
	python3 $(BOOT2_DIR)/pad_checksum -s 0xffffffff $< $@

$(STARTUP_O): src/startup.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYSTEM_O): src/system_RP2040.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(MAIN_O): src/main.c include/rp2040_helpers.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Link the firmware. The objdump disassembly is emitted alongside.
$(FW_ELF): $(APP_OBJS) $(BOOT2_PAD) ld/memmap.ld
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(APP_OBJS) $(BOOT2_PAD) $(LDFLAGS) -o $@
	$(OBJDUMP) -h -d $@ > $(FW_DIS)

$(FW_BIN): $(FW_ELF)
	@mkdir -p $(@D)
	$(OBJCOPY) -O binary $< $@

$(FW_HEX): $(FW_ELF)
	@mkdir -p $(@D)
	$(OBJCOPY) -O ihex $< $@

# Flash: run JLinkExe from the build dir (flash.jlink loads "firmware.hex"
# relative to the working dir). flash.jlink itself is passed as an absolute path
# so this works no matter where make was invoked.
flash: $(FW_HEX)
	cd $(B) && JLinkExe -AutoConnect 1 -CommanderScript $(CURDIR)/flash.jlink

clean:
	rm -rf $(BUILD_DIR)
