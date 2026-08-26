/* The artista 180 application, rebuilt in C: the stitch database, and
 * making a pattern current.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* The descriptor for a pattern, and the base of its working copy. */
u32 stitch_record(u16 n)
{
    return STITCH_TABLE + (u32)((u16)0x0018 * n);
}

u32 stitch_work(u16 n)
{
    return STITCH_WORKING + (u32)(u16)(n << 4);
}

/* H'202A3E. One byte out of a pattern's data, by index. */
u8 pattern_byte(u16 n, u16 index)
{
    u32 p;

    if (STITCH_SET == 0xAA) p = REG32(stitch_record(n));
    else                    p = REG32(stitch_record(n) + 4);

    if (REG8(p + 7) & 0x80) p = REG32(p + 0x14);

    return REG8(p + index);
}

/* Which kind to answer by. The working copy's own kind wins, but only when
 * it has one and only when the caller asked for it; otherwise the
 * catalogue's. This is the head of all six readers. */
static u8 stitch_kind(u16 n, u8 use_working)
{
    u8 kind = (u8)(REG8(stitch_work(n)) & 0x3F);

    if (kind == 0 || use_working == 0) kind = REG8(stitch_record(n) + 0x16);
    return kind;
}

/* H'202DA4. */
u8 stitch_param_1(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x80: return REG8(0x57B6D6UL + (u32)(u16)(n << 2));
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 1);
    default:
        return pattern_byte(n, 0x0009);
    }
}

/* H'202C30. */
u8 stitch_param_2(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x80:
        return REG8(0x57B6D7UL + (u32)(u16)(n << 2));
    case 0x81:
        if (!STITCH_ALT_MODE) return REG8(stitch_work(n) + 2);
        return REG8(0x57B6D7UL + (u32)(u16)(n << 2));
    case 0x82:
        if (STITCH_ALT_MODE) return REG8(stitch_work(n) + 3);
        return REG8(stitch_work(n) + 2);
    case 0x01:
        if (!STITCH_ALT_MODE) return REG8(stitch_work(n) + 2);
        return pattern_byte(n, 0x000B);
    case 0x02:
        if (STITCH_ALT_MODE) return REG8(stitch_work(n) + 3);
        return REG8(stitch_work(n) + 2);
    default:
        return pattern_byte(n, 0x000B);
    }
}

/* H'202B36. The only one whose data byte holds two things: the low nibble
 * is this parameter and the high nibble is something else. */
u8 stitch_param_4(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x80: return REG8(0x57B6D9UL + (u32)(u16)(n << 2));
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 4);
    default:
        return (u8)(pattern_byte(n, 0x000D) & 0x0F);
    }
}

/* H'202E9A. Leaves the pattern number behind at H'11A672, which the
 * original does whether or not it goes on to use it. */
u8 stitch_param_5(u16 n, u8 use_working)
{
    u8 kind;

    kind = (u8)(REG8(stitch_work(n)) & 0x3F);
    REG16(0x11A672UL) = n;
    if (kind == 0 || use_working == 0) kind = REG8(stitch_record(n) + 0x16);

    switch (kind) {
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 5);
    default:
        return pattern_byte(n, 0x000F);
    }
}

/* H'202F98. No catalogue value and no default: outside the working copy
 * this parameter simply does not exist. */
u8 stitch_param_6(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 6);
    default:
        return 0;
    }
}

/* H'203062. */
u8 stitch_param_7(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x81: case 0x01:
        if (!STITCH_ALT_MODE) return REG8(stitch_work(n) + 7);
        return 0;
    case 0x82: case 0x02:
        if (STITCH_ALT_MODE) return REG8(stitch_work(n) + 8);
        return REG8(stitch_work(n) + 7);
    default:
        return 0;
    }
}

/* H'206724. All six back into the working copy, each behind its own "this
 * one is present" flag, and the working copy's kind set to say it now holds
 * something. The kind becomes 2 in the alternate mode and 1 otherwise; bit 7
 * of the kind byte is the one thing kept across the write.
 *
 * The leading argument is a mark: when it is set, something elsewhere is
 * told the pattern has been edited.
 */
