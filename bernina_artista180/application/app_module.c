/* The artista 180 application, rebuilt in C: the embroidery module's panel:
 * its slate, its labels and its numbers.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"
#include "app_keys.h"

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

/* H'2236F8. The pattern the machine is on, put aside -- or asked for back.
 *
 * Which of the two depends on the screen. The screens that carry a pattern
 * -- H'02 to H'04, H'07, H'30, H'33 to H'36 and H'45 -- write the whole of
 * it into the H'11B29x block and put H'11B29B down to say "there is
 * something parked here". Every other screen takes the other path and asks
 * for it back, but only when the dialog state is still the one that parked
 * it, and never while the motor is running.
 *
 * Asking for it back is two bytes and a flag: H'11B29C is what the screen
 * dispatcher tests, and H'2237D0 below is what acts on it.
 */
void screen_state_park(void)
{
    const u8 s = REG8(0x0011A169UL);
    u8 carries;

    if      (s < 0x02) carries = 0;
    else if (s < 0x05) carries = 1;
    else if (s == 0x07) carries = 1;
    else if (s == 0x30) carries = 1;
    else if (s < 0x33) carries = 0;
    else if (s < 0x37) carries = 1;
    else if (s == 0x45) carries = 1;
    else                carries = 0;

    if (carries) {
        REG16(0x0011B290UL) = REG16(0x00FFFEE0UL);
        REG16(0x0011B292UL) = (u16)REG8(0x00FFFEFDUL);   /* a byte, widened */
        REG8(0x0011B294UL) = REG8(0x00FFFEE4UL);
        REG8(0x0011B295UL) = REG8(0x00FFFEE7UL);
        REG8(0x0011B296UL) = REG8(0x00FFFEEAUL);
        REG16(0x0011B298UL) = REG16(0x0011B108UL);
        REG8(0x0011B29AUL) = REG8(0x0011A169UL);
        REG8(0x0011B29BUL) = 0x00;
        REG8(0x0011B29DUL) = REG8(0x0011A174UL);
        return;
    }

    if (REG8(0x00114DC6UL) & 0x80) return;
    if (REG8(0x0011A174UL) != REG8(0x0011B29DUL)) return;

    REG8(0x0011B29CUL) = 0x01;
    REG16(0x00FFFEE0UL) = REG16(0x0011B290UL);
    REG8(0x00FFFEFDUL) = REG8(0x0011B293UL);
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
 *
 * The names are from pressing each button and watching H'11B10E. The codes,
 * the panel as it is printed on the machine, and the two names that do not
 * yet agree with it, are all in app_keys.h.
 */  
void key_scan(void)
{
    if (REG8(0x00FFFEDCUL) & 0x04) { REG16(0x0011B10EUL) = KEY_MEM; return; }
    if (REG8(0x00FFFEDDUL) & 0x08) { REG16(0x0011B10EUL) = KEY_LEFT; return; }
    if (REG8(0x00FFFEDDUL) & 0x20) { REG16(0x0011B10EUL) = KEY_RIGHT; return; }
    if (REG8(0x00FFFEDCUL) & 0x01) { REG16(0x0011B10EUL) = KEY_BUTTONHOLE; return; }
    if (REG8(0x00FFFEDBUL) & 0x01) { REG16(0x0011B10EUL) = KEY_STITCH_SEL; return; }
    if (REG8(0x00FFFEDDUL) & 0x01) { REG16(0x0011B10EUL) = KEY_FANCY; return; }
    if (REG8(0x00FFFEDCUL) & 0x08) { REG16(0x0011B10EUL) = KEY_HELP; return; }
    if (REG8(0x00FFFEDDUL) & 0x02) { REG16(0x0011B10EUL) = KEY_MODULE; return; }
    if (REG8(0x00FFFEDDUL) & 0x04) { REG16(0x0011B10EUL) = KEY_PEN_UPDOWN; return; }
    if (REG8(0x00FFFEDBUL) & 0x04) { REG16(0x0011B10EUL) = KEY_CLR; return; }
    if (REG8(0x00FFFEDCUL) & 0x02) { REG16(0x0011B10EUL) = KEY_A; return; }
    if (REG8(0x00FFFEDDUL) & 0x10) { REG16(0x0011B10EUL) = KEY_OUTPUT; return; }
    if (REG8(0x00FFFEDBUL) & 0x40) { REG16(0x0011B10EUL) = KEY_F; return; }
    if (REG8(0x00FFFEC1UL) & 0x02) { REG16(0x0011B10EUL) = KEY_REVERSE; return; }
    if (REG8(0x00FFFEDBUL) & 0x80) { REG16(0x0011B10EUL) = KEY_C_EQUALS; return; }
    if (REG8(0x00FFFEDBUL) & 0x02) { REG16(0x0011B10EUL) = KEY_FRAME; return; }
    if (REG8(0x00FFFEDCUL) & 0x10) { REG16(0x0011B10EUL) = KEY_SMART; return; }

    if (REG8(0x00FFFEDBUL) & 0x08 &&
        (!(REG8(0x00FFFEC4UL) & 0x01) || module_ready() != 0)) {
        REG16(0x0011B10EUL) = KEY_75;
        REG8(0x0011A16FUL) = 0x01;
        return;
    }

    if (REG8(0x00FFFEDCUL) & 0x20) REG16(0x0011B10EUL) = KEY_ECO;
    else                           REG16(0x0011B10EUL) = KEY_NONE;
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

/* H'222AAC. The screen the panel is asking for, when it is one of the
 * eighteen the dispatcher deals with itself.
 *
 * Four things stop it before it starts: a screen change already under way
 * (H'11A171), one already settled on (H'11B0A8), the machine running with
 * bit 7 of H'FFFEC4, and -- when bit 0 says the embroidery module is there
 * -- the module not being ready. Then H'21F940 says whether the screen
 * being asked for may be gone to at all, and what it is; and the module's
 * own key handler gets it either way.
 *
 * Only H'70 to H'81 are handled here, and seven of those eighteen do
 * nothing. Two of the bodies run into the next one when the configuration
 * block matches neither of the two machines they know about, which is in
 * the original and is kept.
 */
void screen_request(void)
{
    u16 want = 0;

    if (REG8(0x0011A171UL) != 0) return;
    if (REG8(0x0011B0A8UL) != 0) return;
    if (REG8(0x00FFFEC4UL) & 0x80) return;
    if ((REG8(0x00FFFEC4UL) & 0x01) && module_ready() == 0) return;

    if (screen_leave_check(&want, 0x00) != 0x03) return;
    module_key((u8)want);

    if ((u16)(want + 0xFF90) > 0x0011) return;   /* H'70 to H'81 only */

    switch (want) {
    /* The two menu keys: on the sewing screens they only mark what the
     * screen should show next time it is laid out; anywhere else they go
     * to the sewing screen with the first item of that category picked. */
    case KEY_BUTTONHOLE:
    case KEY_STITCH_SEL: {
        const u8 category = (u8)((want == KEY_BUTTONHOLE) ? 0x04 : 0x03);

        if (touch_allowed(want) == 0) return;
        if (REG8(0x0011A169UL) == 0x02 || REG8(0x0011A169UL) == 0x07) {
            REG8(0x0011A170UL) = 0x01;
            REG8(0x0011B28EUL) = category;
        } else {
            REG16(0x0011B108UL) = first_item_of_category(category, MENU_LIST);
            screen_switch(0x02, 0x01, 0x00);
        }
        return;
    }

    case KEY_FANCY:
        if (CONFIG_BLOCK == CONFIG_ALT_POINTER) {
            if (REG8(0x0011A169UL) != 0x05) screen_switch(0x05, 0x01, 0x00);
            return;
        }
        if (CONFIG_BLOCK == CONFIG_NO_MODULE) {
            if (REG8(0x0011A169UL) != 0x06) screen_switch(0x06, 0x01, 0x00);
            return;
        }
        /* falls through -- a machine that is neither gets H'74's body */

    case KEY_MODULE:
        if (REG8(0x0011A174UL) != 0) {
            screen_back_out(REG8(0x0011A169UL));
            screen_hand_over(0x12);
        } else {
            screen_switch(0x12, 0x01, 0x00);
        }
        return;

    case KEY_HELP:
        REG16(0x0011B114UL) = REG16(0x0011B108UL);
        REG8(0x0011B0A6UL) = REG8(0x0011A169UL);
        screen_switch(0x0E, 0x01, 0x00);
        return;

    case KEY_A:
        if (CONFIG_BLOCK == CONFIG_ALT_POINTER) {
            if (REG8(0x0011A169UL) != 0x25) screen_switch(0x25, 0x01, 0x00);
            return;
        }
        if (CONFIG_BLOCK == CONFIG_NO_MODULE) {
            if (REG8(0x0011A169UL) != 0x26) screen_switch(0x26, 0x01, 0x00);
            return;
        }
        /* falls through -- and so does this one, into H'79 */

    case KEY_OUTPUT: {
        const u8 s = REG8(0x0011A169UL);

        if (REG8(0x00114DC6UL) & 0x80) return;
        if (s == 0x01) return;
        if (s >= 0x27) {
            if (s != 0x2D && s != 0x30) {
                if (s < 0x32) return;
                if (s == 0x44) return;
            }
        }
        screen_remember(0x03);
        screen_switch(0x27, 0x01, 0x00);
        REG16(0x0011B116UL) = REG16(0x0011B108UL);
        return;
    }

    case KEY_FRAME:
        screen_switch(0x32, 0x01, 0x00);
        return;

    case KEY_CLR:
        if (REG8(0x0011A169UL) != 0x44) pattern_reset_current();
        return;

    case KEY_ECO:
        if (REG8(0x00FFFEF7UL) & 0x80) foot_demand_restore();
        else                           foot_demand_hold();
        return;

    /* The pattern parked by H'2236F8, asked for back -- but only when the
     * dialog is still the one that parked it, and never while sewing. */
    case KEY_SMART:
        if (REG8(0x00FFFEC4UL) & 0x01) return;
        if (REG8(0x0011B29BUL) != 0 ||
            REG8(0x0011A174UL) != REG8(0x0011B29DUL)) {
            screen_state_park();
            return;
        }
        if (REG8(0x00114DC6UL) & 0x80) return;
        REG8(0x0011B29CUL) = 0x01;
        REG16(0x00FFFEE0UL) = REG16(0x0011B290UL);
        REG8(0x00FFFEFDUL) = REG8(0x0011B293UL);
        return;

    default:                       /* H'75, H'76, H'7A to H'7C, H'7F, H'80 */
        return;
    }
}

/* H'248EEE. The module's own version text, drawn from the block at
 * H'104C90 that the link fills in. */
void module_version_text_draw(void)
{
    version_text_draw((const char *)0x00104C90UL);
}

/* H'230E2C. Screen H'21's press: nothing but the way out.
 *
 * The first call draws the version; after that the only thing the screen
 * answers to is the panel asking for H'77, which takes it to H'1F.
 */
u8 module_version_press(u8 fresh)
{
    u16 out = 0;

    if (fresh != 0) module_version_text_draw();

    if (screen_leave_check(&out, 0x00) != 0x03) return 0x00;
    if (out == KEY_CLR) screen_switch(0x1F, 0x01, 0x00);

    return 0x00;
}

/* ---- the module's own busy test ---------------------------------------
 * H'24610A and what it leans on. H'114D8E says which of twelve things the
 * module is in the middle of, and each of the twelve has its own list of
 * bytes that mean "not yet".
 */

/* H'248614. The link brought up, once and once only: H'114DA2 remembers
 * that it has been. A machine that is downloading -- bit 0 of H'FFFEC4 --
 * is left alone, and so is one that has been here before. */
void module_link_wake(void)
{
    u8 n;

    if (REG8(0x00114DA2UL) != 0) return;
    REG8(0x00114DA2UL) = 0x01;

    if (REG8(0x00FFFEC4UL) & 0x01) return;

    for (n = 0; n < 3; n++) {
        sci0_module_init();
        link_delay(0x0032);
    }

    REG8(0x0011F2A1UL) = 0x04;
    REG8(0x0011F2A2UL) = 0x06;
    link_send_start();
}

/* H'21BF2C, H'21C000, H'21C070 and H'21C0E0. Four little lamps in a row at
 * y H'22 to H'24, one for each of four bits of the module's switch byte.
 *
 * The first has two squares and lights whichever of them the bit picks; the
 * other three have one square each, filled with colour three when the bit
 * is up and blacked out when it is down.
 */
static void module_lamp_box(u16 x, u8 colour)
{
    draw_rect(x, 0x0022, (u16)(x + 0x0002), 0x0024, LCD_FRAME_A, colour, 0x01);
}

void module_lamp_pair(u8 on)
{
    if (on != 0) {
        module_lamp_box(0x000D, 0x00);
        module_lamp_box(0x001A, 0x03);
    } else {
        module_lamp_box(0x000D, 0x03);
        module_lamp_box(0x001A, 0x00);
    }
}

void module_lamp_b(u8 on)
{
    module_lamp_box(0x0042, (u8)(on != 0 ? 0x03 : 0x00));
}

void module_lamp_c(u8 on)
{
    module_lamp_box(0x0070, (u8)(on != 0 ? 0x03 : 0x00));
}

void module_lamp_d(u8 on)
{
    module_lamp_box(0x009C, (u8)(on != 0 ? 0x03 : 0x00));
}

/* H'2486C4. The module asked what its switches are set to, and the four
 * lamps drawn from the answer.
 *
 * H'114DA3 alternates: one visit turns the reporting on -- message H'03,
 * and five seconds given for the answer -- and the next turns it off again
 * with message H'0E. Either way the lamps are drawn from H'114D51 as it
 * stands, which on the first visit is what the five seconds fetched.
 */
void module_switches_show(void)
{
    module_link_wake();
    if (link_wait_idle() == 0) return;

    if (REG8(0x00114DA3UL) != 0) {
        REG8(0x00114DA3UL) = 0x00;
        REG8(0x0011F2A1UL) = 0x0E;
        REG8(0x0011F2A2UL) |= 0x01;
        link_send_start();
    } else {
        REG8(0x00114DA3UL) = 0x01;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x10;
        link_send_start();
        link_delay(0x1388);
    }

    module_lamp_pair((u8)((REG8(0x00114D51UL) & 0x01) == 0x01 ? 0x01 : 0x00));
    module_lamp_b((u8)((REG8(0x00114D51UL) & 0x10) == 0x10 ? 0x01 : 0x00));
    module_lamp_c((u8)((REG8(0x00114D51UL) & 0x04) == 0x04 ? 0x01 : 0x00));
    module_lamp_d((u8)((REG8(0x00114D51UL) & 0x08) == 0x08 ? 0x01 : 0x00));
}

/* H'24610A. Whether the module is still busy with what H'114D8E says it is
 * doing. Twelve states through a jump table, each its own list of "not
 * yet"; anything past the twelfth is not busy.
 *
 * Three of the states clear a byte or two on the way past and one of them
 * -- the state that shows the switches -- does a piece of work of its own,
 * which is why this is not the pure question it looks like.
 */
u8 module_busy(void)
{
    u8 busy = 0x00;

    if (REG8(0x00114D8EUL) > 0x0B) return 0x00;

    switch (REG8(0x00114D8EUL)) {
    case 0x00:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        break;

    case 0x01:
        if (REG8(0x00114D50UL) & 0x20)       busy = 0x01;
        if (!(REG8(0x00114D51UL) & 0x80))    busy = 0x01;
        REG8(0x00114D8BUL) = 0x00;
        REG8(0x00114D8CUL) = 0x00;
        REG8(0x00114D9BUL) = 0x01;
        REG8(0x0011F4E6UL) = 0x00;
        break;

    case 0x02:
    case 0x03:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        if (REG8(0x00114DB9UL) != 0)   busy = 0x01;
        if (REG8(0x00114D87UL) != 0)   busy = 0x01;
        if (REG8(0x00114D7EUL) != 0)   busy = 0x01;
        break;

    case 0x04:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        REG8(0x00114D9AUL) = 0x00;
        REG8(0x00114D99UL) = 0x00;
        if (REG8(0x00114D72UL) != 0)   busy = 0x01;
        if (REG8(0x00114D4FUL) & 0x01) busy = 0x01;
        if (REG8(0x00114D4FUL) & 0x02) busy = 0x01;
        if (REG8(0x00114DB9UL) != 0)   busy = 0x01;
        if (REG8(0x00FFFEC6UL) == 0 && module_link_quiet() == 0) busy = 0x01;
        if (REG8(0x00114D62UL) < 0x0A) busy = 0x01;
        break;

    case 0x05:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        REG8(0x00114D9AUL) = 0x00;
        REG8(0x00114D99UL) = 0x00;
        if (REG8(0x00114DB9UL) != 0)   busy = 0x01;
        break;

    case 0x06:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        REG8(0x00114D99UL) = 0x00;
        if (REG8(0x00114DB9UL) != 0)   busy = 0x01;
        if (REG8(0x00114D7FUL) != 0)   busy = 0x01;
        break;

    case 0x07:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        if (REG8(0x00114D72UL) != 0)   busy = 0x01;
        if (REG8(0x001040B4UL) != 0)   busy = 0x01;
        if (REG8(0x00114DB9UL) != 0)   busy = 0x01;
        if (module_link_quiet() == 0)  busy = 0x01;
        break;

    case 0x08:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        if (REG8(0x00114DB9UL) != 0)   busy = 0x01;
        if (REG8(0x001040B8UL) != 0)   busy = 0x01;
        if (REG8(0x0011A63DUL) != 0)   busy = 0x01;
        if (REG8(0x00114D72UL) != 0)   busy = 0x01;
        if (REG16(0x0011A63AUL) != 0)  busy = 0x01;
        break;

    case 0x09:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        if (REG8(0x001040B8UL) != 0)   busy = 0x01;
        if (REG8(0x00114D72UL) != 0)   busy = 0x01;
        if (REG8(0x0011A63DUL) != 0)   busy = 0x01;
        if (REG8(0x00114DB9UL) != 0)   busy = 0x01;
        if (REG16(0x0011A63AUL) != 0)  busy = 0x01;
        break;

    case 0x0A:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        if (REG8(0x00114DA3UL) != 0) {
            module_switches_show();
            busy = 0x01;
        }
        break;

    default:
        if (REG8(0x00114D50UL) & 0x20) busy = 0x01;
        break;
    }

    return (u8)(busy != 0 ? 0x01 : 0x00);
}

/* H'249DCA. Whether the module screen may be left: anything but H'0D is
 * free to go, and H'0D only while the switches are not being reported. */
u8 module_screen_free(void)
{
    if (REG8(0x00FFFEC5UL) != 0x0D) return 0x01;
    if (REG8(0x00114DA3UL) == 0)    return 0x01;
    return 0x00;
}

/* H'248F02. The module told to stop reporting its switches. */
void module_switches_stop(void)
{
    module_link_wake();
    if (link_wait_idle() == 0) return;

    REG8(0x00114DA3UL) = 0x00;
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61AUL) = 0x14;
    link_send_start();
}

/* H'248780 and H'248882. The module's switch settings fetched and shown
 * twice over: the four lamps drawn from what H'114D51 holds now, then the
 * module asked to report -- message H'12 for the first, H'13 for the
 * second -- the link waited out, and the four lamps drawn again from the
 * answer.
 *
 * The two are the same routine with one number changed. Both are written
 * out in full in the ROM and both are reproduced.
 */
static void module_lamps_from_byte(void)
{
    module_lamp_pair((u8)((REG8(0x00114D51UL) & 0x01) == 0x01 ? 0x01 : 0x00));
    module_lamp_b((u8)((REG8(0x00114D51UL) & 0x10) == 0x10 ? 0x01 : 0x00));
    module_lamp_c((u8)((REG8(0x00114D51UL) & 0x04) == 0x04 ? 0x01 : 0x00));
    module_lamp_d((u8)((REG8(0x00114D51UL) & 0x08) == 0x08 ? 0x01 : 0x00));
}

static void module_switches_ask(u8 message)
{
    module_link_wake();
    if (link_wait_idle() == 0) return;

    REG8(0x00114DA3UL) = 0x00;
    module_lamps_from_byte();

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61AUL) = message;
    link_send_start();

    while ((REG8(0x00114D50UL) & 0x21) != 0 ||
           REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
        rom_host_service();
    }

    module_lamps_from_byte();
}

void module_switches_ask_12(void) { module_switches_ask(0x12); }
void module_switches_ask_13(void) { module_switches_ask(0x13); }

/* H'248984. The switch reporting turned off, or -- when it was off already
 * -- the module told to stop and the link waited out. */
void module_switches_toggle(void)
{
    module_link_wake();
    if (link_wait_idle() == 0) return;

    if (REG8(0x00114DA3UL) != 0) {
        REG8(0x00114DA3UL) = 0x00;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x14;
        link_send_start();
        return;
    }

    REG8(0x0011F2A1UL) = 0x04;
    REG8(0x0011F2A2UL) = 0x0D;
    link_send_start();

    while ((REG8(0x00114D50UL) & 0x22) != 0 ||
           REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
        rom_host_service();
    }
}

/* H'2489F2. The hoop sent home: the offsets forgotten, the module told to
 * take the frame off and then to find its own zero, with a second of grace
 * after each. Answers 1 only if the link stayed quiet the whole way. */
u8 module_hoop_home(void)
{
    module_link_wake();
    if (link_wait_idle() == 0) return 0x00;

    if (REG8(0x00114DA3UL) != 0) {
        REG8(0x00114DA3UL) = 0x00;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x14;
        link_send_start();
        if (link_wait_idle() == 0) return 0x00;
    }

    REG8(0x00104C7AUL) = 0x00;
    REG8(0x00104C7BUL) = 0x00;
    REG8(0x0011F2A1UL) = 0x0D;
    REG8(0x0011F2A2UL) = 0x00;
    link_send_start();
    if (link_wait_idle() == 0) return 0x00;

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A612UL) = 0x01;
    link_send_start();
    link_delay(0x03E8);
    if (link_wait_idle() == 0) return 0x00;

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A615UL) = 0x09;
    REG8(0x0011F2A2UL) &= (u8)~0x08;
    link_send_start();
    link_delay(0x03E8);
    if (link_wait_idle() == 0) return 0x00;

    return 0x01;
}

/* H'248E5C. Leaving the hoop screen: the module's state put back to nothing,
 * the frame's two words for the current hoop zeroed, and the module told
 * about it. */
void module_hoop_leave(void)
{
    REG8(0x00114D8EUL) = 0x00;
    if (link_wait_idle() == 0) return;

    REG8(0x0011F2A1UL) = 0x0D;
    REG8(0x0011F2A2UL) = 0x01;
    link_send_start();
    link_delay(0x07D0);
    if (link_wait_idle() == 0) return;

    REG16(0x0011A266UL +
          (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4))) = 0;
    REG16(0x0011A268UL +
          (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4))) = 0;

    REG8(0x0011F2A1UL) = 0x02;
    link_send_start();
    (void)link_wait_idle();
}

/* H'248AC6. The eighth direction, which does not fit the row above: both
 * offsets down at once. */
void module_hoop_up_left(void)
{
    if (link_wait_idle() == 0) return;
    if (abs_short((short)(signed char)REG8(0x00104C7AUL)) > 0x0064) return;
    if (abs_short((short)(signed char)REG8(0x00104C7BUL)) > 0x0064) return;

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A615UL) = 0x08;
    REG8(0x0011F2A2UL) |= 0x08;
    link_send_start();

    REG8(0x00104C7AUL) = (u8)(REG8(0x00104C7AUL) - 1);
    REG8(0x00104C7BUL) = (u8)(REG8(0x00104C7BUL) - 1);
    hoop_offsets_draw();
}

/* H'248F32. The hoop taken back to its own zero, in three steps with a
 * picture drawn for each: the offsets forgotten and the frame taken off,
 * the frame put back, and the module sent home. */
void module_hoop_reset(void)
{
    module_link_wake();
    if (link_wait_idle() == 0) return;

    REG8(0x00104C7AUL) = 0x00;
    REG8(0x00104C7BUL) = 0x00;
    REG8(0x0011F2A1UL) = 0x0D;
    REG8(0x0011F2A2UL) = 0x00;
    link_send_start();
    module_step_picture(0x01, 0x01);
    if (link_wait_idle() == 0) return;

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A612UL) = 0x01;
    link_send_start();
    module_step_picture(0x02, 0x01);
    link_delay(0x03E8);
    if (link_wait_idle() == 0) return;

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A615UL) = 0x09;
    REG8(0x0011F2A2UL) |= 0x08;
    link_send_start();
    module_step_picture(0x03, 0x01);
    link_delay(0x03E8);
    if (link_wait_idle() == 0) return;

    hoop_offsets_draw();
}

/* H'230AF4. Screen H'4A's press: the hoop moved by hand.
 *
 * Eleven boxes -- the reset, the eight directions, and two ways out -- and
 * the module asked whether it is busy before any of them is acted on.
 * H'11F29C remembers what the module was doing so that leaving can put it
 * back.
 */
u8 hoop_nudge_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x000B, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (REG8(0x00114D8EUL) != 0x0B) REG8(0x0011F29CUL) = REG8(0x00114D8EUL);
    REG8(0x00114D8EUL) = 0x0B;

    if (module_busy() != 0) return 0x00;
    if ((u16)(value - 1) > 0x000A) return 0x00;

    switch (value) {
    case 0x0001: module_hoop_reset();      break;
    case 0x0002: module_hoop_up_left();    break;
    case 0x0003: module_hoop_up();         break;
    case 0x0004: module_hoop_up_right();   break;
    case 0x0005: module_hoop_left();       break;
    case 0x0006: module_hoop_right();      break;
    case 0x0007: module_hoop_down_left();  break;
    case 0x0008: module_hoop_down();       break;
    case 0x0009: module_hoop_down_right(); break;

    case 0x000A:
        screen_stack_pop();
        module_hoop_leave();
        REG8(0x00114D8EUL) = REG8(0x0011F29CUL);
        screen_switch(0x4A, 0x01, 0x00);
        break;

    default:
        screen_stack_pop();
        REG8(0x00114D8EUL) = REG8(0x0011F29CUL);
        screen_switch(0x4A, 0x01, 0x00);
        break;
    }

    return 0x00;
}

/* H'230C42. Screen H'1F's press: the module's own settings.
 *
 * The way out is asked for first, and taking it depends on whether the
 * switches are being reported -- if they are, the press only turns the
 * reporting off. Five boxes after that, and again nothing happens while the
 * module says it is busy.
 */
u8 module_settings_screen(void)
{
    u16 value = 0, index = 0;
    u16 to = 0;

    if (screen_leave_check(&to, 0x00) == 0x03 && to == KEY_CLR) {
        if (module_screen_free() != 0) {
            module_switches_stop();
            screen_switch(0x17, 0x01, 0x00);
            REG8(0x00FFFEC5UL) = 0x00;
            return 0x00;
        }
        module_switches_show();
    }

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    REG8(0x00114D8EUL) = 0x0A;
    if (module_busy() != 0) return 0x00;

    if (value == 0x0001) {
        module_switches_show();
        return 0x00;
    }
    if (value == 0x0002) {
        if (module_hoop_home() == 0) return 0x00;
        screen_switch(0x20, 0x01, 0x00);
        return 0x00;
    }
    if (value == 0x0003) {
        module_switches_ask_12();
        return 0x01;
    }
    if (value == 0x0004) {
        module_switches_ask_13();
        return 0x01;
    }
    if (value == 0x0005) {
        screen_switch(0x21, 0x01, 0x00);
        module_switches_toggle();
        return 0x01;
    }
    return 0x00;
}

/* ---- the module's pattern list -----------------------------------------
 * Screen H'38 and the seven routines under it: a page of five-by-three
 * thumbnails to pick from, and a strip along the bottom holding the ones
 * picked, in order, ready to be sent to the module.
 *
 * The thumbnails are H'23 by H'23 one-bit bitmaps, H'AF bytes each -- five
 * bytes to a row, thirty-five rows -- kept in RAM from H'104D4A on, one per
 * pattern of the design the slot names. H'0FFE9C plus the design's own
 * number says how many there are.
 *
 * The strip is a window H'2F to H'EA across and H'9E to H'C1 down, scrolled
 * a pixel at a time by reading and writing the front buffer. H'104038 is how
 * far along it the cursor has got, H'104039 which entry the cursor is on and
 * H'104044 how many entries there are. The entries themselves are two byte
 * arrays: H'104046 the pattern numbers and H'10405B the widths, both indexed
 * from H'01.
 */
