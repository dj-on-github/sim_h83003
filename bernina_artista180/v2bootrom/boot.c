/* Bernina artista 180 boot ROM, rewritten in C.
 *
 * Reconstructed from the machine's own ROM at H'000000-H'002FFF. What the
 * boot ROM does is narrow: bring the bus and both serial channels up, greet
 * the serial port, decide whether the application flash holds a program, and
 * either hand over to it or sit in a loop waiting for one.
 *
 * The application never calls into here -- checked against every decoded
 * call in the application flash, and there are none -- so the only contract
 * to preserve is the vector table, the trampolines' use of the handler table
 * at H'200000, and the hardware state the application inherits.
 */

#include "h8_3003.h"

void serial_rx_error_nak(void);
int  serial_rx_error_check(void);
void serial_service(void);
static void download_loop(void);

/* ---- timing ------------------------------------------------------------
 * The original spins a fixed inner count rather than using a timer, so the
 * delay is in units of "however long H'144 iterations take". Reproduced as
 * counted loops for the same reason: nothing here can afford to wait on a
 * peripheral that is not up yet.
 */
/* The count the ROM holds. Its inner loop body is four instructions
 * (compare, branch, increment, branch); the two-instruction loop GCC
 * generates here would spin through it in about half the time, so the count
 * is scaled to spend the same time waiting. The scale is not a guess: with
 * it, 'J' emits the same number of announcements as the original ROM does in
 * the same window, and the boot screen comes up identically. */
#define ORIGINAL_INNER_COUNT 0x144
#define INNER_SCALE 2
#define INNER_COUNT (ORIGINAL_INNER_COUNT * INNER_SCALE)

void delay(u16 units)
{
    u16 i, j;

    for (i = 0; i < units; i++)
        for (j = 0; j < INNER_COUNT; j++)
            /* An empty barrier, not a volatile counter: it stops the loop
             * being optimised away while leaving the count in a register,
             * which is what makes one iteration cost the same handful of
             * instructions it costs in the original. */
            __asm__ volatile ("");
}

/* ---- serial ------------------------------------------------------------
 * A note on clearing status flags. The SCI clears a flag when software
 * writes a 0 to it; writing a 1 is ignored. So a flag is cleared by writing
 * the complement of its mask as a plain constant -- never by reading SSR,
 * masking and writing it back.
 *
 * The read-modify-write is a real bug, not a style point. Between the read
 * and the write-back a character can finish arriving and set RDRF; the
 * write-back then stores the stale 0 over it and the byte is gone. The
 * original avoids this by using BCLR, a single instruction; the same C
 * written as "SSR &= ~mask" spreads over several and loses bytes under
 * continuous traffic.

 * Every routine works on whichever channel CHAN_SELECTION picks, which is
 * how the boot ROM talks to either the PC port or the embroidery module
 * without duplicating the code.
 */
static int port0_selected(void)
{
    return (CHAN_SELECTION & CHAN_SEL_PORT0) != 0;
}

/* H'00040A and H'00040E: the two halves of the interrupt mask, which the
 * original keeps as separate two-instruction entry points in the block just
 * after the reset vector. */
static inline void disable_interrupts(void)
{
    __asm__ volatile ("orc #0x80,ccr" ::: "cc");
}

static inline void enable_interrupts(void)
{
    __asm__ volatile ("andc #0x7f,ccr" ::: "cc");
}

void send_serial_data_byte(u8 c)
{
    if (port0_selected()) {
        while (!(SSR0 & SSR_TDRE)) { }
        TDR0 = c;
        SSR0 = (u8)~SSR_TDRE;
    } else {
        while (!(SSR1 & SSR_TDRE)) { }
        TDR1 = c;
        SSR1 = (u8)~SSR_TDRE;
    }
}

u8 read_serial_data_byte(void)
{
    u8 c;
    if (port0_selected()) {
        c = RDR0;
        SSR0 = (u8)~SSR_RDRF;
    } else {
        c = RDR1;
        SSR1 = (u8)~SSR_RDRF;
    }
    return c;
}

u8 get_sci_rx_ready_bit(void)
{
    return port0_selected() ? (u8)(SSR0 & SSR_RDRF) : (u8)(SSR1 & SSR_RDRF);
}

u8 get_sci_tx_ready_bit(void)
{
    return port0_selected() ? (u8)(SSR0 & SSR_TDRE) : (u8)(SSR1 & SSR_TDRE);
}

/* Returns 1 when the line is clean. On an error it drains the byte that
 * caused it, clears the three error flags and reports 0 -- the read is there
 * to clear the condition, not to deliver data. */
int serial_clear_rx_errors(void)
{
    if (port0_selected()) {
        if (!(SSR0 & SSR_RX_ERRORS)) return 1;
        (void)read_serial_data_byte();
        SSR0 = (u8)~SSR_RX_ERRORS;
    } else {
        if (!(SSR1 & SSR_RX_ERRORS)) return 1;
        (void)read_serial_data_byte();
        SSR1 = (u8)~SSR_RX_ERRORS;
    }
    BOOT_STATE_1E = 0;
    return 0;
}

/* Blocks until a byte arrives, servicing line errors while it waits. */
u8 wait_read_serial_byte(void)
{
    while (!get_sci_rx_ready_bit()) {
        serial_rx_error_nak();
    }
    return read_serial_data_byte();
}

void puts_serial(const char *s)
{
    while (*s) {
        send_serial_data_byte((u8)*s);
        s++;
    }
}

/* ---- bringing the machine up ------------------------------------------ */

static void bus_init(void)
{
    ABWCR = 0xFF;   /* every area 8 bits wide */
    BRCR  = 0x00;
    ASTCR = 0xFF;   /* three-state access everywhere */
    WCER  = 0x00;
    WCR   = 0xF1;
    P8DDR = 0x1E;
}

static void sci_init_channel(int port0)
{
    if (port0) {
        SCR0 = 0;
        SMR0 = 0;          /* async, 8N1, phi/1 */
        BRR0 = 0x11;
    } else {
        SCR1 = 0;
        SMR1 = 0;
        BRR1 = 0x11;
    }
    delay(1);              /* one bit time must elapse before enabling */
    if (port0) SCR0 = SCR_TE | SCR_RE;
    else       SCR1 = SCR_TE | SCR_RE;
}

/* H'0006A0. Reprograms the bit rate of whichever channel is currently
 * selected, shutting the receiver and transmitter down across the change.
 * The settling delays either side of it are the original's.
 */
static void sci_set_bitrate(u8 brr)
{
    delay(10);
    if (CHAN_SELECTION & CHAN_SCI0) {
        SCR0 = 0;
        SMR0 = 0;
        BRR0 = brr;
        delay(1);
        SCR0 = SCR_TE | SCR_RE;
    } else {
        SCR1 = 0;
        SMR1 = 0;
        BRR1 = brr;
        delay(1);
        SCR1 = SCR_TE | SCR_RE;
    }
    delay(10);
}

static void hardware_init(void)
{
    BOOT_PTR_10 = 0xFFFE10UL;
    bus_init();
    /* Only SCI1. V3 follows this with sci_init_channel(1) to bring SCI0 up
     * as well; V2 does not have that call, and does not even carry the two
     * routines that would do it -- H'000718 and H'000734 in V3 are absent
     * here. SCI0 is therefore left at its reset state, receiver and
     * transmitter both disabled, until something else enables it. */
    sci_init_channel(0);   /* SCI1 */
    BOOT_STATE_1E = 0;
    CHAN_SELECTION &= (u8)~CHAN_SCI0;
    CHAN_SELECTION &= (u8)~0x04;
}

/* The application's first longword is its entry point, stored big-endian, so
 * a valid 24-bit address always has H'00 in its top byte. The original tests
 * exactly that -- it is a sanity check on the entry pointer, not a test for
 * an erased flash, and it reads as TRUE when the byte is zero. Getting this
 * the wrong way round sends the machine into the download loop. */
static int app_entry_looks_valid(void)
{
    return REG8(APP_TABLE) == 0;
}

static void enter_application(void)
{
    void (*app)(void) = (void (*)(void))APP_ENTRY;
    app();
    for (;;) { }   /* the application does not come back */
}

/* The two characters a host sends to claim the link, at H'002378 in the
 * original ("EB", followed by an unused "XX"). */
static const u8 HANDSHAKE[2] = { 'E', 'B' };

