/* The half of the host tools that talks to the machine: the serial port, the
 * boot ROM's download protocol, and the file reading around it.
 *
 * Split out of artista180_burn_application so that the boot-ROM updater can
 * drive the same protocol without a second copy of it. The sequence and the
 * way the machine has to be caught are documented in that file.
 */
#include "artista180_link.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define APP_BASE   0x200000UL   /* where the application lives in flash */
#define APP_LIMIT  0x200000UL   /* and how much room there is for it */
#define PAGE_SIZE  0x100UL

#define BRR_DEFAULT 0x11        /* 19200 at phi = 11059200 */
#define BAUD_DEFAULT 19200

const char HANDSHAKE[2] = { 'E', 'B' };
const char BOOT_STRING[3] = { 'B', 'O', 'S' };

int verbose = 0;

/* What die() puts in front of its message. Each tool sets this; without it
 * the shared code would name whichever binary it was first written for. */
const char *program_name = "artista180";

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", program_name);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void note(const char *fmt, ...)
{
    va_list ap;
    if (!verbose) return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- the port ---------------------------------------------------------- */

/* The rates the machine can be asked for, and the divisor that picks each.
 * baud = phi / (32 * (BRR + 1)) with phi = 11059200, which is exact for all
 * of these. */
const struct rate RATES[] = {
    {  19200, 0x11 }, {  38400, 0x08 }, {  57600, 0x05 }, { 115200, 0x02 },
};
const int NRATES = (int)(sizeof RATES / sizeof RATES[0]);

speed_t speed_of(unsigned baud)
{
    switch (baud) {
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return 0;
    }
}

/* 8N1, no flow control, raw. VMIN 0 with VTIME 1 makes a read return after a
 * tenth of a second whether or not anything came, which is what lets the
 * waiting loops below time out rather than block for ever. */
void port_configure(int fd, unsigned baud)
{
    struct termios t;
    speed_t s = speed_of(baud);

    if (s == 0) die("no termios constant for %u baud", baud);
    if (tcgetattr(fd, &t) != 0) die("tcgetattr: %s", strerror(errno));

    cfmakeraw(&t);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= (tcflag_t)~CSTOPB;     /* one stop bit */
    t.c_cflag &= (tcflag_t)~PARENB;     /* no parity */
    t.c_cflag &= (tcflag_t)~CRTSCTS;    /* no hardware flow control */
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 1;
    if (cfsetispeed(&t, s) != 0 || cfsetospeed(&t, s) != 0)
        die("cfsetspeed: %s", strerror(errno));
    if (tcsetattr(fd, TCSANOW, &t) != 0) die("tcsetattr: %s", strerror(errno));
}

int port_open(const char *name, unsigned baud)
{
    int fd = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (fd < 0) die("cannot open %s: %s", name, strerror(errno));
    /* Back to blocking now that no modem lines can hold the open. */
    if (fcntl(fd, F_SETFL, 0) != 0) die("fcntl: %s", strerror(errno));
    port_configure(fd, baud);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ---- bytes ------------------------------------------------------------- */

double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void put_bytes(int fd, const void *p, size_t n)
{
    const char *b = p;
    while (n != 0) {
        ssize_t w = write(fd, b, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            die("write: %s", strerror(errno));
        }
        b += w;
        n -= (size_t)w;
    }
}

void put_byte(int fd, int c)
{
    char b = (char)c;
    put_bytes(fd, &b, 1);
}

/* One byte, or -1 if none arrived within `timeout` seconds. */
int get_byte(int fd, double timeout)
{
    const double until = now_seconds() + timeout;
    for (;;) {
        unsigned char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 1) return c;
        if (r < 0 && errno != EINTR && errno != EAGAIN)
            die("read: %s", strerror(errno));
        if (now_seconds() >= until) return -1;
    }
}

/* Send one character and require the machine to echo it back. Every digit of
 * every operand goes through here: the echo is the only acknowledgement the
 * protocol has, and running ahead of it desynchronises the state machine. */
void put_echoed(int fd, int c, const char *what)
{
    int got;
    put_byte(fd, c);
    got = get_byte(fd, 2.0);
    if (got < 0) die("%s: sent '%c' and nothing came back", what, c);
    if (got != c)
        die("%s: sent '%c' and got '%c' (H'%02X) back", what, c,
            got >= 32 && got < 127 ? got : '.', got);
}

void put_hex(int fd, unsigned long v, int digits, const char *what)
{
    static const char D[] = "0123456789ABCDEF";
    int i;
    for (i = digits - 1; i >= 0; i--)
        put_echoed(fd, D[(v >> (4 * i)) & 0xF], what);
}

/* ---- catching the machine ---------------------------------------------- */

/* Wait for the boot ROM's announcement and answer it.
 *
 * The ROM sends "BOS" and then polls for "EB". It drains and discards one
 * byte before it starts polling, and it alternates between the two channels,
 * so the answer is sent one character at a time and the second is only sent
 * once the first has been taken. On SCI1 the ROM echoes the character it
 * matched; on SCI0 it does not, so a missing echo is not an error here.
 *
 * Returns when the handshake has been answered.
 */
void wait_for_boot(int fd, double timeout, int expect_status,
                   const char *waiting_for)
{
    const double until = now_seconds() + timeout;
    int matched = 0;

    note("%s", waiting_for);

    while (now_seconds() < until) {
        int c = get_byte(fd, 0.2);
        if (c < 0) continue;

        if (c == BOOT_STRING[matched]) {
            if (++matched == (int)sizeof BOOT_STRING) {
                note("saw \"BOS\"");
                /* Answer. The first character may be the one the ROM drains
                 * and throws away, so both are sent and the reply is not
                 * insisted on. */
                put_byte(fd, HANDSHAKE[0]);
                usleep(20000);
                put_byte(fd, HANDSHAKE[1]);
                note("answered \"EB\"");

                /* On SCI1 the ROM echoes each character it matched. Those
                 * echoes have to come off the wire before any command goes
                 * out, or the first of them is read as the answer to it.
                 *
                 * What follows them depends on which handshake this was.
                 * The one in boot_main is followed by a status byte -- 'M'
                 * for "no usable application, waiting to be given one",
                 * which is where a successful handshake lands, or 'N' for
                 * "handing over to the application", which means the window
                 * was missed. The one inside 'J' sends nothing at all: it
                 * just goes idle, ready for the next command. */
                for (;;) {
                    int c2 = get_byte(fd, expect_status ? 2.0 : 0.4);
                    if (c2 < 0) {
                        if (!expect_status) return;   /* idle, as expected */
                        die("no answer to the handshake");
                    }
                    if (expect_status && c2 == 'M') {
                        note("machine is in the download loop");
                        return;
                    }
                    if (expect_status && c2 == 'N')
                        die("the machine handed over to the application -- "
                            "the boot window was missed; reset it again");
                }
            }
        } else {
            matched = (c == BOOT_STRING[0]) ? 1 : 0;
        }
    }
    die("no \"BOS\" within %.0f seconds -- reset the machine while this runs",
        timeout);
}

/* A command letter, which the machine echoes through the shared tail. */
void command(int fd, int letter)
{
    put_echoed(fd, letter, "command");
}

/* ---- raising the rate --------------------------------------------------- */

/* 'J' takes two hex digits of BRR, sets the rate, re-announces and expects
 * the host to handshake again at the new rate. If the host does not follow,
 * the machine returns to the default by itself and keeps announcing, so this
 * cannot strand the link.
 */
void raise_rate(int fd, const char *port, unsigned baud, unsigned brr)
{
    (void)port;
    note("asking for %u baud (BRR H'%02X)", baud, brr);
    command(fd, 'J');
    put_hex(fd, brr, 2, "baud divisor");

    /* The machine is now talking at the new rate; follow it before trying to
     * read the announcement it is about to send. */
    usleep(50000);
    port_configure(fd, baud);
    tcflush(fd, TCIFLUSH);

    /* Not a reset: the machine re-announces itself at the new rate, so
     * saying "reset it now" here would be telling the user to undo the
     * thing that just worked. */
    wait_for_boot(fd, 10.0, 0,
                  "waiting for it to re-announce at the new rate");
    note("link is up at %u baud", baud);
}

/* 'N': 256 raw bytes from [addr], which is how anything the machine has
 * left in its RAM is read back. Unlike 'R' these are not hex, so the whole
 * page arrives in one pass and is acknowledged with 'O'. */
void dump_page(int fd, unsigned long addr, unsigned char *out)
{
    int i, c;

    command(fd, 'N');
    put_hex(fd, addr, 6, "dump address");
    for (i = 0; i < 256; i++) {
        c = get_byte(fd, 10.0);
        if (c < 0) die("the dump stopped after %d of 256 bytes", i);
        out[i] = (unsigned char)c;
    }
    c = get_byte(fd, 10.0);
    if (c != 'O')
        die("the dump did not finish cleanly (expected 'O', got %d)", c);
}

/* ---- the burn ----------------------------------------------------------- */

/* Stream the image with 'M'.
 *
 * Two hex digits a byte, each echoed. The ROM keeps the page under the
 * cursor in its RAM buffer and programs it when the address crosses into the
 * next one, so a whole page costs one programming cycle rather than two
 * hundred and fifty-six. The stream ends with any character that is not a
 * hex digit, which commits the part-filled last page; the ROM echoes '?' for
 * it rather than the character itself.
 *
 * Programming a page takes real time on the part, and the ROM does it with
 * interrupts off between one digit and the next. Nothing in the protocol
 * announces it -- the echo of the next digit simply takes longer to come
 * back -- which is why put_echoed waits two seconds rather than milliseconds.
 */
void burn(int fd, unsigned long base, const unsigned char *image,
                 size_t len)
{
    static const char D[] = "0123456789ABCDEF";
    const double started = now_seconds();
    size_t i;
    int last_pct = -1;

    note("streaming %zu bytes to H'%06lX", len, base);
    command(fd, 'M');
    put_hex(fd, base, 6, "start address");

    for (i = 0; i < len; i++) {
        put_echoed(fd, D[image[i] >> 4], "data");
        put_echoed(fd, D[image[i] & 0xF], "data");

        if (verbose) {
            int pct = (int)((i + 1) * 100 / len);
            if (pct != last_pct) {
                double el = now_seconds() - started;
                fprintf(stderr, "\r  %3d%%  %zu/%zu bytes  %.0fs", pct, i + 1,
                        len, el);
                fflush(stderr);
                last_pct = pct;
            }
        }
    }

    /* Anything that is not a hex digit. The machine answers '?' to it. */
    put_byte(fd, '.');
    {
        int got = get_byte(fd, 5.0);
        if (got < 0) die("no answer to the end of the stream");
        if (got != '?')
            die("expected '?' ending the stream, got '%c' (H'%02X)",
                got >= 32 && got < 127 ? got : '.', got);
    }
    if (verbose) fputc('\n', stderr);
    note("streamed in %.0f seconds", now_seconds() - started);
}

/* 'L': a plain 32-bit sum of a range, back as eight hex digits. */
unsigned long checksum(int fd, unsigned long base, unsigned long len)
{
    unsigned long sum = 0;
    int i;

    command(fd, 'L');
    put_hex(fd, base, 6, "checksum address");
    put_hex(fd, len, 6, "checksum length");

    for (i = 0; i < 8; i++) {
        int c = get_byte(fd, 10.0);
        if (c < 0) die("checksum: only %d of 8 digits came back", i);
        if (c >= '0' && c <= '9')      sum = (sum << 4) | (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') sum = (sum << 4) | (unsigned)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') sum = (sum << 4) | (unsigned)(c - 'a' + 10);
        else die("checksum: '%c' is not a hex digit", c);
    }

    /* The eight nibbles are followed by 'O': the last of them leaves the
     * machine in the shared acknowledgement state rather than idle. Left on
     * the wire it would be read as the answer to whatever came next. */
    {
        int c = get_byte(fd, 5.0);
        if (c < 0) die("checksum: no 'O' after the digits");
        if (c != 'O')
            die("checksum: expected 'O' after the digits, got '%c' (H'%02X)",
                c >= 32 && c < 127 ? c : '.', c);
    }
    return sum;
}

unsigned long sum_of(const unsigned char *p, size_t n)
{
    unsigned long s = 0;
    size_t i;
    for (i = 0; i < n; i++) s += p[i];
    return s & 0xFFFFFFFFUL;
}

/* ---- the file ----------------------------------------------------------- */

unsigned char *read_image(const char *path, size_t *out_len,
                          unsigned long limit)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (f == NULL) die("cannot open %s: %s", path, strerror(errno));
    if (fseek(f, 0, SEEK_END) != 0) die("%s: not seekable", path);
    n = ftell(f);
    if (n < 0) die("%s: %s", path, strerror(errno));
    if (n == 0) die("%s is empty", path);
    if ((unsigned long)n > limit)
        die("%s is %ld bytes; there is room for H'%lX", path, n, limit);
    rewind(f);

    buf = malloc((size_t)n);
    if (buf == NULL) die("out of memory for %ld bytes", n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        die("%s: short read", path);
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

