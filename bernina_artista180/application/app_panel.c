/* The artista 180 application, rebuilt in C: the screens, the hoop, the
 * panel's fields and switches, and its strip.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- all ten interrupt slots are now real ------------------------------ */
void isr_sci0_eri(void) __attribute__((interrupt_handler));
void isr_sci0_eri(void) { isr_sci0_eri_body(); }

void isr_sci0_txi(void) __attribute__((interrupt_handler));
void isr_sci0_txi(void) { isr_sci0_txi_body(); }

void isr_sci0_rxi(void) __attribute__((interrupt_handler));
void isr_sci0_rxi(void) { isr_sci0_rxi_body(); }

/* H'21AC2E. One box, and it goes back to whatever the screen stack has on
 * top -- the depth read first and used as the index, which is entry zero of
 * the stack read again as a number. */
u8 screen_back_one(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;
    message_show_held(index);
    if (value != 0x0001) return 0x00;

    if (REG8(0x00114DC6UL) & 0x80) return 0x00;
    screen_stack_pop();
    screen_switch(screen_stack_at(screen_stack_depth()), 0x01, 0x00);
    return 0x00;
}

/* H'216E6C. A message that goes away either when the operator presses its
 * one box or when the thing that put it up stops waiting.
 *
 * Both ways do the same five things: the message held, slot four made
 * current again, the two "a message can go up" bytes set, H'246D7E, and the
 * stack popped. They differ only in what makes H'11A177 go up -- the timed
 * way asks nothing else, the pressed way asks that the stack flag at
 * H'11A17C is down as well -- and in the order of the pop.
 *
 * On the timed way the message number handed to H'211A9E is the first local
 * and nothing has written it, the same read-before-write as H'21BA0E.
 *
 * A clear bit and a waiter that has stopped waiting both fall into the hit
 * test, not just the clear bit: the two tests branch to the same place. */
u8 message_wait_screen(void)
{
    u16 value = 0;
    u16 index;      /* read before it is written on the first path */

    if ((REG8(0x00FFFEC4UL) & 0x01) && link_owner_waiting()) {
        message_show_held(index);
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
        REG8(0x0011B0A9UL) = 0x01;
        message_state_set();
        if (REG8(0x00FFFEC4UL) & 0x01) REG8(0x0011A177UL) = 0x01;
        screen_stack_pop();
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;
    if (value != 0x001A) return 0x00;

    message_show_held(index);
    screen_from_slot(0x04);
    REG8(0x0011A179UL) = 0x01;
    REG8(0x0011B0A9UL) = 0x01;
    message_state_set();
    screen_stack_pop();
    if ((REG8(0x00FFFEC4UL) & 0x01) && REG8(0x0011A17CUL) == 0) {
        REG8(0x0011A177UL) = 0x01;
    }
    return 0x00;
}

/* H'21D104. The presser-foot pressure, H'00 to H'10, shown on the balance
 * bar. H'11B382 is the value being edited and H'11B380 the one to go back
 * to; the byte in flash is at H'57FF8F.
 *
 * Box H'17 puts one on as far as H'10, H'18 takes one off as far as zero,
 * H'7F goes to the middle at eight, H'19 accepts and H'1A cancels. Accepting
 * leaves the screen *before* it writes the flash, and copies the value into
 * a local first because the flash writer takes an address to copy from. */
u8 foot_pressure_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        const u16 v = (u16)REG8(0x0057FF8FUL);

        screen_stack_push();
        REG16(0x0011B382UL) = v;
        REG16(0x0011B380UL) = v;
        balance_bar_draw(v);
    }

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0017) {
        if ((short)REG16(0x0011B382UL) < 0x0010) {
            REG16(0x0011B382UL) = (u16)(REG16(0x0011B382UL) + 1);
            balance_bar_draw(REG16(0x0011B382UL));
        }
        return 0x00;
    }
    if (value == 0x0018) {
        if ((short)REG16(0x0011B382UL) > 0) {
            REG16(0x0011B382UL) = (u16)(REG16(0x0011B382UL) - 1);
            balance_bar_draw(REG16(0x0011B382UL));
        }
        return 0x00;
    }
    if (value == 0x007F) {
        REG16(0x0011B382UL) = 0x0008;
        balance_bar_draw(REG16(0x0011B382UL));
        return 0x00;
    }
    if (value == 0x0019) {
        u8 out;

        screen_stack_pop();
        screen_switch(0x27, 0x01, 0x00);
        FLASH_BUSY |= 0x20;
        out = REG8(0x0011B383UL);
        rom_flash_write(&out, 0x0057FF8FUL, 1);
        FLASH_BUSY &= (u8)~0x20;
        return 0x00;
    }
    if (value == 0x001A) {
        screen_stack_pop();
        REG16(0x0011B382UL) = REG16(0x0011B380UL);
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'218780. The two boxes at H'1C and H'1D, and the stroke that goes with
 * them, put up or taken down.
 *
 * The first argument picks which of the two tables H'218378 draws from and,
 * with it, which stitch category the current pattern has to be in for the
 * stroke to mean anything: H'11 for the first table and H'10 for the
 * second. The second argument is the press: down puts both boxes into state
 * 0 and draws the stroke, or greys them if the category is wrong; up is
 * only acted on when the boxes were greyed, and then it rubs the stroke out,
 * clears H'FFFEFD and draws stroke zero in its place.
 *
 * The record index is widened without sign, unlike every other use of the
 * stitch descriptor table. */
void stitch_stroke_toggle(u8 variant, u8 pressed)
{
    const u8 want = (u8)(variant != 0 ? 0x11 : 0x10);
    const u32 rec = ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL));

    if (pressed != 0) {
        hitbox_set_state(0x001C, 0x001D, 0x00, 0);
        if (REG8(rec + 0x17) == want) {
            preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x01);
        } else {
            hitbox_set_state(0x001C, 0x001D, 0x05, 0);
        }
        return;
    }

    if (REG8(rec + 0x17) != want) return;
    if (hitbox_kind(0x001C) != 0x05) return;

    hitbox_set_state(0x001C, 0x001D, 0x00, 0);
    preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x00);
    REG8(0x00FFFEFDUL) = 0x00;
    preview_stroke_draw(0x0000, variant, 0x01);
}

/* H'219A52. A list of twelve boxes with arrows either side, and a choice
 * that is remembered in H'11B328.
 *
 * The two arrows scroll the list three at a time, and each is only live
 * when its flag -- H'11B0AA up, H'11B0AB down -- says there is more list
 * that way. Scrolling puts the lit box out and lights whichever box the
 * choice has moved to afterwards, found by value rather than by index
 * because the boxes have shifted underneath it.
 *
 * Box H'19 goes on to screen H'10 in slot two and H'1A comes back off the
 * stack to H'0E; both forget the choice and both are refused while sewing.
 * Anything else is a choice: the old lit box goes out, the pressed one comes
 * on, and the value less H'25 goes into H'11B0FE. */
u8 pattern_list_screen(void)
{
    u16 value = 0, index = 0;
    u16 box;

    if (touch_hit(0x0001, 0x0010, &value, &index) != 0x03) return 0x00;

    if (value == 0x0017) {
        if (REG8(0x0011B0AAUL) != 0) {
            message_show_held(index);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x00, 0);
            REG16(0x0011B108UL) =
                hitbox_list_scroll_back(0x0001, 0x000C, 0x0003);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x01, 0);
        }
        return 0x00;
    }

    if (value == 0x0018) {
        if (REG8(0x0011B0ABUL) != 0) {
            message_show_held(index);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x00, 0);
            REG16(0x0011B108UL) =
                hitbox_list_scroll_on(0x0001, 0x000C, 0x0003);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x01, 0);
        }
        return 0x00;
    }

    if (value == 0x0019) {
        if (REG16(0x0011B328UL) != 0) {
            message_show_held(index);
            if (REG8(0x00114DC6UL) & 0x80) return 0x00;
            screen_switch(0x10, 0x02, 0x00);
            REG16(0x0011B328UL) = 0x0000;
        }
        return 0x00;
    }

    if (value == 0x001A) {
        message_show_held(index);
        if (REG8(0x00114DC6UL) & 0x80) return 0x00;
        screen_stack_pop();
        screen_switch(0x0E, 0x01, 0x00);
        REG16(0x0011B328UL) = 0x0000;
        return 0x00;
    }

    if (value != REG16(0x0011B328UL)) {
        if (REG16(0x0011B328UL) != 0) {
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x00);
            hitbox_set_state(box, box, 0x00, 0);
        }
        hitbox_set_state(index, index, 0x01, 0);
        REG16(0x0011B328UL) = value;
        REG8(0x0011B0FEUL) = (u8)((u8)value + 0xDB);
    }
    return 0x00;
}

/* H'219DE0. The help page for whatever H'11B0FE and H'11B0FF name, drawn
 * out of one of the two picture tables.
 *
 * Which table, and how long a record is, both follow the configuration
 * byte: the machine with the module has nine parts to a record and the one
 * without has ten, even though both tables are laid out the same way. The
 * record number is the page times that stride plus the part, added as a
 * byte with the carry taken into the top half.
 *
 * A record whose first part is there and whose second is not is one picture
 * for the whole page: it goes into the scratch buffer and is copied across
 * from there, which is what stops it appearing a strip at a time. Otherwise
 * the nine parts go into the nine boxes -- the first eight through
 * H'212D8A, and the ninth placed by its own width and height so that its
 * bottom right corner lands at H'0112, H'00EA.
 *
 * The two halves are written out in full in the original, one per table. */
