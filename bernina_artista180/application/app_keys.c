/* The front panel: the key matrix, and the touch panel read on the same
 * pass. See app_keys.h for how the two fit together.
 *
 * These were in app_motor.c and app_flash.c, which is where reading the ROM
 * in address order first put them; neither is what they are for.
 */
#include "app.h"
#include "app_keys.h"

/* H'208E6C. The three port C strobes driven high. Called by `port_c_init`
 * below and three times by `key_banks_read`, in both cases straight after
 * those same pins are turned round to inputs -- on this part writing the
 * data register of an input pin sets what the pin will read as the moment
 * it becomes an output again.
 *
 * The comment here used to say "called only from the routine below", which
 * was true of where it sat in app_flash.c and not of the code. */
void port_c_bits_high(void)
{
    PCDR |= 0x01;
    PCDR |= 0x02;
    PCDR |= 0x40;
}
/* H'208FE8. Port C bits 0, 1 and 6 to inputs, P4 bit 5 to an input, then
 * the three data bits above. */
void port_c_init(void)
{
    u8 v;

    v = (u8)(PCDDR_SHADOW & (u8)~0x01); PCDDR = v;
    v = (u8)(v            & (u8)~0x02); PCDDR = v;
    v = (u8)(v            & (u8)~0x40); PCDDR = v;
    PCDDR_SHADOW = v;

    v = (u8)(P4DDR_SHADOW & (u8)~0x20);
    P4DDR = v;
    P4DDR_SHADOW = v;

    port_c_bits_high();
}
/* H'208E2A. The touch panel's two axes, off analog channels 0 and 1. Below
 * H'05 is not a real reading, so it becomes H'02 -- which is also what the
 * debounce writes when the reading moves, and what H'210EE2 later treats as
 * the screen not being touched at all.
 *
 * Not the knobs, despite the name the first reading of this gave it: those
 * are the quadrature pairs on port C that H'20A10C and H'20A19A follow.
 * Everything this leaves ends up in H'FFFED9 and H'FFFEDA, the raw touch
 * coordinates H'210EE2 scales by the calibration at H'11A87E. */
void touch_read(void)
{
    u8 v;

    v = adc_get_result(0);
    REG8(0x11A80BUL) = v;
    if (v < 0x05) REG8(0x11A80BUL) = 0x02;

    v = adc_get_result(1);
    REG8(0x11A80AUL) = v;
    if (v < 0x05) REG8(0x11A80AUL) = 0x02;
}
/* H'208E7A. One pass over the three strobes. Each is made an output and
 * driven low on its own, the latch read, and the three put back high
 * together. The knobs are read on the odd passes. */
void key_banks_read(void)
{
    u8 v;

    v = (u8)(PCDDR_SHADOW | 0x01); PCDDR = v;
    PCDR &= (u8)~0x01;
    REG8(0x11A804UL) = (u8)~KEY_LATCH;
    PCDDR_SHADOW = v;
    port_c_bits_high();

    v = (u8)(PCDDR_SHADOW & (u8)~0x01); PCDDR = v;
    v = (u8)(v | 0x02);                 PCDDR = v;
    PCDR &= (u8)~0x02;
    REG8(0x11A805UL) = (u8)~KEY_LATCH;
    PCDDR_SHADOW = v;
    port_c_bits_high();

    v = (u8)(PCDDR_SHADOW & (u8)~0x02); PCDDR = v;
    v = (u8)(v | 0x40);                 PCDDR = v;
    PCDR &= (u8)~0x40;
    REG8(0x11A806UL) = (u8)~KEY_LATCH;
    PCDDR_SHADOW = v;
    port_c_bits_high();

    v = (u8)(PCDDR_SHADOW & (u8)~0x40); PCDDR = v;
    PCDDR_SHADOW = v;

    if (KEY_PASS & 0x01) touch_read();
}
/* H'208F18. What survives the pass: a bank that has changed is thrown away
 * rather than kept, and a touch reading that has moved by more than two is
 * put back to H'02, which cannot pass the test at the end. */
void key_scan_compare(void)
{
    short a, b;

    if (REG8(0x11A807UL) != REG8(0x11A804UL)) REG8(0x11A807UL) = 0;
    if (REG8(0x11A808UL) != REG8(0x11A805UL)) REG8(0x11A808UL) = 0;
    if (REG8(0x11A809UL) != REG8(0x11A806UL)) REG8(0x11A809UL) = 0;

    a = (short)REG8(0x11A80AUL);
    b = (short)REG8(0x11A80CUL);
    if (!((short)(a - 2) <= b && (short)(a + 2) >= b)) REG8(0x11A80CUL) = 0x02;

    a = (short)REG8(0x11A80BUL);
    b = (short)REG8(0x11A80DUL);
    if (!((short)(a - 2) <= b && (short)(a + 2) >= b)) REG8(0x11A80DUL) = 0x02;
}
/* H'209072. The first pass: read, and keep what was read as the reference
 * every later pass is measured against. */
