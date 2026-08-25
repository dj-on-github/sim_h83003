/* The artista 180 application, rebuilt in C: the module's floating point,
 * and the geometry it works out.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

u32 f2u(float f) { f32bits b; b.f = f; return b.u; }
float u2f(u32 u) { f32bits b; b.u = u; return b.f; }

/* H'24AD22. Splits a float into a fraction in [H'0.5, 1) and a power of two,
 * the C library's frexp. The exponent is worked out a byte at a time and
 * comes back sign extended from eight bits. */
float float_frexp(float x, short *power)
{
    const u32 u = f2u(x);
    u16 hi;
    u8 e;

    if (u == 0) { *power = 0; return u2f(0); }

    hi = (u16)(u >> 16);
    e  = (u8)((u16)((u16)(hi << 1)) >> 8);
    e  = (u8)(e + 0x82);
    *power = (short)(signed char)e;

    return u2f((u & 0x0000FFFFUL)
               | ((u32)(u16)(((u16)(hi & 0x807F)) | 0x3F00) << 16));
}

/* H'24AD62. The whole part out into [ip] and the fraction back, the C
 * library's modf. Anything H'1E7 or bigger is all whole part. */
float float_modf(float x, float *ip)
{
    if ((long)(f2u(x) & 0x7FFFFFFFUL) >= (long)0x4B189680UL) {
        *ip = x;
        return u2f(0);
    }
    *ip = (float)(long)(int)x;
    return x - *ip;
}

/* H'24AE5A. The arc tangent of the size of a number, by a rational fit.
 * Above one the argument is folded to (a-1)/(a+1) and a quarter pi added
 * back at the end. */
float float_atan_core(float x)
{
    float a = u2f(f2u(x) & 0x7FFFFFFFUL);
    float hi, t, n, d;

    if ((long)f2u(a) <= (long)0x3F800000UL) {
        if (f2u(a) == 0x3F800000UL) return 0.7853982f;
        hi = 0.0f;
    } else {
        hi = 0.7853982f;
        a = (a + -1.0f) / (a + 1.0f);
    }

    t = a * a;
    n = ((t * -0.010497842f + 0.3247416f) * t + 2.9960995f) * t + 3.640485f;
    d = (t + 4.209584f) * t + 3.6404853f;

    return hi + a * (n / d);
}

/* H'24ABC4. The arc tangent proper: the size, and then the sign put back
 * unless the answer came out as a positive zero. */
float float_atan(float x)
{
    const float r = float_atan_core(x);

    if ((long)f2u(x) >= 0) return r;
    if ((u16)(f2u(r) >> 16) == 0) return r;
    return u2f(f2u(r) ^ 0x80000000UL);
}

/* H'24AF80. Sine and cosine share one routine: the argument is scaled by
 * two over pi, the whole quarter-turns taken off, and what is left run
 * through a five-term fit of sin(pi/2 x). [quad] is 0 for sine and 1 for
 * cosine, and a negative argument adds two more quarter turns.
 *
 * Above 65532 quarter-turns the reduction is done the long way, with two
 * calls to H'24AD62, because (float)(int) would have nothing left to say. */
float float_sincos(float x, u8 quad)
{
    u8 n = (u8)(quad + (((long)f2u(x) < 0) ? 0x02 : 0x00));
    float y = u2f(f2u(x) & 0x7FFFFFFFUL) * 0.63661975f;
    float f, t, p;

    if ((long)f2u(y) > (long)0x477FFC00UL) {
        float whole, quarters;

        f = float_modf(y, &whole);
        whole = whole + (float)(u32)n;
        float_modf(whole * 0.25f, &quarters);
        n = (u8)(int)(whole - quarters * 4.0f);
    } else {
        const int k = (int)y;

        f = y - (float)(u32)k;
        n = (u8)((u8)(n + (u8)k) & 0x03);
    }

    if (n & 0x01) f = 1.0f - f;
    if (n > 0x01) { if ((u16)(f2u(f) >> 16) != 0) f = u2f(f2u(f) ^ 0x80000000UL); }

    t = f * f;
    p = (((t * 0.00015148513f + -0.0046737664f) * t + 0.07968968f) * t
         + -0.6459637f) * t + 1.5707964f;

    return f * p;
}

/* H'24ABEE and H'24ADDC. Sine, and cosine of the size -- cosine is even, so
 * dropping the sign first costs nothing. */
float float_sin(float x) { return float_sincos(x, 0x00); }

float float_cos(float x)
{
    return float_sincos(u2f(f2u(x) & 0x7FFFFFFFUL), 0x01);
}

/* H'24ABFE. The square root: a first guess out of the mantissa, one
 * Newton step, the exponent halved by adding to the top half of the float,
 * and one more step. Nought comes back as nought; anything below it sets
 * H'11F5A6 to H'21 and comes back as the largest float there is. */
float float_sqrt(float x)
{
    short power;
    float m, e;

    if ((long)f2u(x) <= 0) {
        if (f2u(x) == 0) return u2f(0);
        REG16(0x0011F5A6UL) = 0x0021;
        return 3.4028235e+38f;
    }

    m = float_frexp(x, &power);
    e = (m + 0.75787f) * 0.57155f;
    e = e + m / e;

    /* the exponent is read back as a single byte, halved as a signed byte,
     * and the result shifted into the top half of the float */
    if (power & 0x0001) {
        const short q = (short)((short)(signed char)(u8)(power - 1) / 2);

        e = e * 1.4142135f;
        e = u2f((f2u(e) & 0x0000FFFFUL)
                | ((u32)(u16)((u16)(f2u(e) >> 16) + (u16)(short)(q << 7))
                   << 16));
    } else {
        const short q = (short)((short)(signed char)(u8)power / 2);

        e = u2f((f2u(e) & 0x0000FFFFUL)
                | ((u32)(u16)((u16)(f2u(e) >> 16) + (u16)(short)(q << 7))
                   << 16));
    }

    return x / e + e * 0.25f;
}

/* H'241228. Finds one pattern's header in the stream at H'10C27A and copies
 * it into the H'15-byte record the caller hands over.
 *
 * The stream is a run of blocks, each three bytes of kind, count and size
 * followed by its data: kind H'01 is count * size bytes and kind H'02 is
 * eight. Anything else, or running past H'1137AA, sets bit 1 of H'11A641
 * and gives up. The step of H'0C between blocks is byte H'14 of the record,
 * which this routine sets to H'0F itself and nothing else touches.
 *
 * The index is decremented in the caller's own stack slot, a store the
 * reconstruction cannot make; the cases leave that address out. */
u8 pattern_record_at(u8 *rec, u8 index)
{
    const u32 limit = 0x001137AAUL;
    u32 p = 0x0010C27AUL;
    u8 i;

    rec[0x14] = 0x0F;
    if (index != 0) index = (u8)(index - 1);

    for (i = 0; i < index; i++) {
        const u8 kind = REG8(p);
        const u8 count = REG8(p + 1);
        const u8 size = REG8(p + 2);

        p += 3;
        p = (u32)(p + (u32)(long)(short)(u16)((u16)rec[0x14] - 3));

        if (kind == 0x01)      p = (u32)(p + (u32)(u16)((u16)count * (u16)size));
        else if (kind == 0x02) p = p + 8;
        else { REG8(0x0011A641UL) |= 0x02; return 0x00; }

        if (p >= limit) { REG8(0x0011A641UL) |= 0x02; return 0x00; }
    }

    REG32((u32)(unsigned long)(rec + 0x10)) = p;

    rec[0] = REG8(p);
    rec[1] = REG8(p + 1);
    rec[2] = REG8(p + 2);
    rec[3] = REG8(p + 3);
    rec[4] = REG8(p + 4);

    REG16((u32)(unsigned long)(rec + 0x06)) =
        (u16)(((u16)REG8(p + 5) << 8) | (u16)REG8(p + 6));
    REG16((u32)(unsigned long)(rec + 0x08)) =
        (u16)(((u16)REG8(p + 7) << 8) | (u16)REG8(p + 8));
    REG16((u32)(unsigned long)(rec + 0x0A)) =
        (u16)(((u16)REG8(p + 9) << 8) | (u16)REG8(p + 10));
    REG16((u32)(unsigned long)(rec + 0x0C)) =
        (u16)(((u16)REG8(p + 11) << 8) | (u16)REG8(p + 12));
    REG16((u32)(unsigned long)(rec + 0x0E)) =
        (u16)(((u16)REG8(p + 13) << 8) | (u16)REG8(p + 14));

    return 0x01;
}