static u32 help_page_part(u8 module, u16 rec, u16 part)
{
    return module ? help_picture_module(rec, part) : help_picture(rec, part);
}

void help_page_draw(void)
{
    const u8  module = (u8)(CONFIG_BLOCK == 0xAA);
    const u16 rec = (u16)((u16)((module ? 9 : 10) * (u16)REG8(0x0011B0FEUL))
                          + (u16)REG8(0x0011B0FFUL));
    short i;

    if (help_page_part(module, rec, 0) != 0 &&
        help_page_part(module, rec, 1) == 0) {
        const u32 p = help_page_part(module, rec, 0);

        bitmap_draw(0x0004, 0x0027, 0x0114, 0x00EC,
                    (const u8 *)p, LCD_SCRATCH);
        region_copy(0x0004, 0x0027, 0x0114, 0x00EC, 0x0027,
                    LCD_SCRATCH, LCD_FRAME_A);
        return;
    }

    for (i = 0; i <= 0x0008; i++) {
        const u32 p = help_page_part(module, rec, (u16)i);

        if (p == 0) continue;

        if (i < 0x0008) {
            hitbox_blit((u16)(i + 1), LCD_FRAME_A, p);
        } else {
            const u16 y0 = (u16)(0x00EA - header_word_1((const u8 *)p));
            const u16 x0 = (u16)(0x0112 - header_word_0((const u8 *)p));

            bitmap_draw(x0, y0, 0x0112, 0x00EA, (const u8 *)p, LCD_FRAME_A);
        }
    }
}

/* H'21C846. The beep settings: eight of them, each a pair of bytes -- on or
 * off, and which of three patterns -- copied out of flash into H'11B368 to
 * be edited and written back on accept.
 *
 * Boxes 1 to 8 pick a setting: the pressed one lights, the last one goes
 * out, H'11A1B6 is pointed at its pair, and boxes 9 and H'0A show the on/off
 * state while box H'0B shows the pattern as one of three thirds. Boxes 9 and
 * H'0A set it, H'0B steps the pattern round 1-2-3, H'19 writes the block to
 * flash and H'1A throws it away.
 *
 * On the way in box 5 is greyed on the machine with the module -- that
 * setting means nothing there -- and the whole run is put back to state 0
 * first. Box H'0B does not check H'11A1B6 before following it, unlike boxes
 * 9 and H'0A, which do. */
u8 beep_settings_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        hitbox_set_state(0x0001, 0x000D, 0x00, 0);
        if (CONFIG_BLOCK == 0xAA) hitbox_set_state(0x0005, 0x0005, 0x02, 0);
        REG16(0x0011B366UL) = 0x0000;
        mem_copy((u8 *)0x0011B368UL, (const u8 *)0x0057EFC8UL, 0x0012);
        return 0x00;
    }

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return 0x00;

    if ((short)index <= 0x0008 && REG16(0x0011B366UL) != index) {
        const u16 was = REG16(0x0011B366UL);
        u32 p;

        hitbox_set_state(index, index, 0x01, 0);
        hitbox_set_state(was, was, 0x00, 0);

        p = 0x0011B368UL + (u32)(long)(short)(u16)((u16)(index << 1));
        REG32(0x0011A1B6UL) = p;

        if (REG8(p) != 0) {
            hitbox_set_state(0x0009, 0x0009, 0x01, 0);
            hitbox_set_state(0x000A, 0x000A, 0x00, 0);
        } else {
            hitbox_set_state(0x0009, 0x0009, 0x00, 0);
            hitbox_set_state(0x000A, 0x000A, 0x01, 0);
        }

        hitbox_third_mark(0x000B, REG8(REG32(0x0011A1B6UL) + 1));
        REG16(0x0011B366UL) = index;
    }

    if (value == 0x0009) {
        const u32 p = REG32(0x0011A1B6UL);

        if (p != 0) {
            REG8(p) = 0x01;
            hitbox_set_state(0x0009, 0x0009, 0x01, 0);
            hitbox_set_state(0x000A, 0x000A, 0x00, 0);
        }
        return 0x00;
    }

    if (value == 0x000A) {
        const u32 p = REG32(0x0011A1B6UL);

        if (p != 0) {
            REG8(p) = 0x00;
            hitbox_set_state(0x0009, 0x0009, 0x00, 0);
            hitbox_set_state(0x000A, 0x000A, 0x01, 0);
        }
        return 0x00;
    }

    if (value == 0x000B) {
        REG8(REG32(0x0011A1B6UL) + 1) =
            (u8)(REG8(REG32(0x0011A1B6UL) + 1) + 1);
        if (REG8(REG32(0x0011A1B6UL) + 1) > 0x03) {
            REG8(REG32(0x0011A1B6UL) + 1) = 0x01;
        }
        hitbox_third_mark(0x000B, REG8(REG32(0x0011A1B6UL) + 1));
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0x0011B368UL, 0x0057EFC8UL, 0x12);
        FLASH_BUSY &= (u8)~0x20;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'221B6E. The pattern strip put back on the screen when one of seven
 * screens is returned to.
 *
 * Four shapes between them, and all four end the same way: the cursor put
 * out, the screen made current, its leave hook run with force, and the two
 * "settling" bytes raised. Anything but those seven screens does nothing at
 * all -- not even the tail.
 *
 * The strip itself is the same four rectangles every time, only the width of
 * the top one moving; what changes is which run of boxes carries the list,
 * which run is handed over with state 4, and where the two arrows are. The
 * H'07 and H'34 shapes also slide a run of boxes along first, which is what
 * makes room for the wider strip. */
void pattern_strip_restore(u8 screen)
{
    if (screen == 0x07 || screen == 0x45) {
        hitbox_set_state(0x001F, 0x0020, 0x00, 0);
        hitbox_set_state(0x000B, 0x000F, 0x04, 0);
        hitbox_run_shift(0x000B, 0x000F, 0x0021);
        hitbox_set_state(0x0021, 0x0025, 0x03, 0);
        draw_rect(0x0006, 0x009D, 0x00DD, 0x009E, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x0029, 0x009F, 0x002C, 0x00C1, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x001A, 0x0020, 0x0001, 0x00115A10UL);
        hitbox_set_state(0x0012, 0x0015, 0x04, 0);
        picker_arrows(0x001E, 0x001F, 0x01);
    } else if (screen == 0x34 || screen == 0x36) {
        hitbox_set_state(0x001A, 0x001B, 0x00, 0);
        hitbox_set_state(0x0007, 0x0008, 0x04, 0);
        hitbox_run_shift(0x0007, 0x0008, 0x0013);
        hitbox_set_state(0x0013, 0x0014, 0x03, 0);
        draw_rect(0x0006, 0x009D, 0x00E8, 0x009E, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x0029, 0x009F, 0x002C, 0x00C1, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x0015, 0x001B, 0x0001, 0x00115A10UL);
        hitbox_set_state(0x000F, 0x0012, 0x04, 0);
        picker_arrows(0x0019, 0x001A, 0x01);
    } else if (screen == 0x04) {
        hitbox_set_state(0x0024, 0x0025, 0x00, 0);
        hitbox_set_state(0x0010, 0x0014, 0x04, 0);
        draw_rect(0x0006, 0x009D, 0x00E8, 0x009E, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x0029, 0x009F, 0x002C, 0x00C1, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x001F, 0x0025, 0x0001, 0x00115A10UL);
        hitbox_set_state(0x001B, 0x001E, 0x04, 0);
        picker_arrows(0x0023, 0x0024, 0x01);
    } else if (screen == 0x47) {
        draw_rect(0x0004, 0x009D, 0x0114, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0004, 0x00C6, 0x013B, 0x00EC, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x000E, 0x0012, 0x0001, 0x00119362UL);
    } else {
        return;
    }

    picker_cursor(0x01);
    REG8(0x0011A169UL) = screen;
    screen_leave(screen, 0x01);
    REG8(0x0011A174UL) = 0x01;
    REG8(0x0011A17BUL) = 0x01;
}

/* H'2189A6 and H'218ADE. The two screens that step the stroke number in
 * H'FFFEFD, one per stroke table: H'2189A6 draws from the first and wraps
 * at three, H'218ADE from the second and wraps at H'0F.
 *
 * Entering either calls H'218780 twice, once as a press and once as a
 * release, which is what puts the two boxes and the stroke up to start with;
 * every later pass calls it once, as a release. Box H'6C steps up and H'6B
 * steps down, and each rubs the old stroke out before drawing the new one.
 * A press on a greyed box is dropped before the message, so a screen whose
 * pattern is in the wrong category says nothing at all. */
