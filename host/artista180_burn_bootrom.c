/* artista180_burn_bootrom -- update a v5 boot ROM in a Bernina artista 180,
 * or just ask the boot flash what it is.
 *
 *   artista180_burn_bootrom [options] <port> <updater.bin> [image.bin]
 *
 * ---- why this is not just the application burner with a different address -
 *
 * It cannot be. The boot ROM's 'M' and 'P' commands take an address from the
 * host and neither checks it, so both will happily aim a page-program at
 * bank 0 -- and the moment that command latches, the device answers reads
 * with toggling status instead of data. The next instruction fetch comes
 * back as status and the machine is gone, having already overwritten its own
 * reset vector. Nothing in the ROM guards against it.
 *
 * So the boot flash is never written by the boot ROM. It is written by a
 * small updater running out of the *application* flash: the two devices sit
 * on different bus areas, so while area 0 is busy the CPU keeps fetching
 * instructions, and the new image, out of area 1.
 *
 * ---- the sequence -------------------------------------------------------
 *
 * The first part is an ordinary application-flash burn, which is safe and
 * repeatable because it touches the other device:
 *
 *   J, H       raise the rate and halt, as the application burner does
 *   M 3F0000   the updater, into the top 64K of the application device
 *   M 3F4000   the image it is to install, if one was given
 *
 * Then the two steps that have no equivalent in the application burner:
 *
 *   M 200004   point the entry longword at the updater. 'G' jumps to
 *              whatever is here, and this is the only way to reach code the
 *              ROM did not put there itself.
 *   G          go.
 *
 * ---- how the answer comes back ------------------------------------------
 *
 * Not on the wire. The updater does not speak the protocol: it masks
 * interrupts, does its work, and hands back to the loader.
 *
 * But handing back means the machine boots again and announces itself, so
 * the result can be fetched out of the RAM it was left in. It comes back at
 * the default rate whatever the link was raised to, so the port drops back
 * before catching it, and then 'N' dumps the page: a status byte at
 * H'FFF7F0, and for --identify the maker and device bytes at H'FFF7F8 and
 * H'FFF7F9.
 *
 * ---- what each mode writes ----------------------------------------------
 *
 * --probe writes nothing at all. It asks the boot ROM's own 'P B 00' whether
 * it recognises the boot flash and then refuses the transfer before a byte
 * is programmed, so it needs no updater and can be run at any time. What it
 * cannot tell you is which part it is: all three of the ROM's download
 * streams open with the same "OE", so the answer is only "the ROM will drive
 * this" or "it will not".
 *
 * --identify does write, and the option name says "install nothing" rather
 * than "write nothing" for that reason: the routine that reads the maker and
 * device bytes lives in updater.bin, so it has to be put on the machine
 * before it can be run. It goes into spare *application* flash at H'3E8000,
 * which is the safe device -- an interrupted write there costs nothing but
 * another go, and the boot flash is not touched.
 *
 * ---- this installs into a v5 machine; it does not create one -----------
 *
 * The image slots are slots only because the v5 permanent block -- the
 * vector table, the trampolines and the stage-0 loader in H'000000-H'0007FF
 * -- is already there to choose between them. On a stock machine H'000800 is
 * the boot ROM's own code, and putting an image there overwrites it while
 * leaving the reset vector below it intact: the machine still starts, and
 * dies at the first call into what was overwritten.
 *
 * So an install checks first, by dumping both slots and looking for the
 * "B180" header, and refuses if neither has one. Putting v5 on a machine for
 * the first time means writing the permanent block as well, page 0 included,
 * which is a different and riskier operation than this one and is not what
 * this program does.
 *
 * ---- the one thing worth knowing before running it ----------------------
 *
 * Writing H'200004 programs the page that holds the application's interrupt
 * vector table. That is safe here because the machine is halted first and
 * the ROM masks interrupts across the page write -- but it is the reason
 * the halt is not optional, and the reason this refuses to run without it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "artista180_link.h"

/* Where the updater and its payload go, and the two entries it offers.
 * These match bernina_artista180/v5bootrom/updater.ld. */
#define UPDATER_BASE   0x3F0000UL
#define IDENTIFY_ENTRY 0x3F2000UL
#define INSTALL_ENTRY  0x3F2800UL
#define PAYLOAD_BASE   0x3F4000UL
#define PERMANENT_BASE 0x3F6000UL
#define PERMANENT_LEN  0x800UL