/* H'24217A. How far one pattern reaches from its own centre, along each
 * axis, once its own scale and rotation are applied.
 *
 * The pattern's header gives a width and a height in fifths of a
 * millimetre; H'11A24A and H'11A24B scale them (twice the byte, over a
 * hundred), H'11A250 turns them (five degrees a step, minus a hundred and
 * eighty, into radians) and H'11A24F says which way round.
 *
 * The four corners are worked out from the half-diagonal and the angle to
 * it, and the largest size along each axis is what comes back.
 *
 * When the scaled width is nought the angle to the corner is never worked
 * out and the ROM uses whatever the stack held; this starts it at nought,
 * and no case goes there. */
void pattern_half_extent(u16 *half_w, u16 *half_h, u8 slot)
{
    u8 rec[0x18];
    const u32 base = 0x0011A25AUL + (u32)(long)(short)(u16)((u16)slot << 4);
    const float sx = (float)(u32)REG8(base - 0x10) * 2.0f / 100.0f;
    const float sy = (float)(u32)REG8(base - 0x0F) * 2.0f / 100.0f;
    float hw, hh, angle, theta = 0.0f, r;
    float cx[4], cy[4];
    const u8 flip = REG8(base - 0x0B);
    short i;

    pattern_record_at(rec, slot);

    hw = (float)(long)(short)REG16((u32)(unsigned long)(rec + 0x0A)) / 5.0f;
    hh = (float)(long)(short)REG16((u32)(unsigned long)(rec + 0x0C)) / 5.0f;
    hw = hw * sx;
    hh = hh * sy;

    angle = ((float)(u32)REG8(0x0011A250UL) * 5.0f + -180.0f) * 0.017453277f;

    if (f2u(hw) != 0) theta = float_atan(hh / hw);

    r = float_sqrt(hh * hh + hw * hw) / 2.0f;

    {
        const float nr = ((u16)(f2u(r) >> 16) != 0)
                         ? u2f(f2u(r) ^ 0x80000000UL) : r;
        const float a  = theta - angle;
        const float b  = (((u16)(f2u(theta) >> 16) != 0)
                          ? u2f(f2u(theta) ^ 0x80000000UL) : theta) - angle;

        if (flip == 0) {
            cx[0] = float_cos(a) * r;   cy[0] = float_sin(a) * r;
            cx[1] = float_cos(b) * r;   cy[1] = float_sin(b) * r;
            cx[2] = nr * float_cos(a);  cy[2] = nr * float_sin(a);
            cx[3] = nr * float_cos(b);  cy[3] = nr * float_sin(b);
        } else {
            cx[0] = nr * float_cos(a);  cy[0] = float_sin(a) * r;
            cx[1] = nr * float_cos(b);  cy[1] = float_sin(b) * r;
            cx[2] = float_cos(a) * r;   cy[2] = nr * float_sin(a);
            cx[3] = float_cos(b) * r;   cy[3] = nr * float_sin(b);
        }
    }

    *half_w = (u16)(int)u2f(f2u(cx[0]) & 0x7FFFFFFFUL);
    *half_h = (u16)(int)u2f(f2u(cy[0]) & 0x7FFFFFFFUL);

    for (i = 1; i < 4; i++) {
        const u16 x = (u16)(int)u2f(f2u(cx[i]) & 0x7FFFFFFFUL);
        const u16 y = (u16)(int)u2f(f2u(cy[i]) & 0x7FFFFFFFUL);

        if ((short)*half_w < (short)x) *half_w = x;
        if ((short)*half_h < (short)y) *half_h = y;
    }
}

/* H'241FA0. Every pattern in the run taken together, and the current slot's
 * centre moved to the middle of the box that holds them all. Each pattern is
 * asked for its own reach around the centre it is pinned to, and the centres
 * live sixteen bytes apart at H'11A266 and H'11A268. */
void pattern_run_extent(void)
{
    u16 hw = 0, hh = 0;
    short minx, maxx, miny, maxy;
    u8 i;

    pattern_half_extent(&hw, &hh, 0x01);

    minx = (short)(REG16(0x0011A266UL) - hw);
    maxx = (short)(REG16(0x0011A266UL) + hw);
    miny = (short)(REG16(0x0011A268UL) - hh);
    maxy = (short)(REG16(0x0011A268UL) + hh);

    for (i = 0; (short)(u16)REG8(0x0011A640UL) > (short)(signed char)i; i++) {
        const u32 at = 0x0011A266UL
                     + (u32)(long)(short)((short)(signed char)i << 4);

        pattern_half_extent(&hw, &hh, (u8)(i + 1));

        if ((short)(REG16(at) - hw) < minx) minx = (short)(REG16(at) - hw);
        if ((short)(REG16(at) + hw) > maxx) maxx = (short)(REG16(at) + hw);
        if ((short)(REG16(at + 2) - hh) < miny) miny = (short)(REG16(at + 2) - hh);
        if ((short)(REG16(at + 2) + hh) > maxy) maxy = (short)(REG16(at + 2) + hh);
    }

    {
        const u32 slot = 0x0011A266UL
                       + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4);

        REG16(slot)     = (u16)(short)(minx + (short)((short)(maxx - minx) / 2));
        REG16(slot + 2) = (u16)(short)(miny + (short)((short)(maxy - miny) / 2));
    }
}

u8 module_run_control(u8 what);
u8 module_can_talk(void);
void module_talk_end(void);
void label_percent_left(u8 value);
void module_stop_sequence(u8 *step);

/* The link-quiet test the machines make over and over: the two masks on
 * H'114D50 with the send and receive flags between them, six reads in a
 * fixed order. The ROM writes it out in full at every site. */