#define MOD_STRIP_X0    0x002F
#define MOD_STRIP_X1    0x00EA
#define MOD_STRIP_Y0    0x009E
#define MOD_STRIP_Y1    0x00C1
#define MOD_LINE_TOP    0x00A1
#define MOD_LINE_BASE   0x00BE
#define MOD_STRIP_SPAN  0x00BB
#define MOD_BITMAPS     0x00104D4AUL
#define MOD_BITMAP_LEN  0x00AF
#define MOD_AT          REG8(0x00104038UL)   /* how far along the strip */
#define MOD_POS         REG8(0x00104039UL)   /* which entry the cursor is on */
#define MOD_COUNT       REG8(0x00104044UL)   /* how many entries */
#define MOD_NUMBERS     0x00104045UL         /* +1 for entry one */
#define MOD_WIDTHS      0x0010405AUL
#define MOD_PAGE        REG8(0x00114DB8UL)   /* the first thumbnail shown */
#define MOD_SAVE        0x000F1610UL

/* How many patterns the design in the current slot has. */
static u8 module_thumb_total(void)
{
    const u32 e = (u32)(long)(short)(u16)(0x0012 * (u16)REG8(0x0011A660UL));
    const u8  k = REG8(0x0011A41AUL + e);

    return REG8(0x000FFE9CUL + (u32)k);
}

/* H'24A21A. A rectangle of the front buffer put back to black. */
void module_box_clear(u16 x0, u16 y0, u16 x1, u16 y1)
{
    draw_rect(x0, y0, x1, y1, LCD_FRAME_A, 0x00, 0x01);
}

/* H'249B86, H'249BD2, H'249C6A and H'249C1E. The four arrows, each with a
 * picture for lit and one for not. The first two page the thumbnails and the
 * other two walk the strip. */
void module_page_arrow_back(u8 on)
{
    if (on != 0) hitbox_blit(0x0010, LCD_FRAME_A, 0x0034E46CUL);
    else         hitbox_blit(0x0010, LCD_FRAME_A, 0x0034E4A8UL);
}

void module_page_arrow_on(u8 on)
{
    if (on != 0) hitbox_blit(0x0011, LCD_FRAME_A, 0x0034E4E4UL);
    else         hitbox_blit(0x0011, LCD_FRAME_A, 0x0034E520UL);
}

void module_list_arrow_on(u8 on)
{
    if (on != 0) hitbox_blit(0x0013, LCD_FRAME_A, ARROW_ON_ON);
    else         hitbox_blit(0x0013, LCD_FRAME_A, ARROW_ON_OFF);
}

void module_list_arrow_back(u8 on)
{
    if (on != 0) hitbox_blit(0x0012, LCD_FRAME_A, ARROW_BACK_ON);
    else         hitbox_blit(0x0012, LCD_FRAME_A, ARROW_BACK_OFF);
}

/* H'23A336. One row of the picking grid: five cells of H'24 pixels, each
 * blacked and then given the bitmap of the pattern whose number begins at
 * [first]. [count] says how many of the five carry a pattern; the rest are
 * left black.
 *
 * The bitmap's first row is not picture: it is one set bit at the column the
 * pattern is wide. Finding it fixes two things for the rest of the cell --
 * the columns after it are not drawn, and half the distance from it to H'22
 * becomes the offset every pixel is drawn at, which is what centres a narrow
 * pattern in its cell.
 */
void module_thumb_row_draw(u8 row, u16 first, u8 count)
{
    const u32 base = MOD_BITMAPS + (u32)(u16)((u16)first * MOD_BITMAP_LEN);
    const u16 cy = (u16)((u16)(0x0027 * (u16)row) + 0x002A);
    u16 col;

    for (col = 0; col < 0x0005; col++) {
        const u16 cx = (u16)((u16)(col * 0x0027) + 0x0006);
        u16 limit = 0x0023;
        u16 shift = 0x0005;
        u8  first_bit = 0x01;
        u16 py;

        module_box_clear(cx, cy, (u16)(cx + 0x0023), (u16)(cy + 0x0023));

        if ((u16)count <= col) continue;

        for (py = 0; py < 0x0023; py++) {
            u16 bi;
            u16 px = 0;

            for (bi = 0; bi < 0x0005; bi++) {
                const u8 byte = REG8(base + (u32)(u16)(
                                    (u16)(0x0005 * py) +
                                    (u16)(col * MOD_BITMAP_LEN) + bi));
                u8  mask = 0x80;
                u16 k;

                for (k = 0; k < 0x0008; k++) {
                    if (px > limit) goto next_py;
                    if ((u8)(byte & mask) == mask) {
                        if (first_bit != 0) {
                            limit = (u16)(px - 1);
                            shift = (u16)((u16)(0x0022 - px) >> 1);
                            first_bit = 0x00;
                        } else {
                            plot_pixel((u16)((u16)(cx + px) + shift),
                                       (u16)(cy + py), LCD_FRAME_A, 0x03);
                        }
                    }
                    mask = (u8)(mask >> 1);
                    px = (u16)(px + 1);
                }
            }
        next_py:
            ;
        }
    }
}

/* H'23ABC2. One bitmap drawn where it is asked for, read the same way.
 *
 * [wide] is what tells the preview at the top of the screen from the strip
 * along the bottom. Wide blacks the whole H'24 cell, writes the width it
 * found into H'104036 for the caller to insert with, and draws the pattern
 * centred; narrow blacks only as far as the pattern is wide and draws it
 * against the left edge.
 */
void module_thumb_draw(u16 x, u16 y, u16 wide, const u8 *bits)
{
    u16 limit = 0x0023;
    u16 shift = 0x0005;
    u8  first_bit = 0x01;
    u16 py;

    for (py = 0; py < 0x0023; py++) {
        u16 bi;
        u16 px = 0;

        for (bi = 0; bi < 0x0005; bi++) {
            const u8 byte = bits[(u16)((u16)(0x0005 * py) + bi)];
            u8  mask = 0x80;
            u16 k;

            for (k = 0; k < 0x0008; k++) {
                if (limit <= px) goto next_py;
                if ((u8)(byte & mask) == mask) {
                    if (first_bit != 0) {
                        limit = (u16)(px - 1);
                        shift = (u16)((u16)(0x0022 - px) >> 1);
                        first_bit = 0x00;
                        if (wide != 0) {
                            module_box_clear(x, (u16)(y + 0x0003),
                                             (u16)(x + 0x0023),
                                             (u16)(y + 0x0020));
                            REG8(0x00104036UL) = (u8)(px - 1);
                        } else {
                            module_box_clear(x, (u16)(y + 0x0003),
                                             (u16)((u16)(px + x) - 1),
                                             (u16)(y + 0x0020));
                        }
                    } else if (wide != 0) {
                        plot_pixel((u16)((u16)(x + px) + (u16)(u8)shift),
                                   (u16)(y + py), LCD_FRAME_A, 0x03);
                    } else {
                        plot_pixel((u16)(x + px), (u16)(y + py),
                                   LCD_FRAME_A, 0x03);
                    }
                }
                mask = (u8)(mask >> 1);
                px = (u16)(px + 1);
            }
        }
    next_py:
        ;
    }
}

/* H'23AFEE and H'23B0C6. The strip shifted sideways by [n] pixels, read and
 * written back a pixel at a time. [right] says which way.
 *
 * The two differ in one place only: shifting right, the first takes the left
 * edge as the constant H'2F and the second takes it as H'2F plus how far
 * along the strip the cursor has got. */
void module_strip_scroll(u8 n, u8 right)
{
    u8 x0 = (u8)MOD_STRIP_X0;
    u8 x1 = (u8)MOD_STRIP_X1;
    const u8 y0 = (u8)MOD_STRIP_Y0;
    const u8 y1 = (u8)MOD_STRIP_Y1;
    u8 at, y;

    if (right == 0) {
        for (at = (u8)(x0 + n); at <= x1; at++, x0++) {
            for (y = y0; y <= y1; y++) {
                plot_pixel(x0, y, LCD_FRAME_A,
                           read_pixel(at, y, LCD_FRAME_A));
            }
        }
    } else {
        for (at = (u8)(x1 - n); at >= x0; at--, x1--) {
            for (y = y0; y <= y1; y++) {
                plot_pixel(x1, y, LCD_FRAME_A,
                           read_pixel(at, y, LCD_FRAME_A));
            }
        }
    }
}

void module_strip_scroll_at(u8 n, u8 right)
{
    u8 x0;
    u8 x1 = (u8)MOD_STRIP_X1;
    const u8 y0 = (u8)MOD_STRIP_Y0;
    const u8 y1 = (u8)MOD_STRIP_Y1;
    u8 at, y;

    if (right == 0) {
        x0 = (u8)MOD_STRIP_X0;
        for (at = (u8)(x0 + n); at <= x1; at++, x0++) {
            for (y = y0; y <= y1; y++) {
                plot_pixel(x0, y, LCD_FRAME_A,
                           read_pixel(at, y, LCD_FRAME_A));
            }
        }
    } else {
        x0 = (u8)(MOD_AT + MOD_STRIP_X0);
        for (at = (u8)(x1 - n); at >= x0; at--, x1--) {
            for (y = y0; y <= y1; y++) {
                plot_pixel(x1, y, LCD_FRAME_A,
                           read_pixel(at, y, LCD_FRAME_A));
            }
        }
    }
}

/* H'23B81C. The line under the strip that says where the next pattern will
 * go. Moving it takes it away from where it was; standing still it blinks,
 * off at H'64 of its own counter and on again at H'C8.
 *
 * [on] of nought does not draw at all: it only forgets where the line was,
 * so that the next call puts one down rather than taking one away. */
void module_cursor_line(u8 on)
{
    u16 tick;

    if (on == 0) { REG8(0x0011F564UL) = 0x00; return; }
    if (REG8(0x00114D99UL) != 0) return;

    if (REG8(0x0011F564UL) != MOD_AT) {
        const u16 x = (u16)((u16)REG8(0x0011F564UL) + MOD_STRIP_X0);

        draw_line(x, MOD_LINE_TOP, x, MOD_LINE_BASE, LCD_FRAME_A, 0x00);
        REG8(0x0011F564UL) = MOD_AT;
        return;
    }

    tick = REG16(0x0011F562UL);
    if (tick == 0x0064 || tick == 0x00C8) {
        const u16 x = (u16)(u8)(MOD_AT + MOD_STRIP_X0);

        if (tick == 0x0064) {
            draw_line(x, MOD_LINE_TOP, x, MOD_LINE_BASE, LCD_FRAME_A, 0x00);
        }
        if (REG16(0x0011F562UL) == 0x00C8) {
            draw_line(x, MOD_LINE_TOP, x, MOD_LINE_BASE, LCD_FRAME_A, 0x03);
        }
    }

    tick = REG16(0x0011F562UL);
    if ((short)tick > 0x00C8) { tick = 0; REG16(0x0011F562UL) = 0; }
    REG16(0x0011F562UL) = (u16)(tick + 1);
}

/* H'23B408. One pattern put into the list at the cursor. Past the end it is
 * appended; anywhere else everything above it is moved up one first. */
void module_list_insert(u8 width, u16 number)
{
    const u8 num = (u8)number;

    if (MOD_POS == MOD_COUNT) {
        REG8(MOD_WIDTHS + 0x01 + (u32)MOD_POS) = width;
        REG8(MOD_NUMBERS + 0x01 + (u32)MOD_POS) = num;
    } else {
        u8 k = MOD_COUNT;

        while (k > MOD_POS) {
            REG8(MOD_NUMBERS + 0x01 + (u32)k) = REG8(MOD_NUMBERS + (u32)k);
            REG8(MOD_WIDTHS + 0x01 + (u32)k) = REG8(MOD_WIDTHS + (u32)k);
            k--;
        }
        REG8(MOD_NUMBERS + 0x01 + (u32)MOD_POS) = num;
        REG8(MOD_WIDTHS + 0x01 + (u32)MOD_POS) = width;
    }
    MOD_POS = (u8)(MOD_POS + 1);
    MOD_COUNT = (u8)(MOD_COUNT + 1);
}

/* H'23B4DE. One pattern taken out of the list at [from], and the strip
 * closed up behind it: everything to the right of the cursor moved [n]
 * pixels left, the tail that is left over blacked, and the patterns that
 * have come into view at the right-hand end drawn again. */
void module_list_remove_draw(u8 n, u8 from)
{
    const u8 top = (u8)MOD_LINE_TOP;
    const u8 right = (u8)MOD_STRIP_X1;
    const u8 base = (u8)MOD_LINE_BASE;
    u8 x = (u8)(MOD_AT + MOD_STRIP_X0);
    u8 k = from;
    u8 edge, at, y;

    while (k < MOD_COUNT) {
        REG8(MOD_NUMBERS + 0x01 + (u32)k) = REG8(MOD_NUMBERS + 0x02 + (u32)k);
        REG8(MOD_WIDTHS + 0x01 + (u32)k) = REG8(MOD_WIDTHS + 0x02 + (u32)k);
        k++;
    }
    MOD_COUNT = (u8)(MOD_COUNT - 1);
    MOD_POS = (u8)(MOD_POS - 1);

    if (n == 0) return;

    edge = (u8)(right - n);

    for (at = (u8)(n + x); at <= right; at++, x++) {
        for (y = top; y <= base; y++) {
            plot_pixel(x, y, LCD_FRAME_A, read_pixel(at, y, LCD_FRAME_A));
        }
    }

    for (at = edge; at <= right; at++) {
        for (y = top; y <= base; y++) {
            plot_pixel(at, y, LCD_FRAME_A, 0x00);
        }
    }

    x = (u8)(MOD_AT + MOD_STRIP_X0);
    for (k = from; k < MOD_COUNT; k++) {
        const u8 w = REG8(MOD_WIDTHS + 0x01 + (u32)k);
        const u16 num = (u16)((u16)REG8(MOD_NUMBERS + 0x01 + (u32)k) - 1);

        x = (u8)(x + w);
        if (x < edge) continue;
        if (x > right) break;

        module_thumb_draw((u16)((u16)x - (u16)w), (u16)(top - 0x03), 0x0000,
                          (const u8 *)(MOD_BITMAPS +
                              (u32)(long)(short)(u16)(num * MOD_BITMAP_LEN)));
    }
}

/* H'23A990 and H'23AA8A. The grid paged back and on by a row of five. Both
 * scroll the grid itself with a region copy and then draw the one row that
 * has come into view, and both put the two arrows in the state the new
 * position calls for. */
void module_thumb_page_back(void)
{
    u16 left;
    short room;

    if (MOD_PAGE == 0) return;
    MOD_PAGE = (u8)(MOD_PAGE - 5);

    region_copy(0x0006, 0x002A, 0x00C5, 0x0074, 0x0051,
                LCD_FRAME_A, LCD_FRAME_A);

    left = (u16)((u16)module_thumb_total() - (u16)MOD_PAGE);
    room = (short)left;
    if (room > 0x0005) room = 0x0005;
    if (room >= 0) module_thumb_row_draw(0x00, (u16)MOD_PAGE, (u8)room);

    if (MOD_PAGE != 0) module_page_arrow_back(0x01);
    else               module_page_arrow_back(0x00);

    if ((short)((u16)module_thumb_total() - (u16)MOD_PAGE) > 0x000F) {
        module_page_arrow_on(0x01);
    } else {
        module_page_arrow_on(0x00);
    }
}

void module_thumb_page_on(void)
{
    u16 left;
    short room;

    if ((short)((u16)module_thumb_total() - (u16)MOD_PAGE) <= 0x000F) return;
    MOD_PAGE = (u8)(MOD_PAGE + 5);

    region_copy(0x0006, 0x0051, 0x00C5, 0x009B, 0x002A,
                LCD_FRAME_A, LCD_FRAME_A);

    left = (u16)((u16)module_thumb_total() - (u16)((u16)MOD_PAGE + 0x000A));
    room = (short)left;
    if (room > 0x0005) room = 0x0005;
    if (room >= 0) {
        module_thumb_row_draw(0x02, (u16)((u16)MOD_PAGE + 0x000A), (u8)room);
    }

    if (MOD_PAGE != 0) module_page_arrow_back(0x01);
    else               module_page_arrow_back(0x00);

    if ((short)((u16)module_thumb_total() - (u16)MOD_PAGE) > 0x000F) {
        module_page_arrow_on(0x01);
    } else {
        module_page_arrow_on(0x00);
    }
}

/* The two arrows beside the strip, put in the state the cursor's position
 * calls for. Both of the routines that walk the strip end this way. */
static void module_list_arrows(void)
{
    if (MOD_POS != 0) module_list_arrow_back(0x01);
    else              module_list_arrow_back(0x00);

    if (MOD_POS >= MOD_COUNT) module_list_arrow_on(0x00);
    else                      module_list_arrow_on(0x01);
}

/* H'23B1A2 and H'23B2CE. The cursor one entry back and one entry on.
 *
 * Both move the cursor first and then decide whether the strip has to move
 * with it: while the entry fits in what is already showing, only H'104038
 * changes, and when it does not the strip is scrolled by as much as is
 * needed and the entry that has come into view is drawn.
 */
void module_list_back(void)
{
    u16 width, over, num;

    if (MOD_POS == 0) return;

    width = (u16)REG8(MOD_WIDTHS + (u32)MOD_POS);
    REG16(0x0010403AUL) = (u16)(REG16(0x0010403AUL) - width);
    num = (u16)((u16)REG8(MOD_NUMBERS + (u32)MOD_POS) - 1);
    MOD_POS = (u8)(MOD_POS - 1);

    module_list_arrows();

    over = (u16)(width - (u16)MOD_AT);
    if ((short)over <= 0) {
        MOD_AT = (u8)(MOD_AT - (u8)width);
        module_cursor_line(0x01);
        return;
    }

    MOD_AT = 0x00;
    module_cursor_line(0x01);

    if ((short)over > (short)width) over = width;
    REG16(0x0010403EUL) = (u16)(REG16(0x0010403EUL) - over);
    module_strip_scroll((u8)over, 0x01);
    module_thumb_draw((u16)((u16)MOD_AT + MOD_STRIP_X0), MOD_STRIP_Y0, 0x0000,
                      (const u8 *)(MOD_BITMAPS +
                          (u32)(long)(short)(u16)(num * MOD_BITMAP_LEN)));
}

void module_list_forward(void)
{
    u16 width, over, num;

    if (MOD_POS >= MOD_COUNT) return;

    width = (u16)REG8(MOD_WIDTHS + 0x01 + (u32)MOD_POS);
    REG16(0x0010403AUL) = (u16)(REG16(0x0010403AUL) + width);
    num = (u16)((u16)REG8(MOD_NUMBERS + 0x01 + (u32)MOD_POS) - 1);
    MOD_POS = (u8)(MOD_POS + 1);

    module_list_arrows();

    over = (u16)((u16)(width + (u16)MOD_AT) - MOD_STRIP_SPAN);
    if ((short)over <= 0) {
        MOD_AT = (u8)(MOD_AT + (u8)width);
        module_cursor_line(0x01);
        return;
    }

    MOD_AT = (u8)MOD_STRIP_SPAN;
    module_cursor_line(0x01);

    if ((short)over > (short)width) over = width;
    REG16(0x0010403EUL) = (u16)(REG16(0x0010403EUL) + over);
    module_strip_scroll((u8)over, 0x00);
    module_thumb_draw((u16)((u16)((u16)MOD_AT - width) + MOD_STRIP_X0),
                      MOD_STRIP_Y0, 0x0000,
                      (const u8 *)(MOD_BITMAPS +
                          (u32)(long)(short)(u16)(num * MOD_BITMAP_LEN)));
}

/* H'23B67A. The entry the cursor is on taken out.
 *
 * The last entry of all is the easy one: the cursor comes back by its width
 * and the space it filled is blacked. Anywhere else the strip has to close
 * up behind it, which is what H'23B4DE is for. Either way, when the entry is
 * wider than what is showing to the left of the cursor the strip is put back
 * to its start first.
 */
void module_list_delete(void)
{
    u16 width, over;

    if (MOD_POS == 0) return;

    width = (u16)REG8(MOD_WIDTHS + (u32)MOD_POS);
    REG16(0x0010403AUL) = (u16)(REG16(0x0010403AUL) - width);
    over = (u16)(width - (u16)MOD_AT);

    if (MOD_COUNT == MOD_POS) {
        u8 x;

        MOD_COUNT = (u8)(MOD_COUNT - 1);
        MOD_POS = (u8)(MOD_POS - 1);

        if ((short)over <= 0) {
            MOD_AT = (u8)(MOD_AT - (u8)width);
            module_cursor_line(0x01);
        } else {
            MOD_AT = 0x00;
            module_cursor_line(0x01);
            REG16(0x0010403EUL) = (u16)(REG16(0x0010403EUL) - over);
        }
        x = (u8)(MOD_AT + MOD_STRIP_X0);
        module_box_clear((u16)x, MOD_LINE_TOP, (u16)(u8)(x + (u8)width),
                         MOD_LINE_BASE);
    } else if ((short)over <= 0) {
        MOD_AT = (u8)(MOD_AT - (u8)width);
        module_cursor_line(0x01);
        module_list_remove_draw((u8)width, (u8)(MOD_POS - 1));
    } else {
        if (over == width) over = 0x0000;
        else               over = (u16)MOD_AT;
        MOD_AT = 0x00;
        module_cursor_line(0x01);
        REG16(0x0010403EUL) = (u16)(REG16(0x0010403EUL) - over);
        module_list_remove_draw((u8)over, (u8)(MOD_POS - 1));
    }

    module_list_arrows();
}

/* H'23AD2A. One of the fifteen thumbnails pressed: the pattern it stands for
 * added to the list at the cursor.
 *
 * Twenty is the most the list holds, and asking for a twenty-first only
 * claims the link for message H'17. The first one added clears the two
 * counters at H'11F55E.
 *
 * The picture is drawn twice: once wide, into the preview at the top -- which
 * is what leaves the pattern's width in H'104036 for the insert to carry --
 * and once narrow, into the strip. Four ways to reach that second drawing,
 * turning on whether the cursor is at the end of the list and on whether the
 * new pattern still fits in what is showing. Whichever way it goes, the
 * strip is then copied to H'0F1610, which is where the panel takes it from.
 */
void module_pattern_add(u8 box)
{
    u16 num, width;
    short over;
    const u8 *bits;
    u16 x;

    if (MOD_COUNT >= 0x14) { (void)link_claim(0x17); return; }
    if (MOD_COUNT == 0) {
        REG16(0x0011F55EUL) = 0x0000;
        REG16(0x0011F560UL) = 0x0000;
    }

    num = (u16)((u16)((u16)MOD_PAGE + (u16)box) - 1);
    if ((short)((u16)module_thumb_total() - 1) < (short)num) return;

    bits = (const u8 *)(MOD_BITMAPS +
               (u32)(long)(short)(u16)(num * MOD_BITMAP_LEN));

    module_thumb_draw(0x008A, 0x0003, 0x0001, bits);
    module_list_insert(REG8(0x00104036UL), (u16)(u8)(num + 1));

    width = (u16)REG8(MOD_WIDTHS + (u32)MOD_POS);
    REG16(0x0010403AUL) = (u16)(REG16(0x0010403AUL) + width);
    REG16(0x0010403CUL) = (u16)(REG16(0x0010403CUL) + width);
    over = (short)(u16)((u16)(width + (u16)MOD_AT) - MOD_STRIP_SPAN);

    if (MOD_COUNT == MOD_POS) {
        if (over <= 0) {
            x = (u16)((u16)MOD_AT + MOD_STRIP_X0);
            MOD_AT = (u8)(MOD_AT + (u8)width);
            module_cursor_line(0x01);
        } else {
            MOD_AT = (u8)MOD_STRIP_SPAN;
            module_cursor_line(0x01);
            x = (u16)((u16)((u16)MOD_AT - width) + MOD_STRIP_X0);
            if (over > (short)width) over = (short)width;
            REG16(0x0010403EUL) = (u16)(REG16(0x0010403EUL) + (u16)over);
            module_strip_scroll((u8)over, 0x00);
        }
    } else if (over <= 0) {
        module_strip_scroll_at((u8)width, 0x01);
        x = (u16)((u16)MOD_AT + MOD_STRIP_X0);
        MOD_AT = (u8)(MOD_AT + (u8)width);
        module_cursor_line(0x01);
    } else {
        MOD_AT = (u8)MOD_STRIP_SPAN;
        module_cursor_line(0x01);
        if (over > (short)width) over = (short)width;
        REG16(0x0010403EUL) = (u16)(REG16(0x0010403EUL) + (u16)over);
        module_strip_scroll_at((u8)over, 0x00);
        x = (u16)((u16)((u16)MOD_AT - width) + MOD_STRIP_X0);
    }

    module_thumb_draw(x, MOD_STRIP_Y0, 0x0000, bits);

    module_list_arrow_back(0x01);
    region_copy(MOD_STRIP_X0, MOD_STRIP_Y0, MOD_STRIP_X1, MOD_STRIP_Y1,
                MOD_STRIP_Y0, LCD_FRAME_A, MOD_SAVE);
}

/* H'231776, H'231794 and H'23180C. Three ways out of the module's screens,
 * each saying what the module is doing next before it goes. */
void module_go_stitch(void)
{
    REG8(0x00114D8EUL) = 0x03;
    screen_switch(0x14, 0x01, 0x00);
}

void module_go_menu(void)
{
    REG8(0x00114D8EUL) = 0x02;
    screen_switch(0x13, 0x01, 0x00);
}

void module_go_sewing(void)
{
    REG8(0x00114D8EUL) = 0x04;
    screen_switch(0x23, 0x01, 0x00);
}

/* H'23B938. The key that sends the list to the module, one step of it a
 * pass. H'104042 is which step; every step but the first waits for the link
 * to be quiet before it does anything and then counts on.
 *
 * Step nought is the one that decides: with nothing in the list it puts the
 * three counters of the current design back to nought and leaves for the
 * menu or the stitch screen, and with something in it, it goes to the sewing
 * screen and lets the rest of the steps run.
 */
void module_send_step(void)
{
    const u8 st = REG8(0x00104042UL);

    if (st == 0x00) {
        const u32 e = (u32)(long)(short)(u16)(
                          (u16)(REG8(0x0011A41AUL + (u32)(long)(short)(u16)(
                              0x0012 * (u16)REG8(0x0011A660UL))) << 1));

        REG8(0x00114D98UL) = 0x00;
        REG8(0x00104040UL) = 0x00;
        REG16(0x00104CCEUL + e) = 0x0000;
        REG16(0x00104D06UL + e) = 0x0000;
        REG16(0x00104C96UL + e) = 0x0000;

        if (MOD_COUNT != 0) {
            module_go_sewing();
            REG8(0x00114D8DUL) = MOD_COUNT;
            REG8(0x00104041UL) = 0xFF;
            REG8(0x00104042UL) = (u8)(REG8(0x00104042UL) + 1);
            return;
        }

        REG8(0x00104042UL) = 0x00;
        REG8(0x00114D8EUL) = 0x00;
        REG8(0x00114D93UL) = 0x00;
        if (REG8(0x00114DA1UL) == 0x01) module_go_stitch();
        else                            module_go_menu();
        REG8(0x00114D7EUL) = 0x01;
        if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        return;
    }

    if (st == 0x01) {
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        REG8(0x0011F2A1UL) = 0x0C;
        link_send_start();
        REG8(0x00104042UL) = (u8)(REG8(0x00104042UL) + 1);
        return;
    }

    if (st == 0x02) {
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        link_delay(0x0064);
        REG8(0x00104042UL) = (u8)(REG8(0x00104042UL) + 1);
        return;
    }

    if (st == 0x03) {
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        stitch_reset_current();
        REG8(0x0011F2A1UL) = 0x02;
        link_send_start();
        REG8(0x00104042UL) = (u8)(REG8(0x00104042UL) + 1);
        return;
    }

    if (st == 0x04) {
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        module_hoop_check((u8 *)0x00104042UL);
        return;
    }

    if (st == 0x05) {
        REG8(0x00114D73UL) = 0x01;
        REG8(0x00114D72UL) = 0x03;
        REG8(0x00104042UL) = 0x00;
    }
}

