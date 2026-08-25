/* The artista 180 application, rebuilt in C: SCI0: the embroidery module's
 * link, and its three interrupts.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- SCI0, the embroidery module's link -------------------------------
 * Three interrupts -- receive error, receive full, transmit empty -- and
 * about 3,900 instructions of protocol behind them. The link is 57600 baud
 * on channel 0, half duplex, and everything about it lives in H'11F29E
 * upwards:
 *
 *   H'11F29E  the link state: 0 idle, 1 sending, 2 sending the checksum,
 *             3 and 4 turning the line round, 5 a retry, 6 a timeout,
 *             7 abandon, 8 the answer arrived
 *   H'11F29F  how many times round the loop this state has been
 *   H'11F2A1  which message is being sent, and the switch key for it
 *   H'11F2A2  its sub-code, which for message 5 is a second switch key
 *   H'11F2A3  the byte about to go out
 *   H'11F2A4  the running sum of everything sent, which is the checksum
 *   H'11F2A6  which byte of the message this is
 *   H'11F2A8  how many bytes the message has
 *   H'11F2B5  the retry count
 *   H'11F2B6  the receive state
 *   H'11F2B9  which message came back, and the switch key for it
 *   H'11F2BA  its sub-code, and a second switch key
 *   H'11F2BB  the byte that just arrived
 *
 * A frame is one byte of "message and sub-code" packed as (message << 4) |
 * sub-code, then the payload, then the sum. Every message is laid out the
 * same way, and what changes between them is only where the payload is read
 * from and how long it is.
 */

/* H'2350AE. Start sending. The message is already in H'11F2A1; this clears
 * the running state, works out which of the panel's four "busy" bits to
 * raise for it, and turns the transmitter and its interrupt on. */
void link_send_start(void)
{
    REG8(0x11F2A4UL) = 0x00;
    REG16(0x11F2A6UL) = 0x0000;
    REG8(0x11F2A0UL) = 0x00;

    if (REG8(0x11F29EUL) == 0) {
        REG8(0x11F29EUL) = 0x01;
        REG16(0x11F2A8UL) = 0x0000;
        REG8(0x11F29FUL) = 0x00;   /* the register was just zeroed for A8 */

        switch (REG8(0x11F2A1UL)) {
        case 0x03:
            if (REG8(0x11A616UL) == 0) REG8(0x114D50UL) |= 0x01;
            break;
        case 0x04:
        case 0x01:
            REG8(0x114D50UL) |= 0x02;
            break;
        case 0x0D:
            if (REG8(0x11F2A2UL) != 0) REG8(0x114D50UL) |= 0x20;
            break;
        default:
            break;
        }
    }

    SCR0 |= 0x20;   /* transmit enable */
    SCR0 |= 0x80;   /* transmit interrupt enable */
}

/* H'23514A. Stop sending, and put the line back the way the receiver wants
 * it: bits 0 and 1 of P9DR are the direction control. */
void link_send_stop(void)
{
    SCR0 &= (u8)~0x80;
    P9DR = (u8)(P9DR | 0x03);
}

/* H'2351C4. The twelve bytes at H'11A612 cleared, one at a time. */
void link_clear_11A612(void)
{
    u8 i;

    for (i = 0; i < 0x0C; i++) REG8(0x0011A612UL + i) = 0x00;
}

/* H'2351F4. A wait of [units], each about H'1A9 turns of an empty loop, with
 * the host link serviced between them so a download is not starved. */
void link_delay(u16 units)
{
    volatile u16 outer = units;

    while (outer != 0) {
        volatile u16 inner = 0x01A9;

        outer--;
        while (inner != 0) inner--;
        rom_host_service();
    }
}

/* H'23521E, H'235230, H'235242, H'235254. Four byte-gap delays, counted in
 * a register and nothing else. The first two are the same hundred turns
 * written out twice. */
static void link_gap(u16 turns)
{
    volatile u16 n = turns;

    while (n != 0) n--;
}

void link_gap_100(void)  { link_gap(0x0064); }
void link_gap_100b(void) { link_gap(0x0064); }
void link_gap_10(void)   { link_gap(0x000A); }
void link_gap_350(void)  { link_gap(0x015E); }

/* H'235282 and H'235296. Two pairs of bytes in the module's shared area. */
void link_clear_0FFC18(void)
{
    REG8(0x000FFC19UL) = 0x00;
    REG8(0x000FFC18UL) = 0x00;
}

void link_clear_0FFC16(void)
{
    REG8(0x000FFC17UL) = 0x00;
    REG8(0x000FFC16UL) = 0x00;
}

/* ---- the receive-error interrupt --------------------------------------
 * H'233BE4. Framing, parity and overrun all come here.
 *
 * While a message is on its way in, an error costs one byte: the count goes
 * up and the length comes down. In receive states 2 and 4 -- the module
 * answering -- the whole exchange is abandoned and restarted at state 8.
 *
 * The interesting part is the third block. When the link is in state 4 and
 * the receiver is idle, a framing error on an otherwise silent line is how
 * the machine works out whether a module is plugged in at all: it counts the
 * one bits in whatever RDR holds, and more than four of them means the line
 * is idling high with nothing on it.
 */
void isr_sci0_eri_body(void)
{
    u8 ones, i, v;

    if (REG8(0x11F2B6UL) == 0x01) {
        REG16(0x11F2BEUL) = (u16)(REG16(0x11F2BEUL) + 1);
        REG8(0x11F2BCUL) = (u8)(REG8(0x11F2BCUL) - 1);
        REG16(0xFFFF72UL) = 0x0000;
    }

    if (REG8(0x11F2B6UL) == 0x02 || REG8(0x11F2B6UL) == 0x04) {
        REG16(0x11F2BEUL) = 0x0000;
        REG8(0x11F2B6UL) = 0x01;
        REG8(0x11F2BCUL) = 0x00;
        REG16(0xFFFF72UL) = 0x0000;
        REG8(0x11F29EUL) = 0x08;
        REG8(0x11F2B3UL) = 0xFF;
        SSR0 &= (u8)~0x40;
        link_send_start();
    }

    if (REG8(0x11F29EUL) == 0x04 && REG8(0x11F2B6UL) == 0x00) {
        REG8(0xFFFF60UL) &= (u8)~0x02;
        REG16(0xFFFF72UL) = 0x0000;
        SSR0 &= (u8)~0x40;

        REG8(0x11F2BBUL) = RDR0;
        ones = 0;
        for (i = 0; i < 0x08; i++) {
            v = REG8(0x11F2BBUL);
            if (v & 0x01) ones++;
            REG8(0x11F2BBUL) = (u8)(v >> 1);
        }

        REG8(0x11F29EUL) = (u8)((ones > 0x04) ? 0x05 : 0x06);
        link_send_start();
    }

    if (SSR0 & 0x10) REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x04);
    if (SSR0 & 0x20) REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x01);

    SSR0 &= (u8)~0x20;
    SSR0 &= (u8)~0x10;
    SSR0 &= (u8)~0x08;
}