u8 module_link_quiet(void)
{
    if ((REG8(0x00114D50UL) & 0x21) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    if ((REG8(0x00114D50UL) & 0x22) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    return 0x01;
}

/* H'2417D4. Nine states that fetch the run of patterns out of the module and
 * set the machine up to sew them. H'11A63D walks the states and H'11A640 is
 * how many patterns there are.
 *
 * Nothing happens at all unless there is something to fetch: no patterns and
 * a record that is not kind three moves the caller's step on and leaves.
 *
 * States nought and one share a body -- nought only sets the counter to
 * H'FF first -- which is the same shared-tail trick the jump table itself
 * uses. Every state but the two early exits finishes by blinking the box.
 */
void module_fetch_step(u8 *step)
{
    u8 n;

    if (REG8(0x0011A640UL) == 0 && REG8(PAT_B(0x03)) != 0x03) {
        *step = (u8)(*step + 1);
        return;
    }

    n = REG8(0x0011A63DUL);
    if (n > 0x08) n = 0xFF;              /* anything past eight is the default */

    switch (n) {
    case 0x00:
        if (REG8(0x00114D51UL) & 0x40) { *step = (u8)(*step + 1); return; }
        if (!(REG8(0x0011A63CUL) & 0x02)) { *step = (u8)(*step + 1); return; }
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        REG8(0x0011F58BUL) = 0xFF;
        /* fall through -- states nought and one share a body */
        __attribute__((fallthrough));
    case 0x01:
        if (REG8(0x0011A640UL) == 0) {
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
            break;
        }
        if (!module_link_quiet()) break;
        {
            u8 slot = (u8)(REG8(0x0011F58BUL) + 1);
            u32 to, from;
            short k;

            REG8(0x0011F58BUL) = slot;
            REG8(0x0011A660UL) = slot;
            to   = 0x0011A25AUL + (u32)(long)(short)(u16)((u16)slot << 4);
            from = 0x0011F316UL + (u32)(long)(short)(u16)((u16)slot << 4);
            for (k = 0; k < 4; k++) REG32(to + (u32)k * 4) = REG32(from + (u32)k * 4);

            REG8(0x0011F2A1UL) = 0x02;
            link_send_start();

            /* the ROM compares a word whose high byte is whatever the
             * send left behind; it is nought on every path there is */
            if ((u16)((u16)REG8(0x0011A640UL) - 1) == (u16)REG8(0x0011A660UL))
                REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        break;

    case 0x02:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A61CUL) = 0x07;
            module_run_control(0x01);
            link_send_start();
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        break;

    case 0x03:
        REG8(0x0011A660UL) = REG8(0x0011A640UL);
        stitch_reset_current();
        pattern_run_extent();
        REG8(PAT_B(0x00)) = 0x1C;
        REG8(PAT_B(0x04)) = REG8(0x0011A640UL);
        REG8(PAT_B(0x03)) = 0x03;
        {
            const u32 w = (u32)(long)(short)(u16)((u16)REG8(PAT_B(0x00)) << 1);
            const u32 b = (u32)REG8(PAT_B(0x00));

            REG16(0x00104CCEUL + w) = 0x0000;
            REG16(0x00104D06UL + w) = 0x0000;
            REG16(0x00104C96UL + w) = 0x0000;
            REG8(0x000FFE9CUL + b) = 0x00;
            REG8(0x000FFEB8UL + b) = 0x00;
        }
        REG8(0x001040BCUL) = 0x00;
        REG8(0x001040BDUL) = 0x00;
        REG8(0x0011F58BUL) = 0x00;

        if (REG8(0x00114D8EUL) == 0x04) {
            int_to_decimal((short)((short)(signed char)REG8(0x0011F58BUL) + 1),
                           (char *)0x0011F2D6UL);
            REG8(0x0011F2D8UL) = 0x00;
            text_top_CB((const char *)0x0011F2D6UL);
            int_to_decimal((short)(u16)REG8(0x0011A640UL),
                           (char *)0x0011F2D6UL);
            REG8(0x0011F2D8UL) = 0x00;
            text_top_102((const char *)0x0011F2D6UL);
            label_percent_left(REG8(0x0011F530UL));
        } else if (REG8(0x00114D8EUL) == 0x07) {
            label_percent_left(REG8(0x0011F530UL));
            module_speed_show(0x0000, 0x0001);
        }
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x04:
        if (module_link_quiet()) {
            if (REG16(0x00114D4CUL) & 0x0002) REG8(0x0011F535UL) = 0x0E;
            module_run_control(0x00);
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);

            if (REG8(0x00114D8EUL) == 0x04) {
                int_to_decimal((short)(u16)REG8(0x0011A640UL),
                               (char *)0x0011F2D6UL);
                REG8(0x0011F2D8UL) = 0x00;
                text_top_CB((const char *)0x0011F2D6UL);
                text_top_102((const char *)0x0011F2D6UL);
                label_percent_left(0x64);
            } else if (REG8(0x00114D8EUL) == 0x07) {
                label_percent_left(0x64);
            }
            break;
        }

        if (REG8(0x00114D52UL) & 0x04) {
            const u8 v = (u8)(REG8(0x0011F58BUL) + 1);
            float pct;

            REG8(0x00114D52UL) &= (u8)~0x04;
            REG8(0x0011F530UL) = 0x01;
            REG8(0x0011F58BUL) = v;

            pct = (float)(long)(short)(signed char)v * 100.0f
                / (float)(u32)REG8(0x0011A640UL);
            if ((int)pct == 0) pct = pct + 1.0f;
            label_percent_left((u8)(int)pct);

            if (REG8(0x00114D8EUL) == 0x04) {
                if ((short)(u16)REG8(0x0011A640UL)
                    > (short)(signed char)REG8(0x0011F58BUL))
                    int_to_decimal((short)((short)(signed char)v + 1),
                                   (char *)0x0011F2D6UL);
                else
                    int_to_decimal((short)(signed char)v,
                                   (char *)0x0011F2D6UL);
                REG8(0x0011F2D8UL) = 0x00;
                text_top_CB((const char *)0x0011F2D6UL);
            }
        }
        {
            float done = ((float)(u32)REG8(0x0011F530UL)
                          + (float)(long)(short)(signed char)REG8(0x0011F58BUL)
                            * 100.0f)
                       / (float)(u32)REG8(0x0011A640UL);

            if ((int)done == 0) done = done + 1.0f;
            module_speed_show((u16)(int)done, 0x0001);
        }
        break;

    case 0x05:
        module_stop_sequence((u8 *)0x0011A63DUL);
        module_speed_show(0x0064, 0x0001);
        break;

    case 0x06:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A61CUL) = 0x08;
            link_send_start();
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        module_speed_show(0x0064, 0x0001);
        break;

    case 0x07:
        if (module_link_quiet()) {
            REG8(0x00114D8DUL) = REG8(0x000FFE9CUL + (u32)REG8(PAT_B(0x00)));
            REG8(0x00114D96UL) = pattern_attr_bit3() != 0 ? 0x01 : 0x00;
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        module_speed_show(0x0064, 0x0001);
        break;

    case 0x08:
        if (module_link_quiet()) {
            REG8(0x00114D92UL) = 0xFF;
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        module_speed_show(0x0064, 0x0001);
        break;

    default:
        if (REG8(0x0011F52CUL) == 0) REG8(0x00114D73UL) = 0x01;
        else                         REG8(0x00114D66UL) = 0x00;
        REG8(0x0011A63CUL) &= (u8)~0x02;
        *step = (u8)(*step + 1);
        REG8(0x0011A63DUL) = 0x00;
        module_speed_show(0x00C8, 0x0001);
        break;
    }

    module_panel_blink(0x01);
}

/* H'244F14. Whether the pattern, where it is placed and turned, still fits
 * inside the hoop.
 *
 * The same four corners as H'24217A, but this time with the hoop's own
 * middle and the pattern's placement added in, and each corner tested
 * against the hoop's width and height in H'11A626 and H'11A628. Any corner
 * past an edge, or behind the origin, and the answer is no.
 *
 * The corner comparisons go through H'20074A, which compares two floats by
 * their bit patterns -- and gets it right, including both-negative, so
 * ordinary C comparison does the same thing. */
u8 module_hoop_fits(void)
{
    const u32 a16 = 0x0011A25AUL
                  + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4);
    const u32 b18 = 0x0011A41AUL
                  + (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x0011A660UL));
    const u32 pat = (u32)(long)(short)(u16)((u16)REG8(b18) << 1);
    const float sx = (float)(u32)REG8(a16) * 2.0f / 100.0f;
    const float sy = (float)(u32)REG8(a16 + 1) * 2.0f / 100.0f;
    const float w  = (float)(u32)REG16(0x00104CCEUL + pat) * sx;
    const float h  = (float)(u32)REG16(0x00104D06UL + pat) * sy;
    const float angle = ((float)(u32)REG8(a16 + 6) * 5.0f + -180.0f)
                      * 0.017453277f;
    const u8 flip = REG8(a16 + 5);
    const float hoopx = (float)(u32)REG16(0x0011A626UL);
    const float hoopy = (float)(u32)REG16(0x0011A628UL);
    const float ox = hoopx / 2.0f
                   + (float)(long)(short)REG16(0x0011A266UL
                       + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4))
                     * 5.0f;
    const float oy = hoopy / 2.0f
                   + (float)(long)(short)REG16(0x0011A268UL
                       + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4))
                     * 5.0f;
    float theta = 0.0f, r;
    float cx[4], cy[4];
    short i;

    if (f2u(w) != 0) theta = float_atan(h / w);

    r = float_sqrt(h * h + w * w) / 2.0f;

    {
        const float nr = ((u16)(f2u(r) >> 16) != 0)
                         ? u2f(f2u(r) ^ 0x80000000UL) : r;
        const float p  = theta - angle;
        const float q  = (((u16)(f2u(theta) >> 16) != 0)
                          ? u2f(f2u(theta) ^ 0x80000000UL) : theta) - angle;

        if (flip == 0) {
            cx[0] = float_cos(p) * r  + ox;  cy[0] = float_sin(p) * r  + oy;
            cx[1] = float_cos(q) * r  + ox;  cy[1] = float_sin(q) * r  + oy;
            cx[2] = nr * float_cos(p) + ox;  cy[2] = nr * float_sin(p) + oy;
            cx[3] = nr * float_cos(q) + ox;  cy[3] = nr * float_sin(q) + oy;
        } else {
            cx[0] = nr * float_cos(p) + ox;  cy[0] = float_sin(p) * r  + oy;
            cx[1] = nr * float_cos(q) + ox;  cy[1] = float_sin(q) * r  + oy;
            cx[2] = float_cos(p) * r  + ox;  cy[2] = nr * float_sin(p) + oy;
            cx[3] = float_cos(q) * r  + ox;  cy[3] = nr * float_sin(q) + oy;
        }
    }

    for (i = 0; i < 4; i++) {
        if (cx[i] > hoopx) return 0x00;
        if (cy[i] > hoopy) return 0x00;
        if ((long)f2u(cx[i]) < 0) return 0x00;
        if ((long)f2u(cy[i]) < 0) return 0x00;
    }
    return 0x01;
}

