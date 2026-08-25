/* The artista 180 application, rebuilt in C: the two main loops, the
 * timebase, and the five interrupt handlers.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* H'20BEE2. */
void main_loop_service(void)
{
    loop_tick();
    service_dispatch();
}

/* H'2086B6. */
void main_loop_normal(void)
{
    embroidery_service();
    key_and_diag();
    screen_dispatch();
    service_tick();          /* H'208698 */
}

void app_init(void)
{
    machine_init();

    if (SERVICE_MODE_FLAG & 0x80) {
        for (;;) {
            main_loop_service();
        }
    }
    for (;;) {
        main_loop_normal();
    }
}

/* H'2007B0. Not the main loop -- that is app_init, which never returns.
 * This is the trap the entry jumps to if it ever does, and in the original
 * it is a single instruction branching to itself. Reproduced exactly. */
void app_main(void)
{
    for (;;) {
    }
}

/* The 'G' command's target, H'2003A4. The original sends an 'O' and carries
 * on, which is how the download protocol's caller knows the handover
 * happened. Kept, because the boot ROM's tests check for it. */
void alt_entry(void)
{
    while (!(SSR1 & SSR_TDRE)) { }
    TDR1 = 'O';
    SSR1 = (u8)~SSR_TDRE;
    app_main();
}

/* The two output latches. Both are write-only and both carry more than one
 * thing, so every change is made on the shadow. The pair of writes is not a
 * mistake: the coil enables come up one bit at a time, and the intermediate
 * value goes out to the latch as well. */
static void latch_a_on(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD38UL) | first);
    REG8(0x0A0000UL) = v;
    v = (u8)(v | second);
    REG8(0x0A0000UL) = v;
    REG8(0xFFFD38UL) = v;
}

static void latch_a_off(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD38UL) & ~first);
    REG8(0x0A0000UL) = v;
    v = (u8)(v & ~second);
    REG8(0x0A0000UL) = v;
    REG8(0xFFFD38UL) = v;
}

static void latch_a_bit(u8 mask, int on)
{
    const u8 v = on ? (u8)(REG8(0xFFFD38UL) | mask)
                    : (u8)(REG8(0xFFFD38UL) & ~mask);
    REG8(0x0A0000UL) = v;
    REG8(0xFFFD38UL) = v;
}

static void latch_b_on(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD39UL) | first);
    REG8(0x0C0000UL) = v;
    v = (u8)(v | second);
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

static void latch_b_off(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD39UL) & ~first);
    REG8(0x0C0000UL) = v;
    v = (u8)(v & ~second);
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

/* ---- the machine's timebase and its steppers ---------------------------
 * Seven timer interrupt handlers, and the forty routines under them. Nothing
 * in the application waits on anything without these: H'114DDA, H'114DDE,
 * H'114DE0, H'114DE2 and H'114DE4 are all counted here, and so is every step
 * of every stepper motor.
 *
 * The phase patterns live as data in the code region, from H'250804 to
 * H'250AA0, and are read where they lie -- like the pattern data, they are
 * above the rebuilt image and mergeapp leaves them alone.
 *
 * Two output latches carry the coil drives: H'0A0000, shadowed at H'FFFD38,
 * and H'0C0000 at H'FFFD39. Both are write-only, so a change is always made
 * on the shadow and then written out.
 */

/* ---- the four steppers, one phase at a time ---------------------------
 * Each motor has a phase index counted 0..7 (0..3 for the fourth), a
 * position counter, and a table of coil patterns. The "_back" form steps the
 * index down and the "_on" form steps it up; which one runs is a direction
 * bit in the motor's flag byte.
 */

/* H'20C2C8. Motor 1 -- the needle position -- in the high nibble of the
 * TPC output at H'FFFFA5. */
void motor_1_step(void)
{
    u8 phase;

    if (REG8(0x11A835UL) & 0x01) {
        REG8(0x11A83AUL) = (u8)(REG8(0x11A83AUL) + 1);
        phase = REG8(0x11A838UL);
        REG8(0x11A838UL) = (u8)((phase == 0) ? 0x07 : (phase - 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x25080CUL + REG8(0x11A838UL)));
    } else {
        REG8(0x11A83AUL) = (u8)(REG8(0x11A83AUL) - 1);
        phase = REG8(0x11A838UL);
        REG8(0x11A838UL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x250804UL + REG8(0x11A838UL)));
    }
}

/* H'20C550. Motor 1 forward only, from the third table. */
void motor_1_step_home(void)
{
    const u8 phase = REG8(0x11A838UL);

    REG8(0x11A838UL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
    REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
    REG8(0xFFFFA5UL) =
        (u8)(REG8(0xFFFFA5UL) | REG8(0x250814UL + REG8(0x11A838UL)));
}

/* H'20C36C. Motor 2 -- the feed -- in the low nibble of the same latch. Its
 * position counter moves by two, not one. */
void motor_2_step(void)
{
    u8 phase;

    if (REG8(0x11A835UL) & 0x08) {
        REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) + 2);
        phase = REG8(0x11A83EUL);
        REG8(0x11A83EUL) = (u8)((phase == 0) ? 0x07 : (phase - 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x250924UL + REG8(0x11A83EUL)));
    } else {
        REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) - 2);
        phase = REG8(0x11A83EUL);
        REG8(0x11A83EUL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x25091CUL + REG8(0x11A83EUL)));
    }
}

