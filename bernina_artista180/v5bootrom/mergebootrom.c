/* mergebootrom -- put a compiled boot ROM into a full memory dump.
 *
 * The simulator loads a picture of the whole 16M address space. To try a
 * rebuilt boot ROM in it, the ROM's bytes have to replace the ones the dump
 * captured. This does that and writes a new dump, leaving the original
 * untouched.
 *
 *   mergebootrom -b bootrom.bin -m dump.bin -o merged.bin
 *
 * Two details are not just a memcpy, and getting either wrong leaves a dump
 * that looks right and behaves oddly:
 *
 *  - The boot flash is a 32K device, and the rest of it is erased when it is
 *    programmed. A ROM shorter than the one it replaces would otherwise
 *    leave the tail of the old one behind, still reachable through a stale
 *    pointer. Everything past the new ROM is therefore filled with H'FF.
 *
 *  - Only the low fifteen address lines reach that device, so its contents
 *    repeat every 32K up to H'020000. The dump captured all four copies; a
 *    merged dump has to carry the new ROM in all four, or reads through the
 *    aliases return the old code.
 *
 * This is a host program: build it with the system compiler, not the H8
 * cross-compiler.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The boot flash, as read off a full dump: 32K at address 0, its contents
 * repeating up to H'020000 because the decode is fifteen bits wide. */
#define BOOT_BASE     0x000000UL
#define BOOT_SIZE     0x008000UL
#define ALIAS_LIMIT   0x020000UL
#define ERASED        0xFF

static void usage(FILE *out, const char *argv0)
{
    fprintf(out,
        "usage: %s -b <bootrom.bin> -m <memory dump> -o <merged dump>\n"
        "\n"
        "  -b FILE  compiled boot ROM, raw binary\n"
        "  -m FILE  full memory dump to merge it into (not modified)\n"
        "  -o FILE  merged dump to write\n"
        "  -h       this message\n",
        argv0);
}

/* Reads a whole file. Returns its bytes and sets *len, or NULL on failure,
 * having already reported why. */
static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *buf;

    if (!f) {
        fprintf(stderr, "mergebootrom: cannot open %s: %s\n",
                path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fprintf(stderr, "mergebootrom: cannot size %s\n", path);
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = malloc((size_t)size ? (size_t)size : 1);
    if (!buf) {
        fprintf(stderr, "mergebootrom: out of memory reading %s\n", path);
        fclose(f);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "mergebootrom: short read on %s\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return buf;
}

int main(int argc, char **argv)
{
    const char *boot_path = NULL, *dump_path = NULL, *out_path = NULL;
    unsigned char *boot = NULL, *dump = NULL;
    size_t boot_len = 0, dump_len = 0;
    unsigned long copies = 0, addr;
    FILE *out;
    int i, status = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(a, "-b") && strcmp(a, "-m") && strcmp(a, "-o")) {
            fprintf(stderr, "mergebootrom: unknown option %s\n", a);
            usage(stderr, argv[0]);
            return 2;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "mergebootrom: %s needs a filename\n", a);
            usage(stderr, argv[0]);
            return 2;
        }
        if (!strcmp(a, "-b")) boot_path = argv[++i];
        else if (!strcmp(a, "-m")) dump_path = argv[++i];
        else out_path = argv[++i];
    }

    if (!boot_path || !dump_path || !out_path) {
        fprintf(stderr, "mergebootrom: -b, -m and -o are all required\n");
        usage(stderr, argv[0]);
        return 2;
    }

    boot = read_file(boot_path, &boot_len);
    if (!boot) goto done;
    dump = read_file(dump_path, &dump_len);
    if (!dump) goto done;

    if (boot_len > BOOT_SIZE) {
        fprintf(stderr, "mergebootrom: %s is %lu bytes; the boot flash holds "
                "%lu\n", boot_path, (unsigned long)boot_len,
                (unsigned long)BOOT_SIZE);
        goto done;
    }
    if (dump_len < ALIAS_LIMIT) {
        fprintf(stderr, "mergebootrom: %s is %lu bytes; too short to be a "
                "memory dump (need at least %lu)\n", dump_path,
                (unsigned long)dump_len, (unsigned long)ALIAS_LIMIT);
        goto done;
    }

    /* Program the device, then repeat it through its aliases. */
    for (addr = BOOT_BASE; addr < ALIAS_LIMIT; addr += BOOT_SIZE) {
        if (addr + BOOT_SIZE > dump_len) break;
        memcpy(dump + addr, boot, boot_len);
        memset(dump + addr + boot_len, ERASED, (size_t)(BOOT_SIZE - boot_len));
        copies++;
    }

    out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "mergebootrom: cannot write %s\n", out_path);
        goto done;
    }
    if (fwrite(dump, 1, dump_len, out) != dump_len) {
        fprintf(stderr, "mergebootrom: short write on %s\n", out_path);
        fclose(out);
        goto done;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "mergebootrom: error closing %s\n", out_path);
        goto done;
    }

    printf("merged %lu bytes of %s into %lu-byte %s\n",
           (unsigned long)boot_len, boot_path,
           (unsigned long)dump_len, out_path);
    printf("  boot flash H'%06lX-H'%06lX, %lu bytes erased past the ROM, "
           "%lu copies written\n",
           BOOT_BASE, BOOT_BASE + BOOT_SIZE - 1,
           (unsigned long)(BOOT_SIZE - boot_len), copies);
    status = 0;

done:
    free(boot);
    free(dump);
    return status;
}