static u8 stroke_pick_screen(u8 first_pass, u8 variant, u8 top)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) stitch_stroke_toggle(variant, 0x01);
    stitch_stroke_toggle(variant, 0x00);

    if (touch_hit(0x001C, 0x001D, &value, &index) != 0x03) return 0x00;
    if (hitbox_kind(index) == 0x05) return 0x00;
    message_show_held(index);

    if (value == 0x006C) {
        preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x00);
        if (REG8(0x00FFFEFDUL) < top) {
            const u8 v = (u8)(REG8(0x00FFFEFDUL) + 1);

            REG8(0x00FFFEFDUL) = v;
            preview_stroke_draw((u16)v, variant, 0x01);
        } else {
            REG8(0x00FFFEFDUL) = 0x00;
            preview_stroke_draw(0x0000, variant, 0x01);
        }
        return 0x00;
    }

    if (value == 0x006B) {
        preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x00);
        if (REG8(0x00FFFEFDUL) != 0) {
            const u8 v = (u8)(REG8(0x00FFFEFDUL) - 1);

            REG8(0x00FFFEFDUL) = v;
            preview_stroke_draw((u16)v, variant, 0x01);
        } else {
            REG8(0x00FFFEFDUL) = top;
            preview_stroke_draw((u16)top, variant, 0x01);
        }
    }
    return 0x00;
}

u8 stroke_pick_screen_a(u8 first_pass)
{
    return stroke_pick_screen(first_pass, 0x01, 0x03);
}

u8 stroke_pick_screen_b(u8 first_pass)
{
    return stroke_pick_screen(first_pass, 0x00, 0x0F);
}

/* ---- the hoop offsets ---------------------------------------------------
 * H'248FF0 and the nine handlers behind H'230D4E. The screen shows how far
 * the hoop has been moved from where the module thinks it is, in millimetres
 * each way, and lets it be nudged.
 *
 * H'104C7A and H'104C7B are the two offsets in half-millimetre steps, kept
 * as signed bytes. H'248FF0 draws them: each is halved, its sign thrown
 * away, turned into decimal and given "mm", and the two strings go into the
 * two labels down the left edge.
 *
 * The original writes the two halves of that out twice over, once for a
 * negative reading and once for a non-negative one, and the negative half
 * runs the quotient through H'24ADC8 to take the sign off. Both are
 * reproduced: for a non-negative reading the two agree anyway, so the
 * duplication costs nothing but the instructions it is made of.
 */
static void hoop_offset_label(signed char raw, void (*draw)(const char *))
{
    char text[6];
    char suffix[3];

    suffix[0] = 0x6D;   /* "mm" */
    suffix[1] = 0x6D;
    suffix[2] = 0x00;

    if (raw < 0) {
        const short q = (short)((short)raw / 2);

        int_to_decimal((short)(signed char)abs_short((short)(signed char)q),
                       text);
    } else {
        const short q = (short)((short)raw / 2);

        int_to_decimal((short)(signed char)q, text);
    }

    str_append(text, suffix);
    draw(text);
}

void hoop_offsets_draw(void)
{
    hoop_offset_label((signed char)REG8(0x00104C7AUL), text_left_D9);
    hoop_offset_label((signed char)REG8(0x00104C7BUL), text_left_BC);
}

/* H'248AC6 and the seven below it: one nudge of the hoop, one per direction.
 *
 * All eight are the same routine with two constants changed -- the direction
 * code the module is sent in H'11A615, and which of the two offsets moves --
 * and all eight refuse in the same three ways: the link never went quiet, or
 * either offset is already further than H'64 from home. The limit is tested
 * against the offset *before* the step, so a nudge that would take it past
 * H'64 is allowed and the next one is not.
 *
 * The direction codes run clockwise from north: 1 N, 2 NE, 3 E, 4 SE, 5 S,
 * 6 SW, 7 W, 8 NW. North is H'104C7B going down, so the second offset counts
 * away from the operator.
 */
static void hoop_nudge(u8 code, signed char dx, signed char dy)
{
    if (link_wait_idle() == 0) return;
    if (abs_short((short)(signed char)REG8(0x00104C7AUL)) > 0x0064) return;
    if (abs_short((short)(signed char)REG8(0x00104C7BUL)) > 0x0064) return;

    REG8(0x0011F2A1UL)  = 0x03;
    REG8(0x0011A615UL)  = code;
    REG8(0x0011F2A2UL) |= 0x08;
    link_send_start();

    if (dx != 0) REG8(0x00104C7AUL) = (u8)(REG8(0x00104C7AUL) + dx);
    if (dy != 0) REG8(0x00104C7BUL) = (u8)(REG8(0x00104C7BUL) + dy);

    hoop_offsets_draw();
}

/* H'248E5C. The hoop sent home: message H'0D, two seconds to let it get
 * there, the slot's two stored positions zeroed and then message H'02. Each
 * step waits for the link to go quiet first, and the last wait's answer is
 * thrown away -- there is nothing left to do with it either way. */
static void hoop_reset(void)
{
    REG8(0x00114D8EUL) = 0x00;
    if (link_wait_idle() == 0) return;

    REG8(0x0011F2A1UL) = 0x0D;
    REG8(0x0011F2A2UL) = 0x01;
    link_send_start();
    link_delay(0x07D0);
    if (link_wait_idle() == 0) return;

    REG16(PAT_A(0x0C)) = 0x0000;
    REG16(PAT_A(0x0E)) = 0x0000;
    REG8(0x0011F2A1UL) = 0x02;
    link_send_start();
    (void)link_wait_idle();
}

/* H'230D4E. The nine boxes that move the hoop: eight directions and home.
 *
 * The eight nudges return 0 for the two that only move north and 1 for the
 * rest, which is in the original and looks like nothing more than the order
 * the handlers were written in. Home also leaves for screen H'1F. */
u8 hoop_move_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0009, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    switch ((u16)(value - 1)) {
    case 0x0000: hoop_nudge(0x08, -1, -1); return 0x00;
    case 0x0001: hoop_nudge(0x01,  0, -1); return 0x00;
    case 0x0002: hoop_nudge(0x02,  1, -1); return 0x01;
    case 0x0003: hoop_nudge(0x07, -1,  0); return 0x01;
    case 0x0004: hoop_nudge(0x03,  1,  0); return 0x01;
    case 0x0005: hoop_nudge(0x06, -1,  1); return 0x01;
    case 0x0006: hoop_nudge(0x05,  0,  1); return 0x01;
    case 0x0007: hoop_nudge(0x04,  1,  1); return 0x01;
    case 0x0008:
        hoop_reset();
        screen_switch(0x1F, 0x01, 0x00);
        return 0x01;
    default:
        break;
    }
    return 0x00;
}

/* H'217F04. Ten categories down one menu, and an eleventh box that only
 * takes the screen change.
 *
 * The same shape as H'217DE0 further up, but with the ten handlers written
 * out one per box behind a jump table rather than as a chain of compares.
 * Each is the same four instructions with one constant changed, and the
 * constants are the categories H'05 to H'0E in box order, so the arithmetic
 * would collapse to "value plus four" -- it is left as the table it is,
 * because that is what the original holds and a category out of place should
 * be a difference rather than an off-by-one nobody notices.
 *
 * The message and the screen change both happen before the table is reached,
 * so a value the table does not cover still leaves for screen H'02. */
u8 menu_ten_categories(void)
{
    u16 value = 0, index = 0;
    u8  wanted;

    if (touch_hit(0x0001, 0x000A, &value, &index) != 0x03) return 0x00;

    message_show_held(index);
    screen_switch(0x02, 0x01, 0x00);

    switch (value) {
    case 0x0001: wanted = 0x05; break;
    case 0x0002: wanted = 0x06; break;
    case 0x0003: wanted = 0x07; break;
    case 0x0004: wanted = 0x08; break;
    case 0x0005: wanted = 0x09; break;
    case 0x0006: wanted = 0x0A; break;
    case 0x0007: wanted = 0x0B; break;
    case 0x0008: wanted = 0x0C; break;
    case 0x0009: wanted = 0x0D; break;
    case 0x000A: wanted = 0x0E; break;
    case 0x000B: return 0x01;   /* nothing to pick: the screen change is all */
    default:     return 0x00;
    }

    REG16(0x0011B108UL) = first_item_of_category(wanted, 0x0011A88EUL);
    return 0x01;
}

/* ---- the four "pick one of a list" screens ------------------------------
 * H'21A320, H'21A56C, H'21A7F6 and H'21AA42. Four screens with the same
 * skeleton and different handlers hanging off it: on the first pass the
 * screen is pushed and the remembered box cleared, and every pass runs the
 * hit test, lights the box that was pressed, puts the last one out, and then
 * jumps into a table on the box value.
 *
 * The remembered box lives at H'11B32A, H'11B330, H'11B336 and H'11B33C --
 * six bytes apart, one per screen, in the same order as the routines.
 *
 * They differ in more than their handlers: which values are left out of the
 * lighting (two of them name H'04 and their last box, one takes everything
 * below H'0B), and what the table is indexed by (three by value less one,
 * H'21A320 by value less two). Each is written out with its own guard rather
 * than folded into one parameterised routine, because those differences are
 * exactly the sort a shared skeleton would hide.
 */
static void pick_box_light(u32 state, u16 value)
{
    const u16 was = REG16(state);

    hitbox_set_state(was, was, 0x00, 0);
    hitbox_set_state(value, value, 0x01, 0);
    REG16(state) = value;
}

static void pick_take(u32 held, u16 offset)
{
    REG32(held) = REG32(REG32(0x0011B2AAUL) + (u32)offset);
}

