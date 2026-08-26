/* The artista 180 application, rebuilt in C: the queue, stepping through
 * it, and stopping in the right place.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

static u32 queue_entry(u16 i)
{
    return QUEUE + (u32)(u16)((u16)0x000D * i);
}

/* H'201798. The first two bytes of an entry, as a word. Callers mask it
 * with H'03FF to get the pattern number. */
u16 queue_entry_ref(u16 i)
{
    u32 e = queue_entry(i);

    return (u16)((u16)REG8(e) | (u16)((u16)REG8(e + 1) << 8));
}

/* H'205CA8. The packed parameters of the entry at the current position,
 * unpacked into the pattern's copy. */
void queue_entry_unpack(void)
{
    u32 e = queue_entry(QUEUE_POS);
    u8 b;

    b = REG8(e + 1);
    REG8(0x11A7E1UL) = (u8)((b >> 6) & 0x03);
    REG8(0x11A7D8UL) = (u8)((b & 0x3C) >> 2);

    REG8(0x11A7DAUL) = REG8(queue_entry(QUEUE_POS) + 2);
    REG8(0x11A7D9UL) = REG8(queue_entry(QUEUE_POS) + 3);

    b = REG8(queue_entry(QUEUE_POS) + 4);
    REG8(0x11A7DBUL) = (u8)(b & 0x3F);
    if (b & 0x80) REG8(0x11A7D4UL) |=        0x04;
    else          REG8(0x11A7D4UL) &= (u8)~0x04;

    REG8(0x11A7DCUL) = REG8(queue_entry(QUEUE_POS) + 5);
    REG8(0x11A7DDUL) = REG8(queue_entry(QUEUE_POS) + 6);

    b = REG8(queue_entry(QUEUE_POS) + 7);
    REG8(0x11A7E0UL) = (u8)(b & 0x03);
    REG8(0x11A7DEUL) = (u8)((b & 0xF0) >> 4);
    if (b & 0x08) REG8(0x11A7D4UL) |=        0x10;
    else          REG8(0x11A7D4UL) &= (u8)~0x10;
}

/* The pattern an entry names, with the per-entry offset at byte H'0A added.
 * Every accessor call in this layer goes through this. */
static u16 queue_pattern(u16 i)
{
    return (u16)((u16)(queue_entry_ref(i) & 0x03FF) +
                 (u16)REG8(queue_entry(i) + 0x0A));
}

/* H'203564. Bit 2 of the pattern's byte 6. */
u8 pattern_is_group(u16 n)
{
    return (u8)(pattern_byte(n, 0x0006) & 0x04);
}

/* H'20348C. */
void queue_flags_reset(void)
{
    REG8(0x114DCAUL) &= (u8)~0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DC9UL) &= (u8)~0x02;
    REG8(0x114DCCUL) |=        0x20;
}

/* H'203632. Walks back from the current position to the start of its group
 * and records it. A position whose entry is the group marker, or which is
 * the very first, is itself the start. */
void queue_group_start(void)
{
    u16 i = QUEUE_POS;

    if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END || i == QUEUE_FIRST) {
        QUEUE_GROUP = i;
        return;
    }

    for (i = QUEUE_POS; i >= QUEUE_FIRST; i--) {
        if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END || i == QUEUE_FIRST) {
            QUEUE_GROUP = i;
            return;
        }
    }
}

/* H'203580. Once every H'65 passes, walks the rest of the queue: if any
 * pattern in it is a group, the count at H'FFFEEB goes up from 1 to H'28,
 * and a run head of H'15 raises bit 7 of H'114DCE. */
void queue_scan_service(void)
{
    u16 i;
    u16 n;

    if (REG8(0x11A7EEUL) < 0x64) {
        REG8(0x11A7EEUL)++;
        return;
    }

    REG8(0x11A7EEUL) = 0;
    REG8(0x114DCEUL) &= (u8)~0x80;
    REG8(0xFFFEEBUL) = 0x01;

    for (i = (u16)(QUEUE_GROUP + 1); i <= QUEUE_LAST; i++) {
        if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END) break;
        n = queue_pattern(i);
        if (pattern_is_group(n) != 0) REG8(0xFFFEEBUL) = 0x28;
        if (stitch_run_head(n) == 0x0015) REG8(0x114DCEUL) |= 0x80;
    }
}

/* H'206958. The pattern's parameters into the live set, three of them
 * fetched from the catalogue rather than the queue. */
void sew_params_from_pattern(void)
{
    u16 n;

    REG8(0xFFFEEAUL) = REG8(0x11A7D8UL);
    REG8(0x11A69EUL) = REG8(0x11A7D9UL);
    sew_param_a_set(REG8(0x11A7D9UL));

    n = queue_pattern(QUEUE_POS);
    REG8(0x11A7BFUL) = stitch_param_1(n, 0);
    REG8(0x11A69FUL) = REG8(0x11A7BFUL);

    REG8(0xFFFEE6UL) = REG8(0x11A7C0UL);
    REG8(0x11A6A0UL) = REG8(0x11A7DAUL);
    sew_param_b_set(REG8(0x11A7DAUL));

    n = queue_pattern(QUEUE_POS);
    REG8(0x11A7C1UL) = stitch_param_2(n, 0);
    REG8(0x11A6A1UL) = REG8(0x11A7C1UL);

    REG8(0xFFFEE9UL) = REG8(0x11A7C2UL);
    REG8(0xFFFEECUL) = REG8(0x11A7DBUL);

    n = queue_pattern(QUEUE_POS);
    REG8(0x11A7C5UL) = stitch_param_5(n, 0);
}

/* H'206262. Bit 5 of H'FFFEC1 is a lamp. Depending on the pattern it is
 * held on, blinked, or left off, and H'11A6CD counts the blink. */
void sew_lamp_service(void)
{
    u8 v;

    /* Two ways to be held on, and they share their tail: the current stitch
     * asking for it, or the pattern asking for it while nothing else has a
     * claim. Everything below is the blinking. */
    if (((REG8(0x11A7C8UL) & 0x40) && !(REG8(0x114DCAUL) & 0x10)) ||
        ((REG8(0x11A7BEUL) & 0x80) && (REG8(0x11A7D4UL) & 0x10) &&
         !(REG8(0x114DC9UL) & 0x20) && !(REG8(0x114DCAUL) & 0x10))) {
        REG8(0xFFFEC1UL) |= 0x20;
        REG8(0x11A6CDUL) = 0;
        return;
    }

    if ((REG8(0x11A7D4UL) & 0x10) && (REG8(0x11A681UL) & 0x01)) {
        v = REG8(0xFFFEC1UL);
        if (v & 0x20) {
            REG8(0xFFFEC1UL) = (u8)(v & ~0x20);
        } else {
            REG8(0xFFFEC1UL) = (u8)(v | 0x20);
            REG8(0x11A6CDUL) = 0;
        }
        return;
    }

    if (REG8(0x11A7D4UL) & 0x20) {
        v = REG8(0xFFFEC1UL);
        if (v & 0x20) {
            v = (u8)(REG8(0x11A6CDUL) + 1);
            REG8(0x11A6CDUL) = v;
            if ((u8)(v - 1) >= 0x03) REG8(0xFFFEC1UL) &= (u8)~0x20;
        } else {
            REG8(0xFFFEC1UL) = (u8)(v | 0x20);
            REG8(0x11A6CDUL) = 0;
        }
    } else {
        REG8(0xFFFEC1UL) &= (u8)~0x20;
    }
}

/* H'205DDC. Bit 6 of H'11A7D4 is the request to move on to the next
 * position. Granted only while the queue is live; otherwise it just asks
 * for a redraw. */
void queue_advance(void)
{
    if (!(REG8(0x11A7D4UL) & 0x40)) return;

    REG8(0x11A688UL) = 0;
    REG8(0xFFFEF5UL) &= (u8)~0x40;

    if (!(REG8(0x114DCAUL) & 0x02)) {
        REG8(0x11A6AEUL) |= 0x02;
        return;
    }

    QUEUE_POS = (u16)(QUEUE_GROUP + 1);
    queue_flags_reset();

    REG16(0x11A7E6UL) = queue_pattern(QUEUE_POS);
    REG16(0x11A66EUL) = REG16(0x11A7E6UL);

    REG8(0x11A6AEUL) |= 0x01;
    REG8(0x114DCDUL) &= (u8)~0x08;
    REG8(0x114DCAUL) |= 0x80;
}

/* H'206104. The speed ceiling, scaled by the pattern's own class: full,
 * three quarters, a half or a quarter. Below H'19 nothing is written -- the
 * ceiling is already as low as it goes. */
void sew_speed_limit_scale(void)
{
    u8 v = REG8(0x11A6CEUL);

    if (v < 0x19) return;

    switch (REG8(0x11A7E0UL)) {
    case 0x00: v = (u8)(int)((float)(u32)v);          break;
    case 0x01: v = (u8)(int)((float)(u32)v * 0.75f);  break;
    case 0x02: v = (u8)(int)((float)(u32)v * 0.5f);   break;
    case 0x03: v = (u8)(int)((float)(u32)v * 0.25f);  break;
    default: break;
    }
    REG8(0xFFFECBUL) = v;
}