void stitch_params_set(u8 mark, u16 n,
                       u8 f1, u8 v1, u8 f2, u8 v2, u8 f3, u8 v3,
                       u8 f4, u8 v4, u8 f5, u8 v5, u8 f6, u8 v6,
                       u8 f7, u8 v7)
{
    u32 w = stitch_work(n);
    u8 kind = (u8)(REG8(w) & 0x3F);

    if (kind == 0) kind = 0x01;

    if (mark != 0) {
        REG8(0x11A6AEUL) |= 0x01;
        REG8(0x114DCDUL) &= (u8)~0x08;
    }

    if (f1 != 0) REG8(w + 1) = v1;

    if (STITCH_ALT_MODE) {
        if (f2 != 0) REG8(w + 3) = v2;
        if (f5 != 0) REG8(w + 8) = v5;
        kind = 0x02;
    } else {
        if (f2 != 0) REG8(w + 2) = v2;
        if (f5 != 0) REG8(w + 7) = v5;
    }

    if (f3 != 0) REG8(w + 4) = v3;
    if (f4 != 0) REG8(w + 5) = v4;
    if (f6 != 0) REG8(w + 6) = v6;
    if (f7 != 0) REG8(w + 9) = v7;

    REG8(w) = (u8)(REG8(w) & 0x80);
    REG8(w) = (u8)(REG8(w) | kind);
}

/* One pattern's six parameters read out of the catalogue and written into
 * its working copy. Reading with use_working clear is what makes this a
 * reload of the defaults rather than a copy of what is already there. */
static void stitch_params_reload_one(u16 n)
{
    u8 p6 = stitch_param_6(n, 0);
    u8 p7 = stitch_param_7(n, 0);
    u8 p5 = stitch_param_5(n, 0);
    u8 p4 = stitch_param_4(n, 0);
    u8 p2 = stitch_param_2(n, 0);
    u8 p1 = stitch_param_1(n, 0);

    stitch_params_set(0, n, 1, p1, 1, p2, 1, p4, 1, p5, 1, p7, 1, p6, 1, 0);
}

/* H'2086FC. The two reserved patterns, H'03FE and H'03FF. */
void stitch_params_reload(void)
{
    stitch_params_reload_one(0x03FE);
    stitch_params_reload_one(0x03FF);
}

/* ---- making a pattern current --------------------------------------------
 * H'208210 and everything under it. Choosing a stitch is not a matter of
 * setting one number: the pattern's parameters have to be brought out of the
 * database into the live sewing block, the limits and the motor positions
 * recomputed from them, and the panel told what has changed. This is the
 * layer that does it, built from the bottom up.
 *
 * Three blocks of RAM matter throughout:
 *
 *   H'FFFEE2..H'FFFEFF  the live sewing parameters, what the interrupt
 *                       handlers and the motors read
 *   H'11A7BC..H'11A7EF  the pattern's own copy, as selected
 *   H'11A6A2..H'11A6AC  a saved set, so a temporary change can be undone
 *
 * H'11A6AE collects "this changed" bits for whatever redraws the panel.
 */

/* H'200D24, H'200D34. The two parameters the knobs drive, each held in a
 * pair of bytes and stored at twice the value it is given. Which of the two
 * is the stitch width and which the length is not established here. */
u8 sew_param_a_set(u8 v)
{
    v = (u8)(v << 1);
    REG8(0x11A6D4UL) = v;
    REG8(0x11A6D3UL) = v;
    return v;
}

u8 sew_param_b_set(u8 v)
{
    v = (u8)(v << 1);
    REG8(0x11A6D6UL) = v;
    REG8(0x11A6D5UL) = v;
    return v;
}

/* H'201748. Walks back down the catalogue while byte +H'17 of the
 * descriptor reads 1, and hands back the first value that is not. Byte
 * H'17 marks a pattern as a continuation of the one before it, so this is
 * how the head of a run is found -- though what it returns is the marker,
 * not where it stopped. */
