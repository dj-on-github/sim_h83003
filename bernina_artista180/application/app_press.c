/* The artista 180 application, rebuilt in C: the module's state machines,
 * the touch hit test, and what a press does.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- the module's state machines, and what they call --------------------
 * H'235B0E is the module's own eighteen-state machine, H'2417D4 the nine
 * states that fetch a pattern, and H'2431EE the twelve that start and stop
 * the sewing. They call each other and call back into H'235B0E through
 * H'244DE0, so the whole cluster is written together and declared here.
 */
void module_state_machine(void);
void module_wait_pass(void);
void module_link_lost(void);
void module_screen_step(void);
void module_fetch_step(u8 *step);
void module_run_step(u8 *step);

/* H'244D64. Whether the module is in a state that will take a message. The
 * two states that will are H'04 and H'06, and in H'04 bit 3 of H'FFFEC1
 * takes it away again. Refusals hand the link to an owner first. */
u8 module_can_talk(void)
{
    u8 state;

    if (REG8(0x00114DB9UL) != 0) return 0x00;

    state = REG8(0x00FFFEC0UL);
    if (state != 0x04 && state != 0x06) { link_claim(0x03); return 0x00; }
    if (REG8(0x00FFFEC1UL) & 0x08) { link_claim(0x02); return 0x00; }
    return 0x01;
}

/* H'231E28. A percentage with a "%" after it in the first left-hand label,
 * the same shape as H'231DD2's minutes. */
void label_percent_left(u8 value)
{
    int_to_decimal((short)(u16)value, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;   /* '%' */
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_94((const char *)0x0011F2D6UL);
}

/* H'23E510. Stopping the module: four messages sent one after another, with
 * H'11A63E walking the steps and bit H'8000 of H'11A63A held up for the
 * whole run. The caller's step counter moves on at the end.
 *
 * Step three is the odd one: when the link is busy on the H'21 mask it does
 * not wait, it moves on regardless. */
void module_stop_sequence(u8 *step)
{
    const u8 n = REG8(0x0011A63EUL);

    if (n == 0x00) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG16(0x0011A63AUL) |= 0x8000;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61CUL) = 0x09;
        link_send_start();
        REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
        return;
    }

    if (n == 0x01) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x01;
        link_send_start();
        REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
        return;
    }

    if (n == 0x02) {
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x02;
        link_send_start();
        REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
        return;
    }

    if (n == 0x03) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            (REG8(0x00114D50UL) & 0x22) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
            REG8(0x0011F2A1UL) = 0x0F;
            REG8(0x0011F2A2UL) = 0x01;
            link_send_start();
            REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
            return;
        }
        /* every refusal here still moves on -- step three never waits */
        REG8(0x0011A63EUL) = (u8)(n + 1);
        return;
    }

    if (REG8(0x0011F29EUL) != 0) return;
    if (REG8(0x0011F2B6UL) != 0) return;

    *step = (u8)(*step + 1);
    REG8(0x0011A63EUL) = 0x00;
    REG16(0x0011A63AUL) = (u16)(REG16(0x0011A63AUL) & ~0x8000);
}

/* H'2317B2. Back to screen H'15 with the panel state set to eight. */
void module_to_screen_15(void)
{
    REG8(0x00114D8EUL) = 0x08;
    screen_switch(0x15, 0x01, 0x00);
}

/* H'23DE04. Message H'03/H'0B sent, waiting for the link both before and
 * after -- a real wait, turning the host service over until it is quiet. The
 * answer handed back is whatever H'11F2B6 held at the end, which is always
 * nought, and the caller stores it anyway. */
u8 module_send_0B(void)
{
    for (;;) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            (REG8(0x00114D50UL) & 0x22) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) break;
        rom_host_service();
    }

    if (REG8(0x00114D8CUL) != 0) {
        REG8(0x00114D8CUL) = 0x00;
        REG8(0x00114D8BUL) = 0x00;
    }

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x0B;
    link_send_start();

    for (;;) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) break;
        rom_host_service();
    }

    return 0x00;
}

/* H'243A8C. The run control the two big machines share. What arrives says
 * what to do: H'00 arms it, H'01 starts it, and anything else runs one pass
 * of the sub-machine in H'11F52D.
 *
 * The passes fall into one another: H'00 falls into H'01 and H'01 falls into
 * H'02, so one call can do three steps. H'11F52F says why it is stopping --
 * H'01 the module finished, H'02 or H'03 something asked it to -- and that
 * chooses which screen it goes back to. Everything past H'0B, and every
 * number the table does not know, ends in the same reset.
 */
