/* The artista 180 application, rebuilt in C: the screen bodies' helpers.

 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- the screen bodies' helpers ---------------------------------------
 * H'22382A's thirty-odd screen bodies are inline blocks behind a table of
 * seventy-nine entries at H'2238B0, and they lean on a set of small drawing
 * helpers. These are the ones with the most callers.
 */

/* H'21752E. The box the sewing screen keeps its picture in: cleared in
 * frame B and then drawn from H'34C6CD into frame A. */
void sew_picture_box(void)
{
    draw_rect(0x006B, 0x0051, 0x00CD, 0x0097, LCD_FRAME_B, 0x00, 0x01);
    bitmap_draw(0x006B, 0x0051, 0x00CD, 0x0097,
                (const u8 *)0x0034C6CDUL, LCD_FRAME_A);
}

/* H'213ABC. The little needle-stop picture, one of two by bit 5 of
 * H'FFFEF6. The bit is rotated down to the bottom of the byte three times
 * rather than shifted, which comes to the same thing here. */
void needle_stop_picture(void)
{
    u8 k = (u8)(REG8(0x00FFFEF6UL) & 0x20);
    u8 i;

    for (i = 0; i < 3; i++) k = (u8)((u8)(k << 1) | (u8)(k >> 7));
    k = (u8)(k & 0x07);

    bitmap_draw(0x002C, 0x000B, 0x0030, 0x0019,
                (const u8 *)REG32(0x00115892UL
                                  + (u32)(long)(short)(u16)((u16)k << 2)),
                LCD_FRAME_A);
}

/* H'21B34C. The number in H'FFFECD drawn top right, and only when it moves.
 * H'11B35E remembers what was drawn; [fresh] forgets it. */
void speed_number_draw(u8 fresh)
{
    char buf[6];

    if (fresh != 0) REG16(0x0011B35EUL) = 0xFFFF;

    if ((u16)REG8(0x00FFFECDUL) != REG16(0x0011B35EUL)) {
        int_to_decimal((short)(u16)REG8(0x00FFFECDUL), buf);
        text_draw(buf, 0x00F3, 0x0087, 0x0100, 0x008D, 0x0002, 0x02,
                  (const u8 *)0x00119DE6UL);
        REG16(0x0011B35EUL) = (u16)REG8(0x00FFFECDUL);
    }
}

/* H'213882. The eleven boxes of the stitch-width strip, from a table of
 * four words each at H'11524E, with the one H'FFFEEA names drawn from
 * H'34BD2D over the top.
 *
 * [fresh] redraws the lot; otherwise only a move repaints, and then only
 * the two boxes that changed. H'11B2D0 remembers which was lit. */
void width_strip_draw(u8 fresh)
{
    const u32 tbl = 0x0011524EUL;
    short i;

    if (fresh != 0) {
        REG16(0x0011B2D0UL) = 0xFFFF;

        for (i = 0; i <= 0x000A; i++) {
            const u32 e = tbl + (u32)(long)(short)(u16)((u16)i << 3);

            draw_rect(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                      LCD_FRAME_B, 0x00, 0x01);
        }
        {
            const u32 e = tbl + (u32)(long)(short)(u16)
                          ((u16)((u16)REG8(0x00FFFEEAUL)) << 3);

            bitmap_draw(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                        (const u8 *)0x0034BD2DUL, LCD_FRAME_B);
        }
        REG16(0x0011B2D0UL) = (u16)REG8(0x00FFFEEAUL);
    }

    if ((u16)REG8(0x00FFFEEAUL) != REG16(0x0011B2D0UL)) {
        {
            const u32 e = tbl + (u32)(long)(short)(u16)
                          ((u16)REG16(0x0011B2D0UL) << 3);

            draw_rect(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                      LCD_FRAME_B, 0x00, 0x01);
        }
        {
            const u32 e = tbl + (u32)(long)(short)(u16)
                          ((u16)((u16)REG8(0x00FFFEEAUL)) << 3);

            bitmap_draw(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                        (const u8 *)0x0034BD2DUL, LCD_FRAME_B);
        }
        REG16(0x0011B2D0UL) = (u16)REG8(0x00FFFEEAUL);
    }
}

/* H'21348C. The two arrows beside a list, lit or dim by where the window
 * sits in it. H'11B0AA remembers the back arrow's state and H'11B0AB the
 * forward one, so each is only repainted when it changes; [fresh] sets both
 * flags and draws nothing, which leaves the next call to do the work.
 *
 * The end of the window is entry one's value plus [span], and the length of
 * the list is the first word the list points at. */
void list_arrows(u16 index, u16 span, u16 back_box, u16 fwd_box, u8 fresh)
{
    const u32 table = REG32(0x0011B0BAUL);
    const u32 entry = table + (u32)(long)(short)(u16)(0x12 * index);
    const short top = (short)REG16(REG32(entry + 0x0C));

    if (fresh != 0) {
        REG8(0x0011B0ABUL) = 0x01;
        REG8(0x0011B0AAUL) = 0x01;
        return;
    }

    {
        const u16 v = REG16(REG32(0x0011B0BAUL)
                            + (u32)(long)(short)(u16)(0x12 * index) + 0x08);

        if (v == 0x0001) {
            if (REG8(0x0011B0AAUL) != 0) {
                REG8(0x0011B0AAUL) = 0x00;
                hitbox_blit(back_box, LCD_FRAME_A, 0x0034E4A8UL);
            }
        } else if ((short)v > (short)0x0001) {
            if (REG8(0x0011B0AAUL) == 0) {
                REG8(0x0011B0AAUL) = 0x01;
                hitbox_blit(back_box, LCD_FRAME_A, 0x0034E46CUL);
            }
        }
    }

    {
        const short end = (short)(REG16(REG32(0x0011B0BAUL) + 0x1A) + span);

        if (end > top) {
            if (REG8(0x0011B0ABUL) != 0) {
                REG8(0x0011B0ABUL) = 0x00;
                hitbox_blit(fwd_box, LCD_FRAME_A, 0x0034E520UL);
            }
        } else {
            if (REG8(0x0011B0ABUL) == 0) {
                REG8(0x0011B0ABUL) = 0x01;
                hitbox_blit(fwd_box, LCD_FRAME_A, 0x0034E4E4UL);
            }
        }
    }
}

/* H'2224E8. The pattern strip put back from the second store and its number
 * redrawn, and H'11A176 asked for a repaint. */
void picker_strip_restore(void)
{
    region_copy(0x0030, 0x00A0, 0x00E7, 0x00C0, 0x00A0,
                0x000F1610UL, LCD_FRAME_A);
    dialog_number_draw(REG16(0x0011A1CEUL));
    REG8(0x0011A176UL) = 0x01;
}

/* H'21BDD6. The demonstration screen: five pictures shown one after another,
 * a second each. H'11B362 counts round them, and which set of five depends
 * on whether H'57FF80 says this is a H'B4 machine -- with picture nought
 * coming out of the configuration block rather than the table.
 *
 * [fresh] just puts the counter back to nought. A press of H'77 leaves for
 * screen H'17 with the foot switch position cleared. */
void demo_screen_step(u8 fresh)
{
    u16 code = 0;

    if (fresh != 0) { REG16(0x0011B362UL) = 0x0000; return; }

    if (screen_leave_check(&code, 0x00) == 0x03) {
        if (code == 0x0077) {
            REG8(0x00FFFEC5UL) = 0x00;
            screen_switch(0x17, 0x01, 0x00);
        }
        return;
    }

    if (REG8(0x0057FF80UL) == 0xB4) {
        if (REG16(0x0011B362UL) == 0)
            image_load(REG32(REG32(0x0011B2AEUL) + 0x10), LCD_SCRATCH);
        else
            image_load(REG32(0x001158BAUL
                             + (u32)(long)(short)(u16)
                               (REG16(0x0011B362UL) << 2)), LCD_SCRATCH);
    } else {
        if (REG16(0x0011B362UL) == 0)
            image_load(REG32(REG32(0x0011B2AEUL) + 0x14), LCD_SCRATCH);
        else
            image_load(REG32(0x001158A6UL
                             + (u32)(long)(short)(u16)
                               (REG16(0x0011B362UL) << 2)), LCD_SCRATCH);
    }

    region_copy(0x0000, 0x0000, 0x013F, 0x00EF, 0x0000,
                LCD_SCRATCH, LCD_FRAME_A);
    hold_start(0x03E8);
    REG16(0x0011B362UL) =
        (u16)((short)(u16)(REG16(0x0011B362UL) + 1) % (short)5);
}

/* H'21EF02 and H'21C672. Two settings screens of the same shape: one flash
 * byte, two boxes that are the yes and the no of it, H'19 to save and H'1A
 * to leave without saving. The first owns H'57EFC6 and the second H'57EFC7,
 * and the second also drives bit 6 of H'FFFEC1 from what it saved.
 *
 * The working copy lives in RAM -- H'11B3C0 and H'11B364 -- and is what the
 * single-byte flash write is given the address of. */
u8 setting_toggle_C6(u8 fresh)
{
    u16 value = 0, index = 0;

    if (fresh != 0) {
        screen_stack_push();
        REG8(0x0011B3C0UL) = REG8(0x0057EFC6UL);
        if (REG8(0x0011B3C0UL) != 0) hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        else                         hitbox_set_state(0x0002, 0x0002, 0x01, 0);
    }

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return 0x00;

    if (value == 0x0042) {
        if (REG8(0x0011B3C0UL) == 0) {
            hitbox_set_state(0x0002, 0x0002, 0x00, 0);
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
            REG8(0x0011B3C0UL) = 0x01;
        }
        return 0x00;
    }

    if (value == 0x0043) {
        if (REG8(0x0011B3C0UL) != 0) {
            hitbox_set_state(0x0001, 0x0001, 0x00, 0);
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
            REG8(0x0011B3C0UL) = 0x00;
        }
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        REG8(0x00114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x0011B3C0UL, 0x0057EFC6UL, 1);
        REG8(0x00114DC7UL) &= (u8)~0x20;
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

u8 setting_toggle_C7(u8 fresh)
{
    u16 value = 0, index = 0;

    if (fresh != 0) {
        screen_stack_push();
        hitbox_set_state(0x0001, 0x0002, 0x00, 0);
        if (REG8(0x0057EFC7UL) != 0) hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        else                         hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        REG8(0x0011B364UL) = REG8(0x0057EFC7UL);
    }

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return 0x00;

    if (value == 0x0042) {
        if (REG8(0x0011B364UL) == 0) {
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
            hitbox_set_state(0x0002, 0x0002, 0x00, 0);
            REG8(0x0011B364UL) = 0x01;
        }
        return 0x00;
    }

    if (value == 0x0043) {
        if (REG8(0x0011B364UL) != 0) {
            hitbox_set_state(0x0001, 0x0001, 0x00, 0);
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
            REG8(0x0011B364UL) = 0x00;
        }
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        REG8(0x00114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x0011B364UL, 0x0057EFC7UL, 1);
        REG8(0x00114DC7UL) &= (u8)~0x20;
        if (REG8(0x0057EFC7UL) != 0) REG8(0x00FFFEC1UL) |= 0x40;
        else                         REG8(0x00FFFEC1UL) &= (u8)~0x40;
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

/* H'222FD2. Where the page holding position H'11B212 starts. The first
 * fifteen are all one page; after that the pages are five long, and the
 * answer is the page's own start counted back from the walk. */
u16 list_page_start(void)
{
    const short at = (short)(REG16(0x0011B212UL) + 1);
    short e;

    if (at <= 0x000F) return 0x0001;

    for (e = 0x0010; ; e += 0x0005) {
        if (e == at) return (u16)(e - 10);
        if (e > at)  return (u16)(e - 15);
    }
}

/* H'21B9CE. A screen with one way out of it: key H'77 for screen H'17, with
 * the foot switch position cleared on the way. */
u8 screen_only_77(void)
{
    u16 code = 0;

    if (screen_leave_check(&code, 0x00) == 0x03) {
        if (code == 0x0077) {
            REG8(0x00FFFEC5UL) = 0x00;
            screen_switch(0x17, 0x01, 0x00);
        }
    }
    return 0x00;
}

/* H'21B6C6. The byte in H'FFFED8 drawn as a number, right-aligned in a box
 * halfway down the screen. */
void needle_number_draw(void)
{
    /* the ROM gives this two bytes of stack, which is one short of what
     * three digits need; the field never holds three */
    char buf[4];

    int_to_decimal((short)(u16)REG8(0x00FFFED8UL), buf);
    text_draw(buf, 0x0057, 0x00B5, 0x0064, 0x00BE, 0x0001, 0x00,
              (const u8 *)0x001196EAUL);
}

/* H'21C466 and H'229F7E. Two more of the yes-and-no screens, but these do
 * not save anything: box one and box two set a RAM byte and go straight back
 * to the slot-four screen. The second sets H'11B0A9 as well, and sets it on
 * both answers rather than only on the yes. */
u8 choice_screen_17A(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    message_show_held(index);

    if (value == 0x0001) {
        screen_stack_pop();
        REG8(0x0011A17AUL) = 0x01;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    } else if (value == 0x0002) {
        screen_stack_pop();
        REG8(0x0011A17AUL) = 0x00;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    }
    return 0x00;
}

u8 choice_screen_1E3(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    message_show_held(index);

    if (value == 0x0001) {
        screen_stack_pop();
        REG8(0x0011A1E3UL) = 0x01;
        REG8(0x0011B0A9UL) = 0x01;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    } else if (value == 0x0002) {
        screen_stack_pop();
        REG8(0x0011A1E3UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    }
    return 0x00;
}

/* H'23033A, H'2180C8 and H'21C3B8. Three menu screens of the same shape: a
 * press is read, the message it carries is held, and what the box stands for
 * says where to go. None of them does anything else.
 */

/* H'23033A. The two ways into the embroidery panel, one with the module's
 * own set-up run and one without. */
void menu_embroidery(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return;

    message_show_held(index);

    if (value == 0x0001) {
        REG8(0x00114D8EUL) = 0x07;
        REG8(0x0011F304UL) = 0x01;
        REG8(0x0011F532UL) = 0x07;
        REG8(0x0011F533UL) = 0x07;
        screen_switch(0x37, 0x01, 0x00);
        screen_stack_pop();
    } else if (value == 0x0002) {
        REG8(0x00114D8EUL) = 0x07;
        REG8(0x0011F304UL) = 0x01;
        REG8(0x0011F532UL) = 0x00;
        REG8(0x0011F533UL) = 0x00;
        screen_switch(0x37, 0x01, 0x00);
        screen_stack_pop();
    }
}

/* H'2180C8. Three categories of pattern: whichever is pressed, the screen
 * moves to H'03 and H'11B108 is set to the first item of that category in
 * the list H'11B096 points at. */
void menu_category(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0003, &value, &index) != 0x03) return;

    message_show_held(index);
    screen_switch(0x03, 0x01, 0x00);

    if (value == 0x0001)
        REG16(0x0011B108UL) = first_item_of_category(0x12, REG32(0x0011B096UL));
    else if (value == 0x0002)
        REG16(0x0011B108UL) = first_item_of_category(0x13, REG32(0x0011B096UL));
    else if (value == 0x0003)
        REG16(0x0011B108UL) = first_item_of_category(0x14, REG32(0x0011B096UL));
}

/* H'21C3B8. Four ways out: three screens by number and H'1A to come back off
 * the stack to H'27. */
void menu_four_ways(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return;

    message_show_held(index);

    if (value == 0x0001)      screen_switch(0x4B, 0x01, 0x00);
    else if (value == 0x0002) screen_switch(0x00, 0x01, 0x00);
    else if (value == 0x0003) screen_switch(0x31, 0x01, 0x00);
    else if (value == 0x001A) {
        screen_stack_pop();
        screen_switch(0x27, 0x01, 0x00);
    }
}

/* H'218292, H'218188 and H'217DE0. Three more menu screens. The first two
 * take the operator to a list of one category; the third is the same again
 * with six categories and answers H'01 rather than H'00.
 *
 * H'218292 is the odd one: its second and third boxes are refused with
 * message H'0A while H'11A178 says the machine is mid-something, and its
 * first box goes to a different screen from the other two. */
u8 menu_three_lists(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0003, &value, &index) != 0x03) return 0x00;

    message_show_held(index);

    if (value == 0x0001) {
        screen_switch(0x02, 0x01, 0x00);
        REG16(0x0011B108UL) = first_item_of_category(0x0F, 0x0011A88EUL);
        return 0x00;
    }
    if (value == 0x0002) {
        if (REG8(0x0011A178UL) != 0) { message_show(0x000A); return 0x00; }
        screen_switch(0x33, 0x01, 0x00);
        REG16(0x0011B108UL) = 0x0001;
        return 0x00;
    }
    if (value == 0x0003) {
        if (REG8(0x0011A178UL) != 0) { message_show(0x000A); return 0x00; }
        screen_switch(0x35, 0x01, 0x00);
        REG16(0x0011B108UL) = 0x0001;
    }
    return 0x00;
}

u8 menu_five_categories(void)
{
    u16 value = 0, index = 0;
    u8 wanted;

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;

    message_show_held(index);
    screen_switch(0x03, 0x01, 0x00);

    if      (value == 0x0001) wanted = 0x12;
    else if (value == 0x0002) wanted = 0x13;
    else if (value == 0x0003) wanted = 0x14;
    else if (value == 0x0004) wanted = 0x15;
    else if (value == 0x0005) wanted = 0x16;
    else                      return 0x00;

    REG16(0x0011B108UL) = first_item_of_category(wanted, REG32(0x0011B096UL));
    return 0x00;
}

u8 menu_six_categories(void)
{
    u16 value = 0, index = 0;
    u8 wanted;

    if (touch_hit(0x0001, 0x0006, &value, &index) != 0x03) return 0x00;

    message_show_held(index);
    screen_switch(0x02, 0x01, 0x00);

    if      (value == 0x0001) wanted = 0x05;
    else if (value == 0x0002) wanted = 0x07;
    else if (value == 0x0003) wanted = 0x08;
    else if (value == 0x0004) wanted = 0x0A;
    else if (value == 0x0005) wanted = 0x0B;
    else if (value == 0x0006) wanted = 0x0D;
    else                      return 0x00;

    REG16(0x0011B108UL) = first_item_of_category(wanted, 0x0011A88EUL);
    return 0x01;
}

/* H'219CC8. Twelve boxes, of which two are ways out and the rest a choice.
 *
 * The chosen box is remembered in H'11A1AA so that the next press can put
 * the old one back to state 0 before lighting the new one, and the choice
 * itself is written to H'11B0FF as the value plus H'9C. Both exits put the
 * message up before they look at the sewing flag, so a press while the
 * machine is running is announced and then ignored.
 *
 * Box H'19 does nothing at all when nothing is chosen yet: the whole body,
 * message included, sits inside the test on H'11A1AA. */
u8 menu_twelve_choice(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x000C, &value, &index) != 0x03) return 0x00;

    if (value == 0x0019) {
        if (REG16(0x0011A1AAUL) != 0xFFFF) {
            message_show_held(index);
            if (REG8(0x00114DC6UL) & 0x80) return 0x00;
            screen_switch(0x11, 0x02, 0x00);
            REG16(0x0011A1AAUL) = 0xFFFF;
        }
        return 0x00;
    }

    if (value == 0x001A) {
        message_show_held(index);
        if (REG8(0x00114DC6UL) & 0x80) return 0x00;
        screen_stack_pop();
        screen_switch(0x0F, 0x01, 0x00);
        REG16(0x0011A1AAUL) = 0xFFFF;
        return 0x00;
    }

    if (index != REG16(0x0011A1AAUL)) {
        const u16 held = REG16(0x0011A1AAUL);

        if (held != 0xFFFF) hitbox_set_state(held, held, 0x00, 0);
        hitbox_set_state(index, index, 0x01, 0);
        REG16(0x0011A1AAUL) = index;
        REG8(0x0011B0FFUL) = (u8)((u8)value + 0x9C);
    }
    return 0x00;
}

