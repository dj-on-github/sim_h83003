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
#define UPDATER_BASE    0x003E8000UL
#define IDENTIFY_BASE   0x003E9000UL   /* the identify entry, see below */
#define PAYLOAD         0x003EA000UL

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
__attribute__((section(".text.entry")))
void updater_main(void)
{
    const u8 *src = (const u8 *)PAYLOAD;
    u32 active, target, total, done;

    mask_interrupts();
    STATUS = ST_RUNNING;
    PAGES_DONE = 0;

    /* Write whichever slot is not the one we are running under. */
    active = ACTIVE_BASE;
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
