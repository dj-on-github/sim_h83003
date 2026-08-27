/* Vector table and interrupt trampolines.
 *
 * The table is not just interrupt vectors. Slots 1-6 and 22 hold pointers to
 * boot ROM routines, which the application calls through JSR @@aa:8 -- a
 * memory-indirect call that takes its target from the low 256 bytes. Leaving
 * those slots at zero sends the application to address 0 the first time it
 * uses one.
 *
 * The trampolines cannot be C. Each fetches the real handler's address from
 * the application's table at H'200000, writes it to on-chip RAM, then
 * overwrites that longword's top byte with H'5A -- the opcode for JMP @aa:24
 * -- so the four bytes become a jump instruction, which it then jumps to.
 * The point of the trick is that ER0 has to be restored before the handler
 * runs, so the target cannot live in a register.
 */

    .h8300h

    .equ APP_TABLE, 0x200000
    .equ SCRATCH,   0xFFFD18
    .equ JMP_ABS24, 0x5A

/* One trampoline. \v is the vector number. */
    .macro TRAMP v
    .global tramp_\v
tramp_\v:
    .set slot, APP_TABLE + \v * 4
    push.l  er0
    mov.l   @slot:32, er0
    bra     _dispatch:16
    .endm

/* ---- vector table, at address 0 ---- */
    .section .vectors, "a"
    .long   _start              /*  0 RESET  */
    .long   _serial_service     /*  1 -> H'001090, takes and returns nothing */
    .long   vec2_delay          /*  2 -> H'000426, the delay loop */
    .long   vec3_flash_write    /*  3 -> H'000ADC */
    .long   vec4_bridge         /*  4 -> H'002122 */
    .long   vec5_select_sci0    /*  5 -> H'0022FE */
    .long   vec6_sci0_selected  /*  6 -> H'002312 */
    .irp v, 7,8,9,10,11,12,13,14,15,16,17,18,19,20,21
    .long   tramp_\v
    .endr
    .long   vec22_confirmed     /* 22 -> H'002328 */
    .long   0                   /* 23 reserved */
    .irp v, 24,25,26
    .long   tramp_\v
    .endr
    .long   0                   /* 27 */
    .irp v, 28,29,30
    .long   tramp_\v
    .endr
    .long   0                   /* 31 */
    .irp v, 32,33,34
    .long   tramp_\v
    .endr
    .long   0                   /* 35 */
    .irp v, 36,37,38
    .long   tramp_\v
    .endr
    .long   0                   /* 39 */
    .irp v, 40,41,42
    .long   tramp_\v
    .endr
    .long   0                   /* 43 */
    .irp v, 44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60
    .long   tramp_\v
    .endr
    .long   0, 0, 0             /* 61-63 */

/* ---- the trampolines ---- */
    .section .trampolines, "ax"
    .irp v, 7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,24,25,26,28,29,30,32,33,34,36,37,38,40,41,42,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60
    TRAMP \v
    .endr

    .global _dispatch
_dispatch:
    mov.l   er0, @SCRATCH:32        /* the handler address, 4 bytes */
    mov.b   #JMP_ABS24, r0h
    mov.b   r0h, @SCRATCH:32        /* top byte becomes the opcode */
    pop.l   er0                     /* the handler must see it untouched */
    jmp     @SCRATCH:24             /* run the instruction just built */

/* ---- vector-slot entry shims ----
 * The application is the untouched original binary, so it calls these the
 * original ROM's way: first argument in ER6, any others on the stack, result
 * back in R6. The compiler passes arguments in ER0/ER1/ER2 and returns in
 * R0. These few instructions sit in between, so the rebuilt boot ROM drops
 * straight in under an application that was never recompiled.
 *
 * Slot 1 needs no shim: serial_service takes nothing and returns nothing.
 */
    .section .text.shims, "ax"

    .global vec2_delay
vec2_delay:                         /* void delay(u16 units) */
    mov.w   r6, r0
    jmp     @_delay

    .global vec3_flash_write
vec3_flash_write:                   /* void flash_write_block(src,dst,len) */
    mov.l   er6, er0                /* source, the register argument */
    mov.l   @(4:16,er7), er1        /* destination, past the return address */
    mov.l   @(8:16,er7), er2        /* length */
    jsr     @_flash_write_block
    mov.w   r0, r6
    rts

    .global vec4_bridge
vec4_bridge:                        /* int serial_bridge(void) */
    jsr     @_serial_bridge
    mov.w   r0, r6
    rts

    .global vec5_select_sci0
vec5_select_sci0:                   /* int select_sci0(void) */
    jsr     @_select_sci0
    mov.w   r0, r6
    rts

    .global vec6_sci0_selected
vec6_sci0_selected:                 /* int sci0_is_selected(void) */
    jsr     @_sci0_is_selected
    mov.w   r0, r6
    rts

    .global vec22_confirmed
vec22_confirmed:                    /* int host_confirmed(void) */
    jsr     @_host_confirmed
    mov.w   r0, r6
    rts

/* ---- reset ----
 * The stack pointer must exist before any C runs. H'FFFDF4 is where the
 * original puts it: near the top of the 2K on-chip RAM, below the state
 * block that the boot ROM and application share.
 */
    .section .text.start, "ax"
    .global _start
_start:
    mov.l   #0xFFFDF4, er7
    jsr     @_boot_main
1:  bra     1b
