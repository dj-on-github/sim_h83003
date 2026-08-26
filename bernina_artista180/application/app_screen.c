/* The artista 180 application, rebuilt in C: the display subsystem,
 * bitmaps, screens, the dialog and the picker.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- the display subsystem ----------------------------------------------
 * The screen is 320 by 240 at two bits a pixel -- four pixels to a byte, 80
 * bytes to a line, H'4B00 bytes to a buffer -- and there are three buffers.
 * plot_pixel (H'20E154) is the bottom of it; everything here is built on
 * that and on the byte fills.
 */

/* H'208698. One turn of the machine while the display is busy. This is what
 * service_hook reaches, which is why a fill of 19K does not stop the motors.
 */
void service_tick(void)
{
    REG16(0x114DE2UL) = 0;
    analog_scan();
    stitch_state_init();
    pedal_service();
    main_motor_service();
}

/* H'20E0D8, H'20E0FE. Two big-endian words out of a four-byte header: the
 * first pair and the second. Bitmaps in this ROM carry their width and
 * height that way. */
u16 header_word_0(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | (u16)p[1]);
}

u16 header_word_1(const u8 *p)
{
    return (u16)(((u16)p[2] << 8) | (u16)p[3]);
}

/* H'20E48E. Where a pixel lives: the byte that holds it and which of the
 * four pairs in that byte it is. */
void pixel_address(u16 x, u16 y, u32 base, u32 *addr, u8 *shift)
{
    /* MULXS.W: a signed 16 x 16 into 32, not a 16-bit product widened
     * afterwards. The difference only shows from y = 102 down, where
     * H'140 * y passes H'8000 -- which is most of the screen. */
    long t = (long)(short)0x0140 * (long)(short)y + (long)(short)x;

    *addr  = base + ((u32)t >> 2);
    *shift = (u8)((u32)t & 0x03);
}

/* H'20E562. A vertical line, drawn a pixel at a time. The ends are put in
 * order first, so it does not matter which way round they come. */
void draw_vline(u16 x, u16 y0, u16 y1, u32 buffer, u8 colour)
{
    short a = (short)y0;
    short b = (short)y1;
    short i;

    if (a > b) { short t = a; a = b; b = t; }

    for (i = a; i <= b; i++) plot_pixel(x, (u16)i, buffer, colour);
}

/* H'210428. A copy with the service hook between bytes, so a long one does
 * not starve the machine. Hands back the destination. */
u8 *copy_forward(u8 *dst, const u8 *src, u32 n)
{
    u8 *out = dst;

    while (n-- != 0) {
        service_hook();
        *dst++ = *src++;
    }
    return out;
}

/* H'210470. The same, but safe when the two overlap: a source below the
 * destination that reaches into it is copied backwards. */
u8 *copy_overlapped(u8 *dst, const u8 *src, u32 n)
{
    u8 *out = dst;

    if ((u32)src <= (u32)dst && (u32)((u32)src + n) >= (u32)dst) {
        dst += n;
        src += n;
        while (n-- != 0) {
            service_hook();
            *--dst = *--src;
        }
        return out;
    }

    while (n-- != 0) {
        service_hook();
        *dst++ = *src++;
    }
    return out;
}

/* H'246D8C, and H'246D7E just above it, which sets the same byte. */
void message_state_clear(void)
{
    REG8(0x114DA4UL) = 0;
}

void message_state_set(void)
{
    REG8(0x114DA4UL) = 0x01;
}

/* H'21F40E. Remembers which screen is showing, in one of four slots, and
 * says a screen change has happened. */
void screen_remember(u8 slot)
{
    if (slot == 0x01)      REG8(0x11A16AUL) = REG8(0x11A169UL);
    else if (slot == 0x02) REG8(0x11A16BUL) = REG8(0x11A169UL);
    else if (slot == 0x03) REG8(0x11A16CUL) = REG8(0x11A169UL);
    else if (slot == 0x04) REG8(0x11A16DUL) = REG8(0x11A169UL);
    else return;

    REG8(0x11A172UL) = 0x01;
    REG8(0x11B100UL) = slot;
}

/* H'20E310. One pixel read back, the mirror of plot_pixel and dispatched
 * through the same kind of table for the same reason. */
u8 read_pixel(u16 x, u16 y, u32 buffer)
{
    volatile u8 *p;
    u8 shift;

    service_hook();

    p = (volatile u8 *)(buffer + (u32)y * LCD_BYTES_PER_LINE +
                        ((u32)(x >> 3) << 1) + ((x & 7) >> 2));
    shift = (u8)(6 - 2 * (x & 3));

    return (u8)((*p >> shift) & 0x03);
}

/* H'20E5B6. A horizontal line, and the only primitive that is not a loop
 * over plot_pixel: the whole bytes in the middle are filled at once and only
 * the ragged ends are done a pixel at a time.
 *
 * The fill byte is worked out from the colour, and colour 1 has no case --
 * the original leaves the byte holding whatever the caller's register did.
 * Reproduced by leaving it at zero, which is what a fresh local gives; a
 * line of colour 1 long enough to reach the byte fill is the one thing here
 * that cannot be matched, and it is a fault in the original rather than a
 * gap in this.
 */
void draw_hline(u16 x0, u16 x1, u16 y, u32 buffer, u8 colour)
{
    u32 addr0, addr1;
    u8  sh0, sh1;
    u8  fill = 0;
    u16 a, b;
    short i;
    u32 n;

    a = x0;
    b = x1;
    if ((short)a > (short)b) { u16 t = a; a = b; b = t; }

    pixel_address(a, y, buffer, &addr0, &sh0);
    pixel_address(b, y, buffer, &addr1, &sh1);

    if (colour == 0x00)      fill = 0x00;
    else if (colour == 0x02) fill = 0xAA;
    else if (colour == 0x03) fill = 0xFF;

    if (addr0 == addr1) {
        if ((short)((short)(u16)sh1 - (short)(u16)sh0) == 0x0003) {
            *(volatile u8 *)addr0 = fill;
        } else {
            for (i = (short)(u16)sh0; (short)(u16)sh1 >= i; i++) {
                plot_pixel(a, y, buffer, colour);
                a++;
            }
        }
        return;
    }

    if (sh0 == 0) {
        *(volatile u8 *)addr0 = fill;
    } else {
        for (i = (short)(u16)sh0; i <= 0x0003; i++) {
            plot_pixel(a, y, buffer, colour);
            a++;
        }
    }

    n = addr1 - addr0 - 1;
    if (n != 0) mem_fill(addr0 + 1, fill, n);

    if (sh1 == 0x03) {
        *(volatile u8 *)addr1 = fill;
    } else {
        for (i = 0; (short)(u16)sh1 >= i; i++) {
            plot_pixel(b, y, buffer, colour);
            b--;
        }
    }
}

/* H'20E764. Bresenham, for the lines that are neither. */
void draw_line_bresenham(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer,
                         u8 colour)
{
    short x = (short)x0, y = (short)y0;
    short dx = (short)((short)x1 - (short)x0);
    short dy = (short)((short)y1 - (short)y0);
    signed char sx, sy, mx, my;
    short e, inc, i;

    if (dx < 0) { dx = (short)-dx; sx = -1; } else sx = 1;
    if (dy < 0) { dy = (short)-dy; sy = -1; } else sy = 1;

    if (dx < dy) {
        short t = dx; dx = dy; dy = t;
        mx = 0; my = sy;
    } else {
        mx = sx; my = 0;
    }

    inc = (short)(dy << 1);
    e   = (short)(inc - dx);

    for (i = 0; i <= dx; i++) {
        plot_pixel((u16)x, (u16)y, buffer, colour);
        if (e < 0) {
            x = (short)(x + mx);
            y = (short)(y + my);
            e = (short)(e + inc);
        } else {
            x = (short)(x + sx);
            y = (short)(y + sy);
            e = (short)(e + (short)(inc - (short)(dx << 1)));
        }
    }
}

/* H'20E4E4. A line, sent to whichever of the three is right for it. */
void draw_line(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer, u8 colour)
{
    if (x0 == x1)      draw_vline(x0, y0, y1, buffer, colour);
    else if (y0 == y1) draw_hline(x0, x1, y0, buffer, colour);
    else               draw_line_bresenham(x0, y0, x1, y1, buffer, colour);
}