/* H'2061C6. Everything that has to be looked at once a pass. */
void sew_service(void)
{
    u8 v;

    sew_repeat_service();
    queue_advance();
    sew_latch_7DF();
    sew_latch_7D4_7();
    sew_needle_stop_pin();
    sew_set_67D_5();
    sew_mode_bit_service();
    sew_watch_7E1();
    sew_speed_limit_scale();
    sew_watch_7DE();

    v = REG8(0x11A7BDUL);
    if (!(v & 0x08)) {
        REG8(0xFFFEFAUL) &= (u8)~0x08;
    } else if (!(v & 0x04) && !(v & 0x02)) {
        REG8(0xFFFEFAUL) |= 0x08;
    }

    if (!(REG8(0xFFFEE3UL) & 0x02)) {
        REG8(0x114DCDUL) &= (u8)~0x04;
    } else if (REG8(0x114DC6UL) & 0x80) {
        REG8(0x114DCDUL) |= 0x04;
    }

    if ((REG8(0x114DCAUL) & 0x02) && !(REG8(0x114DC6UL) & 0x80)) {
        queue_scan_service();
    }
}

/* H'203280 and the two handlers its table selects between, H'2033DC and
 * H'2033E8. Seventy-eight screens, and all of them resolve to one of two
 * things: either the machine is held from sewing outright, or it is held
 * only while the queue is somewhere it should not start from.
 *
 * The two handlers share this routine's epilogue and cannot be entered on
 * their own, so they are written inline.
 */
static const u8 sew_mode_holds[0x4E] = {
    1,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,   /* 00..0F */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* 10..1F */
    0,0,0,0,0,0,0,1,1,1,1,1,1,0,1,1,   /* 20..2F */
    0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* 30..3F */
    0,1,1,1,1,0,1,1,1,1,1,1,0,1    /* 40..4D */
};

void sew_mode_dispatch(void)
{
    u8 mode = REG8(0x11A169UL);

    if (mode <= 0x4D && sew_mode_holds[mode]) {
        REG8(0xFFFEC4UL) |= 0x20;
        return;
    }

    if (REG8(0xFFFEC4UL) & 0x01) return;

    if (REG8(0x114DCAUL) & 0x02) {
        if (QUEUE_POS == QUEUE_GROUP ||
            (queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END) {
            REG8(0xFFFEC4UL) |= 0x20;
        }
    } else {
        if (REG16(0x114DDEUL) >= 0x012C) REG8(0xFFFEC4UL) &= (u8)~0x20;
    }
}

/* H'2037FE. Whether the machine counts as running, and whether the panel is
 * to be told. */
void sew_running_flags(void)
{
    u8 v = REG8(0xFFFEC6UL);

    if (v == 0x00 || v == 0x04 || v == 0x05) {
        REG8(0x114DC6UL) &= (u8)~0x80;
        REG8(0x114DCFUL) &= (u8)~0x20;
    } else {
        REG8(0x114DC6UL) |= 0x80;
        if (!(REG8(0x114DCFUL) & 0x20)) REG8(0x114DCFUL) |= 0x20;
    }

    sew_mode_dispatch();

    v = REG8(0xFFFEC0UL);
    if (v == 0x01 || v == 0x00) REG8(0x11A6AEUL) |=        0x80;
    else                        REG8(0x11A6AEUL) &= (u8)~0x80;
}

/* H'21F094. Which screen the panel is showing. Everything that has to
 * behave differently depending on where the operator is reads this. */
u16 mode_code(void)
{
    return (u16)REG8(0x11A169UL);
}

/* H'204BA6. The two knob parameters are held at twice their value while
 * sewing and at their face value on the screens that show a number. This
 * moves between the two, halving on the screens that want it and copying
 * the doubled pair back on the ones that do not. A value over H'C8 cannot
 * be halved into a byte, so it is pinned at H'64 and put back doubled. */
void sew_params_scale_for_mode(void)
{
    u16 mode = mode_code();
    u8 v;
    int halve;

    halve = !(mode < 2);
    if (halve) {
        if      (mode <  0x0005) halve = 1;
        else if (mode == 0x0007) halve = 1;
        else if (mode <  0x000C) halve = 0;
        else if (mode <  0x000E) halve = 1;
        else if (mode == 0x0018) halve = 1;
        else if (mode == 0x002B) halve = 1;
        else if (mode == 0x0030) halve = 1;
        else if (mode <  0x0033) halve = 0;
        else if (mode <  0x0037) halve = 1;
        else if (mode == 0x0042) halve = 1;
        else if (mode <  0x0044) halve = 0;
        else if (mode >  0x0047) halve = 0;
        else                     halve = 1;
    }

    if (!halve) {
        REG8(0x11A6D3UL) = REG8(0x11A6D4UL);
        REG8(0x11A6D5UL) = REG8(0x11A6D6UL);
        return;
    }

    v = REG8(0x11A6D3UL);
    if (v > 0xC8) {
        REG8(0x11A69EUL) = 0x64;
        sew_param_a_set(0x64);
    } else {
        REG8(0x11A69EUL) = (u8)(v / 2);
    }

    v = REG8(0x11A6D5UL);
    if (v > 0xC8) {
        REG8(0x11A6A0UL) = 0x64;
        sew_param_b_set(0x64);
    } else {
        REG8(0x11A6A0UL) = (u8)(v / 2);
    }

    REG8(0x11A6D4UL) = REG8(0x11A6D3UL);
    REG8(0x11A6D6UL) = REG8(0x11A6D5UL);
}

/* H'20394A. The counters, and their two lifetime totals written back into
 * the settings block when the service counter has rolled. Bit 5 of
 * H'114DC7 is held up around each flash write -- the same bit
 * config_block_check uses -- so nothing else touches the device while it is
 * being programmed. */
void sew_counters_service(void)
{
    sew_counters_tick();

    if (REG8(0x114DC6UL) & 0x80) return;
    if (!(REG8(0x114DC8UL) & 0x10)) return;

    REG8(0x114DC8UL) &= (u8)~0x10;

    if (REG32(0x57FF82UL) != REG32(0x11A6E0UL)) {
        REG8(0x114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x11A6E0UL, 0x57FF82UL, 4);
        REG8(0x114DC7UL) &= (u8)~0x20;
    }
    if (REG32(0x57FF86UL) != REG32(0x11A6E4UL)) {
        REG8(0x114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x11A6E4UL, 0x57FF86UL, 4);
        REG8(0x114DC7UL) &= (u8)~0x20;
    }
}

/* H'207DAA. The two limits the variant handling worked out, put back into
 * the pattern's copy, and the live values clamped down to them. */
void sew_limits_apply(void)
{
    REG8(0x11A7C0UL) = REG8(0x11A670UL);
    if (REG8(0x11A7D9UL) > REG8(0x11A7C0UL)) {
        REG8(0x11A7D9UL) = REG8(0x11A7C0UL);
    }

    REG8(0x11A7C2UL) = REG8(0x11A671UL);
    if (REG8(0x11A7DAUL) > REG8(0x11A7C2UL)) {
        REG8(0x11A7DAUL) = REG8(0x11A7C2UL);
    }
}

/* H'206348. The pattern's variants unpacked into the buffer.
 *
 * A pattern is not one shape but a set of them -- the same stitch at
 * different widths, or the same letter in different sizes -- and the chunk
 * loader brings the lot into H'0E0000 as one block. This walks that block:
 * byte 7 bit 7 says there is a variant table at all, byte 8 says how many
 * there are (capped at H'30), and from H'0E0014 there is a pointer per
 * variant into the block.
 *
 * Each variant is then copied up above the pointer array, packed end to
 * end, and its pointer rewritten to where it landed. The length comes out
 * of the variant's own header: a big-endian count at +H'10, two or three
 * bytes a stitch depending on bit 2 of its byte 6, plus H'13 of header.
 * H'11A6E8 ends up holding the same pointers, which is the table
 * sew_variant_load reads.
 *
 * With no variant table the count is forced to one and all H'30 slots point
 * at the start of the block.
 */
void stitch_variants_build(void)
{
    u8  i;
    u32 dst, src;
    u16 len;
    u8  count;

    REG8(0x11A7A8UL) = 0;

    if (!(REG8(0x0E0007UL) & 0x80)) {
        REG8(0x11A7AAUL) = 0x01;
        for (i = 0; i < 0x30; i++) {
            REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2)) = 0x000E0000UL;
        }
        return;
    }

    REG8(0x11A6D1UL) = REG8(0x11A6D2UL);

    REG8(0x11A7ABUL) = REG8(0x0E0012UL);
    REG8(0x11A7AAUL) = REG8(0x0E0008UL);
    if (REG8(0x0E0008UL) > 0x30) REG8(0x11A7AAUL) = 0x30;

    if (REG8(0x11A7BCUL) & 0x08) return;

    i   = 0;
    src = REG32(0x0E0014UL);
    dst = 0x000E0000UL + (u32)(u16)(REG8(0x11A7AAUL) << 2) + 0x14UL;

    len = (u16)((u16)((u16)REG8(src + 0x10) << 8) + (u16)REG8(src + 0x11));
    if (REG8(src + 6) & 0x04) len = (u16)((u16)(0x0003 * len) + 0x0013);
    else                      len = (u16)((u16)(len << 1)      + 0x0013);

    mem_copy((u8 *)dst, (const u8 *)src, len);
    REG32(0x0E0014UL) = dst;
    REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2)) = dst;
    i++;

    count = REG8(0x11A7AAUL);
    while (i < count) {
        src = REG32(0x000E0000UL + (u32)(u16)(i << 2) + 0x14UL);
        dst += (u32)len;

        len = (u16)((u16)((u16)REG8(src + 0x10) << 8) + (u16)REG8(src + 0x11));
        if (REG8(src + 6) & 0x04) len = (u16)((u16)(0x0003 * len) + 0x0013);
        else                      len = (u16)((u16)(len << 1)      + 0x0013);

        mem_copy((u8 *)dst, (const u8 *)src, len);
        REG32(0x000E0000UL + (u32)(u16)(i << 2) + 0x14UL) = dst;
        REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2)) = dst;
        i++;
        count = REG8(0x11A7AAUL);
    }
}