/* H'21BBE6. The display test: three full-screen fills, one press apart.
 *
 * H'11B361 counts them. Arriving with the count at 0 paints the screen in
 * colour 3 and puts the count at 1, and each press of the single box paints
 * the next colour. After the third the count goes back to 0, the panel code
 * at H'FFFEC5 is cleared and the screen leaves for H'17.
 *
 * The fills go straight into the visible buffer, not the back one -- there
 * is nothing here for the flip to bring over. */
u8 display_test(void)
{
    u16 value = 0, index = 0;

    if (REG8(0x0011B361UL) == 0x00) {
        draw_rect(0x0000, 0x0000, 0x013F, 0x00EF, LCD_FRAME_A, 0x03, 0x01);
        REG8(0x0011B361UL) = 0x01;
    }

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;
    if (value != 0x0001) return 0x00;

    if (REG8(0x0011B361UL) == 0x01) {
        draw_rect(0x0000, 0x0000, 0x013F, 0x00EF, LCD_FRAME_A, 0x00, 0x01);
        REG8(0x0011B361UL) = 0x02;
        hold_start(0x0064);
        return 0x00;
    }
    if (REG8(0x0011B361UL) == 0x02) {
        draw_rect(0x0000, 0x0000, 0x013F, 0x00EF, LCD_FRAME_A, 0x02, 0x01);
        REG8(0x0011B361UL) = 0x03;
        hold_start(0x0064);
        return 0x00;
    }
    if (REG8(0x0011B361UL) == 0x03) {
        REG8(0x0011B361UL) = 0x00;
        hold_start(0x0064);
        REG8(0xFFFEC5UL) = 0x00;
        screen_switch(0x17, 0x01, 0x00);
    }
    return 0x00;
}

/* H'2227E6. The input trim, adjusted a step at a time.
 *
 * Boxes 1 and 2 are down and up; each lights itself if it is not lit
 * already, moves the trim one step within H'00..H'FF and holds the screen
 * for H'32 ticks, so that leaning on the box repeats. Box 3 accepts, which
 * means one byte written back to the settings block in flash with the
 * flash-busy bit up around it; box 4 cancels by reading the byte back out
 * of flash. Both leave for screen H'27.
 *
 * The H'02 return from the hit test -- a release -- is what puts the two
 * arrow boxes back to normal, and it is the only thing that does. */
u8 trim_screen(void)
{
    u16 value = 0, index = 0;
    const u8 hit = touch_hit(0x0001, 0x0004, &value, &index);

    if (hit != 0x03) {
        if (hit == 0x02) {
            if (hitbox_kind(0x0001) == 0x01) {
                hitbox_set_state(0x0001, 0x0001, 0x00, 0);
            }
            if (hitbox_kind(0x0002) == 0x01) {
                hitbox_set_state(0x0002, 0x0002, 0x00, 0);
            }
        }
        return 0x00;
    }

    if (value == 0x004E) {
        const u8 trim = INPUT_TRIM;

        if (hitbox_kind(0x0001) != 0x01) {
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        }
        if (trim != 0x00) INPUT_TRIM = (u8)(trim - 1);
        hold_start(0x0032);
        return 0x00;
    }

    if (value == 0x004F) {
        const u8 trim = INPUT_TRIM;

        if (hitbox_kind(0x0002) != 0x01) {
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        }
        if (trim < 0xFF) INPUT_TRIM = (u8)(trim + 1);
        hold_start(0x0032);
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0xFFFEDFUL, 0x57FF92UL, 1);
        FLASH_BUSY &= (u8)~0x20;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        message_show_held(index);
        INPUT_TRIM = SETTING_TRIM;
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'21B7DA. The pedal test: the reading and two lamps, redrawn only when
 * something has changed.
 *
 * H'FFFECE is the scaled pedal reading and H'11A1AC the copy of it that is
 * already on the screen; bits 2 and 3 of H'FFFEC4 are the two switches and
 * H'11A1AE and H'11A1B0 their copies, kept as the masked byte widened to a
 * word rather than as a flag. A switch made is a black rectangle and a
 * switch broken a white one, both drawn into the back buffer.
 *
 * All three copies go to H'FFFF on the way out, so that the screen draws
 * itself in full the next time it is entered.
 *
 * The digits go into a six-byte local -- exactly enough for five and the
 * terminator, which is what a word taken as a signed number needs. */
u8 pedal_test_screen(void)
{
    char text[6];
    u16 to = 0;
    const u16 reading = REG16(0x00FFFECEUL);

    if (REG16(0x0011A1ACUL) != reading) {
        int_to_decimal((short)reading, text);
        text_draw(text, 0x0082, 0x00CB, 0x00CD, 0x00D5, 0x0001, 0x02,
                  (const u8 *)0x001196EAUL);
        REG16(0x0011A1ACUL) = REG16(0x00FFFECEUL);
    }

    if ((u16)(REG8(0x00FFFEC4UL) & 0x04) != REG16(0x0011A1AEUL)) {
        draw_rect(0x0083, 0x00A7, 0x0094, 0x00B0, LCD_FRAME_B,
                  (u8)((REG8(0x00FFFEC4UL) & 0x04) ? 0x00 : 0x03), 0x01);
        REG16(0x0011A1AEUL) = (u16)(REG8(0x00FFFEC4UL) & 0x04);
    }

    if ((u16)(REG8(0x00FFFEC4UL) & 0x08) != REG16(0x0011A1B0UL)) {
        draw_rect(0x00C6, 0x00A7, 0x00CE, 0x00B0, LCD_FRAME_B,
                  (u8)((REG8(0x00FFFEC4UL) & 0x08) ? 0x00 : 0x03), 0x01);
        REG16(0x0011A1B0UL) = (u16)(REG8(0x00FFFEC4UL) & 0x08);
    }

    if (screen_leave_check(&to, 0x00) != 0x03) return 0x00;
    if (to != 0x0077) return 0x00;

    REG16(0x0011A1ACUL) = 0xFFFF;
    REG16(0x0011A1AEUL) = 0xFFFF;
    REG16(0x0011A1B0UL) = 0xFFFF;
    REG8(0xFFFEC5UL) = 0x00;
    screen_switch(0x17, 0x01, 0x00);
    return 0x00;
}

/* H'21BA0E. Which machine this is: box 1 says H'AA and box 2 says H'B4, the
 * two values the configuration byte at H'57FF80 takes.
 *
 * Pressing a box only moves the light and the pending value in H'11B360;
 * nothing is written until the screen is left for H'77, and then only if
 * the pending value differs from what is in flash. H'11A1B2 is the "come
 * back and re-light" flag, set on the way out and acted on the next time
 * the screen is entered -- which is what puts the light back on the box
 * flash actually holds.
 *
 * The message number handed to H'211A9E on the way out is the second local,
 * and on that path nothing has written it: it is the box index the hit test
 * below leaves there, from whichever pass last went that way. That is in
 * the original and is reproduced, so the local is deliberately left
 * uninitialised here too. */
u8 variant_screen(void)
{
    u16 value = 0;
    u16 index;      /* read before it is written -- see above */

    if (REG8(0x0011A1B2UL) != 0) {
        hitbox_set_state(0x0001, 0x0001, 0x00, 0);
        hitbox_set_state(0x0002, 0x0002, 0x00, 0);
        REG8(0x0011B360UL) = CONFIG_BLOCK;
        if (REG8(0x0011B360UL) == 0xAA) {
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        } else {
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        }
        REG8(0x0011A1B2UL) = 0x00;
        return 0x00;
    }

    if (screen_leave_check(&value, 0x00) == 0x03 && value == 0x0077) {
        if (REG8(0x0011B360UL) != CONFIG_BLOCK) {
            sew_picture_box();
            settings_save(0x00);
            FLASH_BUSY |= 0x20;
            rom_flash_write((const void *)0x0011B360UL, 0x0057FF80UL, 1);
            FLASH_BUSY &= (u8)~0x20;
        }
        REG8(0x0011A1B2UL) = 0x01;
        message_show_held(index);
        REG8(0xFFFEC5UL) = 0x00;
        screen_switch(0x17, 0x01, 0x00);
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    if (value == 0x0001) {
        hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        hitbox_set_state(0x0002, 0x0002, 0x00, 0);
        REG8(0x0011B360UL) = 0xAA;
        return 0x00;
    }
    if (value == 0x0002) {
        hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        hitbox_set_state(0x0001, 0x0001, 0x00, 0);
        REG8(0x0011B360UL) = 0xB4;
    }
    return 0x00;
}

/* H'22253E. Calibrating the touch panel: two crosses, and the straight line
 * through them.
 *
 * Entering the screen with H'11A1BC set puts the calibration back to unity,
 * so that the two readings taken here are raw panel counts rather than
 * counts already run through the old line. Box H'50 takes the first cross
 * and box H'51 the second, each once -- H'11B3CA and H'11B3CB say which
 * have been taken -- and each beeps.
 *
 * With both in hand the line is worked out and written to flash. The
 * crosses sit at H'28.8 and H'28.8 + H'F0 across, and H'28.8 and
 * H'28.8 + H'A0 down, which is where the H'F0 and H'A0 come from; H'28.8
 * is 40.5, the half pixel putting the cross on a pixel centre.
 *
 * Afterwards the screen goes to H'4A, or to H'17 if bit 7 of H'FFFEC4 is
 * up -- the flag that says the panel was reached from the service menu. */
u8 touch_cal_screen(void)
{
    u16 value = 0, index = 0;

    if (REG8(0x0011A1BCUL) != 0) {
        TOUCH_CAL_X_SCALE  = f2u(1.0f);
        TOUCH_CAL_Y_SCALE  = f2u(1.0f);
        TOUCH_CAL_X_OFFSET = 0;
        TOUCH_CAL_Y_OFFSET = 0;
        REG8(0x0011A1BCUL) = 0x00;
    }

    if (REG8(0x0011B3CAUL) != 0 && REG8(0x0011B3CBUL) != 0) {
        float scale;

        scale = 240.0f / (float)(long)(short)(u16)
                    (REG16(0x0011B3C6UL) - REG16(0x0011B3C2UL));
        TOUCH_CAL_X_SCALE = f2u(scale);

        scale = 160.0f / (float)(long)(short)(u16)
                    (REG16(0x0011B3C8UL) - REG16(0x0011B3C4UL));
        TOUCH_CAL_Y_SCALE = f2u(scale);

        TOUCH_CAL_X_OFFSET = f2u(40.5f -
            (float)(long)(short)REG16(0x0011B3C2UL) * u2f(TOUCH_CAL_X_SCALE));
        TOUCH_CAL_Y_OFFSET = f2u(40.5f -
            (float)(long)(short)REG16(0x0011B3C4UL) * u2f(TOUCH_CAL_Y_SCALE));

        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0x0011A87EUL, 0x0057FFA0UL, 4);
        rom_flash_write((const void *)0x0011A882UL, 0x0057FFA4UL, 4);
        rom_flash_write((const void *)0x0011A886UL, 0x0057FFA8UL, 4);
        rom_flash_write((const void *)0x0011A88AUL, 0x0057FFACUL, 4);
        FLASH_BUSY &= (u8)~0x20;

        REG8(0x0011B3CAUL) = 0x00;
        REG8(0x0011B3CBUL) = 0x00;
        REG8(0x0011A1BCUL) = 0x01;
        screen_stack_pop();

        if (!(REG8(0x00FFFEC4UL) & 0x80)) {
            screen_switch(0x4A, 0x01, 0x00);
        } else {
            REG8(0xFFFEC5UL) = 0x00;
            screen_switch(0x17, 0x01, 0x00);
        }
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    if (value == 0x0050) {
        if (REG8(0x0011B3CAUL) == 0) {
            REG16(0x0011B3C2UL) = REG16(0x0011B102UL);
            REG16(0x0011B3C4UL) = REG16(0x0011B104UL);
            beep(0x001E, 0x0064, 0x01);
            REG8(0x0011B3CAUL) = 0x01;
        }
        return 0x00;
    }
    if (value == 0x0051) {
        if (REG8(0x0011B3CBUL) == 0) {
            REG16(0x0011B3C6UL) = REG16(0x0011B102UL);
            REG16(0x0011B3C8UL) = REG16(0x0011B104UL);
            beep(0x001E, 0x0064, 0x01);
            REG8(0x0011B3CBUL) = 0x01;
        }
    }
    return 0x00;
}

static void max_speed_draw(u16 v)
{
    char text[6];

    int_to_decimal((short)v, text);
    text_draw(text, 0x00A6, 0x0056, 0x00E7, 0x0060, 0x0001, 0x02,
              (const u8 *)0x001196EAUL);
}

u8 max_speed_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(0x0011B37AUL) = SETTING_LIMIT;
        max_speed_draw(REG16(0x0011B37AUL));
    }

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0015) {
        const u16 now = REG16(0x0011B37AUL);

        if ((short)now >= 0x006E) {
            REG16(0x0011B37AUL) = (u16)(now - 10);
            max_speed_draw((u16)(now - 10));
        }
        return 0x00;
    }

    if (value == 0x0016) {
        const u16 now = REG16(0x0011B37AUL);

        if ((u16)(SPEED_CEILING - 10) >= now) {
            REG16(0x0011B37AUL) = (u16)(now + 10);
            max_speed_draw((u16)(now + 10));
        }
        return 0x00;
    }

    if (value == 0x007F) {
        REG16(0x0011B37AUL) = SPEED_CEILING;
        max_speed_draw(REG16(0x0011B37AUL));
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0x0011B37AUL, 0x0057FF94UL, 2);
        FLASH_BUSY &= (u8)~0x20;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'2303EE. The two boxes on the module's own menu, and the eleven ways the
 * second one can go.
 *
 * H'11F533 is what the module last said it was doing, and it decides where
 * box 2 leads: H'07 is a pattern in hand, and then H'114D8F says whether it
 * came back H'08, H'04 or something else; H'08 and H'09 are the two hoop
 * screens; H'02 to H'04 are a pattern being sewn, which is the long branch;
 * and everything else is a machine that has lost its place, so the buffers
 * are cleared and the screen goes back to H'12.
 *
 * The long branch takes the waiting slot, puts two bytes into its second
 * record, picks the screen by H'114D8E and then sends the module two
 * messages -- H'07 only if bit 6 of H'114D51 is up, then H'01 -- waiting
 * for the link to go quiet after each. Both waits are the whole of
 * H'244D64's quiet test spelled out twice over, the same as everywhere
 * else in this cluster.
 */
void link_send_start(void);
void pattern_mark_ready(void);

void module_menu_screen(void)
{
    u16 value = 0, index = 0;
    u8  kind;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return;
    message_show_held(index);

    if (value == 0x0001) {
        if (REG8(0x0011F533UL) == 0x07) {
            REG8(0x00114D8EUL) = 0x07;
            screen_switch(0x37, 0x01, 0x00);
            REG8(0x00114D90UL) = 0x01;
            REG8(0x00114D8FUL) = 0x00;
            REG8(0x0011A177UL) = 0x01;
        } else {
            REG8(0x00114D8EUL) = 0x07;
            REG8(0x00114D72UL) = 0x3E;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
        }
        REG8(0x00114D8FUL) = 0x00;
        REG8(0x0011F534UL) = 0x01;
        screen_stack_pop();
        return;
    }

    if (value != 0x0002) return;

    kind = REG8(0x0011F533UL);
    REG8(0x00114D8EUL) = kind;

    if (kind == 0x07) {
        const u8 back = REG8(0x00114D8FUL);

        if (back == 0x08) {
            REG8(0x00114D90UL) = 0x08;
            screen_switch(0x37, 0x01, 0x00);
        } else if (back == 0x04) {
            REG8(0x00114D90UL) = 0x04;
            screen_switch(0x37, 0x01, 0x00);
        } else {
            REG8(0x00114D8EUL) = 0x07;
            screen_switch(0x37, 0x01, 0x00);
            REG8(0x0011A177UL) = 0x01;
        }
        REG8(0x00114D8FUL) = 0x00;
    } else if (kind == 0x08) {
        screen_switch(0x15, 0x01, 0x00);
        REG8(0x0011A177UL) = 0x01;
        if (REG8(0x00114D8FUL) == 0x0F) REG8(0x00114D84UL) = 0x01;
    } else if (kind == 0x09) {
        screen_switch(0x16, 0x01, 0x00);
        REG8(0x0011A177UL) = 0x01;
        if (REG8(0x00114D8FUL) == 0x0F) REG8(0x00114D84UL) = 0x01;
    } else if (kind >= 0x02 && kind <= 0x04) {
        pattern_slot_begin();
        REG8(PAT_B(0x03)) = REG8(0x00114DA1UL);
        REG8(PAT_B(0x05)) = (u8)(REG8(0x00114D8CUL) / 0x1B);

        if (REG8(0x00114D8EUL) == 0x03) screen_switch(0x14, 0x01, 0x00);
        else                            screen_switch(0x13, 0x01, 0x00);

        REG8(0x00114D7EUL) = 0x01;
        if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        REG8(0x00114D98UL) = 0x00;

        if (module_link_quiet() && (REG8(0x00114D51UL) & 0x40)) {
            REG8(0x0011F2A1UL) = 0x07;
            REG8(0x00114D51UL) &= (u8)~0x40;
            link_send_start();
            while (!module_link_quiet()) loop_tick();
        }
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x01;
            link_send_start();
            while (!module_link_quiet()) loop_tick();
        }
    } else {
        module_buffers_clear();
        REG8(0x00114D8EUL) = 0x01;
        REG8(0x00114D9BUL) = 0x01;
        screen_switch(0x12, 0x01, 0x00);
    }

    REG8(0x00114D8FUL) = 0x00;
    REG8(0x00114D51UL) &= (u8)~0x40;
    screen_stack_pop();
}