/* H'2309EC. Screen H'38's press: fifteen thumbnails, two arrows to page
 * them, two more to walk the strip, a key that takes an entry out and a key
 * that sends the lot. Nothing happens at all while the module is still busy
 * with whatever H'114D8E says it is doing. */
void module_pattern_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0015, &value, &index) != 0x03) return;
    message_show_held(index);

    REG8(0x00114D8EUL) = 0x06;
    if (module_busy() != 0) return;

    if ((u16)(value - 1) > 0x0014) return;

    switch (value) {
    case 0x0010: module_thumb_page_back(); break;
    case 0x0011: module_thumb_page_on();   break;
    case 0x0012: module_list_back();       break;
    case 0x0013: module_list_forward();    break;
    case 0x0014: module_list_delete();     break;
    case 0x0015: module_send_step();       break;
    default:     module_pattern_add((u8)value); break;
    }
}

/* ---- the furniture the module screens share ----------------------------
 * Small routines the eleven module screens all reach: labels drawn in fixed
 * places, boxes put in and out of their states, and the rectangle of the
 * screen that is kept in a buffer of its own.
 */

/* H'241480. The stitch stream cleared, the sibling of
 * H'244AAC's pattern store: H'10C27A up to but not including H'1137AA. */
void stream_clear(void)
{
    u32 p;

    for (p = 0x0010C27AUL; p < 0x001137AAUL; p++) REG8(p) = 0x00;
}

/* H'2172B6, H'2173E6, H'217432, H'217302, H'21734E and H'21739A. Six labels
 * in six fixed places, all in the same font, all centred. */