/* ---- the transmit interrupt -------------------------------------------
 * H'2324DA. One byte goes out per interrupt, and which byte it is comes out
 * of the message number in H'11F2A1 and the index in H'11F2A6.
 *
 * Every message starts with the same byte -- the number in the high nibble
 * and the sub-code in the low -- and ends by setting the state to 2, which
 * is how the head of the handler knows to send the checksum next. Between
 * them each message has its own payload, and that is all that differs.
 *
 * The long ones are block transfers: H'11F29F counts which 512-byte block
 * this is, the length that goes out is what is left of the whole thing, and
 * the payload is read straight out of the buffer the pattern lives in.
 */

/* The first byte of a frame. */
static void tx_head_byte(void)
{
    REG8(0x11F2A3UL) = (u8)((u8)(REG8(0x11F2A1UL) << 4) | REG8(0x11F2A2UL));
}

/* Where in the transfer this block starts: the block number doubled as a
 * byte and then put in the high half, so H'200 per block -- and a block
 * number of H'80 or more wraps rather than overflowing. */
static u16 tx_block_offset(void)
{
    return (u16)((u16)(u8)(REG8(0x11F29FUL) << 1) << 8);
}

/* Whatever is left of the transfer, capped at one block. */
static void tx_length_left(u16 total)
{
    u16 left = (u16)(total - tx_block_offset());

    REG16(0x11F2A8UL) = left;
    if (left > 0x0200) {
        left = 0x0200;
        REG16(0x11F2A8UL) = left;
    }
    REG8(0x11F2A3UL) = REG8(0x11F2A8UL);   /* the high byte */
}

/* H'233BAE, and its copy at H'2339F0. The byte out, into the checksum, TDRE
 * cleared, and on to the next index. */
static void tx_byte_out(void)
{
    const u8 b = REG8(0x11F2A3UL);

    TDR0 = b;
    REG8(0x11F2A4UL) = (u8)(REG8(0x11F2A4UL) + b);
    SSR0 &= (u8)~0x80;
    REG16(0x11F2A6UL) = (u16)(REG16(0x11F2A6UL) + 1);
}

/* Message 9. One byte and nothing else. */
static void tx_msg_09(void)
{
    if (REG16(0x11F2A6UL) == 0x0000) {
        tx_head_byte();
        REG8(0x11F29EUL) = 0x02;
    }
}

/* Message 11. Four bytes from H'11A636. */
static void tx_msg_0B(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0002;
        link_gap_100();
    } else if (i <= 0x0004) {
        REG8(0x11F2A3UL) = REG8(0x0011A635UL + i);
        link_gap_100();
        if (i == 0x0004) REG8(0x11F29EUL) = 0x02;
    }
}

