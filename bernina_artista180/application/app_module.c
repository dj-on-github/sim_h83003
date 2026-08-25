/* The artista 180 application, rebuilt in C: the embroidery module's panel:
 * its slate, its labels and its numbers.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- what the dispatcher does before it dispatches --------------------
 * Seven routines from the top of H'22382A: the pending screen change, the
 * touch settling, the foot switch, whether the module will take an order,
 * the key scan, and the screen save and restore.
 */

/* H'244C62. Whether the module has nothing of its own going on: seven bytes
 * that all have to be clear. */
u8 module_is_idle(void)
{
    if (REG8(0x00114D7BUL) != 0) return 0x00;
    if (REG8(0x00114D7EUL) != 0) return 0x00;
    if (REG8(0x001040B4UL) != 0) return 0x00;
    if (REG8(0x00104042UL) != 0) return 0x00;
    if (REG8(0x00114D7FUL) != 0) return 0x00;
    if (REG8(0x00114D69UL) != 0) return 0x00;
    if (REG8(0x00114D72UL) != 0) return 0x00;
    return 0x01;
}

/* H'249D6C. Whether the module will take an order: idle, and the link quiet.
 * The link test is the same one written out twice that H'248668 waits on. */
u8 module_ready(void)
{
    if (REG16(0x0011A63AUL) != 0) return 0x00;
    if (module_is_idle() == 0) return 0x00;
    if ((REG8(0x00114D50UL) & 0x21) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    if ((REG8(0x00114D50UL) & 0x22) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    if (REG8(0x00114D50UL) & 0x20) return 0x00;
    if (REG8(0x00114D86UL) != 0) return 0x00;
    return 0x01;
}

/* H'2237D0. A screen change left waiting by something that could not do it
 * itself: three bytes put back where the hardware reads them, a word put
 * back, and then the move. H'11B29C is the flag the dispatcher tests and
 * this clears; H'11B29B says a return happened. */
void screen_restore_pending(void)
{
    REG8(0x00FFFEE4UL) = REG8(0x0011B294UL);
    REG8(0x00FFFEE7UL) = REG8(0x0011B295UL);
    REG8(0x00FFFEEAUL) = REG8(0x0011B296UL);
    REG16(0x0011B108UL) = REG16(0x0011B298UL);
    screen_switch(REG8(0x0011B29AUL), 0x01, 0x00);
    REG8(0x0011B29CUL) = 0x00;
    REG8(0x0011B29BUL) = 0x01;
}

/* H'210E02. Whether the touch reading has settled. The two coordinate bytes
 * are remembered in H'11B2CA and H'11B2CC and a countdown in H'11A19A is set
 * to ten whenever they move. Ten readings the same and the countdown reaches
 * zero -- but the routine only says "yes" on the very first call, when the
 * countdown was zero to begin with. */
u8 touch_settled(void)
{
    if (REG16(0x0011A19AUL) == 0) {
        REG16(0x0011A19AUL) = 0x000A;
        return 0x01;
    }

    if (REG8(0x00FFFED9UL) == REG16(0x0011B2CAUL) &&
        REG8(0x00FFFEDAUL) == REG16(0x0011B2CCUL)) {
        REG16(0x0011A19AUL) = (u16)(REG16(0x0011A19AUL) - 1);
        return 0x00;
    }

    REG16(0x0011B2CAUL) = REG8(0x00FFFED9UL);
    REG16(0x0011B2CCUL) = REG8(0x00FFFEDAUL);
    REG16(0x0011A19AUL) = 0x000A;
    return 0x00;
}

/* H'215448. The foot switch, read from bit 7 of H'FFFEC4 with the position
 * in H'FFFEC5: nothing at all unless the switch is down, then screen H'17
 * for position zero and screen H'00 for position six. Any other position is
 * ignored. */
void foot_switch_screen(void)
{
    u8 where;

    if (!(REG8(0x00FFFEC4UL) & 0x80)) return;

    where = REG8(0x00FFFEC5UL);
    if (where == 0x00)      screen_switch(0x17, 0x01, 0x00);
    else if (where == 0x06) screen_switch(0x00, 0x01, 0x00);
}

/* H'21F68C. The key scan: eighteen bits over four ports tested in a fixed
 * order and the first one down named in H'11B10E, H'6D upwards. H'FFFF is
 * "nothing down".
 *
 * One key is special. H'75 is the one that starts the module, and if bit 0
 * of H'FFFEC4 says the module is there it is only accepted when the module
 * will take the order. When it will not, the key is not reported at all and
 * the scan carries on into the last two tests below it.
 */
void key_scan(void)
{
    if (REG8(0x00FFFEDCUL) & 0x04) { REG16(0x0011B10EUL) = 0x006D; return; }
    if (REG8(0x00FFFEDDUL) & 0x08) { REG16(0x0011B10EUL) = 0x006E; return; }
    if (REG8(0x00FFFEDDUL) & 0x20) { REG16(0x0011B10EUL) = 0x006F; return; }
    if (REG8(0x00FFFEDCUL) & 0x01) { REG16(0x0011B10EUL) = 0x0070; return; }
    if (REG8(0x00FFFEDBUL) & 0x01) { REG16(0x0011B10EUL) = 0x0071; return; }
    if (REG8(0x00FFFEDDUL) & 0x01) { REG16(0x0011B10EUL) = 0x0072; return; }
    if (REG8(0x00FFFEDCUL) & 0x08) { REG16(0x0011B10EUL) = 0x0073; return; }
    if (REG8(0x00FFFEDDUL) & 0x02) { REG16(0x0011B10EUL) = 0x0074; return; }
    if (REG8(0x00FFFEDDUL) & 0x04) { REG16(0x0011B10EUL) = 0x0076; return; }
    if (REG8(0x00FFFEDBUL) & 0x04) { REG16(0x0011B10EUL) = 0x0077; return; }
    if (REG8(0x00FFFEDCUL) & 0x02) { REG16(0x0011B10EUL) = 0x0078; return; }
    if (REG8(0x00FFFEDDUL) & 0x10) { REG16(0x0011B10EUL) = 0x0079; return; }
    if (REG8(0x00FFFEDBUL) & 0x40) { REG16(0x0011B10EUL) = 0x007A; return; }
    if (REG8(0x00FFFEC1UL) & 0x02) { REG16(0x0011B10EUL) = 0x007B; return; }
    if (REG8(0x00FFFEDBUL) & 0x80) { REG16(0x0011B10EUL) = 0x007C; return; }
    if (REG8(0x00FFFEDBUL) & 0x02) { REG16(0x0011B10EUL) = 0x007D; return; }
    if (REG8(0x00FFFEDCUL) & 0x10) { REG16(0x0011B10EUL) = 0x0081; return; }

    if (REG8(0x00FFFEDBUL) & 0x08 &&
        (!(REG8(0x00FFFEC4UL) & 0x01) || module_ready() != 0)) {
        REG16(0x0011B10EUL) = 0x0075;
        REG8(0x0011A16FUL) = 0x01;
        return;
    }

    if (REG8(0x00FFFEDCUL) & 0x20) REG16(0x0011B10EUL) = 0x007E;
    else                           REG16(0x0011B10EUL) = 0xFFFF;
}

/* H'21F4C6. The screen picture and its state put away and fetched back.
 * H'11A172 asks for the put-away and H'11A173 for the fetch, and which of
 * four slots is in H'11B100. Sixteen bytes of state travel with the picture:
 * the live copy is at H'11B0AE and each slot keeps its own at H'11B0BE and
 * every sixteen bytes after that.
 *
 * A slot number outside one to four leaves through the back door: the ask is
 * not cleared and the fetch below it does not happen either. */
void screen_put_away(void)
{
    const u32 STATE = 0x0011B0AEUL;
    u8 slot;
    short i;

    if (REG8(0x0011A172UL) != 0) {
        slot = REG8(0x0011B100UL);
        if (slot < 0x01 || slot > 0x04) return;
        screen_store(slot, 0x01);
        for (i = 0; i < 4; i++)
            REG32(0x0011B0BEUL + (u32)(slot - 1) * 0x10 + (u32)i * 4) =
                REG32(STATE + (u32)i * 4);
        REG8(0x0011A172UL) = 0x00;
    }

    if (REG8(0x0011A173UL) != 0) {
        slot = REG8(0x0011B100UL);
        if (slot < 0x01 || slot > 0x04) return;
        screen_store(slot, 0x00);
        for (i = 0; i < 4; i++)
            REG32(STATE + (u32)i * 4) =
                REG32(0x0011B0BEUL + (u32)(slot - 1) * 0x10 + (u32)i * 4);
        REG8(0x0011A173UL) = 0x00;
    }
}

/* H'23A036. Bit 3 of the attribute byte belonging to the pattern the slot
 * holds. The attributes are one byte each at H'0FFEB8. */
u8 pattern_attr_bit3(void)
{
    const u8 pattern = REG8(PAT_B(0x00));

    return (REG8(0x000FFEB8UL + pattern) & 0x08) ? 0x01 : 0x00;
}

/* H'231994. The current slot's stitch settings back to their defaults.
 * H'11A263 is the one field it leaves alone, and it writes H'11A265 before
 * H'11A264 -- both differences from the fuller version in H'2445F6. */
void stitch_reset_current(void)
{
    REG16(PAT_A(0x0C)) = 0x0000;
    REG16(PAT_A(0x0E)) = 0x0000;
    REG8(PAT_A(0x00)) = 0x32;
    REG8(PAT_A(0x01)) = 0x32;
    REG8(PAT_A(0x02)) = 0x32;
    REG8(PAT_A(0x03)) = 0x32;
    REG8(PAT_A(0x04)) = 0x5F;
    REG8(PAT_A(0x05)) = 0x00;
    REG8(PAT_A(0x06)) = 0x24;
    REG8(PAT_A(0x07)) = 0x32;
    REG8(PAT_A(0x08)) = 0x32;
    REG8(PAT_A(0x0B)) = 0x00;
    REG8(PAT_A(0x0A)) = 0x00;
    REG8(0x0011F4D9UL) = 0x32;
    REG8(0x0011F4DAUL) = 0x32;
    REG16(0x0011F292UL) = 0x0000;
    REG8(0x00114D96UL) = pattern_attr_bit3() != 0 ? 0x01 : 0x00;
}

/* H'23E6C4. The slot number waiting in H'11A640 becomes the current one and
 * its second record is started off: the slot itself, whatever H'114DA1
 * holds, and H'114D8C divided down by H'1B. Slot zero means nothing to
 * pick up and the routine does nothing at all. */
void pattern_slot_begin(void)
{
    const u8 slot = REG8(0x0011A640UL);

    if (slot == 0x00) return;

    REG8(0x0011A660UL) = slot;
    REG8(0x0011A63FUL) = slot;
    REG8(PAT_B(0x04)) = REG8(0x0011A640UL);
    REG8(PAT_B(0x03)) = REG8(0x00114DA1UL);
    REG8(PAT_B(0x05)) = (u8)((u16)REG8(0x00114D8CUL) / 0x1B);
    stitch_reset_current();
}

/* H'2416D6. Fourteen words of module counters, H'11F2E4 to H'11F300, and
 * the original clears them in an order of its own. */
void link_counters_clear(void)
{
    REG16(0x0011F2F6UL) = 0x0000;
    REG16(0x0011F2F8UL) = 0x0000;
    REG16(0x0011F2FAUL) = 0x0000;
    REG16(0x0011F2FCUL) = 0x0000;
    REG16(0x0011F2FEUL) = 0x0000;
    REG16(0x0011F300UL) = 0x0000;
    REG16(0x0011F2E4UL) = 0x0000;
    REG16(0x0011F2E8UL) = 0x0000;
    REG16(0x0011F2E6UL) = 0x0000;
    REG16(0x0011F2EAUL) = 0x0000;
    REG16(0x0011F2EEUL) = 0x0000;
    REG16(0x0011F2F2UL) = 0x0000;
    REG16(0x0011F2F0UL) = 0x0000;
    REG16(0x0011F2F4UL) = 0x0000;
}

/* H'23E462. A single RTS. Something used to happen here. */
void module_reset_hook(void)
{
}

/* H'244AAC. The pattern store, H'104D4A up to but not including H'10C27A,
 * zeroed a byte at a time. */
void pattern_store_clear(void)
{
    u32 p;

    for (p = 0x00104D4AUL; p < 0x0010C27AUL; p++) REG8(p) = 0x00;
}

/* H'2445F6. The module's own state. This writes the same stitch defaults as
 * H'231994 and three more fields with them, then empties the second record
 * altogether and clears the pattern store. */
void module_state_clear(void)
{
    short i;

    for (i = 0; i < 0x000C; i++) REG8(0x0011A612UL + (u16)i) = 0x00;

    REG8(0x001040BCUL) = 0x14;
    REG8(0x001040BDUL) = 0x96;
    REG8(0x00100282UL) = 0x09;
    REG8(0x00100283UL) = 0x3E;
    REG8(0x00100280UL) = 0x00;
    REG8(0x00100281UL) = 0x00;
    REG16(0x0011F302UL) = 0x007D;
    REG8(0x00114D9BUL) = 0x01;
    link_counters_clear();
    module_reset_hook();
    REG16(0x0011F2ECUL) = 0x0024;

    REG16(PAT_A(0x0C)) = 0x0000;
    REG16(PAT_A(0x0E)) = 0x0000;
    REG8(PAT_A(0x00)) = 0x32;
    REG8(PAT_A(0x01)) = 0x32;
    REG8(PAT_A(0x02)) = 0x32;
    REG8(PAT_A(0x03)) = 0x32;
    REG8(PAT_A(0x04)) = 0x5F;
    REG8(PAT_A(0x05)) = 0x00;
    REG8(PAT_A(0x06)) = 0x24;
    REG8(PAT_A(0x07)) = 0x32;
    REG8(PAT_A(0x08)) = 0x32;
    REG8(PAT_A(0x09)) = 0x00;
    REG8(PAT_A(0x0A)) = 0x00;
    REG8(PAT_A(0x0B)) = 0x00;

    REG8(PAT_B(0x00)) = 0x00;
    REG8(PAT_B(0x01)) = 0x00;
    REG8(PAT_B(0x02)) = 0x00;
    REG8(PAT_B(0x03)) = 0x00;
    REG8(PAT_B(0x04)) = 0x00;
    REG8(PAT_B(0x05)) = 0x00;

    REG32(0x0011F2C6UL) = 0x00104D4AUL;
    REG8(0x0011F4D9UL) = 0x32;
    REG8(0x0011F4DAUL) = 0x32;
    pattern_store_clear();

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;
    REG8(0x00114D73UL) = 0x00;
    REG8(0x0011F4E6UL) = 0x00;
    REG8(0x0011F310UL) = 0x00;
    REG8(0x0011F311UL) = 0x00;
    REG8(0x0011F312UL) = 0x00;
    REG8(0x0011F313UL) = 0x00;
    REG8(0x0011F314UL) = 0x00;
    REG16(0x0011A62AUL) = 0x0000;
    REG16(0x0011A62CUL) = 0x0000;
    REG8(0x0011F30EUL) = 0x00;
    REG8(0x0011F305UL) = 0x00;
    REG8(0x0011F304UL) = 0x00;
    REG8(0x00114D66UL) = 0x00;
    REG8(0x00114D62UL) = 0x00;
    REG8(0x00114D98UL) = 0x00;
    REG8(0x00104040UL) = 0x00;
    REG16(0x00114D4CUL) = (u16)(REG16(0x00114D4CUL) & ~0x4000);
}

/* H'244578. The whole slate: the slot picked up, three buffers zeroed, and
 * the module's state cleared behind them. */
void module_buffers_clear(void)
{
    short i;

    pattern_slot_begin();
    for (i = 0; i < 0x003D; i++) REG8(0x00114D7AUL + (u16)i) = 0x00;
    for (i = 0; i < 0x00AC; i++) REG8(0x00104C98UL + (u16)i) = 0x00;
    for (i = 0; i < 0x0400; i++) REG8(0x000FFE80UL + (u16)i) = 0x00;
    module_state_clear();
    REG8(0x00104040UL) = 0x00;
    REG8(0x001040B5UL) = 0x00;
}

void link_send_start(void);

/* ---- the module panel's five labels ------------------------------------
 * Five routines, each drawing one string into one fixed little box, all
 * centred, all with a gap of one. Three run down the left edge and two sit
 * along the top. They are *not* in address order -- H'21789C is the topmost
 * of the three down the left and H'21759E the lowest:
 *
 *   H'21789C  text_left_94    H'217932  text_top_CB
 *   H'2175E8  text_left_BC    H'21797E  text_top_102
 *   H'21759E  text_left_D9
 */
void text_left_94(const char *str)
{
    text_draw(str, 0x0000, 0x0094, 0x0023, 0x009C, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void text_left_BC(const char *str)
{
    text_draw(str, 0x0000, 0x00BC, 0x0023, 0x00C4, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void text_left_D9(const char *str)
{
    text_draw(str, 0x0000, 0x00D9, 0x0023, 0x00E1, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void text_top_CB(const char *str)
{
    text_draw(str, 0x00CB, 0x0012, 0x00E2, 0x0023, 0x0001, 0x02,
              (const u8 *)0x0011936EUL);
}

void text_top_102(const char *str)
{
    text_draw(str, 0x0102, 0x001A, 0x010D, 0x0023, 0x0001, 0x02,
              (const u8 *)0x001196EAUL);
}

/* H'21747E. The same three-way box as H'24A432 but at a fixed place, the
 * one state four uses. */
void module_fixed_box(u8 mode)
{
    if (mode == 0x00)
        draw_rect(0x00F1, 0x002C, 0x0113, 0x004E, LCD_FRAME_A, 0x00, 0x01);
    else if (mode == 0x01)
        bitmap_draw(0x00F1, 0x002C, 0x0113, 0x004E,
                    (const u8 *)0x0034C148UL, LCD_FRAME_A);
    else if (mode == 0x02)
        draw_rect(0x00F1, 0x002C, 0x0113, 0x004E, LCD_FRAME_A, 0x02, 0x01);
}

/* H'2179CA. Box five lit or not, the same shape as H'24A2DA. */
void box5_draw(u8 lit)
{
    if (lit != 0) {
        if (hitbox_kind(0x0005) == 0x01) hitbox_set_state(0x0005, 0x0005, 0x00, 0);
        hitbox_set_state(0x0005, 0x0005, 0x01, 0);
        return;
    }
    hitbox_set_state(0x0005, 0x0005, 0x00, 0);
}

/* H'231DD2. A number with "min" after it, in the third left-hand label. The
 * string is built in the RAM buffer at H'11F2D6 and the word itself is
 * spelled out into H'11F294 a byte at a time. */
void label_minutes(short value)
{
    int_to_decimal(value, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x6D;   /* 'm' */
    REG8(0x0011F295UL) = 0x69;   /* 'i' */
    REG8(0x0011F296UL) = 0x6E;   /* 'n' */
    REG8(0x0011F297UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_94((const char *)0x0011F2D6UL);
}

/* H'236B5A. The two numbers along the top: which colour is being sewn and
 * how many there are. Nothing is drawn unless H'114D91 says so, and a colour
 * number past H'1B is not a number at all -- it sets bit H'0400 of H'114D4C
 * and leaves the labels alone.
 *
 * Both numbers are cut to two digits by dropping a nought over the third
 * byte of the buffer, which is four bytes of stack. */
void label_colours(void)
{
    char buf[4];

    if (REG8(0x00114D91UL) == 0) return;

    if (REG8(0x00114D89UL) > 0x1B) {
        REG16(0x00114D4CUL) |= 0x0400;
        return;
    }

    if (REG8(0x00114D89UL) < REG8(0x00114D8DUL)) {
        int_to_decimal((short)(u16)(REG8(0x00114D89UL) + 1), buf);
        buf[2] = 0x00;
        text_top_CB(buf);
    }

    if (REG8(0x00114D8DUL) != 0) {
        int_to_decimal((short)(u16)REG8(0x00114D8DUL), buf);
        buf[2] = 0x00;
        text_top_102(buf);
    }
}

/* ---- under the module's state machine ---------------------------------
 * H'235B0E is the module's own state machine, eighteen states deep, and it
 * is what H'237E3C waits on. These are the routines beneath it: the panel it
 * draws, the arrows, the progress bar, and the small tests it asks.
 */

/* H'244CF6. Either of the two stop keys down: bit 2 or bit 6 of H'FFFEDB,
 * which H'21F68C knows as keys H'77 and H'7A. */
u8 stop_key_down(void)
{
    const u8 keys = REG8(0x00FFFEDBUL);

    if (keys & 0x04) return 0x01;
    if (keys & 0x40) return 0x01;
    return 0x00;
}

/* H'244D4E. The module's hardware state is one of the two that mean it is
 * running. */
u8 module_running(void)
{
    const u8 state = REG8(0x00FFFEC0UL);

    if (state == 0x04 || state == 0x06) return 0x01;
    return 0x00;
}

/* H'244D10. Two edges left by the interrupt in H'FFFEF7, each handed to the
 * link as an owner and then cleared. H'01 means neither was waiting. */
u8 module_edge_service(void)
{
    const u8 edges = REG8(0x00FFFEF7UL);

    if (edges & 0x02) {
        link_claim(0x09);
        REG8(0x00FFFEF7UL) &= (u8)~0x02;
        return 0x00;
    }
    if (edges & 0x04) {
        link_claim(0x1C);
        REG8(0x00FFFEF7UL) &= (u8)~0x04;
        return 0x00;
    }
    return 0x01;
}

/* H'24A2DA. The pause button in box seven drawn lit or not. Lit is two
 * calls, the first only when the box was already in kind one. */
void pause_button_draw(u8 lit)
{
    if (lit != 0) {
        if (hitbox_kind(0x0007) == 0x01) hitbox_set_state(0x0007, 0x0007, 0x00, 0);
        hitbox_set_state(0x0007, 0x0007, 0x01, 0);
        return;
    }
    hitbox_set_state(0x0007, 0x0007, 0x00, 0);
}

/* H'23E366. The pause button pressed: the module told to hold or to carry on,
 * depending on H'11F30E, and the button redrawn to match. Nothing happens
 * unless the hardware is at rest and the link is quiet. */
void module_pause_toggle(void)
{
    if (REG8(0x00FFFEC6UL) != 0) return;

    if (module_running() == 0) { link_claim(0x03); return; }

    if (REG8(0x00114DBAUL) != 0) return;

    if (REG8(0x0011F30EUL) == 0x00) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        REG8(0x0011F30EUL) = 0x01;
        pause_button_draw(0x01);
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x20;
        link_send_start();
        return;
    }

    if (REG8(0x0011F30EUL) == 0x01) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        REG8(0x0011F30EUL) = 0x00;
        pause_button_draw(0x00);
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x21;
        link_send_start();
        return;
    }

    REG8(0x0011F30EUL) = 0x00;
}

/* H'244DA0. The lid: bit 4 of H'FFFEC4 open. Opening it either releases the
 * pause or claims the link, and closing it sets H'114D98. H'114D86 holding
 * anything at all stops the lot. */
u8 module_lid_check(void)
{
    if (REG8(0x00114D86UL) == 0) {
        if (REG8(0x00FFFEC4UL) & 0x10) {
            REG8(0x00114D98UL) = 0x00;
            if (REG8(0x0011F30EUL) != 0) { module_pause_toggle(); return 0x01; }
            link_claim(0x01);
            return 0x01;
        }
        REG8(0x00114D98UL) = 0x01;
    }
    return 0x00;
}

/* H'24A432. The little picture box beside the module panel, in one of three
 * ways: cleared, drawn from H'34C148, or filled with colour two. Where it
 * goes depends on H'114D8E, and five of the twelve states have no box. */
void module_panel_box(u8 mode)
{
    u16 x, y;

    if (REG8(0x00114DB9UL) != 0) return;
    if (REG8(0x0011B0A8UL) != 0) return;

    switch (REG8(0x00114D8EUL)) {
    case 0x07:                            x = 0x00F1; y = 0x00C8; break;
    case 0x02: case 0x03:                 x = 0x00F0; y = 0x0029; break;
    case 0x06:                            x = 0x00EF; y = 0x002C; break;
    case 0x04: case 0x05: case 0x08:
    case 0x09:                            x = 0x00F1; y = 0x002C; break;
    default:                              return;
    }

    if (mode == 0x00)
        draw_rect(x, y, (u16)(x + 0x22), (u16)(y + 0x22),
                  LCD_FRAME_A, 0x00, 0x01);
    else if (mode == 0x01)
        bitmap_draw(x, y, (u16)(x + 0x22), (u16)(y + 0x22),
                    (const u8 *)0x0034C148UL, LCD_FRAME_A);
    else if (mode == 0x02)
        draw_rect(x, y, (u16)(x + 0x22), (u16)(y + 0x22),
                  LCD_FRAME_A, 0x02, 0x01);
}

/* H'24A572 and H'24A5D0. A run of dots plotted from a table of offsets at
 * H'11A1F0 -- pairs of bytes, x then y, added to the origin given. The first
 * lays down twenty-six of them and the second forty-three, out of the same
 * table. */
static void module_dots(u16 x0, u16 y0, short n)
{
    short i;

    for (i = 0; i < n; i += 2)
        plot_pixel((u16)(x0 + REG8(0x0011A1F0UL + (u32)(long)i)),
                   (u16)(y0 + REG8(0x0011A1F1UL + (u32)(long)i)),
                   LCD_FRAME_A, 0x03);
}

void module_dots_small(u16 x0, u16 y0) { module_dots(x0, y0, 0x0034); }
void module_dots_large(u16 x0, u16 y0) { module_dots(x0, y0, 0x0056); }

/* H'2499BE, H'249A0A, H'249A56, H'249AA2. Four arrows, each a box and a
 * picture: the forward arrow in boxes three and two, the back arrow in boxes
 * two and one. */
void module_arrow_fwd_3(u8 lit)
{
    hitbox_blit(0x0003, LCD_FRAME_A, lit ? ARROW_FWD_LIT : ARROW_FWD_DIM);
}

void module_arrow_back_2(u8 lit)
{
    hitbox_blit(0x0002, LCD_FRAME_A, lit ? ARROW_BACK_LIT : ARROW_BACK_DIM);
}

void module_arrow_fwd_2(u8 lit)
{
    hitbox_blit(0x0002, LCD_FRAME_A, lit ? ARROW_FWD_LIT : ARROW_FWD_DIM);
}

void module_arrow_back_1(u8 lit)
{
    hitbox_blit(0x0001, LCD_FRAME_A, lit ? ARROW_BACK_LIT : ARROW_BACK_DIM);
}

/* H'217AEA. The progress bar across the module panel, drawn into frame B
 * between H'28 and H'BF at rows H'2E to H'4C. The scale is worked out in
 * floating point: a hundred and fifty-one pixels over ninety-nine steps, so
 * one per cent is H'1.5252526 and the zero point is H'28 less that.
 *
 * A value outside nought to a hundred draws nothing at all. */
void module_progress_bar(u16 percent)
{
    const float step = 1.5252526f;
    const float base = 40.0f - step;

    if ((short)percent < 0) return;
    if ((short)percent > 0x0064) return;

    if (percent == 0x0064) {
        draw_rect(0x0028, 0x002E, 0x00BF, 0x004C, LCD_FRAME_B, 0x02, 0x01);
        return;
    }
    if (percent == 0x0000) {
        draw_rect(0x0028, 0x002E, 0x00BF, 0x004C, LCD_FRAME_B, 0x00, 0x01);
        return;
    }

    {
        const u16 edge = (u16)(int)((float)(long)(short)percent * step
                                    + base + 0.5f);

        draw_rect(0x0028, 0x002E, edge, 0x004C, LCD_FRAME_B, 0x02, 0x01);
        draw_rect((u16)(edge + 1), 0x002E, 0x00BF, 0x004C,
                  LCD_FRAME_B, 0x00, 0x01);
    }
}

/* H'2498BE. The panel box blinked while the module is working: on for ten
 * passes, off for ten more, and the counter reset when it runs past twenty.
 * A stop key held freezes it wherever it is. */
void module_panel_blink(u8 on)
{
    const u16 period = 0x000A;

    if (on == 0) {
        module_panel_box(0x02);
        REG16(0x0011F5A2UL) = 0x0000;
        return;
    }

    if (stop_key_down() != 0) return;

    REG16(0x0011F5A2UL) = (u16)(REG16(0x0011F5A2UL) + 1);

    if (REG16(0x0011F5A2UL) != 0 && period > REG16(0x0011F5A2UL)) {
        module_panel_box(0x01);
        return;
    }

    module_panel_box(0x00);
    if ((u16)(period << 1) >= REG16(0x0011F5A2UL)) return;
    REG16(0x0011F5A2UL) = 0x0000;
}

/* H'24992E. The progress bar kept up to date, but only in state seven and
 * only while the link is unclaimed. Anything over a hundred is pinned to a
 * hundred unless it is exactly H'C8, which means finished. H'11F5A4 holds
 * what was last drawn so a repeat costs nothing.
 *
 * The second argument is defaulted to H'0A and then never used again. The
 * original writes the default back into the caller's own stack slot, which
 * is a store the reconstruction has no way to make; the cases leave that
 * address out.
 */
void module_speed_show(u16 value, u16 hold)
{
    if (REG8(0x00114DB9UL) != 0) return;
    if (REG8(0x00114D8EUL) != 0x07) return;

    if ((short)value > 0x0064 && value != 0x00C8) value = 0x0064;
    if (hold == 0) hold = 0x000A;
    (void)hold;

    if (value == 0x0000) {
        REG16(0x0011F5A4UL) = 0x0000;
        module_progress_bar(0x0000);
        return;
    }
    if (value == 0x00C8) {
        REG16(0x0011F5A4UL) = 0x0000;
        module_progress_bar(0x0064);
        return;
    }

    if (stop_key_down() != 0) return;

    if (REG16(0x0011F5A4UL) != value) {
        module_progress_bar(value);
        REG16(0x0011F5A4UL) = value;
    }
}

/* H'23E1A0. A countdown in H'11F572 driving bit 0 of H'114D58 -- the thing
 * that flashes while the module is doing something. What arrives says what
 * to do: H'00 is a tick, H'01 starts it, H'02 stops it, H'03 stops it only
 * when the hardware is at rest, and H'04 starts it unless H'114D96 says not.
 *
 * A tick answers H'01 on the last one and H'05 on the one before, which is
 * how the caller knows the run is ending. */
u8 module_flash_step(u8 what)
{
    if (what == 0x00) {
        const u8 n = REG8(0x0011F572UL);

        if (n != 0) {
            if (n == 0x01) {
                REG8(0x0011F572UL) = 0x00;
                REG8(0x00114D58UL) &= (u8)~0x01;
                return 0x01;
            }
            if (n == 0x02) {
                REG8(0x0011F572UL) = (u8)(n - 1);
                return 0x05;
            }
            REG8(0x0011F572UL) = (u8)(n - 1);
        }
        return 0x00;
    }

    if (what == 0x01) {
        REG8(0x00114D58UL) |= 0x01;
        REG8(0x0011F572UL) = 0x07;
    } else if (what == 0x04) {
        if (REG8(0x00114D96UL) == 0) {
            REG8(0x00114D58UL) |= 0x01;
            REG8(0x0011F572UL) = 0x07;
        }
    } else if (what == 0x02) {
        REG8(0x00114D58UL) &= (u8)~0x01;
        REG8(0x0011F572UL) = 0x00;
    } else if (what == 0x03) {
        const u8 state = REG8(0x00FFFEC6UL);

        if (state == 0x00 || state == 0x05) REG8(0x0011F572UL) = 0x00;
    }
    return 0x00;
}

/* H'242FEA. How many minutes are left, worked out in floating point and
 * drawn in the third left-hand label only when the number changes.
 *
 * Two ways round, by H'114D96. One divides the stitches done by the total;
 * the other scales by a second pair of counters first. Either way the answer
 * is the pattern's own rate times what is left to do, plus one.
 *
 * [mode] H'00 forgets the last number and draws nothing, H'01 refuses to say
 * nought, H'02 always says nought.
 *
 * The ROM leaves its working slot alone when H'114D96 is neither nought nor
 * one, and converts whatever the stack happened to hold. That cannot be
 * written down, so this starts the slot at nought; no case goes there.
 */
void module_minutes_left(u8 mode)
{
    float left = 0.0f;
    u16 rate;

    if (mode == 0) { REG16(0x0011F592UL) = 0x0000; return; }

    REG32(0x0011A654UL) = REG32(0x0011A654UL) + 2;
    REG32(0x0011A65CUL) = REG32(0x0011A65CUL) + 2;

    rate = REG16(0x00104C96UL +
                 (u32)(long)(short)(u16)((u16)REG8(PAT_B(0x00)) << 1));

    if (REG8(0x00114D96UL) == 0x01) {
        left = (float)(u32)rate
             * (1.0f - (float)(u32)REG32(0x0011A654UL)
                     / (float)(u32)REG32(0x0011A650UL))
             + 1.0f;
    } else if (REG8(0x00114D96UL) == 0x00) {
        const float part = (float)(u32)rate
                         * (float)(u32)REG32(0x0011A658UL)
                         / (float)(u32)REG32(0x0011A650UL);

        left = part * (1.0f - (float)(u32)REG32(0x0011A65CUL)
                            / (float)(u32)REG32(0x0011A658UL))
             + 1.0f;
    }

    REG16(0x0011F4E4UL) = (u16)(int)left;

    if (mode == 0x02) REG16(0x0011F4E4UL) = 0x0000;
    else if (mode == 0x01) {
        if (REG16(0x0011F4E4UL) == 0) REG16(0x0011F4E4UL) = 0x0001;
    }

    if (REG16(0x0011F4E4UL) != REG16(0x0011F592UL)) {
        label_minutes((short)REG16(0x0011F4E4UL));
        REG16(0x0011F592UL) = REG16(0x0011F4E4UL);
    }
}

/* H'242F26. Message H'04/H'0E sent and then, on the next pass, the estimate
 * forgotten and the caller's step counter moved on. Both halves wait for the
 * link to be quiet. */
void module_ask_time(u8 *step)
{
    if (REG8(0x0011F590UL) == 0x00) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x0E;
        link_send_start();
        REG8(0x0011F590UL) = (u8)(REG8(0x0011F590UL) + 1);
        return;
    }

    if ((REG8(0x00114D50UL) & 0x21) != 0) return;
    if (REG8(0x0011F29EUL) != 0) return;
    if (REG8(0x0011F2B6UL) != 0) return;
    if ((REG8(0x00114D50UL) & 0x22) != 0) return;
    if (REG8(0x0011F29EUL) != 0) return;
    if (REG8(0x0011F2B6UL) != 0) return;

    module_minutes_left(0x00);
    REG8(0x0011F590UL) = 0x00;
    *step = (u8)(*step + 1);
}

/* ---- the colour picture ------------------------------------------------
 * The module's pattern data at H'104D4A is a run of records, each two words
 * of size followed by that many bytes, and H'238B62 draws the one belonging
 * to a colour as a one-bit-per-pixel stencil.
 */

/* H'238D06. Walks [index] records forward from H'104D4A and reads the four
 * words at the head of the one it lands on, handing back the address just
 * past them. Every word is big-endian and put together a byte at a time.
 *
 * The record's length is its first two words multiplied together plus eight,
 * and the running offset is kept in sixteen bits, so a long enough list
 * wraps -- which is what the caller's limit check is there to catch. */
const u8 *module_colour_record(u16 *w, u16 *h, u16 *px, u16 *py, u8 index)
{
    const u8 *p = (const u8 *)0x00104D4AUL;
    u16 offset = 0;
    u16 i;

    for (i = 0; (u16)index > i; i++) {
        const u16 a = (u16)(((u16)p[0] << 8) | (u16)p[1]);
        const u16 b = (u16)(((u16)p[2] << 8) | (u16)p[3]);

        offset = (u16)(offset + (u16)((u16)(b * a) + 8));
        p = (const u8 *)(0x00104D4AUL + (u32)offset);
    }

    *w  = (u16)(((u16)p[0] << 8) | (u16)p[1]);
    *h  = (u16)(((u16)p[2] << 8) | (u16)p[3]);
    *px = (u16)(((u16)p[4] << 8) | (u16)p[5]);
    *py = (u16)(((u16)p[6] << 8) | (u16)p[7]);
    return p + 8;
}

/* H'238B62. The colour's outline drawn over the panel. The panel is fetched
 * back from the third store first, so what was there is rubbed out, and then
 * the record's bytes are walked bit by bit -- most significant first -- with
 * a pixel put down wherever a bit is set.
 *
 * Where it goes is the record's own two words, shifted by the hoop offset in
 * H'1040BE and H'1040BF: eight times the first less four times the hoop's x,
 * plus H'88, and the second less half the hoop's y, plus H'8B.
 *
 * Reading past H'114D49 sets bit H'2000 of H'114D4C and skips the byte. That
 * is the only guard: the record itself is trusted. */
void module_colour_bitmap(u8 index)
{
    const u16 vx = (u16)((short)(u16)((u16)REG8(0x001040BEUL) << 3) / (short)2);
    const u16 vy = (u16)((u16)REG8(0x001040BFUL) / 2);
    u16 w = 0, h = 0, px = 0, py = 0;
    const u8 *data;
    u16 x0, y0, row, col;

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;

    data = module_colour_record(&w, &h, &px, &py, index);

    x0 = (u16)((u16)((u16)(px << 1) << 2) - vx + 0x0088);
    y0 = (u16)(py - vy + 0x008B);

    region_copy(0x0026, 0x002C, 0x00EA, 0x00EA, 0x002C,
                0x000F1610UL, LCD_FRAME_A);

    for (row = 0; row < h; row++) {
        u16 xbit = 0;

        for (col = 0; col < w; col++) {
            const u32 at = (u32)data + (u32)(u16)((u16)(w * row) + col);
            u8 bits, mask;
            u16 k;

            if (at > 0x00114D49UL) {
                REG16(0x00114D4CUL) |= 0x2000;
                continue;
            }

            bits = REG8(at);
            mask = 0x80;
            for (k = 0; k < 8; k++) {
                if ((u8)(mask & bits) == mask)
                    plot_pixel((u16)(x0 + xbit), (u16)(y0 + row),
                               LCD_FRAME_A, 0x03);
                mask = (u8)(mask >> 1);
                xbit++;
            }
        }
    }
}

/* H'23865A. The same two numbers as H'236B5A, with the colour's picture
 * drawn under them and the cursor forgotten. The limit here is H'3C rather
 * than H'1B, and the numbers are only drawn when H'114D91 says so -- but the
 * picture is drawn either way, once, whichever of the two halves gets to it
 * first. */
void label_colours_picture(void)
{
    char buf[4];
    u8 drawn = 0;

    if (REG8(0x00114D89UL) > 0x3C) {
        REG16(0x00114D4CUL) |= 0x0400;
        return;
    }

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;

    if (REG8(0x00114D89UL) < REG8(0x00114D8DUL)) {
        int_to_decimal((short)(u16)(REG8(0x00114D89UL) + 1), buf);
        buf[2] = 0x00;
        if (REG8(0x00114D91UL) != 0) text_top_CB(buf);
        module_colour_bitmap(REG8(0x00114D89UL));
        drawn = 0x01;
    }

    if (REG8(0x00114D8DUL) != 0) {
        int_to_decimal((short)(u16)REG8(0x00114D8DUL), buf);
        buf[2] = 0x00;
        if (REG8(0x00114D91UL) != 0) text_top_102(buf);
        if (drawn == 0) module_colour_bitmap(REG8(0x00114D89UL));
    }
}

/* ---- the module panel's numbers ----------------------------------------
 * Three routines that put numbers on the embroidery panel. All of them
 * build their string in the RAM buffer at H'11F2D6 and spell the units out
 * into H'11F294 a byte at a time.
 */

/* H'23202A. How many stitches, drawn at one of two places depending on
 * H'114D8E, with a run of dots under it. The number is the pattern's own
 * count plus H'1B for every whole turn in the sixth byte of its record, and
 * when bit 1 of H'114D51 is set the count past H'114DBC is taken off and the
 * longer run of dots drawn instead.
 *
 * A record of kind three has no number at all. */
void module_count_label(void)
{
    u8 n, over = 0x00;
    u8 where;

    if (REG8(PAT_B(0x03)) == 0x03) return;

    REG8(0x0011F2DBUL) = 0x00;

    n = (u8)(REG8(PAT_B(0x00))
             + (u8)(u16)((u16)REG8(PAT_B(0x05)) * 0x1B));

    if (REG8(0x00114D51UL) & 0x02) {
        if (REG8(PAT_B(0x03)) != 0x01) {
            const u8 limit = REG8(0x00114DBCUL);

            if (n > limit) { n = (u8)(n - limit); over = 0x01; }
        }
    }

    int_to_decimal((short)(u16)n, (char *)0x0011F2D6UL);

    where = REG8(0x00114D8EUL);

    if (where >= 0x08 && where <= 0x09) {
        text_draw((const char *)0x0011F2D6UL, 0x000F, 0x0098, 0x001E, 0x00A0,
                  0x0001, 0x02, (const u8 *)0x001196EAUL);
        if (over == 0x01) module_dots_large(0x0002, 0x0098);
        else              module_dots_small(0x0008, 0x0098);
        return;
    }

    if (where >= 0x04 && where <= 0x05) {
        text_draw((const char *)0x0011F2D6UL, 0x000F, 0x004A, 0x001E, 0x0052,
                  0x0001, 0x02, (const u8 *)0x001196EAUL);
        if (over == 0x01) module_dots_large(0x0002, 0x004A);
        else              module_dots_small(0x0008, 0x004A);
    }
}

/* H'231C5E. How wide and how tall, in millimetres, in the first two
 * left-hand labels. The two numbers arrive in stitches and are scaled by the
 * first two bytes of the stitch record: twice the count times the byte, over
 * a hundred, in floating point and in that order. */
void module_size_labels(short across, short down)
{
    const int w = (int)((float)(long)across * 2.0f
                        * (float)(u32)REG8(PAT_A(0x00)) / 100.0f);
    const int h = (int)((float)(long)down * 2.0f
                        * (float)(u32)REG8(PAT_A(0x01)) / 100.0f);

    int_to_decimal((short)w, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x6D;   /* 'm' */
    REG8(0x0011F295UL) = 0x6D;   /* 'm' */
    REG8(0x0011F296UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_D9((const char *)0x0011F2D6UL);

    int_to_decimal((short)h, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x6D;
    REG8(0x0011F295UL) = 0x6D;
    REG8(0x0011F296UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_BC((const char *)0x0011F2D6UL);

    module_count_label();
}

/* H'23228A. How far through, as a percentage with a "%" after it, drawn low
 * on the right. The sum is H'114DBE tenths plus H'114DBF, and the answer is
 * five thousand seven hundred and eighty over that -- H'57.8 times a hundred,
 * multiplied out at run time rather than folded.
 *
 * Six bytes are copied out of H'25077C into the buffer first and then
 * written straight over by the number. Reproduced because it is there. */
void label_percent(void)
{
    char buf[18];
    float sum;

    REG16((u32)(unsigned long)&buf[0]) = REG16(0x0025077CUL);
    REG16((u32)(unsigned long)&buf[2]) = REG16(0x0025077EUL);
    REG16((u32)(unsigned long)&buf[4]) = REG16(0x00250780UL);

    sum = (float)(u32)REG8(0x00114DBEUL) / 10.0f
        + (float)(u32)REG8(0x00114DBFUL);

    int_to_decimal((short)(int)(57.8f * 100.0f / sum), buf);

    REG8(0x0011F294UL) = 0x25;   /* '%' */
    REG8(0x0011F295UL) = 0x00;
    str_append(buf, (const char *)0x0011F294UL);

    text_draw(buf, 0x00F2, 0x00A3, 0x0112, 0x00AE, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

/* H'23BB18. The check before a pattern is sewn: the module is asked for its
 * hoop, and the answer compared against the three stitch-record bytes it was
 * last asked about. Nothing happens at all unless the pattern's attribute
 * bit says it needs one.
 *
 * H'11F565 walks the four steps -- ask, wait, take the answer, done -- and
 * H'104043 is the once-only reset that starts it. Step nought short-circuits
 * the lot when the three bytes have not moved since last time.
 *
 * The blinking box is driven from here too: on while the link is busy, off
 * once the answer is in, and never in state seven.
 */
void module_hoop_check(u8 *step)
{
    u8 n;

    if (pattern_attr_bit3() == 0) { *step = (u8)(*step + 1); return; }

    if (REG8(0x00104043UL) == 0) {
        REG8(0x0011F4D6UL) = 0x00;
        REG8(0x0011F4D7UL) = 0x00;
        REG8(0x0011F4D8UL) = 0x00;
        REG8(0x0011F565UL) = 0x00;
        REG8(0x00104043UL) = 0x01;
    }

    n = REG8(0x0011F565UL);

    if (n == 0x00 || n == 0x01) {
        if (n == 0x00) {
            if (REG8(PAT_A(0x00)) == REG8(0x0011F4D6UL) &&
                REG8(PAT_A(0x01)) == REG8(0x0011F4D7UL) &&
                REG8(PAT_A(0x06)) == REG8(0x0011F4D8UL)) {
                *step = (u8)(*step + 1);
                return;
            }
            REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        }

        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61BUL) = 0x05;
        link_send_start();
        REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        return;
    }

    if (n == 0x02) {
        if ((REG8(0x00114D50UL) & 0x21) != 0 ||
            REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
            if (REG8(0x00114D8EUL) != 0x07) module_panel_blink(0x01);
            return;
        }

        REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        REG8(0x0011F4D6UL) = REG8(PAT_A(0x00));
        REG8(0x0011F4D7UL) = REG8(PAT_A(0x01));
        REG8(0x0011F4D8UL) = REG8(PAT_A(0x06));
        REG8(0x00114D8DUL) = REG8(0x00104045UL);

        if (REG8(0x00104041UL) != REG8(0x00114D8DUL)) {
            REG8(0x00104041UL) = REG8(0x00114D8DUL);
            REG8(0x00114D92UL) = 0xFF;
        }

        if (REG8(0x00114D8EUL) != 0x07) module_panel_blink(0x00);
        return;
    }

    if (n == 0x03) {
        if (REG8(0x00114D8DUL) < REG8(0x00104044UL)) {
            module_panel_blink(0x00);
            link_claim(0x19);
        }
        REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        return;
    }

    if (REG8(0x00114DB9UL) == 0) {
        REG8(0x0011F565UL) = 0x00;
        *step = (u8)(*step + 1);
    }
}
