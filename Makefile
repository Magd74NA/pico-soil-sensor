# Makefile — standalone bare-metal build for the RP2040 (no Pico SDK, no CMake).
#
#   make          # builds boot2 from source, then firmware.elf/.bin/.hex/.dis
#   make flash    # programs firmware.hex over J-Link (needs JLinkExe)
#   make clean
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

ARCHFLAGS := -mcpu=cortex-m0plus -mthumb
CFLAGS   := $(ARCHFLAGS) -Os -g3 -std=c11 -ffreestanding \
            -ffunction-sections -fdata-sections -Iinclude \
            -Wall -Wextra -Wno-main -nostdlib
LDFLAGS  := $(ARCHFLAGS) -nostdlib -nostartfiles \
            -T ld/memmap.ld -Wl,--gc-sections -Wl,-Map=$(FIRMWARE).map

# ---- boot2 built from readable source (same pipeline as the SDK) ----
BOOT2_DIR  := boot2
BOOT2_INC  := -I$(BOOT2_DIR)/include -I$(BOOT2_DIR)
BOOT2_DEFS := -DPICO_FLASH_SPI_CLKDIV=2   # matches boards/include/boards/pico.h

.PHONY: all flash clean
all: $(FIRMWARE).elf $(FIRMWARE).bin $(FIRMWARE).hex

# assemble+link boot2 with its own linker script (SRAM @ 0x20041f00, 252 bytes)
boot2.elf: $(BOOT2_DIR)/boot2_w25q080.S $(BOOT2_DIR)/boot_stage2.ld
	$(CC) $(ARCHFLAGS) $(BOOT2_DEFS) -ffreestanding -nostdlib $(BOOT2_INC) \
	  -c $(BOOT2_DIR)/boot2_w25q080.S -o boot2.o
	$(CC) $(ARCHFLAGS) -nostdlib -nostartfiles -T $(BOOT2_DIR)/boot_stage2.ld \
	  boot2.o -o $@

boot2.bin: boot2.elf
	$(OBJCOPY) -O binary $< $@

# pad to 256 bytes + append the bootrom CRC32, emit as an embeddable .S blob
boot2_padded_checksummed.S: boot2.bin $(BOOT2_DIR)/pad_checksum
	python3 $(BOOT2_DIR)/pad_checksum -s 0xffffffff $< $@

src/startup.o: src/startup.c
	$(CC) $(CFLAGS) -c $< -o $@

src/main.o: src/main.c include/rp2040.h
	$(CC) $(CFLAGS) -c $< -o $@

$(FIRMWARE).elf: src/startup.o src/main.o boot2_padded_checksummed.S ld/memmap.ld
	$(CC) $(CFLAGS) src/startup.o src/main.o boot2_padded_checksummed.S $(LDFLAGS) -o $@
	$(OBJDUMP) -h -d $@ > $(FIRMWARE).dis

$(FIRMWARE).bin: $(FIRMWARE).elf
	$(OBJCOPY) -O binary $< $@

$(FIRMWARE).hex: $(FIRMWARE).elf
	$(OBJCOPY) -O ihex $< $@

flash: $(FIRMWARE).hex
	JLinkExe -AutoConnect 1 -CommanderScript flash.jlink

clean:
	rm -f boot2.o boot2.elf boot2.bin boot2_padded_checksummed.S \
	      src/startup.o src/main.o $(FIRMWARE).elf $(FIRMWARE).bin $(FIRMWARE).hex $(FIRMWARE).dis $(FIRMWARE).map
