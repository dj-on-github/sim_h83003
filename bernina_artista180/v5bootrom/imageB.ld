MEMORY
{
    rom (rx) : ORIGIN = 0x004000, LENGTH = 0x4000
    ram (rwx) : ORIGIN = 0xFFF718, LENGTH = 0x5F0
}
ENTRY(_image_start)
SECTIONS
{
    .header : { KEEP(*(.header)) } > rom
    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    } > rom
    .data : { *(.data*) } > rom
    .bss : { *(.bss*) *(COMMON) } > ram
    /DISCARD/ : { *(.comment) *(.debug*) }
}
