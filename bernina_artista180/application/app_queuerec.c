/* The artista 180 application, rebuilt in C: the queue's own list and
 * records, its ranges, and what a press does.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- the queue's own list ----------------------------------------------
 * H'210784, H'21DD6A and the two little stores below them.
 */

/* H'210784. How many entries at [from] and after it are category 1 -- the
 * ones build_item_list leaves out -- plus one. The end of the run is
 * recomputed on every pass because H'11B11C and H'11B28C can move while the
 * queue is being edited. */
u16 queue_run_length(u16 from)
{
    u16 n = 0x0001;
    short at = (short)from;

    for (;;) {
        const short end = (short)(REG16(0x0011B11CUL) + REG16(0x0011B28CUL));

        at++;
        if (at > end) break;
        if (REG8(ITEM_TABLE +
                 (u32)(long)(short)(u16)(ITEM_STRIDE * (u16)at) + 0x17) != 0x01) {
            break;
        }
        n++;
    }
    return n;
}

/* H'21DD6A. One value inserted into a counted list of words: the tail is
 * shifted up by one and the count goes up with it. The length handed to the
 * move is worked out from the count *before* the insert, so the word being
 * overwritten is included in what is moved. */
void list_insert(u16 *list, u16 at, u16 value)
{
    const u32 base = (u32)list;
    const u32 n = (u32)(u16)((u16)(REG16(base) - at + 1) << 1);
    const u32 slot = base + (u32)(long)(short)(u16)((u16)(at << 1));

    mem_move((void *)(slot + 2), (const void *)slot, n);
    REG16(slot) = value;
    REG16(base) = (u16)(REG16(base) + 1);
}

/* H'229054. One byte into the H'0D-byte record at H'11BBB4. */
void beep_record_byte(u16 n, u8 v)
{
    REG8(0x0011BBB4UL + (u32)(long)(short)(u16)(13 * n)) = v;
}

/* H'228C3C. A word split into the two bytes at H'11BBAA, low half first.
 *
 * The high half is read back off the stack rather than out of the register,
 * which is what makes the order look the wrong way round in the listing. */
void beep_pair_store(u16 v)
{
    REG8(0x0011BBAAUL) = (u8)v;
    REG8(0x0011BBABUL) = (u8)(v >> 8);
}

void queue_put_number(u16 rec, u16 v)          /* H'228C90: ten bits, bytes 0-1 */
{
    REG8(QREC(rec) + 0) = (u8)v;
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & 0xFC);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) |
                               (u8)((u8)(v >> 8) & 0x03));
}

void queue_put_mid4(u16 rec, u8 v)             /* H'228CE4: byte 1 bits 2-5 */
{
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & 0xC3);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) |
                               (u8)((u8)(v << 2) & 0x3C));
}

void queue_put_bit6(u16 rec, u8 v)             /* H'228D30: byte 1 bit 6 */
{
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & (u8)~0x40);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) | v);
}

void queue_put_bit7(u16 rec, u8 v)             /* H'228D94: byte 1 bit 7 */
{
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & (u8)~0x80);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) | v);
}

void queue_put_byte2(u16 rec, u8 v) { REG8(QREC(rec) + 2) = v; }  /* H'228DF8 */
void queue_put_byte3(u16 rec, u8 v) { REG8(QREC(rec) + 3) = v; }  /* H'228E18 */
void queue_put_byte5(u16 rec, u8 v) { REG8(QREC(rec) + 5) = v; }  /* H'228E9E */
void queue_put_byte6(u16 rec, u8 v) { REG8(QREC(rec) + 6) = v; }  /* H'228EDA */
void queue_put_byte10(u16 rec, u8 v) { REG8(QREC(rec) + 10) = v; } /* H'229054 */

void queue_put_low6(u16 rec, u8 v)             /* H'228E38: byte 4 bits 0-5 */
{
    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) & 0xC0);
    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) | (u8)(v & 0x3F));
}

/* H'229074: byte 4 bit 7 -- and only bit 7, though what is or'd in is three
 * bits wide. The value is rotated right three times, so its bottom three
 * bits arrive at the top, and then masked to H'E0. */
void queue_put_top3(u16 rec, u8 v)
{
    u8 k = v;
    u8 i;

    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) & (u8)~0x80);
    for (i = 0; i < 3; i++) k = (u8)((u8)(k >> 1) | (u8)(k << 7));
    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) | (u8)(k & 0xE0));
}

void queue_put_low2(u16 rec, u8 v)             /* H'228F16: byte 7 bits 0-1 */
{
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) & 0xFC);
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) | v);
}

void queue_put_bit3(u16 rec, u8 v)             /* H'228F7A: byte 7 bit 3 */
{
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) & (u8)~0x08);
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) | (u8)(v >> 1));
}

void queue_put_high4(u16 rec, u8 v)            /* H'228FE0: byte 7 bits 4-7 */
{
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) & 0x0F);
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) | (u8)(v << 4));
}

/* H'229100. Six of a pattern's parameters read out of the catalogue and put
 * into a queue record, one getter and one setter each. Every getter is
 * called with its "use the working copy" flag set. */
void queue_put_params(u16 rec, u16 pattern)
{
    queue_put_mid4 (rec, stitch_param_4(pattern, 0x01));
    queue_put_byte2(rec, stitch_param_2(pattern, 0x01));
    queue_put_byte3(rec, stitch_param_1(pattern, 0x01));
    queue_put_low6 (rec, stitch_param_5(pattern, 0x01));
    queue_put_byte5(rec, stitch_param_6(pattern, 0x01));
    queue_put_byte6(rec, stitch_param_7(pattern, 0x01));
}

/* H'2292CC. The first entry's pattern number, one on, put back. */
void queue_number_bump(void)
{
    beep_pair_store((u16)(queue_entry_number_first() + 1));
}

/* H'229198. One queue record filled in from the machine's current state.
 *
 * Two flags at H'11A1E1 and H'11A1E2 stand for "nothing here" and "the end
 * of a group": either one writes the two sentinel numbers H'3FF and H'3FE
 * into the record and into its parameters, clears the flag, and stops. The
 * record is zeroed first either way.
 *
 * Otherwise the pattern number goes in, then the six parameters, then eight
 * more fields straight off the panel ports, and finally the byte H'2290E0
 * answered at the top -- which is why that call comes first and its answer
 * is carried all the way down. */
