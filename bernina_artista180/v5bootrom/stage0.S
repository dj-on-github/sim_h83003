/* The permanent part of the v5 boot ROM.
 *
 * Everything in this file is written into the boot flash once and never
 * again. It occupies H'000000-H'0007FF: the vector table in page 0, the
 * interrupt trampolines in pages 1-3, and the stage-0 loader and its thunks
 * in the pages after them.
 *
 * Above it sit two image slots. An update writes the inactive one and
 * nothing else, so there is no moment at which the machine has no bootable
 * ROM: if power fails part way, the half-written image fails its checksum
 * and stage-0 falls back to the one that was already there.
 *
 * That is the whole point of the arrangement. In the stock ROM the reset
 * vector and the code live in the same pages, so any update has to rewrite
 * the page the machine boots from.
 */

    .h8300h

    .equ APP_TABLE,     0x200000
    .equ SCRATCH,       0xFFFD18    /* the trampolines' scratch, as before */
    .equ JMP_ABS24,     0x5A

/* Two longwords at the bottom of the on-chip RAM. Booting the original
 * application touches nothing below H'FFFD1A -- 1546 bytes of the 2K are
 * never written -- so these are clear of both the stack, which grows down
 * from H'FFFDF4, and the state block the boot ROM and application share. */
    .equ ACTIVE_BASE,   0xFFF710    /* base of the image stage-0 chose */
    .equ THUNK_SCRATCH, 0xFFF714    /* the thunks' own, so an interrupt
                                       arriving mid-thunk cannot steal it */
    .equ STAGE0_ENTRY,  0xFFFD0C    /* where to re-enter the loader.
                                       'X' needs this: an image has no symbol
                                       for stage-0, and reading slot 0 of the
                                       vector table instead means reading
                                       address 0, which the compiler is
                                       entitled to treat as unreachable and
                                       delete. Publishing it here is both
                                       well defined and one indirection
                                       cheaper. */

    .equ IMAGE_A,       0x000800
    .equ IMAGE_B,       0x004000
    .equ SLOT_END,      0x008000

/* Image header, at the base of each slot. */
    .equ H_MAGIC,       0x00        /* "B180" */
    .equ H_GEN,         0x04        /* generation; higher wins, 0 never does */
    .equ H_LEN,         0x08        /* bytes covered by the checksum */
    .equ H_SUM,         0x0C        /* their plain byte sum */
    .equ H_ENTRY,       0x10
    .equ H_EXPORTS,     0x14        /* seven longwords: slots 1-6 and 22 */
    .equ H_SIZE,        0x40

/* ---- vector table, page 0 -- written once, never rewritten ---- */
    .section .vectors, "a"
    .long   _stage0             /*  0 RESET -> the loader, not an image */
    .long   thunk_1             /*  1 the serial service */
    .long   thunk_2             /*  2 the delay loop */
    .long   thunk_3             /*  3 flash write */
    .long   thunk_4             /*  4 the serial bridge */
    .long   thunk_5             /*  5 select SCI0 */
    .long   thunk_6             /*  6 is SCI0 selected */
    .irp v, 7,8,9,10,11,12,13,14,15,16,17,18,19,20,21
    .long   tramp_\v
    .endr
    .long   thunk_22            /* 22 the flag 'Y' raises */
    .long   0
    .irp v, 24,25,26
    .long   tramp_\v
    .endr
    .long   0
    .irp v, 28,29,30
    .long   tramp_\v
    .endr
    .long   0
    .irp v, 32,33,34
    .long   tramp_\v
    .endr
    .long   0
    .irp v, 36,37,38
    .long   tramp_\v
    .endr
    .long   0
    .irp v, 40,41,42
    .long   tramp_\v
    .endr
    .long   0
    .irp v, 44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60
    .long   tramp_\v
    .endr
    .long   0, 0, 0

/* ---- the trampolines, unchanged from the stock ROM ----
 * These need no thunking: each one fetches its handler from the
 * application's own table at H'200000, so it does not depend on which image
 * is active, which is why V2 and V3 carry this block byte for byte alike.
 */
    .macro TRAMP v
    .global tramp_\v
tramp_\v:
    .set slot, APP_TABLE + \v * 4
    push.l  er0
    mov.l   @slot:32, er0
    bra     _dispatch:16
    .endm

    .section .trampolines, "ax"
    .irp v, 7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,24,25,26,28,29,30,32,33,34,36,37,38,40,41,42,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60
    TRAMP \v
    .endr

    .global _dispatch