/* H'000F3E. Offers the link to a host for 500 rounds. A host claims it by
 * sending "EB". Each matched character is echoed, and anything else draws a
 * 'Q' and restarts the match -- which is why a machine sitting in this loop
 * answers 'Q' to every stray byte.
 *
 * This is where V2 and V3 really part company. V3's handshake polls both
 * serial channels in turn: SCI0 silently, SCI1 with the echo, and whichever
 * channel completes the match is left selected in CHAN_SELECTION, so the
 * handshake is also what decides which port the download protocol then runs
 * on. V2 has only the SCI1 half. It never touches CHAN_SELECTION, never looks
 * at SCI0, and cannot be reached on it -- consistent with hardware_init()
 * leaving SCI0 disabled.
 *
 * V3's two "nothing waiting" arms each call serial_clear_rx_errors(), which
 * in the original is a routine of its own at H'0004EE with a channel switch
 * in it. V2 has no such call and no such routine: the loop simply goes round
 * again. A line error therefore sits in SSR1 until the next read clears it,
 * rather than being cleared once per round.
 *
 * A timeout returns 0 with SCI1 still selected, as it was on entry.
 */
static int host_handshake(void)
{
    u8 match = 0;    /* R4L: characters matched so far on SCI1 */
    u32 round;

    read_serial_data_byte();   /* one byte is drained and discarded */

    for (round = 0; round != 500; round++) {
        delay(1);

        if (get_sci_rx_ready_bit()) {
            if (read_serial_data_byte() == HANDSHAKE[match]) {
                send_serial_data_byte(HANDSHAKE[match]);
                if (++match == 2) return 1;
            } else {
                send_serial_data_byte('Q');
                match = 0;
            }
        }
    }
    return 0;
}

/* H'00049A in the original. Returns 1 when the line is clean. On an error it
 * drains the offending byte, clears the three error flags, transmits H'21 --
 * a NAK -- resets the protocol state and returns 0. */
int serial_rx_error_check(void)
{
    if (serial_clear_rx_errors()) return 1;
    send_serial_data_byte(0x21);
    BOOT_STATE_1E = 0;
    return 0;
}

void serial_rx_error_nak(void)
{
    (void)serial_rx_error_check();
}

/* ---- routines the application reaches through the vector table ---------
 * The application calls into the boot ROM with JSR @@aa:8, taking the target
 * from a low vector slot. Only slot 1 is used while the machine runs -- some
 * 14,750 calls, all from the thunk at H'250AE8 -- but the others are part of
 * the same published set and are kept.
 */

/* H'001090. Called round the application's main loop to service the host
 * link. Both guards must pass before any command runs: the transmitter has
 * to be free to answer, and the line has to be error-free.
 *
 * The original then dispatches on the protocol state in H'FFFD1E through a
 * 240-entry table over about 120 handlers at H'0014AE-H'002031 -- most of
 * the remaining boot ROM, and the firmware download protocol. None of it is
 * reconstructed. With nothing connected the state stays 0 and the handlers
 * would have nothing to do, so returning here leaves the machine in the same
 * place the original leaves it when idle.
 */
/* Protocol states, named as they are reached. The dispatcher in the original
 * indexes a 247-entry table at H'0010D2; states with no entry of their own
 * fall to H'00201E, which resets to H'13. */
#define ST_IDLE       0x00 /* H'0014AE -- read a command letter */
#define ST_READ_ADDR  0x01 /* H'001658 -- states 01-06, address digits */
#define ST_READ_LAST  0x06
#define ST_READ_HI    0x07 /* H'00168E -- emit the high nibble of the byte */
#define ST_READ_LO    0x08 /* H'0016D6 -- emit the low nibble, then step on */
#define ST_WRITE_ADDR 0x09 /* H'001742 -- states 09-0E, same but for 'w' */
#define ST_WRITE_LAST 0x0E
#define ST_WRITE_DATA 0x0F /* H'001778 -- states 0F-10, the two data digits */
#define ST_WRITE_LO   0x10
#define ST_WRITE_PUT  0x11 /* H'0017A2 -- store the byte and read it back */
#define ST_ACK_O      0x12 /* H'001800 -- 'O', then idle */
#define ST_ACK_N      0x13 /* H'001812 -- 'N', then idle */
#define ST_ACK_V      0x14 /* H'001824 -- 'V', then idle */
#define ST_RESET      0x3C /* H'0018D0 -- re-enter the boot ROM from the top */
#define ST_GO         0xA0 /* H'001F8C -- hand over to the application */
#define ST_HALT       0xE6 /* H'001F9C -- stop, but keep serving the link */
#define ST_CONFIRM    0xB4 /* H'001CAC -- wait for the host's confirmation */
#define ST_VER_HI     0x32 /* H'00189C -- report the version, high nibble */
#define ST_VER_LO     0x33 /* H'0018BA -- and the low nibble */
#define ST_BAUD_DIGIT 0x28 /* H'001836 -- states 28-29, the new rate divisor */
#define ST_BAUD_LO    0x29
#define ST_BAUD_APPLY 0x2A /* H'001860 -- switch rate and re-establish contact */
#define ST_TO_SCI1_R  0xBE /* H'001CE4 -- "TrME", one character per state */
#define ST_TO_SCI1_M  0xBF /* H'001D12 */
#define ST_TO_SCI1_E  0xC0 /* H'001D40 */
#define ST_DUMP_ADDR  0xF0 /* H'001FAE -- states F0-F5, the address digits */
#define ST_DUMP_LAST  0xF5
#define ST_DUMP_SEND  0xF6 /* H'001FE2 -- 256 raw bytes in one pass */
#define ST_FLASH_ADDR 0x79 /* H'001AAE -- states 79-7E, the address digits */
#define ST_FLASH_LAST 0x7E
#define ST_FLASH_DATA 0x7F /* H'001AE4 -- states 7F-80, the data digits */
#define ST_FLASH_LO   0x80
#define ST_FLASH_PUT  0x81 /* H'001B0E -- read, patch and reprogram the page */
#define ST_DL_KIND    0x5A /* H'001920 -- 'B' for a bank or 'S' for a sector */
#define ST_DL_BANK_ADDR 0x64 /* H'001962 -- states 64-65, the bank number */
#define ST_DL_BANK_LAST 0x65
#define ST_DL_BANK_GO   0x66 /* H'001998 */
#define ST_DL_SECT_ADDR 0x6E /* H'001A1C -- states 6E-71, the sector address */
#define ST_DL_SECT_LAST 0x71
#define ST_DL_SECT_GO   0x72 /* H'001A52 */
#define ST_MOD_ADDR   0x8C /* H'001B84 -- states 8C-91, the address digits */
#define ST_MOD_LAST   0x91
#define ST_MOD_LOAD   0x92 /* H'001BBA -- read the page in, ready to edit */
#define ST_MOD_DATA   0x93 /* H'001BFA -- states 93-94, one byte as two digits */
#define ST_MOD_LO     0x94
#define ST_MOD_PUT    0x95 /* H'001C24 -- store it and step on */
#define ST_MOD_DONE   0x96 /* H'001C8A -- program the part-filled page */
#define ST_SUM_ADDR   0xC8 /* H'001D80 -- states C8-CD, the start address */
#define ST_SUM_LAST   0xCD
#define ST_SUM_STASH  0xCE /* H'001DB6 -- put it aside, reset for the count */
#define ST_SUM_COUNT  0xCF /* H'001DE0 -- states CF-D4, how many bytes */
#define ST_SUM_CLAST  0xD4
#define ST_SUM_GO     0xD5 /* H'001E16 -- add them up */
#define ST_SUM_NIB    0xD6 /* H'001E7A.. -- eight nibbles, most significant first */
#define ST_SUM_NIB_LAST 0xDD

/* The rate divisor the boot ROM starts at, and falls back to when a host
 * fails to follow it to a new one. */
#define BRR_DEFAULT         0x11

/* H'002267 in the original: the machine announcing itself. */
#define BOOT_STRING         "BOS"

/* The byte at H'002266 in the original, sitting immediately before the "BOS"
 * boot string. H'0B is 11, which agrees with the "BiosVersion: 1.10" line in
 * the identify banner. V3 has H'0C here and reads 1.20, so this byte and the
 * banner move together and the 'V' command really does distinguish the two
 * ROMs. */
#define BIOS_VERSION        0x0B

/* Bit 2 of CHAN_SELECTION. Unlike bit 1 this is not a channel select but a
 * sticky flag: 'Y' sets it once the host has confirmed, the routine at
 * H'00212C clears it again, and the boot ROM exposes a read accessor for it
 * at H'002328 so the application can test it. What it authorises is not yet
 * established. */
#define CHAN_CONFIRMED      0x04

#define ST_IDENTIFY   0x50 /* H'0018F6 -- send the identification banner */
#define ST_UNKNOWN    0x13

/* The letter being processed, R4H in the original. The shared tail echoes it,
 * which is why every command's reply starts with the command itself. */