u8 module_run_control(u8 what)
{
    u8 n, v;

    if (what == 0x00) {
        if (REG8(0x0011F52FUL) >= 0x0A) return 0x00;

        REG8(0x00114D4FUL) &= (u8)~0x20;
        if (REG8(0x0011F52FUL) != 0) {
            REG8(0x0011F52DUL) = 0x0A;
            REG8(0x0011F52EUL) = 0x00;
        } else {
            REG8(0x0011F52CUL) = 0x00;
            REG8(0x0011F52DUL) = 0x00;
            REG8(0x0011F52FUL) = 0x00;
            REG8(0x0011F52EUL) = 0x00;
        }
        return 0x01;
    }

    if (what == 0x01) {
        if (REG8(0x0011F52CUL) != 0) return 0x00;

        REG8(0x00114D4FUL) &= (u8)~0x20;
        REG8(0x0011F52CUL) = 0x01;
        REG8(0x0011F52DUL) = 0x00;
        REG8(0x0011F52FUL) = 0x00;
        REG8(0x0011F52EUL) = 0x00;
        REG8(0x0011F530UL) = 0x01;
        return 0x01;
    }

    n = REG8(0x0011F52DUL);

    if (n == 0x00) {
        if (REG8(0x0011F52CUL) == 0) return 0x01;

        if (REG8(0x00114D4FUL) & 0x20) {
            REG8(0x0011F531UL) = 0x01;
            REG8(0x00114D73UL) = 0x01;
            REG8(0x00114D4FUL) &= (u8)~0x20;
            v = REG8(0x0011F530UL);
            if (v < 0x63) { v = (u8)(v + 1); REG8(0x0011F530UL) = v; }
            if (v < 0x64) label_percent_left(v);
        }

        if (!(REG8(0x00FFFEDBUL) & 0x04)) return 0x01;

        label_percent_left(0x62);
        REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        n = 0x01;
    }

    if (n == 0x01) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
            REG8(0x00114D4FUL) &= (u8)~0x20;
            REG8(0x0011F52CUL) = 0x00;
            REG8(0x0011F52DUL) = 0x00;
            REG8(0x0011F52FUL) = 0x00;
            REG8(0x0011F52EUL) = 0x00;
            return 0x01;
        }
        if (!(REG8(0x00114D4FUL) & 0x20)) return 0x01;

        REG8(0x00114D4FUL) &= (u8)~0x20;
        REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        n = 0x02;
    }

    if (n == 0x02) {
        if (REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
            REG8(0x0011F2A1UL) = 0x0E;
            REG8(0x0011F2A2UL) |= 0x04;
            link_send_start();
            label_percent_left(0x64);
            if (REG8(0x0011A63DUL) != 0) REG8(0x0011F52FUL) = 0x02;
            if (REG8(0x00114D7DUL) != 0) REG8(0x0011F52FUL) = 0x03;
            if (REG8(0x001040B4UL) != 0) REG8(0x0011F52FUL) = 0x01;
            REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        }
        return 0x01;
    }

    if (n == 0x03) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            (REG8(0x00114D50UL) & 0x22) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0)
            REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        return 0x01;
    }

    if (n == 0x04) return 0x01;

    if (n == 0x0A) {
        if (REG8(0x001040B4UL) == 0 && REG8(0x0011A63DUL) == 0 &&
            REG8(0x00114D7DUL) == 0 && REG8(0x00114D66UL) == 0)
            REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        return 0x01;
    }

    if (n == 0x0B) {
        const u8 why = REG8(0x0011F52FUL);

        if (why == 0x01) {
            REG8(0x00114D8EUL) = 0x00;
            REG8(0x001040B5UL) = module_send_0B();
            REG8(0x00114D72UL) = 0x01;
            module_to_screen_15();
        } else if (why >= 0x02 && why <= 0x03) {
            REG8(0x00114D93UL) = 0x00;
            REG8(0x00114D98UL) = 0x00;
            REG8(0x0011F30EUL) = 0x00;
            REG8(0x00114D72UL) = 0x01;
            module_to_screen_15();
        } else {
            REG8(0x0011F52CUL) = 0x00;
        }

        if (REG8(PAT_B(0x03)) == 0x03) REG8(0x00114D92UL) = 0xFF;
        REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
    }

    REG8(0x00114D4FUL) &= (u8)~0x20;
    REG8(0x0011F52CUL) = 0x00;
    REG8(0x0011F52DUL) = 0x00;
    REG8(0x0011F52FUL) = 0x00;
    REG8(0x0011F52EUL) = 0x00;
    return 0x01;
}

/* ---- the module key, and what it reaches -------------------------------
 * H'237E3C is the handler for a key press when the embroidery module is
 * attached, and it is a jump table of twelve: the key codes H'6D, H'70-H'75,
 * H'77-H'79, H'7D and H'81 that H'21F68C names. Nine routines under it are
 * written here; the handler itself waits on the module's own state machine
 * at H'235B0E, which is not written yet.
 */

void link_delay(u16 units);
void link_send_start(void);

/* H'23E45A. Where the module's replies land. Two instructions. */
u32 module_reply_buffer(void)
{
    return 0x00104C90UL;
}

/* H'230E6E. The first screen store emptied, a word at a time, all H'2580 of
 * them -- one whole screen. */
void screen_store1_clear(void)
{
    u32 p;
    long n;

    p = 0x000ECB10UL;
    for (n = 0; n < 0x2580L; n++) { REG16(p) = 0x0000; p += 2; }
}