/* H'2431EE. Twelve states that start the module sewing and stop it again,
 * walked by H'11F595 through a reversed key table at H'24329C: the keys
 * H'01 to H'05 and H'0A to H'10 are read forwards while the index counts
 * down, so the handlers sit back to front behind them.
 *
 * Which state it starts in is decided before the dispatch: H'0A when the
 * module is already talking or H'114D4E is clear, H'01 otherwise, and
 * H'114DBB at five puts it back to one. Every state that is waiting for the
 * link blinks the panel box while it waits.
 */
void module_run_step(u8 *step)
{
    u8 n;

    if (REG8(0x0011F595UL) == 0) {
        if (REG8(0x00114DBAUL) != 0) REG8(0x0011F595UL) = 0x0A;
        else                         REG8(0x0011F595UL) = 0x01;

        if (REG8(0x00114D4EUL) == 0) REG8(0x0011F595UL) = 0x0A;

        if (REG8(0x00114D51UL) & 0x10) {
            if (REG8(0x00114DBBUL) == 0) REG8(0x00114DBBUL) = 0x01;
        }
        if (!(REG8(0x00114D51UL) & 0x10)) REG8(0x00114DBBUL) = 0x00;

        if (REG8(0x00114DBBUL) == 0x05) REG8(0x0011F595UL) = 0x01;
    }

    n = REG8(0x0011F595UL);

    switch (n) {
    case 0x01:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A617UL) = 0x01;
            REG8(0x00114D7DUL) = 0x01;
            module_run_control(0x01);
            link_send_start();
            REG8(0x0011F594UL) = 0x00;
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x02:
        if (!module_link_quiet()) { module_panel_blink(0x01); return; }

        if (REG8(0x00114D52UL) & 0x08) {
            REG8(0x00114D52UL) &= (u8)~0x08;
            if (REG8(0x0011F531UL) == 0) REG8(0x0011F534UL) = 0x01;
            REG8(0x0011F531UL) = 0x00;
        }
        module_run_control(0x00);
        if (REG8(0x0011F594UL) == 0) {
            module_fixed_box(0x01);
            REG8(0x0011F594UL) = 0x01;
        }
        if (REG8(0x0011F530UL) > 0x32) {
            while (REG8(0x0011F530UL) <= 0x64) {
                label_percent_left(REG8(0x0011F530UL));
                REG8(0x0011F530UL) = (u8)(REG8(0x0011F530UL) + 1);
            }
        }
        REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        return;

    case 0x03:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x04;
            REG8(0x0011F2A2UL) = 0x02;
            link_send_start();
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x04:
        if (!module_link_quiet()) return;
        if (REG8(0x00114DB9UL) != 0) return;

        if (module_hoop_fits() != 0) {
            if (REG8(0x00114DBBUL) == 0x01) {
                if (module_can_talk() != 0) {
                    REG8(0x0011F2A1UL) = 0x03;
                    REG8(0x0011A613UL) = 0x02;
                    link_send_start();
                    REG8(0x00114DBAUL) = 0x01;
                    REG8(0x00114DBBUL) = 0x05;
                    REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
                }
                return;
            }

            if (REG8(0x00114DBAUL) != 0) {
                if (module_can_talk() != 0) {
                    REG8(0x0011F2A1UL) = 0x03;
                    REG8(0x0011A614UL) = 0x03;
                    REG8(0x00114DBAUL) = 0x00;
                    link_send_start();
                    REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
                }
                return;
            }

            if (module_running() != 0) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A614UL) = 0x03;
                REG8(0x00114DBAUL) = 0x00;
                link_send_start();
                REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
            } else {
                link_claim(0x03);
            }
            return;
        }

        if (module_can_talk() != 0) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A613UL) = 0x02;
            link_send_start();
            REG8(0x00114DBAUL) = 0x01;
            REG8(0x00114D55UL) |= 0x01;
            if (REG8(0x00114DBBUL) == 0x01) REG8(0x00114DBBUL) = 0x05;
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        }
        return;

    case 0x0A:
        if (REG8(0x00114DB9UL) == 0) {
            REG8(0x00114D7DUL) = 0x01;
            if (module_can_talk() != 0) {
                if (REG8(0x00114DA0UL) == 0) module_talk_end();
                REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
            }
        }
        return;

    case 0x0B:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A612UL) = 0x01;
            link_send_start();
            REG8(0x0011F594UL) = 0x00;
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x0C:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A617UL) = 0x01;
            link_send_start();
            module_run_control(0x01);
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x0D:
        if (!module_link_quiet()) { module_panel_blink(0x01); return; }

        module_run_control(0x00);
        REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        if (REG8(0x0011F594UL) == 0) {
            module_fixed_box(0x01);
            REG8(0x0011F594UL) = 0x01;
        }
        if (REG8(0x0011F530UL) > 0x32) {
            while (REG8(0x0011F530UL) <= 0x64) {
                label_percent_left(REG8(0x0011F530UL));
                REG8(0x0011F530UL) = (u8)(REG8(0x0011F530UL) + 1);
            }
        }
        return;

    case 0x0E:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x04;
            REG8(0x0011F2A2UL) = 0x02;
            link_send_start();
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x0F:
        if (REG8(0x00114DB9UL) == 0) {
            if (module_can_talk() != 0)
                REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        }
        return;

    case 0x10:
        if (!module_link_quiet()) { module_panel_blink(0x01); return; }

        if (module_hoop_fits() != 0) {
            if (REG8(0x00114DBBUL) == 0x01) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A613UL) = 0x02;
                link_send_start();
                REG8(0x00114DBAUL) = 0x01;
                REG8(0x00114DBBUL) = 0x05;
            } else {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A614UL) = 0x03;
                link_send_start();
                REG8(0x00114DBAUL) = 0x00;
            }
        } else {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A613UL) = 0x02;
            link_send_start();
            REG8(0x00114DBAUL) = 0x01;
            REG8(0x00114D55UL) |= 0x01;
        }
        REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        return;

    case 0x05:
    default:
        if (module_link_quiet()) {
            *step = (u8)(*step + 1);
            REG8(0x00114D7DUL) = 0x00;
            REG8(0x0011F595UL) = 0x00;
            if (REG8(0x0011F52CUL) != 0) REG8(0x00114D66UL) = 0x00;
        } else {
            module_panel_blink(0x01);
        }
        return;
    }
}

