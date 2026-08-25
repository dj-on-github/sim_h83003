/* The artista 180 application, rebuilt in C: a stitch turned into motor
 * positions, interpolated, and driven.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- turning a stitch into two motor positions --------------------------
 * A stitch in the data is a pair of numbers: how far to move the needle
 * sideways and how far to move the feed. Neither goes to a motor as it
 * stands. Both are scaled by what the operator has set, offset by the
 * pattern's own trim, clamped, and turned round when the pattern is
 * mirrored. H'11A6B8 is where the feed ends up and H'11A6B9/H'11A6BA the
 * two ends of the needle's swing.
 */

/* H'205628. The feed.
 *
 * The low six bits of the stitch's second byte are a code, and thirteen of
 * the codes mean something other than a distance -- a fixed step, a stop, or
 * a step worked out from how far past a limit the counter has run. Anything
 * else is a plain proportion of the stitch length.
 */
void feed_position_set(void)
{
    u8  len   = REG8(0x11A7DAUL);
    u8  full  = 0x64;
    u8  trim  = REG8(0x11A7DDUL);
    u16 over  = sew_limit_overshoot();
    u8  code, amount, result;
    u8  idx;

    if (len > REG8(0x11A7C2UL)) len = REG8(0x11A7C2UL);

    if (REG8(0x114DC6UL) & 0x80) code = REG8(0x11A7C9UL);
    else                         code = REG8(0x11A6B1UL);

    if (code & 0x80) REG8(0x11A6AEUL) |=        0x40;
    else             REG8(0x11A6AEUL) &= (u8)~0x40;

    idx = (u8)((u8)(code & 0x3F) + 0xCD);

    switch (idx) {
    case 0x00: amount = 0x00; break;
    case 0x01: amount = 0x02; break;
    case 0x07: amount = 0x14; break;
    case 0x04: amount = 0x1E; break;
    case 0x08: amount = 0x28; break;
    case 0x05: case 0x06: case 0x09:
    case 0x0A: case 0x0B: case 0x0C:
        amount = 0x00;
        REG8(0x11A6AEUL) |= 0x40;
        break;

    case 0x02:
        if ((REG8(0x114DC6UL) & 0x08) && !(REG8(0x11A7D7UL) & 0x01)) {
            if ((u16)((u16)0x0003 * over) >= 0x0032) {
                amount = 0x32;
            } else {
                amount = (u8)((u8)((u16)(u8)over * (u16)3) + 4);
                if (amount < 0x0C) amount = 0x0C;
            }
        } else {
            amount = (u8)((short)((short)(u16)((u16)((u16)full * (u16)len)
                                              + 0x0032) / (short)0x0064));
            if (amount <= 0x0C) amount = 0x0C;
        }
        break;

    case 0x03:
        if ((REG8(0x114DC6UL) & 0x08) &&
            (u16)((u16)0x0003 * over) <= (u16)len &&
            !(REG8(0x11A7D7UL) & 0x01)) {
            amount = (u8)((u8)((u16)(u8)over * (u16)3) + 4);
            if (amount < 0x0C) amount = 0x0C;
        } else {
            amount = (u8)((short)((short)(u16)((u16)((u16)full * (u16)len)
                                              + 0x0032) / (short)0x0064));
        }
        break;

    default:
        amount = (u8)((short)((short)(u16)(
                     (u16)((u16)((u16)(u8)(code & 0x3F) << 1) * (u16)len)
                     + 0x0032) / (short)0x0064));
        break;
    }

    if (!((REG8(0x11A7BCUL) & 0x02) && !(code & 0x40))) {
        /* The original writes the same addition in both halves of a test on
         * the sign of the trim, so the sign makes no difference here. */
        if (!(REG8(0x114DCAUL) & 0x10)) full = (u8)(full + trim);
    }

    if (REG8(0x114DCBUL) & 0x20) amount = 0;

    if (stitch_run_head(REG16(0x11A7E6UL)) == 0x0011) {
        if (REG8(0x11A7BCUL) & 0x04) {
            u8 k = REG8(0x11A7E1UL);
            if (k == 0x01 || k == 0x02) {
                u8 f = REG8(0x11A6AEUL);
                if (f & 0x40) REG8(0x11A6AEUL) = (u8)(f & ~0x40);
                else          REG8(0x11A6AEUL) = (u8)(f |  0x40);
            }
        }
    }

    if (amount >= 0x3C && (REG8(0x114DCDUL) & 0x01)) amount = 0x3C;
    if (REG8(0x11A668UL) != 0) amount = 0;

    {
        u8 bits = REG8(0x11A6AEUL);
        int subtract = ((bits & 0x20) != 0) == ((bits & 0x40) != 0);

        if (subtract) {
            result = (full >= amount) ? (u8)(full - amount) : 0;
        } else if ((short)(u16)(0x00C8 - (u16)full) >= (short)(u16)amount) {
            result = (u8)(full + amount);
        } else {
            result = 0xC8;
        }
    }

    REG8(0x11A6B8UL) = (u8)((u8)(REG8(0xFFFED8UL) + result) + 2);
}

/* H'2058D8. The needle. Only patterns marked as three bytes a stitch have a
 * sideways component at all; the rest sit in the middle. The two ends come
 * out either side of H'18, and bit 6 of the stitch's own byte says which
 * way round. */