/* Message 1. A pattern's H'12-byte record, chosen by H'11A660. */
static void tx_msg_01(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0012;
        link_gap_100();
    } else if (i == 0x0001) {
        REG8(0x11F2A3UL) = REG8(0x11A660UL);
        link_gap_100();
    } else {
        const u32 at = 0x0011A41AUL
                     + (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL))
                     + (u32)(u16)(i - 2);

        REG32(0x11F2AAUL) = at;
        REG8(0x11F2A3UL) = REG8(at);
        if ((u16)(i - 1) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 2. A pattern's H'10-byte block at H'11A25A. */
static void tx_msg_02(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0010;
        link_gap_100();
    } else if (i == 0x0001) {
        REG8(0x11F2A3UL) = REG8(0x11A660UL);
        link_gap_100();
    } else {
        const u32 at = 0x0011A25AUL
                     + (u32)(long)(short)(u16)((u16)REG8(0x11A660UL) << 4)
                     + (u32)(u16)(i - 2);

        REG32(0x11F2AAUL) = at;
        REG8(0x11F2A3UL) = REG8(at);
        if ((u16)(i - 1) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 3. Twelve bytes from H'11A612, and the panel told the link is
 * busy. The gap after each byte is longer while H'11A61A is H'10 or more. */
static void tx_msg_03(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x000C;
        link_gap_100();
        REG8(0x114D50UL) |= 0x01;
    } else if (i == 0x0001) {
        REG32(0x11F2AAUL) = 0x0011A611UL + i;
        REG8(0x11F2A3UL) = REG8(0x0011A611UL + i);
        if (REG8(0x11A61AUL) >= 0x10) link_gap_350();
        else                          link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x0011A611UL + i;
        REG8(0x11F2A3UL) = REG8(0x0011A611UL + i);
        if (REG16(0x11F2A8UL) == i) {
            REG8(0x11F29EUL) = 0x02;
            link_gap_100();
        } else {
            link_gap_10();
        }
    }
}

/* Messages 4 and 6. One byte each. */
static void tx_msg_04(void)
{
    if (REG16(0x11F2A6UL) == 0x0000) {
        tx_head_byte();
        REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 7. Eight bytes of machine state from H'114D4C, or twelve when bit
 * 5 of H'114D51 is up. */
static void tx_msg_07(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0008;
        if (REG8(0x114D51UL) & 0x20) {
            REG16(0x11F2A8UL) = (u16)(REG16(0x11F2A8UL) + 0x0004);
        }
        link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x00114D4BUL + i;
        REG8(0x11F2A3UL) = REG8(0x00114D4BUL + i);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 8. Three bytes from H'11A661, and its sub-code comes from the low
 * nibble of H'114D5C rather than from H'11F2A2. */
static void tx_msg_08(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        const u8 sub = (u8)(REG8(0x114D5CUL) & 0x0F);

        REG8(0x11F2A2UL) = sub;
        REG8(0x11F2A3UL) = (u8)((u8)(REG8(0x11F2A1UL) << 4) | sub);
        link_gap_100();
    } else if (i <= 0x0003) {
        REG8(0x11F2A3UL) = REG8(0x0011A660UL + i);
        if (i == 0x0003) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 12. H'15 bytes from H'104043, with the first two named. */
static void tx_msg_0C(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0015;
        link_gap_100();
    } else if (i == 0x0001) {
        REG8(0x11F2A3UL) = REG8(0x00104044UL);
        link_gap_100();
    } else if (i == 0x0002) {
        REG8(0x11F2A3UL) = REG8(0x00104045UL);
        link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x00104043UL + i;
        REG8(0x11F2A3UL) = REG8(0x00104043UL + i);
        if ((u16)(i - 2) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 13. H'14 bytes from H'104C79. */
static void tx_msg_0D(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0014;
        link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x00104C79UL + i;
        REG8(0x11F2A3UL) = REG8(0x00104C79UL + i);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 14. One byte, and then the whole exchange is dropped: the second
 * time round it clears everything and stops the transmitter without sending
 * anything at all. Returns 0 when nothing is to go out. */
static int tx_msg_0E(void)
{
    if (REG16(0x11F2A6UL) == 0x0000) {
        tx_head_byte();
        link_gap_100();
        return 1;
    }

    REG16(0x11F2A6UL) = 0x0000;
    REG8(0x11F29FUL) = 0x00;
    REG8(0x11F2A2UL) = 0x00;
    link_gap_100();
    link_send_stop();
    REG8(0x11F29EUL) = 0x00;
    return 0;
}

/* ---- message 5, the block transfers -----------------------------------
 * H'232CD6. Fourteen of them, chosen by the sub-code in H'11F2A2. They all
 * have the same three-byte preamble -- the header, then the length of this
 * block high byte and low -- and then up to H'200 bytes of payload.
 */

/* The common tail of a block: one byte out of [base], and the state moved on
 * when the last of the block has gone. [bias] is where the payload starts in
 * the index, which is 3 for most of them and 4 for the one with an extra
 * byte in front. */
static void tx_block_byte(u32 base, u16 bias)
{
    const u16 i  = REG16(0x11F2A6UL);
    const u32 at = base + (u32)(u16)(tx_block_offset() + i - bias);

    REG32(0x11F2AAUL) = at;
    REG8(0x11F2A3UL) = REG8(at);
    if ((u16)(i - (u16)(bias - 1)) == REG16(0x11F2A8UL)) {
        REG8(0x11F29EUL) = 0x02;
    }
}

/* The low byte of the length, and the end of a transfer that has none. */
static void tx_length_low(void)
{
    REG8(0x11F2A3UL) = REG8(0x11F2A9UL);
    if (REG16(0x11F2A8UL) == 0) REG8(0x11F29EUL) = 0x02;
}

/* Sub-codes 1 and 2: a fixed-length buffer. */
static void tx_blk_fixed(u16 total, u32 base)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
    } else if (i == 0x0001) {
        tx_length_left(total);
    } else if (i == 0x0002) {
        tx_length_low();
    } else {
        tx_block_byte(base, 3);
    }
    link_gap_100();
}

/* Sub-code 3. The length is a stitch count out of H'0FFE80, capped at H'1B,
 * multiplied by the two bytes at H'100282 and H'100283 and with H'AE of
 * header added -- and if that comes to more than H'3DB6 it is clamped and
 * the panel is told the pattern was too big. */
static void tx_blk_sub3(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
    } else if (i == 0x0001) {
        const u8 b = REG8(0x000FFE80UL);
        u16 n = (u16)((b > 0x1B) ? 0x001B : (u16)b);
        u32 prod;
        u16 total;

        REG16(0x11F2A8UL) = n;
        prod = (u32)(u16)((u16)REG8(0x00100282UL) *
                          (u16)REG8(0x00100283UL)) * (u32)n;
        total = (u16)((u16)prod + 0x00AE);
        REG16(0x11F2A8UL) = total;
        if (total > 0x3DB6) {
            total = 0x3DB6;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
        link_gap_100();
    } else if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
    } else {
        tx_block_byte(0x00100280UL, 3);
        link_gap_100b();
    }
}

/* Sub-code 4. Nine named bytes and then the payload, which is addressed as a
 * longword: the block number times H'200 plus a base out of H'11F2CC and a
 * pointer out of H'11F2AE, so this one can reach past 64K. Running past the
 * end of the buffer -- more than H'10000 from the pointer -- raises the same
 * "too big" bit. */
static void tx_blk_sub4(void)
{
    const u16 i = REG16(0x11F2A6UL);

    switch (i) {
    case 0x0000:
        tx_head_byte();
        link_gap_100();
        return;
    case 0x0001: {
        u16 left = (u16)(REG16(0x11F2CEUL) - tx_block_offset());

        REG16(0x11F2A8UL) = left;
        if (left > 0x0200) {
            left = 0x0200;
            REG16(0x11F2A8UL) = left;
        }
        REG8(0x11F2A3UL) = (u8)(left >> 8);
        link_gap_100();
        return;
    }
    case 0x0002: REG8(0x11F2A3UL) = REG8(0x11F2A9UL); link_gap_100(); return;
    case 0x0003: REG8(0x11F2A3UL) = REG8(0x11F2B4UL); link_gap_100(); return;
    case 0x0004: REG8(0x11F2A3UL) = REG8(0x11F2CEUL); link_gap_100(); return;
    case 0x0005: REG8(0x11F2A3UL) = REG8(0x11F2CFUL); link_gap_100(); return;
    case 0x0006: REG8(0x11F2A3UL) = REG8(0x001040BEUL); link_gap_100(); return;
    case 0x0007: REG8(0x11F2A3UL) = REG8(0x001040BFUL); link_gap_100(); return;
    case 0x0008: REG8(0x11F2A3UL) = REG8(0x001040C0UL); link_gap_100(); return;
    default: break;
    }

    if (REG16(0x11F2A8UL) == 0) {
        REG8(0x11F29EUL) = 0x02;
        link_gap_100();
        return;
    }

    {
        u32 off = (u32)REG8(0x11F29FUL);
        u32 at, end;
        int n;

        for (n = 0; n < 9; n++) off <<= 1;      /* the block, times H'200 */
        off += (u32)(u16)i;
        off -= 9;

        at  = (u32)REG16(0x11F2CCUL) + off;
        REG32(0x11F2D0UL) = off;
        end = REG32(0x11F2AEUL);
        at += end;
        end += 0x00010000UL;
        REG32(0x11F2AAUL) = at;
        if (end < at) REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);

        REG8(0x11F2A3UL) = REG8(at);
        if ((u16)(i - 8) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100b();
    }
}

/* Sub-code 5. Four named bytes, and a length that is the two bytes at
 * H'1040BC multiplied together plus five. */
static void tx_blk_sub5(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
    } else if (i == 0x0001) {
        u16 total = (u16)((u16)((u16)REG8(0x001040BCUL) *
                                (u16)REG8(0x001040BDUL)) + 0x0005);

        REG16(0x11F2A8UL) = total;
        if (total > 0x0BBD) {
            total = 0x0BBD;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
        link_gap_100();
    } else if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
    } else if (i == 0x0003) {
        REG8(0x11F2A3UL) = REG8(0x001040C0UL);
        link_gap_100();
    } else {
        tx_block_byte(0x001040BCUL, 4);
        link_gap_100b();
    }
}

/* Sub-code 6. The same eight-or-twelve bytes of machine state as message 7. */
static void tx_blk_sub6(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0008;
        if (REG8(0x114D51UL) & 0x20) {
            REG16(0x11F2A8UL) = (u16)(REG16(0x11F2A8UL) + 0x0004);
        }
    } else {
        REG32(0x11F2AAUL) = 0x00114D4BUL + i;
        REG8(0x11F2A3UL) = REG8(0x00114D4BUL + i);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
    }
    link_gap_100();
}

/* Sub-code 7. The length is not known in advance: the buffer at H'104D48 is
 * scanned two bytes at a time for the pair H'80 H'81, which is the end of
 * the pattern, and the length is however far that was -- or a whole block if
 * it is not in this one. H'11F2B2 remembers that the marker landed exactly
 * on a block boundary, so the next block does not scan again.
 *
 * H'11F53A is how much of the buffer is real; a block that starts past that
 * sends nothing and raises the "too big" bit. */
static void tx_blk_sub7(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
        return;
    }

    if (i == 0x0001) {
        u32 at, end;

        REG16(0x11F2A8UL) = 0x0000;

        if (REG8(0x11F2B2UL) == 0x01) {
            REG8(0x11F2B2UL) = 0x00;
        } else {
            u16 n = 0;

            while (n < 0x0200) {
                u32 a = 0x00104D48UL + (u32)(u16)(tx_block_offset() + n);
                u8  b;

                REG32(0x11F2AAUL) = a;
                b = REG8(a);
                REG8(0x11F2A3UL) = b;

                if (b == 0x80) {
                    a = 0x00104D48UL +
                        (u32)(u16)(tx_block_offset() + n + 1);
                    REG32(0x11F2AAUL) = a;
                    b = REG8(a);
                    REG8(0x11F2A3UL) = b;
                    if (b == 0x81) {
                        REG16(0x11F2A8UL) =
                            (u16)(REG16(0x11F2A8UL) + 2);
                        if (REG16(0x11F2A8UL) == 0x0200) {
                            REG8(0x11F2B2UL) = 0x01;
                        }
                        break;
                    }
                }
                REG16(0x11F2A8UL) = (u16)(REG16(0x11F2A8UL) + 2);
                n = (u16)(n + 2);
            }
        }

        at  = 0x00104D48UL + (u32)(long)(short)tx_block_offset();
        end = REG32(0x11F53AUL) + 0x00104D48UL;
        REG32(0x11F2AAUL) = at;
        if (end < at) {
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
            REG16(0x11F2A8UL) = 0x0000;
        }
        REG8(0x11F2A3UL) = REG8(0x11F2A8UL);
        return;
    }

    if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
        return;
    }

    tx_block_byte(0x00104D48UL, 3);
    link_gap_100();
}

/* Sub-code 8. A length that lives in the two bytes at H'104D46 and is capped
 * at one. */
static void tx_blk_sub8(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
    } else if (i == 0x0001) {
        u16 total = REG16(0x00104D46UL);

        REG16(0x11F2A8UL) = total;
        if (total > 0x0001) {
            total = 0x0001;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
    } else if (i == 0x0002) {
        tx_length_low();
    } else {
        tx_block_byte(0x00104D49UL, 3);
    }
    link_gap_100();
}

/* Sub-codes 9, 12, 13 and 14: four short fixed blocks. */
static void tx_blk_short(u16 total, u32 base, u16 bias)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = total;
    } else {
        const u32 at = base + (u32)(u16)(i - bias);

        REG32(0x11F2AAUL) = at;
        REG8(0x11F2A3UL) = REG8(at);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
    }
    link_gap_100();
}