void module_label_right_top(const char *str)
{
    text_draw(str, 0x00F1, 0x0010, 0x0113, 0x0019, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void module_label_right_mid(const char *str)
{
    text_draw(str, 0x00F1, 0x005E, 0x0113, 0x0067, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void module_label_right_low(const char *str)
{
    text_draw(str, 0x00F1, 0x0085, 0x0113, 0x008E, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void module_label_right_foot(const char *str)
{
    text_draw(str, 0x00F1, 0x00D5, 0x010C, 0x00DE, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void module_label_mid_top(const char *str)
{
    text_draw(str, 0x0078, 0x0010, 0x009A, 0x0019, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void module_label_mid_second(const char *str)
{
    text_draw(str, 0x0078, 0x0037, 0x009A, 0x0040, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

/* H'244CA2. True when bit 6 of H'114D51 is down -- the bit the sewing screen
 * puts up when the module has something to report. */
u8 module_nothing_to_report(void)
{
    if (REG8(0x00114D51UL) & 0x40) return 0x00;
    return 0x01;
}

/* H'244CB4. Whether the hoop that is on is one that can be sewn: the top
 * three bits of H'114D53 name it, and two of the eight are allowed -- H'40
 * with bit 4 up and word H'114D4C's bit H'4000 down, or H'60 with bit 1 of
 * H'114D51 up. */
u8 module_hoop_sewable(void)
{
    if ((u8)(REG8(0x00114D53UL) & 0xE0) == 0x40 &&
        (REG8(0x00114D53UL) & 0x10) &&
        !(REG16(0x00114D4CUL) & 0x4000)) {
        return 0x01;
    }
    if ((REG8(0x00114D51UL) & 0x02) &&
        (u8)(REG8(0x00114D53UL) & 0xE0) == 0x60) {
        return 0x01;
    }
    return 0x00;
}

/* H'24654E. Whether the slot's own block says it is the one design the
 * machine treats specially: four of its sixteen bytes have to read H'32,
 * H'32, nought and H'24. */
u8 module_slot_is_plain(void)
{
    const u32 e = (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4));

    if (REG8(0x0011A25AUL + e) != 0x32) return 0x00;
    if (REG8(0x0011A25BUL + e) != 0x32) return 0x00;
    if (REG8(0x0011A25FUL + e) != 0x00) return 0x00;
    if (REG8(0x0011A260UL + e) != 0x24) return 0x00;
    return 0x01;
}

/* H'23C570. The colour the module has asked for, checked against how many
 * the pattern has: H'1040B1 says which of the two counts is the live one.
 *
 * Four is taken off the number all the way through -- the first four
 * messages are not colours. H'1040B0 keeps the answer whatever happens and
 * H'1040B9 keeps it only while it is inside the count. */
u8 module_colour_check(u8 asked)
{
    const u8 count = (REG8(0x001040B1UL) == 0x01) ? REG8(0x001040AEUL)
                                                  : REG8(0x001040AFUL);
    const u16 n = (u16)((u16)asked - 4);

    REG8(0x001040B0UL) = (u8)(asked - 4);

    if ((short)n > 0x000F) return 0x01;
    if ((short)n > (short)((u16)count + 1)) return 0x01;

    if ((short)n > (short)(u16)count) REG8(0x001040B9UL) = 0x00;
    else                              REG8(0x001040B9UL) = (u8)(asked - 4);
    return 0x00;
}

/* H'24A1EA. One pixel in the back buffer. */
void plot_pixel_back(u16 x, u16 y, u8 colour)
{
    plot_pixel(x, y, LCD_FRAME_B, colour);
}

/* H'249FC2 and H'249FFE. The whole of the module's own rectangle blacked,
 * in the front buffer and in the back one. */
void module_area_clear_front(void)
{
    draw_rect(0x0026, 0x0053, 0x00C1, 0x00EA, LCD_FRAME_A, 0x00, 0x01);
}

void module_area_clear_back(void)
{
    draw_rect(0x0026, 0x0053, 0x00C1, 0x00EA, LCD_FRAME_B, 0x00, 0x01);
}

/* H'24A25A and H'24A29A. A rectangle blacked in the back buffer, and one
 * drawn as an outline in the front. */
void module_box_clear_back(u16 x0, u16 y0, u16 x1, u16 y1)
{
    draw_rect(x0, y0, x1, y1, LCD_FRAME_B, 0x00, 0x01);
}

void module_box_outline(u16 x0, u16 y0, u16 x1, u16 y1)
{
    draw_rect(x0, y0, x1, y1, LCD_FRAME_A, 0x03, 0x00);
}

/* H'24A336. A box put back to plain and then pressed again, which is what
 * repaints one that is already lit. */
void hitbox_repress(u8 box)
{
    hitbox_set_state((u16)box, (u16)box, 0x00, 0);
    hitbox_set_state((u16)box, (u16)box, 0x01, 0);
}

/* H'232394. The same, for a box whose number is inside the strip: boxes H'05
 * to H'13 only, and through the routine that moves the lit one. */
void module_strip_press(u8 box)
{
    if (box <= 0x04) return;
    if (box >= 0x14) return;
    module_lit_box((u16)box);
}

/* H'2317D0 and H'2317EE. Two more ways out of the module's screens. */
void module_go_check(void)
{
    REG8(0x00114D8EUL) = 0x0C;
    screen_switch(0x4C, 0x01, 0x00);
}

void module_go_report(void)
{
    REG8(0x00114D8EUL) = 0x0E;
    screen_switch(0x49, 0x01, 0x00);
}

/* H'231544. The module's rectangle put back from the buffer it is kept in.
 * The first time through there is nothing kept yet: bit 4 of H'114D50 goes
 * up instead, which is what asks for it. */
void module_area_restore(void)
{
    if (REG8(0x0011F4E6UL) == 0) {
        REG8(0x00114D50UL) |= 0x10;
        REG8(0x0011F4E6UL) = 0x01;
        return;
    }
    region_copy(0x0026, 0x0053, 0x00C1, 0x00EA, 0x0053,
                0x000ECB10UL, LCD_FRAME_A);
}

/* H'217A26. Box four lit or put back, and lit again from scratch when it was
 * in the kind that does not repaint. */
void module_box4_press(u8 on)
{
    if (on != 0) {
        if (hitbox_kind(0x0004) == 0x01) {
            hitbox_set_state(0x0004, 0x0004, 0x00, 0);
        }
        hitbox_set_state(0x0004, 0x0004, 0x01, 0);
    } else {
        hitbox_set_state(0x0004, 0x0004, 0x00, 0);
    }
}

/* H'217A82. Boxes three and four as a pair: one lit is the other plain. */
void module_box34_pick(u8 three)
{
    if (three != 0) {
        hitbox_set_state(0x0003, 0x0003, 0x01, 0);
        hitbox_set_state(0x0004, 0x0004, 0x00, 0);
    } else {
        hitbox_set_state(0x0003, 0x0003, 0x00, 0);
        hitbox_set_state(0x0004, 0x0004, 0x01, 0);
    }
}

/* H'217C32. The lit box moved: the one asked for is lit and the one H'11B318
 * remembers is put back. A box of the kind that is already lit is left
 * alone, and so is the memory. */
void module_lit_box(u16 box)
{
    if (hitbox_kind(box) == 0x01) return;

    hitbox_set_state(box, box, 0x01, 0);
    if (box == REG16(0x0011B318UL)) return;

    hitbox_set_state(REG16(0x0011B318UL), REG16(0x0011B318UL), 0x00, 0);
    REG16(0x0011B318UL) = box;
}

/* H'2323AA and H'2323F0. Box three and box H'0A greyed and then blanked --
 * two calls, because the two states draw different things -- unless the bit
 * that says they are wanted is up. */
void module_box3_grey(void)
{
    if (REG8(0x00114D51UL) & 0x02) return;
    hitbox_set_state(0x0003, 0x0003, 0x02, 0);
    hitbox_set_state(0x0003, 0x0003, 0x04, 0);
}

void module_boxA_grey(void)
{
    if (REG8(0x0011F538UL) & 0x01) return;
    hitbox_set_state(0x000A, 0x000A, 0x02, 0);
    hitbox_set_state(0x000A, 0x000A, 0x04, 0);
}

/* H'2352AA. Bit 2 of the byte at H'FFFD1C put down. What the bit drives is
 * not visible from here; it is cleared before a byte goes into the transmit
 * register and it has a routine of its own because two places do it. */
void link_line_release(void)
{
    REG8(0xFFFD1CUL) = (u8)(REG8(0xFFFD1CUL) & (u8)~0x04);
}

/* H'230EA8. The module's rectangle put away into the buffer it is kept in --
 * the mirror of H'231544, and it does nothing until that has been asked for
 * once. */
void module_area_save(void)
{
    if (REG8(0x0011F4E6UL) == 0) return;
    region_copy(0x0026, 0x0053, 0x00C1, 0x00EA, 0x0053,
                LCD_FRAME_A, 0x000ECB10UL);
}

/* H'2316C4. The way out to the settings menu. The module is told the screen
 * is going -- message H'07 -- but only while the link is quiet and only when
 * H'114D51 bit 6 says there is something to tell it, and then it waits for
 * the message to go out before it leaves. */
void module_go_settings(void)
{
    if (module_link_quiet() != 0 && (REG8(0x00114D51UL) & 0x40)) {
        REG8(0x0011F2A1UL) = 0x07;
        REG8(0x00114D51UL) = (u8)(REG8(0x00114D51UL) & (u8)~0x40);
        link_send_start();
        while (module_link_quiet() == 0) rom_host_service();
    }

    screen_switch(0x12, 0x01, 0x00);
    REG8(0x00114D8EUL) = 0x01;
}

/* H'2321B6. The label under the speed bar: the slot's own byte at H'11A264
 * divided by five, or the word "off" when it is nought. The buffer starts as
 * six bytes copied out of H'250776, which are all nought. */
void module_label_speed(void)
{
    const u32 e = (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4));
    char text[6];
    u8 k;

    for (k = 0; k < 3; k++) {
        REG16((u32)(text) + 2u * k) = REG16(0x00250776UL + 2u * k);
    }

    if (REG8(0x0011A264UL + e) != 0) {
        int_to_decimal((short)(u16)(REG8(0x0011A264UL + e) / 5), text);
    } else {
        str_copy(text, (const char *)0x00250AE4UL);
    }

    text_draw(text, 0x00F3, 0x00A8, 0x0111, 0x00B4, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

/* H'23C5EA. The colour strip: fifteen boxes, one for each colour the pattern
 * has, each painted for whether it is inside the count. The one the module
 * is on is left alone. Under it, the percentage done and the bar beside it,
 * which never go backwards -- H'114D56 is what the module last said and the
 * larger of the two is the one drawn. */
void module_colours_show(void)
{
    u8 count, pct;
    u16 box;

    if (REG8(0x00114DB9UL) != 0) return;

    count = (REG8(0x001040B1UL) == 0x01) ? REG8(0x001040AEUL)
                                         : REG8(0x001040AFUL);

    for (box = 0x0005; box <= 0x0013; box++) {
        if ((u16)(box - 4) == (u16)REG8(0x001040B0UL)) continue;
        if ((short)(u16)(box - 4) > (short)(u16)count) hitbox_paint(box, 0x00);
        else                                           hitbox_paint(box, 0x01);
    }

    if (count > 0x0F) pct = 0x64;
    else pct = (u8)((u16)((u16)(0x0064 * (u16)count) / 0x000F));

    if ((REG8(0x00114D51UL) & 0x02) || (REG8(0x00114D51UL) & 0x01)) {
        label_percent_left(REG8(0x00114D56UL) <= pct ? pct
                                                     : REG8(0x00114D56UL));
        module_progress_bar(REG8(0x00114D56UL) <= pct
                                ? (u16)pct : (u16)REG8(0x00114D56UL));
    } else {
        label_percent_left(0x00);
        module_progress_bar(0x0000);
    }
}

/* H'246654. A message from the module that takes the screen away.
 *
 * Only four of the twelve states listen at all, and H'07 listens to one code
 * only. H'80 and H'81 want something to report; H'82 and H'84 want H'11F534
 * up, which is what says a stop has been asked for. The two numbers left in
 * H'11F532 and H'11F533 are what the screen that follows draws.
 */
u8 module_fault_report(u8 code)
{
    const u8 state = REG8(0x00114D8EUL);

    if (!(state == 0x04 || (state >= 0x08 && state < 0x0A))) {
        if (state != 0x07) return 0x00;
        if (code == 0x83) {
            REG8(0x0011F532UL) = 0x07;
            REG8(0x0011F533UL) = 0x07;
            module_go_report();
            return 0x01;
        }
    }

    if (code == 0x80) {
        if (module_nothing_to_report() != 0) return 0x00;
        REG8(0x0011F532UL) = REG8(0x00114D8EUL);
        REG8(0x0011F533UL) = REG8(0x00114D8EUL);
        module_go_check();
        return 0x01;
    }

    if (code == 0x81) {
        if (module_nothing_to_report() != 0) return 0x00;
        REG8(0x0011F534UL) = 0x00;
        REG8(0x0011F532UL) = REG8(0x00114D8EUL);
        if (REG8(0x00114DA1UL) == 0x01) REG8(0x0011F533UL) = 0x03;
        else                            REG8(0x0011F533UL) = 0x02;
        module_go_check();
        return 0x01;
    }

    if (code == 0x82) {
        if (REG8(0x0011F534UL) == 0) return 0x00;
        REG8(0x00114D88UL) = 0x01;
        REG8(0x0011F534UL) = 0x00;
        REG8(0x0011F532UL) = REG8(0x00114D8EUL);
        REG8(0x0011F533UL) = REG8(0x00114D8EUL);
        module_go_check();
        return 0x01;
    }

    if (code == 0x84) {
        if (REG8(0x0011F534UL) == 0) return 0x00;
        REG8(0x00114D88UL) = 0x01;
        REG8(0x0011F534UL) = 0x00;
        REG8(0x0011F532UL) = REG8(0x00114D8EUL);
        if (REG8(0x00114DA1UL) == 0x01) REG8(0x0011F533UL) = 0x03;
        else                            REG8(0x0011F533UL) = 0x02;
        module_go_check();
        return 0x01;
    }

    return 0x00;
}

/* H'2414AE. Every one of the twenty-eight slots put back to nothing: the
 * H'12-byte block at H'11A41A and the H'10-byte one at H'11A25A, then the
 * dozen odds and ends that go with them and the stitch stream. */
void module_slots_clear(void)
{
    u8 s, k;

    for (s = 0; s < 0x1C; s++) {
        const u32 e = (u32)(long)(short)(u16)(0x0012 * (u16)s);

        for (k = 0; k < 0x0C; k++) {
            REG8(0x0011A420UL +
                 (u32)(long)(short)(u16)((u16)(0x0012 * (u16)s) + (u16)k)) = 0x00;
        }
        REG8(0x0011A41AUL + e) = 0x00;
        REG8(0x0011A41BUL + e) = 0x00;
        REG8(0x0011A41CUL + e) = 0x00;
        REG8(0x0011A41DUL + e) = 0x00;
        REG8(0x0011A41EUL + e) = 0x00;
        REG8(0x0011A41FUL + e) = 0x00;
    }

    for (s = 0; s < 0x1C; s++) {
        const u32 e = (u32)(long)(short)(u16)((u16)((u16)s << 4));

        REG16(0x0011A266UL + e) = 0x0000;
        REG16(0x0011A268UL + e) = 0x0000;
        REG8(0x0011A25AUL + e) = 0x32;
        REG8(0x0011A25BUL + e) = 0x32;
        REG8(0x0011A25CUL + e) = 0x32;
        REG8(0x0011A25DUL + e) = 0x32;
        REG8(0x0011A25EUL + e) = 0x5F;
        REG8(0x0011A25FUL + e) = 0x00;
        REG8(0x0011A260UL + e) = 0x24;
        REG8(0x0011A261UL + e) = 0x32;
        REG8(0x0011A262UL + e) = 0x32;
        REG8(0x0011A263UL + e) = 0x00;
        REG8(0x0011A264UL + e) = 0x00;
        REG8(0x0011A265UL + e) = 0x00;
    }

    REG8(0x0011A640UL) = 0x00;
    REG8(0x0011A63FUL) = 0x00;
    REG16(0x0011A63AUL) = 0x0000;
    REG8(0x0011A63CUL) = 0x00;
    REG8(0x0011A63DUL) = 0x00;
    REG8(0x0011A63EUL) = 0x00;
    REG8(0x0011A641UL) = 0x00;
    REG8(0x0011A642UL) = 0x00;
    REG8(0x0011A660UL) = 0x00;
    REG16(0x00114D4CUL) = (u16)(REG16(0x00114D4CUL) & (u16)~0x0002);
    REG16(0x0011F4E8UL) = 0x7FFF;
    REG16(0x0011F4EAUL) = 0x7FFF;
    stream_clear();
}

/* The shorter of the two link-quiet tests: three reads rather than six. The
 * routines that wait between messages use this one. */
static u8 link_idle3(void)
{
    if ((REG8(0x00114D50UL) & 0x21) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    return 0x01;
}

/* H'24A03A and H'24A112. The hoop's own outline, drawn as four lines through
 * the corners at H'11F2E4: x and y of A, then B, then C at H'11F2EE and D at
 * H'11F2F0. One draws into the front buffer and one into the back. */
void module_frame_front(u8 colour)
{
    draw_line(REG16(0x0011F2E4UL), REG16(0x0011F2E8UL),
              REG16(0x0011F2E6UL), REG16(0x0011F2EAUL), LCD_FRAME_A, colour);
    draw_line(REG16(0x0011F2E6UL), REG16(0x0011F2EAUL),
              REG16(0x0011F2EEUL), REG16(0x0011F2F2UL), LCD_FRAME_A, colour);
    draw_line(REG16(0x0011F2EEUL), REG16(0x0011F2F2UL),
              REG16(0x0011F2F0UL), REG16(0x0011F2F4UL), LCD_FRAME_A, colour);
    draw_line(REG16(0x0011F2F0UL), REG16(0x0011F2F4UL),
              REG16(0x0011F2E4UL), REG16(0x0011F2E8UL), LCD_FRAME_A, colour);
}

void module_frame_back(u8 colour)
{
    draw_line(REG16(0x0011F2E4UL), REG16(0x0011F2E8UL),
              REG16(0x0011F2E6UL), REG16(0x0011F2EAUL), LCD_FRAME_B, colour);
    draw_line(REG16(0x0011F2E6UL), REG16(0x0011F2EAUL),
              REG16(0x0011F2EEUL), REG16(0x0011F2F2UL), LCD_FRAME_B, colour);
    draw_line(REG16(0x0011F2EEUL), REG16(0x0011F2F2UL),
              REG16(0x0011F2F0UL), REG16(0x0011F2F4UL), LCD_FRAME_B, colour);
    draw_line(REG16(0x0011F2F0UL), REG16(0x0011F2F4UL),
              REG16(0x0011F2E4UL), REG16(0x0011F2E8UL), LCD_FRAME_B, colour);
}

/* H'23E026. The colour the module is on, drawn big: a one-bit picture H'48
 * across and H'3E down out of the table at H'10032E, with a frame round it
 * three pixels out in the front buffer and two in the back. */
void module_colour_swatch(void)
{
    const u32 bits = 0x0010032EUL +
        (u32)(long)(short)(u16)((u16)((u16)REG8(0x001040B0UL) - 1) * 0x022E);
    const u16 x = 0x003E;
    const u16 y = 0x005B;
    u16 row;

    module_box_clear((u16)(x - 3), (u16)(y - 3),
                     (u16)((u16)(x + 3) + 0x0048),
                     (u16)((u16)(y + 3) + 0x003E));
    module_box_clear_back((u16)(x - 3), (u16)(y - 3),
                          (u16)((u16)(x + 3) + 0x0048),
                          (u16)((u16)(y + 3) + 0x003E));
    module_box_outline((u16)(x - 2), (u16)(y - 2),
                       (u16)((u16)(x + 2) + 0x0048),
                       (u16)((u16)(y + 2) + 0x003E));

    for (row = 0; row < 0x003E; row++) {
        u16 bi;
        u16 px = 0;

        for (bi = 0; bi < 0x0009; bi++) {
            const u8 byte = REG8(bits + (u32)(u16)((u16)(0x0009 * row) + bi));
            u8 mask = 0x80;
            u16 k;

            for (k = 0; k < 0x0008; k++) {
                if ((u8)(mask & byte) == mask) {
                    plot_pixel((u16)(x + px), (u16)(y + row),
                               LCD_FRAME_A, 0x03);
                }
                mask = (u8)(mask >> 1);
                px = (u16)(px + 1);
            }
        }
    }
}

/* H'23DE8E. The four steps that start a colour off. H'1040B6 walks them.
 *
 * Step nought answers the module's own code: H'09 draws the colour when the
 * hoop can be sewn and there is a colour to draw, H'0A steps on, and
 * anything else puts the walk back to the start.
 */
void module_start_step(u8 code)
{
    const u8 st = REG8(0x001040B6UL);

    if (st == 0x00) {
        if (code == 0x09) {
            u8 count;

            if (module_hoop_sewable() == 0) return;
            count = (REG8(0x001040B1UL) == 0x01) ? REG8(0x001040AEUL)
                                                 : REG8(0x001040AFUL);
            if (count == 0) return;
            if (REG8(0x001040B0UL) > count) return;
            module_colour_swatch();
            REG8(0x001040B5UL) = 0x01;
            return;
        }
        if (code == 0x0A) {
            if (module_hoop_sewable() == 0) return;
            REG8(0x001040B6UL) = (u8)(REG8(0x001040B6UL) + 1);
            return;
        }
        REG8(0x001040B6UL) = 0x00;
        REG8(0x0011F571UL) = 0x00;
        return;
    }

    if (st == 0x01) {
        if (module_link_quiet() == 0) return;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61BUL) = 0x16;
        link_send_start();
        REG8(0x001040B6UL) = (u8)(REG8(0x001040B6UL) + 1);
        return;
    }

    if (st == 0x02) {
        if (module_link_quiet() == 0) return;
        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x03;
        link_send_start();
        REG8(0x001040B6UL) = (u8)(REG8(0x001040B6UL) + 1);
        return;
    }

    if (module_link_quiet() == 0) return;
    REG8(0x001040B6UL) = 0x00;
    REG8(0x0011F571UL) = 0x00;
}

/* H'23C450 and H'23C2FA. The two corners of the hoop measured.
 *
 * Both send a run of messages with a wait between each, which is why only
 * the ways they turn back are covered by cases: once a message has gone out,
 * only the link's own interrupt ends the wait.
 */
void module_measure_second(void)
{
    if (!(REG8(0x00114D51UL) & 0x02)) return;
    if (REG8(0x001040B1UL) == 0x04) return;

    while (module_link_quiet() == 0) rom_host_service();

    module_box34_pick(0x01);
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x18;
    link_send_start();
    while (link_idle3() == 0) rom_host_service();

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x06;
    link_send_start();
    while (link_idle3() == 0) rom_host_service();

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x09;
    link_send_start();
    while (link_idle3() == 0) rom_host_service();

    REG8(0x001040B0UL) = 0x00;
    REG8(0x001040B1UL) = 0x04;
    module_colours_show();
    REG8(0x001040B7UL) = 0x00;
    REG8(0x001040B9UL) = 0x00;
}

void module_measure_first(void)
{
    if (!(REG8(0x00114D51UL) & 0x02)) return;
    if (REG8(0x001040B1UL) == 0x01) return;
    if (!(REG8(0x00114D51UL) & 0x01)) { (void)link_claim(0x18); return; }

    while (module_link_quiet() == 0) rom_host_service();

    module_box34_pick(0x00);
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x17;
    link_send_start();
    while (link_idle3() == 0) rom_host_service();

    if ((u8)(REG8(0x00114D53UL) & 0xE0) != 0x40) {
        REG8(0x001040B1UL) = 0x01;
        module_measure_second();
        (void)link_claim(0x11);
        return;
    }

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x06;
    link_send_start();
    while (link_idle3() == 0) rom_host_service();

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x09;
    link_send_start();
    while (link_idle3() == 0) rom_host_service();

    REG8(0x001040B0UL) = 0x00;
    REG8(0x001040B1UL) = 0x01;
    module_colours_show();
    REG8(0x001040B7UL) = 0x00;
    REG8(0x001040B9UL) = 0x00;
}

/* H'2321B4. Another single RTS, between the two label routines. */
void module_label_hook(void)
{
}

/* H'231450 and H'23128C. The two ways the hoop's own numbers are shown.
 *
 * The first puts how big the design is -- the two bytes of the slot's block
 * added -- as a percentage in the top right and the weight underneath it,
 * both built in the scratch string at H'11F2D6 with their unit appended.
 *
 * The second is the four numbers of the scaling screen: the two sizes
 * doubled, and two more taken from two hundred, in the four places down the
 * middle and the right.
 */
void module_size_show(u16 across, u16 down)
{
    const u32 e = (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4));

    int_to_decimal((short)(u16)((u16)REG8(0x0011A25BUL + e) +
                               (u16)REG8(0x0011A25AUL + e)),
                   (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    module_label_right_top((const char *)0x0011F2D6UL);

    int_to_decimal((short)REG16(0x0011F292UL), (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x67;
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    module_label_right_foot((const char *)0x0011F2D6UL);

    module_size_labels((short)across, (short)down);
    module_area_save();
}

void module_scale_show(u16 across, u16 down)
{
    const u32 e = (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4));

    int_to_decimal((short)(u16)((u16)REG8(0x0011A25AUL + e) << 1),
                   (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    module_label_mid_top((const char *)0x0011F2D6UL);

    int_to_decimal((short)(u16)((u16)REG8(0x0011A25BUL + e) << 1),
                   (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    module_label_mid_second((const char *)0x0011F2D6UL);

    int_to_decimal((short)(u16)(0x00C8 -
                       (u16)((u16)REG8(0x0011A261UL + e) << 1)),
                   (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    module_label_right_low((const char *)0x0011F2D6UL);

    int_to_decimal((short)(u16)(0x00C8 -
                       (u16)((u16)REG8(0x0011A25CUL + e) << 1)),
                   (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    module_label_right_mid((const char *)0x0011F2D6UL);

    module_size_labels((short)across, (short)down);
    module_label_speed();
    module_label_hook();
    module_area_save();
}

/* H'23A7B0. The whole picking grid laid out: three rows of five, the four
 * arrows put in the state the position calls for, and the strip along the
 * bottom copied back from the buffer it is kept in.
 *
 * The three rows do not count what is left the same way -- the first takes
 * four off, the second adds one and the third takes nine off -- but all
 * three stop at five, so only the last page tells them apart. */
void module_grid_draw(void)
{
    short room;

    room = (short)((u16)module_thumb_total() - (u16)((u16)MOD_PAGE + 0x0004));
    if (room > 0x0005) room = 0x0005;
    if (room >= 0) module_thumb_row_draw(0x00, (u16)MOD_PAGE, (u8)room);

    room = (short)((u16)((u16)module_thumb_total() - (u16)MOD_PAGE) + 1);
    if (room > 0x0005) room = 0x0005;
    if (room >= 0) {
        module_thumb_row_draw(0x01, (u16)((u16)MOD_PAGE + 0x0005), (u8)room);
    }

    room = (short)((u16)module_thumb_total() - (u16)((u16)MOD_PAGE + 0x0009));
    if (room > 0x0005) room = 0x0005;
    if (room >= 0) {
        module_thumb_row_draw(0x02, (u16)((u16)MOD_PAGE + 0x000A), (u8)room);
    }

    if (MOD_PAGE != 0) module_page_arrow_back(0x01);
    else               module_page_arrow_back(0x00);

    if ((short)((u16)((u16)module_thumb_total() - (u16)MOD_PAGE) + 1) > 0x000F) {
        module_page_arrow_on(0x01);
    } else {
        module_page_arrow_on(0x00);
    }

    if (MOD_POS != 0) module_list_arrow_back(0x01);
    else              module_list_arrow_back(0x00);

    if (MOD_POS >= MOD_COUNT) module_list_arrow_on(0x00);
    else                      module_list_arrow_on(0x01);

    region_copy(MOD_STRIP_X0, MOD_STRIP_Y0,
                (u16)(MOD_STRIP_X0 + 0x00BB), (u16)(MOD_STRIP_Y0 + 0x0023),
                MOD_STRIP_Y0, MOD_SAVE, LCD_FRAME_A);
}

/* H'2369C4. The three hoop pictures down the side, lit for the hoops the
 * design still fits in.
 *
 * The design's size comes out of two tables indexed by its own number --
 * H'104CCE across and H'104D06 down -- doubled and scaled by the slot's own
 * two percentages. Each picture has a pair of limits and goes out as soon as
 * either measurement is past its own.
 */
void module_hoop_pictures(void)
{
    const u32 i2 = (u32)(long)(short)(u16)((u16)(
        (u16)REG8(0x0011A41AUL + (u32)(long)(short)(u16)(
            0x0012 * (u16)REG8(0x0011A660UL))) << 1));
    const u32 s16 = (u32)(long)(short)(u16)(
        (u16)((u16)REG8(0x0011A660UL) << 4));

    const float across = (float)(u32)REG16(0x00104CCEUL + i2) * 2.0f *
                         (float)(u32)REG8(0x0011A25AUL + s16) / 100.0f;
    const float down   = (float)(u32)REG16(0x00104D06UL + i2) * 2.0f *
                         (float)(u32)REG8(0x0011A25BUL + s16) / 100.0f;

    if ((short)(long)across > 0x060E || (short)(long)down > 0x07D0) {
        module_step_picture(0x01, 0x00);
    } else {
        module_step_picture(0x01, 0x01);
    }

    if ((short)(long)across > 0x03E8 || (short)(long)down > 0x0514) {
        module_step_picture(0x02, 0x00);
    } else {
        module_step_picture(0x02, 0x01);
    }

    if ((short)(long)across > 0x0258 || (short)(long)down > 0x0190) {
        module_step_picture(0x03, 0x00);
    } else {
        module_step_picture(0x03, 0x01);
    }
}

/* H'236BE4. The hoop drawn as a rectangle round the middle of the screen:
 * the two half-sizes at H'11A626 and H'11A628 scaled by H'0.076, taken out
 * from the centre at x H'73 and y H'9E.
 *
 * H'114D4E of seven is the hoop that gets a double outline -- eight lines
 * rather than four, one pair inside the other and two more across it. */
void module_hoop_outline(void)
{
    const float k = 0.0759999976f;
    const short w = (short)(long)((float)(u32)REG16(0x0011A626UL) / 2.0f * k);
    const short h = (short)(long)((float)(u32)REG16(0x0011A628UL) / 2.0f * k);
    const u16 left  = (u16)(0x0073 - w);
    const u16 right = (u16)(w + 0x0073);
    const u16 top   = (u16)(0x009E - h);
    const u16 base  = (u16)(h + 0x009E);

    if (REG8(0x00114D4EUL) == 0x07) {
        draw_line((u16)(left - 1), (u16)(top + 1),
                  (u16)(right + 1), (u16)(top + 1), LCD_FRAME_A, 0x02);
        draw_line((u16)(right + 1), (u16)(top + 1),
                  (u16)(right + 1), base, LCD_FRAME_A, 0x02);
        draw_line((u16)(right + 1), base,
                  (u16)(left - 1), base, LCD_FRAME_A, 0x02);
        draw_line((u16)(left - 1), base,
                  (u16)(left - 1), (u16)(top + 1), LCD_FRAME_A, 0x02);
        draw_line((u16)(left - 1), (u16)(top - 1),
                  (u16)(right + 1), (u16)(top - 1), LCD_FRAME_A, 0x02);
        draw_line((u16)(left - 1), (u16)(base + 1),
                  (u16)(right + 1), (u16)(base + 1), LCD_FRAME_A, 0x02);
        draw_line((u16)(left - 1), top,
                  (u16)(right + 1), top, LCD_FRAME_A, 0x02);
        draw_line((u16)(left - 1), base,
                  (u16)(right + 1), base, LCD_FRAME_A, 0x02);
        return;
    }

    draw_line((u16)(left - 1), (u16)(top - 1),
              (u16)(right + 1), (u16)(top - 1), LCD_FRAME_A, 0x02);
    draw_line((u16)(right + 1), (u16)(top - 1),
              (u16)(right + 1), (u16)(base + 1), LCD_FRAME_A, 0x02);
    draw_line((u16)(right + 1), (u16)(base + 1),
              (u16)(left - 1), (u16)(base + 1), LCD_FRAME_A, 0x02);
    draw_line((u16)(left - 1), (u16)(base + 1),
              (u16)(left - 1), (u16)(top - 1), LCD_FRAME_A, 0x02);
}

/* H'242868. The thirteen steps that fetch a run of patterns out of the
 * module and set the machine up to sew them. H'11A63D walks the steps and
 * [which] of one says it is the stitch screen asking rather than the
 * embroidery one.
 *
 * Step nought is the only one that can answer nought: it wants H'114D51 bit
 * 6 up and one of bits 2 and 3 of H'11A63C. Every step after it waits for
 * the link to be quiet and then sends one message; step eight is the one
 * that falls through into step nine rather than ending, which is in the
 * original and is reproduced.
 *
 * Step seven takes the design's number out of H'0FFE80 and folds it into a
 * page and an offset -- H'1B designs to a page -- and step twelve puts the
 * machine into whichever of the two sewing states the answer to [which]
 * called for.
 */
u8 module_run_fetch(u16 which)
{
    const u8 st = REG8(0x0011A63DUL);
    const u32 e12 = (u32)(long)(short)(u16)(
        0x0012 * (u16)REG8(0x0011A660UL));

    if (st > 0x0C) {
        REG16(0x0011A63AUL) = 0x0000;
        REG8(0x0011A63DUL) = 0x00;
        return 0x01;
    }

    switch (st) {
    case 0x00:
        if (!(REG8(0x00114D51UL) & 0x40)) return 0x00;
        if (!(REG8(0x0011A63CUL) & 0x04) &&
            !(REG8(0x0011A63CUL) & 0x08)) return 0x00;
        REG8(0x0011F58EUL) = REG8(0x0011A63CUL);
        REG16(0x0011F58CUL) = which;
        if (which == 0x0001) module_to_screen_15();
        else                 module_go_sewing();
        module_slots_clear();
        REG16(0x0011A63AUL) = (u16)(REG16(0x0011A63AUL) | 0x0020);
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        return 0x01;

    case 0x01: {
        u32 keep[3];
        u8 n;

        if (REG8(0x00114D51UL) & 0x02) {
            if (REG8(0x001040B1UL) == 0x01) REG8(0x0011F58FUL) = 0x01;
            else                            REG8(0x0011F58FUL) = 0x00;
        } else {
            if (REG8(0x0011F58EUL) & 0x04) REG8(0x0011F58FUL) = 0x01;
            if (REG8(0x0011F58EUL) & 0x08) REG8(0x0011F58FUL) = 0x04;
        }

        for (n = 0; n < 3; n++) keep[n] = REG32(0x0011A61EUL + 4u * n);
        module_buffers_clear();
        if (REG16(0x0011F58CUL) == 0x0001) REG8(0x00114D8EUL) = 0x08;
        else                               REG8(0x00114D8EUL) = 0x04;
        for (n = 0; n < 3; n++) REG32(0x0011A61EUL + 4u * n) = keep[n];

        REG8(0x00114DA1UL) = REG8(0x0011F58FUL);
        REG8(0x0011A41DUL + e12) = REG8(0x0011F58FUL);
        REG8(0x0011A41FUL + e12) = 0x00;
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;
    }

    case 0x02:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x07;
        REG8(0x00114D51UL) = (u8)(REG8(0x00114D51UL) & (u8)~0x40);
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x03:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x01;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x04:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x02;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x05:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61BUL) = 0x08;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x06:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x01;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x07:
        if (module_link_quiet() == 0) break;
        REG8(0x0011A41AUL + e12) = REG8(0x000FFE80UL);
        REG8(0x00114DBCUL) = (u8)(REG8(0x000FFE80UL) - REG8(0x00100255UL));
        REG8(0x00114DBDUL) = REG8(0x000FFE80UL);
        while (REG8(0x0011A41AUL + e12) >= 0x1C) {
            REG8(0x0011A41AUL + e12) = (u8)(REG8(0x0011A41AUL + e12) - 0x1B);
            REG8(0x0011A41FUL + e12) = (u8)(REG8(0x0011A41FUL + e12) + 1);
        }
        REG16(0x0011F292UL) = 0x0000;
        REG8(0x00114D73UL) = 0x01;
        REG8(0x0011F2A1UL) = 0x01;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x08:
        if (module_link_quiet() != 0) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A61BUL) = 0x04;
            link_send_start();
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        /* and straight on into step nine, which is what the original does */
        /* fall through */
    case 0x09:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x02;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x0A:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x01;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x0B:
        if (module_link_quiet() == 0) break;
        REG8(0x00114D8DUL) =
            REG8(0x000FFE9CUL + (u32)REG8(0x0011A41AUL + e12));
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x01;
        link_send_start();
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    default:
        if (module_link_quiet() == 0) break;
        if (REG16(0x0011F58CUL) == 0x0001) {
            REG8(0x00114D93UL) = 0x00;
            REG8(0x00114D92UL) = 0xFF;
            REG8(0x0011F4E6UL) = 0x01;
            REG8(0x00114D72UL) = 0x01;
        } else {
            REG8(0x00114D93UL) = 0x01;
            REG8(0x00114D92UL) = 0xFF;
            REG8(0x00114D65UL) = 0x00;
            REG8(0x00114D66UL) = 0x01;
        }
        REG16(0x0011A63AUL) = 0x0000;
        REG8(0x0011A63DUL) = 0x00;
        module_panel_blink(0x00);
        return 0x01;
    }

    module_panel_blink(0x01);
    return 0x01;
}

/* H'24A62E. The marks inside the hoop's rectangle: a dashed cross through
 * the middle, and for two of the hoops something more.
 *
 * The rectangle is worked out the same way H'236BE4 does it, and the middle
 * and the quarter are then plain integer division of the sides. Every dash
 * is a line two pixels long, five apart down the cross and eight apart on
 * the quarter lines.
 *
 * Hoop H'AC gets four more dashed lines, one a quarter in from each side.
 * Hoops H'07 and H'03 get a dashed circle instead, of the quarter's radius:
 * thirty-six chords of five degrees, ten degrees apart, which is why this is
 * the only drawing routine in the machine that needs a sine.
 */
void module_hoop_marks(void)
{
    const float k = 0.0759999976f;
    short w, h, left, right, top, base, cx, cy, qw, qh, at;

    if (REG8(0x00114D4EUL) == 0) return;

    w = (short)(long)((float)(u32)REG16(0x0011A626UL) / 2.0f * k);
    left  = (short)(0x0073 - w);
    right = (short)(w + 0x0073);
    h = (short)(long)((float)(u32)REG16(0x0011A628UL) / 2.0f * k);
    top  = (short)(0x009E - h);
    base = (short)(h + 0x009E);

    cx = (short)((short)((short)(right - left) / (short)2) + left);
    cy = (short)((short)((short)(base - top) / (short)2) + top);
    qw = (short)((short)(right - left) / (short)4);
    qh = (short)((short)(base - top) / (short)4);

    for (at = cy; at < base; at = (short)(at + 5)) {
        draw_line((u16)cx, (u16)at, (u16)cx, (u16)(at + 2), LCD_FRAME_A, 0x02);
    }
    for (at = cy; at > top; at = (short)(at - 5)) {
        draw_line((u16)cx, (u16)at, (u16)cx, (u16)(at - 2), LCD_FRAME_A, 0x02);
    }
    for (at = cx; at < right; at = (short)(at + 5)) {
        draw_line((u16)at, (u16)cy, (u16)(at + 2), (u16)cy, LCD_FRAME_A, 0x02);
    }
    for (at = cx; at > left; at = (short)(at - 5)) {
        draw_line((u16)at, (u16)cy, (u16)(at - 2), (u16)cy, LCD_FRAME_A, 0x02);
    }

    if (REG8(0x00114D4EUL) == 0xAC) {
        for (at = cy; at < base; at = (short)(at + 8)) {
            draw_line((u16)(qw + cx), (u16)at, (u16)(qw + cx), (u16)(at + 2),
                      LCD_FRAME_A, 0x02);
            draw_line((u16)(cx - qw), (u16)at, (u16)(cx - qw), (u16)(at + 2),
                      LCD_FRAME_A, 0x02);
        }
        for (at = cy; at > top; at = (short)(at - 8)) {
            draw_line((u16)(qw + cx), (u16)at, (u16)(qw + cx), (u16)(at - 2),
                      LCD_FRAME_A, 0x02);
            draw_line((u16)(cx - qw), (u16)at, (u16)(cx - qw), (u16)(at - 2),
                      LCD_FRAME_A, 0x02);
        }
        for (at = cx; at < right; at = (short)(at + 8)) {
            draw_line((u16)at, (u16)(qh + cy), (u16)(at + 2), (u16)(qh + cy),
                      LCD_FRAME_A, 0x02);
            draw_line((u16)at, (u16)(cy - qh), (u16)(at + 2), (u16)(cy - qh),
                      LCD_FRAME_A, 0x02);
        }
        for (at = cx; at > left; at = (short)(at - 8)) {
            draw_line((u16)at, (u16)(qh + cy), (u16)(at - 2), (u16)(qh + cy),
                      LCD_FRAME_A, 0x02);
            draw_line((u16)at, (u16)(cy - qh), (u16)(at - 2), (u16)(cy - qh),
                      LCD_FRAME_A, 0x02);
        }
    }

    if (REG8(0x00114D4EUL) != 0x07 && REG8(0x00114D4EUL) != 0x03) return;

    for (at = 0; at < 0x0168; at = (short)(at + 10)) {
        const float a = (float)(long)at * 3.14159012f / 180.0f;
        const short ax = (short)(long)((float)(long)qw * float_cos(a));
        const short ay = (short)(long)((float)(long)qw * float_sin(a));
        const float b = (float)(long)(short)(at + 5) * 3.14159012f / 180.0f;
        const short bx = (short)(long)((float)(long)qw * float_cos(b));
        const short by = (short)(long)((float)(long)qw * float_sin(b));

        draw_line((u16)(ax + cx), (u16)(ay + cy),
                  (u16)(bx + cx), (u16)(by + cy), LCD_FRAME_A, 0x02);
    }
}

/* The ROM negates a float by flipping the sign bit and leaves one whose top
 * word is nought alone, so a zero stays positive. */
static float float_flip(float x)
{
    const u32 u = f2u(x);

    if ((u16)(u >> 16) != 0) return u2f(u ^ 0x80000000UL);
    return x;
}

/* One corner of the design turned by the angle and put where it belongs on
 * the screen: x out from H'73 and y up from H'9E, both with the design's own
 * offset added. */
static void module_turn_point(float px, float py, float cs, float sn,
                              float ox, float oy, short *x, short *y)
{
    *x = (short)((short)((short)(long)(px * cs - py * sn) +
                         (short)(long)ox) + 0x0073);
    *y = (short)((short)((short)(long)(py * cs + px * sn) -
                         (short)(long)oy) + 0x009E);
}

/* H'24073E, and the first half of H'246EEC.
 *
 * The design's rectangle turned by the angle the slot is set to, and the
 * eight points it comes out as put in the twelve words at H'11F2E4.
 *
 * The rectangle is the design's size out of the two tables at H'104CCE and
 * H'104D06, scaled by the slot's own two percentages, taken about its centre
 * and turned by *minus* the angle. The turned corners go on the screen at
 * H'73 across and H'9E down with the design's own offset added. Three more
 * points make the little arrow at the far corner that says which way up it
 * is, and H'11A25F mirrors it.
 *
 * Turning leaves corners a pixel out of square, so each pair is compared and
 * one snapped to the other when they differ by exactly one -- eight
 * comparisons in a fixed order, each seeing what the one before it changed.
 *
 * H'246EEC works this out and then draws it; H'24073E works it out and stops,
 * which is what the screens that only need to know where the design is call.
 */
void module_design_corners(void)
{
    const u32 s16 = (u32)(long)(short)(u16)(
        (u16)((u16)REG8(0x0011A660UL) << 4));
    const u32 idx = (u32)(long)(short)(u16)(
        (u16)((u16)REG8(0x0011A41AUL + (u32)(long)(short)(u16)(
            0x0012 * (u16)REG8(0x0011A660UL))) << 1));
    float ang, cs, sn, sx, sy, ox, oy;
    short w, h, w2, h2;
    short x1, y1, x2, y2, x3, y3, x4, y4;
    short ax, ay, bx, by, cx2, cy2;

    ang = ((float)(u32)REG8(0x0011A260UL + s16) * 5.0f + -180.0f) *
          0.0174532775f;
    cs = float_cos(float_flip(ang));
    sn = float_sin(float_flip(ang));

    sx = (float)(u32)REG8(0x0011A25AUL + s16) * 2.0f / 100.0f * 0.0759999976f;
    sy = (float)(u32)REG8(0x0011A25BUL + s16) * 2.0f / 100.0f * 0.0759999976f;
    w = (short)(long)(sx * (float)(u32)REG16(0x00104CCEUL + idx));
    h = (short)(long)(sy * (float)(u32)REG16(0x00104D06UL + idx));

    ox = (float)(long)(short)REG16(0x0011A266UL + s16) * (5.0f * 0.0759999976f);
    oy = (float)(long)(short)REG16(0x0011A268UL + s16) * (5.0f * 0.0759999976f);

    w2 = (short)((short)((short)-w / (short)2) + w);
    h2 = (short)((short)((short)-h / (short)2) + h);

    module_turn_point((float)(long)(short)((short)-w / (short)2),
                      (float)(long)(short)((short)-h / (short)2),
                      cs, sn, ox, oy, &x1, &y1);
    module_turn_point((float)(long)w2,
                      (float)(long)(short)((short)-h / (short)2),
                      cs, sn, ox, oy, &x2, &y2);
    module_turn_point((float)(long)w2, (float)(long)h2,
                      cs, sn, ox, oy, &x3, &y3);
    module_turn_point((float)(long)(short)((short)-w / (short)2),
                      (float)(long)h2, cs, sn, ox, oy, &x4, &y4);

    if (REG8(0x0011A25FUL + s16) == 0) {
        module_turn_point((float)(long)(short)(w2 + 2),
                          (float)(long)(short)(h2 - 2),
                          cs, sn, ox, oy, &ax, &ay);
        module_turn_point((float)(long)(short)(w2 + 2),
                          (float)(long)(short)(h2 + 2),
                          cs, sn, ox, oy, &bx, &by);
        module_turn_point((float)(long)(short)(w2 - 2),
                          (float)(long)(short)(h2 + 2),
                          cs, sn, ox, oy, &cx2, &cy2);
    } else {
        module_turn_point(float_flip((float)(long)(short)(w2 + 2)),
                          (float)(long)(short)(h2 - 2),
                          cs, sn, ox, oy, &ax, &ay);
        module_turn_point(float_flip((float)(long)(short)(w2 + 2)),
                          (float)(long)(short)(h2 + 2),
                          cs, sn, ox, oy, &bx, &by);
        module_turn_point(float_flip((float)(long)(short)(w2 - 2)),
                          (float)(long)(short)(h2 + 2),
                          cs, sn, ox, oy, &cx2, &cy2);
    }

    /* A corner a pixel out of square snapped back to its neighbour. */
    if (abs_short((short)(x1 - x2)) == 1) x1 = x2;
    if (abs_short((short)(x2 - x3)) == 1) x2 = x3;
    if (abs_short((short)(x3 - x4)) == 1) x3 = x4;
    if (abs_short((short)(x4 - x1)) == 1) x4 = x1;
    if (abs_short((short)(y1 - y2)) == 1) y1 = y2;
    if (abs_short((short)(y2 - y3)) == 1) y2 = y3;
    if (abs_short((short)(y3 - y4)) == 1) y3 = y4;
    if (abs_short((short)(y4 - y1)) == 1) y4 = y1;

    REG16(0x0011F2E4UL) = (u16)x1;
    REG16(0x0011F2E6UL) = (u16)x2;
    REG16(0x0011F2E8UL) = (u16)y1;
    REG16(0x0011F2EAUL) = (u16)y2;
    REG16(0x0011F2EEUL) = (u16)x3;
    REG16(0x0011F2F2UL) = (u16)y3;
    REG16(0x0011F2F0UL) = (u16)x4;
    REG16(0x0011F2F4UL) = (u16)y4;
    REG16(0x0011F2F6UL) = (u16)ax;
    REG16(0x0011F2F8UL) = (u16)ay;
    REG16(0x0011F2FAUL) = (u16)bx;
    REG16(0x0011F2FCUL) = (u16)by;
    REG16(0x0011F2FEUL) = (u16)cx2;
    REG16(0x0011F300UL) = (u16)cy2;
    REG16(0x0011F2ECUL) = (u16)REG8(0x0011A260UL + s16);
}

/* H'246EEC. The design's outline drawn where H'24073E says it is.
 *
 * The corners of the last one drawn are still in the twelve words, and the
 * first thing this does with them is draw the same six lines again in black
 * -- which is the whole of how the old outline comes off without touching
 * anything else in the buffer. Then the new corners are worked out and the
 * six lines drawn again in colour three.
 *
 * It does not run every pass: H'11F59F counts H'47 of them, or three when
 * H'114DB0 is down, and only then is there anything to draw. That count is
 * only kept while H'11A177 is nought -- with it set the screen has just
 * changed and the outline is wanted at once.
 */
void module_design_outline(void)
{
    if (REG8(0x0011A177UL) == 0) {
        if (link_idle3() == 0) { REG8(0x0011F59FUL) = 0x00; return; }

        if (REG8(0x00114DB0UL) != 0) {
            if (REG8(0x0011F59FUL) <= 0x46) {
                REG8(0x0011F59FUL) = (u8)(REG8(0x0011F59FUL) + 1);
                return;
            }
        } else {
            if (REG8(0x0011F59FUL) <= 0x02) {
                REG8(0x0011F59FUL) = (u8)(REG8(0x0011F59FUL) + 1);
                return;
            }
        }

        REG8(0x0011F59FUL) = 0x00;
        if (REG8(0x00114D8EUL) == 0x0B) {
            REG8(0x00114D50UL) = (u8)(REG8(0x00114D50UL) & (u8)~0x10);
            REG8(0x001040BBUL) = 0x00;
            return;
        }
    }

    if (REG16(0x0011F2E4UL) == 0) module_area_clear_front();

    /* The last outline taken off, in black. */
    draw_line(REG16(0x0011F2E4UL), REG16(0x0011F2E8UL),
              REG16(0x0011F2E6UL), REG16(0x0011F2EAUL), LCD_FRAME_A, 0x00);
    draw_line(REG16(0x0011F2E6UL), REG16(0x0011F2EAUL),
              REG16(0x0011F2EEUL), REG16(0x0011F2F2UL), LCD_FRAME_A, 0x00);
    draw_line(REG16(0x0011F2EEUL), REG16(0x0011F2F2UL),
              REG16(0x0011F2F0UL), REG16(0x0011F2F4UL), LCD_FRAME_A, 0x00);
    draw_line(REG16(0x0011F2F0UL), REG16(0x0011F2F4UL),
              REG16(0x0011F2E4UL), REG16(0x0011F2E8UL), LCD_FRAME_A, 0x00);
    draw_line(REG16(0x0011F2F6UL), REG16(0x0011F2F8UL),
              REG16(0x0011F2FAUL), REG16(0x0011F2FCUL), LCD_FRAME_A, 0x00);
    draw_line(REG16(0x0011F2FAUL), REG16(0x0011F2FCUL),
              REG16(0x0011F2FEUL), REG16(0x0011F300UL), LCD_FRAME_A, 0x00);

    REG8(0x00114D50UL) = (u8)(REG8(0x00114D50UL) & (u8)~0x10);
    REG8(0x001040BBUL) = 0x00;

    module_design_corners();

    draw_line(REG16(0x0011F2E4UL), REG16(0x0011F2E8UL),
              REG16(0x0011F2E6UL), REG16(0x0011F2EAUL), LCD_FRAME_A, 0x03);
    draw_line(REG16(0x0011F2E6UL), REG16(0x0011F2EAUL),
              REG16(0x0011F2EEUL), REG16(0x0011F2F2UL), LCD_FRAME_A, 0x03);
    draw_line(REG16(0x0011F2EEUL), REG16(0x0011F2F2UL),
              REG16(0x0011F2F0UL), REG16(0x0011F2F4UL), LCD_FRAME_A, 0x03);
    draw_line(REG16(0x0011F2F0UL), REG16(0x0011F2F4UL),
              REG16(0x0011F2E4UL), REG16(0x0011F2E8UL), LCD_FRAME_A, 0x03);
    draw_line(REG16(0x0011F2F6UL), REG16(0x0011F2F8UL),
              REG16(0x0011F2FAUL), REG16(0x0011F2FCUL), LCD_FRAME_A, 0x03);
    draw_line(REG16(0x0011F2FAUL), REG16(0x0011F2FCUL),
              REG16(0x0011F2FEUL), REG16(0x0011F300UL), LCD_FRAME_A, 0x03);

    module_hoop_outline();
    module_hoop_marks();

    /* The two states that are sewing are the only ones that want the size
     * put up with it. */
    if (REG8(0x00114D8EUL) != 0x08 && REG8(0x00114D8EUL) != 0x09) return;

    {
        const u32 idx = (u32)(long)(short)(u16)(
            (u16)((u16)REG8(0x0011A41AUL + (u32)(long)(short)(u16)(
                0x0012 * (u16)REG8(0x0011A660UL))) << 1));
        const u16 a = (u16)(REG16(0x00104CCEUL + idx) / 10);
        const u16 b = (u16)(REG16(0x00104D06UL + idx) / 10);

        REG16(0x0011F4E2UL) = b;
        REG16(0x0011F4E0UL) = a;
        module_size_labels((short)a, (short)b);
    }
}

/* H'2404F0. One block of the stitch stream drawn.
 *
 * A block's record says which of two kinds it is. Kind one is a one-bit
 * picture -- [1] bytes to a row and [2] rows -- whose middle is put at the
 * offset in words 6 and 8 scaled the way everything else in here is scaled,
 * and drawn a bit at a time in [colour]. Kind two is not a picture at all:
 * eight bytes that are the hoop's four corners, which go into the same words
 * at H'11F2E4 the design's outline uses and are then drawn as a frame.
 *
 * [mode] says which buffer: one the front, two the back.
 */
void module_block_draw(const u8 *rec, u8 colour, u8 mode)
{
    u32 p = REG32((u32)(unsigned long)(rec + 0x10)) + (u32)rec[0x14];

    if (rec[0] == 0x01) {
        const float t = 5.0f * 0.0759999976f;
        const float fx =
            (float)(long)(short)REG16((u32)(unsigned long)(rec + 0x06)) * t;
        const float fy =
            (float)(long)(short)REG16((u32)(unsigned long)(rec + 0x08)) * t;
        const short half = (short)((short)(u16)((u16)rec[1] << 3) / (short)2);
        const u16 mid = (u16)((u16)rec[2] / 2);
        const short x0 = (short)((short)((short)(long)fx - half) + 0x0073);
        const short y0 = (short)((short)((short)-(short)(long)fy -
                                         (short)mid) + 0x009E);
        u16 row;

        for (row = 0; row < (u16)rec[2]; row++) {
            u16 bi;
            u16 px = 0;

            for (bi = 0; bi < (u16)rec[1]; bi++) {
                const u8 byte = REG8(p);
                u8 mask = 0x80;
                u16 k;

                for (k = 0; k < 0x0008; k++) {
                    if ((u8)(byte & mask) == mask) {
                        if (mode == 0x01) {
                            plot_pixel((u16)(x0 + px), (u16)(y0 + row),
                                       LCD_FRAME_A, colour);
                        } else if (mode == 0x02) {
                            plot_pixel_back((u16)(x0 + px), (u16)(y0 + row),
                                            colour);
                        }
                    }
                    mask = (u8)(mask >> 1);
                    px = (u16)(px + 1);
                }
                p++;
            }
        }
        return;
    }

    if (rec[0] != 0x02) return;

    link_counters_clear();
    REG16(0x0011F2E4UL) = (u16)REG8(p); p++;
    REG16(0x0011F2E8UL) = (u16)REG8(p); p++;
    REG16(0x0011F2E6UL) = (u16)REG8(p); p++;
    REG16(0x0011F2EAUL) = (u16)REG8(p); p++;
    REG16(0x0011F2EEUL) = (u16)REG8(p); p++;
    REG16(0x0011F2F2UL) = (u16)REG8(p); p++;
    REG16(0x0011F2F0UL) = (u16)REG8(p); p++;
    REG16(0x0011F2F4UL) = (u16)REG8(p);

    if (mode == 0x01)      module_frame_front(colour);
    else if (mode == 0x02) module_frame_back(colour);
}

/* H'2403D2. The stitch stream walked and drawn.
 *
 * [which] of H'FF is the whole of it: the buffer is blacked first, every
 * block but [skip] is drawn, and the area is then dithered over with black on
 * every other pixel -- which is what greys the part of the hoop the design
 * does not reach. Any other [which] is that one block on its own, with
 * nothing blacked and no dither.
 *
 * A record the stream cannot give up stops the walk rather than skipping it.
 */
void module_stitches_walk(u8 which, u8 colour, u8 mode, u8 skip)
{
    u8 rec[0x18];
    u8 k;
    u16 y;
    u8 odd;

    if (which != 0xFF) {
        if (pattern_record_at(rec, which) == 0) return;
        module_block_draw(rec, colour, mode);
        return;
    }

    if (mode == 0x01)      module_area_clear_front();
    else if (mode == 0x02) module_area_clear_back();

    for (k = 1; k <= REG8(0x0011A640UL); k++) {
        if (skip == k) continue;
        if (pattern_record_at(rec, k) == 0) return;
        module_block_draw(rec, colour, mode);
    }

    odd = 0x00;
    for (y = 0x0055; y < 0x00E8; y++) {
        u16 x;

        odd = (u8)(odd == 0x01 ? 0x00 : 0x01);
        for (x = 0x0028; x < 0x00BF; x = (u16)(x + 2)) {
            if (mode == 0x01) {
                plot_pixel((u16)(x + odd), y, LCD_FRAME_A, 0x00);
            } else if (mode == 0x02) {
                plot_pixel_back((u16)(x + odd), y, 0x00);
            }
        }
    }
}

/* H'2403A2. The whole stream drawn into the back buffer, and the design's
 * corners worked out to go with it. The slot's own block is the one left
 * out. */
void module_stitches_draw(void)
{
    if (REG8(0x0011A640UL) == 0) return;

    module_area_clear_back();
    module_stitches_walk(0xFF, 0x02, 0x02, (u8)(REG8(0x0011A660UL) + 1));
    module_design_corners();
}

/* H'2480D6. What the module's screens do on a pass that is not a press.
 *
 * It is held off two ways. H'11F5A0 counts H'7D passes down, and only when it
 * reaches nought is there anything to do -- unless H'11A177 says the screen
 * has just changed with nothing else pending, which puts it through at once.
 * H'114DB9, the module reporting, stops it altogether.
 *
 * Then H'114D8E picks one of twelve. States nought, one, ten and eleven do
 * nothing; the rest all begin by looking at H'114D86, which is the flag that
 * says a message is still going out, and leave the counters alone when it is
 * up so that the same pass is tried again.
 */
void module_screen_tick(void)
{
    const u32 s16 = (u32)(long)(short)(u16)(
        (u16)((u16)REG8(0x0011A660UL) << 4));

    if (REG8(0x00114DB9UL) != 0) {
        REG8(0x0011F304UL) = 0x00;
        REG8(0x0011A177UL) = 0x00;
        REG8(0x0011F305UL) = 0x00;
        return;
    }

    if (REG8(0x0011F304UL) == 0 && REG8(0x0011A177UL) == 0) {
        REG8(0x0011F5A0UL) = 0x7D;
    }

    if (!(REG8(0x0011A177UL) != 0 && REG8(0x0011B0A8UL) == 0)) {
        if (REG8(0x0011F304UL) == 0) return;
    }

    if (REG8(0x0011F5A0UL) != 0) {
        REG8(0x0011F5A0UL) = (u8)(REG8(0x0011F5A0UL) - 1);
        return;
    }

    if (REG8(0x0011A177UL) != 0) REG8(0x0011F304UL) = 0x00;
    REG8(0x0011F5A0UL) = 0x7D;
    REG8(0x001040B5UL) = 0x00;

    switch (REG8(0x00114D8EUL)) {
    case 0x02:
    case 0x03:
        if (REG8(0x00114D86UL) != 0) return;
        if (REG8(0x00114D88UL) != 0) {
            REG8(0x00114D7EUL) = 0x01;
            REG8(0x00114DADUL) = 0x01;
        }
        break;

    case 0x04:
        if (REG8(0x00114D86UL) != 0) return;
        box5_draw(REG8(0x00114D96UL));
        label_percent();
        pause_button_draw(REG8(0x0011F30EUL));
        module_link_wake();
        REG8(0x00114D62UL) = 0x0A;
        REG8(0x00114D66UL) = 0x01;
        REG8(0x00114D50UL) = (u8)(REG8(0x00114D50UL) & (u8)~0x01);
        REG8(0x00114D50UL) = (u8)(REG8(0x00114D50UL) & (u8)~0x02);
        region_copy(0x0026, 0x002C, 0x00EA, 0x00EA, 0x002C,
                    MOD_SAVE, LCD_FRAME_A);
        module_colour_bitmap(REG8(0x00114D89UL));
        label_colours();
        module_size_labels((short)REG16(0x0011F4E0UL),
                           (short)REG16(0x0011F4E2UL));
        label_minutes((short)REG16(0x0011F4E4UL));
        REG8(0x0011F4E6UL) = 0x00;
        module_boxA_grey();
        break;

    case 0x05:
        if (REG8(0x00114D86UL) != 0) return;
        module_link_wake();
        region_copy(0x0026, 0x002C, 0x00EA, 0x00EA, 0x002C,
                    MOD_SAVE, LCD_FRAME_A);
        module_colour_bitmap(REG8(0x00114D89UL));
        module_size_labels((short)REG16(0x0011F4E0UL),
                           (short)REG16(0x0011F4E2UL));
        label_minutes((short)REG16(0x0011F4E4UL));
        REG8(0x00114D98UL) = 0x01;
        break;

    case 0x06:
        if (REG8(0x00114D86UL) != 0) return;
        module_link_wake();
        if (REG8(0x00114D88UL) != 0) module_grid_draw();
        break;

    case 0x07:
        if (REG8(0x00114D86UL) != 0) return;
        module_colours_show();
        module_box34_pick(0x01);
        module_box34_pick(0x00);
        if (REG8(0x0011F304UL) != 0) module_area_restore();
        if (REG8(0x001040B0UL) != 0) {
            hitbox_paint((u16)((u16)REG8(0x001040B0UL) + 4), 0x01);
        }
        module_stitches_draw();
        if (REG8(0x00114D88UL) != 0 && REG8(0x0011F304UL) == 0) {
            REG8(0x00114D50UL) = (u8)(REG8(0x00114D50UL) | 0x10);
            module_design_outline();
        } else {
            module_hoop_outline();
        }
        module_area_save();
        if (REG8(0x00114D51UL) & 0x02) {
            if (REG8(0x001040B1UL) == 0x01) module_box34_pick(0x00);
            else                            module_box34_pick(0x01);
        }
        if (REG8(0x001040B9UL) != 0) {
            hitbox_repress((u8)(REG8(0x001040B9UL) + 4));
            module_strip_press((u8)(REG8(0x001040B9UL) + 4));
            module_colours_show();
            module_start_step(0x09);
        }
        module_box3_grey();
        break;

    case 0x08:
        if (REG8(0x00114D86UL) != 0) return;
        module_box4_press(REG8(0x0011A25FUL + s16));
        module_hoop_pictures();
        module_stitches_draw();
        if (REG8(0x00114D88UL) != 0) {
            REG8(0x00114D50UL) = (u8)(REG8(0x00114D50UL) | 0x10);
            module_design_outline();
        } else {
            module_hoop_outline();
        }
        module_size_show(REG16(0x0011F4E0UL), REG16(0x0011F4E2UL));
        break;

    case 0x09:
        if (REG8(0x00114D86UL) != 0) return;
        module_hoop_pictures();
        module_stitches_draw();
        if (REG8(0x00114D88UL) != 0) {
            REG8(0x00114D50UL) = (u8)(REG8(0x00114D50UL) | 0x10);
            module_design_outline();
        } else {
            module_hoop_outline();
        }
        module_scale_show(REG16(0x0011F4E0UL), REG16(0x0011F4E2UL));
        break;

    default:
        break;
    }

    REG8(0x0011F304UL) = 0x00;
    REG8(0x0011A177UL) = 0x00;
    REG8(0x0011F305UL) = 0x00;
}

/* The slot's own block index, re-read every time it is wanted: the routines
 * that clear the slate move H'11A660 on, so a copy taken at the top would be
 * the wrong block by the time it is used. */
static u32 module_slot12(void)
{
    return (u32)(long)(short)(u16)(0x0012 * (u16)REG8(0x0011A660UL));
}

/* H'23C6FE. The twelve steps that set a design going, walked by H'1040B8.
 *
 * Step nought is the one that decides whether it can start at all: it sends
 * message H'0B, works out from H'1040B2 which of five things to do, and only
 * H'08 goes on to the rest. Step one puts the machine on the screen the
 * answer to [which] calls for, and steps two to ten each send one message and
 * count on. Step eleven leaves the machine sewing.
 *
 * Every step but nought and one waits for the link before it sends, which is
 * why the cases only reach the ways this turns back.
 */
u8 module_sew_step(u8 which)
{
    if (REG8(0x001040B8UL) > 0x0B) {
        if (module_link_quiet() != 0) REG8(0x001040B8UL) = 0x00;
        return 0x01;
    }

    switch (REG8(0x001040B8UL)) {
    case 0x00:
        REG8(0x00114D8EUL) = 0x00;
        REG8(0x001040B5UL) = module_send_0B();

        if (REG8(0x00114D51UL) & 0x40) {
            if (REG8(0x001040B9UL) != 0) {
                while (module_link_quiet() == 0) rom_host_service();
                module_slots_clear();
                REG8(0x0011F2A1UL) = 0x07;
                REG8(0x00114D51UL) = (u8)(REG8(0x00114D51UL) & (u8)~0x40);
                link_send_start();
                while (module_link_quiet() == 0) rom_host_service();
            } else {
                if (REG8(0x0011A63CUL) & 0x04) {
                    if ((REG8(0x00114D51UL) & 0x01) &&
                        (u8)(REG8(0x00114D53UL) & 0xE0) == 0x40) {
                        return 0x00;
                    }
                    REG8(0x001040B2UL) = 0x10;
                }
                if (REG8(0x0011A63CUL) & 0x08) return 0x00;
            }
        }

        if (REG8(0x001040B9UL) != 0) REG8(0x001040B2UL) = 0x08;
        if (REG8(0x001040B9UL) == 0) {
            const u8 v = REG8(0x001040BAUL);

            if (v >= 0x01 && v <= 0x03) REG8(0x001040B2UL) = 0x10;
            if (REG8(0x00114D9FUL) != 0) REG8(0x001040B2UL) = 0x10;
        }

        if (REG8(0x001040B2UL) == 0x01) return 0x00;
        if (REG8(0x001040B2UL) == 0x02) return 0x00;
        if (REG8(0x001040B2UL) == 0x04) {
            module_buffers_clear();
            module_go_settings();
            return 0x01;
        }
        if (REG8(0x001040B2UL) == 0x08) {
            REG8(0x0011F568UL) = which;
            REG8(0x0011F4E6UL) = 0x00;
            REG8(0x001040BBUL) = 0x01;
            REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
            return 0x01;
        }
        if (REG8(0x001040B2UL) == 0x10) {
            module_buffers_clear();
            module_go_settings();
            return 0x01;
        }
        break;

    case 0x01: {
        u32 keep[3];
        u8 n;

        for (n = 0; n < 3; n++) keep[n] = REG32(0x0011A61EUL + 4u * n);
        module_buffers_clear();
        if (REG8(0x0011F568UL) == 0x01) module_to_screen_15();
        else                            module_go_sewing();
        for (n = 0; n < 3; n++) REG32(0x0011A61EUL + 4u * n) = keep[n];

        if ((REG8(0x00114D51UL) & 0x02) && REG8(0x001040B1UL) != 0x01) {
            REG8(0x00114DA1UL) = 0x00;
            REG8(0x0011A41DUL + module_slot12()) = 0x00;
        } else {
            REG8(0x00114DA1UL) = 0x01;
            REG8(0x0011A41DUL + module_slot12()) = 0x01;
        }
        REG8(0x0011A41FUL + module_slot12()) = 0x00;

        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;
    }

    case 0x02:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x01;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x03:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x02;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x04:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61BUL) = 0x08;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x05:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x01;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x06:
        if (module_link_quiet() == 0) break;
        REG8(0x00114DBCUL) = (u8)(REG8(0x000FFE80UL) - REG8(0x00100255UL));
        REG8(0x00114DBDUL) = REG8(0x000FFE80UL);

        if (REG8(0x001040B9UL) == 0) {
            REG8(0x0011A41AUL + module_slot12()) = REG8(0x000FFE80UL);
        } else {
            REG8(0x0011A41AUL + module_slot12()) = REG8(0x001040B9UL);
            if (REG8(0x0011A41DUL + module_slot12()) == 0) {
                REG8(0x0011A41AUL + module_slot12()) =
                    (u8)(REG8(0x0011A41AUL + module_slot12()) + REG8(0x00114DBCUL));
            }
        }

        while (REG8(0x0011A41AUL + module_slot12()) >= 0x1C) {
            REG8(0x0011A41AUL + module_slot12()) = (u8)(REG8(0x0011A41AUL + module_slot12()) - 0x1B);
            REG8(0x0011A41FUL + module_slot12()) = (u8)(REG8(0x0011A41FUL + module_slot12()) + 1);
        }

        REG16(0x0011F292UL) = 0x0000;
        REG8(0x00114D73UL) = 0x01;
        REG8(0x0011F2A1UL) = 0x01;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x07:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61BUL) = 0x04;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x08:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x02;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x09:
        if (module_link_quiet() == 0) break;
        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x01;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    case 0x0A:
        if (module_link_quiet() == 0) break;
        REG8(0x00114D8DUL) =
            REG8(0x000FFE9CUL + (u32)REG8(0x0011A41AUL + module_slot12()));
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x01;
        link_send_start();
        REG8(0x001040B8UL) = (u8)(REG8(0x001040B8UL) + 1);
        break;

    default:
        if (module_link_quiet() == 0) break;
        if (REG8(0x0011F568UL) == 0x01) {
            REG8(0x00114D93UL) = 0x00;
            REG8(0x00114D92UL) = 0xFF;
            if (REG8(0x001040B9UL) == 0) REG8(0x0011F4E6UL) = 0x01;
            REG8(0x00114D72UL) = 0x01;
        } else {
            REG8(0x00114D93UL) = 0x01;
            REG8(0x00114D92UL) = 0xFF;
            REG8(0x00114D65UL) = 0x00;
            REG8(0x00114D66UL) = 0x01;
            module_boxA_grey();
        }
        REG8(0x001040B8UL) = 0x00;
        module_panel_blink(0x00);
        return 0x01;
    }

    module_panel_blink(0x01);
    return 0x01;
}

/* H'23D150. The run through one colour, seven steps of it.
 *
 * H'1040B3 says which step, and the whole walk is one call: each step ends
 * by servicing the host and blinking the panel, and then, while the step
 * number is still under seven, the walk goes round again. Only the eighth
 * step ends the call the ordinary way -- every other way out is a turn
 * back near the top, so those are the ways the cases can reach.
 *
 * Steps three to six carry a counter of their own, E3 in the original,
 * which step two puts to nought. It marks the hitbox for the colour in
 * hand at ten and unmarks it at twenty, so the box winks while the wait
 * goes on. A call that starts at step three or later finds the counter
 * holding whatever the caller left in the register; nothing reaches those
 * steps without going through step two first.
 *
 * Two things step six does cannot be seen from outside it. The bar and
 * the percentage are drawn and then painted over by the slots being
 * cleared and the colours shown again, so the sum that works the
 * percentage out is only pinned down where H'23D66A draws it last. And
 * the store into H'1040B9 is dead: the colour check on the next line
 * writes the same byte itself.
 */
void module_colour_run(void)
{
    u16 tick = 0;                    /* E3, put to nought by step two */

    if (REG8(0x00114D51UL) & 0x02) {
        REG8(0x0011F56FUL) = (REG8(0x001040B1UL) == 0x01)
                                 ? REG8(0x001040AEUL)
                                 : REG8(0x001040AFUL);
    } else {
        REG8(0x0011F56FUL) = REG8(0x001040AEUL);
        if (!(REG8(0x00114D51UL) & 0x01)) return;
    }

    if ((REG8(0x00114D55UL) & 0x04) && REG8(0x001040B1UL) == 0x04) {
        (void)link_claim(0x21);
        return;
    }
    if (REG8(0x0011A41AUL) == 0x00) return;

    for (;;) {
        const u8 st = REG8(0x001040B3UL);

        if (st == 0x00) {
            if (module_link_quiet()) {
                REG16(0x00114D4CUL) =
                    (u16)(REG16(0x00114D4CUL) & (u16)~0x4000);
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A61BUL) = 0x09;
                link_send_start();
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x01) {
            if (module_link_quiet()) {
                link_delay(0x0032);
                if (REG16(0x00114D4CUL) & 0x4000) {
                    REG8(0x001040B3UL) = 0x00;
                    module_panel_blink(0x00);
                    return;
                }
                if (REG8(0x00114D56UL) == 0x64 ||
                    REG8(0x0011F56FUL) >= 0x0F) {
                    REG8(0x001040B3UL) = 0x00;
                    module_panel_blink(0x00);
                    (void)link_claim(
                        (u8)(REG8(0x001040B1UL) == 0x01 ? 0x11 : 0x1F));
                    return;
                }
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x02) {
            if (module_link_quiet()) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A619UL) = 0x03;
                link_send_start();
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                tick = 0x0000;
            }
        } else if (st == 0x03) {
            if (tick == 0x000A)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x01);
            if (tick == 0x0014)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x00);
            if (tick == 0x0014) tick = 0x0000;
            tick = (u16)(tick + 1);

            if (module_link_quiet()) {
                const u8 which = REG8(0x001040B1UL);

                if (which == 0x01) {
                    REG8(0x0011A63CUL) |= 0x04;
                    REG8(0x0011A63CUL) &= (u8)~0x08;
                } else if (which == 0x04) {
                    REG8(0x0011A63CUL) &= (u8)~0x04;
                    REG8(0x0011A63CUL) |= 0x08;
                } else {
                    REG8(0x0011A63CUL) &= (u8)~0x08;
                    REG8(0x0011A63CUL) &= (u8)~0x04;
                }
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A61BUL) = 0x07;
                link_send_start();
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x04) {
            REG8(0x0011F534UL) = 0x00;
            REG8(0x00114D51UL) &= (u8)~0x40;

            if (tick == 0x000A)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x01);
            if (tick == 0x0014)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x00);
            if (tick == 0x0014) tick = 0x0000;
            tick = (u16)(tick + 1);

            if (module_link_quiet()) {
                module_start_step(0x0A);
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x05) {
            if (tick == 0x000A)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x01);
            if (tick == 0x0014)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x00);
            if (tick == 0x0014) tick = 0x0000;
            tick = (u16)(tick + 1);

            /* No wait on the link here: the colour's own walk is asked to
             * step on for as long as it has anywhere to go. */
            if (REG8(0x001040B6UL) != 0x00) module_start_step(0x05);
            else REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
        } else if (st == 0x06) {
            if (tick == 0x000A)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x01);
            if (tick == 0x0014)
                hitbox_paint((u16)(REG8(0x0011F56FUL) + 0x0005), 0x00);
            if (tick == 0x0014) tick = 0x0000;
            tick = (u16)(tick + 1);

            if (module_link_quiet()) {
                u8 pct;

                if (REG8(0x00114D51UL) & 0x02) {
                    if (REG8(0x001040B1UL) == 0x01)
                        REG8(0x001040AEUL) = (u8)(REG8(0x001040AEUL) + 1);
                    else
                        REG8(0x001040AFUL) = (u8)(REG8(0x001040AFUL) + 1);
                } else {
                    REG8(0x001040AEUL) = (u8)(REG8(0x001040AEUL) + 1);
                }
                REG8(0x0011F56FUL) = (u8)(REG8(0x0011F56FUL) + 1);
                module_colours_show();

                /* Fifteen colours make the whole of it, so the count is
                 * turned into a percentage the same way the bar wants it.
                 * The multiply is a byte one and the divide a signed word
                 * one, which is why the product is read back as a word. */
                pct = (u8)(short)((long)(short)(u16)
                          ((u16)REG8(0x0011F56FUL) * (u16)0x0064) /
                          (short)0x000F);

                module_progress_bar(
                    (u16)(REG8(0x00114D56UL) > pct ? REG8(0x00114D56UL)
                                                   : pct));
                label_percent_left(
                    (u8)(REG8(0x00114D56UL) > pct ? REG8(0x00114D56UL)
                                                  : pct));

                REG8(0x001040B0UL) = 0x00;
                REG8(0x001040B5UL) = 0x00;
                REG8(0x0011F534UL) = 0x00;
                module_slots_clear();

                REG8(0x001040B9UL) = REG8(0x0011F56FUL);
                (void)module_colour_check((u8)(REG8(0x0011F56FUL) + 0x04));
                module_strip_press((u8)(REG8(0x001040B9UL) + 0x04));
                module_colours_show();
                module_start_step(0x09);
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else {
            REG8(0x001040B3UL) = (u8)(st + 1);
        }

        rom_host_service();
        module_panel_blink(0x01);
        if (REG8(0x001040B3UL) < 0x07) continue;

        if (REG8(0x001040B2UL) == 0x01) {
            if (module_slot_is_plain() == 0x00) REG8(0x001040B2UL) = 0x08;
        } else {
            REG8(0x001040B2UL) = 0x08;
        }
        module_panel_blink(0x00);
        REG8(0x001040B3UL) = 0x00;
        link_line_release();
        return;
    }
}

/* H'23D66A. The colour taken back again, and the two walks that do it.
 *
 * H'114DAB picks between them. With it up the whole run is given up: nine
 * steps that put the counts, the boxes and the bar back to nothing and then
 * drop the flag. With it down only the last colour is taken back, and that
 * is eleven steps -- the same opening, then a walk down the row of boxes
 * unlighting them one at a time, with a count of H'7530 turns of the loop
 * between one box and the next so the row unwinds at a pace the eye can
 * follow.
 *
 * The short walk is the one no case can run through: its own step sends a
 * message and the step after it waits for the reply, which only the link's
 * own interrupt brings. The steps that can be reached are the turns back
 * near the top and the runs that reach the last step without sending.
 *
 * The counter the walk unwinds on is ER3 in the original, put to nought by
 * the step before it; a call that starts at the walk itself finds whatever
 * the caller left there.
 */
void module_colour_back(void)
{
    u32 spin = 0;                    /* ER3, put to nought by step five */
    u8  box;                         /* R4L */
    u8  pct;                         /* R4H */

    if (REG8(0x00114D51UL) & 0x02) {
        REG8(0x0011F570UL) = (REG8(0x001040B1UL) == 0x01)
                                 ? REG8(0x001040AEUL)
                                 : REG8(0x001040AFUL);
    } else {
        REG8(0x0011F570UL) = REG8(0x001040AEUL);
        if (!(REG8(0x00114D51UL) & 0x01)) return;
    }

    if (REG8(0x00114DABUL) != 0x00) {
        /* The whole run given up. */
        for (;;) {
            const u8 st = REG8(0x001040B3UL);

            if (st == 0x00) {
                if (module_link_quiet()) {
                    REG16(0x00114D4CUL) =
                        (u16)(REG16(0x00114D4CUL) & (u16)~0x4000);
                    REG8(0x0011F2A1UL) = 0x03;
                    REG8(0x0011A61BUL) = 0x09;
                    link_send_start();
                    REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                }
            } else if (st == 0x01) {
                if (module_link_quiet()) {
                    link_delay(0x0032);
                    if (REG16(0x00114D4CUL) & 0x4000) {
                        REG8(0x001040B3UL) = 0x00;
                        REG8(0x00114D84UL) = 0x00;
                        REG8(0x00114DABUL) = 0x00;
                        return;
                    }
                    REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                }
            } else if (st == 0x02) {
                if (module_fault_report(0x83) == 0x00) {
                    REG8(0x001040B3UL) = 0x00;
                    REG8(0x00114D84UL) = 0x00;
                    REG8(0x00114DABUL) = 0x00;
                    return;
                }
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            } else if (st == 0x03) {
                if (REG8(0x00114D8EUL) == 0x07 && REG8(0x0011A177UL) == 0x00) {
                    if (REG8(0x0011F532UL) == 0x00) {
                        REG8(0x001040B3UL) = 0x00;
                        REG8(0x00114D84UL) = 0x00;
                        REG8(0x00114DABUL) = 0x00;
                        return;
                    }
                    REG8(0x0011F304UL) = 0x01;
                    REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                }
                key_and_diag();
                screen_dispatch();
                service_tick();
            } else if (st == 0x04) {
                module_screen_tick();
                key_and_diag();
                screen_dispatch();
                service_tick();
                if (REG8(0x0011F304UL) == 0x00)
                    REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            } else if (st == 0x05) {
                if (REG8(0x0011F29EUL) == 0x00 && REG8(0x0011F2B6UL) == 0x00) {
                    REG8(0x0011F2A1UL) = 0x03;
                    REG8(0x0011A61BUL) = 0x01;
                    link_send_start();
                    REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                }
            } else if (st == 0x06) {
                if (REG8(0x0011F29EUL) == 0x00 && REG8(0x0011F2B6UL) == 0x00) {
                    REG8(0x001040B9UL) = 0x00;
                    for (box = 0x05; box <= 0x13; box = (u8)(box + 1))
                        hitbox_paint((u16)box, 0x00);
                    REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);

                    if (REG8(0x00114D51UL) & 0x02) {
                        if (REG8(0x001040B1UL) == 0x01)
                            REG8(0x001040AEUL) = 0x00;
                        else
                            REG8(0x001040AFUL) = 0x00;
                    } else {
                        REG8(0x001040AEUL) = 0x00;
                    }
                    REG8(0x0011F570UL) = 0x00;
                    REG8(0x001040B0UL) = 0x00;
                }
            } else if (st == 0x07) {
                if (module_link_quiet()) {
                    pct = (u8)(short)((long)(short)(u16)
                              ((u16)REG8(0x0011F570UL) * (u16)0x0064) /
                              (short)0x000F);
                    module_progress_bar((u16)pct);
                    label_percent_left(pct);
                    if (REG8(0x001040B5UL) != 0x00) REG8(0x0011F304UL) = 0x01;
                    REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                }
            }

            module_panel_blink(0x01);
            rom_host_service();
            if (REG8(0x001040B3UL) < 0x08) continue;

            REG8(0x001040B3UL) = 0x00;
            module_panel_blink(0x00);
            REG8(0x00114D84UL) = 0x00;
            REG8(0x00114DABUL) = 0x00;
            link_line_release();
            REG8(0x001040B2UL) = 0x04;
            return;
        }
    }

    /* Only the last colour taken back. */
    if (REG8(0x0011F570UL) == 0x00) return;
    if (REG8(0x001040B0UL) == 0x00) return;
    if (REG8(0x001040B0UL) > REG8(0x001040AEUL)) return;

    for (;;) {
        const u8 st = REG8(0x001040B3UL);

        if (st == 0x00) {
            if (module_link_quiet()) {
                REG16(0x00114D4CUL) =
                    (u16)(REG16(0x00114D4CUL) & (u16)~0x4000);
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A61BUL) = 0x09;
                link_send_start();
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x01) {
            if (module_link_quiet()) {
                link_delay(0x0032);
                if (REG16(0x00114D4CUL) & 0x4000) {
                    REG8(0x001040B3UL) = 0x00;
                    return;
                }
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x02) {
            if (module_fault_report(0x83) == 0x00) {
                REG8(0x001040B3UL) = 0x00;
                return;
            }
            REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
        } else if (st == 0x03) {
            if (REG8(0x00114D8EUL) == 0x07 && REG8(0x0011A177UL) == 0x00) {
                if (REG8(0x0011F532UL) == 0x00) {
                    REG8(0x001040B3UL) = 0x00;
                    return;
                }
                REG8(0x0011F304UL) = 0x01;
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
            key_and_diag();
            screen_dispatch();
            service_tick();
        } else if (st == 0x04) {
            module_screen_tick();
            key_and_diag();
            screen_dispatch();
            service_tick();
            if (REG8(0x0011F304UL) == 0x00)
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
        } else if (st == 0x05) {
            if (REG8(0x0011F29EUL) == 0x00 && REG8(0x0011F2B6UL) == 0x00) {
                REG8(0x0011F2A1UL) = 0x05;
                REG8(0x0011F2A2UL) = 0x0C;
                link_send_start();
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                spin = 0;
            }
        } else if (st == 0x06) {
            if (REG8(0x0011F29EUL) == 0x00 && REG8(0x0011F2B6UL) == 0x00) {
                box = (u8)(REG8(0x001040B0UL) + 0x04);
                for (;;) {
                    const u16 here = (u16)box;
                    const u16 want = (u16)(REG8(0x0011F570UL) + 0x0004);

                    if ((short)want < (short)here) break;
                    if (box > 0x13) {
                        hitbox_paint((u16)(here - 1), 0x01);
                        break;
                    }
                    if (spin == 0x00007530UL) {
                        if ((u16)(REG8(0x001040B0UL) + 0x0004) == here) {
                            hitbox_paint(here, 0x00);
                        } else {
                            hitbox_paint((u16)(here - 1), 0x01);
                            hitbox_paint(here, 0x00);
                        }
                        box = (u8)(box + 1);
                        spin = 0;
                    }
                    spin++;
                }

                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
                if (REG8(0x00114D51UL) & 0x02) {
                    if (REG8(0x001040B1UL) == 0x01)
                        REG8(0x001040AEUL) = (u8)(REG8(0x001040AEUL) - 1);
                    else
                        REG8(0x001040AFUL) = (u8)(REG8(0x001040AFUL) - 1);
                } else {
                    REG8(0x001040AEUL) = (u8)(REG8(0x001040AEUL) - 1);
                }
                REG8(0x0011F570UL) = (u8)(REG8(0x0011F570UL) - 1);
                REG8(0x001040B0UL) = 0x00;
            }
        } else if (st == 0x07) {
            if (REG8(0x0011F29EUL) == 0x00 && REG8(0x0011F2B6UL) == 0x00) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A61BUL) = 0x02;
                link_send_start();
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x08) {
            if (module_link_quiet()) {
                REG8(0x001040B9UL) = 0x00;
                module_start_step(0x0A);
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        } else if (st == 0x09) {
            if (REG8(0x001040B6UL) != 0x00) module_start_step(0x05);
            else REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
        } else if (st == 0x0A) {
            if (module_link_quiet()) {
                pct = (u8)(short)((long)(short)(u16)
                          ((u16)REG8(0x0011F570UL) * (u16)0x0064) /
                          (short)0x000F);
                module_progress_bar(
                    (u16)(REG8(0x00114D56UL) > pct ? REG8(0x00114D56UL)
                                                   : pct));
                label_percent_left(
                    (u8)(REG8(0x00114D56UL) > pct ? REG8(0x00114D56UL)
                                                  : pct));
                REG8(0x001040B2UL) = 0x04;
                if (REG8(0x001040B5UL) != 0x00) REG8(0x0011F304UL) = 0x01;
                REG8(0x001040B3UL) = (u8)(REG8(0x001040B3UL) + 1);
            }
        }

        rom_host_service();
        module_panel_blink(0x01);
        if (REG8(0x001040B3UL) < 0x0B) continue;

        REG8(0x001040B3UL) = 0x00;
        module_panel_blink(0x00);
        link_line_release();
        return;
    }
}

/* H'23078A. Screen H'37's press: the sewing screen's own keys.
 *
 * H'114D8E is put to seven before anything else, so that whatever the module
 * has to say about itself is said about this screen; nothing at all happens
 * while it is still busy.
 *
 * Keys H'01 and H'15 both take the whole run away -- one to the stitch
 * screen, one to the plain sewing screen -- and both stop first at the two
 * reports that can take the screen away themselves, then at the step that
 * starts the sewing and the one that fetches the run. Key H'02 runs a
 * colour and H'14 takes one back. H'03 and H'04 measure the two corners of
 * the hoop. Everything from H'05 to H'13 is one of the colour boxes along
 * the strip, and pressing one asks for that colour.
 */
void module_sew_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0015, &value, &index) != 0x03) return;

    REG8(0x00114D8EUL) = 0x07;
    if (module_busy() != 0) return;

    if ((u16)(value - 1) > 0x0014) return;

    switch (value) {
    case 0x0001:
        message_show_held(index);
        REG8(0x00114D8FUL) = 0x08;
        if (module_fault_report(0x80) != 0) return;
        if (module_fault_report(0x82) != 0) return;
        if (module_sew_step((u8)value) != 0) return;
        if (module_run_fetch(value) != 0) return;
        embroidery_panel_save();
        REG8(0x00114D72UL) = 0x01;
        screen_switch(0x15, 0x01, 0x00);
        return;

    case 0x0002:
        message_show_held(index);
        module_colour_run();
        return;

    case 0x0003:
        module_measure_second();
        return;

    case 0x0004:
        module_measure_first();
        return;

    case 0x0014:
        message_show_held(index);
        module_colour_back();
        return;

    case 0x0015:
        message_show_held(index);
        REG8(0x00114D8FUL) = 0x04;
        if (module_fault_report(0x80) != 0) return;
        if (module_fault_report(0x82) != 0) return;
        if (module_sew_step((u8)value) != 0) return;
        if (module_run_fetch(value) != 0) return;
        if (module_link_quiet() == 0) return;
        embroidery_panel_save();
        screen_switch(0x23, 0x01, 0x00);
        REG8(0x00114D66UL) = 0x01;
        REG8(0x00114D62UL) = 0x00;
        REG8(0x00114D65UL) = 0x00;
        REG8(0x00114D72UL) = 0x03;
        REG8(0x00114D89UL) = 0x00;
        REG8(0x00114D97UL) = 0x01;
        return;

    default:
        if (module_colour_check((u8)value) != 0) return;
        module_lit_box(index);
        module_colours_show();
        module_start_step(0x09);
        return;
    }
}


/* H'2465E0. The bare half of H'244D64's question: whether the hardware state
 * at H'FFFEC0 is one of the two that will take a message. No claim on the
 * link and nothing else looked at -- the screens that only want to know
 * whether the machine is running call this one. */
u8 module_machine_running(void)
{
    const u8 state = REG8(0x00FFFEC0UL);

    if (state == 0x04 || state == 0x06) return 0x01;
    return 0x00;
}

/* H'230110. Screen H'24's press: the design turned and mirrored.
 *
 * Three boxes. H'01 and H'02 send the module the same message with one bit
 * of H'11A618 between them -- the turn is bit 7 -- and light the matching
 * arrow. H'19 is the way out, to screen H'23.
 *
 * H'114DB3 counts the presses and H'114DB4 says which of the two codes goes
 * into H'11A618 before the bit is put on it.
 *
 * Both arms finish by masking H'11A618 -- with H'3F in one and H'BF in the
 * other -- and neither mask can do anything: the line above has just put
 * H'0A or H'01 into the byte, and neither has bit 6 or bit 7. The masks are
 * written out because the original writes them out.
 */
void module_turn_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0003, &value, &index) != 0x03) return;
    message_show_held(index);

    REG8(0x00114D8EUL) = 0x05;
    if (module_busy() != 0) return;

    if (value == 0x0001) {
        if (module_machine_running() == 0) return;
        if (module_link_quiet() == 0) return;

        REG8(0x00114DB3UL) = (u8)(REG8(0x00114DB3UL) + 1);
        REG8(0x0011A618UL) = (u8)(REG8(0x00114DB4UL) != 0 ? 0x0A : 0x01);
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x01;
        REG8(0x0011A618UL) = (u8)(REG8(0x0011A618UL) & 0x3F);
        link_send_start();
        REG8(0x00114D98UL) = 0x01;
        module_arrow_fwd_2(0x01);
        return;
    }

    if (value == 0x0002) {
        if (module_machine_running() == 0) return;
        if (module_link_quiet() == 0) return;

        REG8(0x00114DB3UL) = (u8)(REG8(0x00114DB3UL) + 1);
        REG8(0x0011A618UL) = (u8)(REG8(0x00114DB4UL) != 0 ? 0x0A : 0x01);
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x01;
        REG8(0x0011A618UL) =
            (u8)((u8)(REG8(0x0011A618UL) | 0x80) & 0xBF);
        link_send_start();
        REG8(0x00114D98UL) = 0x01;
        module_arrow_back_1(0x01);
        return;
    }

    if (value == 0x0019) {
        if (REG8(0x00114D72UL) != 0) return;
        if (module_link_quiet() == 0) return;

        REG8(0x00114D72UL) = 0x05;
        REG8(0x00114D66UL) = 0x01;
        REG8(0x00114D62UL) = 0x07;
        REG8(0x00114D65UL) = 0x00;
        REG8(0x00114D98UL) = 0x00;
        screen_switch(0x23, 0x01, 0x00);
        return;
    }
}


/* H'23E464. Whether box ten is one the panel offers: bit 0 of H'11F538, the
 * same bit H'2323F0 reads before it greys the box. */
u8 module_box10_live(void)
{
    if (REG8(0x0011F538UL) & 0x01) return 0x01;
    return 0x00;
}

/* H'239FAA. The colour picture rubbed out with a checkerboard: every other
 * pixel of the box from H'CB,H'12 to H'E1,H'22 put to black in the front
 * buffer, the offset moving one across from each row to the next. The first
 * row drawn is an odd one -- the toggle runs before the row does. */
void module_colours_dither(void)
{
    u16 y;
    u8  odd = 0x00;

    for (y = 0x0012; y < 0x0023; y = (u16)(y + 1)) {
        u16 x;

        odd = (u8)(odd == 0x01 ? 0x00 : 0x01);
        for (x = 0x00CB; x < 0x00E2; x = (u16)(x + 2)) {
            plot_pixel((u16)(x + odd), y, LCD_FRAME_A, 0x00);
        }
    }
}

/* H'2385A6. The two colour-step asks, held in bits 0 and 1 of H'114D4F.
 *
 * Bit 0 steps the colour on, but only while the count is under H'3C and the
 * design has another colour to step to -- H'114D8D holds how many there are,
 * and the last one is not a colour. Bit 1 steps back, and rubs the colour
 * picture out on the way. Either ask, with H'114D8E at four, lights its own
 * arrow, and either bit is put down once it has been served whether or not
 * the step itself happened.
 */
void module_colour_step_service(void)
{
    if (REG8(0x00114D4FUL) & 0x01) {
        if (REG8(0x00114D89UL) < 0x3C) {
            const short have = (short)(u16)((u16)REG8(0x00114D8DUL) - 1);
            const short at   = (short)(u16)REG8(0x00114D89UL);

            if (have > at) {
                REG8(0x00114D89UL) = (u8)(at + 1);
                label_colours_picture();
                REG16(0x0011F4DCUL) = 0x0000;
                REG16(0x0011F4DEUL) = 0x0000;
            }
        }
        if (REG8(0x00114D8EUL) == 0x04) module_arrow_back_2(0x01);
        REG8(0x00114D4FUL) &= (u8)~0x01;
    }

    if (REG8(0x00114D4FUL) & 0x02) {
        if (REG8(0x00114D89UL) != 0) {
            REG8(0x00114D89UL) = (u8)(REG8(0x00114D89UL) - 1);
            label_colours_picture();
            module_colours_dither();
            REG16(0x0011F4DCUL) = 0x0000;
            REG16(0x0011F4DEUL) = 0x0000;
        }
        if (REG8(0x00114D8EUL) == 0x04) module_arrow_fwd_3(0x01);
        REG8(0x00114D4FUL) &= (u8)~0x02;
    }
}

/* Two of screen H'23's arms are also the tails of two others: box six falls
 * into box seven's body when the link is busy, and box eight into box ten's,
 * which is the same shared-tail trick the jump table itself uses. */
static void module_panel_pause(void)
{
    REG8(0x00114D7DUL) = 0x00;
    REG8(0x00114D93UL) = 0x01;
    if (module_home_request() != 0) return;
    module_pause_toggle();
}

static void module_panel_box10(u16 index)
{
    REG8(0x00114D93UL) = 0x01;
    if (module_home_request() != 0) return;
    if (module_box10_live() != 0) screen_switch(0x4E, 0x01, 0x00);
    message_show_held(index);
}

/* H'22F962. Screen H'23's press: the module's sewing panel, eleven boxes.
 *
 * H'01 starts the sewing, and is the only arm that asks the two reports
 * whether the screen should be taken away instead. H'02 and H'03 step the
 * colour on and back -- the same message with bit 6 of H'11A618 between
 * them. H'04 goes to the turning screen, H'05 toggles the needle, H'06 and
 * H'08 are the two ways out, H'07 pauses, H'0A goes to screen H'4E, and
 * H'09 and H'0B walk the three speed bytes at H'114DBE down and up.
 *
 * Unlike the other presses in this cluster the held message is shown inside
 * each arm and only while the link is quiet, so an arm reached with a
 * message going out draws nothing.
 *
 * Two of the eleven arms have tails no press can reach. Box six falls into
 * box seven's body, and box eight into box ten's, when H'FFFEC6 is nought
 * and the link is busy -- but that is one of the things H'24610A calls busy
 * on screen four, so the routine has already turned back before the arm is
 * entered. Both tails are here because the original has them.
 *
 * The three clamps on the speed bytes are written the way the original
 * writes them: H'114DBF is put to H'38 when it is under H'38 and H'114DC0 to
 * H'C8 when it is over H'C8, so at the boundary itself the clamp writes the
 * value that is already there. Widening either test by one is a mutation no
 * case can see.
 */
void module_panel_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x000B, &value, &index) != 0x03) return;

    REG8(0x00114D8EUL) = 0x04;
    if (module_busy() != 0) return;

    if ((u16)(value - 1) > 0x000A) return;

    if (value == 0x0001) {
        if (module_link_quiet()) message_show_held(index);

        if (REG8(0x00114DACUL) != 0) { module_link_lost(); return; }
        if (REG8(0x00114D9FUL) != 0 || REG8(0x00114D9CUL) != 0) {
            module_reset_walk();
            return;
        }

        REG8(0x00114D93UL) = 0x00;
        (void)module_home_request();
        if (module_fault_report(0x81) != 0) return;
        if (module_fault_report(0x84) != 0) return;

        REG8(0x00114D98UL) = 0x00;
        REG8(0x0011A41DUL + module_slot12()) = REG8(0x00114DA1UL);
        REG8(0x0011A41FUL + module_slot12()) =
            (u8)((u16)REG8(0x00114D8CUL) / 0x1B);
        REG8(0x0011F30EUL) = 0x00;

        if (REG8(0x00FFFEC6UL) == 0) {
            if (REG8(0x00114DA1UL) == 0x01) {
                REG8(0x00114D8EUL) = 0x03;
                screen_switch(0x14, 0x01, 0x00);
            } else {
                REG8(0x00114D8EUL) = 0x02;
                screen_switch(0x13, 0x01, 0x00);
            }
            REG8(0x00114D7EUL) = 0x01;
            if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        } else {
            REG8(0x00114D8AUL) = 0x02;
        }
        return;
    }

    if (value == 0x0002) {
        if (module_link_quiet()) message_show_held(index);

        if (module_machine_running() == 0) return;
        REG8(0x00114D7DUL) = 0x00;
        if (module_hoop_fits() == 0) return;
        if (REG8(0x0011F30EUL) != 0) { module_pause_toggle(); return; }

        module_colour_step_service();
        REG8(0x00114D93UL) = 0x01;
        if (module_home_request() != 0) return;

        if (module_link_quiet()) {
            REG8(0x0011A618UL) = 0x01;
            REG8(0x0011A61AUL) = 0x01;
            REG8(0x0011A618UL) =
                (u8)((u8)(REG8(0x0011A618UL) & 0x7F) | 0x40);
            link_send_start();
            (void)module_flash_step(0x04);
        }
        return;
    }

    if (value == 0x0003) {
        if (module_link_quiet()) message_show_held(index);

        if (module_machine_running() == 0) return;
        if (module_hoop_fits() == 0) return;
        if (REG8(0x0011F30EUL) != 0) { module_pause_toggle(); return; }

        module_colour_step_service();
        REG8(0x00114D93UL) = 0x01;
        if (module_home_request() != 0) return;

        if (module_link_quiet()) {
            REG8(0x0011A618UL) = 0x01;
            REG8(0x0011A61AUL) = 0x01;
            REG8(0x0011A618UL) = (u8)(REG8(0x0011A618UL) | 0xC0);
            link_send_start();
            (void)module_flash_step(0x04);
        }
        return;
    }

    if (value == 0x0004) {
        if (module_link_quiet()) message_show_held(index);

        REG8(0x00114D7DUL) = 0x00;
        if (module_hoop_fits() == 0) return;
        if (REG8(0x0011F30EUL) != 0) { module_pause_toggle(); return; }

        REG8(0x00114D93UL) = 0x00;
        (void)module_home_request();
        (void)module_flash_step(0x04);
        REG16(0x0011F4DCUL) = 0x0000;
        REG16(0x0011F4DEUL) = 0x0000;

        if (REG8(0x00FFFEC6UL) == 0) {
            REG8(0x00114D72UL) = 0x2D;
            screen_switch(0x24, 0x01, 0x00);
        } else {
            REG8(0x00114D8AUL) = 0x05;
        }
        return;
    }

    if (value == 0x0005) {
        REG8(0x00114D7DUL) = 0x00;
        REG8(0x00114D93UL) = 0x01;
        if (module_home_request() != 0) return;

        if (REG8(0x00114D96UL) == 0x01) REG8(0x00114D96UL) = 0x00;
        else                            REG8(0x00114D96UL) = 0x01;
        box5_draw(REG8(0x00114D96UL));
        return;
    }

    if (value == 0x0006) {
        if (module_link_quiet()) message_show_held(index);

        if (REG8(0x00114DACUL) != 0) return;

        REG8(0x00114D93UL) = 0x00;
        (void)module_home_request();
        REG8(0x00114D98UL) = 0x00;
        REG8(0x00114D8EUL) = 0x00;
        REG8(0x0011F30EUL) = 0x00;

        if (REG8(0x00FFFEC6UL) != 0) { REG8(0x00114D8AUL) = 0x01; return; }
        if (module_link_quiet()) {
            screen_switch(0x15, 0x01, 0x00);
            REG8(0x00114D72UL) = 0x01;
            return;
        }
        module_panel_pause();          /* the link busy: box seven's body */
        return;
    }

    if (value == 0x0007) {
        module_panel_pause();
        return;
    }

    if (value == 0x0008) {
        if (module_link_quiet()) message_show_held(index);

        if (REG8(0x00114DACUL) != 0) return;

        REG8(0x00114D93UL) = 0x00;
        (void)module_home_request();
        REG8(0x00114D98UL) = 0x00;
        REG8(0x00114D8EUL) = 0x00;
        REG8(0x0011F30EUL) = 0x00;

        if (REG8(0x00FFFEC6UL) != 0) { REG8(0x00114D8AUL) = 0x03; return; }
        if (module_link_quiet()) {
            screen_switch(0x16, 0x01, 0x00);
            REG8(0x00114D72UL) = 0x02;
            return;
        }
        module_panel_box10(index);     /* the link busy: box ten's body */
        return;
    }

    if (value == 0x0009) {
        REG8(0x00114D93UL) = 0x01;
        if (module_home_request() != 0) return;

        if (REG8(0x00114DBEUL) > 0x12) {
            REG8(0x00114DBEUL) = (u8)(REG8(0x00114DBEUL) + 0xF6);
            REG8(0x00114DBFUL) = (u8)(REG8(0x00114DBFUL) + 0xFB);
            REG8(0x00114DC0UL) = (u8)(REG8(0x00114DC0UL) + 0x19);
        }
        if (REG8(0x00114DBEUL) < 0x12) REG8(0x00114DBEUL) = 0x12;
        if (REG8(0x00114DBFUL) < 0x38) REG8(0x00114DBFUL) = 0x38;
        if (REG8(0x00114DC0UL) > 0xC8) REG8(0x00114DC0UL) = 0xC8;

        label_percent();
        message_show_held(index);
        return;
    }

    if (value == 0x000A) {
        module_panel_box10(index);
        return;
    }

    if (value == 0x000B) {
        REG8(0x00114D93UL) = 0x01;
        if (module_home_request() != 0) return;

        if (REG8(0x00114DBEUL) < 0x3E && REG8(0x00114DBFUL) < 0xFA &&
            REG8(0x00114DC0UL) > 0x19) {
            REG8(0x00114DBEUL) = (u8)(REG8(0x00114DBEUL) + 0x0A);
            REG8(0x00114DBFUL) = (u8)(REG8(0x00114DBFUL) + 0x05);
            REG8(0x00114DC0UL) = (u8)(REG8(0x00114DC0UL) + 0xE7);
        }

        label_percent();
        message_show_held(index);
        return;
    }
}

/* H'22F82A. Screen H'4E's press, and there is nothing under it.
 *
 * The hit test covers boxes one to H'17 and the jump table twenty-five
 * values, and every one of the twenty-five arms is the same two
 * instructions: the answer put to nought and a branch to the return. So the
 * whole of what this routine does is mark the screen state, show the held
 * message and ask whether the module is busy -- and even that question is
 * dead, because H'24610A answers "not busy" for every state past H'0B and
 * this one is H'10.
 *
 * It is written out because the table is really there: twenty-five entries
 * pointing at twenty-five distinct stubs, which is a screen someone laid out
 * and never filled in.
 *
 * Nothing after the busy question can be seen from outside, and mutation
 * testing says so: inverting that question, and widening the range test by
 * one, are the two mutations of seven that no case kills. Both of them only
 * gate arms that do nothing, so there is nothing for a case to see.
 */
void module_extra_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0017, &value, &index) != 0x03) return;

    REG8(0x00114D8EUL) = 0x10;
    message_show_held(index);
    if (module_busy() != 0) return;

    if ((u16)(value - 1) > 0x0018) return;

    /* Every arm of the table returns without doing anything. */
}

/* H'23A06A. The size the hoop is being held to, brought down to the slot's
 * own when the slot is the smaller.
 *
 * H'11F4D9 and H'11F4DA are the width and the height in hand, and the slot's
 * pair at H'11A25A sits beside them. Either of the slot's being under the one
 * in hand is enough; a slot whose word at H'11A266 is nought is not one that
 * can be measured and is left alone. What it sets is the module's own action
 * code H'0A, and it puts bit 3 of H'11F2A2 down -- the bit a hoop nudge puts
 * up, so this is the other half of that pair.
 */
void module_size_shrink(void)
{
    const u32 e = (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4));

    if (REG8(0x0011A25AUL + e) >= REG8(0x0011F4D9UL) &&
        REG8(0x0011A25BUL + e) >= REG8(0x0011F4DAUL)) {
        return;
    }
    if (REG16(0x0011A266UL + e) == 0x0000) return;

    REG8(0x0011A615UL) = 0x0A;
    REG8(0x0011F2A2UL) &= (u8)~0x08;
    REG8(0x0011F4D9UL) = REG8(0x0011A25AUL + e);
    REG8(0x0011F4DAUL) = REG8(0x0011A25BUL + e);
}

/* The five bytes H'245CE6 keeps its copy of, taken from the slot in hand. */
static void module_slot_snapshot(void)
{
    const u8  s = REG8(0x0011A660UL);
    const u32 e = (u32)(long)(short)(u16)((u16)((u16)s << 4));

    REG8(0x0011F310UL) = REG8(0x0011A25AUL + e);
    REG8(0x0011F311UL) = REG8(0x0011A25BUL + e);
    REG8(0x0011F312UL) = REG8(0x0011A25FUL + e);
    REG8(0x0011F313UL) = REG8(0x0011A260UL + e);
    REG8(0x0011F314UL) = s;
}

/* H'245CE6. Whether the slot in hand has moved since the last time anyone
 * asked: four of its bytes and the slot number itself, against the copy at
 * H'11F310.
 *
 * H'114D73 up is an answer of yes whatever the bytes say, and it is put down
 * on the way past. Bit 0 of H'11A63C goes up every time, answer or no.
 *
 * The original writes the five-byte copy out six times over, once at the end
 * of each of the six ways to answer yes; here it is one helper called six
 * times, which is the same five stores in the same order.
 */
u8 module_slot_changed(void)
{
    const u8  s = REG8(0x0011A660UL);
    const u32 e = (u32)(long)(short)(u16)((u16)((u16)s << 4));

    REG8(0x0011A63CUL) |= 0x01;

    if (REG8(0x00114D73UL) != 0) {
        REG8(0x00114D73UL) = 0x00;
        module_slot_snapshot();
        return 0x01;
    }

    if (REG8(0x0011A25AUL + e) != REG8(0x0011F310UL)) {
        module_slot_snapshot();
        return 0x01;
    }
    if (REG8(0x0011A25BUL + e) != REG8(0x0011F311UL)) {
        module_slot_snapshot();
        return 0x01;
    }
    if (REG8(0x0011A25FUL + e) != REG8(0x0011F312UL)) {
        module_slot_snapshot();
        return 0x01;
    }
    if (REG8(0x0011A260UL + e) != REG8(0x0011F313UL)) {
        module_slot_snapshot();
        return 0x01;
    }
    if (REG8(0x0011F314UL) != s) {
        module_slot_snapshot();
        return 0x01;
    }
    return 0x00;
}

/* ---- screen H'16, the module's size and speed settings -----------------
 * Twenty-five keys over six numbers. Four of the numbers are a percentage
 * with a key each for down, back to the middle, and up; one is a pair of
 * bytes that move together; and one is the speed. The arms are the same
 * routine over and over with the field and the label changed, which is the
 * shape H'248AC6's eight hoop nudges already have.
 */

/* The slot's own sixteen-byte block. */
static u32 module_slot16(void)
{
    return (u32)(long)(short)(u16)((u16)((u16)REG8(0x0011A660UL) << 4));
}

/* The guard every one of the number keys goes through. The three keys that
 * put a number back to the middle do not look at H'114D55; the ones that
 * move it up or down do. */
static u8 module_size_key_ready(u8 look_at_state)
{
    if (REG8(0x00114D9FUL) != 0) { (void)link_claim(0x07); return 0x00; }
    if (module_home_request() != 0) return 0x00;

    if (look_at_state != 0 &&
        ((REG8(0x00114D55UL) & 0x02) || (REG8(0x00114D55UL) & 0x04))) {
        (void)link_claim(0x21);
        return 0x00;
    }

    if (module_fault_report(0x80) != 0) return 0x00;
    if (module_fault_report(0x82) != 0) return 0x00;
    if (module_nothing_to_report() == 0) return 0x00;
    return 0x01;
}

/* The number put into H'11F2D6 with a "%" after it, ready for a label. */
static void module_size_text(short shown)
{
    int_to_decimal(shown, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;   /* '%' */
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
}

/* The two labels the percentage keys write into, and the two the paired
 * bytes write into. */
static void module_size_label(u8 which, short shown)
{
    module_size_text(shown);
    switch (which) {
    case 0x00: module_label_mid_top((const char *)0x0011F2D6UL);    break;
    case 0x01: module_label_mid_second((const char *)0x0011F2D6UL); break;
    case 0x02: module_label_right_mid((const char *)0x0011F2D6UL);  break;
    default:   module_label_right_low((const char *)0x0011F2D6UL);  break;
    }
}

/* One percentage, shown as twice the byte it is kept in: down to H'0A, back
 * to H'32, and up to H'64. Up and back both put the byte where they want it
 * and then ask whether the hoop still fits, undoing it when it does not. */
static void module_pct_down(u32 field, u8 which)
{
    const u32 a = field + module_slot16();

    if (REG8(a) <= 0x0A) return;
    if (module_link_quiet() == 0) return;

    REG8(0x00114D50UL) |= 0x10;
    REG8(a) = (u8)(REG8(a) - 1);
    module_size_label(which, (short)(u16)((u16)REG8(a) << 1));
    REG8(0x00114D5EUL) = 0x01;
    REG8(0x0011F2A1UL) = 0x02;
}

static void module_pct_middle(u32 field, u32 keep, u8 which)
{
    const u32 a = field + module_slot16();

    REG8(keep) = REG8(a);
    REG8(a) = 0x32;

    if (module_hoop_fits() == 0) { REG8(a) = REG8(keep); return; }

    module_size_label(which, (short)(u16)((u16)REG8(a) << 1));
    REG8(0x00114D5EUL) = 0x01;
    REG8(0x0011F2A1UL) = 0x02;
    REG8(0x00114D50UL) |= 0x10;
}

static void module_pct_up(u32 field, u8 which)
{
    const u32 a = field + module_slot16();

    if (REG8(a) >= 0x64) return;
    if (module_link_quiet() == 0) return;

    REG8(a) = (u8)(REG8(a) + 1);
    if (module_hoop_fits() == 0) { REG8(a) = (u8)(REG8(a) - 1); return; }

    module_size_label(which, (short)(u16)((u16)REG8(a) << 1));
    REG8(0x00114D5EUL) = 0x01;
    REG8(0x0011F2A1UL) = 0x02;
    REG8(0x00114D50UL) |= 0x10;
}

/* A pair of bytes that move together. The number beside them is H'C8 less
 * *twice* the first byte -- the doubling is the same one the percentages do,
 * and it happens before the subtraction, not after. The key that puts the
 * pair back to the middle shows twice the byte with nothing taken off it.
 *
 * Both bytes have to be inside their limits before either moves, and the
 * number has a bound of its own. These three send a message rather than
 * asking for one with H'114D5E. */
static void module_pair_down(u32 first, u32 second, u8 which)
{
    const u32 e = module_slot16();
    const u8  v = REG8(first + e);
    short shown = (short)(u16)(0x00C8 - (u16)((u16)v << 1));

    if (v <= 0x01) return;
    if (REG8(second + e) <= 0x01) return;
    if (shown >= 0x00BE) return;
    if (module_link_quiet() == 0) return;

    REG8(first + e)  = (u8)(REG8(first + e) - 1);
    REG8(second + e) = (u8)(REG8(second + e) - 1);
    shown = (short)(u16)(0x00C8 - (u16)((u16)REG8(first + e) << 1));

    REG8(0x0011F2A1UL) = 0x02;
    link_send_start();
    module_size_label(which, shown);
}

static void module_pair_middle(u32 first, u32 second, u8 which)
{
    const u32 e = module_slot16();

    if (module_link_quiet() == 0) return;

    REG8(first + e)  = 0x32;
    REG8(second + e) = 0x32;

    REG8(0x0011F2A1UL) = 0x02;
    link_send_start();
    module_size_label(which, (short)(u16)((u16)REG8(first + e) << 1));
}

static void module_pair_up(u32 first, u32 second, u8 which)
{
    const u32 e = module_slot16();
    const u8  v = REG8(first + e);
    short shown = (short)(u16)(0x00C8 - (u16)((u16)v << 1));

    if (v >= 0x00C8) return;
    if (REG8(second + e) >= 0x00C8) return;
    if (shown <= 0x0003) return;
    if (module_link_quiet() == 0) return;

    REG8(first + e)  = (u8)(REG8(first + e) + 1);
    REG8(second + e) = (u8)(REG8(second + e) + 1);
    shown = (short)(u16)(0x00C8 - (u16)((u16)REG8(first + e) << 1));

    REG8(0x0011F2A1UL) = 0x02;
    link_send_start();
    module_size_label(which, shown);
}

/* The speed, kept in H'11A264 and stepped by five.
 *
 * A byte of nought is one that has never been set: the first press on either
 * key puts H'23 in it and steps no further. After that, down takes five off
 * while there are more than five to take, and up puts five on while the byte
 * is at H'AF or under. The key between them puts it back to nought. */
static void module_speed_down(void)
{
    const u32 a = 0x0011A264UL + module_slot16();

    if (REG8(a) == 0x00) {
        REG8(a) = 0x23;
    } else if (REG8(a) > 0x05) {
        REG8(a) = (u8)(REG8(a) + 0xFB);
    }
    module_label_speed();
    REG8(0x00114D5EUL) = 0x01;
    REG8(0x0011F2A1UL) = 0x02;
}

static void module_speed_middle(void)
{
    REG8(0x0011A264UL + module_slot16()) = 0x00;
    module_label_speed();
    REG8(0x00114D5EUL) = 0x01;
    REG8(0x0011F2A1UL) = 0x02;
}

static void module_speed_up(void)
{
    const u32 a = 0x0011A264UL + module_slot16();

    if (REG8(a) == 0x00) {
        REG8(a) = 0x23;
    } else if (REG8(a) <= 0xAF) {
        REG8(a) = (u8)(REG8(a) + 0x05);
    }
    module_label_speed();
    REG8(0x00114D5EUL) = 0x01;
    REG8(0x0011F2A1UL) = 0x02;
}

/* H'22DBFA. Screen H'16's press: the module's size and speed settings.
 *
 * The hit test covers boxes one to H'16 and the table twenty-five values.
 * Values five, six and seven are turned back before the table is reached --
 * the three arms they point at are in the ROM and cannot be got to, which is
 * why they are not written here.
 *
 * Key H'01 starts the sewing and is the only one that waits for the link.
 * H'02 to H'04 and H'09 to H'0B are the two percentages; H'0D to H'0F and
 * H'10 to H'12 the two pairs; H'13 to H'15 the speed. H'08 and H'19 are the
 * two ways out, and H'0C redraws.
 */
void module_sizes_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0016, &value, &index) != 0x03) return;

    /* The three the table has arms for and the routine will not run. */
    if ((short)value >= 0x0005 && (short)value <= 0x0007) return;

    message_show_held(index);
    REG8(0x00114D8EUL) = 0x09;
    if (module_busy() != 0) return;

    if ((u16)(value - 1) > 0x0018) return;

    if (value == 0x0001) {
        if (REG8(0x00114D9FUL) != 0) { module_reset_walk(); return; }
        if (module_home_request() != 0) return;
        if (module_fault_report(0x81) != 0) return;
        if (module_fault_report(0x84) != 0) return;

        pattern_slot_begin();
        REG8(0x0011A41DUL + module_slot12()) = REG8(0x00114DA1UL);
        REG8(0x0011A41FUL + module_slot12()) =
            (u8)((u16)REG8(0x00114D8CUL) / 0x1B);

        /* The message only goes out when the link is quiet and there is
         * something to send; the screen changes either way. */
        if (module_link_quiet() && REG8(0x0011A640UL) != 0) {
            REG8(0x0011F2A1UL) = 0x01;
            link_send_start();
            while (module_link_quiet() == 0) rom_host_service();
        }

        message_show_held(index);
        if (REG8(0x00114DA1UL) == 0x01) {
            REG8(0x00114D8EUL) = 0x03;
            screen_switch(0x14, 0x01, 0x00);
        } else {
            REG8(0x00114D8EUL) = 0x02;
            screen_switch(0x13, 0x01, 0x00);
        }
        REG8(0x00114D7EUL) = 0x01;
        if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        return;
    }

    if (value == 0x0008) {
        if (REG8(0x00114D72UL) != 0) return;
        if (module_home_request() != 0) return;

        REG8(0x00114D50UL) &= (u8)~0x01;
        REG8(0x00114D50UL) &= (u8)~0x02;
        screen_switch(0x15, 0x01, 0x00);
        embroidery_panel_save_b();
        REG8(0x00114D72UL) = 0x01;
        return;
    }

    if (value == 0x000C) {
        if (module_home_request() != 0) return;
        if (module_fault_report(0x82) != 0) return;
        if (REG8(0x00114D66UL) != 0) return;

        if (module_link_quiet() == 0) return;

        if (module_slot_changed() != 0) {
            REG8(0x00114D5DUL) = 0x01;
            REG8(0x00114D67UL) = 0x01;
            REG8(0x0011A619UL) = 0x01;
            if (pattern_attr_bit3() != 0) module_size_shrink();
            REG8(0x00114D50UL) |= 0x08;
            REG8(0x00114D50UL) &= (u8)~0x04;
        } else {
            REG8(0x00114D67UL) = 0x01;
            REG8(0x00114D50UL) &= (u8)~0x08;
            REG8(0x00114D50UL) |= 0x04;
        }
        return;
    }

    if (value == 0x0019) {
        if (REG8(0x00114D72UL) != 0) return;
        if (module_link_quiet() == 0) return;

        screen_switch(0x23, 0x01, 0x00);
        embroidery_panel_save_b();
        REG8(0x00114D72UL) = 0x03;
        REG8(0x00114D89UL) = 0x00;
        REG8(0x00114D97UL) = 0x01;
        pattern_mark_ready();
        return;
    }

    /* Every one of the number keys shares the same way in. */
    {
        const u8 middle = (u8)(value == 0x0003 || value == 0x000A ||
                               value == 0x000E || value == 0x0011 ||
                               value == 0x0014);

        if (module_size_key_ready((u8)(middle ? 0x00 : 0x01)) == 0) return;
    }

    switch (value) {
    case 0x0002: module_pct_down(0x0011A25AUL, 0x00); break;
    case 0x0003: module_pct_middle(0x0011A25AUL, 0x0011F299UL, 0x00); break;
    case 0x0004: module_pct_up(0x0011A25AUL, 0x00); break;

    case 0x0009: module_pct_down(0x0011A25BUL, 0x01); break;
    case 0x000A: module_pct_middle(0x0011A25BUL, 0x0011F29AUL, 0x01); break;
    case 0x000B: module_pct_up(0x0011A25BUL, 0x01); break;

    case 0x000D: module_pair_down(0x0011A25CUL, 0x0011A25DUL, 0x02); break;
    case 0x000E: module_pair_middle(0x0011A25CUL, 0x0011A25DUL, 0x02); break;
    case 0x000F: module_pair_up(0x0011A25CUL, 0x0011A25DUL, 0x02); break;

    case 0x0010: module_pair_down(0x0011A261UL, 0x0011A262UL, 0x03); break;
    case 0x0011: module_pair_middle(0x0011A261UL, 0x0011A262UL, 0x03); break;
    case 0x0012: module_pair_up(0x0011A261UL, 0x0011A262UL, 0x03); break;

    case 0x0013: module_speed_down();   break;
    case 0x0014: module_speed_middle(); break;
    case 0x0015: module_speed_up();     break;

    default: break;
    }
}