/* H'20C596. Motor 2 forward only. */
void motor_2_step_home(void)
{
    const u8 phase = REG8(0x11A83EUL);

    REG8(0x11A83EUL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
    REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
    REG8(0xFFFFA5UL) =
        (u8)(REG8(0xFFFFA5UL) | REG8(0x25092CUL + REG8(0x11A83EUL)));
}

/* H'20C410. Motor 3 -- the hook width -- has the whole of H'FFFFA4. */
void motor_3_step(void)
{
    u8 phase;

    if (REG8(0x11A836UL) & 0x01) {
        REG8(0x11A846UL) = (u8)(REG8(0x11A846UL) + 1);
        phase = REG8(0x11A844UL);
        phase = (u8)((phase == 0) ? 0x07 : (phase - 1));
        REG8(0x11A844UL) = phase;
        REG8(0xFFFFA4UL) = REG8(0x250A24UL + phase);
    } else {
        REG8(0x11A846UL) = (u8)(REG8(0x11A846UL) - 1);
        phase = REG8(0x11A844UL);
        phase = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
        REG8(0x11A844UL) = phase;
        REG8(0xFFFFA4UL) = REG8(0x250A1CUL + phase);
    }
}

/* H'20C5DC. */
void motor_3_step_home(void)
{
    u8 phase = REG8(0x11A844UL);

    phase = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
    REG8(0x11A844UL) = phase;
    REG8(0xFFFFA4UL) = REG8(0x250A2CUL + phase);
}

/* H'20C498. Motor 4 -- the hook position -- is four phases, not eight, and
 * its drive is the low six bits of the H'0C0000 latch. */
void motor_4_step(void)
{
    u8 phase, v;

    if (REG8(0x11A836UL) & 0x08) {
        REG8(0x11A84BUL) = (u8)(REG8(0x11A84BUL) + 1);
        phase = REG8(0x11A849UL);
        REG8(0x11A849UL) = (u8)((phase == 0) ? 0x03 : (phase - 1));
        v = (u8)(REG8(0xFFFD39UL) & 0xC0);
        v = (u8)(v | REG8(0x250A90UL + REG8(0x11A849UL)));
    } else {
        REG8(0x11A84BUL) = (u8)(REG8(0x11A84BUL) - 1);
        phase = REG8(0x11A849UL);
        REG8(0x11A849UL) = (u8)((phase >= 0x03) ? 0x00 : (phase + 1));
        v = (u8)(REG8(0xFFFD39UL) & 0xC0);
        v = (u8)(v | REG8(0x250A8CUL + REG8(0x11A849UL)));
    }
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

/* H'20C614. */
void motor_4_step_home(void)
{
    const u8 phase = REG8(0x11A849UL);
    u8 v;

    REG8(0x11A849UL) = (u8)((phase >= 0x03) ? 0x00 : (phase + 1));
    v = (u8)(REG8(0xFFFD39UL) & 0xC0);
    v = (u8)(v | REG8(0x250A94UL + REG8(0x11A849UL)));
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

/* ---- ITU0: motors 1 and 2 ---------------------------------------------
 * H'20C664. One timer, two motors, taken in turn: while bit 7 of H'11A835 is
 * clear the interrupt belongs to the needle-position motor, and once that
 * has reached its target the bit goes up and the same interrupt drives the
 * feed motor instead.
 *
 * Both run the same shape. A move is split in half -- H'11A83B/H'11A83C for
 * one, H'11A841/H'11A842 for the other -- and the phase index walks up
 * through an acceleration table for the first half and back down through a
 * deceleration table for the second, so the motor ramps up and down inside
 * one move. A move shorter than the ramp sets a flag that holds the index
 * still. GRB0 at H'FFFF6A is the interval to the next step.
 *
 * Targets H'FC to H'FF are not positions but park commands: hold, coast,
 * and two fixed patterns.
 */
static u16 phase_word(u32 table, u8 phase)
{
    return REG16(table + (u32)(long)(short)(u16)((u16)phase << 1));
}

void isr_motors_12_body(void)
{
    u8 pos, tgt, d, n, v;

    REG8(0xFFFF67UL) &= (u8)~0x01;

    if (!(REG8(0x11A835UL) & 0x80)) {
        /* ---- the needle-position motor ---- */
        switch (REG8(0xFFFED2UL)) {
        case 0x01:
            pos = REG8(0x11A83AUL);
            tgt = REG8(0xFFFED3UL);
            if (pos == tgt) {
                REG8(0x11A835UL) |= 0x80;
                REG16(0xFFFF6AUL) = 0x2AF8;
            } else if (tgt == 0xFF) {
                REG8(0x11A835UL) |= 0x80;
                latch_a_on(0x20, 0x40);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25081EUL));
            } else if (tgt == 0xFE) {
                /* coast */
            } else if (tgt == 0xFD) {
                REG8(0x11A835UL) |= 0x80;
                latch_a_off(0x20, 0x40);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25081DUL));
            } else if (tgt == 0xFC) {
                REG8(0x11A835UL) |= 0x80;
                latch_a_off(0x20, 0x40);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25081CUL));
            } else {
                if (pos > tgt) {
                    REG8(0x11A835UL) &= (u8)~0x01;
                    REG8(0x11A837UL) = (u8)(REG8(0x11A83AUL) - tgt);
                } else {
                    REG8(0x11A835UL) |= 0x01;
                    REG8(0x11A837UL) = (u8)(tgt - REG8(0x11A83AUL));
                }

                d = REG8(0x11A837UL);
                if (d < 0x0E) {
                    REG8(0x11A835UL) |= 0x04;
                } else if (d <= 0x20) {
                    REG8(0x11A83DUL) = 0x01;
                }

                d = REG8(0x11A837UL);
                if (d == 0x01)      REG8(0xFFFED2UL) = 0x03;
                else if (d == 0x02) REG8(0xFFFED2UL) = 0x02;
                else                REG8(0xFFFED2UL) = 0x02;

                d = REG8(0x11A837UL);
                REG8(0x11A83BUL) = (u8)(d >> 1);
                REG8(0x11A83CUL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
                REG8(0x11A839UL) = 0x01;
                motor_1_step();
                REG16(0xFFFF6AUL) = (u16)(REG16(0xFFFF68UL) + 0x0CF6);
                latch_a_off(0x20, 0x40);
            }
            break;

        case 0x02:
            REG16(0xFFFF6AUL) = phase_word(
                (REG8(0x11A83DUL) == 0) ? 0x250820UL : 0x2508A0UL,
                REG8(0x11A839UL));
            motor_1_step();
            REG8(0x11A83BUL) = (u8)(REG8(0x11A83BUL) - 1);
            if (REG8(0x11A83BUL) != 0) {
                if (!(REG8(0x11A835UL) & 0x04)) {
                    REG8(0x11A839UL) = (u8)(REG8(0x11A839UL) + 1);
                }
            } else {
                REG8(0xFFFED2UL) = 0x03;
            }
            break;

        case 0x03:
            REG8(0x11A83CUL) = (u8)(REG8(0x11A83CUL) - 1);
            if (REG8(0x11A83CUL) != 0) {
                REG16(0xFFFF6AUL) = phase_word(
                    (REG8(0x11A83DUL) == 0) ? 0x25085EUL : 0x2508DEUL,
                    REG8(0x11A839UL));
                motor_1_step();
                if (!(REG8(0x11A835UL) & 0x04)) {
                    REG8(0x11A839UL) = (u8)(REG8(0x11A839UL) - 1);
                }
            } else {
                REG16(0xFFFF6AUL) = REG16(0x250860UL);
                REG8(0xFFFED2UL) = 0x04;
            }
            break;

        case 0x04:
            REG16(0xFFFF6AUL) = REG16(0x25085EUL);
            REG8(0x11A83DUL) = 0x00;
            REG8(0x11A835UL) &= (u8)~0x04;
            latch_a_on(0x20, 0x40);
            REG8(0xFFFED2UL) = 0x01;
            break;

        case 0x05:
            REG16(0xFFFF6AUL) = 0x2AF8;
            REG8(0x11A83CUL) = 0x00;
            REG8(0x11A83BUL) = 0x00;
            REG8(0x11A83AUL) = 0x00;
            REG8(0x11A835UL) = 0x00;
            REG8(0x11A839UL) = 0x00;
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
            REG8(0x11A838UL) = 0x00;
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25080CUL));
            latch_a_on(0x20, 0x40);
            REG8(0xFFFED2UL) = 0x00;
            break;

        case 0x06:
            REG16(0xFFFF6AUL) = REG16(0x250820UL);
            if (REG8(0x11A83BUL) < 0x0A) {
                REG8(0x11A83BUL) = (u8)(REG8(0x11A83BUL) + 1);
            } else {
                REG8(0x11A83BUL) = 0x00;
                REG8(0xFFFED2UL) = 0x01;
                REG8(0x11A839UL) = 0x01;
            }
            break;

        case 0x00:
            /* Homing: eight phases at a time until the counter runs out. */
            latch_a_on(0x20, 0x40);
            if (REG8(0x11A83BUL) < 0x07) {
                if (REG8(0x11A83CUL) < 0x08) {
                    motor_1_step_home();
                    REG8(0x11A83CUL) = (u8)(REG8(0x11A83CUL) + 1);
                    REG8(0x11A839UL) = 0x00;
                } else {
                    REG8(0x11A839UL) = (u8)(REG8(0x11A839UL) + 1);
                    if (REG8(0x11A839UL) >= 0x08) {
                        REG8(0x11A83CUL) = 0x00;
                        REG8(0x11A83BUL) = (u8)(REG8(0x11A83BUL) + 1);
                    }
                }
                REG16(0xFFFF6AUL) = REG16(0x250820UL);
            } else {
                REG8(0x11A83CUL) = 0x00;
                REG8(0x11A83BUL) = 0x00;
                REG8(0x11A83AUL) = 0x00;
                REG8(0xFFFED2UL) = 0x06;
                REG8(0x11A839UL) = 0x01;
            }
            break;

        default:
            REG8(0xFFFED2UL) = 0x01;
            break;
        }
        return;
    }

    /* ---- the feed motor ---- */
    if (REG8(0xFFFED4UL) > 0x09) {
        REG8(0xFFFED4UL) = 0x01;
        return;
    }

    switch (REG8(0xFFFED4UL)) {
    case 0x01:
        pos = REG8(0x11A840UL);
        tgt = REG8(0xFFFED5UL);
        if (pos == tgt) {
            REG8(0x11A835UL) &= (u8)~0x80;
            REG16(0xFFFF6AUL) = 0x2AF8;
        } else if (tgt == 0xFF) {
            REG8(0x11A835UL) &= (u8)~0x80;
            latch_a_off(0x08, 0x10);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250936UL));
        } else if (tgt == 0xFE) {
            /* coast */
        } else if (tgt == 0xFD) {
            REG8(0x11A835UL) &= (u8)~0x80;
            latch_a_off(0x08, 0x10);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250935UL));
        } else if (tgt == 0xFC) {
            REG8(0x11A835UL) &= (u8)~0x80;
            latch_a_off(0x08, 0x10);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250934UL));
        } else {
            /* An odd position is snapped onto an even one first: the low
             * nibble of where the motor *is*, not of where it is going. */
            n = (u8)(pos & 0x0F);
            if (n == 0x01 || n == 0x05 || n == 0x09 || n == 0x0D) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) - 1);
            } else if (n == 0x03 || n == 0x07 || n == 0x0B || n == 0x0F) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) + 1);
            }

            latch_a_off(0x08, 0x10);

            /* Half the distance, because the feed steps by two. */
            if (REG8(0x11A840UL) > tgt) {
                REG8(0x11A835UL) &= (u8)~0x08;
                REG8(0x11A837UL) =
                    (u8)((short)((short)(u16)REG8(0x11A840UL) -
                                 (short)(u16)tgt) / 2);
            } else {
                REG8(0x11A835UL) |= 0x08;
                REG8(0x11A837UL) =
                    (u8)((short)((short)(u16)tgt -
                                 (short)(u16)REG8(0x11A840UL)) / 2);
            }

            d = REG8(0x11A837UL);
            if (d < 0x1E) {
                REG8(0x11A835UL) |= 0x10;
            } else if (d <= 0x3C) {
                REG8(0x11A843UL) = 0x01;
            }

            if (REG8(0xFFFED5UL) & 0x01) {
                REG8(0x11A835UL) |= 0x20;
                if (REG8(0xFFFED5UL) & 0x02) {
                    if (REG8(0x11A835UL) & 0x08) {
                        REG8(0x11A837UL) = (u8)(REG8(0x11A837UL) + 1);
                    }
                } else {
                    if (!(REG8(0x11A835UL) & 0x08)) {
                        REG8(0x11A837UL) = (u8)(REG8(0x11A837UL) + 1);
                    }
                }
            } else {
                REG8(0x11A835UL) &= (u8)~0x20;
            }

            /* R5 is H'0002 from the divide, so this tests against 0 and
             * then against 2, and the last two arms are the same. */
            d = REG8(0x11A837UL);
            if (d == 0x00)      REG8(0xFFFED4UL) = 0x04;
            else if (d == 0x01) REG8(0xFFFED4UL) = 0x03;
            else if (d == 0x02) REG8(0xFFFED4UL) = 0x02;
            else                REG8(0xFFFED4UL) = 0x02;

            d = REG8(0x11A837UL);
            REG8(0x11A841UL) = (u8)(d >> 1);
            REG8(0x11A842UL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
            REG8(0x11A83FUL) = 0x01;
            if (REG8(0x11A837UL) != 0) motor_2_step();
            REG16(0xFFFF6AUL) = (u16)(REG16(0xFFFF68UL) + 0x0CF6);
        }
        break;

    case 0x02:
        REG16(0xFFFF6AUL) = phase_word(
            (REG8(0x11A843UL) == 0) ? 0x250938UL : 0x2509B6UL,
            REG8(0x11A83FUL));
        motor_2_step();
        REG8(0x11A841UL) = (u8)(REG8(0x11A841UL) - 1);
        if (REG8(0x11A841UL) != 0) {
            if (!(REG8(0x11A835UL) & 0x10)) {
                REG8(0x11A83FUL) = (u8)(REG8(0x11A83FUL) + 1);
            }
        } else {
            REG8(0xFFFED4UL) = 0x03;
        }
        break;

    case 0x03:
        REG16(0xFFFF6AUL) = phase_word(
            (REG8(0x11A843UL) == 0) ? 0x250938UL : 0x2509B6UL,
            REG8(0x11A83FUL));
        REG8(0x11A842UL) = (u8)(REG8(0x11A842UL) - 1);
        if (REG8(0x11A842UL) != 0) {
            motor_2_step();
            if (!(REG8(0x11A835UL) & 0x10)) {
                REG8(0x11A83FUL) = (u8)(REG8(0x11A83FUL) - 1);
            }
        } else if (REG8(0xFFFED5UL) > 0x04) {
            REG8(0xFFFED4UL) = 0x08;
        } else {
            REG8(0xFFFED4UL) = 0x04;
        }
        break;

    case 0x08:
        REG16(0xFFFF6AUL) = REG16(0x25093AUL);
        motor_2_step();
        REG8(0xFFFED4UL) = 0x09;
        break;

    case 0x09:
        REG16(0xFFFF6AUL) = REG16(0x25093AUL);
        if (REG8(0x11A835UL) & 0x08) REG8(0x11A835UL) &= (u8)~0x08;
        else                         REG8(0x11A835UL) |=        0x08;
        motor_2_step();
        REG8(0xFFFED4UL) = 0x04;
        break;

    case 0x04:
        REG16(0xFFFF6AUL) = 0x2AF8;
        v = REG8(0xFFFED5UL);
        if (!(v & 0x01)) {
            if (v & 0x02) latch_a_on(0x08, 0x10);
        } else {
            n = (u8)(v & 0x0F);
            if (n == 0x01 || n == 0x07) {
                v = (u8)(REG8(0xFFFFD3UL) | 0x01);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            } else if (n == 0x09 || n == 0x0F) {
                v = (u8)(REG8(0xFFFFD3UL) & ~0x01);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            } else if (n == 0x03 || n == 0x0D) {
                v = (u8)(REG8(0xFFFFD3UL) & ~0x04);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            } else if (n == 0x05 || n == 0x0B) {
                v = (u8)(REG8(0xFFFFD3UL) | 0x04);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            }

            n = (u8)(REG8(0xFFFED5UL) & 0x0F);
            if (n == 0x01 || n == 0x05 || n == 0x09 || n == 0x0D) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) + 1);
            } else if (n == 0x03 || n == 0x07 || n == 0x0B || n == 0x0F) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) - 1);
            }
            latch_a_on(0x08, 0x10);
        }
        REG8(0xFFFED4UL) = 0x01;
        REG8(0x11A83FUL) = 0x01;
        REG8(0x11A843UL) = 0x00;
        REG8(0x11A835UL) &= (u8)~0x10;
        REG8(0x11A835UL) &= (u8)~0x20;
        break;

    case 0x05:
        REG16(0xFFFF6AUL) = 0x2AF8;
        REG8(0x11A842UL) = 0x00;
        REG8(0x11A841UL) = 0x00;
        REG8(0x11A840UL) = 0x00;
        REG8(0x11A835UL) = 0x00;
        REG8(0x11A83EUL) = 0x00;
        REG8(0x11A83FUL) = 0x00;
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250924UL));
        latch_a_on(0x08, 0x10);
        REG8(0xFFFED4UL) = 0x00;
        break;

    case 0x06:
        REG16(0xFFFF6AUL) = REG16(0x250938UL);
        if (REG8(0x11A841UL) < 0x32) {
            REG8(0x11A841UL) = (u8)(REG8(0x11A841UL) + 1);
        } else {
            REG8(0x11A841UL) = 0x00;
            REG8(0x11A83FUL) = 0x01;
            REG8(0xFFFED4UL) = 0x04;
        }
        break;

    case 0x00:
        if (REG8(0x11A841UL) < 0x0F) {
            if (REG8(0x11A842UL) < 0x08) {
                motor_2_step_home();
                REG8(0x11A842UL) = (u8)(REG8(0x11A842UL) + 1);
                REG8(0x11A83FUL) = 0x00;
            } else {
                REG8(0x11A83FUL) = (u8)(REG8(0x11A83FUL) + 1);
                if (REG8(0x11A83FUL) >= 0x08) {
                    REG8(0x11A842UL) = 0x00;
                    REG8(0x11A841UL) = (u8)(REG8(0x11A841UL) + 1);
                }
            }
            REG16(0xFFFF6AUL) = REG16(0x250938UL);
        } else {
            REG8(0x11A842UL) = 0x00;
            REG8(0x11A841UL) = 0x00;
            REG8(0x11A840UL) = 0x00;
            REG8(0x11A83FUL) = 0x01;
            REG8(0xFFFED4UL) = 0x06;
        }
        break;

    default:
        REG8(0xFFFED4UL) = 0x01;
        break;
    }
}

