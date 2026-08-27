/* artista180_burn_application -- put an application image into a Bernina
 * artista 180 over its serial link.
 *
 *   artista180_burn_application [options] <port> <file.bin>
 *
 * The machine's boot ROM carries a nineteen-command download protocol on
 * the SCI. This drives the handful of it that a firmware burn needs.
 *
 * ---- how the machine is caught ------------------------------------------
 *
 * The protocol only answers while the boot ROM has control. Once the
 * application is running it owns the channel and clears the receive flag
 * itself, so nothing arriving is ever seen. The one way in is the window at
 * reset: the ROM sends "BOS", then polls both channels five hundred times
 * for the two characters "EB". Answer that and it stays in the ROM serving
 * the link; ignore it and it hands over to the application.
 *
 * So the machine must be reset -- powered off and on -- while this is
 * waiting. It listens for "BOS" and answers.
 *
 * ---- the sequence -------------------------------------------------------
 *
 *   J 0 2      raise the rate: the divisor goes into BRR, and the machine
 *              re-announces itself and re-handshakes at the new rate. The
 *              default is 19200 and this asks for 115200, which is six
 *              times less time on the wire. If the host does not follow,
 *              the machine drops back by itself, so a bad rate cannot
 *              strand the link.
 *   H          halt: from here the ROM does nothing but serve the link. It
 *              runs from its own flash device at H'000000, which is not the
 *              one being programmed, so it is safe to rewrite H'200000.
 *   M aaaaaa   stream from that address. Every byte goes as two hex digits;
 *              the ROM buffers a page and programs it when the address
 *              crosses into the next one.
 *   .          any non-hex character ends the stream and commits the
 *              part-filled last page. There is no other way to end it.
 *   L aaaaaa nnnnnn  a plain 32-bit sum of the range, back as eight hex
 *              digits -- the check that the burn landed.
 *   X          reset, and the machine boots the application just written.
 *
 * Every digit the host sends is echoed. That is the flow control: the ROM
 * is a state machine driven one character at a time, and reading the echo
 * back before sending the next is what keeps the two in step.
 */
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

static const char HANDSHAKE[2] = { 'E', 'B' };
static const char BOOT_STRING[3] = { 'B', 'O', 'S' };

static int verbose = 0;

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "artista180_burn_application: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void note(const char *fmt, ...)
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
struct rate { unsigned baud; unsigned brr; };
static const struct rate RATES[] = {
    {  19200, 0x11 }, {  38400, 0x08 }, {  57600, 0x05 }, { 115200, 0x02 },
};
#define NRATES ((int)(sizeof RATES / sizeof RATES[0]))

static speed_t speed_of(unsigned baud)
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
static void port_configure(int fd, unsigned baud)
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

static int port_open(const char *name, unsigned baud)
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

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void put_bytes(int fd, const void *p, size_t n)
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

static void put_byte(int fd, int c)
{
    char b = (char)c;
    put_bytes(fd, &b, 1);
}

/* One byte, or -1 if none arrived within `timeout` seconds. */
static int get_byte(int fd, double timeout)
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
static void put_echoed(int fd, int c, const char *what)
{
    int got;
    put_byte(fd, c);
    got = get_byte(fd, 2.0);
    if (got < 0) die("%s: sent '%c' and nothing came back", what, c);
    if (got != c)
        die("%s: sent '%c' and got '%c' (H'%02X) back", what, c,
            got >= 32 && got < 127 ? got : '.', got);
}