/* H'230EA8 and H'230EF4. The embroidery panel put away into the first store:
 * a box from H'26,H'53 to H'C1,H'EA, and only when H'11F4E6 says there is
 * one. The ROM has it twice, byte for byte, the same way it has H'23521E and
 * H'235230 -- so it is written twice here as well. */
void embroidery_panel_save(void)
{
    if (REG8(0x0011F4E6UL) == 0) return;

    region_copy(0x0026, 0x0053, 0x00C1, 0x00EA, 0x0053,
                LCD_FRAME_A, 0x000ECB10UL);
}

void embroidery_panel_save_b(void)
{
    if (REG8(0x0011F4E6UL) == 0) return;

    region_copy(0x0026, 0x0053, 0x00C1, 0x00EA, 0x0053,
                LCD_FRAME_A, 0x000ECB10UL);
}

/* H'236E9A. The module's cursor rubbed out: a twenty-five pixel box around
 * H'11F4DC, H'11F4DE fetched back from the third store, and the position
 * forgotten. H'114D99 is set on the way in whatever else happens. */
void module_cursor_erase(void)
{
    REG8(0x00114D99UL) = 0x01;

    if (REG8(0x00114D98UL) != 0x01) return;

    REG8(0x00114D98UL) = 0x00;

    if (REG16(0x0011F4DCUL) != 0 && REG16(0x0011F4DEUL) != 0)
        region_copy((u16)(REG16(0x0011F4DCUL) - 0x0C),
                    (u16)(REG16(0x0011F4DEUL) - 0x0C),
                    (u16)(REG16(0x0011F4DCUL) + 0x0C),
                    (u16)(REG16(0x0011F4DEUL) + 0x0C),
                    (u16)(REG16(0x0011F4DEUL) - 0x0C),
                    0x000F6110UL, LCD_FRAME_A);

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;
}

/* H'2426F0. Bit 1 of H'11A63C set when there is a pattern to be going on
 * with: either one waiting in H'11A640, or the current slot's record says
 * kind three. */
void pattern_mark_ready(void)
{
    if (REG8(0x0011A640UL) == 0 && REG8(PAT_B(0x03)) != 0x03) return;

    REG8(0x0011A63CUL) |= 0x02;
}

/* H'23191C. Asks the module to go home, and says whether it was already on
 * its way. H'114D93 is the ask; H'FFFEC6 has to be at rest, meaning zero or
 * five. When there is no ask, a module already homing has its step counter
 * put back to one and the answer is H'01.
 *
 * The two branches share their tail, so a module that is asked while
 * H'FFFEC6 is busy falls into the second test rather than leaving. */
u8 module_home_request(void)
{
    if (REG8(0x00114D93UL) != 0) {
        const u8 state = REG8(0x00FFFEC6UL);

        if (state == 0x00 || state == 0x05) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D66UL) = 0x01;
            REG8(0x00114D62UL) = 0x08;
            REG8(0x00114D65UL) = 0x00;
            REG8(0x00114D94UL) = 0x00;
            if (REG8(0x00114D95UL) != 0) {
                REG8(0x00114D95UL) = 0x00;
                REG8(0x00114D50UL) &= (u8)~0x01;
                REG8(0x00114D50UL) &= (u8)~0x02;
            }
            return 0x00;
        }
    }

    if (REG8(0x00114D66UL) != 0) {
        REG8(0x00114D65UL) = 0x01;
        return 0x01;
    }
    return 0x00;
}

/* H'2431C2. The end of a talk to the module, but only from three of the
 * hardware's states. ITU1 is handed back, the machine parked, and ITU1
 * borrowed again -- in that order, which is the order the ROM has. */
void module_talk_end(void)
{
    const u8 state = REG8(0x00FFFEC0UL);

    if (state != 0x04 && state != 0x06 && state != 0x07) return;

    REG8(0x00114DA0UL) = 0x01;
    itu1_return();
    module_park();
    itu1_borrow();
}

/* H'244A2A. The module started again from nothing: the link brought up, a
 * message H'04/H'06 sent, and a quarter-second wait for the answer. Bit 7 of
 * H'114D51 is the answer having arrived. */
void module_restart(void)
{
    if (REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) return;

    sci0_module_init();
    link_delay(0x000A);
    REG8(0x0011F2A1UL) = 0x04;
    REG8(0x0011F2A2UL) = 0x06;
    link_send_start();
    link_delay(0x00FA);

    if (!(REG8(0x00114D51UL) & 0x80)) return;

    REG8(0x00FFFEC4UL) |= 0x01;
    REG8(0x00FFFEC4UL) |= 0x20;
    REG8(0x00114D78UL) = 0x03;
    REG8(0x00114DA0UL) = 0x00;
    REG8(0x00114DBAUL) = 0x01;
    REG8(0x00114DBBUL) = 0x00;
    module_talk_end();
}

/* H'249DE8. Waits for the module to name itself: five bytes at the head of
 * the reply buffer against five in the machine's own identity block at
 * H'200103, tried up to H'9C4 times with a delay between. Returns H'01 when
 * they matched and H'00 when the tries ran out.
 *
 * The two sides are compared as words, and the ROM's byte is sign extended
 * while the module's is not, so any expected byte of H'80 or over could
 * never match. None of the five is.
 */
