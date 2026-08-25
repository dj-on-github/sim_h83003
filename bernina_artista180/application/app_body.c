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