u16 stitch_run_head(u16 n)
{
    u8 mark = REG8(stitch_record(n) + 0x17);

    while (mark == 0x01) {
        n--;
        mark = REG8(stitch_record(n) + 0x17);
    }
    return mark;
}

/* H'2021F6, H'202212. Two of the "busy" bits let go. */
void sew_clear_busy_3(void)
{
    REG8(0x114DC9UL) &= (u8)~0x08;
    REG8(0x11A6AFUL) = 0;
}

void sew_clear_busy_4(void)
{
    REG8(0x114DC9UL) &= (u8)~0x10;
    REG8(0x11A6AFUL) = 0;
}

/* H'202136. */
void sew_stop_flags_clear(void)
{
    REG8(0x114DC6UL) &= (u8)~0x40;
    REG8(0x114DC7UL) &= (u8)~0x80;
    REG8(0xFFFEF8UL) &= (u8)~0x20;
    REG8(0x11A6CFUL) = 0;
}

/* H'202438. */
void sew_flag_copy_6(void)
{
    if (REG8(0xFFFEF5UL) & 0x40) REG8(0x11A7D4UL) |= 0x40;
}

/* H'206636, H'206652. Two bytes of a block on the bus at H'0E0000. */
u8 bus_byte_10(void) { return REG8(0x0E0010UL); }
u8 bus_byte_11(void) { return REG8(0x0E0011UL); }

/* H'20669A and H'2066CC. The stitch length as the operator sees it.
 *
 * H'11A7AC holds it in fifteenths of the machine's own unit and offset by
 * the block byte at H'0E0010; the screen works in whole units. The two are
 * the round trip: fifteen-hundredths out with rounding, and back the other
 * way with the flags that say the pattern has been changed.
 */
u8 stitch_length_shown(void)
{
    const short scaled =
        (short)((short)(0x0F * (u16)REG8(0x0011A7ACUL)) + 0x0032);

    return (u8)((u8)((short)scaled / 100) - bus_byte_10());
}

void stitch_length_choose(u8 shown)
{
    const u8 raw = (u8)(shown + bus_byte_10());
    const short scaled = (short)((short)(0x64 * (u16)raw) + 0x0007);

    REG8(0x0011A7ACUL) = (u8)((u8)((short)scaled / 15) + 0xFE);

    REG8(0x00114DC6UL) |= 0x08;
    REG8(0x00FFFEFAUL) |= 0x80;
    REG8(0x00FFFEF7UL) &= (u8)~0x08;
    REG8(0x00FFFEF5UL) |= 0x40;
}

/* H'2023BA. The pattern's copy out to the live parameters. */
void sew_params_publish(void)
{
    REG8(0xFFFEE2UL) = REG8(0x11A7BCUL);
    REG8(0xFFFEE3UL) = REG8(0x11A7BDUL);
    REG8(0x11A69FUL) = REG8(0x11A7BFUL);
    REG8(0xFFFEE6UL) = REG8(0x11A7C0UL);
    REG8(0x11A6A1UL) = REG8(0x11A7C1UL);
    REG8(0xFFFEE9UL) = REG8(0x11A7C2UL);
    REG8(0xFFFEEDUL) = REG8(0x11A7C5UL);
    REG8(0xFFFEEBUL) = REG8(0x11A7C4UL);
    REG8(0xFFFEEEUL) = REG8(0x11A7C6UL);
    REG8(0xFFFEEFUL) = REG8(0x11A7C7UL);
}

/* H'2068CE, H'206A6A. Eleven bytes put aside and put back. What is saved is
 * the live set; what is restored writes both halves of the two pairs. */
