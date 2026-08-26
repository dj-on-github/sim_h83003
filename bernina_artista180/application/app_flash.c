/* The artista 180 application, rebuilt in C: the item index, the
 * configuration block, and writing the settings back.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

/* ---- the item index ----------------------------------------------------
 * H'2120A2. Builds the lists the menus are drawn from.
 *
 * There is a table of descriptors, H'18 bytes each, at the address held in
 * H'114DD2 -- H'500000 on this machine. Byte H'17 of each is its category,
 * and the first two longwords are two pointers to its data; which of the two
 * is the live one depends on the configuration byte, so a machine without
 * the embroidery module sees a different set.
 *
 * The table is walked four times, once per list, taking a different range of
 * categories each time:
 *
 *   below H'10, H'12 and above, exactly H'11, exactly H'10
 *
 * Each list is a count followed by that many table indices, and each is laid
 * down immediately after the one before, with its address left in a chain of
 * pointers for whoever draws from it.
 *
 * Two things about the walk are easy to get wrong, and both were:
 *
 *  - Category 2 is not an entry to pass over, it is where the table ends.
 *    On this machine that is index 959, so the walk stops well short of the
 *    1024 the loop bound allows, and the stale entries beyond it stay out of
 *    the lists.
 *  - Entries are grouped in fives -- a menu row -- and a change of category
 *    pads out to a row boundary before starting the next. Category 1 is
 *    never listed at all.
 */
static int category_wanted(u8 category, int list)
{
    switch (list) {
    case LIST_BELOW_10:  return category < 0x10;
    case LIST_12_AND_UP: return category >= 0x12;
    case LIST_EXACTLY_11: return category == 0x11;
    default:             return category == 0x10;
    }
}

static int item_wanted(u32 table, int i, u8 category, int list)
{
    const u32 entry = table + (u32)ITEM_STRIDE * (u32)i;

    if (category == 0) return 0;
    if (!category_wanted(category, list)) return 0;

    /* Which of the two pointers counts depends on how the machine is
     * configured; an entry with no data behind it is not listed. */
    return (CONFIG_BLOCK == CONFIG_ALT_POINTER ? REG32(entry)
                                               : REG32(entry + 4)) != 0;
}

static u16 build_item_list(u32 list, int which)
{
    const u32 table = ITEM_TABLE;
    u16 count = 0;
    u8 previous = REG8(table + ITEM_CATEGORY);   /* entry zero's category */
    int i;

    for (i = 0; i < ITEM_LIMIT; i++) {
        const u8 category =
            REG8(table + (u32)ITEM_STRIDE * (u32)i + ITEM_CATEGORY);

        if (category == ITEM_CAT_END) break;
        if (!item_wanted(table, i, category, which)) continue;

        if (category == previous) {
            if (category != ITEM_CAT_UNLISTED) {
                count++;
                REG16(list + 2 * count) = (u16)i;
            }
        } else if (category == ITEM_CAT_UNLISTED ||
                   previous == ITEM_CAT_UNLISTED) {
            /* A change involving category 1 starts nothing; only a run that
             * was already category 1 continues into the new one. */
            if (previous == ITEM_CAT_UNLISTED) {
                count++;
                REG16(list + 2 * count) = (u16)i;
                previous = category;
            }
        } else {
            const u16 used = count % MENU_ROW;
            if (used != 0) {
                u16 pad;
                for (pad = 1; (MENU_ROW - used) >= pad; pad++) {
                    count++;
                    REG16(list + 2 * count) = 0;
                }
            }
            count++;
            REG16(list + 2 * count) = (u16)i;
            previous = category;
        }
    }

    REG16(list) = count;
    return count;
}

void scan_items(void)
{
    u32 list = MENU_LIST;

    list += 2 * (u32)(build_item_list(list, LIST_BELOW_10) + 1);
    MENU_LIST_2_PTR = list;

    list += 2 * (u32)(build_item_list(list, LIST_12_AND_UP) + 1);
    MENU_LIST_3_PTR = list;

    list += 2 * (u32)(build_item_list(list, LIST_EXACTLY_11) + 1);
    MENU_LIST_4_PTR = list;

    build_item_list(list, LIST_EXACTLY_10);
}

/* H'2105C4. Brings the display up: the tables it draws from, the controller
 * itself, the buffers cleared, and the touch calibration moved out of flash
 * into RAM where the rest of the code reads it. */