u8 module_identify(void)
{
    short n;

    for (n = 0; n <= 0x09C4; n++) {
        const u32 buf = module_reply_buffer();
        u8 i, same = 0;

        for (i = 0; i <= 0x04; i++) {
            if ((u16)REG8(buf + i) ==
                (u16)(short)(signed char)REG8(0x00200103UL + i)) same++;
        }
        if (same >= 0x05) break;
        link_delay(0x0001);
    }

    return n >= 0x09C4 ? 0x00 : 0x01;
}

/* H'24ADF0. The ROM's memmove, and it is a proper one: an overlap where the
 * source is below the destination is copied backwards so it does not eat its
 * own tail. The loop count is tested before the decrement, so a length of
 * zero copies nothing. */
void *mem_move(void *dst, const void *src, u32 len)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    u32 n = len;

    if ((u32)s <= (u32)d && (u32)s + len >= (u32)d) {
        d += len;
        s += len;
        while (n != 0) { n--; *--d = *--s; }
        return dst;
    }
    while (n != 0) { n--; *d++ = *s++; }
    return dst;
}

/* H'21F36E. One of four stored screens copied to or from the front buffer.
 * They sit at H'0ECB10 and every H'4B00 after it -- one whole screen each --
 * and [out] says which way the copy goes. */
void screen_store(u8 slot, u8 out)
{
    u32 at;

    switch (slot) {
    case 0x01: at = 0x000ECB10UL; break;
    case 0x02: at = 0x000F1610UL; break;
    case 0x03: at = 0x000F6110UL; break;
    case 0x04: at = 0x000FAC10UL; break;
    default: return;
    }

    if (out != 0) region_copy(0x0000, 0x0000, 0x013F, 0x00EF, 0x0000,
                              LCD_FRAME_A, at);
    else          region_copy(0x0000, 0x0000, 0x013F, 0x00EF, 0x0000,
                              at, LCD_FRAME_A);
}

void link_delay(u16 units);

/* H'248668. Waits for the link to go quiet, giving up after a hundred turns
 * of H'0A units each. Returns 1 when it went quiet and 0 when it did not.
 *
 * The test is written out twice in the original, the same three conditions
 * each time, and then a fourth on bit 5 of H'114D50. Reproduced as it is. */
u8 link_wait_idle(void)
{
    u8 n;

    for (n = 0; n < 0x64; n++) {
        if ((REG8(0x114D50UL) & 0x21) == 0 &&
            REG8(0x11F29EUL) == 0 && REG8(0x11F2B6UL) == 0 &&
            (REG8(0x114D50UL) & 0x22) == 0 &&
            REG8(0x11F29EUL) == 0 && REG8(0x11F2B6UL) == 0 &&
            !(REG8(0x114D50UL) & 0x20)) {
            return 0x01;
        }
        link_delay(0x000A);
    }
    return 0x00;
}

void picker_arrows(u16 back_box, u16 on_box, u8 fresh)
{
    if (fresh != 0) {
        if ((short)PICK_POS > (short)PICK_FIRST) {
            REG8(0x11B3D6UL) = 0x01;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_ON);
        } else {
            REG8(0x11B3D6UL) = 0x00;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_OFF);
        }
        if ((short)PICK_POS < (short)PICK_LAST) {
            REG8(0x11B3D7UL) = 0x01;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_ON);
        } else {
            REG8(0x11B3D7UL) = 0x00;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_OFF);
        }
        return;
    }

    if (PICK_POS == PICK_FIRST) {
        if (REG8(0x11B3D6UL) != 0) {
            REG8(0x11B3D6UL) = 0x00;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_OFF);
        }
    } else if ((short)PICK_POS > (short)PICK_FIRST) {
        if (REG8(0x11B3D6UL) == 0) {
            REG8(0x11B3D6UL) = 0x01;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_ON);
        }
    }

    if (PICK_POS == PICK_LAST) {
        if (REG8(0x11B3D7UL) != 0) {
            REG8(0x11B3D7UL) = 0x00;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_OFF);
        }
    } else if ((short)PICK_POS < (short)PICK_LAST) {
        if (REG8(0x11B3D7UL) == 0) {
            REG8(0x11B3D7UL) = 0x01;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_ON);
        }
    }
}

/* H'21341E. The box carrying the current speed found, lit, and its item
 * drawn in the preview panel. */
void hitbox_select_current(u16 first, u16 last)
{
    const u16 box = hitbox_find(first, last, REG16(0xFFFEE0UL), 0x01);

    if (hitbox_kind(box) == 0x01) hitbox_set_state(box, box, 0x00, 0);
    hitbox_set_state(box, box, 0x01, 0);
    item_preview(REG16(0xFFFEE0UL));
}