static u8 cmd_letter;

/* H'00163E: echo the command and clear the operand registers. Every command
 * handler ends here. */
static void protocol_tail(void)
{
    send_serial_data_byte(cmd_letter);
    BOOT_ADDR_ACC = 0;
    BOOT_DATA_20 = 0;
}

/* H'0005F0, and H'0005AA which is the same routine again with the escape
 * state fixed at idle. Takes one ASCII hex digit, echoes it, and returns its
 * value.
 *
 * It also advances the protocol state, which is how a run of digit states
 * walks from one to the next without the dispatcher counting anything. A
 * character that is not a hex digit ends the run: a '?' is echoed in place
 * of the digit and the machine goes to [escape]. For most commands that is
 * idle, abandoning the command; 'M' points it at its own finishing state
 * instead, so any stray character means "that is the last byte".
 *
 * The value returned on that path is the raw character, as in the original.
 * It is discarded, since the accumulator is about to be reset or ignored.
 */
static u8 hex_digit_or(u8 escape)
{
    u8 c, echo, value;

    BOOT_STATE_1E++;
    c = read_serial_data_byte();
    echo = c;
    value = c;
    if (c >= '0' && c <= '9') {
        value = (u8)(c - '0');
    } else if (c >= 'A' && c <= 'F') {
        value = (u8)(c - ('A' - 10));
    } else {
        BOOT_STATE_1E = escape;
        echo = '?';
    }
    send_serial_data_byte(echo);
    return value;
}

static u8 hex_digit(void)
{
    return hex_digit_or(ST_IDLE);
}

/* H'001658, and H'001742 which is the same code again. Shifts one hex digit
 * into the address accumulator; six of them make a 24-bit address. */
static void state_addr_digit(void)
{
    u32 acc;

    if (!get_sci_rx_ready_bit()) return;
    acc = BOOT_ADDR_ACC << 4;
    acc += hex_digit();
    BOOT_ADDR_ACC = acc;
}

/* H'000640. Sends the low four bits of a byte as one ASCII hex character. */
static void send_hex_nibble(u8 v)
{
    v &= 0x0F;
    send_serial_data_byte((u8)(v <= 9 ? v + '0' : v + ('A' - 10)));
}

/* The byte currently addressed by the accumulator. H'204000-H'207FFF is
 * walled off and always reads back as H'FF, in both the read and the
 * read-after-write path. */
static u8 target_byte(void)
{
    u32 addr = BOOT_ADDR_ACC;

    if (addr >= 0x204000 && addr < 0x208000) return 0xFF;
    return *(volatile u8 *)addr;
}

/* H'00168E: state 07. High nibble out. */
static void state_read_hi(void)
{
    BOOT_DATA_20 = target_byte();
    send_hex_nibble((u8)(BOOT_DATA_20 >> 4));
    BOOT_STATE_1E = ST_READ_LO;
}

/* H'0016D6: state 08. Low nibble out, then step to the next byte. The count
 * in BOOT_STATE_1F is how many bytes the command asked for; when it runs out
 * the exchange is acknowledged. */
static void state_read_lo(void)
{
    BOOT_DATA_20 = target_byte();
    send_hex_nibble(BOOT_DATA_20);
    BOOT_ADDR_ACC++;
    BOOT_STATE_1F--;
    BOOT_STATE_1E = BOOT_STATE_1F == 0 ? ST_ACK_O : ST_READ_HI;
}

/* H'001778: states 0F-10. Shifts two hex digits into the byte to be written. */
static void state_write_digit(void)
{
    u8 acc;

    if (!get_sci_rx_ready_bit()) return;
    acc = (u8)(BOOT_DATA_20 << 4);
    acc += hex_digit();
    BOOT_DATA_20 = acc;
}

/* H'0017A2: state 11. Stores the byte and reads it straight back, so a write
 * to ROM or to a wall reports failure rather than passing silently. A count
 * of H'FF means the host is streaming: stay in the data states and take
 * another byte instead of acknowledging.
 */
static void state_write_put(void)
{
    u8 data = (u8)BOOT_DATA_20;

    BOOT_STATE_1E = ST_ACK_N;
    *(volatile u8 *)BOOT_ADDR_ACC = data;
    if (target_byte() == data) BOOT_STATE_1E = ST_ACK_O;

    if (BOOT_STATE_1F == 0xFF) {
        BOOT_ADDR_ACC++;
        BOOT_DATA_20 = 0;
        BOOT_STATE_1E = ST_WRITE_DATA;
    }
}

/* H'001800, H'001812, H'001824. The three single-character acknowledgements,
 * shared by every command that reports an outcome. */
static void state_ack_o(void) { send_serial_data_byte('O'); BOOT_STATE_1E = ST_IDLE; }
static void state_ack_n(void) { send_serial_data_byte('N'); BOOT_STATE_1E = ST_IDLE; }
static void state_ack_v(void) { send_serial_data_byte('V'); BOOT_STATE_1E = ST_IDLE; }

/* H'001558 ('r') and H'001544 ('R'). Six address digits, then a read of
 * either one byte or a H'20-byte block. The two differ only in the count. */
static void cmd_read(u8 count)
{
    BOOT_STATE_1E = ST_READ_ADDR;
    BOOT_STATE_1F = count;
    protocol_tail();
}

/* H'00156A ('w') and H'00157E ('W'). The same on the write path. A count of
 * H'FF is streaming mode: the host keeps feeding data bytes, the address
 * walks on by itself, and nothing is acknowledged until the host stops. */
static void cmd_write(u8 count)
{
    BOOT_STATE_1E = ST_WRITE_ADDR;
    BOOT_STATE_1F = count;
    protocol_tail();
}

/* The reset entry in vectors.S, which reloads the stack pointer and runs the
 * boot sequence again -- the original's H'000400. */
extern void start(void);

/* H'0018D0: state 3C, reached from 'X'. Masks interrupts, waits long enough
 * for the echoed letter to clear the transmitter, and re-enters the boot ROM
 * from the top. Does not return, and unlike 'G' it does not bother clearing
 * the state first: the boot sequence reinitialises it anyway.
 */
static void state_reset(void)
{
    disable_interrupts();
    delay(10);
    start();
}

/* H'001F9C: state E6, reached from 'H'. Stops the machine. The state is
 * cleared, interrupts are masked, and from then on the boot ROM does nothing
 * but call the serial service by hand, forever. The download protocol stays
 * alive, so this halts everything except the link. Does not return.
 */
static void state_halt(void)
{
    BOOT_STATE_1E = ST_IDLE;
    disable_interrupts();
    for (;;) serial_service();
}

/* ---- flash -------------------------------------------------------------
 * The program memory is JEDEC-style flash, driven by the usual unlock
 * sequence: H'AA to base+H'5555, H'55 to base+H'2AAA, then a command byte
 * back to base+H'5555, where base is the 64K bank the address falls in.
 *
 * Flash cannot be rewritten a byte at a time in place, so a single-byte
 * change means reading the whole 256-byte page into RAM, patching it and
 * programming the page back. Both copies go through the on-chip data
 * transfer controller rather than the CPU.
 */

#define FLASH_UNLOCK_A      0x5555
#define FLASH_UNLOCK_B      0x2AAA
#define FLASH_CMD_ID        0x90    /* autoselect: manufacturer and device */
#define FLASH_CMD_PROGRAM   0xA0    /* program the bytes that follow */
#define FLASH_CMD_RESET     0xF0    /* back to ordinary read mode */

#define FLASH_PAGE_SIZE     0x100
#define FLASH_BANK_MASK     0x00FF0000UL
#define FLASH_PAGE_MASK     0x00FFFF00UL

/* The identify codes. Only ATMEL_A4 is accepted for writing; the rest are
 * recognised but not programmed by this ROM. NOT_FLASH means the bank did
 * not respond to the autoselect command at all, so it is ordinary memory. */
#define FLASH_NONE          0x00
#define FLASH_ATMEL_A4      0x01
#define FLASH_NOT_FLASH     0x0A

#define FLASH_BUSY_TRIES    0x11
#define ERASE_DQ7_TRIES     0x3A98
#define ERASE_TOGGLE_TRIES  0x2AF8

/* Every access to the array is volatile, and that is load-bearing rather
 * than decorative. The unlock sequence writes H'AA and then the command byte
 * to the same address; without volatile the compiler sees a dead store and
 * drops the H'AA, so the device never leaves read mode and every identify,
 * erase and program silently does nothing. The same applies to the ID reads
 * either side of the autoselect command, and to the page buffer, which the
 * data transfer controller fills behind the compiler's back.
 */
