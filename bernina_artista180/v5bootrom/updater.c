/* Reprogram one image slot of the boot flash, from code running in the
 * application flash.
 *
 * The boot ROM cannot do this to itself. Its 'M' and 'P' commands take an
 * address from the host and neither checks it, so both will happily aim a
 * page-program at bank 0 -- and the moment the command latches, the device
 * answers reads with toggling status instead of data. The next instruction
 * fetch comes back as status, and the machine is gone. Nothing in the ROM
 * guards against this; tool/boot_selfwrite.dart demonstrates it.
 *
 * The way out is not to run from RAM but to run from the *other* device.
 * The boot flash is on bus area 0 and the application flash on area 1, so
 * while area 0 is busy the CPU can keep fetching instructions, and the new
 * image, out of area 1. This file is linked into the spare space at the top
 * of the application flash and reached with 'G', which jumps to the longword
 * at H'200004.
 *
 * Interrupts are masked for the whole update: the vector table is in area 0,
 * so a vector fetch during a page write would read status too. NMI cannot be
 * masked -- see the README.
 */

#include "h8_3003.h"

/* Where the pieces live. The payload is a complete image, header and all,
 * placed by the test harness (on a real machine, by the boot ROM's own 'P'
 * command, which is safe here because it targets area 1). */
/* The top 64K of the application device.
 *
 * Not H'3E8000, which is where these used to sit: that is inside what the
 * 3.01 image actually uses -- its application data runs to H'3EDAB2 -- so
 * staging here overwrote real data and only looked harmless because a page
 * write replaces whatever was there. Both dumps have H'3F0000 upwards
 * erased. */
#define UPDATER_BASE    0x003F0000UL
#define IDENTIFY_BASE   0x003F2000UL   /* the identify entry, see below */
#define INSTALL_BASE    0x003F2800UL   /* the first-install entry */
#define PAYLOAD         0x003F4000UL
#define PERMANENT_SRC   0x003F6000UL   /* the permanent block, for a first
                                          install */
#define PERMANENT_LEN   0x800UL        /* vectors, trampolines, stage-0 */

#define IMAGE_A         0x00000800UL
#define IMAGE_B         0x00004000UL
#define PAGE            0x100

/* Published by stage-0 in the permanent block. */
#define ACTIVE_BASE     REG32(0x00FFF710UL)
#define STAGE0_ENTRY    REG32(0x00FFFD0CUL)

/* Somewhere the harness can read the outcome without a serial port. */
#define STATUS          REG8(0x00FFF7F0UL)
#define PAGES_DONE      REG32(0x00FFF7F4UL)
#define IDENT_MFR       REG8(0x00FFF7F8UL)
#define IDENT_DEV       REG8(0x00FFF7F9UL)

#define ST_RUNNING      0x01
#define ST_PROGRAM_FAIL 0x02
#define ST_VERIFY_FAIL  0x03
#define ST_DONE         0x5A
#define ST_IDENTIFIED   0x1D
#define ST_NOT_V5       0x1E
#define ST_INSTALLED_V5 0x2D

/* The unlock cells of the boot device. Only the low fifteen address lines
 * reach it, which is exactly what these offsets need. */
#define UNLOCK_A        REG8(0x00005555UL)
#define UNLOCK_B        REG8(0x00002AAAUL)

static void mask_interrupts(void)
{
    __asm__ __volatile__("orc #0xC0, ccr" ::: "cc");
}

/* DQ6 toggles on every read while a write is running, so two reads that
 * agree mean it has finished. Bounded, so a dead device cannot wedge us. */
static int wait_ready(u32 addr)
{
    volatile u8 *p = (volatile u8 *)addr;
    u32 tries;

    for (tries = 0; tries < 2000000UL; tries++) {
        u8 a = *p;
        u8 b = *p;
        if (a == b) return 1;
    }
    return 0;
}

/* One 256-byte page. On an AT29C-style part the page write erases the page
 * itself, so there is no separate erase step and no window in which the page
 * is blank. */
static int program_page(u32 dst, const u8 *src)
{
    u16 i;

    UNLOCK_A = 0xAA;
    UNLOCK_B = 0x55;
    UNLOCK_A = 0xA0;
    for (i = 0; i < PAGE; i++) {
        ((volatile u8 *)dst)[i] = src[i];
    }
    return wait_ready(dst);
}

/* A short spin, for the microseconds the part needs to answer the autoselect
 * command. The boot ROM's delay() would do, but it lives in bank 0 and this
 * is the one routine that has bank 0 in a mode where the low two addresses
 * do not read as code. Keeping the wait local avoids the question. */
