/* The updater lives in the spare space at the top of the application flash.
 * The 2M device is used up to H'3E78C2, so H'3E8000 is clear and page
 * aligned. The payload it programs sits above it at H'3EA000. */
MEMORY
{
    rom (rx)  : ORIGIN = 0x3E8000, LENGTH = 0x2000
    ram (rwx) : ORIGIN = 0xFFF7C0, LENGTH = 0x30
}
ENTRY(_updater_main)
SECTIONS
{
    /* .text.entry first: updater_main must be at ORIGIN, because that is
       the address 'G' is pointed at. */
    .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } > rom
    .data : { *(.data*) } > rom
    .bss  : { *(.bss*) *(COMMON) } > ram
    /DISCARD/ : { *(.comment) *(.debug*) }
}