u8 pick_screen_4(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK4_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x0008, &value, &index) != 0x03) return 0x00;

    if (value != 0x0004 && value != 0x0008 && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK4_STATE, value);
    }

    if ((u16)(value - 1) > 0x0007) return 0x00;

    switch (value) {
    case 0x0001: pick_take(PICK4_HELD, 0x60); break;
    case 0x0002: pick_take(PICK4_HELD, 0x64); break;
    case 0x0003: pick_take(PICK4_HELD, 0x68); break;
    case 0x0005: pick_take(PICK4_HELD, 0x6C); break;
    case 0x0006: pick_take(PICK4_HELD, 0x70); break;
    case 0x0007: pick_take(PICK4_HELD, 0x74); break;

    case 0x0004:
        if (REG16(PICK4_STATE) == 0) break;
        message_show_held(index);
        REG32(0x001161B8UL) = REG32(PICK4_HELD);
        screen_remember(0x01);
        screen_switch(0x3A, 0x01, 0x00);
        break;

    case 0x0008:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x39, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

u8 pick_screen_3(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK3_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x000B, &value, &index) != 0x03) return 0x00;

    if (value != 0x0004 && value != 0x000B && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK3_STATE, value);
    }

    if ((u16)(value - 1) > 0x000A) return 0x00;

    switch (value) {
    case 0x0001: pick_take(PICK3_HELD, 0x38); break;
    case 0x0002: pick_take(PICK3_HELD, 0x3C); break;
    case 0x0003: pick_take(PICK3_HELD, 0x40); break;
    case 0x0005: pick_take(PICK3_HELD, 0x44); break;
    case 0x0006: pick_take(PICK3_HELD, 0x48); break;
    case 0x0007: pick_take(PICK3_HELD, 0x4C); break;
    case 0x0008: pick_take(PICK3_HELD, 0x50); break;
    case 0x0009: pick_take(PICK3_HELD, 0x54); break;
    case 0x000A: pick_take(PICK3_HELD, 0x58); break;

    case 0x0004:
        if (REG16(PICK3_STATE) == 0) break;
        message_show_held(index);
        REG32(0x001161B8UL) = REG32(PICK3_HELD);
        screen_remember(0x01);
        screen_switch(0x3A, 0x01, 0x00);
        break;

    case 0x000B:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x39, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

u8 pick_screen_2(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK2_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x000C, &value, &index) != 0x03) return 0x00;

    if ((short)value < 0x000B && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK2_STATE, value);
    }

    if ((u16)(value - 1) > 0x000B) return 0x00;

    switch (value) {
    case 0x0001: pick_take(PICK2_HELD, 0x04); break;
    case 0x0002: pick_take(PICK2_HELD, 0x08); break;
    case 0x0003: pick_take(PICK2_HELD, 0x0C); break;
    case 0x0004: pick_take(PICK2_HELD, 0x10); break;
    case 0x0005: pick_take(PICK2_HELD, 0x14); break;
    case 0x0006: pick_take(PICK2_HELD, 0x18); break;
    case 0x0007: pick_take(PICK2_HELD, 0x1C); break;
    case 0x0008: pick_take(PICK2_HELD, 0x20); break;
    case 0x0009: pick_take(PICK2_HELD, 0x24); break;

    case 0x000A:
        if (CONFIG_BLOCK == 0xAA) pick_take(PICK2_HELD, 0x2C);
        else                      pick_take(PICK2_HELD, 0x28);
        break;

    case 0x000B:
        if (REG16(PICK2_STATE) == 0) break;
        message_show_held(index);
        REG32(0x001161B8UL) = REG32(PICK2_HELD);
        screen_remember(0x01);
        screen_switch(0x3A, 0x01, 0x00);
        break;

    case 0x000C:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x39, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

u8 pick_screen_1(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK1_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x000B, &value, &index) != 0x03) return 0x00;

    if (value != 0x0004 && value != 0x000B && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK1_STATE, value);
    }

    if ((u16)(value - 2) > 0x0009) return 0x00;

    switch (value) {
    case 0x0002: pick_take(PICK1_HELD, 0x30); break;
    case 0x0003: pick_take(PICK1_HELD, 0x34); break;
    case 0x0006: pick_take(PICK1_HELD, 0x5C); break;
    case 0x0008: pick_take(PICK1_HELD, 0x78); break;
    case 0x0009: pick_take(PICK1_HELD, 0x7C); break;
    case 0x000A: pick_take(PICK1_HELD, 0x80); break;

    case 0x0005:                /* in the table, and doing nothing */
    case 0x0007:
        break;

    case 0x0004: {
        const u16 held = REG16(PICK1_STATE);

        message_show_held(index);
        if      (held == 0x0001) screen_switch(0x3B, 0x01, 0x00);
        else if (held == 0x0005) screen_switch(0x3C, 0x01, 0x00);
        else if (held == 0x0007) screen_switch(0x3D, 0x01, 0x00);
        else if (held != 0x0000) {
            REG32(0x001161B8UL) = REG32(PICK1_HELD);
            screen_switch(0x3A, 0x01, 0x00);
        }
        break;
    }

    case 0x000B:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x0E, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'21B3DE. The main menu: fifteen boxes, most of them a screen change.
 *
 * Three things happen before the hit test. Two keys held together with a
 * third input low clears the pattern queue out to flash and says so -- the
 * only place in the application that offers it -- and leaving for screen
 * H'77 clears the panel code. Both run on every pass.
 *
 * The panel code is set from the low byte of the box value before the table
 * is reached, so it is set even for a value the table does not cover.
 *
 * Most handlers are the same three instructions -- put the message up, go to
 * a screen -- but the two that lead to H'1F and H'22 pass 0 for "remember"
 * where the rest pass 1, and boxes 3, 4, 9 and 15 put the message up and do
 * nothing else. Box H'0C is the odd one: no message at all, and it toggles
 * bit 7 of H'FFFEC1 with the box's own light.
 *
 * The H'02 return from the hit test -- a release -- is what puts box H'0C
 * back down again. */
static void main_menu_goto(u16 index, u8 screen, u8 remember)
{
    message_show_held(index);
    screen_switch(screen, 0x01, remember);
}

u8 main_menu_screen(void)
{
    u16 value = 0, index = 0;
    u8  hit;

    if ((REG8(0x00FFFEDBUL) & 0x04) && (REG8(0x00FFFEDCUL) & 0x04) &&
        (REG8(0x00FFFEDDUL) & 0x10)) {
        sew_picture_box();
        FLASH_BUSY |= 0x20;
        queue_clear_to_flash();
        FLASH_BUSY &= (u8)~0x20;
        message_show(0x001D);
        REG8(0x0011A17DUL) = 0x01;
    }

    if (screen_leave_check(&value, 0x00) == 0x03 && value == 0x0077) {
        REG8(0x00FFFEC5UL) = 0x00;
    }

    hit = touch_hit(0x0001, 0x000F, &value, &index);

    if (hit != 0x03) {
        if (hit == 0x02 && hitbox_kind(0x000C) == 0x01) {
            hitbox_set_state(0x000C, 0x000C, 0x00, 0);
            MACHINE_FLAGS &= (u8)~0x80;
        }
        return 0x00;
    }

    REG8(0x00FFFEC5UL) = (u8)value;

    if ((u16)(value - 1) > 0x000E) return 0x00;

    switch (value) {
    case 0x0001: main_menu_goto(index, 0x18, 0x01); break;
    case 0x0002: main_menu_goto(index, 0x19, 0x01); break;
    case 0x0005: main_menu_goto(index, 0x1D, 0x01); break;
    case 0x0006: main_menu_goto(index, 0x00, 0x01); break;
    case 0x0007: main_menu_goto(index, 0x1C, 0x01); break;
    case 0x0008: main_menu_goto(index, 0x1A, 0x01); break;
    case 0x000A: main_menu_goto(index, 0x1B, 0x01); break;
    case 0x000B: main_menu_goto(index, 0x1E, 0x01); break;

    /* these two do not remember the screen they are leaving */
    case 0x000D: main_menu_goto(index, 0x1F, 0x00); break;
    case 0x000E: main_menu_goto(index, 0x22, 0x00); break;

    case 0x0003:
    case 0x0004:
    case 0x0009:
    case 0x000F:
        message_show_held(index);
        break;

    case 0x000C:
        if (hitbox_kind(0x000C) != 0) break;
        hitbox_set_state(0x000C, 0x000C, 0x01, 0);
        /* bit 7 cleared and set again, which is what the original does */
        MACHINE_FLAGS = (u8)((MACHINE_FLAGS & 0x7F) | 0x80);
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'216DE0. The three "are you sure" screens, put up the same way H'216D6C
 * puts up a message: held off while one is already showing, while the splash
 * is up, while a screen change is still settling, and while the machine is
 * sewing.
 *
 * The first two remember the screen being left in slot 4 so they can go back
 * to it; the third does not, and is the only one that cannot be answered. */
void dialog_show(u16 which)
{
    if (REG8(0x0011A179UL) == 0) return;
    if (REG8(0x0011A173UL) != 0) return;
    if (REG8(0x0011B0A8UL) != 0) return;
    if (REG8(0x00114DC6UL) & 0x80) return;

    REG8(0x0011A179UL) = 0x00;

    if (which == 0x0001) {
        screen_remember(0x04);
        screen_switch(0x48, 0x01, 0x00);
    } else if (which == 0x0002) {
        screen_remember(0x04);
        screen_switch(0x49, 0x01, 0x00);
    } else if (which == 0x0003) {
        screen_switch(0x4D, 0x01, 0x00);
    }
}

/* H'21C19C. The settings menu: thirteen boxes, ten of them a screen change.
 *
 * H'11A17A is the "something was changed" flag: finding it set on the way in
 * writes the whole settings block out to flash, says so, and raises
 * H'11A1B4 so that box H'0D knows to go back to the screen the change came
 * from rather than to the remembered one.
 *
 * Box H'0C is the odd one -- it has no screen number of its own, so the one
 * left in the register from the slot argument is used, which is screen 1.
 * That is in the original and is reproduced; it looks like an omission
 * rather than a choice, but it is what the machine does. */
static void settings_menu_goto(u8 screen)
{
    screen_switch(screen, 0x01, 0x00);
}

u8 settings_menu_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011A17AUL) != 0) {
        sew_picture_box();
        settings_save(0x00);
        REG8(0x0011A17AUL) = 0x00;
        message_show(0x001D);
        REG8(0x0011A17DUL) = 0x01;
        REG8(0x0011A1B4UL) = 0x01;
    }

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if ((u16)(value - 1) > 0x000C) return 0x00;

    switch (value) {
    case 0x0001:
        settings_menu_goto(0x44);
        REG8(0x0011A178UL) = 0x01;
        REG8(0x0011A1B4UL) = 0x01;
        break;

    case 0x0002: settings_menu_goto(0x2E); break;
    case 0x0003: settings_menu_goto(0x2B); break;
    case 0x0004: settings_menu_goto(0x29); break;
    case 0x0005: settings_menu_goto(0x2A); break;
    case 0x0006: settings_menu_goto(0x2C); break;
    case 0x0007: settings_menu_goto(0x2F); break;
    case 0x0009: settings_menu_goto(0x28); break;
    case 0x000A: settings_menu_goto(0x4A); break;
    case 0x000C: settings_menu_goto(0x01); break;   /* see above */

    case 0x0008: dialog_show(0x0001); break;

    case 0x000B: break;             /* in the table, and doing nothing */

    case 0x000D:
        screen_stack_pop();
        screen_mark_repaint(REG8(0x0011A16CUL));
        if (REG8(0x0011A1B4UL) != 0) {
            screen_switch(REG8(0x0011A16CUL), 0x01, 0x00);
            REG16(0x0011B108UL) = REG16(0x0011B116UL);
            REG16(0x0011B10AUL) = list_page_start();
            REG8(0x0011A1B4UL) = 0x00;
        } else {
            screen_from_slot(0x03);
        }
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'214D24. The little picture at the top left of the screen, and the "F"
 * beside it.
 *
 * A null picture means the box is cleared instead of blitted. The letter is
 * a one-character string in the application's own image at H'250ADD, drawn
 * right-aligned through the small font at H'119DE6 -- one of the few places
 * anything reads a string constant out of the code region. */
void module_letter_box(u32 picture)
{
    if (picture == 0) {
        draw_rect(0x0048, 0x0002, 0x006A, 0x0024, LCD_FRAME_A, 0x00, 0x01);
    } else {
        bitmap_draw(0x0048, 0x0002, 0x006A, 0x0024,
                    (const u8 *)picture, LCD_FRAME_A);
    }

    text_draw((const char *)0x00250ADDUL, 0x0049, 0x001E, 0x004C, 0x0023,
              0x0001, 0x00, (const u8 *)0x00119DE6UL);
}

/* ---- the panel fields ---------------------------------------------------
 * H'214990. Fourteen little readouts along the top of the screen, each one
 * a few bits of a port latched into its own word at H'11B2FC upwards. A
 * field is only redrawn when its bits have changed since last time, which is
 * what the fourteen words are for.
 *
 * Ten of them keep the value and draw nothing -- the drawing is somebody
 * else's job and the word is only there to be compared against. The other
 * four put a picture in the box at the top left: three index a table of
 * their own by the value, and the fourth picks between two fixed pictures by
 * whether the bit is set at all.
 *
 * Called with [fresh] set, it puts all fourteen words to H'FFFF so that the
 * next pass redraws everything, and draws the picture for [which] out of the
 * icon table instead of doing any of the above.
 */
static void panel_field_plain(u16 v, u32 slot)
{
    if (REG16(slot) != v) REG16(slot) = v;
}

static void panel_field_pic(u16 v, u32 slot, u32 table)
{
    if (REG16(slot) == v) return;
    module_letter_box(REG32(table + (u32)(long)(short)(u16)((u16)(v << 2))));
    REG16(slot) = v;
}

void panel_field_update(u16 which, u8 fresh)
{
    if (fresh != 0) {
        REG16(0x0011B2FCUL) = 0xFFFF;
        REG16(0x0011B2FEUL) = 0xFFFF;
        REG16(0x0011B300UL) = 0xFFFF;
        REG16(0x0011B302UL) = 0xFFFF;
        REG16(0x0011B304UL) = 0xFFFF;
        REG16(0x0011B306UL) = 0xFFFF;
        REG16(0x0011B308UL) = 0xFFFF;
        REG16(0x0011B30AUL) = 0xFFFF;
        REG16(0x0011B30CUL) = 0xFFFF;
        REG16(0x0011B30EUL) = 0xFFFF;
        REG16(0x0011B310UL) = 0xFFFF;
        REG16(0x0011B312UL) = 0xFFFF;
        REG16(0x0011B314UL) = 0xFFFF;
        REG16(0x0011B316UL) = 0xFFFF;

        if (which != 0) {
            module_letter_box(REG32(0x001158CEUL +
                (u32)(long)(short)(u16)((u16)(which << 2))));
        } else {
            module_letter_box(0);
        }
        return;
    }

    if (which == 0) return;

    switch (which) {
    case 0x0001:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x40), 0x0011B304UL);
        break;
    case 0x0002:
        panel_field_plain((u16)(REG8(0x00FFFEF6UL) & 0x10), 0x0011B306UL);
        break;
    case 0x0003:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x10), 0x0011B308UL);
        break;
    case 0x0004:
        panel_field_pic((u16)(REG8(0x00FFFEF9UL) & 0x0F),
                        0x0011B302UL, 0x00115852UL);
        break;
    case 0x0005:
        panel_field_plain((u16)(REG8(0x00FFFEF6UL) & 0x40), 0x0011B300UL);
        break;
    case 0x0006:
        panel_field_plain((u16)(REG8(0x00FFFEF6UL) & 0x80), 0x0011B30AUL);
        break;
    case 0x0007:
        panel_field_pic((u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F),
                        0x0011B30CUL, 0x0011587AUL);
        break;
    case 0x0008:
        panel_field_pic((u16)(REG8(0x00FFFEF6UL) & 0x0F),
                        0x0011B2FEUL, 0x0011582EUL);
        break;
    case 0x0009:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x08), 0x0011B30EUL);
        break;
    case 0x000A:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x80), 0x0011B310UL);
        break;
    case 0x000C:
        panel_field_pic((u16)(REG8(0x00FFFEF5UL) & 0x03),
                        0x0011B2FCUL, 0x0011581EUL);
        break;

    case 0x0044: {
        const u16 v = (u16)(REG8(0x00FFFEF5UL) & 0x04);

        if (REG16(0x0011B312UL) != v) {
            module_letter_box(v != 0 ? 0x0034E55CUL : 0x0034E5EDUL);
            REG16(0x0011B312UL) = v;
        }
        break;
    }

    case 0x0046:
        panel_field_pic((u16)REG8(0x00FFFEFDUL),
                        0x0011B314UL, 0x0011589AUL);
        break;
    case 0x0047:
        panel_field_plain((u16)(REG8(0x00FFFEF8UL) & 0x01), 0x0011B316UL);
        break;

    default:
        break;
    }
}