void needle_position_apply(void)
{
    u8 trim = REG8(0x11A7DCUL);
    u8 wide = REG8(0x11A7D9UL);
    u8 len  = REG8(0x11A7DAUL);
    u8 raw  = 0;
    u8 v    = 0;
    u8 t;

    if (REG8(0x11A7BCUL) & 0x04) {
        if (REG8(0x114DC6UL) & 0x80) raw = REG8(0x11A7CAUL);
        else                         raw = REG8(0x11A6B3UL);
        v = (u8)(raw & 0x3F);

        if (REG8(0x11A7BDUL) & 0x10) {
            if (len <= REG8(0x11A7C2UL)) {
                v = (u8)((short)((short)(u16)((u16)len * (u16)v)
                                 / (short)(u16)REG8(0x11A7C2UL)));
            }
        } else if (REG8(0x11A7BEUL) & 0x20) {
            if (wide <= REG8(0x11A7C0UL)) {
                v = (u8)((short)((short)(u16)((u16)wide * (u16)v)
                                 / (short)(u16)REG8(0x11A7C0UL)));
            }
        }

        if (!((REG8(0x11A7BCUL) & 0x02) && !(REG8(0x11A7C9UL) & 0x40))) {
            if (!(REG8(0x114DCAUL) & 0x10)) {
                if (trim & 0x80) {
                    trim = (u8)(-(signed char)trim);
                    if (!(raw & 0x40)) {
                        if (v >= trim) v = (u8)(v - trim);
                        else           v = 0;
                    }
                } else {
                    if (raw & 0x40) {
                        if (v >= trim) v = (u8)(v - trim);
                        else           v = 0;
                    }
                }
            }
        }

        t = REG8(0x11A7E1UL);
        if (t == 0x00 || t == 0x03) {
            v = (u8)(v | (u8)(raw & 0xC0));
        } else if (t >= 0x01 && t <= 0x02) {
            if (stitch_run_head(REG16(0x11A7E6UL)) == 0x0011) {
                v = (u8)(v | (u8)(raw & 0xC0));
            } else {
                v = (u8)(v | (u8)((u8)(raw & 0xC0) ^ 0x40));
            }
        }
    } else {
        raw = 0;
        v = 0;
    }

    if (v & 0x40) {
        v = (u8)(v & ~0x40);
        t = (u8)((u8)(v / 2) + 0x18);
        REG8(0x11A6BAUL) = t;
        if (v & 0x01) {
            t = (u8)(t + 1);
            REG8(0x11A6BAUL) = t;
        }
        REG8(0x11A6B9UL) = (u8)(t - v);
    } else {
        if (v == 0x01) REG8(0x11A6BAUL) = 0x17;
        else           REG8(0x11A6BAUL) = (u8)(0x18 - (u8)(v / 2));
        REG8(0x11A6B9UL) = (u8)(REG8(0x11A6BAUL) + v);
    }

    REG8(0x11A6BAUL) = (u8)(REG8(0x11A6BAUL) + 1);
    REG8(0x11A6B9UL) = (u8)(REG8(0x11A6B9UL) + 1);
}

/* ---- interpolating between stitches --------------------------------------
 * A repeated stitch does not repeat the same needle position: it walks from
 * one to the next in as many steps as the repeat count. The four positions
 * of the stitch being worked on are held at H'11A68A, H'11A68C, H'11A68E and
 * H'11A690 as 8.8 fixed point, the three step sizes between them at
 * H'11A692, H'11A694 and H'11A696, and bits 4 to 7 of H'114DCD say which way
 * round each pair is. H'11A689 counts the step.
 */

/* Rounding a 8.8 value to the whole number nearest it. */
static u8 fixed_round(u16 v)
{
    return (u8)((u16)(v + 0x0080) >> 8);
}

/* H'203C32. The four positions out of the stitch bytes, and the three step
 * sizes between them. */
void stitch_spans_build(void)
{
    u16 a, b, c, d;
    u8  rep;

    if (REG8(0x11A7C8UL) & 0x80) {
        if (REG8(0x11A7CBUL) & 0x80) REG8(0x114DCDUL) |= 0x10;
    } else {
        REG8(0x114DCDUL) &= (u8)~0x10;
    }

    a = (u16)((u16)(u8)(REG8(0x11A7C8UL) & 0x3F) << 8);
    b = (u16)((u16)(u8)(REG8(0x11A7CBUL) & 0x3F) << 8);
    SPAN_B = b;
    c = (u16)((u16)(u8)(REG8(0x11A7CEUL) & 0x3F) << 8);
    d = (u16)((u16)(u8)(REG8(0x11A7D1UL) & 0x3F) << 8);
    SPAN_D = d;
    SPAN_C = c;
    SPAN_A = a;

    rep = REG8(0x11A7DEUL);

    if (c > a) { REG8(0x114DCDUL) &= (u8)~0x20; STEP_P = (u16)((u32)(u16)(c - a) / rep); }
    else       { REG8(0x114DCDUL) |=       0x20; STEP_P = (u16)((u32)(u16)(a - c) / rep); }

    if (d > b) { REG8(0x114DCDUL) &= (u8)~0x40; STEP_Q = (u16)((u32)(u16)(d - b) / rep); }
    else       { REG8(0x114DCDUL) |=       0x40; STEP_Q = (u16)((u32)(u16)(b - d) / rep); }

    if (b > a) { REG8(0x114DCDUL) &= (u8)~0x80; STEP_R = (u16)((u32)(u16)(b - a) / rep); }
    else       { REG8(0x114DCDUL) |=       0x80; STEP_R = (u16)((u32)(u16)(a - b) / rep); }
}

/* H'204CCA. Where the needle goes on step H'11A689 of a repeat, walking
 * between the first two positions. */
u8 needle_step(void)
{
    u8 i   = REG8(0x11A689UL);
    u8 rep = REG8(0x11A7DEUL);
    u8 f   = REG8(0x114DCDUL);

    if (i == 0x00) {
        stitch_spans_build();
        return REG8(0x11A68AUL);
    }
    if (i == 0x01) {
        return (f & 0x80) ? fixed_round((u16)(SPAN_A - STEP_R))
                          : fixed_round((u16)(SPAN_A + STEP_R));
    }
    if (i == 0x02) {
        if (rep == 0x03) {
            return (f & 0x80) ? fixed_round((u16)(STEP_R + SPAN_B))
                              : fixed_round((u16)(SPAN_B - STEP_R));
        }
        return (f & 0x80) ? fixed_round((u16)(SPAN_A - (u16)(STEP_R << 1)))
                          : fixed_round((u16)((u16)(STEP_R << 1) + SPAN_A));
    }
    if (i == 0x03) {
        if (rep == 0x04) {
            return (f & 0x80) ? fixed_round((u16)(STEP_R + SPAN_B))
                              : fixed_round((u16)(SPAN_B - STEP_R));
        }
        return (f & 0x80) ? fixed_round((u16)((u16)(STEP_R << 1) + SPAN_B))
                          : fixed_round((u16)(SPAN_B - (u16)(STEP_R << 1)));
    }
    if (i == 0x04) {
        return (f & 0x80) ? fixed_round((u16)(STEP_R + SPAN_B))
                          : fixed_round((u16)(SPAN_B - STEP_R));
    }
    return REG8(0x11A68AUL);
}

/* H'204E6A. The same for a mirrored pattern, which walks all four positions
 * rather than two, so there are ten steps rather than five. */