u8 touch_hit(u16 first, u16 last, u16 *out_value, u16 *out_index);
u8 module_home_request(void);
void module_restart(void);
void screen_store1_clear(void);
void embroidery_panel_save(void);
u8 module_identify(void);
void stitch_reset_current(void);
void module_reset_wait(void);
void module_unpark(void);
void link_delay(u16 units);
void module_screen_step(void);
void pattern_mark_ready(void);
void module_link_lost(void);
void module_wait_pass(void);
void module_state_machine(void);
void module_fetch_step(u8 *step);
void module_run_step(u8 *step);
void module_hoop_check(u8 *step);
void module_ask_time(u8 *step);
void module_buffers_clear(void);
void module_cursor_erase(void);
void module_fixed_box(u8 mode);
void box5_draw(u8 lit);
void label_colours(void);
void label_percent(void);
void label_colours_picture(void);
void label_minutes(short value);
void module_size_labels(short across, short down);
void module_minutes_left(u8 mode);
u8 module_flash_step(u8 what);
u8 module_edge_service(void);
u8 module_lid_check(void);
void module_arrow_fwd_2(u8 lit);
void module_arrow_back_1(u8 lit);
void module_arrow_fwd_3(u8 lit);
void module_arrow_back_2(u8 lit);
void module_colour_bitmap(u8 index);
void module_panel_blink(u8 on);
void pedal_service(void);
void main_motor_service(void);

static void module_give_up(void);
static void module_wait_reset(void);
static void module_lost_home(void);
static void module_state_0D(u8 *wait);
static void module_state_0E(u8 *wait);

/* State H'0D of H'235B0E, written out because it is a third of the routine.
 * The module is running: the lid, the stop button and the hardware state are
 * all watched, and when the hardware settles at five the speed is worked out
 * in floating point and written to H'FFFECB.
 *
 * The speed is sixty over four thousandths of the reading, held down to
 * H'114DC0, less one. Two ways to the reading, by whether H'57FF80 says this
 * is a H'B4 machine: the other kind adds three to the tenths and takes five
 * off at the end. */
static void module_state_0D(u8 *wait)
{
    *wait = 0;

    if (!(REG8(0x00FFFEC1UL) & 0x08)) {
        REG8(0x00114D93UL) = 0x01;
        module_home_request();
    }

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) {
            module_flash_step(0x03);
            if (REG8(0x00FFFEC7UL) & 0x01) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D62UL) = 0x0A;
                link_claim(0x1A);
            }
        }
    }

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) { REG8(0x00FFFECBUL) = 0x1E; return; }
    }

    REG8(0x00114D9AUL) = 0x01;
    REG8(0x00114D98UL) = 0x01;

    if (module_edge_service() == 0) {
        REG8(0x00114D93UL) = 0x01;
        module_home_request();
        module_flash_step(0x01);
    }

    if (REG8(0x00FFFEC0UL) == 0x01) { module_lost_home(); return; }

    if ((REG8(0x00FFFEC4UL) & 0x10) && (REG8(0x00FFFEC7UL) & 0x01)) {
        if (REG8(0x00FFFEC3UL) == 0) REG8(0x00FFFEC7UL) |= 0x40;
        if (REG8(0x00FFFEC1UL) & 0x02) {
            REG8(0x00FFFEC7UL) |= 0x40;
            REG8(0x00FFFEC3UL) = 0x00;
        }
    }

    if (REG8(0x00FFFEC0UL) != 0x05) return;

    if (REG8(0x0011F54BUL) == 0x01) {
        REG8(0x0011F54BUL) = 0x00;
        REG8(0x00114D62UL) = 0x0A;
        REG8(0x00FFFEC7UL) |= 0x40;
        REG8(0x00FFFEC3UL) = 0x00;
        REG8(0x00FFFEC4UL) |= 0x20;
        REG8(0x00114D50UL) &= (u8)~0x02;
        REG8(0x00114D50UL) &= (u8)~0x01;
        return;
    }

    while (REG8(0x00FFFEC0UL) != 0x01) {
        if (*wait == 0xFF) { *wait = 0x00; break; }
        link_delay(0x0001);
        *wait = (u8)(*wait + 1);
    }

    if (REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
        module_lost_home();
        return;
    }

    REG16(0x0011A662UL) = 0x1FFF;
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A616UL) = 0x01;
    link_send_start();
    link_delay(0x0008);

    if (REG16(0x0011A662UL) == 0x1FFF) { module_lost_home(); return; }

    if (REG8(0x00114D5CUL) == 0x02) {
        REG8(0x00114D93UL) = 0x00;
        module_home_request();
        REG8(0x00FFFEC4UL) |= 0x20;
        link_claim(0x10);
        return;
    }

    REG8(0x00FFFEC1UL) &= (u8)~0x20;

    if (REG8(0x00114D5CUL) == 0x04) {
        REG8(0x00FFFECBUL) = 0x1E;
        REG8(0x0011F54CUL) = REG8(0x00FFFED1UL);
        REG8(0x00FFFED1UL) = 0x28;
        REG8(0x00114D62UL) = 0x0F;
        return;
    }

    if (REG8(0x00114D5CUL) == 0x05) REG8(0x00FFFEC1UL) |= 0x20;

    if (REG8(0x00114D5CUL) == 0x01) {
        if (REG8(0x00114D96UL) == 0) {
            REG8(0x00114D7CUL) = 0x01;
            link_delay(0x0032);
            REG8(0x00FFFEC7UL) |= 0x40;
            REG8(0x00FFFEC3UL) = 0x00;
            REG8(0x0011F54EUL) = 0x01;
            module_flash_step(0x01);
        }
        if (REG8(0x00114D7CUL) == 0) {
            REG8(0x00114D4FUL) &= (u8)~0x01;
            REG8(0x00114D89UL) = (u8)(REG8(0x00114D89UL) + 1);
            REG8(0x00FFFEC1UL) |= 0x20;
            label_colours_picture();
        }
        REG8(0x00114D62UL) = 0x0E;
        return;
    }

    {
        float v;

        if (REG8(0x0057FF80UL) < 0xB4)
            v = (float)(long)(short)(u16)((u16)REG8(0x00114DBEUL) + 3) / 10.0f
                  * (float)(u32)REG16(0x0011A662UL)
                + (float)(u32)REG8(0x00114DBFUL) + -5.0f;
        else
            v = (float)(u32)REG8(0x00114DBEUL) / 10.0f
                  * (float)(u32)REG16(0x0011A662UL)
                + (float)(u32)REG8(0x00114DBFUL);

        v = 60.0f / (v / 1000.0f * 4.0f);
        if (!((float)(u32)REG8(0x00114DC0UL) >= v))
            v = (float)(u32)REG8(0x00114DC0UL);

        REG8(0x00FFFECBUL) = (u8)((u8)(int)v - 1);
    }

    if (REG16(0x0011A662UL) == 0x01FF) REG8(0x00FFFECBUL) = 0x1E;
    if (REG8(0x0011F54BUL) != 0) REG8(0x00FFFECBUL) = 0x1E;
    REG8(0x00114D62UL) = 0x0E;
    REG8(0x0011F54BUL) = module_flash_step(0x00);
}