/* One byte from the bottom of bank 0.
 *
 * Written the long way round because the two addresses wanted are 0 and 1,
 * and a literal load from address 0 is undefined behaviour the compiler is
 * entitled to turn into a trap -- which it did, leaving a call to abort in
 * the middle of the identify. Taking the base through a volatile stops it
 * being folded back to a constant. */
static u8 read_bank0(u16 offset)
{
    volatile u32 base = 0;

    return *(volatile u8 *)(base + offset);
}

static void spin(u16 n)
{
    volatile u16 i;

    while (n-- != 0) {
        for (i = 0; i < 600; i++) { }
    }
}

/* Identify the boot flash, and program nothing.
 *
 * This is the check worth making before an update: the boot ROM's own
 * flash_identify is static, and every command that reaches it goes on to
 * program the device. 'P B 00' can be refused before it writes, but all
 * three of the ROM's download streams open with the same "OE", so the reply
 * only says "the ROM will drive this part", not which part it is.
 *
 * Running from bank 1 makes the question easy. Autoselect leaves the array
 * readable everywhere except the two low addresses, so the risk here is not
 * the mode itself -- it is that the vector table is at those low addresses.
 * An interrupt taken while bank 0 is in autoselect would fetch its vector
 * and get the manufacturer and device bytes instead. Hence interrupts off
 * for the whole of it, and the device put back into read mode before they
 * come on again.
 *
 * Entered by pointing H'200004 at IDENTIFY_BASE and sending 'G'. Afterwards
 * the two bytes are at H'FFF7F8 and H'FFF7F9, which the boot ROM's 'N'
 * command will dump.
 */
__attribute__((section(".text.identify")))
void updater_identify(void)
{
    u8 mfr, id;

    mask_interrupts();
    STATUS = ST_RUNNING;

    UNLOCK_A = 0xAA;
    UNLOCK_B = 0x55;
    UNLOCK_A = 0x90;              /* autoselect */
    spin(11);

    mfr = read_bank0(0);
    id = read_bank0(1);

    UNLOCK_A = 0xAA;
    UNLOCK_B = 0x55;
    UNLOCK_A = 0xF0;              /* and back to reading the array */
    spin(11);

    IDENT_MFR = mfr;
    IDENT_DEV = id;
    STATUS = ST_IDENTIFIED;

    /* Nothing has been written, so the machine can simply carry on. */
    {
        void (*reset)(void) = (void (*)(void))STAGE0_ENTRY;
        reset();
    }
}

/* Pinned to the start of the section so that the entry really is at
 * UPDATER_BASE. Without this the linker is free to emit program_page first,
 * and 'G' jumps into the middle of the page writer with whatever happens to
 * be in the argument registers -- which in the simulator wrote pages of
 * rubbish over the *other* slot and left the machine with no valid image at
 * all. The one thing this whole design exists to prevent, caused by a
 * section ordering. */
/* Put v5 on a machine that is not running it yet.
 *
 * This is the one operation that cannot be made safe, and the ordering is
 * the whole of what makes it as safe as it can be:
 *
 *   1. The image goes into slot B at H'004000 first. On a stock machine
 *      that is free space -- the 3.01 ROM ends at H'0023FF and nothing
 *      boots from H'004000 -- so this part is fully retryable. The old ROM
 *      is untouched and still runs.
 *
 *   2. Then pages 1-7 of the permanent block. This is where the old ROM
 *      starts to go: its interrupt trampolines are at H'000100 and its boot
 *      code at H'000400. From here on the machine has no working ROM.
 *
 *   3. Then page 0, the vector table, which is what makes any of it run.
 *
 * The window is between the first write of step 2 and the end of step 3:
 * eight page writes, on the order of eighty milliseconds. Lose power in it
 * and the machine has neither ROM and will not start at all -- there is no
 * fallback, because the fallback *is* what step 2 overwrites.
 *
 * Note the image installed here must be linked for slot B. Its header's
 * entry field says where it expects to be, and the host checks that.
 */