/* ---- the panel switches -------------------------------------------------
 * H'214DD4. Twenty fields of the machine's own state, reached by key, and
 * three things that can be done to each: cleared, stepped on, or read.
 *
 * [clear] wins over [step], and with neither the current value comes back --
 * so the same call reads the field or changes it depending on two flags. A
 * key the table does not know answers H'FFFF, and so does key H'14: its
 * table entry points straight at the "not found" tail, which is how a key
 * that exists is made to look like one that does not.
 *
 * Six of the twenty are a single bit toggled in place. The rest each have
 * their own arithmetic -- a count that wraps at nine, one that wraps at
 * three, one that runs 2,3,4,5 and back to 0 with the configuration byte
 * deciding where it wraps, and so on -- and those are written out one by
 * one because that is what they are.
 *
 * [box] is the second argument: a message number to most of them and a box
 * index to key H'47, which asks whether the box is greyed before it does
 * anything.
 */
static u16 panel_bit(u32 port, u8 mask, u8 step, u8 clear)
{
    if (clear != 0) {
        REG8(port) &= (u8)~mask;
        return 0x0000;
    }
    if (step != 0) {
        REG8(port) ^= mask;
        hold_start(0x0064);
        return 0x0000;
    }
    return (u16)(REG8(port) & mask);
}