void display_init(void)
{
    TABLE_SLOT(0) = TABLE_114DE6;
    TABLE_SLOT(1) = TABLE_114FDA;
    TABLE_SLOT(2) = TABLE_11518E;
    TABLE_SLOT(3) = TABLE_1151A2;
    TABLE_SLOT(4) = TABLE_115226;
    TABLE_SLOT(5) = TABLE_11523E;

    build_tables();
    lcd_controller_init();
    REG8(0x11B0A8UL) = 0x01;

    buffer_fill(LCD_FRAME_A, 0x00);
    buffer_fill(LCD_FRAME_B, 0x00);
    buffer_fill(LCD_FRAME_C, 0x00);

    TOUCH_CAL_X_SCALE = FLASH_CAL_X_SCALE;
    TOUCH_CAL_Y_SCALE = FLASH_CAL_Y_SCALE;
    TOUCH_CAL_X_OFFSET = FLASH_CAL_X_OFFSET;
    TOUCH_CAL_Y_OFFSET = FLASH_CAL_Y_OFFSET;

    splash_and_config();
    scan_items();
    display_init_223010();

    /* The tail: pick the item the machine starts on, and gather the
     * settings the drawing code reads from the H'11B2xx block. The two
     * copies of the chosen position are what the display and the menu each
     * look at. */
    {
        const u16 selected = first_item_of_category(0x03, MENU_LIST);

        REG16(0x11B10CUL) = 1;
        REG16(0x11B10AUL) = 1;
        REG16(0x11B290UL) = REG16(0xFFFEE0UL);
        REG16(0x11B292UL) = REG8(0xFFFEFDUL);   /* a byte, widened */
        REG8(0x11B294UL) = REG8(0xFFFEE4UL);
        REG8(0x11B295UL) = REG8(0xFFFEE7UL);
        REG8(0x11B296UL) = REG8(0xFFFEEAUL);
        REG16(0x11B298UL) = selected;
        REG16(0x11B108UL) = selected;
        REG8(0x11B29AUL) = REG8(0x11A169UL);
        REG8(0x11B29DUL) = REG8(0x11A174UL);
        REG8(0x11B29BUL) = 0x01;
        REG8(0x11B29CUL) = 0x00;

        finish_22950C();
    }
}

void set_flash_page_buffer(void)
{
    REG32(0xFFFD10UL) = FLASH_PAGE_BUFFER;
}

/* H'208D22. The external bus, the shadows of the write-only registers, and
 * two device latches.
 *
 * Registers that cannot be read back have their intended value kept in RAM
 * from H'FFFD30 upwards, and this lays down the starting set. H'FFFD30 is
 * the one for port 4's data direction -- the I2C driver above reads and
 * writes it on every transfer -- and the rest are the same idea for other
 * registers.
 *
 * The last two are not shadows of on-chip registers but of latches out on
 * the bus at H'0A0000 and H'0C0000: each is written for real and then
 * remembered, because reading one back returns whatever the hardware drives
 * rather than what was last asked for.
 */
void port_shadows_init(void)
{
    bus_init();

    REG8(0xFFFD30UL) = 0x00;    /* port 4 data direction */
    REG8(0xFFFD31UL) = 0xFF;
    REG8(0xFFFD32UL) = 0x80;
    REG8(0xFFFD33UL) = 0xFC;
    REG8(0xFFFD34UL) = 0xC3;
    REG8(0xFFFD35UL) = 0x00;
    REG8(0xFFFD36UL) = 0xF0;
    REG8(0xFFFD37UL) = 0x00;

    REG8(0x0A0000UL) = 0x05;
    REG8(0xFFFD38UL) = 0x05;

    REG8(0x0C0000UL) = 0x01;
    REG8(0xFFFD39UL) = 0x01;
}

/* ---- the configuration block in flash -----------------------------------
 * H'57FF80 holds the machine's persistent settings: a stamp, two counters,
 * a handful of single-byte choices, and at H'57FFB0 a copy of the firmware
 * version string that last ran. It is in the application flash, so changing
 * anything in it means a flash write, which is why this is the only part of
 * the bring-up that calls back into the boot ROM for something other than a
 * delay.
 *
 * The block in this machine's image reads
 *
 *   B4 A5 | 00 00 21 2B | 00 2B EF C0 | 03 70 | 0F CE 0C 08 | 53 53 | 32
 *
 * against the factory values written below -- so the two counters have run,
 * and four of the choices have been changed by use. H'57FF90 and H'57FF91
 * are the pair config_to_eeprom copies into the settings store.
 */

/* H'24AB2A. The ROM's own strcmp. Returns zero, or the difference of the
 * first pair of bytes that differ, taken unsigned and widened to a signed
 * word. */