static volatile u8 *flash_cell(u32 base, u32 offset)
{
    return (volatile u8 *)(base + offset);
}

/* H'00099A, and the head of H'000782. */
static void flash_command(u32 addr, u8 cmd)
{
    u32 base = addr & FLASH_BANK_MASK;

    *flash_cell(base, FLASH_UNLOCK_A) = 0xAA;
    *flash_cell(base, FLASH_UNLOCK_B) = 0x55;
    *flash_cell(base, FLASH_UNLOCK_A) = cmd;
}

/* H'00073E. Moves a block with the data transfer controller, waiting first
 * for any transfer already in progress to finish. */
static void dma_copy(u32 src, u32 dst, u16 count)
{
    while (DTCR0A & DTE) { }

    MAR0A = src;
    MAR0B = dst;
    ETCR0A = count;
    DTCR0B = 0x10;
    DTCR0A = 0x16;
    DTCR0B &= (u8)~DTE;
    DTCR0B |= DTE;
    DTCR0A &= (u8)~DTE;
    DTCR0A |= DTE;
}

/* H'000782. Identifies the device in the bank an address falls in, then puts
 * it back into read mode.
 *
 * The four bytes the unlock sequence is about to disturb are read first. If
 * the manufacturer and device bytes come back unchanged after the autoselect
 * command, nothing decoded it -- the bank is RAM, not flash -- and the two
 * unlock cells are restored, since the AA and 55 really did land there.
 *
 * The manufacturer tests below fall through into the device tests that
 * follow them, exactly as in the original. That is why codes 6 and 7 can
 * never be returned: the same device IDs are tested again immediately
 * afterwards and overwrite them with 8 and 9. Reproduced as it stands.
 */
static u8 flash_identify(u32 addr)
{
    u32 base = addr & FLASH_BANK_MASK;
    volatile u8 *dev = flash_cell(base, 0);
    u8 saved_a, saved_b, saved_mfr, saved_dev;
    u8 mfr, id, code = FLASH_NONE;

    saved_a   = *flash_cell(base, FLASH_UNLOCK_A);
    saved_b   = *flash_cell(base, FLASH_UNLOCK_B);
    saved_mfr = dev[0];
    saved_dev = dev[1];

    flash_command(base, FLASH_CMD_ID);
    delay(11);

    mfr = dev[0];
    if (mfr == saved_mfr && dev[1] == saved_dev) {
        code = FLASH_NOT_FLASH;
    } else if (mfr != 0x33) {
        if (mfr != 0x01) {
            if (mfr != 0x04) {
                if (mfr != 0x1F) goto restore;
                id = dev[1];
                if      (id == 0xA4) code = FLASH_ATMEL_A4;
                else if (id == 0x13) code = 0x02;
                else if (id == 0xDA) code = 0x03;
                else if (id == 0xD5) code = 0x04;
            }
            id = dev[1];
            if      (id == 0xAD) code = 0x06;
            else if (id == 0xD5) code = 0x07;
        }
        id = dev[1];
        if      (id == 0xAD) code = 0x08;
        else if (id == 0xD5) code = 0x09;
    }

restore:
    flash_command(base, FLASH_CMD_RESET);
    if (code == FLASH_NOT_FLASH) {
        *flash_cell(base, FLASH_UNLOCK_A) = saved_a;
        *flash_cell(base, FLASH_UNLOCK_B) = saved_b;
    }
    delay(11);
    return code;
}

/* H'0008A2. Reads the page an address falls in into the RAM buffer. */
static void flash_page_load(u32 addr)
{
    dma_copy(addr & FLASH_PAGE_MASK, FLASH_PAGE_BUF, FLASH_PAGE_SIZE);
}

/* H'000A1E, and the same loop again at H'0009D2 with a far longer limit.
 * The DQ6 toggle-bit poll: while an operation is in progress the device
 * flips bit 6 on every read, so two reads in a row that agree mean it has
 * finished. Every poll is bounded, so a dead or absent device cannot wedge
 * the boot ROM.
 */
static int flash_poll_toggle(u32 addr, u16 limit)
{
    volatile u8 *p = (volatile u8 *)addr;
    u16 tries = 0;

    while ((p[0] & 0x40) != (p[0] & 0x40)) {
        if (tries++ >= limit) break;
        delay(1);
    }
    return tries < limit;
}

/* H'0008D8. The other convention: the AMD parts hold DQ7 low until the
 * erase finishes rather than toggling DQ6. */
static int flash_poll_dq7(u32 addr, u16 limit)
{
    volatile u8 *p = (volatile u8 *)addr;
    u16 tries = 0;

    while (!(p[0] & 0x80)) {
        if (tries++ >= limit) break;
        delay(1);
    }
    return tries < limit;
}

static int flash_wait(u32 addr)
{
    return flash_poll_toggle(addr, FLASH_BUSY_TRIES);
}

/* H'00091A. Erase the sector an address falls in: unlock, H'80, unlock
 * again, then H'30 to the sector. */
static int flash_erase_sector(u32 addr)
{
    u32 base = addr & FLASH_BANK_MASK;

    *flash_cell(base, FLASH_UNLOCK_A) = 0xAA;
    *flash_cell(base, FLASH_UNLOCK_B) = 0x55;
    *flash_cell(base, FLASH_UNLOCK_A) = 0x80;
    *flash_cell(base, FLASH_UNLOCK_A) = 0xAA;
    *flash_cell(base, FLASH_UNLOCK_B) = 0x55;
    *flash_cell(base, 0) = 0x30;
    delay(1);
    return flash_poll_dq7(base, ERASE_DQ7_TRIES);
}

/* H'000B74. Erase the whole chip: unlock, H'80, then H'10. Note this one
 * takes the address as given rather than masking it to the bank. */
static int flash_erase_chip(u32 addr)
{
    *(volatile u8 *)(addr + FLASH_UNLOCK_A) = 0xAA;
    *(volatile u8 *)(addr + FLASH_UNLOCK_B) = 0x55;
    *(volatile u8 *)(addr + FLASH_UNLOCK_A) = 0x80;
    *(volatile u8 *)(addr + FLASH_UNLOCK_B) = 0x55;
    *(volatile u8 *)(addr + FLASH_UNLOCK_A) = 0x10;
    return flash_poll_toggle(addr, ERASE_TOGGLE_TRIES);
}

/* H'000AA0. Programs the RAM buffer back over the page. */
static int flash_page_program(u32 addr)
{
    flash_command(addr, FLASH_CMD_PROGRAM);
    dma_copy(FLASH_PAGE_BUF, addr & FLASH_PAGE_MASK, FLASH_PAGE_SIZE);
    return flash_wait(addr);
}

/* H'001FE2: state F6, reached from 'N' once the address is in. Sends 256
 * bytes starting there as raw binary -- not as hex, which is what makes this
 * different from 'R' -- and then acknowledges.
 *
 * The whole block goes out in a single pass rather than a byte per pass, and
 * the loop ends when the byte counter wraps. Note there is no H'204000 guard
 * here, unlike the 'r' path: this reads straight through.
 */
static void state_dump_block(void)
{
    u8 i;

    BOOT_DATA_20 = 0;
    do {
        i = (u8)BOOT_DATA_20;
        send_serial_data_byte(*(volatile u8 *)(BOOT_ADDR_ACC + i));
        i++;
        BOOT_DATA_20 = i;
    } while (i != 0);

    BOOT_STATE_1E = ST_ACK_O;
}

/* ---- bulk download -----------------------------------------------------
 * 'P' is the loader the whole protocol exists for. It comes in two forms:
 * 'PB' takes a two-digit bank number and streams as many pages as the host
 * cares to send, 'PS' takes a four-digit sector address and takes exactly
 * one page.
 *
 * The wire protocol is the same throughout. The machine sends 'O' to say it
 * is ready and 'E' to ask for a page; the host answers 'Y' and 256 raw bytes,
 * or anything else to stop, which is acknowledged with 'N'. Each page is
 * reported with 'O', or 'V' if the programming did not verify.
 *
 * Which routine does the programming depends on what answered the identify,
 * because the parts differ in how they are written: the Atmel A4 takes a
 * whole page at once through the DMA, the AMD and Fujitsu parts want a fresh
 * unlock before every byte, and a bank that is not flash is simply filled.
 */

#define DL_READY    'O'
#define DL_REQUEST  'E'
#define DL_YES      'Y'
#define DL_STOP     'N'
#define DL_FAILED   'V'

static void recv_page(u32 dst)
{
    u16 i;

    for (i = 0; i < FLASH_PAGE_SIZE; i++)
        *(volatile u8 *)(dst + i) = wait_read_serial_byte();
}