/* H'207E4E, H'207EC2. Two three-step state machines that wait for a stepper
 * to report itself idle -- H'FFFED4 and H'FFFED6 are the step states the
 * timer handlers keep -- and record a limit once it has. Until then, and
 * after the third step, the limits read H'FF, which means "no limit yet". */
void home_state_b(void)
{
    u8 st = REG8(0x11A678UL);

    if (st == 0x00) {
        if (REG8(0x114DC6UL) & 0x02) REG8(0x11A678UL) = 0x01;
    } else if (st == 0x01) {
        if (REG8(0xFFFED4UL) == 0x01) {
            REG8(0x11A6B8UL) = (u8)(REG8(0xFFFED8UL) + 0x66);
            REG8(0x11A678UL) = 0x02;
        }
    } else if (st == 0x02) {
        if (REG8(0xFFFED4UL) == 0x01) {
            REG8(0xFFFED5UL) = 0xFF;
            REG8(0x11A678UL) = 0x03;
        }
    } else {
        REG8(0xFFFED5UL) = 0xFF;
    }
}

void home_state_c(void)
{
    u8 st = REG8(0x11A679UL);

    if (st == 0x00) {
        if (REG8(0x114DC6UL) & 0x20) REG8(0x11A679UL) = 0x01;
    } else if (st == 0x01) {
        if (REG8(0xFFFED6UL) == 0x01) {
            REG8(0x11A6B9UL) = 0x19;
            REG8(0x11A6BAUL) = 0x19;
            REG8(0x11A679UL) = 0x02;
        }
    } else if (st == 0x02) {
        if (REG8(0xFFFED6UL) == 0x01) {
            REG8(0x11A6B9UL) = 0xFF;
            REG8(0x11A6BAUL) = 0xFF;
            REG8(0x11A679UL) = 0x03;
        }
    } else {
        REG8(0x11A6B9UL) = 0xFF;
        REG8(0x11A6BAUL) = 0xFF;
    }
}

/* H'207F42. */
void sew_mechanism_service(void)
{
    home_state_b();
    home_state_c();
    REG8(0x11A6B7UL) = 0x1B;
    sew_limit_6BB();
    if (!(REG8(0x114DC6UL) & 0x80)) REG8(0xFFFEC7UL) &= (u8)~0x40;
}

/* H'2076C4, H'2076DC, H'207706 and H'207726 are the four things the panel
 * can be asked to do, and the bits in H'11A6AE say which. These two have no
 * dependants and are written here; the other two need the redraw itself. */
void redraw_partial(void)
{
    REG8(0x11A6AEUL) |=        0x04;
    REG8(0x11A6AEUL) &= (u8)~0x08;
}

void redraw_pattern(void)
{
    REG8(0x11A6AEUL) |=        0x01;
    REG8(0x11A6AEUL) &= (u8)~0x04;
    if (REG8(0x114DCAUL) & 0x02) REG8(0x114DCAUL) |= 0x80;
}

/* H'205AB6. Where in the pattern's stitch list the next stitch is, given a
 * step from the start of the group. H'11A6CC is the bytes a stitch takes --
 * two or three -- and H'11A682 the base; a mirrored pattern counts back from
 * the base instead of forward from it. */
void needle_position_set(u8 step)
{
    u16 off;
    u8 kind = REG8(0x11A7E1UL);

    REG16(0x11A680UL) = (u16)step;

    off = (u16)((u16)REG8(0x11A6CCUL) * (u16)step);
    if (kind == 0x02 || kind == 0x03) {
        REG16(0x11A684UL) = (u16)(REG16(0x11A682UL) - off);
    } else {
        REG16(0x11A684UL) = (u16)(off + REG16(0x11A682UL));
    }

    REG8(0x114DC9UL) |= 0x20;

    if (!((REG8(0x114DC6UL) & 0x10) && !(REG8(0x114DCAUL) & 0x02))) {
        if (!(REG8(0x114DCAUL) & 0x40)) {
            if (!(REG8(0x114DC6UL) & 0x10)) return;
            if (!(REG8(0x114DCAUL) & 0x02)) return;
            if ((u16)(REG16(0x11A6C8UL) + 1) != REG16(0xFFFEFEUL)) return;
        }
    }

    REG8(0x114DC9UL) |= 0x08;
    REG8(0x114DC7UL) |= 0x08;
}

/* H'20258A. A byte that belongs to the pattern as a whole rather than to
 * one variant. Bit 7 of the descriptor's kind says it comes from a table in
 * flash; otherwise the variants are searched and the last one whose byte 7
 * has bit 6 set gives it up. */
u8 pattern_variant_flag(u16 n)
{
    u8 out = 0;
    u8 i;
    u32 p;

    if (REG8(stitch_record(n) + 0x16) & 0x80) {
        return REG8(0x57B6D8UL + (u32)(u16)(n << 2));
    }

    for (i = 1; i < REG8(0x11A7AAUL); i++) {
        p = REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2));
        if (REG8(p + 7) & 0x40) out = REG8(p + 0x0B);
    }
    return out;
}

/* H'203EBC. Steps the variant on by itself as the counter runs. 254
 * instructions, not yet reconstructed; it is reached only when a pattern has
 * more than one variant and bit 0 of H'11A7BC is clear. */

/* Defined below, with the rest of the variant handling. */
void variant_advance(void);

/* One byte out of the current variant's stitch data. */
static u8 stitch_data_byte(u16 idx)
{
    u32 base = REG32(0x11A6E8UL +
                     (u32)(short)(u16)(REG8(0x11A7A8UL) << 2));

    return REG8(base + (u32)idx);
}

/* H'204264. Reads the next four stitches out of the variant's data into
 * H'11A7C8..H'11A7D3, which is where the motor handlers pick them up.
 *
 * Four ways round, because a stitch is two bytes or three depending on bit 2
 * of H'11A7BC, and because a mirrored pattern -- H'11A7E1 of 2 or 3 -- is
 * walked backwards. The index runs from H'11A684 and wraps between stitches:
 * H'11A686 is the far end and H'11A682 the near one. In the three-byte
 * mirrored form the middle two bytes of each stitch come out swapped, which
 * is the mirroring itself rather than an accident of the loop.
 *
 * Ahead of all that, bit 3 of H'114DC7 means "do not advance": a fixed
 * stitch is put in instead, and which one depends on how far a stop
 * sequence has got.
 *
 * The codes H'38 to H'3F in the second byte are not stitches but marks
 * against the free-running counter at H'FFFF00, and the tail turns them
 * into the four limits at H'11A7AE to H'11A7B5 that sew_limit_overshoot
 * measures against.
 */
