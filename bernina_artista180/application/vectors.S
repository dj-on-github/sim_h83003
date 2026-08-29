/* The application's entry table and identity block.
 *
 * The boot ROM keeps the CPU's real vector table at address 0. Most of its
 * slots are trampolines that read the handler's address out of this table
 * and jump to it, so an entry here is what makes the application see an
 * interrupt at all. Slots the original leaves as H'FFFFFFFF are ones it does
 * not handle; the trampoline for those is never reached because the
 * interrupt is never enabled.
 *
 * Slot 0 is the entry point, and the boot ROM checks that its top byte is
 * zero before handing over -- that is how it decides an application is
 * present at all. Slot 1 is what the boot ROM's 'G' command jumps to.
 */
    .h8300h

    .section .apptable, "a"
    .long   _start              /*  0  entry; the boot ROM checks this  */
    .long   _alt_entry          /*  1  the 'G' command's target         */
    .long   0                   /*  2  */
    .long   0                   /*  3  */
    .rept   20
    .long   0xFFFFFFFF          /*  4-23 not handled                    */
    .endr
    .long   _isr_24             /* 24  */
    .rept   3
    .long   0xFFFFFFFF          /* 25-27 */
    .endr
    .long   _isr_28             /* 28  */
    .rept   3
    .long   0xFFFFFFFF          /* 29-31 */
    .endr
    .long   _isr_32             /* 32  */
    .rept   3
    .long   0xFFFFFFFF          /* 33-35 */
    .endr
    .long   _isr_36             /* 36  */
    .long   0xFFFFFFFF          /* 37  */
    .long   _isr_38             /* 38  */
    .long   0xFFFFFFFF          /* 39  */
    .long   _isr_40             /* 40  */
    .long   _isr_41             /* 41  */
    .rept   10
    .long   0xFFFFFFFF          /* 42-51 */
    .endr
    .long   _isr_sci0_eri       /* 52  */
    .long   _isr_sci0_rxi       /* 53  */
    .long   _isr_sci0_txi       /* 54  */
    .rept   9
    .long   0xFFFFFFFF          /* 55-63 */
    .endr

/* The identity block, at H'200100. The configuration screen reads these
 * strings out of here, so they are placed rather than merely linked. */
    .section .appinfo, "a"
appinfo_start:
    .asciz  "NMMV05.01"
    .asciz  "English     "
    .asciz  "Deadhat Hackings  "
    .asciz  "Aug 26 "
    /* The fields are fixed width, trailing spaces and all, and the rest of
       the block is erased flash rather than zeros. Both matter: this is
       compared against the original byte for byte. */
    .fill   0x100 - (. - appinfo_start), 1, 0xFF

/* ---- entry ----
 * The original sets a temporary stack, runs a memory check, then moves the
 * stack to H'120000 in external RAM before doing anything else. The stack
 * has to exist before any C runs, so this much cannot be C.
 */
    .section .text.start, "ax"
    .global _start
_start:
    mov.l   #0xFFFFFF00, er7        /* temporary stack, on-chip RAM */
    jsr     @_memory_check
    mov.l   #0x00120000, er7        /* the real stack, external RAM */
    mov.b   r0l, r0l
    beq     1f
    jsr     @_cold_start            /* only when the check says so */
1:  jsr     @_app_init
    jmp     @_app_main