u16 panel_switch(u8 key, u16 box, u8 step, u8 clear)
{
    switch (key) {
    case 0x01:
        if (step != 0) {
            REG8(0x00FFFEF5UL) ^= 0x40;
            hold_start(0x0064);
        }
        return 0x0000;

    case 0x02: return panel_bit(0x00FFFEF6UL, 0x10, step, clear);
    case 0x03: return panel_bit(0x00FFFEF5UL, 0x10, step, clear);
    case 0x05: return panel_bit(0x00FFFEF6UL, 0x40, step, clear);
    case 0x06: return panel_bit(0x00FFFEF6UL, 0x80, step, clear);
    case 0x09: return panel_bit(0x00FFFEF5UL, 0x08, step, clear);
    case 0x0A: return panel_bit(0x00FFFEF5UL, 0x80, step, clear);

    case 0x04:
        if (clear != 0) {
            REG8(0x00FFFEF9UL) &= 0xF0;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)(REG8(0x00FFFEF9UL) & 0x0F);
            v = (u16)((v == 0x0009) ? 0x0000 : (u16)(v + 1));
            REG8(0x00FFFEF9UL) =
                (u8)((REG8(0x00FFFEF9UL) & 0xF0) | (u8)v);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF9UL) & 0x0F);

    case 0x07:
        if (clear != 0) {
            REG8(0x00FFFEF9UL) &= 0x0F;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F);
            if      (v == 0x0005) v = 0x0000;
            else if (v == 0x0000) v = 0x0002;
            else                  v = (u16)(v + 1);
            REG8(0x00FFFEF9UL) =
                (u8)((u8)((u8)v << 4) | (u8)(REG8(0x00FFFEF9UL) & 0x0F));
            return 0x0000;
        }
        return (u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F);

    case 0x08:
        if (clear != 0) {
            REG8(0x00FFFEF6UL) &= 0xF0;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)(REG8(0x00FFFEF6UL) & 0x0F);
            if      (v == 0x0008 && CONFIG_BLOCK == 0xB4) v = 0x0000;
            else if (v == 0x0005 && CONFIG_BLOCK == 0xAA) v = 0x0000;
            else if ((short)v >= 0x0002)                  v = (u16)(v + 1);
            else                                          v = 0x0002;
            REG8(0x00FFFEF6UL) =
                (u8)((REG8(0x00FFFEF6UL) & 0xF0) | (u8)v);
            return 0x0000;
        }
        if ((u8)(REG8(0x00FFFEF6UL) & 0x0F) < 0x02) return 0x0000;
        return (u16)(REG8(0x00FFFEF6UL) & 0x0F);

    case 0x0C:
        if (clear != 0) {
            REG8(0x00FFFEF5UL) &= 0xFC;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)(REG8(0x00FFFEF5UL) & 0x03);
            v = (u16)((v == 0x0003) ? 0x0000 : (u16)(v + 1));
            REG8(0x00FFFEF5UL) =
                (u8)((REG8(0x00FFFEF5UL) & 0xFC) | (u8)v);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF5UL) & 0x03);

    case 0x49:
        if (clear != 0) {
            REG8(0x00FFFEF6UL) &= 0xF0;
            return 0x0000;
        }
        if (step != 0) {
            if ((u8)(REG8(0x00FFFEF6UL) & 0x0F) == 0x01) {
                REG8(0x00FFFEF6UL) &= 0xF0;
            } else {
                REG8(0x00FFFEF6UL) =
                    (u8)((REG8(0x00FFFEF6UL) & 0xF0) | 0x01);
            }
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)(((u8)(REG8(0x00FFFEF6UL) & 0x0F) == 0x01) ? 1 : 0);

    case 0x47:
        if (clear != 0) {
            REG8(0x00FFFEF8UL) &= (u8)~0x01;
            return 0x0000;
        }
        if (step != 0) {
            if (hitbox_kind(box) == 0x05) return 0x0000;
            if (REG8(0x00114DC6UL) & 0x80) return 0x0000;
            REG8(0x00FFFEF8UL) ^= 0x01;
            REG8(0x00FFFEFDUL) = (u8)(REG8(0x00FFFEF8UL) & 0x01);
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF8UL) & 0x01);

    case 0x44:
        if (clear != 0) {
            REG8(0x00FFFEF5UL) |= 0x04;      /* set, where the rest clear */
            return 0x0000;
        }
        if (step != 0) {
            REG8(0x00FFFEF5UL) ^= 0x04;
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)((REG8(0x00FFFEF5UL) & 0x04) ? 0 : 1);

    case 0x46:
        if (clear != 0) {
            REG8(0x00FFFEFDUL) = 0x00;
            return 0x0000;
        }
        if (step != 0) {
            if (REG8(0x00FFFEFDUL) == 0x02) REG8(0x00FFFEFDUL) = 0x00;
            else REG8(0x00FFFEFDUL) = (u8)(REG8(0x00FFFEFDUL) + 1);
            return 0x0000;
        }
        return (u16)((REG8(0x00FFFEFDUL) == 0x01) ? 0 : 1);

    case 0x6E:
        if (step != 0) {
            if (REG8(0x00FFFEEAUL) != 0) {
                REG8(0x00FFFEEAUL) = (u8)(REG8(0x00FFFEEAUL) - 1);
            }
            return 0x0000;
        }
        return (u16)REG8(0x00FFFEEAUL);

    case 0x6F:
        if (step != 0) {
            if (REG8(0x00FFFEEAUL) < 0x0A) {
                REG8(0x00FFFEEAUL) = (u8)(REG8(0x00FFFEEAUL) + 1);
            }
            return 0x0000;
        }
        return (u16)REG8(0x00FFFEEAUL);

    case 0x76:
        if (step != 0) {
            REG8(0x00FFFEF6UL) ^= 0x20;
            if (REG8(0x0011A169UL) != 0x18) {
                u8 k = (u8)(REG8(0x00FFFEF6UL) & 0x20);
                u8 i;

                for (i = 0; i < 3; i++) k = (u8)((u8)(k << 1) | (u8)(k >> 7));
                k = (u8)(k & 0x07);

                bitmap_draw(0x002C, 0x000B, 0x0030, 0x0019,
                            (const u8 *)REG32(0x00115892UL +
                                (u32)(long)(short)(u16)((u16)(k << 2))),
                            LCD_FRAME_A);
            }
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF6UL) & 0x20);

    case 0x7B:
        if (step != 0 && !(REG8(0x00FFFEF8UL) & 0x80)) {
            REG8(0x00FFFEF8UL) |= 0x80;
        }
        return 0x0000;

    case 0x14:                  /* a key that exists and answers "no key" */
    default:
        break;
    }
    return 0xFFFF;
}

/* H'2136A6. Whether anything in a box's list is away from its default, and
 * the little indicator box lit or put out to say so.
 *
 * The box at [box] carries a list at +H'0C -- a count and then that many
 * words, the low byte of each a panel key. Every key is read through
 * H'214DD4 and a positive answer means that field has been changed.
 *
 * Which box is the indicator depends on the screen: H'17 on five of them,
 * H'16 on two more and H'0A on four, and on any other screen the routine
 * does nothing at all.
 *
 * The walk happens only when H'11A17B says the screen has just been set up.
 * It has two halves. The first runs round the list from one past the last
 * box's value, wrapping at the count, and stops when it comes back to this
 * box's own value -- so a value outside 1..count never stops it. The second
 * walks the boxes from [box] to [last] and asks the same question of each,
 * but only counts an answer when the box has been handed over (style 4).
 *
 * H'11A1A4 carries the answer between the walk and the drawing, which is
 * why the drawing can run on a pass that did no walking. Bit 7 of H'FFFEE5
 * decides whether "something changed" lights the box or leaves it alone. */
void panel_any_set(u16 box, u16 last)
{
    const u32 entry = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    const u32 list  = REG32(entry + 0x0C);
    const u16 count = REG16(list);
    const u16 mine  = REG16(entry + 0x08);
    const u16 from  = REG16(HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * last) + 0x08);
    const u8  screen = REG8(0x0011A169UL);
    u16 mark;
    short i;

    if (screen == 0x02 || screen == 0x07 || screen == 0x18 ||
        screen == 0x30 || screen == 0x45) {
        mark = 0x0017;
    } else if (screen >= 0x03 && screen <= 0x04) {
        mark = 0x0016;
    } else if (screen >= 0x33 && screen <= 0x36) {
        mark = 0x000A;
    } else {
        return;
    }

    if (REG8(0x0011A17BUL) != 0) {
        REG8(0x0011A17BUL) = 0x00;

        for (i = (short)(from + 1); ; i++) {
            if (i > (short)count) i = 0x0001;
            if ((u16)i == mine) break;
            if ((short)panel_switch(REG8(list + (u32)(long)(short)
                    (u16)((u16)i << 1) + 1), 0x0000, 0x00, 0x00) > 0) {
                REG8(0x0011A1A4UL) = 0x01;
                goto done;
            }
        }

        for (i = (short)box; i <= (short)last; i++) {
            const u16 v = REG16(HITBOX_TABLE +
                (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i) + 0x08);

            if ((short)panel_switch(REG8(list + (u32)(long)(short)
                    (u16)((u16)(v << 1)) + 1), 0x0000, 0x00, 0x00) > 0) {
                if (hitbox_style((u16)i) == 0x04) {
                    REG8(0x0011A1A4UL) = 0x01;
                    goto done;
                }
            }
        }
        REG8(0x0011A1A4UL) = 0x00;
    }

done:
    if (REG8(0x0011A1A4UL) != 0 && (REG8(0x00FFFEE5UL) & 0x80)) {
        if (hitbox_kind(mark) == 0x00) hitbox_set_state(mark, mark, 0x01, 0);
    } else {
        if (hitbox_kind(mark) == 0x01) hitbox_set_state(mark, mark, 0x00, 0);
    }
}