/* ---- ITU1: motor 3, the hook width -------------------------------------
 * H'20D17A. The same shape as the two above, with one motor to itself. Its
 * drive is the whole of H'FFFFA4 and its enables are bits 6 and 7 of the
 * H'0C0000 latch. */
void isr_motor_3_body(void)
{
    u8 pos, tgt, d;

    REG8(0xFFFF71UL) &= (u8)~0x01;

    switch (REG8(0xFFFED6UL)) {
    case 0x01:
        pos = REG8(0x11A846UL);
        tgt = REG8(0xFFFED7UL);
        if (pos == tgt) {
            REG16(0xFFFF74UL) = 0x2AF8;
        } else if (tgt == 0xFF) {
            latch_b_on(0x40, 0x80);
            REG8(0xFFFFA4UL) = REG8(0x250A36UL);
        } else if (tgt == 0xFE) {
            /* coast */
        } else if (tgt == 0xFD) {
            latch_b_off(0x40, 0x80);
            REG8(0xFFFFA4UL) = REG8(0x250A35UL);
        } else if (tgt == 0xFC) {
            latch_b_off(0x40, 0x80);
            REG8(0xFFFFA4UL) = REG8(0x250A34UL);
        } else {
            if (pos > tgt) {
                REG8(0x11A836UL) &= (u8)~0x01;
                REG8(0x11A837UL) = (u8)(REG8(0x11A846UL) - tgt);
            } else {
                REG8(0x11A836UL) |= 0x01;
                REG8(0x11A837UL) = (u8)(tgt - REG8(0x11A846UL));
            }

            if (REG8(0x11A837UL) < 0x14) REG8(0x11A836UL) |= 0x04;

            d = REG8(0x11A837UL);
            if (d == 0x01)      REG8(0xFFFED6UL) = 0x03;
            else if (d == 0x02) REG8(0xFFFED6UL) = 0x02;
            else                REG8(0xFFFED6UL) = 0x02;

            d = REG8(0x11A837UL);
            REG8(0x11A847UL) = (u8)(d >> 1);
            REG8(0x11A848UL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
            REG8(0x11A845UL) = 0x01;
            motor_3_step();
            REG16(0xFFFF74UL) = (u16)(REG16(0xFFFF72UL) + 0x0CF6);
            latch_b_off(0x40, 0x80);
        }
        break;

    case 0x02:
        REG16(0xFFFF74UL) = phase_word(0x250A38UL, REG8(0x11A845UL));
        motor_3_step();
        REG8(0x11A847UL) = (u8)(REG8(0x11A847UL) - 1);
        if (REG8(0x11A847UL) != 0) {
            REG8(0x11A845UL) = (u8)(REG8(0x11A845UL) + 1);
        } else {
            REG8(0xFFFED6UL) = 0x03;
        }
        break;

    case 0x03:
        REG16(0xFFFF74UL) = phase_word(0x250A38UL, REG8(0x11A845UL));
        REG8(0x11A848UL) = (u8)(REG8(0x11A848UL) - 1);
        if (REG8(0x11A848UL) != 0) {
            motor_3_step();
            REG8(0x11A845UL) = (u8)(REG8(0x11A845UL) - 1);
        } else {
            REG8(0xFFFED6UL) = 0x04;
        }
        break;

    case 0x04:
        REG16(0xFFFF74UL) = 0x2AF8;
        REG8(0x11A836UL) &= (u8)~0x04;
        latch_b_on(0x40, 0x80);
        REG8(0xFFFED6UL) = 0x01;
        break;

    case 0x05:
        REG16(0xFFFF74UL) = 0x2AF8;
        REG8(0x11A848UL) = 0x00;
        REG8(0x11A847UL) = 0x00;
        REG8(0x11A846UL) = 0x00;
        REG8(0x11A836UL) = 0x00;
        REG8(0x11A845UL) = 0x00;
        REG8(0x11A844UL) = 0x00;
        REG8(0xFFFFA4UL) = REG8(0x250A24UL);
        latch_b_on(0x40, 0x80);
        REG8(0xFFFED6UL) = 0x00;
        break;

    case 0x06:
        REG16(0xFFFF74UL) = REG16(0x250A38UL);
        if (REG8(0x11A847UL) < 0x0A) {
            REG8(0x11A847UL) = (u8)(REG8(0x11A847UL) + 1);
        } else {
            REG8(0x11A847UL) = 0x00;
            REG8(0xFFFED6UL) = 0x01;
            REG8(0x11A845UL) = 0x01;
        }
        break;

    case 0x00:
        latch_b_on(0x40, 0x80);
        if (REG8(0x11A847UL) < 0x08) {
            if (REG8(0x11A848UL) < 0x08) {
                motor_3_step_home();
                REG8(0x11A848UL) = (u8)(REG8(0x11A848UL) + 1);
                REG8(0x11A845UL) = 0x00;
            } else {
                REG8(0x11A845UL) = (u8)(REG8(0x11A845UL) + 1);
                if (REG8(0x11A845UL) >= 0x04) {
                    REG8(0x11A848UL) = 0x00;
                    REG8(0x11A847UL) = (u8)(REG8(0x11A847UL) + 1);
                }
            }
            REG16(0xFFFF74UL) = REG16(0x250A38UL);
        } else {
            REG8(0x11A848UL) = 0x00;
            REG8(0x11A847UL) = 0x00;
            REG8(0x11A846UL) = 0x00;
            REG8(0xFFFED6UL) = 0x06;
            REG8(0x11A845UL) = 0x01;
        }
        break;

    default:
        REG8(0xFFFED6UL) = 0x01;
        break;
    }
}

/* ---- ITU2: motor 4, the hook position ----------------------------------
 * H'20D53A. Four phases rather than eight, drive in the low six bits of the
 * H'0C0000 latch, and an idle interval of H'EA60 rather than H'2AF8. The
 * park patterns here are OR'd into the latch rather than replacing it. */
void isr_motor_4_body(void)
{
    u8 pos, tgt, d, v;

    REG8(0xFFFF7BUL) &= (u8)~0x01;

    switch (REG8(0xFFFED0UL)) {
    case 0x01:
        pos = REG8(0x11A84BUL);
        tgt = REG8(0xFFFED1UL);
        if (pos == tgt) {
            REG16(0xFFFF7EUL) = 0xEA60;
        } else if (tgt == 0xFF || tgt == 0xFD || tgt == 0xFC) {
            const u32 park = (tgt == 0xFF) ? 0x250A9AUL
                           : (tgt == 0xFD) ? 0x250A99UL : 0x250A98UL;
            v = (u8)(REG8(0xFFFD39UL) | REG8(park));
            REG8(0x0C0000UL) = v;
            REG8(0xFFFD39UL) = v;
        } else if (tgt == 0xFE) {
            /* coast */
        } else {
            if (pos > tgt) {
                REG8(0x11A836UL) &= (u8)~0x08;
                REG8(0x11A837UL) = (u8)(REG8(0x11A84BUL) - tgt);
            } else {
                REG8(0x11A836UL) |= 0x08;
                REG8(0x11A837UL) = (u8)(tgt - REG8(0x11A84BUL));
            }

            if (REG8(0x11A837UL) < 0x14) REG8(0x11A836UL) |= 0x20;

            d = REG8(0x11A837UL);
            if (d == 0x01)      REG8(0xFFFED0UL) = 0x04;
            else if (d == 0x02) REG8(0xFFFED0UL) = 0x03;
            else                REG8(0xFFFED0UL) = 0x02;

            d = REG8(0x11A837UL);
            REG8(0x11A84CUL) = (u8)(d >> 1);
            REG8(0x11A84DUL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
            REG8(0x11A84AUL) = 0x01;
            motor_4_step();
            REG16(0xFFFF7EUL) =
                (u16)(REG16(0xFFFF7CUL) + REG16(0x250A9CUL));
        }
        break;

    case 0x02:
        REG16(0xFFFF7EUL) = phase_word(0x250A9CUL, REG8(0x11A84AUL));
        motor_4_step();
        REG8(0x11A84CUL) = (u8)(REG8(0x11A84CUL) - 1);
        if (REG8(0x11A84CUL) != 0) {
            REG8(0x11A84AUL) = (u8)(REG8(0x11A84AUL) + 1);
        } else {
            REG8(0xFFFED0UL) = 0x03;
        }
        break;

    case 0x03:
        REG16(0xFFFF7EUL) = phase_word(0x250A9CUL, REG8(0x11A84AUL));
        REG8(0x11A84DUL) = (u8)(REG8(0x11A84DUL) - 1);
        if (REG8(0x11A84DUL) != 0) {
            motor_4_step();
            REG8(0x11A84AUL) = (u8)(REG8(0x11A84AUL) - 1);
        } else {
            REG8(0xFFFED0UL) = 0x04;
        }
        break;

    case 0x04:
        REG16(0xFFFF7EUL) = 0xEA60;
        REG8(0x11A836UL) &= (u8)~0x20;
        latch_b_on(0x10, 0x20);
        REG8(0xFFFED0UL) = 0x01;
        break;

    case 0x05:
        REG16(0xFFFF7EUL) = 0xEA60;
        REG8(0x11A84DUL) = 0x00;
        REG8(0x11A84CUL) = 0x00;
        REG8(0x11A84BUL) = 0x00;
        REG8(0x11A836UL) = 0x00;
        REG8(0x11A84AUL) = 0x00;
        REG8(0x11A849UL) = 0x00;
        v = (u8)(REG8(0xFFFD39UL) | REG8(0x250A90UL));
        REG8(0x0C0000UL) = v;
        v = (u8)(v | 0x10);
        REG8(0x0C0000UL) = v;
        v = (u8)(v | 0x20);
        REG8(0x0C0000UL) = v;
        REG8(0xFFFD39UL) = v;
        REG8(0xFFFED0UL) = 0x00;
        break;

    case 0x06:
        REG16(0xFFFF7EUL) = REG16(0x250A9CUL);
        if (REG8(0x11A84CUL) < 0x0A) {
            REG8(0x11A84CUL) = (u8)(REG8(0x11A84CUL) + 1);
        } else {
            REG8(0x11A84CUL) = 0x00;
            REG8(0xFFFED0UL) = 0x01;
            REG8(0x11A84AUL) = 0x01;
        }
        break;

    case 0x00:
        if (REG8(0x11A84CUL) < 0x0E) {
            if (REG8(0x11A84DUL) < 0x04) {
                motor_4_step_home();
                REG8(0x11A84DUL) = (u8)(REG8(0x11A84DUL) + 1);
                REG8(0x11A84AUL) = 0x00;
            } else {
                REG8(0x11A84AUL) = (u8)(REG8(0x11A84AUL) + 1);
                if (REG8(0x11A84AUL) >= 0x08) {
                    REG8(0x11A84DUL) = 0x00;
                    REG8(0x11A84CUL) = (u8)(REG8(0x11A84CUL) + 1);
                }
            }
            REG16(0xFFFF7EUL) = REG16(0x250A9CUL);
        } else {
            REG8(0x11A84DUL) = 0x00;
            REG8(0x11A84CUL) = 0x00;
            REG8(0x11A84BUL) = 0x00;
            REG8(0xFFFED0UL) = 0x06;
            REG8(0x11A84AUL) = 0x01;
        }
        break;

    default:
        REG8(0xFFFED0UL) = 0x01;
        break;
    }
}

/* ---- ITU3: the sewing speed -------------------------------------------
 * The main motor's tacho drives ITU3's input capture. H'20D99A is the
 * overflow, counting how many whole counter periods have passed, and
 * H'20D9CC is the capture, which turns the two together into the speed at
 * H'FFFECE.
 *
 * The number is H'27B0F0 divided by (overflows << 8) + the top byte of the
 * capture -- so 2,600,176 over the time between two tacho edges. An overflow
 * with no capture behind it leaves bit 4 of H'11A852 up, and the capture
 * that follows skips the division and reports nothing rather than dividing
 * by whatever is there.
 */
void isr_tacho_overflow_body(void)
{
    REG8(0xFFFF85UL) &= (u8)~0x04;

    REG8(0x11A84FUL) = (u8)(REG8(0x11A84FUL) + 1);
    if (REG8(0x11A84FUL) == 0) {
        REG8(0x11A852UL) |= 0x10;
        REG16(0xFFFECEUL) = 0x0000;
    }
}

void isr_tacho_capture_body(void)
{
    REG8(0xFFFF85UL) &= (u8)~0x01;

    if (!(REG8(0x11A852UL) & 0x10)) {
        const u16 ticks = (u16)(((u16)REG8(0x11A84FUL) << 8) +
                                (u16)(u8)(REG16(0xFFFF88UL) >> 8));

        REG16(0xFFFECEUL) = ticks;
        REG16(0xFFFECEUL) = (u16)(0x0027B0F0L / (long)(u32)ticks);
    } else {
        REG8(0x11A852UL) &= (u8)~0x10;
    }

    REG8(0x11A84FUL) = 0x00;
}

/* H'20E04A. ITU4's compare A: one pin raised and nothing else. */
void isr_itu4_a_body(void)
{
    REG8(0xFFFF95UL) &= (u8)~0x01;
    REG8(0xFFFFD6UL) |= 0x08;
}

/* ---- the five motor demands ------------------------------------------
 * H'11A6B7 to H'11A6BB are where the rest of the application leaves what it
 * wants the steppers to do; H'11A821 to H'11A824 are what was last sent, and
 * only a change is written through to H'FFFED1/3/5/7. Each has a ceiling,
 * and the first time round each takes the motor out of its parked state.
 */
void needle_demand_apply(void)
{
    if (!(REG8(0x114DC6UL) & 0x01)) {
        REG8(0x114DC6UL) |= 0x01;
        motor_1_wait_ready();
        REG8(0x11A821UL) = 0x00;
    }
    if (REG8(0x11A6B7UL) > 0x34) REG8(0x11A6B7UL) = 0x34;
    if (REG8(0x11A821UL) != REG8(0x11A6B7UL)) {
        REG8(0x11A821UL) = REG8(0x11A6B7UL);
        REG8(0xFFFED3UL) = REG8(0x11A6B7UL);
    }
}

void feed_demand_apply(void)
{
    if (!(REG8(0x114DC6UL) & 0x02)) {
        motor_2_wait_ready();
        REG8(0x114DC6UL) |= 0x02;
        REG8(0x11A822UL) = 0x00;
    }
    if (REG8(0x11A6B8UL) > 0xDA) REG8(0x11A6B8UL) = 0xDA;
    if (REG8(0x11A822UL) != REG8(0x11A6B8UL)) {
        REG8(0x11A822UL) = REG8(0x11A6B8UL);
        REG8(0xFFFED5UL) = REG8(0x11A6B8UL);
    }
}

/* H'20A61A. The hook width from the copy at H'11A820 rather than from
 * H'11A6B9 -- H'20A652 is what puts one into the other. */
void hook_width_apply(void)
{
    if (REG8(0x11A820UL) > 0x31) REG8(0x11A820UL) = 0x31;
    if (REG8(0x11A823UL) != REG8(0x11A820UL)) {
        REG8(0x11A823UL) = REG8(0x11A820UL);
        REG8(0xFFFED7UL) = REG8(0x11A820UL);
    }
}

void hook_pos_apply(void)
{
    if (!(REG8(0x114DC6UL) & 0x20)) {
        motor_3_wait_ready();
        REG8(0x114DC6UL) |= 0x20;
        REG8(0x11A823UL) = 0x00;
    }
    if (REG8(0x11A6BAUL) > 0x31) REG8(0x11A6BAUL) = 0x31;
    if (REG8(0x11A823UL) != REG8(0x11A6BAUL)) {
        REG8(0x11A823UL) = REG8(0x11A6BAUL);
        REG8(0xFFFED7UL) = REG8(0x11A6BAUL);
    }
    REG8(0x11A820UL) = REG8(0x11A6B9UL);
}

void thread_demand_apply(void)
{
    if (!(REG8(0x114DCDUL) & 0x02)) {
        motor_4_wait_ready();
        REG8(0x114DCDUL) |= 0x02;
        REG8(0x11A824UL) = 0x00;
    }
    if (REG8(0x11A6BBUL) > 0x29) REG8(0x11A6BBUL) = 0x29;
    if (REG8(0x11A824UL) != REG8(0x11A6BBUL)) {
        REG8(0x11A824UL) = REG8(0x11A6BBUL);
        REG8(0xFFFED1UL) = REG8(0x11A6BBUL);
    }
}

/* ---- the sewing phase -------------------------------------------------
 * H'FFFEC0 is where the machine is in the stitch, read off the three
 * position sensors, and H'11A82B remembers which of the four motors have
 * already been let go for this phase so that none is moved twice.
 *
 * H'20A716 is what runs when a phase has lasted H'C8 ticks without the
 * sensors changing -- the machine has stopped somewhere -- and H'20A884 is
 * what runs on the phases that mean it is turning.
 */
void sew_phase_settle(void)
{
    REG8(0x11A82BUL) &= (u8)~0x01;
    REG8(0x11A82BUL) &= (u8)~0x02;

    switch (REG8(0xFFFEC0UL)) {
    case 0x00:
    case 0x01:
    case 0x02:
        REG8(0x11A82BUL) &= (u8)~0x04;
        if (!(REG8(0x11A82BUL) & 0x08)) {
            REG8(0x11A82BUL) |= 0x08;
            hook_pos_apply();
        }
        feed_demand_apply();
        thread_demand_apply();
        break;

    case 0x03:
    case 0x05:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x08;
        thread_demand_apply();
        break;

    case 0x04:
    case 0x06:
        REG8(0x11A82BUL) &= (u8)~0x08;
        if (!(REG8(0x11A82BUL) & 0x04)) {
            REG8(0x11A82BUL) |= 0x04;
            hook_width_apply();
        }
        needle_demand_apply();
        thread_demand_apply();
        break;

    case 0x07:
        REG8(0x11A82BUL) &= (u8)~0x04;
        needle_demand_apply();
        thread_demand_apply();
        break;

    default:
        break;
    }
}

void sew_phase_step(void)
{
    switch (REG8(0xFFFEC0UL)) {
    case 0x00:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x01;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x02)) {
            REG8(0x11A82BUL) |= 0x02;
            feed_demand_apply();
        }
        if (!(REG8(0x11A82BUL) & 0x08)) {
            REG8(0x11A82BUL) |= 0x08;
            hook_pos_apply();
        }
        /* Phase zero calls the thread motor a second time; phases 1 and 2,
         * which are otherwise the same code, do not. */
        thread_demand_apply();
        break;

    case 0x01:
    case 0x02:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x01;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x02)) {
            REG8(0x11A82BUL) |= 0x02;
            feed_demand_apply();
        }
        if (!(REG8(0x11A82BUL) & 0x08)) {
            REG8(0x11A82BUL) |= 0x08;
            hook_pos_apply();
        }
        break;

    case 0x03:
    case 0x05:
        REG8(0x11A82BUL) &= (u8)~0x01;
        REG8(0x11A82BUL) &= (u8)~0x02;
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x08;
        thread_demand_apply();
        break;

    case 0x04:
    case 0x06:
        REG8(0x11A82BUL) &= (u8)~0x08;
        REG8(0x11A82BUL) &= (u8)~0x02;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x01)) {
            REG8(0x11A82BUL) |= 0x01;
            needle_demand_apply();
        }
        if (!(REG8(0x11A82BUL) & 0x04)) {
            REG8(0x11A82BUL) |= 0x04;
            hook_width_apply();
        }
        break;

    case 0x07:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x08;
        REG8(0x11A82BUL) &= (u8)~0x02;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x01)) {
            REG8(0x11A82BUL) |= 0x01;
            needle_demand_apply();
        }
        break;

    default:
        break;
    }
}