void queue_record_fill(u16 rec)
{
    const u8 off = queue_entry_offset(rec);

    mem_set(QREC(rec), 0x00, 0x000D);

    if (REG8(0x0011A1E1UL) != 0) {
        queue_put_number(rec, 0x03FF);
        queue_put_params(rec, 0x03FF);
        REG8(0x0011A1E1UL) = 0x00;
        return;
    }

    if (REG8(0x0011A1E2UL) != 0) {
        queue_put_number(rec, 0x03FE);
        queue_put_params(rec, 0x03FE);
        REG8(0x0011A1E2UL) = 0x00;
        return;
    }

    queue_put_number(rec, REG16(0x00FFFEE0UL));
    queue_put_bit6(rec, (u8)(REG8(0x00FFFEF6UL) & 0x40));
    queue_put_bit7(rec, (u8)(REG8(0x00FFFEF6UL) & 0x80));
    queue_put_params(rec, (u16)(REG16(0x00FFFEE0UL) + (u16)off));
    queue_put_low2(rec, (u8)(REG8(0x00FFFEF5UL) & 0x03));
    queue_put_bit3(rec, (u8)(REG8(0x00FFFEF5UL) & 0x10));
    queue_put_high4(rec, (u8)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F));
    queue_put_top3(rec, (u8)(REG8(0x00FFFEF5UL) & 0x04));
    queue_put_byte10(rec, off);
}

/* H'210AB2. Room made in the three lists for a run of entries.
 *
 * H'210784 says how long the run is -- the entry asked for plus every
 * category-1 entry after it -- and the whole run has to fit: adding it to
 * H'11B198 must not take the total past H'3C, and when it would, H'11A18A is
 * raised and nothing is inserted.
 *
 * Otherwise one slot is opened in H'11B212 for the entry itself, and then
 * one slot in each of H'11B198 and H'11B11E for every entry of the run. */
void queue_make_room(u16 at)
{
    const u16 run = queue_run_length(at);
    short i;
    u16 put = at;

    if ((short)(REG16(0x0011B198UL) + run) > 0x003C) {
        REG8(0x0011A18AUL) = 0x01;
        return;
    }

    list_insert((u16 *)0x0011B212UL, (u16)(REG16(0x0011B212UL) + 1), at);
    REG16(0x0011A186UL) = REG16(0x0011B212UL);

    for (i = 1; i <= (short)run; i++) {
        list_insert((u16 *)0x0011B198UL, (u16)(REG16(0x0011B198UL) + 1),
                    (u16)(REG16(0x0011B11CUL) + REG16(0x0011B198UL) + 1));
        list_insert((u16 *)0x0011B11EUL, (u16)(REG16(0x0011B11EUL) + 1), put);
        put++;
    }
}

/* H'228A92. One entry added to the queue.
 *
 * The queue is full at H'3E8 entries and says so with message H'20. Below
 * that the records above the cursor are shifted up by one -- one H'0D-byte
 * record's worth, moved with the ROM's memmove because the two overlap --
 * the number list has a slot opened in it, the new record is filled in from
 * the machine's state, and the picker is redrawn and stepped on by one.
 *
 * The number inserted into the list at H'11B3D8 is the first argument, read
 * back off the stack rather than out of the register it arrived in; the
 * offset stored in the record is the low byte of the second. */
u8 queue_add_entry(u16 number, u16 offset)
{
    const u16 at = REG16(0x0011A1CCUL);

    if ((short)REG16(0x0011B3D8UL) >= 0x03E8) {
        message_show(0x0020);
        return 0x00;
    }

    REG8(0x0011A184UL) = 0x01;

    mem_move((void *)(0x0011BBB7UL + (u32)(long)(short)(u16)(13 * at)),
             (const void *)(0x0011BBAAUL + (u32)(long)(short)(u16)(13 * at)),
             (u32)(long)(short)(u16)
                 (13 * (u16)(queue_entry_number_first() - at + 1)));

    list_insert((u16 *)0x0011B3D8UL, (u16)(at + 1), number);
    queue_put_byte10((u16)(at + 1), (u8)offset);
    REG16(0x0011A1D2UL) = (u16)(REG16(0x0011A1D2UL) + 1);
    queue_number_bump();
    queue_record_fill((u16)(at + 1));
    picker_draw_range((u16)(REG16(0x0011A1C8UL) + 1), (u16)(at + 1),
                      REG16(0x0011A1D2UL));
    picker_forward(0x0001);
    return 0x01;
}

/* ---- taking an entry out again -----------------------------------------
 * The four little readers and the two removers that answer H'21DD6A and
 * H'228A92.
 */

/* H'22B23A. The low byte of the picker position. */
u8 picker_pos_low(void) { return REG8(0x0011A1CDUL); }

/* H'22B202. Whether the picker's first and last are the same position. */
u8 picker_one_only(void)
{
    return (u8)(REG16(0x0011A1D0UL) == REG16(0x0011A1D2UL) ? 1 : 0);
}

/* H'22B21E. Whether the picker is at or before its first position. */
u8 picker_at_start(void)
{
    return (u8)((short)REG16(0x0011A1CCUL) > (short)REG16(0x0011A1D0UL)
                ? 0 : 1);
}

/* H'2292E4. The first entry's number, one off, put back. */
void queue_number_drop(void)
{
    beep_pair_store((u16)(queue_entry_number_first() - 1));
}

/* H'21DD18. One value taken out of a counted list of words -- the mirror of
 * H'21DD6A. The tail comes down by one, the word left at the end is zeroed,
 * and the count comes down with it. The length is worked out from the count
 * before the delete, so the word being removed is part of what moves. */
void list_delete(u16 *list, u16 at)
{
    const u32 base = (u32)list;
    const u16 n = (u16)((u16)(REG16(base) - at) << 1);
    const u32 slot = base + (u32)(long)(short)(u16)((u16)(at << 1));

    mem_copy((u8 *)slot, (const u8 *)(slot + 2), n);
    REG16(base + (u32)(long)(short)(u16)((u16)(REG16(base) << 1))) = 0x0000;
    REG16(base) = (u16)(REG16(base) - 1);
}