/* The top 64K of the 2M device, which is erased in both dumps. Lower than
 * this is not spare: the 3.01 image's application data runs to H'3EDAB2, and
 * staging over it destroys whatever was there -- silently, because a page
 * write replaces the page rather than failing. */
#define SPARE_LIMIT    0x008000UL

#define APP_ENTRY_ALT  0x200004UL

/* The two image slots in the boot flash, and the header every image carries.
 * A machine running v5 has one of these; a stock machine has its boot ROM's
 * own code there instead. */
#define SLOT_A         0x000800UL
#define SLOT_B         0x004000UL

/* Where updater.c leaves its result. The page is dumped whole and indexed,
 * because 'N' hands over 256 bytes at a time. */
#define RESULT_PAGE    0xFFF700UL
#define RESULT_STATUS  0xFFF7F0UL
#define RESULT_MFR     0xFFF7F8UL
#define RESULT_DEV     0xFFF7F9UL

/* Ask the boot ROM whether it recognises the boot flash, and write nothing.
 *
 * 'P B 00' identifies bank 0 and then offers the transfer with "OE", waiting
 * for the host to agree with 'Y'. Anything else stops it before a page is
 * programmed -- so this is the one question that can be put to the device
 * without putting any code on the machine.
 *
 * Use 'P B', not 'P S': the sector form calls download_page_a4, which sends
 * the same "OE" and then reads 256 bytes with no way to decline, leaving the
 * machine blocked until it is reset. */
static void probe_boot_flash(int fd)
{
    int c;

    note("halting");
    command(fd, 'H');

    note("asking about bank 0");
    command(fd, 'P');
    command(fd, 'B');
    put_hex(fd, 0, 2, "bank");

    c = get_byte(fd, 10.0);
    if (c == 'V') {
        note("the boot ROM does not recognise the boot flash -- it has no "
             "driver for whatever is there");
        note("nothing was written");
        return;
    }
    if (c != 'O')
        die("unexpected answer to 'P B 00': %d", c);

    c = get_byte(fd, 10.0);
    if (c != 'E')
        die("the machine offered the transfer oddly (expected 'E', got %d)",
            c);

    /* Decline, before it programs anything. */
    put_byte(fd, 'N');
    c = get_byte(fd, 10.0);
    if (c != 'N')
        note("the machine answered %d to the refusal rather than 'N'", c);

    note("the boot ROM recognises the boot flash and will program it");
    note("nothing was written. It cannot say which part it is: all three of "
         "its download streams open the same way, so --identify is what "
         "gives the maker and device bytes");
}

/* Is this machine already running a v5 boot ROM?
 *
 * It matters more than anything else this program does. An image slot is
 * only a slot on a machine that has the v5 permanent block installed; on a
 * stock machine H'000800 is the boot ROM's own code, and writing an image
 * there overwrites it. The reset vector lives below H'000800 and survives,
 * so the machine still starts and then dies at the first call into what was
 * overwritten -- H'000FB6 on a 3.01 machine, which is its host_handshake.
 *
 * Reads only. 'N' dumps 256 bytes, and an image says "B180" in its first
 * four.
 */
static int looks_like_v5(int fd)
{
    unsigned char page[256];
    int found = 0;

    dump_page(fd, SLOT_A, page);
    if (memcmp(page, "B180", 4) == 0) {
        note("slot A holds a v5 image, generation %lu",
             ((unsigned long)page[4] << 24) | ((unsigned long)page[5] << 16) |
             ((unsigned long)page[6] << 8) | (unsigned long)page[7]);
        found = 1;
    }
    dump_page(fd, SLOT_B, page);
    if (memcmp(page, "B180", 4) == 0) {
        note("slot B holds a v5 image, generation %lu",
             ((unsigned long)page[4] << 24) | ((unsigned long)page[5] << 16) |
             ((unsigned long)page[6] << 8) | (unsigned long)page[7]);
        found = 1;
    }
    return found;
}

/* The warning, and the answer.
 *
 * Read from /dev/tty rather than stdin: stdin may be the serial port itself
 * -- it is when this is driven from a relay -- and a prompt answered by the
 * machine's own chatter is not a prompt at all.
 */