/* H'000E2C. Atmel A4, a single page. */
static void download_page_a4(u32 addr)
{
    send_serial_data_byte(DL_READY);
    send_serial_data_byte(DL_REQUEST);
    recv_page(FLASH_PAGE_BUF);

    flash_command(addr, FLASH_CMD_PROGRAM);
    dma_copy(FLASH_PAGE_BUF, addr, FLASH_PAGE_SIZE);
    send_serial_data_byte(flash_wait(addr) ? DL_READY : DL_FAILED);
}

/* H'000BC4. Atmel A4, streaming.
 *
 * The receive of one page is overlapped with the programming of the one
 * before it: the acknowledgement sent here, and the busy poll that follows
 * it, both belong to the *previous* page -- the next page has already been
 * taken in by then. That is why the status byte is one page behind, and it
 * is deliberate, not an oversight.
 */
static void download_stream_a4(u32 addr)
{
    int ok = 1;

    send_serial_data_byte(DL_READY);
    while (ok == 1) {
        send_serial_data_byte(DL_REQUEST);
        if (wait_read_serial_byte() != DL_YES) {
            send_serial_data_byte(DL_STOP);
            flash_wait(addr);
            break;
        }
        send_serial_data_byte(DL_YES);
        recv_page(FLASH_PAGE_BUF);

        send_serial_data_byte(ok == 1 ? DL_READY : DL_FAILED);
        ok = flash_wait(addr);
        flash_command(addr, FLASH_CMD_PROGRAM);
        dma_copy(FLASH_PAGE_BUF, addr, FLASH_PAGE_SIZE);
        addr += FLASH_PAGE_SIZE;
    }
}

/* H'000DD6. Not flash at all: the page just lands in memory, no programming
 * and nothing to verify. */
static void download_ram(u32 addr)
{
    send_serial_data_byte(DL_READY);
    send_serial_data_byte(DL_REQUEST);
    recv_page(addr);
    send_serial_data_byte(DL_READY);
}

/* H'000D02. AMD and Fujitsu parts. Every byte needs its own unlock, and a
 * page that starts a 64K bank erases the bank first. */
static void download_stream_amd(u32 addr)
{
    int ok = 1;
    u16 i;

    send_serial_data_byte(DL_READY);
    while (ok == 1) {
        send_serial_data_byte(DL_REQUEST);
        if (wait_read_serial_byte() != DL_YES) {
            send_serial_data_byte(DL_STOP);
            break;
        }
        if ((addr & 0xFFFF) == 0) flash_erase_sector(addr);
        send_serial_data_byte(DL_YES);

        for (i = 0; i < FLASH_PAGE_SIZE; i++) {
            u8 b = wait_read_serial_byte();
            flash_command(addr, FLASH_CMD_PROGRAM);
            *(volatile u8 *)(addr + i) = b;
        }
        send_serial_data_byte(ok == 1 ? DL_READY : DL_FAILED);
        addr += FLASH_PAGE_SIZE;
    }
}

/* H'000EBC. The Atmel H'13 part: erase the chip up front, then program a
 * byte at a time, polling after each one.
 *
 * NOTE: the original writes every byte of the page to the same address --
 * the destination is never advanced inside the loop, only by a page at the
 * end of it. Reproduced as it stands, because this is what the ROM does,
 * but it means this path cannot ever have worked. It is unreachable on this
 * machine, whose flash identifies as the A4 part.
 */
static void download_stream_atmel13(u32 addr)
{
    int ok;
    u16 i;

    ok = flash_erase_chip(addr);
    send_serial_data_byte(ok == 1 ? DL_READY : DL_FAILED);

    while (ok != 0) {
        send_serial_data_byte(DL_REQUEST);
        if (wait_read_serial_byte() != DL_YES) {
            send_serial_data_byte(DL_STOP);
            break;
        }
        send_serial_data_byte(DL_YES);

        for (i = 0; i < FLASH_PAGE_SIZE; i++) {
            u8 b = wait_read_serial_byte();
            flash_command(addr, FLASH_CMD_PROGRAM);
            *(volatile u8 *)addr = b; /* see the note above */
            ok = flash_wait(addr);
        }
        send_serial_data_byte(ok == 1 ? DL_READY : DL_FAILED);
        addr += FLASH_PAGE_SIZE;
    }
}

/* H'001998: state 66, the 'PB' action. The two digits are a bank number, so
 * they move up into bits 16-23 before anything looks at them. */
static void state_download_bank(void)
{
    u32 addr = (BOOT_ADDR_ACC & 0xFFFF) << 16;
    u8 code;

    BOOT_ADDR_ACC = addr;
    code = flash_identify(addr);

    if (code == FLASH_NONE) {
        BOOT_STATE_1E = ST_ACK_V;
    } else if (code == FLASH_ATMEL_A4) {
        download_stream_a4(addr);
        BOOT_STATE_1E = ST_IDLE;
    } else if (code >= 6 && code <= 9) {
        download_stream_amd(addr);
        BOOT_STATE_1E = ST_IDLE;
    } else if (code == 0x02) {
        download_stream_atmel13(addr);
        BOOT_STATE_1E = ST_IDLE;
    } else {
        BOOT_STATE_1E = ST_ACK_V;
    }
}

/* H'001A52: state 72, the 'PS' action. Four digits, moved up by eight bits
 * to give a page-aligned address. */
static void state_download_sector(void)
{
    u32 addr = BOOT_ADDR_ACC << 8;
    u8 code;

    BOOT_ADDR_ACC = addr;
    code = flash_identify(addr);

    if (code == FLASH_ATMEL_A4) {
        download_page_a4(addr);
        BOOT_STATE_1E = ST_IDLE;
    } else if (code == FLASH_NOT_FLASH) {
        download_ram(addr);
        BOOT_STATE_1E = ST_IDLE;
    } else {
        BOOT_STATE_1E = ST_ACK_V;
    }
}

/* H'001920: state 5A, reached from 'P'. One more letter picks the form:
 * 'B' for a whole bank, 'S' for a single sector. */
static void state_download_kind(void)
{
    u8 c;

    if (!get_sci_rx_ready_bit()) return;

    c = read_serial_data_byte();
    if (c == 'B') {
        send_serial_data_byte('B');
        BOOT_STATE_1E = ST_DL_BANK_ADDR;
    } else if (c == 'S') {
        send_serial_data_byte('S');
        BOOT_STATE_1E = ST_DL_SECT_ADDR;
    } else {
        BOOT_STATE_1E = ST_ACK_N;
    }
}

/* H'001592: 'P'. */
static void cmd_download(void)
{
    BOOT_STATE_1E = ST_DL_KIND;
    protocol_tail();
}

/* ---- 'M', editing a page in place --------------------------------------
 * Where 'Z' writes one byte and 'P' takes whole pages, 'M' streams bytes as
 * hex pairs from a starting address and keeps going until the host stops.
 * The page under the cursor is held in the RAM buffer and only committed
 * when the address crosses into the next one, so a run of edits inside a
 * single page costs one programming cycle rather than one per byte.
 *
 * The host stops by sending anything that is not a hex digit. That is not
 * an error: it routes to the finishing state, which commits the part-filled
 * page. There is no other way to end the command.
 */

/* H'001BBA: state 92. */
static void state_modify_load(void)
{
    disable_interrupts();

    if (flash_identify(BOOT_ADDR_ACC) == FLASH_ATMEL_A4) {
        flash_page_load(BOOT_ADDR_ACC);
        BOOT_STATE_1E++;
    } else {
        BOOT_STATE_1E = ST_ACK_V;
    }

    enable_interrupts();
}

/* H'001BFA: states 93 and 94. */
static void state_modify_digit(void)
{
    u8 digit;

    if (!get_sci_rx_ready_bit()) return;

    digit = hex_digit_or(ST_MOD_DONE);
    BOOT_DATA_20 = (u8)((BOOT_DATA_20 << 4) + digit);
}

/* H'001C24: state 95. */
static void state_modify_put(void)
{
    u32 addr;

    *(volatile u8 *)(FLASH_PAGE_BUF + (BOOT_ADDR_ACC & 0xFF)) =
        (u8)BOOT_DATA_20;

    addr = BOOT_ADDR_ACC + 1;
    BOOT_ADDR_ACC = addr;

    if ((addr & 0xFF) == 0) {
        disable_interrupts();
        flash_page_program(addr - 1);
        enable_interrupts();
        BOOT_STATE_1E = ST_MOD_LOAD;
    } else {
        BOOT_STATE_1E = ST_MOD_DATA;
    }
}