/* Sub-code 13, which is the one that does not keep the pointer. */
static void tx_blk_sub13(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0007;
    } else {
        REG8(0x11F2A3UL) = REG8(0x00104C90UL + (u32)(u16)(i - 1));
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
    }
    link_gap_100();
}

/* Sub-code 11. The length is H'AF for each of a count that is looked up
 * twice: the pattern's record gives an index, and H'0FFE9C gives the count.
 * The clamp here is on the H'20-bit product, not on the word. */
static void tx_blk_sub11(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
    } else if (i == 0x0001) {
        const u32 rec = 0x0011A41AUL +
            (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL));
        const u8  n   = REG8(0x000FFE9CUL + (u32)REG8(rec));
        const u32 prod = 0x00AFUL * (u32)(u16)(n + 1);
        u16 total = (u16)prod;

        REG16(0x11F2A8UL) = total;
        if (prod > 0x00011940UL) {
            total = 0x1940;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
        link_gap_100();
    } else if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
    } else {
        tx_block_byte(0x0004D2D0UL, 3);
        link_gap_100b();
    }
}

static void tx_msg_05(void)
{
    switch (REG8(0x11F2A2UL)) {
    case 0x01: tx_blk_fixed(0x0400, 0x000FFE80UL); break;
    case 0x02: tx_blk_fixed(0x00AC, 0x00104C98UL); break;
    case 0x03: tx_blk_sub3();  break;
    case 0x04: tx_blk_sub4();  break;
    case 0x05: tx_blk_sub5();  break;
    case 0x06: tx_blk_sub6();  break;
    case 0x07: tx_blk_sub7();  break;
    case 0x08: tx_blk_sub8();  break;
    case 0x09: tx_blk_short(0x000C, 0x0011A61EUL, 1); break;
    case 0x0B: tx_blk_sub11(); break;
    case 0x0C: tx_blk_short(0x0004, 0x001040AEUL, 1); break;
    case 0x0D: tx_blk_sub13(); break;
    case 0x0E: tx_blk_short(0x0010, 0x0011A64FUL, 0); break;
    default:   break;   /* including H'0A, which sends the header only */
    }
}

/* ---- message 15 -------------------------------------------------------
 * H'2339F4. Eight bytes about the pattern being stitched, and only when the
 * sub-code is 1. Everything past index 7 sends the same last byte over and
 * over, which is what ends it. */
static u32 tx_pattern_record(void)
{
    return 0x0011A41AUL +
           (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL));
}

static u8 tx_pattern_index(void)
{
    return REG8(tx_pattern_record());
}