/* H'216F3C. A number as decimal digits, built backwards and then reversed
 * in place. Always writes at least one digit, and terminates. */
void int_to_decimal(short v, char *out)
{
    char *head = out;
    char *tail = out;

    do {
        *tail++ = (char)((u8)(short)(v % 10) + 0x30);
        v = (short)(v / 10);
    } while (v != 0);

    *tail = 0;
    tail--;

    do {
        char t = *tail;
        *tail = *head;
        *head = t;
        head++;
        tail--;
    } while (head < tail);
}

/* H'24ADC8. */
short abs_short(short v)
{
    return (v >= 0) ? v : (short)-v;
}

/* ---- bitmaps -------------------------------------------------------------
 * A bitmap in this ROM is run-length coded: four header bytes and then a
 * stream in which each byte is a colour in its low two bits and a run length
 * in the other six. Four pixels to an output byte, so a run that lands on a
 * byte boundary can be written whole and only the ragged ends of each row
 * need doing a pixel at a time.
 */

static void rle_start(rle_state *r, const u8 *src)
{
    r->src = src;
    r->pos = 4;
    r->run    = (u8)(src[4] >> 2);
    r->colour = (u8)(src[4] & 0x03);
}

static void rle_step(rle_state *r)
{
    r->run = (u8)(r->run - 1);
    if (r->run == 0) {
        r->pos++;
        r->run    = (u8)(r->src[r->pos] >> 2);
        r->colour = (u8)(r->src[r->pos] & 0x03);
    }
}

/* H'20F192. Draws a bitmap into a buffer.
 *
 * A full-width bitmap -- H'013F, the whole screen less one -- is a straight
 * run of bytes and takes the fast path. Anything narrower is done a row at a
 * time: the pixels before the first whole byte, then the whole bytes, then
 * the pixels after the last one. H'11A6xx is not involved; this is pure
 * geometry.
 */
void bitmap_draw(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src, u32 dst)
{
    u16 lead = 0, trail = 0, mid;
    u32 rowstart, rowend;
    u8  sh0, sh1;
    u8  acc = 0;
    rle_state r;
    short x, y, i;
    u32 p;

    pixel_address(x0, y0, dst, &rowstart, &sh0);
    rle_start(&r, src);

    if ((u16)((u16)x1 - (u16)x0) == 0x013F) {
        pixel_address(x1, y1, dst, &rowend, &sh1);
        for (p = rowstart; p <= rowend; p++) {
            service_hook();
            for (i = 0; i < 4; i++) {
                acc = (u8)((u8)(acc << 2) | r.colour);
                rle_step(&r);
            }
            *(volatile u8 *)p = acc;
        }
        return;
    }

    pixel_address(x1, y0, dst, &rowend, &sh1);
    mid = (u16)((u16)rowend - (u16)rowstart);

    if (sh0 == 0 && sh1 == 0x03) {
        mid = (u16)(mid + 1);
    } else if (sh0 != 0 && sh1 != 0x03) {
        if (mid == 0) {
            lead = (u16)((u16)sh1 - (u16)sh0 + 1);
        } else {
            mid = (u16)(mid - 1);
            lead  = (u16)(4 - (u16)sh0);
            trail = (u16)((u16)sh1 + 1);
        }
    } else if (sh0 == 0) {
        trail = (u16)((u16)sh1 + 1);
    } else {
        lead = (u16)(4 - (u16)sh0);
    }

    if (mid != 0) {
        if (lead  != 0) rowstart += 1;
        if (trail != 0) rowend   -= 1;
    }

    for (y = (short)y0; y <= (short)y1; y++) {
        for (x = (short)x0; (short)((u16)x0 + lead) > x; x++) {
            plot_pixel((u16)x, (u16)y, dst, r.colour);
            rle_step(&r);
        }

        if (mid != 0) {
            for (p = rowstart; p <= rowend; p++) {
                service_hook();
                for (i = 0; i < 4; i++) {
                    acc = (u8)((u8)(acc << 2) | r.colour);
                    rle_step(&r);
                }
                *(volatile u8 *)p = acc;
            }
            rowstart += LCD_BYTES_PER_LINE;
            rowend   += LCD_BYTES_PER_LINE;
        }

        for (x = (short)((u16)x1 - trail + 1); x <= (short)x1; x++) {
            plot_pixel((u16)x, (u16)y, dst, r.colour);
            rle_step(&r);
        }
    }
}

/* ---- the packed pictures ------------------------------------------------
 * H'20F866 and H'20F4DC. The second kind of picture in this ROM.
 *
 * bitmap_draw above takes a run-length source -- a byte at a time, six bits
 * of count and two of colour. The two big pictures behind screens H'08 and
 * H'3A are packed with LZW instead, over an alphabet of three: the pixel
 * values H'00, H'02 and H'03, which are codes H'00, H'01 and H'02.
 *
 * The dictionary is H'101 entries of four bytes at H'0FF710 -- a prefix code
 * and one character each -- and the H'100 bytes above it, H'0FFB14 to
 * H'0FFC13, are where a step leaves the string it decoded. The string is
 * built backwards from the top, so a step says where it *starts*: the
 * caller reads from H'0FFB14 plus that offset upwards and asks for another
 * step once it has passed H'0FFC12.
 *
 * Three words carry between steps: H'11A858 the position in the stream,
 * H'11A85A the next code to hand out, H'11A85C the code the last step ended
 * on. The stream's first four bytes are its header, so the first code is at
 * offset four.
 */
#define LZW_DICT  0x000FF710UL      /* H'101 entries: prefix word, character */
#define LZW_OUT   0x000FFB14UL      /* where a step leaves what it decoded */
#define LZW_TOP   0x000FFC13UL      /* one past the last byte of that */

#define LZW_POS   REG16(0x0011A858UL)
#define LZW_NEXT  REG16(0x0011A85AUL)
#define LZW_OLD   REG16(0x0011A85CUL)

static u32 lzw_entry(u16 code)
{
    return LZW_DICT + (u32)(u16)((u16)(code << 2));
}

/* H'20F866. One code read, and the string it stands for left at the top of
 * the buffer. [first] resets the dictionary to the three the alphabet starts
 * with and takes the first code as itself. */
void lzw_step(const u8 *stream, u16 *out, u8 first)
{
    u32 sp, entry;
    u16 code, c;
    u8  ch = 0;

    if (first != 0) {
        LZW_NEXT = 0x0003;
        LZW_POS  = 0x0004;
        LZW_OLD  = (u16)stream[4];

        REG16(LZW_DICT + 0x00) = 0xFFFF;  REG8(LZW_DICT + 0x02) = 0x00;
        REG16(LZW_DICT + 0x04) = 0xFFFF;  REG8(LZW_DICT + 0x06) = 0x02;
        REG16(LZW_DICT + 0x08) = 0xFFFF;  REG8(LZW_DICT + 0x0A) = 0x03;

        *out = 0x00FE;
        REG8(LZW_TOP - 1) = REG8(LZW_DICT + 0x02 +
            (u32)(long)(short)(u16)((u16)((u16)stream[4] << 2)));
        return;
    }

    LZW_POS = (u16)(LZW_POS + 1);
    code = (u16)stream[LZW_POS];

    /* The entry this step makes: its prefix is the code the last one ended
     * on, and its character is settled below -- it is the first character of
     * whatever this step decodes. */
    entry = lzw_entry(LZW_NEXT);
    REG16(entry) = LZW_OLD;

    if (code >= LZW_NEXT) {
        /* The one case where the code read is the entry being made: what
         * it stands for is the last string with its own first character put
         * on the end, so the walk starts a byte lower and that byte is
         * filled in afterwards. */
        c  = LZW_OLD;
        sp = LZW_TOP - 1;
    } else {
        c  = code;
        sp = LZW_TOP;
    }

    do {
        service_hook();
        ch = REG8(lzw_entry(c) + 0x02);
        REG8(--sp) = ch;
        c = REG16(lzw_entry(c));
    } while (c != 0xFFFF);

    if (code >= LZW_NEXT) REG8(LZW_TOP - 1) = ch;
    REG8(entry + 0x02) = ch;

    *out = (u16)(sp - LZW_OUT);
    LZW_OLD  = code;
    LZW_NEXT = (u16)(LZW_NEXT + 1);
    if (LZW_NEXT > 0x0100) LZW_NEXT = 0x0003;
}