/* H'20AA82. Whichever of the two runs, and how often. Modes 0, 4 and 5 are
 * the stopped ones and get the H'C8-tick settle; 1, 2, 3 and 6 are turning
 * and get a step every tick. */
void sew_phase_tick(void)
{
    const u8 mode = REG8(0xFFFEC6UL);

    if (REG8(0xFFFEF7UL) & 0x80) return;

    if (mode == 0x00 || (mode >= 0x04 && mode <= 0x05)) {
        const u8 n = (u8)(REG8(0x11A825UL) + 1);

        REG8(0x11A825UL) = n;
        if (n >= 0xC8) {
            sew_phase_settle();
            REG8(0x11A825UL) = 0x00;
        }
        return;
    }

    if (mode < 0x01) return;
    if (mode < 0x04 || mode == 0x06) {
        REG8(0x11A825UL) = 0x00;
        sew_phase_step();
    }
}

/* H'20AAE0. The counters. H'11A6D8, H'114DDE, H'114DE0, H'114DDA count up;
 * H'114DDC counts up by two; H'114DE4 counts down, which is what the hall
 * trim and the module wait on; and the key hold-off runs out here. */
void ms_counters_tick(void)
{
    REG16(0x11A6D8UL) = (u16)(REG16(0x11A6D8UL) + 1);
    REG16(0x114DDEUL) = (u16)(REG16(0x114DDEUL) + 1);
    REG16(0x114DE0UL) = (u16)(REG16(0x114DE0UL) + 1);
    REG16(0x114DE2UL) = (u16)(REG16(0x114DE2UL) + 1);
    REG16(0x114DDAUL) = (u16)(REG16(0x114DDAUL) + 1);
    REG16(0x114DDCUL) = (u16)(REG16(0x114DDCUL) + 2);
    REG16(0x114DE4UL) = (u16)(REG16(0x114DE4UL) - 1);

    if (REG8(0x11A802UL) != 0) {
        REG8(0x11A802UL) = (u8)(REG8(0x11A802UL) - 1);
    }
}

