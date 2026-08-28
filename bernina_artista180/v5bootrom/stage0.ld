/* The permanent block: H'000000-H'0007FF of the boot flash.
 * Page 0 is the vector table, pages 1-3 the trampolines, the loader. Nothing here is ever rewritten after the first install. */
MEMORY { rom (rx) : ORIGIN = 0x000000, LENGTH = 0x800 }
ENTRY(_stage0)
SECTIONS
{
    .vectors     0x000000 : { KEEP(*(.vectors)) } > rom
    .trampolines 0x000100 : { KEEP(*(.trampolines)) } > rom
    /* The 47 trampolines run a few bytes past H'000400, so the loader
       follows them rather than being pinned to a page boundary. The
       permanent block is pages 0-7 either way. */
    .stage0               : { KEEP(*(.stage0)) } > rom
    /DISCARD/ : { *(.comment) *(.debug*) }
}
