/* Talking to an artista 180 over its serial link: the port, the boot ROM's
 * download protocol, and the file reading around it.
 *
 * Both host tools drive the same nineteen-command protocol; this is the part
 * they share. artista180_burn_application.c carries the description of the
 * sequence and of the reset window the machine has to be caught in.
 */
#ifndef ARTISTA180_LINK_H
#define ARTISTA180_LINK_H

#include <stddef.h>
#include <termios.h>

#define APP_BASE   0x200000UL   /* where the application lives in flash */
#define APP_LIMIT  0x200000UL   /* and how much room there is for it */
#define PAGE_SIZE  0x100UL

#define BRR_DEFAULT 0x11        /* 19200 at phi = 11059200 */
#define BAUD_DEFAULT 19200

/* The rates the machine can be asked for, and the divisor that picks each.
 * baud = phi / (32 * (BRR + 1)) with phi = 11059200, exact for all of them. */
struct rate { unsigned baud; unsigned brr; };
extern const struct rate RATES[];
extern const int NRATES;

extern const char HANDSHAKE[2];
extern const char BOOT_STRING[3];

/* Progress goes to stderr while this is set. */
extern int verbose;

/* Set by each tool's main, so die() names the right one. */
extern const char *program_name;

void die(const char *fmt, ...);
void note(const char *fmt, ...);

/* ---- the port ---------------------------------------------------------- */
speed_t speed_of(unsigned baud);
void port_configure(int fd, unsigned baud);
int port_open(const char *name, unsigned baud);

/* ---- bytes ------------------------------------------------------------- */
double now_seconds(void);
void put_bytes(int fd, const void *p, size_t n);
void put_byte(int fd, int c);
int get_byte(int fd, double timeout);

/* Sends one character and waits for the machine to echo it, which is the
 * protocol's flow control. */
void put_echoed(int fd, int c, const char *what);

/* Sends [digits] hex digits of [v], each echoed. */
void put_hex(int fd, unsigned long v, int digits, const char *what);

/* ---- the protocol ------------------------------------------------------ */

/* Waits for "BOS" and answers the handshake. [expect_status] asks for the
 * status byte the ROM sends once it has the link. [waiting_for] is what to
 * tell the user: only the first call is waiting on a reset, and the one
 * inside raise_rate is waiting on a re-announcement at the new rate. */
void wait_for_boot(int fd, double timeout, int expect_status,
                   const char *waiting_for);

/* 'N': 256 raw bytes from [addr] into [out], which must have room. */
void dump_page(int fd, unsigned long addr, unsigned char *out);

/* One command letter, echoed. */
void command(int fd, int letter);

/* 'J': raises the link's rate, both ends. */
void raise_rate(int fd, const char *port, unsigned baud, unsigned brr);

/* 'M': streams [len] bytes to [base] as hex pairs, then ends the stream. */
void burn(int fd, unsigned long base, const unsigned char *image, size_t len);

/* 'L': the machine's own sum over a range. */
unsigned long checksum(int fd, unsigned long base, unsigned long len);

/* The same sum computed here, to compare against it. */
unsigned long sum_of(const unsigned char *p, size_t n);

/* ---- files ------------------------------------------------------------- */
unsigned char *read_image(const char *path, size_t *out_len,
                          unsigned long limit);

#endif /* ARTISTA180_LINK_H */