/* The next pixel out of the buffer, with another step taken once the last
 * byte of the one before has been used. */
static u8 lzw_next(const u8 *src, u16 *pos)
{
    const u8 v = REG8(LZW_OUT + (u32)((*pos)++));

    if (*pos > 0x00FE) lzw_step(src, pos, 0x00);
    return v;
}

/* H'20F4DC. The same geometry as bitmap_draw above, pixel for pixel, with
 * an LZW stream where that one has run lengths. */
void bitmap_draw_lzw(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src, u32 dst)
{
    u16 lead = 0, trail = 0, mid;
    u32 rowstart, rowend;
    u8  sh0, sh1;
    u8  acc = 0;
    u16 pos = 0;
    short x, y, i;
    u32 p;

    pixel_address(x0, y0, dst, &rowstart, &sh0);
    lzw_step(src, &pos, 0x01);

    if ((u16)((u16)x1 - (u16)x0) == 0x013F) {
        u32 last_addr;
        u8  last_sh;

        pixel_address(x1, y1, dst, &last_addr, &last_sh);
        for (p = rowstart; p <= last_addr; p++) {
            service_hook();
            for (i = 0; i < 4; i++) {
                acc = (u8)((u8)(acc << 2) | lzw_next(src, &pos));
            }
            *(volatile u8 *)p = acc;
        }
        return;
    }

    pixel_address(x1, y0, dst, &rowend, &sh1);
    mid = (u16)((u16)rowend - (u16)rowstart);

    if (sh0 == 0 && sh1 == 0x03) {
        mid = (u16)(mid + 1);
    } else if (sh0 != 0 && sh1 != 0x03) {
        if (mid == 0) {
            lead = (u16)((u16)sh1 - (u16)sh0 + 1);
        } else {
            mid = (u16)(mid - 1);
            lead  = (u16)(4 - (u16)sh0);
            trail = (u16)((u16)sh1 + 1);
        }
    } else if (sh0 == 0) {
        trail = (u16)((u16)sh1 + 1);
    } else {
        lead = (u16)(4 - (u16)sh0);
    }

    if (mid != 0) {
        if (lead  != 0) rowstart += 1;
        if (trail != 0) rowend   -= 1;
    }

    for (y = (short)y0; y <= (short)y1; y++) {
        for (x = (short)x0; (short)((u16)x0 + lead) > x; x++) {
            plot_pixel((u16)x, (u16)y, dst, lzw_next(src, &pos));
        }

        if (mid != 0) {
            for (p = rowstart; p <= rowend; p++) {
                service_hook();
                for (i = 0; i < 4; i++) {
                    acc = (u8)((u8)(acc << 2) | lzw_next(src, &pos));
                }
                *(volatile u8 *)p = acc;
            }
            rowstart += LCD_BYTES_PER_LINE;
            rowend   += LCD_BYTES_PER_LINE;
        }

        for (x = (short)((u16)x1 - trail + 1); x <= (short)x1; x++) {
            plot_pixel((u16)x, (u16)y, dst, lzw_next(src, &pos));
        }
    }
}

/* H'2102B8. A bitmap drawn mirrored.
 *
 * Mirroring is not done in the decoder: the bitmap is drawn into the third
 * buffer at H'0E8010 the ordinary way and then copied out pixel by pixel
 * with one or both axes reversed. Mode 0 skips the round trip entirely.
 */
void bitmap_draw_mirrored(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src,
                          u32 dst, u8 mode)
{
    short x, y, xd, yd;

    if (mode == 0) {
        bitmap_draw(x0, y0, x1, y1, src, dst);
        return;
    }

    bitmap_draw(x0, y0, x1, y1, src, LCD_SCRATCH);

    if (mode == 0x01) {
        for (y = (short)y0; y <= (short)y1; y++) {
            xd = (short)x1;
            for (x = (short)x0; x <= (short)x1; x++) {
                plot_pixel((u16)xd, (u16)y, dst,
                           read_pixel((u16)x, (u16)y, LCD_SCRATCH));
                xd--;
            }
        }
    } else if (mode == 0x02) {
        yd = (short)y1;
        for (y = (short)y0; y <= (short)y1; y++) {
            for (x = (short)x0; x <= (short)x1; x++) {
                plot_pixel((u16)x, (u16)yd, dst,
                           read_pixel((u16)x, (u16)y, LCD_SCRATCH));
            }
            yd--;
        }
    } else if (mode == 0x03) {
        yd = (short)y1;
        for (y = (short)y0; y <= (short)y1; y++) {
            xd = (short)x1;
            for (x = (short)x0; x <= (short)x1; x++) {
                plot_pixel((u16)xd, (u16)yd, dst,
                           read_pixel((u16)x, (u16)y, LCD_SCRATCH));
                xd--;
            }
            yd--;
        }
    }
}

/* H'20E826. A rectangle, filled or outlined. Filled is a stack of
 * horizontal lines -- the ends are put in order first -- and outlined is the
 * four sides drawn separately. */
void draw_rect(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer, u8 colour,
               u8 filled)
{
    short a, b, y;

    if (filled != 0) {
        a = (short)y0;
        b = (short)y1;
        if (a > b) { short t = a; a = b; b = t; }
        for (y = a; y <= b; y++) draw_hline(x0, x1, (u16)y, buffer, colour);
        return;
    }

    draw_hline(x0, x1, y0, buffer, colour);
    draw_vline(x1, y0, y1, buffer, colour);
    draw_hline(x0, x1, y1, buffer, colour);
    draw_vline(x0, y0, y1, buffer, colour);
}

/* H'20EC12. Copies a rectangle from one buffer to another -- or to a
 * different place in the same one, which is what makes the direction test
 * necessary: rows are taken from the top down when the destination is above
 * the source and from the bottom up when it is below, so an overlapping move
 * does not eat its own tail.
 *
 * The middle whole bytes of each row go through copy_forward and only the
 * ragged ends are done a pixel at a time, the same shape as bitmap_draw.
 * Unlike bitmap_draw, only the leading edge shifts the row pointers: the
 * trailing byte is left in the run.
 */
void region_copy(u16 x0, u16 y0, u16 x1, u16 y1, u16 dst_y, u32 from, u32 to)
{
    u16 lead = 0, trail = 0, mid;
    u32 src_line, dst_line, end_addr;
    u8  sh0, sh1;
    short x, sy, dy;
    u32 off;

    pixel_address(x0, y0,    from, &src_line, &sh0);
    pixel_address(x0, dst_y, to,   &dst_line, &sh1);

    if ((u16)((u16)x1 - (u16)x0) == 0x013F) {
        pixel_address(x1, y1, from, &end_addr, &sh1);
        copy_forward((u8 *)dst_line, (const u8 *)src_line,
                     (u32)(end_addr - src_line + 1));
        return;
    }

    pixel_address(x1, dst_y, to, &end_addr, &sh1);
    mid = (u16)((u16)end_addr - (u16)dst_line);

    if (sh0 == 0 && sh1 == 0x03) {
        mid = (u16)(mid + 1);
    } else if (sh0 != 0 && sh1 != 0x03) {
        if (mid == 0) {
            lead = (u16)((u16)sh1 - (u16)sh0 + 1);
        } else {
            mid = (u16)(mid - 1);
            lead  = (u16)(4 - (u16)sh0);
            trail = (u16)((u16)sh1 + 1);
        }
    } else if (sh0 == 0) {
        trail = (u16)((u16)sh1 + 1);
    } else {
        lead = (u16)(4 - (u16)sh0);
    }

    if (mid != 0 && lead != 0) {
        src_line += 1;
        dst_line += 1;
    }

    if ((short)dst_y <= (short)y0) {
        dy = (short)dst_y;
        for (sy = (short)y0; sy <= (short)y1; sy++) {
            for (x = (short)x0; (short)((u16)x0 + lead) > x; x++) {
                plot_pixel((u16)x, (u16)dy, to,
                           read_pixel((u16)x, (u16)sy, from));
            }
            if (mid != 0) {
                copy_forward((u8 *)dst_line, (const u8 *)src_line, (u32)mid);
                src_line += LCD_BYTES_PER_LINE;
                dst_line += LCD_BYTES_PER_LINE;
            }
            for (x = (short)((u16)x1 - trail + 1); x <= (short)x1; x++) {
                plot_pixel((u16)x, (u16)dy, to,
                           read_pixel((u16)x, (u16)sy, from));
            }
            dy++;
        }
        return;
    }

    dy  = (short)((u16)dst_y + (u16)((u16)y1 - (u16)y0));
    off = (u32)(long)(short)((short)((u16)y1 - (u16)y0) *
                             (short)LCD_BYTES_PER_LINE);
    src_line += off;
    dst_line += off;

    for (sy = (short)y1; sy >= (short)y0; sy--) {
        for (x = (short)x0; (short)((u16)x0 + lead) > x; x++) {
            plot_pixel((u16)x, (u16)dy, to,
                       read_pixel((u16)x, (u16)sy, from));
        }
        if (mid != 0) {
            copy_forward((u8 *)dst_line, (const u8 *)src_line, (u32)mid);
            src_line -= LCD_BYTES_PER_LINE;
            dst_line -= LCD_BYTES_PER_LINE;
        }
        for (x = (short)((u16)x1 - trail + 1); x <= (short)x1; x++) {
            plot_pixel((u16)x, (u16)dy, to,
                       read_pixel((u16)x, (u16)sy, from));
        }
        dy--;
    }
}