/* H'2292FC. One queue record taken out: the records above it come down by
 * one, the record left at the end is zeroed, and the stored number comes
 * down with them.
 *
 * The length of the move is the low word of the product, with no sign
 * extension -- so a position past the first entry's number wraps rather than
 * counting backwards. */
void queue_remove_record(u16 at)
{
    const u16 n = (u16)(13 * (u16)(queue_entry_number_first() - at));

    mem_copy((u8 *)(0x0011BBAAUL + (u32)(long)(short)(u16)(13 * at)),
             (const u8 *)(0x0011BBB7UL + (u32)(long)(short)(u16)(13 * at)), n);
    mem_set(0x0011BBAAUL +
            (u32)(long)(short)(u16)(13 * queue_entry_number_first()),
            0x00, 0x000D);
    queue_number_drop();
}

/* H'20318C. A pattern taken up: its cached flags cleared down to bit 7, its
 * four parameters copied into the live registers, and a handful of state
 * bits put back.
 *
 * The four are copied only when bit 1 of H'114DCA is up *and* the pedal
 * position is not H'FFFF; either failing, the whole set is recomputed from
 * the catalogue instead. */
void pattern_take_up(u16 n)
{
    const u32 flag = 0x000E4010UL + (u32)(u16)((u16)(n << 4));

    REG8(flag) = (u8)(REG8(flag) & 0x80);

    if ((REG8(0x00114DCAUL) & 0x02) && REG16(0x00FFFEFEUL) != 0xFFFF) {
        REG8(0x0011A69EUL) = stitch_param_1(n, 0x01);
        sew_param_a_set(REG8(0x0011A69EUL));
        REG8(0x0011A6A0UL) = stitch_param_2(n, 0x01);
        sew_param_b_set(REG8(0x0011A6A0UL));
        REG8(0x00FFFEEAUL) = stitch_param_4(n, 0x01);
        REG8(0x00FFFEECUL) = stitch_param_5(n, 0x01);
    } else {
        sew_params_for_pattern(n);
        sew_params_publish();
    }

    REG8(0x00114DC7UL) &= (u8)~0x80;
    REG8(0x00FFFEF8UL) &= (u8)~0x20;
    REG8(0x0011A6CFUL) = 0x00;

    if (REG8(0x00114DC6UL) & 0x08) {
        REG8(0x00114DC6UL) &= (u8)~0x08;
        REG8(0x00FFFEFAUL) &= (u8)~0x80;
        REG8(0x00FFFEF7UL) &= (u8)~0x08;
        REG8(0x0011A6AEUL) |= 0x01;
        REG8(0x00114DCDUL) &= (u8)~0x08;
    }
}

/* H'2016F2. Every cached flag in the table cleared down to bit 7, and then
 * the pattern the pedal is on taken up. The loop stops one short of H'400,
 * the same off-by-one item_flags_clear has. */
void pattern_flags_clear_all(void)
{
    const u16 n = (u16)(REG16(0x00FFFEE0UL) + (u16)REG8(0x00FFFEFDUL));
    u16 i;

    for (i = 0; i < 0x03FF; i++) {
        const u32 flag = 0x000E4010UL + (u32)(u16)((u16)(i << 4));

        REG8(flag) = (u8)(REG8(flag) & 0x80);
    }
    pattern_take_up(n);
}

/* H'21C4EC. A screen handed over, with three ways of getting there.
 *
 * While H'11A184 is up the change is only asked for -- dialog 3 puts the
 * question -- and the only preparation done is for screen H'41. Otherwise
 * H'41 and H'12 both go to the screen properly and everything else only
 * makes itself current, which is what leaves the screen change to whoever
 * asked for it. */
void screen_hand_over(u8 screen)
{
    REG8(0x0011B11AUL) = screen;

    if (REG8(0x0011A184UL) != 0) {
        if (REG8(0x0011B11AUL) == 0x41) {
            dialog_backdrop_save(0x01);
            screen_remember(0x01);
            screen_put_away();
        }
        dialog_show(0x0003);
        return;
    }

    if (REG8(0x0011B11AUL) == 0x41) {
        dialog_backdrop_save(0x01);
        screen_remember(0x01);
        screen_switch(REG8(0x0011B11AUL), 0x01, 0x00);
    } else if (REG8(0x0011B11AUL) == 0x12) {
        drawing_reset();
        REG8(0x0011A169UL) = REG8(0x0011B11AUL);
        screen_switch(REG8(0x0011B11AUL), 0x01, 0x00);
    } else {
        drawing_reset();
        REG8(0x0011A169UL) = REG8(0x0011B11AUL);
    }
}

/* H'22A012. The whole queue read back out of flash: the two blocks copied
 * in, the number list rebuilt an entry at a time, and the picker redrawn. */
void queue_reload(void)
{
    short i;

    mem_copy((u8 *)0x0011EE80UL, (const u8 *)0x0057B2D6UL, 0x0400);
    mem_copy((u8 *)0x0011BBAAUL, (const u8 *)0x00578000UL, 0x32D5);

    REG16(0x0011B3D8UL) = queue_entry_number_first();

    for (i = 1; i <= 0x03E8; i++) {
        REG16(0x0011B3D8UL + (u32)(long)(short)(u16)((u16)((u16)i << 1))) =
            queue_entry_number((u16)i);
    }

    picker_rebuild(REG16(0x0011A1CEUL), 0x01, 0x01);
}

/* ---- the queue's ranges -------------------------------------------------
 * H'11EE80 holds H'100 pairs of words -- a first and a last position each --
 * and H'57B2D6 is the copy of them in flash.
 */

/* H'229368. Every range brought into line with the one at H'11A1CE after it
 * has moved: a pair whose two halves are equal is cleared, and a pair that
 * starts after the moved one's old start is shifted by however much the
 * moved one grew. */