/* One axis of the fit test at H'245848. The corner reaches `reach' from the
 * design's own centre, that centre sits `pos' off the middle of the hoop in
 * tenths, and `lim' is how far the hoop goes. Two millimetres are kept in
 * hand at the edge. */
static u8 module_turn_clear(u32 pos, u32 lim, float reach)
{
    const short off  = (short)(u16)((u16)abs_short((short)REG16(pos))
                                    * (u16)0x000A);
    const short room = (short)(u16)((u16)((u16)REG16(lim)
                                          - (u16)(int)reach) - (u16)0x0016);

    if (room < off) return 0x00;
    return 0x01;
}

/* H'245848. Whether the design would still sit inside the hoop if it were
 * turned. One when it fits, nought when a corner would run out.
 *
 * The design's width and height come from H'104CCE and H'104D06, scaled by
 * the slot's two percentages (twice the byte, over a hundred). Those give a
 * diagonal and the angle to its corner.
 *
 * `turn' says which rotation to try: nought takes the one the slot already
 * holds at H'11A260, one steps it by `by' -- down when `up' is nought, up
 * when it is one, and neither when it is anything else. Five degrees a step,
 * minus a hundred and eighty, into radians; H'11A25F turns it back the other
 * way.
 *
 * The corner is then swung to each side of the angle and both are asked, on
 * each axis, to keep clear of the hoop. The first that runs out gives up.
 *
 * With a nought-wide design the ROM never works out the corner angle, and
 * with a `turn' above one it never works out the rotation either; both use
 * whatever the stack held. These start at nought, as H'24217A's does, and no
 * caller goes to either. */