void sew_params_save(void)
{
    REG8(0x11A6A2UL) = REG8(0xFFFEEAUL);
    REG8(0x11A6A3UL) = REG8(0x11A69EUL);
    REG8(0x11A6A4UL) = REG8(0x11A6D3UL);
    REG8(0x11A6A5UL) = REG8(0x11A69FUL);
    REG8(0x11A6A6UL) = REG8(0xFFFEE6UL);
    REG8(0x11A6A7UL) = REG8(0x11A6A0UL);
    REG8(0x11A6A8UL) = REG8(0x11A6D5UL);
    REG8(0x11A6A9UL) = REG8(0x11A6A1UL);
    REG8(0x11A6AAUL) = REG8(0xFFFEE9UL);
    REG8(0x11A6ABUL) = REG8(0xFFFEECUL);
    REG8(0x11A6ACUL) = REG8(0xFFFEEDUL);
}

void sew_params_restore(void)
{
    u8 v;

    REG8(0xFFFEEAUL) = REG8(0x11A6A2UL);
    REG8(0x11A69EUL) = REG8(0x11A6A3UL);
    v = REG8(0x11A6A4UL);
    REG8(0x11A6D4UL) = v;
    REG8(0x11A6D3UL) = v;
    REG8(0x11A69FUL) = REG8(0x11A6A5UL);
    REG8(0xFFFEE6UL) = REG8(0x11A6A6UL);
    REG8(0x11A6A0UL) = REG8(0x11A6A7UL);
    v = REG8(0x11A6A8UL);
    REG8(0x11A6D6UL) = v;
    REG8(0x11A6D5UL) = v;
    REG8(0x11A6A1UL) = REG8(0x11A6A9UL);
    REG8(0xFFFEE9UL) = REG8(0x11A6AAUL);
    REG8(0xFFFEECUL) = REG8(0x11A6ABUL);
    REG8(0xFFFEEDUL) = REG8(0x11A6ACUL);
}

/* H'202456. The live parameters copied the other way, into the pattern's
 * copy, with three of them unpacked out of the bytes they share. */
void sew_params_capture(void)
{
    u8 v;

    v = REG8(0xFFFEF5UL);
    REG8(0x11A7D4UL) = v;
    REG8(0x11A7E0UL) = (u8)(v & 0x03);

    v = REG8(0xFFFEF6UL);
    REG8(0x11A7D5UL) = v;
    REG8(0x11A7E1UL) = (u8)((v >> 6) & 0x03);
    REG8(0x11A7E2UL) = (u8)(REG8(0xFFFEF6UL) & 0x0F);

    REG8(0x11A7D6UL) = REG8(0xFFFEF7UL);
    REG8(0x11A7D7UL) = REG8(0xFFFEF8UL);
    REG8(0x11A7D8UL) = REG8(0xFFFEEAUL);
    REG8(0x11A7D9UL) = REG8(0x11A69EUL);
    REG8(0x11A7DAUL) = REG8(0x11A6A0UL);
    REG8(0x11A7DBUL) = REG8(0xFFFEECUL);
    REG8(0x11A7DCUL) = REG8(0xFFFEFBUL);
    REG8(0x11A7DDUL) = REG8(0xFFFEFCUL);

    REG8(0x11A7DEUL) = (u8)((REG8(0xFFFEF9UL) & 0xF0) >> 4);
    REG8(0x11A7DFUL) = (u8)(REG8(0xFFFEF9UL) & 0x0F);
}

/* H'20251C. Eighteen bytes of the pattern's copy loaded from a row of a
 * table of pointers at H'11A6E8, chosen by H'11A7A8. */
void sew_variant_load(void)
{
    u8 i;
    u32 row;

    for (i = 0; i < 0x12; i++) {
        row = REG32(0x11A6E8UL + (u32)(short)(u16)(REG8(0x11A7A8UL) << 2));
        REG8(0x11A7B6UL + i) = REG8(row + i);
    }
    REG8(0x11A7E3UL) = REG8(0x11A7C0UL);
    REG8(0x11A7E4UL) = REG8(0x11A7C2UL);
}

/* H'2079E0. Which of the two data sets is in use decides whether the
 * alternate-mode bit is allowed to be on at all. */