/* H'001C8A: state 96. */
static void state_modify_done(void)
{
    disable_interrupts();
    flash_page_program(BOOT_ADDR_ACC - 1);
    enable_interrupts();
    BOOT_STATE_1E = ST_IDLE;
}

/* H'0015F6: 'M'. */
static void cmd_modify(void)
{
    BOOT_STATE_1E = ST_MOD_ADDR;
    protocol_tail();
}

/* ---- 'L', checksumming a range -----------------------------------------
 * Six digits of start address, six of byte count, and the 32-bit sum comes
 * back as eight hex digits. This is how a host checks that a download
 * landed intact without reading the whole range back over the wire.
 */

/* H'000F7E. A plain 32-bit sum of [count] bytes from [start]. */
static u32 byte_sum(u32 start, u32 count)
{
    const volatile u8 *p = (const volatile u8 *)start;
    u32 sum = 0;

    while (count-- != 0) sum += *p++;
    return sum;
}

/* H'001DB6: state CE. The start address is put aside in the first longword
 * of the page buffer -- free at this point, since no page is being
 * programmed -- and the accumulator is reset to take the count. */
static void state_sum_stash(void)
{
    *(volatile u32 *)FLASH_PAGE_BUF = BOOT_ADDR_ACC;
    BOOT_ADDR_ACC = 0;
    BOOT_STATE_1E = ST_SUM_COUNT;
}

/* H'001E16: state D5. H'204000-H'207FFF is walled off here as it is on the
 * read path, but rather than reading back as H'FF it answers H'AFAFAFAF --
 * a value no real range would sum to, so a host can tell it apart from a
 * genuine result. */
static void state_sum_go(void)
{
    u32 start = *(volatile u32 *)FLASH_PAGE_BUF;

    if (start >= 0x204000 && start < 0x208000) {
        BOOT_ADDR_ACC = 0xAFAFAFAFUL;
    } else {
        BOOT_ADDR_ACC = byte_sum(start, BOOT_ADDR_ACC);
    }
    BOOT_STATE_1E++;
}

/* H'001E7A and the seven states after it, one nibble per pass. The ROM codes
 * some of these as byte reads out of the accumulator and others as repeated
 * shifts; the nibbles are the same either way. */
static void state_sum_nibble(void)
{
    u8 shift = (u8)((ST_SUM_NIB_LAST - BOOT_STATE_1E) * 4);

    send_hex_nibble((u8)(BOOT_ADDR_ACC >> shift));
    if (BOOT_STATE_1E == ST_SUM_NIB_LAST) {
        BOOT_STATE_1E = ST_ACK_O;
    } else {
        BOOT_STATE_1E++;
    }
}

/* H'00160A: 'L'. */
static void cmd_checksum(void)
{
    BOOT_STATE_1E = ST_SUM_ADDR;
    BOOT_ADDR_ACC = 0;   /* the shared tail clears it again */
    protocol_tail();
}

/* H'001B0E: state 81, reached from 'Z' once the address and the byte are in.
 * Writes that one byte into flash: the page it falls in is read into the RAM
 * buffer, the byte patched in, and the whole page programmed back.
 *
 * Interrupts stay masked across the whole operation. The flash is the code
 * memory, and it cannot be read while it is being programmed, so anything
 * that tried to run from it in the meantime would fetch garbage.
 *
 * A bank that is not the part this ROM knows how to program, or a page that
 * does not come back within the busy timeout, is reported with 'V' rather
 * than the ordinary 'N'.
 */
static void state_flash_byte(void)
{
    disable_interrupts();

    if (flash_identify(BOOT_ADDR_ACC) == FLASH_ATMEL_A4) {
        flash_page_load(BOOT_ADDR_ACC);
        *(volatile u8 *)(FLASH_PAGE_BUF + (BOOT_ADDR_ACC & 0xFF)) =
            (u8)BOOT_DATA_20;
        BOOT_STATE_1E = flash_page_program(BOOT_ADDR_ACC) ? ST_ACK_O : ST_ACK_V;
    } else {
        BOOT_STATE_1E = ST_ACK_V;
    }

    enable_interrupts();
}

/* H'0015D8: 'Z'. */
static void cmd_flash_byte(void)
{
    BOOT_STATE_1E = ST_FLASH_ADDR;
    protocol_tail();
}

/* H'0015B6: 'N'. */
static void cmd_dump(void)
{
    BOOT_STATE_1E = ST_DUMP_ADDR;
    protocol_tail();
}

/* States BE and BF (H'001CE4, H'001D12): accept one expected character,
 * echoing it, and move on to the next state. Anything else is refused with
 * the shared 'N'. */
static void expect_char(u8 want, u8 next)
{
    if (!get_sci_rx_ready_bit()) return;

    if (read_serial_data_byte() == want) {
        send_serial_data_byte(want);
        BOOT_STATE_1E = next;
    } else {
        BOOT_STATE_1E = ST_ACK_N;
    }
}

/* H'001D40: state C0, the last character of "TrME". Moves the protocol onto
 * SCI1 -- the PC port. The 'E' is echoed and the settling delay taken before
 * the switch, so both still go out on the channel the host is using.
 */
static void state_to_sci1(void)
{
    if (!get_sci_rx_ready_bit()) return;

    if (read_serial_data_byte() == 'E') {
        send_serial_data_byte('E');
        delay(100);
        BOOT_STATE_1E = ST_IDLE;
        CHAN_SELECTION &= (u8)~CHAN_SCI0;
    } else {
        BOOT_STATE_1E = ST_ACK_N;
    }
}

/* H'0015EC: 'T'. Three more characters must follow before anything happens. */
static void cmd_to_sci1(void)
{
    BOOT_STATE_1E = ST_TO_SCI1_R;
    protocol_tail();
}

/* H'001860: state 2A, reached from 'J' once both digits are in. Switches to
 * the rate the host asked for, announces the machine again, and waits for the
 * host to re-establish contact at that rate.
 *
 * If the host does not follow -- because the divisor was one it cannot match
 * -- the rate drops back to the default and the machine keeps announcing
 * itself until contact is made again, so a bad rate cannot strand the link.
 */
static void state_baud_apply(void)
{
    sci_set_bitrate((u8)BOOT_DATA_20);
    delay(500);
    puts_serial(BOOT_STRING);

    if (!host_handshake()) {
        sci_set_bitrate(BRR_DEFAULT);
        do {
            puts_serial(BOOT_STRING);
        } while (!host_handshake());
    }
    BOOT_STATE_1E = ST_IDLE;
}

/* H'00159E: 'J'. */
static void cmd_baud(void)
{
    BOOT_STATE_1E = ST_BAUD_DIGIT;
    protocol_tail();
}

/* H'00189C and H'0018BA: states 32 and 33, reached from 'V'. Report the
 * version byte as two hex characters, one nibble per pass through the service
 * routine. Neither waits on the receiver -- there is nothing to read. */
static void state_version_hi(void)
{
    send_hex_nibble((u8)(BIOS_VERSION >> 4));
    BOOT_STATE_1E = ST_VER_LO;
}

static void state_version_lo(void)
{
    send_hex_nibble(BIOS_VERSION);
    BOOT_STATE_1E = ST_IDLE;
}

/* H'0015AA: 'V'. */
static void cmd_version(void)
{
    BOOT_STATE_1E = ST_VER_HI;
    protocol_tail();
}

/* H'001CAC: state B4, reached from 'Y'. Waits for the host to confirm with a
 * 'Q'. On confirmation it echoes the 'Q', raises the confirmed flag and drops
 * back to idle; anything else is refused with the shared 'N'. */
static void state_confirm(void)
{
    if (!get_sci_rx_ready_bit()) return;

    if (read_serial_data_byte() == 'Q') {
        send_serial_data_byte('Q');
        CHAN_SELECTION |= CHAN_CONFIRMED;
        BOOT_STATE_1E = ST_IDLE;
    } else {
        BOOT_STATE_1E = ST_ACK_N;
    }
}

/* H'0015E2: 'Y'. */
static void cmd_confirm(void)
{
    BOOT_STATE_1E = ST_CONFIRM;
    protocol_tail();
}

/* H'001628: 'S'. Starts the application at the first slot of its entry table
 * -- H'000412 in the original, the same slot the boot sequence uses -- after
 * waiting long enough for the acknowledgement to clear the transmitter.
 *
 * It sends that acknowledgement itself rather than going through the shared
 * tail, and it never returns, so the branch to the tail that follows it in
 * the original is unreachable. It sets no protocol state.
 */
static void cmd_start(void)
{
    void (*app)(void) = (void (*)(void))APP_ENTRY;

    send_serial_data_byte('S');
    delay(10);
    app();
}