/* H'20A092. The main motor's chopper. H'11A826 counts to H'11A827 and the
 * enable bit follows, so the ratio of the two is the duty; past H'32 the
 * count restarts and the on-time is reloaded from H'11A811. The latch is
 * shared with the stepper enables, which is why the mask goes up around it.
 */
void motor_pwm_tick(void)
{
    const u8 n = (u8)(REG8(0x11A826UL) + 1);

    REG8(0x11A826UL) = n;
    if (n <= REG8(0x11A827UL)) {
        interrupts_off();
        latch_a_bit(0x80, 1);
        interrupts_on();
    } else {
        interrupts_off();
        latch_a_bit(0x80, 0);
        interrupts_on();
        if (REG8(0x11A826UL) > 0x32) {
            REG8(0x11A826UL) = 0x00;
            REG8(0x11A827UL) = REG8(0x11A811UL);
        }
    }
}

/* H'20A10C and H'20A19A. The two knobs. Each is a quadrature pair on port C
 * -- bits 2 and 3 for one, 4 and 5 for the other -- and each turn moves the
 * count by one. Wrapping below zero reads as H'FE or above and is clamped
 * back to nothing; the ceiling comes in as an argument. */
static u8 knob_track(u8 count, u8 limit, u32 state, u8 shift)
{
    const u8 now  = (u8)((REG8(0xFFFFD7UL) >> shift) & 0x03);
    const u8 was  = REG8(state);
    u8 v = count;

    if (was != now) {
        switch (was) {
        case 0x00:
            if (now == 0x01) v++;
            if (now == 0x02) v--;
            break;
        case 0x01:
            if (now == 0x03) v++;
            if (now == 0x00) v--;
            break;
        case 0x03:
            if (now == 0x02) v++;
            if (now == 0x01) v--;
            break;
        case 0x02:
            if (now == 0x00) v++;
            if (now == 0x03) v--;
            break;
        default:
            break;
        }
    }

    if (v >= 0xFE)  v = 0x00;
    if (v >= limit) v = limit;

    REG8(state) = now;
    return v;
}

