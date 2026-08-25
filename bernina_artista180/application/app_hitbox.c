/* The artista 180 application, rebuilt in C: the item preview, the two
 * bars, and the hit-box table.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

static u32 item_descriptor(u16 item)
{
    return ITEM_TABLE + (u32)(long)(short)(u16)(ITEM_STRIDE * item);
}

/* The item's number, drawn under the picture. */
static void item_preview_number(u16 item)
{
    char buf[8];

    int_to_decimal((short)REG16(item_descriptor(item) + 0x14), buf);
    text_draw(buf, 0x006C, 0x0007, 0x0084, 0x0010, 0x0001, 0x01,
              (const u8 *)0x001196EAUL);
}

static void item_preview_clear(void)
{
    draw_rect(PREVIEW_PANEL_X0, PREVIEW_PANEL_Y0,
              PREVIEW_PANEL_X1, PREVIEW_PANEL_Y1,
              LCD_FRAME_A, 0x00, 0x01);
}

/* H'21260A. A stitch: unpacked into the scratch buffer and copied out turned
 * a quarter turn, the source's x becoming the screen's y and running the
 * other way. */
void item_preview_stitch(u16 item)
{
    short sy, dx, sx, dy;

    bitmap_draw(0x0000, 0x0000, 0x0027, 0x002F,
                (const u8 *)REG32(item_descriptor(item) + 0x08),
                PREVIEW_SCRATCH);
    item_preview_clear();

    for (sy = 0x0009, dx = 0x0083; sy <= 0x002D; sy++, dx++) {
        for (sx = 0x0005, dy = 0x0023; sx <= 0x0022; sx++, dy--) {
            plot_pixel((u16)dx, (u16)dy, LCD_FRAME_A,
                       read_pixel((u16)sx, (u16)sy, PREVIEW_SCRATCH));
        }
    }
    item_preview_number(item);
}

/* H'212760. A whole-pattern picture: the same scratch buffer, copied out the
 * right way up and put further to the right. */
void item_preview_pattern(u16 item)
{
    short sy, dy, sx, dx;

    bitmap_draw(0x0000, 0x0000, 0x0022, 0x0022,
                (const u8 *)REG32(item_descriptor(item) + 0x08),
                PREVIEW_SCRATCH);
    item_preview_clear();

    for (sy = 0x0003, dy = 0x0006; sy <= 0x0020; sy++, dy++) {
        for (sx = 0x0002, dx = 0x0089; sx <= 0x0020; sx++, dx++) {
            plot_pixel((u16)dx, (u16)dy, LCD_FRAME_A,
                       read_pixel((u16)sx, (u16)sy, PREVIEW_SCRATCH));
        }
    }
}

/* H'212844. Anything else: the picture is blitted straight into the panel,
 * centred on H'9A, H'14 from the width and height in its own header. */
void item_preview_plain(u16 item)
{
    const u32 desc = item_descriptor(item);
    const u32 src  = REG32(desc + 0x0C);
    const u16 w    = header_word_0((const u8 *)src);
    const u16 h    = header_word_1((const u8 *)src);
    const u16 x0   = (u16)(0x009A - (u16)((short)w / 2));
    const u16 y0   = (u16)(0x0014 - (u16)((short)h / 2));

    item_preview_clear();
    bitmap_draw(x0, y0, (u16)(w + x0), (u16)(h + y0),
                (const u8 *)src, LCD_FRAME_A);
    item_preview_number(item);
}

/* H'2125B0. Which of the three, by category. Screen H'18 has no panel. */
void item_preview(u16 item)
{
    u8 cat;

    if (REG8(0x11A169UL) == 0x18) return;

    cat = REG8(item_descriptor(item) + 0x17);
    if (cat >= 0x05 && cat <= 0x0F) item_preview_plain(item);
    else if (cat >= 0x12)           item_preview_pattern(item);
    else                            item_preview_stitch(item);
}

/* The width bar runs up from H'95, so a bigger number is a smaller y. */
static u16 bar_w_pixel(long v)
{
    return (u16)(BAR_W_BASE - (u16)(int)((float)v * 1.01f + 0.5f));
}

/* The length bar runs right from H'D3. */
static u16 bar_l_pixel(long v)
{
    return (u16)((u16)(int)((float)v + 0.5f) + BAR_L_LEFT);
}