void stitch_fetch_next(void)
{
    u8  code = 0;
    u16 i = REG16(0x11A684UL);
    u8  c;
    u16 t;

    REG8(0x11A6B0UL) = REG8(0x11A7C8UL);
    REG8(0x11A6B1UL) = REG8(0x11A7C9UL);
    REG8(0x11A6B2UL) = REG8(0x11A7DAUL);
    REG8(0x11A6B3UL) = REG8(0x11A7CAUL);

    if (REG8(0x114DC7UL) & 0x08) {
        u8 n;

        if (REG8(0x11A7BCUL) & 0x80) code = REG8(0x11A6B5UL);
        else                         code = REG8(0x11A6B4UL);

        if (REG8(0x114DCAUL) & 0x10) {
            REG8(0x11A7C8UL) = code;
            REG8(0x11A7C9UL) = 0x33;
            REG8(0x11A7CAUL) = 0x00;
        } else if (REG8(0x114DC9UL) & 0x10) {
            n = REG8(0x11A6AFUL);
            if (n != 0x02 && n <= 0x04) {
                REG8(0x11A7C8UL) = code;
                REG8(0x11A7C9UL) = 0x33;
                REG8(0x11A7CAUL) = 0x00;
            } else if (n == 0x02) {
                REG8(0x11A7C8UL) = code;
                REG8(0x11A7C9UL) = 0xB4;
                REG8(0x11A7CAUL) = 0x00;
            }
        } else if (REG8(0x114DC9UL) & 0x08) {
            n = REG8(0x11A6AFUL);
            if (n != 0x02 && n <= 0x04) {
                REG8(0x11A7C8UL) = REG8(0x11A6B4UL);
                REG8(0x11A7C9UL) = 0x33;
                REG8(0x11A7CAUL) = 0x00;
            } else if (n == 0x02) {
                REG8(0x11A7C8UL) = REG8(0x11A6B4UL);
                REG8(0x11A7C9UL) = 0xB4;
                REG8(0x11A7CAUL) = 0x00;
            }
        }
        REG8(0x114DC7UL) &= (u8)~0x08;

    } else if (!(REG8(0x11A7BCUL) & 0x04)) {
        /* Two bytes a stitch. */
        c = REG8(0x11A7E1UL);
        if (c == 0x02 || c == 0x03) {
            REG8(0x11A7C8UL) = stitch_data_byte(i--);
            REG8(0x11A7C9UL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i--);
            REG8(0x11A7CCUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i--);
            REG8(0x11A7CFUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i--);
            REG8(0x11A7D2UL) = stitch_data_byte(i--);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) - 2);
        } else {
            REG8(0x11A7C8UL) = stitch_data_byte(i++);
            REG8(0x11A7C9UL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i++);
            REG8(0x11A7CCUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i++);
            REG8(0x11A7CFUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i++);
            REG8(0x11A7D2UL) = stitch_data_byte(i++);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) + 2);
        }

    } else {
        /* Three bytes a stitch. */
        c = REG8(0x11A7E1UL);
        if (c == 0x02 || c == 0x03) {
            REG8(0x11A7C8UL) = stitch_data_byte(i--);
            REG8(0x11A7CAUL) = stitch_data_byte(i--);
            REG8(0x11A7C9UL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i--);
            REG8(0x11A7CDUL) = stitch_data_byte(i--);
            REG8(0x11A7CCUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i--);
            REG8(0x11A7D0UL) = stitch_data_byte(i--);
            REG8(0x11A7CFUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i--);
            REG8(0x11A7D3UL) = stitch_data_byte(i--);
            REG8(0x11A7D2UL) = stitch_data_byte(i--);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) + 0xFFFD);
        } else {
            REG8(0x11A7C8UL) = stitch_data_byte(i++);
            REG8(0x11A7C9UL) = stitch_data_byte(i++);
            REG8(0x11A7CAUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i++);
            REG8(0x11A7CCUL) = stitch_data_byte(i++);
            REG8(0x11A7CDUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i++);
            REG8(0x11A7CFUL) = stitch_data_byte(i++);
            REG8(0x11A7D0UL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i++);
            REG8(0x11A7D2UL) = stitch_data_byte(i++);
            REG8(0x11A7D3UL) = stitch_data_byte(i++);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) + 3);
        }
    }

    c = REG8(0x11A7C9UL);
    if (c == 0x3C) {
        t = (u16)(REG16(0xFFFF00UL) + 1);
        REG16(0x11A7AEUL) = t;
        REG16(0x11A7B0UL) = (u16)(t + (u16)REG8(0x11A7ACUL));
    } else if (c == 0x3D) {
        REG16(0x11A7B0UL) = (u16)(REG16(0xFFFF00UL) - 1);
    } else if (c == 0x3E) {
        REG16(0x11A7B2UL) = (u16)(REG16(0xFFFF00UL) + 1);
    } else if (c == 0x3F) {
        REG16(0x11A7B4UL) = (u16)(REG16(0xFFFF00UL) - 1);
    } else if (c == 0x38) {
        t = (u16)(REG16(0xFFFF00UL) + 1);
        REG16(0x11A7B2UL) = t;
        REG16(0x11A7AEUL) = t;
        t = (u16)(t + (u16)REG8(0x11A7ACUL));
        REG16(0x11A7B4UL) = t;
        REG16(0x11A7B0UL) = t;
    } else if (c == 0x39) {
        t = REG16(0xFFFF00UL);
        REG16(0x11A7B4UL) = t;
        REG16(0x11A7B0UL) = t;
    }

    if (REG8(0x11A7AAUL) > 0x01 && !(REG8(0x11A7BCUL) & 0x01)) {
        variant_advance();
    }
}

/* H'205B72. Sets a pattern up to be sewn from the beginning.
 *
 * A stitch is two bytes or three -- H'11A6CC -- and H'11A67E is how many of
 * them there are. From those come the two ends the fetch walks between:
 * H'11A682 where it starts and H'11A686 where it turns round, which are the
 * other way about for a mirrored pattern. H'11A6B4 and H'11A6B5 are the
 * first byte at each end, which is what the hold path puts out when the
 * machine is stopping.
 */
void stitch_begin(void)
{
    u16 count, near, far;
    u32 base;

    REG8(0x11A6B0UL) = REG8(0x11A7C8UL);
    REG8(0x11A6B1UL) = REG8(0x11A7C9UL);
    REG8(0x11A6B2UL) = REG8(0x11A7DAUL);
    REG8(0x11A6B3UL) = REG8(0x11A7CAUL);

    sew_variant_load();

    REG8(0x11A6CCUL) = (REG8(0x11A7BCUL) & 0x04) ? 0x03 : 0x02;

    count = (u16)((u16)((u16)REG8(0x11A7C6UL) << 8) + (u16)REG8(0x11A7C7UL));
    REG16(0x11A67EUL) = count;

    if (REG8(0x11A7E1UL) == 0x02 || REG8(0x11A7E1UL) == 0x03) {
        far  = (u16)((u16)REG8(0x11A6CCUL) + 0x0012);
        near = (u16)((u16)((u16)(count - 1) * (u16)REG8(0x11A6CCUL)) + far);
    } else {
        near = 0x0012;
        far  = (u16)((u16)((u16)(count - 1) * (u16)REG8(0x11A6CCUL)) + 0x0012);
    }
    REG16(0x11A682UL) = near;
    REG16(0x11A686UL) = far;

    base = REG32(0x11A6E8UL + (u32)(short)(u16)(REG8(0x11A7A8UL) << 2));
    REG8(0x11A6B4UL) = REG8(base + (u32)near);
    REG8(0x11A6B5UL) = REG8(base + (u32)REG16(0x11A686UL));

    needle_position_set(0);
    REG16(0x11A680UL) = 0;
    stitch_fetch_next();
}

/* H'206F0A. Back to the first stitch and fetch. */
void stitch_restart(void)
{
    needle_position_set(0);
    stitch_fetch_next();
}

/* ---- stepping through the queue ------------------------------------------
 * H'114DCA bit 3, H'114DCC bits 4 and 5 and H'114DC9 bits 1 and 2 between
 * them say where in the queue the machine is -- first position of a group,
 * last one, or somewhere in between -- and these four set them. */
/* H'2034BE. */
void queue_step_flags_clear(void)
{
    REG8(0x114DCAUL) &= (u8)~0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DCCUL) &= (u8)~0x20;
}

/* H'2034E0. */
void queue_step_flags_last(void)
{
    REG8(0x114DCAUL) |=        0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DCCUL) &= (u8)~0x20;
}

/* H'203502. */
void queue_step_flags_first(void)
{
    u16 next = (u16)(QUEUE_POS + 1);

    if ((queue_entry_ref(next) & 0x03FF) == QUEUE_END ||
        QUEUE_POS == QUEUE_LAST) {
        REG8(0x114DCAUL) |= 0x08;
    } else {
        REG8(0x114DCAUL) &= (u8)~0x08;
    }
    REG8(0x114DCCUL) |=        0x10;
    REG8(0x114DCCUL) &= (u8)~0x20;
    REG8(0x114DC9UL) |=        0x04;
}

/* H'203446, H'20345A. */
void queue_flag_clear_cc4(void)
{
    REG8(0x114DCCUL) &= (u8)~0x10;
}

void queue_flags_group_end(void)
{
    REG8(0x114DCAUL) |=        0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DC9UL) &= (u8)~0x02;
    REG8(0x114DCCUL) |=        0x20;
}

/* H'2017CE. How many positions are left in the group. */
u16 queue_group_count(void)
{
    u16 count = 0;
    u16 i = (u16)(QUEUE_GROUP + 1);

    while (i <= QUEUE_LAST) {
        if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END) break;
        count++;
        i++;
    }
    return count;
}

/* H'20370A. On to the next position, wrapping back to the start of the
 * group at the end of it, and the step flags set to match. */
void queue_step_next(void)
{
    QUEUE_POS = (u16)(QUEUE_POS + 1);

    if ((queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END ||
        (u16)(QUEUE_POS - 1) == QUEUE_LAST) {
        QUEUE_POS = (u16)(QUEUE_GROUP + 1);
        queue_step_flags_first();
    } else if ((queue_entry_ref((u16)(QUEUE_POS + 1)) & 0x03FF) == QUEUE_END ||
               QUEUE_POS == QUEUE_LAST) {
        queue_step_flags_last();
    } else {
        queue_step_flags_clear();
    }

    REG16(0x11A7E6UL) = queue_pattern(QUEUE_POS);
    REG16(0x11A66EUL) = REG16(0x11A7E6UL);
    REG8(0x11A6AEUL) |= 0x01;
    REG8(0x114DCDUL) &= (u8)~0x08;
    REG8(0x114DCAUL) |= 0x80;
}

/* H'203DC2. The same, but only when there is more than one position, and
 * with the needle-stop pin raised on the way if the pattern says so. */
void queue_step_or_stop(void)
{
    if (QUEUE_POS == 0) return;
    if (QUEUE_FIRST == QUEUE_LAST) return;

    if (!(REG8(0xFFFEF8UL) & 0x20) && !(REG8(0x11A7BDUL) & 0x20)) {
        if (!(REG8(0x114DCAUL) & 0x08)) { queue_step_next(); return; }
        if (!(REG8(0x114DC6UL) & 0x40)) { queue_step_next(); return; }
        if (REG8(0x11A688UL) < REG8(0x11A7DFUL)) { queue_step_next(); return; }
    }

    REG8(0xFFFEC7UL) |= 0x40;
    REG8(0x11A688UL) = 0;
    queue_step_next();
}

/* H'203E36. On to the next variant of the same pattern, or back to the
 * first and on to the next position. */
void variant_step(void)
{
    short last = (short)(u16)((u16)REG8(0x11A7AAUL) - 1);

    REG8(0x11A668UL) = 0x01;
    if (REG8(0xFFFEF8UL) & 0x20) REG8(0xFFFEC7UL) |= 0x40;

    if (last <= (short)(u16)REG8(0x11A7A8UL)) {
        REG8(0x11A7A8UL) = 0;
        if (REG8(0x114DCAUL) & 0x02) {
            queue_step_or_stop();
        } else if (REG8(0x114DC6UL) & 0x08) {
            REG8(0x11A7ADUL) = 0x01;
            REG16(0xFFFF00UL) = 0x1000;
            REG16(0x11A698UL) = 0;
        }
    } else {
        REG8(0x11A7A8UL) = (u8)(REG8(0x11A7A8UL) + 1);
    }
}

/* H'20225E. The three parameters a pattern has no working copy for, taken
 * from the default tables in flash. The mark says the machine has to stop
 * and start again before the change takes. */
void pattern_defaults_load(u16 n, u8 mark)
{
    u32 off = (u32)(u16)(n << 2);
    u8 v;

    REG8(0x11A7BFUL) = REG8(0x57B6D6UL + off);

    if (REG8(0x11A7BDUL) & 0x40) v = REG8(0x57B6D8UL + off);
    else                         v = REG8(0x57B6D7UL + off);
    REG8(0x11A7C1UL) = v;

    REG8(0x11A69FUL) = REG8(0x11A7BFUL);
    REG8(0x11A6A1UL) = v;

    REG8(0x11A7C3UL) = (u8)(REG8(0x11A7C3UL) & 0xF0);
    REG8(0x11A7C3UL) = (u8)(REG8(0x11A7C3UL) | REG8(0x57B6D9UL + off));

    if (mark != 0) {
        REG8(0x114DC6UL) |= 0x08;
        REG8(0xFFFEF7UL) |= 0x08;
        REG8(0xFFFEFAUL) &= (u8)~0x80;
    }
}

/* H'20261E. The companion of pattern_variant_flag: the same search, but for
 * the variants whose bit 6 is clear, and a different default table. */
u8 pattern_variant_flag_b(u16 n)
{
    u8 out = 0;
    u8 i;
    u32 p;

    if (REG8(stitch_record(n) + 0x16) & 0x80) {
        return REG8(0x57B6D7UL + (u32)(u16)(n << 2));
    }

    for (i = 1; i < REG8(0x11A7AAUL); i++) {
        p = REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2));
        if (!(REG8(p + 7) & 0x40)) out = REG8(p + 0x0B);
    }
    return out;
}