u8 knob_a_track(u8 count, u16 limit)
{
    return knob_track(count, (u8)limit, 0x11A828UL, 2);
}

u8 knob_b_track(u8 count, u16 limit)
{
    return knob_track(count, (u8)limit, 0x11A829UL, 4);
}

/* H'20A22C. The handwheel. Two sensors -- bit 1 of port C's input register
 * and bit 4 of the H'080000 latch -- read as a two-bit code, and every
 * change moves the position by one. A jump of two, where a step was missed,
 * is taken in whichever direction the wheel was last going: H'11A82B bit 7
 * is that memory. A change on either sensor also raises a bit in H'114DCE,
 * which is what the foot-lift calibration watches. */
u16 handwheel_track(u16 position)
{
    u8 now = 0;
    const u8 was = REG8(0x11A82AUL);
    u16 p = position;

    if (REG8(0xFFFFD6UL) & 0x02) now |= 0x01; else now &= (u8)~0x01;
    if (REG8(0x080000UL) & 0x10) now |= 0x02; else now &= (u8)~0x02;

    if ((was & 0x01) != (now & 0x01)) REG8(0x114DCEUL) |= 0x02;
    if ((was & 0x02) != (now & 0x02)) REG8(0x114DCEUL) |= 0x01;

    if (was != now) {
        u8 fwd = 0, back = 0, jump = 0;

        switch (was) {
        case 0x00: fwd = 0x01; back = 0x02; jump = 0x03; break;
        case 0x01: fwd = 0x03; back = 0x00; jump = 0x02; break;
        case 0x03: fwd = 0x02; back = 0x01; jump = 0x00; break;
        case 0x02: fwd = 0x00; back = 0x03; jump = 0x01; break;
        default:   break;
        }

        if (was <= 0x03) {
            if (now == fwd) {
                p = (u16)(p + 1);
                REG8(0x11A82BUL) |= 0x80;
            } else if (now == back) {
                p = (u16)(p - 1);
                REG8(0x11A82BUL) &= (u8)~0x80;
            } else if (now == jump) {
                if (REG8(0x11A82BUL) & 0x80) p = (u16)(p + 2);
                else                         p = (u16)(p - 2);
            }
        }
    }

    REG8(0x11A82AUL) = now;
    return p;
}