/* H'20FA18. The width bar. [fresh] redraws the whole thing; without it only
 * the difference from last time is painted. */
void bar_width(u16 value, u8 fresh, u32 buffer, u8 colour)
{
    u16 y;
    u16 limit;

    if (fresh != 0) {
        y = bar_w_pixel((long)(short)value);
        draw_rect(BAR_W_X0, y, BAR_W_X1, BAR_W_BASE, buffer, colour, 0x01);
        if ((short)value < 0x0064) {
            draw_rect(BAR_W_X0, BAR_W_TOP, BAR_W_X1, (u16)(y - 1),
                      buffer, 0x00, 0x01);
        }
        REG16(0x11A860UL) = y;
        REG16(0x11A85EUL) = value;
        REG16(0x11A862UL) = 0xFFFF;
        REG16(0x11A864UL) = 0xFFFF;
        REG16(0x11A866UL) =
            bar_w_pixel((long)(u32)(u8)(REG8(0xFFFEE8UL) & 0x7F));
    } else if (value != REG16(0x11A85EUL)) {
        y = bar_w_pixel((long)(short)value);
        if ((short)value > (short)REG16(0x11A85EUL)) {
            draw_rect(BAR_W_X0, y, BAR_W_X1, (u16)(REG16(0x11A860UL) - 1),
                      buffer, colour, 0x01);
        } else {
            draw_rect(BAR_W_X0, REG16(0x11A860UL), BAR_W_X1, (u16)(y - 1),
                      buffer, 0x00, 0x01);
        }
        REG16(0x11A860UL) = y;
        REG16(0x11A85EUL) = value;
    }

    /* The limit mark, moved when the limit has changed. */
    limit = (u16)(u8)(REG8(0xFFFEE8UL) & 0x7F);
    if (limit != REG16(0x11A862UL)) {
        if ((short)REG16(0x11A866UL) >= (short)REG16(0x11A860UL)) {
            if (REG16(0x11A864UL) == 0) {
                draw_hline(BAR_W_X0, BAR_W_X1, REG16(0x11A866UL),
                           buffer, colour);
            }
        } else {
            if (REG16(0x11A864UL) != 0) {
                draw_hline(BAR_W_X0, BAR_W_X1, REG16(0x11A866UL),
                           buffer, 0x00);
            }
        }
        REG16(0x11A866UL) = bar_w_pixel((long)(u32)limit);
        REG16(0x11A862UL) = limit;
    }

    /* And taken off or put back when bit 7 of H'FFFEE5 changes. */
    {
        const u16 on = (u16)(u8)(REG8(0xFFFEE5UL) & 0x80);

        if (on != REG16(0x11A864UL)) {
            draw_hline(BAR_W_X0, BAR_W_X1, REG16(0x11A866UL), buffer,
                       (u8)((REG8(0xFFFEE5UL) & 0x80) ? colour : 0x00));
            REG16(0x11A864UL) = on;
        }
    }
}

/* H'20FF7A. The length bar. Two pictures sit beside it -- H'34BAE7 and
 * H'34BA1B -- and which of them is shown follows bit 4 of H'FFFEF8. */