/* ---- screens -------------------------------------------------------------
 * H'11A169 is the screen showing now and H'11A168 the one before it.
 * H'11A16A to H'11A16D are four slots a screen can be remembered in, so that
 * "back" means something different depending on how the operator got here.
 */

/* H'21F1DE. What leaving a screen costs. The table says which of three
 * things -- and most of the sixty-eight screens want none of them. Kind 1
 * also puts the pattern number and a word of panel state aside, but only
 * when the pattern has changed since last time or the caller insists. */
static const u8 screen_leave_kind[0x44] = {
    1,1,1,0,0,1,0,0,0,0,1,1,0,0,0,0,
    3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,
    0,2,3,0,0,0,0,0,0,0,0,0,0,0,1,0,
    0,1,1,1,1,3,3,0,0,0,0,0,0,0,0,0,
    0,0,0,1
};

void screen_leave(u8 screen, u8 force)
{
    u8 idx = (u8)(screen + 0xFE);
    u8 kind = (idx <= 0x43) ? screen_leave_kind[idx] : 0;

    if (kind == 1) {
        if (REG16(0xFFFEE0UL) != REG16(0x11A1BAUL) || force != 0) {
            REG8(0x11A168UL)  = REG8(0x11A169UL);
            REG16(0x11B110UL) = REG16(0x11B108UL);
            REG16(0x11A1BAUL) = REG16(0xFFFEE0UL);
        }
    } else if (kind == 2 || kind == 3) {
        REG8(0x11A168UL) = REG8(0x11A169UL);
    }
}

/* H'22248A. Saves the part of the screen a dialog is about to cover, into
 * the second of the four stored screens. H'11A176 is a request left by
 * whatever wants it back; the argument forces it either way. */
void dialog_backdrop_save(u8 force)
{
    if (REG8(0x11A176UL) == 0 && force == 0) return;

    region_copy(0x0030, 0x00A0, 0x00E7, 0x00C0, 0x00A0,
                LCD_FRAME_A, 0x000F1610UL);

    if (force == 0) REG8(0x11A176UL) = 0;
}

/* H'21F09E. Goes to a screen.
 *
 * Nothing happens while the machine is sewing. Going back to the screen a
 * slot already holds is a return rather than a move, and takes the short
 * path. Otherwise the current screen is put in the slot if asked, the one
 * being left gets its leave hook, and the new one becomes current.
 */
void screen_switch(u8 screen, u8 slot, u8 remember)
{
    u8 held = 0;

    if (REG8(0x114DC6UL) & 0x80) return;

    if      (slot == 0x01) held = REG8(0x11A16AUL);
    else if (slot == 0x02) held = REG8(0x11A16BUL);
    else if (slot == 0x03) held = REG8(0x11A16CUL);
    else if (slot == 0x04) held = REG8(0x11A16DUL);

    if (slot >= 0x01 && slot <= 0x04 && held == screen &&
        REG8(0x11B0A8UL) != 0) {
        REG8(0x11A169UL) = screen;
        REG8(0x11B0A8UL) = 0;
        return;
    }

    if (remember != 0) {
        if      (slot == 0x01) REG8(0x11A16AUL) = REG8(0x11A169UL);
        else if (slot == 0x02) REG8(0x11A16BUL) = REG8(0x11A169UL);
        else if (slot == 0x03) REG8(0x11A16CUL) = REG8(0x11A169UL);
        else if (slot == 0x04) REG8(0x11A16DUL) = REG8(0x11A169UL);
    }

    screen_leave(REG8(0x11A169UL), 0);

    REG8(0x11A169UL) = screen;
    REG8(0x11B0A8UL) = 0x01;

    if (REG8(0x11A174UL) != 0) {
        picker_cursor(0x02);
        dialog_backdrop_save(0x00);
    }
}

/* H'216D6C. Puts a message up: goes to the message screen and points
 * H'115D12 at the message's own record, which is what draws it. Held off
 * while a message is already up, while the splash is showing, or while a
 * screen change is still settling. */
void message_show(u16 msg)
{
    u32 table;

    if (REG8(0x11A179UL) == 0) return;
    if (REG8(0x11A173UL) != 0) return;
    if (REG8(0x11A171UL) != 0) return;
    if (REG8(0x11B0A8UL) != 0) return;

    REG8(0x11A179UL) = 0;
    message_state_clear();
    screen_remember(0x04);
    screen_switch(0x3E, 0x04, 0x00);

    table = REG32(0x11B2A2UL);
    REG32(0x115D12UL) = REG32(table + (u32)(long)(short)(u16)(msg << 2));
}

/* H'20EFE2. Moves a run of bytes sideways in the visible buffer, a line at a
 * time. The register argument is where the run is taken *from* and the fifth
 * is where it goes; the third gives the far end, and the width is the
 * distance between the far end and the source, plus one.
 *
 * Its three output pointers are worked out between the pushes that pass
 * them, so they are not the frame offsets they look like -- and the copy's
 * destination is the second address, not the first. Both of those kept this
 * routine out of the last two parts.
 */
void scroll_rect(u16 x_from, u16 y0, u16 x_end, u16 y1, u16 x_to)
{
    u32 from, to, end;
    u8  sh;
    short y;
    u32 width;

    pixel_address(x_from, y0, LCD_FRAME_A, &from, &sh);
    pixel_address(x_to,   y0, LCD_FRAME_A, &to,   &sh);
    pixel_address(x_end,  y0, LCD_FRAME_A, &end,  &sh);

    width = (u32)(long)(short)(abs_short((short)((u16)end - (u16)from)) + 1);

    for (y = (short)y0; y <= (short)y1; y++) {
        copy_overlapped((u8 *)to, (const u8 *)from, width);
        to   += LCD_BYTES_PER_LINE;
        from += LCD_BYTES_PER_LINE;
    }
}

/* H'2289EE, H'228A5E. The strip's contents shifted left and right by [n]
 * pixels: left takes from n in and lays it down at the left edge, right does
 * the reverse. */
void dialog_scroll_left(u16 n)
{
    scroll_rect((u16)(n + DIALOG_X0), DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                DIALOG_X0);
}

void dialog_scroll_right(u16 n)
{
    scroll_rect(DIALOG_X0, DIALOG_Y0, (u16)(DIALOG_X1 - n), DIALOG_Y1,
                (u16)(n + DIALOG_X0));
}

/* H'228A20, H'2289B2. How far the strip has to move to bring a position to
 * one edge or the other, rounded up to a whole four pixels -- one byte. */
u16 dialog_shift_to_left_edge(u16 x)
{
    short t = (short)((short)DIALOG_X0 - (short)x);
    short q = (short)(t / 4);

    if ((short)(t % 4) != 0) q++;
    return (u16)(q << 2);
}

u16 dialog_shift_to_right_edge(u16 x)
{
    short t = (short)((u16)x + 0xFF19);
    short q = (short)(t / 4);

    if ((short)(t % 4) != 0) q++;
    return (u16)(q << 2);
}