/* H'20A40A. The beeper, a millisecond at a time. H'11A24A counts the period
 * down and H'11A24E is how much of it is silence, so the pin follows the
 * two; when the period runs out and there are repeats left, it is reloaded
 * from on-time plus off-time. */
void beep_tick(void)
{
    u16 left = REG16(0x11A24AUL);

    if (left != 0) {
        const u16 off = REG16(0x11A24EUL);

        left = (u16)(left - 1);
        REG16(0x11A24AUL) = left;
        if (left >= off) {
            REG8(0xFFFEC1UL) |=        0x80;
            REG8(0xFFFFC7UL) |=        0x10;
        } else {
            REG8(0xFFFEC1UL) &= (u8)~0x80;
            REG8(0xFFFFC7UL) &= (u8)~0x10;
        }
    } else if (REG8(0x11A250UL) > 0x01) {
        REG8(0x11A250UL) = (u8)(REG8(0x11A250UL) - 1);
        REG16(0x11A24AUL) = (u16)(REG16(0x11A24CUL) + REG16(0x11A24EUL));
    }
}

/* H'20A47C. Where the needle is, off three sensors: bit 0 of port C's input
 * register and bits 1 and 2 of the H'080000 latch, packed into H'FFFEC0.
 * While the machine is running, the ticks spent in each phase are counted,
 * and the count at the moment phase 5 arrives from phase 1 is left at
 * H'114DD8 -- which is how long one stitch took. */