/* H'206F1A. The next thing to sew: the next variant, the next position, or
 * the same stitch again. H'FFFEC7 bit 6 is the needle-stop request, raised
 * whenever a boundary is crossed that the operator asked to be stopped at. */
static void needle_stop_request(void)
{
    REG8(0xFFFEC7UL) |= 0x40;
    REG8(0x11A688UL) = 0;
    if ((REG8(0x11A7BDUL) & 0x20) && (REG8(0xFFFEC7UL) & 0x02)) {
        REG8(0xFFFEF6UL) &= (u8)~0x20;
        REG8(0x114DCFUL) |= 0x04;
    }
}

void stitch_next_or_variant(void)
{
    if (!(REG8(0x114DC9UL) & 0x80)) {
        stitch_fetch_next();
        return;
    }

    if (REG8(0x11A7AAUL) > 0x01) {
        int stop = 1;

        if (!(REG8(0x11A7BCUL) & 0x10) && !(REG8(0x11A7BDUL) & 0x20)) {
            if (!(REG8(0xFFFEF8UL) & 0x20))     stop = 0;
            else if (REG8(0x114DC6UL) & 0x08)   stop = 0;
        }
        if (stop) needle_stop_request();

        if (REG8(0x11A7BCUL) & 0x01) variant_step();
        else                         stitch_restart();
        return;
    }

    if (REG8(0x114DCAUL) & 0x02) {
        queue_step_or_stop();
        return;
    }

    needle_position_set(0);
    stitch_fetch_next();

    if ((REG8(0x114DC6UL) & 0x40) &&
        REG8(0x11A688UL) >= REG8(0x11A7DFUL)) {
        needle_stop_request();
        return;
    }
    if (!(REG8(0x11A7BCUL) & 0x10) && !(REG8(0x11A7BDUL) & 0x20) &&
        !(REG8(0xFFFEF8UL) & 0x20)) {
        return;
    }
    needle_stop_request();
}

/* H'20704A. One stitch done. Counts the position on, notices the end of the
 * pattern in two places -- one stitch before it and at it -- and either
 * fetches the same stitch again or asks for the next thing. */
void stitch_advance(void)
{
    u16 pos = (u16)(REG16(0x11A680UL) + 1);
    u8 v, f;

    REG16(0x11A680UL) = pos;

    if (REG8(0x11A7D4UL) & 0x80) {
        if (REG16(0x11A67EUL) >= 0x0002) {
            if ((u16)(pos - 1) == (u16)(REG16(0x11A67EUL) >> 1)) {
                REG8(0xFFFEC7UL) |= 0x40;
            }
        }
    }

    v = REG8(0x114DC9UL);
    if (v & 0x20) {
        REG8(0x114DC9UL) = (u8)(v & ~0x20);
        if (REG8(0x11A7A8UL) == 0 && (REG8(0x114DC6UL) & 0x40)) {
            f = REG8(0x114DCAUL);
            if (f & 0x02) {
                if (f & 0x08) REG8(0x11A688UL) = (u8)(REG8(0x11A688UL) + 1);
            } else {
                REG8(0x11A688UL) = (u8)(REG8(0x11A688UL) + 1);
            }
        }
    }

    if ((u16)(REG16(0x11A67EUL) - 1) == REG16(0x11A680UL)) {
        REG8(0x114DC9UL) |= 0x40;
        if (REG8(0x11A7BCUL) & 0x80) {
            f = REG8(0x114DCAUL);
            if (f & 0x02) {
                if ((f & 0x08) && (REG8(0x114DC6UL) & 0x10)) {
                    REG8(0x114DC9UL) |= 0x10;
                }
            } else {
                if (REG8(0x114DC6UL) & 0x10) REG8(0x114DC9UL) |= 0x10;
            }
        }
    } else {
        REG8(0x114DC9UL) &= (u8)~0x40;
    }

    if (REG16(0x11A680UL) == REG16(0x11A67EUL)) {
        REG8(0x114DC9UL) |= 0x80;
        if (!(REG8(0x11A7BCUL) & 0x80)) {
            f = REG8(0x114DCAUL);
            if (f & 0x02) {
                if (f & 0x08) {
                    if (REG8(0x114DC6UL) & 0x10) REG8(0x114DC9UL) |= 0x10;
                    else                         REG8(0x114DCAUL) |= 0x10;
                } else {
                    REG8(0x114DCAUL) |= 0x10;
                }
            } else {
                if (REG8(0x114DC6UL) & 0x10) REG8(0x114DC9UL) |= 0x10;
            }
        }
    } else {
        REG8(0x114DC9UL) &= (u8)~0x80;
    }

    if ((REG8(0x114DCAUL) & 0x10) || (REG8(0x114DC9UL) & 0x10)) {
        REG8(0x114DC7UL) |= 0x08;
        stitch_fetch_next();
        REG8(0xFFFEC1UL) &= (u8)~0x20;
    } else {
        stitch_next_or_variant();
        sew_lamp_service();
    }
}

/* H'2071F8, H'207236. Two stop sequences, counted in H'11A6AF. */
void stop_step_a(void)
{
    u8 n;

    REG8(0xFFFEC1UL) &= (u8)~0x20;
    n = (u8)(REG8(0x11A6AFUL) + 1);
    REG8(0x11A6AFUL) = n;

    if (n >= 0x03) {
        sew_clear_busy_3();
        stitch_fetch_next();
    } else {
        REG8(0x114DC7UL) |= 0x08;
        stitch_fetch_next();
    }
}

/* H'2072A2. */
void redraw_after_edit(void)
{
    REG8(0x114DCAUL) &= (u8)~0x10;
    stitch_next_or_variant();
}

void stop_step_b(void)
{
    u8 n;

    REG8(0xFFFEC1UL) &= (u8)~0x20;
    n = (u8)(REG8(0x11A6AFUL) + 1);
    REG8(0x11A6AFUL) = n;

    if (n >= 0x04) {
        sew_clear_busy_4();
        if (REG8(0x11A7BCUL) & 0x80) {
            stitch_advance();
        } else {
            stitch_next_or_variant();
            sew_lamp_service();
        }
        return;
    }

    if (n == 0x03 && (REG8(0x11A7BCUL) & 0x80)) {
        REG8(0x114DC7UL) &= (u8)~0x08;
    } else if (n != 0x03) {
        REG8(0x114DC7UL) |= 0x08;
    }
    stitch_fetch_next();
}

/* H'2072BA. One stitch's worth of time gone by. H'11A7DE is how many
 * repeats the operator asked for; H'11A689 counts them. */
void stitch_tick(void)
{
    u8 want, done;

    REG16(0x11A698UL) = (u16)(REG16(0x11A698UL) + 1);

    want = REG8(0x11A7DEUL);
    if (want == 0) {
        stitch_advance();
        return;
    }

    REG8(0x11A689UL) = (u8)(REG8(0x11A689UL) + 1);
    done = REG8(0x11A689UL);

    if (REG8(0x114DCDUL) & 0x10) {
        if ((short)(u16)((u16)want << 1) <= (short)(u16)done) {
            REG8(0x11A689UL) = 0;
            stitch_advance();
        } else if (done == 0x01) {
            stitch_advance();
        }
    } else {
        if (done >= want) {
            REG8(0x11A689UL) = 0;
            stitch_advance();
        }
    }
}

/* H'2026B2. The live parameters taken from the pattern's working copy --
 * what the operator has set -- rather than from the catalogue. Bit 7 of the
 * descriptor's kind is copied into the working copy's kind on the way, so
 * the two agree about where the pattern came from.
 *
 * Which fields are read depends on the kind. Kinds 1 and H'81 keep one set;
 * 2 and H'82 keep the pair that bit 6 of H'11A7BD selects between; anything
 * else has no working copy worth reading and the catalogue's defaults are
 * used, with the variant flag written back into the working copy so the
 * next pass has it.
 */