void bar_length(u16 value, u8 fresh, u32 buffer, u8 colour)
{
    u16 x;
    u16 limit;

    if (fresh != 0) REG16(0x11A876UL) = 0xFFFF;

    {
        const u16 flag = (u16)(u8)(REG8(0xFFFEF8UL) & 0x10);

        if (flag != REG16(0x11A876UL)) {
            bitmap_draw(0x00D2, 0x0006, 0x0139, 0x0013,
                        (const u8 *)((REG8(0xFFFEF8UL) & 0x10)
                                     ? 0x0034BAE7UL : 0x0034BA1BUL),
                        LCD_FRAME_A);
            REG16(0x11A876UL) = (u16)(u8)(REG8(0xFFFEF8UL) & 0x10);
        }
    }

    if (fresh != 0) {
        x = bar_l_pixel((long)(short)value);
        draw_rect(BAR_L_LEFT, BAR_L_Y0, x, BAR_L_Y1, buffer, colour, 0x01);
        if ((short)value < 0x0064) {
            draw_rect((u16)(x + 1), BAR_L_Y0, BAR_L_RIGHT, BAR_L_Y1,
                      buffer, 0x00, 0x01);
        }
        REG16(0x11A872UL) = x;
        REG16(0x11A874UL) = value;
        REG16(0x11A878UL) = 0xFFFF;
        REG16(0x11A87AUL) = 0xFFFF;
        REG16(0x11A87CUL) =
            bar_l_pixel((long)(u32)(u8)(REG8(0xFFFEE5UL) & 0x7F));
    } else if (value != REG16(0x11A874UL)) {
        x = bar_l_pixel((long)(short)value);
        if ((short)value < (short)REG16(0x11A874UL)) {
            draw_rect((u16)(x + 1), BAR_L_Y0, REG16(0x11A872UL), BAR_L_Y1,
                      buffer, 0x00, 0x01);
        } else {
            draw_rect((u16)(REG16(0x11A872UL) + 1), BAR_L_Y0, x, BAR_L_Y1,
                      buffer, colour, 0x01);
        }
        REG16(0x11A872UL) = x;
        REG16(0x11A874UL) = value;
    }

    limit = (u16)(u8)(REG8(0xFFFEE5UL) & 0x7F);
    if (limit != REG16(0x11A878UL)) {
        if ((short)REG16(0x11A87CUL) <= (short)REG16(0x11A872UL)) {
            if (REG16(0x11A87AUL) == 0) {
                draw_vline(REG16(0x11A87CUL), BAR_L_Y0, BAR_L_Y1,
                           buffer, colour);
            }
        } else {
            if (REG16(0x11A87AUL) != 0) {
                draw_vline(REG16(0x11A87CUL), BAR_L_Y0, BAR_L_Y1,
                           buffer, 0x00);
            }
        }
        REG16(0x11A87CUL) = bar_l_pixel((long)(u32)limit);
        REG16(0x11A878UL) = limit;
    }

    {
        const u16 on = (u16)(u8)(REG8(0xFFFEE5UL) & 0x80);

        if (on != REG16(0x11A87AUL)) {
            draw_vline(REG16(0x11A87CUL), BAR_L_Y0, BAR_L_Y1, buffer,
                       (u8)((REG8(0xFFFEE5UL) & 0x80) ? colour : 0x00));
            REG16(0x11A87AUL) = on;
        }
    }
}

static u32 hitbox_at(u16 index)
{
    return HITBOX_TABLE +
           (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)index);
}

/* H'211ADA. What a box is. */
u8 hitbox_kind(u16 index)
{
    return REG8(hitbox_at(index) + 0x10);
}

/* H'211B00. How a box is drawn. */
u8 hitbox_style(u16 index)
{
    return REG8(hitbox_at(index) + 0x11);
}

/* H'210D6C. Every box in the table back to "live, drawn plainly". The count
 * is entry zero's first word, and the walk starts at one. */
void hitbox_reset_all(void)
{
    const u16 count = REG16(HITBOX_TABLE);
    short i;

    for (i = 1; (short)count >= i; i++) {
        const u32 e = hitbox_at((u16)i);

        REG8(e + 0x10) = 0x00;
        REG8(e + 0x11) = 0x03;
    }
}

/* H'211B26. Which box carries [value], searching indices [first] to [last].
 *
 * A box with no list of its own matches on the value itself. A box with a
 * list matches on what the list holds at that offset -- except on screen
 * H'44, where the two boxes of a pair are told apart by their flag byte and
 * matched against H'11A186 or H'11A188 instead, which is how the two halves
 * of a range are edited separately. Zero means nothing matched. */
u16 hitbox_find(u16 first, u16 last, u16 value, u8 second)
{
    const u32 table = HITBOX_TABLE;
    short i;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        u32 list;

        if (REG8(e + 0x10) == 0x02) continue;

        list = REG32(e + 0x0C);
        if (list == 0) {
            if (REG16(e + 0x08) == value) return (u16)i;
            continue;
        }

        if (REG8(0x11A169UL) == 0x44 && REG8(e + 0x0A) == 0x01) {
            const u16 want = second ? REG16(0x11A186UL) : REG16(0x11A188UL);

            if (REG16(e + 0x08) == want) return (u16)i;
            continue;
        }

        {
            const u32 at = list +
                (u32)(long)(short)(u16)(REG16(e + 0x08) << 1);

            if (REG16(at) == value) return (u16)i;
        }
    }
    return 0x0000;
}