void sew_mode_arbitrate(void)
{
    u8 v;

    if (STITCH_SET == 0xAA) {
        REG8(0xFFFEF8UL) &= (u8)~0x10;
        return;
    }

    v = REG8(0xFFFEF8UL);
    if ((v & 0x04) || (v & 0x08)) {
        REG8(0xFFFEF8UL) = (u8)(v | 0x10);
        if (REG8(0x11A7BDUL) & 0x02) REG8(0x114DCEUL) |= 0x40;
        else                         REG8(0x114DCEUL) &= (u8)~0x40;
        return;                 /* and not on into the test below */
    }

    if (REG8(0x114DCEUL) & 0x40) REG8(0xFFFEF8UL) |=        0x10;
    else                         REG8(0xFFFEF8UL) &= (u8)~0x10;
}

/* H'205E82, H'205EB6, H'205F0C, H'20602E. Four small watchers: each notices
 * that something has moved and raises a bit in H'11A6AE, or drives a pin. */
void sew_watch_7DE(void)
{
    if (REG8(0x11A7EFUL) != REG8(0x11A7DEUL)) {
        REG8(0x11A6AEUL) |= 0x02;
        REG8(0x11A7EFUL) = REG8(0x11A7DEUL);
    }
}

void sew_watch_7E1(void)
{
    u8 v = REG8(0x11A7E1UL);

    if (v == 0x02 || v == 0x03) {
        if (!(REG8(0x114DCAUL) & 0x20)) {
            REG8(0x11A6AEUL) |= 0x02;
            REG8(0x114DCAUL) |= 0x20;
        }
    } else {
        if (REG8(0x114DCAUL) & 0x20) {
            REG8(0x11A6AEUL) |= 0x02;
            REG8(0x114DCAUL) &= (u8)~0x20;
        }
    }
}

void sew_needle_stop_pin(void)
{
    if (REG8(0x11A7D5UL) & 0x20) {
        REG8(0xFFFEC7UL) |=        0x02;
        REG8(0xFFFEC7UL) &= (u8)~0x04;
    } else {
        REG8(0xFFFEC7UL) &= (u8)~0x02;
        REG8(0xFFFEC7UL) |=        0x04;
    }
}

void sew_watch_67D(void)
{
    u8 on = (REG8(0x11A67DUL) & 0x20) != 0;

    if (REG8(0x114DCDUL) & 0x01) on = !on;

    if (on) REG8(0x11A6AEUL) |=        0x20;
    else    REG8(0x11A6AEUL) &= (u8)~0x20;
}

/* H'205A80. A limit derived from a setting in flash and the pattern's own
 * byte, clamped into H'00..H'28 and then subtracted from H'29. */
void sew_limit_6BB(void)
{
    u8 setting = REG8(0x57FF8FUL);
    short t;

    if (setting > 0x10) setting = 0x08;

    t = (short)(signed char)(u8)(REG8(0x11A7DBUL) + setting + 0xF8);
    if (t < 0) t = 0;
    if (t > 0x28) t = 0x28;

    REG8(0x11A6BBUL) = (u8)(0x29 - t);
}

/* H'2051AC. Interpolates between two ends by a percentage, and offsets the
 * result by H'11A6C2. The multiply is 16-bit and its product is sign
 * extended before the divide, the same as elsewhere in this ROM. */
u8 sew_interpolate(u8 percent, u8 from, u8 to)
{
    u16 span = (u16)((u16)from - (u16)to);
    u16 p    = (u16)(span * (u16)percent);
    short q  = (short)((short)p / (short)0x0064);

    return (u8)((u8)((u8)q + REG8(0x11A6C2UL)) + to);
}