static int confirmed(void)
{
    FILE *tty;
    char line[64];

    fprintf(stderr,
"\n"
"  ---------------------------------------------------------------\n"
"  This writes the boot ROM itself, and it cannot be made safe.\n"
"\n"
"  The new image goes into free space first, which is retryable.\n"
"  Then the eight pages of the permanent block are written, and\n"
"  those are where the machine's existing boot ROM lives. From the\n"
"  first of them until the last one lands -- eight page writes, on\n"
"  the order of eighty milliseconds -- the machine has no working\n"
"  boot ROM at all.\n"
"\n"
"  Lose power in that window and the machine will not start, and\n"
"  cannot be recovered over the serial link, because the thing that\n"
"  answers the serial link is what is being replaced. It would need\n"
"  the flash chip reprogrammed off-board.\n"
"\n"
"  Do not do this on mains you do not trust.\n"
"  ---------------------------------------------------------------\n"
"\n");
    fprintf(stderr, "Type Y to go ahead, N to stop: ");
    fflush(stderr);

    tty = fopen("/dev/tty", "r");
    if (tty == NULL) {
        fprintf(stderr, "\n");
        die("cannot read an answer (no terminal). Use --yes if you mean it "
            "and have read the warning above");
    }
    if (fgets(line, sizeof line, tty) == NULL) {
        fclose(tty);
        return 0;
    }
    fclose(tty);
    return line[0] == 'Y' || line[0] == 'y';
}