u8 module_turn_fits(u8 turn, u8 up, u8 by)
{
    const u32 s16 = (u32)(long)(short)(u16)(
        (u16)((u16)REG8(0x0011A660UL) << 4));
    const u32 idx = (u32)(long)(short)(u16)(
        (u16)((u16)REG8(0x0011A41AUL + (u32)(long)(short)(u16)(
            0x0012 * (u16)REG8(0x0011A660UL))) << 1));
    const float w = (float)(u32)REG8(0x0011A25AUL + s16) * 2.0f / 100.0f
                    * (float)(u32)REG16(0x00104CCEUL + idx);
    const float h = (float)(u32)REG8(0x0011A25BUL + s16) * 2.0f / 100.0f
                    * (float)(u32)REG16(0x00104D06UL + idx);
    const float diag = float_sqrt(h * h + w * w);
    float corner = 0.0f, ang = 0.0f, t, x, y;

    if (f2u(w) != 0) corner = float_atan(h / w);

    if (turn == 0x01) {
        if (up == 0x00) {
            ang = (float)(u32)REG8(0x0011A260UL + s16) - (float)(u32)by;
        } else if (up == 0x01) {
            ang = (float)(u32)REG8(0x0011A260UL + s16) + (float)(u32)by;
        } else {
            ang = (float)(u32)REG8(0x0011A260UL + s16);
        }
    } else if (turn == 0x00) {
        ang = (float)(u32)REG8(0x0011A260UL + s16);
    }

    ang = (ang * 5.0f + -180.0f) * 0.017453277f;
    if (REG8(0x0011A25FUL + s16) == 0x01) ang = float_flip(ang);

    t = corner + ang;
    x = float_cos(t) * diag;
    if ((long)f2u(x) < 0) x = float_flip(x);
    y = float_sin(t) * diag;
    if ((long)f2u(y) < 0) y = float_flip(y);

    if (module_turn_clear(0x0011A266UL + s16, 0x0011A626UL, x) == 0) return 0x00;
    if (module_turn_clear(0x0011A268UL + s16, 0x0011A628UL, y) == 0) return 0x00;

    t = ang - corner;
    x = float_cos(t) * diag;
    if ((long)f2u(x) < 0) x = float_flip(x);
    y = float_sin(t) * diag;
    if ((long)f2u(y) < 0) y = float_flip(y);

    if (module_turn_clear(0x0011A266UL + s16, 0x0011A626UL, x) == 0) return 0x00;
    if (module_turn_clear(0x0011A268UL + s16, 0x0011A628UL, y) == 0) return 0x00;

    return 0x01;
}