/* H'2051EA. The offset the interpolation above works from. */
void sew_offset_set(void)
{
    u8 a = REG8(0x11A7D8UL);
    u8 top = 0x64;
    u8 b = REG8(0x11A7D9UL);
    u8 extra = 0;
    u16 t;

    if (STITCH_SET == 0xB4 && !(REG8(0xFFFEF8UL) & 0x10)) {
        if (b > 0x38) b = 0x38;
        top = 0x38;
        extra = (u8)((short)((short)(0x0064 - (u16)top) / (short)2));
    }

    t = (u16)((u16)((u16)top - (u16)b) * (u16)a);
    REG8(0x11A6C2UL) = (u8)((u8)((short)((short)t / (short)10)) + extra);
}

/* H'2055B6. How far past one of four limits the free-running counter at
 * H'FFFF00 has gone, or zero if it is still inside. */
u16 sew_limit_overshoot(void)
{
    u16 now = REG16(0xFFFF00UL);
    u8 which = REG8(0x11A7ADUL);
    u16 edge;

    if (which == 0x00) {
        edge = REG16(0x11A7AEUL);
        if (now >= edge) return (u16)(now - edge);
    } else if (which == 0x01) {
        edge = REG16(0x11A7B0UL);
        if (now <= edge) return (u16)(edge - now);
    } else if (which == 0x02) {
        edge = REG16(0x11A7B2UL);
        if (now >= edge) return (u16)(now - edge);
    } else if (which == 0x03) {
        edge = REG16(0x11A7B4UL);
        if (now <= edge) return (u16)(edge - now);
    }
    return 0;
}

/* H'202318. Two of the live parameters scaled from the pattern's units into
 * the panel's, H'12 per unit rounded, capped at H'64 -- but only for the
 * H'B4 data set in the ordinary mode. Otherwise they go across as they are.
 * Bit 7 of H'FFFEE5 belongs to something else and is kept. */
void sew_display_scale(u8 v)
{
    u8 top = 0x64;
    u8 a, b;

    if (STITCH_SET == 0xB4 && !(REG8(0xFFFEF8UL) & 0x10)) {
        a = (u8)((short)((short)(u16)((u16)(0x12 * (u16)v) + 5) / (short)10));
        if (a > top) a = top;

        b = (u8)((short)((short)(u16)((u16)(0x12 * (u16)REG8(0x11A69FUL)) + 5)
                         / (short)10));
        if (b > top) b = top;

        REG8(0xFFFEE5UL) = (u8)((REG8(0xFFFEE5UL) & 0x80) | b);
        REG8(0xFFFEE4UL) = a;
    } else {
        REG8(0xFFFEE4UL) = REG8(0x11A69EUL);
        REG8(0xFFFEE5UL) = (u8)((REG8(0xFFFEE5UL) & 0x80) |
                                REG8(0x11A69FUL));
    }
}

/* H'203872. The hour and stitch counters. H'11A6D8 counts up while the
 * motor is turning and rolls a service counter every H'EA60; the two
 * longwords at H'11A6E0 and H'11A6DC are lifetime totals, one of them only
 * while the pedal is down. */
void sew_counters_tick(void)
{
    u8 v;

    if ((REG8(0x114DC6UL) & 0x80) && REG8(0xFFFEC8UL) != 0) {
        if (REG16(0x11A6D8UL) >= 0xEA60) {
            REG16(0x11A6D8UL) = (u16)(REG16(0x11A6D8UL) + 0x15A0);
            v = (u8)(REG8(0x11A6D0UL) + 1);
            REG8(0x11A6D0UL) = v;
            if (v >= 0x02) {
                REG8(0x11A6D0UL) = 0;
                REG8(0x114DC8UL) |= 0x10;
            }
            REG32(0x11A6E0UL) = REG32(0x11A6E0UL) + 1;
            if (REG8(0xFFFEC4UL) & 0x01) {
                REG32(0x11A6DCUL) = REG32(0x11A6DCUL) + 1;
            }
        }
        REG16(0x11A6DAUL) = REG16(0x11A6D8UL);
    } else if (!(REG8(0x114DC6UL) & 0x80)) {
        REG16(0x11A6D8UL) = REG16(0x11A6DAUL);
    }

    if (!(REG8(0x11A6AEUL) & 0x80)) {
        REG8(0x114DC8UL) |= 0x08;
    } else if (REG8(0x114DC8UL) & 0x08) {
        REG8(0x114DC8UL) &= (u8)~0x08;
        REG32(0x11A6E4UL) = REG32(0x11A6E4UL) + 1;
    }
}