/* H'212D8A and H'212E02. One box painted: the first blits a picture into it,
 * the second fills it. Both add the screen's origin to the box.
 *
 * The buffer comes before the picture in the argument list, which is the
 * other way round from bitmap_draw itself. */
void hitbox_blit(u16 index, u32 buffer, u32 src)
{
    const u32 e = hitbox_at(index);

    bitmap_draw((u16)(REG16(e + 0x00) + HITBOX_X0),
                (u16)(REG16(e + 0x02) + HITBOX_Y0),
                (u16)(REG16(e + 0x04) + HITBOX_X0),
                (u16)(REG16(e + 0x06) + HITBOX_Y0),
                (const u8 *)src, buffer);
}

void hitbox_fill(u16 index, u32 buffer)
{
    const u32 e = hitbox_at(index);

    draw_rect((u16)(REG16(e + 0x00) + HITBOX_X0),
              (u16)(REG16(e + 0x02) + HITBOX_Y0),
              (u16)(REG16(e + 0x04) + HITBOX_X0),
              (u16)(REG16(e + 0x06) + HITBOX_Y0),
              buffer, 0x00, 0x01);
}

/* H'217C84. Either of the two, into the second buffer, chosen by a flag. */
void hitbox_paint(u16 index, u8 with_picture)
{
    if (with_picture) hitbox_blit(index, LCD_FRAME_B, 0x0034C8D3UL);
    else              hitbox_fill(index, LCD_FRAME_B);
}

/* ---- the screen stack -------------------------------------------------
 * H'11A18B is a depth followed by that many screen numbers, so "back" has
 * somewhere to go. H'11A17C says whether there is anything on it.
 */

/* H'21CED4. The current screen pushed, unless it is already on top. */
void screen_stack_push(void)
{
    const u16 depth = (u16)REG8(0x11A18BUL);

    if (depth != 0 &&
        REG8(0x0011A18BUL + (u32)(long)(short)depth) == REG8(0x11A169UL)) {
        return;
    }

    REG8(0x11A18BUL) = (u8)(depth + 1);
    REG8(0x0011A18BUL + (u32)(long)(short)(u16)(depth + 1)) =
        REG8(0x11A169UL);
    REG8(0x11A17CUL) = 0x01;
}

/* H'21CF50. One off the top, and the flag down when it empties. */
void screen_stack_pop(void)
{
    const u16 depth = (u16)REG8(0x11A18BUL);

    if (depth == 0) return;

    REG8(0x11A18BUL) = (u8)(depth - 1);
    if ((u16)(depth - 1) == 0) REG8(0x11A17CUL) = 0x00;
}

/* H'21F46A. One of the four remembered screens made current. H'11B100 says
 * which of the four it was. */
void screen_from_slot(u8 slot)
{
    switch (slot) {
    case 0x01: REG8(0x11A169UL) = REG8(0x11A16AUL); break;
    case 0x02: REG8(0x11A169UL) = REG8(0x11A16BUL); break;
    case 0x03: REG8(0x11A169UL) = REG8(0x11A16CUL); break;
    case 0x04: REG8(0x11A169UL) = REG8(0x11A16DUL); break;
    default: return;
    }
    REG8(0x11A173UL) = 0x01;
    REG8(0x11B100UL) = slot;
}

/* ---- the touch hold-off -----------------------------------------------
 * H'11A182 counts down H'19 passes after a touch, so one press is not read
 * as several.
 */

/* H'210E78. */
void touch_holdoff_start(void)
{
    REG16(0x11A182UL) = 0x0019;
}

/* H'210E88. True when the hold-off has run out. */
u8 touch_holdoff_done(void)
{
    if ((short)REG16(0x11A182UL) <= 0) return 0x01;

    REG16(0x11A182UL) = (u16)(REG16(0x11A182UL) - 1);
    return 0x00;
}

/* H'210EA8. Whether a touch may be acted on while the machine is running.
 *
 * With the motor stopped, anything goes. With it running, the first touch
 * after H'11A16F went up is swallowed, and after that only the twelve codes
 * from H'83 to H'8E -- the ones on the sewing screen itself -- are let
 * through. */