/* State H'0E. Waits for the hardware to reach the state that means one
 * stitch is done -- seven on this machine, three on a H'B4 one -- turning
 * the delay over up to H'FF times, then sends message H'03/H'02 and goes
 * back to H'0D. H'11F54E says this was the last one. */
static void module_state_0E(u8 *wait)
{
    const u8 want = (REG8(0x0057FF80UL) < 0xB4) ? 0x07 : 0x03;

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) return;
    }

    for (;;) {
        if (REG8(0x00FFFEC0UL) == want) break;
        if (*wait == 0xFF) break;
        link_delay(0x0001);
        *wait = (u8)(*wait + 1);
        if (REG8(0x00FFFEC0UL) == want) break;
        if (REG8(0x00FFFEC0UL) == 0x01) break;
        if (REG8(0x00FFFEC0UL) == 0x00) break;
    }

    if (REG8(0x00FFFEC0UL) != want) return;

    if (REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
        module_lost_home();
        return;
    }

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A616UL) = 0x02;
    link_send_start();
    REG8(0x00114D62UL) = 0x0D;

    if (REG8(0x0011F54EUL) != 0) {
        module_minutes_left(0x02);
        REG8(0x0011F54EUL) = 0x00;
        REG8(0x00114D62UL) = 0x0A;
    } else {
        module_minutes_left(0x01);
    }
}

/* H'244DE0's own reset: everything cleared and message H'03/H'04 sent. */
static void module_wait_reset(void)
{
    REG8(0x00114D83UL) = 0x00;
    REG8(0x00114D66UL) = 0x00;
    REG8(0x00114D62UL) = 0x00;
    REG8(0x00114D95UL) = 0x00;
    REG8(0x00114D50UL) &= (u8)~0x01;
    REG8(0x00114D50UL) &= (u8)~0x02;
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61AUL) = 0x04;
    link_send_start();
}

/* The block H'235B0E writes out to give up on the module:
 * everything cleared, the hardware released, message H'03/H'04 sent, and the
 * screen asked for again if a homing was pending. */
static void module_give_up(void)
{
    REG8(0x00114D65UL) = 0x00;
    REG8(0x00114D66UL) = 0x00;
    REG8(0x00114D62UL) = 0x00;
    REG8(0x00114D95UL) = 0x00;
    REG8(0x00FFFEC4UL) |= 0x20;
    REG8(0x00114D50UL) &= (u8)~0x01;
    REG8(0x00114D50UL) &= (u8)~0x02;
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61AUL) = 0x04;
    link_send_start();

    if (REG8(0x00114D93UL) != 0) {
        REG8(0x00114D66UL) = 0x01;
        REG8(0x00114D62UL) = 0x08;
    }
    module_screen_step();
}

/* The failure every state of H'235B0E shares: bit H'0200 of H'114D4C up,
 * and the module asked to go home. */
static void module_lost_home(void)
{
    REG16(0x00114D4CUL) |= 0x0200;
    REG8(0x00114D94UL) = 0x01;
    REG8(0x00114D93UL) = 0x01;
    module_home_request();
}

/* H'235B0E. The module's own state machine: eighteen states in H'114D62,
 * with a four-step settling sub-machine in H'114D65 in front of them that
 * only runs once the main states are past seven.
 *
 * H'244DE0 turns this over in a loop and this calls H'2315A4, which calls
 * H'23182A, which calls H'244DE0 -- the four of them are one knot and are
 * written together.
 *
 * Two registers the ROM never initialises are reproduced as locals starting
 * at nought: R1, the H'C8 spin count in state H'0A, and R3L, the delay
 * counter states H'0D and H'0E share. Neither survives a call, so neither
 * ever counts in the original either.
 */