/* H'001600: 'H'. */
static void cmd_halt(void)
{
    BOOT_STATE_1E = ST_HALT;
    protocol_tail();
}

/* H'0015C0: 'X'. */
static void cmd_reset(void)
{
    BOOT_STATE_1E = ST_RESET;
    protocol_tail();
}

/* H'001F8C: state A0, reached from 'G'. Hands the machine to the application
 * through the second slot of its entry table -- H'00041C in the original,
 * which loads the longword at H'200004 and jumps to it. It does not come
 * back, so the state is cleared first, for the benefit of anything that
 * inspects it afterwards.
 */
static void state_go(void)
{
    void (*app)(void) = (void (*)(void))APP_ENTRY_ALT;

    BOOT_STATE_1E = ST_IDLE;
    app();
}

/* H'00161E: 'G'. The jump happens on the next pass through the service
 * routine, so the letter is echoed before control is given away. */
static void cmd_go(void)
{
    BOOT_STATE_1E = ST_GO;
    protocol_tail();
}

/* H'0015CA: 'K'. Sets no state at all. It only replaces the echoed letter
 * with 'O', so the host gets an acknowledgement and the machine stays idle. */
static void cmd_ack(void)
{
    cmd_letter = 'O';
    protocol_tail();
}

/* H'0015CE: 'I'. Arms the identify state; the banner goes out on the next
 * pass through the service routine. */
static void cmd_identify(void)
{
    BOOT_STATE_1E = ST_IDENTIFY;
    protocol_tail();
}

/* H'0018F6: state H'50. Three strings, then back to idle. */
static void state_identify(void)
{
    puts_serial("BERNINA Electronic AG\r");
    puts_serial("BiosVersion: 1.10\r");
    puts_serial("Mai 97\r");
    BOOT_STATE_1E = ST_IDLE;
}

/* H'0014AE: state 0. Read a byte and dispatch on it. The original searches a
 * 19-entry letter table at H'0014E5 and jumps through a table at H'0014F4
 * indexed backwards; a switch says the same thing.
 *
 * H'00163C handles a letter that is not in the table: it substitutes 'Q' and
 * falls into the same tail, so the reply to an unknown command is "Q".
 */
static void state_idle(void)
{
    if (!get_sci_rx_ready_bit()) return;
    cmd_letter = read_serial_data_byte();
    switch (cmd_letter) {
    case 'I':
        cmd_identify();
        break;
    case 'r':
        cmd_read(0x01);
        break;
    case 'R':
        cmd_read(0x20);
        break;
    case 'w':
        cmd_write(0x01);
        break;
    case 'W':
        cmd_write(0xFF);
        break;
    case 'K':
        cmd_ack();
        break;
    case 'G':
        cmd_go();
        break;
    case 'X':
        cmd_reset();
        break;
    case 'H':
        cmd_halt();
        break;
    case 'S':
        cmd_start();
        break;
    case 'Y':
        cmd_confirm();
        break;
    case 'V':
        cmd_version();
        break;
    case 'J':
        cmd_baud();
        break;
    case 'T':
        cmd_to_sci1();
        break;
    case 'N':
        cmd_dump();
        break;
    case 'Z':
        cmd_flash_byte();
        break;
    case 'P':
        cmd_download();
        break;
    case 'M':
        cmd_modify();
        break;
    case 'L':
        cmd_checksum();
        break;
    default:
        cmd_letter = 'Q';
        protocol_tail();
        break;
    }
}

/* H'001090. Called round the application's main loop and round the boot
 * ROM's own loop. Both guards must pass before any command runs: the
 * transmitter has to be free to answer, and the line has to be error-free.
 *
 * Note that this is only reachable in practice while the boot ROM has
 * control. Once the application is running it owns SCI1 and clears RDRF
 * itself, so the idle state never sees an arriving byte.
 */
void serial_service(void)
{
    if (!get_sci_tx_ready_bit()) return;
    if (serial_rx_error_check() != 1) return;
    switch (BOOT_STATE_1E) {
    case ST_IDLE:
        state_idle();
        break;
    case ST_IDENTIFY:
        state_identify();
        break;
    case ST_READ_ADDR:
    case ST_READ_ADDR + 1:
    case ST_READ_ADDR + 2:
    case ST_READ_ADDR + 3:
    case ST_READ_ADDR + 4:
    case ST_READ_LAST:
    case ST_WRITE_ADDR:
    case ST_WRITE_ADDR + 1:
    case ST_WRITE_ADDR + 2:
    case ST_WRITE_ADDR + 3:
    case ST_WRITE_ADDR + 4:
    case ST_WRITE_LAST:
    case ST_DUMP_ADDR:      /* H'001FAE is H'001658 again */
    case ST_DUMP_ADDR + 1:
    case ST_DUMP_ADDR + 2:
    case ST_DUMP_ADDR + 3:
    case ST_DUMP_ADDR + 4:
    case ST_DUMP_LAST:
    case ST_FLASH_ADDR:     /* H'001AAE is H'001658 again */
    case ST_FLASH_ADDR + 1:
    case ST_FLASH_ADDR + 2:
    case ST_FLASH_ADDR + 3:
    case ST_FLASH_ADDR + 4:
    case ST_FLASH_LAST:
    case ST_DL_BANK_ADDR:   /* H'001962 and H'001A1C are H'001658 again */
    case ST_DL_BANK_LAST:
    case ST_DL_SECT_ADDR:
    case ST_DL_SECT_ADDR + 1:
    case ST_DL_SECT_ADDR + 2:
    case ST_DL_SECT_LAST:
    case ST_MOD_ADDR:       /* H'001B84 is H'001658 again */
    case ST_MOD_ADDR + 1:
    case ST_MOD_ADDR + 2:
    case ST_MOD_ADDR + 3:
    case ST_MOD_ADDR + 4:
    case ST_MOD_LAST:
    case ST_SUM_ADDR:       /* H'001D80 and H'001DE0 are H'001658 again */
    case ST_SUM_ADDR + 1:
    case ST_SUM_ADDR + 2:
    case ST_SUM_ADDR + 3:
    case ST_SUM_ADDR + 4:
    case ST_SUM_LAST:
    case ST_SUM_COUNT:
    case ST_SUM_COUNT + 1:
    case ST_SUM_COUNT + 2:
    case ST_SUM_COUNT + 3:
    case ST_SUM_COUNT + 4:
    case ST_SUM_CLAST:
        state_addr_digit();
        break;
    case ST_READ_HI:
        state_read_hi();
        break;
    case ST_READ_LO:
        state_read_lo();
        break;
    case ST_WRITE_DATA:
    case ST_WRITE_LO:
    case ST_BAUD_DIGIT:   /* H'001836 is H'001778 again, byte for byte */
    case ST_BAUD_LO:
    case ST_FLASH_DATA:     /* H'001AE4 is H'001778 again */
    case ST_FLASH_LO:
        state_write_digit();
        break;
    case ST_WRITE_PUT:
        state_write_put();
        break;
    case ST_ACK_O:
        state_ack_o();
        break;
    case ST_ACK_N:
        state_ack_n();
        break;
    case ST_ACK_V:
        state_ack_v();
        break;
    case ST_RESET:
        state_reset();
        break;
    case ST_GO:
        state_go();
        break;
    case ST_HALT:
        state_halt();
        break;
    case ST_CONFIRM:
        state_confirm();
        break;
    case ST_BAUD_APPLY:
        state_baud_apply();
        break;
    case ST_TO_SCI1_R:
        expect_char('r', ST_TO_SCI1_M);
        break;
    case ST_TO_SCI1_M:
        expect_char('M', ST_TO_SCI1_E);
        break;
    case ST_TO_SCI1_E:
        state_to_sci1();
        break;
    case ST_DUMP_SEND:
        state_dump_block();
        break;
    case ST_FLASH_PUT:
        state_flash_byte();
        break;
    case ST_DL_KIND:
        state_download_kind();
        break;
    case ST_DL_BANK_GO:
        state_download_bank();
        break;
    case ST_DL_SECT_GO:
        state_download_sector();
        break;
    case ST_MOD_LOAD:
        state_modify_load();
        break;
    case ST_MOD_DATA:
    case ST_MOD_LO:
        state_modify_digit();
        break;
    case ST_MOD_PUT:
        state_modify_put();
        break;
    case ST_MOD_DONE:
        state_modify_done();
        break;
    case ST_SUM_STASH:
        state_sum_stash();
        break;
    case ST_SUM_GO:
        state_sum_go();
        break;
    case ST_SUM_NIB:
    case ST_SUM_NIB + 1:
    case ST_SUM_NIB + 2:
    case ST_SUM_NIB + 3:
    case ST_SUM_NIB + 4:
    case ST_SUM_NIB + 5:
    case ST_SUM_NIB + 6:
    case ST_SUM_NIB_LAST:
        state_sum_nibble();
        break;
    case ST_VER_HI:
        state_version_hi();
        break;
    case ST_VER_LO:
        state_version_lo();
        break;
    default:
        BOOT_STATE_1E = ST_UNKNOWN;   /* H'00201E */
        break;
    }
}

