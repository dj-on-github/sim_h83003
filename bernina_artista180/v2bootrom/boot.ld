/* Layout of the Bernina artista 180 boot ROM.
 *
 * Only two addresses are fixed by the hardware or by the application:
 *   - the vector table must start at 0, because the CPU fetches the reset
 *     vector from there;
 *   - the trampolines must be reachable from it, and are placed at H'000100
 *     immediately after the table, as in the original.
 * Everything else is the linker's choice. The original put its reset code at
 * H'000400; nothing depends on that, since the reset vector names it.
 */
MEMORY
{
    rom (rx)  : ORIGIN = 0x000000, LENGTH = 12K       /* the boot ROM device */
    ram (rwx) : ORIGIN = 0xFFF710, LENGTH = 2K - 268  /* on-chip RAM below
                                                         the state block */
}

ENTRY(_start)

SECTIONS
{
    .vectors 0x000000 : { KEEP(*(.vectors)) } > rom
    .trampolines 0x000100 : { KEEP(*(.trampolines)) } > rom

    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    } > rom

    .data : { *(.data*) } > rom
    .bss  : { *(.bss*) *(COMMON) } > ram

    /DISCARD/ : { *(.comment) *(.debug*) }

    __rom_end = .;
}