short str_compare(const char *a, const char *b)
{
    while (*a == *b) {
        if (*a++ == 0) return 0;
        b++;
    }
    return (short)((u8)*a - (u8)*b);
}

/* H'24AB9A. The ROM's own strlen. */
u32 str_length(const char *s)
{
    const char *p = s;

    while (*p != 0) p++;
    return (u32)(p - s);
}

/* ---- writing the settings back out ------------------------------------
 * H'21DDC4 and the five routines it calls. This is the whole of what the
 * machine keeps across a power cycle, put back into flash in one go:
 *
 *   H'57EED6  seven blocks of H'22 bytes, from H'115A20 upwards
 *   H'57EFC4  a word, always 1
 *   H'57EFC6  the splash flag
 *   H'57EFC8  H'12 bytes of beep settings
 *   H'57EFC7  a byte, always 1
 *   H'57FF8A -> H'57FF94, and three single bytes in the machine block
 *
 * Bit 5 of H'114DC7 stands for "flash is being written", and is up across
 * the whole of it -- the same bit config_block_check raises.
 *
 * The argument decides how far the reset goes. Non-zero clears the pattern
 * queue and the cache as well, which is what config_block_check asks for
 * when it finds a flash that has never been written.
 */

/* H'201556. Bit 7 cleared on byte H'16 of every item descriptor, and the
 * byte at H'0E4010 + H'10 * index zeroed with it. Read-modify-write through
 * a one-byte local, because the flash writer takes an address to copy from.
 *
 * The loop stops one short of the full 1024: CMP.W #H'3FF followed by BCC
 * leaves at index H'3FF, so the last descriptor keeps its bit. */
void item_flags_clear(void)
{
    u16 i;

    FLASH_BUSY |= 0x20;

    for (i = 0; i < 0x03FF; i++) {
        const u32 field = ITEM_TABLE + (u32)ITEM_STRIDE * (u32)i + 0x16;
        u8 b;

        REG8(0x000E4010UL + ((u32)(u16)(i << 4))) = 0x00;

        b = (u8)(REG8(field) & ~0x80);
        rom_flash_write(&b, field, 1);
    }

    FLASH_BUSY &= (u8)~0x20;
    REG8(0x11A6AEUL) |= 0x02;
}

/* H'21090C. The run length in field H'14 of the descriptor the item lists
 * are built from -- the terminator's own descriptor, whose index sits in
 * H'11B11C.
 *
 * A descriptor is H'18 bytes and the flash writer works on whole blocks, so
 * the descriptor is copied into a local, one field is changed, and the whole
 * thing goes back. The copy is six longwords, which is where the H'18 comes
 * from. */
void item_run_length_set(u16 length)
{
    u8  copy[0x18];
    u32 entry = ITEM_TABLE + (u32)ITEM_STRIDE * (u32)(short)REG16(ITEM_BASE_INDEX);
    int i;

    for (i = 0; i < 6; i++) {
        *(u32 *)&copy[4 * i] = REG32(entry + 4 * (u32)i);
    }
    *(u16 *)&copy[0x14] = length;

    FLASH_BUSY |= 0x20;
    entry = ITEM_TABLE + (u32)ITEM_STRIDE * (u32)(short)REG16(ITEM_BASE_INDEX);
    rom_flash_write(copy, entry, 0x18);
    FLASH_BUSY &= (u8)~0x20;
}

/* H'21DFCA. One user pattern left, and the lists rebuilt round it. */
void user_items_reset(void)
{
    item_run_length_set(0x0001);
    display_init_223010();
}

/* H'21DFDC. The factory beep settings -- nine pairs, "does it beep" and "how
 * many times", in the order message_beep indexes them. Built in a local and
 * written as one H'12-byte block. */
void beep_defaults_write(void)
{
    u8 d[0x12];
    int i;

    for (i = 0; i <= 8; i++) {
        d[2 * i]     = 0x00;
        d[2 * i + 1] = 0x01;
    }
    d[0x06] = 0x01; d[0x07] = 0x02;
    d[0x0A] = 0x00; d[0x0B] = 0x03;
    d[0x0C] = 0x01; d[0x0D] = 0x02;
    d[0x0E] = 0x01; d[0x0F] = 0x02;
    d[0x10] = 0x01; d[0x11] = 0x02;

    rom_flash_write(d, 0x0057EFC8UL, 0x12);
}

/* H'200E46. The first byte of each of 1024 ten-byte records at H'57C6D6,
 * zeroed. This one does run the whole 1024: BHI, not BCC. */
