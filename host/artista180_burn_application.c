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
 */#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "artista180_link.h"

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

    program_name = "artista180_burn_application";
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

    image = read_image(path, &len, APP_LIMIT);
    want = sum_of(image, len);
    note("%s: %zu bytes, sum H'%08lX", path, len, want);

    fd = port_open(port, BAUD_DEFAULT);
    note("%s open at %u baud", port, BAUD_DEFAULT);

    wait_for_boot(fd, wait, 1,
                  "waiting for the machine to announce itself "
                  "-- reset it now");

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

