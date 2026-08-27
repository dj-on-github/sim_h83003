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

#endif