static u32 status_foot_record(u32 base)
{
    return REG32(base + (u32)(long)(short)(u16)
                     ((u16)(STATUS_FOOT + 0xFF80) << 3));
}

static void status_mark_clear(void)
{
    draw_rect(0x0032, 0x001E, 0x0046, 0x0025, LCD_FRAME_A, 0x00, 0x01);
}

void status_bar_refresh(u8 redraw_all)
{
    char text[6];

    if (redraw_all != 0) {
        REG16(0x0011B2F2UL) = 0xFFFF;
        REG16(0x0011B2F0UL) = 0xFFFF;
        REG16(0x0011B2F4UL) = 0xFFFF;
        REG16(0x0011B2F6UL) = 0xFFFF;
        REG16(0x0011B2F8UL) = 0xFFFF;
        REG16(0x0011B2FAUL) = 0xFFFF;
    }

    if (STATUS_FOOT != REG16(0x0011B2F2UL) ||
        STATUS_E2_2 != REG16(0x0011B2F0UL) ||
        STATUS_FA_4 != REG16(0x0011B2F8UL) ||
        STATUS_FA_3 != REG16(0x0011B2FAUL)) {
        draw_rect(0x0036, 0x0004, 0x0046, 0x001C, LCD_FRAME_A, 0x00, 0x01);
    }

    if (STATUS_FOOT != REG16(0x0011B2F2UL)) {
        if (REG8(0x00FFFEEBUL) < 0x80) {
            int_to_decimal((short)STATUS_FOOT, text);
            text_draw(text, 0x0011, 0x000D, 0x001F, 0x0016, 0x0001, 0x01,
                      (const u8 *)0x001196EAUL);
            REG16(0x0011B2F0UL) = 0xFFFF;
            REG16(0x0011B2F8UL) = 0xFFFF;
        } else {
            const u32 picture = status_foot_record(0x00115422UL);

            text_draw((const char *)status_foot_record(0x0011541EUL),
                      0x0011, 0x000D, 0x001F, 0x0016, 0x0001, 0x01,
                      (const u8 *)0x001196EAUL);

            if (picture != 0) {
                bitmap_draw(0x0038, 0x0004, 0x0041, 0x001C,
                            (const u8 *)picture, LCD_FRAME_A);
            }
        }
        REG16(0x0011B2F2UL) = STATUS_FOOT;
    }

    if (STATUS_E2_2 != REG16(0x0011B2F0UL)) {
        if (REG8(0x00FFFEE2UL) & 0x04) {
            bitmap_draw(0x0036, 0x000B, 0x0046, 0x001B,
                        (const u8 *)0x0034B898UL, LCD_FRAME_A);
        }
        REG16(0x0011B2F0UL) = STATUS_E2_2;
    }

    if (STATUS_FA_4 != REG16(0x0011B2F8UL)) {
        if (REG8(0x00FFFEFAUL) & 0x10) {
            bitmap_draw(0x0036, 0x0005, 0x0046, 0x001B,
                        (const u8 *)0x0034B96FUL, LCD_FRAME_A);
        }
        REG16(0x0011B2F8UL) = STATUS_FA_4;
    }

    if (STATUS_FA_3 != REG16(0x0011B2FAUL)) {
        if (REG8(0x00FFFEFAUL) & 0x08) {
            bitmap_draw(0x0036, 0x0008, 0x0046, 0x0018,
                        (const u8 *)0x0034B9BAUL, LCD_FRAME_A);
        }
        REG16(0x0011B2FAUL) = STATUS_FA_3;
    }

    if (STATUS_FA_7 != REG16(0x0011B2F4UL)) {
        if (REG8(0x00FFFEFAUL) & 0x80) {
            status_mark_clear();
            bitmap_draw(0x0032, 0x001E, 0x0046, 0x0025,
                        (const u8 *)0x0034B8CFUL, LCD_FRAME_A);
        } else if (!(REG8(0x00FFFEF7UL) & 0x08)) {
            status_mark_clear();
        }
        REG16(0x0011B2F4UL) = STATUS_FA_7;
    }

    if (STATUS_F7_3 != REG16(0x0011B2F6UL)) {
        if (REG8(0x00FFFEF7UL) & 0x08) {
            status_mark_clear();
            bitmap_draw(0x0032, 0x0020, 0x0046, 0x0025,
                        (const u8 *)0x0034B920UL, LCD_FRAME_A);
        } else if (!(REG8(0x00FFFEFAUL) & 0x80)) {
            status_mark_clear();
        }
        REG16(0x0011B2F6UL) = STATUS_F7_3;
    }
}

/* H'21B726. The needle position, in steps of four.
 *
 * H'FFFED8 holds it and H'21B6C6 draws it. Box 1 takes four off but not
 * below zero, box 2 puts four on but not above H'10, and neither redraws
 * unless it moved. The screen is left for H'17 the moment the panel asks
 * for H'77.
 *
 * The two locals share a slot: the screen number the leave check writes and
 * the box value the hit test writes are the same two bytes, which is why
 * the hit test's index goes in the *second* local here and the first in the
 * screens that have no leave check. */
u8 needle_pos_screen(void)
{
    u16 value = 0, index = 0;

    if (screen_leave_check(&value, 0x00) == 0x03 && value == 0x0077) {
        REG8(0xFFFEC5UL) = 0x00;
        screen_switch(0x17, 0x01, 0x00);
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0001) {
        const u8 now = REG8(0x00FFFED8UL);

        if (now != 0) {
            REG8(0x00FFFED8UL) = (u8)(now - 4);
            needle_number_draw();
        }
        return 0x00;
    }
    if (value == 0x0002) {
        const u8 now = REG8(0x00FFFED8UL);

        if (now < 0x10) {
            REG8(0x00FFFED8UL) = (u8)(now + 4);
            needle_number_draw();
        }
    }
    return 0x00;
}

/* H'222DF4. Three runs of screen numbers that want repainting when bit 0 of
 * H'FFFEC4 is up: H'13 to H'16, H'23 to H'24, and H'37 to H'38. Everything
 * else, and a clear bit, leaves H'11A177 alone.
 *
 * The screen number comes back off the stack rather than out of R6L, which
 * is where it arrived -- the routine pushes the register and reads its own
 * low byte again. */
void screen_mark_repaint(u8 screen)
{
    if (!(REG8(0x00FFFEC4UL) & 0x01)) return;

    if (screen < 0x13) return;
    if (screen >= 0x17) {
        if (screen < 0x23) return;
        if (screen >= 0x25) {
            if (screen < 0x37) return;
            if (screen > 0x38) return;
        }
    }
    REG8(0x0011A177UL) = 0x01;
}

/* H'21BD1E. The application's version, drawn once and then left alone.
 *
 * H'11A1B3 is the "not drawn yet" flag: it is cleared on the way in and set
 * again on the way out, so coming back to the screen draws it afresh. The
 * string is the identity block at H'200100 from its fourth byte -- past the
 * "NMM" -- which is six characters and the terminator, and the local it goes
 * into is eight bytes. */
u8 version_screen(void)
{
    char text[8];
    u16 to = 0;

    if (REG8(0x0011A1B3UL) != 0) {
        REG8(0x0011A1B3UL) = 0x00;
        str_copy(text, (const char *)(APP_IDENTITY + 3));
        text_draw(text, 0x0035, 0x0017, 0x005D, 0x001E, 0x0001, 0x01,
                  (const u8 *)0x00119A66UL);
    }

    if (screen_leave_check(&to, 0x00) != 0x03) return 0x00;
    if (to != 0x0077) return 0x00;

    REG8(0x0011A1B3UL) = 0x01;
    REG8(0xFFFEC5UL) = 0x00;
    screen_switch(0x17, 0x01, 0x00);
    return 0x00;
}

/* H'219978. Four boxes: three that go somewhere and one that goes back.
 *
 * Only box H'52 answers 1; the other three answer 0. Going back reads the
 * screen to return to out of H'11B0A6, tells H'222DF4 about it, and puts
 * H'11B114 into H'11B108 on the way. */
u8 menu_four_screens(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0052) { screen_switch(0x2D, 0x01, 0x00); return 0x01; }
    if (value == 0x0053) { screen_switch(0x39, 0x01, 0x00); return 0x00; }
    if (value == 0x0054) { screen_switch(0x0F, 0x01, 0x00); return 0x00; }

    if (value == 0x001A) {
        if (REG8(0x00114DC6UL) & 0x80) return 0x00;
        screen_stack_pop();
        screen_mark_repaint(REG8(0x0011B0A6UL));
        REG16(0x0011B108UL) = REG16(0x0011B114UL);
        screen_switch(REG8(0x0011B0A6UL), 0x01, 0x00);
    }
    return 0x00;
}

u32 module_reply_buffer(void);

/* H'22298C. The version again, this time with the module's.
 *
 * The application's own comes from the identity block as in H'21BD1E; the
 * module's is whatever H'23E45A points at, and it is only drawn if it is not
 * the empty string at H'250ADF. One box, H'1A, goes back to screen H'4A. */
u8 module_version_screen(u8 first_pass)
{
    char text[8];
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        const char *reply;

        str_copy(text, (const char *)(APP_IDENTITY + 3));
        text_draw(text, 0x007B, 0x004C, 0x00E8, 0x005E, 0x0001, 0x01,
                  (const u8 *)0x0011936EUL);

        reply = (const char *)module_reply_buffer();
        if (str_compare(reply, (const char *)0x00250ADFUL) != 0) {
            text_draw(reply, 0x007B, 0x0099, 0x00E8, 0x00AB, 0x0001, 0x01,
                      (const u8 *)0x0011936EUL);
        }
    }

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;

    if (value == 0x001A) {
        message_show_held(index);
        screen_stack_pop();
        screen_switch(0x4A, 0x01, 0x00);
    }
    return 0x00;
}

/* H'21CF7C and H'21CF86. How deep the screen stack is, and what is at a
 * given depth. Entry zero of the stack is the depth itself, which is why
 * both read from the same address. */
u16 screen_stack_depth(void)
{
    return (u16)REG8(0x0011A18BUL);
}

u8 screen_stack_at(u16 n)
{
    return REG8(0x0011A18BUL + (u32)(long)(short)n);
}

/* H'2220DC. The drawing areas cleared and the queue position taken away. */
void drawing_reset(void)
{
    finish_22950C();
    QUEUE_POS = 0xFFFF;
}

/* H'210544 and H'21056C. The help pictures: two tables of H'24-byte records,
 * nine longwords each, one table per machine. H'219DE0 picks the second when
 * the configuration byte says H'AA -- the machine with the module -- so the
 * first is the one without.
 *
 * The record number is multiplied out as a longword and the field number
 * added to the *low half* of it, so the index is a signed word however far
 * the multiplication carried. */
static u32 help_picture_at(u32 table, u16 entry, u16 part)
{
    return REG32(table + (u32)(long)(short)(u16)
                     ((u16)(entry * 0x24) + (u16)(part << 2)));
}

u32 help_picture(u16 entry, u16 part)
{
    return help_picture_at(0x003CFA2CUL, entry, part);
}

u32 help_picture_module(u16 entry, u16 part)
{
    return help_picture_at(0x003D2024UL, entry, part);
}

/* H'21C150. The version string drawn where H'21BD1E draws it, and with the
 * same font. Like H'20E126 it takes its argument back off the stack rather
 * than out of the register it arrived in. */
void version_text_draw(const char *s)
{
    text_draw(s, 0x0035, 0x0017, 0x005D, 0x001E, 0x0001, 0x01,
              (const u8 *)0x00119A66UL);
}

/* H'218C1A. A cursor that blinks: one pass in ten flips it, and H'11B31C
 * says which way it is. The line is H'1B tall and drawn in the visible
 * buffer, so nothing has to be flipped over for it to show. */
void cursor_blink(u16 x, u16 y)
{
    const u16 n = (u16)(REG16(0x0011B31AUL) + 1);

    REG16(0x0011B31AUL) = n;
    if (n != 0x000A) return;

    draw_line(x, y, x, (u16)(y + 0x1A), LCD_FRAME_A,
              (u8)(REG8(0x0011B31CUL) != 0 ? 0x00 : 0x03));

    REG8(0x0011B31CUL) = (u8)(REG8(0x0011B31CUL) == 0 ? 0x01 : 0x00);
    REG16(0x0011B31AUL) = 0x0000;
}

/* H'24A374. Whether the thing that owns the link is still waiting for what
 * it asked for. Six owners have an answer; every other owner, an owner of
 * zero, a state at H'114D86 that is not three, and a clear bit 0 of
 * H'FFFEC4 all say no.
 *
 * H'114DB9 is re-read before each of the six tests rather than kept, which
 * is in the original and makes no difference here. */
u8 link_owner_waiting(void)
{
    if (!(REG8(0x00FFFEC4UL) & 0x01)) return 0x00;
    if (REG8(0x00114DB9UL) == 0) return 0x00;
    if (REG8(0x00114D86UL) != 0x03) return 0x00;

    if (REG8(0x00114DB9UL) == 0x01 && (MACHINE_FLAGS & 0x08)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x02 && !(MACHINE_FLAGS & 0x08)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x11 && !(REG8(0x00114D51UL) & 0x01)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x18 && (REG8(0x00114D51UL) & 0x01)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x10 && !(MACHINE_FLAGS & 0x08)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x1A && !(REG8(0x00FFFEC7UL) & 0x01)) return 0x01;

    return 0x00;
}

/* H'217CCE. One third of a box marked.
 *
 * The box is cleared in the back buffer and then a narrow rectangle drawn
 * inside it in colour 2: the first third at +2, the second at +H'0E, the
 * third at +H'1A, each six wide and running from two below the top to
 * H'20 below it. Any other third does nothing at all, not even the clear.
 *
 * Like H'212E78 the whole H'12-byte entry is copied into a local first and
 * the coordinates read out of the copy. */
void hitbox_third_mark(u16 box, u8 which)
{
    const u32 e = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    u16 copy[9];
    u16 x0, y0, x1, y1;
    int n;

    for (n = 0; n < 9; n++) copy[n] = REG16(e + (u32)(2 * n));

    if      (which == 0x01) x0 = (u16)(copy[0] + 0x02);
    else if (which == 0x02) x0 = (u16)(copy[0] + 0x0E);
    else if (which == 0x03) x0 = (u16)(copy[0] + 0x1A);
    else                    return;

    y0 = (u16)(copy[1] + 0x02);
    x1 = (u16)(x0 + 0x06);
    y1 = (u16)(copy[1] + 0x20);

    draw_rect(copy[0], copy[1], copy[2], copy[3], LCD_FRAME_B, 0x00, 0x01);
    draw_rect(x0, y0, x1, y1, LCD_FRAME_B, 0x02, 0x01);
}

/* H'212FF0. A run of boxes moved along: the value at +8 and the list at
 * +H'0C copied from one box to another, and the state carried over with
 * H'211518 whenever it differs so that the picture follows.
 *
 * Which way the copy runs depends on which way the run moves, so that a
 * source and destination that overlap do not eat themselves: moving down
 * copies from the first box forward, moving up copies from the last box
 * back. The two ends are the same distance apart either way, which is what
 * [last] - [first] + [dest] works out. */
void hitbox_run_shift(u16 first, u16 last, u16 dest)
{
    const u32 table = HITBOX_TABLE;
    short from, to;

    if ((short)first > (short)dest) {
        for (from = (short)first, to = (short)dest;
             from <= (short)last; from++, to++) {
            const u32 d = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)to);
            const u32 s = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)from);

            REG16(d + 0x08) = REG16(s + 0x08);
            REG32(d + 0x0C) = REG32(s + 0x0C);
            if (REG8(d + 0x10) != REG8(s + 0x10)) {
                hitbox_set_state((u16)to, (u16)to, REG8(s + 0x10), 0);
            }
        }
        return;
    }

    if ((short)first < (short)dest) {
        for (from = (short)last,
             to = (short)(u16)((u16)((u16)last - (u16)first) + (u16)dest);
             from >= (short)first; from--, to--) {
            const u32 d = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)to);
            const u32 s = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)from);

            REG16(d + 0x08) = REG16(s + 0x08);
            REG32(d + 0x0C) = REG32(s + 0x0C);
            if (REG8(d + 0x10) != REG8(s + 0x10)) {
                hitbox_set_state((u16)to, (u16)to, REG8(s + 0x10), 0);
            }
        }
    }
}

/* H'21CFB0. The bar beside the balance setting, drawn from the middle of
 * its track outwards.
 *
 * The track runs from H'38 to H'6A below the origin and the bar's far end
 * is the value put through a straight line -- times minus three and an
 * eighth, plus 106 and a half -- so a value of eight lands exactly on the
 * middle at H'51 and larger values go up. The bar itself is colour 2 and
 * whatever is left of the track is cleared, which takes two rectangles when
 * the bar is short and one when it fills its half. */