/* H'20078E, H'200784. A byte copy. The count is in ER4, source in ER5 and
 * destination in ER6, which is not the convention anything else here uses --
 * these two are hand-written and their callers set the registers directly.
 * H'200784 is the same thing with the top half of the count cleared, so it
 * takes a 16-bit length. */
void mem_copy_long(u8 *dst, const u8 *src, u32 n)
{
    if (n == 0) return;
    do {
        *dst++ = *src++;
        n--;
    } while (n != 0);
}

void mem_copy(u8 *dst, const u8 *src, u16 n)
{
    mem_copy_long(dst, src, (u32)n);
}

/* H'20655A, H'20661C. The selected pattern's data streamed into the buffer
 * at H'0E0000, a chunk at a time. H'11A6D2 is how many chunks the whole is
 * cut into and H'11A6D1 is which one is next; the chunk size follows from
 * H'1800 divided by the count. */
void stitch_chunk_load(void)
{
    u8  chunks = REG8(0x11A6D2UL);
    u16 size   = (u16)((short)((short)0x1800 / (short)(u16)chunks));
    u8  index  = REG8(0x11A6D1UL);
    u32 offset = (u32)(short)(u16)(size * (u16)index);
    u32 src;

    REG8(0x11A6D1UL) = (u8)(index + 1);

    if (STITCH_SET == 0xAA) src = REG32(stitch_record(REG16(0x11A7E6UL)));
    else                    src = REG32(stitch_record(REG16(0x11A7E6UL)) + 4);

    mem_copy((u8 *)(0x0E0000UL + offset), (const u8 *)(src + offset), size);
}

void stitch_chunk_first(void)
{
    REG8(0x11A6D1UL) = 0x00;
    REG8(0x11A6D2UL) = 0x10;
    stitch_chunk_load();
}

/* H'20666E. The smaller of two bytes out of the loaded pattern. */
u8 stitch_length_limit(void)
{
    u8 a = REG8(0x0E0013UL);
    u8 b = bus_byte_11();

    if (a >= b) a = b;
    return a;
}

/* H'2021D8, H'2060D2, H'206100. */
void sew_clear_67D_5(void)
{
    REG8(0x11A6AEUL) &= (u8)~0x20;
    REG8(0x11A67DUL) &= (u8)~0x20;
}

void sew_set_67D_5(void)
{
    if (!(REG8(0x11A7D4UL) & 0x08)) {
        REG8(0x11A6AEUL) |= 0x20;
        REG8(0x11A67DUL) |= 0x20;
    } else {
        sew_clear_67D_5();
    }
}

/* H'205FC0. The variant the pattern is shown in, when the machine is in the
 * mode where that is chosen by hand. */
void sew_variant_select(void)
{
    u8 v;

    if (REG8(0xFFFEFDUL) == 0x02) {
        v = (u8)(REG8(0xFFFEFAUL) & 0x07);
        if (v == 0) REG8(0x11A7A8UL) = 0;
        else        REG8(0x11A7A8UL) = (u8)(v + 0xFF);
        return;
    }

    if (REG8(0x114DCDUL) & 0x01) {
        if (!(REG8(0x11A67CUL) & 0x80)) {
            REG8(0x114DCBUL) |= 0x20;
            REG8(0x11A67CUL) |= 0x80;
        }
    } else {
        REG8(0x11A67CUL) &= (u8)~0x80;
    }
}

/* H'206084, H'2060A6. */
void sew_variant_service(void)
{
    if (!(REG8(0x114DC6UL) & 0x08)) {
        if (REG8(0x11A7BDUL) & 0x08) sew_variant_select();
        else                         sew_watch_67D();
    }
}