static void tx_msg_0F(void)
{
    if (REG8(0x11F2A2UL) != 0x01) return;

    switch (REG16(0x11F2A6UL)) {
    case 0x0000: tx_head_byte(); break;
    case 0x0001: REG8(0x11F2A3UL) = REG8(0x11A642UL); break;
    case 0x0002: REG8(0x11F2A3UL) = REG8(0x11A640UL); break;
    case 0x0003:
        REG8(0x11F2A3UL) = REG8(0x000FFE9CUL + (u32)tx_pattern_index());
        break;
    case 0x0004:
        REG8(0x11F2A3UL) = REG8(0x000FFEB8UL + (u32)tx_pattern_index());
        break;
    case 0x0005:
        REG8(0x11F2A3UL) = REG8(0x00104CCEUL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        break;
    case 0x0006:
        REG8(0x11F2A3UL) = REG8(0x00104CCFUL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        break;
    case 0x0007:
        REG8(0x11F2A3UL) = REG8(0x00104D06UL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        break;
    default:
        REG8(0x11F2A3UL) = REG8(0x00104D07UL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        REG8(0x11F29EUL) = 0x02;
        break;
    }
    link_gap_100();
}

/* ---- the transmit interrupt itself ------------------------------------- */
void isr_sci0_txi_body(void)
{
    switch (REG8(0x11F29EUL)) {
    case 0x05:
        /* A retry: start the message again, and give up after five. */
        REG8(0x11F29EUL) = 0x01;
        REG16(0x11F2A6UL) = 0x0000;
        REG8(0x11F2B5UL) = (u8)(REG8(0x11F2B5UL) + 1);
        link_delay(0x000A);
        if (REG8(0x11F2B5UL) == 0x05) {
            link_send_stop();
            REG8(0x11F2B5UL) = 0x00;
            return;
        }
        break;

    case 0x06:
        /* A timeout: another block, unless H'200 of them have gone by, in
         * which case the message is abandoned and whatever it was for is
         * put back the way it was. */
        REG8(0x11F29FUL) = (u8)(REG8(0x11F29FUL) + 1);
        REG16(0x11F2A6UL) = 0x0000;
        if (REG16(0x11F2A8UL) >= 0x0200) {
            REG8(0x11F29EUL) = 0x01;
            break;
        }
        REG8(0x11F29FUL) = 0x00;
        REG8(0x11F2A2UL) = 0x00;
        link_send_stop();
        if (REG8(0x11F2A1UL) == 0x03) {
            link_clear_11A612();
        } else if (REG8(0x11F2A1UL) == 0x06) {
            REG8(0x114D60UL) = 0xFD;
        } else if (REG8(0x11F2A1UL) == 0x07) {
            REG8(0x114D55UL) &= (u8)~0x01;
            REG8(0x114D4FUL) &= (u8)~0x01;
            REG8(0x114D4FUL) &= (u8)~0x02;
            REG8(0x114D4FUL) &= (u8)~0x40;
            REG8(0x114D4FUL) &= (u8)~0x80;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) & ~0x0002);
            REG8(0x114D52UL) = (u8)(REG8(0x114D52UL) & 0x03);
            REG8(0x114D51UL) &= (u8)~0x20;
        }
        REG8(0x11F29EUL) = 0x00;
        return;

    case 0x07:
        REG16(0x11F2A6UL) = 0x0000;
        REG8(0x11F29FUL) = 0x00;
        REG8(0x11F29EUL) = 0x00;
        link_send_stop();
        return;

    case 0x02:
        /* The checksum. */
        REG8(0x11F2A3UL) = REG8(0x11F2A4UL);
        REG8(0x11F29EUL) = 0x03;
        break;

    case 0x03:
        /* Turn the line round and wait for the answer. */
        REG8(0x11F29EUL) = 0x04;
        link_send_stop();
        REG16(0xFFFF72UL) = 0x0000;
        REG8(0xFFFF60UL) |= 0x02;
        return;

    case 0x08:
        /* Answering something that came in. */
        if (REG16(0x11F2A6UL) == 0x0000) {
            REG8(0x11F2A3UL) = REG8(0x11F2B3UL);
            link_gap_100();
        } else if (REG16(0x11F2A6UL) == 0x0001) {
            if (REG8(0x11F2B9UL) == 0x06) {
                REG8(0x114D60UL) = 0x01;
                REG8(0x114D63UL) = 0x01;
                SCR0 &= (u8)~0x40;
                rom_select_sci0();
            }
            link_send_stop();
            REG8(0x11F29EUL) = 0x00;
            return;
        }
        break;

    default:
        break;
    }

    if (REG8(0x11F29EUL) == 0x01) {
        const u8 msg = REG8(0x11F2A1UL);

        if ((u8)(msg - 1) <= 0x0E) {
            switch (msg) {
            case 0x01: tx_msg_01(); break;
            case 0x02: tx_msg_02(); break;
            case 0x03: tx_msg_03(); break;
            case 0x04: tx_msg_04(); break;
            case 0x05: tx_msg_05(); break;
            case 0x06: tx_msg_04(); break;   /* H'232A24, the same code */
            case 0x07: tx_msg_07(); break;
            case 0x08: tx_msg_08(); break;
            case 0x09: tx_msg_09(); break;
            case 0x0A: break;                /* straight to the byte out */
            case 0x0B: tx_msg_0B(); break;
            case 0x0C: tx_msg_0C(); break;
            case 0x0D: tx_msg_0D(); break;
            case 0x0E: if (!tx_msg_0E()) return; break;
            case 0x0F: tx_msg_0F(); break;
            default: break;
            }
        }
    }

    tx_byte_out();
}

/* ---- the receive interrupt --------------------------------------------
 * H'233CF8. The mirror image of the transmit side: one byte per interrupt,
 * put where the message says it goes.
 *
 * H'11F2B6 is the receive state -- 0 waiting for the first byte of a frame,
 * 1 taking its payload, 2 and 4 waiting for the checksum -- H'11F2BE is the
 * index, H'11F2C0 the length, and H'11F2BC the running sum, which is
 * compared with the byte that arrives at the end.
 */

/* Every message ends the same way: on to the next index, and the byte into
 * the sum. This is H'234EDC, and its copies at H'234C78 and inside several
 * of the handlers. */
static void rx_byte_done(void)
{
    REG16(0x11F2BEUL) = (u16)(REG16(0x11F2BEUL) + 1);
    REG8(0x11F2BCUL) = (u8)(REG8(0x11F2BCUL) + REG8(0x11F2BBUL));
}

/* One byte into a buffer, with the pointer kept where the protocol keeps
 * it, and the state moved on when the index has reached the length. */
static void rx_store(u32 at)
{
    REG32(0x11F2C2UL) = at;
    REG8(at) = REG8(0x11F2BBUL);
    if (REG16(0x11F2C0UL) == REG16(0x11F2BEUL)) REG8(0x11F2B6UL) = 0x04;
}

/* Where a block of a long transfer starts: H'11F2B7 counts them. */
static u16 rx_block_offset(void)
{
    return (u16)((u16)(u8)(REG8(0x11F2B7UL) << 1) << 8);
}

/* The end of a block: state 4 when the whole thing has arrived, state 2
 * when this was a full block and more is coming. */
static void rx_block_end(u16 reached)
{
    const u16 len = REG16(0x11F2C0UL);

    if (reached != len) return;
    REG8(0x11F2B6UL) = (u8)((len < 0x0200) ? 0x04 : 0x02);
}

/* The two length bytes of a block transfer, high then low. */
static void rx_length_high(void)
{
    REG16(0x11F2C0UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
}

static void rx_length_low(void)
{
    const u16 v = (u16)(REG16(0x11F2C0UL) | (u16)REG8(0x11F2BBUL));

    REG16(0x11F2C0UL) = v;
    if (v == 0) REG8(0x11F2B6UL) = 0x04;
}

/* A plain block transfer into [base]. */
static void rx_blk(u32 base)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        /* the header, already unpacked */
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
    } else {
        const u32 at = base + (u32)(u16)(rx_block_offset() + i - 3);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 2));
    }
}

/* Sub-code 3. The same, with the length capped at H'3DB6. */
static void rx_blk_sub3(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
        if (REG16(0x11F2C0UL) > 0x3DB6) {
            REG16(0x11F2C0UL) = 0x3DB6;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
    } else if (i != 0x0000) {
        const u32 at = 0x00100280UL + (u32)(u16)(rx_block_offset() + i - 3);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 2));
    }
}