u8 touch_allowed(u16 code)
{
    if (!(REG8(0x114DC6UL) & 0x80)) return 0x01;

    if (REG8(0x11A16FUL) != 0) {
        REG8(0x11A16FUL) = 0x00;
        return 0x00;
    }

    if ((short)code >= 0x0083 && (short)code <= 0x008E) return 0x00;
    return 0x01;
}

/* ---- three more that the screens share --------------------------------- */

/* H'21F87A. Whether leaving for screen H'11B10E means the one being left
 * should go on the stack. Screens H'75 and H'7E never do; on the sewing
 * screen and the two like it, a jump to one of the settings screens does
 * not; and H'78 turns the display over on the way out.
 *
 * H'11A17C being zero -- nothing on the stack yet -- also means no. */
u8 screen_leave_stacks(void)
{
    const u16 to = REG16(0x11B10EUL);
    const u8  from = REG8(0x11A169UL);

    if (to == 0x0075 || to == 0x007E) return 0x00;
    if (REG8(0x11A17CUL) == 0) return 0x00;

    if (REG8(0x11A178UL) != 0 && from != 0x46) {
        if ((short)to >= 0x0070) {
            if ((short)to < 0x0073 || to == 0x0077 || to == 0x007D) return 0x00;
        }
        if (to == 0x0078) {
            message_show(0x000A);
            return 0x01;
        }
        return 0x01;
    }

    if (from == 0x2B) {
        if ((short)to >= 0x006E && (short)to <= 0x006F) return 0x00;
        return 0x01;
    }

    if (from >= 0x0C && (from < 0x0E || from == 0x42)) {
        if ((short)to >= 0x006E) {
            if ((short)to < 0x0070 || to == 0x0077) return 0x00;
        }
        return 0x01;
    }
    return 0x01;
}

/* H'21F940. What to do about the screen the panel is asking for: 2 means
 * nothing, 3 means go there, and the number is left where the caller asked
 * for it.
 *
 * Being asked for the screen that is already up is refused -- except for
 * H'7B, which is allowed round again, and except when the caller forces it.
 * Both of those jump straight to the write, which is why they skip the
 * "already there" test rather than answering 2. */
u8 screen_leave_check(u16 *out, u8 forced)
{
    const u16 to = REG16(0x11B10EUL);

    if (to == 0xFFFF) return 0x02;
    if (screen_leave_stacks() != 0) return 0x02;

    if (forced == 0 && to != 0x007B) {
        if (REG16(0x11A17EUL) == to) return 0x02;
    }

    *out = REG16(0x11B10EUL);
    return 0x03;
}

/* H'24662C. One owner at a time for the module link: the first caller gets
 * it, and anyone else is refused and left in H'11F536. */
u8 link_claim(u8 owner)
{
    if (REG8(0x114DB9UL) != 0 && REG8(0x114DB9UL) != owner) {
        REG8(0x11F536UL) = owner;
        return 0x00;
    }
    REG8(0x114DB9UL) = owner;
    return 0x01;
}

/* H'24AAD2. The ROM's own strncpy: at most [n] bytes, and the rest of the
 * [n] filled with zeros once the terminator has been copied. The count is
 * tested before it is decremented, so [n] of zero copies nothing. */
char *str_copy_n(char *dst, const char *src, u32 n)
{
    char *p = dst;

    while (n-- != 0) {
        if ((*p++ = *src++) == 0) {
            while (n-- != 0) *p++ = 0;
            break;
        }
    }
    return dst;
}

/* H'24AAA2. The ROM's own strcpy, and H'24AB62 below it its strcat. Both
 * copy loops end on the flags the store left, not on the increment -- ADDS
 * does not touch them -- so they stop on the terminator having copied it. */
char *str_copy(char *dst, const char *src)
{
    char *p = dst;

    while ((*p = *src++) != 0) p++;
    return dst;
}

char *str_append(char *dst, const char *src)
{
    char *p = dst;

    while (*p != 0) p++;
    while ((*p = *src++) != 0) p++;
    return dst;
}