void sew_params_from_working(u16 n)
{
    u32 w = stitch_work(n);
    u8  kind;

    REG8(w) = (u8)(REG8(w) & ~0x80);
    REG8(w) = (u8)(REG8(w) | (u8)(REG8(stitch_record(n) + 0x16) & 0x80));

    kind = REG8(w);

    if (kind == 0x01 || kind == 0x81) {
        REG8(0x11A69EUL) = REG8(w + 1);
        sew_param_a_set(REG8(w + 1));
        if (REG8(0x11A7BDUL) & 0x40) {
            REG8(0x11A6A0UL) = REG8(0x11A7C1UL);
            REG8(0xFFFEFCUL) = 0x00;
        } else {
            REG8(0x11A6A0UL) = REG8(w + 2);
            REG8(0xFFFEFCUL) = REG8(w + 7);
        }
        sew_param_b_set(REG8(0x11A6A0UL));
        REG8(0xFFFEEAUL) = REG8(w + 4);
        REG8(0xFFFEECUL) = REG8(w + 5);
        REG8(0xFFFEFBUL) = REG8(w + 6);
        REG8(0x11A7ACUL) = REG8(w + 9);

    } else if (kind == 0x02 || kind == 0x82) {
        REG8(0x11A69EUL) = REG8(w + 1);
        sew_param_a_set(REG8(w + 1));
        if (REG8(0x11A7BDUL) & 0x40) {
            REG8(0x11A6A0UL) = REG8(w + 3);
            REG8(0xFFFEFCUL) = REG8(w + 8);
        } else {
            REG8(0x11A6A0UL) = REG8(w + 2);
            REG8(0xFFFEFCUL) = REG8(w + 7);
        }
        sew_param_b_set(REG8(0x11A6A0UL));
        REG8(0xFFFEEAUL) = REG8(w + 4);
        REG8(0xFFFEECUL) = REG8(w + 5);
        REG8(0xFFFEFBUL) = REG8(w + 6);
        REG8(0x11A7ACUL) = REG8(w + 9);

    } else {
        REG8(0x11A69EUL) = REG8(0x11A7BFUL);
        sew_param_a_set(REG8(0x11A7BFUL));
        if (REG8(0x11A7BDUL) & 0x40) REG8(w + 2) = pattern_variant_flag_b(n);
        else                         REG8(w + 3) = pattern_variant_flag(n);
        REG8(0x11A6A0UL) = REG8(0x11A7C1UL);
        sew_param_b_set(REG8(0x11A7C1UL));
        REG8(0xFFFEEAUL) = (u8)(REG8(0x11A7C3UL) & 0x0F);
        REG8(0xFFFEECUL) = REG8(0x11A7C5UL);
        REG8(0xFFFEFBUL) = 0x00;
        REG8(0xFFFEFCUL) = 0x00;
    }

    REG8(0x11A7D9UL) = REG8(0x11A69EUL);
    REG8(0x11A7DAUL) = REG8(0x11A6A0UL);
    REG8(0x11A7D8UL) = REG8(0xFFFEEAUL);
    REG8(0x11A7DBUL) = REG8(0xFFFEECUL);
    REG8(0x11A7DCUL) = REG8(0xFFFEFBUL);
    REG8(0x11A7DDUL) = REG8(0xFFFEFCUL);
}

/* H'2029FA. */
void sew_params_for_pattern(u16 n)
{
    if (REG8(stitch_record(n) + 0x16) & 0x80) pattern_defaults_load(n, 0);
    if (n != 0) sew_params_from_working(n);
}

/* H'206B00. Makes the pattern at H'11A7E6 the one the machine is sewing.
 *
 * The first half only runs when H'114DCD bit 3 is clear, and it is the
 * expensive part: the pattern's data is streamed in, its variants unpacked,
 * the four counter limits reset to H'1000 and the stitch time worked out
 * from two bytes on the bus.
 *
 * The second half decides where the parameters come from. With no queue,
 * from the pattern's working copy. With a queue and a fresh position, from
 * the queue entry -- unless bit 0 of H'114DC9 says the entry is not to be
 * believed. And the count of what is left in the group decides whether this
 * position is the last one.
 */
void pattern_make_current(void)
{
    u8 v;

    REG8(0x11A668UL) = 0;
    REG8(0x11A669UL) = 0;

    if (!(REG8(0x114DCDUL) & 0x08)) {
        if (REG16(0x11A7E6UL) <= 0x0001) {
            if (REG16(0x11A7E6UL) == 0) REG16(0x11A7E6UL) = 0x0001;
            REG8(0x11A7BCUL) = REG8(0x0E0006UL);
            REG8(0x11A7BDUL) = REG8(0x0E0007UL);
            if (!(REG8(0x11A7BCUL) & 0x08)) {
                if (!(REG8(0x11A6AEUL) & 0x02)) stitch_chunk_first();
            } else {
                REG8(0xFFFEF5UL) |= 0x04;
                REG8(0x11A7D4UL) |= 0x04;
                REG8(0x0E4020UL) = (u8)(REG8(0x0E4020UL) & 0x80);
            }
        } else {
            if (!(REG8(0x11A6AEUL) & 0x02)) stitch_chunk_first();
        }

        stitch_variants_build();

        REG8(0xFFFEFAUL) &= (u8)~0x10;
        REG8(0xFFFEFAUL) = (u8)(REG8(0xFFFEFAUL) & 0x98);
        if (REG8(0x11A7AAUL) <= 0x05) REG8(0xFFFEFAUL) |= 0x20;
        if (REG8(0x11A7AAUL) >= 0x06) {
            REG8(0xFFFEFAUL) = (u8)(REG8(0xFFFEFAUL) | 0x60);
        }

        v = (u8)(bus_byte_10() + stitch_length_limit());
        REG8(0x11A7ACUL) =
            (u8)((short)((short)(u16)((u16)((u16)0x64 * (u16)v) + 7)
                         / (short)15));

        REG8(0x11A7ADUL) = 0x01;
        REG16(0x11A7B4UL) = 0x1000;
        REG16(0x11A7B2UL) = 0x1000;
        REG16(0x11A7B0UL) = 0x1000;
        REG16(0x11A7AEUL) = 0x1000;
        REG16(0xFFFF00UL) = 0x1000;
        REG16(0x11A698UL) = 0x0000;

        if (!(REG8(0x11A6AEUL) & 0x02)) {
            REG8(0x114DC6UL) &= (u8)~0x08;
            REG8(0xFFFEFAUL) &= (u8)~0x80;
            REG8(0xFFFEF7UL) &= (u8)~0x08;
            REG8(0x114DCEUL) &= (u8)~0x40;
        }
    }

    REG8(0x11A6CFUL) = 0;
    REG8(0x11A689UL) = 0;
    REG8(0x114DC7UL) &= (u8)~0x80;
    REG8(0x114DCFUL) &= (u8)~0x08;
    REG8(0x11A6AEUL) &= (u8)~0x02;
    REG8(0x114DCBUL) &= (u8)~0x20;
    REG8(0x114DCAUL) &= (u8)~0x10;

    if (STITCH_SET == 0xB4) REG8(0xFFFEF8UL) |= 0x10;
    REG8(0xFFFEF8UL) &= (u8)~0x20;

    if (!(REG8(0x114DCAUL) & 0x02)) {
        sew_clear_busy_3();
        sew_clear_busy_4();
        REG8(0x11A688UL) = 0;
    }

    if (((REG8(0x114DC6UL) & 0x10) && !(REG8(0x114DCAUL) & 0x02)) ||
        ((REG8(0x114DC6UL) & 0x10) && (REG8(0x114DCAUL) & 0x02) &&
         (u16)(QUEUE_GROUP + 1) == QUEUE_POS)) {
        REG8(0x114DC9UL) |= 0x08;
        REG8(0x114DC7UL) |= 0x08;
    }

    if (!(REG8(0x114DCAUL) & 0x02)) {
        stitch_begin();
        sew_params_for_pattern(REG16(0x11A7E6UL));
        sew_params_publish();
    } else if (!(REG8(0x114DCAUL) & 0x80) || (REG8(0x114DCDUL) & 0x08)) {
        if (REG8(0x114DCDUL) & 0x08) {
            stitch_begin();
        } else {
            stitch_begin();
            sew_params_for_pattern(REG16(0x11A7E6UL));
            sew_params_publish();
        }
    } else if (QUEUE_POS != 0) {
        REG8(0x114DCAUL) &= (u8)~0x80;
        stitch_begin();
        if (REG8(0x114DC9UL) & 0x01) {
            REG8(0x114DC9UL) &= (u8)~0x01;
            sew_params_for_pattern(REG16(0x11A7E6UL));
            sew_params_publish();
        } else {
            queue_entry_unpack();
        }
        if (REG8(0x114DC7UL) & 0x01) sew_params_from_pattern();
        if (queue_group_count() <= 0x0001) queue_flags_group_end();
        else                               queue_flag_clear_cc4();
    }

    REG8(0x114DCDUL) &= (u8)~0x08;

    v = (u8)((short)((short)(u16)((u16)0xDC *
                                  (u16)(u8)((REG8(0x11A7C3UL) & 0xF0) >> 4))
                     / (short)15));
    REG8(0x11A6CEUL) = v;
    if ((REG8(0x11A7BCUL) & 0x04) && v > 0x96) REG8(0x11A6CEUL) = 0x96;

    REG8(0x114DCFUL) &= (u8)~0x40;
    REG8(0x114DCFUL) &= (u8)~0x80;
    if (REG8(0x11A7BCUL) & 0x04) {
        if (!(REG8(0x11A7BDUL) & 0x10)) {
            if (!(REG8(0x11A7BEUL) & 0x20)) REG8(0x114DCFUL) |= 0x40;
            if (!(REG8(0x11A7BEUL) & 0x40)) REG8(0x114DCFUL) |= 0x80;
        }
    }

    sew_lamp_service();

    REG8(0x11A7A9UL) = REG8(0x11A7A8UL);
    REG16(0x11A6C6UL) = QUEUE_POS;
}