/* Sub-code 4. Nine named bytes, then a payload addressed as a longword out
 * of H'11F2C6 -- and one that runs past H'10000 from there is clamped to the
 * last byte rather than being allowed to write outside the buffer. */
static void rx_blk_sub4_arrived(void)
{
    REG8(0x11F2B6UL) = 0x04;
    REG8(0x114D50UL) = REG8(0x001040C0UL);
    REG16(0x11F2CCUL) = (u16)(REG16(0x11F2CCUL) + REG16(0x11F2CEUL));
}

static void rx_blk_sub4(void)
{
    const u16 i = REG16(0x11F2BEUL);
    u32 off, at, end;
    int n;

    switch (i) {
    case 0x0000: return;
    case 0x0001: rx_length_high(); return;
    case 0x0002:
        REG16(0x11F2C0UL) =
            (u16)(REG16(0x11F2C0UL) | (u16)REG8(0x11F2BBUL));
        return;
    case 0x0003:
        REG8(0x11F2CBUL) = REG8(0x11F2BBUL);
        if (REG8(0x11F2BBUL) == 0) REG16(0x11F2CCUL) = 0x0000;
        return;
    case 0x0004: REG16(0x11F2CEUL) = (u16)((u16)REG8(0x11F2BBUL) << 8); return;
    case 0x0005:
        REG16(0x11F2CEUL) =
            (u16)(REG16(0x11F2CEUL) | (u16)REG8(0x11F2BBUL));
        return;
    case 0x0006: REG8(0x001040BEUL) = REG8(0x11F2BBUL); return;
    case 0x0007: REG8(0x001040BFUL) = REG8(0x11F2BBUL); return;
    case 0x0008: REG8(0x001040C0UL) = REG8(0x11F2BBUL); return;
    default: break;
    }

    if (REG16(0x11F2C0UL) == 0) rx_blk_sub4_arrived();

    off = (u32)REG8(0x11F2B7UL);
    for (n = 0; n < 9; n++) off <<= 1;
    off += (u32)(u16)i;
    off -= 9;

    at  = (u32)REG16(0x11F2CCUL) + off;
    REG32(0x11F2D0UL) = off;
    end = REG32(0x11F2C6UL);
    at += end;
    end += 0x00010000UL;
    REG32(0x11F2C2UL) = at;
    if (end < at) {
        REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        REG32(0x11F2C2UL) = REG32(0x11F2C6UL) + 0x0000FFFFUL;
    }

    REG8(REG32(0x11F2C2UL)) = REG8(0x11F2BBUL);

    if ((u16)(i - 8) == REG16(0x11F2C0UL)) {
        if (REG16(0x11F2C0UL) < 0x0200) rx_blk_sub4_arrived();
        else                            REG8(0x11F2B6UL) = 0x02;
    }
}

/* Sub-code 5. One extra byte in front of the payload, which is also copied
 * into the panel's busy byte. */
static void rx_blk_sub5(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        return;
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
    } else if (i == 0x0003) {
        REG8(0x001040C0UL) = REG8(0x11F2BBUL);
        REG8(0x114D50UL) = REG8(0x11F2BBUL);
    } else {
        const u32 at = 0x001040BCUL + (u32)(u16)(rx_block_offset() + i - 4);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 3));
    }
}

/* Sub-code 6. The eight-or-twelve bytes of machine state. */
static void rx_blk_sub6(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0008;
        return;
    }

    if (REG8(0x114D51UL) & 0x20) {
        REG16(0x11F2C0UL) = (u16)(REG16(0x11F2C0UL) + 0x0004);
        REG8(0x114D51UL) &= (u8)~0x20;
    }
    rx_store(0x00114D4BUL + i);
}

/* Sub-code 8. The one that remembers where the pattern ended. */
static void rx_blk_sub8(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        return;
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
    } else {
        const u32 at = 0x00104D49UL + (u32)(u16)(rx_block_offset() + i - 3);
        const u16 len = REG16(0x11F2C0UL);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 2) == len) {
            if (len < 0x0200) {
                REG8(0x11F2B6UL) = 0x04;
                REG32(0x00104D44UL) =
                    (u32)(u16)(rx_block_offset() + len);
            } else {
                REG8(0x11F2B6UL) = 0x02;
            }
        }
    }
}

/* Sub-code 11. The length is checked against H'11940 with the block number
 * folded in, so a transfer that would run past the buffer is cut to one. */
static void rx_blk_sub11(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        return;
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        u32 total;

        rx_length_low();
        total = (u32)(u16)(rx_block_offset() + REG16(0x11F2C0UL));
        if (total > 0x00011940UL) {
            REG16(0x11F2C0UL) = 0x0001;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
    } else {
        const u32 at = REG32(0x11F2C6UL) +
                       (u32)(u16)(rx_block_offset() + i - 3);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 2));
    }
}

/* Message 5's fourteen sub-codes. H'2342E8. */
static void rx_msg_05(void)
{
    switch (REG8(0x11F2BAUL)) {
    case 0x01: rx_blk(0x000FFE80UL); break;
    case 0x02: rx_blk(0x00104C98UL); break;
    case 0x03: rx_blk_sub3();  break;
    case 0x04: rx_blk_sub4();  break;
    case 0x05: rx_blk_sub5();  break;
    case 0x06: rx_blk_sub6();  break;
    case 0x07: rx_blk(0x00104D48UL); break;
    case 0x08: rx_blk_sub8();  break;
    case 0x09:
        if (REG16(0x11F2BEUL) == 0x0000) REG16(0x11F2C0UL) = 0x000C;
        else rx_store(0x0011A61DUL + REG16(0x11F2BEUL));
        break;
    case 0x0B: rx_blk_sub11(); break;
    case 0x0C:
        if (REG16(0x11F2BEUL) == 0x0000) REG16(0x11F2C0UL) = 0x0004;
        else rx_store(0x001040ADUL + REG16(0x11F2BEUL));
        break;
    case 0x0D:
        if (REG16(0x11F2BEUL) == 0x0000) {
            REG16(0x11F2C0UL) = 0x0007;
        } else {
            REG8(0x00104C90UL + (u32)(u16)(REG16(0x11F2BEUL) - 1)) =
                REG8(0x11F2BBUL);
            if (REG16(0x11F2C0UL) == REG16(0x11F2BEUL)) {
                REG8(0x11F2B6UL) = 0x04;
            }
        }
        break;
    case 0x0E:
        if (REG16(0x11F2BEUL) == 0x0000) REG16(0x11F2C0UL) = 0x0010;
        else rx_store(0x0011A64FUL + REG16(0x11F2BEUL));
        break;
    default: break;   /* H'0A, which is the header alone */
    }
}

/* Message 15. H'234C7C. Eight values about the pattern, and every one of
 * them is only kept when the pattern's record says H'1C -- which is the one
 * kind of pattern the module can send back. */
static u8 rx_pattern_index(void)
{
    return REG8(0x0011A41AUL +
                (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL)));
}