void queue_ranges_shift(void)
{
    const u32 at = (u32)(long)(short)(u16)((u16)(REG16(0x0011A1CEUL) << 2));
    const u16 grew = (u16)(REG16(0x0011EE82UL + at) -
                           REG16(0x0057B2D8UL + at));
    const u16 was  = REG16(0x0011EE80UL + at);
    short i;

    for (i = 0; i <= 0x00FF; i++) {
        const u32 e = (u32)(long)(short)(u16)((u16)((u16)i << 2));

        if (REG16(0x0011EE80UL + e) == REG16(0x0011EE82UL + e)) {
            REG16(0x0011EE82UL + e) = 0x0000;
            REG16(0x0011EE80UL + e) = 0x0000;
        } else if ((short)REG16(0x0011EE80UL + e) > (short)was) {
            REG16(0x0011EE80UL + e) = (u16)(REG16(0x0011EE80UL + e) + grew);
            REG16(0x0011EE82UL + e) = (u16)(REG16(0x0011EE82UL + e) + grew);
        }
    }
}

/* H'229468. The queue and its ranges written back out to flash: the range at
 * H'11A1CE takes the picker's own first and last, the rest are brought into
 * line, and both blocks are programmed with the flash-busy bit up. */
void queue_save_ranges(void)
{
    const u32 at = (u32)(long)(short)(u16)((u16)(REG16(0x0011A1CEUL) << 2));

    REG8(0x0011A184UL) = 0x00;
    FLASH_BUSY |= 0x20;
    rom_flash_write((const void *)0x0011BBAAUL, 0x00578000UL, 0x32D5);

    REG16(0x0011EE80UL + at) = REG16(0x0011A1D0UL);
    REG16(0x0011EE82UL + at) = REG16(0x0011A1D2UL);
    queue_ranges_shift();

    rom_flash_write((const void *)0x0011EE80UL, 0x0057B2D6UL, 0x0400);
    FLASH_BUSY &= (u8)~0x20;
}

/* H'228B80. One entry taken out from under the cursor, and only when the
 * cursor is past the first position. Emptying the queue puts the picker's
 * left edge back to H'30, with the cursor taken down and put up again round
 * the move. */
void queue_delete_entry(void)
{
    if (REG16(0x0011A1CCUL) == 0) return;
    if ((short)REG16(0x0011A1CCUL) <= (short)REG16(0x0011A1D0UL)) return;

    REG8(0x0011A184UL) = 0x01;
    picker_back(0x0001);
    REG16(0x0011A1D2UL) = (u16)(REG16(0x0011A1D2UL) - 1);
    list_delete((u16 *)0x0011B3D8UL, (u16)(REG16(0x0011A1CCUL) + 1));

    if (REG16(0x0011B3D8UL) == 0) {
        picker_cursor(0x00);
        REG16(0x0011A1C8UL) = 0x0030;
        picker_cursor(0x01);
    }

    queue_remove_record((u16)(REG16(0x0011A1CCUL) + 1));
    picker_draw_range((u16)(REG16(0x0011A1C8UL) + 1),
                      (u16)(REG16(0x0011A1CCUL) + 1), REG16(0x0011A1D2UL));
}

/* H'222F00. Everything the machine holds about the pattern put back to
 * nothing, ready for the next one.
 *
 * H'FFFEF9 is anded with H'F0 and then with H'0F, which can only leave
 * nothing. That is in the original and is kept. */