void sew_mode_bit_service(void)
{
    if (REG8(0x11A7D7UL) & 0x80) REG8(0x114DCDUL) |=        0x01;
    else                         REG8(0x114DCDUL) &= (u8)~0x01;
    sew_variant_service();
}

/* H'207D6E. Both knob parameters clamped down to what the pattern allows. */
void sew_params_clamp(u8 max_a, u8 max_b)
{
    u8 a = REG8(0x11A69EUL);

    if (a > max_a) {
        REG8(0x11A69EUL) = max_a;
        sew_param_a_set(max_a);
    }
    if (REG8(0x11A6A0UL) > max_b) {
        REG8(0x11A6A0UL) = max_b;
        sew_param_b_set(max_b);
    }
}

/* H'2020BC, H'2020BE, H'2020E4, H'202166, H'2021A0. Two latched changes:
 * each notices its flag has flipped, remembers that it has, and does the
 * one-off work. H'2020BC is empty in the original and left empty here. */
void sew_edge_on_a(void) { }

void sew_edge_off_a(void)
{
    REG8(0x114DC7UL) &= (u8)~0x80;
    REG8(0xFFFEF8UL) &= (u8)~0x20;
    REG8(0x11A6CFUL) = 0;
}

void sew_edge_off_b(void)
{
    REG8(0x114DC6UL) |= 0x40;

    if (REG8(0x114DCAUL) & 0x02) {
        if (REG8(0x114DCAUL) & 0x08) REG8(0x11A688UL) = 0x01;
        else                         REG8(0x11A688UL) = 0x00;
    } else {
        if (REG8(0x114DC9UL) & 0x20) REG8(0x11A688UL) = 0x00;
        else                         REG8(0x11A688UL) = 0x01;
    }
}

void sew_latch_7D4_7(void)
{
    if (REG8(0x11A7D4UL) & 0x80) {
        if (REG8(0x114DD0UL) == 0) {
            REG8(0x114DD0UL) = 0x01;
            sew_edge_on_a();
        }
    } else {
        if (REG8(0x114DD0UL) != 0) {
            REG8(0x114DD0UL) = 0x00;
            sew_edge_off_a();
        }
    }
}

void sew_latch_7DF(void)
{
    if (REG8(0x11A7DFUL) == 0) {
        if (REG8(0x114DD1UL) == 0) {
            REG8(0x114DD1UL) = 0x01;
            sew_stop_flags_clear();
        }
    } else {
        if (REG8(0x114DD1UL) != 0) {
            REG8(0x114DD1UL) = 0x00;
            sew_edge_off_b();
        }
    }
}

/* H'20222E, H'205F3E. */
void sew_busy_release(void)
{
    u8 v;

    REG8(0x114DC6UL) &= (u8)~0x10;
    v = REG8(0x114DC9UL);
    if ((v & 0x08) || (v & 0x10)) REG8(0x11A6AEUL) |= 0x02;
    sew_clear_busy_3();
    sew_clear_busy_4();
}

/* Bit 4 of H'11A7D5 is the pattern asking to be sewn a fixed number of
 * times rather than continuously. Without it, whatever was counting is let
 * go; with it, the count is armed once. */
void sew_repeat_service(void)
{
    u8 v;

    if (!(REG8(0x11A7D5UL) & 0x10) || (REG8(0x114DC8UL) & 0x01)) {
        sew_busy_release();
        return;
    }
    if (REG8(0x114DCAUL) & 0x01) return;
    if (REG8(0x114DC6UL) & 0x10) return;

    REG8(0x114DC6UL) |= 0x10;

    v = REG8(0x114DC9UL);
    if (!(v & 0x20)) {
        /* The original tests the same bit twice here, so the three checks
         * that follow the second test can never run: with the bit clear it
         * leaves at once, and with it set it never reaches them. Written
         * the same way, and the unreachable half is left out rather than
         * guessed at. */
        return;
    }

    REG8(0x114DC9UL) |= 0x08;
    REG8(0x11A6AEUL) |= 0x02;
}