/* ---- the touch hit test -----------------------------------------------
 * H'210EE2, and H'211252 beside it. Which box the operator pressed.
 *
 * The panel leaves a raw reading in H'FFFED9 and H'FFFEDA and the two are
 * turned into screen coordinates by a straight line each -- gain at H'11A87E
 * and H'11A882, offset at H'11A886 and H'11A88A, all four single-precision
 * floats. Below 5 on either axis means nothing is being touched.
 *
 * The answer is one of three codes: 2 for "nothing to do", 3 for "this box,
 * act on it". The value the box stands for and its index go back through
 * two pointers.
 *
 * H'211252 is the same thing for a press that arrives down the serial link
 * rather than off the glass: H'11F547 and H'11F548 holding H'CA say a host
 * is driving, and then the box number comes from H'11F549.
 */
static float float_at(u32 a)
{
    union { u32 u; float f; } v;

    v.u = REG32(a);
    return v.f;
}

/* H'21548A. What a press actually does. Written out below the two hit
 * tests, once everything it calls exists. */
void screen_action(u16 value, u16 index, u8 second);

/* The value a box stands for, and the two halves of a range on the screens
 * that edit one. */
static u16 hitbox_value_of(u32 e, u16 *out_value)
{
    const u32 list = REG32(e + 0x0C);

    if (list == 0) {
        *out_value = REG16(e + 0x08);
        return *out_value;
    }

    *out_value = REG16(list +
        (u32)(long)(short)(u16)(REG16(e + 0x08) << 1));

    /* On the three screens that edit a range, a box flagged as one of a
     * pair pushes the old value down into H'11A188 and takes H'11A186 for
     * itself. On every other screen -- and on those three for a box that is
     * not one of a pair -- a flagged box clears both instead. */
    if ((REG8(0x11A169UL) == 0x44 || REG8(0x11A169UL) == 0x30 ||
         REG8(0x11A169UL) == 0x45) && REG8(e + 0x0A) == 0x01) {
        REG16(0x11A188UL) = REG16(0x11A186UL);
        REG16(0x11A186UL) = REG16(e + 0x08);
        return *out_value;
    }
    if (REG8(e + 0x0A) == 0x01) {
        REG16(0x11A186UL) = 0x0000;
        REG16(0x11A188UL) = 0x0000;
    }
    return *out_value;
}

/* What both of them do once a box has been picked: the beep, the action if
 * one is due, and the hold-off that keeps one press from reading as many.
 * [seen] is where the last value is remembered -- the two have one each. */
static u8 hitbox_press(u32 e, u16 value, u16 index, u32 seen)
{
    if (touch_allowed(value) == 0) return 0x02;

    message_beep((u16)((REG8(e + 0x0A) == 0x01) ? 0x0001 : 0x0002));

    if (REG8(0x11A16FUL) != 0) {
        screen_action(value, index,
                      (u8)((REG8(e + 0x0A) == 0x01) ? 0x00 : 0x01));
        REG16(seen) = 0xFFFF;
        return 0x02;
    }

    if (value != REG16(seen)) {
        REG16(seen) = value;
        touch_holdoff_start();
        return 0x03;
    }

    if (touch_holdoff_done() != 0) return 0x03;
    return 0x02;
}

/* H'211252. */
u8 remote_hit(u16 first, u16 last, u16 *out_value, u16 *out_index)
{
    const u16 box = (u16)REG8(0x11F549UL);
    u32 e;

    if (REG8(0x11F547UL) != 0xCA || box == 0) {
        REG8(0x11F549UL) = 0x00;
        REG16(0x11A1A2UL) = 0xFFFF;
        REG8(0x11A1A1UL) = 0x00;
        return 0x02;
    }

    if (REG8(0x11A1A0UL) != REG8(0x11A169UL)) {
        REG8(0x11A1A0UL) = REG8(0x11A169UL);
        REG8(0x11B2CFUL) = 0x01;
    }
    REG8(0x11A1A1UL) = 0x01;

    if ((short)box < (short)first || (short)box > (short)last) {
        REG8(0x11F549UL) = 0x00;
        return 0x02;
    }

    *out_index = box;
    e = HITBOX_TABLE + (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    hitbox_value_of(e, out_value);
    REG8(0x11F549UL) = 0x00;

    return hitbox_press(e, *out_value, box, 0x11A1A2UL);
}

/* H'210EE2. */
u8 touch_hit(u16 first, u16 last, u16 *out_value, u16 *out_index)
{
    u16 ox, oy;
    u32 table;
    short i;

    if (REG8(0x11F547UL) == 0xCA && REG8(0x11F548UL) == 0xCA) {
        return remote_hit(first, last, out_value, out_index);
    }

    if (REG8(0xFFFED9UL) < 0x05 || REG8(0xFFFEDAUL) < 0x05) {
        REG16(0x11A19EUL) = 0xFFFF;
        REG8(0x11A19DUL) = 0x00;
        return 0x02;
    }

    if (REG8(0x11A19CUL) != REG8(0x11A169UL)) {
        REG8(0x11A19CUL) = REG8(0x11A169UL);
        REG8(0x11B2CEUL) = 0x01;
    }

    /* A screen that has just changed swallows the press that is still down
     * from the last one. */
    if (REG8(0x11A19DUL) != 0 && REG8(0x11B2CEUL) != 0) return 0x02;
    if (REG8(0x11A19DUL) == 0 && REG8(0x11B2CEUL) != 0) REG8(0x11B2CEUL) = 0x00;
    REG8(0x11A19DUL) = 0x01;

    REG16(0x11B102UL) = (u16)(int)
        ((float)(u32)REG8(0xFFFED9UL) * float_at(0x11A87EUL) +
         float_at(0x11A886UL));
    REG16(0x11B104UL) = (u16)(int)
        ((float)(u32)REG8(0xFFFEDAUL) * float_at(0x11A882UL) +
         float_at(0x11A88AUL));

    ox = HITBOX_X0;
    oy = HITBOX_Y0;
    table = HITBOX_TABLE;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = table +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        if (REG8(e + 0x10) == 0x02) continue;
        if ((short)(REG16(e + 0x00) + ox) > (short)REG16(0x11B102UL)) continue;
        if ((short)(REG16(e + 0x04) + ox) < (short)REG16(0x11B102UL)) continue;
        if ((short)(REG16(e + 0x02) + oy) > (short)REG16(0x11B104UL)) continue;
        if ((short)(REG16(e + 0x06) + oy) < (short)REG16(0x11B104UL)) continue;

        *out_index = (u16)i;
        hitbox_value_of(e, out_value);

        return hitbox_press(e, *out_value, (u16)i, 0x11A19EUL);
    }
    return 0x02;
}