void balance_bar_draw(u16 value)
{
    const u16 x0  = (u16)(HITBOX_X0 + 0x89);
    const u16 x1  = (u16)(HITBOX_X0 + 0x9B);
    const u16 top = (u16)(HITBOX_Y0 + 0x38);
    const u16 mid = (u16)(HITBOX_Y0 + 0x51);
    const u16 bot = (u16)(HITBOX_Y0 + 0x6A);
    const u16 end = (u16)(HITBOX_Y0 +
        (u16)(int)((float)(long)(short)value * -3.125f + 106.5f));

    draw_rect(x0, mid, x1, end, LCD_FRAME_A, 0x02, 0x01);

    if ((short)end <= (short)mid) {
        if ((short)end > (short)top) {
            draw_rect(x0, top, x1, (u16)(end - 1), LCD_FRAME_A, 0x00, 0x01);
        }
        draw_rect(x0, (u16)(mid + 1), x1, bot, LCD_FRAME_A, 0x00, 0x01);
    } else {
        draw_rect(x0, top, x1, (u16)(mid - 1), LCD_FRAME_A, 0x00, 0x01);
        if ((short)end < (short)bot) {
            draw_rect(x0, (u16)(end + 1), x1, bot, LCD_FRAME_A, 0x00, 0x01);
        }
    }
}

/* H'218378. Two strokes of a preview drawn or rubbed out.
 *
 * There are two tables of H'10-byte entries in RAM, H'1152DE and H'11531E,
 * four entries each: two line segments per entry, x0 y0 x1 y1 twice over.
 * The second argument picks the table and the third picks the colour -- 3
 * to draw, 0 to rub out -- and everything is offset by H'8C across and H'64
 * down, into the back buffer.
 *
 * The original spells all four branches out in full, sixteen table reads
 * apiece, each one recomputing the index. */
void preview_stroke_draw(u16 index, u8 alt, u8 on)
{
    const u32 e = (alt != 0 ? 0x001152DEUL : 0x0011531EUL) +
        (u32)(long)(short)(u16)((u16)(index << 4));
    const u8 colour = (u8)(on != 0 ? 0x03 : 0x00);

    draw_line((u16)(REG16(e + 0x00) + 0x8C), (u16)(REG16(e + 0x02) + 0x64),
              (u16)(REG16(e + 0x04) + 0x8C), (u16)(REG16(e + 0x06) + 0x64),
              LCD_FRAME_B, colour);
    draw_line((u16)(REG16(e + 0x08) + 0x8C), (u16)(REG16(e + 0x0A) + 0x64),
              (u16)(REG16(e + 0x0C) + 0x8C), (u16)(REG16(e + 0x0E) + 0x64),
              LCD_FRAME_B, colour);
}

/* H'213274 and H'213356. A run of boxes scrolled through its list, on and
 * back.
 *
 * Both move the run with H'212FF0 first and then patch up the [step] boxes
 * left over at the far end: on, their slots go up by [step] and any that
 * lands past the end of the list is greyed; back, their slots come down and
 * any that was grey and is now inside the list is un-greyed. The un-greying
 * writes the state byte straight rather than going through H'211518, but
 * the greying does not, so that the picture is drawn.
 *
 * The list's length comes out of whatever list the *first* box points at,
 * before anything has moved. Both answer with box one's slot. */
u16 hitbox_list_scroll_on(u16 first, u16 last, u16 step)
{
    const u32 table = HITBOX_TABLE;
    const u16 length = REG16(REG32(table +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * first) + 0x0C));
    short i;

    hitbox_run_shift((u16)(step + first), last, first);

    for (i = (short)(u16)((u16)last - (u16)(step - 1));
         i <= (short)last; i++) {
        const u32 e = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        if (REG8(e + 0x10) == 0x02) REG8(e + 0x10) = 0x00;
        REG16(e + 0x08) = (u16)(REG16(e + 0x08) + step);
        if ((short)REG16(e + 0x08) > (short)length) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
        }
    }

    hitbox_redraw_run(first, last);
    return REG16(table + 0x1A);
}

u16 hitbox_list_scroll_back(u16 first, u16 last, u16 step)
{
    const u32 table = HITBOX_TABLE;
    const u16 length = REG16(REG32(table +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * first) + 0x0C));
    short i;

    hitbox_run_shift(first, (u16)(last - step), (u16)(step + first));

    for (i = (short)first; (short)(u16)(step + first) > i; i++) {
        const u32 e = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        REG16(e + 0x08) = (u16)(REG16(e + 0x08) - step);
        if (REG8(e + 0x10) == 0x02 &&
            (short)REG16(e + 0x08) <= (short)length) {
            REG8(e + 0x10) = 0x00;
        }
    }

    hitbox_redraw_run(first, last);
    return REG16(table + 0x1A);
}

/* ---- the screen bodies -------------------------------------------------
 * H'22382A dispatches to one of seventy-one of them through a table of
 * seventy-nine, and each is the whole of what one screen does on every pass:
 * lay itself out the first time, then read the panel and keep itself up to
 * date. They are written here as they are reconstructed.
 */

/* H'223A50. The sewing screen -- H'02, and H'07 which is the same screen
 * with the queue's strip beside it.
 *
 * Three parts, and the first two only run when something has asked for
 * them. H'11B0A8 means "just arrived": the four background pictures are
 * copied out of H'116A1A, every box is put back to plain, the second buffer
 * is wiped and the background is unpacked into the first, and then the
 * pattern strip, the four category boxes and the arrows beside them are
 * filled in. H'11B0A9 means "lay the panel out again", which is the two
 * bars, the width strip and the six boxes of whichever strip the pattern
 * calls for.
 *
 * The third part runs every pass: the two bars redrawn where they have
 * moved, the press read, and the four things that keep themselves up to
 * date given a chance to. A menu key waiting in H'11A170 is dealt with
 * last, because it changes what the strip is showing.
 */
void screen_body_02(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00116A1AUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_FRAME_A);

        hitbox_fill_from_list(0x0001, 0x000F, REG16(0x0011B108UL), MENU_LIST);
        hitbox_fill_from_list(0x0016, 0x0019, 0x0001, 0x00115A06UL);
        list_arrows(0x0001, 0x0005, 0x0018, 0x0019, 0x01);
        status_bar_refresh(0x01);

        if (REG8(0x0011A174UL) != 0) {
            pattern_strip_restore(0x07);
            hitbox_select_current(0x0021, 0x0025);
        }
        needle_stop_picture();

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        width_strip_draw(0x01);
        hitbox_select_current(0x0001, 0x000F);

        panel_strip_choose();
        hitbox_fill_from_list(0x0010, 0x0015, REG16(0x0011B10CUL),
                              REG32(0x0011A196UL));
        panel_strip_draw(0x0010, 0x0015, 0x01);

        if (REG8(0x0011A174UL) != 0) {
            picker_strip_restore();
            picker_cursor(0x03);
            picker_arrows(0x001E, 0x001F, 0x01);
        }
        REG8(0x0011B0A9UL) = 0x00;
    }

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);

    screen_touch();

    panel_strip_draw(0x0010, 0x0015, 0x00);
    list_arrows(0x0001, 0x0005, 0x0018, 0x0019, 0x00);
    if (REG8(0x0011A169UL) == 0x07) picker_arrows(0x001E, 0x001F, 0x00);
    width_strip_draw(0x00);
    status_bar_refresh(0x00);

    if (REG8(0x0011A170UL) != 0) menu_repick();
}

/* H'223CCA. The two menu screens -- H'03, and H'04 which is the same screen
 * with the queue's strip beside it.
 *
 * The same three parts as H'223A50 above, and the same order, but a
 * different screen: the background comes from H'116FD0, the strip is the
 * twenty boxes H'01 to H'14 filled from the second menu list rather than
 * fifteen from the first, the panel is the six boxes H'19 to H'1E, and the
 * queue's arrows are H'23 and H'24.
 *
 * The one thing it does not do is take up a waiting menu key: H'223A50 ends
 * by calling H'212C60 when H'11A170 is up, and this screen -- which *is* the
 * menu -- has no strip to re-pick.
 */
void screen_body_03(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00116FD0UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_FRAME_A);

        hitbox_fill_from_list(0x0001, 0x0014, REG16(0x0011B108UL),
                              REG32(0x0011B096UL));
        hitbox_fill_from_list(0x0015, 0x0018, 0x0001, 0x00115A06UL);
        list_arrows(0x0001, 0x0005, 0x0017, 0x0018, 0x01);
        status_bar_refresh(0x01);

        if (REG8(0x0011A174UL) != 0) pattern_strip_restore(0x04);
        needle_stop_picture();

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        width_strip_draw(0x01);
        hitbox_select_current(0x0001, 0x0014);

        panel_strip_choose();
        hitbox_fill_from_list(0x0019, 0x001E, REG16(0x0011B10CUL),
                              REG32(0x0011A196UL));
        panel_strip_draw(0x0019, 0x001E, 0x01);

        if (REG8(0x0011A174UL) != 0) {
            picker_strip_restore();
            picker_cursor(0x03);
            picker_arrows(0x0023, 0x0024, 0x01);
        }
        REG8(0x0011B0A9UL) = 0x00;
    }

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);

    screen_touch();

    panel_strip_draw(0x0019, 0x001E, 0x00);
    list_arrows(0x0001, 0x0005, 0x0017, 0x0018, 0x00);
    if (REG8(0x0011A169UL) == 0x04) picker_arrows(0x0023, 0x0024, 0x00);
    width_strip_draw(0x00);
    status_bar_refresh(0x00);
}

/* ---- the plain screens -------------------------------------------------
 * The bodies from here down are shorter than the four above: most of them
 * are a background unpacked once and a press handler run every pass, with
 * nothing that keeps itself up to date in between.
 */

/* H'2239EC. Screen H'00, the touch calibration.
 *
 * It pushes the screen it came from -- the only way back is through the
 * calibration finishing -- and it is the one body that unpacks its
 * background straight into the front buffer and wipes the back one
 * afterwards, rather than the other way round. There is no lay-out pass:
 * H'22253E does the whole screen itself.
 */
void screen_body_00(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00115CDEUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_FRAME_A);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);

        REG8(0x0011B0A8UL) = 0x00;
    }

    touch_cal_screen();
}

/* H'224220 and H'2242E8. The two category menus, H'05 and H'06.
 *
 * Both are the same shape: the background is unpacked into the scratch
 * buffer and the piece of it the four words at H'11B0B2 describe is copied
 * into the front one, so that a picture larger than its place on the screen
 * can be cut down to it. The lay-out pass is one call -- the item preview --
 * and the press is the screen's own handler.
 */
static void menu_screen_lay_out(u32 block)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = block;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        item_preview(REG16(0x00FFFEE0UL));
        REG8(0x0011B0A9UL) = 0x00;
    }
}

void screen_body_05(void)
{
    menu_screen_lay_out(0x0011705EUL);
    (void)menu_six_categories();
}

void screen_body_06(void)
{
    menu_screen_lay_out(0x00117146UL);
    (void)menu_ten_categories();
}

/* H'2243B0 and H'224478, screens H'25 and H'26: the same again with their
 * own backgrounds and their own press handlers. */
void screen_body_25(void)
{
    menu_screen_lay_out(0x0011720AUL);
    menu_category();
}

void screen_body_26(void)
{
    menu_screen_lay_out(0x0011721AUL);
    (void)menu_five_categories();
}

/* H'224644, screen H'3E: a message put up and waited on.
 *
 * The picture is blitted into the scratch buffer, the same rectangle is
 * blacked out in the back one, and the piece is copied into the front. It
 * lowers H'11A179 -- "a message may go up" -- on the way in, so one message
 * cannot land on top of another, and the wait only runs while H'11A17D is
 * down.
 */
void screen_body_3E(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00115D12UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    (const u8 *)REG32(0x0011B0AEUL), LCD_SCRATCH);
        draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                  REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                  LCD_FRAME_B, 0x00, 0x01);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011A179UL) = 0x00;
    }

    if (REG8(0x0011A17DUL) == 0) message_wait_screen();
}

/* H'224AC6, screen H'0D: one picture chosen out of seven, with the two bars
 * beside it.
 *
 * The lay-out is the box blacked out first and the picture unpacked into
 * the scratch buffer afterwards -- the other way round from H'3E above --
 * and the panel's fresh call is the "everything zeroed" one, H'01 to H'01,
 * rather than the run the pass itself draws.
 */
void screen_body_0D(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00116032UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                  REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                  LCD_FRAME_B, 0x00, 0x01);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        item_preview(REG16(0x00FFFEE0UL));

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        hitbox_fill_from_list(0x0009, 0x000A, 0x0001, 0x00115F44UL);
        width_strip_draw(0x01);
        panel_strip_draw(0x0001, 0x0001, 0x01);
        REG8(0x0011B0A9UL) = 0x00;
    }

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);
    panel_strip_draw(0x0009, 0x000A, 0x00);
    width_strip_draw(0x00);
    picture_choice_screen();
}

/* H'224C98, screen H'0B: not a screen at all, but a fork.
 *
 * The two bits of H'FFFEFA that say which of the two needle-position
 * settings is in force pick between screens H'0C and H'0D, and the body
 * does nothing else -- it never draws, and the screen it chooses draws
 * itself on the next pass. The three rotations and two masks are the
 * original's own way of reaching bits 6 and 5.
 */
void screen_body_0B(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u8 which = REG8(0x00FFFEFAUL);
        u8 i;

        REG8(0x0011B0A8UL) = 0x00;

        for (i = 0; i < 3; i++) {
            which = (u8)((u8)(which << 1) | (u8)(which >> 7));
        }
        which = (u8)(which & 0x07);
        which = (u8)(which & 0x03);

        if (which == 0x01) screen_switch(0x0C, 0x01, 0x00);
        else               screen_switch(0x0D, 0x01, 0x00);
    }
}

/* H'224CE2, screen H'0E: one of the four "which screen" menus.
 *
 * The picture is not in the block this time -- the block is only the
 * rectangle and the box table -- and comes from the sixth of the tables the
 * display bring-up filled in, at +H'04. The one thing it does besides
 * drawing is grey the first box out on a machine whose stitch data is the
 * H'AA set rather than the H'B4 one.
 */
void screen_body_0E(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x0011609CUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(TABLE_SLOT_6 + 0x04), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        if (STITCH_SET == 0xAA) hitbox_set_state(0x0001, 0x0001, 0x02, 0);

        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)menu_four_screens();
}

/* H'224DB4, screen H'0F: twelve boxes filled from the eighth table, with
 * arrows either side.
 *
 * The arrows are drawn fresh on the way in and again every pass, which is
 * the only reason the lay-out and the pass are not the same code.
 */
void screen_body_0F(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x0011658EUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(TABLE_SLOT_6 + 0x08), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        hitbox_fill_from_list(0x0001, 0x000C, 0x0001, TABLE_SLOT_8);
        list_arrows(0x0001, 0x0003, 0x000D, 0x000F, 0x01);

        REG8(0x0011B0A8UL) = 0x00;
    }

    list_arrows(0x0001, 0x0003, 0x000D, 0x000F, 0x00);
    (void)pattern_list_screen();
}

/* ---- the four "pick one" screens ---------------------------------------
 * H'2250FA, H'2252B6, H'22536A and H'22541E -- screens H'39, H'3B, H'3C and
 * H'3D. One shape between them: the block, the picture taken from the table
 * at H'11B2A6 at an offset of its own, and a press handler of its own that
 * is called once with H'01 on the way in and once with H'00 every pass.
 *
 * H'39 is the one with something extra: two boxes greyed, and greyed again
 * on the H'AA stitch set -- the second call changes nothing, because the
 * first has already put them in that state, but it is what the original
 * does.
 */
static void pick_screen_lay_out(u32 block, u16 offset)
{
    u32 src = block;
    u32 dst = 0x0011B0AEUL;
    u8 n;

    for (n = 4; n != 0; n--) {
        REG32(dst) = REG32(src);
        src += 4;
        dst += 4;
    }

    hitbox_reset_all();
    lcd_buffer_fill(LCD_FRAME_B, 0x00);
    image_load(REG32(REG32(0x0011B2A6UL) + (u32)offset), LCD_SCRATCH);
    region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
}

void screen_body_39(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        pick_screen_lay_out(0x00116184UL, 0x0004);
        hitbox_set_state(0x0009, 0x000A, 0x02, 0);
        if (STITCH_SET == 0xAA) hitbox_set_state(0x0009, 0x000A, 0x02, 0);
        REG8(0x0011B0A8UL) = 0x00;
        (void)pick_screen_1(0x01);
    }
    (void)pick_screen_1(0x00);
}

void screen_body_3B(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        pick_screen_lay_out(0x001162B2UL, 0x0008);
        REG8(0x0011B0A8UL) = 0x00;
        (void)pick_screen_2(0x01);
    }
    (void)pick_screen_2(0x00);
}

void screen_body_3C(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        pick_screen_lay_out(0x0011639AUL, 0x000C);
        REG8(0x0011B0A8UL) = 0x00;
        (void)pick_screen_3(0x01);
    }
    (void)pick_screen_3(0x00);
}

void screen_body_3D(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        pick_screen_lay_out(0x0011644CUL, 0x0010);
        REG8(0x0011B0A8UL) = 0x00;
        (void)pick_screen_4(0x01);
    }
    (void)pick_screen_4(0x00);
}

/* H'225FFA, screen H'19: the needle-position setting.
 *
 * The only body that picks its own picture rather than taking one out of a
 * block: H'32B7D0 on the H'B4 machine and H'32CDA2 on the other. Both
 * buffers are wiped first, which none of the others do -- there is no
 * background to keep.
 */
void screen_body_19(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00118280UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_A, 0x00);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);

        if (STITCH_SET == 0xB4) image_load(0x0032B7D0UL, LCD_SCRATCH);
        else                    image_load(0x0032CDA2UL, LCD_SCRATCH);

        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        speed_number_draw(0x01);
        needle_number_draw();

        REG8(0x0011B0A8UL) = 0x00;
    }

    speed_number_draw(0x00);
    (void)needle_pos_screen();
}

/* H'2260E0 and H'22613A, screens H'1A and H'1B: the pedal test and the one
 * that only leaves for H'77.
 *
 * Neither has a block at all: the picture is a constant and goes straight
 * into the front buffer, with no rectangle copied out of the scratch one.
 */
static void plain_picture_lay_out(u32 picture)
{
    lcd_buffer_fill(LCD_FRAME_A, 0x00);
    lcd_buffer_fill(LCD_FRAME_B, 0x00);
    image_load(picture, LCD_FRAME_A);
    speed_number_draw(0x01);
}