void pattern_state_reset(void)
{
    pattern_flags_clear_all();

    REG8(0x00FFFEF5UL) &= (u8)~0x08;
    REG8(0x00FFFEF5UL) &= (u8)~0x10;
    REG8(0x00FFFEF6UL) &= (u8)~0x80;
    REG8(0x00FFFEF6UL) &= (u8)~0x40;
    REG8(0x00FFFEF6UL) &= (u8)~0x10;
    REG8(0x00FFFEF5UL) &= (u8)~0x80;
    REG8(0x00FFFEF9UL) = (u8)((u8)(REG8(0x00FFFEF9UL) & 0xF0) & 0x0F);
    REG8(0x00FFFEF5UL) |= 0x04;

    if (REG8(ITEM_TABLE +
             (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x17) == 0x16) {
        REG8(0x00FFFEFDUL) = 0x00;
    }

    REG8(0x00FFFEFCUL) = 0x00;
    REG8(0x00FFFEFBUL) = 0x00;

    REG8(0x00FFFEF6UL) = (u8)(REG8(0x00FFFEF6UL) & 0xF0);
    if (!(REG8(0x00FFFEF6UL) & 0x20)) {
        REG8(0x00FFFEF6UL) ^= 0x20;
        needle_stop_picture();
    }

    REG8(0x00FFFEF5UL) = (u8)(REG8(0x00FFFEF5UL) & 0xFC);
    REG8(0x0011A17BUL) = 0x01;
}

/* H'200EC0, H'2010EC. The nine settings of one pattern, moved between the
 * ten-byte record at H'57C6D6 in flash and the sixteen-byte working copy at
 * H'0E4010. Byte 0 of the record is the "this slot is in use" flag that
 * H'200E46 clears and that the save below sets; the other nine are the
 * settings, and they do not go into the working copy in order:
 *
 *   record +1 +2 +3 +4 +5 +6 +7 +8 +9
 *   work   +1 +2 +7 +3 +8 +4 +5 +6 +9
 *
 * The load also republishes four of them into the panel's own bytes. Which
 * of record +2/+3 and +4/+5 is used depends on bit 6 of H'11A7BD, the
 * alternate-mode flag: the record carries both sets and the mode picks one.
 */
void stitch_working_load(u16 n, u8 announce)
{
    const u32 src  = 0x0057C6D6UL + (u32)(u16)(10 * n);
    const u32 work = 0x000E4010UL + (u32)(u16)((u16)(n << 4));

    REG8(work + 0x01) = REG8(src + 1);
    REG8(work + 0x02) = REG8(src + 2);
    REG8(work + 0x07) = REG8(src + 3);
    REG8(work + 0x03) = REG8(src + 4);
    REG8(work + 0x08) = REG8(src + 5);
    REG8(work + 0x04) = REG8(src + 6);
    REG8(work + 0x05) = REG8(src + 7);
    REG8(work + 0x06) = REG8(src + 8);
    REG8(work + 0x09) = REG8(src + 9);

    REG8(0x0011A7D9UL) = REG8(src + 1);
    REG8(0x0011A69EUL) = REG8(src + 1);
    sew_param_a_set(REG8(src + 1));

    if (REG8(0x0011A7BDUL) & 0x40) {
        REG8(0x0011A6A0UL) = REG8(src + 4);
        REG8(0x0011A7DAUL) = REG8(src + 4);
        REG8(0x00FFFEFCUL) = REG8(src + 5);
    } else {
        REG8(0x0011A6A0UL) = REG8(src + 2);
        REG8(0x0011A7DAUL) = REG8(src + 2);
        REG8(0x00FFFEFCUL) = REG8(src + 3);
    }
    sew_param_b_set(REG8(0x0011A6A0UL));

    REG8(0x0011A7D8UL) = REG8(src + 6);
    REG8(0x00FFFEEAUL) = REG8(src + 6);
    REG8(0x0011A7DBUL) = REG8(src + 7);
    REG8(0x00FFFEECUL) = REG8(src + 7);
    REG8(0x00FFFEFBUL) = REG8(src + 8);
    REG8(0x0011A7ACUL) = REG8(src + 9);

    if (announce != 0) {
        REG8(0x00114DC6UL) |= 0x08;
        REG8(0x00FFFEF7UL) |= 0x08;
        REG8(0x00FFFEFAUL) &= (u8)~0x80;
        REG8(0x00FFFEF5UL) |= 0x40;
    }
}

/* H'2010EC. The same nine the other way, a byte at a time through the boot
 * rom's flash writer, with bit 5 of H'114DC7 held up for the length of it.
 * Byte 0 of the record goes to 1, which is what marks the slot as used. */
void stitch_working_save(u16 n)
{
    const u32 dst  = 0x0057C6D6UL + (u32)(u16)(10 * n);
    const u32 work = 0x000E4010UL + (u32)(u16)((u16)(n << 4));
    u8 one = 0x01;

    REG8(0x00114DC7UL) |= 0x20;

    rom_flash_write(&one, dst + 0, 1);
    rom_flash_write((const void *)(work + 0x01), dst + 1, 1);
    rom_flash_write((const void *)(work + 0x03), dst + 4, 1);
    rom_flash_write((const void *)(work + 0x08), dst + 5, 1);
    rom_flash_write((const void *)(work + 0x02), dst + 2, 1);
    rom_flash_write((const void *)(work + 0x07), dst + 3, 1);
    rom_flash_write((const void *)(work + 0x04), dst + 6, 1);
    rom_flash_write((const void *)(work + 0x05), dst + 7, 1);
    rom_flash_write((const void *)(work + 0x06), dst + 8, 1);
    rom_flash_write((const void *)(work + 0x09), dst + 9, 1);

    REG8(0x00114DC7UL) &= (u8)~0x20;
}

/* H'213164. A run of boxes scrolled on by one entry.
 *
 * H'212FF0 slides boxes [first]+1..[last] down over [first], which leaves
 * the last two boxes holding the same thing; the last one is then moved on
 * to the next entry of its own list and the whole run drawn again. The
 * wrap-around point is the count word at the head of the *first* box's
 * list, read before the slide -- a run always shows one list, so the count
 * is the same list's either way, but it is read from the box that is about
 * to be overwritten.
 *
 * What the new entry looks like depends on its kind byte: the nine kinds
 * below are drawn unlit, and everything else asks H'214DD4 what the field
 * is set to and lights the box if the answer is positive.
 *
 * The answer is the value the first box ends up with, which is the entry
 * the run now starts at. */
u16 hitbox_run_scroll(u16 first, u16 last)
{
    const u32 head = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * first);
    const u32 tail = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * last);
    const u16 count = REG16(REG32(head + 0x0C));
    u16 value;
    u8  kind;

    hitbox_run_shift((u16)(first + 1), last, first);

    value = REG16(tail + 0x08);
    if (value == count) value = 0x0001;
    else                value = (u16)(value + 1);
    REG16(tail + 0x08) = value;

    kind = REG8(REG32(tail + 0x0C) +
                (u32)(long)(short)(u16)((u16)(value << 1)) + 1);

    if (kind == 0x04 ||
        (kind >= 0x07 && kind < 0x09) ||
        (kind >= 0x0B && kind < 0x0D) ||
        kind == 0x44 || kind == 0x46) {
        hitbox_set_state(last, last, 0x00, 0);
    } else {
        const u16 on = panel_switch(kind, last, 0, 0);
        hitbox_set_state(last, last, (u8)(((short)on > 0) ? 0x01 : 0x00), 0);
    }

    hitbox_redraw_run(first, last);
    REG8(0x0011A17BUL) = 0x01;
    return REG16(head + 0x08);
}

/* H'2220F0. Backing out of a sub-screen to the one it was opened from.
 *
 * Four screens know how to do this and the rest do nothing at all. Each
 * names the screen it goes back to -- H'07 and H'45 go to H'02 or H'30
 * depending on which of the two is showing, H'34 and H'36 to H'35 or H'33,
 * H'04 to H'03, H'47 to H'46 -- and then repaints the two panels the
 * sub-screen covered and puts its strip of boxes back into their menu
 * states. H'47 only clears the wide panel and has no boxes to put back.
 *
 * H'11A169 is written before H'21F1DE is called, so what is put away is the
 * screen being *returned to*, not the one being left. That is in the
 * original. */
void screen_back_out(u8 screen)
{
    picker_cursor(0x00);
    REG8(0x0011A174UL) = 0x00;

    if (screen == 0x07 || screen == 0x45) {
        REG8(0x0011A169UL) = (u8)((REG8(0x0011A169UL) == 0x07) ? 0x02 : 0x30);

        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 2, 1);
        hitbox_set_state(0x001F, 0x0020, 0x02, 0);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0, 1);
        hitbox_set_state(0x0021, 0x0025, 0x04, 0);

        hitbox_set_state(0x000B, 0x000F, 0x03, 0);
        hitbox_redraw_run(0x000B, 0x000F);
        hitbox_set_state(0x0012, 0x0015, 0x03, 0);
        hitbox_redraw_run(0x0012, 0x0015);
    } else if (screen == 0x34 || screen == 0x36) {
        REG8(0x0011A169UL) = (u8)((REG8(0x0011A169UL) == 0x36) ? 0x35 : 0x33);

        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 2, 1);
        hitbox_set_state(0x001A, 0x001B, 0x02, 0);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0, 1);
        hitbox_set_state(0x0013, 0x0014, 0x04, 0);

        hitbox_set_state(0x0007, 0x0008, 0x03, 0);
        hitbox_redraw_run(0x0007, 0x0008);
        hitbox_set_state(0x000F, 0x0012, 0x03, 0);
        hitbox_redraw_run(0x000F, 0x0012);
    } else if (screen == 0x04) {
        REG8(0x0011A169UL) = 0x03;

        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 2, 1);
        hitbox_set_state(0x0024, 0x0025, 0x02, 0);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0, 1);

        hitbox_set_state(0x0010, 0x0014, 0x03, 0);
        hitbox_redraw_run(0x0010, 0x0014);
        hitbox_set_state(0x001B, 0x001E, 0x03, 0);
        hitbox_redraw_run(0x001B, 0x001E);
    } else if (screen == 0x47) {
        REG8(0x0011A169UL) = 0x46;

        draw_rect(0x0004, 0x009D, 0x013B, 0x00EC, LCD_FRAME_A, 0, 1);
    } else {
        return;
    }

    screen_leave(REG8(0x0011A169UL), 0x01);
    REG8(0x0011A17BUL) = 0x01;
}