/* Every one of these is a leaf of the dispatch above, and every one of them
 * is four instructions that pick a record and branch out. They are the table
 * entries, not routines, so they are written above as numbers:
 *
 * H'21562E  H'2156AE  H'2158D6  H'215912  H'21592C  H'215946  H'215960  H'21597A
 * H'215994  H'2159AE  H'2159C8  H'2159E2  H'2159FC  H'215A16  H'215A30  H'215A6C
 * H'215A86  H'215AA0  H'215ADC  H'215AF6  H'215B10  H'215B2A  H'215B44  H'215B5E
 * H'215B78  H'215B92  H'215BAC  H'215BC6  H'215BE0  H'215BFA  H'215C14  H'215C2E
 * H'215C48  H'215C62  H'215C7C  H'215C96  H'215CB0  H'215CCA  H'215CE4  H'215CFE
 * H'215D18  H'215D32  H'215D4C  H'215D88  H'215DA6  H'215E1E  H'215F1C  H'215FF4
 * H'21606C  H'2160B8  H'2160D2  H'2160EC  H'216106  H'216120  H'21613A  H'216154
 * H'21616E  H'216188  H'2161A2  H'2161BA  H'21623E  H'216258  H'216272  H'21628C
 * H'2162A6  H'2162C0  H'2162DA  H'2162F4  H'21630E  H'216328  H'216342  H'21635C
 * H'216376  H'216390  H'2163C2  H'216446  H'216460  H'21647A  H'216494  H'2164AE
 * H'2164C8  H'2164E2  H'2164FC  H'216516  H'216530  H'21654A  H'216564  H'21657C
 * H'216654  H'21666C  H'216836  H'216850  H'21686A  H'216884  H'21689E  H'2168B8
 * H'2168D2  H'2168EC  H'216906  H'216920  H'21693A  H'216954  H'21696E  H'216988
 * H'2169A2  H'2169BC  H'2169D6  H'2169F0  H'216A0A  H'216A24  H'216A3E  H'216A58
 * H'216A72  H'216A8C  H'216AA6  H'216AC0  H'216ADA  H'216AF4  H'216B0E  H'216B28
 * H'216B42  H'216B5C  H'216B76  H'216B90  H'216BAA  H'216BC4  H'216BDE  H'216BF8
 * H'216C12  H'216C2C  H'216C46  H'216C60  H'216C9C  H'216CFA  H'216D12  H'216D2A
 * H'216D42
 */

/* The value on the box, 1 to H'82, on the twenty screens that share
 * one table. H'2156CE. */
static const u16 help_by_value[] = {
    0x00A8, 0x00B4, 0x00A4, 0x00B0, 0x00BC, 0x00C0, 0x00C4, 0xFFF2,
    0x00A0, 0x00AC, 0x00CC, 0x009C, 0x00E8, 0x00DC, 0x00E0, 0x00E4,
    0x0108, 0x00F4, 0x00F8, 0x00FC, 0x00F0, 0xFFFF, 0x0124, 0x0128,
    0x00EC, 0x0110, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x011C,
    0x0120, 0xFFFF, 0xFFFF, 0xFFF3, 0x0104, 0x00D8, 0x0100, 0x010C,
    0x01C0, 0x01B4, 0x01B8, 0x01C4, 0xFFF4, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFF1, 0xFFF1, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0118, 0x01B0,
    0xFFFF, 0x00B8,
};

/* H'21608C. */
static const u16 help_menu_35[] = {
    0x0168, 0x012C, 0x012C, 0x0170, 0x016C, 0x0164, 0x01D8, 0x0174,
    0x01EC, 0xFFFF, 0x01EC,
};