void screen_body_1A(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        plain_picture_lay_out(0x0032E100UL);
        REG8(0x0011B0A8UL) = 0x00;
    }

    speed_number_draw(0x00);
    (void)pedal_test_screen();
}

void screen_body_1B(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        plain_picture_lay_out(0x0032E610UL);
        REG8(0x0011B0A8UL) = 0x00;
    }

    speed_number_draw(0x00);
    (void)screen_only_77();
}

/* H'22623A, screen H'1D: the display test. Nothing but the back buffer
 * wiped and the block copied; H'21BBE6 draws the whole thing itself. */
void screen_body_1D(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x0011823AUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        lcd_buffer_fill(LCD_FRAME_B, 0x00);

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)display_test();
}

/* H'226282, screen H'1E: the version screen. Both buffers wiped and one
 * picture blitted at coordinates of its own -- there is no block. */
void screen_body_1E(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        lcd_buffer_fill(LCD_FRAME_A, 0x00);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        bitmap_draw(0x000B, 0x0011, 0x0027, 0x0026,
                    (const u8 *)0x0034C08CUL, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)version_screen();
}

/* H'226394, screen H'20: moving the hoop. The block's own picture, and the
 * back buffer wiped after it rather than before. */
void screen_body_20(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001183C0UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)hoop_move_screen();
}

/* H'2264AE, screen H'22: the demonstration. Two wiped buffers and a step
 * called once on the way in and once every pass, the same shape as the four
 * "pick one" screens. */
void screen_body_22(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        lcd_buffer_fill(LCD_FRAME_A, 0x00);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        REG8(0x0011B0A8UL) = 0x00;
        (void)demo_screen_step(0x01);
    }

    (void)demo_screen_step(0x00);
}

/* H'2265F4, screen H'4A: four ways out, over a picture from the table at
 * H'11B2AE. */
void screen_body_4A(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00118536UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(REG32(0x0011B2AEUL) + 0x0C), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)menu_four_ways();
}

/* H'2266A4, screen H'48: one of the module's choice screens. Its picture is
 * blitted rather than unpacked -- it is already a bitmap, at +H'50 of the
 * table at H'11B2A2 -- and the back buffer is blacked out over the same
 * rectangle rather than wiped whole. */
void screen_body_48(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00118CFEUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    (const u8 *)REG32(REG32(0x0011B2A2UL) + 0x50), LCD_SCRATCH);
        draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                  REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                  LCD_FRAME_B, 0x00, 0x01);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)choice_screen_17A();
}

/* H'224ECC, screen H'10: twelve boxes and a picture chosen by H'11B0FE.
 *
 * The picture is the H'11B0FE'th pointer of the table at H'11B2C6, and its
 * own header gives its size: it is drawn from H'61, H'BE outwards. A null
 * pointer there simply leaves that part of the screen alone.
 */
void screen_body_10(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00116688UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        hitbox_set_state(0x0001, 0x000C, 0x00, 0);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(TABLE_SLOT_6 + 0x0C), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        if (REG32(TABLE_SLOT_10 +
                  (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011B0FEUL) << 2)))
            != 0) {
            const u32 picture = REG32(TABLE_SLOT_10 +
                (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011B0FEUL) << 2)));

            bitmap_draw(0x0061, 0x00BE,
                        (u16)(header_word_0((const u8 *)picture) + 0x0061),
                        (u16)(header_word_1((const u8 *)picture) + 0x00BE),
                        (const u8 *)picture, LCD_FRAME_A);
        }

        if (STITCH_SET == 0xAA) hitbox_set_state(0x000B, 0x000B, 0x02, 0);

        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)menu_twelve_choice();
}

/* H'2268A4, H'2269B6, screens H'49 and H'4C: two more of the module's
 * choice screens, the same shape as H'48 with their own picture and their
 * own press.
 *
 * H'49 is the one that ends two ways: with the module's bit up it goes
 * through the tail that remembers where the machine is, and without it
 * through the one that does not. The answer says which -- H'01 for the
 * ordinary tail. */
static void module_choice_lay_out(u32 block, u16 offset)
{
    u32 src = block;
    u32 dst = 0x0011B0AEUL;
    u8 n;

    screen_stack_push();

    for (n = 4; n != 0; n--) {
        REG32(dst) = REG32(src);
        src += 4;
        dst += 4;
    }

    hitbox_reset_all();
    bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                (const u8 *)REG32(REG32(0x0011B2A2UL) + (u32)offset),
                LCD_SCRATCH);
    draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
              REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
              LCD_FRAME_B, 0x00, 0x01);
    region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
}

u8 screen_body_49(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        module_choice_lay_out(0x00118D0EUL, 0x0048);
        REG8(0x0011B0A8UL) = 0x00;
    }

    if (REG8(0x00FFFEC4UL) & 0x01) {
        (void)menu_embroidery();
        return 0x01;
    }

    (void)choice_screen_1E3();
    return 0x00;
}

void screen_body_4C(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        module_choice_lay_out(0x00118D1EUL, 0x004C);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)module_menu_screen();
}

/* H'226AB6 and H'226BA0, screens H'28 and H'29: two settings screens.
 *
 * Both wipe the two buffers *after* the picture has gone into the scratch
 * one rather than before, which is only visible if something else was
 * already there. H'28 takes a bitmap out of the table at H'11B2B2 and H'29
 * an unpacked picture out of the one at H'11B2AE.
 */
void screen_body_28(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001185A0UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    (const u8 *)REG32(REG32(0x0011B2B2UL) + 0x04),
                    LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_A, 0x00);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        (void)setting_toggle_C7(0x01);
    }

    (void)setting_toggle_C7(0x00);
}

void screen_body_29(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001186ACUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(REG32(0x0011B2AEUL) + 0x04), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_A, 0x00);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        (void)beep_settings_screen(0x01);
    }

    (void)beep_settings_screen(0x00);
}

/* H'226C64 and H'226F16, screens H'2A and H'2C: the top speed and the foot
 * pressure.
 *
 * Both take their bitmap out of the block itself and both stow the screen
 * they came from in store H'03 on the way in -- these are the two settings
 * that can be reached from more than one place.
 */
static void setting_screen_lay_out(u32 block)
{
    u32 src = block;
    u32 dst = 0x0011B0AEUL;
    u8 n;

    for (n = 4; n != 0; n--) {
        REG32(dst) = REG32(src);
        src += 4;
        dst += 4;
    }

    hitbox_reset_all();
    bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                (const u8 *)REG32(0x0011B0AEUL), LCD_SCRATCH);
    screen_store(0x03, 0x00);
    draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
              REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
              LCD_FRAME_B, 0x00, 0x01);
    region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
}

void screen_body_2A(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        setting_screen_lay_out(0x00118728UL);
        REG8(0x0011B0A8UL) = 0x00;
        (void)max_speed_screen(0x01);
    }

    (void)max_speed_screen(0x00);
}

void screen_body_2C(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        setting_screen_lay_out(0x0011880CUL);
        REG8(0x0011B0A8UL) = 0x00;
        (void)foot_pressure_screen(0x01);
    }

    (void)foot_pressure_screen(0x00);
}

/* H'224540, screen H'08: the big packed picture.
 *
 * The only screen that both remembers where it came from on the way in and
 * draws its background with the LZW decoder. The picture goes into the
 * scratch buffer, the same rectangle is filled black in the back one, and
 * then the rectangle is copied to the front.
 */
void screen_body_08(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00115D12UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        sew_picture_box();
        bitmap_draw_lzw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                        REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                        (const u8 *)REG32(0x0011B0AEUL), LCD_SCRATCH);
        draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                  REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                  LCD_FRAME_B, 0x00, 0x01);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011A179UL) = 0x00;
    }

    (void)message_wait_screen();
}

/* H'2251F0, screen H'3A: the other packed picture, and the whole screen of
 * it -- H'0000 to H'013F across, which is the decoder's straight-run path. */
void screen_body_3A(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001161B8UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        sew_picture_box();
        bitmap_draw_lzw(0x0000, 0x0000, 0x013F, 0x00EF,
                        (const u8 *)REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)screen_back_one();
}

/* H'22711E, screen H'2E: the panel strip chosen. The block's own picture
 * unpacked into the scratch buffer and the piece of it the four words at
 * H'11B0B2 describe copied into the front one, with H'21E082 called once on
 * the way in and once every pass. */
void screen_body_2E(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00118AC8UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        (void)panel_marks_screen(0x01);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)panel_marks_screen(0x00);
}

/* H'225C30 and H'225DC6, screens H'17 and H'18: the service screens.
 *
 * Both have the ordinary block-and-picture lay-out, both fill their boxes
 * from the same two lists, and both draw the two bars, the width strip, the
 * ten service marks and the speed number, all with the flag up; then every
 * pass draws the marks, the number, the strip and the two bars again.
 *
 * H'17 is the main menu underneath; H'18 has the pattern strip as well, and
 * its own press is the one that only answers H'77.
 */
void screen_body_17(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001181B0UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        (void)hitbox_fill_from_list(0x0010, 0x0015, 0x0001, 0x00115A20UL);
        (void)hitbox_fill_from_list(0x0016, 0x0017, 0x0001, 0x00115A06UL);

        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        width_strip_draw(0x01);
        service_marks_draw(0x01);
        speed_number_draw(0x01);

        REG8(0x0011B0A8UL) = 0x00;
    }

    service_marks_draw(0x00);
    speed_number_draw(0x00);
    width_strip_draw(0x00);
    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);
    (void)main_menu_screen();
}

void screen_body_18(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001181C0UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);

        (void)hitbox_fill_from_list(0x0001, 0x000F, 0x0001, 0x0011A88EUL);
        (void)hitbox_fill_from_list(0x0010, 0x0015, 0x0001, 0x00115A20UL);
        (void)hitbox_fill_from_list(0x0016, 0x0019, 0x0001, 0x00115A06UL);

        list_arrows(0x0001, 0x0005, 0x0018, 0x0019, 0x01);
        hitbox_select_current(0x0001, 0x000F);
        panel_strip_draw(0x0010, 0x0015, 0x01);

        width_strip_draw(0x01);
        service_marks_draw(0x01);
        speed_number_draw(0x01);

        REG8(0x0011B0A8UL) = 0x00;
    }

    service_marks_draw(0x00);
    speed_number_draw(0x00);
    width_strip_draw(0x00);
    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);

    (void)module_busy_screen();
    panel_strip_draw(0x0010, 0x0015, 0x00);
    list_arrows(0x0001, 0x0005, 0x0018, 0x0019, 0x00);
}

/* H'227DBE, screen H'42: the queue's editing panel.
 *
 * The picture goes into the scratch buffer and the same rectangle -- the top
 * H'9F rows from x H'5B across -- is blacked out in the back one and copied
 * to the front. Then the two bars, the width strip, the arrows and the panel
 * itself, all with their "draw the lot" flag up.
 *
 * Every pass after that draws the two bars again without the flag, runs the
 * panel's press, and draws the arrows and the strip again.
 */
void screen_body_42(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00119194UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        draw_rect(0x005B, 0x0000, 0x013F, 0x009E, LCD_FRAME_B, 0x00, 0x01);
        bitmap_draw(0x005B, 0x0000, 0x013F, 0x009E,
                    (const u8 *)REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(0x005B, 0x0000, 0x013F, 0x009E, 0x0000,
                    LCD_SCRATCH, LCD_FRAME_A);

        status_bar_refresh(0x01);
        needle_stop_picture();

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        width_strip_draw(0x01);
        picker_arrows(0x000C, 0x000D, 0x01);
        queue_edit_screen(0x01);
        hitbox_set_state(0x0001, 0x0007, 0x00, 0);
        hitbox_set_state(0x0001, 0x0007, 0x05, 0);
        picker_cursor(0x03);
        REG8(0x0011B0A9UL) = 0x00;
    }

    (void)screen_touch();

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);
    queue_edit_screen(0x00);
    picker_arrows(0x000C, 0x000D, 0x00);
    width_strip_draw(0x00);
    status_bar_refresh(0x00);
}

/* H'227FC4, screen H'43: the queue as a strip.
 *
 * The block's picture goes into the scratch buffer through the ordinary
 * run-length decoder, the same rectangle is blacked out in the back one so
 * the cursor has somewhere to be drawn, and the rectangle is copied to the
 * front. The corners are constants here rather than the block's own.
 */
void screen_body_43(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001191ECUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        bitmap_draw(0x000B, 0x0010, 0x0134, 0x00DB,
                    (const u8 *)REG32(0x0011B0AEUL), LCD_SCRATCH);
        draw_rect(0x000B, 0x0010, 0x0134, 0x00DB, LCD_FRAME_B, 0x00, 0x01);
        region_copy(0x000B, 0x0010, 0x0134, 0x00DB, 0x0010,
                    LCD_SCRATCH, LCD_FRAME_A);

        picker_cursor(0x01);
        queue_strip_screen(0x01);

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) REG8(0x0011B0A9UL) = 0x00;

    queue_strip_screen(0x00);
}

/* H'2280CA, screens H'46 and H'47: the number keypad.
 *
 * The lay-out is the ordinary block-and-picture one, and after it the
 * keypad's own routine is called once to set the field up. What is extra is
 * the second flag at H'11B0A9: on H'47, with a queue being built and no
 * queue edit in progress, the pattern strip is put back and its two arrows
 * drawn, and every pass after that the arrows are drawn again.
 */
void screen_body_46(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00119352UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        number_keypad_screen(0x01);

        if (REG8(0x0011A174UL) != 0 && REG8(0x0011A178UL) == 0) {
            pattern_strip_restore(0x47);
        }

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        if (REG8(0x0011A174UL) != 0 && REG8(0x0011A178UL) == 0) {
            picker_strip_restore();
            picker_cursor(0x03);
            picker_arrows(0x000E, 0x000F, 0x01);
        }
        REG8(0x0011B0A9UL) = 0x00;
    }

    (void)screen_touch();
    number_keypad_screen(0x00);

    if (REG8(0x0011A169UL) == 0x47) picker_arrows(0x000E, 0x000F, 0x00);
}

/* H'2271CC, screen H'2F: another toggle, with one of two pictures depending
 * on the stitch set. */
void screen_body_2F(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00118B32UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();

        if (STITCH_SET == 0xB4) {
            bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                        REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                        (const u8 *)REG32(REG32(0x0011B2B2UL) + 0x08),
                        LCD_SCRATCH);
        } else {
            bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                        REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                        (const u8 *)REG32(REG32(0x0011B2B2UL) + 0x0C),
                        LCD_SCRATCH);
        }

        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        (void)setting_toggle_C6(0x01);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)setting_toggle_C6(0x00);
}

/* H'2272F6, screen H'01: the thread trimmer. The same lay-out as the two
 * settings screens above, with the screen it came from pushed as well. */
void screen_body_01(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        screen_stack_push();
        setting_screen_lay_out(0x00118B9CUL);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)trim_screen();
}

/* H'2273FC, screen H'32: three lists to choose between.
 *
 * The back buffer is wiped before the picture is unpacked rather than
 * after, which is the other way round from the other menus, and the second
 * and third boxes are greyed on the H'AA stitch set.
 */
void screen_body_32(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117272UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        if (STITCH_SET == 0xAA) hitbox_set_state(0x0002, 0x0003, 0x02, 0);

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        item_preview(REG16(0x00FFFEE0UL));
        REG8(0x0011B0A9UL) = 0x00;
    }

    (void)menu_three_lists();
}

/* H'2274EA, screens H'33, H'34, H'35 and H'36: the four alphabet screens.
 *
 * One body for four screens, and it tells them apart three times over: H'35
 * and H'36 take their list from H'11B09A and the picture at H'300782, H'33
 * and H'34 from H'11B09E and H'300A47; the pattern strip is put back with
 * H'36 for the first pair and H'34 for the second; and each pair has its own
 * "what has been typed" routine, run on the way in, again when the panel is
 * laid out, and once more every pass.
 *
 * H'34 and H'36 are the two with the queue's arrows in the tail.
 */
static u8 alphabet_pair_a(void)
{
    return (u8)(REG8(0x0011A169UL) >= 0x35 && REG8(0x0011A169UL) <= 0x36);
}

static u8 alphabet_pair_b(void)
{
    return (u8)(REG8(0x0011A169UL) >= 0x33 && REG8(0x0011A169UL) <= 0x34);
}

void screen_body_33(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x0011749EUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_FRAME_A);

        if (REG8(0x0011A169UL) == 0x35 || REG8(0x0011A169UL) == 0x36) {
            hitbox_fill_from_list(0x0001, 0x0008, REG16(0x0011B108UL),
                                  REG32(0x0011B09AUL));
            bitmap_draw(0x005E, 0x005E, 0x00E8, 0x00C1,
                        (const u8 *)0x00300782UL, LCD_FRAME_A);
        } else {
            hitbox_fill_from_list(0x0001, 0x0008, REG16(0x0011B108UL),
                                  REG32(0x0011B09EUL));
            bitmap_draw(0x005E, 0x005E, 0x00E8, 0x00C1,
                        (const u8 *)0x00300A47UL, LCD_FRAME_A);
        }

        hitbox_fill_from_list(0x0009, 0x000C, 0x0001, 0x00115A06UL);
        list_arrows(0x0001, 0x0002, 0x000B, 0x000C, 0x01);
        hitbox_select_current(0x0001, 0x0008);
        status_bar_refresh(0x01);

        if (REG8(0x0011A174UL) != 0) {
            if (REG8(0x0011A169UL) == 0x35 || REG8(0x0011A169UL) == 0x36) {
                pattern_strip_restore(0x36);
            }
            if (REG8(0x0011A169UL) == 0x33 || REG8(0x0011A169UL) == 0x34) {
                pattern_strip_restore(0x34);
            }
            hitbox_select_current(0x0013, 0x0014);
        }

        needle_stop_picture();

        if (alphabet_pair_a())      (void)stroke_pick_screen_a(0x01);
        else if (alphabet_pair_b()) (void)stroke_pick_screen_b(0x01);

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        width_strip_draw(0x01);

        panel_strip_choose();
        hitbox_fill_from_list(0x000D, 0x0012, REG16(0x0011B10CUL),
                              REG32(0x0011A196UL));
        panel_strip_draw(0x000D, 0x0012, 0x01);
        hitbox_select_current(0x0001, 0x0008);

        if (alphabet_pair_a())      (void)stroke_pick_screen_a(0x01);
        else if (alphabet_pair_b()) (void)stroke_pick_screen_b(0x01);

        if (REG8(0x0011A174UL) != 0) {
            picker_strip_restore();
            picker_cursor(0x03);
            picker_arrows(0x0019, 0x001A, 0x01);
        }

        REG8(0x0011B0A9UL) = 0x00;
    }

    list_arrows(0x0001, 0x0002, 0x000B, 0x000C, 0x00);

    if (REG8(0x0011A169UL) == 0x36 || REG8(0x0011A169UL) == 0x34) {
        picker_arrows(0x0019, 0x001A, 0x00);
    }

    width_strip_draw(0x00);
    status_bar_refresh(0x00);
    screen_touch();
    panel_strip_draw(0x000D, 0x0012, 0x00);

    if (alphabet_pair_a())      (void)stroke_pick_screen_a(0x00);
    else if (alphabet_pair_b()) (void)stroke_pick_screen_b(0x00);

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);
}