static void put_hex(int fd, unsigned long v, int digits, const char *what)
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
static void wait_for_boot(int fd, double timeout, int expect_status)
{
    const double until = now_seconds() + timeout;
    int matched = 0;

    note("waiting for the machine to announce itself -- reset it now");

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
static void command(int fd, int letter)
{
    put_echoed(fd, letter, "command");
}

/* ---- raising the rate --------------------------------------------------- */

/* 'J' takes two hex digits of BRR, sets the rate, re-announces and expects
 * the host to handshake again at the new rate. If the host does not follow,
 * the machine returns to the default by itself and keeps announcing, so this
 * cannot strand the link.
 */
static void raise_rate(int fd, const char *port, unsigned baud, unsigned brr)
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

    wait_for_boot(fd, 10.0, 0);
    note("link is up at %u baud", baud);
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
static void burn(int fd, unsigned long base, const unsigned char *image,
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
static unsigned long checksum(int fd, unsigned long base, unsigned long len)
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

static unsigned long sum_of(const unsigned char *p, size_t n)
{
    unsigned long s = 0;
    size_t i;
    for (i = 0; i < n; i++) s += p[i];
    return s & 0xFFFFFFFFUL;
}

/* ---- the file ----------------------------------------------------------- */

static unsigned char *read_image(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (f == NULL) die("cannot open %s: %s", path, strerror(errno));
    if (fseek(f, 0, SEEK_END) != 0) die("%s: not seekable", path);
    n = ftell(f);
    if (n < 0) die("%s: %s", path, strerror(errno));
    if (n == 0) die("%s is empty", path);
    if ((unsigned long)n > APP_LIMIT)
        die("%s is %ld bytes; the application area holds H'%lX", path, n,
            APP_LIMIT);
    rewind(f);

    buf = malloc((size_t)n);
    if (buf == NULL) die("out of memory for %ld bytes", n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        die("%s: short read", path);
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

/* ---- ------------------------------------------------------------------- */

static void usage(void)
{
    fprintf(stderr,
        "usage: artista180_burn_application [options] <port> <file.bin>\n"
        "\n"
        "  <port>   the serial port the machine is on:\n"
        "             Linux   /dev/ttyUSB0, /dev/ttyS0\n"
        "             macOS   /dev/cu.usbserial-XXXX\n"
        "  <file>   the application image, as built by the application's\n"
        "           Makefile -- app.bin\n"
        "\n"
        "options:\n"
        "  -a ADDR  burn at this address (default H'200000)\n"
        "  -b BAUD  rate to raise the link to: 19200, 38400, 57600 or\n"
        "           115200 (default 115200; 19200 leaves it alone)\n"
        "  -w SECS  how long to wait for the machine to be reset (default 60)\n"
        "  -n       burn but do not start the application afterwards\n"
        "  -q       quiet\n"
        "\n"
        "The machine must be reset while this is waiting: the download\n"
        "protocol only answers in the window before the boot ROM hands over\n"
        "to the application.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *port = NULL, *path = NULL;
    unsigned long base = APP_BASE;
    unsigned baud = 115200, brr = 0x02;
    double wait = 60.0;
    int start_after = 1, i, fd;
    unsigned char *image;
    size_t len;
    unsigned long want, got;

    verbose = 1;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            switch (argv[i][1]) {
            case 'a':
                if (++i == argc) usage();
                base = strtoul(argv[i], NULL, 16);
                break;
            case 'b': {
                int k, found = 0;
                if (++i == argc) usage();
                baud = (unsigned)strtoul(argv[i], NULL, 10);
                for (k = 0; k < NRATES; k++)
                    if (RATES[k].baud == baud) { brr = RATES[k].brr; found = 1; }
                if (!found) die("%u is not one of the rates the machine has",
                                baud);
                break;
            }
            case 'w':
                if (++i == argc) usage();
                wait = strtod(argv[i], NULL);
                break;
            case 'n': start_after = 0; break;
            case 'q': verbose = 0; break;
            default: usage();
            }
        } else if (port == NULL) {
            port = argv[i];
        } else if (path == NULL) {
            path = argv[i];
        } else {
            usage();
        }
    }
    if (port == NULL || path == NULL) usage();

    image = read_image(path, &len);
    want = sum_of(image, len);
    note("%s: %zu bytes, sum H'%08lX", path, len, want);

    fd = port_open(port, BAUD_DEFAULT);
    note("%s open at %u baud", port, BAUD_DEFAULT);

    wait_for_boot(fd, wait, 1);

    if (baud != BAUD_DEFAULT) raise_rate(fd, port, baud, brr);

    /* Halt first. From here the ROM serves the link and nothing else, and it
     * is running from its own flash device -- not the one about to be
     * rewritten. */
    note("halting");
    command(fd, 'H');

    burn(fd, base, image, len);

    got = checksum(fd, base, len);
    if (got != want)
        die("checksum H'%08lX, expected H'%08lX -- the burn did not land",
            got, want);
    note("checksum H'%08lX matches", got);

    if (start_after) {
        note("resetting");
        command(fd, 'X');
    }

    free(image);
    close(fd);
    if (verbose) fprintf(stderr, "done\n");
    return 0;
}