/* The guard three of screen H'15's keys share. Home first, then the two
 * state bits, then the module's own busy byte; the two that go on to set a
 * bit in H'11A63A also want a clean fault report and something to report,
 * and the one in the middle does not. */
static u8 module_hoop_key_ready(u8 look_at_faults)
{
    if (module_home_request() != 0) return 0x00;
    if ((REG8(0x00114D55UL) & 0x02) || (REG8(0x00114D55UL) & 0x04)) {
        (void)link_claim(0x21);
        return 0x00;
    }
    if (REG8(0x00114D9FUL) != 0) { (void)link_claim(0x07); return 0x00; }
    if (look_at_faults != 0) {
        if (module_fault_report(0x80) != 0) return 0x00;
        if (module_nothing_to_report() == 0) return 0x00;
    }
    return 0x01;
}

/* The number the three size keys put in the top right. The two percentages
 * always move together on this screen, so their sum is twice either one --
 * the same doubled figure screen H'16 shows, worked out the other way. */
static void module_hoop_pct_text(void)
{
    const u32 e = module_slot16();

    int_to_decimal((short)(u16)((u16)REG8(0x0011A25BUL + e)
                                + (u16)REG8(0x0011A25AUL + e)),
                   (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;   /* '%' */
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    module_label_right_top((const char *)0x0011F2D6UL);
    REG8(0x00114D5EUL) = 0x01;
    REG8(0x0011F2A1UL) = 0x02;
}

/* One of the nine keys that move the hoop. H'11A615 carries the direction
 * and the message goes straight out; the middle key, which puts the hoop
 * back where it started, is the one that does not count the step. */
static void module_hoop_nudge_key(u8 dir, u8 counted)
{
    if (module_home_request() != 0) return;
    if (module_running() == 0) { (void)link_claim(0x03); return; }
    if (module_link_quiet() == 0) return;

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A615UL) = dir;
    if (counted != 0) {
        REG8(0x00114DB0UL) = (u8)(REG8(0x00114DB0UL) + 1);
        if (REG8(0x00114DB1UL) != 0) REG8(0x0011F2A2UL) |= 0x01;
    }
    REG8(0x0011F2A2UL) &= (u8)~0x08;
    REG8(0x00114D50UL) |= 0x01;
    REG8(0x00114D50UL) |= 0x10;
    link_send_start();
    if (counted != 0 && REG8(0x00114DB1UL) != 0) REG8(0x00114DB2UL) = 0x01;
}

/* The two keys that turn the design. H'11A260 counts five degrees a step
 * from one to H'48, and H'114DB6 says whether the key is being held down --
 * held takes six steps at a time and falls back to one when six would not
 * fit. The label counts the other way round, a hundred and eighty less five
 * degrees a step, brought back into range by a whole turn. */
static void module_hoop_turn_key(u8 up)
{
    const u32 e = module_slot16();
    u8 ok;

    if (module_size_key_ready(0x01) == 0) return;
    if (module_link_quiet() == 0) return;

    if (REG8(0x00114DB6UL) != 0) {
        ok = module_turn_fits(0x01, up, 0x06);
        if (ok == 0) {
            REG8(0x00114DB6UL) = 0x00;
            ok = module_turn_fits(0x01, up, 0x01);
        }
    } else {
        ok = module_turn_fits(0x01, up, 0x01);
    }
    if (ok == 0) return;

    REG8(0x00114DB5UL) = (u8)(REG8(0x00114DB5UL) + 1);

    if (up == 0x00) {
        if (REG8(0x00114DB6UL) != 0) {
            if (REG8(0x0011A260UL + e) >= 0x06) {
                REG8(0x0011A260UL + e) = (u8)(REG8(0x0011A260UL + e) - 0x06);
            } else {
                REG8(0x0011A260UL + e) = 0x00;
            }
        } else {
            if (REG8(0x0011A260UL + e) >= 0x01) {
                REG8(0x0011A260UL + e) = (u8)(REG8(0x0011A260UL + e) - 0x01);
            } else {
                REG8(0x0011A260UL + e) = 0x00;
            }
        }
        if (REG8(0x0011A260UL + e) == 0x00) REG8(0x0011A260UL + e) = 0x48;
    } else {
        if (REG8(0x00114DB6UL) != 0) {
            if (REG8(0x0011A260UL + e) <= 0x42) {
                REG8(0x0011A260UL + e) = (u8)(REG8(0x0011A260UL + e) + 0x06);
            } else {
                REG8(0x0011A260UL + e) = 0x48;
            }
        } else {
            if (REG8(0x0011A260UL + e) <= 0x47) {
                REG8(0x0011A260UL + e) = (u8)(REG8(0x0011A260UL + e) + 0x01);
            } else {
                REG8(0x0011A260UL + e) = 0x48;
            }
        }
        if (REG8(0x0011A260UL + e) == 0x48) REG8(0x0011A260UL + e) = 0x00;
    }

    {
        const short shown = (short)(u16)(0x00B4
            - (u16)((u16)REG8(0x0011A260UL + e) * (u16)0x0005));

        REG16(0x0011F292UL) = (u16)shown;
        if (shown < 0) {
            REG16(0x0011F292UL) = (u16)(REG16(0x0011F292UL) + 0x0168);
        }
        int_to_decimal((short)REG16(0x0011F292UL), (char *)0x0011F2D6UL);
        REG8(0x0011F294UL) = 0x67;   /* the degree mark in this font */
        REG8(0x0011F295UL) = 0x00;
        str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
        module_label_right_foot((const char *)0x0011F2D6UL);
        REG8(0x00114D5EUL) = 0x01;
        REG8(0x0011F2A1UL) = 0x02;
        REG8(0x00114D50UL) |= 0x10;
    }
}

/* H'22C24C. Screen H'15's press: where the design sits in the hoop, how big
 * it is and which way round it faces.
 *
 * The hit test covers boxes one to H'19 and the table twenty-five values,
 * every one of which has an arm of its own.
 *
 * H'01 starts the sewing, H'08 and H'19 are the two ways out and H'0C
 * redraws. H'02, H'03 and H'0D each set a bit in H'11A63A. H'04 mirrors the
 * design. H'05 to H'07 are the size, which moves both percentages together.
 * H'09 to H'0B talk to the module. H'0E to H'16 are the nine keys that move
 * the hoop, H'17 and H'18 the two that turn the design.
 *
 * Box four is the only one whose press is not shown as held. */
void module_hoop_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0019, &value, &index) != 0x03) return;

    if (value != 0x0004) message_show_held(index);
    REG8(0x00114D8EUL) = 0x08;
    if (module_busy() != 0) return;

    if ((u16)(value - 1) > 0x0018) return;

    if (value == 0x0001) {
        if (REG8(0x00114D9FUL) != 0) { module_reset_walk(); return; }
        if (module_home_request() != 0) return;
        if (module_fault_report(0x81) != 0) return;
        if (module_fault_report(0x84) != 0) return;

        pattern_slot_begin();
        REG8(0x0011A41DUL + module_slot12()) = REG8(0x00114DA1UL);
        REG8(0x0011A41FUL + module_slot12()) =
            (u8)((u16)REG8(0x00114D8CUL) / 0x1B);

        /* The message only goes out when the link is quiet and there is
         * something to send; the screen changes either way. */
        if (module_link_quiet() && REG8(0x0011A640UL) != 0) {
            REG8(0x0011F2A1UL) = 0x01;
            link_send_start();
            while (module_link_quiet() == 0) rom_host_service();
        }

        message_show_held(index);
        if (REG8(0x00114DA1UL) == 0x01) {
            REG8(0x00114D8EUL) = 0x03;
            screen_switch(0x14, 0x01, 0x00);
        } else {
            REG8(0x00114D8EUL) = 0x02;
            screen_switch(0x13, 0x01, 0x00);
        }
        REG8(0x00114D7EUL) = 0x01;
        if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        return;
    }

    if (value == 0x0002) {
        if (module_hoop_key_ready(0x01) == 0) return;
        if (module_link_quiet() == 0) return;
        REG16(0x0011A63AUL) = (u16)(REG16(0x0011A63AUL) | 0x0001);
        return;
    }

    if (value == 0x0003) {
        if (module_hoop_key_ready(0x00) == 0) return;
        if (module_link_quiet() == 0) return;
        REG16(0x0011A63AUL) = (u16)(REG16(0x0011A63AUL) | 0x0002);
        return;
    }

    if (value == 0x000D) {
        if (module_hoop_key_ready(0x01) == 0) return;
        if (module_link_quiet() == 0) return;
        REG16(0x0011A63AUL) = (u16)(REG16(0x0011A63AUL) | 0x0004);
        return;
    }

    if (value == 0x0004) {
        const u32 e = module_slot16();

        if (module_size_key_ready(0x01) == 0) return;
        if (module_link_quiet() == 0) return;

        REG8(0x0011A25FUL + e) =
            (REG8(0x0011A25FUL + e) == 0x01) ? 0x00 : 0x01;
        module_box4_press(REG8(0x0011A25FUL + e));
        REG8(0x00114D5EUL) = 0x01;
        REG8(0x0011F2A1UL) = 0x02;
        REG8(0x00114D50UL) |= 0x10;
        return;
    }

    if (value == 0x0005) {
        const u32 e = module_slot16();

        if (module_size_key_ready(0x01) == 0) return;
        if (REG8(0x0011A25AUL + e) <= 0x0A) return;
        if (REG8(0x0011A25BUL + e) <= 0x0A) return;
        if (module_link_quiet() == 0) return;

        REG8(0x00114D50UL) |= 0x10;
        REG8(0x0011A25AUL + e) = (u8)(REG8(0x0011A25AUL + e) - 1);
        REG8(0x0011A25BUL + e) = (u8)(REG8(0x0011A25BUL + e) - 1);
        module_hoop_pct_text();
        return;
    }

    if (value == 0x0006) {
        const u32 e = module_slot16();

        /* This one asks neither the module's busy byte nor the two state
         * bits: it only puts the size back to a hundred per cent. */
        if (module_home_request() != 0) return;
        if (module_fault_report(0x80) != 0) return;
        if (module_fault_report(0x82) != 0) return;
        if (module_nothing_to_report() == 0) return;
        if (module_link_quiet() == 0) return;

        REG8(0x0011F299UL) = REG8(0x0011A25AUL + e);
        REG8(0x0011F29AUL) = REG8(0x0011A25BUL + e);
        REG8(0x0011A25AUL + e) = 0x32;
        REG8(0x0011A25BUL + e) = 0x32;
        if (module_hoop_fits() != 0) {
            module_hoop_pct_text();
            REG8(0x00114D50UL) |= 0x10;
        } else {
            REG8(0x0011A25AUL + e) = REG8(0x0011F299UL);
            REG8(0x0011A25BUL + e) = REG8(0x0011F29AUL);
        }
        return;
    }

    if (value == 0x0007) {
        const u32 e = module_slot16();

        if (module_size_key_ready(0x01) == 0) return;
        if (REG8(0x0011A25AUL + e) >= 0x64) return;
        if (REG8(0x0011A25BUL + e) >= 0x64) return;
        if (module_link_quiet() == 0) return;

        REG8(0x0011A25AUL + e) = (u8)(REG8(0x0011A25AUL + e) + 1);
        REG8(0x0011A25BUL + e) = (u8)(REG8(0x0011A25BUL + e) + 1);
        if (module_hoop_fits() != 0) {
            module_hoop_pct_text();
            REG8(0x00114D50UL) |= 0x10;
        } else {
            REG8(0x0011A25AUL + e) = (u8)(REG8(0x0011A25AUL + e) - 1);
            REG8(0x0011A25BUL + e) = (u8)(REG8(0x0011A25BUL + e) - 1);
        }
        return;
    }

    if (value == 0x0008) {
        if (REG8(0x00114D72UL) != 0) return;
        if (module_home_request() != 0) return;

        REG8(0x00114D50UL) &= (u8)~0x01;
        REG8(0x00114D50UL) &= (u8)~0x02;
        embroidery_panel_save();
        screen_switch(0x16, 0x01, 0x00);
        REG8(0x00114D72UL) = 0x02;
        return;
    }

    if (value == 0x0009 || value == 0x000A) {
        if (module_home_request() != 0) return;
        if (module_running() == 0) { (void)link_claim(0x03); return; }
        if (module_link_quiet() == 0) return;
        if (REG8(0x00114D66UL) != 0) return;

        REG8(0x00114D5DUL) = 0x01;
        REG8(0x0011A614UL) = (value == 0x0009) ? 0x03 : 0x02;
        return;
    }

    if (value == 0x000B) {
        if (module_home_request() != 0) return;
        if (module_can_talk() == 0) return;
        if (REG8(0x00114DA0UL) == 0) module_talk_end();
        if (REG8(0x00114D66UL) != 0) return;
        if (module_link_quiet() == 0) return;

        REG8(0x00114D74UL) = 0x01;
        REG8(0x00114D5DUL) = 0x01;
        REG8(0x0011A612UL) = 0x01;
        REG8(0x0011A614UL) = 0x01;
        REG8(0x00114DBAUL) = 0x00;
        return;
    }

    if (value == 0x000C) {
        if (module_home_request() != 0) return;
        if (module_fault_report(0x82) != 0) return;
        if (REG8(0x00114D66UL) != 0) return;
        if (module_link_quiet() == 0) return;

        if (module_slot_changed() != 0) {
            REG8(0x00114D5DUL) = 0x01;
            REG8(0x00114D67UL) = 0x01;
            REG8(0x0011A619UL) = 0x01;
            if (pattern_attr_bit3() != 0) module_size_shrink();
            REG8(0x00114D50UL) |= 0x08;
            REG8(0x00114D50UL) &= (u8)~0x04;
        } else {
            REG8(0x00114D67UL) = 0x01;
            REG8(0x00114D50UL) &= (u8)~0x08;
            REG8(0x00114D50UL) |= 0x04;
        }
        return;
    }

    /* The nine that move the hoop. H'12 is the one in the middle. */
    if (value >= 0x000E && value <= 0x0016) {
        static const u8 way[9] = { 0x08, 0x01, 0x02, 0x07, 0x09,
                                   0x03, 0x06, 0x05, 0x04 };
        const u8 dir = way[value - 0x000E];

        module_hoop_nudge_key(dir, (u8)(value != 0x0012));
        return;
    }

    if (value == 0x0017) { module_hoop_turn_key(0x00); return; }
    if (value == 0x0018) { module_hoop_turn_key(0x01); return; }

    if (value == 0x0019) {
        if (REG8(0x00114D72UL) != 0) return;
        if (module_link_quiet() == 0) return;

        screen_switch(0x23, 0x01, 0x00);
        embroidery_panel_save();
        REG8(0x00114D72UL) = 0x03;
        REG8(0x00114D89UL) = 0x00;
        REG8(0x00114D97UL) = 0x01;
        pattern_mark_ready();
        return;
    }
}