void stitch_records_clear(void)
{
    u8  zero = 0x00;
    u16 i;

    FLASH_BUSY |= 0x20;
    for (i = 0; i <= 0x03FF; i++) {
        rom_flash_write(&zero, 0x0057C6D6UL + 10UL * (u32)i, 1);
    }
    FLASH_BUSY &= (u8)~0x20;
}

/* H'22A23A. The pattern queue and the two caches beside it, cleared in RAM
 * and then written down over the copies in flash. */
void queue_clear_to_flash(void)
{
    mem_set(0x0011B3D8UL, 0x00, 0x07D2);
    mem_set(0x0011BBAAUL, 0x00, 0x32D5);
    mem_set(0x0011EE80UL, 0x00, 0x0400);

    rom_flash_write((const void *)0x0011BBAAUL, 0x00578000UL, 0x32D5);
    rom_flash_write((const void *)0x0011EE80UL, 0x0057B2D6UL, 0x0400);
}

void settings_save(u8 clear_queue)
{
    u8  b;
    u16 w;
    int i;

    item_flags_clear();
    user_items_reset();

    FLASH_BUSY |= 0x20;

    for (i = 0; i < 7; i++) {
        rom_flash_write((const void *)(0x00115A20UL + 0x22UL * (u32)i),
                        0x0057EED6UL + 0x22UL * (u32)i, 0x22);
    }

    w = 0x0001; rom_flash_write(&w, 0x0057EFC4UL, 2);
    b = 0x01;   rom_flash_write(&b, 0x0057EFC6UL, 1);

    beep_defaults_write();
    stitch_records_clear();

    if (clear_queue != 0) queue_clear_to_flash();

    rom_flash_write((const void *)0x0057FF8AUL, 0x0057FF94UL, 2);
    b = 0x08;   rom_flash_write(&b, 0x0057FF8FUL, 1);
    b = 0x01;   rom_flash_write(&b, 0x0057EFC7UL, 1);
    b = 0x32;   rom_flash_write(&b, 0x0057FF92UL, 1);

    FLASH_BUSY &= (u8)~0x20;
}

void config_block_check(void)
{
    u8  b;
    u16 w;
    u32 l;

    FLASH_BUSY |= 0x20;

    if (CONFIG_STAMPED != 0xA5) {
        settings_save(0x01);

        b = 0xB4; rom_flash_write(&b, 0x57FF80UL, 1);
        l = 0;    rom_flash_write(&l, 0x57FF82UL, 4);
        l = 0;    rom_flash_write(&l, 0x57FF86UL, 4);
        w = 0x0370; rom_flash_write(&w, 0x57FF8AUL, 2);
        w = 0x0370; rom_flash_write(&w, 0x57FF94UL, 2);
        b = 0x0F; rom_flash_write(&b, 0x57FF8CUL, 1);
        b = 0x8C; rom_flash_write(&b, 0x57FF8DUL, 1);
        b = 0x08; rom_flash_write(&b, 0x57FF8EUL, 1);
        b = 0x08; rom_flash_write(&b, 0x57FF8FUL, 1);
        b = 0x40; rom_flash_write(&b, 0x57FF90UL, 1);
        b = 0x40; rom_flash_write(&b, 0x57FF91UL, 1);
        b = 0x32; rom_flash_write(&b, 0x57FF92UL, 1);

        /* Ten bytes, which is "NMMV03.01" and its terminator exactly. */
        rom_flash_write((const void *)APP_IDENTITY, CONFIG_VERSION, 10);

        /* A settle after programming that does not settle. The loop leaves
         * on the first pass, because what it tests is the value of the
         * counter before the increment rather than after it, and that is
         * zero. The 50000 it was meant to count to is still computed, into
         * a result nothing reads. Left as it is: it costs five
         * instructions and the flash has finished by now anyway. */
        {
            u16 i = 0;
            while (i++ != 0)
                (void)(i == 50000);
        }

        b = 0xA5; rom_flash_write(&b, 0x57FF81UL, 1);
    } else if (str_compare((const char *)CONFIG_VERSION,
                           (const char *)APP_IDENTITY) != 0) {
        settings_save(0x01);
        rom_flash_write((const void *)APP_IDENTITY, CONFIG_VERSION,
                        str_length((const char *)APP_IDENTITY) + 1);
    }

    FLASH_BUSY &= (u8)~0x20;
}

/* H'2061A0. Two pins turned round to inputs: P8 bit 1 and P4 bit 5. Each is
 * cleared in the shadow, written through to the register, and put back. */