/* H'2161DA. */
static const u16 help_menu_21[] = {
    0x0168, 0x017C, 0x0178, 0x0148, 0x013C, 0x013C, 0x013C, 0x0174,
    0x0130, 0x0134, 0x0138, 0x0140, 0x01DC, 0x0144, 0x0144, 0x0144,
    0x0144, 0x0144, 0x0144, 0x0144, 0x0144, 0x0144, 0x014C, 0x014C,
    0x0150,
};

/* H'2163E2. */
static const u16 help_menu_22[] = {
    0x0168, 0x0154, 0x0154, 0x0154, 0xFFFF, 0xFFFF, 0xFFFF, 0x0164,
    0x0158, 0x0158, 0x0158, 0x0140, 0x015C, 0x015C, 0x015C, 0x0160,
    0x0160, 0x0160, 0x01F0, 0x01F0, 0x01F0, 0xFFFF, 0xFFFF, 0xFFFF,
    0x0150,
};

/* The kinds of pattern, and a record for each. The keys are at
 * H'2166F2 and the handlers at H'21675A, counting down as the search
 * counts up -- the same reversed-table idiom the service dispatcher
 * uses. */
static const u16 help_kind_key[] = {
    0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008,
    0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F, 0x0010,
    0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018,
    0x0019, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D,
    0x002E, 0x002F, 0x0030, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x0146,
    0x0148, 0x015A, 0x015B, 0x015C, 0x015D, 0x015E,
};
static const u16 help_kind_value[] = {
    0x0004, 0x0008, 0x000C, 0x0010, 0x0014, 0x0018, 0x0018, 0x001C,
    0x0020, 0x0024, 0x0028, 0x002C, 0x0030, 0x0034, 0x0038, 0x003C,
    0x0040, 0x0028, 0x0030, 0x0024, 0x0044, 0x0048, 0x0048, 0x004C,
    0x004C, 0x0184, 0x0188, 0x0190, 0x0194, 0x0198, 0x019C, 0x01A0,
    0x01A4, 0x01A8, 0x01AC, 0x0050, 0x0054, 0x0058, 0x005C, 0x0060,
    0x0064, 0x0068, 0x006C, 0x0070, 0x0074, 0x0078, 0x0078, 0x018C,
    0xFFF5, 0xFFF5, 0xFFF5, 0xFFF5, 0xFFF5, 0xFFF5,
};

/* And if the kind is not in that table, the pattern's category,
 * H'05 to H'0F. H'216CCE. */
static const u16 help_by_category[] = {
    0x0088, 0x0090, 0x0088, 0x0088, 0x0090, 0x0088, 0x0088, 0x0090,
    0x0088, 0x0090, 0x008C,
};

/* One record chosen. The five values above H'FF00 are the ones the original
 * spells out with a test rather than a table entry. */
static void help_pick(u16 code)
{
    u16 offset = code;

    switch (code) {
    case HELP_NONE:
        return;
    case HELP_SCREEN35:
        offset = (u16)((REG8(0x11A16DUL) == 0x35) ? 0x0094 : 0x0098);
        break;
    case HELP_MODULE_1E0:
        offset = (u16)((REG8(0x57FF80UL) == 0xAA) ? 0x01E0 : 0x00C8);
        break;
    case HELP_MODULE_0D4:
        offset = (u16)((REG8(0x57FF80UL) == 0xAA) ? 0x00D4 : 0x00D0);
        break;
    case HELP_MODULE_1CC:
        offset = (u16)((REG8(0x57FF80UL) == 0xAA) ? 0x01CC : 0x01C8);
        break;
    case HELP_MODULE_1D4:
        offset = (u16)((REG8(0x57FF80UL) == 0xB4) ? 0x01D4 : 0x007C);
        break;
    default:
        break;
    }
    REG32(0x115D12UL) = REG32(TABLE_SLOT(0) + (u32)offset);
}

/* No record: the screen the press was on is put back, and on most of the
 * paths the picker cursor is set going again. */
static void help_give_up(int cursor)
{
    screen_switch(REG8(0x11A16DUL), 0x04, 0x00);
    if (cursor) picker_cursor(0x03);
}

/* The eleven screens that do not share the big table. */
static void help_from(const u16 *table, u16 n, u16 v, int cursor)
{
    if ((u16)(v - 1) > (u16)(n - 1)) {
        help_give_up(cursor);
        return;
    }
    if (table[v - 1] == HELP_NONE) {
        help_give_up(cursor);
        return;
    }
    help_pick(table[v - 1]);
}

/* H'21562E. Screens H'25 and H'26: values 1 to 4 share one record, and
 * which one depends on whether the module is fitted. */
static void help_screen_25(u16 v)
{
    if ((short)v >= 0x0001 && (short)v <= 0x0004) {
        help_pick((u16)((REG8(0x57FF80UL) == 0xAA) ? 0x01D0 : 0x0084));
        return;
    }
    if (v == 0x0005) {
        help_pick(0x0080);
        return;
    }
    help_give_up(0);
}

/* H'215DA6, H'215E1E, H'215F1C, H'215FF4, H'21657C. Five screens with a
 * handful of values each, written out as tests rather than as a table. */
static void help_screen_13(u16 v)
{
    if (v == 0x0017)      help_pick(0x0124);
    else if (v == 0x0018) help_pick(0x0128);
    else if (v == 0x001A) help_pick(0x0110);
    else                  help_give_up(0);
}