/* H'226194, screen H'1C: the motor test. The back buffer is wiped before
 * the picture is unpacked rather than after. */
void screen_body_1C(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00118206UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)variant_screen();
}

/* H'2264EE, screen H'27: the queue's own menu.
 *
 * Box H'0B is greyed either way; on the H'AA stitch set box H'06 goes with
 * it and H'0B is greyed a second time, which changes nothing.
 */
void screen_body_27(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001184CCUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(REG32(0x0011B2AEUL) + 0x08), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        hitbox_set_state(0x000B, 0x000B, 0x02, 0);
        if (STITCH_SET == 0xAA) {
            hitbox_set_state(0x0006, 0x0006, 0x02, 0);
            hitbox_set_state(0x000B, 0x000B, 0x02, 0);
        }

        REG8(0x0011B0A8UL) = 0x00;
        (void)settings_menu_screen(0x01);
    }

    (void)settings_menu_screen(0x00);
}

/* H'2248F4, screen H'0C: the needle position, with the two bars beside it.
 *
 * Almost the same as H'0D above -- the box blacked out, the picture
 * unpacked into the scratch buffer, the piece copied across, and the item
 * preview drawn -- but its panel run is H'07 to H'08 rather than H'09 to
 * H'0A, and the fresh panel call is H'01 to H'01 as it is there.
 */
void screen_body_0C(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00115F4AUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                  REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                  LCD_FRAME_B, 0x00, 0x01);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        item_preview(REG16(0x00FFFEE0UL));

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        hitbox_fill_from_list(0x0007, 0x0008, 0x0001, 0x00115F44UL);
        width_strip_draw(0x01);
        panel_strip_draw(0x0001, 0x0001, 0x01);
        REG8(0x0011B0A9UL) = 0x00;
    }

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);
    panel_strip_draw(0x0007, 0x0008, 0x00);
    width_strip_draw(0x00);
    (void)needle_choice_screen();
}

/* H'227942, screen H'4B: another of the module's screens, the plain
 * block-and-picture shape with its press called once on the way in and once
 * every pass. */
void screen_body_4B(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00118CB8UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        (void)module_version_screen(0x01);
    }

    (void)module_version_screen(0x00);
}

/* H'2267A4, screen H'4D: the module choice shape again, at +H'58, with the
 * press that leaves for whatever H'11B11A names. */
void screen_body_4D(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        module_choice_lay_out(0x00118D2EUL, 0x0058);
        REG8(0x0011B0A8UL) = 0x00;
    }

    screen_slot_two_screen();
}

/* H'2279F4, screen H'3F: the length and width of one stitch.
 *
 * Two shapes. A group pattern has both numbers and a picture H'BB wide
 * beside them; anything else has one number and a picture that runs the
 * whole width. Which it is goes into H'11B0AC, and H'21D88A reads it again
 * for the number of boxes to hit-test.
 *
 * Where the pattern comes from depends on H'11A175: with the queue being
 * edited it is the record at H'FFFEFE, otherwise the machine's own
 * H'FFFEE0. The two run their index through the multiply differently -- the
 * queue's is sign-extended and the machine's is not -- which is faithful
 * rather than meaningful, since neither can be negative.
 */
void screen_body_3F(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src;
        u32 dst;
        u8 n;

        lcd_buffer_fill(LCD_FRAME_B, 0x00);

        if (REG8(0x0011A175UL) != 0) {
            const u16 at = (u16)queue_entry_offset(REG16(0x00FFFEFEUL));
            const u16 no = queue_entry_number(REG16(0x00FFFEFEUL));

            REG8(0x0011B0ACUL) = pattern_is_group((u16)(at + no));
        } else {
            REG8(0x0011B0ACUL) = (u8)(REG8(0x00FFFEE2UL) & 0x04);
        }

        src = (REG8(0x0011B0ACUL) != 0) ? 0x00118DCEUL : 0x00118E4AUL;
        dst = 0x0011B0AEUL;
        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        if (REG8(0x0011B0ACUL) != 0) {
            image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);

            if (REG8(0x0011A175UL) != 0) {
                const u16 at = (u16)queue_entry_offset(REG16(0x00FFFEFEUL));
                const u16 no = queue_entry_number(REG16(0x00FFFEFEUL));

                bitmap_draw(0x0004, 0x0028, 0x00BB, 0x00C7,
                            (const u8 *)REG32(ITEM_TABLE +
                                (u32)(long)(short)(u16)(ITEM_STRIDE * (u16)(at + no)) + 0x10),
                            LCD_SCRATCH);
            } else {
                const u16 k = (u16)(REG16(0x00FFFEE0UL) +
                                    (u16)REG8(0x00FFFEFDUL));

                bitmap_draw(0x0004, 0x0028, 0x00BB, 0x00C7,
                            (const u8 *)REG32(ITEM_TABLE +
                                (u32)(u16)(ITEM_STRIDE * k) + 0x10),
                            LCD_SCRATCH);
            }
        } else {
            image_load(0x0030ADC0UL, LCD_SCRATCH);

            if (REG8(0x0011A175UL) != 0) {
                bitmap_draw(0x0004, 0x0028, 0x0114, 0x00C7,
                            (const u8 *)REG32(ITEM_TABLE +
                                (u32)(long)(short)(u16)(ITEM_STRIDE *
                                    queue_entry_number(REG16(0x00FFFEFEUL))) + 0x10),
                            LCD_SCRATCH);
            } else {
                bitmap_draw(0x0004, 0x0028, 0x0114, 0x00C7,
                            (const u8 *)REG32(ITEM_TABLE +
                                (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x10),
                            LCD_SCRATCH);
            }
        }

        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        hitbox_reset_all();
        REG8(0x0011B0A8UL) = 0x00;
        stitch_size_screen(0x01);
    }

    stitch_size_screen(0x00);
}

/* H'224846, screen H'0A: the stitch length. The plain block-and-picture
 * shape, with H'218FCE called once on the way in and once every pass. */
void screen_body_0A(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00115E80UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        (void)stitch_length_screen(0x01);
    }

    (void)stitch_length_screen(0x00);
}

/* H'22643A, screen H'21: the module's version. Two wiped buffers, one
 * picture at coordinates of its own, and the press that draws the text and
 * waits for the way out. */
void screen_body_21(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        lcd_buffer_fill(LCD_FRAME_A, 0x00);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        bitmap_draw(0x0008, 0x0013, 0x0024, 0x0025,
                    (const u8 *)0x0034C0C9UL, LCD_FRAME_A);

        REG8(0x0011B0A8UL) = 0x00;
        (void)module_version_press(0x01);
    }

    (void)module_version_press(0x00);
}

/* H'22301A. What a press does on the queue's editing screen, H'44.
 *
 * Two presses in one. The strip of fifteen boxes the queue is shown in is
 * only looked at when the screen's leave check says H'03 and hands back
 * H'77, which is what that strip's press comes back as; anything else falls
 * through to the three keys beside it, H'26 to H'28.
 *
 * On the strip the box pressed is found by the item it carries, the
 * position the cursor is on is deleted, and the strip is filled again --
 * from the box pressed when it is past the first, from the page the walk is
 * on when the first box is a plain one, and from H'11B108 when the search
 * came back with nothing. The original writes the same six-call tail out
 * twice, once for each way in; it is written once here.
 *
 * The three keys are H'19 -- done, which goes back to H'27 with the cursor
 * put where the queue's own numbering says -- H'10, which writes the
 * patterns the list names down onto their slots, and H'0D, which goes to
 * H'46.
 */
void queue_edit_press(void)
{
    u16 hit[2];

    if (screen_leave_check(&hit[0], 0x00) == 0x03 && hit[0] == 0x0077) {
        const u16 box  = hitbox_find(0x0001, 0x000F, REG16(0x00FFFEE0UL), 0x01);
        const u16 from = queue_entry_delete();

        if (from == 0) return;

        if ((short)box < 0x0001) {
            hitbox_fill_from_list(0x0001, 0x000F, REG16(0x0011B108UL),
                                  ITEM_LIST_OUT);
        } else if ((short)box == 0x0001 && hitbox_kind(0x0002) == 0x02) {
            hitbox_fill_from_list(0x0001, 0x000F, list_page_start(),
                                  ITEM_LIST_OUT);
        } else {
            hitbox_fill_from_list(box, 0x000F, from, ITEM_LIST_OUT);
        }

        if ((short)REG16(0x0011A186UL) > (short)REG16(ITEM_LIST_OUT)) {
            REG16(0x0011A186UL) = (u16)(REG16(0x0011A186UL) - 1);
        }
        REG16(0x00FFFEE0UL) = REG16(ITEM_LIST_OUT +
            (u32)(long)(short)(u16)((u16)(REG16(0x0011A186UL) << 1)));
        hitbox_select_current(0x0001, 0x000F);
        return;
    }

    if (touch_hit(0x0026, 0x0028, &hit[0], &hit[1]) != 0x03) return;

    message_show_held(hit[1]);

    if (hit[0] == 0x0019) {
        screen_stack_pop();
        display_init_223010();

        if (REG16(0x00FFFEE0UL) > REG16(ITEM_BASE_INDEX)) {
            REG16(0x0011A186UL) =
                (u16)(REG16(0x00FFFEE0UL) - REG16(ITEM_BASE_INDEX));
        } else {
            REG16(0x0011A186UL) = 0x0000;
        }

        screen_switch(0x27, 0x01, 0x00);
        REG8(0x0011A178UL) = 0x00;
    } else if (hit[0] == 0x0010) {
        queue_items_renumber();
        display_init_223010();

        if (REG16(0x0011A186UL) != 0) {
            REG16(0x00FFFEE0UL) = REG16(ITEM_LIST_OUT +
                (u32)(long)(short)(u16)((u16)(REG16(0x0011A186UL) << 1)));
        }
    } else if (hit[0] == 0x000D) {
        screen_switch(0x46, 0x01, 0x00);
    }
}

/* H'223F2A. The queue's three screens -- H'30, H'44 and H'45.
 *
 * The same three parts again, over the queue's list at H'11B212 rather than
 * a menu. H'44 is the editing screen and the one that differs: it starts
 * the strip at the page the walk is on rather than wherever it was, it
 * pushes the screen it came from and puts up its own three keys, it has no
 * pattern strip beside it, and its press goes to H'22301A above. H'45 is
 * the one with the queue's arrows in the tail.
 */
void screen_body_30(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00116D0CUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(0x0011B0AEUL), LCD_FRAME_A);

        if (REG8(0x0011A169UL) == 0x44) {
            REG16(0x0011B10AUL) = list_page_start();
        }

        hitbox_fill_from_list(0x0001, 0x000F, REG16(0x0011B10AUL),
                              ITEM_LIST_OUT);
        hitbox_fill_from_list(0x0016, 0x0019, 0x0001, 0x00115A06UL);

        if (REG8(0x0011A169UL) == 0x44) {
            screen_stack_push();
            hitbox_fill_boxed_from_list(0x0026, 0x0028, 0x0001, 0x00116D1CUL);
        }

        list_arrows(0x0001, 0x0005, 0x0018, 0x0019, 0x01);
        status_bar_refresh(0x01);

        if (REG8(0x0011A174UL) != 0 && REG8(0x0011A169UL) != 0x44) {
            pattern_strip_restore(0x45);
            hitbox_select_current(0x0021, 0x0025);
        }
        needle_stop_picture();

        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        width_strip_draw(0x01);
        hitbox_select_current(0x0001, 0x000F);

        panel_strip_choose();
        hitbox_fill_from_list(0x0010, 0x0015, REG16(0x0011B10CUL),
                              REG32(0x0011A196UL));
        panel_strip_draw(0x0010, 0x0015, 0x01);

        if (REG8(0x0011A174UL) != 0 && REG8(0x0011A169UL) != 0x44) {
            picker_strip_restore();
            picker_cursor(0x03);
            picker_arrows(0x001E, 0x001F, 0x01);
        }
        REG8(0x0011B0A9UL) = 0x00;

        /* Still inside the lay-out: the "queue is full" message, which
         * H'210AB2 raises and only this screen puts up. */
        if (REG8(0x0011A169UL) == 0x44 && REG8(0x0011A18AUL) != 0) {
            message_show(0x0015);
            REG8(0x0011A18AUL) = 0x00;
        }
    }

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);

    screen_touch();
    if (REG8(0x0011A169UL) == 0x44) queue_edit_press();

    panel_strip_draw(0x0010, 0x0015, 0x00);
    list_arrows(0x0001, 0x0005, 0x0018, 0x0019, 0x00);
    if (REG8(0x0011A169UL) == 0x45) picker_arrows(0x001E, 0x001F, 0x00);
    width_strip_draw(0x00);
    status_bar_refresh(0x00);
}

/* H'227CC6, H'227898 and H'2262EE, screens H'41, H'31 and H'1F: the three
 * that lay their background out the plain way -- a run-length picture into
 * the scratch buffer, the back buffer wiped, and the rectangle the block
 * names copied forward.
 *
 * The block is four longs at a fixed address, copied into H'11B0AE first:
 * the picture, then the three words of the rectangle.
 */
static void plain_screen_lay_out(u32 block)
{
    u32 src = block;
    u32 dst = 0x0011B0AEUL;
    u8 n;

    for (n = 4; n != 0; n--) {
        REG32(dst) = REG32(src);
        src += 4;
        dst += 4;
    }

    hitbox_reset_all();
    lcd_buffer_fill(LCD_FRAME_B, 0x00);
    image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
    region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
}

/* H'227CC6, screen H'41: the picker strip on its own.
 *
 * H'11B0A9 is the second flag, set when something else has changed the list
 * out from under the screen: the strip is laid out again and the cursor put
 * back, but the background is left alone.
 */
void screen_body_41(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        plain_screen_lay_out(0x00118FF8UL);
        REG8(0x0011B0A8UL) = 0x00;
        picker_arrows(0x0012, 0x0013, 0x01);
        picker_strip_screen(0x0001);
    }

    if (REG8(0x0011B0A9UL) != 0) {
        picker_strip_screen(0x0002);
        picker_cursor(0x03);
        REG8(0x0011B0A9UL) = 0x00;
    }

    picker_strip_screen(0x0003);
    picker_arrows(0x0012, 0x0013, 0x00);
}

/* H'227898, screen H'31: the hoop moved by hand. The screen it came from is
 * pushed, because the way out is back to it. */
void screen_body_31(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        screen_stack_push();
        plain_screen_lay_out(0x00118C84UL);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)hoop_nudge_screen();
}

/* H'2262EE, screen H'1F: the module's own settings. The picture is unpacked
 * before the back buffer is wiped, the other way round from H'41 and H'31 --
 * the two do not touch the same buffer, so it makes no difference, but it is
 * what the original does. */
void screen_body_1F(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001182FCUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    (void)module_settings_screen();
}

/* H'226D6E, screen H'2B: the three sewing settings with both bars and the
 * width strip beside them.
 *
 * The background comes out of the block as a bitmap, not a run-length
 * picture, and the screen it came from goes into store H'03 -- the same
 * shape as H'2A and H'2C, but with the bars, the strip and the preview
 * drawn over the top, and the flag cleared last rather than first.
 */
void screen_body_2B(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        setting_screen_lay_out(0x00118780UL);
        bar_width((u16)REG8(0x00FFFEE7UL), 0x01, LCD_FRAME_A, 0x03);
        bar_length((u16)REG8(0x00FFFEE4UL), 0x01, LCD_FRAME_A, 0x03);
        width_strip_draw(0x01);
        item_preview(REG16(0x00FFFEE0UL));
        (void)sew_settings_screen(0x01);
        REG8(0x0011B0A8UL) = 0x00;
    }

    bar_width((u16)REG8(0x00FFFEE7UL), 0x00, LCD_FRAME_A, 0x03);
    bar_length((u16)REG8(0x00FFFEE4UL), 0x00, LCD_FRAME_A, 0x03);
    width_strip_draw(0x00);
    (void)sew_settings_screen(0x00);
}

/* H'227020, screen H'2D: the needle stop position. The same bitmap
 * background as H'2B, without the store and without anything drawn over
 * it. */
void screen_body_2D(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001187FCUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        bitmap_draw(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    (const u8 *)REG32(0x0011B0AEUL), LCD_SCRATCH);
        draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                  REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                  LCD_FRAME_B, 0x00, 0x01);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
        (void)needle_position_screen(0x01);
    }

    (void)needle_position_screen(0x00);
}

/* H'22382A. What the machine does with a pass: the screen it is on, run.
 *
 * The four things that come first are the same whichever screen it is --
 * the cursor blinked, a parked screen change taken up, a held message given
 * its time, and the panel read -- and then the screen number picks one of
 * seventy-one bodies out of a table of seventy-nine.
 *
 * Two of those four can end the pass early. A message still being held ends
 * it without remembering where the machine is; a settled touch with nothing
 * asked for ends it after remembering. So does a screen number above H'4E,
 * which the table does not cover.
 *
 * Fifty-three screens have their body written so far. The other eighteen fall
 * through the switch and reach the tail, which is not what
 * the original does -- it jumps to a body for every screen it knows -- but
 * is what a partly-written table can do. Each body added takes one more
 * screen out of the default.
 */