/* H'207342. Has the operator changed what is selected?
 *
 * Three cases. H'FFFEFE of H'FFFF means there is no queue at all and the
 * pattern comes from H'FFFEE0 plus H'FFFEFD, the number keyed in. A queue
 * that has just appeared has to be taken up: its bounds noted, the group
 * start found, and the first pattern loaded. And a queue already running
 * only needs looking at when the position, the variant or the pattern has
 * moved -- H'114DC2 is the flag that says a step was taken deliberately
 * rather than by the sewing running on.
 */
void panel_selection_check(void)
{
    u16 t;
    u8  v;

    if (QUEUE_POS == 0xFFFF) {
        v = REG8(0x114DCAUL);
        if (v & 0x02) {
            v = (u8)(v & ~0x02);
            REG8(0x114DC9UL) &= (u8)~0x04;
            REG8(0x114DC9UL) &= (u8)~0x01;
            REG8(0x114DC9UL) &= (u8)~0x02;
            v = (u8)(v & ~0x80);
            REG8(0x114DCAUL) = v;
            REG16(0x11A6C6UL) = 0;
            t = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
            REG16(0x11A7E6UL) = t;
            REG16(0x11A6C4UL) = t;
            REG8(0x11A6AEUL) |= 0x01;
            REG8(0x114DCDUL) &= (u8)~0x08;
            return;
        }

        t = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
        if (t == REG16(0x11A6C4UL) &&
            REG8(0x11A7A8UL) == REG8(0x11A7A9UL)) return;

        t = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
        REG16(0x11A7E6UL) = t;
        REG16(0x11A6C4UL) = t;
        if (REG8(0x11A7A8UL) != REG8(0x11A7A9UL)) {
            REG8(0x114DCDUL) |= 0x08;
            REG8(0x11A6AEUL) |= 0x01;
        } else {
            REG8(0x11A6AEUL) |= 0x01;
            REG8(0x114DCDUL) &= (u8)~0x08;
        }
        return;
    }

    if (REG16(0x114DC4UL) != QUEUE_POS) {
        if (REG16(0x114DC4UL) == QUEUE_GROUP &&
            (u16)(QUEUE_GROUP + 1) == QUEUE_POS) {
            REG8(0x114DC2UL) = 0x01;
        }
        REG16(0x114DC4UL) = QUEUE_POS;
    }

    if (!(REG8(0x114DCAUL) & 0x02)) {
        REG8(0x114DCAUL) |= 0x02;
        REG8(0x114DC9UL) |= 0x04;
        REG16(0x11A66CUL) = queue_entry_ref(0);
        REG16(0x11A6CAUL) = QUEUE_LAST;

        if (QUEUE_POS == QUEUE_FIRST) {
            QUEUE_POS = (u16)(QUEUE_FIRST + 1);
            QUEUE_GROUP = QUEUE_FIRST;
        } else {
            queue_group_start();
        }

        if ((queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END) return;
        if (QUEUE_FIRST == QUEUE_LAST) return;

        REG16(0x11A7E6UL) = queue_pattern(QUEUE_POS);
        REG16(0x11A66EUL) = REG16(0x11A7E6UL);
        REG8(0x11A6AEUL) |= 0x01;
        REG8(0x114DCDUL) &= (u8)~0x08;
        REG8(0x114DCAUL) |= 0x80;
        return;
    }

    if (REG8(0x11A7A8UL) != REG8(0x11A7A9UL)) {
        REG8(0x114DCDUL) |= 0x08;
        REG8(0x11A6AEUL) |= 0x01;
        REG8(0x114DCAUL) &= (u8)~0x80;
        return;
    }

    if (!(QUEUE_POS == REG16(0x11A6C6UL) &&
          queue_pattern(QUEUE_POS) == REG16(0x11A66EUL) &&
          REG8(0x114DC2UL) != 0x01)) {
        if ((queue_entry_ref(QUEUE_POS) & 0x03FF) != QUEUE_END &&
            QUEUE_FIRST != QUEUE_LAST &&
            QUEUE_POS != QUEUE_GROUP) {
            t = queue_pattern(QUEUE_POS);
            REG16(0x11A7E6UL) = t;
            REG16(0x11A66EUL) = t;
            REG16(0x11A6C4UL) = t;
            REG8(0x11A6AEUL) |= 0x01;
            REG8(0x114DCDUL) &= (u8)~0x08;
            REG8(0x114DCAUL) |= 0x80;
            REG8(0x114DC9UL) |= 0x04;
            REG8(0x114DC2UL) = 0x00;
        }
    }

    if (QUEUE_LAST != REG16(0x11A6CAUL) ||
        (queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END) {
        REG8(0x114DC9UL) |= 0x02;
        REG16(0x11A6CAUL) = QUEUE_LAST;
    }
}

/* H'207706, H'207726. The other two of the four things the panel can be
 * asked for: a full rebuild, and one stitch's worth of running. */
void redraw_full(void)
{
    REG8(0x11A6AEUL) &= (u8)~0x01;
    REG8(0x11A6AEUL) &= (u8)~0x04;
    REG8(0x11A6AEUL) |=        0x08;
    pattern_make_current();
}

void redraw_running(void)
{
    u8 v;

    REG8(0x11A6AEUL) |= 0x04;
    REG8(0x11A6AEUL) |= 0x08;

    v = REG8(0x11A6CFUL);
    if (v != 0) {
        v = (u8)(v - 1);
        REG8(0x11A6CFUL) = v;
        if (v <= 0x03) REG8(0x114DC7UL) |= 0x80;
    }

    if (REG8(0x114DCAUL) & 0x10)      redraw_after_edit();
    else if (REG8(0x114DC9UL) & 0x10) stop_step_b();
    else if (REG8(0x114DC9UL) & 0x08) stop_step_a();
    else                              stitch_tick();
}

/* H'20778A, H'20785E. The panel's own little state machine, at H'11A6AD.
 * A pass picks one of the four things to do from the bits in H'11A6AE, does
 * it, and parks at H'63 so the next pass starts again. The two differ in
 * which bits take precedence -- H'20785E is the one used while the machine
 * is running, and it puts the stitch work first. */
void panel_service_idle(void)
{
    u8 what, bits;

    if (REG8(0x11A6ADUL) == 0) {
        panel_selection_check();

        bits = REG8(0x11A6AEUL);
        if (!(bits & 0x80) && !(bits & 0x01)) REG8(0x11A6ADUL) = 0x01;

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && !(bits & 0x08) && (bits & 0x04) && !(bits & 0x01)) {
            REG8(0x11A6ADUL) = 0x04;
        }

        bits = REG8(0x11A6AEUL);
        if (bits & 0x01) REG8(0x11A6ADUL) = 0x03;

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && (bits & 0x02)) REG8(0x11A6ADUL) = 0x02;
    }

    what = REG8(0x11A6ADUL);
    if (what == 0x01) {
        redraw_partial();
        if (REG8(0x11A6AEUL) & 0x02) { what = 0x02; REG8(0x11A6ADUL) = 0x02; }
        else                         { what = 0x63; REG8(0x11A6ADUL) = 0x63; }
    }
    if (what == 0x02) {
        redraw_pattern();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x03) {
        redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x04) {
        redraw_running();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x63) REG8(0x11A6ADUL) = 0x00;
}

void panel_service_running(void)
{
    u8 what, bits;

    if (REG8(0x11A6ADUL) == 0) {
        panel_selection_check();

        if (!(REG8(0x11A6AEUL) & 0x80)) REG8(0x11A6ADUL) = 0x01;

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && !(bits & 0x08) && (bits & 0x04) && !(bits & 0x01)) {
            REG8(0x11A6ADUL) = 0x04;
        }

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && !(bits & 0x01) && (bits & 0x02)) {
            REG8(0x11A6ADUL) = 0x02;
        }

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && (bits & 0x01)) REG8(0x11A6ADUL) = 0x03;
    }

    what = REG8(0x11A6ADUL);
    if (what == 0x01) {
        redraw_partial();
        bits = REG8(0x11A6AEUL);
        if (!(bits & 0x01) && (bits & 0x02)) {
            what = 0x02; REG8(0x11A6ADUL) = 0x02;
        } else {
            what = 0x63; REG8(0x11A6ADUL) = 0x63;
        }
    }
    if (what == 0x02) {
        redraw_pattern();
        if (REG8(0x11A6AEUL) & 0x80) redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x03) {
        redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x04) {
        redraw_running();
        if (REG8(0x11A6AEUL) & 0x01) redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x63) REG8(0x11A6ADUL) = 0x00;
}

/* H'207956. One pass of the panel, with the chunk loader kept fed: if the
 * pattern's data has not all arrived yet, another chunk comes in first. */
void panel_service(void)
{
    if (REG8(0x11A6D1UL) < REG8(0x11A6D2UL)) stitch_chunk_load();

    if (REG8(0x114DC6UL) & 0x80) panel_service_running();
    else                         panel_service_idle();
}