/* H'200EA4. The first byte of one of the ten-byte records at H'57C6D6, the
 * block settings_save copies out to flash. The index is worked out in
 * sixteen bits and then zero-extended, so a record number past H'1999 wraps
 * rather than running off the end. */
u8 stitch_record_kind(u16 n)
{
    return REG8(0x0057C6D6UL + (u32)(u16)(10 * n));
}

/* ---- the panel strip ----------------------------------------------------
 * H'213B16. The drawing counterpart of H'214DD4: one box of the strip along
 * the top of the screen brought up to date from the field it stands for.
 *
 * Four shapes between the twenty handlers. Six are a lamp -- the box lit or
 * put out by whether a bit is set. Five patch a picture into the icon table
 * at the slot the box's own list entry names and then redraw the box, which
 * is how a box with one entry shows one of several pictures. Five grey the
 * box out or bring it back. The last two are one of each with an extra test.
 *
 * Each remembers what it last drew in its own word at H'11B2D2 upwards and
 * does nothing when nothing has changed. Entering with [fresh] set puts all
 * fifteen words to H'FFFF and does no drawing at all.
 *
 * H'11B0A8 and four screen numbers keep the fields underneath from being
 * updated while a screen change is settling.
 *
 * The three flags the stitch descriptor sets -- taken from its word at +H'14
 * -- decide which boxes are greyed: H'16 and H'17 raise one, H'3C to H'3E
 * raise all three, and anything else lowers them.
 */
static void strip_light(u16 box, u32 slot, u16 v)
{
    if (REG16(slot) == v) return;
    hitbox_set_state(box, box, (u8)(v != 0 ? 0x01 : 0x00), 0);
    REG16(slot) = v;
}

/* The box's list entry names a slot in the icon table; the picture is put
 * there and the box drawn again from it. */
static void strip_patch(u16 box, u32 entry, u32 picture)
{
    const u32 at = REG32(entry + 0x0C) +
        (u32)(long)(short)(u16)((u16)(REG16(entry + 0x08) << 1));

    REG32(0x001158CEUL +
          (u32)(long)(short)(u16)((u16)(REG16(at) << 2))) = picture;
    hitbox_redraw_run(box, box);
}

static void strip_picture(u16 box, u32 entry, u32 slot, u16 v, u32 table)
{
    if (REG16(slot) == v) return;
    strip_patch(box, entry,
                REG32(table + (u32)(long)(short)(u16)((u16)(v << 2))));
    REG16(slot) = v;
}

static void strip_grey(u16 box, u8 want)
{
    if (want != 0) {
        if (hitbox_kind(box) != 0x05) hitbox_set_state(box, box, 0x05, 0);
    } else {
        if (hitbox_kind(box) == 0x05) hitbox_set_state(box, box, 0x00, 0);
    }
}

void panel_strip_box(u8 key, u16 box, u8 fresh)
{
    const u32 entry = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    const u8  screen = REG8(0x0011A169UL);
    u16 kind;
    u8  a, b, c;

    if (screen != 0x0C && screen != 0x0D && screen != 0x42 &&
        screen != 0x18 && REG8(0x0011B0A8UL) == 0) {
        panel_field_update(REG16(0x0057EFC4UL), fresh);
    }

    if (fresh != 0) {
        REG16(0x0011B2D2UL) = 0xFFFF;
        REG16(0x0011B2D4UL) = 0xFFFF;
        REG16(0x0011B2EEUL) = 0xFFFF;
        REG16(0x0011B2D6UL) = 0xFFFF;
        REG16(0x0011B2D8UL) = 0xFFFF;
        REG16(0x0011B2DAUL) = 0xFFFF;
        REG16(0x0011B2DCUL) = 0xFFFF;
        REG16(0x0011B2DEUL) = 0xFFFF;
        REG16(0x0011B2E0UL) = 0xFFFF;
        REG16(0x0011B2E2UL) = 0xFFFF;
        REG16(0x0011B2E4UL) = 0xFFFF;
        REG16(0x0011B2E6UL) = 0xFFFF;
        REG16(0x0011B2E8UL) = 0xFFFF;
        REG16(0x0011B2EAUL) = 0xFFFF;
        REG16(0x0011B2ECUL) = 0xFFFF;
        return;            /* the fresh path branches past the tail below */
    }

    kind = REG16(ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x14);

    if (kind == 0x0016) {
        a = 0x01; b = 0x00; c = 0x00;
    } else if (kind == 0x0017) {
        c = 0x01; a = 0x01; b = 0x00;
        REG8(0x00FFFEFDUL) = 0x00;
    } else if ((short)kind >= 0x003C && (short)kind <= 0x003E) {
        b = 0x01; c = 0x01; a = 0x01;
        REG8(0x00FFFEFDUL) = 0x00;
    } else {
        b = 0x00; c = 0x00; a = 0x00;
    }

    switch (key) {
    case 0x01: strip_light(box, 0x0011B2DAUL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x40)); break;
    case 0x02: strip_light(box, 0x0011B2DCUL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x10)); break;
    case 0x03: strip_light(box, 0x0011B2DEUL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x10)); break;
    case 0x05: strip_light(box, 0x0011B2D6UL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x40)); break;
    case 0x06: strip_light(box, 0x0011B2E0UL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x80)); break;
    case 0x09: strip_light(box, 0x0011B2E4UL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x08)); break;
    case 0x0A: strip_light(box, 0x0011B2E6UL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x80)); break;

    case 0x04: strip_picture(box, entry, 0x0011B2D8UL,
                   (u16)(REG8(0x00FFFEF9UL) & 0x0F), 0x00115852UL); break;
    case 0x07: strip_picture(box, entry, 0x0011B2E2UL,
                   (u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F),
                   0x0011587AUL); break;
    case 0x08: strip_picture(box, entry, 0x0011B2D4UL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x0F), 0x0011582EUL); break;
    case 0x0C: strip_picture(box, entry, 0x0011B2D2UL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x03), 0x0011581EUL); break;
    case 0x46: strip_picture(box, entry, 0x0011B2EAUL,
                   (u16)REG8(0x00FFFEFDUL), 0x0011589AUL); break;

    case 0x44: {
        const u16 v = (u16)(REG8(0x00FFFEF5UL) & 0x04);

        if (REG16(0x0011B2E8UL) != v) {
            strip_patch(box, entry, v != 0 ? 0x0034E55CUL : 0x0034E5EDUL);
            REG16(0x0011B2E8UL) = v;
        }
        break;
    }

    case 0x49: {
        const u16 v = (u16)(REG8(0x00FFFEF6UL) & 0x0F);

        if (REG16(0x0011B2EEUL) != v) {
            hitbox_set_state(box, box, (u8)(v == 0x0001 ? 0x01 : 0x00), 0);
            REG16(0x0011B2EEUL) = v;
        }
        break;
    }

    case 0x12: strip_grey(box, (u8)((REG8(0x00FFFEF8UL) & 0x01) || b)); break;
    case 0x13: strip_grey(box, (u8)((REG8(0x00FFFEF8UL) & 0x01) || a)); break;
    case 0x14: strip_grey(box, (u8)((REG8(0x00FFFEF8UL) & 0x01) || a)); break;

    case 0x4C: {
        const u16 n = (u16)(REG16(0x00FFFEE0UL) +
                            (u16)REG8(0x00FFFEFDUL));

        strip_grey(box, (u8)(stitch_record_kind(n) == 0 ||
                             (REG8(0x00FFFEF8UL) & 0x01)));
        break;
    }

    case 0x4D: {
        const u8 on = (u8)((REG8(0x00FFFEFAUL) & 0x80) ||
                           (REG8(0x00FFFEF7UL) & 0x08));

        strip_grey(box, (u8)(on ? ((REG8(0x00FFFEF8UL) & 0x01) != 0) : 1));
        break;
    }

    case 0x47:
        if (c != 0) {
            if (hitbox_kind(box) != 0x05) {
                hitbox_set_state(box, box, 0x05, 0);
                REG8(0x00FFFEF8UL) &= (u8)~0x01;
            }
        } else {
            if (hitbox_kind(box) == 0x05) {
                hitbox_set_state(box, box, 0x00, 0);
            }
        }
        if (hitbox_kind(box) == 0x05) goto tail;
        strip_light(box, 0x0011B2ECUL, (u16)(REG8(0x00FFFEF8UL) & 0x01));
        break;

    default:
        break;
    }

tail:
    if (REG8(0x00FFFEE2UL) & 0x04) {
        if ((u8)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F) != 0) {
            REG8(0x00FFFEF9UL) &= 0x0F;
        }
    }
}

/* H'2135FE. The whole panel strip brought up to date.
 *
 * With [fresh] set it makes one call with everything zeroed, which is what
 * puts the fifteen remembered words to H'FFFF, and raises H'11A17B so that
 * H'2136A6 walks the list next time. Otherwise it runs the boxes from
 * [first] to [last], and for each one works out which panel key that box
 * stands for: the box's own value indexes its list, and the low byte of the
 * word found there is the key.
 *
 * The original copies the whole H'12-byte hitbox entry into a local frame
 * first and reads the value and the list pointer out of the copy. Nothing
 * changes the entry in between, so the copy makes no difference -- but it is
 * what the original does and the fields are read from the copy here too.
 *
 * Either way it finishes by asking H'2136A6 whether anything in the run is
 * away from its default. */