/* ---- what a press does --------------------------------------------------
 *
 * H'21FF3C is the biggest routine in the application: a press turned into
 * a message and the message acted on, over sixty-eight screens. Three
 * helpers first, for the shapes it repeats.
 */

/* The six screens that have a queue under them. */
static u8 queue_screen(u8 screen)
{
    return (u8)(screen == 0x04 || screen == 0x07 || screen == 0x34 ||
                screen == 0x36 || screen == 0x45 || screen == 0x47);
}

/* Whether the picker will let the screen go. Asked only while H'11A175 is
 * up -- that is, while the queue dialog is the thing showing. The first
 * test is on the low byte of the position alone, which is what the original
 * asks for. */
static u8 picker_may_leave(void)
{
    if (picker_pos_low() == 0) return 0;
    if (picker_at_start() != 0) return 0;
    if (picker_one_only() != 0) return 0;
    return 1;
}

/* The body under messages H'17 and H'18: the pattern strip moved a page
 * back or a page on, with the pressed box taken out of its lit state and
 * put back into it round the move. Six copies in the original, differing
 * only in how far the strip runs, how big a page is, and whether a second
 * run of boxes follows it along. */
static void strip_page(u16 index, u16 last, u16 step, u8 forward,
                       u16 shift_first, u16 shift_last, u16 shift_dest,
                       u16 redraw_first, u16 redraw_last)
{
    u16 was;

    message_show_held(index);

    was = hitbox_find(0x0001, last, REG16(0x00FFFEE0UL), 0x01);
    hitbox_set_state(was, was, 0x00, 0);

    REG16(0x0011B108UL) = forward
        ? hitbox_list_scroll_on(0x0001, last, step)
        : hitbox_list_scroll_back(0x0001, last, step);

    was = hitbox_find(0x0001, last, REG16(0x00FFFEE0UL), 0x01);
    hitbox_set_state(was, was, 0x01, 0);

    if (redraw_last != 0) {
        hitbox_run_shift(shift_first, shift_last, shift_dest);
        hitbox_redraw_run(redraw_first, redraw_last);
    }
}

/* H'21FF3C. What a press does, screen by screen: the biggest single routine
 * in the application and the one everything else on the panel hangs off.
 *
 * It is three dispatches deep. The screen picks a prelude, and six of the
 * twelve screens that have one take the press as a *pattern* -- the boxes of
 * the pattern strip -- and are finished with it. Anything the prelude does
 * not claim falls through to the common body, where the screen picks a
 * second range of boxes to hit-test; that hit yields a message number, and
 * the message number picks one of twenty-four bodies out of a table of a
 * hundred and twenty-four. Eighty-one of the hundred and twenty-four do
 * nothing at all.
 *
 * Two words of local are threaded through the whole thing: the value the
 * box carries, which becomes the message number, and the box's own index.
 *
 * Every path returns zero.
 */
