/* The front panel: the key matrix, and the touch panel that is read on the
 * same pass.
 *
 * The keys are a matrix -- three strobes on port C, eight returns from the
 * latch at H'060000, active low. `key_banks_read` drives one strobe low at a
 * time, reads the latch, and puts all three back; `port_c_bits_high` is that
 * restore, and it writes the data register of pins that are momentarily
 * inputs, because on this part that decides what a pin will read as the
 * instant it becomes an output again.
 *
 * The whole panel is read ten times over. A bank that reads differently from
 * the pass before is thrown away rather than believed, and a touch reading
 * that has moved by more than two counts is put back to H'02, which cannot
 * pass the test at the end. Only what has been still for ten passes is
 * published, which is why the bring-up spends a settled scan here before it
 * trusts anything.
 *
 * The touch panel is not on the matrix at all: its two axes are analog
 * channels 0 and 1, read by `touch_read` on the odd passes and published to
 * H'FFFED9 and H'FFFEDA. Nor are the knobs, which are quadrature pairs on
 * port C bits 2-3 and 4-5 followed by the interrupt in app_isr.c.
 */
#ifndef APP_KEYS_H
#define APP_KEYS_H

/* The three port C strobes, and the pins they share with the knobs. */
void port_c_bits_high(void);
void port_c_init(void);

/* One pass of the scan, and the pieces it is made of. */
void touch_read(void);
void key_banks_read(void);
void key_scan_compare(void);
void key_scan_first(void);
void key_scan_again(void);
void key_scan_finish(void);

/* What the rest of the machine calls: one pass, and a whole settled scan. */
void key_scan_step(void);
void keys_scan_settled(void);

/* ---- what a key press asks for -------------------------------------------
 *
 * `key_scan` does not report a key. It writes the number of the screen the
 * key is asking for into H'11B10E, and H'FFFF when nothing is down. Every
 * one of these is a button on the front of the machine; the names come from
 * pressing them one at a time and watching H'11B10E.
 *
 * `key_scan` is the only thing in the whole application that writes H'11B10E
 * -- 21 stores in the original, all of them inside it -- so this list is the
 * complete set of values that can ever come out of `screen_leave_check`.
 *
 * H'7F and H'80 are NOT in it, which matters: those numbers do appear in the
 * screen bodies, compared against touch box indices, which are a different
 * numbering that happens to overlap this one. A comparison is only asking
 * about a button if the value it tests came from H'11B10E.
 *
 * The panel as it reads on the machine, written down while pressing it:
 *
 *   stitchsel   buttonhole
 *   fancy       Frame
 *   A           module
 *   clr         mem
 *
 *   penupdown                     Left     Right
 *   ?           Help              smart    setup
 *
 *                                          eco
 *
 * Left cluster of three:
 *    F  C=
 *    Reverse
 *
 * Two of these are not labelled in words on the machine, only drawn, and
 * both were first written down from the drawing and both were wrong:
 *
 *   H'72  a thick line          -- read as a thickness control; it selects
 *                                 the fancy stitches
 *   H'74  a machine with two    -- read as a machine input; it is the
 *         arrows pointing at it    embroidery module button
 *
 * Worth knowing before anyone reads the panel from the icons again.
 *
 * The button drawn "?" is H'75, and pressing every button on the front of
 * the machine did not produce it. See the note on KEY_75 below.
 */
#define KEY_MEM             0x006D
#define KEY_LEFT            0x006E
#define KEY_RIGHT           0x006F
#define KEY_BUTTONHOLE      0x0070
#define KEY_STITCH_SEL      0x0071
#define KEY_FANCY           0x0072      /* drawn as a thick line */
#define KEY_HELP            0x0073
#define KEY_MODULE          0x0074      /* drawn as a machine with
                                           two arrows at it */

/* Not identified. Pressing every button on the front of the machine did not
 * produce it, so whatever closes this contact is either not on the panel or
 * not fitted -- these boards are shared across the model range.
 *
 * What is known about it:
 *
 *  - It is bank 0 bit 3: the first strobe, the same one that carries stitch
 *    sel, Frame, clr, F and C=.
 *
 *  - It is the only key the scan can decline. When bit 0 of H'FFFEC4 is set
 *    it is reported only if the module will take the order; otherwise the
 *    scan carries on past it into the last two tests. The original stores
 *    H'75 twice, once in each arm of that test, which is why it has 21
 *    stores to H'11B10E and the C has 20.
 *
 *  - It has no case in the key dispatch, so it never changes screen. Its
 *    whole effect is to set H'11A16F, which makes touch_allowed() swallow
 *    the next touch while the motor is running. So it is not a key that goes
 *    somewhere -- it is a key that says "ignore what I am about to lean on".
 *
 * To tell "the contact never closes" from "the contact closes and the gate
 * suppresses it", watch bit 3 of H'FFFEDB (or H'11A807, the raw bank) rather
 * than H'11B10E. H'11B10E only ever sees it once it is past the gate. */
#define KEY_75              0x0075

#define KEY_PEN_UPDOWN      0x0076
#define KEY_CLR             0x0077
#define KEY_A               0x0078
#define KEY_SETUP          0x0079
#define KEY_F               0x007A
#define KEY_REVERSE         0x007B
#define KEY_C_EQUALS        0x007C
#define KEY_FRAME           0x007D
#define KEY_ECO             0x007E
#define KEY_SMART           0x0081

/* Nothing down. */
#define KEY_NONE            0xFFFF

#endif
