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
    /* No .text.* wildcard here on purpose: it would swallow .text.identify
       and the explicit address further down would then place nothing. The
       compiler is not asked for function sections, so everything that is not
       one of the two pinned entries lands in plain .text. */
    .text : {
        KEEP(*(.text.entry))
        *(.text)
        *(.rodata*)
    } > rom

    /* The identify entry, at a fixed address of its own so the host can
       point H'200004 at either it or the updater without knowing how big
       the other one came out. H'1000 in is clear of both. */
    .text.identify 0x3E9000 : { KEEP(*(.text.identify)) } > rom
    .data : { *(.data*) } > rom
    .bss  : { *(.bss*) *(COMMON) } > ram
    /DISCARD/ : { *(.comment) *(.debug*) }
}