static void rx_msg_0F(void)
{
    if (REG8(0x11F2BAUL) != 0x01) return;

    switch (REG16(0x11F2BEUL)) {
    case 0x0000: break;
    case 0x0001: REG8(0x11A642UL) = REG8(0x11F2BBUL); break;
    case 0x0002: REG8(0x11A640UL) = REG8(0x11F2BBUL); break;
    case 0x0003:
        if (rx_pattern_index() == 0x1C) {
            REG8(0x000FFE9CUL + (u32)rx_pattern_index()) = REG8(0x11F2BBUL);
        }
        break;
    case 0x0004:
        if (rx_pattern_index() == 0x1C) {
            REG8(0x000FFEB8UL + (u32)rx_pattern_index()) = REG8(0x11F2BBUL);
        }
        break;
    case 0x0005:
        if (rx_pattern_index() == 0x1C) {
            REG16(0x00104CCEUL +
                  (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1)) =
                (u16)((u16)REG8(0x11F2BBUL) << 8);
        }
        break;
    case 0x0006:
        if (rx_pattern_index() == 0x1C) {
            const u32 at = 0x00104CCEUL +
                (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1);
            REG16(at) = (u16)(REG16(at) | (u16)REG8(0x11F2BBUL));
        }
        break;
    case 0x0007:
        if (rx_pattern_index() == 0x1C) {
            REG16(0x00104D06UL +
                  (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1)) =
                (u16)((u16)REG8(0x11F2BBUL) << 8);
        }
        break;
    default:
        if (rx_pattern_index() == 0x1C) {
            const u32 at = 0x00104D06UL +
                (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1);
            REG16(at) = (u16)(REG16(at) | (u16)REG8(0x11F2BBUL));
        }
        REG8(0x11F2B6UL) = 0x04;
        break;
    }
}

/* ---- what happens when the checksum arrives ---------------------------
 * Receive states 2 and 4. The byte that just came in is the module's sum;
 * if it matches ours the answer is H'AA and the message is acted on, and if
 * it does not the answer is H'FF and bit 3 of H'114D4C says so.
 */
static void rx_frame_done(void)
{
    REG8(0xFFFF60UL) &= (u8)~0x02;
    REG16(0xFFFF72UL) = 0x0000;

    switch (REG8(0x11F2B9UL)) {
    case 0x03:
        REG8(0x114D5DUL) = 0x01;
        if (REG8(0x11A615UL) != 0) {
            REG8(0x114D70UL) = (u8)((REG8(0x11F2BAUL) & 0x08) ? 0x01 : 0x00);
            if (REG8(0x11F2BAUL) & 0x01) REG8(0x114D76UL) |=        0x01;
            else                         REG8(0x114D76UL) &= (u8)~0x01;
        }
        break;
    case 0x04:
        REG8(0x114D5EUL) = REG8(0x11F2BAUL);
        break;
    case 0x0D:
        if (REG8(0x11F2BAUL) == 0x01)      REG8(0x114D6DUL) = 0x01;
        else if (REG8(0x11F2BAUL) == 0x02) REG8(0x114D6EUL) = 0x01;
        break;
    case 0x05:
        REG8(0x114D50UL) &= (u8)~0x02;
        break;
    case 0x09:
        REG8(0x114D51UL) |= 0x80;
        break;
    case 0x01:
        REG8(0x114D4FUL) |= 0x04;
        break;
    default:
        break;
    }
}

/* ---- the rest of the messages ----------------------------------------- */

/* Message 1: a pattern's H'12-byte record. Message 2: its H'10-byte block.
 * Both take the pattern number in the second byte, and both clamp it: a
 * number of H'1C or more is forced down to H'1B, which is as many as there
 * is room for. */
static void rx_msg_01(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0012;
    } else if (i == 0x0001) {
        REG8(0x11A660UL) = REG8(0x11F2BBUL);
        if (REG8(0x11F2BBUL) >= 0x1C) REG8(0x11A660UL) = 0x1B;
    } else {
        const u32 at = 0x0011A41AUL +
            (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL)) +
            (u32)(u16)(i - 2);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 1) == REG16(0x11F2C0UL)) REG8(0x11F2B6UL) = 0x04;
    }
}

static void rx_msg_02(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0010;
    } else if (i == 0x0001) {
        REG8(0x11A660UL) = REG8(0x11F2BBUL);
        if (REG8(0x11F2BBUL) >= 0x1C) REG8(0x11A660UL) = 0x1B;
    } else {
        const u32 at = 0x0011A25AUL +
            (u32)(long)(short)(u16)((u16)REG8(0x11A660UL) << 4) +
            (u32)(u16)(i - 2);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 1) == REG16(0x11F2C0UL)) REG8(0x11F2B6UL) = 0x04;
    }
}

/* Message 3. Twelve bytes into H'11A612. */
static void rx_msg_03(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) REG16(0x11F2C0UL) = 0x000C;
    else             rx_store(0x0011A611UL + i);
}

/* Messages 4 and 6. The header alone. */
static void rx_msg_04(void)
{
    if (REG16(0x11F2BEUL) == 0x0000) REG8(0x11F2B6UL) = 0x04;
}

/* Message 7. The eight-or-twelve bytes of machine state. */
static void rx_msg_07(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0008;
        return;
    }
    if (REG8(0x114D51UL) & 0x20) {
        REG16(0x11F2C0UL) = (u16)(REG16(0x11F2C0UL) + 0x0004);
        REG8(0x114D51UL) &= (u8)~0x20;
    }
    rx_store(0x00114D4BUL + i);
}

/* Message 8. Its sub-code goes straight into H'114D5C, and the three bytes
 * after it are a byte and a word. */
static void rx_msg_08(void)
{
    switch (REG16(0x11F2BEUL)) {
    case 0x0000: REG8(0x114D5CUL) = REG8(0x11F2BAUL); break;
    case 0x0001: REG8(0x11A661UL) = REG8(0x11F2BBUL); break;
    case 0x0002:
        REG16(0x11A662UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
        break;
    case 0x0003:
        REG16(0x11A662UL) =
            (u16)(REG16(0x11A662UL) | (u16)REG8(0x11F2BBUL));
        REG8(0x11F2B6UL) = 0x04;
        break;
    default: break;
    }
}

/* Message 9. Bit 0 of H'114D51 raised when the sub-code is not zero. */
static void rx_msg_09(void)
{
    if (REG16(0x11F2BEUL) != 0x0000) return;

    if (REG8(0x11F2BAUL) != 0) REG8(0x114D51UL) |= 0x01;
    REG8(0x11F2B6UL) = 0x04;
}

/* Message 11. Two words, each a byte at a time. */
static void rx_msg_0B(void)
{
    switch (REG16(0x11F2BEUL)) {
    case 0x0000: break;
    case 0x0001:
        REG16(0x11A636UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
        break;
    case 0x0002:
        REG16(0x11A636UL) =
            (u16)(REG16(0x11A636UL) | (u16)REG8(0x11F2BBUL));
        break;
    case 0x0003:
        REG16(0x11A638UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
        break;
    case 0x0004:
        REG16(0x11A638UL) =
            (u16)(REG16(0x11A638UL) | (u16)REG8(0x11F2BBUL));
        REG8(0x11F2B6UL) = 0x04;
        break;
    default: break;
    }
}

/* Message 12. H'15 bytes into H'104043, with the first two named. */
static void rx_msg_0C(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0015;
    } else if (i == 0x0001) {
        REG8(0x00104044UL) = REG8(0x11F2BBUL);
    } else if (i == 0x0002) {
        REG8(0x00104045UL) = REG8(0x11F2BBUL);
    } else {
        const u32 at = 0x00104043UL + (u32)(u16)i;

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 2) == REG16(0x11F2C0UL)) REG8(0x11F2B6UL) = 0x04;
    }
}

