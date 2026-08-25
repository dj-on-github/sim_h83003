/* Layout of the application image.
 *
 * The application does not own the CPU's vector table -- the boot ROM does,
 * at address 0. What it owns is the table at H'200000 that the boot ROM's
 * trampolines fetch handlers from, so this is both an entry table and, for
 * the interrupts the application actually takes, its vector table.
 *
 *   H'200000  the table: 64 longwords
 *   H'200100  the identity block the configuration screen displays
 *   H'200200  code
 *
 * Everything above the image is left alone by mergeapp: the code region
 * still holds the original above whatever has been rebuilt, and H'280000
 * upwards is 1.4M of pattern data that is not code at all.
 */
OUTPUT_ARCH(h8300h)
ENTRY(_start)

MEMORY {
    apptable (r)  : ORIGIN = 0x200000, LENGTH = 0x100
    appinfo  (r)  : ORIGIN = 0x200100, LENGTH = 0x100
    code    (rx)  : ORIGIN = 0x200200, LENGTH = 0x50E00
}

SECTIONS {
    .apptable : { KEEP(*(.apptable)) } > apptable
    .appinfo  : { KEEP(*(.appinfo))  } > appinfo

    /* --gc-sections is here for libgcc's sake: it drops the arithmetic the
       application does not use. The application's own objects are KEEPt,
       because most of what they hold is reached from the original's jump
       tables rather than from anything the linker can see. */
    .text : {
        *(.text.start)
        KEEP(vectors.o(.text .text.*))
        KEEP(app_*.o(.text .text.*))
        KEEP(app_*.o(.rodata .rodata.*))
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > code

    /* Writable data lives in RAM. Nothing is copied there yet: the startup
       code clears what it needs, as the original does. */
    .data : { *(.data .data.*) } > code
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } > code

    /DISCARD/ : { *(.comment) *(.note*) }
}