void pins_to_input_2061A0(void)
{
    u8 v;

    v = (u8)(P8DDR_SHADOW & (u8)~0x02);
    P8DDR = v;
    P8DDR_SHADOW = v;

    v = (u8)(P4DDR_SHADOW & (u8)~0x20);
    P4DDR = v;
    P4DDR_SHADOW = v;
}

/* H'20A38C. Port B and the input trim.
 *
 * Bits 0 and 1 of port B and bits 2 to 5 of port C become inputs; P4 bit 4
 * becomes an output; bit 7 of the latch at H'0A0000 goes low. Each bit is
 * written through to the register on its own rather than the byte being
 * assembled first, which is what the original does and what matters if any
 * of those pins is watched rather than merely sampled.
 */
void port_b_init(void)
{
    u8 v;

    v = (u8)(PBDDR_SHADOW & (u8)~0x01); PBDDR = v;
    v = (u8)(v             & (u8)~0x02); PBDDR = v;
    PBDDR_SHADOW = v;

    v = (u8)(PCDDR_SHADOW & (u8)~0x04); PCDDR = v;
    v = (u8)(v            & (u8)~0x08); PCDDR = v;
    v = (u8)(v            & (u8)~0x10); PCDDR = v;
    v = (u8)(v            & (u8)~0x20); PCDDR = v;
    PCDDR_SHADOW = v;

    v = (u8)(P4DDR_SHADOW | 0x10);
    P4DDR = v;
    P4DDR_SHADOW = v;

    v = (u8)(LATCH_A_SHADOW & (u8)~0x80);
    LATCH_A = v;
    LATCH_A_SHADOW = v;

    REG8(0x11A826UL) = 0;

    INPUT_TRIM = SETTING_TRIM;
    if (INPUT_TRIM <= 0x0A || INPUT_TRIM >= 0xF5) INPUT_TRIM = 0x32;
}

/* H'208E6C. Three port C outputs driven high. Called only from the routine
 * below, straight after those same three pins are turned round to inputs --
 * on this part writing the data register of an input pin sets what the pin
 * will read as the moment it becomes an output again. */
void port_c_bits_high(void)
{
    PCDR |= 0x01;
    PCDR |= 0x02;
    PCDR |= 0x40;
}

/* H'208FE8. Port C bits 0, 1 and 6 to inputs, P4 bit 5 to an input, then
 * the three data bits above. */
void port_c_init(void)
{
    u8 v;

    v = (u8)(PCDDR_SHADOW & (u8)~0x01); PCDDR = v;
    v = (u8)(v            & (u8)~0x02); PCDDR = v;
    v = (u8)(v            & (u8)~0x40); PCDDR = v;
    PCDDR_SHADOW = v;

    v = (u8)(P4DDR_SHADOW & (u8)~0x20);
    P4DDR = v;
    P4DDR_SHADOW = v;

    port_c_bits_high();
}

/* H'200D44. One pattern's three side records copied onto another's slot.
 *
 * A pattern carries three things besides its descriptor: a four-byte record
 * at H'57B6D6, a ten-byte one at H'57C6D6 and the sixteen-byte working copy
 * at H'0E4010. The first two are in flash and go back through the boot
 * ROM's writer, so they are read into a frame first; the third is ordinary
 * RAM and is copied straight across.
 *
 * H'114DC7 bit 5 is up while the flash is being written, which is what the
 * millisecond handler looks at to leave the bus alone.
 *
 * Every index is multiplied as a word and used without sign extension, so a
 * pattern number past H'0FFF wraps rather than running off the tables.
 */
void item_records_copy(u16 from, u16 to)
{
    u32 head;
    u16 tail[5];
    u32 at;
    u8 n;

    head = REG32(0x0057B6D6UL + (u32)(u16)((u16)(from << 2)));

    at = 0x0057C6D6UL + (u32)(u16)((u16)(10 * from));
    for (n = 0; n < 5; n++) {
        tail[n] = REG16(at);
        at += 2;
    }

    {
        u32 src = STITCH_WORKING + (u32)(u16)((u16)(from << 4));
        u32 dst = STITCH_WORKING + (u32)(u16)((u16)(to << 4));

        for (n = 4; n != 0; n--) {
            REG32(dst) = REG32(src);
            src += 4;
            dst += 4;
        }
    }

    FLASH_BUSY |= 0x20;
    rom_flash_write(&head, 0x0057B6D6UL + (u32)(u16)((u16)(to << 2)), 4);
    rom_flash_write(tail, 0x0057C6D6UL + (u32)(u16)((u16)(10 * to)), 0x0A);
    FLASH_BUSY &= (u8)~0x20;
}