static void help_screen_41(u16 v)
{
    if (v == 0x0017)      help_pick(0x0124);
    else if (v == 0x0018) help_pick(0x0128);
    else if (v == 0x0040) help_pick(0x011C);
    else if (v == 0x0041) help_pick(0x0120);
    else if (v == 0x000E) help_pick(0x00DC);
    else if (v == 0x0019) help_pick(0x00EC);
    else if (v == 0x001A) help_pick(0x0110);
    else                  help_give_up(1);
}

static void help_screen_38(u16 v)
{
    if (v == 0x0010)      help_pick(0x0124);
    else if (v == 0x0011) help_pick(0x0128);
    else if (v == 0x0012) help_pick(0x011C);
    else if (v == 0x0013) help_pick(0x0120);
    else if (v == 0x0014) help_pick(0x00DC);
    else if (v == 0x0015) help_pick(0x0150);
    else                  help_give_up(0);
}

static void help_screen_24(u16 v)
{
    if (v == 0x0001)      help_pick(0x0170);
    else if (v == 0x0002) help_pick(0x0170);
    else if (v == 0x0019) help_pick(0x0150);
    else                  help_give_up(0);
}

static void help_screen_37(u16 v)
{
    if (v == 0x0001)      help_pick(0x0164);
    else if (v == 0x0002) help_pick(0x0180);
    else if (v == 0x0003) help_pick(0x01E4);
    else if (v == 0x0004) help_pick(0x01E8);
    else if (v == 0x0014) help_pick(0x00DC);
    else if (v == 0x0015) help_pick(0x0150);
    else                  help_give_up(0);
}

/* ---- the other half: a press on the second box of a pair ---------------
 * H'21666C. Here the record comes from what kind of pattern the value names
 * rather than from the value itself. Field H'14 of the descriptor is the
 * kind and field H'17 the category, and the category is the fallback.
 */
static void help_by_pattern(u16 v)
{
    const u32 rec = ITEM_TABLE +
        (u32)(long)(short)(u16)(ITEM_STRIDE * v);
    const u16 kind = REG16(rec + 0x14);
    u16 k;

    for (k = 0; k < 54; k++) {
        if (help_kind_key[k] == kind) {
            help_pick(help_kind_value[k]);
            return;
        }
    }

    {
        const u8 cat = REG8(rec + 0x17);

        if ((u8)(cat - 5) > 0x0A) {
            help_give_up(1);
            return;
        }
        help_pick(help_by_category[cat - 5]);
    }
}

static void help_no_flag(u16 v)
{
    const u8 screen = REG8(0x11A16DUL);

    if (screen == 0x13 || screen == 0x14 || screen == 0x38) {
        screen_switch(screen, 0x04, 0x00);
        return;
    }
    if (screen == 0x41) {
        screen_switch(screen, 0x04, 0x00);
        REG8(0x11B0A9UL) = 0x01;
    }
    help_by_pattern(v);
}

/* H'21548A. */
void screen_action(u16 value, u16 index, u8 second)
{
    const u8 from = REG8(0x11A169UL);

    REG8(0x11A16FUL) = 0x00;

    /* The two screens that are themselves the help are not remembered, and
     * a press on one of them does nothing at all. */
    if (from != 0x08 && from != 0x3E) screen_remember(0x04);

    if (hitbox_kind(index) == 0) message_show_held(index);

    if (REG8(0x11A169UL) == 0x08 || REG8(0x11A169UL) == 0x3E) return;

    screen_switch(0x08, 0x04, 0x00);

    if (second == 0) {
        help_no_flag(value);
        return;
    }

    switch (REG8(0x11A16DUL)) {
    case 0x25: case 0x26:
        help_screen_25(value);
        break;
    case 0x13: case 0x14:
        help_screen_13(value);
        break;
    case 0x41:
        help_screen_41(value);
        break;
    case 0x38:
        help_screen_38(value);
        break;
    case 0x24:
        help_screen_24(value);
        break;
    case 0x23:
        help_from(help_menu_35, 11, value, 0);
        break;
    case 0x15:
        help_from(help_menu_21, 0x19, value, 0);
        break;
    case 0x16:
        help_from(help_menu_22, 0x19, value, 0);
        break;
    case 0x37:
        help_screen_37(value);
        break;
    case 0x02: case 0x03: case 0x04: case 0x07: case 0x09:
    case 0x0A: case 0x0C: case 0x0D: case 0x2C: case 0x2D:
    case 0x30: case 0x33: case 0x34: case 0x35: case 0x36:
    case 0x3F: case 0x42: case 0x45: case 0x46: case 0x47:
        if ((u16)(value - 1) > 0x0081) {
            help_give_up(1);
            break;
        }
        if (help_by_value[value - 1] == HELP_NONE) {
            help_give_up(1);
            break;
        }
        help_pick(help_by_value[value - 1]);
        break;
    default:
        /* Everything else, and anything outside H'02 to H'47. */
        screen_switch(REG8(0x11A16DUL), 0x04, 0x00);
        break;
    }
}