void screen_dispatch(void)
{
    picker_cursor(0x04);
    if (REG8(0x0011B29CUL) != 0) screen_restore_pending();

    if (message_hold_done() == 0) return;

    key_scan();

    if (touch_settled() == 0 && REG16(0x0011B10EUL) == 0xFFFF) {
        REG16(0x0011A17EUL) = REG16(0x0011B10EUL);
        return;
    }

    if (REG8(0x0011A16EUL) != 0) {
        foot_switch_screen();
        REG8(0x0011A16EUL) = 0x00;
    }

    screen_request();
    screen_put_away();

    if (REG8(0x0011A169UL) <= 0x4E) {
        switch (REG8(0x0011A169UL)) {
        case 0x02:
        case 0x07:
            screen_body_02();
            break;
        case 0x03:
        case 0x04:
            screen_body_03();
            break;
        case 0x30:
        case 0x44:
        case 0x45:
            screen_body_30();
            break;
        case 0x00:
            screen_body_00();
            break;
        case 0x05:
            screen_body_05();
            break;
        case 0x06:
            screen_body_06();
            break;
        case 0x25:
            screen_body_25();
            break;
        case 0x26:
            screen_body_26();
            break;
        case 0x3E:
            screen_body_3E();
            break;
        case 0x0D:
            screen_body_0D();
            break;
        case 0x0B:
            screen_body_0B();
            break;
        case 0x0E:
            screen_body_0E();
            break;
        case 0x0F:
            screen_body_0F();
            break;
        case 0x39:
            screen_body_39();
            break;
        case 0x3B:
            screen_body_3B();
            break;
        case 0x3C:
            screen_body_3C();
            break;
        case 0x3D:
            screen_body_3D();
            break;
        case 0x19:
            screen_body_19();
            break;
        case 0x1A:
            screen_body_1A();
            break;
        case 0x1B:
            screen_body_1B();
            break;
        case 0x1D:
            screen_body_1D();
            break;
        case 0x1E:
            screen_body_1E();
            break;
        case 0x20:
            screen_body_20();
            break;
        case 0x22:
            screen_body_22();
            break;
        case 0x4A:
            screen_body_4A();
            break;
        case 0x48:
            screen_body_48();
            break;
        case 0x10:
            screen_body_10();
            break;
        case 0x49:
            /* The one body with two ways out: zero means the pass ends
             * without remembering where the machine is. */
            if (screen_body_49() == 0) return;
            break;
        case 0x4C:
            screen_body_4C();
            break;
        case 0x28:
            screen_body_28();
            break;
        case 0x29:
            screen_body_29();
            break;
        case 0x2A:
            screen_body_2A();
            break;
        case 0x08:
            screen_body_08();
            break;
        case 0x2C:
            screen_body_2C();
            break;
        case 0x2B:
            screen_body_2B();
            break;
        case 0x2D:
            screen_body_2D();
            break;
        case 0x1F:
            screen_body_1F();
            break;
        case 0x31:
            screen_body_31();
            break;
        case 0x41:
            screen_body_41();
            break;
        case 0x38:
            screen_body_38();
            break;
        case 0x37:
            screen_body_37();
            break;
        case 0x24:
            screen_body_24();
            break;
        case 0x23:
            screen_body_23();
            break;
        case 0x4E:
            screen_body_4E();
            break;
        case 0x09:
            screen_body_09();
            break;
        case 0x11:
            screen_body_11();
            break;
        case 0x12:
            screen_body_12();
            break;
        case 0x13:
            screen_body_13();
            break;
        case 0x14:
            screen_body_14();
            break;
        case 0x15:
            screen_body_15();
            break;
        case 0x16:
            screen_body_16();
            break;
        case 0x2E:
            screen_body_2E();
            break;
        case 0x3A:
            screen_body_3A();
            break;
        case 0x17:
            screen_body_17();
            break;
        case 0x18:
            screen_body_18();
            break;
        case 0x42:
            screen_body_42();
            break;
        case 0x43:
            screen_body_43();
            break;
        case 0x46:
        case 0x47:
            screen_body_46();
            break;
        case 0x2F:
            screen_body_2F();
            break;
        case 0x01:
            screen_body_01();
            break;
        case 0x32:
            screen_body_32();
            break;
        case 0x1C:
            screen_body_1C();
            break;
        case 0x27:
            screen_body_27();
            break;
        case 0x0C:
            screen_body_0C();
            break;
        case 0x4B:
            screen_body_4B();
            break;
        case 0x4D:
            screen_body_4D();
            break;
        case 0x3F:
            screen_body_3F();
            break;
        case 0x0A:
            screen_body_0A();
            break;
        case 0x21:
            screen_body_21();
            break;
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
            screen_body_33();
            break;
        case 0x40:
            break;              /* its table entry *is* the tail */
        default:
            break;              /* the other eighteen, not written yet */
        }
    }

    REG16(0x0011A17EUL) = REG16(0x0011B10EUL);
}

/* ---- going to a pattern by its number ---------------------------------
 * H'212A44, H'21A246, H'24B10A and H'22323A. Screens H'46 and H'47 are the
 * keypad the operator types a pattern number into; these four are what turns
 * the typed number into a screen.
 */

/* H'212A44. Which of the three lists holds the pattern whose *number* --
 * the word at offset H'14 of its descriptor, not its position -- is
 * [number], and where in that list it is.
 *
 * The three are looked at in turn: the menu list at H'11A88E and then the
 * two the block pointers at H'11B09A and H'11B09E name. The list that had
 * it goes back through [which], and the position in it is the answer; zero
 * and a null pointer mean none of them did.
 */
u16 list_holding(u16 number, u32 *which)
{
    static const u32 heads[3] = { 0x0011A88EUL, 0x0011B09AUL, 0x0011B09EUL };
    u8 k;

    for (k = 0; k < 3; k++) {
        const u32 list = (k == 0) ? heads[0] : REG32(heads[k]);
        short i;

        for (i = 1; i <= (short)REG16(list); i++) {
            const u32 entry = ITEM_TABLE +
                (u32)(long)(short)(u16)((u16)(REG16(list +
                    (u32)(long)(short)(u16)((u16)((u16)i << 1))) * ITEM_STRIDE));

            if (REG16(entry + 0x14) != number) continue;
            *which = list;
            return (u16)i;
        }
    }

    *which = 0x00000000UL;
    return 0x0000;
}

/* H'21A246. The pattern with that number made current, and the screen that
 * shows its category gone to. Answers zero when no list has it.
 *
 * H'11B108 is which page of five the pattern sits on, counted from the
 * first of its own category: the difference rounded down to a multiple of
 * five and put back on. */
u8 goto_pattern_number(u16 number)
{
    u32 list = 0;
    u16 at = list_holding(number, &list);
    u16 first;
    u8 category;

    if (at == 0) return 0x00;

    category = REG8(ITEM_TABLE +
        (u32)(long)(short)(u16)((u16)(REG16(list +
            (u32)(long)(short)(u16)((u16)(at << 1))) * ITEM_STRIDE)) +
        ITEM_CATEGORY);

    first = first_item_of_category(category, list);
    REG16(0x0011B108UL) =
        (u16)((u16)((short)((long)(short)(u16)(at - first) / 5) * 5) + first);
    REG16(0x00FFFEE0UL) =
        REG16(list + (u32)(long)(short)(u16)((u16)(at << 1)));

    if (category == 0x11)      screen_switch(0x35, 0x01, 0x00);
    else if (category == 0x10) screen_switch(0x33, 0x01, 0x00);
    else                       screen_switch(0x02, 0x01, 0x00);

    return 0x01;
}

/* H'24B10A. The ROM's own strtol, and the only place the character-class
 * table at H'250783 is used from the application.
 *
 * Everything the C library asks of it is here: the leading space skipped,
 * an optional sign, base zero working the prefix out for itself, "0x"
 * allowed when the base is sixteen, and a value that would not fit clamped
 * with H'22 -- ERANGE -- left in H'11F5A6.
 *
 * Two details are its own. The class table is indexed by the character
 * *signed*, so anything above H'7F reads below the table; and [endptr] is
 * set back to where the string began not only when no digit was found but
 * also when the value overflowed.
 */
#define CTYPE_TABLE   0x00250783UL
#define CTYPE_UPPER   0x01
#define CTYPE_ALNUM   0x07
#define CTYPE_DIGIT   0x04
#define CTYPE_SPACE   0x08

static u8 char_class(u8 c)
{
    return REG8(CTYPE_TABLE + (u32)(long)(short)(signed char)c);
}