/* ---- stopping in the right place ----------------------------------------
 * The needle is not stopped the moment it is asked to be: it has to finish
 * the stitch, and on some patterns the half-way point counts as a place to
 * stop as well as the end. H'11A6CF is a countdown of stitches still to go
 * before the stop takes.
 */

/* H'203A10. How many stitches that is. H'11A7DE is the repeat count, three
 * stitches each and one over; four when there is no repeat. Four more when
 * the machine is in the mode at H'114DC6 bit 4 and the stop was asked for at
 * the end rather than the half-way point.
 *
 * The two comparisons the original makes after that have empty bodies and
 * are left out. */
void stop_countdown_set(u8 at_half)
{
    u8 v = REG8(0x11A7DEUL);

    if (v == 0) REG8(0x11A6CFUL) = 0x04;
    else        REG8(0x11A6CFUL) = (u8)((u8)((u16)v * (u16)3) + 1);

    if (REG8(0x114DC6UL) & 0x10) {
        if (at_half == 0) {
            REG8(0x11A6CFUL) = (u8)(REG8(0x11A6CFUL) + 4);
        }
    }
}

/* H'203A6E. Whether this stitch is one the machine may stop on.
 *
 * A stop that has come due clears itself and lets the presser foot back
 * down. Then, if the position has reached the last few stitches of the
 * pattern -- or of its first half, when H'11A7D4 bit 7 says the half-way
 * point counts -- the countdown is armed. What arms it differs between a
 * queue and a single pattern, and a pattern with more than one variant only
 * stops between variants when it is marked to.
 */
void needle_stop_service(void)
{
    u16 limit = 0;
    u8  at_half;
    u8  v;

    if (!(REG8(0x114DC6UL) & 0x80)) {
        v = REG8(0xFFFEC7UL);
        if (v & 0x40) {
            REG8(0xFFFEC7UL) = (u8)(v & ~0x40);
            REG8(0xFFFEF8UL) &= (u8)~0x20;
            v = REG8(0x114DCFUL);
            if (v & 0x04) {
                REG8(0x114DCFUL) = (u8)(v & ~0x04);
                REG8(0xFFFEF6UL) |= 0x20;
            }
            REG8(0x114DC7UL) &= (u8)~0x80;
            REG8(0x11A6CFUL) = 0;
        }
    }

    if ((REG8(0x11A7D4UL) & 0x80) &&
        (u16)(REG16(0x11A67EUL) >> 1) > REG16(0x11A680UL)) {
        at_half = 1;
        if ((u16)(REG16(0x11A67EUL) >> 1) > 0x0003) {
            limit = (u16)((u16)(REG16(0x11A67EUL) >> 1) + 0xFFFD);
        }
    } else {
        at_half = 0;
        if (REG16(0x11A67EUL) > 0x0003) {
            limit = (u16)(REG16(0x11A67EUL) + 0xFFFD);
        }
    }

    if (REG16(0x11A680UL) < limit) return;
    if (REG8(0x11A6CFUL) != 0) return;

    if (!(REG8(0x114DCAUL) & 0x02)) {
        if (at_half != 0) {
            stop_countdown_set(at_half);
            return;
        }

        v = 1;
        if (!(REG8(0x11A7BCUL) & 0x10) && !(REG8(0x11A7BDUL) & 0x20)) {
            if (!(REG8(0xFFFEF8UL) & 0x20))    v = 0;
            else if (REG8(0x114DC6UL) & 0x08)  v = 0;
        }
        if (v && !(REG8(0x114DC6UL) & 0x40)) stop_countdown_set(at_half);

        if (REG8(0x114DC6UL) & 0x40) {
            if (REG8(0x11A688UL) >= REG8(0x11A7DFUL)) {
                if (REG8(0x11A7AAUL) > 0x01) {
                    if (REG8(0x11A7BCUL) & 0x10) stop_countdown_set(at_half);
                } else {
                    stop_countdown_set(at_half);
                }
            }
        }
        return;
    }

    if (at_half != 0) {
        stop_countdown_set(at_half);
        return;
    }

    v = 0;
    if (!(REG8(0xFFFEF8UL) & 0x20) && !(REG8(0x11A7BDUL) & 0x20)) {
        if (!(REG8(0x114DCAUL) & 0x08))         v = 1;
        else if (!(REG8(0x114DC6UL) & 0x40))    v = 1;
        else if (REG8(0x11A688UL) < REG8(0x11A7DFUL)) v = 1;
    }
    if (v) return;

    if (REG8(0x11A7AAUL) > 0x01) {
        if (REG8(0x11A7BCUL) & 0x10) stop_countdown_set(at_half);
    } else {
        stop_countdown_set(at_half);
    }
}

/* H'201506, H'20150E and H'201516. Three of the machine's live settings,
 * read back one byte each. The routines that *set* them are H'20153E,
 * H'20154A and H'201536 next door; these are the readers, and they are
 * written out twice in the ROM -- H'20151E, H'201526 and H'20152E are the
 * same three again. */
u8 sew_param_a_get(void)
{
    return REG8(0x0011A69EUL);
}

u8 sew_param_b_get(void)
{
    return REG8(0x0011A6A0UL);
}

u8 stitch_width_get(void)
{
    return REG8(0x00FFFEEAUL);
}

/* H'2015EE, H'201628 and H'201662. The three settings taken from the row of
 * the table at H'11A6E8 that H'11A7A8 picks, which is how a pattern brings
 * its own defaults with it. The first two go through the knob setters as
 * well as the byte; the third is masked to four bits. */
static u32 sew_row(void)
{
    return REG32(0x0011A6E8UL +
        (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A7A8UL) << 2)));
}

void sew_param_a_load(void)
{
    const u8 v = REG8(sew_row() + 0x09);

    REG8(0x0011A69EUL) = v;
    (void)sew_param_a_set(v);
}

void sew_param_b_load(void)
{
    const u8 v = REG8(sew_row() + 0x0B);

    REG8(0x0011A6A0UL) = v;
    (void)sew_param_b_set(v);
}

void stitch_width_load(void)
{
    REG8(0x00FFFEEAUL) = (u8)(REG8(sew_row() + 0x0D) & 0x0F);
}

/* H'20151E, H'201526 and H'20152E. The same three readers again -- the ROM
 * has two copies of each -- and H'201536, H'20153E and H'20154A, the writers
 * that pair with them. The first two writers put the value through the knob
 * setters as well as the byte. */
u8 stitch_width_get2(void)
{
    return REG8(0x00FFFEEAUL);
}

u8 sew_param_a_get2(void)
{
    return REG8(0x0011A69EUL);
}

u8 sew_param_b_get2(void)
{
    return REG8(0x0011A6A0UL);
}

void stitch_width_put(u8 v)
{
    REG8(0x00FFFEEAUL) = v;
}

void sew_param_a_put(u8 v)
{
    REG8(0x0011A69EUL) = v;
    (void)sew_param_a_set(v);
}

void sew_param_b_put(u8 v)
{
    REG8(0x0011A6A0UL) = v;
    (void)sew_param_b_set(v);
}

/* H'2012EC. One pattern's working copy written back into flash.
 *
 * The catalogue's own byte at H'16 gets bit 7 raised -- "this one has been
 * changed" -- in the copy at H'0E4010 and in flash, and the four parameter
 * bytes beside it go from the copy into the table at H'57B6D6. Then the
 * three defaults are read back, which is what makes the change take.
 */
void pattern_settings_write(u16 n)
{
    const u32 entry = ITEM_TABLE + (u32)((u16)(n * ITEM_STRIDE));
    const u32 copy  = 0x000E4010UL + (u32)((u16)(n << 4));
    u8 mark = REG8(entry + 0x16);
    u8 k;

    FLASH_BUSY |= 0x20;

    REG8(copy) = (u8)(REG8(copy) | 0x80);
    mark = (u8)(mark | 0x80);
    rom_flash_write(&mark, entry + 0x16, 1);

    for (k = 0; k < 4; k++) {
        rom_flash_write((const void *)(copy + 1 + k),
                        0x0057B6D6UL + (u32)((u16)(n << 2)) + k, 1);
    }

    FLASH_BUSY &= (u8)~0x20;
    pattern_defaults_load(n, 0x00);
}

/* H'20145E. The pattern the machine is on written back -- and with it every
 * unlisted entry that belongs to it.
 *
 * The run is found by walking forward past the category-1 entries and then
 * back again counting them; a category of H'10 means the whole run is one
 * pattern and all of it is written, and anything else means only the one
 * the machine is actually on.
 */
void pattern_settings_store(void)
{
    u16 at = (u16)(REG16(0x00FFFEE0UL) + (u16)REG8(0x00FFFEFDUL));
    const u16 first = at;
    u8 run = 0;
    u8 cat;

    cat = REG8(ITEM_TABLE + (u32)((u16)(at * ITEM_STRIDE)) + ITEM_CATEGORY);

    do {
        at = (u16)(at + 1);
        cat = REG8(ITEM_TABLE + (u32)((u16)(at * ITEM_STRIDE)) + ITEM_CATEGORY);
    } while (cat == 0x01);

    do {
        run = (u8)(run + 1);
        at = (u16)(at - 1);
        cat = REG8(ITEM_TABLE + (u32)((u16)(at * ITEM_STRIDE)) + ITEM_CATEGORY);
    } while (cat == 0x01);

    if (cat == 0x10) {
        u8 k;

        for (k = 0; k < run; k++) {
            pattern_settings_write(at);
            at = (u16)(at + 1);
        }
        return;
    }

    pattern_settings_write(first);
}