/* H'228C20, H'228C50. The pattern number out of a queue entry. Note the two
 * bytes go together the other way round from queue_entry_ref: low byte
 * first here, high byte first there. Both are in the ROM as they stand. */
u16 queue_entry_number_first(void)
{
    return (u16)(((u16)REG8(0x11BBABUL) << 8) | (u16)REG8(0x11BBAAUL));
}

u16 queue_entry_number(u16 i)
{
    u32 e = QUEUE + (u32)(long)(short)((short)0x000D * (short)i);

    return (u16)((u16)((u16)(u8)(REG8(e + 1) & 0x03) << 8) | (u16)REG8(e));
}

/* H'2290E0. The per-entry offset byte at +H'0A. */
u8 queue_entry_offset(u16 i)
{
    return REG8(QUEUE + (u32)(long)(short)((short)0x000D * (short)i) + 0x0A);
}

/* H'22AA5C. Whether a pattern is one of the ones marked H'16 in the
 * catalogue -- those are the ones that are not turned round. */
u8 pattern_not_16(u16 n)
{
    return (u8)((REG8(stitch_record(n) + 0x17) == 0x16) ? 0 : 1);
}

/* H'2296D2. Which way round an entry is drawn: bits 6 and 7 of its second
 * byte, with 1 and 2 swapped when the pattern may be turned. */
u8 queue_entry_facing(u16 i)
{
    u32 e = QUEUE + (u32)(long)(short)((short)0x000D * (short)i);
    u8  face = (u8)((REG8(e + 1) >> 6) & 0x03);

    if (pattern_not_16(queue_entry_number(i)) != 0) {
        if (face == 0x01 || face == 0x02) face = (u8)(face ^ 0x03);
    }
    return face;
}

void picker_goto(u16 pos);
void picker_forward(u16 n);
void picker_back(u16 n);
void picker_cursor(u8 mode);
void picker_rebuild(u16 n, u8 show_number, u8 redraw);
void picker_draw_range(u16 x, u16 from, u16 to);

/* The thumbnail for a position: the cached pattern number plus the entry's
 * own offset, then the pointer at +H'0C of that catalogue record. */
static u32 picker_thumb(u16 i)
{
    u16 n = (u16)(REG16(PICK_CACHE + (u32)(long)(short)((short)i << 1)) +
                  (u16)queue_entry_offset(i));

    return REG32(stitch_record(n) + 0x0C);
}

static u16 picker_thumb_width(u16 i)
{
    return header_word_0((const u8 *)picker_thumb(i));
}

/* H'21700A. A string drawn from a font table, in a box that is cleared
 * first. The glyph for a character is the pointer at font + ch * 4 - H'84,
 * so the table starts at H'21 -- the first printable character. Each glyph
 * carries its own width and height and sits on the baseline at y1.
 *
 * Three alignments: 1 from the left, 0 from the right, 2 centred, and the
 * centred one has to measure the whole string first.
 */
void text_draw(const char *str, u16 x0, u16 y0, u16 x1, u16 y1, u16 gap,
               u8 align, const u8 *font)
{
    u16 len = (u16)str_length(str);
    const u8 *g;
    u16 w, h, y;
    short x, i;

    draw_rect(x0, y0, x1, y1, LCD_FRAME_A, 0, 1);

    if (str == 0) return;

    if (align == 0x01) {
        x = (short)x0;
        for (i = 0; i < (short)len; i++) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            w = header_word_0(g);
            h = header_word_1(g);
            y = (u16)((u16)y1 - h);
            bitmap_draw((u16)x, y, (u16)((u16)x + w), (u16)(y + h), g,
                        LCD_FRAME_A);
            x = (short)(x + (short)(u16)(gap + w + 1));
        }
        return;
    }

    if (align == 0x00) {
        x = (short)x1;
        for (i = (short)(len - 1); i >= 0; i--) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            w = header_word_0(g);
            h = header_word_1(g);
            y = (u16)((u16)y1 - h);
            x = (short)(x - (short)w);
            bitmap_draw((u16)x, y, (u16)((u16)x + w), (u16)(y + h), g,
                        LCD_FRAME_A);
            x = (short)(x - (short)(u16)(gap + 1));
        }
        return;
    }

    if (align == 0x02) {
        u16 mid = (u16)((short)((short)((u16)x0 + (u16)x1) / (short)2));
        u16 total = 0;

        for (i = 0; i < (short)len; i++) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            total = (u16)(total + header_word_0(g) + 1);
        }
        total = (u16)(total - 1);
        total = (u16)(total + (u16)((u16)(len - 1) * gap));

        x = (short)((u16)mid - (u16)((short)((short)total / (short)2)));

        for (i = 0; i < (short)len; i++) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            w = header_word_0(g);
            h = header_word_1(g);
            y = (u16)((u16)y1 - h);
            bitmap_draw((u16)x, y, (u16)((u16)x + w), (u16)(y + h), g,
                        LCD_FRAME_A);
            x = (short)(x + (short)(u16)(gap + w + 1));
        }
    }
}