static void hitbox_rect_inset(u32 e, u32 buffer, u8 colour)
{
    draw_rect((u16)(REG16(e + 0x00) + HITBOX_X0 + 2),
              (u16)(REG16(e + 0x02) + HITBOX_Y0 + 2),
              (u16)(REG16(e + 0x04) + HITBOX_X0 - 2),
              (u16)(REG16(e + 0x06) + HITBOX_Y0 - 2),
              buffer, colour, 0x01);
}

static void hitbox_rect_raw(u32 e, u32 buffer, u8 colour)
{
    draw_rect(REG16(e + 0x00), REG16(e + 0x02),
              REG16(e + 0x04), REG16(e + 0x06),
              buffer, colour, 0x01);
}

static void hitbox_blit_raw(u32 e, u32 src, u32 buffer)
{
    bitmap_draw(REG16(e + 0x00), REG16(e + 0x02),
                REG16(e + 0x04), REG16(e + 0x06),
                (const u8 *)src, buffer);
}

/* Either the picture the caller gave, or a plain fill in [colour]. */
static void hitbox_face(u32 e, u32 picture, u8 colour)
{
    if (picture == 0) hitbox_rect_inset(e, LCD_FRAME_B, colour);
    else              hitbox_blit_raw(e, picture, LCD_FRAME_B);
}

void hitbox_set_state(u16 first, u16 last, u8 what, u32 picture)
{
    short i;

    if ((short)first <= 0) return;
    if ((short)first > (short)last) return;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        const u8 style = REG8(e + 0x11);

        switch (what) {
        case 0x05:
            /* Blanked with the background picture. */
            if (REG8(e + 0x10) == 0x05) break;
            if (style == 0x03) {
                hitbox_blit_raw(e, HITBOX_PICTURE, LCD_FRAME_B);
            }
            REG8(e + 0x10) = 0x05;
            break;

        case 0x01:
            /* Pressed. */
            if (REG8(e + 0x10) == 0x01) break;
            if (style == 0x03) hitbox_face(e, picture, 0x02);
            REG8(e + 0x10) = 0x01;
            break;

        case 0x00:
            /* Back to normal. Style 4 -- a box drawn by someone else -- is
             * left as it is. */
            if (REG8(e + 0x10) == 0x00) break;
            if (style != 0x04) hitbox_face(e, picture, 0x00);
            REG8(e + 0x10) = 0x00;
            break;

        case 0x02:
            /* Greyed out: drawn into the front buffer, and if it was
             * pressed, the press is taken off the back one. */
            if (REG8(e + 0x10) == 0x02) break;
            if (style == 0x03) {
                hitbox_rect_raw(e, LCD_FRAME_A, 0x02);
                if (REG8(e + 0x10) == 0x01) {
                    hitbox_rect_inset(e, LCD_FRAME_B, 0x00);
                }
            }
            REG8(e + 0x10) = 0x02;
            break;

        case 0x04:
            /* Handed over: the state stays, only the style changes. */
            if (style != 0x03) break;
            if (REG8(e + 0x10) == 0x01) {
                hitbox_face(e, picture, 0x00);
            } else if (REG8(e + 0x10) == 0x05) {
                hitbox_rect_inset(e, LCD_FRAME_B, 0x00);
            }
            REG8(e + 0x11) = 0x04;
            break;

        case 0x03:
            /* Handed back: whatever state the box is in is drawn again. */
            if (style == 0x03) break;
            if (REG8(e + 0x10) == 0x01) {
                hitbox_face(e, picture, 0x02);
                REG8(e + 0x11) = 0x03;
            } else if (REG8(e + 0x10) == 0x00) {
                hitbox_face(e, picture, 0x00);
                REG8(e + 0x11) = 0x03;
            } else if (REG8(e + 0x10) == 0x02) {
                hitbox_rect_raw(e, LCD_FRAME_A, 0x02);
                REG8(e + 0x11) = 0x03;
            } else if (REG8(e + 0x10) == 0x05) {
                hitbox_blit_raw(e, HITBOX_PICTURE, LCD_FRAME_B);
                REG8(e + 0x11) = 0x03;
            }
            break;

        default:
            break;
        }
    }
}