void sew_position_sense(void)
{
    u8 code = 0;

    if (REG8(0xFFFFD6UL) & 0x01) code |= 0x04;
    if (REG8(0x080000UL) & 0x02) code |= 0x02;
    if (REG8(0x080000UL) & 0x04) code |= 0x01;

    if (REG8(0x114DC6UL) & 0x80) {
        REG16(0x11A82CUL) = (u16)(REG16(0x11A82CUL) + 1);
        if (REG8(0xFFFEC0UL) == 0x05 && code == 0x01) {
            REG16(0x114DD8UL) = REG16(0x11A82CUL);
            REG16(0x11A82CUL) = 0x0000;
        }
    }

    REG8(0xFFFEC0UL) = code;
}

/* ---- ITU4: the millisecond, in three slices ---------------------------
 * H'20DFB0. When bit 2 of H'114DCD is up the machine is in the foot-lift
 * calibration and the work is split across three interrupts at H'0E67 apiece
 * rather than done in one at H'2B34; H'11A853 says which slice is next.
 * H'11A850 counts the whole ones.
 */
void tick_slice_1(void)
{
    REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    sew_position_sense();
    ms_counters_tick();
    motor_pwm_tick();
}

void tick_slice_2(void)
{
    REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    beep_tick();

    if (mode_code() == 0x000A) {
        REG8(0x11B101UL) = knob_a_track(REG8(0x11B101UL), 0x00FF);
    } else if (!(REG8(0x114DCFUL) & 0x40)) {
        REG8(0x11A6D3UL) = knob_a_track(REG8(0x11A6D3UL), 0x00C8);
    }
}

void tick_slice_3(void)
{
    REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    sew_phase_tick();

    if (mode_code() != 0x000A && !(REG8(0x114DCFUL) & 0x80)) {
        REG8(0x11A6D5UL) = knob_b_track(REG8(0x11A6D5UL), 0x00C8);
    }
}

/* H'20AC1C. All of it in one go, which is what happens when the calibration
 * is not running. The knobs and the handwheel are skipped while bit 0 of
 * H'FFFEC4 says service mode or bit 7 of H'FFFEF7 says the sewing side is
 * held off. */
void tick_all(void)
{
    sew_position_sense();
    ms_counters_tick();
    motor_pwm_tick();
    beep_tick();
    sew_phase_tick();

    if (REG8(0xFFFEC4UL) & 0x01) return;
    if (REG8(0xFFFEF7UL) & 0x80) return;

    if (mode_code() == 0x000A) {
        REG8(0x11B101UL) = knob_a_track(REG8(0x11B101UL), 0x00FF);
        return;
    }

    if (!(REG8(0x114DCFUL) & 0x40)) {
        REG8(0x11A6D3UL) = knob_a_track(REG8(0x11A6D3UL), 0x00C8);
    }
    if (!(REG8(0x114DCFUL) & 0x80)) {
        REG8(0x11A6D5UL) = knob_b_track(REG8(0x11A6D5UL), 0x00C8);
    }
    if (REG8(0x114DCDUL) & 0x04) {
        REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    }
}

void isr_millisecond_body(void)
{
    REG8(0xFFFF95UL) &= (u8)~0x02;

    if (REG8(0x114DCDUL) & 0x04) {
        const u8 slice = (u8)(REG8(0x11A853UL) + 1);

        REG16(0xFFFF9AUL) = 0x0E67;
        REG8(0x11A853UL) = slice;

        if (slice == 0x01) {
            tick_slice_1();
        } else if (slice == 0x02) {
            tick_slice_2();
        } else if (slice == 0x03) {
            REG8(0xFFFFD7UL) &= (u8)~0x80;
            REG8(0xFFFFD6UL) &= (u8)~0x08;
            REG16(0x11A850UL) = (u16)(REG16(0x11A850UL) + 1);
            tick_slice_3();
            REG8(0xFFFFD7UL) |= 0x80;
            REG8(0x11A853UL) = 0x00;
        }
    } else {
        REG16(0xFFFF9AUL) = 0x2B34;
        REG8(0xFFFFD7UL) &= (u8)~0x80;
        REG8(0xFFFFD6UL) &= (u8)~0x08;
        /* ANDC #H'3F: both mask bits down, not just the one. */
        __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
        REG16(0x11A850UL) = (u16)(REG16(0x11A850UL) + 1);
        tick_all();
        REG8(0xFFFFD7UL) |= 0x80;
    }
}

/* ---- the vector entries -----------------------------------------------
 * Each is the interrupt-handler wrapper for the body above it. The bodies
 * are ordinary functions so that the comparison harness can call them; the
 * originals are entered and left the same way, so a case can also be put on
 * the vector address itself.
 */
void isr_24(void) __attribute__((interrupt_handler));
void isr_24(void) { isr_motors_12_body(); }

void isr_28(void) __attribute__((interrupt_handler));
void isr_28(void) { isr_motor_3_body(); }

void isr_32(void) __attribute__((interrupt_handler));
void isr_32(void) { isr_motor_4_body(); }

void isr_36(void) __attribute__((interrupt_handler));
void isr_36(void) { isr_tacho_capture_body(); }

void isr_38(void) __attribute__((interrupt_handler));
void isr_38(void) { isr_tacho_overflow_body(); }

void isr_40(void) __attribute__((interrupt_handler));
void isr_40(void) { isr_itu4_a_body(); }

void isr_41(void) __attribute__((interrupt_handler));
void isr_41(void) { isr_millisecond_body(); }
