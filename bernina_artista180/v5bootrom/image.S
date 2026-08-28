/* The replaceable half of the v5 boot ROM: a header, an entry point and the
 * seven ABI shims. No vector table and no trampolines -- those are permanent
 * and live in stage0.S.
 *
 * IMAGE_GEN is passed in on the command line. Stage-0 picks the valid image
 * with the higher generation, so an update is "write the inactive slot with
 * a number one larger", and generation 0 is never chosen.
 */

    .h8300h

    .section .header, "a"
    .ascii  "B180"
    .long   IMAGE_GEN
    .long   0                       /* length,   filled in by mkimage.py */
    .long   0                       /* checksum, likewise */
    .long   _image_start
    .long   _serial_service         /* slot 1 */
    .long   vec2_delay              /* slot 2 */
    .long   vec3_flash_write        /* slot 3 */
    .long   vec4_bridge             /* slot 4 */
    .long   vec5_select_sci0        /* slot 5 */
    .long   vec6_sci0_selected      /* slot 6 */
    .long   vec22_confirmed         /* slot 22 */
    .space  16                      /* pad the header to 64 bytes */

/* Stage-0 has already set the stack, but an image that sets its own can be
 * jumped to from anywhere, which is what the updater's verify pass does. */
    .section .text.start, "ax"
    .global _image_start
_image_start:
    mov.l   #0xFFFDF4, er7
    jsr     @_boot_main
1:  bra     1b

/* ---- vector-slot entry shims, unchanged from the stock ROM ---- */
    .section .text.shims, "ax"

    .global vec2_delay
vec2_delay:
    mov.w   r6, r0
    jmp     @_delay

    .global vec3_flash_write
vec3_flash_write:
    mov.l   er6, er0
    mov.l   @(4:16,er7), er1
    mov.l   @(8:16,er7), er2
    jsr     @_flash_write_block
    mov.w   r0, r6
    rts

    .global vec4_bridge
vec4_bridge:
    jsr     @_serial_bridge
    mov.w   r0, r6
    rts

    .global vec5_select_sci0
vec5_select_sci0:
    jsr     @_select_sci0
    mov.w   r0, r6
    rts

    .global vec6_sci0_selected
vec6_sci0_selected:
    jsr     @_sci0_is_selected
    mov.w   r0, r6
    rts

    .global vec22_confirmed
vec22_confirmed:
    jsr     @_host_confirmed
    mov.w   r0, r6
    rts