/* H'23A012. Whether the box `n' of the picking grid would reach past the
 * last pattern there is. H'114D8B is the first pattern the page shows and
 * H'0FFE80 how many there are altogether, so a box beyond the count is one
 * with nothing in it. */
u8 module_pick_past_end(u8 n)
{
    const u16 want = (u16)((u16)REG8(0x00114D8BUL) + (u16)n);
    const u16 have = (u16)REG8(0x000FFE80UL);

    if ((short)want > (short)have) return 0x01;
    return 0x00;
}

/* H'249AEE and H'249B3A. Two more arrows: box ten and box eleven, each with
 * a lit picture and a dim one. They share their pictures with the page
 * arrows below, which is why the addresses are written out here rather than
 * given names of their own. */
void module_arrow_10(u8 lit)
{
    if (lit != 0) hitbox_blit(0x000A, LCD_FRAME_A, 0x0034E46CUL);
    else          hitbox_blit(0x000A, LCD_FRAME_A, 0x0034E4A8UL);
}

void module_arrow_11(u8 lit)
{
    if (lit != 0) hitbox_blit(0x000B, LCD_FRAME_A, 0x0034E4E4UL);
    else          hitbox_blit(0x000B, LCD_FRAME_A, 0x0034E520UL);
}

/* H'23A716. The stitch stream and the marks that go with it put back to
 * nothing: H'0A41 bytes of whatever H'11F2C6 points at, the two H'15-byte
 * tables at H'10405B and H'104046, and the dozen odds and ends around
 * H'104037 that say where the stream has got to. */
void module_stitch_marks_clear(void)
{
    u16 i;
    u8 k;

    for (i = 0; i < 0x0A41; i++) REG8(REG32(0x0011F2C6UL) + (u32)i) = 0x00;

    for (k = 0; k < 0x15; k++) {
        REG8(0x0010405BUL + (u32)k) = 0x00;
        REG8(0x00104046UL + (u32)k) = 0x00;
    }

    REG8(0x00104037UL) = 0x00;
    REG8(0x00104044UL) = 0x00;
    REG8(0x00104040UL) = 0x00;
    REG16(0x0010403CUL) = 0x0000;
    REG8(0x00104038UL) = 0x00;
    REG8(0x00104039UL) = 0x00;
    REG16(0x0010403AUL) = 0x0000;
    REG16(0x0010403EUL) = 0x0000;
    REG8(0x00104042UL) = 0x00;
    REG8(0x00104043UL) = 0x00;
}

/* H'231F72. A number written right-aligned into a box, and -- when `mark' is
 * anything but nought -- the character at H'6D in this font put in a second
 * box sixty-two pixels to its left. H'11F2DB is cleared first so that a
 * shorter number cannot leave a digit of the last one behind it. */
void module_number_label(short n, u16 x0, u16 y0, u16 x1, u16 y1, u16 mark)
{
    REG8(0x0011F2DBUL) = 0x00;
    int_to_decimal(n, (char *)0x0011F2D6UL);
    text_draw((const char *)0x0011F2D6UL, x0, y0, x1, y1,
              0x0001, 0x00, (const u8 *)0x00119DE6UL);

    if (mark != 0) {
        REG8(0x0011F2D6UL) = 0x6D;
        REG8(0x0011F2D7UL) = 0x00;
        text_draw((const char *)0x0011F2D6UL, (u16)(x0 + 0xFFC2), y0,
                  (u16)(x1 + 0xFFC2), y1, 0x0001, 0x00,
                  (const u8 *)0x00119DE6UL);
    }
}

/* H'244ADA. The picked pattern handed over to the module: a code in
 * H'11A61B worked out from the slot's second record byte, then two messages
 * sent one after the other, each waited on until the link goes quiet again.
 *
 * The code is H'0C for a record byte of nought and one more for each step up
 * to eight; anything above that gets H'15. The ROM writes this as a jump
 * table of nine arms that each store one constant, which is a table of nine
 * constants with the arithmetic spelled out.
 *
 * The wait after the first message tests only three of the six things
 * H'244CE8 does -- the ROM has the shorter form here and the longer one
 * either side of it. */
void module_pattern_send(void)
{
    const u8 k = REG8(0x0011A41FUL + module_slot12());

    REG8(0x0011A61BUL) = (u8)(0x0C + (k > 0x08 ? 0x09 : k));

    while (module_link_quiet() == 0) module_panel_blink(0x01);

    REG8(0x0011F2A1UL) = 0x03;
    link_send_start();

    while ((REG8(0x00114D50UL) & 0x21) != 0 || REG8(0x0011F29EUL) != 0 ||
           REG8(0x0011F2B6UL) != 0) {
        module_panel_blink(0x01);
    }

    REG8(0x0011F2A1UL) = 0x04;
    REG8(0x0011F2A2UL) = 0x01;
    link_send_start();

    while (module_link_quiet() == 0) module_panel_blink(0x01);

    module_panel_box(0x01);
}

/* H'239390. One row of the picking grid: three cells across, each holding a
 * pattern's thumbnail and its number beside it.
 *
 * `row' picks the row and gives the cells their y; the loop runs the three
 * cells across and gives them their x, and the pattern each cell shows is
 * H'114D8B plus three for the row plus one for the cell. `count' is how many
 * of the three have a pattern at all -- a cell past it is cleared and left
 * empty.
 *
 * The thumbnails are H'022E bytes apart in a block that is either in RAM at
 * H'0E0200 or in the data at H'10032E, which `from_ram' chooses, and each is
 * H'3E rows of nine bytes with a bit to a pixel, drawn one pixel at a time.
 *
 * The number beside a cell is drawn with the mark after it only when the
 * module has more patterns than the machine knows about and this cell is one
 * of the extra ones -- bit 1 of H'114D51 says the module has been asked, and
 * H'100255 against H'0FFE80 is how many more. The ROM writes the plain case
 * out twice, once for each way of reaching it. */
void module_pick_row(u8 row, u16 first, u16 count, u16 from_ram)
{
    const u32 src = ((from_ram & 0xFF) == 0x01 ? 0x000E0200UL : 0x0010032EUL)
                  + (u32)(u16)((u16)first * (u16)0x022E);
    const u16 y0 = (u16)((u16)((u16)row << 2)
                         + (u16)((u16)0x003E * (u16)row) + 0x0029);
    const u8 gap = 0x06;
    u16 c;

    for (c = 0; c < 0x0003; c++) {
        const u16 x0 = (u16)((u16)(0x0008 + (u16)(0x004C * c))
                             + (c < 0x0002 ? 0x0000 : 0x0002));
        const u8 n = (u8)((u8)((u8)c + 0x01) + (u8)(0x03 * row)
                          + REG8(0x00114D8BUL));
        u8 shown = n;
        u16 mark = 0x0000;

        module_box_clear((u16)(x0 + 1), y0,
                         (u16)(x0 + 0x47), (u16)(y0 + 0x3D));

        if ((u16)(u8)count <= c) continue;

        {
            u16 i, j, b, at;

            for (i = 0; i < 0x003E; i++) {
                at = 0;
                for (j = 0; j < 0x0009; j++) {
                    const u8 byte = REG8(src + (u32)(u16)(
                        (u16)((u16)0x0009 * i) + (u16)((u16)0x022E * c) + j));
                    u8 m = 0x80;

                    for (b = 0; b < 0x0008; b++) {
                        if ((u8)(m & byte) == m) {
                            plot_pixel((u16)(x0 + at), (u16)(y0 + i),
                                       LCD_FRAME_A, 0x03);
                        }
                        m = (u8)(m >> 1);
                        at++;
                    }
                }
            }
        }

        if (REG8(0x00114D51UL) & 0x02) {
            const u8 extra = (u8)(REG8(0x000FFE80UL) - REG8(0x00100255UL));

            REG8(0x00114DBCUL) = extra;
            REG8(0x00114DBDUL) = REG8(0x000FFE80UL);

            if ((short)(u16)((u16)REG8(0x000FFE80UL)
                             - (u16)REG8(0x00100255UL)) < (short)(u16)n &&
                REG8(0x0011A41DUL + module_slot12()) == 0) {
                shown = (u8)(n - extra);
                mark = 0x0001;
            }
        }

        module_number_label((short)(u16)shown, (u16)(x0 - gap + 0x0044), y0,
                            (u16)(x0 + 0x0046), (u16)(y0 + gap), mark);
    }
}

/* H'239678 and H'2397D2. The two keys that page the picking grid.
 *
 * Both start by looking for a colour boundary between where the page is now
 * and where the step would take it: the colours are H'1B patterns apart, and
 * if one falls inside the step the page does not move at all -- instead
 * H'114D8C is put on the boundary, H'114D87 says which way it was reached
 * and the slot's record byte takes the colour's number. Only when no
 * boundary is in the way does the page itself move three along.
 *
 * When it does move, the three rows already on the screen are slid up or
 * down by a region copy and the one row that has come into view is drawn.
 * The two arrows are then lit or dimmed by where the page has ended up. */
void module_page_back(void)
{
    u8 k;

    if (REG8(0x00114D8BUL) == 0) return;

    for (k = 0x01; k < 0x0A; k++) {
        const u16 top   = (u16)((u16)0x001B * (u16)k);
        const u16 first = (u16)REG8(0x00114D8BUL);

        if ((short)top < (short)first) continue;
        if ((short)(u16)(top + 0xFFFA) > (short)first) continue;
        if ((u16)((u16)(k - 1) * (u16)0x001B) == (u16)REG8(0x00114D8CUL)) {
            continue;
        }

        REG8(0x00114D8CUL) = (u8)((u8)(k - 1) * (u8)0x1B);
        REG8(0x00114D87UL) = 0x02;
        REG8(0x0011A41FUL + module_slot12()) = (u8)(k - 1);
        REG8(0x00114DAFUL) = 0x00;
        return;
    }

    REG8(0x00114D8BUL) = (u8)(REG8(0x00114D8BUL) - 0x03);

    region_copy(0x0008, 0x0029, 0x00ED, 0x00A8, 0x006B,
                LCD_FRAME_A, LCD_FRAME_A);

    {
        short rows = (short)(u16)((u16)REG8(0x000FFE80UL)
                                  - (u16)REG8(0x00114D8BUL));
        const u8 row = (u8)(REG8(0x00114D8BUL) - REG8(0x00114D8CUL));

        if (rows > 0x0003) rows = 0x0003;
        if (rows >= 0) module_pick_row(0x00, (u16)row, (u16)rows, 0x0000);
    }

    module_arrow_10(REG8(0x00114D8BUL) != 0 ? 0x01 : 0x00);
    module_arrow_11((short)(u16)((u16)REG8(0x000FFE80UL)
                                 - (u16)REG8(0x00114D8BUL)) > 0x0009
                    ? 0x01 : 0x00);
}

void module_page_on(void)
{
    u8 k;

    if (!((short)(u16)((u16)REG8(0x000FFE80UL)
                       - (u16)REG8(0x00114D8BUL)) > 0x0009)) {
        return;
    }

    for (k = 0x01; k < 0x0A; k++) {
        const u16 top   = (u16)((u16)0x001B * (u16)k);
        const u16 first = (u16)REG8(0x00114D8BUL);

        if ((short)(u16)(top + 0xFFF7) > (short)first) continue;
        if ((short)(u16)(top + 0xFFFD) < (short)first) continue;
        if (top == (u16)REG8(0x00114D8CUL)) continue;

        REG8(0x00114D8CUL) = (u8)((u8)0x1B * k);
        REG8(0x00114D87UL) = 0x01;
        REG8(0x0011A41FUL + module_slot12()) = k;
        REG8(0x00114DAFUL) = 0x01;
        return;
    }

    REG8(0x00114D8BUL) = (u8)(REG8(0x00114D8BUL) + 0x03);

    region_copy(0x0008, 0x006B, 0x00ED, 0x00EA, 0x0029,
                LCD_FRAME_A, LCD_FRAME_A);

    {
        short rows = (short)(u16)((u16)REG8(0x000FFE80UL)
                                  - (u16)((u16)REG8(0x00114D8BUL) + 0x0006));
        const u8 row = (u8)((u8)(REG8(0x00114D8BUL)
                                 - REG8(0x00114D8CUL)) + 0x06);

        if (rows > 0x0003) rows = 0x0003;
        if (rows >= 0) module_pick_row(0x02, (u16)row, (u16)rows, 0x0000);
    }

    module_arrow_10(REG8(0x00114D8BUL) != 0 ? 0x01 : 0x00);
    module_arrow_11((short)(u16)((u16)REG8(0x000FFE80UL)
                                 - (u16)REG8(0x00114D8BUL)) > 0x0009
                    ? 0x01 : 0x00);
}

/* H'22BF8C. Screen H'14's press: the grid of patterns to pick from.
 *
 * The hit test covers boxes one to H'0D, but it is the box's *value* that is
 * dispatched on and those run well past the box numbers -- H'17 and H'18
 * page the grid, H'55 and H'1A are the two ways out, and one to nine are the
 * nine cells. Unlike the other module screens this one is a run of
 * comparisons rather than a jump table.
 *
 * Picking a cell works out which colour the pattern belongs to -- H'1B
 * patterns to a colour -- and puts the colour in the slot's record and the
 * pattern's place within it beside it. Where it goes next depends on bit 3
 * of the pattern's attribute byte. */
void module_pick_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return;

    message_show_held(index);
    REG8(0x00114D8EUL) = 0x03;
    if (module_busy() != 0) return;

    if (value == 0x0017) { module_page_back(); return; }
    if (value == 0x0018) { module_page_on();   return; }

    if (value == 0x0055) {
        module_buffers_clear();
        REG8(0x00114D7EUL) = 0x01;
        REG8(0x00114DA1UL) = 0x00;
        REG8(0x0011A41DUL + module_slot12()) = 0x00;
        REG8(0x00114D8BUL) = 0x00;
        REG8(0x00114D9BUL) = 0x01;
        REG8(0x00114D8EUL) = 0x02;
        screen_switch(0x13, 0x01, 0x00);
        return;
    }

    if (value == 0x001A) {
        module_buffers_clear();
        REG8(0x00114D9BUL) = 0x01;
        REG8(0x00114D8EUL) = 0x01;
        screen_switch(0x12, 0x01, 0x00);
        return;
    }

    if ((short)value < 0x0001) return;
    if ((short)value > 0x0009) return;

    {
        const u8 v = (u8)value;

        if (module_pick_past_end(v) != 0) return;
        if (REG16(0x00114D4CUL) & 0x4000) return;
        if (module_link_quiet() == 0) return;
        if (!(REG8(0x00114D51UL) & 0x01)) return;
        if (REG8(0x00114D72UL) != 0) return;

        REG8(0x00114D98UL) = 0x00;
        stitch_reset_current();

        REG8(0x0011A41FUL + module_slot12()) =
            (u8)((u16)(u8)((u8)(REG8(0x00114D8BUL) + v) - 0x01) / 0x1B);
        REG8(0x0011A41AUL + module_slot12()) = (u8)(REG8(0x00114D8BUL) + v);
        REG8(0x0011A41AUL + module_slot12()) =
            (u8)(REG8(0x0011A41AUL + module_slot12())
                 - (u8)((u16)REG8(0x0011A41FUL + module_slot12()) * 0x1B));

        REG16(0x0011B106UL) = value;
        REG8(0x0011F4E6UL) = 0x00;
        REG16(0x0011F292UL) = 0x0000;
        REG8(0x00114D89UL) = 0x00;
        REG16(0x0011F4DCUL) = 0x0000;
        REG16(0x0011F4DEUL) = 0x0000;

        module_pattern_send();

        if (pattern_attr_bit3() != 0) {
            screen_switch(0x38, 0x01, 0x00);
            module_stitch_marks_clear();
            REG8(0x00114D8EUL) = 0x06;
            REG8(0x00114D7FUL) = 0x01;
            REG8(0x00114D96UL) = 0x01;
        } else {
            REG8(0x00114D69UL) = 0x01;
            screen_switch(0x23, 0x01, 0x00);
            REG8(0x00114D73UL) = 0x01;
            REG8(0x00114D72UL) = 0x03;
            REG8(0x00114D96UL) = 0x00;
        }
    }
}

/* H'22BCCC. Screen H'13's press, which is screen H'14's over again with four
 * things changed:
 *
 *   - the state byte it leaves behind is H'02 rather than H'03;
 *   - the H'55 key is the one that wants the module answering, not the cell,
 *     so bit 0 of H'114D51 is tested there instead;
 *   - that key hands on the *other* kind, so H'114DA1 and the slot's H'11A41D
 *     both take one rather than nought, and it goes to screen H'14;
 *   - the H'1A key writes its two bytes the other way round.
 *
 * Everything else -- the paging keys, the nine cells and the colour
 * arithmetic under them -- is the same, and the cell arm ends in the same
 * H'244ADA that cannot be reached in a comparison run. */
void module_pick_screen_b(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return;

    message_show_held(index);
    REG8(0x00114D8EUL) = 0x02;
    if (module_busy() != 0) return;

    if (value == 0x0017) { module_page_back(); return; }
    if (value == 0x0018) { module_page_on();   return; }

    if (value == 0x0055) {
        if (!(REG8(0x00114D51UL) & 0x01)) return;

        module_buffers_clear();
        REG8(0x00114D7EUL) = 0x01;
        REG8(0x00114DA1UL) = 0x01;
        REG8(0x0011A41DUL + module_slot12()) = 0x01;
        REG8(0x00114D8BUL) = 0x00;
        REG8(0x00114D9BUL) = 0x01;
        REG8(0x00114D8EUL) = 0x03;
        screen_switch(0x14, 0x01, 0x00);
        return;
    }

    if (value == 0x001A) {
        module_buffers_clear();
        REG8(0x00114D8EUL) = 0x01;
        REG8(0x00114D9BUL) = 0x01;
        screen_switch(0x12, 0x01, 0x00);
        return;
    }

    if ((short)value < 0x0001) return;
    if ((short)value > 0x0009) return;

    {
        const u8 v = (u8)value;

        if (module_pick_past_end(v) != 0) return;
        if (REG16(0x00114D4CUL) & 0x4000) return;
        if (module_link_quiet() == 0) return;
        if (REG8(0x00114D72UL) != 0) return;

        REG8(0x00114D98UL) = 0x00;
        stitch_reset_current();

        REG8(0x0011A41FUL + module_slot12()) =
            (u8)((u16)(u8)((u8)(REG8(0x00114D8BUL) + v) - 0x01) / 0x1B);
        REG8(0x0011A41AUL + module_slot12()) = (u8)(REG8(0x00114D8BUL) + v);
        REG8(0x0011A41AUL + module_slot12()) =
            (u8)(REG8(0x0011A41AUL + module_slot12())
                 - (u8)((u16)REG8(0x0011A41FUL + module_slot12()) * 0x1B));

        REG16(0x0011B106UL) = value;
        REG8(0x0011F4E6UL) = 0x00;
        REG16(0x0011F292UL) = 0x0000;
        REG8(0x00114D89UL) = 0x00;
        REG16(0x0011F4DCUL) = 0x0000;
        REG16(0x0011F4DEUL) = 0x0000;

        module_pattern_send();

        if (pattern_attr_bit3() != 0) {
            screen_switch(0x38, 0x01, 0x00);
            module_stitch_marks_clear();
            REG8(0x00114D8EUL) = 0x06;
            REG8(0x00114D7FUL) = 0x01;
            REG8(0x00114D96UL) = 0x01;
        } else {
            REG8(0x00114D69UL) = 0x01;
            screen_switch(0x23, 0x01, 0x00);
            REG8(0x00114D73UL) = 0x01;
            REG8(0x00114D72UL) = 0x03;
            REG8(0x00114D96UL) = 0x00;
        }
    }
}

/* H'22BB2A. Screen H'12's press: which of the two kinds of pattern to pick.
 *
 * Three boxes, and the third does nothing at all. The first two do the same
 * eight things in the same order and differ only in what they hand on: key
 * one leaves H'114DA1 and the slot's H'11A41D at nought and goes to screen
 * H'13, key two puts one in both and goes to H'14 -- which is the pair of
 * picking screens the other way round from where they send each other.
 *
 * Both first ask the module to say what it is, and give up with a claim of
 * H'0B when it will not. Only the second also wants bit 0 of H'114D51,
 * which is the module having answered how many patterns it holds. */
void module_kind_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0003, &value, &index) != 0x03) return;

    message_show_held(index);
    REG8(0x00114D8EUL) = 0x01;
    if (module_busy() != 0) return;

    if (value == 0x0001) {
        if (module_identify() == 0) { (void)link_claim(0x0B); return; }

        REG8(0x00114D7EUL) = 0x01;
        REG8(0x00114D9BUL) = 0x01;
        REG8(0x0011A63CUL) = 0x00;
        REG8(0x0011F4E6UL) = 0x00;
        (void)link_claim(0x05);
        pattern_slot_begin();
        screen_switch(0x13, 0x01, 0x00);
        REG8(0x00114DA1UL) = 0x00;
        REG8(0x0011A41DUL + module_slot12()) = 0x00;
        REG8(0x0011A41FUL + module_slot12()) = 0x00;
        REG8(0x00114D8EUL) = 0x02;
        return;
    }

    if (value == 0x0002) {
        if (module_identify() == 0) { (void)link_claim(0x0B); return; }
        if (!(REG8(0x00114D51UL) & 0x01)) return;

        REG8(0x00114D7EUL) = 0x01;
        REG8(0x00114D9BUL) = 0x01;
        REG8(0x0011A63CUL) = 0x00;
        REG8(0x0011F4E6UL) = 0x00;
        (void)link_claim(0x05);
        pattern_slot_begin();
        screen_switch(0x14, 0x01, 0x00);
        REG8(0x00114DA1UL) = 0x01;
        REG8(0x0011A41DUL + module_slot12()) = 0x01;
        REG8(0x0011A41FUL + module_slot12()) = 0x00;
        REG8(0x00114D8EUL) = 0x03;
        return;
    }
}

/* H'2382EE. The pedal seen for the first time: H'114D78 is H'EE while there
 * is nothing to notice and H'FF once it has been, so the pass only happens
 * on the edge. Bit 0 of H'FFFEC4 is the pedal itself. */
void module_pedal_pass(void)
{
    if (REG8(0x00114D78UL) == 0xEE) return;
    if (!(REG8(0x00FFFEC4UL) & 0x01)) return;

    REG8(0x00114D78UL) = 0xFF;
    module_wait_pass();
}

/* H'21A070. Screen H'11's press: the two digits typed on the number pad
 * taken as a pattern number.
 *
 * Only boxes nine and ten are live. Box ten's value H'1A goes back one
 * screen; box nine's H'19 looks the number up: the two digits at H'11B0FE
 * and H'11B0FF make a number, that indexes the word table H'11B2BA points
 * at, and the word there is the pattern. A nought there, or a pattern the
 * machine will not go to, and it gives up to screen H'0F.
 *
 * The two calls to H'206724 write the same field of the pattern's queue
 * record and differ only in the value -- nought when the second digit is
 * four, five when the pattern is number one -- and nothing else in the
 * record is touched, all fourteen other flags being nought.
 *
 * The test on H'57FF80 before the digits are read chooses between two arms
 * that are the same instructions, which is the compiler having duplicated
 * one; it is written once here.
 *
 * Both keys are refused outright while bit 7 of H'114DC6 is up. */
void goto_number_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0009, 0x000A, &value, &index) != 0x03) return;

    message_show_held(index);

    if (value == 0x0019) {
        short typed;
        u16 pattern;

        if (REG8(0x00114DC6UL) & 0x80) return;

        screen_stack_clear();

        typed = (short)(u16)((u16)((u16)0x000A * (u16)REG8(0x0011B0FEUL))
                             + (u16)REG8(0x0011B0FFUL));
        pattern = REG16(REG32(0x0011B2BAUL)
                        + (u32)(long)(short)(u16)((u16)typed << 1));

        if (pattern == 0x0000) {
            screen_switch(0x0F, 0x01, 0x00);
            return;
        }

        if (goto_pattern_number(pattern) == 0) {
            screen_switch(0x0F, 0x01, 0x00);
            return;
        }

        if (REG8(0x0011B0FFUL) == 0x04) {
            queue_record_set(0x01, REG16(0x00FFFEE0UL),
                             0x00, 0xFF, 0x00, 0xFF, 0x01, 0x00, 0x00, 0xFF,
                             0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF);
        } else if (pattern == 0x0001) {
            queue_record_set(0x01, REG16(0x00FFFEE0UL),
                             0x00, 0xFF, 0x00, 0xFF, 0x01, 0x05, 0x00, 0xFF,
                             0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF);
        }

        if (REG8(0x00FFFEC4UL) & 0x01) module_pedal_pass();
        return;
    }

    if (value == 0x001A) {
        if (REG8(0x00114DC6UL) & 0x80) return;

        screen_stack_pop();
        screen_switch(0x10, 0x01, 0x00);
        return;
    }
}

/* H'231F14. A number written centred in a box, in the font at H'119A66.
 *
 * The plain sister of H'231F72: no mark after it, centred rather than
 * right-aligned, and the other font. H'11F2DB is cleared first so that a
 * shorter number cannot leave a digit of the last one behind it. Fourteen
 * places call this, which is more than call any other routine still without
 * a case. */
void module_number_centred(short n, u16 x0, u16 y0, u16 x1, u16 y1)
{
    REG8(0x0011F2DBUL) = 0x00;
    int_to_decimal(n, (char *)0x0011F2D6UL);
    text_draw((const char *)0x0011F2D6UL, x0, y0, x1, y1,
              0x0001, 0x02, (const u8 *)0x00119A66UL);
}
