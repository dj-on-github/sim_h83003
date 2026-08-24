/* mergeapp -- put a compiled application into a full memory dump.
 *
 *   mergeapp -a app.bin -m dump.bin -o merged.bin
 *
 * The application is being rebuilt a piece at a time, so this splices only
 * as far as the built image reaches and leaves the rest of the dump alone.
 * That matters more than it sounds: above the rebuilt part sits the
 * original's own machine code, still at its original addresses, and above
 * that H'280000 upwards is 1.4M of pattern data that is not code at all.
 * Overwriting or eliding either would leave an image that looks plausible
 * and behaves nothing like the machine.
 *
 * The image is placed at its link address, H'200000, which is where the
 * entry table has to be for the boot ROM to find it.
 *
 * This is a host program: build it with the system compiler, not the H8
 * cross-compiler.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_BASE      0x200000UL   /* where the entry table lives      */
#define CODE_LIMIT    0x251000UL   /* above this is data, not code     */
#define DATA_TOP      0x3EE000UL   /* the last of the pattern data     */

static void usage(FILE *out, const char *argv0)
{
    fprintf(out,
        "usage: %s -a <app.bin> -m <memory dump> -o <merged dump>\n"
        "\n"
        "  -a FILE  compiled application, raw binary, linked at H'200000\n"
        "  -m FILE  full memory dump to merge it into (not modified)\n"
        "  -o FILE  merged dump to write\n"
        "  -h       this message\n",
        argv0);
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *buf;

    if (!f) {
        fprintf(stderr, "mergeapp: cannot open %s: %s\n",
                path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fprintf(stderr, "mergeapp: cannot size %s\n", path);
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = malloc((size_t)size ? (size_t)size : 1);
    if (!buf) {
        fprintf(stderr, "mergeapp: out of memory reading %s\n", path);
        fclose(f);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "mergeapp: short read on %s\n", path);
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
    const char *app_path = NULL, *dump_path = NULL, *out_path = NULL;
    unsigned char *app = NULL, *dump = NULL;
    size_t app_len = 0, dump_len = 0;
    FILE *out;
    int i, status = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(a, "-a") && strcmp(a, "-m") && strcmp(a, "-o")) {
            fprintf(stderr, "mergeapp: unknown option %s\n", a);
            usage(stderr, argv[0]);
            return 2;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "mergeapp: %s needs a filename\n", a);
            usage(stderr, argv[0]);
            return 2;
        }
        if (!strcmp(a, "-a")) app_path = argv[++i];
        else if (!strcmp(a, "-m")) dump_path = argv[++i];
        else out_path = argv[++i];
    }

    if (!app_path || !dump_path || !out_path) {
        fprintf(stderr, "mergeapp: -a, -m and -o are all required\n");
        usage(stderr, argv[0]);
        return 2;
    }

    app = read_file(app_path, &app_len);
    if (!app) goto done;
    dump = read_file(dump_path, &dump_len);
    if (!dump) goto done;

    if (dump_len < DATA_TOP) {
        fprintf(stderr, "mergeapp: %s is %lu bytes; too short to be a memory "
                "dump (need at least %lu)\n", dump_path,
                (unsigned long)dump_len, DATA_TOP);
        goto done;
    }
    if (APP_BASE + app_len > CODE_LIMIT) {
        fprintf(stderr, "mergeapp: %s reaches H'%06lX, past the end of the "
                "code region at H'%06lX\n", app_path,
                APP_BASE + (unsigned long)app_len, CODE_LIMIT);
        goto done;
    }

    memcpy(dump + APP_BASE, app, app_len);

    out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "mergeapp: cannot write %s\n", out_path);
        goto done;
    }
    if (fwrite(dump, 1, dump_len, out) != dump_len) {
        fprintf(stderr, "mergeapp: short write on %s\n", out_path);
        fclose(out);
        goto done;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "mergeapp: error closing %s\n", out_path);
        goto done;
    }

    printf("merged %lu bytes of %s into %lu-byte %s\n",
           (unsigned long)app_len, app_path,
           (unsigned long)dump_len, out_path);
    printf("  application H'%06lX-H'%06lX; H'%06lX upwards left as it was\n",
           APP_BASE, APP_BASE + (unsigned long)app_len - 1,
           APP_BASE + (unsigned long)app_len);
    status = 0;

done:
    free(app);
    free(dump);
    return status;
}