void module_state_machine(void)
{
    u16 spin = 0;
    u8 wait = 0;

    if (REG8(0x00114D65UL) != 0 && REG8(0x00114D62UL) > 0x07) {
        const u8 k = REG8(0x00114D65UL);

        if (k == 0x01) {
            if (REG8(0x00114D62UL) < 0x0F) {
                if (REG8(0x00FFFEC0UL) == 0x06) {
                    REG8(0x00FFFECBUL) = 0x1E;
                    REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                    return;
                }
                REG8(0x00FFFECBUL) = 0x1E;
                {
                    const u8 s = REG8(0x00FFFEC6UL);

                    if (s == 0x00 || s == 0x05)
                        REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                }
            }
            if (REG8(0x00114D94UL) != 0) return;
            /* otherwise on into the states */
        } else if (k == 0x02) {
            if (REG8(0x00FFFEC0UL) == 0x07) {
                REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                return;
            }
            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05)
                    REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
            }
            return;
        } else if (k == 0x03) {
            if (REG8(0x00FFFEC0UL) == 0x05) {
                REG8(0x00FFFEC7UL) |= 0x40;
                REG8(0x00FFFEC3UL) = 0x00;
                REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                return;
            }
            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05)
                    REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
            }
            return;
        } else {
            link_delay(0x0014);
            REG8(0x00114D94UL) = 0x00;
            REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);

            if (REG8(0x00114D65UL) == 0xC8) module_give_up();
            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05) module_give_up();
            }
            return;
        }
    }

    switch (REG8(0x00114D62UL)) {
    case 0x00:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x04;
            REG8(0x0011F2A2UL) = 0x02;
            link_send_start();
            REG8(0x0011F54DUL) = 0x7D;
            REG8(0x00114D99UL) = 0x00;
            module_flash_step(0x01);
            REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x01:
        if (REG8(0x0011F54DUL) != 0) {
            REG8(0x0011F54DUL) = (u8)(REG8(0x0011F54DUL) - 1);
            module_panel_blink(0x01);
        } else {
            REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        }
        return;

    case 0x02:
        module_fetch_step((u8 *)0x00114D62UL);
        return;

    case 0x03:
        module_hoop_check((u8 *)0x00114D62UL);
        return;

    case 0x04:
        if (!module_link_quiet()) return;
        module_fixed_box(0x01);
        REG8(0x00114D91UL) = 0x01;
        label_colours();
        box5_draw(REG8(0x00114D96UL));
        label_percent();
        module_arrow_back_2(0x00);
        module_arrow_fwd_3(0x01);
        {
            const u32 pat = (u32)(long)(short)(u16)
                            ((u16)REG8(PAT_B(0x00)) << 1);
            const u16 w = (u16)(REG16(0x00104CCEUL + pat) / 10);
            const u16 h = (u16)(REG16(0x00104D06UL + pat) / 10);

            REG16(0x0011F4E2UL) = h;
            REG16(0x0011F4E0UL) = w;
            module_size_labels((short)w, (short)h);
        }
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x05:
        {
            /* the sum is kept as a full word here and as a byte below */
            const u16 n = (u16)((u16)((u16)REG8(PAT_B(0x05)) * 0x1B)
                                + (u16)REG8(PAT_B(0x00)));

            if (n != (u16)REG8(0x00114D92UL)) {
                REG8(0x00114D7BUL) = 0x01;
                REG8(0x0011F4E6UL) = 0x00;
                REG8(0x00114D7CUL) = 0x00;
            } else {
                REG8(0x00114D7BUL) = 0x14;
            }
        }
        REG8(0x00114D92UL) = (u8)(REG8(PAT_B(0x00))
                                  + (u8)(u16)((u16)REG8(PAT_B(0x05)) * 0x1B));
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x06:
        if (REG8(0x00114D7BUL) != 0) return;
        module_run_step((u8 *)0x00114D62UL);
        return;

    case 0x07:
        if (module_link_quiet() && REG8(0x00114DB9UL) == 0)
            REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        else
            module_panel_blink(0x01);
        return;

    case 0x08:
        if ((REG8(0x00114D50UL) & 0x21) != 0 ||
            REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
            module_fixed_box(0x01);
            return;
        }
        REG8(0x00FFFECBUL) = 0x1E;
        {
            const u32 pat = (u32)(long)(short)(u16)
                            ((u16)REG8(PAT_B(0x00)) << 1);
            const u16 w = (u16)(REG16(0x00104CCEUL + pat) / 10);
            const u16 h = (u16)(REG16(0x00104D06UL + pat) / 10);

            REG16(0x0011F4E2UL) = h;
            REG16(0x0011F4E0UL) = w;
            module_size_labels((short)w, (short)h);
            REG16(0x0011F4E4UL) = REG16(0x00104C96UL + pat);
            if (REG16(0x0011F4E4UL) != 0)
                label_minutes((short)REG16(0x0011F4E4UL));
        }
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x01;
        link_send_start();
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x09:
        if ((REG8(0x00114D50UL) & 0x21) != 0 ||
            REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
            module_fixed_box(0x01);
            return;
        }
        link_claim(REG8(0x0011F535UL));
        module_panel_blink(0x00);
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x0A:
        if (REG8(0x00114D7CUL) != 0) {
            const u8 k = REG8(0x00114D7CUL);

            if (k == 0x01) {
                spin = 0;
                REG8(0x00114D7CUL) = (u8)(k + 1);
            } else if (k == 0x02) {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s != 0x00 && s != 0x05) {
                    spin = (u16)(spin + 1);
                    if (spin == 0x00C8) REG8(0x00FFFEC4UL) |= 0x20;
                    return;
                }
                REG8(0x00114D7CUL) = (u8)(REG8(0x00114D7CUL) + 1);
            } else if (k == 0x03) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D4FUL) |= 0x01;
                REG8(0x00114D7CUL) = (u8)(REG8(0x00114D7CUL) + 1);
            } else if (k == 0x04) {
                REG8(0x00114D7CUL) = 0x00;
                REG8(0x00114D50UL) &= (u8)~0x01;
                REG8(0x00114D50UL) &= (u8)~0x02;
            }
            return;
        }

        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            REG8(0x00114D7BUL) == 0 && REG8(0x00114D4EUL) != 0 &&
            REG8(0x00114DBAUL) == 0 && REG8(0x00114DB9UL) == 0) {
            if ((REG8(0x00FFFEC1UL) & 0x08) && REG8(0x0011F30EUL) == 0) {
                REG8(0x00114D62UL) = 0x0B;
                REG8(0x0011F54EUL) = 0x00;
                return;
            }
            module_lid_check();
            return;
        }
        REG8(0x00114D98UL) = 0x00;
        return;

    case 0x0B:
        module_ask_time((u8 *)0x00114D62UL);
        if (REG8(0x00114D62UL) == 0x0C) {
            REG8(0x00114D98UL) = 0x01;
            REG8(0x00FFFEC4UL) &= (u8)~0x20;
        }
        return;

    case 0x0C:
        {
            const u8 s = REG8(0x00FFFEC6UL);

            if (s != 0x00 && s != 0x05) {
                REG8(0x00114D62UL) = 0x0D;
                REG8(0x00114D95UL) = 0x01;
                REG8(0x00114D99UL) = 0x00;
            }
        }
        if (REG8(0x0011F30EUL) != 0) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D62UL) = 0x0A;
        }
        if ((REG8(0x00FFFEC4UL) & 0x10) && (REG8(0x00FFFEC7UL) & 0x01)) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D62UL) = 0x0A;
            link_claim(0x1A);
        }
        if ((REG8(0x00FFFEC4UL) & 0x10) && !(REG8(0x00FFFEC1UL) & 0x08)) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D62UL) = 0x0A;
        }
        return;

    case 0x0D:
        module_state_0D(&wait);
        return;

    case 0x0E:
        module_state_0E(&wait);
        return;

    case 0x0F:
        if (REG8(0x00FFFEC0UL) == 0x03) {
            if (REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A616UL) = 0x02;
                link_send_start();
                REG8(0x00114D62UL) = 0x10;
            } else {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00FFFED1UL) = REG8(0x0011F54CUL);
                REG16(0x00114D4CUL) |= 0x0200;
                REG8(0x00114D62UL) = 0x00;
                REG8(0x00114D66UL) = 0x00;
            }
        }
        return;

    case 0x10:
        if (REG8(0x00FFFEC0UL) == 0x04) {
            REG8(0x00FFFECBUL) = 0x00;
            pedal_service();
            main_motor_service();
            REG8(0x00114D62UL) = 0x11;
        }
        return;

    case 0x11:
        link_delay(REG16(0x0011A662UL));
        REG8(0x00FFFED1UL) = REG8(0x0011F54CUL);
        REG8(0x00FFFECBUL) = 0x1E;
        pedal_service();
        main_motor_service();
        REG8(0x00114D62UL) = 0x0D;
        return;

    default:
        return;
    }
}

/* H'2315A4. Which screen the module wants next, by H'114D8A. State four is
 * the only one that does not clear the request afterwards. */
void module_screen_step(void)
{
    switch (REG8(0x00114D8AUL)) {
    case 0x01:
        screen_switch(0x15, 0x01, 0x00);
        REG8(0x00114D72UL) = 0x01;
        REG8(0x00114D8AUL) = 0x00;
        break;

    case 0x02:
        if (REG8(0x00114DA1UL) == 0x01) screen_switch(0x14, 0x01, 0x00);
        else                            screen_switch(0x13, 0x01, 0x00);
        REG8(0x00114D7EUL) = 0x01;
        if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        REG8(0x00114D8AUL) = 0x00;
        break;

    case 0x03:
        screen_switch(0x16, 0x01, 0x00);
        REG8(0x00114D72UL) = 0x02;
        REG8(0x00114D8AUL) = 0x00;
        break;

    case 0x04:
        REG8(0x00114D72UL) = 0x3E;
        screen_switch(0x37, 0x01, 0x00);
        pattern_mark_ready();
        break;

    case 0x05:
        REG8(0x00114D72UL) = 0x2D;
        screen_switch(0x24, 0x01, 0x00);
        REG8(0x00114D8AUL) = 0x00;
        module_arrow_fwd_2(0x01);
        module_arrow_back_1(0x01);
        module_colour_bitmap(REG8(0x00114D89UL));
        break;

    case 0x06:
        module_link_lost();
        break;

    default:
        REG8(0x00114D8AUL) = 0x00;
        break;
    }
}

/* H'23182A. The module has gone: everything cleared out, the screen put back
 * to the one that matches what bit 0 of H'114D51 says was attached, and the
 * whole thing marked as unplugged. If the hardware is still moving it waits
 * a pass instead and asks again next time. */