/* H'22A1DE. The position's number, drawn in the corner of the strip. */
void dialog_number_draw(u16 n)
{
    char digits[4];

    int_to_decimal((short)n, digits);
    text_draw(digits, 0x00A4, 0x00DA, 0x00C2, 0x00E1, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

/* H'22955A. Draws the thumbnails for positions [from] to [to], starting at
 * x and stopping when the next one would run off the right edge. What is
 * left of the strip is cleared. */
void picker_draw_range(u16 x, u16 from, u16 to)
{
    short i  = (short)from;
    short px = (short)x;
    u32   g;
    u16   w, h;

    while (i <= (short)to) {
        g = picker_thumb((u16)i);
        w = header_word_0((const u8 *)g);

        if ((short)((short)w + px) > (short)0x00E7) {
            if (px <= (short)0x00E7) {
                draw_rect((u16)px, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                          LCD_FRAME_A, 0, 1);
            }
            return;
        }

        if (i != 0 && px >= (short)DIALOG_X0) {
            draw_rect((u16)px, DIALOG_Y0, (u16)((short)w + px), DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
            h = header_word_1((const u8 *)g);
            bitmap_draw_mirrored((u16)px, (u16)(0x00BF - h),
                                 (u16)((short)w + px), 0x00BF,
                                 (const u8 *)g, LCD_FRAME_A,
                                 queue_entry_facing((u16)i));
            px = (short)(px + (short)(u16)(w + 1));
        }
        i++;
    }

    if ((short)(i - 1) == (short)PICK_LAST) {
        if (px <= (short)0x00E7) {
            draw_rect((u16)px, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
        }
    }
}

/* The cursor is a short vertical line in the second buffer, under the
 * thumbnail. Screen H'43 keeps its position somewhere else and draws it
 * upwards rather than down. */
static void picker_cursor_line(u8 colour)
{
    if (REG8(0x11A169UL) == 0x43) {
        draw_line(REG16(0x11B3CEUL), REG16(0x11B3D0UL),
                  REG16(0x11B3CEUL), (u16)(REG16(0x11B3D0UL) + 0xFFE2),
                  LCD_FRAME_B, colour);
    } else {
        draw_line(PICK_X, PICK_Y, PICK_X, (u16)(PICK_Y + 0x001E),
                  LCD_FRAME_B, colour);
    }
}

/* H'228216. The cursor: put it out, take it away, or blink it.
 *
 * Mode 4 means "whatever it was doing"; H'11A1E9 remembers that. Mode 3 is
 * the once-a-pass tick, which counts H'11A1E6 to H'64 and flips the line
 * between colour 0 and colour 3.
 */
void picker_cursor(u8 mode)
{
    u8 m = mode;

    if (m == 0x04) m = REG8(0x11A1E9UL);
    if (m != REG8(0x11A1E9UL) && m != 0x03) REG8(0x11A1E9UL) = m;

    if (m == 0x00) {
        if (REG8(0x11A1E8UL) == 0) {
            picker_cursor_line(0);
            REG8(0x11A1E8UL) = 0x01;
        }
        REG8(0x11A1EAUL) = 0;
        return;
    }

    if (m == 0x02) return;
    if (m != 0x01 && m != 0x03) return;

    if (m == 0x01) {
        /* Mode 1 does not return: it puts the cursor where it belongs and
         * then falls into the blink below, the same as mode 3. */
        REG8(0x11A1EAUL) = 0x01;
        if (REG8(0x11A169UL) != 0x43) picker_goto(QUEUE_POS);
    } else {
        if (REG8(0x11A1DEUL) != 0) {
            QUEUE_POS = REG16(0x11B3D2UL);
            REG8(0x11A1DEUL) = 0;
        }
        if (REG8(0x11A1E4UL) != 0 && REG8(0x11A169UL) != 0x41) {
            picker_rebuild(REG16(0x11B3D4UL), 0x01, 0x01);
            REG8(0x11A1E4UL) = 0;
        }
        if (REG8(0x11A1EAUL) != 0x01) {
            REG8(0x11A1E9UL) = 0;
            return;
        }
        REG8(0x11A1E9UL) = 0x01;
    }

    REG16(0x11A1E6UL) = (u16)(REG16(0x11A1E6UL) + 1);
    if (REG16(0x11A1E6UL) >= 0x0064) {
        picker_cursor_line((u8)(REG8(0x11A1E8UL) != 0 ? 0x03 : 0x00));
        REG8(0x11A1E8UL) = (u8)(REG8(0x11A1E8UL) == 0 ? 0x01 : 0x00);
        REG16(0x11A1E6UL) = 0;
    }
}

/* H'2284C4. Puts the cursor on a position, scrolling either way to get
 * there. H'FFFF means "back to the beginning". */
void picker_goto(u16 pos)
{
    if (REG8(0x114DC6UL) & 0x80) return;

    if (pos == 0xFFFF) {
        picker_cursor(0);
        PICK_X = DIALOG_X0;
        QUEUE_POS = PICK_FIRST;
        PICK_POS = PICK_FIRST;
        picker_cursor(1);
        return;
    }

    if ((short)pos < (short)PICK_POS) {
        picker_back((u16)((u16)PICK_POS - pos));
    } else if ((short)pos > (short)PICK_POS) {
        picker_forward((u16)(pos - (u16)PICK_POS));
    }
}

/* H'22852E. Forward [n] positions. The widths of everything stepped over
 * are added up; if that carries the cursor past the right edge the strip is
 * scrolled, and if it would have to scroll further than the strip is wide it
 * is redrawn from the new position instead. */
void picker_forward(u16 n)
{
    u16 total = 0;
    u16 first;
    short i;
    u16 shift;

    if (PICK_POS == PICK_LAST) return;

    picker_cursor(0);
    first = (u16)(PICK_POS + 1);

    if ((short)((u16)PICK_POS + n) > (short)PICK_LAST) PICK_POS = PICK_LAST;
    else                                               PICK_POS = (u16)(PICK_POS + n);

    for (i = (short)first; i <= (short)PICK_POS; i++) {
        total = (u16)(total + picker_thumb_width((u16)i) + 1);
    }

    if ((short)((u16)PICK_X + total) > (short)0x00E7) {
        shift = dialog_shift_to_right_edge((u16)(PICK_X + total));
        PICK_X = (u16)(PICK_X - shift);

        if ((short)PICK_X < (short)DIALOG_X0) {
            draw_rect(DIALOG_X0, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
            picker_draw_range(DIALOG_X0, PICK_POS, PICK_LAST);
            PICK_X = (u16)(picker_thumb_width(PICK_POS) + DIALOG_X0);
        } else {
            dialog_scroll_left(shift);
            draw_rect((u16)(PICK_X + 1), DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
            picker_draw_range((u16)(PICK_X + 1), first, PICK_POS);
            PICK_X = (u16)(PICK_X + total);
        }
    } else {
        PICK_X = (u16)(PICK_X + total);
    }

    QUEUE_POS = PICK_POS;
    picker_cursor(1);
}

/* H'228734. Back [n] positions, the same the other way about. */
void picker_back(u16 n)
{
    u16 total = 0;
    short target;
    short i;
    u16 shift;

    if (PICK_POS == PICK_FIRST) return;

    picker_cursor(0);

    if ((short)((u16)PICK_POS - n) < (short)PICK_FIRST) target = (short)PICK_FIRST;
    else                                                target = (short)(u16)(PICK_POS - n);

    for (i = target; i <= (short)PICK_POS; i++) {
        total = (u16)(total + picker_thumb_width((u16)i) + 1);
    }

    if ((short)((u16)PICK_X - total) < (short)DIALOG_X0) {
        if ((short)(PICK_FIRST + 1) < (short)PICK_POS) {
            shift = dialog_shift_to_left_edge((u16)(PICK_X - total));
            PICK_X = (u16)(PICK_X + shift);

            if ((short)PICK_X >= (short)0x00E7) {
                draw_rect(DIALOG_X0, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                          LCD_FRAME_A, 0, 1);
                picker_draw_range(DIALOG_X0, (u16)target, PICK_LAST);
                if (target == 0) {
                    PICK_X = DIALOG_X0;
                } else {
                    PICK_X = (u16)(picker_thumb_width((u16)target) +
                                   DIALOG_X0);
                }
            } else {
                dialog_scroll_right(shift);
                draw_rect(DIALOG_X0, DIALOG_Y0, PICK_X, DIALOG_Y1,
                          LCD_FRAME_A, 0, 1);
                PICK_X = (u16)(PICK_X - total);
                picker_draw_range((u16)(PICK_X + 1), (u16)target, PICK_POS);
                PICK_X = (u16)(PICK_X + picker_thumb_width((u16)target) + 1);
            }
        }
    } else {
        total = (u16)(total - (picker_thumb_width((u16)target) + 1));
        PICK_X = (u16)(PICK_X - total);
    }

    PICK_POS = (u16)target;
    QUEUE_POS = (u16)target;
    picker_cursor(1);
}

/* H'22A098. Rebuilds the whole picker: the queue is copied out of flash,
 * every position's pattern number is cached, and the row is redrawn. */
void picker_rebuild(u16 n, u8 show_number, u8 redraw)
{
    short i;

    REG8(0x11A184UL) = 0;
    REG16(0x11A1CEUL) = n;

    if (show_number != 0) dialog_number_draw(n);

    if (redraw != 0) {
        draw_rect(DIALOG_X0, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                  LCD_FRAME_A, 0, 1);
    }

    mem_copy((u8 *)0x0011BBAAUL, (const u8 *)0x00578000UL, 0x32D5);

    REG16(PICK_CACHE) = queue_entry_number_first();
    for (i = 1; i <= (short)0x03E8; i++) {
        REG16(PICK_CACHE + (u32)(long)(short)(i << 1)) =
            queue_entry_number((u16)i);
    }

    if (REG16(0x11EE82UL + (u32)(long)(short)((short)n << 2)) != 0) {
        PICK_FIRST = REG16(0x11EE80UL + (u32)(long)(short)((short)n << 2));
        PICK_LAST  = REG16(0x11EE82UL + (u32)(long)(short)((short)n << 2));
        if (redraw != 0) {
            picker_draw_range(0x0031, (u16)(PICK_FIRST + 1), PICK_LAST);
        }
    } else {
        PICK_FIRST = queue_entry_number_first();
        PICK_LAST  = PICK_FIRST;
    }

    picker_goto(0xFFFF);

    if (PICK_FIRST != PICK_LAST) picker_forward(0x0001);
}

/* H'210DC6. The noise a message makes. H'57EFC8 in the settings block is a
 * pair of bytes a message: the first says whether it makes one at all and
 * the second how many times. */
void message_beep(u16 msg)
{
    u32 e = 0x0057EFC8UL + (u32)(long)(short)((short)msg << 1);

    if (REG8(e) != 0) beep(0x001E, 0x0064, REG8(e + 1));
}

/* H'20369A. A running queue sent back to the start of its group. */
void queue_group_restart(void)
{
    if (REG8(0x11A175UL) != 0) return;

    if (!(REG8(0x114DC9UL) & 0x02)) {
        if (QUEUE_POS != QUEUE_FIRST) return;
        if (QUEUE_FIRST == QUEUE_LAST) return;
    }

    if (REG8(0x114DC9UL) & 0x02) {
        REG8(0x114DC9UL) &= (u8)~0x02;
        REG8(0xFFFEC4UL) |= 0x20;
        REG16(0x114DDEUL) = 0;
    }

    QUEUE_POS = (u16)(QUEUE_GROUP + 1);
    queue_step_flags_first();
}

/* H'20966A. The speed target in the service mode, where the handwheel
 * position rather than the pedal sets it. Past H'64 the reading is stretched
 * on to H'1E..H'FF, and the first time it gets there the machine says so --
 * a beep and message H'1A. */
void speed_target_service(void)
{
    u8 v = REG8(0xFFFEC3UL);
    u8 r;

    if (v == 0x00 || v == 0x02) {
        REG8(0x114DD6UL) = 0;
        REG8(0x114DCFUL) &= (u8)~0x10;
        return;
    }
    if (!(v == 0x01 || (v >= 0x03 && v <= 0x05))) {
        REG8(0x114DD6UL) = 0;
        REG8(0x114DCFUL) &= (u8)~0x10;
        return;
    }

    r = REG8(0xFFFEC2UL);
    if (r < 0x64 || r > 0xD2) {
        if (r > 0xD2) REG8(0x114DD6UL) = 0xFF;
        return;
    }

    REG8(0x114DD6UL) = (u8)((u8)((short)((short)(u16)
                          ((u16)((u16)((u16)r + 0xFF9C) * (u16)0x00E1))
                          / (short)0x006E)) + 0x1E);

    if (!(REG8(0x114DCFUL) & 0x10) && !(REG8(0xFFFEC4UL) & 0x01)) {
        REG8(0x114DCFUL) |= 0x10;
        message_beep(0x0007);
        message_show(0x001A);
    }
}

/* ---- the stitch database, opened -----------------------------------------
 * H'208826. The last of the bring-up calls before the display, and the one
 * that reaches furthest: below it is the stitch pattern database, which is
 * the machine's real subject matter rather than another peripheral.
 *
 * Its own body is small and is written out here: a block of state bytes at
 * H'FFFEF5 upwards, the refresh word at H'FFFF00, and the two counters. What
 * it then calls is not.
 *
 * H'2086FC reads the six parameters of patterns H'03FE and H'03FF back out
 * of the database and writes them into the working records at H'0E4010,
 * through six accessors and one setter that all address a table of 24-byte
 * descriptors pointed at by H'114DD2. H'208210 is the current-stitch setup,
 * 326 instructions over some sixty routines.
 *
 * Both are left as named stubs. Together they are eighty-two routines and
 * about 6,900 instructions -- comparable to everything reconstructed up to
 * this point -- and they are a subsystem rather than a bring-up step, so
 * they belong in a part of their own where the record structure can be
 * pinned down and each accessor checked against the original. Reconstructing
 * the shell around them and leaving the middle guessed at would be worse
 * than leaving them named.
 *
 * Because of that, this routine has no comparison case: any window that
 * takes in the two calls runs the original through the database and the
 * rebuild through a pair of stubs.
 */

void stitch_database_open(void)
{
    REG16(0xFFFF00UL) = 0x1000;

    REG8(0xFFFEF5UL) = 0x04;
    REG8(0xFFFEF6UL) = 0x20;
    REG8(0xFFFEF7UL) = 0x00;
    REG8(0xFFFEF8UL) = 0x00;
    REG8(0xFFFEF9UL) = 0x00;
    REG8(0xFFFEFAUL) = 0x00;
    REG8(0xFFFEFBUL) = 0x00;
    REG8(0xFFFEFCUL) = 0x00;
    REG8(0x11A6ADUL) = 0x00;

    REG16(0xFFFEE0UL) = 0x0001;
    REG8(0xFFFEFDUL) = 0x00;

    stitch_params_reload();
    stitch_state_init();
}

/* H'22B242. The big picture of one queue position, drawn at [x],[y].
 *
 * With nothing to show -- no position, only one position, or the cursor at
 * the first -- the box is painted out instead, which is the same three
 * questions in the same order that decide whether the picker will let the
 * screen go.
 *
 * H'11F28A to H'11F290 remember the rectangle the last picture went in, so
 * that it can be rubbed out before the new one is drawn; they are then set
 * to the new one. The picture's own header gives its width and height, and
 * it is blitted the way round the entry says.
 *
 * [redraw] also puts the strip itself back, starting far enough to the left
 * that the picture's width is allowed for.
 */
void picker_preview(u16 x, u16 y, u16 pos, u8 redraw)
{
    u32 g;

    if (!picker_may_leave()) {
        draw_rect(x, y, (u16)(x + 0x0042), (u16)(y + 0x0029),
                  LCD_FRAME_A, 0x00, 0x01);
        return;
    }

    g = picker_thumb(pos);

    draw_rect(REG16(0x0011F28AUL), REG16(0x0011F28CUL),
              REG16(0x0011F28EUL), REG16(0x0011F290UL),
              LCD_FRAME_A, 0x00, 0x01);

    REG16(0x0011F28AUL) = x;
    REG16(0x0011F28CUL) = y;
    REG16(0x0011F28EUL) = (u16)(header_word_0((const u8 *)g) + x);
    REG16(0x0011F290UL) = (u16)(header_word_1((const u8 *)g) + y);

    bitmap_draw_mirrored(REG16(0x0011F28AUL), REG16(0x0011F28CUL),
                         REG16(0x0011F28EUL), REG16(0x0011F290UL),
                         (const u8 *)g, LCD_FRAME_A, queue_entry_facing(pos));

    if (redraw != 0) {
        picker_draw_range(
            (u16)(REG16(0x0011A1C8UL) - header_word_0((const u8 *)g)),
            pos, pos);
    }
}

/* ---- screen H'43, the queue laid out as a strip ------------------------
 * The queue seen all at once: every entry's picture drawn side by side,
 * wrapping to a new row when the next one would pass the right edge, with a
 * cursor under the current entry and an arrow at each end.
 *
 * Five words at H'11A1D4 describe the area it fills -- left, right, top,
 * bottom and the height of one row -- and three at H'11B3CE carry where the
 * drawing has got to: the x it will put the next picture at, the baseline of
 * the row it is on, and which entry the cursor is under. H'11A1DF and
 * H'11A1E0 remember which way each arrow is drawn.
 */
#define STRIP_X0    REG16(0x0011A1D4UL)
#define STRIP_X1    REG16(0x0011A1D6UL)
#define STRIP_Y0    REG16(0x0011A1D8UL)
#define STRIP_Y1    REG16(0x0011A1DAUL)
#define STRIP_ROW   REG16(0x0011A1DCUL)
#define STRIP_X     REG16(0x0011B3CEUL)
#define STRIP_Y     REG16(0x0011B3D0UL)
#define STRIP_AT    REG16(0x0011B3D2UL)

/* The picture the queue's [i]th entry stands for. */
static u32 queue_strip_picture(u16 i)
{
    const u16 n = (u16)(REG16(PICK_CACHE +
        (u32)(long)(short)(u16)((u16)((u16)i << 1))) + queue_entry_offset(i));

    return REG32(ITEM_TABLE +
        (u32)(long)(short)(u16)((u16)(n * ITEM_STRIDE)) + 0x0C);
}

/* H'22B4B4. Entries [first] to [last] drawn along the strip from wherever
 * the caller says, wrapping to the next row down when one will not fit and
 * stopping when the rows run out.
 *
 * [y] is the baseline: a picture is drawn upwards from it by its own
 * height, and which way round it faces comes from the queue entry.
 */
void queue_strip_run_draw(u16 y, u16 first, u16 last)
{
    u16 x = STRIP_X0;
    short i;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 picture = queue_strip_picture((u16)i);
        u16 right = (u16)(header_word_0((const u8 *)picture) + x);

        if ((short)right > (short)STRIP_X1) {
            x = STRIP_X0;
            right = (u16)(header_word_0((const u8 *)picture) + x);
            y = (u16)(y + STRIP_ROW);
            if ((short)y > (short)STRIP_Y1) return;
        }

        bitmap_draw_mirrored(x, (u16)(y - header_word_1((const u8 *)picture)),
                             right, y, (const u8 *)picture, LCD_FRAME_A,
                             queue_entry_facing((u16)i));
        x = (u16)(right + 1);
    }
}

/* H'22B592. The two arrows, drawn again only when they change. The one at
 * the end of the queue goes out when the cursor reaches the last entry and
 * the one at the start when it reaches the first. */
void queue_strip_arrows(void)
{
    if (STRIP_AT == PICK_LAST) {
        if (REG8(0x0011A1E0UL) != 0) {
            hitbox_blit(0x0002, LCD_FRAME_A, ARROW_ON_OFF);
            REG8(0x0011A1E0UL) = 0x00;
        }
    } else if ((short)STRIP_AT < (short)PICK_LAST) {
        if (REG8(0x0011A1E0UL) == 0) {
            hitbox_blit(0x0002, LCD_FRAME_A, ARROW_ON_ON);
            REG8(0x0011A1E0UL) = 0x01;
        }
    }

    if (STRIP_AT == PICK_FIRST) {
        if (REG8(0x0011A1DFUL) != 0) {
            hitbox_blit(0x0001, LCD_FRAME_A, ARROW_BACK_OFF);
            REG8(0x0011A1DFUL) = 0x00;
        }
    } else if ((short)STRIP_AT > (short)PICK_FIRST) {
        if (REG8(0x0011A1DFUL) == 0) {
            hitbox_blit(0x0001, LCD_FRAME_A, ARROW_BACK_ON);
            REG8(0x0011A1DFUL) = 0x01;
        }
    }
}

/* H'22B8AE and H'22B94A. The whole strip moved one row up or one row down,
 * with the row it leaves behind blacked out. */
void queue_strip_scroll_up(void)
{
    region_copy(STRIP_X0, (u16)(STRIP_Y0 + 1), STRIP_X1, STRIP_Y1,
                (u16)(STRIP_Y0 - STRIP_ROW + 1), LCD_FRAME_A, LCD_FRAME_A);
    draw_rect(STRIP_X0, (u16)(STRIP_Y1 - STRIP_ROW + 1), STRIP_X1, STRIP_Y1,
              LCD_FRAME_A, 0x00, 0x01);
}

void queue_strip_scroll_down(void)
{
    region_copy(STRIP_X0, (u16)(STRIP_Y0 - STRIP_ROW + 1), STRIP_X1,
                (u16)(STRIP_Y1 - STRIP_ROW), (u16)(STRIP_Y0 + 1),
                LCD_FRAME_A, LCD_FRAME_A);
    draw_rect(STRIP_X0, (u16)(STRIP_Y0 - STRIP_ROW + 1), STRIP_X1, STRIP_Y0,
              LCD_FRAME_A, 0x00, 0x01);
}

/* H'22B9E8. Walking back from entry [from], where the row it is on begins
 * and how far along that row the walk got.
 *
 * The widths are added up backwards until one more would pass the right
 * edge, or until the first entry of the queue is reached. The entry the row
 * starts with goes back through [first]. */
u16 queue_row_back(u16 *first, u16 from)
{
    u16 x = STRIP_X0;
    short i = (short)from;

    for (;;) {
        const u32 picture = queue_strip_picture((u16)i);
        const u16 right = (u16)(header_word_0((const u8 *)picture) + x);

        if ((short)(u16)(right + 1) > (short)STRIP_X1 ||
            (u16)(PICK_FIRST + 1) == (u16)i) {
            if ((u16)(PICK_FIRST + 1) == (u16)i) {
                *first = (u16)i;
                return right;
            }
            *first = (u16)(i + 1);
            return (u16)(x - 1);
        }

        x = (u16)(right + 1);
        i--;
    }
}

/* H'22BA86. Which entry the row that [upto] falls on begins with: the
 * widths added up forwards from the first entry, and the answer moved on
 * every time the row wraps. */
u16 queue_row_first(u16 upto)
{
    u16 x = STRIP_X0;
    u16 answer = (u16)(PICK_FIRST + 1);
    short i;

    for (i = (short)(u16)(PICK_FIRST + 1); i <= (short)upto; i++) {
        const u32 picture = queue_strip_picture((u16)i);
        u16 right = (u16)(header_word_0((const u8 *)picture) + x);

        if ((short)right > (short)STRIP_X1) {
            x = STRIP_X0;
            right = (u16)(header_word_0((const u8 *)picture) + x);
            answer = (u16)i;
        }
        x = (u16)(right + 1);
    }

    return answer;
}

/* H'22B698 and H'22B7B8. The cursor moved one entry on or one entry back.
 *
 * Both take the cursor out first and put it back at the end. Moving on past
 * the right edge starts a new row, and past the bottom row scrolls the whole
 * strip up and draws the new bottom row; moving back is the same the other
 * way, except that where a row *starts* has to be worked out by walking the
 * widths backwards.
 */
void queue_strip_forward(void)
{
    u32 picture;

    if (STRIP_AT == PICK_LAST) return;

    picker_cursor(0x00);
    STRIP_AT = (u16)(STRIP_AT + 1);
    picture = queue_strip_picture(STRIP_AT);
    STRIP_X = (u16)(STRIP_X + header_word_0((const u8 *)picture) + 1);

    if ((short)STRIP_X > (short)STRIP_X1) {
        picture = queue_strip_picture(STRIP_AT);
        STRIP_X = (u16)(header_word_0((const u8 *)picture) + STRIP_X0);

        if ((short)(u16)(STRIP_Y + STRIP_ROW) > (short)STRIP_Y1) {
            queue_strip_scroll_up();
            queue_strip_run_draw(STRIP_Y, STRIP_AT, PICK_LAST);
        } else {
            STRIP_Y = (u16)(STRIP_Y + STRIP_ROW);
        }
    }

    picker_cursor(0x01);
}

void queue_strip_back(void)
{
    u32 picture;
    u16 first = 0;

    if ((u16)(PICK_FIRST + 1) == STRIP_AT) return;

    picker_cursor(0x00);
    picture = queue_strip_picture(STRIP_AT);
    STRIP_X = (u16)(STRIP_X - (header_word_0((const u8 *)picture) + 1));

    if ((short)STRIP_X <= (short)STRIP_X0) {
        STRIP_X = queue_row_back(&first, (u16)(STRIP_AT - 1));

        if ((short)(u16)(STRIP_Y - STRIP_ROW) >= (short)STRIP_Y0) {
            STRIP_Y = (u16)(STRIP_Y - STRIP_ROW);
        } else {
            queue_strip_scroll_down();
            queue_strip_run_draw(STRIP_Y, first, STRIP_AT);
        }
    }

    STRIP_AT = (u16)(STRIP_AT - 1);
    picker_cursor(0x01);
}

/* H'22B3A2. Screen H'43 itself.
 *
 * The lay-out draws the row the cursor's entry is on and then steps the
 * cursor forward to it one entry at a time, which is how the three words at
 * H'11B3CE end up holding where it really is. Three boxes: the two arrows
 * and the way out.
 */
void queue_strip_screen(u8 fresh)
{
    u16 value = 0, index = 0;

    if (fresh != 0) {
        u16 first;
        short i;

        REG8(0x0011A1E0UL) = 0x01;
        REG8(0x0011A1DFUL) = 0x01;
        STRIP_X = (u16)(STRIP_X0 - 1);
        STRIP_Y = STRIP_Y0;

        first = queue_row_first(PICK_POS);
        queue_strip_run_draw(STRIP_Y0, first, PICK_LAST);
        STRIP_AT = (u16)(first - 1);

        for (i = (short)first; i <= (short)PICK_POS; i++) queue_strip_forward();

        REG8(0x0011A1DEUL) = 0x01;
    }

    queue_strip_arrows();

    if (touch_hit(0x0001, 0x0003, &value, &index) != 0x03) return;

    if (value == 0x0040) {
        if (REG8(0x0011A1DFUL) != 0) {
            message_show_held(index);
            queue_strip_back();
        }
        return;
    }

    if (value == 0x0041) {
        if (REG8(0x0011A1E0UL) != 0) {
            message_show_held(index);
            queue_strip_forward();
        }
        return;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        message_show_held(index);
        screen_switch(REG8(0x0011B0A3UL), 0x01, 0x00);
    }
}

/* H'21CF9C. The screen stack emptied. */
void screen_stack_clear(void)
{
    REG8(0x0011A18BUL) = 0x00;
    REG8(0x0011A17CUL) = 0x00;
}