void panel_strip_draw(u16 first, u16 last, u8 fresh)
{
    if (fresh != 0) {
        panel_strip_box(0x00, 0x0000, 0x01);
        REG8(0x0011A17BUL) = 0x01;
    } else {
        short i;

        for (i = (short)first; i <= (short)last; i++) {
            const u32 e = HITBOX_TABLE +
                (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
            u16 copy[9];
            u32 list;
            u16 value;
            int n;

            for (n = 0; n < 9; n++) copy[n] = REG16(e + (u32)(2 * n));

            value = copy[4];                              /* the +H'08 field */
            list  = ((u32)copy[6] << 16) | (u32)copy[7];  /* the +H'0C field */

            panel_strip_box(REG8(list +
                (u32)(long)(short)(u16)((u16)(value << 1)) + 1),
                (u16)i, 0x00);
        }
    }

    panel_any_set(first, last);
}

/* ---- which strip the screen wears ---------------------------------------
 * H'21F9D0. The pattern's category decides which of six lists the panel
 * strip is filled from, and H'11A196 remembers which one is up so the work
 * is done once per change rather than once per pass.
 *
 * The category less three indexes a table of twenty-three entries covering
 * seven bodies, so most categories share a strip. Each body is the same
 * four steps -- fill the run from the list, draw it fresh, remember the
 * list, and say the screen wants laying out again -- with its own list, its
 * own run of boxes, and its own idea of which panel bits to put back.
 *
 * The first two bodies are the odd ones. H'21FA6C looks at the pattern's
 * own kind first and picks between two lists on that; H'21FC26 is its
 * second half on its own. Both finish by clearing H'FFFEFD when the pedal
 * bit is down, which none of the others do.
 */
static u16 strip_pattern_kind(void)
{
    return REG16(ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x14);
}

static void strip_switch(u16 first, u16 last, u32 list)
{
    hitbox_fill_from_list(first, last, 0x0001, list);
    panel_strip_draw(first, last, 0x01);
    REG32(0x0011A196UL) = list;
    REG8(0x00FFFEFDUL) = 0x00;
    REG8(0x00FFFEF8UL) &= (u8)~0x01;
    REG16(0x0011B10CUL) = 0x0001;
}

/* The list H'57EEF8 wants six panel bits put back as well, and zeroes the
 * whole of H'FFFEF9 -- written in the original as "and with H'0F, then and
 * with H'F0", which can only give nothing. */
static void strip_switch_eef8(void)
{
    hitbox_fill_from_list(0x0010, 0x0015, 0x0001, 0x0057EEF8UL);
    panel_strip_draw(0x0010, 0x0015, 0x01);
    REG32(0x0011A196UL) = 0x0057EEF8UL;
    REG8(0x00FFFEF9UL) = (u8)((u8)(REG8(0x00FFFEF9UL) & 0x0F) & 0xF0);
    REG8(0x00FFFEF6UL) &= (u8)~0x80;
    REG8(0x00FFFEF6UL) &= (u8)~0x40;
    REG8(0x00FFFEF5UL) &= (u8)~0x10;
    REG8(0x00FFFEF6UL) &= (u8)~0x10;
    REG8(0x00FFFEF5UL) &= (u8)~0x08;
    REG8(0x00FFFEF5UL) &= (u8)~0x80;
    REG16(0x0011B10CUL) = 0x0001;
}

static void strip_pedal_settle(void)
{
    if (REG8(0x00FFFEFDUL) != 0 && !(REG8(0x00FFFEF8UL) & 0x01)) {
        REG8(0x00FFFEFDUL) = 0x00;
    }
}

void panel_strip_choose(void)
{
    const u8 idx = (u8)(REG8(ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x17) + 0xFD);

    if (idx > 0x16) return;

    switch (idx) {
    case 0x00:
        if (strip_pattern_kind() != 0x0016 &&
            strip_pattern_kind() != 0x0017) {
            if (REG32(0x0011A196UL) != 0x0057EED6UL) {
                strip_switch(0x0010, 0x0015, 0x0057EED6UL);
            }
        }
        if (strip_pattern_kind() == 0x0016 ||
            strip_pattern_kind() == 0x0017) {
            if (REG32(0x0011A196UL) != 0x0057EEF8UL) strip_switch_eef8();
            strip_pedal_settle();
        }
        break;

    case 0x01:
        if (REG32(0x0011A196UL) != 0x0057EEF8UL) strip_switch_eef8();
        strip_pedal_settle();
        break;

    case 0x02: case 0x04: case 0x05: case 0x07:
    case 0x08: case 0x0A: case 0x0C:
        if (REG32(0x0011A196UL) != 0x0057EF1AUL) {
            strip_switch(0x0010, 0x0015, 0x0057EF1AUL);
        }
        break;

    case 0x03: case 0x06: case 0x09: case 0x0B:
        if (REG32(0x0011A196UL) != 0x0057EF80UL) {
            strip_switch(0x0010, 0x0015, 0x0057EF80UL);
        }
        break;

    case 0x0D: case 0x0E:
        if (REG32(0x0011A196UL) != 0x0057EF5EUL) {
            hitbox_fill_from_list(0x000D, 0x0012, 0x0001, 0x0057EF5EUL);
            panel_strip_draw(0x000D, 0x0012, 0x01);
            REG32(0x0011A196UL) = 0x0057EF5EUL;
            REG8(0x00FFFEFDUL) = 0x00;
            REG8(0x00FFFEF9UL) &= 0x0F;
            REG8(0x00FFFEF8UL) &= (u8)~0x01;
            REG16(0x0011B10CUL) = 0x0001;
        }
        break;

    case 0x0F: case 0x10: case 0x11: case 0x12:
    case 0x14: case 0x15: case 0x16:
        if (REG32(0x0011A196UL) != 0x0057EF3CUL) {
            strip_switch(0x0019, 0x001E, 0x0057EF3CUL);
        }
        break;

    case 0x13:
        if (REG32(0x0011A196UL) != 0x0057EFA2UL) {
            /* this one puts the two bits back before it fills, not after */
            REG8(0x00FFFEFDUL) = 0x00;
            REG8(0x00FFFEF8UL) &= (u8)~0x01;
            hitbox_fill_from_list(0x0019, 0x001E, 0x0001, 0x0057EFA2UL);
            panel_strip_draw(0x0019, 0x001E, 0x01);
            REG32(0x0011A196UL) = 0x0057EFA2UL;
            REG16(0x0011B10CUL) = 0x0001;
        }
        break;

    default:
        break;
    }
}

/* H'2195F2. Seven pictures to choose between, and four keys that are not
 * pictures at all.
 *
 * The screen answers a press or, failing that, a screen change asked for
 * elsewhere -- H'21F940 writes its answer into the same local the hit test
 * uses, so one key search covers both. The keys are searched rather than
 * indexed, and the handlers are stored back to front behind H'2196B6, the
 * same reversed-table idiom H'214DD4 uses.
 *
 * The seven picture keys, H'88 to H'8E, each blit their own bitmap into the
 * one box and leave their own number in the bottom three bits of H'FFFEFA.
 * The first of them ands those bits away and ors nothing back, which is the
 * only reason it reads as a separate case rather than as "or with zero".
 *
 * Four keys hand straight to H'214DD4 as a step, and the low byte of the box
 * value is the panel key -- so the same word is both the box's value here
 * and a key over there. */
static void picture_choice(u16 index, u32 picture, u8 code)
{
    message_show_held(index);
    bitmap_draw(0x0045, 0x0057, 0x0086, 0x0098,
                (const u8 *)picture, LCD_FRAME_A);
    REG8(0x00FFFEFAUL) = (u8)((u8)(REG8(0x00FFFEFAUL) & 0xF8) | code);
}

u8 picture_choice_screen(void)
{
    u16 value = 0, index = 0;
    u8  hit = touch_hit(0x0001, 0x000B, &value, &index);

    if (hit != 0x03) hit = screen_leave_check(&value, 0x00);
    if (hit != 0x03) return 0x00;

    switch (value) {
    case 0x0008:
    case 0x000C:
    case 0x006E:
    case 0x006F:
        (void)panel_switch((u8)value, index, 0x01, 0x00);
        break;

    case 0x000B:
        message_show_held(index);
        screen_remember(0x01);
        screen_switch(0x3F, 0x01, 0x00);
        break;

    case 0x001A:
        message_show_held(index);
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        screen_switch(REG8(0x0011B0A7UL), 0x01, 0x00);
        REG8(0x0011B0A9UL) = 0x01;
        break;

    case 0x0088: picture_choice(index, 0x0034F208UL, 0x00); break;
    case 0x0089: picture_choice(index, 0x0034F33CUL, 0x01); break;
    case 0x008A: picture_choice(index, 0x0034F463UL, 0x02); break;
    case 0x008B: picture_choice(index, 0x0034F570UL, 0x03); break;
    case 0x008C: picture_choice(index, 0x0034F69CUL, 0x04); break;
    case 0x008D: picture_choice(index, 0x0034F7C8UL, 0x05); break;
    case 0x008E: picture_choice(index, 0x0034F8D5UL, 0x06); break;

    default:
        break;
    }
    return 0x00;
}