static void usage(void)
{
    fprintf(stderr,
        "usage: artista180_burn_bootrom [options] <port> <updater.bin> "
        "[image.bin]\n"
        "\n"
        "  <port>        the serial port the machine is on\n"
        "  <updater.bin> bernina_artista180/v5bootrom/updater.bin. Needed\n"
        "                by --identify and by an install; --probe does not\n"
        "                take it.\n"
        "  [image.bin]   the boot image to install, imageN.bin. Only for an\n"
        "                install.\n"
        "\n"
        "options:\n"
        "  --install-v5  put v5 on a machine that is not running it yet.\n"
        "              Needs updater.bin and imageB.bin, and writes the\n"
        "              boot ROM itself. Read what it prints before saying\n"
        "              yes: there is a window in which a power failure\n"
        "              leaves the machine unable to start at all.\n"
        "  --yes       do not ask. Only if you have read that warning.\n"
        "  --probe     ask the boot ROM whether it recognises the boot\n"
        "              flash. Writes nothing whatever, and needs no\n"
        "              updater. Worth doing first.\n"
        "  --identify  read the boot flash's maker and device bytes. This\n"
        "              installs no boot image, but it does write the\n"
        "              updater into spare application flash to run it.\n"
        "  -b BAUD     19200 | 38400 | 57600 | 115200 (default 115200)\n"
        "  -w SECS     how long to wait for the machine to be reset\n"
        "              (default 60)\n"
        "  -q          quiet\n"
        "\n"
        "The machine has to be reset while this waits, the same way the\n"
        "application burner catches it.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *port = NULL, *updater_path = NULL, *image_path = NULL;
    unsigned baud = 115200, brr = 0x02;
    double wait = 60.0;
    int identify = 0, probe = 0, install_v5 = 0, assume_yes = 0, i, fd;
    unsigned char *updater, *image = NULL, *permanent = NULL;
    size_t updater_len, image_len = 0, permanent_len = 0;
    const char *perm_path = "bernina_artista180/v5bootrom/permanent.bin";
    unsigned long entry;

    program_name = "artista180_burn_bootrom";
    verbose = 1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--identify") == 0) {
            identify = 1;
        } else if (strcmp(argv[i], "--probe") == 0) {
            probe = 1;
        } else if (strcmp(argv[i], "--install-v5") == 0) {
            install_v5 = 1;
        } else if (strcmp(argv[i], "--yes") == 0) {
            assume_yes = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            switch (argv[i][1]) {
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
            case 'q': verbose = 0; break;
            default: usage();
            }
        } else if (port == NULL) {
            port = argv[i];
        } else if (updater_path == NULL) {
            updater_path = argv[i];
        } else if (image_path == NULL) {
            image_path = argv[i];
        } else {
            usage();
        }
    }
    if (port == NULL) usage();
    if (probe + identify + install_v5 > 1)
        die("--probe, --identify and --install-v5 are three different "
            "operations; ask for one");
    if (probe && (updater_path != NULL || image_path != NULL))
        die("--probe runs nothing on the machine, so it takes no files");
    if (!probe && updater_path == NULL) usage();
    if (!probe && !identify && image_path == NULL)
        die("give an image to install, or --identify to only look, or "
            "--probe to only ask");
    if (identify && image_path != NULL)
        die("--identify installs no image, so it takes none");
    if (install_v5 && image_path == NULL)
        die("--install-v5 needs the boot image to install as well as the "
            "updater: imageB.bin, which is the one linked for slot B");

    entry = identify ? IDENTIFY_ENTRY
                     : install_v5 ? INSTALL_ENTRY : UPDATER_BASE;

    if (probe) {
        fd = port_open(port, BAUD_DEFAULT);
        note("%s open at %u baud", port, BAUD_DEFAULT);
        wait_for_boot(fd, wait, 1,
                      "waiting for the machine to announce itself -- reset "
                      "it now");
        probe_boot_flash(fd);
        close(fd);
        return 0;
    }

    updater = read_image(updater_path, &updater_len, SPARE_LIMIT);
    note("%s: %lu bytes, to H'%06lX", updater_path,
         (unsigned long)updater_len, UPDATER_BASE);
    if (image_path != NULL) {
        image = read_image(image_path, &image_len, SPARE_LIMIT);
        if (image_len < 0x40 || memcmp(image, "B180", 4) != 0)
            die("%s has no B180 header -- is it one of the v5 images?",
                image_path);
        note("%s: %lu bytes, to H'%06lX", image_path,
             (unsigned long)image_len, PAYLOAD_BASE);
    }

    if (install_v5) {
        /* The image has to be the one linked for slot B: on a stock machine
         * that is the only free space, and an image linked for slot A would
         * be written to an address it cannot run at. Its header says where
         * it expects to be. */
        const unsigned long want = (unsigned long)image[0x10] << 24 |
                                   (unsigned long)image[0x11] << 16 |
                                   (unsigned long)image[0x12] << 8 |
                                   (unsigned long)image[0x13];
        if (want < SLOT_B || want >= SLOT_B + 0x4000UL)
            die("%s is linked to run at H'%06lX, which is not slot B. A "
                "first install needs imageB.bin", image_path, want);

        permanent = read_image(perm_path, &permanent_len, PERMANENT_LEN);
        if (permanent_len != PERMANENT_LEN)
            die("%s is %lu bytes; the permanent block is exactly H'%lX",
                perm_path, (unsigned long)permanent_len, PERMANENT_LEN);
        note("%s: %lu bytes, to H'%06lX", perm_path,
             (unsigned long)permanent_len, PERMANENT_BASE);

        if (!assume_yes && !confirmed())
            die("stopped. Nothing was written");
    }

    fd = port_open(port, BAUD_DEFAULT);
    note("%s open at %u baud", port, BAUD_DEFAULT);

    wait_for_boot(fd, wait, 1,
                  "waiting for the machine to announce itself -- reset it now");

    if (baud != BAUD_DEFAULT) raise_rate(fd, port, baud, brr);

    /* Halt before touching the flash. The application is running out of the
     * device about to be written, and its vectors are in the page that the
     * H'200004 write programs. */
    note("halting");
    command(fd, 'H');

    /* Before a byte goes anywhere. --identify only ever runs the identify
     * entry, which reads the boot flash and writes none of it, so it is
     * allowed on any machine; an install is not. */
    if (!identify) {
        int is_v5;
        note("looking at what is in the boot flash");
        is_v5 = looks_like_v5(fd);
        if (!install_v5 && !is_v5)
            die("neither boot flash slot holds a v5 image, so this machine "
                "is not running a v5 boot ROM. Installing one would write "
                "over the boot ROM that is there -- H'000800 is its own code "
                "on a stock machine, not a spare slot. Use --install-v5 if "
                "that is what you meant. Nothing was written.");
        /* And the other way about: a first install writes slot B and the
         * permanent block, and on a machine already running v5 slot B may
         * be the image it is running out of. */
        if (install_v5 && is_v5)
            die("this machine is already running v5, and --install-v5 would "
                "write over slot B, which may be the image it is running. "
                "Install an image the ordinary way instead. Nothing was "
                "written.");
    }

    note("streaming the updater");
    burn(fd, UPDATER_BASE, updater, updater_len);
    if (image != NULL) {
        note("streaming the image");
        burn(fd, PAYLOAD_BASE, image, image_len);
    }
    if (permanent != NULL) {
        note("streaming the permanent block");
        burn(fd, PERMANENT_BASE, permanent, permanent_len);
    }

    /* Check what went down the wire before handing control to it. */
    {
        unsigned long want = sum_of(updater, updater_len);
        unsigned long got = checksum(fd, UPDATER_BASE, updater_len);
        if (want != got)
            die("the updater did not land: sent H'%08lX, the machine has "
                "H'%08lX", want, got);
        note("updater checksum H'%08lX matches", got);
    }
    if (image != NULL) {
        unsigned long want = sum_of(image, image_len);
        unsigned long got = checksum(fd, PAYLOAD_BASE, image_len);
        if (want != got)
            die("the image did not land: sent H'%08lX, the machine has "
                "H'%08lX", want, got);
        note("image checksum H'%08lX matches", got);
    }
    if (permanent != NULL) {
        unsigned long want = sum_of(permanent, permanent_len);
        unsigned long got = checksum(fd, PERMANENT_BASE, permanent_len);
        if (want != got)
            die("the permanent block did not land: sent H'%08lX, the "
                "machine has H'%08lX", want, got);
        note("permanent block checksum H'%08lX matches", got);
    }

    /* Point 'G' at the updater. Four bytes, in the page the ROM buffers and
     * programs when the stream ends. */
    note("pointing H'%06lX at H'%06lX", APP_ENTRY_ALT, entry);
    {
        unsigned char be[4];
        be[0] = (unsigned char)(entry >> 24);
        be[1] = (unsigned char)(entry >> 16);
        be[2] = (unsigned char)(entry >> 8);
        be[3] = (unsigned char)entry;
        burn(fd, APP_ENTRY_ALT, be, sizeof be);
    }

    note("go");
    command(fd, 'G');

    /* The updater says nothing on the wire -- it masks interrupts, works,
     * and hands back to the loader. But handing back means the machine boots
     * again and announces itself, so the answer can be fetched out of the
     * RAM it left it in. It comes back at the default rate, whatever the
     * link was raised to. */
    note("waiting for the machine to come back");
    port_configure(fd, BAUD_DEFAULT);
    tcflush(fd, TCIFLUSH);
    wait_for_boot(fd, 120.0, 1,
                  "waiting for the updater to finish and the machine to "
                  "restart");

    {
        unsigned char page[256];
        unsigned status, mfr, dev;

        dump_page(fd, RESULT_PAGE, page);
        status = page[RESULT_STATUS & 0xFF];
        mfr = page[RESULT_MFR & 0xFF];
        dev = page[RESULT_DEV & 0xFF];

        if (identify) {
            if (status != 0x1D)
                die("the identify did not run: status H'%02X", status);
            note("boot flash: manufacturer H'%02X, device H'%02X", mfr, dev);
            if (mfr == 0x1F && dev == 0xA4) {
                note("that is the Atmel part the updater programs a page at "
                     "a time");
            } else {
                note("that is NOT the part this was written for -- do not "
                     "install anything until it is understood");
            }
        } else {
            switch (status) {
            case 0x5A:
                note("installed: the inactive slot holds the new image and "
                     "the machine has restarted on it");
                break;
            case 0x02:
                die("the updater could not program the boot flash "
                    "(status H'02); the old image is still what runs");
            case 0x03:
                die("the updater programmed but could not verify "
                    "(status H'03)");
            case 0x2D:
                note("v5 installed: the permanent block and an image are in "
                     "the boot flash, and the machine has restarted on them");
                break;
            case 0x1E:
                die("the updater refused: stage-0 had not named an active "
                    "slot, so this machine is not running v5. Nothing was "
                    "written to the boot flash.");
            default:
                die("the updater did not finish: status H'%02X", status);
            }
        }
    }

    close(fd);
    free(updater);
    free(image);
    free(permanent);
    return 0;
}