/* H'211E9C. The same again, with a box drawn round each picture.
 *
 * H'211C38 above blits the picture on its own; this one fills the box out
 * two pixels beyond the rectangle in colour 2 first, so the picture sits in
 * a panel. The two pattern lists take their picture out of the stitch
 * descriptor and everything else indexes the icon table, the same split as
 * H'211C38 makes -- but with only two of the five lists counted as
 * patterns, not all five.
 *
 * A value of zero, and a run longer than the list, both leave the box in
 * state 2. There is no wrapping here.
 *
 * The origin is read once at the top rather than per box, which matters
 * only if something moved it half way through -- nothing does. */
u16 hitbox_fill_boxed_from_list(u16 first, u16 last, u16 value, u32 list)
{
    const u16 ox = HITBOX_X0;
    const u16 oy = HITBOX_Y0;
    const u16 length = REG16(list);
    short i = (short)first;
    short v = (short)value;

    for (; i <= (short)last; i++, v++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        u16 slot;

        REG32(e + 0x0C) = list;
        REG16(e + 0x08) = (u16)v;

        slot = REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
        if (slot == 0 || v > (short)length) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
            continue;
        }

        if (REG8(e + 0x11) != 0x04) {
            u32 picture;

            draw_rect((u16)(REG16(e + 0x00) + ox - 2),
                      (u16)(REG16(e + 0x02) + oy - 2),
                      (u16)(REG16(e + 0x04) + ox + 2),
                      (u16)(REG16(e + 0x06) + oy + 2),
                      LCD_FRAME_A, 0x02, 0x01);

            slot = REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
            if (list == 0x0011A88EUL || list == REG32(0x11B096UL)) {
                picture = REG32(ITEM_TABLE +
                    (u32)(long)(short)(u16)(ITEM_STRIDE * slot) + 0x08);
            } else {
                picture = REG32(0x001158CEUL +
                    (u32)(long)(short)(u16)((u16)(slot << 2)));
            }

            bitmap_draw((u16)(REG16(e + 0x00) + ox),
                        (u16)(REG16(e + 0x02) + oy),
                        (u16)(REG16(e + 0x04) + ox),
                        (u16)(REG16(e + 0x06) + oy),
                        (const u8 *)picture, LCD_FRAME_A);
        }
        hitbox_set_state((u16)i, (u16)i, 0x00, 0);
    }

    return (u16)(v + 1);
}

/* H'211A9E. A message put up and held for H'96 ticks. */
void message_show_held(u16 msg)
{
    hitbox_set_state(msg, msg, 0x01, 0);
    REG16(0x114DE0UL) = 0x0000;
    REG16(0x11A166UL) = 0x0096;
    REG16(0x11A180UL) = msg;
}

/* ---- a run of boxes filled from a list --------------------------------
 * H'211C38. The list is a count followed by that many values, and each box
 * in the run gets one of them. Which picture a value stands for depends on
 * which list it is: five of them -- the pattern lists -- take the picture
 * out of the stitch descriptor at H'114DD2, and everything else indexes the
 * icon table at H'1158CE.
 *
 * A run longer than the list has its tail put into state 2, so the buttons
 * past the end of a short list go grey. The value returned is one past the
 * last box that got something.
 */
static u32 hitbox_list_is_patterns(u32 list)
{
    return (list == 0x0011A88EUL || list == REG32(0x11B096UL) ||
            list == 0x0011B212UL || list == REG32(0x11B09AUL) ||
            list == REG32(0x11B09EUL));
}