long str_to_long(const char *nptr, const char **endptr, short base)
{
    const char *p = nptr;
    const char *digits;
    u32 value = 0, acc = 0;
    char sign;

    while (char_class((u8)*p) & CTYPE_SPACE) p++;

    if (*p == '+' || *p == '-') sign = *p++;
    else                        sign = '+';

    if ((short)base < 0 || base == 1 || (short)base > 0x24) {
        if (endptr != 0) *endptr = nptr;
        return 0;
    }

    if (base == 0) {
        if (*p != '0') {
            base = 10;
        } else if (p[1] == 'x' || p[1] == 'X') {
            base = 16;
            p += 2;
        } else {
            base = 8;
        }
    } else if (*p == '0' && base == 16 && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    while (*p == '0') p++;
    digits = p;

    for (;;) {
        const u8 c = (u8)*p;
        short d;

        if ((char_class(c) & CTYPE_ALNUM) == 0) break;

        if (char_class(c) & CTYPE_DIGIT) {
            d = (short)((short)(signed char)c - 0x30);
        } else if (char_class(c) & CTYPE_UPPER) {
            d = (short)((short)(signed char)(u8)(c | 0x20) - 0x57);
        } else {
            d = (short)((short)(signed char)c - 0x57);
        }

        if (d >= (short)base) break;

        value = (u32)((long)(short)base * (long)acc) + (u32)(long)d;
        if (value < acc) { value = 0xFFFFFFFFUL; break; }
        acc = value;
        p++;
    }

    if (endptr != 0) *endptr = (digits == p) ? nptr : p;

    if (value > 0x7FFFFFFFUL) {
        REG16(0x0011F5A6UL) = 0x0022;
        value = 0x7FFFFFFFUL;
        if (endptr != 0) *endptr = nptr;
        return (sign == '+') ? (long)value : (long)0x80000000UL;
    }

    return (sign == '+') ? (long)value : -(long)value;
}

/* The descriptor of a pattern, and the field the keypad draws into. */
static u32 item_entry(u16 pattern)
{
    return ITEM_TABLE +
        (u32)(long)(short)(u16)((u16)(pattern * ITEM_STRIDE));
}

static void number_field_draw(void)
{
    text_draw((const char *)0x0011A1C2UL, 0x0054, 0x0032, 0x00A1, 0x0043,
              0x0002, 0x00, (const u8 *)0x0011936EUL);
}

/* H'22323A. Screens H'46 and H'47: the keypad.
 *
 * Thirteen boxes. H'1B to H'24 are the digits -- the value carried is the
 * digit plus H'15, which is its ASCII code less H'15 again -- H'0E rubs one
 * out, H'1A leaves, and H'19 is what acts on what was typed. Four digits at
 * most, and a leading zero is refused because a zero can only be appended
 * to something that is already there.
 *
 * The two screens differ in what H'19 does. On H'47 the number is looked up
 * and, unless the pattern is one of the three kinds that cannot be queued,
 * added to the queue; anywhere else it either goes to the pattern's own
 * screen or, when a queue is being edited, makes room for it in the queue
 * and goes to H'44.
 */
void number_keypad_screen(u8 fresh)
{
    char scratch[6];
    u32 list = 0;
    const char *rest = 0;
    u16 value = 0, index = 0;
    u16 length;
    u16 number;
    u8 k;

    for (k = 0; k < 6; k++) scratch[k] = (char)REG8(0x00250770UL + k);

    if (fresh != 0) {
        screen_stack_push();
        REG8(0x0011B3CCUL) = REG8(0x00FFFEFDUL);
        REG8(0x00FFFEFDUL) = 0x00;
        REG16(0x0011A1BEUL) = 0x0000;
        (void)str_copy((char *)0x0011A1C2UL, (const char *)0x00250ADFUL);
        return;
    }

    cursor_blink(0x00AB, 0x002E);

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return;
    message_show_held(index);

    length = (u16)str_length((const char *)0x0011A1C2UL);

    if (value == 0x001B) {
        /* A zero, which only goes on the end of something. */
        if (length == 0 || (short)length >= 4) return;
        scratch[0] = (char)(value + 0x15);
        scratch[1] = 0;
        (void)str_append((char *)0x0011A1C2UL, scratch);
        number_field_draw();
        return;
    }

    if ((short)value >= 0x001C && (short)value <= 0x0024) {
        if ((short)length >= 4) return;
        scratch[0] = (char)(value + 0x15);
        scratch[1] = 0;
        (void)str_append((char *)0x0011A1C2UL, scratch);
        number_field_draw();
        return;
    }

    if (value == 0x000E) {
        /* One rubbed out: the front of the string copied over itself. */
        if (length == 0) return;
        (void)str_copy_n(scratch, (const char *)0x0011A1C2UL,
                         (u32)(long)(short)(u16)(length - 1));
        (void)str_copy((char *)0x0011A1C2UL, scratch);
        number_field_draw();
        return;
    }

    if (value == 0x001A) {
        /* The way out, which is not the same one on the two screens. */
        if (REG8(0x0011A169UL) == 0x47) {
            dialog_backdrop_save(0x01);
            screen_stack_pop();
            if (REG16(0x0011A1BEUL) != 0) {
                (void)goto_pattern_number(REG16(0x0011A1C0UL));
            } else {
                screen_switch(REG8(0x0011B0A5UL), 0x01, 0x00);
                REG16(0x0011B108UL) = REG16(0x0011B118UL);
            }
        } else {
            screen_stack_pop();
            if (REG8(0x0011A178UL) != 0) {
                screen_switch(0x44, 0x01, 0x00);
            } else {
                screen_switch(REG8(0x0011B0A5UL), 0x01, 0x00);
                REG16(0x0011B108UL) = REG16(0x0011B118UL);
            }
        }
        REG8(0x00FFFEFDUL) = REG8(0x0011B3CCUL);
        return;
    }

    if (value != 0x0019) return;
    if (length == 0) return;

    number = (u16)str_to_long((const char *)0x0011A1C2UL, &rest, 0x000A);

    if (REG8(0x0011A169UL) == 0x47) {
        /* Adding to the queue: three kinds of pattern are refused. */
        u16 at = list_holding(number, &list);
        u16 pattern;

        REG16(0x0011A1BEUL) = at;
        if (at == 0) return;

        REG16(0x0011A1C0UL) = number;
        REG8(0x0011B3CCUL) = 0x00;

        pattern = REG16(list + (u32)(long)(short)(u16)((u16)(at << 1)));
        REG16(0x00FFFEE0UL) = pattern;

        if (REG8(item_entry(pattern) + ITEM_CATEGORY) != 0x04 &&
            REG16(item_entry(pattern) + 0x14) != 0x0016 &&
            REG16(item_entry(pattern) + 0x14) != 0x0017) {
            (void)queue_add_entry(pattern, 0x0000);
        }
        return;
    }

    if (REG8(0x0011A178UL) != 0 &&
        REG8(0x0011A169UL) != 0x44 && REG8(0x0011A169UL) != 0x30) {
        u16 at = list_holding(number, &list);
        u8 category;

        REG16(0x0011A1BEUL) = at;
        category = REG8(item_entry(REG16(list +
            (u32)(long)(short)(u16)((u16)(at << 1)))) + ITEM_CATEGORY);

        if (category == 0x11 || category == 0x10) {
            message_show(0x000A);
            return;
        }

        if (REG16(0x0011A1BEUL) == 0) return;

        REG16(0x00FFFEE0UL) = REG16(list +
            (u32)(long)(short)(u16)((u16)(REG16(0x0011A1BEUL) << 1)));
        screen_stack_pop();
        queue_make_room(REG16(0x00FFFEE0UL));
        screen_switch(0x44, 0x01, 0x00);
        return;
    }

    if (goto_pattern_number(number) != 0) screen_stack_pop();
    return;
}

/* H'21B682. Screen H'18's press, which is nothing of its own: the common
 * press handler, and then the one way out. */
u8 module_busy_screen(void)
{
    u16 to = 0;

    (void)screen_touch();

    if (screen_leave_check(&to, 0x00) != 0x03) return 0x00;
    if (to != 0x0077) return 0x00;

    REG8(0x00FFFEC5UL) = 0x00;
    screen_switch(0x17, 0x01, 0x00);
    return 0x00;
}

/* H'21CDA4. The screen that edits the three live settings and can put them
 * back: the three are copied into H'11B37C on the way in, and the way out
 * either keeps what has been done or writes the copies back over it.
 *
 * The hit test is over three boxes, and when it says "nothing" the panel is
 * asked instead -- so the same keys reach it from the glass or from the
 * buttons. Keys H'6E and H'6F are handed straight to the panel, which is how
 * the width strip's two ends work here.
 */
u8 sew_settings_screen(u8 fresh)
{
    u16 value = 0, index = 0;
    u8 r;

    if (fresh != 0) {
        screen_stack_push();
        REG8(0x0011B37CUL) = sew_param_b_get2();
        REG8(0x0011B37DUL) = sew_param_a_get2();
        REG8(0x0011B37EUL) = stitch_width_get2();
    }

    r = touch_hit(0x0001, 0x0003, &value, &index);
    if (r == 0x02) r = screen_leave_check(&value, 0x00);
    if (r != 0x03) return 0x00;

    if (value == 0x007F) {
        message_show_held(index);
        sew_param_b_load();
        sew_param_a_load();
        stitch_width_load();
        return 0x00;
    }

    if (value == 0x0019) {
        message_show_held(index);
        screen_stack_pop();
        pattern_settings_store();
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        message_show_held(index);
        screen_stack_pop();
        sew_param_b_put(REG8(0x0011B37CUL));
        sew_param_a_put(REG8(0x0011B37DUL));
        stitch_width_put(REG8(0x0011B37EUL));
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if ((short)value >= 0x006E && (short)value <= 0x006F) {
        (void)panel_switch((u8)value, index, 0x01, 0x00);
    }

    return 0x00;
}

/* H'21D264. The needle-position screen: a number from H'00 to H'28 with a
 * bar beside it, taken either from the queue entry the machine is on or from
 * the live setting, depending on whether the queue dialog is up.
 *
 * The limit the mark sits at comes from the pattern itself when the queue is
 * showing and from H'FFFEED otherwise. H'11B384 is the value being edited and
 * H'11B385 what it was on the way in, so that H'1A can put it back.
 */
u8 needle_position_screen(u8 fresh)
{
    u16 value = 0, index = 0;
    u8 limit;

    if (REG8(0x0011A175UL) != 0) {
        limit = stitch_param_5(
            (u16)(queue_entry_number(REG16(0x00FFFEFEUL)) +
                  queue_entry_offset(REG16(0x00FFFEFEUL))), 0x00);
    } else {
        limit = (u8)(REG8(0x00FFFEEDUL) & 0x7F);
    }

    if (fresh != 0) {
        screen_stack_push();

        if (REG8(0x0011A175UL) != 0) {
            const u8 v = queue_get_low6(REG16(0x00FFFEFEUL));

            REG8(0x0011B384UL) = v;
            REG8(0x0011B385UL) = v;
        } else {
            const u8 v = REG8(0x00FFFEECUL);

            REG8(0x0011B384UL) = v;
            REG8(0x0011B385UL) = v;
        }

        bar_needle((u16)REG8(0x0011B384UL), (u16)limit, 0x01,
                   LCD_FRAME_A, 0x02);
    }

    bar_needle((u16)REG8(0x0011B384UL), (u16)limit, 0x00, LCD_FRAME_A, 0x02);

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0017) {
        if (REG8(0x0011B384UL) >= 0x28) return 0x00;
        REG8(0x0011B384UL) = (u8)(REG8(0x0011B384UL) + 1);
        if (REG8(0x0011A175UL) != 0) {
            queue_put_low6(REG16(0x00FFFEFEUL), REG8(0x0011B384UL));
        } else {
            REG8(0x00FFFEECUL) = REG8(0x0011B384UL);
        }
        return 0x00;
    }

    if (value == 0x0018) {
        if (REG8(0x0011B384UL) == 0) return 0x00;
        REG8(0x0011B384UL) = (u8)(REG8(0x0011B384UL) - 1);
        if (REG8(0x0011A175UL) != 0) {
            queue_put_low6(REG16(0x00FFFEFEUL), REG8(0x0011B384UL));
        } else {
            REG8(0x00FFFEECUL) = REG8(0x0011B384UL);
        }
        return 0x00;
    }

    if (value == 0x007F) {
        REG8(0x0011B384UL) = limit;
        if (REG8(0x0011A175UL) != 0) {
            queue_put_low6(REG16(0x00FFFEFEUL), limit);
        } else {
            REG8(0x00FFFEECUL) = limit;
        }
        return 0x00;
    }

    if (value == 0x0019) {
        if (REG8(0x00114DC6UL) & 0x80) return 0x00;

        if (REG8(0x0011A175UL) != 0) {
            screen_stack_pop();
            REG8(0x0011A184UL) = 0x01;
            screen_from_slot(0x01);
            REG8(0x0011B0A9UL) = 0x01;
            return 0x00;
        }

        screen_stack_clear();
        screen_switch(REG8(0x0011A168UL), 0x01, 0x00);
        REG16(0x0011B108UL) = REG16(0x0011B110UL);
        if (REG8(0x00FFFEC4UL) & 0x01) REG8(0x0011A177UL) = 0x01;
        return 0x00;
    }

    if (value != 0x001A) return 0x00;
    if (REG8(0x00114DC6UL) & 0x80) return 0x00;

    screen_stack_pop();

    if (REG8(0x0011A175UL) != 0) {
        queue_put_low6(REG16(0x00FFFEFEUL), REG8(0x0011B385UL));
        screen_from_slot(0x01);
        REG8(0x0011B0A9UL) = 0x01;
        return 0x00;
    }

    REG8(0x00FFFEECUL) = REG8(0x0011B385UL);

    if (screen_stack_depth() != 0) {
        screen_switch(screen_stack_at(screen_stack_depth()), 0x01, 0x00);
        return 0x00;
    }

    screen_switch(REG8(0x0011A168UL), 0x01, 0x00);
    REG16(0x0011B108UL) = REG16(0x0011B110UL);
    if (REG8(0x00FFFEC4UL) & 0x01) REG8(0x0011A177UL) = 0x01;
    return 0x00;
}

/* H'21AC9E. The service screen's ten little marks and its two counters.
 *
 * Each mark is one bit of one input port: a three-by-three square filled in
 * colour three when the bit is up and blacked out when it is down, and only
 * drawn when it has changed. The ten remembered values are the words from
 * H'11B342 up, and [fresh] puts them all to H'FFFF so that the whole lot is
 * drawn again.
 *
 * The tenth is only looked at while H'FFFEC5 is zero. The two counters below
 * are longwords in the settings window: the first shown in minutes, the
 * second as it stands, and each remembered one higher than it is so that the
 * first pass always draws them.
 */
static void service_mark(u32 seen, u16 now, u16 x0, u16 y0, u16 x1, u16 y1)
{
    if (REG16(seen) == now) return;

    draw_rect(x0, y0, x1, y1, LCD_FRAME_A,
              (u8)(now != 0 ? 0x03 : 0x00), 0x01);
    REG16(seen) = now;
}

void service_marks_draw(u8 fresh)
{
    char text[12];

    if (fresh != 0) {
        REG16(0x0011B354UL) = 0xFFFF;
        REG16(0x0011B352UL) = 0xFFFF;
        REG16(0x0011B350UL) = 0xFFFF;
        REG16(0x0011B34EUL) = 0xFFFF;
        REG16(0x0011B34CUL) = 0xFFFF;
        REG16(0x0011B34AUL) = 0xFFFF;
        REG16(0x0011B348UL) = 0xFFFF;
        REG16(0x0011B346UL) = 0xFFFF;
        REG16(0x0011B344UL) = 0xFFFF;
        REG16(0x0011B342UL) = 0xFFFF;
        REG32(0x0011B356UL) = REG32(0x0057FF82UL) + 1;
        REG32(0x0011B35AUL) = REG32(0x0057FF86UL) + 1;
    }

    service_mark(0x0011B342UL, (u16)(u8)(REG8(0x00FFFEC0UL) & 0x04),
                 0x0005, 0x0018, 0x0007, 0x001A);
    service_mark(0x0011B344UL, (u16)(u8)(REG8(0x00FFFEC0UL) & 0x02),
                 0x0016, 0x0018, 0x0018, 0x001A);
    service_mark(0x0011B346UL, (u16)(u8)(REG8(0x00FFFEC0UL) & 0x01),
                 0x0027, 0x0018, 0x0029, 0x001A);
    service_mark(0x0011B348UL, (u16)(u8)(REG8(0x00FFFEF8UL) & 0x04),
                 0x0034, 0x0018, 0x0036, 0x001A);
    service_mark(0x0011B34AUL, (u16)(u8)(REG8(0x00FFFEF8UL) & 0x08),
                 0x0048, 0x0018, 0x004A, 0x001A);
    service_mark(0x0011B34CUL, (u16)(u8)(REG8(0x00FFFEC1UL) & 0x02),
                 0x0057, 0x0023, 0x0059, 0x0025);
    service_mark(0x0011B34EUL, (u16)(u8)(REG8(0x00FFFEC7UL) & 0x01),
                 0x006E, 0x0023, 0x0070, 0x0025);
    service_mark(0x0011B350UL, (u16)(u8)(REG8(0x00FFFEC1UL) & 0x08),
                 0x009B, 0x0023, 0x009D, 0x0025);
    service_mark(0x0011B352UL, (u16)(u8)(REG8(0x00FFFEC1UL) & 0x04),
                 0x00B0, 0x0023, 0x00B2, 0x0025);

    if (REG8(0x00FFFEC5UL) != 0) return;

    service_mark(0x0011B354UL, (u16)(u8)(REG8(0x00FFFEC4UL) & 0x10),
                 0x011E, 0x00A5, 0x0120, 0x00A7);

    if (REG32(0x0011B356UL) != REG32(0x0057FF82UL)) {
        REG32(0x0011B356UL) = REG32(0x0057FF82UL);
        long_to_decimal(REG32(0x0057FF82UL) / 0x3C, text);
        text_draw(text, 0x00F0, 0x009B, 0x0108, 0x00A3, 0x0001, 0x02,
                  (const u8 *)0x00119A66UL);
    }

    if (REG32(0x0011B35AUL) != REG32(0x0057FF86UL)) {
        REG32(0x0011B35AUL) = REG32(0x0057FF86UL);
        long_to_decimal(REG32(0x0057FF86UL), text);
        text_draw(text, 0x0100, 0x00B9, 0x0130, 0x00C1, 0x0001, 0x02,
                  (const u8 *)0x00119A66UL);
    }
}

/* H'225B8A, screen H'38: the module's pattern list.
 *
 * The same plain lay-out as H'1F -- the picture unpacked before the back
 * buffer is wiped -- and then the whole of the screen is H'2309EC.
 */
void screen_body_38(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117FF0UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_pattern_screen();
}

/* H'225AE4, screen H'37: the module sewing screen.
 *
 * The same plain lay-out as H'1F and H'38, with the block at H'117E54, and
 * then the whole of the screen is H'23078A.
 */
void screen_body_37(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117E54UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_sew_screen();
}


/* H'225A3E, screen H'24: the design turned and mirrored.
 *
 * The same plain lay-out as H'1F, H'37 and H'38, with the block at
 * H'117CB8, and then the whole of the screen is H'230110.
 */
void screen_body_24(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117CB8UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_turn_screen();
}

/* H'225998, screen H'23: the module's sewing panel.
 *
 * The same plain lay-out as H'1F, H'24, H'37 and H'38, with the block at
 * H'117C60, and then the whole of the screen is H'22F962.
 */
void screen_body_23(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117C60UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_panel_screen();
}

/* H'2258F2, screen H'4E: the screen behind box ten of the sewing panel.
 *
 * The same plain lay-out as H'1F, H'23, H'24, H'37 and H'38, with the block
 * at H'1177D4, and then H'22F82A -- which does nothing at all.
 */
void screen_body_4E(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001177D4UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_extra_screen();
}

/* H'22584C, screen H'16: the module's size and speed settings.
 *
 * The same plain lay-out as the rest of the cluster, with the block at
 * H'117B78, and then the whole of the screen is H'22DBFA.
 */
void screen_body_16(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117B78UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_sizes_screen();
}

/* H'2257A6, screen H'15: where the design sits in the hoop.
 *
 * The same plain lay-out as the rest of the cluster, with the block at
 * H'1179CA, and then the whole of the screen is H'22C24C.
 */
void screen_body_15(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001179CAUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_hoop_screen();
}

/* H'2256AC, screen H'14: the grid of patterns to pick from.
 *
 * Not the plain lay-out the rest of the cluster has: after the picture is
 * loaded a bitmap goes into the corner of the scratch buffer, and once the
 * whole thing has been copied forward box H'0D takes a picture of its own.
 * The block is at H'117602 and the press is H'22BF8C.
 */
void screen_body_14(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117602UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        bitmap_draw(0x0007, 0x0002, 0x0027, 0x0025,
                    (const u8 *)0x0034BE0BUL, LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        hitbox_blit(0x000D, LCD_FRAME_A, 0x0034BFC7UL);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_pick_screen();
}

/* H'2255B2, screen H'13: the other grid of patterns to pick from.
 *
 * The same shape as screen H'14's body and the same block at H'117602, with
 * its own corner bitmap and its own picture for box H'0D. The press is
 * H'22BCCC.
 */
void screen_body_13(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00117602UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        bitmap_draw(0x0009, 0x0004, 0x004C, 0x0023,
                    (const u8 *)0x0034BD38UL, LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        hitbox_blit(0x000D, LCD_FRAME_A, 0x0034BF08UL);
        REG8(0x0011B0A8UL) = 0x00;
    }

    module_pick_screen_b();
}

/* H'2254D2, screen H'12: which kind of pattern to pick.
 *
 * The plain lay-out with the block at H'1174F6, and then two things the rest
 * of the cluster does not do: box three is put into state H'02 as the screen
 * is laid out, and H'11B0A9 is raised so that the *next* pass through --
 * this one included -- draws the preview of whatever item H'FFFEE0 holds.
 * The press is H'22BB2A.
 */
void screen_body_12(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x001174F6UL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        hitbox_set_state(0x0003, 0x0003, 0x02, 0);
        REG8(0x0011B0A8UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011B0A9UL) != 0) {
        item_preview(REG16(0x00FFFEE0UL));
        REG8(0x0011B0A9UL) = 0x00;
    }

    module_kind_screen();
}

/* H'225046, screen H'11: the number pad's own screen.
 *
 * Its lay-out block is at H'11675E, and the picture it loads is not a
 * constant -- it comes from the tenth word of whatever H'11B2B6 points at.
 * The screen is pushed on the stack as it arrives and the help page drawn
 * over it. The press is H'21A070.
 */
void screen_body_11(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x0011675EUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        screen_stack_push();

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        lcd_buffer_fill(LCD_FRAME_B, 0x00);
        image_load(REG32(REG32(0x0011B2B6UL) + 0x10), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        REG8(0x0011B0A8UL) = 0x00;
        help_page_draw();
    }

    goto_number_screen();
}

/* H'218CBE. Screen H'09's whole screen: a stitch length typed in as a
 * number. Called with one as the screen is laid out and with nought on
 * every pass after.
 *
 * The typed digits live as a string at H'11A1A5. Box value H'0E rubs the
 * last one out, H'1A leaves without changing anything, H'19 takes what has
 * been typed -- anything from four up to the ceiling H'11B31E holds -- and
 * everything else is a digit, which is the box value plus H'15. A leading
 * nought is refused and so is a third digit.
 *
 * The two buffers copied out of H'250758 and H'25075E at the top are both
 * written over before they are read; they are the compiler's, not the
 * screen's. The two bytes after the first of them are the one-character
 * string a digit is appended through, which is why the second is set to
 * nought before anything else happens.
 */
void stitch_number_screen(u8 arrived)
{
    char shown[8];
    char scratch[8];
    char one[2];
    u16 value = 0, index = 0;
    u16 len;
    u8 i;

    for (i = 0; i < 6; i++) scratch[i] = (char)REG8(0x00250758UL + (u32)i);
    for (i = 0; i < 6; i++) shown[i]   = (char)REG8(0x0025075EUL + (u32)i);
    one[1] = 0;

    if (arrived != 0) {
        const u16 ceiling = (u16)bus_byte_11();

        screen_stack_push();
        str_copy((char *)0x0011A1A5UL, (const char *)0x00250ADFUL);

        REG16(0x0011B31EUL) = ceiling;
        int_to_decimal((short)ceiling, shown);
        str_append(shown, (const char *)0x00250AE0UL);
        text_draw(shown, 0x0022, 0x0036, 0x0049, 0x003F,
                  0x0001, 0x01, (const u8 *)0x001196EAUL);

        if (REG8(0x00FFFEF7UL) & 0x08) {
            const u16 now = (u16)stitch_length_shown();

            int_to_decimal((short)now, (char *)0x0011A1A5UL);
            text_draw((const char *)0x0011A1A5UL, 0x0063, 0x0033, 0x0099,
                      0x0043, 0x0002, 0x00, (const u8 *)0x0011936EUL);
        }
    }

    cursor_blink(0x009F, 0x002E);

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return;

    message_show_held(index);
    len = (u16)str_length((const char *)0x0011A1A5UL);

    if (value == 0x000E) {
        if (len != 0) {
            str_copy_n(scratch, (const char *)0x0011A1A5UL,
                       (u32)(long)(short)(len - 1));
            str_copy((char *)0x0011A1A5UL, scratch);
            text_draw((const char *)0x0011A1A5UL, 0x0063, 0x0033, 0x0099,
                      0x0043, 0x0002, 0x00, (const u8 *)0x0011936EUL);
        }
        return;
    }

    if (value == 0x001A) {
        screen_from_slot(0x01);
        REG8(0x0011B0A9UL) = 0x01;
        screen_stack_pop();
        return;
    }

    if (value == 0x0019) {
        const char *end = 0;
        const short typed = (short)str_to_long((const char *)0x0011A1A5UL,
                                               &end, 0x000A);

        if (typed >= 0x0004 && typed <= (short)REG16(0x0011B31EUL)) {
            stitch_length_choose((u8)typed);
            screen_from_slot(0x01);
            REG8(0x0011B0A9UL) = 0x01;
            screen_stack_pop();
        }
        return;
    }

    one[0] = (char)(u8)((u8)value + 0x15);

    if (len == 0 && (u8)one[0] == 0x30) return;   /* no leading nought */
    if ((short)len > 0x0001) return;              /* and no third digit */

    str_append((char *)0x0011A1A5UL, one);
    text_draw((const char *)0x0011A1A5UL, 0x0063, 0x0033, 0x0099, 0x0043,
              0x0002, 0x00, (const u8 *)0x0011936EUL);
}

/* H'22474C, screen H'09: the stitch length typed as a number.
 *
 * Its block is at H'115E3A, and unlike the rest of the cluster it clears the
 * back buffer with a filled rectangle rather than a whole-buffer fill before
 * the picture goes in. The box list at H'115E1E fills boxes one to H'0D.
 * H'218CBE is called with one as it lays out and with nought every pass.
 */
void screen_body_09(void)
{
    if (REG8(0x0011B0A8UL) != 0) {
        u32 src = 0x00115E3AUL;
        u32 dst = 0x0011B0AEUL;
        u8 n;

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }

        hitbox_reset_all();
        draw_rect(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                  REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                  LCD_FRAME_B, 0x00, 0x01);
        image_load(REG32(0x0011B0AEUL), LCD_SCRATCH);
        region_copy(REG16(0x0011B0B2UL), REG16(0x0011B0B4UL),
                    REG16(0x0011B0B6UL), REG16(0x0011B0B8UL),
                    REG16(0x0011B0B4UL), LCD_SCRATCH, LCD_FRAME_A);
        hitbox_fill_from_list(0x0001, 0x000D, 0x0001, 0x00115E1EUL);
        REG8(0x0011B0A8UL) = 0x00;
        stitch_number_screen(0x01);
    }

    stitch_number_screen(0x00);
}