void module_link_lost(void)
{
    const u8 state = REG8(0x00FFFEC6UL);

    if (state == 0x00 || state == 0x05) {
        module_buffers_clear();

        if (REG8(0x00114D51UL) & 0x01) {
            REG8(0x00114D7EUL) = 0x01;
            REG8(0x00114D8BUL) = 0x00;
            REG8(0x00114D9BUL) = 0x01;
            link_claim(0x05);
            screen_switch(0x14, 0x01, 0x00);
            REG8(0x00114DA1UL) = 0x01;
            REG8(PAT_B(0x03)) = 0x01;
            REG8(0x00114D8EUL) = 0x03;
        } else {
            REG8(0x00114D7EUL) = 0x01;
            link_claim(0x05);
            REG8(0x00114D8BUL) = 0x00;
            REG8(0x00114D9BUL) = 0x01;
            screen_switch(0x13, 0x01, 0x00);
            REG8(0x00114DA1UL) = 0x00;
            REG8(PAT_B(0x03)) = 0x00;
            REG8(0x00114D8EUL) = 0x02;
        }
        REG8(0x00114D8AUL) = 0x00;
        return;
    }

    if (REG8(0x00114D8AUL) == 0) module_wait_pass();
    REG8(0x00114D8AUL) = 0x06;
}

/* H'244DE0. A pass held open while the module finishes moving: the state
 * machine, the pedal and the main motor turned over until H'114D83 comes
 * back to nought. H'114D83 counting to H'C8 gives up and puts everything
 * back, and so does the hardware coming to rest. */
void module_wait_pass(void)
{
    module_cursor_erase();

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) return;
    }

    do {
        const u8 n = REG8(0x00114D83UL);

        if (n == 0x00) {
            REG8(0x00114D83UL) = (u8)(n + 1);
        } else if (n == 0x01) {
            if (REG8(0x00114D62UL) != 0x0E) {
                REG8(0x00FFFEC4UL) |= 0x20;
                link_delay(0x000A);
                REG8(0x00114D83UL) = (u8)(REG8(0x00114D83UL) + 1);
            }
        } else {
            link_delay(0x0014);
            REG8(0x00114D83UL) = (u8)(REG8(0x00114D83UL) + 1);

            /* this routine's own giving-up block, which is not the one
             * H'235B0E uses: it clears H'114D83 rather than H'114D65, does
             * not touch H'FFFEC4, and does not ask for a screen */
            if (REG8(0x00114D83UL) == 0xC8) module_wait_reset();

            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05) module_wait_reset();
            }
        }

        if (REG8(0x00114D66UL) != 0) module_state_machine();
        pedal_service();
        main_motor_service();
    } while (REG8(0x00114D83UL) != 0);

    REG8(0x00114D98UL) = 0x00;
    REG8(0x00114D99UL) = 0x01;
}

/* H'231B32. The module reset held open: H'11F9C walks two steps, the first
 * turning a whole pass over and the second clearing everything out and going
 * back to screen H'12. Nothing happens unless H'114D9F asks for it. */
void module_reset_wait(void)
{
    if (REG8(0x00114D9FUL) == 0) return;

    do {
        const u8 n = REG8(0x00114D9CUL);

        if (n == 0x00) {
            REG8(0x00114D4FUL) &= (u8)~0x08;
            REG8(0x00114D80UL) = 0x00;
            module_wait_pass();
            REG8(0x00114D9CUL) = (u8)(REG8(0x00114D9CUL) + 1);
        } else if (n == 0x01) {
            if (REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
                module_buffers_clear();
                screen_switch(0x12, 0x01, 0x00);
                REG8(0x00114D9CUL) = 0x00;
            }
        }
    } while (REG8(0x00114D9CUL) != 0);
}

/* H'237E3C. A key press while the embroidery module is attached.
 *
 * Twelve keys in a reversed jump table at H'237E7A: the codes H'21F68C
 * hands over. Most of them share the same two guards -- H'114D78 at H'EE
 * means the module is mid-something, and bit 0 of H'FFFEC4 means it is
 * really there -- and then wait a pass for the hardware to settle.
 *
 * Key H'6D is the one that does the work: it is the button that leaves the
 * embroidery screens, and where it goes depends on H'114D8E. Eight or nine
 * puts the panel away and goes to screen H'37; four or five stops the run
 * first; one to three asks the module to name itself and only goes on if it
 * answers.
 */
void module_key(u8 key)
{
    switch (key) {
    case 0x74:
        if (REG8(0x00114D78UL) <= 0x01) return;

        module_wait_pass();
        if (REG8(0x00114D78UL) == 0xEE) {
            module_restart();
            screen_store1_clear();
        }
        if ((REG8(0x00114D4FUL) & 0x08) || REG8(0x00114D9CUL) != 0)
            module_reset_wait();
        else
            module_buffers_clear();

        if (module_link_quiet() && (REG8(0x00114D51UL) & 0x40)) {
            REG8(0x0011F2A1UL) = 0x07;
            REG8(0x00114D51UL) &= (u8)~0x40;
            link_send_start();
            while (!module_link_quiet()) rom_host_service();
        }
        REG8(0x00114D8EUL) = 0x01;
        return;

    case 0x70: case 0x71: case 0x72: case 0x78: case 0x7D:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        REG8(0x00114D78UL) = 0xFF;
        module_unpark();
        REG8(0x00114D8EUL) = 0x00;
        module_wait_pass();
        return;

    case 0x73: case 0x79:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        {
            const u8 s = REG8(0x00FFFEC6UL);

            if (s == 0x00 || s == 0x05) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D66UL) = 0x00;
            }
        }
        module_wait_pass();
        REG8(0x00114D88UL) = 0x01;
        REG8(0x0011F305UL) = 0x01;
        return;

    case 0x75:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        {
            const u8 s = REG8(0x00FFFEC6UL);

            if (s == 0x00 || s == 0x05) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D66UL) = 0x00;
            }
        }
        module_wait_pass();
        return;

    case 0x77:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        REG8(0x00114D84UL) = 0x01;
        return;

    case 0x6D:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        if (module_home_request() != 0) return;
        if (REG8(0x00114D72UL) != 0) return;
        if (!module_link_quiet()) return;

        REG8(0x001040BAUL) = REG8(0x00114D8EUL);

        if (REG8(0x00114D8EUL) >= 0x08 && REG8(0x00114D8EUL) <= 0x09) {
            embroidery_panel_save();
            REG8(0x00114D8EUL) = 0x07;
            REG8(0x00114D72UL) = 0x06;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
            return;
        }

        if (REG8(0x00114D8EUL) >= 0x04 && REG8(0x00114D8EUL) <= 0x05) {
            if (REG8(0x00114DACUL) != 0) return;

            REG8(0x00114D93UL) = 0x00;
            module_home_request();
            REG8(0x00114D98UL) = 0x00;
            REG8(0x0011F30EUL) = 0x00;
            REG8(0x00114D8EUL) = 0x00;

            if (REG8(0x00FFFEC6UL) != 0) { REG8(0x00114D8AUL) = 0x04; return; }
            if (!module_link_quiet()) return;

            REG8(0x00114D72UL) = 0x3E;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
            return;
        }

        if (REG8(0x00114D8EUL) >= 0x01 && REG8(0x00114D8EUL) <= 0x03) {
            if (module_identify() == 0) { link_claim(0x0B); return; }

            REG8(0x0011A63CUL) = 0x00;
            REG8(0x0011F4E6UL) = 0x00;
            REG8(0x00114D96UL) = 0x00;
            REG8(0x0011F534UL) = 0x00;
            REG8(0x00114D98UL) = 0x00;
            stitch_reset_current();
            REG16(0x0011F292UL) = 0x0000;
            REG8(0x00114D89UL) = 0x00;
            REG16(0x0011F4DCUL) = 0x0000;
            REG16(0x0011F4DEUL) = 0x0000;
            REG8(0x00114D8EUL) = 0x00;
            REG8(0x00114D72UL) = 0x3F;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
        }
        return;

    case 0x81:
    default:
        return;
    }
}