__attribute__((section(".text.install")))
void updater_install_v5(void)
{
    const u8 *img = (const u8 *)PAYLOAD;
    const u8 *perm = (const u8 *)PERMANENT_SRC;
    u32 total, done;

    mask_interrupts();
    STATUS = ST_RUNNING;
    PAGES_DONE = 0;

    total = (((u32)img[8] << 24) | ((u32)img[9] << 16) |
             ((u32)img[10] << 8) | (u32)img[11]) + 0x40;
    total = (total + (PAGE - 1)) & ~(u32)(PAGE - 1);

    /* 1. The image, body first and header last, into the free slot. */
    for (done = PAGE; done < total; done += PAGE) {
        if (!program_page(IMAGE_B + done, img + done)) {
            STATUS = ST_PROGRAM_FAIL;
            for (;;) { }
        }
        PAGES_DONE = done / PAGE;
    }
    if (!program_page(IMAGE_B, img)) {
        STATUS = ST_PROGRAM_FAIL;
        for (;;) { }
    }
    for (done = 0; done < total; done++) {
        if (((const volatile u8 *)IMAGE_B)[done] != img[done]) {
            STATUS = ST_VERIFY_FAIL;
            for (;;) { }
        }
    }

    /* 2. Pages 1-7. The old ROM goes here; there is no way back after this
     *    until step 3 lands. */
    for (done = PAGE; done < PERMANENT_LEN; done += PAGE) {
        if (!program_page(done, perm + done)) {
            STATUS = ST_PROGRAM_FAIL;
            for (;;) { }
        }
        PAGES_DONE = (total / PAGE) + (done / PAGE);
    }

    /* 3. Page 0, and the machine is v5. */
    if (!program_page(0, perm)) {
        STATUS = ST_PROGRAM_FAIL;
        for (;;) { }
    }
    for (done = 0; done < PERMANENT_LEN; done++) {
        if (read_bank0((u16)done) != perm[done]) {
            STATUS = ST_VERIFY_FAIL;
            for (;;) { }
        }
    }

    STATUS = ST_INSTALLED_V5;

    /* Start what was just written, by way of its own reset vector. */
    {
        const u32 entry = ((u32)read_bank0(0) << 24) |
                          ((u32)read_bank0(1) << 16) |
                          ((u32)read_bank0(2) << 8) | (u32)read_bank0(3);
        void (*go)(void) = (void (*)(void))entry;
        go();
    }
}

__attribute__((section(".text.entry")))
void updater_main(void)
{
    const u8 *src = (const u8 *)PAYLOAD;
    u32 active, target, total, done;

    mask_interrupts();
    STATUS = ST_RUNNING;
    PAGES_DONE = 0;

    /* Write whichever slot is not the one we are running under.
     *
     * ACTIVE_BASE is published by stage-0, so it names a slot only on a
     * machine that is already running v5. Anywhere else it holds whatever
     * happened to be in that RAM, and the choice below would fall through
     * to slot A -- which on a stock machine is not a spare slot at all but
     * the boot ROM's own code, from H'000800 upwards. Writing it overwrites
     * the running ROM: the reset vector below H'000800 survives, so the
     * machine still starts, and then dies at the first call into what was
     * overwritten.
     *
     * There is no version of that worth risking, so refuse instead. */
    active = ACTIVE_BASE;
    if (active != IMAGE_A && active != IMAGE_B) {
        STATUS = ST_NOT_V5;
        for (;;) { }
    }
    target = (active == IMAGE_A) ? IMAGE_B : IMAGE_A;

    /* The payload's own header says how long it is. Round up to a page. */
    total = (((u32)src[8] << 24) | ((u32)src[9] << 16) |
             ((u32)src[10] << 8) | (u32)src[11]) + 0x40;
    total = (total + (PAGE - 1)) & ~(u32)(PAGE - 1);

    /* The body first, the header page last.
     *
     * The header is what makes a slot valid: magic, generation, length and
     * the checksum over everything after it. Writing it last means the slot
     * goes from "not an image" to "the new image" in a single 256-byte page
     * write, with nothing in between. Stop the machine anywhere before that
     * final write and the slot still has no valid header, so stage-0 ignores
     * it and boots the image in the other slot, which was never touched.
     *
     * Either order is safe, because the other slot stays valid throughout.
     * This one is tidier: there is no window in which a slot claims to be an
     * image and is not one. */
    for (done = PAGE; done < total; done += PAGE) {
        if (!program_page(target + done, src + done)) {
            STATUS = ST_PROGRAM_FAIL;
            for (;;) { }
        }
        PAGES_DONE = done / PAGE;
    }
    if (!program_page(target, src)) {
        STATUS = ST_PROGRAM_FAIL;
        for (;;) { }
    }
    PAGES_DONE = total / PAGE;

    /* Read it back. The device is idle by now, so these are ordinary reads. */
    for (done = 0; done < total; done++) {
        if (((const volatile u8 *)target)[done] != src[done]) {
            STATUS = ST_VERIFY_FAIL;
            for (;;) { }
        }
    }

    STATUS = ST_DONE;

    /* Hand back to the loader, which will now pick the new image because its
     * generation is higher. Nothing has to be told which slot to run. */
    {
        void (*reset)(void) = (void (*)(void))STAGE0_ENTRY;
        reset();
    }
}