/* Message 13. H'14 bytes into H'104C79. */
static void rx_msg_0D(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) REG16(0x11F2C0UL) = 0x0014;
    else             rx_store(0x00104C79UL + i);
}

/* Message 14. One byte, and the exchange ends there: the line is given back
 * and, if something was waiting on it, H'114D6C is answered. Returns 0 to
 * say that nothing else is to be done with this byte. */
static int rx_msg_0E(void)
{
    if (REG16(0x11F2BEUL) != 0x0000) return 1;

    REG8(0x00104C8EUL) = REG8(0x11F2BAUL);
    REG16(0x11F2BEUL) = 0x0000;
    REG8(0x11F2BCUL) = 0x00;
    REG8(0x11F2B6UL) = 0x00;
    REG8(0xFFFF60UL) &= (u8)~0x02;
    REG16(0xFFFF72UL) = 0x0000;
    if (REG8(0x114D6CUL) != 0) REG8(0x114D6CUL) = 0xBB;
    return 0;
}

/* ---- the receive interrupt itself -------------------------------------- */
void isr_sci0_rxi_body(void)
{
    u8 msg;

    REG8(0x11F2BBUL) = RDR0;
    SSR0 &= (u8)~0x40;
    REG16(0xFFFF72UL) = 0x0000;

    if (REG8(0x11F2B6UL) != 0x01) {
        if (REG8(0x11F2B6UL) != 0x00) {
            /* States 2 and 4 are the checksum; anything else is ignored. */
            if (REG8(0x11F2B6UL) == 0x02) {
                REG8(0x11F2B6UL) = 0x01;
                REG16(0x11F2BEUL) = 0x0000;
                REG8(0x11F29EUL) = 0x08;
                if (REG8(0x11F2BBUL) != REG8(0x11F2BCUL)) {
                    REG8(0x11F2B3UL) = 0xFF;
                    REG16(0x114D4CUL) =
                        (u16)(REG16(0x114D4CUL) | 0x0008);
                } else {
                    REG8(0x11F2B3UL) = 0xAA;
                    REG8(0x11F2B7UL) = (u8)(REG8(0x11F2B7UL) + 1);
                }
                link_send_start();
                REG8(0x11F2BCUL) = 0x00;
            } else if (REG8(0x11F2B6UL) == 0x04) {
                REG16(0x11F2BEUL) = 0x0000;
                REG8(0x11F29EUL) = 0x08;
                if (REG8(0x11F2BBUL) != REG8(0x11F2BCUL)) {
                    REG8(0x11F2B3UL) = 0xFF;
                    REG8(0x11F2B6UL) = 0x01;
                    REG16(0x114D4CUL) =
                        (u16)(REG16(0x114D4CUL) | 0x0008);
                } else {
                    REG8(0x11F2B3UL) = 0xAA;
                    rx_frame_done();
                    REG8(0x11F2B6UL) = 0x00;
                }
                link_send_start();
                REG8(0x11F2BCUL) = 0x00;
            }
            return;
        }

        /* State 0: the first byte of a frame. */
        REG8(0x11F2B6UL) = 0x01;

        if (REG8(0x11F29EUL) != 0x04) {
            const u8 b = REG8(0x11F2BBUL);

            if (b == 0xFF || b == 0x00 || b == 0xAA) {
                REG8(0x11F2B6UL) = 0x00;
                return;
            }
        }

        if (REG8(0x11F29EUL) == 0x04) {
            /* The answer to something we sent. Counting the one bits is how
             * a line with nothing on it is told from a real reply: between
             * three and five of them is a byte, anything else is noise. */
            u8 ones = 0, i;

            REG8(0xFFFF60UL) &= (u8)~0x02;
            REG16(0xFFFF72UL) = 0x0000;

            for (i = 0; i < 0x08; i++) {
                const u8 v = REG8(0x11F2BBUL);

                if (v & 0x01) ones++;
                REG8(0x11F2BBUL) = (u8)(v >> 1);
            }

            REG8(0x11F29EUL) =
                (u8)((ones <= 0x05 && ones >= 0x03) ? 0x06 : 0x05);
            link_send_start();
            REG8(0x11F2B6UL) = 0x00;
            return;
        }

        if (REG8(0x11F29EUL) != 0x00) {
            /* Something arrived in the middle of our own message. */
            link_send_stop();
            link_delay(0x000A);
            link_send_start();
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0010);
            REG8(0x11F2B6UL) = 0x00;
            return;
        }

        /* A fresh frame: unpack the message and its sub-code. */
        REG8(0x11F2B7UL) = 0x00;
        REG8(0x11F2B8UL) = 0x00;
        REG8(0x11F2BCUL) = 0x00;
        REG16(0x11F2BEUL) = 0x0000;
        REG8(0x11F2CAUL) = 0x00;
        REG8(0x11F2B9UL) = (u8)(REG8(0x11F2BBUL) >> 4);
        REG8(0x11F2BAUL) = (u8)(REG8(0x11F2BBUL) & 0x0F);
        REG8(0x11F2B6UL) = 0x01;
        REG8(0xFFFF60UL) |= 0x02;
    }

    msg = REG8(0x11F2B9UL);
    if ((u8)(msg - 1) <= 0x0E) {
        switch (msg) {
        case 0x01: rx_msg_01(); break;
        case 0x02: rx_msg_02(); break;
        case 0x03: rx_msg_03(); break;
        case 0x04: rx_msg_04(); break;
        case 0x05: rx_msg_05(); break;
        case 0x06: rx_msg_04(); break;   /* H'2340D8, the same code */
        case 0x07: rx_msg_07(); break;
        case 0x08: rx_msg_08(); break;
        case 0x09: rx_msg_09(); break;
        case 0x0A: break;
        case 0x0B: rx_msg_0B(); break;
        case 0x0C: rx_msg_0C(); break;
        case 0x0D: rx_msg_0D(); break;
        case 0x0E: if (!rx_msg_0E()) return; break;
        case 0x0F: rx_msg_0F(); break;
        default: break;
        }
    }

    rx_byte_done();
}