u8 screen_touch(void)
{
    u16 value = 0;                    /* the box's value, then the message */
    u16 index = 0;                    /* and which box it was */
    u8  screen = REG8(0x0011A169UL);
    u8  r;

    switch (screen) {

    /* ---- the pattern strip ------------------------------------------- */
    case 0x02: case 0x18: case 0x30: case 0x44:
        if (touch_hit(0x0001, 0x000F, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x000F, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            const u8 s = REG8(0x0011A169UL);

            if (s != 0x44 && s != 0x30) {
                queue_make_room(REG16(0x00FFFEE0UL));
                screen_switch(0x44, 0x01, 0x00);
            }
        }
        return 0;

    case 0x07: case 0x45:
        if (touch_hit(0x0001, 0x000A, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            u16 was = hitbox_find(0x0001, 0x000F, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);

            was = hitbox_find(0x0021, 0x0025, REG16(0x00FFFEE0UL), 0);
            if (was != 0) hitbox_set_state(was, was, 0x00, 0);

            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            const u8 s = REG8(0x0011A169UL);

            if (s != 0x44 && s != 0x30) {
                queue_make_room(REG16(0x00FFFEE0UL));
                screen_switch(0x44, 0x01, 0x00);
                return 0;
            }
        }
        if (REG32(0x0011A196UL) != 0x0057EEF8UL) {
            queue_add_entry(REG16(0x00FFFEE0UL), REG8(0x00FFFEFDUL));
            hold_start(0x0096);
        }
        return 0;

    case 0x03:
        if (touch_hit(0x0001, 0x0014, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x0014, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) screen_switch(0x44, 0x01, 0x00);
        return 0;

    case 0x04:
        if (touch_hit(0x0001, 0x000F, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x0014, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            screen_switch(0x44, 0x01, 0x00);
            return 0;
        }
        queue_add_entry(REG16(0x00FFFEE0UL), REG8(0x00FFFEFDUL));
        hold_start(0x0096);
        return 0;

    case 0x33: case 0x35:
        if (touch_hit(0x0001, 0x0008, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x0008, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) screen_switch(0x44, 0x01, 0x00);
        return 0;

    case 0x34: case 0x36:
        if (touch_hit(0x0001, 0x0006, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            u16 was = hitbox_find(0x0001, 0x0008, REG16(0x00FFFEE0UL), 0);
            u8  now, next;

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);

            was = hitbox_find(0x0013, 0x0014, REG16(0x00FFFEE0UL), 0);
            if (was != 0) hitbox_set_state(was, was, 0x00, 0);

            /* Moving between the two halves of a two-part pattern drops
             * the mirror flag. The category of the one showing is read
             * unsigned and the category of the one pressed signed, which is
             * in the original. */
            now = REG8(ITEM_TABLE +
                       (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x17);
            if (now == 0x10) {
                next = REG8(ITEM_TABLE +
                            (u32)(long)(short)(u16)(ITEM_STRIDE * value) + 0x17);
                if (next == 0x11) REG8(0x00FFFEFDUL) = 0x00;
            } else if (now == 0x11) {
                next = REG8(ITEM_TABLE +
                            (u32)(long)(short)(u16)(ITEM_STRIDE * value) + 0x17);
                if (next == 0x10) REG8(0x00FFFEFDUL) = 0x00;
            }

            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            screen_switch(0x44, 0x01, 0x00);
            return 0;
        }
        queue_add_entry(REG16(0x00FFFEE0UL), REG8(0x00FFFEFDUL));
        hold_start(0x0096);
        return 0;

    default:
        break;
    }

    /* ---- the rest of the screen -------------------------------------- */
    switch (screen) {
    case 0x02: case 0x18: case 0x30:
        r = touch_hit(0x0010, 0x0019, &value, &index);
        break;

    case 0x07: case 0x45:
        r = touch_hit(0x0010, 0x0011, &value, &index);
        if (r != 0x03) r = touch_hit(0x0016, 0x0020, &value, &index);
        break;

    case 0x03:
        r = touch_hit(0x0015, 0x001E, &value, &index);
        break;

    case 0x04:
        r = touch_hit(0x0015, 0x001A, &value, &index);
        if (r != 0x03) r = touch_hit(0x001F, 0x0025, &value, &index);
        break;

    case 0x33: case 0x35:
        r = touch_hit(0x0009, 0x0012, &value, &index);
        break;

    case 0x34: case 0x36:
        r = touch_hit(0x0009, 0x000E, &value, &index);
        if (r != 0x03) r = touch_hit(0x0015, 0x001B, &value, &index);
        break;

    case 0x42: r = touch_hit(0x0008, 0x000A, &value, &index); break;
    case 0x44: r = touch_hit(0x0018, 0x0019, &value, &index); break;
    case 0x46: r = touch_hit(0x0000, 0x0000, &value, &index); break;
    case 0x47: r = touch_hit(0x000E, 0x0012, &value, &index); break;

    default:
        return 0;
    }

    if (r == 0x02) r = screen_leave_check(&value, 0x00);
    if (r != 0x03) {
        /* H'221B48 */
        if (REG8(0x00FFFEF8UL) & 0x80) REG8(0x00FFFEF8UL) &= (u8)~0x80;
        return 0;
    }

    for (;;) {
        if ((u16)(value - 1) > 0x007B) return 0;

        switch (value) {

        /* The panel's own state: twenty of the hundred and twenty-four
         * messages are a field of H'214DD4, stepped on by the press. */
        case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
        case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0C: case 0x44:
        case 0x46: case 0x47: case 0x49: case 0x6E: case 0x6F: case 0x76:
        case 0x7B:
            panel_switch((u8)value, index, 0x01, 0x00);
            return 0;

        /* ---- the pattern strip ---------------------------------------- */
        case 0x17: case 0x18: {
            const u8  forward = (u8)(value == 0x18);
            const u8  screen2 = REG8(0x0011A169UL);
            u16 last, step;
            u16 sf = 0, sl = 0, sd = 0, rf = 0, rl = 0;

            if (REG8(forward ? 0x0011B0ABUL : 0x0011B0AAUL) == 0) return 0;

            switch (screen2) {
            case 0x02: case 0x18: case 0x30: case 0x44:
                last = 0x000F; step = 0x0005; break;
            case 0x07: case 0x45:
                last = 0x000F; step = 0x0005;
                sf = 0x000B; sl = 0x000F; sd = 0x0021;
                rf = 0x0021; rl = 0x0025; break;
            case 0x03: case 0x04:
                last = 0x0014; step = 0x0005; break;
            case 0x33: case 0x35:
                last = 0x0008; step = 0x0002; break;
            case 0x34: case 0x36:
                last = 0x0008; step = 0x0002;
                sf = 0x0007; sl = 0x0008; sd = 0x0013;
                rf = 0x0013; rl = 0x0014; break;
            default:
                return 0;
            }

            strip_page(index, last, step, forward, sf, sl, sd, rf, rl);
            return 0;
        }

        case 0x15: {
            const u8 s = REG8(0x0011A169UL);
            u16 first, add, cap;

            if (s == 0x02 || s == 0x07 || s == 0x18 || s == 0x30 || s == 0x45) {
                first = 0x0010; add = 0x000F; cap = 0x0015;
            } else if (s >= 0x03 && s <= 0x04) {
                first = 0x0019; add = 0x0018; cap = 0x001E;
            } else if (s >= 0x33 && s <= 0x36) {
                first = 0x000D; add = 0x000C; cap = 0x0012;
            } else if (s == 0x42) {
                first = 0x0006; add = 0x0005; cap = 0x0007;
            } else {
                return 0;
            }

            message_show_held(index);
            {
                const u16 t = (u16)(REG16(REG32(0x0011A196UL)) + add);
                const u16 to = ((short)t <= (short)cap) ? t : cap;

                REG16(0x0011B10CUL) = hitbox_run_scroll(first, to);
            }
            return 0;
        }

        /* ---- the queue ------------------------------------------------ */
        case 0x45: {
            const u8 s = REG8(0x0011A169UL);

            message_show_held(index);
            if (REG8(0x0011A178UL) != 0) return 0;

            if (s == 0x30 || s == 0x45) {
                screen_switch(REG8(0x0011B0A4UL), 0x01, 0x00);
                REG16(0x0011B108UL) = REG16(0x0011B112UL);
            } else {
                REG8(0x0011B0A4UL) = s;
                REG16(0x0011B112UL) = REG16(0x0011B108UL);
                screen_switch(0x30, 0x01, 0x00);
                REG16(0x0011B10AUL) = 0x0001;
            }
            return 0;
        }

        case 0x48: {
            const u8 s = REG8(0x0011A169UL);

            if (s == 0x04 || s == 0x07 || s == 0x34 || s == 0x36 || s == 0x45) {
                message_show_held(index);
                REG8(0x0011B0A3UL) = REG8(0x0011A169UL);
                screen_switch(0x43, 0x01, 0x00);
            }
            return 0;
        }

        case 0x0E:
            if (queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                queue_delete_entry();
            }
            return 0;

        case 0x0F:
            if (queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                screen_hand_over(0x41);
            }
            return 0;

        case 0x10:
            if (queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                queue_save_ranges();
            }
            return 0;

        case 0x40:
            if (REG8(0x0011B3D6UL) != 0 && queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                picker_back(0x0001);
            }
            return 0;

        case 0x41:
            if (REG8(0x0011B3D7UL) != 0 && queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                picker_forward(0x0001);
            }
            return 0;

        case 0x11: {
            const u8 s = REG8(0x0011A169UL);

            if (s == 0x04 || s == 0x07 || s == 0x34 || s == 0x36 || s == 0x45) {
                message_show_held(index);
                if (!(REG8(0x00114DC6UL) & 0x80)) {
                    REG8(0x0011B0A2UL) = REG8(0x0011A169UL);
                    screen_switch(0x42, 0x01, 0x00);
                    REG8(0x0011A175UL) = 0x01;
                }
            } else if (s == 0x42) {
                message_show_held(index);
                screen_stack_pop();
                dialog_backdrop_save(0x01);
                screen_switch(REG8(0x0011B0A2UL), 0x01, 0x00);
                REG8(0x0011A175UL) = 0x00;
            }
            return 0;
        }

        case 0x4A:
            message_show_held(index);
            pattern_state_reset();
            return 0;

        case 0x4B:
            message_show_held(index);
            if (REG8(0x0011A175UL) != 0) {
                if (!picker_may_leave()) return 0;
                screen_remember(0x01);
            }
            screen_switch(0x2D, 0x01, 0x00);
            return 0;

        case 0x0B:
            message_show_held(index);
            if (REG8(0x0011A175UL) != 0 && !picker_may_leave()) return 0;
            screen_remember(0x01);
            screen_switch(0x3F, 0x01, 0x00);
            return 0;

        case 0x0D:
            message_show_held(index);
            REG8(0x0011B0A5UL) = REG8(0x0011A169UL);
            REG16(0x0011B118UL) = REG16(0x0011B108UL);
            screen_switch(0x46, 0x01, 0x00);
            return 0;

        /* ---- the boxes that lead somewhere ---------------------------- */
        case 0x12:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            screen_remember(0x01);
            screen_switch(0x09, 0x01, 0x00);
            return 0;

        case 0x13:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            if (!(REG8(0x00114DC6UL) & 0x80)) {
                REG8(0x0011B0A7UL) = REG8(0x0011A169UL);
                REG8(0x00FFFEFDUL) = 0x02;
                screen_switch(0x0B, 0x01, 0x00);
            }
            return 0;

        case 0x14:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            screen_remember(0x01);
            screen_switch(0x0A, 0x01, 0x00);
            return 0;

        case 0x4C:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            stitch_working_load((u16)(REG16(0x00FFFEE0UL) + REG8(0x00FFFEFDUL)),
                                0x01);
            return 0;

        case 0x4D:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            stitch_working_save((u16)(REG16(0x00FFFEE0UL) + REG8(0x00FFFEFDUL)));
            return 0;

        /* ---- coming back from a sub-screen ---------------------------- */
        case 0x6D: {
            const u8 s = REG8(0x0011A169UL);

            if (REG8(0x00114DC6UL) & 0x80) return 0;

            switch (s) {
            case 0x02: case 0x30:
                pattern_strip_restore((u8)((s == 0x02) ? 0x07 : 0x45));
                queue_reload();
                if (hitbox_find(0x000B, 0x000F, REG16(0x00FFFEE0UL), 0x01) != 0) {
                    value = 0x0018;
                    continue;
                }
                hold_start(0x0064);
                return 0;

            case 0x33: case 0x35:
                pattern_strip_restore((u8)((s == 0x35) ? 0x36 : 0x34));
                queue_reload();
                if (hitbox_find(0x0007, 0x0008, REG16(0x00FFFEE0UL), 0x01) != 0) {
                    value = 0x0018;
                    continue;
                }
                hold_start(0x0064);
                return 0;

            case 0x03:
                pattern_strip_restore(0x04);
                queue_reload();
                if (hitbox_find(0x0010, 0x0014, REG16(0x00FFFEE0UL), 0x01) != 0) {
                    value = 0x0018;
                    continue;
                }
                hold_start(0x0064);
                return 0;

            case 0x46:
                pattern_strip_restore(0x47);
                queue_reload();
                hold_start(0x0064);
                return 0;

            case 0x34: case 0x36:
                screen_back_out(s);
                if (REG8(0x0011A169UL) == 0x35) {
                    bitmap_draw(0x005E, 0x005E, 0x00E8, 0x00C1,
                                (const u8 *)0x00300782UL, LCD_FRAME_A);
                    stitch_stroke_toggle(0x01, 0x01);
                } else {
                    bitmap_draw(0x005E, 0x005E, 0x00E8, 0x00C1,
                                (const u8 *)0x00300A47UL, LCD_FRAME_A);
                    stitch_stroke_toggle(0x00, 0x01);
                }
                screen_hand_over(REG8(0x0011A169UL));
                hold_start(0x0064);
                return 0;

            case 0x07: case 0x45:
            case 0x04:
            case 0x47:
                screen_back_out(s);
                screen_hand_over(REG8(0x0011A169UL));
                hold_start(0x0064);
                return 0;

            default:
                return 0;
            }
        }

        /* H'57EFC4 holds a message the machine wants acting on next, which
         * is taken here and put through the same table. */
        case 0x7A: {
            const u16 next = REG16(0x0057EFC4UL);

            if (next == 0) return 0;
            value = next;
            index = 0x0000;
            REG8(0x0011A17BUL) = 0x01;
            continue;
        }

        case 0x7C:
            REG8(0x00FFFEF8UL) |= 0x20;
            return 0;

        default:
            return 0;
        }
    }
}