u8 needle_step_mirrored(void)
{
    u8 i   = REG8(0x11A689UL);
    u8 rep = REG8(0x11A7DEUL);
    u8 f   = REG8(0x114DCDUL);

    switch (i) {
    case 0x00:
        stitch_spans_build();
        return REG8(0x11A68AUL);
    case 0x01:
        return REG8(0x11A68CUL);
    case 0x02:
        return (f & 0x20) ? fixed_round((u16)(SPAN_A - STEP_P))
                          : fixed_round((u16)(STEP_P + SPAN_A));
    case 0x03:
        return (f & 0x40) ? fixed_round((u16)(SPAN_B - STEP_Q))
                          : fixed_round((u16)(STEP_Q + SPAN_B));
    case 0x04:
        if (rep == 0x03) {
            return (f & 0x20) ? fixed_round((u16)(STEP_P + SPAN_C))
                              : fixed_round((u16)(SPAN_C - STEP_P));
        }
        return (f & 0x20) ? fixed_round((u16)(SPAN_A - (u16)(STEP_P << 1)))
                          : fixed_round((u16)((u16)(STEP_P << 1) + SPAN_A));
    case 0x05:
        if (rep == 0x03) {
            return (f & 0x40) ? fixed_round((u16)(STEP_Q + SPAN_D))
                              : fixed_round((u16)(SPAN_D - STEP_Q));
        }
        return (f & 0x40) ? fixed_round((u16)(SPAN_B - (u16)(STEP_Q << 1)))
                          : fixed_round((u16)((u16)(STEP_Q << 1) + SPAN_B));
    case 0x06:
        if (rep == 0x04) {
            return (f & 0x20) ? fixed_round((u16)(STEP_P + SPAN_C))
                              : fixed_round((u16)(SPAN_C - STEP_P));
        }
        return (f & 0x20) ? fixed_round((u16)((u16)(STEP_P << 1) + SPAN_C))
                          : fixed_round((u16)(SPAN_C - (u16)(STEP_P << 1)));
    case 0x07:
        if (rep == 0x04) {
            return (f & 0x40) ? fixed_round((u16)(STEP_Q + SPAN_D))
                              : fixed_round((u16)(SPAN_D - STEP_Q));
        }
        return (f & 0x40) ? fixed_round((u16)((u16)(STEP_Q << 1) + SPAN_D))
                          : fixed_round((u16)(SPAN_D - (u16)(STEP_Q << 1)));
    case 0x08:
        return (f & 0x20) ? fixed_round((u16)(STEP_P + SPAN_C))
                          : fixed_round((u16)(SPAN_C - STEP_P));
    case 0x09:
        return (f & 0x40) ? fixed_round((u16)(STEP_Q + SPAN_D))
                          : fixed_round((u16)(SPAN_D - STEP_Q));
    default:
        return REG8(0x11A68AUL);
    }
}

/* H'205266. The needle's own position for this stitch.
 *
 * H'11A7BE's low three bits are how wide the swing is in whole units, and
 * H'11A7D9 the width the operator has set -- capped at H'38 on the H'B4 data
 * set, and scaled by H'43/H'64 when the pattern asks. From those come the
 * two ends of the swing, either side of H'19.
 *
 * Then the same thirteen codes the feed uses, and again most of them are a
 * fixed place rather than a proportion. The last case is the one that
 * matters: it is a plain position, walked through a repeat if there is one,
 * turned round if the pattern is mirrored, and finally interpolated between
 * the two ends by sew_interpolate.
 */