_dispatch:
    mov.l   er0, @SCRATCH:32
    mov.b   #JMP_ABS24, r0h
    mov.b   r0h, @SCRATCH:32
    pop.l   er0
    jmp     @SCRATCH:24

/* ---- stage 0 and the thunks ---- */
    .section .stage0, "ax"

/* One vector-slot thunk. The application calls these through JSR @@aa:8
 * exactly as before; they add one indirection through the active image's
 * export table so that page 0 never has to name an image-relative address.
 *
 * ER0 is the only register touched and it is restored before the target
 * runs, so the ROM calling convention -- first argument in ER6, the rest on
 * the stack, result in R6 -- passes through untouched.
 */
    .macro THUNK n, off
    .global thunk_\n
thunk_\n:
    push.l  er0
    mov.l   @ACTIVE_BASE:32, er0
    mov.l   @(\off:16, er0), er0
    bra     _thunk_dispatch:16
    .endm

    THUNK 1,  H_EXPORTS
    THUNK 2,  H_EXPORTS+4
    THUNK 3,  H_EXPORTS+8
    THUNK 4,  H_EXPORTS+12
    THUNK 5,  H_EXPORTS+16
    THUNK 6,  H_EXPORTS+20
    THUNK 22, H_EXPORTS+24

_thunk_dispatch:
    mov.l   er0, @THUNK_SCRATCH:32
    mov.b   #JMP_ABS24, r0h
    mov.b   r0h, @THUNK_SCRATCH:32
    pop.l   er0
    jmp     @THUNK_SCRATCH:24

/* Both calls below say ":16" explicitly. BSR with an 8-bit displacement
 * reaches -128..+127, and gas does not diagnose an overflow -- it truncates,
 * so the call silently lands 256 bytes past where it should. That is what
 * adding fourteen bytes to _stage0 did the first time: the second call to
 * _validate went to H'0005BA instead of H'0004BA, image B failed to
 * validate, the machine booted image A, and the only symptom was a different
 * screen. Nothing warned.
 */

/* Is the image at ER1 whole? Returns ER0 = 1 or 0, and leaves ER1 alone.
 * A slot that has never been written reads as all zeroes or all H'FF, and
 * fails the magic; a slot written only part way fails the sum. */
_validate:
    mov.l   @er1, er0
    mov.l   #0x42313830, er2        /* "B180" */
    cmp.l   er2, er0
    bne     _bad
    mov.l   @(H_LEN:16, er1), er3
    mov.l   er3, er3
    beq     _bad                    /* an empty image is not a valid one */
    mov.l   #(SLOT_END - IMAGE_A - H_SIZE), er2
    cmp.l   er2, er3
    bhi     _bad                    /* longer than any slot: refuse it */
    mov.l   er1, er4
    add.l   #H_SIZE, er4
    sub.l   er2, er2                /* running sum */
1:  mov.b   @er4, r0l
    extu.w  r0
    extu.l  er0
    add.l   er0, er2
    inc.l   #1, er4
    dec.l   #1, er3
    bne     1b
    mov.l   @(H_SUM:16, er1), er0
    cmp.l   er0, er2
    bne     _bad
    mov.l   #1, er0
    rts
_bad:
    sub.l   er0, er0
    rts

/* The loader. Picks the valid image with the higher generation, publishes
 * its base for the thunks, and jumps to it. */
    .global _stage0
_stage0:
    mov.l   #0xFFFDF4, er7          /* the stack the stock ROM uses */
    mov.l   #_stage0, er0
    mov.l   er0, @STAGE0_ENTRY:32   /* so 'X' can come back here */
    sub.l   er5, er5                /* best base so far: none */
    sub.l   er6, er6                /* and its generation */

    mov.l   #IMAGE_A, er1
    bsr     _validate:16
    mov.l   er0, er0
    beq     1f
    mov.l   @(H_GEN:16, er1), er2
    cmp.l   er6, er2
    bls     1f
    mov.l   er2, er6
    mov.l   er1, er5
1:
    mov.l   #IMAGE_B, er1
    bsr     _validate:16
    mov.l   er0, er0
    beq     2f
    mov.l   @(H_GEN:16, er1), er2
    cmp.l   er6, er2
    bls     2f
    mov.l   er2, er6
    mov.l   er1, er5
2:
    mov.l   er5, er5
    beq     _no_image
    mov.l   er5, @ACTIVE_BASE:32
    mov.l   @(H_ENTRY:16, er5), er0
    jmp     @er0

/* Both slots unreadable. This cannot happen through an interrupted update
 * -- that is the property the whole layout exists to give -- so there is
 * nothing useful to do but stop somewhere identifiable. */
_no_image:
    bra     _no_image
