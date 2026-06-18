/*
 * memmap.ld — minimal RP2040 memory layout (bare metal).
 *
 * Flash image layout:
 *
 *   0x10000000 +-----------------------------------+
 *              | .boot2  (256 B, padded+checksummed)|  <- runs first (in SRAM)
 *   0x10000100 +-----------------------------------+
 *              | __vectors (SP, reset, ...)         |  <- boot2 vectors here
 *              | .text* / .rodata*                  |
 *              | __etext  (LMA source for .data)    |
 *              +-----------------------------------+
 *              | .data  (load image, copied to RAM) |
 *              +-----------------------------------+
 *
 *   RAM 0x20000000: .data -> .bss -> (heap) -> (stack grows down to __StackTop)
 *
 * Compare with the SDK's pico_crt0/rp2040/memmap_default.ld.
 */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2048K
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

ENTRY(Reset_Handler)

SECTIONS
{
    /* ---- Second-stage bootloader: exactly 256 bytes at flash start ---- */
    .boot2 : {
        __boot2_start__ = .;
        KEEP(*(.boot2))
        __boot2_end__ = .;
    } > FLASH
    ASSERT(__boot2_end__ - __boot2_start__ == 256,
           "ERROR: boot2 must be exactly 256 bytes")

    /* ---- Application image. Starts at 0x10000100 because .boot2 is 256 B ---- */
    .text : {
        . = ALIGN(4);
        KEEP(*(.vectors))          /* must be first: boot2 vectors into here  */
        *(.text*)                   /* Reset_Handler + Default_Handler live    */
                                    /* here in .text*                          */
        *(.text*)
        *(.text.*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* ARM exception unwinding tables (harmless even if unused). */
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    __exidx_start = .;
    .ARM.exidx  : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH
    __exidx_end = .;

    . = ALIGN(4);

    /* ---- .data: VMA in RAM, LMA in FLASH right after .text/.rodata ---- */
    .data : {
        . = ALIGN(4);
        __data_start__ = .;
        *(.data*)
        *(.data.*)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM AT > FLASH
    __etext = LOADADDR(.data);

    /* ---- .bss: zero-initialised, lives only in RAM ---- */
    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* ---- Stack: top of the 256K SRAM. Full-descending; grows down. ---- */
    . = ALIGN(8);
    __StackTop    = ORIGIN(RAM) + LENGTH(RAM);   /* 0x20040000 */
    __StackLimit  = __StackTop;
    PROVIDE(__stack = __StackTop);

    /DISCARD/ : {
        *(.comment)
        *(.ARM.attributes)
    }
}