void key_scan_first(void)
{
    KEY_PASS++;
    key_banks_read();
    REG8(0x11A807UL) = REG8(0x11A804UL);
    REG8(0x11A808UL) = REG8(0x11A805UL);
    REG8(0x11A809UL) = REG8(0x11A806UL);
    REG8(0x11A80CUL) = REG8(0x11A80AUL);
    REG8(0x11A80DUL) = REG8(0x11A80BUL);
}
/* H'208FCC, and H'2090C4 to H'2090FC, which are eight entries that do
 * nothing but reach it. */
void key_scan_again(void)
{
    KEY_PASS++;
    key_banks_read();
    key_scan_compare();
}
/* H'209104. The tenth pass, which publishes.
 *
 * The three banks go to H'FFFEDB..H'FFFEDD. If any key came through, a
 * hold-off of H'96 goes into H'11A802 and no further scan starts until an
 * interrupt has counted it down -- that is the auto-repeat rate. The touch
 * coordinates go to H'FFFED9 and H'FFFEDA, only when both axes are above
 * H'05, and only once, latched by bit 1 of H'11A80E so that a finger held
 * still does not keep re-sending it. Bit 7 of H'FFFEF7 is something else owning the panel: then everything
 * published this pass is taken back, unless a key in the second bank asked
 * for it.
 */
void key_scan_finish(void)
{
    u8 bank0, bank2;

    KEY_PASS = 0;
    key_banks_read();
    key_scan_compare();

    bank0 = REG8(0x11A807UL);
    REG8(0xFFFEDCUL) = REG8(0x11A808UL);
    bank2 = REG8(0x11A809UL);
    REG8(0xFFFEDDUL) = bank2;
    REG8(0xFFFEDBUL) = bank0;

    if (bank0 == 0 && REG8(0xFFFEDCUL) == 0 && bank2 == 0) KEY_HOLDOFF = 0;
    else                                                   KEY_HOLDOFF = 0x96;

    if (REG8(0x11A80CUL) <= 0x05 || REG8(0x11A80DUL) <= 0x05) {
        REG8(0x11A80EUL) &= (u8)~0x02;
        REG8(0xFFFEDAUL) = 0;
        REG8(0xFFFED9UL) = 0;
    } else if (!(REG8(0x11A80EUL) & 0x02)) {
        REG8(0x11A80EUL) |= 0x02;
        REG8(0xFFFED9UL) = REG8(0x11A80CUL);
        REG8(0xFFFEDAUL) = REG8(0x11A80DUL);
    }

    if ((REG8(0xFFFEF7UL) & 0x80) && !(REG8(0xFFFEDCUL) & 0x20)) {
        REG8(0xFFFEDAUL) = 0;
        REG8(0xFFFED9UL) = 0;
        REG8(0xFFFEDDUL) = 0;
        REG8(0xFFFEDCUL) = 0;
        REG8(0xFFFEDBUL) = 0;
    }
}
/* H'20901A. One pass, or nothing while the hold-off is running. */
void key_scan_step(void)
{
    if (KEY_HOLDOFF != 0) return;

    switch (KEY_PASS) {
    case 0x00: key_scan_first(); break;
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06: case 0x07: case 0x08: key_scan_again(); break;
    case 0x09: key_scan_finish(); break;
    default: break;
    }
}
/* H'20ACC8. A whole settled scan, then what the machine makes of it.
 *
 * Bit 2 of the first bank is the one that matters here: it says the
 * embroidery module is attached, and its absence is what decides whether
 * H'FFFEC5 -- which analog_scan reads before it will write the trim back to
 * the settings store -- is left at zero or set to 6.
 */
void keys_scan_settled(void)
{
    do {
        key_scan_step();
    } while (KEY_PASS != 0);

    if (REG8(0xFFFEDBUL) & 0x04) {
        PEDAL_FLAGS |= 0x80;
        REG8(0xFFFEC5UL) = 0;
    } else {
        u8 v = REG8(0xFFFEDDUL);
        if (!(v & 0x08)) {
            PEDAL_FLAGS &= (u8)~0x80;
            REG8(0xFFFEC5UL) = 0;
        } else if (v & 0x20) {
            PEDAL_FLAGS |= 0x80;
            REG8(0xFFFEC5UL) = 0x06;
        }
    }
}