/* ---- routines the application calls through the vector table -----------
 * Slots 2-6 and 22 hold boot ROM entry points rather than interrupt
 * handlers. The application is the untouched original binary, so it calls
 * them the original ROM's way: the first argument in ER6, any others on the
 * stack, and the result back in R6. The compiler passes arguments in
 * ER0/ER1/ER2 and returns in R0, so each slot has a short assembly shim in
 * vectors.S that translates between the two. What the vector points at is
 * the shim; the C below is the body.
 */

/* Ring buffers for the bridge, at the addresses the original uses. Indices
 * run 0..H'6E, so each direction holds 111 bytes. */
#define BRIDGE_LAST         0x6E
#define SCI0_RX_HEAD        REG8(0xFFFE12UL)
#define SCI0_RX_TAIL        REG8(0xFFFE13UL)
#define SCI0_RX_DATA        0xFFFE14UL
#define SCI1_RX_HEAD        REG8(0xFFFE10UL)
#define SCI1_RX_TAIL        REG8(0xFFFE11UL)
#define SCI1_RX_DATA        0xFFFE87UL

/* H'000ADC, slot 3. Writes a block of any length and alignment into flash,
 * a page at a time. Each page is read into the buffer before the part that
 * is changing is overwritten, so the bytes either side of the block survive
 * -- which is what makes this usable for saving a handful of settings into
 * a page that holds something else as well.
 */
u16 flash_write_block(u32 src, u32 dst, u32 len)
{
    u16 chunk;
    int last = 0;

    do {
        flash_page_load(dst);

        if ((dst & 0xFF) + len > FLASH_PAGE_SIZE) {
            chunk = (u16)(FLASH_PAGE_SIZE - (dst & 0xFF));
        } else {
            chunk = (u16)len;
            last = 1;
        }

        dma_copy(src, FLASH_PAGE_BUF + (dst & 0xFF), chunk);
        flash_page_program(dst);

        src += chunk;
        dst += chunk;
        len -= chunk;
    } while (!last);

    /* The original sets no return value; R6 happens to hold the last chunk
     * size when it returns. Handed back here so a caller that reads it sees
     * what it saw before. */
    return chunk;
}

/* The escape sequence is "TrME" -- the same one the 'T' command uses.
 *
 * The original's first test compares the match count against E6, which it
 * never initialises; the surrounding chain makes plain that zero is meant,
 * and an uninitialised register read has no equivalent in C, so it is
 * written as zero here. Worth knowing when comparing the two: if E6 happens
 * to arrive non-zero, the original can never start matching.
 */
static u8 escape_char(u16 matched, u8 current)
{
    switch (matched) {
    case 0:  return 'T';
    case 1:  return 'r';
    case 2:  return 'M';
    case 3:  return 'E';
    default: return current;   /* the ROM's chain has no final else */
    }
}

/* H'002122, slot 4. Joins the two serial ports back to back, so a host on
 * SCI1 can talk to whatever is on SCI0 -- the embroidery module -- with the
 * machine passing bytes through in the middle. Each direction has its own
 * ring buffer, and a line error on either side is reported to the host as a
 * NAK.
 *
 * It runs until "TrME" has gone past in both directions, then hands back 1.
 * The single `expected` byte is shared by both directions, as in the ROM,
 * so whichever side last received a byte decides what the other is matching
 * against.
 */
int serial_bridge(void)
{
    u16 esc_to_host = 0;     /* r3: progress on bytes going out to SCI1 */
    u16 esc_to_module = 0;   /* e3: and on those going out to SCI0 */
    u8 expected = 0;         /* r5h */
    u8 i, b;

    SCI0_RX_HEAD = 0;        /* H'0020FA */
    SCI0_RX_TAIL = 0;
    SCI1_RX_HEAD = 0;        /* H'00210E */
    SCI1_RX_TAIL = 0;
    CHAN_SELECTION &= (u8)~CHAN_CONFIRMED;

    while (!(SSR1 & SSR_TDRE)) { }
    TDR1 = 'O';
    SSR1 = (u8)~SSR_TDRE;

    while (esc_to_host != 4 || esc_to_module != 4) {
        if ((SSR1 & SSR_TDRE) && SCI0_RX_HEAD != SCI0_RX_TAIL) {
            i = SCI0_RX_TAIL;
            SCI0_RX_TAIL = (u8)(i + 1);
            b = *(volatile u8 *)(SCI0_RX_DATA + i);
            TDR1 = b;
            esc_to_host = (b == expected) ? (u16)(esc_to_host + 1) : 0;
            SSR1 = (u8)~SSR_TDRE;
            if (SCI0_RX_TAIL > BRIDGE_LAST) SCI0_RX_TAIL = 0;
        }

        if ((SSR0 & SSR_TDRE) && SCI1_RX_HEAD != SCI1_RX_TAIL) {
            i = SCI1_RX_TAIL;
            SCI1_RX_TAIL = (u8)(i + 1);
            b = *(volatile u8 *)(SCI1_RX_DATA + i);
            TDR0 = b;
            esc_to_module = (b == expected) ? (u16)(esc_to_module + 1) : 0;
            SSR0 = (u8)~SSR_TDRE;
            if (SCI1_RX_TAIL > BRIDGE_LAST) SCI1_RX_TAIL = 0;
        }

        if (SSR0 & SSR_RX_ERRORS) {
            (void)RDR0;
            SSR0 = (u8)~SSR_RX_ERRORS;
            TDR1 = 0x21;          /* the NAK goes to the host either way */
            SSR1 = (u8)~SSR_TDRE;
        }

        if (SSR0 & SSR_RDRF) {
            i = SCI0_RX_HEAD;
            SCI0_RX_HEAD = (u8)(i + 1);
            *(volatile u8 *)(SCI0_RX_DATA + i) = RDR0;
            if (SCI0_RX_HEAD > BRIDGE_LAST) SCI0_RX_HEAD = 0;
            SSR0 = (u8)~SSR_RDRF;
            expected = escape_char(esc_to_host, expected);
        }

        if (SSR1 & SSR_RX_ERRORS) {
            (void)RDR1;
            SSR1 = (u8)~SSR_RX_ERRORS;
            TDR1 = 0x21;
            SSR1 = (u8)~SSR_TDRE;
        }

        if (SSR1 & SSR_RDRF) {
            i = SCI1_RX_HEAD;
            SCI1_RX_HEAD = (u8)(i + 1);
            *(volatile u8 *)(SCI1_RX_DATA + i) = RDR1;
            if (SCI1_RX_HEAD > BRIDGE_LAST) SCI1_RX_HEAD = 0;
            SSR1 = (u8)~SSR_RDRF;
            expected = escape_char(esc_to_module, expected);
        }
    }
    return 1;
}

/* H'0022FE, slot 5: select serial port 0 and report success. */
int select_sci0(void)
{
    CHAN_SELECTION |= CHAN_SEL_PORT0;
    return 1;
}

/* H'002312, slot 6: is port 0 the selected one? The original turns the bit
 * into a value with BLD/SUBX/NEG rather than branching. */
int sci0_is_selected(void)
{
    return (CHAN_SELECTION & CHAN_SCI0) ? 1 : 0;
}

/* H'002328, slot 22: the confirmed flag that 'Y' raises, as 0 or 1. */
int host_confirmed(void)
{
    return (CHAN_SELECTION & CHAN_CONFIRMED) ? 1 : 0;
}

/* The failure loop: with no usable application the machine sits here and
 * runs the download protocol, waiting to be given one. */
static void download_loop(void)
{
    serial_service();
}

/* ---- the boot sequence ------------------------------------------------- */

void boot_main(void)
{
    hardware_init();
    puts_serial("BOS");

    /* Both conditions must hold, and the original branches away from the
     * handover unless they do. */
    if (host_handshake() != 1 && app_entry_looks_valid()) {
        CHAN_SELECTION &= (u8)~BOOT_FAILED_FLAG;
        send_serial_data_byte('N');
        delay(10);
        enter_application();
        return;
    }

    /* No usable application: say so and wait to be given one. */
    send_serial_data_byte('M');
    delay(10);
    CHAN_SELECTION |= BOOT_FAILED_FLAG;
    for (;;) {
        download_loop();
    }
}