void needle_span_set(void)
{
    u8 span = (u8)(REG8(0x11A7BEUL) & 0x07);
    u8 trim = 0;
    u8 wide = REG8(0x11A7D9UL);
    u8 lo, hi, half, idx, code, k;

    if (STITCH_SET == 0xB4 && !(REG8(0xFFFEF8UL) & 0x10)) {
        if (wide > 0x38) wide = 0x38;
    }

    if (!(REG8(0x11A7D4UL) & 0x04) && (REG8(0x11A7BCUL) & 0x20)) {
        wide = (u8)((short)((short)(u16)((u16)0x43 * (u16)wide)
                            / (short)0x0064));
    }

    if (wide >= 0x42 && (REG8(0xFFFEF8UL) & 0x10)) span = (u8)(span + 1);

    if (REG8(0x11A7BEUL) & 0x10) {
        lo = (u8)(0x19 - (u8)(span / 2));
        hi = (u8)(lo + span);
    } else {
        hi = (u8)((u8)(span / 2) + 0x19);
        lo = (u8)(hi - span);
    }

    half = (u8)(wide / 2);
    code = REG8(0x11A7C8UL);
    idx  = (u8)((u8)(code & 0x3F) + 0xCD);

    switch (idx) {
    case 0x00:
        REG8(0x11A6B6UL) = (u8)(lo - (u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)));
        break;
    case 0x04:
        half = (u8)((short)((short)((short)(u16)((u16)0x57 * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)(lo - (u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)));
        break;
    case 0x05:
        half = (u8)((short)((short)((short)(u16)((u16)0x4B * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)(lo - (u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)));
        break;
    case 0x06: REG8(0x11A6B6UL) = (u8)(lo + 0xFD); break;
    case 0x01: REG8(0x11A6B6UL) = lo;              break;
    case 0x08: REG8(0x11A6B6UL) = 0x19;            break;
    case 0x02: REG8(0x11A6B6UL) = hi;              break;
    case 0x07: REG8(0x11A6B6UL) = (u8)(hi + 3);    break;
    case 0x09:
        half = (u8)((short)((short)((short)(u16)((u16)0x4B * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)((u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)) + hi);
        break;
    case 0x0A:
        half = (u8)((short)((short)((short)(u16)((u16)0x57 * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)((u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)) + hi);
        break;
    case 0x03:
        REG8(0x11A6B6UL) = (u8)((u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)) + hi);
        break;
    case 0x0B: REG8(0xFFFEFAUL) |= 0x10; break;
    case 0x0C: break;

    default:
        REG8(0x11A6B6UL) = (u8)(code & 0x3F);

        if (REG8(0x11A7DEUL) != 0) {
            if (REG8(0x114DCDUL) & 0x10) REG8(0x11A6B6UL) = needle_step_mirrored();
            else                          REG8(0x11A6B6UL) = needle_step();
        }

        if (!(stitch_run_head(REG16(0x11A7E6UL)) == 0x0011 &&
              (REG8(0x11A7BCUL) & 0x04))) {
            k = REG8(0x11A7E1UL);
            if (k == 0x01 || k == 0x03) {
                REG8(0x11A6B6UL) = (u8)(0x32 - REG8(0x11A6B6UL));
            }
        }

        if ((REG8(0x114DCEUL) & 0x80) && (REG8(0x11A7BCUL) & 0x20) &&
            stitch_run_head(REG16(0x11A7E6UL)) != 0x0015 &&
            (REG8(0x114DCAUL) & 0x02)) {
            trim = 0x16;
        }

        REG8(0x11A6B6UL) = (u8)(sew_interpolate(
                                    (u8)(REG8(0x11A6B6UL) << 1), wide, trim)
                                / 2);
        break;
    }

    REG8(0x11A6B7UL) = (u8)(REG8(0x11A6B6UL) + 2);
}

/* H'207988. A whole pass of the sewing side: the panel, the once-a-pass
 * service, the stop check, the interpolation offset, and then -- only when
 * nothing is mid-rebuild -- the geometry that turns the current stitch into
 * the two motor positions. The other data set has no geometry of its own and
 * parks everything at H'FF. */
void sew_pass(void)
{
    panel_service();
    sew_service();
    needle_stop_service();
    sew_offset_set();

    if (REG8(0x11A66AUL) != 0) return;

    needle_span_set();
    feed_position_set();

    if (STITCH_SET == 0xB4) {
        needle_position_apply();
        sew_limit_6BB();
    } else {
        REG8(0xFFFED1UL) = 0xFF;
        REG8(0x11A6BBUL) = 0xFF;
        REG8(0xFFFED7UL) = 0xFF;
        REG8(0x11A6B9UL) = 0xFF;
        REG8(0x11A6BAUL) = 0xFF;
    }
}

/* H'207F74. The live parameters written back into the working copies.
 *
 * Eight of them are watched against the copy at H'11A7F0, and only a real
 * change is written through -- every write is a flash-backed setting for
 * that pattern, and the panel calls this on every pass.
 *
 * A change does not necessarily belong to one pattern. Byte +H'17 of a
 * descriptor marks a pattern as a continuation of the one before it, so a
 * run of them is one thing as far as the operator is concerned; the walk
 * forward and back finds the head of the run, and if the head is marked
 * H'10 the change goes to every member rather than to the one.
 */
void sew_params_writeback(u16 n)
{
    u16 first = n;
    u8  changed = 0;
    u8  count = 0;
    u8  mark;
    u8  i;

    mark = REG8(stitch_record(n) + 0x17);

    if (REG16(0x11A7FAUL) != n) {
        REG8(0x11A7F8UL) = 0; REG8(0x11A7F7UL) = 0; REG8(0x11A7F6UL) = 0;
        REG8(0x11A7F5UL) = 0; REG8(0x11A7F4UL) = 0; REG8(0x11A7F3UL) = 0;
        REG8(0x11A7F2UL) = 0; REG8(0x11A7F1UL) = 0; REG8(0x11A7F0UL) = 0;
    }

    if (REG8(0x11A69EUL) != REG8(0x11A7F0UL)) {
        REG8(0x11A7F0UL) = REG8(0x11A69EUL); changed = 1;
    }
    if (REG8(0x11A7BDUL) & 0x40) {
        if (REG8(0x11A6A0UL) != REG8(0x11A7F2UL)) {
            REG8(0x11A7F2UL) = REG8(0x11A6A0UL); changed = 1;
        }
    } else {
        if (REG8(0x11A6A0UL) != REG8(0x11A7F1UL)) {
            REG8(0x11A7F1UL) = REG8(0x11A6A0UL); changed = 1;
        }
    }
    if (REG8(0xFFFEEAUL) != REG8(0x11A7F3UL)) {
        REG8(0x11A7F3UL) = REG8(0xFFFEEAUL); changed = 1;
    }
    if (REG8(0xFFFEECUL) != REG8(0x11A7F4UL)) {
        REG8(0x11A7F4UL) = REG8(0xFFFEECUL); changed = 1;
    }
    if (REG8(0x11A7BDUL) & 0x40) {
        if (REG8(0xFFFEFCUL) != REG8(0x11A7F6UL)) {
            REG8(0x11A7F6UL) = REG8(0xFFFEFCUL); changed = 1;
        }
    } else {
        if (REG8(0xFFFEFCUL) != REG8(0x11A7F5UL)) {
            REG8(0x11A7F5UL) = REG8(0xFFFEFCUL); changed = 1;
        }
    }
    if (REG8(0xFFFEFBUL) != REG8(0x11A7F7UL)) {
        REG8(0x11A7F7UL) = REG8(0xFFFEFBUL); changed = 1;
    }
    if (REG8(0x11A7ACUL) != REG8(0x11A7F8UL)) {
        REG8(0x11A7F8UL) = REG8(0x11A7ACUL); changed = 1;
    }

    if (changed == 0 && REG16(0x11A7FAUL) == n) return;

    REG16(0x11A7FAUL) = n;

    do {
        n = (u16)(n + 1);
        mark = REG8(stitch_record(n) + 0x17);
    } while (mark == 0x01);

    do {
        count = (u8)(count + 1);
        n = (u16)(n - 1);
        mark = REG8(stitch_record(n) + 0x17);
    } while (mark == 0x01);

    if (mark == 0x10) {
        for (i = 0; i < count; i++) {
            stitch_params_set(0, n,
                              1, REG8(0x11A69EUL), 1, REG8(0x11A6A0UL),
                              1, REG8(0xFFFEEAUL), 1, REG8(0xFFFEECUL),
                              1, REG8(0xFFFEFCUL), 1, REG8(0xFFFEFBUL),
                              1, REG8(0x11A7ACUL));
            n = (u16)(n + 1);
        }
    } else {
        stitch_params_set(0, first,
                          1, REG8(0x11A69EUL), 1, REG8(0x11A6A0UL),
                          1, REG8(0xFFFEEAUL), 1, REG8(0xFFFEECUL),
                          1, REG8(0xFFFEFCUL), 1, REG8(0xFFFEFBUL),
                          1, REG8(0x11A7ACUL));
    }
}

/* H'206EB4. Once a second -- H'114DDE counts milliseconds -- three of the
 * numbers the panel shows get bit 7 set. That bit is what tells the display
 * the value has been refreshed rather than left over. */
void display_refresh_tick(void)
{
    u8 v;

    if (REG16(0x114DDEUL) < 0x03E8) return;

    REG16(0x114DDEUL) = 0;
    REG8(0xFFFEE5UL) = (u8)(REG8(0xFFFEE5UL) + 0x80);
    REG8(0xFFFEE8UL) = (u8)(REG8(0xFFFEE8UL) + 0x80);
    REG8(0xFFFEEDUL) = (u8)(REG8(0xFFFEEDUL) + 0x80);

    v = REG8(0x114DC7UL);
    if (v & 0x40) REG8(0x114DC7UL) = (u8)(v & ~0x40);
}

/* H'204C86. The two knob values out to the panel. Bit 7 of H'FFFEE8 is the
 * refresh bit above and is kept; a value over H'64 goes across without its
 * companion. */
void display_params_publish(void)
{
    u8 v;

    sew_display_scale(REG8(0x11A69EUL));

    v = REG8(0x11A6A0UL);
    if (v > 0x64) {
        REG8(0xFFFEE7UL) = 0x64;
    } else {
        REG8(0xFFFEE7UL) = v;
        REG8(0xFFFEE8UL) = (u8)((u8)(REG8(0xFFFEE8UL) & 0x80) |
                                REG8(0x11A6A1UL));
    }
}

/* H'207A58. The widest the needle may swing, which depends on which presser
 * foot is fitted -- H'11A7E2 is the foot, one to eight -- and on which data
 * set the machine is running. Three tables, one per set, and a foot outside
 * the range means no limit beyond the pattern's own.
 *
 * The argument says whether this is a fresh selection. On a fresh one the
 * width is left alone; otherwise a foot that has just appeared pulls the
 * width back to what the pattern asks for, which is what stops a wide stitch
 * being sewn into a narrow foot.
 */
static const u8 sew_width_alt[8]  = { 0x4D, 0x4B, 0x3F, 0x33,
                                      0x26, 0x1A, 0x0E, 0x00 };
static const u8 sew_width_setA[8] = { 0x3A, 0x3C, 0x28, 0x14,
                                      0x00, 0x00, 0x00, 0x00 };
static const u8 sew_width_setB[8] = { 0x23, 0x24, 0x18, 0x0C,
                                      0x00, 0x00, 0x00, 0x00 };

void sew_width_limit_set(u8 fresh)
{
    u8 foot = REG8(0x11A7E2UL);
    u8 idx  = (u8)(foot + 0xFF);
    u8 v;

    REG8(0x11A670UL) = REG8(0x11A7C0UL);
    REG8(0x11A671UL) = REG8(0x11A7C2UL);

    if (foot != 0) {
        REG8(0x114DCFUL) |= 0x01;
        if (fresh == 0) REG8(0xFFFEEAUL) = 0x05;
        REG8(0x11A7D8UL) = 0x05;
    }

    if (idx <= 0x07) {
        if (REG8(0xFFFEF8UL) & 0x10)   REG8(0x11A670UL) = sew_width_alt[idx];
        else if (STITCH_SET == 0xAA)   REG8(0x11A670UL) = sew_width_setA[idx];
        else                           REG8(0x11A670UL) = sew_width_setB[idx];
        return;
    }

    REG8(0x11A670UL) = REG8(0x11A7E3UL);

    v = REG8(0x114DCFUL);
    if (!(v & 0x01)) return;
    REG8(0x114DCFUL) = (u8)(v & ~0x01);
    if (fresh != 0) return;

    REG8(0x11A7D9UL) = REG8(0x11A7BFUL);
    REG8(0x11A69EUL) = REG8(0x11A7BFUL);
    sew_param_a_set(REG8(0x11A7BFUL));
    REG8(0x11A7D8UL) = (u8)(REG8(0x11A7C3UL) & 0x0F);
    REG8(0xFFFEEAUL) = (u8)(REG8(0x11A7C3UL) & 0x0F);
}

/* H'203EBC. Steps the variant on by itself.
 *
 * On a pattern with more than one variant the machine can be left to run
 * through them, and what decides when to move on is either the free-running
 * counter at H'FFFF00 against the four limits, or -- when H'11A7D7 bit 0
 * says the pattern is timed rather than measured -- a count of stitches.
 * H'11A7AD is which of the four limits is being watched.
 *
 * The window is worked out from two bytes on the bus scaled by H'64/H'0F,
 * which is how far the machine travels in the time it takes to decide.
 * H'11A669 is set three steps ahead of a change so the display has warning.
 */
void variant_advance(void)
{
    u16 now  = REG16(0xFFFF00UL);
    u16 sum  = (u16)((u16)bus_byte_10() + (u16)bus_byte_11());
    u16 span = (u16)((u32)((u32)0x0064 * (u32)sum) / 0x000FUL);
    u8  st;

    if (REG8(0xFFFEFDUL) == 0x02) return;

    if (!(REG8(0x114DC6UL) & 0x08) && !(REG8(0x114DCBUL) & 0x20)) {
        if ((u16)(REG16(0x11A7AEUL) + span) > now) return;
        if (!(REG8(0x11A7BDUL) & 0x02)) return;
    }

    if (REG8(0x11A7D7UL) & 0x01) {
        if (REG8(0x114DCBUL) & 0x20) {
            REG8(0x114DCBUL) &= (u8)~0x20;
            REG8(0x11A7A8UL) = (u8)(REG8(0x11A7A8UL) + 1);
            if (REG8(0x11A7A8UL) >= REG8(0x11A7AAUL)) REG8(0x11A7A8UL) = 0;

            if (REG16(0x11A698UL) >= 0x0002) {
                if (REG8(0x11A7ADUL) == 0x01) {
                    REG16(0x11A69AUL) = REG16(0x11A698UL);
                    REG16(0x11A69CUL) = REG16(0x11A698UL);
                    REG8(0x11A7ADUL) = 0;
                    if (REG8(0x11A7ABUL) == 0x01) {
                        REG8(0x114DC6UL) |= 0x08;
                        REG8(0xFFFEFAUL) |= 0x80;
                        REG8(0xFFFEF7UL) &= (u8)~0x08;
                    }
                } else {
                    REG16(0x11A69CUL) = REG16(0x11A698UL);
                    REG8(0x11A7ADUL) = 0x01;
                    REG8(0x114DC6UL) |= 0x08;
                    REG8(0xFFFEFAUL) |= 0x80;
                    REG8(0xFFFEF7UL) &= (u8)~0x08;
                }
                REG16(0x11A698UL) = 0;
            }
            return;
        }

        {
            u16 edge = (REG8(0x11A7ADUL) == 0) ? REG16(0x11A69CUL)
                                               : REG16(0x11A69AUL);
            if ((u16)(REG16(0x11A698UL) + 1) >= edge) {
                REG8(0x11A7ADUL) = (u8)((REG8(0x11A7ADUL) == 0) ? 0x01 : 0x00);
                variant_step();
                REG16(0x11A698UL) = 0;
            } else if ((u16)(REG16(0x11A698UL) + 3) >= edge) {
                REG8(0x11A669UL) = 0x01;
            } else {
                REG8(0x11A669UL) = 0x00;
            }
        }
        return;
    }

    if (!(REG8(0x114DC6UL) & 0x08)) {
        REG8(0x114DCBUL) &= (u8)~0x20;
        REG8(0x11A7A8UL) = (u8)(REG8(0x11A7A8UL) + 1);
        if (REG8(0x11A7A8UL) >= REG8(0x11A7AAUL)) REG8(0x11A7A8UL) = 0;

        if ((u16)(REG16(0x11A7AEUL) + 0x000A) <= now) {
            REG8(0x114DC6UL) |= 0x08;
            REG8(0xFFFEFAUL) |= 0x80;
            REG8(0xFFFEF7UL) &= (u8)~0x08;
            REG16(0x11A7B4UL) = (u16)(now - 1);
            REG16(0x11A7B0UL) = (u16)(now - 1);
            REG8(0x11A7ACUL) = (u8)((u8)(now - 1) - REG8(0x11A7AFUL));
            REG8(0x11A7ADUL) = 0x02;
        }
        return;
    }

    st = REG8(0x11A7ADUL);
    if (st == 0x00) {
        if (now <= REG16(0x11A7AEUL)) {
            REG8(0x11A7ADUL) = (u8)(st + 1);
            variant_step();
        } else if ((u16)(now + 0xFFFD) <= REG16(0x11A7AEUL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else if (st == 0x01) {
        if (now >= REG16(0x11A7B0UL)) {
            REG8(0x11A7ADUL) = (u8)(st + 1);
            variant_step();
        } else if ((u16)(now + 0x0003) <= REG16(0x11A7B0UL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else if (st == 0x02) {
        if (now <= REG16(0x11A7B2UL)) {
            REG8(0x11A7ADUL) = (u8)(st + 1);
            variant_step();
        } else if ((u16)(now + 0xFFFD) <= REG16(0x11A7B2UL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else if (st == 0x03) {
        if (now >= REG16(0x11A7B4UL)) {
            REG8(0x11A7ADUL) = 0;
            variant_step();
        } else if ((u16)(now + 0x0003) <= REG16(0x11A7B4UL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else {
        REG8(0x11A7ADUL) = 0;
    }
}

/* ---- the main motor's state machine -------------------------------------
 * H'FFFEC6 is what the motor is doing: 0 stopped, 1 running, 2 slowing, 3
 * hunting for the needle-up position, 4 braking, 5 the winder, 6 the
 * thread cutter. H'11A850 is how long it has been in that state, counted by
 * the timer handler, and H'11A84E the speed it is being asked for.
 *
 * ITU channel 3 is the position sensor's input capture; channel 4 is the
 * PWM. Both are stopped while their registers are changed, which is why
 * TSTR bit 4 goes down and up around every write to GRA4.
 */

/* H'20D904, H'20D90E. The PWM's compare-match A interrupt, which is what
 * drives the winder, on and off with the mask up. */
void pwm4_matcha_on(void)
{
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    TIER4 |= 0x01;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

void pwm4_matcha_off(void)
{
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    TIER4 &= (u8)~0x01;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

/* H'20D918. ITU channel 3 set up to capture the position sensor. */
void position_capture_init(void)
{
    u8 v;

    TSNC &= (u8)~0x08;
    TMDR &= (u8)~0x08;
    TOER = (u8)(TOER & 0xF6);
    REG8(0xFFFF82UL) = 0x28;          /* TCR3: count at the clock */

    v = (u8)(PBDDR_SHADOW & ~0x01);
    PBDDR = v;
    PBDDR_SHADOW = v;
    PBDR &= (u8)~0x01;

    REG8(0xFFFF83UL) = 0x55;          /* TIOR3: capture on both edges */
    REG16(0xFFFF88UL) = 0xFFFF;       /* GRA3 */
    REG16(0xFFFF8AUL) = 0xFFFF;       /* GRB3 */
    REG8(0xFFFF84UL) = 0x00;          /* TIER3 */
    TSTR |= 0x08;
}

/* H'20D95C, H'20D98C. The capture interrupts on and off. */
void position_capture_on(void)
{
    position_capture_init();
    REG8(0x11A852UL) |= 0x10;
    REG16(0xFFFECEUL) = 0;
    REG8(0x11A84FUL) = 0;
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    REG8(0xFFFF84UL) |= 0x01;
    REG8(0xFFFF84UL) |= 0x04;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

void position_capture_off(void)
{
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    REG8(0xFFFF84UL) &= (u8)~0x01;
    REG8(0xFFFF84UL) &= (u8)~0x04;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

/* H'20DB1A. The brake, off and straight back on -- a pulse on P6 bit 2. */
void motor_brake_pulse(void)
{
    P6DR &= (u8)~0x04;
    P6DR |=        0x04;
}

/* H'20DA2E. The speed out to the PWM. Below H'0A the mark is parked past
 * the period, which is off; above it the mark is worked back from the
 * period, and there are two scales because the period itself differs
 * between the sewing motor and the winder. */
void main_motor_speed_set(u8 speed)
{
    TSTR &= (u8)~0x10;

    if (speed < 0x0A) {
        GRA4 = 0x2B98;
    } else if (GRB4 == 0x2B34) {
        GRA4 = (u16)(0x2B34 - (u16)((u16)((u16)speed - 1) * (u16)0x002B));
    } else {
        GRA4 = (u16)(0x0E67 - (u16)((u16)((u16)speed - 1) * (u16)0x000E));
    }

    TSTR |= 0x10;
}

/* H'20DB24. One pass of the state machine. */
void main_motor_service(void)
{
    u8 mode = REG8(0xFFFEC0UL);
    u8 st, v;

    main_motor_speed_set(REG8(0x11A84EUL));

    st = REG8(0xFFFEC6UL);

    if (st == 0x00) {
        P4DR |= 0x01;
        REG8(0x11A84EUL) = 0;

        if (REG8(0xFFFEC7UL) & 0x80) {
            if (REG8(0xFFFEC3UL) != 0) {
                REG8(0xFFFEC6UL) = 0x06;
                motor_brake_pulse();
                P4DR &= (u8)~0x01;
                position_capture_on();
                REG16(0x11A850UL) = 0;
                return;
            }
        } else if (REG8(0xFFFEC7UL) & 0x01) {
            if (!((REG8(0xFFFEC4UL) & 0x01) &&
                  REG8(0x114D62UL) < 0x0D)) {
                GRA4 = 0x2B98;
                P4DR |= 0x01;
                TSTR &= (u8)~0x10;
                TMDR &= (u8)~0x10;
                TSTR |=        0x10;
                REG8(0xFFFEC6UL) = 0x05;
            }
        } else if (REG8(0xFFFEC3UL) == 0x01) {
            REG8(0xFFFEC6UL) = 0x03;
            P4DR &= (u8)~0x01;
            motor_brake_pulse();
            REG8(0x11A852UL) |= 0x04;
            REG16(0x11A850UL) = 0;
        } else if (REG8(0xFFFEC3UL) >= 0x03 && REG8(0xFFFEC3UL) <= 0x05) {
            REG8(0xFFFEC6UL) = 0x01;
            P4DR &= (u8)~0x01;
            motor_brake_pulse();
        }
        REG16(0x11A850UL) = 0;
        return;
    }

    if (st == 0x01) {
        REG8(0x11A84EUL) = REG8(0xFFFEC8UL);
        if (REG8(0xFFFEC7UL) & 0x40) {
            REG8(0xFFFEC6UL) = 0x02;
            P4DR |= 0x01;
        } else if (REG8(0xFFFEC3UL) < 0x03) {
            REG8(0xFFFEC6UL) = 0x02;
            P4DR |= 0x01;
        } else if (adc_get_result(3) >= 0x32) {
            /* still turning: nothing to do */
        } else {
            if (REG16(0x11A850UL) > 0x1388) {
                REG8(0xFFFEC6UL) = 0x04;
                P4DR |= 0x01;
                motor_brake_pulse();
            }
            return;
        }
        REG16(0x11A850UL) = 0;
        return;
    }

    if (st == 0x02) {
        REG8(0x11A84EUL) = REG8(0xFFFECAUL);
        if (adc_get_result(5) < 0x16) {
            REG8(0xFFFEC6UL) = 0x03;
            P4DR &= (u8)~0x01;
            REG16(0x11A850UL) = 0;
        } else if (REG16(0x11A850UL) > 0x03E8) {
            REG8(0xFFFEC6UL) = 0x03;
            P4DR &= (u8)~0x01;
            REG16(0x11A850UL) = 0;
        } else if (REG8(0xFFFEC3UL) == 0x03 && !(REG8(0xFFFEC7UL) & 0x40)) {
            REG8(0xFFFEC6UL) = 0x01;
            P4DR &= (u8)~0x01;
        }
        return;
    }

    if (st == 0x03) {
        REG8(0x11A84EUL) = REG8(0xFFFECAUL);

        if (REG16(0x11A850UL) > 0x0028) {
            if ((REG8(0xFFFEC7UL) & 0x02) || (REG8(0x11A852UL) & 0x04)) {
                v = REG8(0x11A852UL);
                if (v & 0x01) {
                    if (mode == 0x04) {
                        REG8(0x11A852UL) = (u8)(v & ~0x07);
                        P4DR |= 0x01;
                        motor_brake_pulse();
                        REG8(0xFFFEC6UL) = 0x04;
                    }
                } else if (mode == 0x06) {
                    REG8(0x11A852UL) = (u8)(v | 0x01);
                }
            }
            if ((REG8(0xFFFEC7UL) & 0x04) || (REG8(0x11A852UL) & 0x04)) {
                v = REG8(0x11A852UL);
                if (v & 0x02) {
                    if (mode == 0x02) {
                        REG8(0x11A852UL) = (u8)(v & ~0x07);
                        P4DR |= 0x01;
                        motor_brake_pulse();
                        REG8(0xFFFEC6UL) = 0x04;
                    }
                } else if (mode == 0x01) {
                    REG8(0x11A852UL) = (u8)(v | 0x02);
                }
            }
        }

        if (REG16(0x11A850UL) > 0x03E8) {
            P4DR |= 0x01;
            motor_brake_pulse();
            REG8(0xFFFEC6UL) = 0x04;
            REG8(0x11A852UL) &= (u8)~0x04;
        }
        if (REG8(0xFFFEC3UL) > 0x02 && !(REG8(0xFFFEC7UL) & 0x40)) {
            REG8(0xFFFEC6UL) = 0x01;
            P4DR &= (u8)~0x01;
            REG8(0x11A852UL) &= (u8)~0x04;
        }
        return;
    }

    if (st == 0x04) {
        P4DR &= (u8)~0x01;
        REG16(0x11A850UL) = 0;
        REG8(0x11A84EUL) = 0;
        if (REG8(0xFFFEC3UL) == 0) REG8(0xFFFEC6UL) = 0x00;
        return;
    }

    if (st == 0x05) {
        v = REG8(0xFFFEC8UL);
        if (v > 0x0A && !(REG8(0x11A852UL) & 0x20)) {
            TSTR &= (u8)~0x10;
            pwm4_matcha_on();
            TSTR |= 0x10;
            REG8(0x11A852UL) |= 0x20;
        }
        if (REG8(0xFFFEC8UL) <= 0x0A) {
            TSTR &= (u8)~0x10;
            pwm4_matcha_off();
            TSTR |= 0x10;
            REG8(0x11A852UL) &= (u8)~0x20;
        }
        v = REG8(0xFFFEC8UL);
        REG8(0x11A84EUL) = (v > 0xDC) ? 0xDC : v;
        REG16(0x11A850UL) = 0;

        if (!(REG8(0xFFFEC7UL) & 0x01)) {
            if (!((REG8(0xFFFEC4UL) & 0x01) && REG8(0x114D62UL) < 0x0D)) {
                REG8(0x11A852UL) &= (u8)~0x20;
                GRA4 = 0x2B98;
                TSTR &= (u8)~0x10;
                pwm4_matcha_off();
                PBDR &= (u8)~0x08;
                TMDR |= 0x10;
                TSTR |= 0x10;
                REG8(0xFFFEC6UL) = 0x04;
            }
        }
        return;
    }

    if (st == 0x06) {
        REG8(0x11A84EUL) = REG8(0xFFFEC8UL);
        REG8(0xFFFECDUL) = (u8)((u32)((u32)(u16)(0x012C -
                                (u16)adc_get_result(3)) *
                                (u32)REG16(0xFFFECEUL)) / 0x03F0UL);
        if (!(REG8(0xFFFEC7UL) & 0x80)) {
            REG8(0xFFFEC6UL) = 0x04;
            P4DR |= 0x01;
            motor_brake_pulse();
            position_capture_off();
        } else {
            REG16(0x11A850UL) = 0;
        }
    }
}

/* Puts a message up. The front end of the display subsystem, and its own
 * part -- see the README. */

/* H'208210. The whole of "make the machine agree with what is selected",
 * called once a pass from the main loop and once from the bring-up.
 *
 * The first half decides whether the selection has moved far enough to be
 * worth rebuilding for, and calls pattern_make_current if it has. H'11A66A
 * is raised when the rebuild is under way, and everything downstream that
 * would use half-built state looks at it.
 *
 * The second half moves the parameters between the live set and the
 * pattern's own, in whichever direction H'11A175 says, and then runs the
 * sewing pass.
 */
void stitch_state_init(void)
{
    u16 sel;
    u16 keyed;
    u8  v;

    sew_running_flags();
    sew_params_scale_for_mode();
    sew_mode_arbitrate();
    REG8(0x11A66AUL) = 0;

    if ((REG8(0x114DCAUL) & 0x02) && QUEUE_POS != 0xFFFF) {
        int fresh = 1;

        sel = (u16)(queue_entry_ref(QUEUE_POS) & 0x03FF);

        if (QUEUE_POS != QUEUE_GROUP) {
            if ((queue_entry_ref(QUEUE_POS) & 0x03FF) != QUEUE_END &&
                QUEUE_POS == REG16(0x11A7FCUL)) {
                fresh = 0;
            }
        }
        if (fresh) {
            REG16(0x11A7FCUL) = QUEUE_POS;
            queue_group_start();
            REG8(0x11A66AUL) = 0x01;
        }

        if (QUEUE_LAST == QUEUE_FIRST) REG8(0x11A66AUL) = 0x01;

        REG16(0x11A7FEUL) = queue_entry_ref(0);

        if ((short)REG16(0x11A7FEUL) > (short)REG16(0x11A66CUL)) {
            if (QUEUE_POS != QUEUE_FIRST) {
                if (sel == 0x03FE || sel == 0x03FF) {
                    REG16(0x11A6C4UL) = sel;
                    REG16(0x11A7E6UL) = sel;
                } else {
                    keyed = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
                    REG16(0x11A7E6UL) = keyed;
                    REG16(0x11A6C4UL) = keyed;
                }
                REG8(0x114DCAUL) |= 0x80;
                REG8(0x114DCDUL) &= (u8)~0x08;
                REG8(0x114DC9UL) |= 0x01;
                REG8(0x114DC9UL) |= 0x04;
                pattern_make_current();
            }
        } else {
            keyed = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
            if (keyed != REG16(0x11A800UL)) {
                REG16(0x11A7E6UL) = keyed;
                REG16(0x11A6C4UL) = keyed;
                REG8(0x11A6AEUL) |= 0x01;
                REG8(0x114DCDUL) &= (u8)~0x08;
                pattern_make_current();
            }
        }

        REG16(0x11A66CUL) = REG16(0x11A7FEUL);
    }

    if ((REG8(0x114DCAUL) & 0x02) && QUEUE_POS != 0xFFFF) {
        if (REG8(0x114DC9UL) & 0x04) {
            if (REG8(0x11A175UL) == 0x01) {
                v = REG8(0x114DC7UL);
                if (!(v & 0x01)) {
                    REG8(0x114DC7UL) = (u8)(v | 0x01);
                    sew_params_save();
                    queue_entry_unpack();
                    sew_params_from_pattern();
                }
                sew_params_clamp(REG8(0x11A7E3UL), REG8(0x11A7E4UL));
            } else {
                v = REG8(0x114DC7UL);
                if (v & 0x01) {
                    REG8(0x114DC7UL) = (u8)(v & ~0x01);
                    sew_params_restore();
                } else {
                    sew_params_capture();
                    queue_entry_unpack();
                }
                sew_width_limit_set(0);
                sew_limits_apply();
            }
        } else {
            sew_flag_copy_6();
        }
    } else {
        sew_params_capture();
        sew_width_limit_set(0);
        sew_params_clamp(REG8(0x11A670UL), REG8(0x11A671UL));
        sew_limits_apply();
    }

    if (REG8(0xFFFEC4UL) & 0x01) {
        sew_mechanism_service();
    } else {
        if (!(REG8(0xFFFEC4UL) & 0x80)) {
            REG8(0x11A679UL) = 0;
            REG8(0x11A678UL) = 0;

            if (REG8(0xFFFEF7UL) & 0x02) {
                if (REG8(0x114DC6UL) & 0x80) {
                    REG8(0xFFFEC7UL) |= 0x40;
                } else {
                    message_show(0x0009);
                    REG8(0xFFFEF7UL) &= (u8)~0x02;
                }
            }
            if (REG8(0xFFFEF7UL) & 0x04) {
                if (!(REG8(0x114DC6UL) & 0x80)) {
                    message_show(0x001C);
                    REG8(0xFFFEF7UL) &= (u8)~0x04;
                }
            }
        }
        sew_pass();
    }

    display_refresh_tick();
    sew_counters_service();

    if (REG8(0xFFFEC4UL) & 0x01) {
        sew_params_writeback(0x03FD);
    } else {
        keyed = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
        if ((u8)keyed != 0 && REG16(0x11A7E6UL) != 0) {
            if (REG8(0x114DCAUL) & 0x02) {
                if (QUEUE_POS != 0xFFFF) {
                    if (!(REG8(0x114DC7UL) & 0x01) &&
                        (REG8(0x114DC9UL) & 0x04)) {
                        if (REG8(stitch_record(REG16(0xFFFEE0UL)) + 0x17)
                            != 0x04) {
                            keyed = (u16)(REG16(0xFFFEE0UL) +
                                          (u16)REG8(0xFFFEFDUL));
                            if (!(keyed >= 0x0016 && keyed <= 0x0019)) {
                                sew_params_writeback(
                                    (u16)(REG16(0xFFFEE0UL) +
                                          (u16)REG8(0xFFFEFDUL)));
                            }
                        }
                    }
                }
            } else {
                sew_params_writeback((u16)(REG16(0xFFFEE0UL) +
                                           (u16)REG8(0xFFFEFDUL)));
            }
        }
    }

    display_params_publish();
    REG16(0x11A800UL) = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
}