u16 hitbox_fill_from_list(u16 first, u16 last, u16 value, u32 list)
{
    const u16 length = REG16(list);
    const u32 ox = (u32)HITBOX_X0, oy = (u32)HITBOX_Y0;
    short i = (short)first;
    short v = (short)value;
    short local_len;
    u8 wraps = 0;

    if (hitbox_list_is_patterns(list)) {
        for (; i <= (short)last; i++, v++) {
            const u32 e = HITBOX_TABLE +
                (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

            REG32(e + 0x0C) = list;
            REG16(e + 0x08) = (u16)v;

            if (REG16(list + (u32)(long)(short)(u16)((u16)v << 1)) == 0) {
                hitbox_set_state((u16)i, (u16)i, 0x02, 0);
                continue;
            }
            if (v > (short)length) continue;    /* past the end: left alone */

            if (REG8(e + 0x11) != 0x04) {
                const u16 pattern =
                    REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
                const u32 rec = ITEM_TABLE +
                    (u32)(long)(short)(u16)(ITEM_STRIDE * pattern);

                bitmap_draw((u16)(REG16(e + 0x00) + ox),
                            (u16)(REG16(e + 0x02) + oy),
                            (u16)(REG16(e + 0x04) + ox),
                            (u16)(REG16(e + 0x06) + oy),
                            (const u8 *)REG32(rec + 0x08), LCD_FRAME_A);
            }
            hitbox_set_state((u16)i, (u16)i, 0x00, 0);
        }
        return (u16)(v + 1);
    }

    /* Not a pattern list. A run starting past the first value whose slot is
     * empty is pulled back to one, and one whose slot is not is marked as
     * wrapping: when it reaches the end of the list it starts again, so a
     * short list repeats across the row rather than leaving it half grey. */
    if ((short)value > 1) {
        if (REG16(list + (u32)(long)(short)(u16)((u16)value << 1)) != 0) {
            wraps = 1;
        } else {
            value = 0x0001;
            v = 1;
        }
    }
    local_len = (short)length;

    for (; i <= (short)last; i++, v++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        REG32(e + 0x0C) = list;
        REG16(e + 0x08) = (u16)v;

        if (REG16(list + (u32)(long)(short)(u16)((u16)v << 1)) == 0 ||
            v > local_len) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
            continue;
        }

        if (REG8(e + 0x11) != 0x04) {
            const u16 icon =
                REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
            const u32 src = REG32(0x001158CEUL +
                (u32)(long)(short)(u16)((u16)icon << 2));

            bitmap_draw((u16)(REG16(e + 0x00) + ox),
                        (u16)(REG16(e + 0x02) + oy),
                        (u16)(REG16(e + 0x04) + ox),
                        (u16)(REG16(e + 0x06) + oy),
                        (const u8 *)src, LCD_FRAME_A);
        }
        hitbox_set_state((u16)i, (u16)i, 0x00, 0);

        /* The wrap is tested against the list's real length, not against the
         * one the wrap itself writes back. */
        if (v == (short)length && wraps) {
            local_len = (short)(value - 1);
            v = 0;
            wraps = 0;
        }
    }
    return (u16)(v + 1);
}

/* H'212E78. A run of boxes drawn again from whatever list each one already
 * points at.
 *
 * H'211C38 and H'211E9C above put a list *into* a run; this one takes each
 * box as it stands. It copies the whole H'12-byte entry into a local first
 * and works from the copy, which is why the fields below are read out of an
 * array rather than out of the table.
 *
 * A box in state 2 is left alone, and so is one not drawn in style 3. A
 * slot of zero puts the box into state 2; anything else is blitted at the
 * box's own coordinates with no origin added, from the stitch descriptor for
 * the five pattern lists and from the icon table for everything else. */
void hitbox_redraw_run(u16 first, u16 last)
{
    short i;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        u16 box[9];
        u32 list;
        u16 slot;
        int n;

        for (n = 0; n < 9; n++) box[n] = REG16(e + (u32)(2 * n));

        list = ((u32)box[6] << 16) | (u32)box[7];

        if ((u8)(box[8] >> 8) == 0x02) continue;   /* +H'10, the state */
        if ((u8)box[8] != 0x03) continue;          /* +H'11, the style */

        slot = REG16(list + (u32)(long)(short)(u16)((u16)(box[4] << 1)));
        if (slot == 0) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
            continue;
        }

        slot = REG16(list + (u32)(long)(short)(u16)((u16)(box[4] << 1)));
        if (hitbox_list_is_patterns(list)) {
            bitmap_draw(box[0], box[1], box[2], box[3],
                        (const u8 *)REG32(ITEM_TABLE +
                            (u32)(long)(short)(u16)(ITEM_STRIDE * slot) + 0x08),
                        LCD_FRAME_A);
        } else {
            bitmap_draw(box[0], box[1], box[2], box[3],
                        (const u8 *)REG32(0x001158CEUL +
                            (u32)(long)(short)(u16)((u16)(slot << 2))),
                        LCD_FRAME_A);
        }
    }
}

/* ---- five that most of the screens lean on ----------------------------- */

void screen_store(u8 slot, u8 out);
