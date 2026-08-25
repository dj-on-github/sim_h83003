/* The artista 180 application, rebuilt in C.
 *
 * Work in progress. What is here so far is the skeleton the rest hangs off:
 * the entry sequence, the interrupt slots the original fills in, and the
 * calls it makes back into the boot ROM. Everything that has not been
 * reconstructed yet is a stub, and the build/splice/run loop is arranged so
 * that a stub is obvious rather than silently wrong -- a stub interrupt
 * handler returns, it does not pretend to have done something.
 *
 * Scale, measured rather than guessed: the code region H'200000-H'250FFF
 * holds 1310 distinct routines in 324K. Booting to an idle screen executes
 * about 56K of that across 66 spans, so the boot path is a fraction of the
 * whole and is where the work starts.
 */

#include "../bootrom/h8_3003.h"

/* ---- calling back into the boot ROM ------------------------------------
 * The boot ROM publishes routines through the low vector slots, reached with
 * JSR @@aa:8 -- a memory-indirect call that takes its target from the vector
 * table. The application is the caller here, so it uses the original ROM's
 * convention: first argument in ER6, result back in R6. These wrappers are
 * assembly for that reason.
 */

/* Slot 1: the boot ROM's host-link service, called round every loop in the
 * application through the thunk at H'250AE8 -- JMP @@H'04. It takes nothing
 * and returns nothing, so there is no shim on the boot ROM side. */
void rom_host_service(void)
{
    __asm__ volatile("jsr @@0x04"
                     :
                     :
                     : "er0", "er1", "er2", "er3", "er6", "cc", "memory");
}

/* Slot 2: the boot ROM's delay loop. */
void rom_delay(u16 units)
{
    __asm__ volatile("mov.w %0,r6\n\t"
                     "jsr   @@0x08"
                     :
                     : "r"(units)
                     : "er6", "cc", "memory");
}

/* Slot 3: the boot ROM's flash writer, flash_write_block(src, dst, len).
 *
 * The application reaches it through a one-instruction thunk at H'250AEC --
 * JMP @@H'0C:8 -- so the two stack arguments have to be laid down here, the
 * length first so that the destination ends up nearest the return address.
 */
void rom_flash_write(const void *src, u32 dst, u32 len)
{
    __asm__ volatile("mov.l %2,er0\n\t"
                     "mov.l er0,@-er7\n\t"
                     "mov.l %1,er0\n\t"
                     "mov.l er0,@-er7\n\t"
                     "mov.l %0,er6\n\t"
                     "jsr   @@0x0c\n\t"
                     "adds  #4,er7\n\t"
                     "adds  #4,er7"
                     :
                     : "r"(src), "r"(dst), "r"(len)
                     : "er0", "er6", "cc", "memory");
}

/* Slot 5: select serial port 0. */
void rom_select_sci0(void)
{
    __asm__ volatile("jsr @@0x14" ::: "er6", "cc", "memory");
}

/* Slot 22: the flag the download protocol's 'Y' command raises. */
int rom_host_confirmed(void)
{
    u16 result;
    __asm__ volatile("jsr  @@0x58\n\t"
                     "mov.w r6,%0"
                     : "=r"(result)
                     :
                     : "er6", "cc", "memory");
    return result != 0;
}

/* A routine that has not been reconstructed yet. It does nothing, and says
 * which address it stands for, so that a stub is visible in the source
 * rather than being mistaken for a routine that genuinely has no body. */
#define STUB(name, addr)                                                      \
    static void name(void) { /* H'addr -- NOT RECONSTRUCTED */ }

/* ---- the I2C bus on port 4 ---------------------------------------------
 * Bit-banged, SDA on P4 bit 7 and SCL on bit 6, talking to a 24Cxx-style
 * serial EEPROM at the usual pair of addresses -- H'50 to write, H'51 to
 * read. This is where the machine keeps its settings.
 *
 * P4DDR is write-only on this chip, so its intended value is kept in RAM at
 * H'FFFD30 and written through from there. Reading the port back to
 * modify it would return the pin states, not what was last asked for.
 */
#define I2C_SDA         0x80    /* P4 bit 7 */
#define I2C_SCL         0x40    /* P4 bit 6 */
#define P4DDR_SHADOW    REG8(0xFFFD30UL)

#define EEPROM_WRITE    0x50
#define EEPROM_READ     0x51

/* H'200A3C. A spin, not a timer: the bus is driven by instruction timing. */
static void i2c_delay(u16 count)
{
    while (count != 0) {
        count--;
        __asm__ volatile("");
    }
}

/* Point SDA at the bus, or let it go so the device can drive it. */
static void sda_output(void)
{
    u8 ddr = P4DDR_SHADOW | I2C_SDA;
    P4DDR = ddr;
    P4DDR_SHADOW = ddr;
}

static void sda_input(void)
{
    u8 ddr = (u8)(P4DDR_SHADOW & ~I2C_SDA);
    P4DDR = ddr;
    P4DDR_SHADOW = ddr;
}

/* H'200A4C. Releases SDA and gives one clock -- the acknowledge slot after a
 * byte has gone out. The original does not look at what the device does with
 * it. */
static void i2c_ack_clock(void)
{
    sda_input();
    P4DR |= I2C_SCL;
    P4DR &= (u8)~I2C_SCL;
}

/* H'200B72. Start: SDA high, then low, while SCL is high. */
static void i2c_start(void)
{
    P4DR |= I2C_SDA;
    sda_output();
    P4DR |= I2C_SCL;
    i2c_delay(3);
    P4DR &= (u8)~I2C_SDA;
    i2c_delay(3);
}

/* H'200BA4. Stop: SDA low, then high, while SCL is high. */
static void i2c_stop(void)
{
    i2c_delay(3);
    P4DR &= (u8)~I2C_SDA;
    sda_output();
    P4DR |= I2C_SCL;
    i2c_delay(3);
    P4DR |= I2C_SDA;
    i2c_delay(3);
}

/* H'200A8C. Eight bits out, most significant first, then the acknowledge
 * clock. */
static void i2c_write_byte(u8 value)
{
    u8 bit;

    P4DR &= (u8)~I2C_SCL;
    sda_output();

    for (bit = 0; bit < 8; bit++) {
        if (value & 0x80) {
            P4DR |= I2C_SDA;
        } else {
            P4DR &= (u8)~I2C_SDA;
        }
        P4DR |= I2C_SCL;
        value = (u8)(value << 1);
        P4DR &= (u8)~I2C_SCL;
    }
    i2c_ack_clock();
}

/* H'200AD6. Eight bits in, then SDA driven high for one clock -- a not
 * acknowledge, which is what ends a read. */
static u8 i2c_read_byte(void)
{
    u8 value = 0;
    u8 bit;

    sda_input();
    P4DR &= (u8)~I2C_SCL;

    for (bit = 0; bit < 8; bit++) {
        P4DR |= I2C_SCL;
        value = (u8)(value << 1);
        if (P4DR & I2C_SDA) value++;
        P4DR &= (u8)~I2C_SCL;
    }

    P4DR |= I2C_SDA;
    sda_output();
    P4DR |= I2C_SCL;
    P4DR &= (u8)~I2C_SCL;
    return value;
}

/* H'200CB6. Brings the bus up: both lines outputs and high, with the pull-up
 * enabled on SDA, then a stop so any device part-way through a transfer lets
 * go of it. */
void i2c_init(void)
{
    u8 ddr = P4DDR_SHADOW | I2C_SCL;
    P4DDR = ddr;
    P4DR |= I2C_SCL;
    P4PCR |= I2C_SDA;
    ddr |= I2C_SDA;
    P4DDR = ddr;
    P4DDR_SHADOW = ddr;
    P4DR |= I2C_SDA;
    i2c_stop();
}

/* H'200BDE. A current-address read: the EEPROM's own pointer decides which
 * byte comes back, so this only means anything straight after a write has
 * left it where it is wanted. */
u8 eeprom_read_current(void)
{
    u8 value;

    i2c_start();
    i2c_write_byte(EEPROM_READ);
    value = i2c_read_byte();
    i2c_stop();
    return value;
}

/* H'200C72. Writes one byte and checks it went in, by reading it straight
 * back. The delay is the device's write cycle -- it will not answer at all
 * until it has finished. Returns 1 when the read back agrees. */
u8 eeprom_write_verify(u8 address, u8 value)
{
    i2c_start();
    i2c_write_byte(EEPROM_WRITE);
    i2c_write_byte(address);
    i2c_write_byte(value);
    i2c_stop();

    i2c_delay(300);

    return eeprom_read_current() == value ? 1 : 0;
}

/* ---- the bus controller ------------------------------------------------
 * H'2009E8. The application sets the external bus up differently from the
 * boot ROM: two-state access for one area and an extra wait state. */
void bus_init(void)
{
    ABWCR = 0xFF;   /* every area 8 bits wide */
    ASTCR = 0xFD;   /* three-state everywhere except area 1 */
    WCR   = 0xF3;
    WCER  = 0x00;
    BRCR  = 0x00;
}

/* ---- entry ------------------------------------------------------------- */

/* H'20038E and H'20039A. Zero-fill [start, end), walking down from the top.
 *
 * Two versions because the original has two: the first compares only the low
 * halves of the pointers, so it is limited to a region that does not cross a
 * 64K boundary, and the second compares all 24 bits. The comparison happens
 * before the first store, so a region whose ends are equal fills nothing --
 * which several of the calls below rely on, whether or not they meant to.
 */
static void fill16(u16 start, u32 end)
{
    while ((u16)end != start) {
        *(volatile u8 *)(--end) = 0;
    }
}

static void fill32(u32 start, u32 end)
{
    while (end != start) {
        *(volatile u8 *)(--end) = 0;
    }
}

/* H'2007AC. Decides whether this is a cold start.
 *
 * The original is two instructions: load 1, return. There is no test — every
 * start is a cold start. Reproduced as it stands rather than "improved",
 * because whatever retained state the clear below destroys, the machine has
 * always destroyed it. */
u8 memory_check(void)
{
    return 1;
}

/* H'200220. Clears the application's working memory.
 *
 * The list is the original's, in its order, no-ops included: the first three
 * calls have equal ends and fill nothing at all. They are kept because this
 * is a reconstruction, and because a list that matches makes the next person
 * comparing the two a great deal happier.
 *
 * The last two entries are the display: H'040000 and H'044B00, H'4B00 bytes
 * each, which is 320 x 240 at two bits per pixel -- a pair of frame buffers.
 */
void cold_start(void)
{
    fill16(0xF5A8, 0x0011F5A8);   /* fills nothing */
    fill16(0xF5A8, 0x0011F5A8);   /* fills nothing */
    fill32(0x0011F5A8, 0x0011F5A8); /* fills nothing */

    fill32(0x0011A24A, 0x0011F5A8);
    fill32(0x00FFFD40, 0x00FFFD40); /* fills nothing */
    fill32(0x00FFFEC0, 0x00FFFF10);

    fill32(0x000E0000, 0x000E1800);
    fill32(0x000E4000, 0x000E8010);
    fill32(0x000FFE00, 0x00114D4A);
    fill32(0x00114D4A, 0x00114DC2);
    fill32(0x000E8010, 0x000FFC13);

    fill32(0x00040000, 0x00044B00); /* frame buffer */
    fill32(0x00044B00, 0x00049600); /* and the second one */
}

/* ---- the display -------------------------------------------------------
 * The SED1351F controller answers at H'020000 and the picture lives at
 * H'040000: 320 x 240 at two bits per pixel, H'4B00 bytes, with a second
 * buffer of the same size behind it at H'044B00.
 */
#define LCD_REG(n)          REG8(0x020000UL + (n))
#define LCD_BUFFER_BYTES    0x4B00
#define LCD_FRAME_A         0x00040000UL
#define LCD_FRAME_B         0x00044B00UL
#define LCD_FRAME_C         0x000E8010UL

/* H'20E062. Fifteen register writes and no loops. The last one enables the
 * controller, which is why it is a second write to register 1: the first
 * sets the mode while it is still off. */
void lcd_controller_init(void)
{
    LCD_REG(0x01) = 0x59;
    LCD_REG(0x02) = 0x4F;   /* bytes per line, less one: 80 -> 320 pixels */
    LCD_REG(0x03) = 0x01;
    LCD_REG(0x04) = 0xEF;   /* lines, less one: 240                        */
    LCD_REG(0x05) = 0x00;
    LCD_REG(0x06) = 0x00;
    LCD_REG(0x07) = 0x00;
    LCD_REG(0x08) = 0x00;
    LCD_REG(0x09) = 0x4B;
    LCD_REG(0x0A) = 0xEF;
    LCD_REG(0x0B) = 0x00;
    LCD_REG(0x0D) = 0x00;
    LCD_REG(0x0E) = 0xAA;
    LCD_REG(0x0F) = 0x55;
    LCD_REG(0x01) = 0xDB;   /* and on */
}

/* The third display buffer, used as scratch by the mirroring and the
 * splash. Defined here because splash_and_config needs it. */
#define LCD_SCRATCH  0x000E8010UL

/* Defined with the rest of the display, further down. */
void region_copy(u16 x0, u16 y0, u16 x1, u16 y1, u16 dst_y,
                 u32 from, u32 to);
void message_show(u16 msg);
void picker_cursor(u8 mode);
void number_draw(u16 value, u16 x, u16 y);

/* H'208698. Defined near the end, once everything it calls exists. */
static void service_tick(void);

/* H'20F0FE. Called from inside the long fills, so that whatever needs
 * attention while the CPU is busy clearing 19K still gets it. */
static void service_hook(void)
{
    if ((REG8(0x114DC6UL) & 0x80) || (REG8(0x114DCFUL) & 0x10)) {
        if (REG16(0x114DE2UL) >= 10) service_tick();
    }
}

/* H'2104E2. A byte fill that lets the service hook run between bytes.
 *
 * Not static: it is a routine in its own right in the original and has its
 * own comparison cases, and once H'20E126 below started calling it the
 * compiler was inlining every copy and leaving no symbol to name. */
void mem_fill(u32 dest, u8 value, u32 count)
{
    volatile u8 *p = (volatile u8 *)dest;

    while (count != 0) {
        count--;
        service_hook();
        *p++ = value;
    }
}

/* H'20E126. One display buffer filled with a byte: H'4B00 of them, which is
 * 320 by 240 at two bits a pixel. Sixty-one of the screen bodies call it.
 *
 * The argument it fills from comes back off the stack rather than out of the
 * register it arrived in, because by then the pushes have moved the frame --
 * and the saved copy of that register is thrown away with the arguments
 * rather than popped, so the caller does not get it back. */
void lcd_buffer_fill(u32 buffer, u8 value)
{
    mem_fill(buffer, value, 0x4B00);
}

/* H'20E126. One display buffer's worth. */
static void buffer_fill(u32 dest, u8 value)
{
    mem_fill(dest, value, LCD_BUFFER_BYTES);
}

/* ---- tables the display works from -------------------------------------
 * H'21051C and its neighbours: each returns the address of one table. They
 * are one instruction and a return in the original, and the addresses are
 * what matters, so they are kept as named constants rather than functions.
 */
#define TABLE_114DE6    0x00114DE6UL
#define TABLE_114FDA    0x00114FDAUL
#define TABLE_115066    0x00115066UL
#define TABLE_1150D6    0x001150D6UL
#define TABLE_3D4250    0x003D4250UL   /* in the pattern data */
#define TABLE_11510E    0x0011510EUL
#define TABLE_115122    0x00115122UL
#define TABLE_11518E    0x0011518EUL
#define TABLE_1151A2    0x001151A2UL
#define TABLE_115226    0x00115226UL
#define TABLE_11523E    0x0011523EUL

/* Where sub_2105C4 keeps them. */
#define TABLE_SLOT(n)   REG32(0x11B29EUL + 4 * (n))

/* Touch calibration: kept in flash, copied to RAM at start-up. */
#define FLASH_CAL_X_SCALE   REG32(0x57FFA0UL)
#define FLASH_CAL_Y_SCALE   REG32(0x57FFA4UL)
#define FLASH_CAL_X_OFFSET  REG32(0x57FFA8UL)
#define FLASH_CAL_Y_OFFSET  REG32(0x57FFACUL)
#define TOUCH_CAL_X_SCALE   REG32(0x11A87EUL)
#define TOUCH_CAL_Y_SCALE   REG32(0x11A882UL)
#define TOUCH_CAL_X_OFFSET  REG32(0x11A886UL)
#define TOUCH_CAL_Y_OFFSET  REG32(0x11A88AUL)


/* H'208B3C. The machine's input and state block, H'FFFEC0 to H'FFFF0F.
 *
 * Almost all of it goes to zero; the handful of values that do not are the
 * interesting ones. This is where the touch coordinates, the A/D samples and
 * the mode flags live -- the same cells the display bring-up later copies
 * settings out of, which is why those copies read zero until this runs.
 *
 * The original writes each byte separately. The runs of zeros are loops here
 * because nothing observes the order, and H'FFFF0E is skipped in both: it is
 * the one byte in the range this routine does not touch.
 */
void input_state_init(void)
{
    u32 a;

    REG8(0xFFFEC0UL) = 0x00;
    REG8(0xFFFEC1UL) = 0x50;
    REG8(0xFFFEC2UL) = 0x00;
    REG8(0xFFFEC3UL) = 0x05;
    REG8(0xFFFEC4UL) = 0x00;
    REG8(0xFFFEC5UL) = 0x00;
    REG8(0xFFFEC6UL) = 0x04;

    for (a = 0xFFFEC7UL; a <= 0xFFFECDUL; a++) REG8(a) = 0x00;
    REG16(0xFFFECEUL) = 0x0000;

    /* H'FFFED9 and H'FFFEDA in here are the raw touch coordinates. */
    for (a = 0xFFFED0UL; a <= 0xFFFEDFUL; a++) REG8(a) = 0x00;
    REG16(0xFFFEE0UL) = 0x0000;
    for (a = 0xFFFEE4UL; a <= 0xFFFEEBUL; a++) REG8(a) = 0x00;

    REG8(0xFFFEECUL) = 0x10;
    REG8(0xFFFEEDUL) = 0x10;

    REG8(0xFFFEE2UL) = 0x00;
    REG8(0xFFFEE3UL) = 0x00;

    /* H'FFFEF0 and H'FFFEF1 are the AN4 and AN6 samples. */
    for (a = 0xFFFEEEUL; a <= 0xFFFEFDUL; a++) REG8(a) = 0x00;

    REG16(0xFFFEFEUL) = 0xFFFF;
    REG16(0xFFFF00UL) = 0x1000;
    for (a = 0xFFFF02UL; a <= 0xFFFF0DUL; a++) REG8(a) = 0x00;
    REG8(0xFFFF0FUL) = 0x00;
}

/* H'2439A2. Sixteen pointers into RAM, laid out at H'0FFE00.
 *
 * A table of where things are rather than of what to do: the addresses are
 * all in the application's own variables, and the code that works on the
 * embroidery link reaches them through here.
 */
void pointer_table_init(void)
{
    static const u32 addresses[16] = {
        0x00114DBEUL, 0x00114D4CUL, 0x0011A63AUL, 0x001040AEUL,
        0x0011A660UL, 0x0011A41AUL, 0x000FFE80UL, 0x00114D5CUL,
        0x0011A662UL, 0x00114D7AUL, 0x0011A25AUL, 0x0011A612UL,
        0x00104C7AUL, 0x00104036UL, 0x0011F538UL, 0x0011F53AUL,
    };
    int i;

    for (i = 0; i < 16; i++) {
        REG32(0x000FFE00UL + 4 * i) = addresses[i];
    }
}

/* H'24491C. Brings up SCI0 -- the embroidery module's link -- and the state
 * that goes with it.
 *
 * The rate divisor is H'05, which is 57600 baud at this machine's clock: the
 * same rate the download protocol switches the other channel to. The control
 * register is set to receive with the receive interrupt enabled, which is
 * what the application's vector slots 52 to 54 are for.
 *
 * P9DDR is write-only and shadowed at H'FFFD34, and is written twice on the
 * way -- once with bit 0 set and again with bit 2 cleared -- before the
 * shadow is updated. RDRF is cleared by writing a constant rather than by
 * reading the register and masking: a character finishing between the read
 * and the write-back would otherwise be lost.
 */
void sci0_module_init(void)
{
    u8 ddr;

    REG8(0x11F29EUL) = 0x00;
    REG8(0x11F2B6UL) = 0x00;
    REG8(0x11F2B2UL) = 0x00;
    REG8(0x11F2CAUL) = 0x00;
    REG8(0x11F2B3UL) = 0x00;
    REG8(0x11F2B5UL) = 0x00;

    ddr = (u8)(REG8(0xFFFD34UL) | 0x01);
    P9DDR = ddr;
    ddr = (u8)(ddr & ~0x04);
    P9DDR = ddr;
    REG8(0xFFFD34UL) = ddr;

    /* One instruction, not "SSR0 &= ~SSR_RDRF" and not a constant write.
     * The mask-and-write-back spreads over several instructions and can lose
     * a character that arrives in between; a constant write would also set
     * the multiprocessor bit to whatever the constant carries. BCLR does
     * neither, which is why the original uses it. */
    __asm__ volatile("bclr #6,@0xb4:8" ::: "memory");
    SMR0 = 0x00;
    BRR0 = 0x05;              /* 57600 baud */

    P9DR |= 0x03;
    SCR0 = 0x50;              /* receive, with the receive interrupt */
    SCR0 |= 0x10;
    P9DR |= 0x03;

    REG16(0x114D4CUL) = 0x0000;

    pointer_table_init();
}

/* H'209B0C. Copies two configuration bytes out of flash and puts them where
 * everything else expects to find them.
 *
 * Each goes three places: a working copy in the input block, the serial
 * EEPROM at H'A9 and H'AA, and a second copy at H'11A812. Writing them back
 * to the EEPROM on every start-up is how the settings store is kept in step
 * with the configuration block in flash, which is the one that can be
 * changed over the download protocol.
 *
 * Whether the write verified is not looked at.
 */
void config_to_eeprom(void)
{
    REG8(0xFFFEF3UL) = REG8(0x57FF90UL);
    eeprom_write_verify(0xA9, REG8(0xFFFEF3UL));
    REG8(0x11A812UL) = REG8(0xFFFEF3UL);

    REG8(0xFFFEF4UL) = REG8(0x57FF91UL);
    eeprom_write_verify(0xAA, REG8(0xFFFEF4UL));
    REG8(0x11A813UL) = REG8(0xFFFEF4UL);
}

/* ---- the A/D converter -------------------------------------------------
 * Four result registers shared by eight inputs: channels 0 and 4 both land
 * in ADDRA, 1 and 5 in ADDRB, and so on. Only the high byte of each pair is
 * ever read, so a reading is a byte.
 *
 * The original dispatches on the channel through a jump table for each of
 * these; the table is not reproduced, because it decides nothing an index
 * cannot.
 */
#define ADC_CHANNELS        8
#define ADC_RESULTS         0x11A251UL   /* where the scan leaves them */
#define ADC_SPIN_LIMIT      0x3E8

/* The result register a channel reports into. */
static u8 adc_read_channel(u8 channel)
{
    switch (channel & 3) {
    case 0:  return ADDRAH;
    case 1:  return ADDRBH;
    case 2:  return ADDRCH;
    default: return ADDRDH;
    }
}

/* H'20083E. Starts a scanning conversion, leaving whatever else is in the
 * control register alone. */
void adc_start_conversion(void)
{
    ADCSR |= ADCSR_START;
}

/* H'20084A. One reading out of the array the scan fills. */
u8 adc_get_result(u8 index)
{
    return REG8(ADC_RESULTS + index);
}

/* H'200860. Takes [samples] readings from one channel and returns their
 * average.
 *
 * Two details worth keeping. The spin waiting for the conversion to finish
 * is bounded, and on running out it reads the register anyway rather than
 * failing -- a stuck converter gives a stale number, not a hang. And the
 * budget is spent across the whole call, not reset per sample: it is
 * cleared once before the loop, so a slow first reading leaves less room
 * for the rest.
 */
u8 adc_convert_polled(u8 channel, u8 samples)
{
    u16 spins = 0;
    u16 total = 0;
    u8 n;

    ADCSR = (u8)(channel & 0x07);

    for (n = 0; n < samples; n++) {
        ADCSR |= ADCSR_START;
        while (!(ADCSR & ADCSR_ADF) && spins < ADC_SPIN_LIMIT) {
            spins++;
        }
        if ((channel & 0x07) < ADC_CHANNELS) {
            total = (u16)(total + adc_read_channel(channel));
        }
    }
    return samples == 0 ? 0 : (u8)(total / samples);
}

/* H'200914. Points the converter at a channel and starts it, handing back
 * whatever that channel's register still held. A channel out of range
 * selects nothing and starts anyway, as in the original. */
u8 adc_start_channel(u8 channel, u8 mode)
{
    u8 previous = 0;

    if ((channel & 0xFF) <= 0x07) {
        previous = adc_read_channel(channel);
    }
    ADCSR = mode;
    ADCSR |= ADCSR_START;
    return previous;
}

/* ---- shared constants -------------------------------------------------
 * The configuration byte and the descriptor table are read from several
 * places, so they are defined once here rather than beside whichever
 * routine happened to need them first.
 */
/* The machine's configuration byte, in flash. The download protocol reads
 * the same byte to decide whether an embroidery session is open -- H'B4
 * means no module -- so it is both a configuration and a status. */
#define CONFIG_BLOCK        REG8(0x57FF80UL)
#define CONFIG_NO_MODULE    0xB4
#define SPLASH_ENABLED      REG8(0x57EFC6UL)
#define CONFIG_57EFC7       REG8(0x57EFC7UL)

#define ITEM_TABLE          REG32(0x114DD2UL)
#define ITEM_STRIDE         0x18
#define ITEM_CATEGORY       0x17
#define ITEM_LIMIT          0x400
#define ITEM_CAT_END        0x02   /* the table's terminator */
#define ITEM_CAT_UNLISTED   0x01
#define MENU_LIST           0x11A88EUL
#define MENU_ROW            5
#define CONFIG_ALT_POINTER  0xAA

/* Where each list after the first records its address. */
#define MENU_LIST_2_PTR     REG32(0x11B096UL)
#define MENU_LIST_3_PTR     REG32(0x11B09AUL)
#define MENU_LIST_4_PTR     REG32(0x11B09EUL)

/* Which categories a list takes. */
#define LIST_BELOW_10       0
#define LIST_12_AND_UP      1
#define LIST_EXACTLY_11     2
#define LIST_EXACTLY_10     3


/* ---- drawing ------------------------------------------------------------
 * H'20E154. One pixel.
 *
 * Two bits per pixel, four to a byte, 80 bytes to a line. The original works
 * out the byte from x >> 3 -- an eight-pixel, two-byte group -- and then
 * dispatches through a table of eight routines on x & 7, one per pixel
 * position, each with its shift and mask written out. That is the same
 * arithmetic as the expression below, and there is nothing to be gained by
 * reproducing the jump table: it exists because the original was compiled
 * that way, not because anything depends on it.
 *
 * The service hook runs on every pixel, as it does in the fills.
 */
#define LCD_BYTES_PER_LINE  0x50

void plot_pixel(u16 x, u16 y, u32 buffer, u8 colour)
{
    volatile u8 *p;
    u8 shift;

    service_hook();

    p = (volatile u8 *)(buffer + (u32)y * LCD_BYTES_PER_LINE +
                        ((u32)(x >> 3) << 1) + ((x & 7) >> 2));
    shift = (u8)(6 - 2 * (x & 3));

    *p = (u8)((*p & ~(3 << shift)) | ((colour & 3) << shift));
}

/* H'212994. The position in [list] of the first entry with category
 * [wanted] that has data behind it, counting from one. Returns 1 when there
 * is none -- not zero, so the caller always has a usable position.
 *
 * The original copies each descriptor into a frame before looking at it,
 * which changes nothing outside that frame; read straight from the table
 * here.
 *
 * Note the pointer test is written the other way round from the one in
 * build_item_list: there it is "H'AA selects the first pointer", here it is
 * "H'B4 selects the second". They agree on this machine, whose configuration
 * byte is H'B4, and both are reproduced as they stand. */
u16 first_item_of_category(u8 wanted, u32 list)
{
    const u32 table = ITEM_TABLE;
    const u16 count = REG16(list);
    u16 n;

    for (n = 1; n <= count; n++) {
        const u32 entry =
            table + (u32)ITEM_STRIDE * (u32)REG16(list + 2 * n);

        if (REG8(entry + ITEM_CATEGORY) != wanted) continue;
        if ((CONFIG_BLOCK == CONFIG_NO_MODULE ? REG32(entry + 4)
                                              : REG32(entry)) != 0) {
            return n;
        }
    }
    return 1;
}

/* H'21088E. Copies one list to another, leaving out the entries whose
 * category is 1 -- the ones build_item_list never lists either. */
#define ITEM_LIST_IN        0x11B11EUL
#define ITEM_LIST_OUT       0x11B212UL

void filter_unlisted(void)
{
    const u32 table = ITEM_TABLE;
    const u16 count = REG16(ITEM_LIST_IN);
    u16 kept = 0;
    u16 n;

    for (n = 1; n <= count; n++) {
        const u16 index = REG16(ITEM_LIST_IN + 2 * n);
        const u32 entry = table + (u32)ITEM_STRIDE * (u32)index;

        if (REG8(entry + ITEM_CATEGORY) == ITEM_CAT_UNLISTED) continue;
        kept++;
        REG16(ITEM_LIST_OUT + 2 * kept) = index;
    }
    REG16(ITEM_LIST_OUT) = kept;
}

/* H'200762. Fills [count] bytes at [dest] with [value]. The count is a word
 * in the original, widened to a longword before the loop, and a count of
 * zero fills nothing. */
void mem_set(u32 dest, u8 value, u16 count)
{
    volatile u8 *p = (volatile u8 *)dest;
    u32 left = count;

    while (left != 0) {
        *p++ = value;
        left--;
    }
}

/* H'21073E. The table index of the first entry with [category], or H'FFFF
 * when there is none.
 *
 * Unlike the list builder this does not stop at the category-2 terminator;
 * it scans the whole 1024. That is how sub_210808 finds the terminator
 * itself, by asking for category 2. */
u16 first_index_of_category(u8 category)
{
    const u32 table = ITEM_TABLE;
    u16 i;

    for (i = 0; i < ITEM_LIMIT; i++) {
        if (REG8(table + (u32)ITEM_STRIDE * (u32)i + ITEM_CATEGORY) ==
            category) {
            return i;
        }
    }
    return 0xFFFF;
}

/* H'210808. Builds two identical lists of consecutive table indices,
 * starting just past the terminator entry.
 *
 * The length comes out of the terminator's own descriptor, at offset H'14 --
 * so the entry that marks the end of the table also says how many follow it.
 * Both lists are a count followed by that many indices; one is read by the
 * menu and the other by the display.
 */
#define ITEM_BASE_INDEX     0x11B11CUL
#define ITEM_RUN_LENGTH     0x11B28CUL
#define ITEM_LIST_DISPLAY   0x11B198UL

void build_consecutive_lists(void)
{
    const u16 base = first_index_of_category(ITEM_CAT_END);
    const u32 entry = ITEM_TABLE + (u32)ITEM_STRIDE * (u32)base;
    const u16 length = REG16(entry + 0x14);
    u16 i;

    REG16(ITEM_BASE_INDEX) = base;
    REG16(ITEM_LIST_DISPLAY) = length;
    REG16(ITEM_LIST_IN) = length;
    REG16(ITEM_RUN_LENGTH) = length;

    /* The bound is re-read each time round, as in the original. */
    for (i = 1; i <= REG16(ITEM_RUN_LENGTH); i++) {
        const u16 value = (u16)(REG16(ITEM_BASE_INDEX) + i);
        REG16(ITEM_LIST_DISPLAY + 2 * i) = value;
        REG16(ITEM_LIST_IN + 2 * i) = value;
    }
}

/* H'22950C. Clears the two working areas the drawing code builds into, and
 * sets three values it starts from. */
void finish_22950C(void)
{
    mem_set(0x0011B3D8UL, 0x00, 0x07D2);
    mem_set(0x0011BBAAUL, 0x00, 0x32D5);

    REG16(0x11A1C8UL) = 0x0030;
    REG16(0x11A1CAUL) = 0x00A1;
    REG16(0x11A1CCUL) = 0x0000;
}

/* ---- more of the display bring-up --------------------------------------
 * The four routines sub_2105C4 calls after the controller is running. None
 * of them paints: the drawing happens later, from the main loop, through the
 * primitives around H'20E1xx and H'20F43C.
 */

#define TABLE_SLOT_6        REG32(0x11B2B6UL)
#define TABLE_SLOT_7        REG32(0x11B2BAUL)
#define TABLE_SLOT_8        REG32(0x11B2BEUL)
#define TABLE_SLOT_9        REG32(0x11B2C2UL)
#define TABLE_SLOT_10       REG32(0x11B2C6UL)

/* H'210C20. Five more table addresses, then 27 longwords copied out of the
 * table at H'115066 into H'11595E. Entry zero is not copied; the loop runs
 * from one. */
static void build_tables(void)
{
    int i;

    TABLE_SLOT_6 = TABLE_11510E;
    TABLE_SLOT_7 = TABLE_3D4250;
    TABLE_SLOT_8 = TABLE_1150D6;
    TABLE_SLOT_9 = TABLE_115066;
    TABLE_SLOT_10 = TABLE_115122;

    for (i = 1; i <= 0x1B; i++) {
        REG32(0x11595EUL + 4 * i) = REG32(TABLE_SLOT_9 + 4 * i);
    }
}

/* H'20F128. A run-length picture, unpacked into a buffer.
 *
 * The first word says how many words there are, itself included; every word
 * after it is one run -- the length in the high byte, the colour byte in the
 * low. Nothing in here knows about the screen. It is a byte fill repeated,
 * and a picture is whatever the run lengths make of the 80 bytes a line.
 */
void image_load(u32 runs, u32 dest)
{
    const u16 count = REG16(runs);
    u16 i;

    for (i = 1; i < count; i++) {
        const u16 w   = REG16(runs + 2 * (u32)i);
        const u32 len = (u32)(u8)(w >> 8);

        mem_fill(dest, (u8)w, len);
        dest += len;
    }
}

/* H'2119EE. Hold whatever is on the screen for [ticks].
 *
 * Not a delay -- it returns at once. It sets the length of the hold and
 * zeroes the count of how long it has run, and the millisecond tick does the
 * counting. H'211A02 is the other half: it says whether the hold has run
 * out, and clears the screen when it has.
 */
#define HOLD_ELAPSED  REG16(0x114DE0UL)
#define HOLD_LENGTH   REG16(0x11A166UL)

void hold_start(u16 ticks)
{
    HOLD_ELAPSED = 0x0000;
    HOLD_LENGTH  = ticks;
}

/* H'210CB0. The boot splash, and one configuration bit.
 *
 * The splash only runs when the byte at H'57EFC6 says so, and in the image
 * this was reconstructed from it does not -- which is why the machine goes
 * straight to its normal screen. Which of two pictures it would show depends
 * on the configuration byte: H'B4, the value that also means "no embroidery
 * module", selects the other one.
 *
 * The picture is decompressed into the off-screen buffer and blitted over
 * the whole 320 x 240 display, and then held: the hold is a countdown the
 * millisecond tick runs down, not a wait here.
 */
static void splash_and_config(void)
{
    if (SPLASH_ENABLED != 0) {
        REG8(0x11A171UL) = 0x01;

        if (CONFIG_BLOCK == CONFIG_NO_MODULE) {
            image_load(REG32(REG32(0x11B2AEUL) + 0x10), LCD_SCRATCH);
        } else {
            image_load(REG32(REG32(0x11B2AEUL) + 0x14), LCD_SCRATCH);
        }
        region_copy(0x0000, 0x0000, 0x013F, 0x00EF, 0x0000,
                    LCD_SCRATCH, LCD_FRAME_A);
        hold_start(0x0FA0);
    }

    if (CONFIG_57EFC7 != 0) {
        REG8(0xFFFEC1UL) |= 0x40;
    } else {
        REG8(0xFFFEC1UL) &= (u8)~0x40;
    }
}


/* H'223010. Two calls and nothing else. */
static void display_init_223010(void)
{
    build_consecutive_lists();
    filter_unlisted();
}

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

/* H'2007B2. Points the flash page buffer at H'0FFC14.
 *
 * This is the pointer the boot ROM's flash routines read out of H'FFFD10 to
 * find their 256-byte staging area -- see ../bootrom/app.c. The application
 * owns the memory and tells the boot ROM where it is, which is why a byte
 * written through the download protocol lands somewhere sensible. */
#define FLASH_PAGE_BUFFER   0x000FFC14UL

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

/* The ROM's own strcmp. Returns zero, or the difference of the first pair
 * of bytes that differ, taken unsigned and widened to a signed word. */
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

/* H'20888C. Makes sure the configuration block is there and current.
 *
 * H'57FF81 is the stamp. If it does not read H'A5 the block has never been
 * written -- a virgin flash, or one just erased by the download protocol --
 * and the factory values go down, the stamp last of all, so that a machine
 * interrupted part way through comes back here rather than running on a
 * half-written block.
 *
 * If the stamp is there, the only thing checked is the version string: when
 * the application in flash is not the one that wrote the block, the new
 * version string is recorded. That is how the machine knows it has been
 * updated.
 *
 * Bit 5 of H'114DC7 is held up across the whole thing. sub_21DDC4, which
 * writes the settings out to flash, raises the same bit.
 */
#define FLASH_BUSY          REG8(0x114DC7UL)

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

#define CONFIG_STAMPED      REG8(0x57FF81UL)
#define CONFIG_VERSION      0x57FFB0UL
#define APP_IDENTITY        0x200100UL

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

/* ---- shadowed ports ------------------------------------------------------
 * Several of the ports are write-only, so their intended contents are kept
 * in RAM from H'FFFD30 upwards and written through from there. Two more
 * shadows stand for latches out on the external bus rather than for pins.
 */
#define P8DDR_SHADOW    REG8(0xFFFD33UL)
#define PBDDR_SHADOW    REG8(0xFFFD36UL)
#define PCDDR_SHADOW    REG8(0xFFFD37UL)
#define LATCH_A         REG8(0x0A0000UL)
#define LATCH_A_SHADOW  REG8(0xFFFD38UL)

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

/* The trim the analog input applies, at H'FFFEDF: it is added as an offset
 * to a scaled reading before the result is stored. H'32 is the middle of
 * its range and the value a setting outside H'0B..H'F4 falls back to. */
#define INPUT_TRIM      REG8(0xFFFEDFUL)
#define SETTING_TRIM    REG8(0x57FF92UL)

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

/* ---- the timers and the stepper motors ----------------------------------
 * H'20D8AE, thirteen calls in a row and no depth below them. Three of the
 * five ITU channels, the four stepper motors, and the pins and latches that
 * hold their drivers awake.
 *
 * Each motor has a phase table in the code region and a small block of
 * state in RAM at H'11A83x. Three of them drive their phases through the
 * timing pattern controller, which shifts a byte from NDRA or NDRB onto the
 * port pins on a timer compare match rather than under the program's foot;
 * the fourth has its phases wired to one of the external latches instead
 * and is stepped by writing that latch.
 */

/* Motor state, one block each. The first byte is the phase index into the
 * table, and H'FFFEDx is the step the interrupt handler is to take next. */
#define MOTOR_A_PHASE   REG8(0x11A838UL)
#define MOTOR_B_PHASE   REG8(0x11A83EUL)
#define MOTOR_C_PHASE   REG8(0x11A844UL)
#define MOTOR_D_PHASE   REG8(0x11A849UL)

#define MOTOR_A_TABLE   ((const volatile u8 *)0x25080CUL)
#define MOTOR_B_TABLE   ((const volatile u8 *)0x250924UL)
#define MOTOR_C_TABLE   ((const volatile u8 *)0x250A24UL)
#define MOTOR_D_TABLE   ((const volatile u8 *)0x250A90UL)

#define LATCH_B         REG8(0x0C0000UL)
#define LATCH_B_SHADOW  REG8(0xFFFD39UL)
#define P9DDR_SHADOW    REG8(0xFFFD34UL)
#define PADDR_SHADOW    REG8(0xFFFD35UL)

/* H'20BF06, H'20BF32, H'20BF5E. ITU channels 0, 1 and 2: neither
 * synchronised nor in PWM, counting at the system clock and cleared by a
 * compare match on GRA. GRA is H'2AF8 on the first two, which at this
 * machine's clock is a tick a millisecond, and H'044C on the third, which
 * is about a tenth of that. GRB is parked at H'FFFF so it never matches. */
void itu0_init(void)
{
    TSNC &= (u8)~0x01;
    TMDR &= (u8)~0x01;
    TCR0  = 0x20;
    TIOR0 = 0x00;
    TCNT0 = 0x0000;
    GRB0  = 0xFFFF;
    GRA0  = 0x2AF8;
}

void itu1_init(void)
{
    TSNC &= (u8)~0x02;
    TMDR &= (u8)~0x02;
    TCR1  = 0x20;
    TIOR1 = 0x00;
    TCNT1 = 0x0000;
    GRB1  = 0xFFFF;
    GRA1  = 0x2AF8;
}

void itu2_init(void)
{
    TSNC &= (u8)~0x04;
    TMDR &= (u8)~0x04;
    TCR2  = 0x20;
    TIOR2 = 0x00;
    TCNT2 = 0x0000;
    GRB2  = 0xFFFF;
    GRA2  = 0x044C;
}

/* H'20BF8A. Port A whole and the top half of port B become outputs and go
 * high, and the pattern controller is told which of those pins it owns:
 * all eight of port A, the top four of port B. */
void tpc_ports_init(void)
{
    u8 v;

    PADDR = 0xFF;
    PADDR_SHADOW = 0xFF;

    v = (u8)(PBDDR_SHADOW | 0xF0);
    PBDDR = v;
    PBDDR_SHADOW = v;

    PADR = 0xFF;
    PBDR = (u8)(PBDR | 0xF0);

    NDERA = 0xFF;
    NDERB = 0xF0;
    TPCR  = 0x40;
    TPMR  = 0xF0;
}

/* H'20C022. Motor A: phases on the top half of port A, enable on bits 5
 * and 6 of the latch at H'0A0000. */
void motor_a_init(void)
{
    REG8(0x11A83CUL) = 0;
    REG8(0x11A83BUL) = 0;
    REG8(0x11A83AUL) = 0;
    REG8(0x11A835UL) = 0;
    REG8(0xFFFED2UL) = 0x01;
    REG8(0x11A839UL) = 0x01;

    NDRA = (u8)(NDRA & 0x0F);
    MOTOR_A_PHASE = 0;
    NDRA = (u8)(NDRA | MOTOR_A_TABLE[0]);

    {
        u8 v = (u8)(LATCH_A_SHADOW | 0x20); LATCH_A = v;
        v = (u8)(v | 0x40);                 LATCH_A = v;
        LATCH_A_SHADOW = v;
    }
}

/* H'20C092. Motor B: phases on the bottom half of port A, enable on bits 3
 * and 4 of the same latch. */
void motor_b_init(void)
{
    REG8(0x11A842UL) = 0;
    REG8(0x11A841UL) = 0;
    REG8(0x11A840UL) = 0;
    REG8(0x11A835UL) = 0;
    REG8(0xFFFED4UL) = 0x01;
    REG8(0x11A83FUL) = 0x01;

    NDRA = (u8)(NDRA & 0xF0);
    MOTOR_B_PHASE = 0;
    NDRA = (u8)(NDRA | MOTOR_B_TABLE[0]);

    {
        u8 v = (u8)(LATCH_A_SHADOW | 0x08); LATCH_A = v;
        v = (u8)(v | 0x10);                 LATCH_A = v;
        LATCH_A_SHADOW = v;
    }
}

/* H'20C102. Motor C: phases on the top half of port B, enable on bits 6 and
 * 7 of the latch at H'0C0000. NDRB takes the table byte whole -- the four
 * bits the controller does not own are masked off by NDERB. */
void motor_c_init(void)
{
    REG8(0x11A848UL) = 0;
    REG8(0x11A847UL) = 0;
    REG8(0x11A846UL) = 0;
    REG8(0x11A836UL) = 0;
    REG8(0xFFFED6UL) = 0x01;
    REG8(0x11A845UL) = 0x01;

    MOTOR_C_PHASE = 0;
    NDRB = MOTOR_C_TABLE[0];

    {
        u8 v = (u8)(LATCH_B_SHADOW | 0x40); LATCH_B = v;
        v = (u8)(v | 0x80);                 LATCH_B = v;
        LATCH_B_SHADOW = v;
    }
}

/* H'20C168. Motor D is the odd one out: its phases are not on a port the
 * pattern controller drives but in the low bits of the H'0C0000 latch, so
 * the phase and the two enable bits go out together. */
void motor_d_init(void)
{
    u8 v;

    REG8(0x11A84DUL) = 0;
    REG8(0x11A84CUL) = 0;
    REG8(0x11A84BUL) = 0;
    REG8(0x11A836UL) = 0;
    REG8(0xFFFED0UL) = 0x01;
    REG8(0x11A84AUL) = 0x01;

    MOTOR_D_PHASE = 0;

    v = (u8)(LATCH_B_SHADOW | MOTOR_D_TABLE[0]); LATCH_B = v;
    v = (u8)(v | 0x10);                          LATCH_B = v;
    v = (u8)(v | 0x20);                          LATCH_B = v;
    LATCH_B_SHADOW = v;
}

/* H'20BFC4. Every motor enable up, one bit at a time. */
void motor_enables_on(void)
{
    u8 v;

    v = (u8)(LATCH_A_SHADOW | 0x08); LATCH_A = v;
    v = (u8)(v | 0x10);              LATCH_A = v;
    v = (u8)(v | 0x20);              LATCH_A = v;
    v = (u8)(v | 0x40);              LATCH_A = v;
    LATCH_A_SHADOW = v;

    v = (u8)(LATCH_B_SHADOW | 0x40); LATCH_B = v;
    v = (u8)(v | 0x80);              LATCH_B = v;
    v = (u8)(v | 0x10);              LATCH_B = v;
    v = (u8)(v | 0x20);              LATCH_B = v;
    LATCH_B_SHADOW = v;
}

/* H'20BEEC. P9 bit 5 out and high. */
void p9_bit5_high(void)
{
    u8 v = (u8)(P9DDR_SHADOW | 0x20);

    P9DDR = v;
    P9DDR_SHADOW = v;
    P9DR |= 0x20;
}

/* H'20C1D4, H'20C20E, H'20C236. Compare-match A interrupt on, counter
 * running. Once these are on the motors step from interrupt level and
 * nothing in the foreground touches their phases again. */
void itu0_start(void)
{
    REG8(0xFFFED4UL) = 0x04;
    REG8(0xFFFED2UL) = 0x04;
    TIER0 = 0x01;
    TSTR |= 0x01;
}

void itu1_start(void)
{
    REG8(0xFFFED6UL) = 0x01;
    TIER1 = 0x01;
    TSTR |= 0x02;
}

void itu2_start(void)
{
    REG8(0xFFFED0UL) = 0x04;
    TIER2 = 0x01;
    TSTR |= 0x04;
}

void motors_and_timers_init(void)
{
    itu0_init();
    itu1_init();
    itu2_init();
    tpc_ports_init();
    motor_a_init();
    motor_b_init();
    motor_c_init();
    motor_d_init();
    motor_enables_on();
    p9_bit5_high();
    itu0_start();
    itu1_start();
    itu2_start();
}

/* ---- the main motor ------------------------------------------------------
 * H'20E054. The sewing motor is not stepped: it is driven from ITU channel
 * 4 in PWM mode, and this is what sets that up. GRB is the period and GRA
 * the mark, so writing GRA is how the speed is set from then on.
 *
 * The period goes down as H'2B34, which at this machine's clock is a
 * kilohertz. The mark goes down as H'2B98, which is longer than the period
 * and so never matches: the motor starts stopped, and the first real speed
 * is written by whatever asks for one.
 */
#define P6DDR_SHADOW    REG8(0xFFFD32UL)

/* H'20DA8C. The driver's enable and direction pins, and the state block
 * that goes with them. Each pin is set in the data register before its
 * direction register makes it an output, and set again afterwards. */
void main_motor_pins_init(void)
{
    u8 v;

    P4DR |= 0x01;
    v = (u8)(P4DDR_SHADOW | 0x01);
    P4DDR = v;
    P4DDR_SHADOW = v;
    P4DR |= 0x01;

    v = (u8)(P6DDR_SHADOW | 0x04);
    P6DDR = v;
    P6DDR_SHADOW = v;

    PCDR |= 0x80;
    P6DR |= 0x04;
    v = (u8)(PCDDR_SHADOW | 0x80);
    PCDDR = v;
    PCDDR_SHADOW = v;
    P6DR |= 0x04;

    PBDR &= (u8)~0x08;
    v = (u8)(PBDDR_SHADOW | 0x08);
    PBDDR = v;
    PBDDR_SHADOW = v;
    PBDR &= (u8)~0x08;

    REG8(0x11A852UL)  = 0;
    REG16(0x11A850UL) = 0;
    REG8(0x11A84EUL)  = 0;
    REG8(0xFFFEC6UL)  = 0x04;
    REG16(0xFFFECEUL) = 0;
}

/* H'20DF78. ITU channel 4 into PWM, unsynchronised, its buffer registers
 * turned off, its output enabled at the pin, and the counter started. */
void pwm4_init(void)
{
    TSNC &= (u8)~0x10;
    TMDR |= 0x10;
    TFCR &= 0xC3;

    TOER  = 0x02;
    TCR4  = 0x40;           /* count at the clock, clear on a GRB match */
    TIOR4 = 0x00;
    GRA4  = 0x2B98;         /* the mark -- longer than the period, so off */
    GRB4  = 0x2B34;         /* the period, a kilohertz */
    TIER4 = 0x00;

    TSTR |= 0x10;
}

/* H'20D8E4. The period interrupt on. Masked while it happens, because the
 * handler it arms reads the state byte cleared on the line above. */
void pwm4_interrupt_enable(void)
{
    REG8(0x11A853UL) = 0;
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    TIER4 |= 0x02;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

void main_motor_init(void)
{
    main_motor_pins_init();
    pwm4_init();
    pwm4_interrupt_enable();
}

/* ---- the foot control ----------------------------------------------------
 * H'209FC0. The pedal is an analog input like any other: the scan leaves its
 * reading at H'11A253 and this is what turns that into a speed.
 *
 * The reading is not used as a speed directly. It is first put into one of
 * six zones, which is the mode at H'FFFEC3 -- stopped, the two slow steps,
 * two running steps and full -- and only then turned into a number for the
 * PWM. Between the zones there is a hold in bit 0 of H'114DD7 so that a
 * pedal resting on a boundary does not chatter between two speeds.
 *
 * The arithmetic at the end is in single-precision floating point, which is
 * where the original's library at H'200530 and its neighbours came from.
 * The format is IEEE-754, the same one the compiler uses here, so this is
 * written as plain float and the library goes away.
 */
#define PEDAL_MODE      REG8(0xFFFEC3UL)    /* the zone, 0 to 5 */
#define PEDAL_LAST      REG8(0xFFFEC2UL)    /* the reading it came from */
#define PEDAL_FLAGS     REG8(0xFFFEC4UL)
#define MACHINE_FLAGS   REG8(0xFFFEC1UL)
#define SPEED_FLAGS     REG8(0xFFFEC7UL)
#define SPEED_STATE     REG8(0x114DD7UL)
#define HOLD_COUNT      REG16(0x114DDCUL)
#define SPEED_TARGET    REG8(0x114DD6UL)
#define SPEED_OUT       REG8(0xFFFEC8UL)
#define SPEED_TOP       REG8(0xFFFEC9UL)    /* from the settings block */
#define SPEED_BOTTOM    REG8(0xFFFECAUL)
#define SPEED_LIMIT     REG8(0xFFFECBUL)
#define MOTOR_MODE      REG8(0xFFFEC6UL)
#define SETTING_SPEED   REG8(0x57FF8DUL)
#define SETTING_LIMIT   REG16(0x57FF94UL)

/* H'20369A. Defined with the queue, below. */
void queue_group_restart(void);

/* H'20966A. Defined with the queue, below. */
void speed_target_service(void);

/* H'2037DC. A recognised pedal movement. The name this had in part 3q --
 * a click -- was a guess from the setting bit it tests; what it actually
 * does, once H'20369A was reconstructed, is send a running queue back to the
 * start of its group. */
void pedal_restart_check(void)
{
    REG8(0xFFFEFAUL) &= (u8)~0x10;
    if (REG8(0x114DCAUL) & 0x02) queue_group_restart();
}

/* H'200998. Hands the beeper a pattern: how long on, how long off, and how
 * many times. The first word is the period, which is the two added. */
void beep(u16 on, u16 off, u8 count)
{
    REG16(0x11A24AUL) = (u16)(off + on);
    REG16(0x11A24CUL) = on;
    REG16(0x11A24EUL) = off;
    REG8(0x11A250UL)  = count;
    MACHINE_FLAGS |= 0x80;
    P4DR |= 0x10;
}

/* H'20946E. The zone the pedal is in, with hysteresis.
 *
 * The six zones are H'00-H'23 stopped, H'24-H'41, H'42-H'56, H'57-H'63,
 * H'64-H'D1 and H'D2 up. Three of them mean one thing when every stepper is
 * idle and another when one is still moving, which is what the flag built
 * at the top stands for. Bit 0 of H'114DD7 is the hold: a zone that wants
 * to act sets it, and only acts on the reading after that.
 */
void pedal_scan(void)
{
    u8 mode = PEDAL_MODE;
    u8 reading;
    u8 idle = 1;
    u8 v;

    if (!((REG8(0x114DC6UL) & 0x01) && REG8(0xFFFED2UL) &&
          (REG8(0x114DC6UL) & 0x02) && REG8(0xFFFED4UL) &&
          (REG8(0x114DC6UL) & 0x20) && REG8(0xFFFED6UL))) {
        idle = 0;
    }

    /* The original pushes a second argument, H'0A, which H'20084A never
     * looks at. Dropped here. */
    reading = adc_get_result(2);

    if (reading < 0x24) {
        mode = 0;
        SPEED_STATE &= (u8)~0x01;
    }
    if (reading >= 0x24 && reading < 0x42 && (PEDAL_FLAGS & 0x10)) {
        v = SPEED_STATE;
        if (v & 0x01) { mode = 1; SPEED_STATE = (u8)(v & ~0x01); }
        else          {           SPEED_STATE = (u8)(v |  0x01); }
    }
    if (reading >= 0x42 && reading < 0x57 && (PEDAL_FLAGS & 0x10)) {
        mode = 2;
        SPEED_STATE &= (u8)~0x01;
    }
    if (reading >= 0x57 && reading < 0x64 && (PEDAL_FLAGS & 0x10)) {
        if (idle == 0) {
            v = SPEED_STATE;
            if (v & 0x01) { mode = 1; SPEED_STATE = (u8)(v & ~0x01); }
            else          {           SPEED_STATE = (u8)(v |  0x01); }
        } else {
            mode = 3;
            SPEED_STATE &= (u8)~0x01;
        }
    }
    if (reading >= 0x64 && reading < 0xD2 && (PEDAL_FLAGS & 0x10)) {
        if (idle == 0) {
            v = SPEED_STATE;
            if (v & 0x01) { mode = 1; SPEED_STATE = (u8)(v & ~0x01); }
            else          {           SPEED_STATE = (u8)(v |  0x01); }
        } else {
            mode = 4;
            SPEED_STATE &= (u8)~0x01;
        }
    }
    if (reading >= 0xD2 && (PEDAL_FLAGS & 0x10)) {
        if (idle == 0) {
            v = SPEED_STATE;
            if (v & 0x01) { mode = 1; SPEED_STATE = (u8)(v & ~0x01); }
            else          {           SPEED_STATE = (u8)(v |  0x01); }
        } else {
            mode = 5;
            SPEED_STATE &= (u8)~0x01;
        }
    }

    /* One click a movement, not one a scan. */
    if (mode == 0) {
        REG8(0x11A81FUL) = 0;
    } else if (REG8(0x11A81FUL) == 0) {
        pedal_restart_check();
        REG8(0x11A81FUL) = 1;
    }

    PEDAL_LAST = reading;

    if ((PEDAL_FLAGS & 0x20) && mode != 0 && MOTOR_MODE != 0x05) return;

    v = SPEED_STATE;
    if (v & 0x02) {
        if (mode != 0) {
            SPEED_STATE = (u8)(v & ~0x02);
            PEDAL_MODE = mode;
        }
    } else {
        if ((PEDAL_FLAGS & 0x01) && mode == 1) mode = 3;
        PEDAL_MODE = mode;
    }
}

/* H'2099FA. The pedal held back.
 *
 * While the machine is running and the pedal is held, H'114DDC counts. Past
 * about half a second it beeps three times to say it has been noticed, and
 * past a second it forces the mode to 5 and latches bit 1 so the release is
 * what takes effect rather than the hold. Letting go clears the count.
 */
void pedal_hold_service(void)
{
    u8 flags = PEDAL_FLAGS;
    u8 v;

    if (!(flags & 0x01) || (flags & 0x20) || (SPEED_FLAGS & 0x01) ||
        !(MACHINE_FLAGS & 0x08)) {
        flags = PEDAL_FLAGS;
        if ((flags & 0x01) && !(flags & 0x20)) return;
        SPEED_STATE &= (u8)~0x02;
        SPEED_STATE &= (u8)~0x04;
        return;
    }

    if (!(MACHINE_FLAGS & 0x02) || !(PEDAL_FLAGS & 0x10)) {
        HOLD_COUNT = 0;
        SPEED_STATE &= (u8)~0x04;
        return;
    }

    v = SPEED_STATE;
    if (v & 0x04) return;
    SPEED_STATE = (u8)(v | 0x04);

    if (PEDAL_MODE != 0) {
        PEDAL_MODE = 0;
        SPEED_STATE &= (u8)~0x02;
        return;
    }

    if (HOLD_COUNT > 0x01F4 && HOLD_COUNT < 0x0258) beep(0x0064, 0x00C8, 3);

    if (HOLD_COUNT >= 0x03E8) {
        PEDAL_MODE = 0x05;
        SPEED_STATE |= 0x02;
    } else {
        SPEED_STATE &= (u8)~0x04;
    }
}

/* H'20972C. The speed the zone asks for, before the settings are applied.
 * H'DC is full and H'19 is the slowest that turns the motor at all; bit 5 of
 * H'114DC8 is the setting that says every zone runs flat out. */
void speed_target_set(void)
{
    u8 mode;

    if (SPEED_STATE & 0x02) { SPEED_TARGET = 0xDC; return; }

    if (PEDAL_FLAGS & 0x01) {
        if (SPEED_LIMIT == 0) SPEED_TARGET = 0;
        return;
    }

    mode = PEDAL_MODE;
    if (mode == 0x00) {
        SPEED_TARGET = 0;
    } else if (mode == 0x01) {
        SPEED_TARGET = (REG8(0x114DC8UL) & 0x20) ? 0xDC : 0x19;
    } else if (mode == 0x02) {
        SPEED_TARGET = (REG8(0x114DC8UL) & 0x20) ? 0xDC : 0x00;
    } else if (mode == 0x03) {
        SPEED_TARGET = (REG8(0x114DC8UL) & 0x20) ? 0xDC : 0x19;
    } else if (mode == 0x04) {
        if (REG8(0x114DC8UL) & 0x20) {
            SPEED_TARGET = 0xDC;
        } else {
            /* The fourth zone is the one that follows the pedal rather than
             * standing at a step: the reading is stretched from H'64..H'D2
             * onto H'19..H'DC. */
            u16 t = (u16)(PEDAL_LAST - 0x64);
            SPEED_TARGET = (u8)((short)((u32)t * 0xC3 / 0x6E) + 0x19);
        }
    } else if (mode == 0x05) {
        SPEED_TARGET = 0xDC;
    } else {
        SPEED_TARGET = 0;
    }
}

/* H'209842. The target scaled by the settings, and out to the PWM.
 *
 * Two ratios come out of the settings block: H'57FF8D is the machine's top
 * speed and H'57FF94 its limit, both against H'DC. Everything is worked out
 * as a fraction of full and multiplied back at the end, which is why the
 * original reached for floating point over a range that never leaves a
 * byte.
 */
void speed_scale(void)
{
    u8 target = SPEED_TARGET;
    float full  = (float)(u32)SPEED_TOP / 220.0f;
    float limit = (float)(u32)(u16)(SETTING_LIMIT >> 2) / 220.0f;
    float factor;

    if (target >= 0x19) {
        if (SPEED_FLAGS & 0x80) {
            factor = 1.0f;
        } else if (SPEED_STATE & 0x02) {
            factor = (float)(u32)SPEED_LIMIT / (float)(u32)220;
            target = (u8)(int)((float)(u32)target * factor);
        } else {
            factor = ((float)(u32)SPEED_LIMIT + -25.0f) / (float)(u32)195;
            if (!(REG8(0x114DC7UL) & 0x80) && SPEED_LIMIT < 0x19) {
                factor = 0.0f;
            } else {
                factor = factor * limit;
            }
            target = (u8)(int)((float)(u32)(u8)(target - 0x19) * factor);
            target = (u8)(target + 0x19);
            if (REG8(0x114DCFUL) & 0x08) { if (target > 0x96) target = 0x96; }
            if (REG8(0x114DCDUL) & 0x01) { if (target > 0x96) target = 0x96; }
        }
    } else {
        target = 0;
    }

    SPEED_OUT = (u8)(int)((float)(u32)target * full);
}

/* H'2099D0. */
void speed_service(void)
{
    if (MOTOR_MODE == 0x05) {
        speed_target_service();
        SPEED_OUT = SPEED_TARGET;
    } else {
        speed_target_set();
        speed_scale();
    }
}

/* H'209AF0. The whole pedal path, skipped while bit 7 of H'FFFEF7 says
 * something else owns the motor. */
void pedal_service(void)
{
    if (!(REG8(0xFFFEF7UL) & 0x80)) {
        pedal_scan();
        pedal_hold_service();
        speed_service();
    }
}

/* H'209FC0. The starting state, and one pass through the pedal path so that
 * nothing downstream sees an uninitialised speed. */
void speed_control_init(void)
{
    u8 top;

    SPEED_LIMIT = 0xDC;

    top = SETTING_SPEED;
    SPEED_TOP = top;
    if (!(top >= 0x32 && top <= 0xFA)) {
        top = 0x8C;
        SPEED_TOP = top;
    }
    SPEED_BOTTOM = (u8)((short)((u16)(0x19 * top) / 0x00DC));

    SPEED_OUT   = 0x00;
    SPEED_FLAGS = 0x02;
    PEDAL_MODE  = 0x05;
    SPEED_STATE &= (u8)~0x02;

    pedal_service();
}

/* ---- the analog inputs ---------------------------------------------------
 * H'20A030. Everything the machine measures rather than switches goes
 * through the one converter: the foot control, two optical sensors, the
 * handwheel position and the supply. The channels are taken in a fixed
 * order, one conversion started each pass and the previous one's result
 * collected at the same time, so nothing ever waits for the converter.
 *
 * The results land in the array at H'11A251, indexed by channel, which is
 * what adc_get_result reads.
 */
#define INPUT_LATCH     REG8(0x080000UL)    /* the switches, on the bus */
#define ADC_STATE       REG8(0x11A818UL)    /* the channel in flight */
#define ADC_NEXT        REG8(0x11A819UL)    /* and the one after it */
#define SENSOR_A        REG8(0xFFFEF0UL)
#define SENSOR_B        REG8(0xFFFEF1UL)

/* H'250AFA and H'250AF6: the interrupt mask up and down. Both are two
 * instructions in the code region, called rather than inlined. */
void interrupts_off(void)
{
    __asm__ volatile("orc #0x80,ccr" ::: "cc", "memory");
}

void interrupts_on(void)
{
    __asm__ volatile("andc #0x7f,ccr" ::: "cc", "memory");
}

/* H'210DC6. Defined with the display, below. */
void message_beep(u16 msg);

/* H'209262, H'209286. One of the two sensors has read low for long enough
 * to mean something. Whether that is reported is a setting, in flash. */
void sensor_a_alarm(void)
{
    if (REG8(0x57EFD0UL) != 0) {
        REG8(0xFFFEF7UL) |= 0x02;
        message_beep(0);
    }
}

void sensor_b_alarm(void)
{
    if (REG8(0x57EFD2UL) != 0) {
        REG8(0xFFFEF7UL) |= 0x04;
        message_beep(0);
    }
}

/* H'2091F0. The reading each sensor starts from, and its counters cleared.
 * The second argument the original passes adc_get_result is ignored there,
 * so it is not passed here. */
void analog_baseline(void)
{
    SPEED_STATE |= 0x08;
    REG8(0xFFFEF7UL) &= (u8)~0x02;
    SENSOR_A = adc_get_result(4);
    REG8(0x11A814UL) = 0;
    REG8(0x11A815UL) = 0;

    SPEED_STATE |= 0x10;
    REG8(0xFFFEF7UL) &= (u8)~0x04;
    SENSOR_B = adc_get_result(6);
    REG8(0x11A816UL) = 0;
    REG8(0x11A817UL) = 0;
}

/* H'209D1A. One step of the round robin: collect what the channel named in
 * H'11A818 has finished and start the next one. The order is 0, 2, 3, 1, 5,
 * 7 and round again, and bit 1 of the H'0A0000 latch -- which selects what
 * is wired to the front of the converter -- is moved with it. The latch is
 * shared with the motor enables, so it is changed with interrupts off.
 */
void adc_sequence_step(void)
{
    u8 channel = ADC_STATE;
    u8 result;

    if (channel == 0x00) {
        result = adc_start_channel(0x00, 0x04);
        REG8(ADC_RESULTS + 0x00) = result;
        ADC_NEXT = 0x02;
        interrupts_off();
        LATCH_A_SHADOW |= 0x02;
        LATCH_A = LATCH_A_SHADOW;
        interrupts_on();
    } else if (channel == 0x02) {
        result = adc_start_channel(0x02, 0x04);
        REG8(ADC_RESULTS + 0x02) = result;
        ADC_NEXT = 0x03;
    } else if (channel == 0x03) {
        result = adc_start_channel(0x03, 0x04);
        REG8(ADC_RESULTS + 0x03) = result;
        ADC_NEXT = 0x01;
    } else if (channel == 0x01) {
        result = adc_start_channel(0x01, 0x04);
        REG8(ADC_RESULTS + 0x01) = result;
        ADC_NEXT = 0x05;
        interrupts_off();
        LATCH_A_SHADOW &= (u8)~0x02;
        LATCH_A = LATCH_A_SHADOW;
        interrupts_on();
    } else if (channel == 0x05) {
        result = adc_start_channel(0x05, 0x04);
        REG8(ADC_RESULTS + 0x05) = result;
        ADC_NEXT = 0x07;
    } else if (channel == 0x07) {
        result = adc_start_channel(0x07, 0x04);
        REG8(ADC_RESULTS + 0x07) = result;
        ADC_NEXT = 0x00;
    }
}

/* H'209C3A. Channel 4, and the count of how long it has read high. */
void adc_read_sensor_a(void)
{
    u8 result;

    ADC_STATE = 0x04;
    result = adc_start_channel(0x04, 0x06);
    REG8(ADC_RESULTS + 0x04) = result;
    ADC_STATE = 0x06;
    SENSOR_A = result;

    if (result >= 0x4C && REG8(0x11A814UL) < 0xC8) REG8(0x11A814UL)++;
}

/* H'209C96. Whatever the round robin left in flight, collected, and the
 * next one started. Bit 7 of H'114DC6 is the setting that says the second
 * sensor is fitted; without it the count is held at zero. */
void adc_read_sensor_b(void)
{
    u8 channel = ADC_STATE;
    u8 result;

    result = adc_start_channel(channel, ADC_NEXT);
    REG8(ADC_RESULTS + channel) = result;
    SENSOR_B = result;
    ADC_STATE = ADC_NEXT;

    if (REG8(0x114DC6UL) & 0x80) {
        if (SENSOR_B >= 0x4C && SENSOR_B <= 0x96 &&
            REG8(0x11A816UL) < 0xC8) {
            REG8(0x11A816UL)++;
        }
    } else {
        REG8(0x11A816UL) = 0;
    }
}

/* H'2092AA, H'209326. The two sensors turned into an alarm. Each acts once
 * a pass, held off by its own bit in H'114DD7, and only while H'FFFEC0 says
 * the machine is running. The first needs eight passes in a row before it
 * says anything; the second says it the first time. */
void sensor_a_service(void)
{
    u8 v;

    if (REG8(0xFFFEC0UL) != 0x01) {
        SPEED_STATE &= (u8)~0x08;
        return;
    }
    if (MACHINE_FLAGS & 0x20) return;

    v = SPEED_STATE;
    if (v & 0x08) return;
    SPEED_STATE = (u8)(v | 0x08);

    if (REG8(0x11A814UL) >= 0x01) {
        REG8(0x11A814UL) = 0;
        REG8(0x11A815UL) = 0;
    } else {
        REG8(0x11A814UL) = 0;
        REG8(0x11A815UL)++;
        if (REG8(0x11A815UL) >= 0x08) {
            sensor_a_alarm();
            REG8(0x11A815UL) = 0;
        }
    }
}

void sensor_b_service(void)
{
    u8 v;

    if (REG8(0xFFFEC0UL) != 0x01) {
        SPEED_STATE &= (u8)~0x10;
        return;
    }

    v = SPEED_STATE;
    if (v & 0x10) return;
    SPEED_STATE = (u8)(v | 0x10);

    if (REG8(0x11A816UL) >= 0x01) {
        REG8(0x11A816UL) = 0;
        REG8(0x11A817UL)++;
        if (REG8(0x11A817UL) >= 0x01) {
            sensor_b_alarm();
            REG8(0x11A817UL) = 0;
        }
    } else {
        REG8(0x11A816UL) = 0;
        REG8(0x11A817UL) = 0;
    }
}

/* H'209444. One switch straight through to a flag. */
void presser_switch_read(void)
{
    if (!(INPUT_LATCH & 0x08)) SPEED_FLAGS |= 0x01;
    else                       SPEED_FLAGS &= (u8)~0x01;
}

/* H'209B56. The two trim bytes kept in the settings store, offset by H'28
 * unless bit 1 of H'FFFEE3 says otherwise, and saturating rather than
 * wrapping. Written only when they have moved, because each write is a
 * bit-banged I2C exchange and the EEPROM only takes so many. */
void trim_to_eeprom(void)
{
    u8 step = 0x28;
    u8 a = REG8(0xFFFEF3UL);
    u8 b = REG8(0xFFFEF4UL);

    if (REG8(0xFFFEE3UL) & 0x02) step = 0;

    a = ((u16)a + step > 0x00FF) ? 0xFF : (u8)(a + step);
    b = ((u16)b + step > 0x00FF) ? 0xFF : (u8)(b + step);

    if (REG8(0x11A812UL) != a) {
        eeprom_write_verify(0xA9, a);
        REG8(0x11A812UL) = a;
    }
    if (REG8(0x11A813UL) != b) {
        eeprom_write_verify(0xAA, b);
        REG8(0x11A813UL) = b;
    }
}

/* H'209E48. One pass over everything analog.
 *
 * The handwheel on channel 7 comes first, and the number it turns into --
 * H'E1 minus the reading, scaled by H'A0/H'FF, offset by the trim and
 * scaled again by H'32/H'FF -- is the position the rest of the machine
 * works from. The original does that in 16-bit arithmetic and the middle
 * product overflows: H'E1 * H'A0 is 36000, which the sign extension before
 * the divide then reads as negative. It is written the same way here.
 *
 * Then the round robin, the sensors, and finally the switch latch on the
 * bus and four port pins copied bit for bit into the flag bytes.
 */
void analog_scan(void)
{
    u8 reading = adc_get_result(7);
    u16 p;
    u8 a, sum;
    u8 v;

    REG8(0xFFFEDEUL) = reading;

    p = (u16)((u16)(0x00E1 - reading) * (u16)0x00A0);
    a = (u8)((short)p / (short)0x00FF);
    sum = (u8)(INPUT_TRIM + a);
    p = (u16)((u16)0x32 * sum);
    REG8(0x11A811UL) = (u8)((short)p / (short)0x00FF);

    adc_sequence_step();
    adc_read_sensor_a();
    adc_read_sensor_b();
    sensor_a_service();
    sensor_b_service();
    presser_switch_read();

    if (INPUT_LATCH & 0x10) REG8(0xFFFEF8UL) &= (u8)~0x04;
    else                    REG8(0xFFFEF8UL) |=        0x04;

    if (PBDR & 0x02)        REG8(0xFFFEF8UL) &= (u8)~0x08;
    else                    REG8(0xFFFEF8UL) |=        0x08;

    if (INPUT_LATCH & 0x01) MACHINE_FLAGS |=        0x01;
    else                    MACHINE_FLAGS &= (u8)~0x01;

    if (P8DR & 0x02)        MACHINE_FLAGS |=        0x02;
    else                    MACHINE_FLAGS &= (u8)~0x02;

    if (INPUT_LATCH & 0x40) MACHINE_FLAGS |=        0x04;
    else                    MACHINE_FLAGS &= (u8)~0x04;

    if (INPUT_LATCH & 0x80) MACHINE_FLAGS |=        0x08;
    else                    MACHINE_FLAGS &= (u8)~0x08;

    if (P4DR & 0x20)        PEDAL_FLAGS |=        0x10;
    else                    PEDAL_FLAGS &= (u8)~0x10;

    if (REG8(0xFFFEC5UL) != 0x0F) trim_to_eeprom();

    v = MACHINE_FLAGS;
    if (v & 0x10) P4DR &= (u8)~0x08; else P4DR |=        0x08;
    if (v & 0x20) P4DR |=        0x02; else P4DR &= (u8)~0x02;
    if (v & 0x40) P4DR |=        0x04; else P4DR &= (u8)~0x04;
    if (v & 0x80) P4DR |=        0x10; else P4DR &= (u8)~0x10;
}

/* H'20A030. Four P4 pins to outputs, the trim taken from the settings
 * block, the outputs to a known state, and seven passes of the scan so that
 * every channel has a real reading before anything reads one. */
void analog_input_init(void)
{
    u8 v = P4DDR_SHADOW;
    u8 i;

    v = (u8)(v | 0x02); P4DDR = v;
    v = (u8)(v | 0x04); P4DDR = v;
    v = (u8)(v | 0x08); P4DDR = v;
    v = (u8)(v | 0x10); P4DDR = v;
    P4DDR_SHADOW = v;

    INPUT_TRIM = SETTING_TRIM;

    P4DR &= (u8)~0x08;
    P4DR |=        0x04;
    P4DR &= (u8)~0x02;
    MACHINE_FLAGS &= (u8)~0x80;
    P4DR &= (u8)~0x10;

    analog_baseline();

    for (i = 7; i != 0; i--) analog_scan();
}

/* ---- the front panel keys ------------------------------------------------
 * H'20ACC8. The keys are a matrix: three strobes on port C -- bits 0, 1 and
 * 6 -- and eight returns read from a latch on the bus at H'060000, active
 * low, so what is stored is the complement. Two of the analog channels come
 * in on the same pass: they are the two knobs, and they are treated as part
 * of the panel rather than as measurements.
 *
 * The whole thing is read ten times over. A bank that reads differently from
 * the pass before is zeroed rather than believed, so only a key held still
 * for ten passes survives to the end; the knobs must stay within two counts
 * over the same ten. That is the debounce, and it is why the bring-up
 * spends a scan here before it trusts anything.
 */
#define KEY_LATCH       REG8(0x060000UL)
#define KEY_PASS        REG8(0x11A803UL)    /* 0 to 9 */
#define KEY_HOLDOFF     REG8(0x11A802UL)    /* set after a key is seen */

/* H'208E2A. The two knobs. Below H'05 is not a real reading, so it becomes
 * H'02 -- which is also what the debounce writes when they move. */
void knobs_read(void)
{
    u8 v;

    v = adc_get_result(0);
    REG8(0x11A80BUL) = v;
    if (v < 0x05) REG8(0x11A80BUL) = 0x02;

    v = adc_get_result(1);
    REG8(0x11A80AUL) = v;
    if (v < 0x05) REG8(0x11A80AUL) = 0x02;
}

/* H'208E7A. One pass over the three strobes. Each is made an output and
 * driven low on its own, the latch read, and the three put back high
 * together. The knobs are read on the odd passes. */
void key_banks_read(void)
{
    u8 v;

    v = (u8)(PCDDR_SHADOW | 0x01); PCDDR = v;
    PCDR &= (u8)~0x01;
    REG8(0x11A804UL) = (u8)~KEY_LATCH;
    PCDDR_SHADOW = v;
    port_c_bits_high();

    v = (u8)(PCDDR_SHADOW & (u8)~0x01); PCDDR = v;
    v = (u8)(v | 0x02);                 PCDDR = v;
    PCDR &= (u8)~0x02;
    REG8(0x11A805UL) = (u8)~KEY_LATCH;
    PCDDR_SHADOW = v;
    port_c_bits_high();

    v = (u8)(PCDDR_SHADOW & (u8)~0x02); PCDDR = v;
    v = (u8)(v | 0x40);                 PCDDR = v;
    PCDR &= (u8)~0x40;
    REG8(0x11A806UL) = (u8)~KEY_LATCH;
    PCDDR_SHADOW = v;
    port_c_bits_high();

    v = (u8)(PCDDR_SHADOW & (u8)~0x40); PCDDR = v;
    PCDDR_SHADOW = v;

    if (KEY_PASS & 0x01) knobs_read();
}

/* H'208F18. What survives the pass: a bank that has changed is thrown away
 * rather than kept, and a knob that has moved by more than two is put back
 * to H'02, which cannot pass the test at the end. */
void key_scan_compare(void)
{
    short a, b;

    if (REG8(0x11A807UL) != REG8(0x11A804UL)) REG8(0x11A807UL) = 0;
    if (REG8(0x11A808UL) != REG8(0x11A805UL)) REG8(0x11A808UL) = 0;
    if (REG8(0x11A809UL) != REG8(0x11A806UL)) REG8(0x11A809UL) = 0;

    a = (short)REG8(0x11A80AUL);
    b = (short)REG8(0x11A80CUL);
    if (!((short)(a - 2) <= b && (short)(a + 2) >= b)) REG8(0x11A80CUL) = 0x02;

    a = (short)REG8(0x11A80BUL);
    b = (short)REG8(0x11A80DUL);
    if (!((short)(a - 2) <= b && (short)(a + 2) >= b)) REG8(0x11A80DUL) = 0x02;
}

/* H'209072. The first pass: read, and keep what was read as the reference
 * every later pass is measured against. */
void key_scan_first(void)
{
    KEY_PASS++;
    key_banks_read();
    REG8(0x11A807UL) = REG8(0x11A804UL);
    REG8(0x11A808UL) = REG8(0x11A805UL);
    REG8(0x11A809UL) = REG8(0x11A806UL);
    REG8(0x11A80CUL) = REG8(0x11A80AUL);
    REG8(0x11A80DUL) = REG8(0x11A80BUL);
}

/* H'208FCC, and H'2090C4 to H'2090FC, which are eight entries that do
 * nothing but reach it. */
void key_scan_again(void)
{
    KEY_PASS++;
    key_banks_read();
    key_scan_compare();
}

/* H'209104. The tenth pass, which publishes.
 *
 * The three banks go to H'FFFEDB..H'FFFEDD. If any key came through, a
 * hold-off of H'96 goes into H'11A802 and no further scan starts until an
 * interrupt has counted it down -- that is the auto-repeat rate. The knobs
 * are published only when both are above H'05, and only once, latched by
 * bit 1 of H'11A80E so that holding a knob still does not keep re-sending
 * it. Bit 7 of H'FFFEF7 is something else owning the panel: then everything
 * published this pass is taken back, unless a key in the second bank asked
 * for it.
 */
void key_scan_finish(void)
{
    u8 bank0, bank2;

    KEY_PASS = 0;
    key_banks_read();
    key_scan_compare();

    bank0 = REG8(0x11A807UL);
    REG8(0xFFFEDCUL) = REG8(0x11A808UL);
    bank2 = REG8(0x11A809UL);
    REG8(0xFFFEDDUL) = bank2;
    REG8(0xFFFEDBUL) = bank0;

    if (bank0 == 0 && REG8(0xFFFEDCUL) == 0 && bank2 == 0) KEY_HOLDOFF = 0;
    else                                                   KEY_HOLDOFF = 0x96;

    if (REG8(0x11A80CUL) <= 0x05 || REG8(0x11A80DUL) <= 0x05) {
        REG8(0x11A80EUL) &= (u8)~0x02;
        REG8(0xFFFEDAUL) = 0;
        REG8(0xFFFED9UL) = 0;
    } else if (!(REG8(0x11A80EUL) & 0x02)) {
        REG8(0x11A80EUL) |= 0x02;
        REG8(0xFFFED9UL) = REG8(0x11A80CUL);
        REG8(0xFFFEDAUL) = REG8(0x11A80DUL);
    }

    if ((REG8(0xFFFEF7UL) & 0x80) && !(REG8(0xFFFEDCUL) & 0x20)) {
        REG8(0xFFFEDAUL) = 0;
        REG8(0xFFFED9UL) = 0;
        REG8(0xFFFEDDUL) = 0;
        REG8(0xFFFEDCUL) = 0;
        REG8(0xFFFEDBUL) = 0;
    }
}

/* H'20901A. One pass, or nothing while the hold-off is running. */
void key_scan_step(void)
{
    if (KEY_HOLDOFF != 0) return;

    switch (KEY_PASS) {
    case 0x00: key_scan_first(); break;
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06: case 0x07: case 0x08: key_scan_again(); break;
    case 0x09: key_scan_finish(); break;
    default: break;
    }
}

/* H'20ACC8. A whole settled scan, then what the machine makes of it.
 *
 * Bit 2 of the first bank is the one that matters here: it says the
 * embroidery module is attached, and its absence is what decides whether
 * H'FFFEC5 -- which analog_scan reads before it will write the trim back to
 * the settings store -- is left at zero or set to 6.
 */
void keys_scan_settled(void)
{
    do {
        key_scan_step();
    } while (KEY_PASS != 0);

    if (REG8(0xFFFEDBUL) & 0x04) {
        PEDAL_FLAGS |= 0x80;
        REG8(0xFFFEC5UL) = 0;
    } else {
        u8 v = REG8(0xFFFEDDUL);
        if (!(v & 0x08)) {
            PEDAL_FLAGS &= (u8)~0x80;
            REG8(0xFFFEC5UL) = 0;
        } else if (v & 0x20) {
            PEDAL_FLAGS |= 0x80;
            REG8(0xFFFEC5UL) = 0x06;
        }
    }
}

/* ---- the stitch database -------------------------------------------------
 * Three things hold a stitch pattern between them.
 *
 * The catalogue is a table of 24-byte descriptors, based at the pointer in
 * H'114DD2 and indexed by pattern number. Two of its fields are pointers to
 * the pattern's own data -- one at +0 and one at +4 -- and which of the two
 * is used is decided by H'57FF80 in the settings block, the byte
 * config_block_check writes as H'B4. A descriptor also carries a kind at
 * +H'16.
 *
 * The pattern data itself is a record whose bytes are read by index. Bit 7
 * of its byte 7 says it is indirect: the record proper is then at the
 * pointer in its byte H'14, and the index applies there instead.
 *
 * The working copy is 16 bytes per pattern at H'0E4010, indexed by pattern
 * number shifted left four -- H'0E4010 to H'0E8000, which is the block part
 * 2 found the start-up clearing. This is where a pattern's parameters live
 * once the operator has changed them, and byte 0 is its own kind, which is
 * what says whether the working copy or the catalogue is to be believed.
 *
 * Six parameters travel together. Each has a reader here and all six are
 * written by one setter, and they are identified by where they sit in the
 * working copy:
 *
 *   working  data   default        reader
 *   +H'01    +H'09  H'57B6D6       stitch_param_1
 *   +H'02/3  +H'0B  H'57B6D7       stitch_param_2
 *   +H'04    +H'0D  H'57B6D9       stitch_param_4   (low nibble only)
 *   +H'05    +H'0F  --             stitch_param_5
 *   +H'06    --     --             stitch_param_6
 *   +H'07/8  --     --             stitch_param_7
 *
 * The default tables are four bytes a pattern in flash. Two of the six have
 * a second working byte, and bit 6 of H'11A7BD picks between the pair -- one
 * mode's value and the other's, kept side by side so that switching modes
 * does not lose either.
 */
#define STITCH_TABLE    REG32(0x114DD2UL)   /* -> the 24-byte descriptors */
#define STITCH_WORKING  0x0E4010UL          /* 16 bytes a pattern */
#define STITCH_ALT_MODE (REG8(0x11A7BDUL) & 0x40)
#define STITCH_SET      REG8(0x57FF80UL)    /* which of the two data sets */

/* The descriptor for a pattern, and the base of its working copy. */
static u32 stitch_record(u16 n)
{
    return STITCH_TABLE + (u32)((u16)0x0018 * n);
}

static u32 stitch_work(u16 n)
{
    return STITCH_WORKING + (u32)(u16)(n << 4);
}

/* H'202A3E. One byte out of a pattern's data, by index. */
u8 pattern_byte(u16 n, u16 index)
{
    u32 p;

    if (STITCH_SET == 0xAA) p = REG32(stitch_record(n));
    else                    p = REG32(stitch_record(n) + 4);

    if (REG8(p + 7) & 0x80) p = REG32(p + 0x14);

    return REG8(p + index);
}

/* Which kind to answer by. The working copy's own kind wins, but only when
 * it has one and only when the caller asked for it; otherwise the
 * catalogue's. This is the head of all six readers. */
static u8 stitch_kind(u16 n, u8 use_working)
{
    u8 kind = (u8)(REG8(stitch_work(n)) & 0x3F);

    if (kind == 0 || use_working == 0) kind = REG8(stitch_record(n) + 0x16);
    return kind;
}

/* H'202DA4. */
u8 stitch_param_1(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x80: return REG8(0x57B6D6UL + (u32)(u16)(n << 2));
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 1);
    default:
        return pattern_byte(n, 0x0009);
    }
}

/* H'202C30. */
u8 stitch_param_2(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x80:
        return REG8(0x57B6D7UL + (u32)(u16)(n << 2));
    case 0x81:
        if (!STITCH_ALT_MODE) return REG8(stitch_work(n) + 2);
        return REG8(0x57B6D7UL + (u32)(u16)(n << 2));
    case 0x82:
        if (STITCH_ALT_MODE) return REG8(stitch_work(n) + 3);
        return REG8(stitch_work(n) + 2);
    case 0x01:
        if (!STITCH_ALT_MODE) return REG8(stitch_work(n) + 2);
        return pattern_byte(n, 0x000B);
    case 0x02:
        if (STITCH_ALT_MODE) return REG8(stitch_work(n) + 3);
        return REG8(stitch_work(n) + 2);
    default:
        return pattern_byte(n, 0x000B);
    }
}

/* H'202B36. The only one whose data byte holds two things: the low nibble
 * is this parameter and the high nibble is something else. */
u8 stitch_param_4(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x80: return REG8(0x57B6D9UL + (u32)(u16)(n << 2));
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 4);
    default:
        return (u8)(pattern_byte(n, 0x000D) & 0x0F);
    }
}

/* H'202E9A. Leaves the pattern number behind at H'11A672, which the
 * original does whether or not it goes on to use it. */
u8 stitch_param_5(u16 n, u8 use_working)
{
    u8 kind;

    kind = (u8)(REG8(stitch_work(n)) & 0x3F);
    REG16(0x11A672UL) = n;
    if (kind == 0 || use_working == 0) kind = REG8(stitch_record(n) + 0x16);

    switch (kind) {
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 5);
    default:
        return pattern_byte(n, 0x000F);
    }
}

/* H'202F98. No catalogue value and no default: outside the working copy
 * this parameter simply does not exist. */
u8 stitch_param_6(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x81: case 0x82: case 0x01: case 0x02:
        return REG8(stitch_work(n) + 6);
    default:
        return 0;
    }
}

/* H'203062. */
u8 stitch_param_7(u16 n, u8 use_working)
{
    switch (stitch_kind(n, use_working)) {
    case 0x81: case 0x01:
        if (!STITCH_ALT_MODE) return REG8(stitch_work(n) + 7);
        return 0;
    case 0x82: case 0x02:
        if (STITCH_ALT_MODE) return REG8(stitch_work(n) + 8);
        return REG8(stitch_work(n) + 7);
    default:
        return 0;
    }
}

/* H'206724. All six back into the working copy, each behind its own "this
 * one is present" flag, and the working copy's kind set to say it now holds
 * something. The kind becomes 2 in the alternate mode and 1 otherwise; bit 7
 * of the kind byte is the one thing kept across the write.
 *
 * The leading argument is a mark: when it is set, something elsewhere is
 * told the pattern has been edited.
 */
void stitch_params_set(u8 mark, u16 n,
                       u8 f1, u8 v1, u8 f2, u8 v2, u8 f3, u8 v3,
                       u8 f4, u8 v4, u8 f5, u8 v5, u8 f6, u8 v6,
                       u8 f7, u8 v7)
{
    u32 w = stitch_work(n);
    u8 kind = (u8)(REG8(w) & 0x3F);

    if (kind == 0) kind = 0x01;

    if (mark != 0) {
        REG8(0x11A6AEUL) |= 0x01;
        REG8(0x114DCDUL) &= (u8)~0x08;
    }

    if (f1 != 0) REG8(w + 1) = v1;

    if (STITCH_ALT_MODE) {
        if (f2 != 0) REG8(w + 3) = v2;
        if (f5 != 0) REG8(w + 8) = v5;
        kind = 0x02;
    } else {
        if (f2 != 0) REG8(w + 2) = v2;
        if (f5 != 0) REG8(w + 7) = v5;
    }

    if (f3 != 0) REG8(w + 4) = v3;
    if (f4 != 0) REG8(w + 5) = v4;
    if (f6 != 0) REG8(w + 6) = v6;
    if (f7 != 0) REG8(w + 9) = v7;

    REG8(w) = (u8)(REG8(w) & 0x80);
    REG8(w) = (u8)(REG8(w) | kind);
}

/* One pattern's six parameters read out of the catalogue and written into
 * its working copy. Reading with use_working clear is what makes this a
 * reload of the defaults rather than a copy of what is already there. */
static void stitch_params_reload_one(u16 n)
{
    u8 p6 = stitch_param_6(n, 0);
    u8 p7 = stitch_param_7(n, 0);
    u8 p5 = stitch_param_5(n, 0);
    u8 p4 = stitch_param_4(n, 0);
    u8 p2 = stitch_param_2(n, 0);
    u8 p1 = stitch_param_1(n, 0);

    stitch_params_set(0, n, 1, p1, 1, p2, 1, p4, 1, p5, 1, p7, 1, p6, 1, 0);
}

/* H'2086FC. The two reserved patterns, H'03FE and H'03FF. */
void stitch_params_reload(void)
{
    stitch_params_reload_one(0x03FE);
    stitch_params_reload_one(0x03FF);
}

/* ---- making a pattern current --------------------------------------------
 * H'208210 and everything under it. Choosing a stitch is not a matter of
 * setting one number: the pattern's parameters have to be brought out of the
 * database into the live sewing block, the limits and the motor positions
 * recomputed from them, and the panel told what has changed. This is the
 * layer that does it, built from the bottom up.
 *
 * Three blocks of RAM matter throughout:
 *
 *   H'FFFEE2..H'FFFEFF  the live sewing parameters, what the interrupt
 *                       handlers and the motors read
 *   H'11A7BC..H'11A7EF  the pattern's own copy, as selected
 *   H'11A6A2..H'11A6AC  a saved set, so a temporary change can be undone
 *
 * H'11A6AE collects "this changed" bits for whatever redraws the panel.
 */

/* H'200D24, H'200D34. The two parameters the knobs drive, each held in a
 * pair of bytes and stored at twice the value it is given. Which of the two
 * is the stitch width and which the length is not established here. */
u8 sew_param_a_set(u8 v)
{
    v = (u8)(v << 1);
    REG8(0x11A6D4UL) = v;
    REG8(0x11A6D3UL) = v;
    return v;
}

u8 sew_param_b_set(u8 v)
{
    v = (u8)(v << 1);
    REG8(0x11A6D6UL) = v;
    REG8(0x11A6D5UL) = v;
    return v;
}

/* H'201748. Walks back down the catalogue while byte +H'17 of the
 * descriptor reads 1, and hands back the first value that is not. Byte
 * H'17 marks a pattern as a continuation of the one before it, so this is
 * how the head of a run is found -- though what it returns is the marker,
 * not where it stopped. */
u16 stitch_run_head(u16 n)
{
    u8 mark = REG8(stitch_record(n) + 0x17);

    while (mark == 0x01) {
        n--;
        mark = REG8(stitch_record(n) + 0x17);
    }
    return mark;
}

/* H'2021F6, H'202212. Two of the "busy" bits let go. */
void sew_clear_busy_3(void)
{
    REG8(0x114DC9UL) &= (u8)~0x08;
    REG8(0x11A6AFUL) = 0;
}

void sew_clear_busy_4(void)
{
    REG8(0x114DC9UL) &= (u8)~0x10;
    REG8(0x11A6AFUL) = 0;
}

/* H'202136. */
void sew_stop_flags_clear(void)
{
    REG8(0x114DC6UL) &= (u8)~0x40;
    REG8(0x114DC7UL) &= (u8)~0x80;
    REG8(0xFFFEF8UL) &= (u8)~0x20;
    REG8(0x11A6CFUL) = 0;
}

/* H'202438. */
void sew_flag_copy_6(void)
{
    if (REG8(0xFFFEF5UL) & 0x40) REG8(0x11A7D4UL) |= 0x40;
}

/* H'206636, H'206652. Two bytes of a block on the bus at H'0E0000. */
u8 bus_byte_10(void) { return REG8(0x0E0010UL); }
u8 bus_byte_11(void) { return REG8(0x0E0011UL); }

/* H'2023BA. The pattern's copy out to the live parameters. */
void sew_params_publish(void)
{
    REG8(0xFFFEE2UL) = REG8(0x11A7BCUL);
    REG8(0xFFFEE3UL) = REG8(0x11A7BDUL);
    REG8(0x11A69FUL) = REG8(0x11A7BFUL);
    REG8(0xFFFEE6UL) = REG8(0x11A7C0UL);
    REG8(0x11A6A1UL) = REG8(0x11A7C1UL);
    REG8(0xFFFEE9UL) = REG8(0x11A7C2UL);
    REG8(0xFFFEEDUL) = REG8(0x11A7C5UL);
    REG8(0xFFFEEBUL) = REG8(0x11A7C4UL);
    REG8(0xFFFEEEUL) = REG8(0x11A7C6UL);
    REG8(0xFFFEEFUL) = REG8(0x11A7C7UL);
}

/* H'2068CE, H'206A6A. Eleven bytes put aside and put back. What is saved is
 * the live set; what is restored writes both halves of the two pairs. */
void sew_params_save(void)
{
    REG8(0x11A6A2UL) = REG8(0xFFFEEAUL);
    REG8(0x11A6A3UL) = REG8(0x11A69EUL);
    REG8(0x11A6A4UL) = REG8(0x11A6D3UL);
    REG8(0x11A6A5UL) = REG8(0x11A69FUL);
    REG8(0x11A6A6UL) = REG8(0xFFFEE6UL);
    REG8(0x11A6A7UL) = REG8(0x11A6A0UL);
    REG8(0x11A6A8UL) = REG8(0x11A6D5UL);
    REG8(0x11A6A9UL) = REG8(0x11A6A1UL);
    REG8(0x11A6AAUL) = REG8(0xFFFEE9UL);
    REG8(0x11A6ABUL) = REG8(0xFFFEECUL);
    REG8(0x11A6ACUL) = REG8(0xFFFEEDUL);
}

void sew_params_restore(void)
{
    u8 v;

    REG8(0xFFFEEAUL) = REG8(0x11A6A2UL);
    REG8(0x11A69EUL) = REG8(0x11A6A3UL);
    v = REG8(0x11A6A4UL);
    REG8(0x11A6D4UL) = v;
    REG8(0x11A6D3UL) = v;
    REG8(0x11A69FUL) = REG8(0x11A6A5UL);
    REG8(0xFFFEE6UL) = REG8(0x11A6A6UL);
    REG8(0x11A6A0UL) = REG8(0x11A6A7UL);
    v = REG8(0x11A6A8UL);
    REG8(0x11A6D6UL) = v;
    REG8(0x11A6D5UL) = v;
    REG8(0x11A6A1UL) = REG8(0x11A6A9UL);
    REG8(0xFFFEE9UL) = REG8(0x11A6AAUL);
    REG8(0xFFFEECUL) = REG8(0x11A6ABUL);
    REG8(0xFFFEEDUL) = REG8(0x11A6ACUL);
}

/* H'202456. The live parameters copied the other way, into the pattern's
 * copy, with three of them unpacked out of the bytes they share. */
void sew_params_capture(void)
{
    u8 v;

    v = REG8(0xFFFEF5UL);
    REG8(0x11A7D4UL) = v;
    REG8(0x11A7E0UL) = (u8)(v & 0x03);

    v = REG8(0xFFFEF6UL);
    REG8(0x11A7D5UL) = v;
    REG8(0x11A7E1UL) = (u8)((v >> 6) & 0x03);
    REG8(0x11A7E2UL) = (u8)(REG8(0xFFFEF6UL) & 0x0F);

    REG8(0x11A7D6UL) = REG8(0xFFFEF7UL);
    REG8(0x11A7D7UL) = REG8(0xFFFEF8UL);
    REG8(0x11A7D8UL) = REG8(0xFFFEEAUL);
    REG8(0x11A7D9UL) = REG8(0x11A69EUL);
    REG8(0x11A7DAUL) = REG8(0x11A6A0UL);
    REG8(0x11A7DBUL) = REG8(0xFFFEECUL);
    REG8(0x11A7DCUL) = REG8(0xFFFEFBUL);
    REG8(0x11A7DDUL) = REG8(0xFFFEFCUL);

    REG8(0x11A7DEUL) = (u8)((REG8(0xFFFEF9UL) & 0xF0) >> 4);
    REG8(0x11A7DFUL) = (u8)(REG8(0xFFFEF9UL) & 0x0F);
}

/* H'20251C. Eighteen bytes of the pattern's copy loaded from a row of a
 * table of pointers at H'11A6E8, chosen by H'11A7A8. */
void sew_variant_load(void)
{
    u8 i;
    u32 row;

    for (i = 0; i < 0x12; i++) {
        row = REG32(0x11A6E8UL + (u32)(short)(u16)(REG8(0x11A7A8UL) << 2));
        REG8(0x11A7B6UL + i) = REG8(row + i);
    }
    REG8(0x11A7E3UL) = REG8(0x11A7C0UL);
    REG8(0x11A7E4UL) = REG8(0x11A7C2UL);
}

/* H'2079E0. Which of the two data sets is in use decides whether the
 * alternate-mode bit is allowed to be on at all. */
void sew_mode_arbitrate(void)
{
    u8 v;

    if (STITCH_SET == 0xAA) {
        REG8(0xFFFEF8UL) &= (u8)~0x10;
        return;
    }

    v = REG8(0xFFFEF8UL);
    if ((v & 0x04) || (v & 0x08)) {
        REG8(0xFFFEF8UL) = (u8)(v | 0x10);
        if (REG8(0x11A7BDUL) & 0x02) REG8(0x114DCEUL) |= 0x40;
        else                         REG8(0x114DCEUL) &= (u8)~0x40;
    }

    if (REG8(0x114DCEUL) & 0x40) REG8(0xFFFEF8UL) |=        0x10;
    else                         REG8(0xFFFEF8UL) &= (u8)~0x10;
}

/* H'205E82, H'205EB6, H'205F0C, H'20602E. Four small watchers: each notices
 * that something has moved and raises a bit in H'11A6AE, or drives a pin. */
void sew_watch_7DE(void)
{
    if (REG8(0x11A7EFUL) != REG8(0x11A7DEUL)) {
        REG8(0x11A6AEUL) |= 0x02;
        REG8(0x11A7EFUL) = REG8(0x11A7DEUL);
    }
}

void sew_watch_7E1(void)
{
    u8 v = REG8(0x11A7E1UL);

    if (v == 0x02 || v == 0x03) {
        if (!(REG8(0x114DCAUL) & 0x20)) {
            REG8(0x11A6AEUL) |= 0x02;
            REG8(0x114DCAUL) |= 0x20;
        }
    } else {
        if (REG8(0x114DCAUL) & 0x20) {
            REG8(0x11A6AEUL) |= 0x02;
            REG8(0x114DCAUL) &= (u8)~0x20;
        }
    }
}

void sew_needle_stop_pin(void)
{
    if (REG8(0x11A7D5UL) & 0x20) {
        REG8(0xFFFEC7UL) |=        0x02;
        REG8(0xFFFEC7UL) &= (u8)~0x04;
    } else {
        REG8(0xFFFEC7UL) &= (u8)~0x02;
        REG8(0xFFFEC7UL) |=        0x04;
    }
}

void sew_watch_67D(void)
{
    u8 on = (REG8(0x11A67DUL) & 0x20) != 0;

    if (REG8(0x114DCDUL) & 0x01) on = !on;

    if (on) REG8(0x11A6AEUL) |=        0x20;
    else    REG8(0x11A6AEUL) &= (u8)~0x20;
}

/* H'205A80. A limit derived from a setting in flash and the pattern's own
 * byte, clamped into H'00..H'28 and then subtracted from H'29. */
void sew_limit_6BB(void)
{
    u8 setting = REG8(0x57FF8FUL);
    short t;

    if (setting > 0x10) setting = 0x08;

    t = (short)(signed char)(u8)(REG8(0x11A7DBUL) + setting + 0xF8);
    if (t < 0) t = 0;
    if (t > 0x28) t = 0x28;

    REG8(0x11A6BBUL) = (u8)(0x29 - t);
}

/* H'2051AC. Interpolates between two ends by a percentage, and offsets the
 * result by H'11A6C2. The multiply is 16-bit and its product is sign
 * extended before the divide, the same as elsewhere in this ROM. */
u8 sew_interpolate(u8 percent, u8 from, u8 to)
{
    u16 span = (u16)((u16)from - (u16)to);
    u16 p    = (u16)(span * (u16)percent);
    short q  = (short)((short)p / (short)0x0064);

    return (u8)((u8)((u8)q + REG8(0x11A6C2UL)) + to);
}

/* H'2051EA. The offset the interpolation above works from. */
void sew_offset_set(void)
{
    u8 a = REG8(0x11A7D8UL);
    u8 top = 0x64;
    u8 b = REG8(0x11A7D9UL);
    u8 extra = 0;
    u16 t;

    if (STITCH_SET == 0xB4 && !(REG8(0xFFFEF8UL) & 0x10)) {
        if (b > 0x38) b = 0x38;
        top = 0x38;
        extra = (u8)((short)((short)(0x0064 - (u16)top) / (short)2));
    }

    t = (u16)((u16)((u16)top - (u16)b) * (u16)a);
    REG8(0x11A6C2UL) = (u8)((u8)((short)((short)t / (short)10)) + extra);
}

/* H'2055B6. How far past one of four limits the free-running counter at
 * H'FFFF00 has gone, or zero if it is still inside. */
u16 sew_limit_overshoot(void)
{
    u16 now = REG16(0xFFFF00UL);
    u8 which = REG8(0x11A7ADUL);
    u16 edge;

    if (which == 0x00) {
        edge = REG16(0x11A7AEUL);
        if (now >= edge) return (u16)(now - edge);
    } else if (which == 0x01) {
        edge = REG16(0x11A7B0UL);
        if (now <= edge) return (u16)(edge - now);
    } else if (which == 0x02) {
        edge = REG16(0x11A7B2UL);
        if (now >= edge) return (u16)(now - edge);
    } else if (which == 0x03) {
        edge = REG16(0x11A7B4UL);
        if (now <= edge) return (u16)(edge - now);
    }
    return 0;
}

/* H'202318. Two of the live parameters scaled from the pattern's units into
 * the panel's, H'12 per unit rounded, capped at H'64 -- but only for the
 * H'B4 data set in the ordinary mode. Otherwise they go across as they are.
 * Bit 7 of H'FFFEE5 belongs to something else and is kept. */
void sew_display_scale(u8 v)
{
    u8 top = 0x64;
    u8 a, b;

    if (STITCH_SET == 0xB4 && !(REG8(0xFFFEF8UL) & 0x10)) {
        a = (u8)((short)((short)(u16)((u16)(0x12 * (u16)v) + 5) / (short)10));
        if (a > top) a = top;

        b = (u8)((short)((short)(u16)((u16)(0x12 * (u16)REG8(0x11A69FUL)) + 5)
                         / (short)10));
        if (b > top) b = top;

        REG8(0xFFFEE5UL) = (u8)((REG8(0xFFFEE5UL) & 0x80) | b);
        REG8(0xFFFEE4UL) = a;
    } else {
        REG8(0xFFFEE4UL) = REG8(0x11A69EUL);
        REG8(0xFFFEE5UL) = (u8)((REG8(0xFFFEE5UL) & 0x80) |
                                REG8(0x11A69FUL));
    }
}

/* H'203872. The hour and stitch counters. H'11A6D8 counts up while the
 * motor is turning and rolls a service counter every H'EA60; the two
 * longwords at H'11A6E0 and H'11A6DC are lifetime totals, one of them only
 * while the pedal is down. */
void sew_counters_tick(void)
{
    u8 v;

    if ((REG8(0x114DC6UL) & 0x80) && REG8(0xFFFEC8UL) != 0) {
        if (REG16(0x11A6D8UL) >= 0xEA60) {
            REG16(0x11A6D8UL) = (u16)(REG16(0x11A6D8UL) + 0x15A0);
            v = (u8)(REG8(0x11A6D0UL) + 1);
            REG8(0x11A6D0UL) = v;
            if (v >= 0x02) {
                REG8(0x11A6D0UL) = 0;
                REG8(0x114DC8UL) |= 0x10;
            }
            REG32(0x11A6E0UL) = REG32(0x11A6E0UL) + 1;
            if (REG8(0xFFFEC4UL) & 0x01) {
                REG32(0x11A6DCUL) = REG32(0x11A6DCUL) + 1;
            }
        }
        REG16(0x11A6DAUL) = REG16(0x11A6D8UL);
    } else if (!(REG8(0x114DC6UL) & 0x80)) {
        REG16(0x11A6D8UL) = REG16(0x11A6DAUL);
    }

    if (!(REG8(0x11A6AEUL) & 0x80)) {
        REG8(0x114DC8UL) |= 0x08;
    } else if (REG8(0x114DC8UL) & 0x08) {
        REG8(0x114DC8UL) &= (u8)~0x08;
        REG32(0x11A6E4UL) = REG32(0x11A6E4UL) + 1;
    }
}

/* H'20078E, H'200784. A byte copy. The count is in ER4, source in ER5 and
 * destination in ER6, which is not the convention anything else here uses --
 * these two are hand-written and their callers set the registers directly.
 * H'200784 is the same thing with the top half of the count cleared, so it
 * takes a 16-bit length. */
void mem_copy_long(u8 *dst, const u8 *src, u32 n)
{
    if (n == 0) return;
    do {
        *dst++ = *src++;
        n--;
    } while (n != 0);
}

void mem_copy(u8 *dst, const u8 *src, u16 n)
{
    mem_copy_long(dst, src, (u32)n);
}

/* H'20655A, H'20661C. The selected pattern's data streamed into the buffer
 * at H'0E0000, a chunk at a time. H'11A6D2 is how many chunks the whole is
 * cut into and H'11A6D1 is which one is next; the chunk size follows from
 * H'1800 divided by the count. */
void stitch_chunk_load(void)
{
    u8  chunks = REG8(0x11A6D2UL);
    u16 size   = (u16)((short)((short)0x1800 / (short)(u16)chunks));
    u8  index  = REG8(0x11A6D1UL);
    u32 offset = (u32)(short)(u16)(size * (u16)index);
    u32 src;

    REG8(0x11A6D1UL) = (u8)(index + 1);

    if (STITCH_SET == 0xAA) src = REG32(stitch_record(REG16(0x11A7E6UL)));
    else                    src = REG32(stitch_record(REG16(0x11A7E6UL)) + 4);

    mem_copy((u8 *)(0x0E0000UL + offset), (const u8 *)(src + offset), size);
}

void stitch_chunk_first(void)
{
    REG8(0x11A6D1UL) = 0x00;
    REG8(0x11A6D2UL) = 0x10;
    stitch_chunk_load();
}

/* H'20666E. The smaller of two bytes out of the loaded pattern. */
u8 stitch_length_limit(void)
{
    u8 a = REG8(0x0E0013UL);
    u8 b = bus_byte_11();

    if (a >= b) a = b;
    return a;
}

/* H'2021D8, H'2060D2, H'206100. */
void sew_clear_67D_5(void)
{
    REG8(0x11A6AEUL) &= (u8)~0x20;
    REG8(0x11A67DUL) &= (u8)~0x20;
}

void sew_set_67D_5(void)
{
    if (!(REG8(0x11A7D4UL) & 0x08)) {
        REG8(0x11A6AEUL) |= 0x20;
        REG8(0x11A67DUL) |= 0x20;
    } else {
        sew_clear_67D_5();
    }
}

/* H'205FC0. The variant the pattern is shown in, when the machine is in the
 * mode where that is chosen by hand. */
void sew_variant_select(void)
{
    u8 v;

    if (REG8(0xFFFEFDUL) == 0x02) {
        v = (u8)(REG8(0xFFFEFAUL) & 0x07);
        if (v == 0) REG8(0x11A7A8UL) = 0;
        else        REG8(0x11A7A8UL) = (u8)(v + 0xFF);
        return;
    }

    if (REG8(0x114DCDUL) & 0x01) {
        if (!(REG8(0x11A67CUL) & 0x80)) {
            REG8(0x114DCBUL) |= 0x20;
            REG8(0x11A67CUL) |= 0x80;
        }
    } else {
        REG8(0x11A67CUL) &= (u8)~0x80;
    }
}

/* H'206084, H'2060A6. */
void sew_variant_service(void)
{
    if (!(REG8(0x114DC6UL) & 0x08)) {
        if (REG8(0x11A7BDUL) & 0x08) sew_variant_select();
        else                         sew_watch_67D();
    }
}

void sew_mode_bit_service(void)
{
    if (REG8(0x11A7D7UL) & 0x80) REG8(0x114DCDUL) |=        0x01;
    else                         REG8(0x114DCDUL) &= (u8)~0x01;
    sew_variant_service();
}

/* H'207D6E. Both knob parameters clamped down to what the pattern allows. */
void sew_params_clamp(u8 max_a, u8 max_b)
{
    u8 a = REG8(0x11A69EUL);

    if (a > max_a) {
        REG8(0x11A69EUL) = max_a;
        sew_param_a_set(max_a);
    }
    if (REG8(0x11A6A0UL) > max_b) {
        REG8(0x11A6A0UL) = max_b;
        sew_param_b_set(max_b);
    }
}

/* H'2020BC, H'2020BE, H'2020E4, H'202166, H'2021A0. Two latched changes:
 * each notices its flag has flipped, remembers that it has, and does the
 * one-off work. H'2020BC is empty in the original and left empty here. */
void sew_edge_on_a(void) { }

void sew_edge_off_a(void)
{
    REG8(0x114DC7UL) &= (u8)~0x80;
    REG8(0xFFFEF8UL) &= (u8)~0x20;
    REG8(0x11A6CFUL) = 0;
}

void sew_edge_off_b(void)
{
    REG8(0x114DC6UL) |= 0x40;

    if (REG8(0x114DCAUL) & 0x02) {
        if (REG8(0x114DCAUL) & 0x08) REG8(0x11A688UL) = 0x01;
        else                         REG8(0x11A688UL) = 0x00;
    } else {
        if (REG8(0x114DC9UL) & 0x20) REG8(0x11A688UL) = 0x00;
        else                         REG8(0x11A688UL) = 0x01;
    }
}

void sew_latch_7D4_7(void)
{
    if (REG8(0x11A7D4UL) & 0x80) {
        if (REG8(0x114DD0UL) == 0) {
            REG8(0x114DD0UL) = 0x01;
            sew_edge_on_a();
        }
    } else {
        if (REG8(0x114DD0UL) != 0) {
            REG8(0x114DD0UL) = 0x00;
            sew_edge_off_a();
        }
    }
}

void sew_latch_7DF(void)
{
    if (REG8(0x11A7DFUL) == 0) {
        if (REG8(0x114DD1UL) == 0) {
            REG8(0x114DD1UL) = 0x01;
            sew_stop_flags_clear();
        }
    } else {
        if (REG8(0x114DD1UL) != 0) {
            REG8(0x114DD1UL) = 0x00;
            sew_edge_off_b();
        }
    }
}

/* H'20222E, H'205F3E. */
void sew_busy_release(void)
{
    u8 v;

    REG8(0x114DC6UL) &= (u8)~0x10;
    v = REG8(0x114DC9UL);
    if ((v & 0x08) || (v & 0x10)) REG8(0x11A6AEUL) |= 0x02;
    sew_clear_busy_3();
    sew_clear_busy_4();
}

/* Bit 4 of H'11A7D5 is the pattern asking to be sewn a fixed number of
 * times rather than continuously. Without it, whatever was counting is let
 * go; with it, the count is armed once. */
void sew_repeat_service(void)
{
    u8 v;

    if (!(REG8(0x11A7D5UL) & 0x10) || (REG8(0x114DC8UL) & 0x01)) {
        sew_busy_release();
        return;
    }
    if (REG8(0x114DCAUL) & 0x01) return;
    if (REG8(0x114DC6UL) & 0x10) return;

    REG8(0x114DC6UL) |= 0x10;

    v = REG8(0x114DC9UL);
    if (!(v & 0x20)) {
        /* The original tests the same bit twice here, so the three checks
         * that follow the second test can never run: with the bit clear it
         * leaves at once, and with it set it never reaches them. Written
         * the same way, and the unreachable half is left out rather than
         * guessed at. */
        return;
    }

    REG8(0x114DC9UL) |= 0x08;
    REG8(0x11A6AEUL) |= 0x02;
}

/* ---- the queue -----------------------------------------------------------
 * A row of 13-byte entries at H'11BBAA, indexed by position. The machine
 * sews a sequence of patterns, not one pattern, and this is the sequence:
 * H'FFFEFE is the position being worked on, H'11A6C8 the start of the group
 * it belongs to, and H'11A1D0/H'11A1D2 the ends of the whole.
 *
 * The first two bytes of an entry carry the pattern number in ten bits; the
 * rest are the parameters the operator has set for that position, packed.
 * H'03FE in the number is the marker that separates one group from the next.
 */
#define QUEUE           0x11BBAAUL
#define QUEUE_POS       REG16(0xFFFEFEUL)
#define QUEUE_GROUP     REG16(0x11A6C8UL)
#define QUEUE_FIRST     REG16(0x11A1D0UL)
#define QUEUE_LAST      REG16(0x11A1D2UL)
#define QUEUE_END       0x03FE

static u32 queue_entry(u16 i)
{
    return QUEUE + (u32)(u16)((u16)0x000D * i);
}

/* H'201798. The first two bytes of an entry, as a word. Callers mask it
 * with H'03FF to get the pattern number. */
u16 queue_entry_ref(u16 i)
{
    u32 e = queue_entry(i);

    return (u16)((u16)REG8(e) | (u16)((u16)REG8(e + 1) << 8));
}

/* H'205CA8. The packed parameters of the entry at the current position,
 * unpacked into the pattern's copy. */
void queue_entry_unpack(void)
{
    u32 e = queue_entry(QUEUE_POS);
    u8 b;

    b = REG8(e + 1);
    REG8(0x11A7E1UL) = (u8)((b >> 6) & 0x03);
    REG8(0x11A7D8UL) = (u8)((b & 0x3C) >> 2);

    REG8(0x11A7DAUL) = REG8(queue_entry(QUEUE_POS) + 2);
    REG8(0x11A7D9UL) = REG8(queue_entry(QUEUE_POS) + 3);

    b = REG8(queue_entry(QUEUE_POS) + 4);
    REG8(0x11A7DBUL) = (u8)(b & 0x3F);
    if (b & 0x80) REG8(0x11A7D4UL) |=        0x04;
    else          REG8(0x11A7D4UL) &= (u8)~0x04;

    REG8(0x11A7DCUL) = REG8(queue_entry(QUEUE_POS) + 5);
    REG8(0x11A7DDUL) = REG8(queue_entry(QUEUE_POS) + 6);

    b = REG8(queue_entry(QUEUE_POS) + 7);
    REG8(0x11A7E0UL) = (u8)(b & 0x03);
    REG8(0x11A7DEUL) = (u8)((b & 0xF0) >> 4);
    if (b & 0x08) REG8(0x11A7D4UL) |=        0x10;
    else          REG8(0x11A7D4UL) &= (u8)~0x10;
}

/* The pattern an entry names, with the per-entry offset at byte H'0A added.
 * Every accessor call in this layer goes through this. */
static u16 queue_pattern(u16 i)
{
    return (u16)((u16)(queue_entry_ref(i) & 0x03FF) +
                 (u16)REG8(queue_entry(i) + 0x0A));
}

/* H'203564. Bit 2 of the pattern's byte 6. */
u8 pattern_is_group(u16 n)
{
    return (u8)(pattern_byte(n, 0x0006) & 0x04);
}

/* H'20348C. */
void queue_flags_reset(void)
{
    REG8(0x114DCAUL) &= (u8)~0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DC9UL) &= (u8)~0x02;
    REG8(0x114DCCUL) |=        0x20;
}

/* H'203632. Walks back from the current position to the start of its group
 * and records it. A position whose entry is the group marker, or which is
 * the very first, is itself the start. */
void queue_group_start(void)
{
    u16 i = QUEUE_POS;

    if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END || i == QUEUE_FIRST) {
        QUEUE_GROUP = i;
        return;
    }

    for (i = QUEUE_POS; i >= QUEUE_FIRST; i--) {
        if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END || i == QUEUE_FIRST) {
            QUEUE_GROUP = i;
            return;
        }
    }
}

/* H'203580. Once every H'65 passes, walks the rest of the queue: if any
 * pattern in it is a group, the count at H'FFFEEB goes up from 1 to H'28,
 * and a run head of H'15 raises bit 7 of H'114DCE. */
void queue_scan_service(void)
{
    u16 i;
    u16 n;

    if (REG8(0x11A7EEUL) < 0x64) {
        REG8(0x11A7EEUL)++;
        return;
    }

    REG8(0x11A7EEUL) = 0;
    REG8(0x114DCEUL) &= (u8)~0x80;
    REG8(0xFFFEEBUL) = 0x01;

    for (i = (u16)(QUEUE_GROUP + 1); i <= QUEUE_LAST; i++) {
        if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END) break;
        n = queue_pattern(i);
        if (pattern_is_group(n) != 0) REG8(0xFFFEEBUL) = 0x28;
        if (stitch_run_head(n) == 0x0015) REG8(0x114DCEUL) |= 0x80;
    }
}

/* H'206958. The pattern's parameters into the live set, three of them
 * fetched from the catalogue rather than the queue. */
void sew_params_from_pattern(void)
{
    u16 n;

    REG8(0xFFFEEAUL) = REG8(0x11A7D8UL);
    REG8(0x11A69EUL) = REG8(0x11A7D9UL);
    sew_param_a_set(REG8(0x11A7D9UL));

    n = queue_pattern(QUEUE_POS);
    REG8(0x11A7BFUL) = stitch_param_1(n, 0);
    REG8(0x11A69FUL) = REG8(0x11A7BFUL);

    REG8(0xFFFEE6UL) = REG8(0x11A7C0UL);
    REG8(0x11A6A0UL) = REG8(0x11A7DAUL);
    sew_param_b_set(REG8(0x11A7DAUL));

    n = queue_pattern(QUEUE_POS);
    REG8(0x11A7C1UL) = stitch_param_2(n, 0);
    REG8(0x11A6A1UL) = REG8(0x11A7C1UL);

    REG8(0xFFFEE9UL) = REG8(0x11A7C2UL);
    REG8(0xFFFEECUL) = REG8(0x11A7DBUL);

    n = queue_pattern(QUEUE_POS);
    REG8(0x11A7C5UL) = stitch_param_5(n, 0);
}

/* H'206262. Bit 5 of H'FFFEC1 is a lamp. Depending on the pattern it is
 * held on, blinked, or left off, and H'11A6CD counts the blink. */
void sew_lamp_service(void)
{
    u8 v;

    /* Two ways to be held on, and they share their tail: the current stitch
     * asking for it, or the pattern asking for it while nothing else has a
     * claim. Everything below is the blinking. */
    if (((REG8(0x11A7C8UL) & 0x40) && !(REG8(0x114DCAUL) & 0x10)) ||
        ((REG8(0x11A7BEUL) & 0x80) && (REG8(0x11A7D4UL) & 0x10) &&
         !(REG8(0x114DC9UL) & 0x20) && !(REG8(0x114DCAUL) & 0x10))) {
        REG8(0xFFFEC1UL) |= 0x20;
        REG8(0x11A6CDUL) = 0;
        return;
    }

    if ((REG8(0x11A7D4UL) & 0x10) && (REG8(0x11A681UL) & 0x01)) {
        v = REG8(0xFFFEC1UL);
        if (v & 0x20) {
            REG8(0xFFFEC1UL) = (u8)(v & ~0x20);
        } else {
            REG8(0xFFFEC1UL) = (u8)(v | 0x20);
            REG8(0x11A6CDUL) = 0;
        }
        return;
    }

    if (REG8(0x11A7D4UL) & 0x20) {
        v = REG8(0xFFFEC1UL);
        if (v & 0x20) {
            v = (u8)(REG8(0x11A6CDUL) + 1);
            REG8(0x11A6CDUL) = v;
            if ((u8)(v - 1) >= 0x03) REG8(0xFFFEC1UL) &= (u8)~0x20;
        } else {
            REG8(0xFFFEC1UL) = (u8)(v | 0x20);
            REG8(0x11A6CDUL) = 0;
        }
    } else {
        REG8(0xFFFEC1UL) &= (u8)~0x20;
    }
}

/* H'205DDC. Bit 6 of H'11A7D4 is the request to move on to the next
 * position. Granted only while the queue is live; otherwise it just asks
 * for a redraw. */
void queue_advance(void)
{
    if (!(REG8(0x11A7D4UL) & 0x40)) return;

    REG8(0x11A688UL) = 0;
    REG8(0xFFFEF5UL) &= (u8)~0x40;

    if (!(REG8(0x114DCAUL) & 0x02)) {
        REG8(0x11A6AEUL) |= 0x02;
        return;
    }

    QUEUE_POS = (u16)(QUEUE_GROUP + 1);
    queue_flags_reset();

    REG16(0x11A7E6UL) = queue_pattern(QUEUE_POS);
    REG16(0x11A66EUL) = REG16(0x11A7E6UL);

    REG8(0x11A6AEUL) |= 0x01;
    REG8(0x114DCDUL) &= (u8)~0x08;
    REG8(0x114DCAUL) |= 0x80;
}

/* H'206104. The speed ceiling, scaled by the pattern's own class: full,
 * three quarters, a half or a quarter. Below H'19 nothing is written -- the
 * ceiling is already as low as it goes. */
void sew_speed_limit_scale(void)
{
    u8 v = REG8(0x11A6CEUL);

    if (v < 0x19) return;

    switch (REG8(0x11A7E0UL)) {
    case 0x00: v = (u8)(int)((float)(u32)v);          break;
    case 0x01: v = (u8)(int)((float)(u32)v * 0.75f);  break;
    case 0x02: v = (u8)(int)((float)(u32)v * 0.5f);   break;
    case 0x03: v = (u8)(int)((float)(u32)v * 0.25f);  break;
    default: break;
    }
    REG8(0xFFFECBUL) = v;
}

/* H'2061C6. Everything that has to be looked at once a pass. */
void sew_service(void)
{
    u8 v;

    sew_repeat_service();
    queue_advance();
    sew_latch_7DF();
    sew_latch_7D4_7();
    sew_needle_stop_pin();
    sew_set_67D_5();
    sew_mode_bit_service();
    sew_watch_7E1();
    sew_speed_limit_scale();
    sew_watch_7DE();

    v = REG8(0x11A7BDUL);
    if (!(v & 0x08)) {
        REG8(0xFFFEFAUL) &= (u8)~0x08;
    } else if (!(v & 0x04) && !(v & 0x02)) {
        REG8(0xFFFEFAUL) |= 0x08;
    }

    if (!(REG8(0xFFFEE3UL) & 0x02)) {
        REG8(0x114DCDUL) &= (u8)~0x04;
    } else if (REG8(0x114DC6UL) & 0x80) {
        REG8(0x114DCDUL) |= 0x04;
    }

    if ((REG8(0x114DCAUL) & 0x02) && !(REG8(0x114DC6UL) & 0x80)) {
        queue_scan_service();
    }
}

/* H'203280 and the two handlers its table selects between, H'2033DC and
 * H'2033E8. Seventy-eight screens, and all of them resolve to one of two
 * things: either the machine is held from sewing outright, or it is held
 * only while the queue is somewhere it should not start from.
 *
 * The two handlers share this routine's epilogue and cannot be entered on
 * their own, so they are written inline.
 */
static const u8 sew_mode_holds[0x4E] = {
    1,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,   /* 00..0F */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* 10..1F */
    0,0,0,0,0,0,0,1,1,1,1,1,1,0,1,1,   /* 20..2F */
    0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* 30..3F */
    0,1,1,1,1,0,1,1,1,1,1,1,0,1    /* 40..4D */
};

void sew_mode_dispatch(void)
{
    u8 mode = REG8(0x11A169UL);

    if (mode <= 0x4D && sew_mode_holds[mode]) {
        REG8(0xFFFEC4UL) |= 0x20;
        return;
    }

    if (REG8(0xFFFEC4UL) & 0x01) return;

    if (REG8(0x114DCAUL) & 0x02) {
        if (QUEUE_POS == QUEUE_GROUP ||
            (queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END) {
            REG8(0xFFFEC4UL) |= 0x20;
        }
    } else {
        if (REG16(0x114DDEUL) >= 0x012C) REG8(0xFFFEC4UL) &= (u8)~0x20;
    }
}

/* H'2037FE. Whether the machine counts as running, and whether the panel is
 * to be told. */
void sew_running_flags(void)
{
    u8 v = REG8(0xFFFEC6UL);

    if (v == 0x00 || v == 0x04 || v == 0x05) {
        REG8(0x114DC6UL) &= (u8)~0x80;
        REG8(0x114DCFUL) &= (u8)~0x20;
    } else {
        REG8(0x114DC6UL) |= 0x80;
        if (!(REG8(0x114DCFUL) & 0x20)) REG8(0x114DCFUL) |= 0x20;
    }

    sew_mode_dispatch();

    v = REG8(0xFFFEC0UL);
    if (v == 0x01 || v == 0x00) REG8(0x11A6AEUL) |=        0x80;
    else                        REG8(0x11A6AEUL) &= (u8)~0x80;
}

/* H'21F094. Which screen the panel is showing. Everything that has to
 * behave differently depending on where the operator is reads this. */
u16 mode_code(void)
{
    return (u16)REG8(0x11A169UL);
}

/* H'204BA6. The two knob parameters are held at twice their value while
 * sewing and at their face value on the screens that show a number. This
 * moves between the two, halving on the screens that want it and copying
 * the doubled pair back on the ones that do not. A value over H'C8 cannot
 * be halved into a byte, so it is pinned at H'64 and put back doubled. */
void sew_params_scale_for_mode(void)
{
    u16 mode = mode_code();
    u8 v;
    int halve;

    halve = !(mode < 2);
    if (halve) {
        if      (mode <  0x0005) halve = 1;
        else if (mode == 0x0007) halve = 1;
        else if (mode <  0x000C) halve = 0;
        else if (mode <  0x000E) halve = 1;
        else if (mode == 0x0018) halve = 1;
        else if (mode == 0x002B) halve = 1;
        else if (mode == 0x0030) halve = 1;
        else if (mode <  0x0033) halve = 0;
        else if (mode <  0x0037) halve = 1;
        else if (mode == 0x0042) halve = 1;
        else if (mode <  0x0044) halve = 0;
        else if (mode >  0x0047) halve = 0;
        else                     halve = 1;
    }

    if (!halve) {
        REG8(0x11A6D3UL) = REG8(0x11A6D4UL);
        REG8(0x11A6D5UL) = REG8(0x11A6D6UL);
        return;
    }

    v = REG8(0x11A6D3UL);
    if (v > 0xC8) {
        REG8(0x11A69EUL) = 0x64;
        sew_param_a_set(0x64);
    } else {
        REG8(0x11A69EUL) = (u8)(v / 2);
    }

    v = REG8(0x11A6D5UL);
    if (v > 0xC8) {
        REG8(0x11A6A0UL) = 0x64;
        sew_param_b_set(0x64);
    } else {
        REG8(0x11A6A0UL) = (u8)(v / 2);
    }

    REG8(0x11A6D4UL) = REG8(0x11A6D3UL);
    REG8(0x11A6D6UL) = REG8(0x11A6D5UL);
}

/* H'20394A. The counters, and their two lifetime totals written back into
 * the settings block when the service counter has rolled. Bit 5 of
 * H'114DC7 is held up around each flash write -- the same bit
 * config_block_check uses -- so nothing else touches the device while it is
 * being programmed. */
void sew_counters_service(void)
{
    sew_counters_tick();

    if (REG8(0x114DC6UL) & 0x80) return;
    if (!(REG8(0x114DC8UL) & 0x10)) return;

    REG8(0x114DC8UL) &= (u8)~0x10;

    if (REG32(0x57FF82UL) != REG32(0x11A6E0UL)) {
        REG8(0x114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x11A6E0UL, 0x57FF82UL, 4);
        REG8(0x114DC7UL) &= (u8)~0x20;
    }
    if (REG32(0x57FF86UL) != REG32(0x11A6E4UL)) {
        REG8(0x114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x11A6E4UL, 0x57FF86UL, 4);
        REG8(0x114DC7UL) &= (u8)~0x20;
    }
}

/* H'207DAA. The two limits the variant handling worked out, put back into
 * the pattern's copy, and the live values clamped down to them. */
void sew_limits_apply(void)
{
    REG8(0x11A7C0UL) = REG8(0x11A670UL);
    if (REG8(0x11A7D9UL) > REG8(0x11A7C0UL)) {
        REG8(0x11A7D9UL) = REG8(0x11A7C0UL);
    }

    REG8(0x11A7C2UL) = REG8(0x11A671UL);
    if (REG8(0x11A7DAUL) > REG8(0x11A7C2UL)) {
        REG8(0x11A7DAUL) = REG8(0x11A7C2UL);
    }
}

/* H'206348. The pattern's variants unpacked into the buffer.
 *
 * A pattern is not one shape but a set of them -- the same stitch at
 * different widths, or the same letter in different sizes -- and the chunk
 * loader brings the lot into H'0E0000 as one block. This walks that block:
 * byte 7 bit 7 says there is a variant table at all, byte 8 says how many
 * there are (capped at H'30), and from H'0E0014 there is a pointer per
 * variant into the block.
 *
 * Each variant is then copied up above the pointer array, packed end to
 * end, and its pointer rewritten to where it landed. The length comes out
 * of the variant's own header: a big-endian count at +H'10, two or three
 * bytes a stitch depending on bit 2 of its byte 6, plus H'13 of header.
 * H'11A6E8 ends up holding the same pointers, which is the table
 * sew_variant_load reads.
 *
 * With no variant table the count is forced to one and all H'30 slots point
 * at the start of the block.
 */
void stitch_variants_build(void)
{
    u8  i;
    u32 dst, src;
    u16 len;
    u8  count;

    REG8(0x11A7A8UL) = 0;

    if (!(REG8(0x0E0007UL) & 0x80)) {
        REG8(0x11A7AAUL) = 0x01;
        for (i = 0; i < 0x30; i++) {
            REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2)) = 0x000E0000UL;
        }
        return;
    }

    REG8(0x11A6D1UL) = REG8(0x11A6D2UL);

    REG8(0x11A7ABUL) = REG8(0x0E0012UL);
    REG8(0x11A7AAUL) = REG8(0x0E0008UL);
    if (REG8(0x0E0008UL) > 0x30) REG8(0x11A7AAUL) = 0x30;

    if (REG8(0x11A7BCUL) & 0x08) return;

    i   = 0;
    src = REG32(0x0E0014UL);
    dst = 0x000E0000UL + (u32)(u16)(REG8(0x11A7AAUL) << 2) + 0x14UL;

    len = (u16)((u16)((u16)REG8(src + 0x10) << 8) + (u16)REG8(src + 0x11));
    if (REG8(src + 6) & 0x04) len = (u16)((u16)(0x0003 * len) + 0x0013);
    else                      len = (u16)((u16)(len << 1)      + 0x0013);

    mem_copy((u8 *)dst, (const u8 *)src, len);
    REG32(0x0E0014UL) = dst;
    REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2)) = dst;
    i++;

    count = REG8(0x11A7AAUL);
    while (i < count) {
        src = REG32(0x000E0000UL + (u32)(u16)(i << 2) + 0x14UL);
        dst += (u32)len;

        len = (u16)((u16)((u16)REG8(src + 0x10) << 8) + (u16)REG8(src + 0x11));
        if (REG8(src + 6) & 0x04) len = (u16)((u16)(0x0003 * len) + 0x0013);
        else                      len = (u16)((u16)(len << 1)      + 0x0013);

        mem_copy((u8 *)dst, (const u8 *)src, len);
        REG32(0x000E0000UL + (u32)(u16)(i << 2) + 0x14UL) = dst;
        REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2)) = dst;
        i++;
        count = REG8(0x11A7AAUL);
    }
}

/* H'207E4E, H'207EC2. Two three-step state machines that wait for a stepper
 * to report itself idle -- H'FFFED4 and H'FFFED6 are the step states the
 * timer handlers keep -- and record a limit once it has. Until then, and
 * after the third step, the limits read H'FF, which means "no limit yet". */
void home_state_b(void)
{
    u8 st = REG8(0x11A678UL);

    if (st == 0x00) {
        if (REG8(0x114DC6UL) & 0x02) REG8(0x11A678UL) = 0x01;
    } else if (st == 0x01) {
        if (REG8(0xFFFED4UL) == 0x01) {
            REG8(0x11A6B8UL) = (u8)(REG8(0xFFFED8UL) + 0x66);
            REG8(0x11A678UL) = 0x02;
        }
    } else if (st == 0x02) {
        if (REG8(0xFFFED4UL) == 0x01) {
            REG8(0xFFFED5UL) = 0xFF;
            REG8(0x11A678UL) = 0x03;
        }
    } else {
        REG8(0xFFFED5UL) = 0xFF;
    }
}

void home_state_c(void)
{
    u8 st = REG8(0x11A679UL);

    if (st == 0x00) {
        if (REG8(0x114DC6UL) & 0x20) REG8(0x11A679UL) = 0x01;
    } else if (st == 0x01) {
        if (REG8(0xFFFED6UL) == 0x01) {
            REG8(0x11A6B9UL) = 0x19;
            REG8(0x11A6BAUL) = 0x19;
            REG8(0x11A679UL) = 0x02;
        }
    } else if (st == 0x02) {
        if (REG8(0xFFFED6UL) == 0x01) {
            REG8(0x11A6B9UL) = 0xFF;
            REG8(0x11A6BAUL) = 0xFF;
            REG8(0x11A679UL) = 0x03;
        }
    } else {
        REG8(0x11A6B9UL) = 0xFF;
        REG8(0x11A6BAUL) = 0xFF;
    }
}

/* H'207F42. */
void sew_mechanism_service(void)
{
    home_state_b();
    home_state_c();
    REG8(0x11A6B7UL) = 0x1B;
    sew_limit_6BB();
    if (!(REG8(0x114DC6UL) & 0x80)) REG8(0xFFFEC7UL) &= (u8)~0x40;
}

/* H'2076C4, H'2076DC, H'207706 and H'207726 are the four things the panel
 * can be asked to do, and the bits in H'11A6AE say which. These two have no
 * dependants and are written here; the other two need the redraw itself. */
void redraw_partial(void)
{
    REG8(0x11A6AEUL) |=        0x04;
    REG8(0x11A6AEUL) &= (u8)~0x08;
}

void redraw_pattern(void)
{
    REG8(0x11A6AEUL) |=        0x01;
    REG8(0x11A6AEUL) &= (u8)~0x04;
    if (REG8(0x114DCAUL) & 0x02) REG8(0x114DCAUL) |= 0x80;
}

/* H'205AB6. Where in the pattern's stitch list the next stitch is, given a
 * step from the start of the group. H'11A6CC is the bytes a stitch takes --
 * two or three -- and H'11A682 the base; a mirrored pattern counts back from
 * the base instead of forward from it. */
void needle_position_set(u8 step)
{
    u16 off;
    u8 kind = REG8(0x11A7E1UL);

    REG16(0x11A680UL) = (u16)step;

    off = (u16)((u16)REG8(0x11A6CCUL) * (u16)step);
    if (kind == 0x02 || kind == 0x03) {
        REG16(0x11A684UL) = (u16)(REG16(0x11A682UL) - off);
    } else {
        REG16(0x11A684UL) = (u16)(off + REG16(0x11A682UL));
    }

    REG8(0x114DC9UL) |= 0x20;

    if (!((REG8(0x114DC6UL) & 0x10) && !(REG8(0x114DCAUL) & 0x02))) {
        if (!(REG8(0x114DCAUL) & 0x40)) {
            if (!(REG8(0x114DC6UL) & 0x10)) return;
            if (!(REG8(0x114DCAUL) & 0x02)) return;
            if ((u16)(REG16(0x11A6C8UL) + 1) != REG16(0xFFFEFEUL)) return;
        }
    }

    REG8(0x114DC9UL) |= 0x08;
    REG8(0x114DC7UL) |= 0x08;
}

/* H'20258A. A byte that belongs to the pattern as a whole rather than to
 * one variant. Bit 7 of the descriptor's kind says it comes from a table in
 * flash; otherwise the variants are searched and the last one whose byte 7
 * has bit 6 set gives it up. */
u8 pattern_variant_flag(u16 n)
{
    u8 out = 0;
    u8 i;
    u32 p;

    if (REG8(stitch_record(n) + 0x16) & 0x80) {
        return REG8(0x57B6D8UL + (u32)(u16)(n << 2));
    }

    for (i = 1; i < REG8(0x11A7AAUL); i++) {
        p = REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2));
        if (REG8(p + 7) & 0x40) out = REG8(p + 0x0B);
    }
    return out;
}

/* H'203EBC. Steps the variant on by itself as the counter runs. 254
 * instructions, not yet reconstructed; it is reached only when a pattern has
 * more than one variant and bit 0 of H'11A7BC is clear. */

/* Defined below, with the rest of the variant handling. */
void variant_advance(void);

/* One byte out of the current variant's stitch data. */
static u8 stitch_data_byte(u16 idx)
{
    u32 base = REG32(0x11A6E8UL +
                     (u32)(short)(u16)(REG8(0x11A7A8UL) << 2));

    return REG8(base + (u32)idx);
}

/* H'204264. Reads the next four stitches out of the variant's data into
 * H'11A7C8..H'11A7D3, which is where the motor handlers pick them up.
 *
 * Four ways round, because a stitch is two bytes or three depending on bit 2
 * of H'11A7BC, and because a mirrored pattern -- H'11A7E1 of 2 or 3 -- is
 * walked backwards. The index runs from H'11A684 and wraps between stitches:
 * H'11A686 is the far end and H'11A682 the near one. In the three-byte
 * mirrored form the middle two bytes of each stitch come out swapped, which
 * is the mirroring itself rather than an accident of the loop.
 *
 * Ahead of all that, bit 3 of H'114DC7 means "do not advance": a fixed
 * stitch is put in instead, and which one depends on how far a stop
 * sequence has got.
 *
 * The codes H'38 to H'3F in the second byte are not stitches but marks
 * against the free-running counter at H'FFFF00, and the tail turns them
 * into the four limits at H'11A7AE to H'11A7B5 that sew_limit_overshoot
 * measures against.
 */
void stitch_fetch_next(void)
{
    u8  code = 0;
    u16 i = REG16(0x11A684UL);
    u8  c;
    u16 t;

    REG8(0x11A6B0UL) = REG8(0x11A7C8UL);
    REG8(0x11A6B1UL) = REG8(0x11A7C9UL);
    REG8(0x11A6B2UL) = REG8(0x11A7DAUL);
    REG8(0x11A6B3UL) = REG8(0x11A7CAUL);

    if (REG8(0x114DC7UL) & 0x08) {
        u8 n;

        if (REG8(0x11A7BCUL) & 0x80) code = REG8(0x11A6B5UL);
        else                         code = REG8(0x11A6B4UL);

        if (REG8(0x114DCAUL) & 0x10) {
            REG8(0x11A7C8UL) = code;
            REG8(0x11A7C9UL) = 0x33;
            REG8(0x11A7CAUL) = 0x00;
        } else if (REG8(0x114DC9UL) & 0x10) {
            n = REG8(0x11A6AFUL);
            if (n != 0x02 && n <= 0x04) {
                REG8(0x11A7C8UL) = code;
                REG8(0x11A7C9UL) = 0x33;
                REG8(0x11A7CAUL) = 0x00;
            } else if (n == 0x02) {
                REG8(0x11A7C8UL) = code;
                REG8(0x11A7C9UL) = 0xB4;
                REG8(0x11A7CAUL) = 0x00;
            }
        } else if (REG8(0x114DC9UL) & 0x08) {
            n = REG8(0x11A6AFUL);
            if (n != 0x02 && n <= 0x04) {
                REG8(0x11A7C8UL) = REG8(0x11A6B4UL);
                REG8(0x11A7C9UL) = 0x33;
                REG8(0x11A7CAUL) = 0x00;
            } else if (n == 0x02) {
                REG8(0x11A7C8UL) = REG8(0x11A6B4UL);
                REG8(0x11A7C9UL) = 0xB4;
                REG8(0x11A7CAUL) = 0x00;
            }
        }
        REG8(0x114DC7UL) &= (u8)~0x08;

    } else if (!(REG8(0x11A7BCUL) & 0x04)) {
        /* Two bytes a stitch. */
        c = REG8(0x11A7E1UL);
        if (c == 0x02 || c == 0x03) {
            REG8(0x11A7C8UL) = stitch_data_byte(i--);
            REG8(0x11A7C9UL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i--);
            REG8(0x11A7CCUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i--);
            REG8(0x11A7CFUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i--);
            REG8(0x11A7D2UL) = stitch_data_byte(i--);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) - 2);
        } else {
            REG8(0x11A7C8UL) = stitch_data_byte(i++);
            REG8(0x11A7C9UL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i++);
            REG8(0x11A7CCUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i++);
            REG8(0x11A7CFUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i++);
            REG8(0x11A7D2UL) = stitch_data_byte(i++);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) + 2);
        }

    } else {
        /* Three bytes a stitch. */
        c = REG8(0x11A7E1UL);
        if (c == 0x02 || c == 0x03) {
            REG8(0x11A7C8UL) = stitch_data_byte(i--);
            REG8(0x11A7CAUL) = stitch_data_byte(i--);
            REG8(0x11A7C9UL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i--);
            REG8(0x11A7CDUL) = stitch_data_byte(i--);
            REG8(0x11A7CCUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i--);
            REG8(0x11A7D0UL) = stitch_data_byte(i--);
            REG8(0x11A7CFUL) = stitch_data_byte(i--);
            if (i < REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i--);
            REG8(0x11A7D3UL) = stitch_data_byte(i--);
            REG8(0x11A7D2UL) = stitch_data_byte(i--);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) + 0xFFFD);
        } else {
            REG8(0x11A7C8UL) = stitch_data_byte(i++);
            REG8(0x11A7C9UL) = stitch_data_byte(i++);
            REG8(0x11A7CAUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CBUL) = stitch_data_byte(i++);
            REG8(0x11A7CCUL) = stitch_data_byte(i++);
            REG8(0x11A7CDUL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7CEUL) = stitch_data_byte(i++);
            REG8(0x11A7CFUL) = stitch_data_byte(i++);
            REG8(0x11A7D0UL) = stitch_data_byte(i++);
            if (i > REG16(0x11A686UL)) i = REG16(0x11A682UL);
            REG8(0x11A7D1UL) = stitch_data_byte(i++);
            REG8(0x11A7D2UL) = stitch_data_byte(i++);
            REG8(0x11A7D3UL) = stitch_data_byte(i++);
            REG16(0x11A684UL) = (u16)(REG16(0x11A684UL) + 3);
        }
    }

    c = REG8(0x11A7C9UL);
    if (c == 0x3C) {
        t = (u16)(REG16(0xFFFF00UL) + 1);
        REG16(0x11A7AEUL) = t;
        REG16(0x11A7B0UL) = (u16)(t + (u16)REG8(0x11A7ACUL));
    } else if (c == 0x3D) {
        REG16(0x11A7B0UL) = (u16)(REG16(0xFFFF00UL) - 1);
    } else if (c == 0x3E) {
        REG16(0x11A7B2UL) = (u16)(REG16(0xFFFF00UL) + 1);
    } else if (c == 0x3F) {
        REG16(0x11A7B4UL) = (u16)(REG16(0xFFFF00UL) - 1);
    } else if (c == 0x38) {
        t = (u16)(REG16(0xFFFF00UL) + 1);
        REG16(0x11A7B2UL) = t;
        REG16(0x11A7AEUL) = t;
        t = (u16)(t + (u16)REG8(0x11A7ACUL));
        REG16(0x11A7B4UL) = t;
        REG16(0x11A7B0UL) = t;
    } else if (c == 0x39) {
        t = REG16(0xFFFF00UL);
        REG16(0x11A7B4UL) = t;
        REG16(0x11A7B0UL) = t;
    }

    if (REG8(0x11A7AAUL) > 0x01 && !(REG8(0x11A7BCUL) & 0x01)) {
        variant_advance();
    }
}

/* H'205B72. Sets a pattern up to be sewn from the beginning.
 *
 * A stitch is two bytes or three -- H'11A6CC -- and H'11A67E is how many of
 * them there are. From those come the two ends the fetch walks between:
 * H'11A682 where it starts and H'11A686 where it turns round, which are the
 * other way about for a mirrored pattern. H'11A6B4 and H'11A6B5 are the
 * first byte at each end, which is what the hold path puts out when the
 * machine is stopping.
 */
void stitch_begin(void)
{
    u16 count, near, far;
    u32 base;

    REG8(0x11A6B0UL) = REG8(0x11A7C8UL);
    REG8(0x11A6B1UL) = REG8(0x11A7C9UL);
    REG8(0x11A6B2UL) = REG8(0x11A7DAUL);
    REG8(0x11A6B3UL) = REG8(0x11A7CAUL);

    sew_variant_load();

    REG8(0x11A6CCUL) = (REG8(0x11A7BCUL) & 0x04) ? 0x03 : 0x02;

    count = (u16)((u16)((u16)REG8(0x11A7C6UL) << 8) + (u16)REG8(0x11A7C7UL));
    REG16(0x11A67EUL) = count;

    if (REG8(0x11A7E1UL) == 0x02 || REG8(0x11A7E1UL) == 0x03) {
        far  = (u16)((u16)REG8(0x11A6CCUL) + 0x0012);
        near = (u16)((u16)((u16)(count - 1) * (u16)REG8(0x11A6CCUL)) + far);
    } else {
        near = 0x0012;
        far  = (u16)((u16)((u16)(count - 1) * (u16)REG8(0x11A6CCUL)) + 0x0012);
    }
    REG16(0x11A682UL) = near;
    REG16(0x11A686UL) = far;

    base = REG32(0x11A6E8UL + (u32)(short)(u16)(REG8(0x11A7A8UL) << 2));
    REG8(0x11A6B4UL) = REG8(base + (u32)near);
    REG8(0x11A6B5UL) = REG8(base + (u32)REG16(0x11A686UL));

    needle_position_set(0);
    REG16(0x11A680UL) = 0;
    stitch_fetch_next();
}

/* H'206F0A. Back to the first stitch and fetch. */
void stitch_restart(void)
{
    needle_position_set(0);
    stitch_fetch_next();
}

/* ---- stepping through the queue ------------------------------------------
 * H'114DCA bit 3, H'114DCC bits 4 and 5 and H'114DC9 bits 1 and 2 between
 * them say where in the queue the machine is -- first position of a group,
 * last one, or somewhere in between -- and these four set them. */
/* H'2034BE. */
void queue_step_flags_clear(void)
{
    REG8(0x114DCAUL) &= (u8)~0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DCCUL) &= (u8)~0x20;
}

/* H'2034E0. */
void queue_step_flags_last(void)
{
    REG8(0x114DCAUL) |=        0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DCCUL) &= (u8)~0x20;
}

/* H'203502. */
void queue_step_flags_first(void)
{
    u16 next = (u16)(QUEUE_POS + 1);

    if ((queue_entry_ref(next) & 0x03FF) == QUEUE_END ||
        QUEUE_POS == QUEUE_LAST) {
        REG8(0x114DCAUL) |= 0x08;
    } else {
        REG8(0x114DCAUL) &= (u8)~0x08;
    }
    REG8(0x114DCCUL) |=        0x10;
    REG8(0x114DCCUL) &= (u8)~0x20;
    REG8(0x114DC9UL) |=        0x04;
}

/* H'203446, H'20345A. */
void queue_flag_clear_cc4(void)
{
    REG8(0x114DCCUL) &= (u8)~0x10;
}

void queue_flags_group_end(void)
{
    REG8(0x114DCAUL) |=        0x08;
    REG8(0x114DCCUL) &= (u8)~0x10;
    REG8(0x114DC9UL) &= (u8)~0x02;
    REG8(0x114DCCUL) |=        0x20;
}

/* H'2017CE. How many positions are left in the group. */
u16 queue_group_count(void)
{
    u16 count = 0;
    u16 i = (u16)(QUEUE_GROUP + 1);

    while (i <= QUEUE_LAST) {
        if ((queue_entry_ref(i) & 0x03FF) == QUEUE_END) break;
        count++;
        i++;
    }
    return count;
}

/* H'20370A. On to the next position, wrapping back to the start of the
 * group at the end of it, and the step flags set to match. */
void queue_step_next(void)
{
    QUEUE_POS = (u16)(QUEUE_POS + 1);

    if ((queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END ||
        (u16)(QUEUE_POS - 1) == QUEUE_LAST) {
        QUEUE_POS = (u16)(QUEUE_GROUP + 1);
        queue_step_flags_first();
    } else if ((queue_entry_ref((u16)(QUEUE_POS + 1)) & 0x03FF) == QUEUE_END ||
               QUEUE_POS == QUEUE_LAST) {
        queue_step_flags_last();
    } else {
        queue_step_flags_clear();
    }

    REG16(0x11A7E6UL) = queue_pattern(QUEUE_POS);
    REG16(0x11A66EUL) = REG16(0x11A7E6UL);
    REG8(0x11A6AEUL) |= 0x01;
    REG8(0x114DCDUL) &= (u8)~0x08;
    REG8(0x114DCAUL) |= 0x80;
}

/* H'203DC2. The same, but only when there is more than one position, and
 * with the needle-stop pin raised on the way if the pattern says so. */
void queue_step_or_stop(void)
{
    if (QUEUE_POS == 0) return;
    if (QUEUE_FIRST == QUEUE_LAST) return;

    if (!(REG8(0xFFFEF8UL) & 0x20) && !(REG8(0x11A7BDUL) & 0x20)) {
        if (!(REG8(0x114DCAUL) & 0x08)) { queue_step_next(); return; }
        if (!(REG8(0x114DC6UL) & 0x40)) { queue_step_next(); return; }
        if (REG8(0x11A688UL) < REG8(0x11A7DFUL)) { queue_step_next(); return; }
    }

    REG8(0xFFFEC7UL) |= 0x40;
    REG8(0x11A688UL) = 0;
    queue_step_next();
}

/* H'203E36. On to the next variant of the same pattern, or back to the
 * first and on to the next position. */
void variant_step(void)
{
    short last = (short)(u16)((u16)REG8(0x11A7AAUL) - 1);

    REG8(0x11A668UL) = 0x01;
    if (REG8(0xFFFEF8UL) & 0x20) REG8(0xFFFEC7UL) |= 0x40;

    if (last <= (short)(u16)REG8(0x11A7A8UL)) {
        REG8(0x11A7A8UL) = 0;
        if (REG8(0x114DCAUL) & 0x02) {
            queue_step_or_stop();
        } else if (REG8(0x114DC6UL) & 0x08) {
            REG8(0x11A7ADUL) = 0x01;
            REG16(0xFFFF00UL) = 0x1000;
            REG16(0x11A698UL) = 0;
        }
    } else {
        REG8(0x11A7A8UL) = (u8)(REG8(0x11A7A8UL) + 1);
    }
}

/* H'20225E. The three parameters a pattern has no working copy for, taken
 * from the default tables in flash. The mark says the machine has to stop
 * and start again before the change takes. */
void pattern_defaults_load(u16 n, u8 mark)
{
    u32 off = (u32)(u16)(n << 2);
    u8 v;

    REG8(0x11A7BFUL) = REG8(0x57B6D6UL + off);

    if (REG8(0x11A7BDUL) & 0x40) v = REG8(0x57B6D8UL + off);
    else                         v = REG8(0x57B6D7UL + off);
    REG8(0x11A7C1UL) = v;

    REG8(0x11A69FUL) = REG8(0x11A7BFUL);
    REG8(0x11A6A1UL) = v;

    REG8(0x11A7C3UL) = (u8)(REG8(0x11A7C3UL) & 0xF0);
    REG8(0x11A7C3UL) = (u8)(REG8(0x11A7C3UL) | REG8(0x57B6D9UL + off));

    if (mark != 0) {
        REG8(0x114DC6UL) |= 0x08;
        REG8(0xFFFEF7UL) |= 0x08;
        REG8(0xFFFEFAUL) &= (u8)~0x80;
    }
}

/* H'20261E. The companion of pattern_variant_flag: the same search, but for
 * the variants whose bit 6 is clear, and a different default table. */
u8 pattern_variant_flag_b(u16 n)
{
    u8 out = 0;
    u8 i;
    u32 p;

    if (REG8(stitch_record(n) + 0x16) & 0x80) {
        return REG8(0x57B6D7UL + (u32)(u16)(n << 2));
    }

    for (i = 1; i < REG8(0x11A7AAUL); i++) {
        p = REG32(0x11A6E8UL + (u32)(short)(u16)(i << 2));
        if (!(REG8(p + 7) & 0x40)) out = REG8(p + 0x0B);
    }
    return out;
}

/* H'206F1A. The next thing to sew: the next variant, the next position, or
 * the same stitch again. H'FFFEC7 bit 6 is the needle-stop request, raised
 * whenever a boundary is crossed that the operator asked to be stopped at. */
static void needle_stop_request(void)
{
    REG8(0xFFFEC7UL) |= 0x40;
    REG8(0x11A688UL) = 0;
    if ((REG8(0x11A7BDUL) & 0x20) && (REG8(0xFFFEC7UL) & 0x02)) {
        REG8(0xFFFEF6UL) &= (u8)~0x20;
        REG8(0x114DCFUL) |= 0x04;
    }
}

void stitch_next_or_variant(void)
{
    if (!(REG8(0x114DC9UL) & 0x80)) {
        stitch_fetch_next();
        return;
    }

    if (REG8(0x11A7AAUL) > 0x01) {
        int stop = 1;

        if (!(REG8(0x11A7BCUL) & 0x10) && !(REG8(0x11A7BDUL) & 0x20)) {
            if (!(REG8(0xFFFEF8UL) & 0x20))     stop = 0;
            else if (REG8(0x114DC6UL) & 0x08)   stop = 0;
        }
        if (stop) needle_stop_request();

        if (REG8(0x11A7BCUL) & 0x01) variant_step();
        else                         stitch_restart();
        return;
    }

    if (REG8(0x114DCAUL) & 0x02) {
        queue_step_or_stop();
        return;
    }

    needle_position_set(0);
    stitch_fetch_next();

    if ((REG8(0x114DC6UL) & 0x40) &&
        REG8(0x11A688UL) >= REG8(0x11A7DFUL)) {
        needle_stop_request();
        return;
    }
    if (!(REG8(0x11A7BCUL) & 0x10) && !(REG8(0x11A7BDUL) & 0x20) &&
        !(REG8(0xFFFEF8UL) & 0x20)) {
        return;
    }
    needle_stop_request();
}

/* H'20704A. One stitch done. Counts the position on, notices the end of the
 * pattern in two places -- one stitch before it and at it -- and either
 * fetches the same stitch again or asks for the next thing. */
void stitch_advance(void)
{
    u16 pos = (u16)(REG16(0x11A680UL) + 1);
    u8 v, f;

    REG16(0x11A680UL) = pos;

    if (REG8(0x11A7D4UL) & 0x80) {
        if (REG16(0x11A67EUL) >= 0x0002) {
            if ((u16)(pos - 1) == (u16)(REG16(0x11A67EUL) >> 1)) {
                REG8(0xFFFEC7UL) |= 0x40;
            }
        }
    }

    v = REG8(0x114DC9UL);
    if (v & 0x20) {
        REG8(0x114DC9UL) = (u8)(v & ~0x20);
        if (REG8(0x11A7A8UL) == 0 && (REG8(0x114DC6UL) & 0x40)) {
            f = REG8(0x114DCAUL);
            if (f & 0x02) {
                if (f & 0x08) REG8(0x11A688UL) = (u8)(REG8(0x11A688UL) + 1);
            } else {
                REG8(0x11A688UL) = (u8)(REG8(0x11A688UL) + 1);
            }
        }
    }

    if ((u16)(REG16(0x11A67EUL) - 1) == REG16(0x11A680UL)) {
        REG8(0x114DC9UL) |= 0x40;
        if (REG8(0x11A7BCUL) & 0x80) {
            f = REG8(0x114DCAUL);
            if (f & 0x02) {
                if ((f & 0x08) && (REG8(0x114DC6UL) & 0x10)) {
                    REG8(0x114DC9UL) |= 0x10;
                }
            } else {
                if (REG8(0x114DC6UL) & 0x10) REG8(0x114DC9UL) |= 0x10;
            }
        }
    } else {
        REG8(0x114DC9UL) &= (u8)~0x40;
    }

    if (REG16(0x11A680UL) == REG16(0x11A67EUL)) {
        REG8(0x114DC9UL) |= 0x80;
        if (!(REG8(0x11A7BCUL) & 0x80)) {
            f = REG8(0x114DCAUL);
            if (f & 0x02) {
                if (f & 0x08) {
                    if (REG8(0x114DC6UL) & 0x10) REG8(0x114DC9UL) |= 0x10;
                    else                         REG8(0x114DCAUL) |= 0x10;
                } else {
                    REG8(0x114DCAUL) |= 0x10;
                }
            } else {
                if (REG8(0x114DC6UL) & 0x10) REG8(0x114DC9UL) |= 0x10;
            }
        }
    } else {
        REG8(0x114DC9UL) &= (u8)~0x80;
    }

    if ((REG8(0x114DCAUL) & 0x10) || (REG8(0x114DC9UL) & 0x10)) {
        REG8(0x114DC7UL) |= 0x08;
        stitch_fetch_next();
        REG8(0xFFFEC1UL) &= (u8)~0x20;
    } else {
        stitch_next_or_variant();
        sew_lamp_service();
    }
}

/* H'2071F8, H'207236. Two stop sequences, counted in H'11A6AF. */
void stop_step_a(void)
{
    u8 n;

    REG8(0xFFFEC1UL) &= (u8)~0x20;
    n = (u8)(REG8(0x11A6AFUL) + 1);
    REG8(0x11A6AFUL) = n;

    if (n >= 0x03) {
        sew_clear_busy_3();
        stitch_fetch_next();
    } else {
        REG8(0x114DC7UL) |= 0x08;
        stitch_fetch_next();
    }
}

/* H'2072A2. */
void redraw_after_edit(void)
{
    REG8(0x114DCAUL) &= (u8)~0x10;
    stitch_next_or_variant();
}

void stop_step_b(void)
{
    u8 n;

    REG8(0xFFFEC1UL) &= (u8)~0x20;
    n = (u8)(REG8(0x11A6AFUL) + 1);
    REG8(0x11A6AFUL) = n;

    if (n >= 0x04) {
        sew_clear_busy_4();
        if (REG8(0x11A7BCUL) & 0x80) {
            stitch_advance();
        } else {
            stitch_next_or_variant();
            sew_lamp_service();
        }
        return;
    }

    if (n == 0x03 && (REG8(0x11A7BCUL) & 0x80)) {
        REG8(0x114DC7UL) &= (u8)~0x08;
    } else if (n != 0x03) {
        REG8(0x114DC7UL) |= 0x08;
    }
    stitch_fetch_next();
}

/* H'2072BA. One stitch's worth of time gone by. H'11A7DE is how many
 * repeats the operator asked for; H'11A689 counts them. */
void stitch_tick(void)
{
    u8 want, done;

    REG16(0x11A698UL) = (u16)(REG16(0x11A698UL) + 1);

    want = REG8(0x11A7DEUL);
    if (want == 0) {
        stitch_advance();
        return;
    }

    REG8(0x11A689UL) = (u8)(REG8(0x11A689UL) + 1);
    done = REG8(0x11A689UL);

    if (REG8(0x114DCDUL) & 0x10) {
        if ((short)(u16)((u16)want << 1) <= (short)(u16)done) {
            REG8(0x11A689UL) = 0;
            stitch_advance();
        } else if (done == 0x01) {
            stitch_advance();
        }
    } else {
        if (done >= want) {
            REG8(0x11A689UL) = 0;
            stitch_advance();
        }
    }
}

/* H'2026B2. The live parameters taken from the pattern's working copy --
 * what the operator has set -- rather than from the catalogue. Bit 7 of the
 * descriptor's kind is copied into the working copy's kind on the way, so
 * the two agree about where the pattern came from.
 *
 * Which fields are read depends on the kind. Kinds 1 and H'81 keep one set;
 * 2 and H'82 keep the pair that bit 6 of H'11A7BD selects between; anything
 * else has no working copy worth reading and the catalogue's defaults are
 * used, with the variant flag written back into the working copy so the
 * next pass has it.
 */
void sew_params_from_working(u16 n)
{
    u32 w = stitch_work(n);
    u8  kind;

    REG8(w) = (u8)(REG8(w) & ~0x80);
    REG8(w) = (u8)(REG8(w) | (u8)(REG8(stitch_record(n) + 0x16) & 0x80));

    kind = REG8(w);

    if (kind == 0x01 || kind == 0x81) {
        REG8(0x11A69EUL) = REG8(w + 1);
        sew_param_a_set(REG8(w + 1));
        if (REG8(0x11A7BDUL) & 0x40) {
            REG8(0x11A6A0UL) = REG8(0x11A7C1UL);
            REG8(0xFFFEFCUL) = 0x00;
        } else {
            REG8(0x11A6A0UL) = REG8(w + 2);
            REG8(0xFFFEFCUL) = REG8(w + 7);
        }
        sew_param_b_set(REG8(0x11A6A0UL));
        REG8(0xFFFEEAUL) = REG8(w + 4);
        REG8(0xFFFEECUL) = REG8(w + 5);
        REG8(0xFFFEFBUL) = REG8(w + 6);
        REG8(0x11A7ACUL) = REG8(w + 9);

    } else if (kind == 0x02 || kind == 0x82) {
        REG8(0x11A69EUL) = REG8(w + 1);
        sew_param_a_set(REG8(w + 1));
        if (REG8(0x11A7BDUL) & 0x40) {
            REG8(0x11A6A0UL) = REG8(w + 3);
            REG8(0xFFFEFCUL) = REG8(w + 8);
        } else {
            REG8(0x11A6A0UL) = REG8(w + 2);
            REG8(0xFFFEFCUL) = REG8(w + 7);
        }
        sew_param_b_set(REG8(0x11A6A0UL));
        REG8(0xFFFEEAUL) = REG8(w + 4);
        REG8(0xFFFEECUL) = REG8(w + 5);
        REG8(0xFFFEFBUL) = REG8(w + 6);
        REG8(0x11A7ACUL) = REG8(w + 9);

    } else {
        REG8(0x11A69EUL) = REG8(0x11A7BFUL);
        sew_param_a_set(REG8(0x11A7BFUL));
        if (REG8(0x11A7BDUL) & 0x40) REG8(w + 2) = pattern_variant_flag_b(n);
        else                         REG8(w + 3) = pattern_variant_flag(n);
        REG8(0x11A6A0UL) = REG8(0x11A7C1UL);
        sew_param_b_set(REG8(0x11A7C1UL));
        REG8(0xFFFEEAUL) = (u8)(REG8(0x11A7C3UL) & 0x0F);
        REG8(0xFFFEECUL) = REG8(0x11A7C5UL);
        REG8(0xFFFEFBUL) = 0x00;
        REG8(0xFFFEFCUL) = 0x00;
    }

    REG8(0x11A7D9UL) = REG8(0x11A69EUL);
    REG8(0x11A7DAUL) = REG8(0x11A6A0UL);
    REG8(0x11A7D8UL) = REG8(0xFFFEEAUL);
    REG8(0x11A7DBUL) = REG8(0xFFFEECUL);
    REG8(0x11A7DCUL) = REG8(0xFFFEFBUL);
    REG8(0x11A7DDUL) = REG8(0xFFFEFCUL);
}

/* H'2029FA. */
void sew_params_for_pattern(u16 n)
{
    if (REG8(stitch_record(n) + 0x16) & 0x80) pattern_defaults_load(n, 0);
    if (n != 0) sew_params_from_working(n);
}

/* H'206B00. Makes the pattern at H'11A7E6 the one the machine is sewing.
 *
 * The first half only runs when H'114DCD bit 3 is clear, and it is the
 * expensive part: the pattern's data is streamed in, its variants unpacked,
 * the four counter limits reset to H'1000 and the stitch time worked out
 * from two bytes on the bus.
 *
 * The second half decides where the parameters come from. With no queue,
 * from the pattern's working copy. With a queue and a fresh position, from
 * the queue entry -- unless bit 0 of H'114DC9 says the entry is not to be
 * believed. And the count of what is left in the group decides whether this
 * position is the last one.
 */
void pattern_make_current(void)
{
    u8 v;

    REG8(0x11A668UL) = 0;
    REG8(0x11A669UL) = 0;

    if (!(REG8(0x114DCDUL) & 0x08)) {
        if (REG16(0x11A7E6UL) <= 0x0001) {
            if (REG16(0x11A7E6UL) == 0) REG16(0x11A7E6UL) = 0x0001;
            REG8(0x11A7BCUL) = REG8(0x0E0006UL);
            REG8(0x11A7BDUL) = REG8(0x0E0007UL);
            if (!(REG8(0x11A7BCUL) & 0x08)) {
                if (!(REG8(0x11A6AEUL) & 0x02)) stitch_chunk_first();
            } else {
                REG8(0xFFFEF5UL) |= 0x04;
                REG8(0x11A7D4UL) |= 0x04;
                REG8(0x0E4020UL) = (u8)(REG8(0x0E4020UL) & 0x80);
            }
        } else {
            if (!(REG8(0x11A6AEUL) & 0x02)) stitch_chunk_first();
        }

        stitch_variants_build();

        REG8(0xFFFEFAUL) &= (u8)~0x10;
        REG8(0xFFFEFAUL) = (u8)(REG8(0xFFFEFAUL) & 0x98);
        if (REG8(0x11A7AAUL) <= 0x05) REG8(0xFFFEFAUL) |= 0x20;
        if (REG8(0x11A7AAUL) >= 0x06) {
            REG8(0xFFFEFAUL) = (u8)(REG8(0xFFFEFAUL) | 0x60);
        }

        v = (u8)(bus_byte_10() + stitch_length_limit());
        REG8(0x11A7ACUL) =
            (u8)((short)((short)(u16)((u16)((u16)0x64 * (u16)v) + 7)
                         / (short)15));

        REG8(0x11A7ADUL) = 0x01;
        REG16(0x11A7B4UL) = 0x1000;
        REG16(0x11A7B2UL) = 0x1000;
        REG16(0x11A7B0UL) = 0x1000;
        REG16(0x11A7AEUL) = 0x1000;
        REG16(0xFFFF00UL) = 0x1000;
        REG16(0x11A698UL) = 0x0000;

        if (!(REG8(0x11A6AEUL) & 0x02)) {
            REG8(0x114DC6UL) &= (u8)~0x08;
            REG8(0xFFFEFAUL) &= (u8)~0x80;
            REG8(0xFFFEF7UL) &= (u8)~0x08;
            REG8(0x114DCEUL) &= (u8)~0x40;
        }
    }

    REG8(0x11A6CFUL) = 0;
    REG8(0x11A689UL) = 0;
    REG8(0x114DC7UL) &= (u8)~0x80;
    REG8(0x114DCFUL) &= (u8)~0x08;
    REG8(0x11A6AEUL) &= (u8)~0x02;
    REG8(0x114DCBUL) &= (u8)~0x20;
    REG8(0x114DCAUL) &= (u8)~0x10;

    if (STITCH_SET == 0xB4) REG8(0xFFFEF8UL) |= 0x10;
    REG8(0xFFFEF8UL) &= (u8)~0x20;

    if (!(REG8(0x114DCAUL) & 0x02)) {
        sew_clear_busy_3();
        sew_clear_busy_4();
        REG8(0x11A688UL) = 0;
    }

    if (((REG8(0x114DC6UL) & 0x10) && !(REG8(0x114DCAUL) & 0x02)) ||
        ((REG8(0x114DC6UL) & 0x10) && (REG8(0x114DCAUL) & 0x02) &&
         (u16)(QUEUE_GROUP + 1) == QUEUE_POS)) {
        REG8(0x114DC9UL) |= 0x08;
        REG8(0x114DC7UL) |= 0x08;
    }

    if (!(REG8(0x114DCAUL) & 0x02)) {
        stitch_begin();
        sew_params_for_pattern(REG16(0x11A7E6UL));
        sew_params_publish();
    } else if (!(REG8(0x114DCAUL) & 0x80) || (REG8(0x114DCDUL) & 0x08)) {
        if (REG8(0x114DCDUL) & 0x08) {
            stitch_begin();
        } else {
            stitch_begin();
            sew_params_for_pattern(REG16(0x11A7E6UL));
            sew_params_publish();
        }
    } else if (QUEUE_POS != 0) {
        REG8(0x114DCAUL) &= (u8)~0x80;
        stitch_begin();
        if (REG8(0x114DC9UL) & 0x01) {
            REG8(0x114DC9UL) &= (u8)~0x01;
            sew_params_for_pattern(REG16(0x11A7E6UL));
            sew_params_publish();
        } else {
            queue_entry_unpack();
        }
        if (REG8(0x114DC7UL) & 0x01) sew_params_from_pattern();
        if (queue_group_count() <= 0x0001) queue_flags_group_end();
        else                               queue_flag_clear_cc4();
    }

    REG8(0x114DCDUL) &= (u8)~0x08;

    v = (u8)((short)((short)(u16)((u16)0xDC *
                                  (u16)(u8)((REG8(0x11A7C3UL) & 0xF0) >> 4))
                     / (short)15));
    REG8(0x11A6CEUL) = v;
    if ((REG8(0x11A7BCUL) & 0x04) && v > 0x96) REG8(0x11A6CEUL) = 0x96;

    REG8(0x114DCFUL) &= (u8)~0x40;
    REG8(0x114DCFUL) &= (u8)~0x80;
    if (REG8(0x11A7BCUL) & 0x04) {
        if (!(REG8(0x11A7BDUL) & 0x10)) {
            if (!(REG8(0x11A7BEUL) & 0x20)) REG8(0x114DCFUL) |= 0x40;
            if (!(REG8(0x11A7BEUL) & 0x40)) REG8(0x114DCFUL) |= 0x80;
        }
    }

    sew_lamp_service();

    REG8(0x11A7A9UL) = REG8(0x11A7A8UL);
    REG16(0x11A6C6UL) = QUEUE_POS;
}

/* H'207342. Has the operator changed what is selected?
 *
 * Three cases. H'FFFEFE of H'FFFF means there is no queue at all and the
 * pattern comes from H'FFFEE0 plus H'FFFEFD, the number keyed in. A queue
 * that has just appeared has to be taken up: its bounds noted, the group
 * start found, and the first pattern loaded. And a queue already running
 * only needs looking at when the position, the variant or the pattern has
 * moved -- H'114DC2 is the flag that says a step was taken deliberately
 * rather than by the sewing running on.
 */
void panel_selection_check(void)
{
    u16 t;
    u8  v;

    if (QUEUE_POS == 0xFFFF) {
        v = REG8(0x114DCAUL);
        if (v & 0x02) {
            v = (u8)(v & ~0x02);
            REG8(0x114DC9UL) &= (u8)~0x04;
            REG8(0x114DC9UL) &= (u8)~0x01;
            REG8(0x114DC9UL) &= (u8)~0x02;
            v = (u8)(v & ~0x80);
            REG8(0x114DCAUL) = v;
            REG16(0x11A6C6UL) = 0;
            t = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
            REG16(0x11A7E6UL) = t;
            REG16(0x11A6C4UL) = t;
            REG8(0x11A6AEUL) |= 0x01;
            REG8(0x114DCDUL) &= (u8)~0x08;
            return;
        }

        t = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
        if (t == REG16(0x11A6C4UL) &&
            REG8(0x11A7A8UL) == REG8(0x11A7A9UL)) return;

        t = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
        REG16(0x11A7E6UL) = t;
        REG16(0x11A6C4UL) = t;
        if (REG8(0x11A7A8UL) != REG8(0x11A7A9UL)) {
            REG8(0x114DCDUL) |= 0x08;
            REG8(0x11A6AEUL) |= 0x01;
        } else {
            REG8(0x11A6AEUL) |= 0x01;
            REG8(0x114DCDUL) &= (u8)~0x08;
        }
        return;
    }

    if (REG16(0x114DC4UL) != QUEUE_POS) {
        if (REG16(0x114DC4UL) == QUEUE_GROUP &&
            (u16)(QUEUE_GROUP + 1) == QUEUE_POS) {
            REG8(0x114DC2UL) = 0x01;
        }
        REG16(0x114DC4UL) = QUEUE_POS;
    }

    if (!(REG8(0x114DCAUL) & 0x02)) {
        REG8(0x114DCAUL) |= 0x02;
        REG8(0x114DC9UL) |= 0x04;
        REG16(0x11A66CUL) = queue_entry_ref(0);
        REG16(0x11A6CAUL) = QUEUE_LAST;

        if (QUEUE_POS == QUEUE_FIRST) {
            QUEUE_POS = (u16)(QUEUE_FIRST + 1);
            QUEUE_GROUP = QUEUE_FIRST;
        } else {
            queue_group_start();
        }

        if ((queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END) return;
        if (QUEUE_FIRST == QUEUE_LAST) return;

        REG16(0x11A7E6UL) = queue_pattern(QUEUE_POS);
        REG16(0x11A66EUL) = REG16(0x11A7E6UL);
        REG8(0x11A6AEUL) |= 0x01;
        REG8(0x114DCDUL) &= (u8)~0x08;
        REG8(0x114DCAUL) |= 0x80;
        return;
    }

    if (REG8(0x11A7A8UL) != REG8(0x11A7A9UL)) {
        REG8(0x114DCDUL) |= 0x08;
        REG8(0x11A6AEUL) |= 0x01;
        REG8(0x114DCAUL) &= (u8)~0x80;
        return;
    }

    if (!(QUEUE_POS == REG16(0x11A6C6UL) &&
          queue_pattern(QUEUE_POS) == REG16(0x11A66EUL) &&
          REG8(0x114DC2UL) != 0x01)) {
        if ((queue_entry_ref(QUEUE_POS) & 0x03FF) != QUEUE_END &&
            QUEUE_FIRST != QUEUE_LAST &&
            QUEUE_POS != QUEUE_GROUP) {
            t = queue_pattern(QUEUE_POS);
            REG16(0x11A7E6UL) = t;
            REG16(0x11A66EUL) = t;
            REG16(0x11A6C4UL) = t;
            REG8(0x11A6AEUL) |= 0x01;
            REG8(0x114DCDUL) &= (u8)~0x08;
            REG8(0x114DCAUL) |= 0x80;
            REG8(0x114DC9UL) |= 0x04;
            REG8(0x114DC2UL) = 0x00;
        }
    }

    if (QUEUE_LAST != REG16(0x11A6CAUL) ||
        (queue_entry_ref(QUEUE_POS) & 0x03FF) == QUEUE_END) {
        REG8(0x114DC9UL) |= 0x02;
        REG16(0x11A6CAUL) = QUEUE_LAST;
    }
}

/* H'207706, H'207726. The other two of the four things the panel can be
 * asked for: a full rebuild, and one stitch's worth of running. */
void redraw_full(void)
{
    REG8(0x11A6AEUL) &= (u8)~0x01;
    REG8(0x11A6AEUL) &= (u8)~0x04;
    REG8(0x11A6AEUL) |=        0x08;
    pattern_make_current();
}

void redraw_running(void)
{
    u8 v;

    REG8(0x11A6AEUL) |= 0x04;
    REG8(0x11A6AEUL) |= 0x08;

    v = REG8(0x11A6CFUL);
    if (v != 0) {
        v = (u8)(v - 1);
        REG8(0x11A6CFUL) = v;
        if (v <= 0x03) REG8(0x114DC7UL) |= 0x80;
    }

    if (REG8(0x114DCAUL) & 0x10)      redraw_after_edit();
    else if (REG8(0x114DC9UL) & 0x10) stop_step_b();
    else if (REG8(0x114DC9UL) & 0x08) stop_step_a();
    else                              stitch_tick();
}

/* H'20778A, H'20785E. The panel's own little state machine, at H'11A6AD.
 * A pass picks one of the four things to do from the bits in H'11A6AE, does
 * it, and parks at H'63 so the next pass starts again. The two differ in
 * which bits take precedence -- H'20785E is the one used while the machine
 * is running, and it puts the stitch work first. */
void panel_service_idle(void)
{
    u8 what, bits;

    if (REG8(0x11A6ADUL) == 0) {
        panel_selection_check();

        bits = REG8(0x11A6AEUL);
        if (!(bits & 0x80) && !(bits & 0x01)) REG8(0x11A6ADUL) = 0x01;

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && !(bits & 0x08) && (bits & 0x04) && !(bits & 0x01)) {
            REG8(0x11A6ADUL) = 0x04;
        }

        bits = REG8(0x11A6AEUL);
        if (bits & 0x01) REG8(0x11A6ADUL) = 0x03;

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && (bits & 0x02)) REG8(0x11A6ADUL) = 0x02;
    }

    what = REG8(0x11A6ADUL);
    if (what == 0x01) {
        redraw_partial();
        if (REG8(0x11A6AEUL) & 0x02) { what = 0x02; REG8(0x11A6ADUL) = 0x02; }
        else                         { what = 0x63; REG8(0x11A6ADUL) = 0x63; }
    }
    if (what == 0x02) {
        redraw_pattern();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x03) {
        redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x04) {
        redraw_running();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x63) REG8(0x11A6ADUL) = 0x00;
}

void panel_service_running(void)
{
    u8 what, bits;

    if (REG8(0x11A6ADUL) == 0) {
        panel_selection_check();

        if (!(REG8(0x11A6AEUL) & 0x80)) REG8(0x11A6ADUL) = 0x01;

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && !(bits & 0x08) && (bits & 0x04) && !(bits & 0x01)) {
            REG8(0x11A6ADUL) = 0x04;
        }

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && !(bits & 0x01) && (bits & 0x02)) {
            REG8(0x11A6ADUL) = 0x02;
        }

        bits = REG8(0x11A6AEUL);
        if ((bits & 0x80) && (bits & 0x01)) REG8(0x11A6ADUL) = 0x03;
    }

    what = REG8(0x11A6ADUL);
    if (what == 0x01) {
        redraw_partial();
        bits = REG8(0x11A6AEUL);
        if (!(bits & 0x01) && (bits & 0x02)) {
            what = 0x02; REG8(0x11A6ADUL) = 0x02;
        } else {
            what = 0x63; REG8(0x11A6ADUL) = 0x63;
        }
    }
    if (what == 0x02) {
        redraw_pattern();
        if (REG8(0x11A6AEUL) & 0x80) redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x03) {
        redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x04) {
        redraw_running();
        if (REG8(0x11A6AEUL) & 0x01) redraw_full();
        what = 0x63; REG8(0x11A6ADUL) = 0x63;
    }
    if (what == 0x63) REG8(0x11A6ADUL) = 0x00;
}

/* H'207956. One pass of the panel, with the chunk loader kept fed: if the
 * pattern's data has not all arrived yet, another chunk comes in first. */
void panel_service(void)
{
    if (REG8(0x11A6D1UL) < REG8(0x11A6D2UL)) stitch_chunk_load();

    if (REG8(0x114DC6UL) & 0x80) panel_service_running();
    else                         panel_service_idle();
}

/* ---- stopping in the right place ----------------------------------------
 * The needle is not stopped the moment it is asked to be: it has to finish
 * the stitch, and on some patterns the half-way point counts as a place to
 * stop as well as the end. H'11A6CF is a countdown of stitches still to go
 * before the stop takes.
 */

/* H'203A10. How many stitches that is. H'11A7DE is the repeat count, three
 * stitches each and one over; four when there is no repeat. Four more when
 * the machine is in the mode at H'114DC6 bit 4 and the stop was asked for at
 * the end rather than the half-way point.
 *
 * The two comparisons the original makes after that have empty bodies and
 * are left out. */
void stop_countdown_set(u8 at_half)
{
    u8 v = REG8(0x11A7DEUL);

    if (v == 0) REG8(0x11A6CFUL) = 0x04;
    else        REG8(0x11A6CFUL) = (u8)((u8)((u16)v * (u16)3) + 1);

    if (REG8(0x114DC6UL) & 0x10) {
        if (at_half == 0) {
            REG8(0x11A6CFUL) = (u8)(REG8(0x11A6CFUL) + 4);
        }
    }
}

/* H'203A6E. Whether this stitch is one the machine may stop on.
 *
 * A stop that has come due clears itself and lets the presser foot back
 * down. Then, if the position has reached the last few stitches of the
 * pattern -- or of its first half, when H'11A7D4 bit 7 says the half-way
 * point counts -- the countdown is armed. What arms it differs between a
 * queue and a single pattern, and a pattern with more than one variant only
 * stops between variants when it is marked to.
 */
void needle_stop_service(void)
{
    u16 limit = 0;
    u8  at_half;
    u8  v;

    if (!(REG8(0x114DC6UL) & 0x80)) {
        v = REG8(0xFFFEC7UL);
        if (v & 0x40) {
            REG8(0xFFFEC7UL) = (u8)(v & ~0x40);
            REG8(0xFFFEF8UL) &= (u8)~0x20;
            v = REG8(0x114DCFUL);
            if (v & 0x04) {
                REG8(0x114DCFUL) = (u8)(v & ~0x04);
                REG8(0xFFFEF6UL) |= 0x20;
            }
            REG8(0x114DC7UL) &= (u8)~0x80;
            REG8(0x11A6CFUL) = 0;
        }
    }

    if ((REG8(0x11A7D4UL) & 0x80) &&
        (u16)(REG16(0x11A67EUL) >> 1) > REG16(0x11A680UL)) {
        at_half = 1;
        if ((u16)(REG16(0x11A67EUL) >> 1) > 0x0003) {
            limit = (u16)((u16)(REG16(0x11A67EUL) >> 1) + 0xFFFD);
        }
    } else {
        at_half = 0;
        if (REG16(0x11A67EUL) > 0x0003) {
            limit = (u16)(REG16(0x11A67EUL) + 0xFFFD);
        }
    }

    if (REG16(0x11A680UL) < limit) return;
    if (REG8(0x11A6CFUL) != 0) return;

    if (!(REG8(0x114DCAUL) & 0x02)) {
        if (at_half != 0) {
            stop_countdown_set(at_half);
            return;
        }

        v = 1;
        if (!(REG8(0x11A7BCUL) & 0x10) && !(REG8(0x11A7BDUL) & 0x20)) {
            if (!(REG8(0xFFFEF8UL) & 0x20))    v = 0;
            else if (REG8(0x114DC6UL) & 0x08)  v = 0;
        }
        if (v && !(REG8(0x114DC6UL) & 0x40)) stop_countdown_set(at_half);

        if (REG8(0x114DC6UL) & 0x40) {
            if (REG8(0x11A688UL) >= REG8(0x11A7DFUL)) {
                if (REG8(0x11A7AAUL) > 0x01) {
                    if (REG8(0x11A7BCUL) & 0x10) stop_countdown_set(at_half);
                } else {
                    stop_countdown_set(at_half);
                }
            }
        }
        return;
    }

    if (at_half != 0) {
        stop_countdown_set(at_half);
        return;
    }

    v = 0;
    if (!(REG8(0xFFFEF8UL) & 0x20) && !(REG8(0x11A7BDUL) & 0x20)) {
        if (!(REG8(0x114DCAUL) & 0x08))         v = 1;
        else if (!(REG8(0x114DC6UL) & 0x40))    v = 1;
        else if (REG8(0x11A688UL) < REG8(0x11A7DFUL)) v = 1;
    }
    if (v) return;

    if (REG8(0x11A7AAUL) > 0x01) {
        if (REG8(0x11A7BCUL) & 0x10) stop_countdown_set(at_half);
    } else {
        stop_countdown_set(at_half);
    }
}

/* ---- turning a stitch into two motor positions --------------------------
 * A stitch in the data is a pair of numbers: how far to move the needle
 * sideways and how far to move the feed. Neither goes to a motor as it
 * stands. Both are scaled by what the operator has set, offset by the
 * pattern's own trim, clamped, and turned round when the pattern is
 * mirrored. H'11A6B8 is where the feed ends up and H'11A6B9/H'11A6BA the
 * two ends of the needle's swing.
 */

/* H'205628. The feed.
 *
 * The low six bits of the stitch's second byte are a code, and thirteen of
 * the codes mean something other than a distance -- a fixed step, a stop, or
 * a step worked out from how far past a limit the counter has run. Anything
 * else is a plain proportion of the stitch length.
 */
void feed_position_set(void)
{
    u8  len   = REG8(0x11A7DAUL);
    u8  full  = 0x64;
    u8  trim  = REG8(0x11A7DDUL);
    u16 over  = sew_limit_overshoot();
    u8  code, amount, result;
    u8  idx;

    if (len > REG8(0x11A7C2UL)) len = REG8(0x11A7C2UL);

    if (REG8(0x114DC6UL) & 0x80) code = REG8(0x11A7C9UL);
    else                         code = REG8(0x11A6B1UL);

    if (code & 0x80) REG8(0x11A6AEUL) |=        0x40;
    else             REG8(0x11A6AEUL) &= (u8)~0x40;

    idx = (u8)((u8)(code & 0x3F) + 0xCD);

    switch (idx) {
    case 0x00: amount = 0x00; break;
    case 0x01: amount = 0x02; break;
    case 0x07: amount = 0x14; break;
    case 0x04: amount = 0x1E; break;
    case 0x08: amount = 0x28; break;
    case 0x05: case 0x06: case 0x09:
    case 0x0A: case 0x0B: case 0x0C:
        amount = 0x00;
        REG8(0x11A6AEUL) |= 0x40;
        break;

    case 0x02:
        if ((REG8(0x114DC6UL) & 0x08) && !(REG8(0x11A7D7UL) & 0x01)) {
            if ((u16)((u16)0x0003 * over) >= 0x0032) {
                amount = 0x32;
            } else {
                amount = (u8)((u8)((u16)(u8)over * (u16)3) + 4);
                if (amount < 0x0C) amount = 0x0C;
            }
        } else {
            amount = (u8)((short)((short)(u16)((u16)((u16)full * (u16)len)
                                              + 0x0032) / (short)0x0064));
            if (amount <= 0x0C) amount = 0x0C;
        }
        break;

    case 0x03:
        if ((REG8(0x114DC6UL) & 0x08) &&
            (u16)((u16)0x0003 * over) <= (u16)len &&
            !(REG8(0x11A7D7UL) & 0x01)) {
            amount = (u8)((u8)((u16)(u8)over * (u16)3) + 4);
            if (amount < 0x0C) amount = 0x0C;
        } else {
            amount = (u8)((short)((short)(u16)((u16)((u16)full * (u16)len)
                                              + 0x0032) / (short)0x0064));
        }
        break;

    default:
        amount = (u8)((short)((short)(u16)(
                     (u16)((u16)((u16)(u8)(code & 0x3F) << 1) * (u16)len)
                     + 0x0032) / (short)0x0064));
        break;
    }

    if (!((REG8(0x11A7BCUL) & 0x02) && !(code & 0x40))) {
        /* The original writes the same addition in both halves of a test on
         * the sign of the trim, so the sign makes no difference here. */
        if (!(REG8(0x114DCAUL) & 0x10)) full = (u8)(full + trim);
    }

    if (REG8(0x114DCBUL) & 0x20) amount = 0;

    if (stitch_run_head(REG16(0x11A7E6UL)) == 0x0011) {
        if (REG8(0x11A7BCUL) & 0x04) {
            u8 k = REG8(0x11A7E1UL);
            if (k == 0x01 || k == 0x02) {
                u8 f = REG8(0x11A6AEUL);
                if (f & 0x40) REG8(0x11A6AEUL) = (u8)(f & ~0x40);
                else          REG8(0x11A6AEUL) = (u8)(f |  0x40);
            }
        }
    }

    if (amount >= 0x3C && (REG8(0x114DCDUL) & 0x01)) amount = 0x3C;
    if (REG8(0x11A668UL) != 0) amount = 0;

    {
        u8 bits = REG8(0x11A6AEUL);
        int subtract = ((bits & 0x20) != 0) == ((bits & 0x40) != 0);

        if (subtract) {
            result = (full >= amount) ? (u8)(full - amount) : 0;
        } else if ((short)(u16)(0x00C8 - (u16)full) >= (short)(u16)amount) {
            result = (u8)(full + amount);
        } else {
            result = 0xC8;
        }
    }

    REG8(0x11A6B8UL) = (u8)((u8)(REG8(0xFFFED8UL) + result) + 2);
}

/* H'2058D8. The needle. Only patterns marked as three bytes a stitch have a
 * sideways component at all; the rest sit in the middle. The two ends come
 * out either side of H'18, and bit 6 of the stitch's own byte says which
 * way round. */
void needle_position_apply(void)
{
    u8 trim = REG8(0x11A7DCUL);
    u8 wide = REG8(0x11A7D9UL);
    u8 len  = REG8(0x11A7DAUL);
    u8 raw  = 0;
    u8 v    = 0;
    u8 t;

    if (REG8(0x11A7BCUL) & 0x04) {
        if (REG8(0x114DC6UL) & 0x80) raw = REG8(0x11A7CAUL);
        else                         raw = REG8(0x11A6B3UL);
        v = (u8)(raw & 0x3F);

        if (REG8(0x11A7BDUL) & 0x10) {
            if (len <= REG8(0x11A7C2UL)) {
                v = (u8)((short)((short)(u16)((u16)len * (u16)v)
                                 / (short)(u16)REG8(0x11A7C2UL)));
            }
        } else if (REG8(0x11A7BEUL) & 0x20) {
            if (wide <= REG8(0x11A7C0UL)) {
                v = (u8)((short)((short)(u16)((u16)wide * (u16)v)
                                 / (short)(u16)REG8(0x11A7C0UL)));
            }
        }

        if (!((REG8(0x11A7BCUL) & 0x02) && !(REG8(0x11A7C9UL) & 0x40))) {
            if (!(REG8(0x114DCAUL) & 0x10)) {
                if (trim & 0x80) {
                    trim = (u8)(-(signed char)trim);
                    if (!(raw & 0x40)) {
                        if (v >= trim) v = (u8)(v - trim);
                        else           v = 0;
                    }
                } else {
                    if (raw & 0x40) {
                        if (v >= trim) v = (u8)(v - trim);
                        else           v = 0;
                    }
                }
            }
        }

        t = REG8(0x11A7E1UL);
        if (t == 0x00 || t == 0x03) {
            v = (u8)(v | (u8)(raw & 0xC0));
        } else if (t >= 0x01 && t <= 0x02) {
            if (stitch_run_head(REG16(0x11A7E6UL)) == 0x0011) {
                v = (u8)(v | (u8)(raw & 0xC0));
            } else {
                v = (u8)(v | (u8)((u8)(raw & 0xC0) ^ 0x40));
            }
        }
    } else {
        raw = 0;
        v = 0;
    }

    if (v & 0x40) {
        v = (u8)(v & ~0x40);
        t = (u8)((u8)(v / 2) + 0x18);
        REG8(0x11A6BAUL) = t;
        if (v & 0x01) {
            t = (u8)(t + 1);
            REG8(0x11A6BAUL) = t;
        }
        REG8(0x11A6B9UL) = (u8)(t - v);
    } else {
        if (v == 0x01) REG8(0x11A6BAUL) = 0x17;
        else           REG8(0x11A6BAUL) = (u8)(0x18 - (u8)(v / 2));
        REG8(0x11A6B9UL) = (u8)(REG8(0x11A6BAUL) + v);
    }

    REG8(0x11A6BAUL) = (u8)(REG8(0x11A6BAUL) + 1);
    REG8(0x11A6B9UL) = (u8)(REG8(0x11A6B9UL) + 1);
}

/* ---- interpolating between stitches --------------------------------------
 * A repeated stitch does not repeat the same needle position: it walks from
 * one to the next in as many steps as the repeat count. The four positions
 * of the stitch being worked on are held at H'11A68A, H'11A68C, H'11A68E and
 * H'11A690 as 8.8 fixed point, the three step sizes between them at
 * H'11A692, H'11A694 and H'11A696, and bits 4 to 7 of H'114DCD say which way
 * round each pair is. H'11A689 counts the step.
 */

/* Rounding a 8.8 value to the whole number nearest it. */
static u8 fixed_round(u16 v)
{
    return (u8)((u16)(v + 0x0080) >> 8);
}

#define SPAN_A  REG16(0x11A68AUL)
#define SPAN_B  REG16(0x11A68CUL)
#define SPAN_C  REG16(0x11A68EUL)
#define SPAN_D  REG16(0x11A690UL)
#define STEP_P  REG16(0x11A692UL)
#define STEP_Q  REG16(0x11A694UL)
#define STEP_R  REG16(0x11A696UL)

/* H'203C32. The four positions out of the stitch bytes, and the three step
 * sizes between them. */
void stitch_spans_build(void)
{
    u16 a, b, c, d;
    u8  rep;

    if (REG8(0x11A7C8UL) & 0x80) {
        if (REG8(0x11A7CBUL) & 0x80) REG8(0x114DCDUL) |= 0x10;
    } else {
        REG8(0x114DCDUL) &= (u8)~0x10;
    }

    a = (u16)((u16)(u8)(REG8(0x11A7C8UL) & 0x3F) << 8);
    b = (u16)((u16)(u8)(REG8(0x11A7CBUL) & 0x3F) << 8);
    SPAN_B = b;
    c = (u16)((u16)(u8)(REG8(0x11A7CEUL) & 0x3F) << 8);
    d = (u16)((u16)(u8)(REG8(0x11A7D1UL) & 0x3F) << 8);
    SPAN_D = d;
    SPAN_C = c;
    SPAN_A = a;

    rep = REG8(0x11A7DEUL);

    if (c > a) { REG8(0x114DCDUL) &= (u8)~0x20; STEP_P = (u16)((u32)(u16)(c - a) / rep); }
    else       { REG8(0x114DCDUL) |=       0x20; STEP_P = (u16)((u32)(u16)(a - c) / rep); }

    if (d > b) { REG8(0x114DCDUL) &= (u8)~0x40; STEP_Q = (u16)((u32)(u16)(d - b) / rep); }
    else       { REG8(0x114DCDUL) |=       0x40; STEP_Q = (u16)((u32)(u16)(b - d) / rep); }

    if (b > a) { REG8(0x114DCDUL) &= (u8)~0x80; STEP_R = (u16)((u32)(u16)(b - a) / rep); }
    else       { REG8(0x114DCDUL) |=       0x80; STEP_R = (u16)((u32)(u16)(a - b) / rep); }
}

/* H'204CCA. Where the needle goes on step H'11A689 of a repeat, walking
 * between the first two positions. */
u8 needle_step(void)
{
    u8 i   = REG8(0x11A689UL);
    u8 rep = REG8(0x11A7DEUL);
    u8 f   = REG8(0x114DCDUL);

    if (i == 0x00) {
        stitch_spans_build();
        return REG8(0x11A68AUL);
    }
    if (i == 0x01) {
        return (f & 0x80) ? fixed_round((u16)(SPAN_A - STEP_R))
                          : fixed_round((u16)(SPAN_A + STEP_R));
    }
    if (i == 0x02) {
        if (rep == 0x03) {
            return (f & 0x80) ? fixed_round((u16)(STEP_R + SPAN_B))
                              : fixed_round((u16)(SPAN_B - STEP_R));
        }
        return (f & 0x80) ? fixed_round((u16)(SPAN_A - (u16)(STEP_R << 1)))
                          : fixed_round((u16)((u16)(STEP_R << 1) + SPAN_A));
    }
    if (i == 0x03) {
        if (rep == 0x04) {
            return (f & 0x80) ? fixed_round((u16)(STEP_R + SPAN_B))
                              : fixed_round((u16)(SPAN_B - STEP_R));
        }
        return (f & 0x80) ? fixed_round((u16)((u16)(STEP_R << 1) + SPAN_B))
                          : fixed_round((u16)(SPAN_B - (u16)(STEP_R << 1)));
    }
    if (i == 0x04) {
        return (f & 0x80) ? fixed_round((u16)(STEP_R + SPAN_B))
                          : fixed_round((u16)(SPAN_B - STEP_R));
    }
    return REG8(0x11A68AUL);
}

/* H'204E6A. The same for a mirrored pattern, which walks all four positions
 * rather than two, so there are ten steps rather than five. */
u8 needle_step_mirrored(void)
{
    u8 i   = REG8(0x11A689UL);
    u8 rep = REG8(0x11A7DEUL);
    u8 f   = REG8(0x114DCDUL);

    switch (i) {
    case 0x00:
        stitch_spans_build();
        return REG8(0x11A68AUL);
    case 0x01:
        return REG8(0x11A68CUL);
    case 0x02:
        return (f & 0x20) ? fixed_round((u16)(SPAN_A - STEP_P))
                          : fixed_round((u16)(STEP_P + SPAN_A));
    case 0x03:
        return (f & 0x40) ? fixed_round((u16)(SPAN_B - STEP_Q))
                          : fixed_round((u16)(STEP_Q + SPAN_B));
    case 0x04:
        if (rep == 0x03) {
            return (f & 0x20) ? fixed_round((u16)(STEP_P + SPAN_C))
                              : fixed_round((u16)(SPAN_C - STEP_P));
        }
        return (f & 0x20) ? fixed_round((u16)(SPAN_A - (u16)(STEP_P << 1)))
                          : fixed_round((u16)((u16)(STEP_P << 1) + SPAN_A));
    case 0x05:
        if (rep == 0x03) {
            return (f & 0x40) ? fixed_round((u16)(STEP_Q + SPAN_D))
                              : fixed_round((u16)(SPAN_D - STEP_Q));
        }
        return (f & 0x40) ? fixed_round((u16)(SPAN_B - (u16)(STEP_Q << 1)))
                          : fixed_round((u16)((u16)(STEP_Q << 1) + SPAN_B));
    case 0x06:
        if (rep == 0x04) {
            return (f & 0x20) ? fixed_round((u16)(STEP_P + SPAN_C))
                              : fixed_round((u16)(SPAN_C - STEP_P));
        }
        return (f & 0x20) ? fixed_round((u16)((u16)(STEP_P << 1) + SPAN_C))
                          : fixed_round((u16)(SPAN_C - (u16)(STEP_P << 1)));
    case 0x07:
        if (rep == 0x04) {
            return (f & 0x40) ? fixed_round((u16)(STEP_Q + SPAN_D))
                              : fixed_round((u16)(SPAN_D - STEP_Q));
        }
        return (f & 0x40) ? fixed_round((u16)((u16)(STEP_Q << 1) + SPAN_D))
                          : fixed_round((u16)(SPAN_D - (u16)(STEP_Q << 1)));
    case 0x08:
        return (f & 0x20) ? fixed_round((u16)(STEP_P + SPAN_C))
                          : fixed_round((u16)(SPAN_C - STEP_P));
    case 0x09:
        return (f & 0x40) ? fixed_round((u16)(STEP_Q + SPAN_D))
                          : fixed_round((u16)(SPAN_D - STEP_Q));
    default:
        return REG8(0x11A68AUL);
    }
}

/* H'205266. The needle's own position for this stitch.
 *
 * H'11A7BE's low three bits are how wide the swing is in whole units, and
 * H'11A7D9 the width the operator has set -- capped at H'38 on the H'B4 data
 * set, and scaled by H'43/H'64 when the pattern asks. From those come the
 * two ends of the swing, either side of H'19.
 *
 * Then the same thirteen codes the feed uses, and again most of them are a
 * fixed place rather than a proportion. The last case is the one that
 * matters: it is a plain position, walked through a repeat if there is one,
 * turned round if the pattern is mirrored, and finally interpolated between
 * the two ends by sew_interpolate.
 */
void needle_span_set(void)
{
    u8 span = (u8)(REG8(0x11A7BEUL) & 0x07);
    u8 trim = 0;
    u8 wide = REG8(0x11A7D9UL);
    u8 lo, hi, half, idx, code, k;

    if (STITCH_SET == 0xB4 && !(REG8(0xFFFEF8UL) & 0x10)) {
        if (wide > 0x38) wide = 0x38;
    }

    if (!(REG8(0x11A7D4UL) & 0x04) && (REG8(0x11A7BCUL) & 0x20)) {
        wide = (u8)((short)((short)(u16)((u16)0x43 * (u16)wide)
                            / (short)0x0064));
    }

    if (wide >= 0x42 && (REG8(0xFFFEF8UL) & 0x10)) span = (u8)(span + 1);

    if (REG8(0x11A7BEUL) & 0x10) {
        lo = (u8)(0x19 - (u8)(span / 2));
        hi = (u8)(lo + span);
    } else {
        hi = (u8)((u8)(span / 2) + 0x19);
        lo = (u8)(hi - span);
    }

    half = (u8)(wide / 2);
    code = REG8(0x11A7C8UL);
    idx  = (u8)((u8)(code & 0x3F) + 0xCD);

    switch (idx) {
    case 0x00:
        REG8(0x11A6B6UL) = (u8)(lo - (u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)));
        break;
    case 0x04:
        half = (u8)((short)((short)((short)(u16)((u16)0x57 * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)(lo - (u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)));
        break;
    case 0x05:
        half = (u8)((short)((short)((short)(u16)((u16)0x4B * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)(lo - (u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)));
        break;
    case 0x06: REG8(0x11A6B6UL) = (u8)(lo + 0xFD); break;
    case 0x01: REG8(0x11A6B6UL) = lo;              break;
    case 0x08: REG8(0x11A6B6UL) = 0x19;            break;
    case 0x02: REG8(0x11A6B6UL) = hi;              break;
    case 0x07: REG8(0x11A6B6UL) = (u8)(hi + 3);    break;
    case 0x09:
        half = (u8)((short)((short)((short)(u16)((u16)0x4B * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)((u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)) + hi);
        break;
    case 0x0A:
        half = (u8)((short)((short)((short)(u16)((u16)0x57 * (u16)wide)
                                    / (short)0x0064)) / (short)2);
        REG8(0x11A6B6UL) = (u8)((u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)) + hi);
        break;
    case 0x03:
        REG8(0x11A6B6UL) = (u8)((u8)((short)((short)(u16)
                                ((u16)half - (u16)span) / (short)2)) + hi);
        break;
    case 0x0B: REG8(0xFFFEFAUL) |= 0x10; break;
    case 0x0C: break;

    default:
        REG8(0x11A6B6UL) = (u8)(code & 0x3F);

        if (REG8(0x11A7DEUL) != 0) {
            if (REG8(0x114DCDUL) & 0x10) REG8(0x11A6B6UL) = needle_step_mirrored();
            else                          REG8(0x11A6B6UL) = needle_step();
        }

        if (!(stitch_run_head(REG16(0x11A7E6UL)) == 0x0011 &&
              (REG8(0x11A7BCUL) & 0x04))) {
            k = REG8(0x11A7E1UL);
            if (k == 0x01 || k == 0x03) {
                REG8(0x11A6B6UL) = (u8)(0x32 - REG8(0x11A6B6UL));
            }
        }

        if ((REG8(0x114DCEUL) & 0x80) && (REG8(0x11A7BCUL) & 0x20) &&
            stitch_run_head(REG16(0x11A7E6UL)) != 0x0015 &&
            (REG8(0x114DCAUL) & 0x02)) {
            trim = 0x16;
        }

        REG8(0x11A6B6UL) = (u8)(sew_interpolate(
                                    (u8)(REG8(0x11A6B6UL) << 1), wide, trim)
                                / 2);
        break;
    }

    REG8(0x11A6B7UL) = (u8)(REG8(0x11A6B6UL) + 2);
}

/* H'207988. A whole pass of the sewing side: the panel, the once-a-pass
 * service, the stop check, the interpolation offset, and then -- only when
 * nothing is mid-rebuild -- the geometry that turns the current stitch into
 * the two motor positions. The other data set has no geometry of its own and
 * parks everything at H'FF. */
void sew_pass(void)
{
    panel_service();
    sew_service();
    needle_stop_service();
    sew_offset_set();

    if (REG8(0x11A66AUL) != 0) return;

    needle_span_set();
    feed_position_set();

    if (STITCH_SET == 0xB4) {
        needle_position_apply();
        sew_limit_6BB();
    } else {
        REG8(0xFFFED1UL) = 0xFF;
        REG8(0x11A6BBUL) = 0xFF;
        REG8(0xFFFED7UL) = 0xFF;
        REG8(0x11A6B9UL) = 0xFF;
        REG8(0x11A6BAUL) = 0xFF;
    }
}

/* H'207F74. The live parameters written back into the working copies.
 *
 * Eight of them are watched against the copy at H'11A7F0, and only a real
 * change is written through -- every write is a flash-backed setting for
 * that pattern, and the panel calls this on every pass.
 *
 * A change does not necessarily belong to one pattern. Byte +H'17 of a
 * descriptor marks a pattern as a continuation of the one before it, so a
 * run of them is one thing as far as the operator is concerned; the walk
 * forward and back finds the head of the run, and if the head is marked
 * H'10 the change goes to every member rather than to the one.
 */
void sew_params_writeback(u16 n)
{
    u16 first = n;
    u8  changed = 0;
    u8  count = 0;
    u8  mark;
    u8  i;

    mark = REG8(stitch_record(n) + 0x17);

    if (REG16(0x11A7FAUL) != n) {
        REG8(0x11A7F8UL) = 0; REG8(0x11A7F7UL) = 0; REG8(0x11A7F6UL) = 0;
        REG8(0x11A7F5UL) = 0; REG8(0x11A7F4UL) = 0; REG8(0x11A7F3UL) = 0;
        REG8(0x11A7F2UL) = 0; REG8(0x11A7F1UL) = 0; REG8(0x11A7F0UL) = 0;
    }

    if (REG8(0x11A69EUL) != REG8(0x11A7F0UL)) {
        REG8(0x11A7F0UL) = REG8(0x11A69EUL); changed = 1;
    }
    if (REG8(0x11A7BDUL) & 0x40) {
        if (REG8(0x11A6A0UL) != REG8(0x11A7F2UL)) {
            REG8(0x11A7F2UL) = REG8(0x11A6A0UL); changed = 1;
        }
    } else {
        if (REG8(0x11A6A0UL) != REG8(0x11A7F1UL)) {
            REG8(0x11A7F1UL) = REG8(0x11A6A0UL); changed = 1;
        }
    }
    if (REG8(0xFFFEEAUL) != REG8(0x11A7F3UL)) {
        REG8(0x11A7F3UL) = REG8(0xFFFEEAUL); changed = 1;
    }
    if (REG8(0xFFFEECUL) != REG8(0x11A7F4UL)) {
        REG8(0x11A7F4UL) = REG8(0xFFFEECUL); changed = 1;
    }
    if (REG8(0x11A7BDUL) & 0x40) {
        if (REG8(0xFFFEFCUL) != REG8(0x11A7F6UL)) {
            REG8(0x11A7F6UL) = REG8(0xFFFEFCUL); changed = 1;
        }
    } else {
        if (REG8(0xFFFEFCUL) != REG8(0x11A7F5UL)) {
            REG8(0x11A7F5UL) = REG8(0xFFFEFCUL); changed = 1;
        }
    }
    if (REG8(0xFFFEFBUL) != REG8(0x11A7F7UL)) {
        REG8(0x11A7F7UL) = REG8(0xFFFEFBUL); changed = 1;
    }
    if (REG8(0x11A7ACUL) != REG8(0x11A7F8UL)) {
        REG8(0x11A7F8UL) = REG8(0x11A7ACUL); changed = 1;
    }

    if (changed == 0 && REG16(0x11A7FAUL) == n) return;

    REG16(0x11A7FAUL) = n;

    do {
        n = (u16)(n + 1);
        mark = REG8(stitch_record(n) + 0x17);
    } while (mark == 0x01);

    do {
        count = (u8)(count + 1);
        n = (u16)(n - 1);
        mark = REG8(stitch_record(n) + 0x17);
    } while (mark == 0x01);

    if (mark == 0x10) {
        for (i = 0; i < count; i++) {
            stitch_params_set(0, n,
                              1, REG8(0x11A69EUL), 1, REG8(0x11A6A0UL),
                              1, REG8(0xFFFEEAUL), 1, REG8(0xFFFEECUL),
                              1, REG8(0xFFFEFCUL), 1, REG8(0xFFFEFBUL),
                              1, REG8(0x11A7ACUL));
            n = (u16)(n + 1);
        }
    } else {
        stitch_params_set(0, first,
                          1, REG8(0x11A69EUL), 1, REG8(0x11A6A0UL),
                          1, REG8(0xFFFEEAUL), 1, REG8(0xFFFEECUL),
                          1, REG8(0xFFFEFCUL), 1, REG8(0xFFFEFBUL),
                          1, REG8(0x11A7ACUL));
    }
}

/* H'206EB4. Once a second -- H'114DDE counts milliseconds -- three of the
 * numbers the panel shows get bit 7 set. That bit is what tells the display
 * the value has been refreshed rather than left over. */
void display_refresh_tick(void)
{
    u8 v;

    if (REG16(0x114DDEUL) < 0x03E8) return;

    REG16(0x114DDEUL) = 0;
    REG8(0xFFFEE5UL) = (u8)(REG8(0xFFFEE5UL) + 0x80);
    REG8(0xFFFEE8UL) = (u8)(REG8(0xFFFEE8UL) + 0x80);
    REG8(0xFFFEEDUL) = (u8)(REG8(0xFFFEEDUL) + 0x80);

    v = REG8(0x114DC7UL);
    if (v & 0x40) REG8(0x114DC7UL) = (u8)(v & ~0x40);
}

/* H'204C86. The two knob values out to the panel. Bit 7 of H'FFFEE8 is the
 * refresh bit above and is kept; a value over H'64 goes across without its
 * companion. */
void display_params_publish(void)
{
    u8 v;

    sew_display_scale(REG8(0x11A69EUL));

    v = REG8(0x11A6A0UL);
    if (v > 0x64) {
        REG8(0xFFFEE7UL) = 0x64;
    } else {
        REG8(0xFFFEE7UL) = v;
        REG8(0xFFFEE8UL) = (u8)((u8)(REG8(0xFFFEE8UL) & 0x80) |
                                REG8(0x11A6A1UL));
    }
}

/* H'207A58. The widest the needle may swing, which depends on which presser
 * foot is fitted -- H'11A7E2 is the foot, one to eight -- and on which data
 * set the machine is running. Three tables, one per set, and a foot outside
 * the range means no limit beyond the pattern's own.
 *
 * The argument says whether this is a fresh selection. On a fresh one the
 * width is left alone; otherwise a foot that has just appeared pulls the
 * width back to what the pattern asks for, which is what stops a wide stitch
 * being sewn into a narrow foot.
 */
static const u8 sew_width_alt[8]  = { 0x4D, 0x4B, 0x3F, 0x33,
                                      0x26, 0x1A, 0x0E, 0x00 };
static const u8 sew_width_setA[8] = { 0x3A, 0x3C, 0x28, 0x14,
                                      0x00, 0x00, 0x00, 0x00 };
static const u8 sew_width_setB[8] = { 0x23, 0x24, 0x18, 0x0C,
                                      0x00, 0x00, 0x00, 0x00 };

void sew_width_limit_set(u8 fresh)
{
    u8 foot = REG8(0x11A7E2UL);
    u8 idx  = (u8)(foot + 0xFF);
    u8 v;

    REG8(0x11A670UL) = REG8(0x11A7C0UL);
    REG8(0x11A671UL) = REG8(0x11A7C2UL);

    if (foot != 0) {
        REG8(0x114DCFUL) |= 0x01;
        if (fresh == 0) REG8(0xFFFEEAUL) = 0x05;
        REG8(0x11A7D8UL) = 0x05;
    }

    if (idx <= 0x07) {
        if (REG8(0xFFFEF8UL) & 0x10)   REG8(0x11A670UL) = sew_width_alt[idx];
        else if (STITCH_SET == 0xAA)   REG8(0x11A670UL) = sew_width_setA[idx];
        else                           REG8(0x11A670UL) = sew_width_setB[idx];
        return;
    }

    REG8(0x11A670UL) = REG8(0x11A7E3UL);

    v = REG8(0x114DCFUL);
    if (!(v & 0x01)) return;
    REG8(0x114DCFUL) = (u8)(v & ~0x01);
    if (fresh != 0) return;

    REG8(0x11A7D9UL) = REG8(0x11A7BFUL);
    REG8(0x11A69EUL) = REG8(0x11A7BFUL);
    sew_param_a_set(REG8(0x11A7BFUL));
    REG8(0x11A7D8UL) = (u8)(REG8(0x11A7C3UL) & 0x0F);
    REG8(0xFFFEEAUL) = (u8)(REG8(0x11A7C3UL) & 0x0F);
}

/* H'203EBC. Steps the variant on by itself.
 *
 * On a pattern with more than one variant the machine can be left to run
 * through them, and what decides when to move on is either the free-running
 * counter at H'FFFF00 against the four limits, or -- when H'11A7D7 bit 0
 * says the pattern is timed rather than measured -- a count of stitches.
 * H'11A7AD is which of the four limits is being watched.
 *
 * The window is worked out from two bytes on the bus scaled by H'64/H'0F,
 * which is how far the machine travels in the time it takes to decide.
 * H'11A669 is set three steps ahead of a change so the display has warning.
 */
void variant_advance(void)
{
    u16 now  = REG16(0xFFFF00UL);
    u16 sum  = (u16)((u16)bus_byte_10() + (u16)bus_byte_11());
    u16 span = (u16)((u32)((u32)0x0064 * (u32)sum) / 0x000FUL);
    u8  st;

    if (REG8(0xFFFEFDUL) == 0x02) return;

    if (!(REG8(0x114DC6UL) & 0x08) && !(REG8(0x114DCBUL) & 0x20)) {
        if ((u16)(REG16(0x11A7AEUL) + span) > now) return;
        if (!(REG8(0x11A7BDUL) & 0x02)) return;
    }

    if (REG8(0x11A7D7UL) & 0x01) {
        if (REG8(0x114DCBUL) & 0x20) {
            REG8(0x114DCBUL) &= (u8)~0x20;
            REG8(0x11A7A8UL) = (u8)(REG8(0x11A7A8UL) + 1);
            if (REG8(0x11A7A8UL) >= REG8(0x11A7AAUL)) REG8(0x11A7A8UL) = 0;

            if (REG16(0x11A698UL) >= 0x0002) {
                if (REG8(0x11A7ADUL) == 0x01) {
                    REG16(0x11A69AUL) = REG16(0x11A698UL);
                    REG16(0x11A69CUL) = REG16(0x11A698UL);
                    REG8(0x11A7ADUL) = 0;
                    if (REG8(0x11A7ABUL) == 0x01) {
                        REG8(0x114DC6UL) |= 0x08;
                        REG8(0xFFFEFAUL) |= 0x80;
                        REG8(0xFFFEF7UL) &= (u8)~0x08;
                    }
                } else {
                    REG16(0x11A69CUL) = REG16(0x11A698UL);
                    REG8(0x11A7ADUL) = 0x01;
                    REG8(0x114DC6UL) |= 0x08;
                    REG8(0xFFFEFAUL) |= 0x80;
                    REG8(0xFFFEF7UL) &= (u8)~0x08;
                }
                REG16(0x11A698UL) = 0;
            }
            return;
        }

        {
            u16 edge = (REG8(0x11A7ADUL) == 0) ? REG16(0x11A69CUL)
                                               : REG16(0x11A69AUL);
            if ((u16)(REG16(0x11A698UL) + 1) >= edge) {
                REG8(0x11A7ADUL) = (u8)((REG8(0x11A7ADUL) == 0) ? 0x01 : 0x00);
                variant_step();
                REG16(0x11A698UL) = 0;
            } else if ((u16)(REG16(0x11A698UL) + 3) >= edge) {
                REG8(0x11A669UL) = 0x01;
            } else {
                REG8(0x11A669UL) = 0x00;
            }
        }
        return;
    }

    if (!(REG8(0x114DC6UL) & 0x08)) {
        REG8(0x114DCBUL) &= (u8)~0x20;
        REG8(0x11A7A8UL) = (u8)(REG8(0x11A7A8UL) + 1);
        if (REG8(0x11A7A8UL) >= REG8(0x11A7AAUL)) REG8(0x11A7A8UL) = 0;

        if ((u16)(REG16(0x11A7AEUL) + 0x000A) <= now) {
            REG8(0x114DC6UL) |= 0x08;
            REG8(0xFFFEFAUL) |= 0x80;
            REG8(0xFFFEF7UL) &= (u8)~0x08;
            REG16(0x11A7B4UL) = (u16)(now - 1);
            REG16(0x11A7B0UL) = (u16)(now - 1);
            REG8(0x11A7ACUL) = (u8)((u8)(now - 1) - REG8(0x11A7AFUL));
            REG8(0x11A7ADUL) = 0x02;
        }
        return;
    }

    st = REG8(0x11A7ADUL);
    if (st == 0x00) {
        if (now <= REG16(0x11A7AEUL)) {
            REG8(0x11A7ADUL) = (u8)(st + 1);
            variant_step();
        } else if ((u16)(now + 0xFFFD) <= REG16(0x11A7AEUL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else if (st == 0x01) {
        if (now >= REG16(0x11A7B0UL)) {
            REG8(0x11A7ADUL) = (u8)(st + 1);
            variant_step();
        } else if ((u16)(now + 0x0003) <= REG16(0x11A7B0UL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else if (st == 0x02) {
        if (now <= REG16(0x11A7B2UL)) {
            REG8(0x11A7ADUL) = (u8)(st + 1);
            variant_step();
        } else if ((u16)(now + 0xFFFD) <= REG16(0x11A7B2UL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else if (st == 0x03) {
        if (now >= REG16(0x11A7B4UL)) {
            REG8(0x11A7ADUL) = 0;
            variant_step();
        } else if ((u16)(now + 0x0003) <= REG16(0x11A7B4UL)) {
            REG8(0x11A669UL) = 0x01;
        } else {
            REG8(0x11A669UL) = 0x00;
        }
    } else {
        REG8(0x11A7ADUL) = 0;
    }
}

/* ---- the main motor's state machine -------------------------------------
 * H'FFFEC6 is what the motor is doing: 0 stopped, 1 running, 2 slowing, 3
 * hunting for the needle-up position, 4 braking, 5 the winder, 6 the
 * thread cutter. H'11A850 is how long it has been in that state, counted by
 * the timer handler, and H'11A84E the speed it is being asked for.
 *
 * ITU channel 3 is the position sensor's input capture; channel 4 is the
 * PWM. Both are stopped while their registers are changed, which is why
 * TSTR bit 4 goes down and up around every write to GRA4.
 */

/* H'20D904, H'20D90E. The PWM's compare-match A interrupt, which is what
 * drives the winder, on and off with the mask up. */
void pwm4_matcha_on(void)
{
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    TIER4 |= 0x01;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

void pwm4_matcha_off(void)
{
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    TIER4 &= (u8)~0x01;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

/* H'20D918. ITU channel 3 set up to capture the position sensor. */
void position_capture_init(void)
{
    u8 v;

    TSNC &= (u8)~0x08;
    TMDR &= (u8)~0x08;
    TOER = (u8)(TOER & 0xF6);
    REG8(0xFFFF82UL) = 0x28;          /* TCR3: count at the clock */

    v = (u8)(PBDDR_SHADOW & ~0x01);
    PBDDR = v;
    PBDDR_SHADOW = v;
    PBDR &= (u8)~0x01;

    REG8(0xFFFF83UL) = 0x55;          /* TIOR3: capture on both edges */
    REG16(0xFFFF88UL) = 0xFFFF;       /* GRA3 */
    REG16(0xFFFF8AUL) = 0xFFFF;       /* GRB3 */
    REG8(0xFFFF84UL) = 0x00;          /* TIER3 */
    TSTR |= 0x08;
}

/* H'20D95C, H'20D98C. The capture interrupts on and off. */
void position_capture_on(void)
{
    position_capture_init();
    REG8(0x11A852UL) |= 0x10;
    REG16(0xFFFECEUL) = 0;
    REG8(0x11A84FUL) = 0;
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    REG8(0xFFFF84UL) |= 0x01;
    REG8(0xFFFF84UL) |= 0x04;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

void position_capture_off(void)
{
    __asm__ volatile("orc #0xc0,ccr" ::: "cc", "memory");
    REG8(0xFFFF84UL) &= (u8)~0x01;
    REG8(0xFFFF84UL) &= (u8)~0x04;
    __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
}

/* H'20DB1A. The brake, off and straight back on -- a pulse on P6 bit 2. */
void motor_brake_pulse(void)
{
    P6DR &= (u8)~0x04;
    P6DR |=        0x04;
}

/* H'20DA2E. The speed out to the PWM. Below H'0A the mark is parked past
 * the period, which is off; above it the mark is worked back from the
 * period, and there are two scales because the period itself differs
 * between the sewing motor and the winder. */
void main_motor_speed_set(u8 speed)
{
    TSTR &= (u8)~0x10;

    if (speed < 0x0A) {
        GRA4 = 0x2B98;
    } else if (GRB4 == 0x2B34) {
        GRA4 = (u16)(0x2B34 - (u16)((u16)((u16)speed - 1) * (u16)0x002B));
    } else {
        GRA4 = (u16)(0x0E67 - (u16)((u16)((u16)speed - 1) * (u16)0x000E));
    }

    TSTR |= 0x10;
}

/* H'20DB24. One pass of the state machine. */
void main_motor_service(void)
{
    u8 mode = REG8(0xFFFEC0UL);
    u8 st, v;

    main_motor_speed_set(REG8(0x11A84EUL));

    st = REG8(0xFFFEC6UL);

    if (st == 0x00) {
        P4DR |= 0x01;
        REG8(0x11A84EUL) = 0;

        if (REG8(0xFFFEC7UL) & 0x80) {
            if (REG8(0xFFFEC3UL) != 0) {
                REG8(0xFFFEC6UL) = 0x06;
                motor_brake_pulse();
                P4DR &= (u8)~0x01;
                position_capture_on();
                REG16(0x11A850UL) = 0;
                return;
            }
        } else if (REG8(0xFFFEC7UL) & 0x01) {
            if (!((REG8(0xFFFEC4UL) & 0x01) &&
                  REG8(0x114D62UL) < 0x0D)) {
                GRA4 = 0x2B98;
                P4DR |= 0x01;
                TSTR &= (u8)~0x10;
                TMDR &= (u8)~0x10;
                TSTR |=        0x10;
                REG8(0xFFFEC6UL) = 0x05;
            }
        } else if (REG8(0xFFFEC3UL) == 0x01) {
            REG8(0xFFFEC6UL) = 0x03;
            P4DR &= (u8)~0x01;
            motor_brake_pulse();
            REG8(0x11A852UL) |= 0x04;
            REG16(0x11A850UL) = 0;
        } else if (REG8(0xFFFEC3UL) >= 0x03 && REG8(0xFFFEC3UL) <= 0x05) {
            REG8(0xFFFEC6UL) = 0x01;
            P4DR &= (u8)~0x01;
            motor_brake_pulse();
        }
        REG16(0x11A850UL) = 0;
        return;
    }

    if (st == 0x01) {
        REG8(0x11A84EUL) = REG8(0xFFFEC8UL);
        if (REG8(0xFFFEC7UL) & 0x40) {
            REG8(0xFFFEC6UL) = 0x02;
            P4DR |= 0x01;
        } else if (REG8(0xFFFEC3UL) < 0x03) {
            REG8(0xFFFEC6UL) = 0x02;
            P4DR |= 0x01;
        } else if (adc_get_result(3) >= 0x32) {
            /* still turning: nothing to do */
        } else {
            if (REG16(0x11A850UL) > 0x1388) {
                REG8(0xFFFEC6UL) = 0x04;
                P4DR |= 0x01;
                motor_brake_pulse();
            }
            return;
        }
        REG16(0x11A850UL) = 0;
        return;
    }

    if (st == 0x02) {
        REG8(0x11A84EUL) = REG8(0xFFFECAUL);
        if (adc_get_result(5) < 0x16) {
            REG8(0xFFFEC6UL) = 0x03;
            P4DR &= (u8)~0x01;
            REG16(0x11A850UL) = 0;
        } else if (REG16(0x11A850UL) > 0x03E8) {
            REG8(0xFFFEC6UL) = 0x03;
            P4DR &= (u8)~0x01;
            REG16(0x11A850UL) = 0;
        } else if (REG8(0xFFFEC3UL) == 0x03 && !(REG8(0xFFFEC7UL) & 0x40)) {
            REG8(0xFFFEC6UL) = 0x01;
            P4DR &= (u8)~0x01;
        }
        return;
    }

    if (st == 0x03) {
        REG8(0x11A84EUL) = REG8(0xFFFECAUL);

        if (REG16(0x11A850UL) > 0x0028) {
            if ((REG8(0xFFFEC7UL) & 0x02) || (REG8(0x11A852UL) & 0x04)) {
                v = REG8(0x11A852UL);
                if (v & 0x01) {
                    if (mode == 0x04) {
                        REG8(0x11A852UL) = (u8)(v & ~0x07);
                        P4DR |= 0x01;
                        motor_brake_pulse();
                        REG8(0xFFFEC6UL) = 0x04;
                    }
                } else if (mode == 0x06) {
                    REG8(0x11A852UL) = (u8)(v | 0x01);
                }
            }
            if ((REG8(0xFFFEC7UL) & 0x04) || (REG8(0x11A852UL) & 0x04)) {
                v = REG8(0x11A852UL);
                if (v & 0x02) {
                    if (mode == 0x02) {
                        REG8(0x11A852UL) = (u8)(v & ~0x07);
                        P4DR |= 0x01;
                        motor_brake_pulse();
                        REG8(0xFFFEC6UL) = 0x04;
                    }
                } else if (mode == 0x01) {
                    REG8(0x11A852UL) = (u8)(v | 0x02);
                }
            }
        }

        if (REG16(0x11A850UL) > 0x03E8) {
            P4DR |= 0x01;
            motor_brake_pulse();
            REG8(0xFFFEC6UL) = 0x04;
            REG8(0x11A852UL) &= (u8)~0x04;
        }
        if (REG8(0xFFFEC3UL) > 0x02 && !(REG8(0xFFFEC7UL) & 0x40)) {
            REG8(0xFFFEC6UL) = 0x01;
            P4DR &= (u8)~0x01;
            REG8(0x11A852UL) &= (u8)~0x04;
        }
        return;
    }

    if (st == 0x04) {
        P4DR &= (u8)~0x01;
        REG16(0x11A850UL) = 0;
        REG8(0x11A84EUL) = 0;
        if (REG8(0xFFFEC3UL) == 0) REG8(0xFFFEC6UL) = 0x00;
        return;
    }

    if (st == 0x05) {
        v = REG8(0xFFFEC8UL);
        if (v > 0x0A && !(REG8(0x11A852UL) & 0x20)) {
            TSTR &= (u8)~0x10;
            pwm4_matcha_on();
            TSTR |= 0x10;
            REG8(0x11A852UL) |= 0x20;
        }
        if (REG8(0xFFFEC8UL) <= 0x0A) {
            TSTR &= (u8)~0x10;
            pwm4_matcha_off();
            TSTR |= 0x10;
            REG8(0x11A852UL) &= (u8)~0x20;
        }
        v = REG8(0xFFFEC8UL);
        REG8(0x11A84EUL) = (v > 0xDC) ? 0xDC : v;
        REG16(0x11A850UL) = 0;

        if (!(REG8(0xFFFEC7UL) & 0x01)) {
            if (!((REG8(0xFFFEC4UL) & 0x01) && REG8(0x114D62UL) < 0x0D)) {
                REG8(0x11A852UL) &= (u8)~0x20;
                GRA4 = 0x2B98;
                TSTR &= (u8)~0x10;
                pwm4_matcha_off();
                PBDR &= (u8)~0x08;
                TMDR |= 0x10;
                TSTR |= 0x10;
                REG8(0xFFFEC6UL) = 0x04;
            }
        }
        return;
    }

    if (st == 0x06) {
        REG8(0x11A84EUL) = REG8(0xFFFEC8UL);
        REG8(0xFFFECDUL) = (u8)((u32)((u32)(u16)(0x012C -
                                (u16)adc_get_result(3)) *
                                (u32)REG16(0xFFFECEUL)) / 0x03F0UL);
        if (!(REG8(0xFFFEC7UL) & 0x80)) {
            REG8(0xFFFEC6UL) = 0x04;
            P4DR |= 0x01;
            motor_brake_pulse();
            position_capture_off();
        } else {
            REG16(0x11A850UL) = 0;
        }
    }
}

/* Puts a message up. The front end of the display subsystem, and its own
 * part -- see the README. */

/* H'208210. The whole of "make the machine agree with what is selected",
 * called once a pass from the main loop and once from the bring-up.
 *
 * The first half decides whether the selection has moved far enough to be
 * worth rebuilding for, and calls pattern_make_current if it has. H'11A66A
 * is raised when the rebuild is under way, and everything downstream that
 * would use half-built state looks at it.
 *
 * The second half moves the parameters between the live set and the
 * pattern's own, in whichever direction H'11A175 says, and then runs the
 * sewing pass.
 */
void stitch_state_init(void)
{
    u16 sel;
    u16 keyed;
    u8  v;

    sew_running_flags();
    sew_params_scale_for_mode();
    sew_mode_arbitrate();
    REG8(0x11A66AUL) = 0;

    if ((REG8(0x114DCAUL) & 0x02) && QUEUE_POS != 0xFFFF) {
        int fresh = 1;

        sel = (u16)(queue_entry_ref(QUEUE_POS) & 0x03FF);

        if (QUEUE_POS != QUEUE_GROUP) {
            if ((queue_entry_ref(QUEUE_POS) & 0x03FF) != QUEUE_END &&
                QUEUE_POS == REG16(0x11A7FCUL)) {
                fresh = 0;
            }
        }
        if (fresh) {
            REG16(0x11A7FCUL) = QUEUE_POS;
            queue_group_start();
            REG8(0x11A66AUL) = 0x01;
        }

        if (QUEUE_LAST == QUEUE_FIRST) REG8(0x11A66AUL) = 0x01;

        REG16(0x11A7FEUL) = queue_entry_ref(0);

        if ((short)REG16(0x11A7FEUL) > (short)REG16(0x11A66CUL)) {
            if (QUEUE_POS != QUEUE_FIRST) {
                if (sel == 0x03FE || sel == 0x03FF) {
                    REG16(0x11A6C4UL) = sel;
                    REG16(0x11A7E6UL) = sel;
                } else {
                    keyed = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
                    REG16(0x11A7E6UL) = keyed;
                    REG16(0x11A6C4UL) = keyed;
                }
                REG8(0x114DCAUL) |= 0x80;
                REG8(0x114DCDUL) &= (u8)~0x08;
                REG8(0x114DC9UL) |= 0x01;
                REG8(0x114DC9UL) |= 0x04;
                pattern_make_current();
            }
        } else {
            keyed = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
            if (keyed != REG16(0x11A800UL)) {
                REG16(0x11A7E6UL) = keyed;
                REG16(0x11A6C4UL) = keyed;
                REG8(0x11A6AEUL) |= 0x01;
                REG8(0x114DCDUL) &= (u8)~0x08;
                pattern_make_current();
            }
        }

        REG16(0x11A66CUL) = REG16(0x11A7FEUL);
    }

    if ((REG8(0x114DCAUL) & 0x02) && QUEUE_POS != 0xFFFF) {
        if (REG8(0x114DC9UL) & 0x04) {
            if (REG8(0x11A175UL) == 0x01) {
                v = REG8(0x114DC7UL);
                if (!(v & 0x01)) {
                    REG8(0x114DC7UL) = (u8)(v | 0x01);
                    sew_params_save();
                    queue_entry_unpack();
                    sew_params_from_pattern();
                }
                sew_params_clamp(REG8(0x11A7E3UL), REG8(0x11A7E4UL));
            } else {
                v = REG8(0x114DC7UL);
                if (v & 0x01) {
                    REG8(0x114DC7UL) = (u8)(v & ~0x01);
                    sew_params_restore();
                } else {
                    sew_params_capture();
                    queue_entry_unpack();
                }
                sew_width_limit_set(0);
                sew_limits_apply();
            }
        } else {
            sew_flag_copy_6();
        }
    } else {
        sew_params_capture();
        sew_width_limit_set(0);
        sew_params_clamp(REG8(0x11A670UL), REG8(0x11A671UL));
        sew_limits_apply();
    }

    if (REG8(0xFFFEC4UL) & 0x01) {
        sew_mechanism_service();
    } else {
        if (!(REG8(0xFFFEC4UL) & 0x80)) {
            REG8(0x11A679UL) = 0;
            REG8(0x11A678UL) = 0;

            if (REG8(0xFFFEF7UL) & 0x02) {
                if (REG8(0x114DC6UL) & 0x80) {
                    REG8(0xFFFEC7UL) |= 0x40;
                } else {
                    message_show(0x0009);
                    REG8(0xFFFEF7UL) &= (u8)~0x02;
                }
            }
            if (REG8(0xFFFEF7UL) & 0x04) {
                if (!(REG8(0x114DC6UL) & 0x80)) {
                    message_show(0x001C);
                    REG8(0xFFFEF7UL) &= (u8)~0x04;
                }
            }
        }
        sew_pass();
    }

    display_refresh_tick();
    sew_counters_service();

    if (REG8(0xFFFEC4UL) & 0x01) {
        sew_params_writeback(0x03FD);
    } else {
        keyed = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
        if ((u8)keyed != 0 && REG16(0x11A7E6UL) != 0) {
            if (REG8(0x114DCAUL) & 0x02) {
                if (QUEUE_POS != 0xFFFF) {
                    if (!(REG8(0x114DC7UL) & 0x01) &&
                        (REG8(0x114DC9UL) & 0x04)) {
                        if (REG8(stitch_record(REG16(0xFFFEE0UL)) + 0x17)
                            != 0x04) {
                            keyed = (u16)(REG16(0xFFFEE0UL) +
                                          (u16)REG8(0xFFFEFDUL));
                            if (!(keyed >= 0x0016 && keyed <= 0x0019)) {
                                sew_params_writeback(
                                    (u16)(REG16(0xFFFEE0UL) +
                                          (u16)REG8(0xFFFEFDUL)));
                            }
                        }
                    }
                }
            } else {
                sew_params_writeback((u16)(REG16(0xFFFEE0UL) +
                                           (u16)REG8(0xFFFEFDUL)));
            }
        }
    }

    display_params_publish();
    REG16(0x11A800UL) = (u16)(REG16(0xFFFEE0UL) + (u16)REG8(0xFFFEFDUL));
}

/* ---- the display subsystem ----------------------------------------------
 * The screen is 320 by 240 at two bits a pixel -- four pixels to a byte, 80
 * bytes to a line, H'4B00 bytes to a buffer -- and there are three buffers.
 * plot_pixel (H'20E154) is the bottom of it; everything here is built on
 * that and on the byte fills.
 */

/* H'208698. One turn of the machine while the display is busy. This is what
 * service_hook reaches, which is why a fill of 19K does not stop the motors.
 */
static void service_tick(void)
{
    REG16(0x114DE2UL) = 0;
    analog_scan();
    stitch_state_init();
    pedal_service();
    main_motor_service();
}

/* H'20E0D8, H'20E0FE. Two big-endian words out of a four-byte header: the
 * first pair and the second. Bitmaps in this ROM carry their width and
 * height that way. */
u16 header_word_0(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | (u16)p[1]);
}

u16 header_word_1(const u8 *p)
{
    return (u16)(((u16)p[2] << 8) | (u16)p[3]);
}

/* H'20E48E. Where a pixel lives: the byte that holds it and which of the
 * four pairs in that byte it is. */
void pixel_address(u16 x, u16 y, u32 base, u32 *addr, u8 *shift)
{
    /* MULXS.W: a signed 16 x 16 into 32, not a 16-bit product widened
     * afterwards. The difference only shows from y = 102 down, where
     * H'140 * y passes H'8000 -- which is most of the screen. */
    long t = (long)(short)0x0140 * (long)(short)y + (long)(short)x;

    *addr  = base + ((u32)t >> 2);
    *shift = (u8)((u32)t & 0x03);
}

/* H'20E562. A vertical line, drawn a pixel at a time. The ends are put in
 * order first, so it does not matter which way round they come. */
void draw_vline(u16 x, u16 y0, u16 y1, u32 buffer, u8 colour)
{
    short a = (short)y0;
    short b = (short)y1;
    short i;

    if (a > b) { short t = a; a = b; b = t; }

    for (i = a; i <= b; i++) plot_pixel(x, (u16)i, buffer, colour);
}

/* H'210428. A copy with the service hook between bytes, so a long one does
 * not starve the machine. Hands back the destination. */
u8 *copy_forward(u8 *dst, const u8 *src, u32 n)
{
    u8 *out = dst;

    while (n-- != 0) {
        service_hook();
        *dst++ = *src++;
    }
    return out;
}

/* H'210470. The same, but safe when the two overlap: a source below the
 * destination that reaches into it is copied backwards. */
u8 *copy_overlapped(u8 *dst, const u8 *src, u32 n)
{
    u8 *out = dst;

    if ((u32)src <= (u32)dst && (u32)((u32)src + n) >= (u32)dst) {
        dst += n;
        src += n;
        while (n-- != 0) {
            service_hook();
            *--dst = *--src;
        }
        return out;
    }

    while (n-- != 0) {
        service_hook();
        *dst++ = *src++;
    }
    return out;
}

/* H'246D8C, and H'246D7E just above it, which sets the same byte. */
void message_state_clear(void)
{
    REG8(0x114DA4UL) = 0;
}

void message_state_set(void)
{
    REG8(0x114DA4UL) = 0x01;
}

/* H'21F40E. Remembers which screen is showing, in one of four slots, and
 * says a screen change has happened. */
void screen_remember(u8 slot)
{
    if (slot == 0x01)      REG8(0x11A16AUL) = REG8(0x11A169UL);
    else if (slot == 0x02) REG8(0x11A16BUL) = REG8(0x11A169UL);
    else if (slot == 0x03) REG8(0x11A16CUL) = REG8(0x11A169UL);
    else if (slot == 0x04) REG8(0x11A16DUL) = REG8(0x11A169UL);
    else return;

    REG8(0x11A172UL) = 0x01;
    REG8(0x11B100UL) = slot;
}

/* H'20E310. One pixel read back, the mirror of plot_pixel and dispatched
 * through the same kind of table for the same reason. */
u8 read_pixel(u16 x, u16 y, u32 buffer)
{
    volatile u8 *p;
    u8 shift;

    service_hook();

    p = (volatile u8 *)(buffer + (u32)y * LCD_BYTES_PER_LINE +
                        ((u32)(x >> 3) << 1) + ((x & 7) >> 2));
    shift = (u8)(6 - 2 * (x & 3));

    return (u8)((*p >> shift) & 0x03);
}

/* H'20E5B6. A horizontal line, and the only primitive that is not a loop
 * over plot_pixel: the whole bytes in the middle are filled at once and only
 * the ragged ends are done a pixel at a time.
 *
 * The fill byte is worked out from the colour, and colour 1 has no case --
 * the original leaves the byte holding whatever the caller's register did.
 * Reproduced by leaving it at zero, which is what a fresh local gives; a
 * line of colour 1 long enough to reach the byte fill is the one thing here
 * that cannot be matched, and it is a fault in the original rather than a
 * gap in this.
 */
void draw_hline(u16 x0, u16 x1, u16 y, u32 buffer, u8 colour)
{
    u32 addr0, addr1;
    u8  sh0, sh1;
    u8  fill = 0;
    u16 a, b;
    short i;
    u32 n;

    a = x0;
    b = x1;
    if ((short)a > (short)b) { u16 t = a; a = b; b = t; }

    pixel_address(a, y, buffer, &addr0, &sh0);
    pixel_address(b, y, buffer, &addr1, &sh1);

    if (colour == 0x00)      fill = 0x00;
    else if (colour == 0x02) fill = 0xAA;
    else if (colour == 0x03) fill = 0xFF;

    if (addr0 == addr1) {
        if ((short)((short)(u16)sh1 - (short)(u16)sh0) == 0x0003) {
            *(volatile u8 *)addr0 = fill;
        } else {
            for (i = (short)(u16)sh0; (short)(u16)sh1 >= i; i++) {
                plot_pixel(a, y, buffer, colour);
                a++;
            }
        }
        return;
    }

    if (sh0 == 0) {
        *(volatile u8 *)addr0 = fill;
    } else {
        for (i = (short)(u16)sh0; i <= 0x0003; i++) {
            plot_pixel(a, y, buffer, colour);
            a++;
        }
    }

    n = addr1 - addr0 - 1;
    if (n != 0) mem_fill(addr0 + 1, fill, n);

    if (sh1 == 0x03) {
        *(volatile u8 *)addr1 = fill;
    } else {
        for (i = 0; (short)(u16)sh1 >= i; i++) {
            plot_pixel(b, y, buffer, colour);
            b--;
        }
    }
}

/* H'20E764. Bresenham, for the lines that are neither. */
void draw_line_bresenham(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer,
                         u8 colour)
{
    short x = (short)x0, y = (short)y0;
    short dx = (short)((short)x1 - (short)x0);
    short dy = (short)((short)y1 - (short)y0);
    signed char sx, sy, mx, my;
    short e, inc, i;

    if (dx < 0) { dx = (short)-dx; sx = -1; } else sx = 1;
    if (dy < 0) { dy = (short)-dy; sy = -1; } else sy = 1;

    if (dx < dy) {
        short t = dx; dx = dy; dy = t;
        mx = 0; my = sy;
    } else {
        mx = sx; my = 0;
    }

    inc = (short)(dy << 1);
    e   = (short)(inc - dx);

    for (i = 0; i <= dx; i++) {
        plot_pixel((u16)x, (u16)y, buffer, colour);
        if (e < 0) {
            x = (short)(x + mx);
            y = (short)(y + my);
            e = (short)(e + inc);
        } else {
            x = (short)(x + sx);
            y = (short)(y + sy);
            e = (short)(e + (short)(inc - (short)(dx << 1)));
        }
    }
}

/* H'20E4E4. A line, sent to whichever of the three is right for it. */
void draw_line(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer, u8 colour)
{
    if (x0 == x1)      draw_vline(x0, y0, y1, buffer, colour);
    else if (y0 == y1) draw_hline(x0, x1, y0, buffer, colour);
    else               draw_line_bresenham(x0, y0, x1, y1, buffer, colour);
}

/* H'216F3C. A number as decimal digits, built backwards and then reversed
 * in place. Always writes at least one digit, and terminates. */
void int_to_decimal(short v, char *out)
{
    char *head = out;
    char *tail = out;

    do {
        *tail++ = (char)((u8)(short)(v % 10) + 0x30);
        v = (short)(v / 10);
    } while (v != 0);

    *tail = 0;
    tail--;

    do {
        char t = *tail;
        *tail = *head;
        *head = t;
        head++;
        tail--;
    } while (head < tail);
}

/* H'24ADC8. */
short abs_short(short v)
{
    return (v >= 0) ? v : (short)-v;
}

/* ---- bitmaps -------------------------------------------------------------
 * A bitmap in this ROM is run-length coded: four header bytes and then a
 * stream in which each byte is a colour in its low two bits and a run length
 * in the other six. Four pixels to an output byte, so a run that lands on a
 * byte boundary can be written whole and only the ragged ends of each row
 * need doing a pixel at a time.
 */

/* The decoder's state, which every loop below advances the same way. */
typedef struct {
    const u8 *src;
    u16       pos;
    u8        run;
    u8        colour;
} rle_state;

static void rle_start(rle_state *r, const u8 *src)
{
    r->src = src;
    r->pos = 4;
    r->run    = (u8)(src[4] >> 2);
    r->colour = (u8)(src[4] & 0x03);
}

static void rle_step(rle_state *r)
{
    r->run = (u8)(r->run - 1);
    if (r->run == 0) {
        r->pos++;
        r->run    = (u8)(r->src[r->pos] >> 2);
        r->colour = (u8)(r->src[r->pos] & 0x03);
    }
}

/* H'20F192. Draws a bitmap into a buffer.
 *
 * A full-width bitmap -- H'013F, the whole screen less one -- is a straight
 * run of bytes and takes the fast path. Anything narrower is done a row at a
 * time: the pixels before the first whole byte, then the whole bytes, then
 * the pixels after the last one. H'11A6xx is not involved; this is pure
 * geometry.
 */
void bitmap_draw(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src, u32 dst)
{
    u16 lead = 0, trail = 0, mid;
    u32 rowstart, rowend;
    u8  sh0, sh1;
    u8  acc = 0;
    rle_state r;
    short x, y, i;
    u32 p;

    pixel_address(x0, y0, dst, &rowstart, &sh0);
    rle_start(&r, src);

    if ((u16)((u16)x1 - (u16)x0) == 0x013F) {
        pixel_address(x1, y1, dst, &rowend, &sh1);
        for (p = rowstart; p <= rowend; p++) {
            service_hook();
            for (i = 0; i < 4; i++) {
                acc = (u8)((u8)(acc << 2) | r.colour);
                rle_step(&r);
            }
            *(volatile u8 *)p = acc;
        }
        return;
    }

    pixel_address(x1, y0, dst, &rowend, &sh1);
    mid = (u16)((u16)rowend - (u16)rowstart);

    if (sh0 == 0 && sh1 == 0x03) {
        mid = (u16)(mid + 1);
    } else if (sh0 != 0 && sh1 != 0x03) {
        if (mid == 0) {
            lead = (u16)((u16)sh1 - (u16)sh0 + 1);
        } else {
            mid = (u16)(mid - 1);
            lead  = (u16)(4 - (u16)sh0);
            trail = (u16)((u16)sh1 + 1);
        }
    } else if (sh0 == 0) {
        trail = (u16)((u16)sh1 + 1);
    } else {
        lead = (u16)(4 - (u16)sh0);
    }

    if (mid != 0) {
        if (lead  != 0) rowstart += 1;
        if (trail != 0) rowend   -= 1;
    }

    for (y = (short)y0; y <= (short)y1; y++) {
        for (x = (short)x0; (short)((u16)x0 + lead) > x; x++) {
            plot_pixel((u16)x, (u16)y, dst, r.colour);
            rle_step(&r);
        }

        if (mid != 0) {
            for (p = rowstart; p <= rowend; p++) {
                service_hook();
                for (i = 0; i < 4; i++) {
                    acc = (u8)((u8)(acc << 2) | r.colour);
                    rle_step(&r);
                }
                *(volatile u8 *)p = acc;
            }
            rowstart += LCD_BYTES_PER_LINE;
            rowend   += LCD_BYTES_PER_LINE;
        }

        for (x = (short)((u16)x1 - trail + 1); x <= (short)x1; x++) {
            plot_pixel((u16)x, (u16)y, dst, r.colour);
            rle_step(&r);
        }
    }
}

/* H'2102B8. A bitmap drawn mirrored.
 *
 * Mirroring is not done in the decoder: the bitmap is drawn into the third
 * buffer at H'0E8010 the ordinary way and then copied out pixel by pixel
 * with one or both axes reversed. Mode 0 skips the round trip entirely.
 */
void bitmap_draw_mirrored(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src,
                          u32 dst, u8 mode)
{
    short x, y, xd, yd;

    if (mode == 0) {
        bitmap_draw(x0, y0, x1, y1, src, dst);
        return;
    }

    bitmap_draw(x0, y0, x1, y1, src, LCD_SCRATCH);

    if (mode == 0x01) {
        for (y = (short)y0; y <= (short)y1; y++) {
            xd = (short)x1;
            for (x = (short)x0; x <= (short)x1; x++) {
                plot_pixel((u16)xd, (u16)y, dst,
                           read_pixel((u16)x, (u16)y, LCD_SCRATCH));
                xd--;
            }
        }
    } else if (mode == 0x02) {
        yd = (short)y1;
        for (y = (short)y0; y <= (short)y1; y++) {
            for (x = (short)x0; x <= (short)x1; x++) {
                plot_pixel((u16)x, (u16)yd, dst,
                           read_pixel((u16)x, (u16)y, LCD_SCRATCH));
            }
            yd--;
        }
    } else if (mode == 0x03) {
        yd = (short)y1;
        for (y = (short)y0; y <= (short)y1; y++) {
            xd = (short)x1;
            for (x = (short)x0; x <= (short)x1; x++) {
                plot_pixel((u16)xd, (u16)yd, dst,
                           read_pixel((u16)x, (u16)y, LCD_SCRATCH));
                xd--;
            }
            yd--;
        }
    }
}

/* H'20E826. A rectangle, filled or outlined. Filled is a stack of
 * horizontal lines -- the ends are put in order first -- and outlined is the
 * four sides drawn separately. */
void draw_rect(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer, u8 colour,
               u8 filled)
{
    short a, b, y;

    if (filled != 0) {
        a = (short)y0;
        b = (short)y1;
        if (a > b) { short t = a; a = b; b = t; }
        for (y = a; y <= b; y++) draw_hline(x0, x1, (u16)y, buffer, colour);
        return;
    }

    draw_hline(x0, x1, y0, buffer, colour);
    draw_vline(x1, y0, y1, buffer, colour);
    draw_hline(x0, x1, y1, buffer, colour);
    draw_vline(x0, y0, y1, buffer, colour);
}

/* H'20EC12. Copies a rectangle from one buffer to another -- or to a
 * different place in the same one, which is what makes the direction test
 * necessary: rows are taken from the top down when the destination is above
 * the source and from the bottom up when it is below, so an overlapping move
 * does not eat its own tail.
 *
 * The middle whole bytes of each row go through copy_forward and only the
 * ragged ends are done a pixel at a time, the same shape as bitmap_draw.
 * Unlike bitmap_draw, only the leading edge shifts the row pointers: the
 * trailing byte is left in the run.
 */
void region_copy(u16 x0, u16 y0, u16 x1, u16 y1, u16 dst_y, u32 from, u32 to)
{
    u16 lead = 0, trail = 0, mid;
    u32 src_line, dst_line, end_addr;
    u8  sh0, sh1;
    short x, sy, dy;
    u32 off;

    pixel_address(x0, y0,    from, &src_line, &sh0);
    pixel_address(x0, dst_y, to,   &dst_line, &sh1);

    if ((u16)((u16)x1 - (u16)x0) == 0x013F) {
        pixel_address(x1, y1, from, &end_addr, &sh1);
        copy_forward((u8 *)dst_line, (const u8 *)src_line,
                     (u32)(end_addr - src_line + 1));
        return;
    }

    pixel_address(x1, dst_y, to, &end_addr, &sh1);
    mid = (u16)((u16)end_addr - (u16)dst_line);

    if (sh0 == 0 && sh1 == 0x03) {
        mid = (u16)(mid + 1);
    } else if (sh0 != 0 && sh1 != 0x03) {
        if (mid == 0) {
            lead = (u16)((u16)sh1 - (u16)sh0 + 1);
        } else {
            mid = (u16)(mid - 1);
            lead  = (u16)(4 - (u16)sh0);
            trail = (u16)((u16)sh1 + 1);
        }
    } else if (sh0 == 0) {
        trail = (u16)((u16)sh1 + 1);
    } else {
        lead = (u16)(4 - (u16)sh0);
    }

    if (mid != 0 && lead != 0) {
        src_line += 1;
        dst_line += 1;
    }

    if ((short)dst_y <= (short)y0) {
        dy = (short)dst_y;
        for (sy = (short)y0; sy <= (short)y1; sy++) {
            for (x = (short)x0; (short)((u16)x0 + lead) > x; x++) {
                plot_pixel((u16)x, (u16)dy, to,
                           read_pixel((u16)x, (u16)sy, from));
            }
            if (mid != 0) {
                copy_forward((u8 *)dst_line, (const u8 *)src_line, (u32)mid);
                src_line += LCD_BYTES_PER_LINE;
                dst_line += LCD_BYTES_PER_LINE;
            }
            for (x = (short)((u16)x1 - trail + 1); x <= (short)x1; x++) {
                plot_pixel((u16)x, (u16)dy, to,
                           read_pixel((u16)x, (u16)sy, from));
            }
            dy++;
        }
        return;
    }

    dy  = (short)((u16)dst_y + (u16)((u16)y1 - (u16)y0));
    off = (u32)(long)(short)((short)((u16)y1 - (u16)y0) *
                             (short)LCD_BYTES_PER_LINE);
    src_line += off;
    dst_line += off;

    for (sy = (short)y1; sy >= (short)y0; sy--) {
        for (x = (short)x0; (short)((u16)x0 + lead) > x; x++) {
            plot_pixel((u16)x, (u16)dy, to,
                       read_pixel((u16)x, (u16)sy, from));
        }
        if (mid != 0) {
            copy_forward((u8 *)dst_line, (const u8 *)src_line, (u32)mid);
            src_line -= LCD_BYTES_PER_LINE;
            dst_line -= LCD_BYTES_PER_LINE;
        }
        for (x = (short)((u16)x1 - trail + 1); x <= (short)x1; x++) {
            plot_pixel((u16)x, (u16)dy, to,
                       read_pixel((u16)x, (u16)sy, from));
        }
        dy--;
    }
}

/* ---- screens -------------------------------------------------------------
 * H'11A169 is the screen showing now and H'11A168 the one before it.
 * H'11A16A to H'11A16D are four slots a screen can be remembered in, so that
 * "back" means something different depending on how the operator got here.
 */

/* H'21F1DE. What leaving a screen costs. The table says which of three
 * things -- and most of the sixty-eight screens want none of them. Kind 1
 * also puts the pattern number and a word of panel state aside, but only
 * when the pattern has changed since last time or the caller insists. */
static const u8 screen_leave_kind[0x44] = {
    1,1,1,0,0,1,0,0,0,0,1,1,0,0,0,0,
    3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,
    0,2,3,0,0,0,0,0,0,0,0,0,0,0,1,0,
    0,1,1,1,1,3,3,0,0,0,0,0,0,0,0,0,
    0,0,0,1
};

void screen_leave(u8 screen, u8 force)
{
    u8 idx = (u8)(screen + 0xFE);
    u8 kind = (idx <= 0x43) ? screen_leave_kind[idx] : 0;

    if (kind == 1) {
        if (REG16(0xFFFEE0UL) != REG16(0x11A1BAUL) || force != 0) {
            REG8(0x11A168UL)  = REG8(0x11A169UL);
            REG16(0x11B110UL) = REG16(0x11B108UL);
            REG16(0x11A1BAUL) = REG16(0xFFFEE0UL);
        }
    } else if (kind == 2 || kind == 3) {
        REG8(0x11A168UL) = REG8(0x11A169UL);
    }
}

/* H'22248A. Saves the part of the screen a dialog is about to cover, into
 * the second of the four stored screens. H'11A176 is a request left by
 * whatever wants it back; the argument forces it either way. */
void dialog_backdrop_save(u8 force)
{
    if (REG8(0x11A176UL) == 0 && force == 0) return;

    region_copy(0x0030, 0x00A0, 0x00E7, 0x00C0, 0x00A0,
                LCD_FRAME_A, 0x000F1610UL);

    if (force == 0) REG8(0x11A176UL) = 0;
}

/* H'21F09E. Goes to a screen.
 *
 * Nothing happens while the machine is sewing. Going back to the screen a
 * slot already holds is a return rather than a move, and takes the short
 * path. Otherwise the current screen is put in the slot if asked, the one
 * being left gets its leave hook, and the new one becomes current.
 */
void screen_switch(u8 screen, u8 slot, u8 remember)
{
    u8 held = 0;

    if (REG8(0x114DC6UL) & 0x80) return;

    if      (slot == 0x01) held = REG8(0x11A16AUL);
    else if (slot == 0x02) held = REG8(0x11A16BUL);
    else if (slot == 0x03) held = REG8(0x11A16CUL);
    else if (slot == 0x04) held = REG8(0x11A16DUL);

    if (slot >= 0x01 && slot <= 0x04 && held == screen &&
        REG8(0x11B0A8UL) != 0) {
        REG8(0x11A169UL) = screen;
        REG8(0x11B0A8UL) = 0;
        return;
    }

    if (remember != 0) {
        if      (slot == 0x01) REG8(0x11A16AUL) = REG8(0x11A169UL);
        else if (slot == 0x02) REG8(0x11A16BUL) = REG8(0x11A169UL);
        else if (slot == 0x03) REG8(0x11A16CUL) = REG8(0x11A169UL);
        else if (slot == 0x04) REG8(0x11A16DUL) = REG8(0x11A169UL);
    }

    screen_leave(REG8(0x11A169UL), 0);

    REG8(0x11A169UL) = screen;
    REG8(0x11B0A8UL) = 0x01;

    if (REG8(0x11A174UL) != 0) {
        picker_cursor(0x02);
        dialog_backdrop_save(0x00);
    }
}

/* H'216D6C. Puts a message up: goes to the message screen and points
 * H'115D12 at the message's own record, which is what draws it. Held off
 * while a message is already up, while the splash is showing, or while a
 * screen change is still settling. */
void message_show(u16 msg)
{
    u32 table;

    if (REG8(0x11A179UL) == 0) return;
    if (REG8(0x11A173UL) != 0) return;
    if (REG8(0x11A171UL) != 0) return;
    if (REG8(0x11B0A8UL) != 0) return;

    REG8(0x11A179UL) = 0;
    message_state_clear();
    screen_remember(0x04);
    screen_switch(0x3E, 0x04, 0x00);

    table = REG32(0x11B2A2UL);
    REG32(0x115D12UL) = REG32(table + (u32)(long)(short)(u16)(msg << 2));
}

/* ---- the dialog ---------------------------------------------------------
 * The dialog occupies x H'30 to H'E7 and y H'A0 to H'C0 -- a strip across
 * the bottom of the screen. Its contents scroll sideways within that strip,
 * which is what H'20EFE2 and its two callers are for.
 */
#define DIALOG_X0   0x0030
#define DIALOG_X1   0x00E7
#define DIALOG_Y0   0x00A0
#define DIALOG_Y1   0x00C0

/* H'20EFE2. Moves a run of bytes sideways in the visible buffer, a line at a
 * time. The register argument is where the run is taken *from* and the fifth
 * is where it goes; the third gives the far end, and the width is the
 * distance between the far end and the source, plus one.
 *
 * Its three output pointers are worked out between the pushes that pass
 * them, so they are not the frame offsets they look like -- and the copy's
 * destination is the second address, not the first. Both of those kept this
 * routine out of the last two parts.
 */
void scroll_rect(u16 x_from, u16 y0, u16 x_end, u16 y1, u16 x_to)
{
    u32 from, to, end;
    u8  sh;
    short y;
    u32 width;

    pixel_address(x_from, y0, LCD_FRAME_A, &from, &sh);
    pixel_address(x_to,   y0, LCD_FRAME_A, &to,   &sh);
    pixel_address(x_end,  y0, LCD_FRAME_A, &end,  &sh);

    width = (u32)(long)(short)(abs_short((short)((u16)end - (u16)from)) + 1);

    for (y = (short)y0; y <= (short)y1; y++) {
        copy_overlapped((u8 *)to, (const u8 *)from, width);
        to   += LCD_BYTES_PER_LINE;
        from += LCD_BYTES_PER_LINE;
    }
}

/* H'2289EE, H'228A5E. The strip's contents shifted left and right by [n]
 * pixels: left takes from n in and lays it down at the left edge, right does
 * the reverse. */
void dialog_scroll_left(u16 n)
{
    scroll_rect((u16)(n + DIALOG_X0), DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                DIALOG_X0);
}

void dialog_scroll_right(u16 n)
{
    scroll_rect(DIALOG_X0, DIALOG_Y0, (u16)(DIALOG_X1 - n), DIALOG_Y1,
                (u16)(n + DIALOG_X0));
}

/* H'228A20, H'2289B2. How far the strip has to move to bring a position to
 * one edge or the other, rounded up to a whole four pixels -- one byte. */
u16 dialog_shift_to_left_edge(u16 x)
{
    short t = (short)((short)DIALOG_X0 - (short)x);
    short q = (short)(t / 4);

    if ((short)(t % 4) != 0) q++;
    return (u16)(q << 2);
}

u16 dialog_shift_to_right_edge(u16 x)
{
    short t = (short)((u16)x + 0xFF19);
    short q = (short)(t / 4);

    if ((short)(t % 4) != 0) q++;
    return (u16)(q << 2);
}

/* H'228C20, H'228C50. The pattern number out of a queue entry. Note the two
 * bytes go together the other way round from queue_entry_ref: low byte
 * first here, high byte first there. Both are in the ROM as they stand. */
u16 queue_entry_number_first(void)
{
    return (u16)(((u16)REG8(0x11BBABUL) << 8) | (u16)REG8(0x11BBAAUL));
}

u16 queue_entry_number(u16 i)
{
    u32 e = QUEUE + (u32)(long)(short)((short)0x000D * (short)i);

    return (u16)((u16)((u16)(u8)(REG8(e + 1) & 0x03) << 8) | (u16)REG8(e));
}

/* H'2290E0. The per-entry offset byte at +H'0A. */
u8 queue_entry_offset(u16 i)
{
    return REG8(QUEUE + (u32)(long)(short)((short)0x000D * (short)i) + 0x0A);
}

/* H'22AA5C. Whether a pattern is one of the ones marked H'16 in the
 * catalogue -- those are the ones that are not turned round. */
u8 pattern_not_16(u16 n)
{
    return (u8)((REG8(stitch_record(n) + 0x17) == 0x16) ? 0 : 1);
}

/* H'2296D2. Which way round an entry is drawn: bits 6 and 7 of its second
 * byte, with 1 and 2 swapped when the pattern may be turned. */
u8 queue_entry_facing(u16 i)
{
    u32 e = QUEUE + (u32)(long)(short)((short)0x000D * (short)i);
    u8  face = (u8)((REG8(e + 1) >> 6) & 0x03);

    if (pattern_not_16(queue_entry_number(i)) != 0) {
        if (face == 0x01 || face == 0x02) face = (u8)(face ^ 0x03);
    }
    return face;
}

/* ---- the pattern picker -------------------------------------------------
 * The strip shows a row of stitch thumbnails with a cursor under one of
 * them. H'11A1CC is the position the cursor is on, H'11A1C8 where it sits on
 * the screen, H'11A1D0 and H'11A1D2 the first and last positions in the row,
 * and H'11B3D8 a cache of the pattern number for each of a thousand
 * positions so the picker does not go back to the queue for every redraw.
 *
 * These call each other in a ring -- the cursor calls the goto, the goto
 * calls the two scrolls, the scrolls call the cursor -- so they go in
 * together.
 */
#define PICK_POS      REG16(0x11A1CCUL)   /* which position */
#define PICK_X        REG16(0x11A1C8UL)   /* and where it is on screen */
#define PICK_Y        REG16(0x11A1CAUL)
#define PICK_FIRST    REG16(0x11A1D0UL)
#define PICK_LAST     REG16(0x11A1D2UL)
#define PICK_CACHE    0x11B3D8UL

void picker_goto(u16 pos);
void picker_forward(u16 n);
void picker_back(u16 n);
void picker_cursor(u8 mode);
void picker_rebuild(u16 n, u8 show_number, u8 redraw);
void picker_draw_range(u16 x, u16 from, u16 to);

/* The thumbnail for a position: the cached pattern number plus the entry's
 * own offset, then the pointer at +H'0C of that catalogue record. */
static u32 picker_thumb(u16 i)
{
    u16 n = (u16)(REG16(PICK_CACHE + (u32)(long)(short)((short)i << 1)) +
                  (u16)queue_entry_offset(i));

    return REG32(stitch_record(n) + 0x0C);
}

static u16 picker_thumb_width(u16 i)
{
    return header_word_0((const u8 *)picker_thumb(i));
}

/* H'21700A. A string drawn from a font table, in a box that is cleared
 * first. The glyph for a character is the pointer at font + ch * 4 - H'84,
 * so the table starts at H'21 -- the first printable character. Each glyph
 * carries its own width and height and sits on the baseline at y1.
 *
 * Three alignments: 1 from the left, 0 from the right, 2 centred, and the
 * centred one has to measure the whole string first.
 */
void text_draw(const char *str, u16 x0, u16 y0, u16 x1, u16 y1, u16 gap,
               u8 align, const u8 *font)
{
    u16 len = (u16)str_length(str);
    const u8 *g;
    u16 w, h, y;
    short x, i;

    draw_rect(x0, y0, x1, y1, LCD_FRAME_A, 0, 1);

    if (str == 0) return;

    if (align == 0x01) {
        x = (short)x0;
        for (i = 0; i < (short)len; i++) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            w = header_word_0(g);
            h = header_word_1(g);
            y = (u16)((u16)y1 - h);
            bitmap_draw((u16)x, y, (u16)((u16)x + w), (u16)(y + h), g,
                        LCD_FRAME_A);
            x = (short)(x + (short)(u16)(gap + w + 1));
        }
        return;
    }

    if (align == 0x00) {
        x = (short)x1;
        for (i = (short)(len - 1); i >= 0; i--) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            w = header_word_0(g);
            h = header_word_1(g);
            y = (u16)((u16)y1 - h);
            x = (short)(x - (short)w);
            bitmap_draw((u16)x, y, (u16)((u16)x + w), (u16)(y + h), g,
                        LCD_FRAME_A);
            x = (short)(x - (short)(u16)(gap + 1));
        }
        return;
    }

    if (align == 0x02) {
        u16 mid = (u16)((short)((short)((u16)x0 + (u16)x1) / (short)2));
        u16 total = 0;

        for (i = 0; i < (short)len; i++) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            total = (u16)(total + header_word_0(g) + 1);
        }
        total = (u16)(total - 1);
        total = (u16)(total + (u16)((u16)(len - 1) * gap));

        x = (short)((u16)mid - (u16)((short)((short)total / (short)2)));

        for (i = 0; i < (short)len; i++) {
            g = (const u8 *)REG32((u32)((long)(signed char)str[i] * 4) +
                                  (u32)font - 0x84UL);
            w = header_word_0(g);
            h = header_word_1(g);
            y = (u16)((u16)y1 - h);
            bitmap_draw((u16)x, y, (u16)((u16)x + w), (u16)(y + h), g,
                        LCD_FRAME_A);
            x = (short)(x + (short)(u16)(gap + w + 1));
        }
    }
}

/* H'22A1DE. The position's number, drawn in the corner of the strip. */
void dialog_number_draw(u16 n)
{
    char digits[4];

    int_to_decimal((short)n, digits);
    text_draw(digits, 0x00A4, 0x00DA, 0x00C2, 0x00E1, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

/* H'22955A. Draws the thumbnails for positions [from] to [to], starting at
 * x and stopping when the next one would run off the right edge. What is
 * left of the strip is cleared. */
void picker_draw_range(u16 x, u16 from, u16 to)
{
    short i  = (short)from;
    short px = (short)x;
    u32   g;
    u16   w, h;

    while (i <= (short)to) {
        g = picker_thumb((u16)i);
        w = header_word_0((const u8 *)g);

        if ((short)((short)w + px) > (short)0x00E7) {
            if (px <= (short)0x00E7) {
                draw_rect((u16)px, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                          LCD_FRAME_A, 0, 1);
            }
            return;
        }

        if (i != 0 && px >= (short)DIALOG_X0) {
            draw_rect((u16)px, DIALOG_Y0, (u16)((short)w + px), DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
            h = header_word_1((const u8 *)g);
            bitmap_draw_mirrored((u16)px, (u16)(0x00BF - h),
                                 (u16)((short)w + px), 0x00BF,
                                 (const u8 *)g, LCD_FRAME_A,
                                 queue_entry_facing((u16)i));
            px = (short)(px + (short)(u16)(w + 1));
        }
        i++;
    }

    if ((short)(i - 1) == (short)PICK_LAST) {
        if (px <= (short)0x00E7) {
            draw_rect((u16)px, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
        }
    }
}

/* The cursor is a short vertical line in the second buffer, under the
 * thumbnail. Screen H'43 keeps its position somewhere else and draws it
 * upwards rather than down. */
static void picker_cursor_line(u8 colour)
{
    if (REG8(0x11A169UL) == 0x43) {
        draw_line(REG16(0x11B3CEUL), REG16(0x11B3D0UL),
                  REG16(0x11B3CEUL), (u16)(REG16(0x11B3D0UL) + 0xFFE2),
                  LCD_FRAME_B, colour);
    } else {
        draw_line(PICK_X, PICK_Y, PICK_X, (u16)(PICK_Y + 0x001E),
                  LCD_FRAME_B, colour);
    }
}

/* H'228216. The cursor: put it out, take it away, or blink it.
 *
 * Mode 4 means "whatever it was doing"; H'11A1E9 remembers that. Mode 3 is
 * the once-a-pass tick, which counts H'11A1E6 to H'64 and flips the line
 * between colour 0 and colour 3.
 */
void picker_cursor(u8 mode)
{
    u8 m = mode;

    if (m == 0x04) m = REG8(0x11A1E9UL);
    if (m != REG8(0x11A1E9UL) && m != 0x03) REG8(0x11A1E9UL) = m;

    if (m == 0x00) {
        if (REG8(0x11A1E8UL) == 0) {
            picker_cursor_line(0);
            REG8(0x11A1E8UL) = 0x01;
        }
        REG8(0x11A1EAUL) = 0;
        return;
    }

    if (m == 0x02) return;
    if (m != 0x01 && m != 0x03) return;

    if (m == 0x01) {
        /* Mode 1 does not return: it puts the cursor where it belongs and
         * then falls into the blink below, the same as mode 3. */
        REG8(0x11A1EAUL) = 0x01;
        if (REG8(0x11A169UL) != 0x43) picker_goto(QUEUE_POS);
    } else {
        if (REG8(0x11A1DEUL) != 0) {
            QUEUE_POS = REG16(0x11B3D2UL);
            REG8(0x11A1DEUL) = 0;
        }
        if (REG8(0x11A1E4UL) != 0 && REG8(0x11A169UL) != 0x41) {
            picker_rebuild(REG16(0x11B3D4UL), 0x01, 0x01);
            REG8(0x11A1E4UL) = 0;
        }
        if (REG8(0x11A1EAUL) != 0x01) {
            REG8(0x11A1E9UL) = 0;
            return;
        }
        REG8(0x11A1E9UL) = 0x01;
    }

    REG16(0x11A1E6UL) = (u16)(REG16(0x11A1E6UL) + 1);
    if (REG16(0x11A1E6UL) >= 0x0064) {
        picker_cursor_line((u8)(REG8(0x11A1E8UL) != 0 ? 0x03 : 0x00));
        REG8(0x11A1E8UL) = (u8)(REG8(0x11A1E8UL) == 0 ? 0x01 : 0x00);
        REG16(0x11A1E6UL) = 0;
    }
}

/* H'2284C4. Puts the cursor on a position, scrolling either way to get
 * there. H'FFFF means "back to the beginning". */
void picker_goto(u16 pos)
{
    if (REG8(0x114DC6UL) & 0x80) return;

    if (pos == 0xFFFF) {
        picker_cursor(0);
        PICK_X = DIALOG_X0;
        QUEUE_POS = PICK_FIRST;
        PICK_POS = PICK_FIRST;
        picker_cursor(1);
        return;
    }

    if ((short)pos < (short)PICK_POS) {
        picker_back((u16)((u16)PICK_POS - pos));
    } else if ((short)pos > (short)PICK_POS) {
        picker_forward((u16)(pos - (u16)PICK_POS));
    }
}

/* H'22852E. Forward [n] positions. The widths of everything stepped over
 * are added up; if that carries the cursor past the right edge the strip is
 * scrolled, and if it would have to scroll further than the strip is wide it
 * is redrawn from the new position instead. */
void picker_forward(u16 n)
{
    u16 total = 0;
    u16 first;
    short i;
    u16 shift;

    if (PICK_POS == PICK_LAST) return;

    picker_cursor(0);
    first = (u16)(PICK_POS + 1);

    if ((short)((u16)PICK_POS + n) > (short)PICK_LAST) PICK_POS = PICK_LAST;
    else                                               PICK_POS = (u16)(PICK_POS + n);

    for (i = (short)first; i <= (short)PICK_POS; i++) {
        total = (u16)(total + picker_thumb_width((u16)i) + 1);
    }

    if ((short)((u16)PICK_X + total) > (short)0x00E7) {
        shift = dialog_shift_to_right_edge((u16)(PICK_X + total));
        PICK_X = (u16)(PICK_X - shift);

        if ((short)PICK_X < (short)DIALOG_X0) {
            draw_rect(DIALOG_X0, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
            picker_draw_range(DIALOG_X0, PICK_POS, PICK_LAST);
            PICK_X = (u16)(picker_thumb_width(PICK_POS) + DIALOG_X0);
        } else {
            dialog_scroll_left(shift);
            draw_rect((u16)(PICK_X + 1), DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                      LCD_FRAME_A, 0, 1);
            picker_draw_range((u16)(PICK_X + 1), first, PICK_POS);
            PICK_X = (u16)(PICK_X + total);
        }
    } else {
        PICK_X = (u16)(PICK_X + total);
    }

    QUEUE_POS = PICK_POS;
    picker_cursor(1);
}

/* H'228734. Back [n] positions, the same the other way about. */
void picker_back(u16 n)
{
    u16 total = 0;
    short target;
    short i;
    u16 shift;

    if (PICK_POS == PICK_FIRST) return;

    picker_cursor(0);

    if ((short)((u16)PICK_POS - n) < (short)PICK_FIRST) target = (short)PICK_FIRST;
    else                                                target = (short)(u16)(PICK_POS - n);

    for (i = target; i <= (short)PICK_POS; i++) {
        total = (u16)(total + picker_thumb_width((u16)i) + 1);
    }

    if ((short)((u16)PICK_X - total) < (short)DIALOG_X0) {
        if ((short)(PICK_FIRST + 1) < (short)PICK_POS) {
            shift = dialog_shift_to_left_edge((u16)(PICK_X - total));
            PICK_X = (u16)(PICK_X + shift);

            if ((short)PICK_X >= (short)0x00E7) {
                draw_rect(DIALOG_X0, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                          LCD_FRAME_A, 0, 1);
                picker_draw_range(DIALOG_X0, (u16)target, PICK_LAST);
                if (target == 0) {
                    PICK_X = DIALOG_X0;
                } else {
                    PICK_X = (u16)(picker_thumb_width((u16)target) +
                                   DIALOG_X0);
                }
            } else {
                dialog_scroll_right(shift);
                draw_rect(DIALOG_X0, DIALOG_Y0, PICK_X, DIALOG_Y1,
                          LCD_FRAME_A, 0, 1);
                PICK_X = (u16)(PICK_X - total);
                picker_draw_range((u16)(PICK_X + 1), (u16)target, PICK_POS);
                PICK_X = (u16)(PICK_X + picker_thumb_width((u16)target) + 1);
            }
        }
    } else {
        total = (u16)(total - (picker_thumb_width((u16)target) + 1));
        PICK_X = (u16)(PICK_X - total);
    }

    PICK_POS = (u16)target;
    QUEUE_POS = (u16)target;
    picker_cursor(1);
}

/* H'22A098. Rebuilds the whole picker: the queue is copied out of flash,
 * every position's pattern number is cached, and the row is redrawn. */
void picker_rebuild(u16 n, u8 show_number, u8 redraw)
{
    short i;

    REG8(0x11A184UL) = 0;
    REG16(0x11A1CEUL) = n;

    if (show_number != 0) dialog_number_draw(n);

    if (redraw != 0) {
        draw_rect(DIALOG_X0, DIALOG_Y0, DIALOG_X1, DIALOG_Y1,
                  LCD_FRAME_A, 0, 1);
    }

    mem_copy((u8 *)0x0011BBAAUL, (const u8 *)0x00578000UL, 0x32D5);

    REG16(PICK_CACHE) = queue_entry_number_first();
    for (i = 1; i <= (short)0x03E8; i++) {
        REG16(PICK_CACHE + (u32)(long)(short)(i << 1)) =
            queue_entry_number((u16)i);
    }

    if (REG16(0x11EE82UL + (u32)(long)(short)((short)n << 2)) != 0) {
        PICK_FIRST = REG16(0x11EE80UL + (u32)(long)(short)((short)n << 2));
        PICK_LAST  = REG16(0x11EE82UL + (u32)(long)(short)((short)n << 2));
        if (redraw != 0) {
            picker_draw_range(0x0031, (u16)(PICK_FIRST + 1), PICK_LAST);
        }
    } else {
        PICK_FIRST = queue_entry_number_first();
        PICK_LAST  = PICK_FIRST;
    }

    picker_goto(0xFFFF);

    if (PICK_FIRST != PICK_LAST) picker_forward(0x0001);
}

/* H'210DC6. The noise a message makes. H'57EFC8 in the settings block is a
 * pair of bytes a message: the first says whether it makes one at all and
 * the second how many times. */
void message_beep(u16 msg)
{
    u32 e = 0x0057EFC8UL + (u32)(long)(short)((short)msg << 1);

    if (REG8(e) != 0) beep(0x001E, 0x0064, REG8(e + 1));
}

/* H'20369A. A running queue sent back to the start of its group. */
void queue_group_restart(void)
{
    if (REG8(0x11A175UL) != 0) return;

    if (!(REG8(0x114DC9UL) & 0x02)) {
        if (QUEUE_POS != QUEUE_FIRST) return;
        if (QUEUE_FIRST == QUEUE_LAST) return;
    }

    if (REG8(0x114DC9UL) & 0x02) {
        REG8(0x114DC9UL) &= (u8)~0x02;
        REG8(0xFFFEC4UL) |= 0x20;
        REG16(0x114DDEUL) = 0;
    }

    QUEUE_POS = (u16)(QUEUE_GROUP + 1);
    queue_step_flags_first();
}

/* H'20966A. The speed target in the service mode, where the handwheel
 * position rather than the pedal sets it. Past H'64 the reading is stretched
 * on to H'1E..H'FF, and the first time it gets there the machine says so --
 * a beep and message H'1A. */
void speed_target_service(void)
{
    u8 v = REG8(0xFFFEC3UL);
    u8 r;

    if (v == 0x00 || v == 0x02) {
        REG8(0x114DD6UL) = 0;
        REG8(0x114DCFUL) &= (u8)~0x10;
        return;
    }
    if (!(v == 0x01 || (v >= 0x03 && v <= 0x05))) {
        REG8(0x114DD6UL) = 0;
        REG8(0x114DCFUL) &= (u8)~0x10;
        return;
    }

    r = REG8(0xFFFEC2UL);
    if (r < 0x64 || r > 0xD2) {
        if (r > 0xD2) REG8(0x114DD6UL) = 0xFF;
        return;
    }

    REG8(0x114DD6UL) = (u8)((u8)((short)((short)(u16)
                          ((u16)((u16)((u16)r + 0xFF9C) * (u16)0x00E1))
                          / (short)0x006E)) + 0x1E);

    if (!(REG8(0x114DCFUL) & 0x10) && !(REG8(0xFFFEC4UL) & 0x01)) {
        REG8(0x114DCFUL) |= 0x10;
        message_beep(0x0007);
        message_show(0x001A);
    }
}

/* ---- the stitch database, opened -----------------------------------------
 * H'208826. The last of the bring-up calls before the display, and the one
 * that reaches furthest: below it is the stitch pattern database, which is
 * the machine's real subject matter rather than another peripheral.
 *
 * Its own body is small and is written out here: a block of state bytes at
 * H'FFFEF5 upwards, the refresh word at H'FFFF00, and the two counters. What
 * it then calls is not.
 *
 * H'2086FC reads the six parameters of patterns H'03FE and H'03FF back out
 * of the database and writes them into the working records at H'0E4010,
 * through six accessors and one setter that all address a table of 24-byte
 * descriptors pointed at by H'114DD2. H'208210 is the current-stitch setup,
 * 326 instructions over some sixty routines.
 *
 * Both are left as named stubs. Together they are eighty-two routines and
 * about 6,900 instructions -- comparable to everything reconstructed up to
 * this point -- and they are a subsystem rather than a bring-up step, so
 * they belong in a part of their own where the record structure can be
 * pinned down and each accessor checked against the original. Reconstructing
 * the shell around them and leaving the middle guessed at would be worse
 * than leaving them named.
 *
 * Because of that, this routine has no comparison case: any window that
 * takes in the two calls runs the original through the database and the
 * rebuild through a pair of stubs.
 */

void stitch_database_open(void)
{
    REG16(0xFFFF00UL) = 0x1000;

    REG8(0xFFFEF5UL) = 0x04;
    REG8(0xFFFEF6UL) = 0x20;
    REG8(0xFFFEF7UL) = 0x00;
    REG8(0xFFFEF8UL) = 0x00;
    REG8(0xFFFEF9UL) = 0x00;
    REG8(0xFFFEFAUL) = 0x00;
    REG8(0xFFFEFBUL) = 0x00;
    REG8(0xFFFEFCUL) = 0x00;
    REG8(0x11A6ADUL) = 0x00;

    REG16(0xFFFEE0UL) = 0x0001;
    REG8(0xFFFEFDUL) = 0x00;

    stitch_params_reload();
    stitch_state_init();
}

/* ---- bringing the machine up -------------------------------------------
 * H'208D88, the spine of the whole start-up. Twenty calls in order, ending
 * with the one that brings the display alive. Each is named for its address
 * until it is reconstructed, and a stub does nothing rather than pretending.
 *
 * Reconstructed so far: the I2C bus (H'200CB6), the settings copy and
 * the configuration block check.
 */

/* Settings the bring-up reads out of the H'57FF8x window and keeps in RAM.
 * A value above H'10 is not one of the choices, so it falls back to 8. */
#define SETTING_INDEX       REG8(0x57FF8EUL)
#define SETTING_WORD_A      REG32(0x57FF82UL)
#define SETTING_WORD_B      REG32(0x57FF86UL)
#define ACTIVE_SETTING      REG8(0xFFFED8UL)
#define SETTING_COPY_A      REG32(0x11A6E0UL)
#define SETTING_COPY_B      REG32(0x11A6E4UL)

void machine_init(void)
{
    set_flash_page_buffer();
    port_shadows_init();
    adc_start_conversion();
    input_state_init();
    sci0_module_init();

    i2c_init();                 /* H'200CB6 -- reconstructed */

    config_to_eeprom();
    config_block_check();

    ACTIVE_SETTING = SETTING_INDEX;
    if (ACTIVE_SETTING > 0x10) ACTIVE_SETTING = 0x08;
    SETTING_COPY_A = SETTING_WORD_A;
    SETTING_COPY_B = SETTING_WORD_B;

    stitch_database_open();
    port_c_init();
    pins_to_input_2061A0();
    port_b_init();
    main_motor_init();
    motors_and_timers_init();
    keys_scan_settled();
    analog_input_init();
    speed_control_init();
    display_init();
}

/* The two subsystems that are still to be reconstructed, and the only two
 * stubs left in the application.
 *
 *   H'22382A  the screen dispatcher: one switch on H'11A169 over 79 screens,
 *             through the table at H'2238B0. It reaches 354 routines and
 *             about 200 KB of the image -- every screen the operator sees.
 *
 *   H'2354AE  the embroidery module's state machine, 420 instructions of
 *             sequencing over a further 150 routines in H'23xxxx-H'24xxxx.
 *
 * Both are called from the loops below. Returning from them leaves the
 * machine running with a blank screen and no module, which is what a stub
 * has to do; nothing else in the loops depends on either one.
 */
STUB(screen_dispatch, 22382A)
STUB(embroidery_service, 2354AE)

/* ---- the diagnostics screen -------------------------------------------
 * H'201802. Forty-seven numbers put on the screen in four columns, and one
 * temperature worked out in floating point. Nothing here changes the
 * machine: it is the page a technician turns on with the key combination
 * H'207DFC watches for, and after that it redraws itself once every 256
 * passes -- H'11A7E9 counts them and only the pass that finds it at zero
 * draws.
 *
 * Its three arguments are put on the screen and nothing else is done with
 * them: the word at H'11A674 and the two bytes after it, which is whatever
 * the caller wanted to look at.
 */
void diag_screen(u16 a, u8 b, u8 c)
{
    const float raw = (float)(u32)REG8(0xFFFEDEUL) * 0.0196f;
    u32 slot;
    u8  d9, d1, d2, dA, d7C, dC7;
    u8  n;

    /* A parameter that has moved since the pattern was made current bumps
     * a counter, and the counter is one of the numbers below. */
    if (REG8(0x11A175UL) == 0x01) {
        if (!(REG8(0x11A69EUL) == REG8(0xFFFEE4UL) &&
              REG8(0x11A6A0UL) == REG8(0xFFFEE7UL))) {
            REG8(0xFFFECCUL) = (u8)(REG8(0xFFFECCUL) + 1);
        }
    }

    d9  = (u8)((REG8(0x114DC9UL) & 0x04) ? 1 : 0);
    d1  = (u8)((REG8(0x114DC9UL) & 0x01) ? 1 : 0);
    d2  = (u8)((REG8(0x114DC9UL) & 0x02) ? 1 : 0);
    dA  = (u8)((REG8(0x114DCAUL) & 0x08) ? 1 : 0);
    d7C = (u8)((REG8(0x11A7BCUL) & 0x80) ? 1 : 0);
    dC7 = (u8)((REG8(0x114DC7UL) & 0x80) ? 1 : 0);

    n = REG8(0x11A7E9UL);
    REG8(0x11A7E9UL) = (u8)(n + 1);
    if (n != 0) return;

    number_draw(d7C,                       0x0064, 0x0014);
    number_draw(REG8(0xFFFEECUL),          0x0064, 0x001E);

    number_draw(REG16(0xFFFEE0UL),         0x00DF, 0x0028);
    number_draw(REG16(0x11A7E6UL),         0x00DF, 0x0032);
    number_draw(REG8(0x11A688UL),          0x00DF, 0x003C);
    number_draw(queue_group_count(),       0x00DF, 0x0049);
    number_draw((REG16(0xFFFEFEUL) > 0x0400) ? 0x0400 : REG16(0xFFFEFEUL),
                                           0x00DF, 0x0056);
    number_draw(REG16(0x11A6C8UL),         0x00DF, 0x0060);
    number_draw(REG16(0x11A1D0UL),         0x00DF, 0x006A);
    number_draw(REG16(0x11A1D2UL),         0x00DF, 0x0074);
    number_draw(REG8(0x11A175UL),          0x00DF, 0x0082);

    number_draw(REG8(0xFFFECCUL),          0x00F0, 0x0028);
    number_draw(mode_code(),               0x00F0, 0x0032);
    number_draw(REG8(0x11A6CFUL),          0x00F0, 0x003C);
    number_draw(dC7,                       0x00F0, 0x0046);

    /* Four bytes out of the H'10-byte record the current speed indexes. */
    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4015UL + slot), 0x00F0, 0x0053);
    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4025UL + slot), 0x00F0, 0x005D);
    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4035UL + slot), 0x00F0, 0x0067);
    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4045UL + slot), 0x00F0, 0x0071);

    number_draw(d9,                        0x00F0, 0x0082);
    number_draw(d1,                        0x00F0, 0x008C);
    number_draw(d2,                        0x00F0, 0x0096);
    number_draw(dA,                        0x00F0, 0x00A0);
    number_draw(REG8(0x11A7DFUL),          0x00F0, 0x00AA);

    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4011UL + slot), 0x0100, 0x0053);
    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4021UL + slot), 0x0100, 0x005D);
    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4031UL + slot), 0x0100, 0x0067);
    slot = (u32)(u16)(REG16(0xFFFEE0UL) << 4);
    number_draw(REG8(0x000E4041UL + slot), 0x0100, 0x0071);

    number_draw(REG8(0xFFFED1UL),          0x0114, 0x0028);
    number_draw(REG8(0xFFFED3UL),          0x0114, 0x0032);
    number_draw(REG8(0xFFFED5UL),          0x0114, 0x003C);
    number_draw(REG8(0xFFFED7UL),          0x0114, 0x0046);
    number_draw(REG8(0x11A69EUL),          0x0114, 0x0056);
    number_draw(REG8(0x11A7D9UL),          0x0114, 0x0060);
    number_draw(REG8(0x11A7C0UL),          0x0114, 0x006A);
    number_draw(REG8(0x11A6A0UL),          0x0114, 0x0078);
    number_draw(REG8(0x11A7DAUL),          0x0114, 0x0082);
    number_draw(REG8(0x11A7C2UL),          0x0114, 0x008C);
    number_draw(REG16(0xFFFEE0UL),         0x0114, 0x0099);
    number_draw(REG8(0xFFFEFDUL),          0x0114, 0x00A3);
    number_draw(mode_code(),               0x0114, 0x00AD);
    number_draw(REG16(0x11A672UL),         0x0114, 0x00B7);
    number_draw(a,                         0x0114, 0x00C1);
    number_draw((REG8(0x11A7BDUL) & 0x40) ? 0x0001 : 0x0000,
                                           0x0114, 0x00CB);
    number_draw(b,                         0x0114, 0x00D5);
    number_draw(c,                         0x0114, 0x00DF);

    number_draw((u8)(int)((4.4833002f - raw) / 0.04422f), 0x00DF, 0x00BE);
}

/* H'207DFC. The keys, then the diagnostics screen if it has been asked for,
 * and then the host link.
 *
 * The way in is a key combination: bit 1 of H'FFFEC1 with bit 5 of H'FFFEDC
 * raises H'11A66B, and once it is up the screen is drawn on every pass from
 * then on. There is no way back out of it in this routine. */
void key_and_diag(void)
{
    key_scan_step();

    if (REG8(0x11A66BUL) == 0x01) {
        diag_screen(REG16(0x11A674UL), REG8(0x11A676UL), REG8(0x11A677UL));
    } else if ((REG8(0xFFFEC1UL) & 0x02) && (REG8(0xFFFEDCUL) & 0x20)) {
        REG8(0x11A66BUL) = 0x01;
    }

    rom_host_service();
}

/* H'20ADF8. The three things every loop in the machine does between one
 * piece of work and the next: the keys and the host link, the screen, and
 * the analog round robin. Not to be confused with service_tick, which is
 * H'208698 -- the sewing side's pass. */
void loop_tick(void)
{
    key_and_diag();
    screen_dispatch();
    analog_scan();
}

/* ---- service mode -------------------------------------------------------
 * H'20BEE2 and the forty-odd routines under it. Holding a key at power-on
 * brings the machine up here instead of into the sewing screen, and what it
 * offers is a bench: each of the four steppers driven to fixed positions,
 * the main motor run at a fixed speed, the hall sensor trimmed against the
 * value in the settings block, and the foot lift calibrated into the EEPROM.
 *
 * The whole thing is one dispatch on H'FFFEC5, the code the panel leaves
 * there, through a pair of tables at H'20BCD8 (44 keys) and H'20BDB0 (their
 * handlers, in reverse). Every handler is a loop that runs until the code
 * changes under it, so the panel is what ends a test, and every one of them
 * finishes by calling service_exit to put the machine back as it was.
 */

/* H'2007CA. A number drawn into a 16 x 8 box at (x, y), in the middle of
 * the box, from the font at H'119DE6. Used only by the service screens. */
void number_draw(u16 value, u16 x, u16 y)
{
    char buf[8];

    int_to_decimal((short)value, buf);
    text_draw(buf, x, y, (u16)(x + 0x0F), (u16)(y + 0x07),
              0x0001, 0x02, (const u8 *)0x00119DE6UL);
}

/* H'209BDC. The two foot-lift end stops written into the settings block. */
void foot_calibration_save(void)
{
    FLASH_BUSY |= 0x20;
    rom_flash_write((const void *)0x00FFFEF3UL, 0x0057FF90UL, 1);
    rom_flash_write((const void *)0x00FFFEF4UL, 0x0057FF91UL, 1);
    FLASH_BUSY &= (u8)~0x20;
}

/* H'20AD30. One pass of the machine while a test is running: the selection,
 * the pedal, and the main motor. H'FFFEC3 going to zero drops bit 7 of
 * H'114DC8, which is what the loops watch to know the pedal has been let go;
 * while it is up, H'FFFEC8 is held at zero. */
void service_pass(void)
{
    stitch_state_init();
    pedal_service();

    if (REG8(0xFFFEC3UL) == 0) {
        REG8(0x114DC8UL) &= (u8)~0x80;
    } else if (REG8(0x114DC8UL) & 0x80) {
        REG8(0xFFFEC8UL) = 0x00;
    }

    main_motor_service();
}

/* H'20AD6C. The way out of every test: motors released, speed back to the
 * idle 4, and the dispatch code cleared so the loop that called it stops. */
void service_exit(void)
{
    REG8(0xFFFEC4UL) &= (u8)~0x04;
    REG8(0xFFFEC4UL) &= (u8)~0x08;
    REG8(0x114DCDUL) &= (u8)~0x04;
    REG8(0xFFFEF9UL) = 0x00;
    REG8(0xFFFEC7UL) &= (u8)~0x80;
    REG8(0x114DC8UL) &= (u8)~0x40;
    REG8(0x114DC8UL) |=        0x80;
    REG8(0xFFFEC8UL) = 0x00;
    REG8(0x11A82FUL) = 0x00;

    main_motor_service();

    REG8(0x114DC6UL) &= (u8)~0x01;
    REG8(0x114DC6UL) &= (u8)~0x02;
    REG8(0x114DC6UL) &= (u8)~0x20;
    REG8(0x114DCDUL) &= (u8)~0x02;

    REG16(0xFFFEE0UL) = 0x0004;
    REG8(0xFFFEFDUL) = 0x00;
    REG8(0xFFFEC5UL) = 0x00;

    service_pass();
}

/* H'2085B2 and H'20864C. The pair that brackets talking to the embroidery
 * module: the first parks the machine and remembers where the four steppers
 * were, the second puts them back.
 *
 * The wait in the middle is a real one -- BHI, so it runs until the tick
 * counter passes H'C8 -- unlike the ones in the motor tests, which test the
 * other way round and leave at once. */
void module_park(void)
{
    REG8(0xFFFEF6UL) |= 0x20;
    REG8(0x11A7D5UL) |= 0x20;

    sew_needle_stop_pin();

    REG16(0xFFFEE0UL) = 0x03FD;
    REG8(0xFFFEFDUL) = 0x00;

    sew_pass();

    REG8(0x11A6BCUL) = REG8(0x11A6B7UL);
    REG8(0x11A6BDUL) = REG8(0x11A6B8UL);
    REG8(0x11A6BEUL) = REG8(0x11A6B9UL);
    REG8(0x11A6BFUL) = REG8(0x11A6BAUL);

    if (REG8(0xFFFED3UL) != 0x1B) {
        REG8(0x11A6B7UL) = 0x1B;
        REG16(0x114DDEUL) = 0x0000;
        while (REG16(0x114DDEUL) <= 0x00C8) { }
    }

    REG8(0x114DC6UL) &= (u8)~0x02;
}

/* H'21F9A6. Idle speed, and the settings pointer back to the top of the
 * block. */
void module_speed_idle(void)
{
    REG16(0xFFFEE0UL) = 0x0001;
    REG8(0xFFFEFDUL) = 0x00;
    REG32(0x11A196UL) = 0x0057EED6UL;
}

/* H'20864C. */
void module_unpark(void)
{
    REG8(0x114DC6UL) &= (u8)~0x02;
    REG8(0x114DC6UL) &= (u8)~0x20;

    REG8(0x11A6B7UL) = REG8(0x11A6BCUL);
    REG8(0x11A6B8UL) = REG8(0x11A6BDUL);
    REG8(0x11A6B9UL) = REG8(0x11A6BEUL);
    REG8(0x11A6BAUL) = REG8(0x11A6BFUL);

    module_speed_idle();
}

/* H'244988 and H'2449E2. ITU1 borrowed and given back.
 *
 * While the embroidery module is being talked to, ITU1 is retimed -- TCR1 to
 * H'F8, GRB1 to H'FFFF, TIER1 to H'A3, GRA1 to zero, and its start bit
 * cleared -- and what was there is kept at H'11F2D4. H'114DB7 says which of
 * the two states it is in, so neither can happen twice. */
void itu1_borrow(void)
{
    if (REG8(0x114DB7UL) != 0) return;

    REG8(0xFFFF60UL) &= (u8)~0x02;

    REG8(0x11F2D4UL) = REG8(0xFFFF70UL);
    REG8(0xFFFF70UL) = 0xF8;
    REG16(0x11F2E0UL) = REG16(0xFFFF74UL);
    REG16(0xFFFF74UL) = 0xFFFF;
    REG8(0x11F2D5UL) = REG8(0xFFFF6EUL);
    REG8(0xFFFF6EUL) = 0xA3;
    REG16(0x11F2E2UL) = REG16(0xFFFF72UL);
    REG16(0xFFFF72UL) = 0x0000;
    REG8(0xFFFF71UL) = (u8)(REG8(0xFFFF71UL) & 0xF8);

    REG8(0x114DB7UL) = 0x01;
}

void itu1_return(void)
{
    if (REG8(0x114DB7UL) == 0) return;

    REG8(0xFFFF60UL) &= (u8)~0x02;

    REG8(0xFFFF71UL) = (u8)(REG8(0xFFFF71UL) & 0xF8);
    REG16(0xFFFF72UL) = REG16(0x11F2E2UL);
    REG8(0xFFFF6EUL) = REG8(0x11F2D5UL);
    REG16(0xFFFF74UL) = REG16(0x11F2E0UL);
    REG8(0xFFFF70UL) = REG8(0x11F2D4UL);

    REG8(0xFFFF60UL) |= 0x02;

    REG8(0x114DB7UL) = 0x00;
}

/* H'2485B0. The module link brought up once, the first time anything asks
 * for it. H'11F5A1 is the "has been" flag. */
void module_link_once(void)
{
    if (REG8(0x11F5A1UL) != 0) return;

    REG8(0x11F5A1UL) = 0x01;

    sci0_module_init();
    itu1_return();
    module_park();
    itu1_borrow();
    REG8(0xFFFFB2UL) = 0x00;
    module_unpark();
    itu1_return();
}

/* H'2485E2. The same, but as a test that runs until the panel leaves code
 * H'0D. */
void service_module_pass(void)
{
    sci0_module_init();
    itu1_return();
    module_park();
    itu1_borrow();

    while (REG8(0xFFFEC5UL) == 0x0D) loop_tick();

    REG8(0xFFFFB2UL) = 0x00;
    module_unpark();
    itu1_return();
}

/* H'20AE06. The readings the service screen starts from.
 *
 * The first is the machine's temperature or supply, whichever H'FFFEDE is:
 * a byte turned into (H'4.4833 - x * H'0.0196) / H'0.04422, which is the
 * shape of a divider around a sensor. The second is analog channel 2 scaled
 * by H'64 / H'100. The five bytes after that are the parked positions the
 * four steppers are driven from. */
void service_readings(void)
{
    float t;

    t = 4.4833002f - (float)(u32)REG8(0xFFFEDEUL) * 0.0196f;
    REG8(0xFFFEE4UL) = (u8)(int)(t / 0.04422f);

    REG8(0xFFFEE7UL) = (u8)((u16)(0x64 * (u16)adc_get_result(2)) / 0x0100);

    REG8(0x11A6B7UL) = 0x1B;
    REG8(0x11A6B8UL) = 0x6E;
    REG8(0x11A6B9UL) = 0x19;
    REG8(0x11A6BAUL) = 0x19;
    REG8(0x11A6BBUL) = 0x15;

    /* Only on the first pass, and only with no test selected, are the two
     * foot-lift end stops put on the screen. */
    if (REG8(0x11A833UL) == 0 && REG8(0xFFFEC5UL) == 0) {
        number_draw(REG8(0xFFFEF3UL), 0x002C, 0x000C);
        number_draw(REG8(0xFFFEF4UL), 0x0043, 0x000C);
    }

    module_link_once();

    REG8(0x11A833UL) = (u8)(REG8(0x11A833UL) + 1);
}

/* ---- the steppers on the bench ----------------------------------------
 * H'FFFED3, H'FFFED5, H'FFFED7 and H'FFFED1 are the four command bytes --
 * needle, feed, hook width and hook position -- and H'11A6B7 to H'11A6BB
 * shadow them. Codes C0 to F5 in the dispatch drive one motor: C0 releases
 * it, C2 to C5 put it at H'FC, H'FD, H'FE or H'FF, and B1 to B5 do all four
 * at once.
 *
 * The FD and FE positions of the needle motor write only the shadow and not
 * the command byte -- H'20B15E and H'20B1B8 against H'20B0FE and H'20B212 --
 * where all three of the other motors write both. Left as it is.
 */
void motor_1_off(void)
{
    REG8(0x114DC6UL) &= (u8)~0x01;
    REG8(0xFFFED3UL) = 0x00;
    REG8(0x11A6B7UL) = 0x00;
    REG8(0x114DC8UL) |= 0x40;
}

void motor_2_off(void)
{
    REG8(0x114DC6UL) &= (u8)~0x02;
    REG8(0xFFFED5UL) = 0x00;
    REG8(0x11A6B8UL) = 0x00;
    REG8(0x114DC8UL) |= 0x40;
}

void motor_3_off(void)
{
    REG8(0x114DC6UL) &= (u8)~0x20;
    REG8(0xFFFED7UL) = 0x00;
    REG8(0x11A6B9UL) = 0x00;
    REG8(0x11A6BAUL) = 0x00;
    REG8(0x114DC8UL) |= 0x40;
}

void motor_4_off(void)
{
    REG8(0x114DCDUL) &= (u8)~0x02;
    REG8(0xFFFED1UL) = 0x00;
    REG8(0x11A6BBUL) = 0x00;
    REG8(0x114DC8UL) |= 0x40;
}

/* H'20B0A8. All four released, a quarter second apart -- except that the
 * wait is another of the settle loops that leave at once: the counter is
 * zeroed and the test is "below H'FA", which it is. */
static void tick_settle_FA(void)
{
    REG16(0x114DDAUL) = 0x0000;
    while (REG16(0x114DDAUL) >= 0x00FA) { }
}

void motors_off_stepped(void)
{
    motor_1_off(); tick_settle_FA();
    motor_2_off(); tick_settle_FA();
    motor_3_off(); tick_settle_FA();
    motor_4_off();
}

void motor_1_pos_FC(void) { REG8(0xFFFED3UL) = 0xFC; REG8(0x11A6B7UL) = 0xFC; }
void motor_2_pos_FC(void) { REG8(0xFFFED5UL) = 0xFC; REG8(0x11A6B8UL) = 0xFC; }
void motor_3_pos_FC(void) { REG8(0xFFFED7UL) = 0xFC; REG8(0x11A6B9UL) = 0xFC;
                            REG8(0x11A6BAUL) = 0xFC; }
void motor_4_pos_FC(void) { REG8(0xFFFED1UL) = 0xFC; REG8(0x11A6BBUL) = 0xFC; }

void motors_pos_FC(void)
{
    motor_1_pos_FC(); motor_2_pos_FC(); motor_3_pos_FC(); motor_4_pos_FC();
}

void motor_1_pos_FD(void) { REG8(0x11A6B7UL) = 0xFD; }
void motor_2_pos_FD(void) { REG8(0xFFFED5UL) = 0xFD; REG8(0x11A6B8UL) = 0xFD; }
void motor_3_pos_FD(void) { REG8(0xFFFED7UL) = 0xFD; REG8(0x11A6B9UL) = 0xFD;
                            REG8(0x11A6BAUL) = 0xFD; }
void motor_4_pos_FD(void) { REG8(0xFFFED1UL) = 0xFD; REG8(0x11A6BBUL) = 0xFD; }

void motors_pos_FD(void)
{
    motor_1_pos_FD(); motor_2_pos_FD(); motor_3_pos_FD(); motor_4_pos_FD();
}

void motor_1_pos_FE(void) { REG8(0x11A6B7UL) = 0xFE; }
void motor_2_pos_FE(void) { REG8(0xFFFED5UL) = 0xFE; REG8(0x11A6B8UL) = 0xFE; }
void motor_3_pos_FE(void) { REG8(0xFFFED7UL) = 0xFE; REG8(0x11A6B9UL) = 0xFE;
                            REG8(0x11A6BAUL) = 0xFE; }
void motor_4_pos_FE(void) { REG8(0xFFFED1UL) = 0xFE; REG8(0x11A6BBUL) = 0xFE; }

void motors_pos_FE(void)
{
    motor_1_pos_FE(); motor_2_pos_FE(); motor_3_pos_FE(); motor_4_pos_FE();
}

void motor_1_pos_FF(void) { REG8(0xFFFED3UL) = 0xFF; REG8(0x11A6B7UL) = 0xFF; }
void motor_2_pos_FF(void) { REG8(0xFFFED5UL) = 0xFF; REG8(0x11A6B8UL) = 0xFF; }
void motor_3_pos_FF(void) { REG8(0xFFFED7UL) = 0xFF; REG8(0x11A6B9UL) = 0xFF;
                            REG8(0x11A6BAUL) = 0xFF; }
void motor_4_pos_FF(void) { REG8(0xFFFED1UL) = 0xFF; REG8(0x11A6BBUL) = 0xFF; }

void motors_pos_FF(void)
{
    motor_1_pos_FF(); motor_2_pos_FF(); motor_3_pos_FF(); motor_4_pos_FF();
}

/* ---- the hall sensor trim ---------------------------------------------
 * H'FFFECE is what the sensor reads, H'57FF8A the value it is supposed to
 * read, and H'FFFEC9 the drive that gets it there. The trim walks the drive
 * one step at a time until the reading sits in the ten-count band above the
 * target, and when it has stayed there for ten passes the drive is written
 * into the settings block.
 */

/* H'20B272. The drive byte into H'57FF8D. */
void hall_target_save(void)
{
    FLASH_BUSY |= 0x20;
    rom_flash_write((const void *)0x00FFFEC9UL, 0x0057FF8DUL, 1);
    FLASH_BUSY &= (u8)~0x20;

    REG8(0xFFFEC7UL) &= (u8)~0x80;
    REG8(0x114DC8UL) |=        0x80;
}

/* H'20B2C4. Ten passes at an end stop and the trim gives up there. */
void hall_step_settle(void)
{
    const u8 n = REG8(0x11A82FUL);

    REG8(0x11A82FUL) = (u8)(n + 1);
    if (n < 0x0A) return;

    REG8(0xFFFEC9UL) = 0xFA;
    REG8(0xFFFEC4UL) |=        0x08;
    REG8(0xFFFEC4UL) &= (u8)~0x04;
    hall_target_save();
}

/* H'20B2FC. One step of the trim, once every hundred ticks. */
void hall_trim(void)
{
    u16 target;
    u8  drive;

    if ((short)REG16(0x114DE4UL) >= 0) return;

    target = REG16(0x57FF8AUL);
    REG16(0x114DE4UL) = 0x0064;

    if (REG16(0xFFFECEUL) >= target &&
        (u16)(target + 0x000A) >= REG16(0xFFFECEUL)) {
        const u8 n = REG8(0x11A82FUL);

        REG8(0x11A82FUL) = (u8)(n + 1);
        if (n >= 0x0A) {
            REG8(0x11A82FUL) = 0x00;
            REG8(0xFFFEC4UL) |=        0x04;
            REG8(0xFFFEC4UL) &= (u8)~0x08;
            hall_target_save();
        }
        return;
    }

    if (REG16(0xFFFECEUL) < target) {
        drive = REG8(0xFFFEC9UL);
        if (drive >= 0xFA) {
            hall_step_settle();
        } else {
            REG8(0xFFFEC9UL) = (u8)(drive + 1);
            REG8(0x11A82FUL) = 0x00;
        }
        return;
    }

    if ((u16)(target + 0x000A) < REG16(0xFFFECEUL)) {
        drive = REG8(0xFFFEC9UL);
        if (drive <= 0x32) {
            hall_step_settle();
        } else {
            REG8(0xFFFEC9UL) = (u8)(drive - 1);
            REG8(0x11A82FUL) = 0x00;
        }
    }
}

/* ---- the motor-cycle test ---------------------------------------------
 * H'11A82F is the step of an eight-way sequence, and H'20B3D0 is what each
 * step means. Codes C0, D0, E0, F0 step one motor between its two positions
 * and code 03 walks the whole sequence.
 */
void motor_preset(u8 step)
{
    REG8(0x114DC6UL) |= 0x01;
    REG8(0x114DC6UL) |= 0x02;
    REG8(0x114DC6UL) |= 0x20;
    REG8(0x114DCDUL) |= 0x02;
    REG16(0x114DDAUL) = 0x0000;

    switch (step) {
    case 0x00:
        REG8(0xFFFED3UL) = 0x02; REG8(0x11A6B7UL) = 0x02;
        REG8(0xFFFEE4UL) = 0x00;
        break;
    case 0x01:
        REG8(0xFFFED5UL) = 0x02; REG8(0x11A6B8UL) = 0x02;
        REG8(0xFFFEE7UL) = 0x00;
        break;
    case 0x02:
        REG8(0xFFFED3UL) = 0x34; REG8(0x11A6B7UL) = 0x34;
        REG8(0xFFFEE4UL) = 0x64;
        break;
    case 0x03:
        REG8(0xFFFED5UL) = 0xD8; REG8(0x11A6B8UL) = 0xD8;
        REG8(0xFFFEE7UL) = 0x64;
        break;
    case 0x04:
        REG8(0xFFFED7UL) = 0x01; REG8(0x11A6B9UL) = 0x01;
        REG8(0x11A6BAUL) = 0x01; REG8(0xFFFEE4UL) = 0x00;
        break;
    case 0x05:
        REG8(0xFFFED1UL) = 0x01; REG8(0x11A6BBUL) = 0x01;
        REG8(0xFFFEE4UL) = 0x00;
        break;
    case 0x06:
        REG8(0xFFFED7UL) = 0x31; REG8(0x11A6B9UL) = 0x31;
        REG8(0x11A6BAUL) = 0x31; REG8(0xFFFEE7UL) = 0x64;
        break;
    case 0x07:
        REG8(0xFFFED1UL) = 0x28; REG8(0x11A6BBUL) = 0x28;
        REG8(0xFFFEE7UL) = 0x64;
        break;
    default:
        break;
    }
}

/* H'20B518, H'20B554, H'20B590, H'20B5CC. One motor between its pair of
 * steps, a quarter second at a time, and only while its ready byte reads 1.
 * H'114DDA is the tick counter and here too the test is inverted, so the
 * quarter second is not waited for. */
void motor_1_cycle(void)
{
    if (REG8(0xFFFED2UL) != 0x01) return;
    if (REG16(0x114DDAUL) < 0x00FA) return;

    REG8(0x11A82FUL) = (u8)((REG8(0x11A82FUL) >= 0x02) ? 0x00 : 0x02);
    motor_preset(REG8(0x11A82FUL));
}

void motor_2_cycle(void)
{
    if (REG8(0xFFFED4UL) != 0x01) return;
    if (REG16(0x114DDAUL) < 0x00FA) return;

    REG8(0x11A82FUL) = (u8)((REG8(0x11A82FUL) >= 0x03) ? 0x01 : 0x03);
    motor_preset(REG8(0x11A82FUL));
}

void motor_3_cycle(void)
{
    if (REG8(0xFFFED6UL) != 0x01) return;
    if (REG16(0x114DDAUL) < 0x00FA) return;

    REG8(0x11A82FUL) = (u8)((REG8(0x11A82FUL) >= 0x06) ? 0x04 : 0x06);
    motor_preset(REG8(0x11A82FUL));
}

void motor_4_cycle(void)
{
    if (REG8(0xFFFED0UL) != 0x01) return;
    if (REG16(0x114DDAUL) < 0x00FA) return;

    REG8(0x11A82FUL) = (u8)((REG8(0x11A82FUL) >= 0x07) ? 0x05 : 0x07);
    motor_preset(REG8(0x11A82FUL));
}

/* H'20B608. All four in turn, and only when all four are ready. A machine
 * with the embroidery module -- configuration byte H'B4 -- walks all eight
 * steps; without it, only the first four, which are the needle and feed. */
void motors_cycle_all(void)
{
    u8 step;

    if (REG8(0xFFFED2UL) != 0x01) return;
    if (REG8(0xFFFED4UL) != 0x01) return;
    if (REG8(0xFFFED6UL) != 0x01) return;
    if (REG8(0xFFFED0UL) != 0x01) return;
    if (REG16(0x114DDAUL) < 0x00FA) return;

    step = (u8)(REG8(0x11A82FUL) + 1);
    REG8(0x11A82FUL) = step;
    if (step >= ((REG8(0x57FF80UL) == 0xB4) ? 0x08 : 0x04)) {
        REG8(0x11A82FUL) = 0x00;
    }

    motor_preset(REG8(0x11A82FUL));
}

/* H'20C260 and its three neighbours. Wait for one motor to say it is ready,
 * then put it in state 5. Nothing bounds these: they spin until the stepper
 * interrupt writes the byte. */
void motor_1_wait_ready(void)
{
    while (REG8(0xFFFED2UL) != 0x01) { }
    REG8(0xFFFED2UL) = 0x05;
}

void motor_2_wait_ready(void)
{
    while (REG8(0xFFFED4UL) != 0x01) { }
    REG8(0xFFFED4UL) = 0x05;
}

void motor_3_wait_ready(void)
{
    while (REG8(0xFFFED6UL) != 0x01) { }
    REG8(0xFFFED6UL) = 0x05;
}

void motor_4_wait_ready(void)
{
    while (REG8(0xFFFED0UL) != 0x01) { }
    REG8(0xFFFED0UL) = 0x05;
}

/* ---- the foot lift ----------------------------------------------------
 * H'FFFEF3 and H'FFFEF4 are the two end stops of the presser-foot lift, and
 * they live in the EEPROM at H'A9 and H'AA. The calibration drives the foot
 * up until each one stops moving, and writes down where that was.
 */

/* H'20B686. Both stops zeroed, in RAM and in the EEPROM, and half a second
 * of settling with the six state bits held down. */
void foot_zero(void)
{
    REG8(0xFFFEF3UL) = 0x00;
    eeprom_write_verify(0xA9, 0x00);
    REG8(0xFFFEF4UL) = 0x00;
    eeprom_write_verify(0xAA, 0x00);

    REG8(0x11A831UL) = 0x00;
    REG8(0x11A832UL) = 0x00;
    REG16(0x114DE4UL) = 0x01F4;

    do {
        REG8(0x114DCEUL) &= (u8)~0x01;
        REG8(0x114DCEUL) &= (u8)~0x02;
        REG8(0x114DCEUL) &= (u8)~0x04;
        REG8(0x114DCEUL) &= (u8)~0x08;
        REG8(0x114DCEUL) &= (u8)~0x10;
        REG8(0x114DCEUL) &= (u8)~0x20;
        REG8(0x114DCDUL) |= 0x04;
    } while ((short)REG16(0x114DE4UL) >= 0);
}

/* H'20B702. Eight counts backed off each stop and written to the EEPROM.
 * A stop that came out below 8 or at H'FF is not a real end, and the beeper
 * says so before the numbers go down anyway. */
void foot_offset_apply(void)
{
    u8 v;

    if (!(REG8(0xFFFEF3UL) > 0x08 && REG8(0xFFFEF4UL) > 0x08 &&
          REG8(0xFFFEF3UL) < 0xFF && REG8(0xFFFEF4UL) < 0xFF)) {
        beep(0x0064, 0x00C8, 0x03);
    }

    v = REG8(0xFFFEF3UL);
    v = (u8)((v < 0x08) ? 0x00 : (v - 0x08));
    REG8(0xFFFEF3UL) = v;
    eeprom_write_verify(0xA9, v);

    v = REG8(0xFFFEF4UL);
    v = (u8)((v < 0x08) ? 0x00 : (v - 0x08));
    REG8(0xFFFEF4UL) = v;
    eeprom_write_verify(0xAA, v);

    REG8(0x114DCEUL) |= 0x20;
    REG8(0x114DCEUL) |= 0x04;
    REG8(0x114DCEUL) |= 0x01;
    REG8(0x114DCEUL) |= 0x02;
}

/* H'20B7AA. One step up on each stop that is still moving. Three passes with
 * no movement and that stop is called done -- bit 3 for the first, bit 4 for
 * the second. H'FFFEEA counts the steps for the screen, wrapping at ten. */
void foot_ramp(void)
{
    u8 moved = 0;
    u8 v;

    if (!(REG8(0x114DCEUL) & 0x01)) {
        v = REG8(0xFFFEF3UL);
        if (v < 0xFF) {
            v = (u8)(v + 1);
            moved = 0x01;
            REG8(0xFFFEF3UL) = v;
            eeprom_write_verify(0xA9, v);
            REG8(0x11A831UL) = 0x00;
        }
    } else {
        v = (u8)(REG8(0x11A831UL) + 1);
        REG8(0x11A831UL) = v;
        if (v >= 0x03) {
            REG8(0x114DCEUL) |= 0x08;
        } else {
            REG8(0x114DCEUL) &= (u8)~0x01;
            REG8(0x114DCEUL) &= (u8)~0x08;
        }
    }

    if (!(REG8(0x114DCEUL) & 0x02)) {
        v = REG8(0xFFFEF4UL);
        if (v < 0xFF) {
            v = (u8)(v + 1);
            moved = 0x01;
            REG8(0xFFFEF4UL) = v;
            eeprom_write_verify(0xAA, v);
            REG8(0x11A832UL) = 0x00;
        }
    } else {
        v = (u8)(REG8(0x11A832UL) + 1);
        REG8(0x11A832UL) = v;
        if (v >= 0x03) {
            REG8(0x114DCEUL) |= 0x10;
        } else {
            REG8(0x114DCEUL) &= (u8)~0x02;
            REG8(0x114DCEUL) &= (u8)~0x10;
        }
    }

    if (moved != 0) {
        const u8 n = REG8(0xFFFEEAUL);

        REG8(0xFFFEEAUL) = (u8)(n + 1);
        if (n >= 0x0A) REG8(0xFFFEEAUL) = 0x00;
    }
}

/* H'20B948. The calibration's state: ramp while there is somewhere to go,
 * and once both stops are done, back them off and write them. */
void foot_test_step(void)
{
    const u8 s = REG8(0x114DCEUL);

    /* Only all three together take the way out. One stop finished and the
     * other not falls through to the ramp, which is what keeps the second
     * one moving. */
    if ((s & 0x08) && (s & 0x10) && !(s & 0x20)) {
        foot_offset_apply();
        return;
    }

    if (REG8(0xFFFED4UL) != 0x01) {
        REG8(0x114DCEUL) &= (u8)~0x04;
        return;
    }

    if (!(REG8(0x114DCEUL) & 0x04) && !(REG8(0x114DCEUL) & 0x20)) {
        foot_ramp();
        REG8(0x114DCEUL) |= 0x04;
    }
}

/* ---- the tests themselves ---------------------------------------------- */

/* H'20BAFE. Code 01: sew at speed 1 until the panel changes. */
void service_sew_slow(void)
{
    REG16(0xFFFEE0UL) = 0x0001;
    REG8(0xFFFEFDUL) = 0x00;

    do {
        loop_tick();
        service_pass();
    } while (REG8(0xFFFEC5UL) == 0x01 || (REG8(0x114DC6UL) & 0x80));

    service_exit();
}

/* H'20BA72. Code 02: sew at H'3FC, and when it stops, the position the
 * needle came to rest at goes into the settings block -- but only if it is
 * H'10 or below, which is the range a correctly stopped needle lands in. */
void service_sew_fast(void)
{
    REG16(0xFFFEE0UL) = 0x03FC;
    REG8(0xFFFEFDUL) = 0x00;

    if (REG8(0x57FF80UL) == 0xAA) REG8(0xFFFEF9UL) = 0x20;

    do {
        loop_tick();
        service_pass();
    } while (REG8(0xFFFEC5UL) == 0x02 || (REG8(0x114DC6UL) & 0x80));

    if (REG8(0xFFFED8UL) <= 0x10) {
        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0x00FFFED8UL, 0x0057FF8EUL, 1);
        FLASH_BUSY &= (u8)~0x20;
    }

    service_exit();
}

/* H'20BC7C. Code 03: the eight-step motor sequence. */
void service_motor_cycle(void)
{
    while (REG8(0xFFFEC5UL) == 0x03) {
        motors_cycle_all();
        loop_tick();
    }
    service_exit();
}

/* H'20BBF4. Code 04: each motor waited for in turn and then parked at its
 * home position. The gaps between them are register spin loops -- the count
 * starts from whatever the caller left in R6H, so only the first of the four
 * runs at all, and even that one is at most 250 turns. */
void service_motor_home(void)
{
    volatile u8 spin = 0;

    while (spin < 0xFA) spin++;
    motor_1_wait_ready();
    REG8(0xFFFED3UL) = 0x1A; REG8(0x11A6B7UL) = 0x1A;

    while (spin < 0xFA) spin++;
    motor_2_wait_ready();
    REG8(0xFFFED5UL) = 0x00; REG8(0x11A6B8UL) = 0x00;

    while (spin < 0xFA) spin++;
    motor_3_wait_ready();
    REG8(0xFFFED7UL) = 0x19; REG8(0x11A6B9UL) = 0x19; REG8(0x11A6BAUL) = 0x19;

    while (spin < 0xFA) spin++;
    motor_4_wait_ready();
    REG8(0xFFFED1UL) = 0x19; REG8(0x11A6BBUL) = 0x19;

    while (REG8(0xFFFEC5UL) == 0x04) loop_tick();

    service_exit();
}

/* H'20AF5A. The hall window out of the settings block.
 *
 * The byte goes into the high half of the word and is shifted there, so what
 * comes out is the byte times four times H'100 -- and the two bits that
 * shift off the top of the byte are lost, which for a setting above H'3F
 * makes the window wrap rather than grow. */
void hall_window_set(void)
{
    REG16(0x114DE4UL) = (u16)((u16)(u8)(REG8(0x57FF8CUL) << 2) << 8);
}

/* H'20BB36. Code 08: the hall trim, running while the pedal is down. */
void service_hall_trim(void)
{
    REG8(0x114DC6UL) |= 0x01;
    REG8(0x114DC6UL) |= 0x02;
    REG8(0x114DC6UL) |= 0x20;
    REG8(0x114DCDUL) |= 0x02;
    REG8(0x114DC6UL) &= (u8)~0x80;
    REG8(0xFFFEC7UL) |=        0x80;
    REG8(0x114DC8UL) |=        0x20;

    hall_window_set();

    do {
        loop_tick();

        if (REG8(0xFFFEC3UL) != 0) {
            if (!(REG8(0x114DC8UL) & 0x80) &&
                (short)REG16(0x114DE4UL) <= 0x0064) {
                hall_trim();
            }
        } else {
            REG8(0x114DC8UL) &= (u8)~0x80;
            REG8(0x11A82FUL) = 0x00;
            hall_window_set();
        }

        service_pass();
    } while (REG8(0xFFFEC5UL) == 0x08);

    REG8(0xFFFEC7UL) &= (u8)~0x80;
    REG8(0x114DC8UL) &= (u8)~0x20;
    REG8(0x114DC6UL) &= (u8)~0x80;

    speed_control_init();
    main_motor_service();
    service_exit();
}

/* H'20AEEE. Code 09: analog channels 4 and 6 on the screen, over and over.
 * Both are scaled by H'64 / H'100, and the raw byte is left in the two
 * foot-lift bytes -- which is what the screen draws from. */
void service_analog_show(void)
{
    do {
        u8 v;

        v = adc_get_result(4);
        REG8(0xFFFEF0UL) = v;
        REG8(0xFFFEE7UL) = (u8)((u16)(0x64 * (u16)v) / 0x0100);

        v = adc_get_result(6);
        REG8(0xFFFEF1UL) = v;
        REG8(0xFFFEE4UL) = (u8)((u16)(0x64 * (u16)v) / 0x0100);

        loop_tick();
    } while (REG8(0xFFFEC5UL) == 0x09);

    service_exit();
}

/* H'20AF92. Code 0A: run and watch the hall reading, holding it inside the
 * window. H'114DE4 counting past H'1D4C without the reading arriving is
 * what raises bit 7 of H'114DC8. */
void service_hall_watch(void)
{
    do {
        loop_tick();
        service_pass();

        if ((short)REG16(0x114DE4UL) > 0) {
            if ((short)REG16(0x114DE4UL) <= 0x1D4C) {
                REG8(0x114DC8UL) |= 0x80;
            }
        } else {
            REG16(0x114DE4UL) = 0x3A98;
            REG8(0x114DC8UL) &= (u8)~0x80;
        }
    } while (REG8(0xFFFEC5UL) == 0x0A || (REG8(0x114DC6UL) & 0x80));

    service_exit();
}

/* H'20AF76. Code 0D: the module link, over and over. */
void service_module_test(void)
{
    do {
        loop_tick();
        service_module_pass();
    } while (REG8(0xFFFEC5UL) == 0x0D);

    service_exit();
}

/* H'20B99C. Code 0F: the foot-lift calibration. The two end stops are put on
 * the screen when it finishes, and written into the settings block. */
void service_foot_calibrate(void)
{
    foot_zero();

    do {
        loop_tick();
        motor_2_cycle();
        foot_test_step();

        REG8(0xFFFEE4UL) =
            (u8)((u16)(0x64 * (u16)REG8(0xFFFEF3UL)) / 0x0100);
        REG8(0xFFFEE7UL) =
            (u8)((u16)(0x64 * (u16)REG8(0xFFFEF4UL)) / 0x0100);
    } while (REG8(0xFFFEC5UL) == 0x0F && !(REG8(0x114DCEUL) & 0x20));

    number_draw(REG8(0xFFFEF3UL), 0x00B5, 0x00A2);
    number_draw(REG8(0xFFFEF4UL), 0x00CE, 0x00A2);

    REG8(0xFFFEE4UL) = (u8)((u16)(0x64 * (u16)REG8(0xFFFEF3UL)) / 0x0100);
    REG8(0xFFFEE7UL) = (u8)((u16)(0x64 * (u16)REG8(0xFFFEF4UL)) / 0x0100);

    foot_calibration_save();
    REG8(0xFFFEC5UL) = 0x00;

    service_exit();
}

/* H'20BC9A. One pass of the service loop: a tick, the readings, and then
 * whichever test the panel is asking for. The two tables are laid out so
 * that the index into the handlers counts down as the key search counts up,
 * which is why the handler list is in the reverse order of the keys.
 *
 * Codes 80 to 84 are in the key table with handlers that do nothing at all,
 * and so is 0E, whose handler is a bare service_tick. */
void service_dispatch(void)
{
    loop_tick();
    service_readings();

    switch (REG8(0xFFFEC5UL)) {
    case 0x01: service_sew_slow();       break;
    case 0x02: service_sew_fast();       break;
    case 0x03: service_motor_cycle();    break;
    case 0x04: service_motor_home();     break;
    case 0x08: service_hall_trim();      break;
    case 0x09: service_analog_show();    break;
    case 0x0A: service_hall_watch();     break;
    case 0x0D: service_module_test();    break;
    case 0x0E: loop_tick();           break;
    case 0x0F: service_foot_calibrate(); break;

    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: break;

    case 0xB1: motors_off_stepped(); break;
    case 0xB2: motors_pos_FC();      break;
    case 0xB3: motors_pos_FD();      break;
    case 0xB4: motors_pos_FE();      break;
    case 0xB5: motors_pos_FF();      break;

    case 0xC0: motor_1_cycle();  break;
    case 0xC1: motor_1_off();    break;
    case 0xC2: motor_1_pos_FC(); break;
    case 0xC3: motor_1_pos_FD(); break;
    case 0xC4: motor_1_pos_FE(); break;
    case 0xC5: motor_1_pos_FF(); break;

    case 0xD0: motor_2_cycle();  break;
    case 0xD1: motor_2_off();    break;
    case 0xD2: motor_2_pos_FC(); break;
    case 0xD3: motor_2_pos_FD(); break;
    case 0xD4: motor_2_pos_FE(); break;
    case 0xD5: motor_2_pos_FF(); break;

    case 0xE0: motor_3_cycle();  break;
    case 0xE1: motor_3_off();    break;
    case 0xE2: motor_3_pos_FC(); break;
    case 0xE3: motor_3_pos_FD(); break;
    case 0xE4: motor_3_pos_FE(); break;
    case 0xE5: motor_3_pos_FF(); break;

    case 0xF0: motor_4_cycle();  break;
    case 0xF1: motor_4_off();    break;
    case 0xF2: motor_4_pos_FC(); break;
    case 0xF3: motor_4_pos_FD(); break;
    case 0xF4: motor_4_pos_FE(); break;
    case 0xF5: motor_4_pos_FF(); break;

    default: break;
    }
}

/* ---- the two main loops -----------------------------------------------
 * H'208E10, and it does not return.
 *
 * H'FFFEC4 bit 7 is set at power-on if the service key was held, and picks
 * which of the two loops the machine spends the rest of its life in.
 */
#define SERVICE_MODE_FLAG   REG8(0xFFFEC4UL)

/* H'20BEE2. */
void main_loop_service(void)
{
    loop_tick();
    service_dispatch();
}

/* H'2086B6. */
void main_loop_normal(void)
{
    embroidery_service();
    key_and_diag();
    screen_dispatch();
    service_tick();          /* H'208698 */
}

void app_init(void)
{
    machine_init();

    if (SERVICE_MODE_FLAG & 0x80) {
        for (;;) {
            main_loop_service();
        }
    }
    for (;;) {
        main_loop_normal();
    }
}

/* H'2007B0. Not the main loop -- that is app_init, which never returns.
 * This is the trap the entry jumps to if it ever does, and in the original
 * it is a single instruction branching to itself. Reproduced exactly. */
void app_main(void)
{
    for (;;) {
    }
}

/* The 'G' command's target, H'2003A4. The original sends an 'O' and carries
 * on, which is how the download protocol's caller knows the handover
 * happened. Kept, because the boot ROM's tests check for it. */
void alt_entry(void)
{
    while (!(SSR1 & SSR_TDRE)) { }
    TDR1 = 'O';
    SSR1 = (u8)~SSR_TDRE;
    app_main();
}

/* The two output latches. Both are write-only and both carry more than one
 * thing, so every change is made on the shadow. The pair of writes is not a
 * mistake: the coil enables come up one bit at a time, and the intermediate
 * value goes out to the latch as well. */
static void latch_a_on(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD38UL) | first);
    REG8(0x0A0000UL) = v;
    v = (u8)(v | second);
    REG8(0x0A0000UL) = v;
    REG8(0xFFFD38UL) = v;
}

static void latch_a_off(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD38UL) & ~first);
    REG8(0x0A0000UL) = v;
    v = (u8)(v & ~second);
    REG8(0x0A0000UL) = v;
    REG8(0xFFFD38UL) = v;
}

static void latch_a_bit(u8 mask, int on)
{
    const u8 v = on ? (u8)(REG8(0xFFFD38UL) | mask)
                    : (u8)(REG8(0xFFFD38UL) & ~mask);
    REG8(0x0A0000UL) = v;
    REG8(0xFFFD38UL) = v;
}

static void latch_b_on(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD39UL) | first);
    REG8(0x0C0000UL) = v;
    v = (u8)(v | second);
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

static void latch_b_off(u8 first, u8 second)
{
    u8 v = (u8)(REG8(0xFFFD39UL) & ~first);
    REG8(0x0C0000UL) = v;
    v = (u8)(v & ~second);
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

/* ---- the machine's timebase and its steppers ---------------------------
 * Seven timer interrupt handlers, and the forty routines under them. Nothing
 * in the application waits on anything without these: H'114DDA, H'114DDE,
 * H'114DE0, H'114DE2 and H'114DE4 are all counted here, and so is every step
 * of every stepper motor.
 *
 * The phase patterns live as data in the code region, from H'250804 to
 * H'250AA0, and are read where they lie -- like the pattern data, they are
 * above the rebuilt image and mergeapp leaves them alone.
 *
 * Two output latches carry the coil drives: H'0A0000, shadowed at H'FFFD38,
 * and H'0C0000 at H'FFFD39. Both are write-only, so a change is always made
 * on the shadow and then written out.
 */

/* ---- the four steppers, one phase at a time ---------------------------
 * Each motor has a phase index counted 0..7 (0..3 for the fourth), a
 * position counter, and a table of coil patterns. The "_back" form steps the
 * index down and the "_on" form steps it up; which one runs is a direction
 * bit in the motor's flag byte.
 */

/* H'20C2C8. Motor 1 -- the needle position -- in the high nibble of the
 * TPC output at H'FFFFA5. */
void motor_1_step(void)
{
    u8 phase;

    if (REG8(0x11A835UL) & 0x01) {
        REG8(0x11A83AUL) = (u8)(REG8(0x11A83AUL) + 1);
        phase = REG8(0x11A838UL);
        REG8(0x11A838UL) = (u8)((phase == 0) ? 0x07 : (phase - 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x25080CUL + REG8(0x11A838UL)));
    } else {
        REG8(0x11A83AUL) = (u8)(REG8(0x11A83AUL) - 1);
        phase = REG8(0x11A838UL);
        REG8(0x11A838UL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x250804UL + REG8(0x11A838UL)));
    }
}

/* H'20C550. Motor 1 forward only, from the third table. */
void motor_1_step_home(void)
{
    const u8 phase = REG8(0x11A838UL);

    REG8(0x11A838UL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
    REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
    REG8(0xFFFFA5UL) =
        (u8)(REG8(0xFFFFA5UL) | REG8(0x250814UL + REG8(0x11A838UL)));
}

/* H'20C36C. Motor 2 -- the feed -- in the low nibble of the same latch. Its
 * position counter moves by two, not one. */
void motor_2_step(void)
{
    u8 phase;

    if (REG8(0x11A835UL) & 0x08) {
        REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) + 2);
        phase = REG8(0x11A83EUL);
        REG8(0x11A83EUL) = (u8)((phase == 0) ? 0x07 : (phase - 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x250924UL + REG8(0x11A83EUL)));
    } else {
        REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) - 2);
        phase = REG8(0x11A83EUL);
        REG8(0x11A83EUL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
        REG8(0xFFFFA5UL) =
            (u8)(REG8(0xFFFFA5UL) | REG8(0x25091CUL + REG8(0x11A83EUL)));
    }
}

/* H'20C596. Motor 2 forward only. */
void motor_2_step_home(void)
{
    const u8 phase = REG8(0x11A83EUL);

    REG8(0x11A83EUL) = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
    REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
    REG8(0xFFFFA5UL) =
        (u8)(REG8(0xFFFFA5UL) | REG8(0x25092CUL + REG8(0x11A83EUL)));
}

/* H'20C410. Motor 3 -- the hook width -- has the whole of H'FFFFA4. */
void motor_3_step(void)
{
    u8 phase;

    if (REG8(0x11A836UL) & 0x01) {
        REG8(0x11A846UL) = (u8)(REG8(0x11A846UL) + 1);
        phase = REG8(0x11A844UL);
        phase = (u8)((phase == 0) ? 0x07 : (phase - 1));
        REG8(0x11A844UL) = phase;
        REG8(0xFFFFA4UL) = REG8(0x250A24UL + phase);
    } else {
        REG8(0x11A846UL) = (u8)(REG8(0x11A846UL) - 1);
        phase = REG8(0x11A844UL);
        phase = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
        REG8(0x11A844UL) = phase;
        REG8(0xFFFFA4UL) = REG8(0x250A1CUL + phase);
    }
}

/* H'20C5DC. */
void motor_3_step_home(void)
{
    u8 phase = REG8(0x11A844UL);

    phase = (u8)((phase >= 0x07) ? 0x00 : (phase + 1));
    REG8(0x11A844UL) = phase;
    REG8(0xFFFFA4UL) = REG8(0x250A2CUL + phase);
}

/* H'20C498. Motor 4 -- the hook position -- is four phases, not eight, and
 * its drive is the low six bits of the H'0C0000 latch. */
void motor_4_step(void)
{
    u8 phase, v;

    if (REG8(0x11A836UL) & 0x08) {
        REG8(0x11A84BUL) = (u8)(REG8(0x11A84BUL) + 1);
        phase = REG8(0x11A849UL);
        REG8(0x11A849UL) = (u8)((phase == 0) ? 0x03 : (phase - 1));
        v = (u8)(REG8(0xFFFD39UL) & 0xC0);
        v = (u8)(v | REG8(0x250A90UL + REG8(0x11A849UL)));
    } else {
        REG8(0x11A84BUL) = (u8)(REG8(0x11A84BUL) - 1);
        phase = REG8(0x11A849UL);
        REG8(0x11A849UL) = (u8)((phase >= 0x03) ? 0x00 : (phase + 1));
        v = (u8)(REG8(0xFFFD39UL) & 0xC0);
        v = (u8)(v | REG8(0x250A8CUL + REG8(0x11A849UL)));
    }
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

/* H'20C614. */
void motor_4_step_home(void)
{
    const u8 phase = REG8(0x11A849UL);
    u8 v;

    REG8(0x11A849UL) = (u8)((phase >= 0x03) ? 0x00 : (phase + 1));
    v = (u8)(REG8(0xFFFD39UL) & 0xC0);
    v = (u8)(v | REG8(0x250A94UL + REG8(0x11A849UL)));
    REG8(0x0C0000UL) = v;
    REG8(0xFFFD39UL) = v;
}

/* ---- ITU0: motors 1 and 2 ---------------------------------------------
 * H'20C664. One timer, two motors, taken in turn: while bit 7 of H'11A835 is
 * clear the interrupt belongs to the needle-position motor, and once that
 * has reached its target the bit goes up and the same interrupt drives the
 * feed motor instead.
 *
 * Both run the same shape. A move is split in half -- H'11A83B/H'11A83C for
 * one, H'11A841/H'11A842 for the other -- and the phase index walks up
 * through an acceleration table for the first half and back down through a
 * deceleration table for the second, so the motor ramps up and down inside
 * one move. A move shorter than the ramp sets a flag that holds the index
 * still. GRB0 at H'FFFF6A is the interval to the next step.
 *
 * Targets H'FC to H'FF are not positions but park commands: hold, coast,
 * and two fixed patterns.
 */
static u16 phase_word(u32 table, u8 phase)
{
    return REG16(table + (u32)(long)(short)(u16)((u16)phase << 1));
}

void isr_motors_12_body(void)
{
    u8 pos, tgt, d, n, v;

    REG8(0xFFFF67UL) &= (u8)~0x01;

    if (!(REG8(0x11A835UL) & 0x80)) {
        /* ---- the needle-position motor ---- */
        switch (REG8(0xFFFED2UL)) {
        case 0x01:
            pos = REG8(0x11A83AUL);
            tgt = REG8(0xFFFED3UL);
            if (pos == tgt) {
                REG8(0x11A835UL) |= 0x80;
                REG16(0xFFFF6AUL) = 0x2AF8;
            } else if (tgt == 0xFF) {
                REG8(0x11A835UL) |= 0x80;
                latch_a_on(0x20, 0x40);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25081EUL));
            } else if (tgt == 0xFE) {
                /* coast */
            } else if (tgt == 0xFD) {
                REG8(0x11A835UL) |= 0x80;
                latch_a_off(0x20, 0x40);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25081DUL));
            } else if (tgt == 0xFC) {
                REG8(0x11A835UL) |= 0x80;
                latch_a_off(0x20, 0x40);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
                REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25081CUL));
            } else {
                if (pos > tgt) {
                    REG8(0x11A835UL) &= (u8)~0x01;
                    REG8(0x11A837UL) = (u8)(REG8(0x11A83AUL) - tgt);
                } else {
                    REG8(0x11A835UL) |= 0x01;
                    REG8(0x11A837UL) = (u8)(tgt - REG8(0x11A83AUL));
                }

                d = REG8(0x11A837UL);
                if (d < 0x0E) {
                    REG8(0x11A835UL) |= 0x04;
                } else if (d <= 0x20) {
                    REG8(0x11A83DUL) = 0x01;
                }

                d = REG8(0x11A837UL);
                if (d == 0x01)      REG8(0xFFFED2UL) = 0x03;
                else if (d == 0x02) REG8(0xFFFED2UL) = 0x02;
                else                REG8(0xFFFED2UL) = 0x02;

                d = REG8(0x11A837UL);
                REG8(0x11A83BUL) = (u8)(d >> 1);
                REG8(0x11A83CUL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
                REG8(0x11A839UL) = 0x01;
                motor_1_step();
                REG16(0xFFFF6AUL) = (u16)(REG16(0xFFFF68UL) + 0x0CF6);
                latch_a_off(0x20, 0x40);
            }
            break;

        case 0x02:
            REG16(0xFFFF6AUL) = phase_word(
                (REG8(0x11A83DUL) == 0) ? 0x250820UL : 0x2508A0UL,
                REG8(0x11A839UL));
            motor_1_step();
            REG8(0x11A83BUL) = (u8)(REG8(0x11A83BUL) - 1);
            if (REG8(0x11A83BUL) != 0) {
                if (!(REG8(0x11A835UL) & 0x04)) {
                    REG8(0x11A839UL) = (u8)(REG8(0x11A839UL) + 1);
                }
            } else {
                REG8(0xFFFED2UL) = 0x03;
            }
            break;

        case 0x03:
            REG8(0x11A83CUL) = (u8)(REG8(0x11A83CUL) - 1);
            if (REG8(0x11A83CUL) != 0) {
                REG16(0xFFFF6AUL) = phase_word(
                    (REG8(0x11A83DUL) == 0) ? 0x25085EUL : 0x2508DEUL,
                    REG8(0x11A839UL));
                motor_1_step();
                if (!(REG8(0x11A835UL) & 0x04)) {
                    REG8(0x11A839UL) = (u8)(REG8(0x11A839UL) - 1);
                }
            } else {
                REG16(0xFFFF6AUL) = REG16(0x250860UL);
                REG8(0xFFFED2UL) = 0x04;
            }
            break;

        case 0x04:
            REG16(0xFFFF6AUL) = REG16(0x25085EUL);
            REG8(0x11A83DUL) = 0x00;
            REG8(0x11A835UL) &= (u8)~0x04;
            latch_a_on(0x20, 0x40);
            REG8(0xFFFED2UL) = 0x01;
            break;

        case 0x05:
            REG16(0xFFFF6AUL) = 0x2AF8;
            REG8(0x11A83CUL) = 0x00;
            REG8(0x11A83BUL) = 0x00;
            REG8(0x11A83AUL) = 0x00;
            REG8(0x11A835UL) = 0x00;
            REG8(0x11A839UL) = 0x00;
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0x0F);
            REG8(0x11A838UL) = 0x00;
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x25080CUL));
            latch_a_on(0x20, 0x40);
            REG8(0xFFFED2UL) = 0x00;
            break;

        case 0x06:
            REG16(0xFFFF6AUL) = REG16(0x250820UL);
            if (REG8(0x11A83BUL) < 0x0A) {
                REG8(0x11A83BUL) = (u8)(REG8(0x11A83BUL) + 1);
            } else {
                REG8(0x11A83BUL) = 0x00;
                REG8(0xFFFED2UL) = 0x01;
                REG8(0x11A839UL) = 0x01;
            }
            break;

        case 0x00:
            /* Homing: eight phases at a time until the counter runs out. */
            latch_a_on(0x20, 0x40);
            if (REG8(0x11A83BUL) < 0x07) {
                if (REG8(0x11A83CUL) < 0x08) {
                    motor_1_step_home();
                    REG8(0x11A83CUL) = (u8)(REG8(0x11A83CUL) + 1);
                    REG8(0x11A839UL) = 0x00;
                } else {
                    REG8(0x11A839UL) = (u8)(REG8(0x11A839UL) + 1);
                    if (REG8(0x11A839UL) >= 0x08) {
                        REG8(0x11A83CUL) = 0x00;
                        REG8(0x11A83BUL) = (u8)(REG8(0x11A83BUL) + 1);
                    }
                }
                REG16(0xFFFF6AUL) = REG16(0x250820UL);
            } else {
                REG8(0x11A83CUL) = 0x00;
                REG8(0x11A83BUL) = 0x00;
                REG8(0x11A83AUL) = 0x00;
                REG8(0xFFFED2UL) = 0x06;
                REG8(0x11A839UL) = 0x01;
            }
            break;

        default:
            REG8(0xFFFED2UL) = 0x01;
            break;
        }
        return;
    }

    /* ---- the feed motor ---- */
    if (REG8(0xFFFED4UL) > 0x09) {
        REG8(0xFFFED4UL) = 0x01;
        return;
    }

    switch (REG8(0xFFFED4UL)) {
    case 0x01:
        pos = REG8(0x11A840UL);
        tgt = REG8(0xFFFED5UL);
        if (pos == tgt) {
            REG8(0x11A835UL) &= (u8)~0x80;
            REG16(0xFFFF6AUL) = 0x2AF8;
        } else if (tgt == 0xFF) {
            REG8(0x11A835UL) &= (u8)~0x80;
            latch_a_off(0x08, 0x10);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250936UL));
        } else if (tgt == 0xFE) {
            /* coast */
        } else if (tgt == 0xFD) {
            REG8(0x11A835UL) &= (u8)~0x80;
            latch_a_off(0x08, 0x10);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250935UL));
        } else if (tgt == 0xFC) {
            REG8(0x11A835UL) &= (u8)~0x80;
            latch_a_off(0x08, 0x10);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
            REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250934UL));
        } else {
            /* An odd position is snapped onto an even one first: the low
             * nibble of where the motor *is*, not of where it is going. */
            n = (u8)(pos & 0x0F);
            if (n == 0x01 || n == 0x05 || n == 0x09 || n == 0x0D) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) - 1);
            } else if (n == 0x03 || n == 0x07 || n == 0x0B || n == 0x0F) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) + 1);
            }

            latch_a_off(0x08, 0x10);

            /* Half the distance, because the feed steps by two. */
            if (REG8(0x11A840UL) > tgt) {
                REG8(0x11A835UL) &= (u8)~0x08;
                REG8(0x11A837UL) =
                    (u8)((short)((short)(u16)REG8(0x11A840UL) -
                                 (short)(u16)tgt) / 2);
            } else {
                REG8(0x11A835UL) |= 0x08;
                REG8(0x11A837UL) =
                    (u8)((short)((short)(u16)tgt -
                                 (short)(u16)REG8(0x11A840UL)) / 2);
            }

            d = REG8(0x11A837UL);
            if (d < 0x1E) {
                REG8(0x11A835UL) |= 0x10;
            } else if (d <= 0x3C) {
                REG8(0x11A843UL) = 0x01;
            }

            if (REG8(0xFFFED5UL) & 0x01) {
                REG8(0x11A835UL) |= 0x20;
                if (REG8(0xFFFED5UL) & 0x02) {
                    if (REG8(0x11A835UL) & 0x08) {
                        REG8(0x11A837UL) = (u8)(REG8(0x11A837UL) + 1);
                    }
                } else {
                    if (!(REG8(0x11A835UL) & 0x08)) {
                        REG8(0x11A837UL) = (u8)(REG8(0x11A837UL) + 1);
                    }
                }
            } else {
                REG8(0x11A835UL) &= (u8)~0x20;
            }

            /* R5 is H'0002 from the divide, so this tests against 0 and
             * then against 2, and the last two arms are the same. */
            d = REG8(0x11A837UL);
            if (d == 0x00)      REG8(0xFFFED4UL) = 0x04;
            else if (d == 0x01) REG8(0xFFFED4UL) = 0x03;
            else if (d == 0x02) REG8(0xFFFED4UL) = 0x02;
            else                REG8(0xFFFED4UL) = 0x02;

            d = REG8(0x11A837UL);
            REG8(0x11A841UL) = (u8)(d >> 1);
            REG8(0x11A842UL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
            REG8(0x11A83FUL) = 0x01;
            if (REG8(0x11A837UL) != 0) motor_2_step();
            REG16(0xFFFF6AUL) = (u16)(REG16(0xFFFF68UL) + 0x0CF6);
        }
        break;

    case 0x02:
        REG16(0xFFFF6AUL) = phase_word(
            (REG8(0x11A843UL) == 0) ? 0x250938UL : 0x2509B6UL,
            REG8(0x11A83FUL));
        motor_2_step();
        REG8(0x11A841UL) = (u8)(REG8(0x11A841UL) - 1);
        if (REG8(0x11A841UL) != 0) {
            if (!(REG8(0x11A835UL) & 0x10)) {
                REG8(0x11A83FUL) = (u8)(REG8(0x11A83FUL) + 1);
            }
        } else {
            REG8(0xFFFED4UL) = 0x03;
        }
        break;

    case 0x03:
        REG16(0xFFFF6AUL) = phase_word(
            (REG8(0x11A843UL) == 0) ? 0x250938UL : 0x2509B6UL,
            REG8(0x11A83FUL));
        REG8(0x11A842UL) = (u8)(REG8(0x11A842UL) - 1);
        if (REG8(0x11A842UL) != 0) {
            motor_2_step();
            if (!(REG8(0x11A835UL) & 0x10)) {
                REG8(0x11A83FUL) = (u8)(REG8(0x11A83FUL) - 1);
            }
        } else if (REG8(0xFFFED5UL) > 0x04) {
            REG8(0xFFFED4UL) = 0x08;
        } else {
            REG8(0xFFFED4UL) = 0x04;
        }
        break;

    case 0x08:
        REG16(0xFFFF6AUL) = REG16(0x25093AUL);
        motor_2_step();
        REG8(0xFFFED4UL) = 0x09;
        break;

    case 0x09:
        REG16(0xFFFF6AUL) = REG16(0x25093AUL);
        if (REG8(0x11A835UL) & 0x08) REG8(0x11A835UL) &= (u8)~0x08;
        else                         REG8(0x11A835UL) |=        0x08;
        motor_2_step();
        REG8(0xFFFED4UL) = 0x04;
        break;

    case 0x04:
        REG16(0xFFFF6AUL) = 0x2AF8;
        v = REG8(0xFFFED5UL);
        if (!(v & 0x01)) {
            if (v & 0x02) latch_a_on(0x08, 0x10);
        } else {
            n = (u8)(v & 0x0F);
            if (n == 0x01 || n == 0x07) {
                v = (u8)(REG8(0xFFFFD3UL) | 0x01);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            } else if (n == 0x09 || n == 0x0F) {
                v = (u8)(REG8(0xFFFFD3UL) & ~0x01);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            } else if (n == 0x03 || n == 0x0D) {
                v = (u8)(REG8(0xFFFFD3UL) & ~0x04);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            } else if (n == 0x05 || n == 0x0B) {
                v = (u8)(REG8(0xFFFFD3UL) | 0x04);
                REG8(0xFFFFD3UL) = v; REG8(0xFFFFA5UL) = v;
            }

            n = (u8)(REG8(0xFFFED5UL) & 0x0F);
            if (n == 0x01 || n == 0x05 || n == 0x09 || n == 0x0D) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) + 1);
            } else if (n == 0x03 || n == 0x07 || n == 0x0B || n == 0x0F) {
                REG8(0x11A840UL) = (u8)(REG8(0x11A840UL) - 1);
            }
            latch_a_on(0x08, 0x10);
        }
        REG8(0xFFFED4UL) = 0x01;
        REG8(0x11A83FUL) = 0x01;
        REG8(0x11A843UL) = 0x00;
        REG8(0x11A835UL) &= (u8)~0x10;
        REG8(0x11A835UL) &= (u8)~0x20;
        break;

    case 0x05:
        REG16(0xFFFF6AUL) = 0x2AF8;
        REG8(0x11A842UL) = 0x00;
        REG8(0x11A841UL) = 0x00;
        REG8(0x11A840UL) = 0x00;
        REG8(0x11A835UL) = 0x00;
        REG8(0x11A83EUL) = 0x00;
        REG8(0x11A83FUL) = 0x00;
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) & 0xF0);
        REG8(0xFFFFA5UL) = (u8)(REG8(0xFFFFA5UL) | REG8(0x250924UL));
        latch_a_on(0x08, 0x10);
        REG8(0xFFFED4UL) = 0x00;
        break;

    case 0x06:
        REG16(0xFFFF6AUL) = REG16(0x250938UL);
        if (REG8(0x11A841UL) < 0x32) {
            REG8(0x11A841UL) = (u8)(REG8(0x11A841UL) + 1);
        } else {
            REG8(0x11A841UL) = 0x00;
            REG8(0x11A83FUL) = 0x01;
            REG8(0xFFFED4UL) = 0x04;
        }
        break;

    case 0x00:
        if (REG8(0x11A841UL) < 0x0F) {
            if (REG8(0x11A842UL) < 0x08) {
                motor_2_step_home();
                REG8(0x11A842UL) = (u8)(REG8(0x11A842UL) + 1);
                REG8(0x11A83FUL) = 0x00;
            } else {
                REG8(0x11A83FUL) = (u8)(REG8(0x11A83FUL) + 1);
                if (REG8(0x11A83FUL) >= 0x08) {
                    REG8(0x11A842UL) = 0x00;
                    REG8(0x11A841UL) = (u8)(REG8(0x11A841UL) + 1);
                }
            }
            REG16(0xFFFF6AUL) = REG16(0x250938UL);
        } else {
            REG8(0x11A842UL) = 0x00;
            REG8(0x11A841UL) = 0x00;
            REG8(0x11A840UL) = 0x00;
            REG8(0x11A83FUL) = 0x01;
            REG8(0xFFFED4UL) = 0x06;
        }
        break;

    default:
        REG8(0xFFFED4UL) = 0x01;
        break;
    }
}

/* ---- ITU1: motor 3, the hook width -------------------------------------
 * H'20D17A. The same shape as the two above, with one motor to itself. Its
 * drive is the whole of H'FFFFA4 and its enables are bits 6 and 7 of the
 * H'0C0000 latch. */
void isr_motor_3_body(void)
{
    u8 pos, tgt, d;

    REG8(0xFFFF71UL) &= (u8)~0x01;

    switch (REG8(0xFFFED6UL)) {
    case 0x01:
        pos = REG8(0x11A846UL);
        tgt = REG8(0xFFFED7UL);
        if (pos == tgt) {
            REG16(0xFFFF74UL) = 0x2AF8;
        } else if (tgt == 0xFF) {
            latch_b_on(0x40, 0x80);
            REG8(0xFFFFA4UL) = REG8(0x250A36UL);
        } else if (tgt == 0xFE) {
            /* coast */
        } else if (tgt == 0xFD) {
            latch_b_off(0x40, 0x80);
            REG8(0xFFFFA4UL) = REG8(0x250A35UL);
        } else if (tgt == 0xFC) {
            latch_b_off(0x40, 0x80);
            REG8(0xFFFFA4UL) = REG8(0x250A34UL);
        } else {
            if (pos > tgt) {
                REG8(0x11A836UL) &= (u8)~0x01;
                REG8(0x11A837UL) = (u8)(REG8(0x11A846UL) - tgt);
            } else {
                REG8(0x11A836UL) |= 0x01;
                REG8(0x11A837UL) = (u8)(tgt - REG8(0x11A846UL));
            }

            if (REG8(0x11A837UL) < 0x14) REG8(0x11A836UL) |= 0x04;

            d = REG8(0x11A837UL);
            if (d == 0x01)      REG8(0xFFFED6UL) = 0x03;
            else if (d == 0x02) REG8(0xFFFED6UL) = 0x02;
            else                REG8(0xFFFED6UL) = 0x02;

            d = REG8(0x11A837UL);
            REG8(0x11A847UL) = (u8)(d >> 1);
            REG8(0x11A848UL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
            REG8(0x11A845UL) = 0x01;
            motor_3_step();
            REG16(0xFFFF74UL) = (u16)(REG16(0xFFFF72UL) + 0x0CF6);
            latch_b_off(0x40, 0x80);
        }
        break;

    case 0x02:
        REG16(0xFFFF74UL) = phase_word(0x250A38UL, REG8(0x11A845UL));
        motor_3_step();
        REG8(0x11A847UL) = (u8)(REG8(0x11A847UL) - 1);
        if (REG8(0x11A847UL) != 0) {
            REG8(0x11A845UL) = (u8)(REG8(0x11A845UL) + 1);
        } else {
            REG8(0xFFFED6UL) = 0x03;
        }
        break;

    case 0x03:
        REG16(0xFFFF74UL) = phase_word(0x250A38UL, REG8(0x11A845UL));
        REG8(0x11A848UL) = (u8)(REG8(0x11A848UL) - 1);
        if (REG8(0x11A848UL) != 0) {
            motor_3_step();
            REG8(0x11A845UL) = (u8)(REG8(0x11A845UL) - 1);
        } else {
            REG8(0xFFFED6UL) = 0x04;
        }
        break;

    case 0x04:
        REG16(0xFFFF74UL) = 0x2AF8;
        REG8(0x11A836UL) &= (u8)~0x04;
        latch_b_on(0x40, 0x80);
        REG8(0xFFFED6UL) = 0x01;
        break;

    case 0x05:
        REG16(0xFFFF74UL) = 0x2AF8;
        REG8(0x11A848UL) = 0x00;
        REG8(0x11A847UL) = 0x00;
        REG8(0x11A846UL) = 0x00;
        REG8(0x11A836UL) = 0x00;
        REG8(0x11A845UL) = 0x00;
        REG8(0x11A844UL) = 0x00;
        REG8(0xFFFFA4UL) = REG8(0x250A24UL);
        latch_b_on(0x40, 0x80);
        REG8(0xFFFED6UL) = 0x00;
        break;

    case 0x06:
        REG16(0xFFFF74UL) = REG16(0x250A38UL);
        if (REG8(0x11A847UL) < 0x0A) {
            REG8(0x11A847UL) = (u8)(REG8(0x11A847UL) + 1);
        } else {
            REG8(0x11A847UL) = 0x00;
            REG8(0xFFFED6UL) = 0x01;
            REG8(0x11A845UL) = 0x01;
        }
        break;

    case 0x00:
        latch_b_on(0x40, 0x80);
        if (REG8(0x11A847UL) < 0x08) {
            if (REG8(0x11A848UL) < 0x08) {
                motor_3_step_home();
                REG8(0x11A848UL) = (u8)(REG8(0x11A848UL) + 1);
                REG8(0x11A845UL) = 0x00;
            } else {
                REG8(0x11A845UL) = (u8)(REG8(0x11A845UL) + 1);
                if (REG8(0x11A845UL) >= 0x04) {
                    REG8(0x11A848UL) = 0x00;
                    REG8(0x11A847UL) = (u8)(REG8(0x11A847UL) + 1);
                }
            }
            REG16(0xFFFF74UL) = REG16(0x250A38UL);
        } else {
            REG8(0x11A848UL) = 0x00;
            REG8(0x11A847UL) = 0x00;
            REG8(0x11A846UL) = 0x00;
            REG8(0xFFFED6UL) = 0x06;
            REG8(0x11A845UL) = 0x01;
        }
        break;

    default:
        REG8(0xFFFED6UL) = 0x01;
        break;
    }
}

/* ---- ITU2: motor 4, the hook position ----------------------------------
 * H'20D53A. Four phases rather than eight, drive in the low six bits of the
 * H'0C0000 latch, and an idle interval of H'EA60 rather than H'2AF8. The
 * park patterns here are OR'd into the latch rather than replacing it. */
void isr_motor_4_body(void)
{
    u8 pos, tgt, d, v;

    REG8(0xFFFF7BUL) &= (u8)~0x01;

    switch (REG8(0xFFFED0UL)) {
    case 0x01:
        pos = REG8(0x11A84BUL);
        tgt = REG8(0xFFFED1UL);
        if (pos == tgt) {
            REG16(0xFFFF7EUL) = 0xEA60;
        } else if (tgt == 0xFF || tgt == 0xFD || tgt == 0xFC) {
            const u32 park = (tgt == 0xFF) ? 0x250A9AUL
                           : (tgt == 0xFD) ? 0x250A99UL : 0x250A98UL;
            v = (u8)(REG8(0xFFFD39UL) | REG8(park));
            REG8(0x0C0000UL) = v;
            REG8(0xFFFD39UL) = v;
        } else if (tgt == 0xFE) {
            /* coast */
        } else {
            if (pos > tgt) {
                REG8(0x11A836UL) &= (u8)~0x08;
                REG8(0x11A837UL) = (u8)(REG8(0x11A84BUL) - tgt);
            } else {
                REG8(0x11A836UL) |= 0x08;
                REG8(0x11A837UL) = (u8)(tgt - REG8(0x11A84BUL));
            }

            if (REG8(0x11A837UL) < 0x14) REG8(0x11A836UL) |= 0x20;

            d = REG8(0x11A837UL);
            if (d == 0x01)      REG8(0xFFFED0UL) = 0x04;
            else if (d == 0x02) REG8(0xFFFED0UL) = 0x03;
            else                REG8(0xFFFED0UL) = 0x02;

            d = REG8(0x11A837UL);
            REG8(0x11A84CUL) = (u8)(d >> 1);
            REG8(0x11A84DUL) = (u8)(REG8(0x11A837UL) - (u8)(d >> 1));
            REG8(0x11A84AUL) = 0x01;
            motor_4_step();
            REG16(0xFFFF7EUL) =
                (u16)(REG16(0xFFFF7CUL) + REG16(0x250A9CUL));
        }
        break;

    case 0x02:
        REG16(0xFFFF7EUL) = phase_word(0x250A9CUL, REG8(0x11A84AUL));
        motor_4_step();
        REG8(0x11A84CUL) = (u8)(REG8(0x11A84CUL) - 1);
        if (REG8(0x11A84CUL) != 0) {
            REG8(0x11A84AUL) = (u8)(REG8(0x11A84AUL) + 1);
        } else {
            REG8(0xFFFED0UL) = 0x03;
        }
        break;

    case 0x03:
        REG16(0xFFFF7EUL) = phase_word(0x250A9CUL, REG8(0x11A84AUL));
        REG8(0x11A84DUL) = (u8)(REG8(0x11A84DUL) - 1);
        if (REG8(0x11A84DUL) != 0) {
            motor_4_step();
            REG8(0x11A84AUL) = (u8)(REG8(0x11A84AUL) - 1);
        } else {
            REG8(0xFFFED0UL) = 0x04;
        }
        break;

    case 0x04:
        REG16(0xFFFF7EUL) = 0xEA60;
        REG8(0x11A836UL) &= (u8)~0x20;
        latch_b_on(0x10, 0x20);
        REG8(0xFFFED0UL) = 0x01;
        break;

    case 0x05:
        REG16(0xFFFF7EUL) = 0xEA60;
        REG8(0x11A84DUL) = 0x00;
        REG8(0x11A84CUL) = 0x00;
        REG8(0x11A84BUL) = 0x00;
        REG8(0x11A836UL) = 0x00;
        REG8(0x11A84AUL) = 0x00;
        REG8(0x11A849UL) = 0x00;
        v = (u8)(REG8(0xFFFD39UL) | REG8(0x250A90UL));
        REG8(0x0C0000UL) = v;
        v = (u8)(v | 0x10);
        REG8(0x0C0000UL) = v;
        v = (u8)(v | 0x20);
        REG8(0x0C0000UL) = v;
        REG8(0xFFFD39UL) = v;
        REG8(0xFFFED0UL) = 0x00;
        break;

    case 0x06:
        REG16(0xFFFF7EUL) = REG16(0x250A9CUL);
        if (REG8(0x11A84CUL) < 0x0A) {
            REG8(0x11A84CUL) = (u8)(REG8(0x11A84CUL) + 1);
        } else {
            REG8(0x11A84CUL) = 0x00;
            REG8(0xFFFED0UL) = 0x01;
            REG8(0x11A84AUL) = 0x01;
        }
        break;

    case 0x00:
        if (REG8(0x11A84CUL) < 0x0E) {
            if (REG8(0x11A84DUL) < 0x04) {
                motor_4_step_home();
                REG8(0x11A84DUL) = (u8)(REG8(0x11A84DUL) + 1);
                REG8(0x11A84AUL) = 0x00;
            } else {
                REG8(0x11A84AUL) = (u8)(REG8(0x11A84AUL) + 1);
                if (REG8(0x11A84AUL) >= 0x08) {
                    REG8(0x11A84DUL) = 0x00;
                    REG8(0x11A84CUL) = (u8)(REG8(0x11A84CUL) + 1);
                }
            }
            REG16(0xFFFF7EUL) = REG16(0x250A9CUL);
        } else {
            REG8(0x11A84DUL) = 0x00;
            REG8(0x11A84CUL) = 0x00;
            REG8(0x11A84BUL) = 0x00;
            REG8(0xFFFED0UL) = 0x06;
            REG8(0x11A84AUL) = 0x01;
        }
        break;

    default:
        REG8(0xFFFED0UL) = 0x01;
        break;
    }
}

/* ---- ITU3: the sewing speed -------------------------------------------
 * The main motor's tacho drives ITU3's input capture. H'20D99A is the
 * overflow, counting how many whole counter periods have passed, and
 * H'20D9CC is the capture, which turns the two together into the speed at
 * H'FFFECE.
 *
 * The number is H'27B0F0 divided by (overflows << 8) + the top byte of the
 * capture -- so 2,600,176 over the time between two tacho edges. An overflow
 * with no capture behind it leaves bit 4 of H'11A852 up, and the capture
 * that follows skips the division and reports nothing rather than dividing
 * by whatever is there.
 */
void isr_tacho_overflow_body(void)
{
    REG8(0xFFFF85UL) &= (u8)~0x04;

    REG8(0x11A84FUL) = (u8)(REG8(0x11A84FUL) + 1);
    if (REG8(0x11A84FUL) == 0) {
        REG8(0x11A852UL) |= 0x10;
        REG16(0xFFFECEUL) = 0x0000;
    }
}

void isr_tacho_capture_body(void)
{
    REG8(0xFFFF85UL) &= (u8)~0x01;

    if (!(REG8(0x11A852UL) & 0x10)) {
        const u16 ticks = (u16)(((u16)REG8(0x11A84FUL) << 8) +
                                (u16)(u8)(REG16(0xFFFF88UL) >> 8));

        REG16(0xFFFECEUL) = ticks;
        REG16(0xFFFECEUL) = (u16)(0x0027B0F0L / (long)(u32)ticks);
    } else {
        REG8(0x11A852UL) &= (u8)~0x10;
    }

    REG8(0x11A84FUL) = 0x00;
}

/* H'20E04A. ITU4's compare A: one pin raised and nothing else. */
void isr_itu4_a_body(void)
{
    REG8(0xFFFF95UL) &= (u8)~0x01;
    REG8(0xFFFFD6UL) |= 0x08;
}

/* ---- the five motor demands ------------------------------------------
 * H'11A6B7 to H'11A6BB are where the rest of the application leaves what it
 * wants the steppers to do; H'11A821 to H'11A824 are what was last sent, and
 * only a change is written through to H'FFFED1/3/5/7. Each has a ceiling,
 * and the first time round each takes the motor out of its parked state.
 */
void needle_demand_apply(void)
{
    if (!(REG8(0x114DC6UL) & 0x01)) {
        REG8(0x114DC6UL) |= 0x01;
        motor_1_wait_ready();
        REG8(0x11A821UL) = 0x00;
    }
    if (REG8(0x11A6B7UL) > 0x34) REG8(0x11A6B7UL) = 0x34;
    if (REG8(0x11A821UL) != REG8(0x11A6B7UL)) {
        REG8(0x11A821UL) = REG8(0x11A6B7UL);
        REG8(0xFFFED3UL) = REG8(0x11A6B7UL);
    }
}

void feed_demand_apply(void)
{
    if (!(REG8(0x114DC6UL) & 0x02)) {
        motor_2_wait_ready();
        REG8(0x114DC6UL) |= 0x02;
        REG8(0x11A822UL) = 0x00;
    }
    if (REG8(0x11A6B8UL) > 0xDA) REG8(0x11A6B8UL) = 0xDA;
    if (REG8(0x11A822UL) != REG8(0x11A6B8UL)) {
        REG8(0x11A822UL) = REG8(0x11A6B8UL);
        REG8(0xFFFED5UL) = REG8(0x11A6B8UL);
    }
}

/* H'20A61A. The hook width from the copy at H'11A820 rather than from
 * H'11A6B9 -- H'20A652 is what puts one into the other. */
void hook_width_apply(void)
{
    if (REG8(0x11A820UL) > 0x31) REG8(0x11A820UL) = 0x31;
    if (REG8(0x11A823UL) != REG8(0x11A820UL)) {
        REG8(0x11A823UL) = REG8(0x11A820UL);
        REG8(0xFFFED7UL) = REG8(0x11A820UL);
    }
}

void hook_pos_apply(void)
{
    if (!(REG8(0x114DC6UL) & 0x20)) {
        motor_3_wait_ready();
        REG8(0x114DC6UL) |= 0x20;
        REG8(0x11A823UL) = 0x00;
    }
    if (REG8(0x11A6BAUL) > 0x31) REG8(0x11A6BAUL) = 0x31;
    if (REG8(0x11A823UL) != REG8(0x11A6BAUL)) {
        REG8(0x11A823UL) = REG8(0x11A6BAUL);
        REG8(0xFFFED7UL) = REG8(0x11A6BAUL);
    }
    REG8(0x11A820UL) = REG8(0x11A6B9UL);
}

void thread_demand_apply(void)
{
    if (!(REG8(0x114DCDUL) & 0x02)) {
        motor_4_wait_ready();
        REG8(0x114DCDUL) |= 0x02;
        REG8(0x11A824UL) = 0x00;
    }
    if (REG8(0x11A6BBUL) > 0x29) REG8(0x11A6BBUL) = 0x29;
    if (REG8(0x11A824UL) != REG8(0x11A6BBUL)) {
        REG8(0x11A824UL) = REG8(0x11A6BBUL);
        REG8(0xFFFED1UL) = REG8(0x11A6BBUL);
    }
}

/* ---- the sewing phase -------------------------------------------------
 * H'FFFEC0 is where the machine is in the stitch, read off the three
 * position sensors, and H'11A82B remembers which of the four motors have
 * already been let go for this phase so that none is moved twice.
 *
 * H'20A716 is what runs when a phase has lasted H'C8 ticks without the
 * sensors changing -- the machine has stopped somewhere -- and H'20A884 is
 * what runs on the phases that mean it is turning.
 */
void sew_phase_settle(void)
{
    REG8(0x11A82BUL) &= (u8)~0x01;
    REG8(0x11A82BUL) &= (u8)~0x02;

    switch (REG8(0xFFFEC0UL)) {
    case 0x00:
    case 0x01:
    case 0x02:
        REG8(0x11A82BUL) &= (u8)~0x04;
        if (!(REG8(0x11A82BUL) & 0x08)) {
            REG8(0x11A82BUL) |= 0x08;
            hook_pos_apply();
        }
        feed_demand_apply();
        thread_demand_apply();
        break;

    case 0x03:
    case 0x05:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x08;
        thread_demand_apply();
        break;

    case 0x04:
    case 0x06:
        REG8(0x11A82BUL) &= (u8)~0x08;
        if (!(REG8(0x11A82BUL) & 0x04)) {
            REG8(0x11A82BUL) |= 0x04;
            hook_width_apply();
        }
        needle_demand_apply();
        thread_demand_apply();
        break;

    case 0x07:
        REG8(0x11A82BUL) &= (u8)~0x04;
        needle_demand_apply();
        thread_demand_apply();
        break;

    default:
        break;
    }
}

void sew_phase_step(void)
{
    switch (REG8(0xFFFEC0UL)) {
    case 0x00:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x01;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x02)) {
            REG8(0x11A82BUL) |= 0x02;
            feed_demand_apply();
        }
        if (!(REG8(0x11A82BUL) & 0x08)) {
            REG8(0x11A82BUL) |= 0x08;
            hook_pos_apply();
        }
        /* Phase zero calls the thread motor a second time; phases 1 and 2,
         * which are otherwise the same code, do not. */
        thread_demand_apply();
        break;

    case 0x01:
    case 0x02:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x01;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x02)) {
            REG8(0x11A82BUL) |= 0x02;
            feed_demand_apply();
        }
        if (!(REG8(0x11A82BUL) & 0x08)) {
            REG8(0x11A82BUL) |= 0x08;
            hook_pos_apply();
        }
        break;

    case 0x03:
    case 0x05:
        REG8(0x11A82BUL) &= (u8)~0x01;
        REG8(0x11A82BUL) &= (u8)~0x02;
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x08;
        thread_demand_apply();
        break;

    case 0x04:
    case 0x06:
        REG8(0x11A82BUL) &= (u8)~0x08;
        REG8(0x11A82BUL) &= (u8)~0x02;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x01)) {
            REG8(0x11A82BUL) |= 0x01;
            needle_demand_apply();
        }
        if (!(REG8(0x11A82BUL) & 0x04)) {
            REG8(0x11A82BUL) |= 0x04;
            hook_width_apply();
        }
        break;

    case 0x07:
        REG8(0x11A82BUL) &= (u8)~0x04;
        REG8(0x11A82BUL) &= (u8)~0x08;
        REG8(0x11A82BUL) &= (u8)~0x02;
        thread_demand_apply();
        if (!(REG8(0x11A82BUL) & 0x01)) {
            REG8(0x11A82BUL) |= 0x01;
            needle_demand_apply();
        }
        break;

    default:
        break;
    }
}

/* H'20AA82. Whichever of the two runs, and how often. Modes 0, 4 and 5 are
 * the stopped ones and get the H'C8-tick settle; 1, 2, 3 and 6 are turning
 * and get a step every tick. */
void sew_phase_tick(void)
{
    const u8 mode = REG8(0xFFFEC6UL);

    if (REG8(0xFFFEF7UL) & 0x80) return;

    if (mode == 0x00 || (mode >= 0x04 && mode <= 0x05)) {
        const u8 n = (u8)(REG8(0x11A825UL) + 1);

        REG8(0x11A825UL) = n;
        if (n >= 0xC8) {
            sew_phase_settle();
            REG8(0x11A825UL) = 0x00;
        }
        return;
    }

    if (mode < 0x01) return;
    if (mode < 0x04 || mode == 0x06) {
        REG8(0x11A825UL) = 0x00;
        sew_phase_step();
    }
}

/* H'20AAE0. The counters. H'11A6D8, H'114DDE, H'114DE0, H'114DDA count up;
 * H'114DDC counts up by two; H'114DE4 counts down, which is what the hall
 * trim and the module wait on; and the key hold-off runs out here. */
void ms_counters_tick(void)
{
    REG16(0x11A6D8UL) = (u16)(REG16(0x11A6D8UL) + 1);
    REG16(0x114DDEUL) = (u16)(REG16(0x114DDEUL) + 1);
    REG16(0x114DE0UL) = (u16)(REG16(0x114DE0UL) + 1);
    REG16(0x114DE2UL) = (u16)(REG16(0x114DE2UL) + 1);
    REG16(0x114DDAUL) = (u16)(REG16(0x114DDAUL) + 1);
    REG16(0x114DDCUL) = (u16)(REG16(0x114DDCUL) + 2);
    REG16(0x114DE4UL) = (u16)(REG16(0x114DE4UL) - 1);

    if (REG8(0x11A802UL) != 0) {
        REG8(0x11A802UL) = (u8)(REG8(0x11A802UL) - 1);
    }
}

/* H'20A092. The main motor's chopper. H'11A826 counts to H'11A827 and the
 * enable bit follows, so the ratio of the two is the duty; past H'32 the
 * count restarts and the on-time is reloaded from H'11A811. The latch is
 * shared with the stepper enables, which is why the mask goes up around it.
 */
void motor_pwm_tick(void)
{
    const u8 n = (u8)(REG8(0x11A826UL) + 1);

    REG8(0x11A826UL) = n;
    if (n <= REG8(0x11A827UL)) {
        interrupts_off();
        latch_a_bit(0x80, 1);
        interrupts_on();
    } else {
        interrupts_off();
        latch_a_bit(0x80, 0);
        interrupts_on();
        if (REG8(0x11A826UL) > 0x32) {
            REG8(0x11A826UL) = 0x00;
            REG8(0x11A827UL) = REG8(0x11A811UL);
        }
    }
}

/* H'20A10C and H'20A19A. The two knobs. Each is a quadrature pair on port C
 * -- bits 2 and 3 for one, 4 and 5 for the other -- and each turn moves the
 * count by one. Wrapping below zero reads as H'FE or above and is clamped
 * back to nothing; the ceiling comes in as an argument. */
static u8 knob_track(u8 count, u8 limit, u32 state, u8 shift)
{
    const u8 now  = (u8)((REG8(0xFFFFD7UL) >> shift) & 0x03);
    const u8 was  = REG8(state);
    u8 v = count;

    if (was != now) {
        switch (was) {
        case 0x00:
            if (now == 0x01) v++;
            if (now == 0x02) v--;
            break;
        case 0x01:
            if (now == 0x03) v++;
            if (now == 0x00) v--;
            break;
        case 0x03:
            if (now == 0x02) v++;
            if (now == 0x01) v--;
            break;
        case 0x02:
            if (now == 0x00) v++;
            if (now == 0x03) v--;
            break;
        default:
            break;
        }
    }

    if (v >= 0xFE)  v = 0x00;
    if (v >= limit) v = limit;

    REG8(state) = now;
    return v;
}

u8 knob_a_track(u8 count, u16 limit)
{
    return knob_track(count, (u8)limit, 0x11A828UL, 2);
}

u8 knob_b_track(u8 count, u16 limit)
{
    return knob_track(count, (u8)limit, 0x11A829UL, 4);
}

/* H'20A22C. The handwheel. Two sensors -- bit 1 of port C's input register
 * and bit 4 of the H'080000 latch -- read as a two-bit code, and every
 * change moves the position by one. A jump of two, where a step was missed,
 * is taken in whichever direction the wheel was last going: H'11A82B bit 7
 * is that memory. A change on either sensor also raises a bit in H'114DCE,
 * which is what the foot-lift calibration watches. */
u16 handwheel_track(u16 position)
{
    u8 now = 0;
    const u8 was = REG8(0x11A82AUL);
    u16 p = position;

    if (REG8(0xFFFFD6UL) & 0x02) now |= 0x01; else now &= (u8)~0x01;
    if (REG8(0x080000UL) & 0x10) now |= 0x02; else now &= (u8)~0x02;

    if ((was & 0x01) != (now & 0x01)) REG8(0x114DCEUL) |= 0x02;
    if ((was & 0x02) != (now & 0x02)) REG8(0x114DCEUL) |= 0x01;

    if (was != now) {
        u8 fwd = 0, back = 0, jump = 0;

        switch (was) {
        case 0x00: fwd = 0x01; back = 0x02; jump = 0x03; break;
        case 0x01: fwd = 0x03; back = 0x00; jump = 0x02; break;
        case 0x03: fwd = 0x02; back = 0x01; jump = 0x00; break;
        case 0x02: fwd = 0x00; back = 0x03; jump = 0x01; break;
        default:   break;
        }

        if (was <= 0x03) {
            if (now == fwd) {
                p = (u16)(p + 1);
                REG8(0x11A82BUL) |= 0x80;
            } else if (now == back) {
                p = (u16)(p - 1);
                REG8(0x11A82BUL) &= (u8)~0x80;
            } else if (now == jump) {
                if (REG8(0x11A82BUL) & 0x80) p = (u16)(p + 2);
                else                         p = (u16)(p - 2);
            }
        }
    }

    REG8(0x11A82AUL) = now;
    return p;
}

/* H'20A40A. The beeper, a millisecond at a time. H'11A24A counts the period
 * down and H'11A24E is how much of it is silence, so the pin follows the
 * two; when the period runs out and there are repeats left, it is reloaded
 * from on-time plus off-time. */
void beep_tick(void)
{
    u16 left = REG16(0x11A24AUL);

    if (left != 0) {
        const u16 off = REG16(0x11A24EUL);

        left = (u16)(left - 1);
        REG16(0x11A24AUL) = left;
        if (left >= off) {
            REG8(0xFFFEC1UL) |=        0x80;
            REG8(0xFFFFC7UL) |=        0x10;
        } else {
            REG8(0xFFFEC1UL) &= (u8)~0x80;
            REG8(0xFFFFC7UL) &= (u8)~0x10;
        }
    } else if (REG8(0x11A250UL) > 0x01) {
        REG8(0x11A250UL) = (u8)(REG8(0x11A250UL) - 1);
        REG16(0x11A24AUL) = (u16)(REG16(0x11A24CUL) + REG16(0x11A24EUL));
    }
}

/* H'20A47C. Where the needle is, off three sensors: bit 0 of port C's input
 * register and bits 1 and 2 of the H'080000 latch, packed into H'FFFEC0.
 * While the machine is running, the ticks spent in each phase are counted,
 * and the count at the moment phase 5 arrives from phase 1 is left at
 * H'114DD8 -- which is how long one stitch took. */
void sew_position_sense(void)
{
    u8 code = 0;

    if (REG8(0xFFFFD6UL) & 0x01) code |= 0x04;
    if (REG8(0x080000UL) & 0x02) code |= 0x02;
    if (REG8(0x080000UL) & 0x04) code |= 0x01;

    if (REG8(0x114DC6UL) & 0x80) {
        REG16(0x11A82CUL) = (u16)(REG16(0x11A82CUL) + 1);
        if (REG8(0xFFFEC0UL) == 0x05 && code == 0x01) {
            REG16(0x114DD8UL) = REG16(0x11A82CUL);
            REG16(0x11A82CUL) = 0x0000;
        }
    }

    REG8(0xFFFEC0UL) = code;
}

/* ---- ITU4: the millisecond, in three slices ---------------------------
 * H'20DFB0. When bit 2 of H'114DCD is up the machine is in the foot-lift
 * calibration and the work is split across three interrupts at H'0E67 apiece
 * rather than done in one at H'2B34; H'11A853 says which slice is next.
 * H'11A850 counts the whole ones.
 */
void tick_slice_1(void)
{
    REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    sew_position_sense();
    ms_counters_tick();
    motor_pwm_tick();
}

void tick_slice_2(void)
{
    REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    beep_tick();

    if (mode_code() == 0x000A) {
        REG8(0x11B101UL) = knob_a_track(REG8(0x11B101UL), 0x00FF);
    } else if (!(REG8(0x114DCFUL) & 0x40)) {
        REG8(0x11A6D3UL) = knob_a_track(REG8(0x11A6D3UL), 0x00C8);
    }
}

void tick_slice_3(void)
{
    REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    sew_phase_tick();

    if (mode_code() != 0x000A && !(REG8(0x114DCFUL) & 0x80)) {
        REG8(0x11A6D5UL) = knob_b_track(REG8(0x11A6D5UL), 0x00C8);
    }
}

/* H'20AC1C. All of it in one go, which is what happens when the calibration
 * is not running. The knobs and the handwheel are skipped while bit 0 of
 * H'FFFEC4 says service mode or bit 7 of H'FFFEF7 says the sewing side is
 * held off. */
void tick_all(void)
{
    sew_position_sense();
    ms_counters_tick();
    motor_pwm_tick();
    beep_tick();
    sew_phase_tick();

    if (REG8(0xFFFEC4UL) & 0x01) return;
    if (REG8(0xFFFEF7UL) & 0x80) return;

    if (mode_code() == 0x000A) {
        REG8(0x11B101UL) = knob_a_track(REG8(0x11B101UL), 0x00FF);
        return;
    }

    if (!(REG8(0x114DCFUL) & 0x40)) {
        REG8(0x11A6D3UL) = knob_a_track(REG8(0x11A6D3UL), 0x00C8);
    }
    if (!(REG8(0x114DCFUL) & 0x80)) {
        REG8(0x11A6D5UL) = knob_b_track(REG8(0x11A6D5UL), 0x00C8);
    }
    if (REG8(0x114DCDUL) & 0x04) {
        REG16(0xFFFF00UL) = handwheel_track(REG16(0xFFFF00UL));
    }
}

void isr_millisecond_body(void)
{
    REG8(0xFFFF95UL) &= (u8)~0x02;

    if (REG8(0x114DCDUL) & 0x04) {
        const u8 slice = (u8)(REG8(0x11A853UL) + 1);

        REG16(0xFFFF9AUL) = 0x0E67;
        REG8(0x11A853UL) = slice;

        if (slice == 0x01) {
            tick_slice_1();
        } else if (slice == 0x02) {
            tick_slice_2();
        } else if (slice == 0x03) {
            REG8(0xFFFFD7UL) &= (u8)~0x80;
            REG8(0xFFFFD6UL) &= (u8)~0x08;
            REG16(0x11A850UL) = (u16)(REG16(0x11A850UL) + 1);
            tick_slice_3();
            REG8(0xFFFFD7UL) |= 0x80;
            REG8(0x11A853UL) = 0x00;
        }
    } else {
        REG16(0xFFFF9AUL) = 0x2B34;
        REG8(0xFFFFD7UL) &= (u8)~0x80;
        REG8(0xFFFFD6UL) &= (u8)~0x08;
        /* ANDC #H'3F: both mask bits down, not just the one. */
        __asm__ volatile("andc #0x3f,ccr" ::: "cc", "memory");
        REG16(0x11A850UL) = (u16)(REG16(0x11A850UL) + 1);
        tick_all();
        REG8(0xFFFFD7UL) |= 0x80;
    }
}

/* ---- the vector entries -----------------------------------------------
 * Each is the interrupt-handler wrapper for the body above it. The bodies
 * are ordinary functions so that the comparison harness can call them; the
 * originals are entered and left the same way, so a case can also be put on
 * the vector address itself.
 */
void isr_24(void) __attribute__((interrupt_handler));
void isr_24(void) { isr_motors_12_body(); }

void isr_28(void) __attribute__((interrupt_handler));
void isr_28(void) { isr_motor_3_body(); }

void isr_32(void) __attribute__((interrupt_handler));
void isr_32(void) { isr_motor_4_body(); }

void isr_36(void) __attribute__((interrupt_handler));
void isr_36(void) { isr_tacho_capture_body(); }

void isr_38(void) __attribute__((interrupt_handler));
void isr_38(void) { isr_tacho_overflow_body(); }

void isr_40(void) __attribute__((interrupt_handler));
void isr_40(void) { isr_itu4_a_body(); }

void isr_41(void) __attribute__((interrupt_handler));
void isr_41(void) { isr_millisecond_body(); }

/* ---- the item preview -------------------------------------------------
 * H'2125B0 and the three routines under it. When the operator moves through
 * a list, the item under the cursor is drawn large in a panel at the top of
 * the screen, and which of three ways depends on the item's category -- byte
 * H'17 of its descriptor.
 *
 * Two of the three go through the scratch buffer at H'0E8010 and copy it out
 * a pixel at a time, which is how the picture is turned: the source is read
 * along one axis and written along the other. A stitch pattern is drawn
 * ninety degrees round from how it is stored, because it is stored the way
 * it is sewn.
 */
#define PREVIEW_PANEL_X0  0x006C
#define PREVIEW_PANEL_Y0  0x0006
#define PREVIEW_PANEL_X1  0x00C8
#define PREVIEW_PANEL_Y1  0x0023
#define PREVIEW_SCRATCH   0x000E8010UL

static u32 item_descriptor(u16 item)
{
    return ITEM_TABLE + (u32)(long)(short)(u16)(ITEM_STRIDE * item);
}

/* The item's number, drawn under the picture. */
static void item_preview_number(u16 item)
{
    char buf[8];

    int_to_decimal((short)REG16(item_descriptor(item) + 0x14), buf);
    text_draw(buf, 0x006C, 0x0007, 0x0084, 0x0010, 0x0001, 0x01,
              (const u8 *)0x001196EAUL);
}

static void item_preview_clear(void)
{
    draw_rect(PREVIEW_PANEL_X0, PREVIEW_PANEL_Y0,
              PREVIEW_PANEL_X1, PREVIEW_PANEL_Y1,
              LCD_FRAME_A, 0x00, 0x01);
}

/* H'21260A. A stitch: unpacked into the scratch buffer and copied out turned
 * a quarter turn, the source's x becoming the screen's y and running the
 * other way. */
void item_preview_stitch(u16 item)
{
    short sy, dx, sx, dy;

    bitmap_draw(0x0000, 0x0000, 0x0027, 0x002F,
                (const u8 *)REG32(item_descriptor(item) + 0x08),
                PREVIEW_SCRATCH);
    item_preview_clear();

    for (sy = 0x0009, dx = 0x0083; sy <= 0x002D; sy++, dx++) {
        for (sx = 0x0005, dy = 0x0023; sx <= 0x0022; sx++, dy--) {
            plot_pixel((u16)dx, (u16)dy, LCD_FRAME_A,
                       read_pixel((u16)sx, (u16)sy, PREVIEW_SCRATCH));
        }
    }
    item_preview_number(item);
}

/* H'212760. A whole-pattern picture: the same scratch buffer, copied out the
 * right way up and put further to the right. */
void item_preview_pattern(u16 item)
{
    short sy, dy, sx, dx;

    bitmap_draw(0x0000, 0x0000, 0x0022, 0x0022,
                (const u8 *)REG32(item_descriptor(item) + 0x08),
                PREVIEW_SCRATCH);
    item_preview_clear();

    for (sy = 0x0003, dy = 0x0006; sy <= 0x0020; sy++, dy++) {
        for (sx = 0x0002, dx = 0x0089; sx <= 0x0020; sx++, dx++) {
            plot_pixel((u16)dx, (u16)dy, LCD_FRAME_A,
                       read_pixel((u16)sx, (u16)sy, PREVIEW_SCRATCH));
        }
    }
}

/* H'212844. Anything else: the picture is blitted straight into the panel,
 * centred on H'9A, H'14 from the width and height in its own header. */
void item_preview_plain(u16 item)
{
    const u32 desc = item_descriptor(item);
    const u32 src  = REG32(desc + 0x0C);
    const u16 w    = header_word_0((const u8 *)src);
    const u16 h    = header_word_1((const u8 *)src);
    const u16 x0   = (u16)(0x009A - (u16)((short)w / 2));
    const u16 y0   = (u16)(0x0014 - (u16)((short)h / 2));

    item_preview_clear();
    bitmap_draw(x0, y0, (u16)(w + x0), (u16)(h + y0),
                (const u8 *)src, LCD_FRAME_A);
    item_preview_number(item);
}

/* H'2125B0. Which of the three, by category. Screen H'18 has no panel. */
void item_preview(u16 item)
{
    u8 cat;

    if (REG8(0x11A169UL) == 0x18) return;

    cat = REG8(item_descriptor(item) + 0x17);
    if (cat >= 0x05 && cat <= 0x0F) item_preview_plain(item);
    else if (cat >= 0x12)           item_preview_pattern(item);
    else                            item_preview_stitch(item);
}

/* ---- the two bars ------------------------------------------------------
 * H'20FA18 and H'20FF7A, with twenty callers each: the width bar down the
 * right-hand edge of the screen and the length bar across the top of it.
 * Every screen that lets either be changed draws them, and they are drawn
 * incrementally -- only the part that moved is painted, which is why each
 * keeps its own idea of where it was.
 *
 * The bar is one number scaled to pixels and the limit mark is another,
 * drawn as a line across it. The scaling is done in floating point over a
 * range that never leaves a byte, the same as the speed calculation in part
 * 3q: a byte times H'1.01 plus a half, or just plus a half.
 *
 *   H'11A85E / H'11A874   the value the bar was last drawn at
 *   H'11A860 / H'11A872   where that put the end of it
 *   H'11A862 / H'11A878   the limit the mark was last drawn at
 *   H'11A864 / H'11A87A   whether the mark was drawn
 *   H'11A866 / H'11A87C   where the mark is
 */
#define BAR_W_X0    0x0127
#define BAR_W_X1    0x012C
#define BAR_W_TOP   0x0030
#define BAR_W_BASE  0x0095
#define BAR_L_Y0    0x0014
#define BAR_L_Y1    0x0019
#define BAR_L_LEFT  0x00D3
#define BAR_L_RIGHT 0x0137

/* The width bar runs up from H'95, so a bigger number is a smaller y. */
static u16 bar_w_pixel(long v)
{
    return (u16)(BAR_W_BASE - (u16)(int)((float)v * 1.01f + 0.5f));
}

/* The length bar runs right from H'D3. */
static u16 bar_l_pixel(long v)
{
    return (u16)((u16)(int)((float)v + 0.5f) + BAR_L_LEFT);
}

/* H'20FA18. The width bar. [fresh] redraws the whole thing; without it only
 * the difference from last time is painted. */
void bar_width(u16 value, u8 fresh, u32 buffer, u8 colour)
{
    u16 y;
    u16 limit;

    if (fresh != 0) {
        y = bar_w_pixel((long)(short)value);
        draw_rect(BAR_W_X0, y, BAR_W_X1, BAR_W_BASE, buffer, colour, 0x01);
        if ((short)value < 0x0064) {
            draw_rect(BAR_W_X0, BAR_W_TOP, BAR_W_X1, (u16)(y - 1),
                      buffer, 0x00, 0x01);
        }
        REG16(0x11A860UL) = y;
        REG16(0x11A85EUL) = value;
        REG16(0x11A862UL) = 0xFFFF;
        REG16(0x11A864UL) = 0xFFFF;
        REG16(0x11A866UL) =
            bar_w_pixel((long)(u32)(u8)(REG8(0xFFFEE8UL) & 0x7F));
    } else if (value != REG16(0x11A85EUL)) {
        y = bar_w_pixel((long)(short)value);
        if ((short)value > (short)REG16(0x11A85EUL)) {
            draw_rect(BAR_W_X0, y, BAR_W_X1, (u16)(REG16(0x11A860UL) - 1),
                      buffer, colour, 0x01);
        } else {
            draw_rect(BAR_W_X0, REG16(0x11A860UL), BAR_W_X1, (u16)(y - 1),
                      buffer, 0x00, 0x01);
        }
        REG16(0x11A860UL) = y;
        REG16(0x11A85EUL) = value;
    }

    /* The limit mark, moved when the limit has changed. */
    limit = (u16)(u8)(REG8(0xFFFEE8UL) & 0x7F);
    if (limit != REG16(0x11A862UL)) {
        if ((short)REG16(0x11A866UL) >= (short)REG16(0x11A860UL)) {
            if (REG16(0x11A864UL) == 0) {
                draw_hline(BAR_W_X0, BAR_W_X1, REG16(0x11A866UL),
                           buffer, colour);
            }
        } else {
            if (REG16(0x11A864UL) != 0) {
                draw_hline(BAR_W_X0, BAR_W_X1, REG16(0x11A866UL),
                           buffer, 0x00);
            }
        }
        REG16(0x11A866UL) = bar_w_pixel((long)(u32)limit);
        REG16(0x11A862UL) = limit;
    }

    /* And taken off or put back when bit 7 of H'FFFEE5 changes. */
    {
        const u16 on = (u16)(u8)(REG8(0xFFFEE5UL) & 0x80);

        if (on != REG16(0x11A864UL)) {
            draw_hline(BAR_W_X0, BAR_W_X1, REG16(0x11A866UL), buffer,
                       (u8)((REG8(0xFFFEE5UL) & 0x80) ? colour : 0x00));
            REG16(0x11A864UL) = on;
        }
    }
}

/* H'20FF7A. The length bar. Two pictures sit beside it -- H'34BAE7 and
 * H'34BA1B -- and which of them is shown follows bit 4 of H'FFFEF8. */
void bar_length(u16 value, u8 fresh, u32 buffer, u8 colour)
{
    u16 x;
    u16 limit;

    if (fresh != 0) REG16(0x11A876UL) = 0xFFFF;

    {
        const u16 flag = (u16)(u8)(REG8(0xFFFEF8UL) & 0x10);

        if (flag != REG16(0x11A876UL)) {
            bitmap_draw(0x00D2, 0x0006, 0x0139, 0x0013,
                        (const u8 *)((REG8(0xFFFEF8UL) & 0x10)
                                     ? 0x0034BAE7UL : 0x0034BA1BUL),
                        LCD_FRAME_A);
            REG16(0x11A876UL) = (u16)(u8)(REG8(0xFFFEF8UL) & 0x10);
        }
    }

    if (fresh != 0) {
        x = bar_l_pixel((long)(short)value);
        draw_rect(BAR_L_LEFT, BAR_L_Y0, x, BAR_L_Y1, buffer, colour, 0x01);
        if ((short)value < 0x0064) {
            draw_rect((u16)(x + 1), BAR_L_Y0, BAR_L_RIGHT, BAR_L_Y1,
                      buffer, 0x00, 0x01);
        }
        REG16(0x11A872UL) = x;
        REG16(0x11A874UL) = value;
        REG16(0x11A878UL) = 0xFFFF;
        REG16(0x11A87AUL) = 0xFFFF;
        REG16(0x11A87CUL) =
            bar_l_pixel((long)(u32)(u8)(REG8(0xFFFEE5UL) & 0x7F));
    } else if (value != REG16(0x11A874UL)) {
        x = bar_l_pixel((long)(short)value);
        if ((short)value < (short)REG16(0x11A874UL)) {
            draw_rect((u16)(x + 1), BAR_L_Y0, REG16(0x11A872UL), BAR_L_Y1,
                      buffer, 0x00, 0x01);
        } else {
            draw_rect((u16)(REG16(0x11A872UL) + 1), BAR_L_Y0, x, BAR_L_Y1,
                      buffer, colour, 0x01);
        }
        REG16(0x11A872UL) = x;
        REG16(0x11A874UL) = value;
    }

    limit = (u16)(u8)(REG8(0xFFFEE5UL) & 0x7F);
    if (limit != REG16(0x11A878UL)) {
        if ((short)REG16(0x11A87CUL) <= (short)REG16(0x11A872UL)) {
            if (REG16(0x11A87AUL) == 0) {
                draw_vline(REG16(0x11A87CUL), BAR_L_Y0, BAR_L_Y1,
                           buffer, colour);
            }
        } else {
            if (REG16(0x11A87AUL) != 0) {
                draw_vline(REG16(0x11A87CUL), BAR_L_Y0, BAR_L_Y1,
                           buffer, 0x00);
            }
        }
        REG16(0x11A87CUL) = bar_l_pixel((long)(u32)limit);
        REG16(0x11A878UL) = limit;
    }

    {
        const u16 on = (u16)(u8)(REG8(0xFFFEE5UL) & 0x80);

        if (on != REG16(0x11A87AUL)) {
            draw_vline(REG16(0x11A87CUL), BAR_L_Y0, BAR_L_Y1, buffer,
                       (u8)((REG8(0xFFFEE5UL) & 0x80) ? colour : 0x00));
            REG16(0x11A87AUL) = on;
        }
    }
}

/* ---- the hit-box table ------------------------------------------------
 * Every screen the operator sees is a list of boxes, and this is the layer
 * all seventy-nine of them are built on. H'11B0BA points at the list and
 * each entry is H'12 bytes:
 *
 *   +H'00  x0   +H'02  y0   +H'04  x1   +H'06  y1   -- all relative
 *   +H'08  the value the box stands for
 *   +H'0A  a flag: 1 means the box is one of a pair that share a value
 *   +H'0C  a pointer to a list to look the value up in, or zero
 *   +H'10  what the box is: 2 means "not there", anything else means live
 *   +H'11  how it is drawn
 *
 * The boxes are relative to an origin in H'11B0B2 and H'11B0B4, so a screen
 * can be moved without touching its table. Entry 0 is not a box: its first
 * word is how many there are.
 */
#define HITBOX_TABLE   REG32(0x11B0BAUL)
#define HITBOX_X0      REG16(0x11B0B2UL)
#define HITBOX_Y0      REG16(0x11B0B4UL)
#define HITBOX_STRIDE  0x12

static u32 hitbox_at(u16 index)
{
    return HITBOX_TABLE +
           (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)index);
}

/* H'211ADA. What a box is. */
u8 hitbox_kind(u16 index)
{
    return REG8(hitbox_at(index) + 0x10);
}

/* H'211B00. How a box is drawn. */
u8 hitbox_style(u16 index)
{
    return REG8(hitbox_at(index) + 0x11);
}

/* H'210D6C. Every box in the table back to "live, drawn plainly". The count
 * is entry zero's first word, and the walk starts at one. */
void hitbox_reset_all(void)
{
    const u16 count = REG16(HITBOX_TABLE);
    short i;

    for (i = 1; (short)count >= i; i++) {
        const u32 e = hitbox_at((u16)i);

        REG8(e + 0x10) = 0x00;
        REG8(e + 0x11) = 0x03;
    }
}

/* H'211B26. Which box carries [value], searching indices [first] to [last].
 *
 * A box with no list of its own matches on the value itself. A box with a
 * list matches on what the list holds at that offset -- except on screen
 * H'44, where the two boxes of a pair are told apart by their flag byte and
 * matched against H'11A186 or H'11A188 instead, which is how the two halves
 * of a range are edited separately. Zero means nothing matched. */
u16 hitbox_find(u16 first, u16 last, u16 value, u8 second)
{
    const u32 table = HITBOX_TABLE;
    short i;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        u32 list;

        if (REG8(e + 0x10) == 0x02) continue;

        list = REG32(e + 0x0C);
        if (list == 0) {
            if (REG16(e + 0x08) == value) return (u16)i;
            continue;
        }

        if (REG8(0x11A169UL) == 0x44 && REG8(e + 0x0A) == 0x01) {
            const u16 want = second ? REG16(0x11A186UL) : REG16(0x11A188UL);

            if (REG16(e + 0x08) == want) return (u16)i;
            continue;
        }

        {
            const u32 at = list +
                (u32)(long)(short)(u16)(REG16(e + 0x08) << 1);

            if (REG16(at) == value) return (u16)i;
        }
    }
    return 0x0000;
}

/* H'212D8A and H'212E02. One box painted: the first blits a picture into it,
 * the second fills it. Both add the screen's origin to the box.
 *
 * The buffer comes before the picture in the argument list, which is the
 * other way round from bitmap_draw itself. */
void hitbox_blit(u16 index, u32 buffer, u32 src)
{
    const u32 e = hitbox_at(index);

    bitmap_draw((u16)(REG16(e + 0x00) + HITBOX_X0),
                (u16)(REG16(e + 0x02) + HITBOX_Y0),
                (u16)(REG16(e + 0x04) + HITBOX_X0),
                (u16)(REG16(e + 0x06) + HITBOX_Y0),
                (const u8 *)src, buffer);
}

void hitbox_fill(u16 index, u32 buffer)
{
    const u32 e = hitbox_at(index);

    draw_rect((u16)(REG16(e + 0x00) + HITBOX_X0),
              (u16)(REG16(e + 0x02) + HITBOX_Y0),
              (u16)(REG16(e + 0x04) + HITBOX_X0),
              (u16)(REG16(e + 0x06) + HITBOX_Y0),
              buffer, 0x00, 0x01);
}

/* H'217C84. Either of the two, into the second buffer, chosen by a flag. */
void hitbox_paint(u16 index, u8 with_picture)
{
    if (with_picture) hitbox_blit(index, LCD_FRAME_B, 0x0034C8D3UL);
    else              hitbox_fill(index, LCD_FRAME_B);
}

/* ---- the screen stack -------------------------------------------------
 * H'11A18B is a depth followed by that many screen numbers, so "back" has
 * somewhere to go. H'11A17C says whether there is anything on it.
 */

/* H'21CED4. The current screen pushed, unless it is already on top. */
void screen_stack_push(void)
{
    const u16 depth = (u16)REG8(0x11A18BUL);

    if (depth != 0 &&
        REG8(0x0011A18BUL + (u32)(long)(short)depth) == REG8(0x11A169UL)) {
        return;
    }

    REG8(0x11A18BUL) = (u8)(depth + 1);
    REG8(0x0011A18BUL + (u32)(long)(short)(u16)(depth + 1)) =
        REG8(0x11A169UL);
    REG8(0x11A17CUL) = 0x01;
}

/* H'21CF50. One off the top, and the flag down when it empties. */
void screen_stack_pop(void)
{
    const u16 depth = (u16)REG8(0x11A18BUL);

    if (depth == 0) return;

    REG8(0x11A18BUL) = (u8)(depth - 1);
    if ((u16)(depth - 1) == 0) REG8(0x11A17CUL) = 0x00;
}

/* H'21F46A. One of the four remembered screens made current. H'11B100 says
 * which of the four it was. */
void screen_from_slot(u8 slot)
{
    switch (slot) {
    case 0x01: REG8(0x11A169UL) = REG8(0x11A16AUL); break;
    case 0x02: REG8(0x11A169UL) = REG8(0x11A16BUL); break;
    case 0x03: REG8(0x11A169UL) = REG8(0x11A16CUL); break;
    case 0x04: REG8(0x11A169UL) = REG8(0x11A16DUL); break;
    default: return;
    }
    REG8(0x11A173UL) = 0x01;
    REG8(0x11B100UL) = slot;
}

/* ---- the touch hold-off -----------------------------------------------
 * H'11A182 counts down H'19 passes after a touch, so one press is not read
 * as several.
 */

/* H'210E78. */
void touch_holdoff_start(void)
{
    REG16(0x11A182UL) = 0x0019;
}

/* H'210E88. True when the hold-off has run out. */
u8 touch_holdoff_done(void)
{
    if ((short)REG16(0x11A182UL) <= 0) return 0x01;

    REG16(0x11A182UL) = (u16)(REG16(0x11A182UL) - 1);
    return 0x00;
}

/* H'210EA8. Whether a touch may be acted on while the machine is running.
 *
 * With the motor stopped, anything goes. With it running, the first touch
 * after H'11A16F went up is swallowed, and after that only the twelve codes
 * from H'83 to H'8E -- the ones on the sewing screen itself -- are let
 * through. */
u8 touch_allowed(u16 code)
{
    if (!(REG8(0x114DC6UL) & 0x80)) return 0x01;

    if (REG8(0x11A16FUL) != 0) {
        REG8(0x11A16FUL) = 0x00;
        return 0x00;
    }

    if ((short)code >= 0x0083 && (short)code <= 0x008E) return 0x00;
    return 0x01;
}

/* ---- three more that the screens share --------------------------------- */

/* H'21F87A. Whether leaving for screen H'11B10E means the one being left
 * should go on the stack. Screens H'75 and H'7E never do; on the sewing
 * screen and the two like it, a jump to one of the settings screens does
 * not; and H'78 turns the display over on the way out.
 *
 * H'11A17C being zero -- nothing on the stack yet -- also means no. */
u8 screen_leave_stacks(void)
{
    const u16 to = REG16(0x11B10EUL);
    const u8  from = REG8(0x11A169UL);

    if (to == 0x0075 || to == 0x007E) return 0x00;
    if (REG8(0x11A17CUL) == 0) return 0x00;

    if (REG8(0x11A178UL) != 0 && from != 0x46) {
        if ((short)to >= 0x0070) {
            if ((short)to < 0x0073 || to == 0x0077 || to == 0x007D) return 0x00;
        }
        if (to == 0x0078) {
            message_show(0x000A);
            return 0x01;
        }
        return 0x01;
    }

    if (from == 0x2B) {
        if ((short)to >= 0x006E && (short)to <= 0x006F) return 0x00;
        return 0x01;
    }

    if (from >= 0x0C && (from < 0x0E || from == 0x42)) {
        if ((short)to >= 0x006E) {
            if ((short)to < 0x0070 || to == 0x0077) return 0x00;
        }
        return 0x01;
    }
    return 0x01;
}

/* H'21F940. What to do about the screen the panel is asking for: 2 means
 * nothing, 3 means go there, and the number is left where the caller asked
 * for it.
 *
 * Being asked for the screen that is already up is refused -- except for
 * H'7B, which is allowed round again, and except when the caller forces it.
 * Both of those jump straight to the write, which is why they skip the
 * "already there" test rather than answering 2. */
u8 screen_leave_check(u16 *out, u8 forced)
{
    const u16 to = REG16(0x11B10EUL);

    if (to == 0xFFFF) return 0x02;
    if (screen_leave_stacks() != 0) return 0x02;

    if (forced == 0 && to != 0x007B) {
        if (REG16(0x11A17EUL) == to) return 0x02;
    }

    *out = REG16(0x11B10EUL);
    return 0x03;
}

/* H'24662C. One owner at a time for the module link: the first caller gets
 * it, and anyone else is refused and left in H'11F536. */
u8 link_claim(u8 owner)
{
    if (REG8(0x114DB9UL) != 0 && REG8(0x114DB9UL) != owner) {
        REG8(0x11F536UL) = owner;
        return 0x00;
    }
    REG8(0x114DB9UL) = owner;
    return 0x01;
}

/* H'24AAD2. The ROM's own strncpy: at most [n] bytes, and the rest of the
 * [n] filled with zeros once the terminator has been copied. The count is
 * tested before it is decremented, so [n] of zero copies nothing. */
char *str_copy_n(char *dst, const char *src, u32 n)
{
    char *p = dst;

    while (n-- != 0) {
        if ((*p++ = *src++) == 0) {
            while (n-- != 0) *p++ = 0;
            break;
        }
    }
    return dst;
}

/* H'24AAA2. The ROM's own strcpy, and H'24AB62 below it its strcat. Both
 * copy loops end on the flags the store left, not on the increment -- ADDS
 * does not touch them -- so they stop on the terminator having copied it. */
char *str_copy(char *dst, const char *src)
{
    char *p = dst;

    while ((*p = *src++) != 0) p++;
    return dst;
}

char *str_append(char *dst, const char *src)
{
    char *p = dst;

    while (*p != 0) p++;
    while ((*p = *src++) != 0) p++;
    return dst;
}

/* ---- what a box looks like -------------------------------------------
 * H'211518, and it has 250 callers -- more than any other routine in the
 * application. It walks a run of boxes and puts each into a state, drawing
 * whatever that state looks like on the way. The state lives at +H'10 and
 * how the box is drawn at +H'11, and a box already in the state asked for
 * is left alone, which is what keeps the screen from being repainted on
 * every pass.
 *
 * Two shapes of rectangle appear. A picture is blitted at the box's own
 * coordinates, with the screen origin *not* added; a plain fill is drawn at
 * the box shifted by the origin and inset by two pixels all round, so the
 * fill sits inside the border the picture drew. That asymmetry is in the
 * original and both are reproduced.
 */
#define HITBOX_PICTURE  0x0034C8D3UL

static void hitbox_rect_inset(u32 e, u32 buffer, u8 colour)
{
    draw_rect((u16)(REG16(e + 0x00) + HITBOX_X0 + 2),
              (u16)(REG16(e + 0x02) + HITBOX_Y0 + 2),
              (u16)(REG16(e + 0x04) + HITBOX_X0 - 2),
              (u16)(REG16(e + 0x06) + HITBOX_Y0 - 2),
              buffer, colour, 0x01);
}

static void hitbox_rect_raw(u32 e, u32 buffer, u8 colour)
{
    draw_rect(REG16(e + 0x00), REG16(e + 0x02),
              REG16(e + 0x04), REG16(e + 0x06),
              buffer, colour, 0x01);
}

static void hitbox_blit_raw(u32 e, u32 src, u32 buffer)
{
    bitmap_draw(REG16(e + 0x00), REG16(e + 0x02),
                REG16(e + 0x04), REG16(e + 0x06),
                (const u8 *)src, buffer);
}

/* Either the picture the caller gave, or a plain fill in [colour]. */
static void hitbox_face(u32 e, u32 picture, u8 colour)
{
    if (picture == 0) hitbox_rect_inset(e, LCD_FRAME_B, colour);
    else              hitbox_blit_raw(e, picture, LCD_FRAME_B);
}

void hitbox_set_state(u16 first, u16 last, u8 what, u32 picture)
{
    short i;

    if ((short)first <= 0) return;
    if ((short)first > (short)last) return;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        const u8 style = REG8(e + 0x11);

        switch (what) {
        case 0x05:
            /* Blanked with the background picture. */
            if (REG8(e + 0x10) == 0x05) break;
            if (style == 0x03) {
                hitbox_blit_raw(e, HITBOX_PICTURE, LCD_FRAME_B);
            }
            REG8(e + 0x10) = 0x05;
            break;

        case 0x01:
            /* Pressed. */
            if (REG8(e + 0x10) == 0x01) break;
            if (style == 0x03) hitbox_face(e, picture, 0x02);
            REG8(e + 0x10) = 0x01;
            break;

        case 0x00:
            /* Back to normal. Style 4 -- a box drawn by someone else -- is
             * left as it is. */
            if (REG8(e + 0x10) == 0x00) break;
            if (style != 0x04) hitbox_face(e, picture, 0x00);
            REG8(e + 0x10) = 0x00;
            break;

        case 0x02:
            /* Greyed out: drawn into the front buffer, and if it was
             * pressed, the press is taken off the back one. */
            if (REG8(e + 0x10) == 0x02) break;
            if (style == 0x03) {
                hitbox_rect_raw(e, LCD_FRAME_A, 0x02);
                if (REG8(e + 0x10) == 0x01) {
                    hitbox_rect_inset(e, LCD_FRAME_B, 0x00);
                }
            }
            REG8(e + 0x10) = 0x02;
            break;

        case 0x04:
            /* Handed over: the state stays, only the style changes. */
            if (style != 0x03) break;
            if (REG8(e + 0x10) == 0x01) {
                hitbox_face(e, picture, 0x00);
            } else if (REG8(e + 0x10) == 0x05) {
                hitbox_rect_inset(e, LCD_FRAME_B, 0x00);
            }
            REG8(e + 0x11) = 0x04;
            break;

        case 0x03:
            /* Handed back: whatever state the box is in is drawn again. */
            if (style == 0x03) break;
            if (REG8(e + 0x10) == 0x01) {
                hitbox_face(e, picture, 0x02);
                REG8(e + 0x11) = 0x03;
            } else if (REG8(e + 0x10) == 0x00) {
                hitbox_face(e, picture, 0x00);
                REG8(e + 0x11) = 0x03;
            } else if (REG8(e + 0x10) == 0x02) {
                hitbox_rect_raw(e, LCD_FRAME_A, 0x02);
                REG8(e + 0x11) = 0x03;
            } else if (REG8(e + 0x10) == 0x05) {
                hitbox_blit_raw(e, HITBOX_PICTURE, LCD_FRAME_B);
                REG8(e + 0x11) = 0x03;
            }
            break;

        default:
            break;
        }
    }
}

/* H'211E9C. The same again, with a box drawn round each picture.
 *
 * H'211C38 above blits the picture on its own; this one fills the box out
 * two pixels beyond the rectangle in colour 2 first, so the picture sits in
 * a panel. The two pattern lists take their picture out of the stitch
 * descriptor and everything else indexes the icon table, the same split as
 * H'211C38 makes -- but with only two of the five lists counted as
 * patterns, not all five.
 *
 * A value of zero, and a run longer than the list, both leave the box in
 * state 2. There is no wrapping here.
 *
 * The origin is read once at the top rather than per box, which matters
 * only if something moved it half way through -- nothing does. */
u16 hitbox_fill_boxed_from_list(u16 first, u16 last, u16 value, u32 list)
{
    const u16 ox = HITBOX_X0;
    const u16 oy = HITBOX_Y0;
    const u16 length = REG16(list);
    short i = (short)first;
    short v = (short)value;

    for (; i <= (short)last; i++, v++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        u16 slot;

        REG32(e + 0x0C) = list;
        REG16(e + 0x08) = (u16)v;

        slot = REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
        if (slot == 0 || v > (short)length) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
            continue;
        }

        if (REG8(e + 0x11) != 0x04) {
            u32 picture;

            draw_rect((u16)(REG16(e + 0x00) + ox - 2),
                      (u16)(REG16(e + 0x02) + oy - 2),
                      (u16)(REG16(e + 0x04) + ox + 2),
                      (u16)(REG16(e + 0x06) + oy + 2),
                      LCD_FRAME_A, 0x02, 0x01);

            slot = REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
            if (list == 0x0011A88EUL || list == REG32(0x11B096UL)) {
                picture = REG32(ITEM_TABLE +
                    (u32)(long)(short)(u16)(ITEM_STRIDE * slot) + 0x08);
            } else {
                picture = REG32(0x001158CEUL +
                    (u32)(long)(short)(u16)((u16)(slot << 2)));
            }

            bitmap_draw((u16)(REG16(e + 0x00) + ox),
                        (u16)(REG16(e + 0x02) + oy),
                        (u16)(REG16(e + 0x04) + ox),
                        (u16)(REG16(e + 0x06) + oy),
                        (const u8 *)picture, LCD_FRAME_A);
        }
        hitbox_set_state((u16)i, (u16)i, 0x00, 0);
    }

    return (u16)(v + 1);
}

/* H'211A9E. A message put up and held for H'96 ticks. */
void message_show_held(u16 msg)
{
    hitbox_set_state(msg, msg, 0x01, 0);
    REG16(0x114DE0UL) = 0x0000;
    REG16(0x11A166UL) = 0x0096;
    REG16(0x11A180UL) = msg;
}

/* ---- a run of boxes filled from a list --------------------------------
 * H'211C38. The list is a count followed by that many values, and each box
 * in the run gets one of them. Which picture a value stands for depends on
 * which list it is: five of them -- the pattern lists -- take the picture
 * out of the stitch descriptor at H'114DD2, and everything else indexes the
 * icon table at H'1158CE.
 *
 * A run longer than the list has its tail put into state 2, so the buttons
 * past the end of a short list go grey. The value returned is one past the
 * last box that got something.
 */
static u32 hitbox_list_is_patterns(u32 list)
{
    return (list == 0x0011A88EUL || list == REG32(0x11B096UL) ||
            list == 0x0011B212UL || list == REG32(0x11B09AUL) ||
            list == REG32(0x11B09EUL));
}

u16 hitbox_fill_from_list(u16 first, u16 last, u16 value, u32 list)
{
    const u16 length = REG16(list);
    const u32 ox = (u32)HITBOX_X0, oy = (u32)HITBOX_Y0;
    short i = (short)first;
    short v = (short)value;
    short local_len;
    u8 wraps = 0;

    if (hitbox_list_is_patterns(list)) {
        for (; i <= (short)last; i++, v++) {
            const u32 e = HITBOX_TABLE +
                (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

            REG32(e + 0x0C) = list;
            REG16(e + 0x08) = (u16)v;

            if (REG16(list + (u32)(long)(short)(u16)((u16)v << 1)) == 0) {
                hitbox_set_state((u16)i, (u16)i, 0x02, 0);
                continue;
            }
            if (v > (short)length) continue;    /* past the end: left alone */

            if (REG8(e + 0x11) != 0x04) {
                const u16 pattern =
                    REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
                const u32 rec = ITEM_TABLE +
                    (u32)(long)(short)(u16)(ITEM_STRIDE * pattern);

                bitmap_draw((u16)(REG16(e + 0x00) + ox),
                            (u16)(REG16(e + 0x02) + oy),
                            (u16)(REG16(e + 0x04) + ox),
                            (u16)(REG16(e + 0x06) + oy),
                            (const u8 *)REG32(rec + 0x08), LCD_FRAME_A);
            }
            hitbox_set_state((u16)i, (u16)i, 0x00, 0);
        }
        return (u16)(v + 1);
    }

    /* Not a pattern list. A run starting past the first value whose slot is
     * empty is pulled back to one, and one whose slot is not is marked as
     * wrapping: when it reaches the end of the list it starts again, so a
     * short list repeats across the row rather than leaving it half grey. */
    if ((short)value > 1) {
        if (REG16(list + (u32)(long)(short)(u16)((u16)value << 1)) != 0) {
            wraps = 1;
        } else {
            value = 0x0001;
            v = 1;
        }
    }
    local_len = (short)length;

    for (; i <= (short)last; i++, v++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        REG32(e + 0x0C) = list;
        REG16(e + 0x08) = (u16)v;

        if (REG16(list + (u32)(long)(short)(u16)((u16)v << 1)) == 0 ||
            v > local_len) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
            continue;
        }

        if (REG8(e + 0x11) != 0x04) {
            const u16 icon =
                REG16(list + (u32)(long)(short)(u16)((u16)v << 1));
            const u32 src = REG32(0x001158CEUL +
                (u32)(long)(short)(u16)((u16)icon << 2));

            bitmap_draw((u16)(REG16(e + 0x00) + ox),
                        (u16)(REG16(e + 0x02) + oy),
                        (u16)(REG16(e + 0x04) + ox),
                        (u16)(REG16(e + 0x06) + oy),
                        (const u8 *)src, LCD_FRAME_A);
        }
        hitbox_set_state((u16)i, (u16)i, 0x00, 0);

        /* The wrap is tested against the list's real length, not against the
         * one the wrap itself writes back. */
        if (v == (short)length && wraps) {
            local_len = (short)(value - 1);
            v = 0;
            wraps = 0;
        }
    }
    return (u16)(v + 1);
}

/* H'212E78. A run of boxes drawn again from whatever list each one already
 * points at.
 *
 * H'211C38 and H'211E9C above put a list *into* a run; this one takes each
 * box as it stands. It copies the whole H'12-byte entry into a local first
 * and works from the copy, which is why the fields below are read out of an
 * array rather than out of the table.
 *
 * A box in state 2 is left alone, and so is one not drawn in style 3. A
 * slot of zero puts the box into state 2; anything else is blitted at the
 * box's own coordinates with no origin added, from the stitch descriptor for
 * the five pattern lists and from the icon table for everything else. */
void hitbox_redraw_run(u16 first, u16 last)
{
    short i;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = HITBOX_TABLE +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
        u16 box[9];
        u32 list;
        u16 slot;
        int n;

        for (n = 0; n < 9; n++) box[n] = REG16(e + (u32)(2 * n));

        list = ((u32)box[6] << 16) | (u32)box[7];

        if ((u8)(box[8] >> 8) == 0x02) continue;   /* +H'10, the state */
        if ((u8)box[8] != 0x03) continue;          /* +H'11, the style */

        slot = REG16(list + (u32)(long)(short)(u16)((u16)(box[4] << 1)));
        if (slot == 0) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
            continue;
        }

        slot = REG16(list + (u32)(long)(short)(u16)((u16)(box[4] << 1)));
        if (hitbox_list_is_patterns(list)) {
            bitmap_draw(box[0], box[1], box[2], box[3],
                        (const u8 *)REG32(ITEM_TABLE +
                            (u32)(long)(short)(u16)(ITEM_STRIDE * slot) + 0x08),
                        LCD_FRAME_A);
        } else {
            bitmap_draw(box[0], box[1], box[2], box[3],
                        (const u8 *)REG32(0x001158CEUL +
                            (u32)(long)(short)(u16)((u16)(slot << 2))),
                        LCD_FRAME_A);
        }
    }
}

/* ---- five that most of the screens lean on ----------------------------- */

void screen_store(u8 slot, u8 out);

/* ---- what the dispatcher does before it dispatches --------------------
 * Seven routines from the top of H'22382A: the pending screen change, the
 * touch settling, the foot switch, whether the module will take an order,
 * the key scan, and the screen save and restore.
 */

/* H'244C62. Whether the module has nothing of its own going on: seven bytes
 * that all have to be clear. */
u8 module_is_idle(void)
{
    if (REG8(0x00114D7BUL) != 0) return 0x00;
    if (REG8(0x00114D7EUL) != 0) return 0x00;
    if (REG8(0x001040B4UL) != 0) return 0x00;
    if (REG8(0x00104042UL) != 0) return 0x00;
    if (REG8(0x00114D7FUL) != 0) return 0x00;
    if (REG8(0x00114D69UL) != 0) return 0x00;
    if (REG8(0x00114D72UL) != 0) return 0x00;
    return 0x01;
}

/* H'249D6C. Whether the module will take an order: idle, and the link quiet.
 * The link test is the same one written out twice that H'248668 waits on. */
u8 module_ready(void)
{
    if (REG16(0x0011A63AUL) != 0) return 0x00;
    if (module_is_idle() == 0) return 0x00;
    if ((REG8(0x00114D50UL) & 0x21) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    if ((REG8(0x00114D50UL) & 0x22) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    if (REG8(0x00114D50UL) & 0x20) return 0x00;
    if (REG8(0x00114D86UL) != 0) return 0x00;
    return 0x01;
}

/* H'2237D0. A screen change left waiting by something that could not do it
 * itself: three bytes put back where the hardware reads them, a word put
 * back, and then the move. H'11B29C is the flag the dispatcher tests and
 * this clears; H'11B29B says a return happened. */
void screen_restore_pending(void)
{
    REG8(0x00FFFEE4UL) = REG8(0x0011B294UL);
    REG8(0x00FFFEE7UL) = REG8(0x0011B295UL);
    REG8(0x00FFFEEAUL) = REG8(0x0011B296UL);
    REG16(0x0011B108UL) = REG16(0x0011B298UL);
    screen_switch(REG8(0x0011B29AUL), 0x01, 0x00);
    REG8(0x0011B29CUL) = 0x00;
    REG8(0x0011B29BUL) = 0x01;
}

/* H'210E02. Whether the touch reading has settled. The two coordinate bytes
 * are remembered in H'11B2CA and H'11B2CC and a countdown in H'11A19A is set
 * to ten whenever they move. Ten readings the same and the countdown reaches
 * zero -- but the routine only says "yes" on the very first call, when the
 * countdown was zero to begin with. */
u8 touch_settled(void)
{
    if (REG16(0x0011A19AUL) == 0) {
        REG16(0x0011A19AUL) = 0x000A;
        return 0x01;
    }

    if (REG8(0x00FFFED9UL) == REG16(0x0011B2CAUL) &&
        REG8(0x00FFFEDAUL) == REG16(0x0011B2CCUL)) {
        REG16(0x0011A19AUL) = (u16)(REG16(0x0011A19AUL) - 1);
        return 0x00;
    }

    REG16(0x0011B2CAUL) = REG8(0x00FFFED9UL);
    REG16(0x0011B2CCUL) = REG8(0x00FFFEDAUL);
    REG16(0x0011A19AUL) = 0x000A;
    return 0x00;
}

/* H'215448. The foot switch, read from bit 7 of H'FFFEC4 with the position
 * in H'FFFEC5: nothing at all unless the switch is down, then screen H'17
 * for position zero and screen H'00 for position six. Any other position is
 * ignored. */
void foot_switch_screen(void)
{
    u8 where;

    if (!(REG8(0x00FFFEC4UL) & 0x80)) return;

    where = REG8(0x00FFFEC5UL);
    if (where == 0x00)      screen_switch(0x17, 0x01, 0x00);
    else if (where == 0x06) screen_switch(0x00, 0x01, 0x00);
}

/* H'21F68C. The key scan: eighteen bits over four ports tested in a fixed
 * order and the first one down named in H'11B10E, H'6D upwards. H'FFFF is
 * "nothing down".
 *
 * One key is special. H'75 is the one that starts the module, and if bit 0
 * of H'FFFEC4 says the module is there it is only accepted when the module
 * will take the order. When it will not, the key is not reported at all and
 * the scan carries on into the last two tests below it.
 */
void key_scan(void)
{
    if (REG8(0x00FFFEDCUL) & 0x04) { REG16(0x0011B10EUL) = 0x006D; return; }
    if (REG8(0x00FFFEDDUL) & 0x08) { REG16(0x0011B10EUL) = 0x006E; return; }
    if (REG8(0x00FFFEDDUL) & 0x20) { REG16(0x0011B10EUL) = 0x006F; return; }
    if (REG8(0x00FFFEDCUL) & 0x01) { REG16(0x0011B10EUL) = 0x0070; return; }
    if (REG8(0x00FFFEDBUL) & 0x01) { REG16(0x0011B10EUL) = 0x0071; return; }
    if (REG8(0x00FFFEDDUL) & 0x01) { REG16(0x0011B10EUL) = 0x0072; return; }
    if (REG8(0x00FFFEDCUL) & 0x08) { REG16(0x0011B10EUL) = 0x0073; return; }
    if (REG8(0x00FFFEDDUL) & 0x02) { REG16(0x0011B10EUL) = 0x0074; return; }
    if (REG8(0x00FFFEDDUL) & 0x04) { REG16(0x0011B10EUL) = 0x0076; return; }
    if (REG8(0x00FFFEDBUL) & 0x04) { REG16(0x0011B10EUL) = 0x0077; return; }
    if (REG8(0x00FFFEDCUL) & 0x02) { REG16(0x0011B10EUL) = 0x0078; return; }
    if (REG8(0x00FFFEDDUL) & 0x10) { REG16(0x0011B10EUL) = 0x0079; return; }
    if (REG8(0x00FFFEDBUL) & 0x40) { REG16(0x0011B10EUL) = 0x007A; return; }
    if (REG8(0x00FFFEC1UL) & 0x02) { REG16(0x0011B10EUL) = 0x007B; return; }
    if (REG8(0x00FFFEDBUL) & 0x80) { REG16(0x0011B10EUL) = 0x007C; return; }
    if (REG8(0x00FFFEDBUL) & 0x02) { REG16(0x0011B10EUL) = 0x007D; return; }
    if (REG8(0x00FFFEDCUL) & 0x10) { REG16(0x0011B10EUL) = 0x0081; return; }

    if (REG8(0x00FFFEDBUL) & 0x08 &&
        (!(REG8(0x00FFFEC4UL) & 0x01) || module_ready() != 0)) {
        REG16(0x0011B10EUL) = 0x0075;
        REG8(0x0011A16FUL) = 0x01;
        return;
    }

    if (REG8(0x00FFFEDCUL) & 0x20) REG16(0x0011B10EUL) = 0x007E;
    else                           REG16(0x0011B10EUL) = 0xFFFF;
}

/* H'21F4C6. The screen picture and its state put away and fetched back.
 * H'11A172 asks for the put-away and H'11A173 for the fetch, and which of
 * four slots is in H'11B100. Sixteen bytes of state travel with the picture:
 * the live copy is at H'11B0AE and each slot keeps its own at H'11B0BE and
 * every sixteen bytes after that.
 *
 * A slot number outside one to four leaves through the back door: the ask is
 * not cleared and the fetch below it does not happen either. */
void screen_put_away(void)
{
    const u32 STATE = 0x0011B0AEUL;
    u8 slot;
    short i;

    if (REG8(0x0011A172UL) != 0) {
        slot = REG8(0x0011B100UL);
        if (slot < 0x01 || slot > 0x04) return;
        screen_store(slot, 0x01);
        for (i = 0; i < 4; i++)
            REG32(0x0011B0BEUL + (u32)(slot - 1) * 0x10 + (u32)i * 4) =
                REG32(STATE + (u32)i * 4);
        REG8(0x0011A172UL) = 0x00;
    }

    if (REG8(0x0011A173UL) != 0) {
        slot = REG8(0x0011B100UL);
        if (slot < 0x01 || slot > 0x04) return;
        screen_store(slot, 0x00);
        for (i = 0; i < 4; i++)
            REG32(STATE + (u32)i * 4) =
                REG32(0x0011B0BEUL + (u32)(slot - 1) * 0x10 + (u32)i * 4);
        REG8(0x0011A173UL) = 0x00;
    }
}

/* ---- the module's slate wiped clean -----------------------------------
 * H'244578 is called from sixteen places, all of them the start of some
 * piece of embroidery work, and it is the one routine that puts the module
 * back to a known state: three buffers zeroed, the current slot's two
 * records started off, and the pattern store emptied.
 *
 * Two records describe the pattern in the slot named by H'11A660. The
 * first is sixteen bytes at H'11A25A -- the stitch settings, indexed by
 * slot << 4 -- and the second is eighteen bytes at H'11A41A, indexed by
 * slot * H'12. Both indices are worked out afresh for every single field
 * in the original, which is written out here as it stands.
 */
#define PAT_A(off) (0x0011A25AUL \
    + (u32)(long)(short)(u16)((u16)REG8(0x11A660UL) << 4) + (u32)(off))
#define PAT_B(off) (0x0011A41AUL \
    + (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL)) + (u32)(off))

/* H'23A036. Bit 3 of the attribute byte belonging to the pattern the slot
 * holds. The attributes are one byte each at H'0FFEB8. */
u8 pattern_attr_bit3(void)
{
    const u8 pattern = REG8(PAT_B(0x00));

    return (REG8(0x000FFEB8UL + pattern) & 0x08) ? 0x01 : 0x00;
}

/* H'231994. The current slot's stitch settings back to their defaults.
 * H'11A263 is the one field it leaves alone, and it writes H'11A265 before
 * H'11A264 -- both differences from the fuller version in H'2445F6. */
void stitch_reset_current(void)
{
    REG16(PAT_A(0x0C)) = 0x0000;
    REG16(PAT_A(0x0E)) = 0x0000;
    REG8(PAT_A(0x00)) = 0x32;
    REG8(PAT_A(0x01)) = 0x32;
    REG8(PAT_A(0x02)) = 0x32;
    REG8(PAT_A(0x03)) = 0x32;
    REG8(PAT_A(0x04)) = 0x5F;
    REG8(PAT_A(0x05)) = 0x00;
    REG8(PAT_A(0x06)) = 0x24;
    REG8(PAT_A(0x07)) = 0x32;
    REG8(PAT_A(0x08)) = 0x32;
    REG8(PAT_A(0x0B)) = 0x00;
    REG8(PAT_A(0x0A)) = 0x00;
    REG8(0x0011F4D9UL) = 0x32;
    REG8(0x0011F4DAUL) = 0x32;
    REG16(0x0011F292UL) = 0x0000;
    REG8(0x00114D96UL) = pattern_attr_bit3() != 0 ? 0x01 : 0x00;
}

/* H'23E6C4. The slot number waiting in H'11A640 becomes the current one and
 * its second record is started off: the slot itself, whatever H'114DA1
 * holds, and H'114D8C divided down by H'1B. Slot zero means nothing to
 * pick up and the routine does nothing at all. */
void pattern_slot_begin(void)
{
    const u8 slot = REG8(0x0011A640UL);

    if (slot == 0x00) return;

    REG8(0x0011A660UL) = slot;
    REG8(0x0011A63FUL) = slot;
    REG8(PAT_B(0x04)) = REG8(0x0011A640UL);
    REG8(PAT_B(0x03)) = REG8(0x00114DA1UL);
    REG8(PAT_B(0x05)) = (u8)((u16)REG8(0x00114D8CUL) / 0x1B);
    stitch_reset_current();
}

/* H'2416D6. Fourteen words of module counters, H'11F2E4 to H'11F300, and
 * the original clears them in an order of its own. */
void link_counters_clear(void)
{
    REG16(0x0011F2F6UL) = 0x0000;
    REG16(0x0011F2F8UL) = 0x0000;
    REG16(0x0011F2FAUL) = 0x0000;
    REG16(0x0011F2FCUL) = 0x0000;
    REG16(0x0011F2FEUL) = 0x0000;
    REG16(0x0011F300UL) = 0x0000;
    REG16(0x0011F2E4UL) = 0x0000;
    REG16(0x0011F2E8UL) = 0x0000;
    REG16(0x0011F2E6UL) = 0x0000;
    REG16(0x0011F2EAUL) = 0x0000;
    REG16(0x0011F2EEUL) = 0x0000;
    REG16(0x0011F2F2UL) = 0x0000;
    REG16(0x0011F2F0UL) = 0x0000;
    REG16(0x0011F2F4UL) = 0x0000;
}

/* H'23E462. A single RTS. Something used to happen here. */
void module_reset_hook(void)
{
}

/* H'244AAC. The pattern store, H'104D4A up to but not including H'10C27A,
 * zeroed a byte at a time. */
void pattern_store_clear(void)
{
    u32 p;

    for (p = 0x00104D4AUL; p < 0x0010C27AUL; p++) REG8(p) = 0x00;
}

/* H'2445F6. The module's own state. This writes the same stitch defaults as
 * H'231994 and three more fields with them, then empties the second record
 * altogether and clears the pattern store. */
void module_state_clear(void)
{
    short i;

    for (i = 0; i < 0x000C; i++) REG8(0x0011A612UL + (u16)i) = 0x00;

    REG8(0x001040BCUL) = 0x14;
    REG8(0x001040BDUL) = 0x96;
    REG8(0x00100282UL) = 0x09;
    REG8(0x00100283UL) = 0x3E;
    REG8(0x00100280UL) = 0x00;
    REG8(0x00100281UL) = 0x00;
    REG16(0x0011F302UL) = 0x007D;
    REG8(0x00114D9BUL) = 0x01;
    link_counters_clear();
    module_reset_hook();
    REG16(0x0011F2ECUL) = 0x0024;

    REG16(PAT_A(0x0C)) = 0x0000;
    REG16(PAT_A(0x0E)) = 0x0000;
    REG8(PAT_A(0x00)) = 0x32;
    REG8(PAT_A(0x01)) = 0x32;
    REG8(PAT_A(0x02)) = 0x32;
    REG8(PAT_A(0x03)) = 0x32;
    REG8(PAT_A(0x04)) = 0x5F;
    REG8(PAT_A(0x05)) = 0x00;
    REG8(PAT_A(0x06)) = 0x24;
    REG8(PAT_A(0x07)) = 0x32;
    REG8(PAT_A(0x08)) = 0x32;
    REG8(PAT_A(0x09)) = 0x00;
    REG8(PAT_A(0x0A)) = 0x00;
    REG8(PAT_A(0x0B)) = 0x00;

    REG8(PAT_B(0x00)) = 0x00;
    REG8(PAT_B(0x01)) = 0x00;
    REG8(PAT_B(0x02)) = 0x00;
    REG8(PAT_B(0x03)) = 0x00;
    REG8(PAT_B(0x04)) = 0x00;
    REG8(PAT_B(0x05)) = 0x00;

    REG32(0x0011F2C6UL) = 0x00104D4AUL;
    REG8(0x0011F4D9UL) = 0x32;
    REG8(0x0011F4DAUL) = 0x32;
    pattern_store_clear();

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;
    REG8(0x00114D73UL) = 0x00;
    REG8(0x0011F4E6UL) = 0x00;
    REG8(0x0011F310UL) = 0x00;
    REG8(0x0011F311UL) = 0x00;
    REG8(0x0011F312UL) = 0x00;
    REG8(0x0011F313UL) = 0x00;
    REG8(0x0011F314UL) = 0x00;
    REG16(0x0011A62AUL) = 0x0000;
    REG16(0x0011A62CUL) = 0x0000;
    REG8(0x0011F30EUL) = 0x00;
    REG8(0x0011F305UL) = 0x00;
    REG8(0x0011F304UL) = 0x00;
    REG8(0x00114D66UL) = 0x00;
    REG8(0x00114D62UL) = 0x00;
    REG8(0x00114D98UL) = 0x00;
    REG8(0x00104040UL) = 0x00;
    REG16(0x00114D4CUL) = (u16)(REG16(0x00114D4CUL) & ~0x4000);
}

/* H'244578. The whole slate: the slot picked up, three buffers zeroed, and
 * the module's state cleared behind them. */
void module_buffers_clear(void)
{
    short i;

    pattern_slot_begin();
    for (i = 0; i < 0x003D; i++) REG8(0x00114D7AUL + (u16)i) = 0x00;
    for (i = 0; i < 0x00AC; i++) REG8(0x00104C98UL + (u16)i) = 0x00;
    for (i = 0; i < 0x0400; i++) REG8(0x000FFE80UL + (u16)i) = 0x00;
    module_state_clear();
    REG8(0x00104040UL) = 0x00;
    REG8(0x001040B5UL) = 0x00;
}

void link_send_start(void);

/* ---- the module panel's five labels ------------------------------------
 * Five routines, each drawing one string into one fixed little box, all
 * centred, all with a gap of one. Three run down the left edge and two sit
 * along the top. They are *not* in address order -- H'21789C is the topmost
 * of the three down the left and H'21759E the lowest:
 *
 *   H'21789C  text_left_94    H'217932  text_top_CB
 *   H'2175E8  text_left_BC    H'21797E  text_top_102
 *   H'21759E  text_left_D9
 */
void text_left_94(const char *str)
{
    text_draw(str, 0x0000, 0x0094, 0x0023, 0x009C, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void text_left_BC(const char *str)
{
    text_draw(str, 0x0000, 0x00BC, 0x0023, 0x00C4, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void text_left_D9(const char *str)
{
    text_draw(str, 0x0000, 0x00D9, 0x0023, 0x00E1, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

void text_top_CB(const char *str)
{
    text_draw(str, 0x00CB, 0x0012, 0x00E2, 0x0023, 0x0001, 0x02,
              (const u8 *)0x0011936EUL);
}

void text_top_102(const char *str)
{
    text_draw(str, 0x0102, 0x001A, 0x010D, 0x0023, 0x0001, 0x02,
              (const u8 *)0x001196EAUL);
}

/* H'21747E. The same three-way box as H'24A432 but at a fixed place, the
 * one state four uses. */
void module_fixed_box(u8 mode)
{
    if (mode == 0x00)
        draw_rect(0x00F1, 0x002C, 0x0113, 0x004E, LCD_FRAME_A, 0x00, 0x01);
    else if (mode == 0x01)
        bitmap_draw(0x00F1, 0x002C, 0x0113, 0x004E,
                    (const u8 *)0x0034C148UL, LCD_FRAME_A);
    else if (mode == 0x02)
        draw_rect(0x00F1, 0x002C, 0x0113, 0x004E, LCD_FRAME_A, 0x02, 0x01);
}

/* H'2179CA. Box five lit or not, the same shape as H'24A2DA. */
void box5_draw(u8 lit)
{
    if (lit != 0) {
        if (hitbox_kind(0x0005) == 0x01) hitbox_set_state(0x0005, 0x0005, 0x00, 0);
        hitbox_set_state(0x0005, 0x0005, 0x01, 0);
        return;
    }
    hitbox_set_state(0x0005, 0x0005, 0x00, 0);
}

/* H'231DD2. A number with "min" after it, in the third left-hand label. The
 * string is built in the RAM buffer at H'11F2D6 and the word itself is
 * spelled out into H'11F294 a byte at a time. */
void label_minutes(short value)
{
    int_to_decimal(value, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x6D;   /* 'm' */
    REG8(0x0011F295UL) = 0x69;   /* 'i' */
    REG8(0x0011F296UL) = 0x6E;   /* 'n' */
    REG8(0x0011F297UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_94((const char *)0x0011F2D6UL);
}

/* H'236B5A. The two numbers along the top: which colour is being sewn and
 * how many there are. Nothing is drawn unless H'114D91 says so, and a colour
 * number past H'1B is not a number at all -- it sets bit H'0400 of H'114D4C
 * and leaves the labels alone.
 *
 * Both numbers are cut to two digits by dropping a nought over the third
 * byte of the buffer, which is four bytes of stack. */
void label_colours(void)
{
    char buf[4];

    if (REG8(0x00114D91UL) == 0) return;

    if (REG8(0x00114D89UL) > 0x1B) {
        REG16(0x00114D4CUL) |= 0x0400;
        return;
    }

    if (REG8(0x00114D89UL) < REG8(0x00114D8DUL)) {
        int_to_decimal((short)(u16)(REG8(0x00114D89UL) + 1), buf);
        buf[2] = 0x00;
        text_top_CB(buf);
    }

    if (REG8(0x00114D8DUL) != 0) {
        int_to_decimal((short)(u16)REG8(0x00114D8DUL), buf);
        buf[2] = 0x00;
        text_top_102(buf);
    }
}

/* ---- under the module's state machine ---------------------------------
 * H'235B0E is the module's own state machine, eighteen states deep, and it
 * is what H'237E3C waits on. These are the routines beneath it: the panel it
 * draws, the arrows, the progress bar, and the small tests it asks.
 */

/* Pictures for the two arrows, the same four H'229714 uses. */
#define ARROW_BACK_LIT  0x0034E390UL
#define ARROW_BACK_DIM  0x0034E3C7UL
#define ARROW_FWD_LIT   0x0034E3FEUL
#define ARROW_FWD_DIM   0x0034E435UL

/* H'244CF6. Either of the two stop keys down: bit 2 or bit 6 of H'FFFEDB,
 * which H'21F68C knows as keys H'77 and H'7A. */
u8 stop_key_down(void)
{
    const u8 keys = REG8(0x00FFFEDBUL);

    if (keys & 0x04) return 0x01;
    if (keys & 0x40) return 0x01;
    return 0x00;
}

/* H'244D4E. The module's hardware state is one of the two that mean it is
 * running. */
u8 module_running(void)
{
    const u8 state = REG8(0x00FFFEC0UL);

    if (state == 0x04 || state == 0x06) return 0x01;
    return 0x00;
}

/* H'244D10. Two edges left by the interrupt in H'FFFEF7, each handed to the
 * link as an owner and then cleared. H'01 means neither was waiting. */
u8 module_edge_service(void)
{
    const u8 edges = REG8(0x00FFFEF7UL);

    if (edges & 0x02) {
        link_claim(0x09);
        REG8(0x00FFFEF7UL) &= (u8)~0x02;
        return 0x00;
    }
    if (edges & 0x04) {
        link_claim(0x1C);
        REG8(0x00FFFEF7UL) &= (u8)~0x04;
        return 0x00;
    }
    return 0x01;
}

/* H'24A2DA. The pause button in box seven drawn lit or not. Lit is two
 * calls, the first only when the box was already in kind one. */
void pause_button_draw(u8 lit)
{
    if (lit != 0) {
        if (hitbox_kind(0x0007) == 0x01) hitbox_set_state(0x0007, 0x0007, 0x00, 0);
        hitbox_set_state(0x0007, 0x0007, 0x01, 0);
        return;
    }
    hitbox_set_state(0x0007, 0x0007, 0x00, 0);
}

/* H'23E366. The pause button pressed: the module told to hold or to carry on,
 * depending on H'11F30E, and the button redrawn to match. Nothing happens
 * unless the hardware is at rest and the link is quiet. */
void module_pause_toggle(void)
{
    if (REG8(0x00FFFEC6UL) != 0) return;

    if (module_running() == 0) { link_claim(0x03); return; }

    if (REG8(0x00114DBAUL) != 0) return;

    if (REG8(0x0011F30EUL) == 0x00) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        REG8(0x0011F30EUL) = 0x01;
        pause_button_draw(0x01);
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x20;
        link_send_start();
        return;
    }

    if (REG8(0x0011F30EUL) == 0x01) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        REG8(0x0011F30EUL) = 0x00;
        pause_button_draw(0x00);
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x21;
        link_send_start();
        return;
    }

    REG8(0x0011F30EUL) = 0x00;
}

/* H'244DA0. The lid: bit 4 of H'FFFEC4 open. Opening it either releases the
 * pause or claims the link, and closing it sets H'114D98. H'114D86 holding
 * anything at all stops the lot. */
u8 module_lid_check(void)
{
    if (REG8(0x00114D86UL) == 0) {
        if (REG8(0x00FFFEC4UL) & 0x10) {
            REG8(0x00114D98UL) = 0x00;
            if (REG8(0x0011F30EUL) != 0) { module_pause_toggle(); return 0x01; }
            link_claim(0x01);
            return 0x01;
        }
        REG8(0x00114D98UL) = 0x01;
    }
    return 0x00;
}

/* H'24A432. The little picture box beside the module panel, in one of three
 * ways: cleared, drawn from H'34C148, or filled with colour two. Where it
 * goes depends on H'114D8E, and five of the twelve states have no box. */
void module_panel_box(u8 mode)
{
    u16 x, y;

    if (REG8(0x00114DB9UL) != 0) return;
    if (REG8(0x0011B0A8UL) != 0) return;

    switch (REG8(0x00114D8EUL)) {
    case 0x07:                            x = 0x00F1; y = 0x00C8; break;
    case 0x02: case 0x03:                 x = 0x00F0; y = 0x0029; break;
    case 0x06:                            x = 0x00EF; y = 0x002C; break;
    case 0x04: case 0x05: case 0x08:
    case 0x09:                            x = 0x00F1; y = 0x002C; break;
    default:                              return;
    }

    if (mode == 0x00)
        draw_rect(x, y, (u16)(x + 0x22), (u16)(y + 0x22),
                  LCD_FRAME_A, 0x00, 0x01);
    else if (mode == 0x01)
        bitmap_draw(x, y, (u16)(x + 0x22), (u16)(y + 0x22),
                    (const u8 *)0x0034C148UL, LCD_FRAME_A);
    else if (mode == 0x02)
        draw_rect(x, y, (u16)(x + 0x22), (u16)(y + 0x22),
                  LCD_FRAME_A, 0x02, 0x01);
}

/* H'24A572 and H'24A5D0. A run of dots plotted from a table of offsets at
 * H'11A1F0 -- pairs of bytes, x then y, added to the origin given. The first
 * lays down twenty-six of them and the second forty-three, out of the same
 * table. */
static void module_dots(u16 x0, u16 y0, short n)
{
    short i;

    for (i = 0; i < n; i += 2)
        plot_pixel((u16)(x0 + REG8(0x0011A1F0UL + (u32)(long)i)),
                   (u16)(y0 + REG8(0x0011A1F1UL + (u32)(long)i)),
                   LCD_FRAME_A, 0x03);
}

void module_dots_small(u16 x0, u16 y0) { module_dots(x0, y0, 0x0034); }
void module_dots_large(u16 x0, u16 y0) { module_dots(x0, y0, 0x0056); }

/* H'2499BE, H'249A0A, H'249A56, H'249AA2. Four arrows, each a box and a
 * picture: the forward arrow in boxes three and two, the back arrow in boxes
 * two and one. */
void module_arrow_fwd_3(u8 lit)
{
    hitbox_blit(0x0003, LCD_FRAME_A, lit ? ARROW_FWD_LIT : ARROW_FWD_DIM);
}

void module_arrow_back_2(u8 lit)
{
    hitbox_blit(0x0002, LCD_FRAME_A, lit ? ARROW_BACK_LIT : ARROW_BACK_DIM);
}

void module_arrow_fwd_2(u8 lit)
{
    hitbox_blit(0x0002, LCD_FRAME_A, lit ? ARROW_FWD_LIT : ARROW_FWD_DIM);
}

void module_arrow_back_1(u8 lit)
{
    hitbox_blit(0x0001, LCD_FRAME_A, lit ? ARROW_BACK_LIT : ARROW_BACK_DIM);
}

/* H'217AEA. The progress bar across the module panel, drawn into frame B
 * between H'28 and H'BF at rows H'2E to H'4C. The scale is worked out in
 * floating point: a hundred and fifty-one pixels over ninety-nine steps, so
 * one per cent is H'1.5252526 and the zero point is H'28 less that.
 *
 * A value outside nought to a hundred draws nothing at all. */
void module_progress_bar(u16 percent)
{
    const float step = 1.5252526f;
    const float base = 40.0f - step;

    if ((short)percent < 0) return;
    if ((short)percent > 0x0064) return;

    if (percent == 0x0064) {
        draw_rect(0x0028, 0x002E, 0x00BF, 0x004C, LCD_FRAME_B, 0x02, 0x01);
        return;
    }
    if (percent == 0x0000) {
        draw_rect(0x0028, 0x002E, 0x00BF, 0x004C, LCD_FRAME_B, 0x00, 0x01);
        return;
    }

    {
        const u16 edge = (u16)(int)((float)(long)(short)percent * step
                                    + base + 0.5f);

        draw_rect(0x0028, 0x002E, edge, 0x004C, LCD_FRAME_B, 0x02, 0x01);
        draw_rect((u16)(edge + 1), 0x002E, 0x00BF, 0x004C,
                  LCD_FRAME_B, 0x00, 0x01);
    }
}

/* H'2498BE. The panel box blinked while the module is working: on for ten
 * passes, off for ten more, and the counter reset when it runs past twenty.
 * A stop key held freezes it wherever it is. */
void module_panel_blink(u8 on)
{
    const u16 period = 0x000A;

    if (on == 0) {
        module_panel_box(0x02);
        REG16(0x0011F5A2UL) = 0x0000;
        return;
    }

    if (stop_key_down() != 0) return;

    REG16(0x0011F5A2UL) = (u16)(REG16(0x0011F5A2UL) + 1);

    if (REG16(0x0011F5A2UL) != 0 && period > REG16(0x0011F5A2UL)) {
        module_panel_box(0x01);
        return;
    }

    module_panel_box(0x00);
    if ((u16)(period << 1) >= REG16(0x0011F5A2UL)) return;
    REG16(0x0011F5A2UL) = 0x0000;
}

/* H'24992E. The progress bar kept up to date, but only in state seven and
 * only while the link is unclaimed. Anything over a hundred is pinned to a
 * hundred unless it is exactly H'C8, which means finished. H'11F5A4 holds
 * what was last drawn so a repeat costs nothing.
 *
 * The second argument is defaulted to H'0A and then never used again. The
 * original writes the default back into the caller's own stack slot, which
 * is a store the reconstruction has no way to make; the cases leave that
 * address out.
 */
void module_speed_show(u16 value, u16 hold)
{
    if (REG8(0x00114DB9UL) != 0) return;
    if (REG8(0x00114D8EUL) != 0x07) return;

    if ((short)value > 0x0064 && value != 0x00C8) value = 0x0064;
    if (hold == 0) hold = 0x000A;
    (void)hold;

    if (value == 0x0000) {
        REG16(0x0011F5A4UL) = 0x0000;
        module_progress_bar(0x0000);
        return;
    }
    if (value == 0x00C8) {
        REG16(0x0011F5A4UL) = 0x0000;
        module_progress_bar(0x0064);
        return;
    }

    if (stop_key_down() != 0) return;

    if (REG16(0x0011F5A4UL) != value) {
        module_progress_bar(value);
        REG16(0x0011F5A4UL) = value;
    }
}

/* H'23E1A0. A countdown in H'11F572 driving bit 0 of H'114D58 -- the thing
 * that flashes while the module is doing something. What arrives says what
 * to do: H'00 is a tick, H'01 starts it, H'02 stops it, H'03 stops it only
 * when the hardware is at rest, and H'04 starts it unless H'114D96 says not.
 *
 * A tick answers H'01 on the last one and H'05 on the one before, which is
 * how the caller knows the run is ending. */
u8 module_flash_step(u8 what)
{
    if (what == 0x00) {
        const u8 n = REG8(0x0011F572UL);

        if (n != 0) {
            if (n == 0x01) {
                REG8(0x0011F572UL) = 0x00;
                REG8(0x00114D58UL) &= (u8)~0x01;
                return 0x01;
            }
            if (n == 0x02) {
                REG8(0x0011F572UL) = (u8)(n - 1);
                return 0x05;
            }
            REG8(0x0011F572UL) = (u8)(n - 1);
        }
        return 0x00;
    }

    if (what == 0x01) {
        REG8(0x00114D58UL) |= 0x01;
        REG8(0x0011F572UL) = 0x07;
    } else if (what == 0x04) {
        if (REG8(0x00114D96UL) == 0) {
            REG8(0x00114D58UL) |= 0x01;
            REG8(0x0011F572UL) = 0x07;
        }
    } else if (what == 0x02) {
        REG8(0x00114D58UL) &= (u8)~0x01;
        REG8(0x0011F572UL) = 0x00;
    } else if (what == 0x03) {
        const u8 state = REG8(0x00FFFEC6UL);

        if (state == 0x00 || state == 0x05) REG8(0x0011F572UL) = 0x00;
    }
    return 0x00;
}

/* H'242FEA. How many minutes are left, worked out in floating point and
 * drawn in the third left-hand label only when the number changes.
 *
 * Two ways round, by H'114D96. One divides the stitches done by the total;
 * the other scales by a second pair of counters first. Either way the answer
 * is the pattern's own rate times what is left to do, plus one.
 *
 * [mode] H'00 forgets the last number and draws nothing, H'01 refuses to say
 * nought, H'02 always says nought.
 *
 * The ROM leaves its working slot alone when H'114D96 is neither nought nor
 * one, and converts whatever the stack happened to hold. That cannot be
 * written down, so this starts the slot at nought; no case goes there.
 */
void module_minutes_left(u8 mode)
{
    float left = 0.0f;
    u16 rate;

    if (mode == 0) { REG16(0x0011F592UL) = 0x0000; return; }

    REG32(0x0011A654UL) = REG32(0x0011A654UL) + 2;
    REG32(0x0011A65CUL) = REG32(0x0011A65CUL) + 2;

    rate = REG16(0x00104C96UL +
                 (u32)(long)(short)(u16)((u16)REG8(PAT_B(0x00)) << 1));

    if (REG8(0x00114D96UL) == 0x01) {
        left = (float)(u32)rate
             * (1.0f - (float)(u32)REG32(0x0011A654UL)
                     / (float)(u32)REG32(0x0011A650UL))
             + 1.0f;
    } else if (REG8(0x00114D96UL) == 0x00) {
        const float part = (float)(u32)rate
                         * (float)(u32)REG32(0x0011A658UL)
                         / (float)(u32)REG32(0x0011A650UL);

        left = part * (1.0f - (float)(u32)REG32(0x0011A65CUL)
                            / (float)(u32)REG32(0x0011A658UL))
             + 1.0f;
    }

    REG16(0x0011F4E4UL) = (u16)(int)left;

    if (mode == 0x02) REG16(0x0011F4E4UL) = 0x0000;
    else if (mode == 0x01) {
        if (REG16(0x0011F4E4UL) == 0) REG16(0x0011F4E4UL) = 0x0001;
    }

    if (REG16(0x0011F4E4UL) != REG16(0x0011F592UL)) {
        label_minutes((short)REG16(0x0011F4E4UL));
        REG16(0x0011F592UL) = REG16(0x0011F4E4UL);
    }
}

/* H'242F26. Message H'04/H'0E sent and then, on the next pass, the estimate
 * forgotten and the caller's step counter moved on. Both halves wait for the
 * link to be quiet. */
void module_ask_time(u8 *step)
{
    if (REG8(0x0011F590UL) == 0x00) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x04;
        REG8(0x0011F2A2UL) = 0x0E;
        link_send_start();
        REG8(0x0011F590UL) = (u8)(REG8(0x0011F590UL) + 1);
        return;
    }

    if ((REG8(0x00114D50UL) & 0x21) != 0) return;
    if (REG8(0x0011F29EUL) != 0) return;
    if (REG8(0x0011F2B6UL) != 0) return;
    if ((REG8(0x00114D50UL) & 0x22) != 0) return;
    if (REG8(0x0011F29EUL) != 0) return;
    if (REG8(0x0011F2B6UL) != 0) return;

    module_minutes_left(0x00);
    REG8(0x0011F590UL) = 0x00;
    *step = (u8)(*step + 1);
}

/* ---- the colour picture ------------------------------------------------
 * The module's pattern data at H'104D4A is a run of records, each two words
 * of size followed by that many bytes, and H'238B62 draws the one belonging
 * to a colour as a one-bit-per-pixel stencil.
 */

/* H'238D06. Walks [index] records forward from H'104D4A and reads the four
 * words at the head of the one it lands on, handing back the address just
 * past them. Every word is big-endian and put together a byte at a time.
 *
 * The record's length is its first two words multiplied together plus eight,
 * and the running offset is kept in sixteen bits, so a long enough list
 * wraps -- which is what the caller's limit check is there to catch. */
const u8 *module_colour_record(u16 *w, u16 *h, u16 *px, u16 *py, u8 index)
{
    const u8 *p = (const u8 *)0x00104D4AUL;
    u16 offset = 0;
    u16 i;

    for (i = 0; (u16)index > i; i++) {
        const u16 a = (u16)(((u16)p[0] << 8) | (u16)p[1]);
        const u16 b = (u16)(((u16)p[2] << 8) | (u16)p[3]);

        offset = (u16)(offset + (u16)((u16)(b * a) + 8));
        p = (const u8 *)(0x00104D4AUL + (u32)offset);
    }

    *w  = (u16)(((u16)p[0] << 8) | (u16)p[1]);
    *h  = (u16)(((u16)p[2] << 8) | (u16)p[3]);
    *px = (u16)(((u16)p[4] << 8) | (u16)p[5]);
    *py = (u16)(((u16)p[6] << 8) | (u16)p[7]);
    return p + 8;
}

/* H'238B62. The colour's outline drawn over the panel. The panel is fetched
 * back from the third store first, so what was there is rubbed out, and then
 * the record's bytes are walked bit by bit -- most significant first -- with
 * a pixel put down wherever a bit is set.
 *
 * Where it goes is the record's own two words, shifted by the hoop offset in
 * H'1040BE and H'1040BF: eight times the first less four times the hoop's x,
 * plus H'88, and the second less half the hoop's y, plus H'8B.
 *
 * Reading past H'114D49 sets bit H'2000 of H'114D4C and skips the byte. That
 * is the only guard: the record itself is trusted. */
void module_colour_bitmap(u8 index)
{
    const u16 vx = (u16)((short)(u16)((u16)REG8(0x001040BEUL) << 3) / (short)2);
    const u16 vy = (u16)((u16)REG8(0x001040BFUL) / 2);
    u16 w = 0, h = 0, px = 0, py = 0;
    const u8 *data;
    u16 x0, y0, row, col;

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;

    data = module_colour_record(&w, &h, &px, &py, index);

    x0 = (u16)((u16)((u16)(px << 1) << 2) - vx + 0x0088);
    y0 = (u16)(py - vy + 0x008B);

    region_copy(0x0026, 0x002C, 0x00EA, 0x00EA, 0x002C,
                0x000F1610UL, LCD_FRAME_A);

    for (row = 0; row < h; row++) {
        u16 xbit = 0;

        for (col = 0; col < w; col++) {
            const u32 at = (u32)data + (u32)(u16)((u16)(w * row) + col);
            u8 bits, mask;
            u16 k;

            if (at > 0x00114D49UL) {
                REG16(0x00114D4CUL) |= 0x2000;
                continue;
            }

            bits = REG8(at);
            mask = 0x80;
            for (k = 0; k < 8; k++) {
                if ((u8)(mask & bits) == mask)
                    plot_pixel((u16)(x0 + xbit), (u16)(y0 + row),
                               LCD_FRAME_A, 0x03);
                mask = (u8)(mask >> 1);
                xbit++;
            }
        }
    }
}

/* H'23865A. The same two numbers as H'236B5A, with the colour's picture
 * drawn under them and the cursor forgotten. The limit here is H'3C rather
 * than H'1B, and the numbers are only drawn when H'114D91 says so -- but the
 * picture is drawn either way, once, whichever of the two halves gets to it
 * first. */
void label_colours_picture(void)
{
    char buf[4];
    u8 drawn = 0;

    if (REG8(0x00114D89UL) > 0x3C) {
        REG16(0x00114D4CUL) |= 0x0400;
        return;
    }

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;

    if (REG8(0x00114D89UL) < REG8(0x00114D8DUL)) {
        int_to_decimal((short)(u16)(REG8(0x00114D89UL) + 1), buf);
        buf[2] = 0x00;
        if (REG8(0x00114D91UL) != 0) text_top_CB(buf);
        module_colour_bitmap(REG8(0x00114D89UL));
        drawn = 0x01;
    }

    if (REG8(0x00114D8DUL) != 0) {
        int_to_decimal((short)(u16)REG8(0x00114D8DUL), buf);
        buf[2] = 0x00;
        if (REG8(0x00114D91UL) != 0) text_top_102(buf);
        if (drawn == 0) module_colour_bitmap(REG8(0x00114D89UL));
    }
}

/* ---- the module panel's numbers ----------------------------------------
 * Three routines that put numbers on the embroidery panel. All of them
 * build their string in the RAM buffer at H'11F2D6 and spell the units out
 * into H'11F294 a byte at a time.
 */

/* H'23202A. How many stitches, drawn at one of two places depending on
 * H'114D8E, with a run of dots under it. The number is the pattern's own
 * count plus H'1B for every whole turn in the sixth byte of its record, and
 * when bit 1 of H'114D51 is set the count past H'114DBC is taken off and the
 * longer run of dots drawn instead.
 *
 * A record of kind three has no number at all. */
void module_count_label(void)
{
    u8 n, over = 0x00;
    u8 where;

    if (REG8(PAT_B(0x03)) == 0x03) return;

    REG8(0x0011F2DBUL) = 0x00;

    n = (u8)(REG8(PAT_B(0x00))
             + (u8)(u16)((u16)REG8(PAT_B(0x05)) * 0x1B));

    if (REG8(0x00114D51UL) & 0x02) {
        if (REG8(PAT_B(0x03)) != 0x01) {
            const u8 limit = REG8(0x00114DBCUL);

            if (n > limit) { n = (u8)(n - limit); over = 0x01; }
        }
    }

    int_to_decimal((short)(u16)n, (char *)0x0011F2D6UL);

    where = REG8(0x00114D8EUL);

    if (where >= 0x08 && where <= 0x09) {
        text_draw((const char *)0x0011F2D6UL, 0x000F, 0x0098, 0x001E, 0x00A0,
                  0x0001, 0x02, (const u8 *)0x001196EAUL);
        if (over == 0x01) module_dots_large(0x0002, 0x0098);
        else              module_dots_small(0x0008, 0x0098);
        return;
    }

    if (where >= 0x04 && where <= 0x05) {
        text_draw((const char *)0x0011F2D6UL, 0x000F, 0x004A, 0x001E, 0x0052,
                  0x0001, 0x02, (const u8 *)0x001196EAUL);
        if (over == 0x01) module_dots_large(0x0002, 0x004A);
        else              module_dots_small(0x0008, 0x004A);
    }
}

/* H'231C5E. How wide and how tall, in millimetres, in the first two
 * left-hand labels. The two numbers arrive in stitches and are scaled by the
 * first two bytes of the stitch record: twice the count times the byte, over
 * a hundred, in floating point and in that order. */
void module_size_labels(short across, short down)
{
    const int w = (int)((float)(long)across * 2.0f
                        * (float)(u32)REG8(PAT_A(0x00)) / 100.0f);
    const int h = (int)((float)(long)down * 2.0f
                        * (float)(u32)REG8(PAT_A(0x01)) / 100.0f);

    int_to_decimal((short)w, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x6D;   /* 'm' */
    REG8(0x0011F295UL) = 0x6D;   /* 'm' */
    REG8(0x0011F296UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_D9((const char *)0x0011F2D6UL);

    int_to_decimal((short)h, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x6D;
    REG8(0x0011F295UL) = 0x6D;
    REG8(0x0011F296UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_BC((const char *)0x0011F2D6UL);

    module_count_label();
}

/* H'23228A. How far through, as a percentage with a "%" after it, drawn low
 * on the right. The sum is H'114DBE tenths plus H'114DBF, and the answer is
 * five thousand seven hundred and eighty over that -- H'57.8 times a hundred,
 * multiplied out at run time rather than folded.
 *
 * Six bytes are copied out of H'25077C into the buffer first and then
 * written straight over by the number. Reproduced because it is there. */
void label_percent(void)
{
    char buf[18];
    float sum;

    REG16((u32)(unsigned long)&buf[0]) = REG16(0x0025077CUL);
    REG16((u32)(unsigned long)&buf[2]) = REG16(0x0025077EUL);
    REG16((u32)(unsigned long)&buf[4]) = REG16(0x00250780UL);

    sum = (float)(u32)REG8(0x00114DBEUL) / 10.0f
        + (float)(u32)REG8(0x00114DBFUL);

    int_to_decimal((short)(int)(57.8f * 100.0f / sum), buf);

    REG8(0x0011F294UL) = 0x25;   /* '%' */
    REG8(0x0011F295UL) = 0x00;
    str_append(buf, (const char *)0x0011F294UL);

    text_draw(buf, 0x00F2, 0x00A3, 0x0112, 0x00AE, 0x0001, 0x02,
              (const u8 *)0x00119A66UL);
}

/* H'23BB18. The check before a pattern is sewn: the module is asked for its
 * hoop, and the answer compared against the three stitch-record bytes it was
 * last asked about. Nothing happens at all unless the pattern's attribute
 * bit says it needs one.
 *
 * H'11F565 walks the four steps -- ask, wait, take the answer, done -- and
 * H'104043 is the once-only reset that starts it. Step nought short-circuits
 * the lot when the three bytes have not moved since last time.
 *
 * The blinking box is driven from here too: on while the link is busy, off
 * once the answer is in, and never in state seven.
 */
void module_hoop_check(u8 *step)
{
    u8 n;

    if (pattern_attr_bit3() == 0) { *step = (u8)(*step + 1); return; }

    if (REG8(0x00104043UL) == 0) {
        REG8(0x0011F4D6UL) = 0x00;
        REG8(0x0011F4D7UL) = 0x00;
        REG8(0x0011F4D8UL) = 0x00;
        REG8(0x0011F565UL) = 0x00;
        REG8(0x00104043UL) = 0x01;
    }

    n = REG8(0x0011F565UL);

    if (n == 0x00 || n == 0x01) {
        if (n == 0x00) {
            if (REG8(PAT_A(0x00)) == REG8(0x0011F4D6UL) &&
                REG8(PAT_A(0x01)) == REG8(0x0011F4D7UL) &&
                REG8(PAT_A(0x06)) == REG8(0x0011F4D8UL)) {
                *step = (u8)(*step + 1);
                return;
            }
            REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        }

        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61BUL) = 0x05;
        link_send_start();
        REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        return;
    }

    if (n == 0x02) {
        if ((REG8(0x00114D50UL) & 0x21) != 0 ||
            REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
            if (REG8(0x00114D8EUL) != 0x07) module_panel_blink(0x01);
            return;
        }

        REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        REG8(0x0011F4D6UL) = REG8(PAT_A(0x00));
        REG8(0x0011F4D7UL) = REG8(PAT_A(0x01));
        REG8(0x0011F4D8UL) = REG8(PAT_A(0x06));
        REG8(0x00114D8DUL) = REG8(0x00104045UL);

        if (REG8(0x00104041UL) != REG8(0x00114D8DUL)) {
            REG8(0x00104041UL) = REG8(0x00114D8DUL);
            REG8(0x00114D92UL) = 0xFF;
        }

        if (REG8(0x00114D8EUL) != 0x07) module_panel_blink(0x00);
        return;
    }

    if (n == 0x03) {
        if (REG8(0x00114D8DUL) < REG8(0x00104044UL)) {
            module_panel_blink(0x00);
            link_claim(0x19);
        }
        REG8(0x0011F565UL) = (u8)(REG8(0x0011F565UL) + 1);
        return;
    }

    if (REG8(0x00114DB9UL) == 0) {
        REG8(0x0011F565UL) = 0x00;
        *step = (u8)(*step + 1);
    }
}

/* ---- the module's floating point ---------------------------------------
 * The embroidery maths needs a square root, a sine, a cosine and an arc
 * tangent, and the ROM carries its own: H'24ABFE, H'24ABEE, H'24ADDC and
 * H'24ABC4, with H'24AF80, H'24AE5A, H'24AD22 and H'24AD62 under them.
 *
 * Every constant is written here as the decimal that encodes to the ROM's
 * exact bit pattern, and every operation is in the order the ROM does it,
 * so the answers agree to the last bit. The three routines that take a
 * float apart do it on the bit pattern, which is what the H8 does with
 * ADD.W and AND.W on the top half of a register.
 */
typedef union { float f; u32 u; } f32bits;

static u32 f2u(float f) { f32bits b; b.f = f; return b.u; }
static float u2f(u32 u) { f32bits b; b.u = u; return b.f; }

/* H'24AD22. Splits a float into a fraction in [H'0.5, 1) and a power of two,
 * the C library's frexp. The exponent is worked out a byte at a time and
 * comes back sign extended from eight bits. */
float float_frexp(float x, short *power)
{
    const u32 u = f2u(x);
    u16 hi;
    u8 e;

    if (u == 0) { *power = 0; return u2f(0); }

    hi = (u16)(u >> 16);
    e  = (u8)((u16)((u16)(hi << 1)) >> 8);
    e  = (u8)(e + 0x82);
    *power = (short)(signed char)e;

    return u2f((u & 0x0000FFFFUL)
               | ((u32)(u16)(((u16)(hi & 0x807F)) | 0x3F00) << 16));
}

/* H'24AD62. The whole part out into [ip] and the fraction back, the C
 * library's modf. Anything H'1E7 or bigger is all whole part. */
float float_modf(float x, float *ip)
{
    if ((long)(f2u(x) & 0x7FFFFFFFUL) >= (long)0x4B189680UL) {
        *ip = x;
        return u2f(0);
    }
    *ip = (float)(long)(int)x;
    return x - *ip;
}

/* H'24AE5A. The arc tangent of the size of a number, by a rational fit.
 * Above one the argument is folded to (a-1)/(a+1) and a quarter pi added
 * back at the end. */
float float_atan_core(float x)
{
    float a = u2f(f2u(x) & 0x7FFFFFFFUL);
    float hi, t, n, d;

    if ((long)f2u(a) <= (long)0x3F800000UL) {
        if (f2u(a) == 0x3F800000UL) return 0.7853982f;
        hi = 0.0f;
    } else {
        hi = 0.7853982f;
        a = (a + -1.0f) / (a + 1.0f);
    }

    t = a * a;
    n = ((t * -0.010497842f + 0.3247416f) * t + 2.9960995f) * t + 3.640485f;
    d = (t + 4.209584f) * t + 3.6404853f;

    return hi + a * (n / d);
}

/* H'24ABC4. The arc tangent proper: the size, and then the sign put back
 * unless the answer came out as a positive zero. */
float float_atan(float x)
{
    const float r = float_atan_core(x);

    if ((long)f2u(x) >= 0) return r;
    if ((u16)(f2u(r) >> 16) == 0) return r;
    return u2f(f2u(r) ^ 0x80000000UL);
}

/* H'24AF80. Sine and cosine share one routine: the argument is scaled by
 * two over pi, the whole quarter-turns taken off, and what is left run
 * through a five-term fit of sin(pi/2 x). [quad] is 0 for sine and 1 for
 * cosine, and a negative argument adds two more quarter turns.
 *
 * Above 65532 quarter-turns the reduction is done the long way, with two
 * calls to H'24AD62, because (float)(int) would have nothing left to say. */
float float_sincos(float x, u8 quad)
{
    u8 n = (u8)(quad + (((long)f2u(x) < 0) ? 0x02 : 0x00));
    float y = u2f(f2u(x) & 0x7FFFFFFFUL) * 0.63661975f;
    float f, t, p;

    if ((long)f2u(y) > (long)0x477FFC00UL) {
        float whole, quarters;

        f = float_modf(y, &whole);
        whole = whole + (float)(u32)n;
        float_modf(whole * 0.25f, &quarters);
        n = (u8)(int)(whole - quarters * 4.0f);
    } else {
        const int k = (int)y;

        f = y - (float)(u32)k;
        n = (u8)((u8)(n + (u8)k) & 0x03);
    }

    if (n & 0x01) f = 1.0f - f;
    if (n > 0x01) { if ((u16)(f2u(f) >> 16) != 0) f = u2f(f2u(f) ^ 0x80000000UL); }

    t = f * f;
    p = (((t * 0.00015148513f + -0.0046737664f) * t + 0.07968968f) * t
         + -0.6459637f) * t + 1.5707964f;

    return f * p;
}

/* H'24ABEE and H'24ADDC. Sine, and cosine of the size -- cosine is even, so
 * dropping the sign first costs nothing. */
float float_sin(float x) { return float_sincos(x, 0x00); }

float float_cos(float x)
{
    return float_sincos(u2f(f2u(x) & 0x7FFFFFFFUL), 0x01);
}

/* H'24ABFE. The square root: a first guess out of the mantissa, one
 * Newton step, the exponent halved by adding to the top half of the float,
 * and one more step. Nought comes back as nought; anything below it sets
 * H'11F5A6 to H'21 and comes back as the largest float there is. */
float float_sqrt(float x)
{
    short power;
    float m, e;

    if ((long)f2u(x) <= 0) {
        if (f2u(x) == 0) return u2f(0);
        REG16(0x0011F5A6UL) = 0x0021;
        return 3.4028235e+38f;
    }

    m = float_frexp(x, &power);
    e = (m + 0.75787f) * 0.57155f;
    e = e + m / e;

    /* the exponent is read back as a single byte, halved as a signed byte,
     * and the result shifted into the top half of the float */
    if (power & 0x0001) {
        const short q = (short)((short)(signed char)(u8)(power - 1) / 2);

        e = e * 1.4142135f;
        e = u2f((f2u(e) & 0x0000FFFFUL)
                | ((u32)(u16)((u16)(f2u(e) >> 16) + (u16)(short)(q << 7))
                   << 16));
    } else {
        const short q = (short)((short)(signed char)(u8)power / 2);

        e = u2f((f2u(e) & 0x0000FFFFUL)
                | ((u32)(u16)((u16)(f2u(e) >> 16) + (u16)(short)(q << 7))
                   << 16));
    }

    return x / e + e * 0.25f;
}

/* H'241228. Finds one pattern's header in the stream at H'10C27A and copies
 * it into the H'15-byte record the caller hands over.
 *
 * The stream is a run of blocks, each three bytes of kind, count and size
 * followed by its data: kind H'01 is count * size bytes and kind H'02 is
 * eight. Anything else, or running past H'1137AA, sets bit 1 of H'11A641
 * and gives up. The step of H'0C between blocks is byte H'14 of the record,
 * which this routine sets to H'0F itself and nothing else touches.
 *
 * The index is decremented in the caller's own stack slot, a store the
 * reconstruction cannot make; the cases leave that address out. */
u8 pattern_record_at(u8 *rec, u8 index)
{
    const u32 limit = 0x001137AAUL;
    u32 p = 0x0010C27AUL;
    u8 i;

    rec[0x14] = 0x0F;
    if (index != 0) index = (u8)(index - 1);

    for (i = 0; i < index; i++) {
        const u8 kind = REG8(p);
        const u8 count = REG8(p + 1);
        const u8 size = REG8(p + 2);

        p += 3;
        p = (u32)(p + (u32)(long)(short)(u16)((u16)rec[0x14] - 3));

        if (kind == 0x01)      p = (u32)(p + (u32)(u16)((u16)count * (u16)size));
        else if (kind == 0x02) p = p + 8;
        else { REG8(0x0011A641UL) |= 0x02; return 0x00; }

        if (p >= limit) { REG8(0x0011A641UL) |= 0x02; return 0x00; }
    }

    REG32((u32)(unsigned long)(rec + 0x10)) = p;

    rec[0] = REG8(p);
    rec[1] = REG8(p + 1);
    rec[2] = REG8(p + 2);
    rec[3] = REG8(p + 3);
    rec[4] = REG8(p + 4);

    REG16((u32)(unsigned long)(rec + 0x06)) =
        (u16)(((u16)REG8(p + 5) << 8) | (u16)REG8(p + 6));
    REG16((u32)(unsigned long)(rec + 0x08)) =
        (u16)(((u16)REG8(p + 7) << 8) | (u16)REG8(p + 8));
    REG16((u32)(unsigned long)(rec + 0x0A)) =
        (u16)(((u16)REG8(p + 9) << 8) | (u16)REG8(p + 10));
    REG16((u32)(unsigned long)(rec + 0x0C)) =
        (u16)(((u16)REG8(p + 11) << 8) | (u16)REG8(p + 12));
    REG16((u32)(unsigned long)(rec + 0x0E)) =
        (u16)(((u16)REG8(p + 13) << 8) | (u16)REG8(p + 14));

    return 0x01;
}

/* H'24217A. How far one pattern reaches from its own centre, along each
 * axis, once its own scale and rotation are applied.
 *
 * The pattern's header gives a width and a height in fifths of a
 * millimetre; H'11A24A and H'11A24B scale them (twice the byte, over a
 * hundred), H'11A250 turns them (five degrees a step, minus a hundred and
 * eighty, into radians) and H'11A24F says which way round.
 *
 * The four corners are worked out from the half-diagonal and the angle to
 * it, and the largest size along each axis is what comes back.
 *
 * When the scaled width is nought the angle to the corner is never worked
 * out and the ROM uses whatever the stack held; this starts it at nought,
 * and no case goes there. */
void pattern_half_extent(u16 *half_w, u16 *half_h, u8 slot)
{
    u8 rec[0x18];
    const u32 base = 0x0011A25AUL + (u32)(long)(short)(u16)((u16)slot << 4);
    const float sx = (float)(u32)REG8(base - 0x10) * 2.0f / 100.0f;
    const float sy = (float)(u32)REG8(base - 0x0F) * 2.0f / 100.0f;
    float hw, hh, angle, theta = 0.0f, r;
    float cx[4], cy[4];
    const u8 flip = REG8(base - 0x0B);
    short i;

    pattern_record_at(rec, slot);

    hw = (float)(long)(short)REG16((u32)(unsigned long)(rec + 0x0A)) / 5.0f;
    hh = (float)(long)(short)REG16((u32)(unsigned long)(rec + 0x0C)) / 5.0f;
    hw = hw * sx;
    hh = hh * sy;

    angle = ((float)(u32)REG8(0x0011A250UL) * 5.0f + -180.0f) * 0.017453277f;

    if (f2u(hw) != 0) theta = float_atan(hh / hw);

    r = float_sqrt(hh * hh + hw * hw) / 2.0f;

    {
        const float nr = ((u16)(f2u(r) >> 16) != 0)
                         ? u2f(f2u(r) ^ 0x80000000UL) : r;
        const float a  = theta - angle;
        const float b  = (((u16)(f2u(theta) >> 16) != 0)
                          ? u2f(f2u(theta) ^ 0x80000000UL) : theta) - angle;

        if (flip == 0) {
            cx[0] = float_cos(a) * r;   cy[0] = float_sin(a) * r;
            cx[1] = float_cos(b) * r;   cy[1] = float_sin(b) * r;
            cx[2] = nr * float_cos(a);  cy[2] = nr * float_sin(a);
            cx[3] = nr * float_cos(b);  cy[3] = nr * float_sin(b);
        } else {
            cx[0] = nr * float_cos(a);  cy[0] = float_sin(a) * r;
            cx[1] = nr * float_cos(b);  cy[1] = float_sin(b) * r;
            cx[2] = float_cos(a) * r;   cy[2] = nr * float_sin(a);
            cx[3] = float_cos(b) * r;   cy[3] = nr * float_sin(b);
        }
    }

    *half_w = (u16)(int)u2f(f2u(cx[0]) & 0x7FFFFFFFUL);
    *half_h = (u16)(int)u2f(f2u(cy[0]) & 0x7FFFFFFFUL);

    for (i = 1; i < 4; i++) {
        const u16 x = (u16)(int)u2f(f2u(cx[i]) & 0x7FFFFFFFUL);
        const u16 y = (u16)(int)u2f(f2u(cy[i]) & 0x7FFFFFFFUL);

        if ((short)*half_w < (short)x) *half_w = x;
        if ((short)*half_h < (short)y) *half_h = y;
    }
}

/* H'241FA0. Every pattern in the run taken together, and the current slot's
 * centre moved to the middle of the box that holds them all. Each pattern is
 * asked for its own reach around the centre it is pinned to, and the centres
 * live sixteen bytes apart at H'11A266 and H'11A268. */
void pattern_run_extent(void)
{
    u16 hw = 0, hh = 0;
    short minx, maxx, miny, maxy;
    u8 i;

    pattern_half_extent(&hw, &hh, 0x01);

    minx = (short)(REG16(0x0011A266UL) - hw);
    maxx = (short)(REG16(0x0011A266UL) + hw);
    miny = (short)(REG16(0x0011A268UL) - hh);
    maxy = (short)(REG16(0x0011A268UL) + hh);

    for (i = 0; (short)(u16)REG8(0x0011A640UL) > (short)(signed char)i; i++) {
        const u32 at = 0x0011A266UL
                     + (u32)(long)(short)((short)(signed char)i << 4);

        pattern_half_extent(&hw, &hh, (u8)(i + 1));

        if ((short)(REG16(at) - hw) < minx) minx = (short)(REG16(at) - hw);
        if ((short)(REG16(at) + hw) > maxx) maxx = (short)(REG16(at) + hw);
        if ((short)(REG16(at + 2) - hh) < miny) miny = (short)(REG16(at + 2) - hh);
        if ((short)(REG16(at + 2) + hh) > maxy) maxy = (short)(REG16(at + 2) + hh);
    }

    {
        const u32 slot = 0x0011A266UL
                       + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4);

        REG16(slot)     = (u16)(short)(minx + (short)((short)(maxx - minx) / 2));
        REG16(slot + 2) = (u16)(short)(miny + (short)((short)(maxy - miny) / 2));
    }
}

u8 module_run_control(u8 what);
u8 module_can_talk(void);
void module_talk_end(void);
void label_percent_left(u8 value);
void module_stop_sequence(u8 *step);

/* The link-quiet test the machines make over and over: the two masks on
 * H'114D50 with the send and receive flags between them, six reads in a
 * fixed order. The ROM writes it out in full at every site. */
static u8 module_link_quiet(void)
{
    if ((REG8(0x00114D50UL) & 0x21) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    if ((REG8(0x00114D50UL) & 0x22) != 0) return 0x00;
    if (REG8(0x0011F29EUL) != 0) return 0x00;
    if (REG8(0x0011F2B6UL) != 0) return 0x00;
    return 0x01;
}

/* H'2417D4. Nine states that fetch the run of patterns out of the module and
 * set the machine up to sew them. H'11A63D walks the states and H'11A640 is
 * how many patterns there are.
 *
 * Nothing happens at all unless there is something to fetch: no patterns and
 * a record that is not kind three moves the caller's step on and leaves.
 *
 * States nought and one share a body -- nought only sets the counter to
 * H'FF first -- which is the same shared-tail trick the jump table itself
 * uses. Every state but the two early exits finishes by blinking the box.
 */
void module_fetch_step(u8 *step)
{
    u8 n;

    if (REG8(0x0011A640UL) == 0 && REG8(PAT_B(0x03)) != 0x03) {
        *step = (u8)(*step + 1);
        return;
    }

    n = REG8(0x0011A63DUL);
    if (n > 0x08) n = 0xFF;              /* anything past eight is the default */

    switch (n) {
    case 0x00:
        if (REG8(0x00114D51UL) & 0x40) { *step = (u8)(*step + 1); return; }
        if (!(REG8(0x0011A63CUL) & 0x02)) { *step = (u8)(*step + 1); return; }
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        REG8(0x0011F58BUL) = 0xFF;
        /* fall through -- states nought and one share a body */
        __attribute__((fallthrough));
    case 0x01:
        if (REG8(0x0011A640UL) == 0) {
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
            break;
        }
        if (!module_link_quiet()) break;
        {
            u8 slot = (u8)(REG8(0x0011F58BUL) + 1);
            u32 to, from;
            short k;

            REG8(0x0011F58BUL) = slot;
            REG8(0x0011A660UL) = slot;
            to   = 0x0011A25AUL + (u32)(long)(short)(u16)((u16)slot << 4);
            from = 0x0011F316UL + (u32)(long)(short)(u16)((u16)slot << 4);
            for (k = 0; k < 4; k++) REG32(to + (u32)k * 4) = REG32(from + (u32)k * 4);

            REG8(0x0011F2A1UL) = 0x02;
            link_send_start();

            /* the ROM compares a word whose high byte is whatever the
             * send left behind; it is nought on every path there is */
            if ((u16)((u16)REG8(0x0011A640UL) - 1) == (u16)REG8(0x0011A660UL))
                REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        break;

    case 0x02:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A61CUL) = 0x07;
            module_run_control(0x01);
            link_send_start();
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        break;

    case 0x03:
        REG8(0x0011A660UL) = REG8(0x0011A640UL);
        stitch_reset_current();
        pattern_run_extent();
        REG8(PAT_B(0x00)) = 0x1C;
        REG8(PAT_B(0x04)) = REG8(0x0011A640UL);
        REG8(PAT_B(0x03)) = 0x03;
        {
            const u32 w = (u32)(long)(short)(u16)((u16)REG8(PAT_B(0x00)) << 1);
            const u32 b = (u32)REG8(PAT_B(0x00));

            REG16(0x00104CCEUL + w) = 0x0000;
            REG16(0x00104D06UL + w) = 0x0000;
            REG16(0x00104C96UL + w) = 0x0000;
            REG8(0x000FFE9CUL + b) = 0x00;
            REG8(0x000FFEB8UL + b) = 0x00;
        }
        REG8(0x001040BCUL) = 0x00;
        REG8(0x001040BDUL) = 0x00;
        REG8(0x0011F58BUL) = 0x00;

        if (REG8(0x00114D8EUL) == 0x04) {
            int_to_decimal((short)((short)(signed char)REG8(0x0011F58BUL) + 1),
                           (char *)0x0011F2D6UL);
            REG8(0x0011F2D8UL) = 0x00;
            text_top_CB((const char *)0x0011F2D6UL);
            int_to_decimal((short)(u16)REG8(0x0011A640UL),
                           (char *)0x0011F2D6UL);
            REG8(0x0011F2D8UL) = 0x00;
            text_top_102((const char *)0x0011F2D6UL);
            label_percent_left(REG8(0x0011F530UL));
        } else if (REG8(0x00114D8EUL) == 0x07) {
            label_percent_left(REG8(0x0011F530UL));
            module_speed_show(0x0000, 0x0001);
        }
        REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        break;

    case 0x04:
        if (module_link_quiet()) {
            if (REG16(0x00114D4CUL) & 0x0002) REG8(0x0011F535UL) = 0x0E;
            module_run_control(0x00);
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);

            if (REG8(0x00114D8EUL) == 0x04) {
                int_to_decimal((short)(u16)REG8(0x0011A640UL),
                               (char *)0x0011F2D6UL);
                REG8(0x0011F2D8UL) = 0x00;
                text_top_CB((const char *)0x0011F2D6UL);
                text_top_102((const char *)0x0011F2D6UL);
                label_percent_left(0x64);
            } else if (REG8(0x00114D8EUL) == 0x07) {
                label_percent_left(0x64);
            }
            break;
        }

        if (REG8(0x00114D52UL) & 0x04) {
            const u8 v = (u8)(REG8(0x0011F58BUL) + 1);
            float pct;

            REG8(0x00114D52UL) &= (u8)~0x04;
            REG8(0x0011F530UL) = 0x01;
            REG8(0x0011F58BUL) = v;

            pct = (float)(long)(short)(signed char)v * 100.0f
                / (float)(u32)REG8(0x0011A640UL);
            if ((int)pct == 0) pct = pct + 1.0f;
            label_percent_left((u8)(int)pct);

            if (REG8(0x00114D8EUL) == 0x04) {
                if ((short)(u16)REG8(0x0011A640UL)
                    > (short)(signed char)REG8(0x0011F58BUL))
                    int_to_decimal((short)((short)(signed char)v + 1),
                                   (char *)0x0011F2D6UL);
                else
                    int_to_decimal((short)(signed char)v,
                                   (char *)0x0011F2D6UL);
                REG8(0x0011F2D8UL) = 0x00;
                text_top_CB((const char *)0x0011F2D6UL);
            }
        }
        {
            float done = ((float)(u32)REG8(0x0011F530UL)
                          + (float)(long)(short)(signed char)REG8(0x0011F58BUL)
                            * 100.0f)
                       / (float)(u32)REG8(0x0011A640UL);

            if ((int)done == 0) done = done + 1.0f;
            module_speed_show((u16)(int)done, 0x0001);
        }
        break;

    case 0x05:
        module_stop_sequence((u8 *)0x0011A63DUL);
        module_speed_show(0x0064, 0x0001);
        break;

    case 0x06:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A61CUL) = 0x08;
            link_send_start();
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        module_speed_show(0x0064, 0x0001);
        break;

    case 0x07:
        if (module_link_quiet()) {
            REG8(0x00114D8DUL) = REG8(0x000FFE9CUL + (u32)REG8(PAT_B(0x00)));
            REG8(0x00114D96UL) = pattern_attr_bit3() != 0 ? 0x01 : 0x00;
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        module_speed_show(0x0064, 0x0001);
        break;

    case 0x08:
        if (module_link_quiet()) {
            REG8(0x00114D92UL) = 0xFF;
            REG8(0x0011A63DUL) = (u8)(REG8(0x0011A63DUL) + 1);
        }
        module_speed_show(0x0064, 0x0001);
        break;

    default:
        if (REG8(0x0011F52CUL) == 0) REG8(0x00114D73UL) = 0x01;
        else                         REG8(0x00114D66UL) = 0x00;
        REG8(0x0011A63CUL) &= (u8)~0x02;
        *step = (u8)(*step + 1);
        REG8(0x0011A63DUL) = 0x00;
        module_speed_show(0x00C8, 0x0001);
        break;
    }

    module_panel_blink(0x01);
}

/* H'244F14. Whether the pattern, where it is placed and turned, still fits
 * inside the hoop.
 *
 * The same four corners as H'24217A, but this time with the hoop's own
 * middle and the pattern's placement added in, and each corner tested
 * against the hoop's width and height in H'11A626 and H'11A628. Any corner
 * past an edge, or behind the origin, and the answer is no.
 *
 * The corner comparisons go through H'20074A, which compares two floats by
 * their bit patterns -- and gets it right, including both-negative, so
 * ordinary C comparison does the same thing. */
u8 module_hoop_fits(void)
{
    const u32 a16 = 0x0011A25AUL
                  + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4);
    const u32 b18 = 0x0011A41AUL
                  + (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x0011A660UL));
    const u32 pat = (u32)(long)(short)(u16)((u16)REG8(b18) << 1);
    const float sx = (float)(u32)REG8(a16) * 2.0f / 100.0f;
    const float sy = (float)(u32)REG8(a16 + 1) * 2.0f / 100.0f;
    const float w  = (float)(u32)REG16(0x00104CCEUL + pat) * sx;
    const float h  = (float)(u32)REG16(0x00104D06UL + pat) * sy;
    const float angle = ((float)(u32)REG8(a16 + 6) * 5.0f + -180.0f)
                      * 0.017453277f;
    const u8 flip = REG8(a16 + 5);
    const float hoopx = (float)(u32)REG16(0x0011A626UL);
    const float hoopy = (float)(u32)REG16(0x0011A628UL);
    const float ox = hoopx / 2.0f
                   + (float)(long)(short)REG16(0x0011A266UL
                       + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4))
                     * 5.0f;
    const float oy = hoopy / 2.0f
                   + (float)(long)(short)REG16(0x0011A268UL
                       + (u32)(long)(short)(u16)((u16)REG8(0x0011A660UL) << 4))
                     * 5.0f;
    float theta = 0.0f, r;
    float cx[4], cy[4];
    short i;

    if (f2u(w) != 0) theta = float_atan(h / w);

    r = float_sqrt(h * h + w * w) / 2.0f;

    {
        const float nr = ((u16)(f2u(r) >> 16) != 0)
                         ? u2f(f2u(r) ^ 0x80000000UL) : r;
        const float p  = theta - angle;
        const float q  = (((u16)(f2u(theta) >> 16) != 0)
                          ? u2f(f2u(theta) ^ 0x80000000UL) : theta) - angle;

        if (flip == 0) {
            cx[0] = float_cos(p) * r  + ox;  cy[0] = float_sin(p) * r  + oy;
            cx[1] = float_cos(q) * r  + ox;  cy[1] = float_sin(q) * r  + oy;
            cx[2] = nr * float_cos(p) + ox;  cy[2] = nr * float_sin(p) + oy;
            cx[3] = nr * float_cos(q) + ox;  cy[3] = nr * float_sin(q) + oy;
        } else {
            cx[0] = nr * float_cos(p) + ox;  cy[0] = float_sin(p) * r  + oy;
            cx[1] = nr * float_cos(q) + ox;  cy[1] = float_sin(q) * r  + oy;
            cx[2] = float_cos(p) * r  + ox;  cy[2] = nr * float_sin(p) + oy;
            cx[3] = float_cos(q) * r  + ox;  cy[3] = nr * float_sin(q) + oy;
        }
    }

    for (i = 0; i < 4; i++) {
        if (cx[i] > hoopx) return 0x00;
        if (cy[i] > hoopy) return 0x00;
        if ((long)f2u(cx[i]) < 0) return 0x00;
        if ((long)f2u(cy[i]) < 0) return 0x00;
    }
    return 0x01;
}

/* H'2431EE. Twelve states that start the module sewing and stop it again,
 * walked by H'11F595 through a reversed key table at H'24329C: the keys
 * H'01 to H'05 and H'0A to H'10 are read forwards while the index counts
 * down, so the handlers sit back to front behind them.
 *
 * Which state it starts in is decided before the dispatch: H'0A when the
 * module is already talking or H'114D4E is clear, H'01 otherwise, and
 * H'114DBB at five puts it back to one. Every state that is waiting for the
 * link blinks the panel box while it waits.
 */
void module_run_step(u8 *step)
{
    u8 n;

    if (REG8(0x0011F595UL) == 0) {
        if (REG8(0x00114DBAUL) != 0) REG8(0x0011F595UL) = 0x0A;
        else                         REG8(0x0011F595UL) = 0x01;

        if (REG8(0x00114D4EUL) == 0) REG8(0x0011F595UL) = 0x0A;

        if (REG8(0x00114D51UL) & 0x10) {
            if (REG8(0x00114DBBUL) == 0) REG8(0x00114DBBUL) = 0x01;
        }
        if (!(REG8(0x00114D51UL) & 0x10)) REG8(0x00114DBBUL) = 0x00;

        if (REG8(0x00114DBBUL) == 0x05) REG8(0x0011F595UL) = 0x01;
    }

    n = REG8(0x0011F595UL);

    switch (n) {
    case 0x01:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A617UL) = 0x01;
            REG8(0x00114D7DUL) = 0x01;
            module_run_control(0x01);
            link_send_start();
            REG8(0x0011F594UL) = 0x00;
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x02:
        if (!module_link_quiet()) { module_panel_blink(0x01); return; }

        if (REG8(0x00114D52UL) & 0x08) {
            REG8(0x00114D52UL) &= (u8)~0x08;
            if (REG8(0x0011F531UL) == 0) REG8(0x0011F534UL) = 0x01;
            REG8(0x0011F531UL) = 0x00;
        }
        module_run_control(0x00);
        if (REG8(0x0011F594UL) == 0) {
            module_fixed_box(0x01);
            REG8(0x0011F594UL) = 0x01;
        }
        if (REG8(0x0011F530UL) > 0x32) {
            while (REG8(0x0011F530UL) <= 0x64) {
                label_percent_left(REG8(0x0011F530UL));
                REG8(0x0011F530UL) = (u8)(REG8(0x0011F530UL) + 1);
            }
        }
        REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        return;

    case 0x03:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x04;
            REG8(0x0011F2A2UL) = 0x02;
            link_send_start();
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x04:
        if (!module_link_quiet()) return;
        if (REG8(0x00114DB9UL) != 0) return;

        if (module_hoop_fits() != 0) {
            if (REG8(0x00114DBBUL) == 0x01) {
                if (module_can_talk() != 0) {
                    REG8(0x0011F2A1UL) = 0x03;
                    REG8(0x0011A613UL) = 0x02;
                    link_send_start();
                    REG8(0x00114DBAUL) = 0x01;
                    REG8(0x00114DBBUL) = 0x05;
                    REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
                }
                return;
            }

            if (REG8(0x00114DBAUL) != 0) {
                if (module_can_talk() != 0) {
                    REG8(0x0011F2A1UL) = 0x03;
                    REG8(0x0011A614UL) = 0x03;
                    REG8(0x00114DBAUL) = 0x00;
                    link_send_start();
                    REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
                }
                return;
            }

            if (module_running() != 0) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A614UL) = 0x03;
                REG8(0x00114DBAUL) = 0x00;
                link_send_start();
                REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
            } else {
                link_claim(0x03);
            }
            return;
        }

        if (module_can_talk() != 0) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A613UL) = 0x02;
            link_send_start();
            REG8(0x00114DBAUL) = 0x01;
            REG8(0x00114D55UL) |= 0x01;
            if (REG8(0x00114DBBUL) == 0x01) REG8(0x00114DBBUL) = 0x05;
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        }
        return;

    case 0x0A:
        if (REG8(0x00114DB9UL) == 0) {
            REG8(0x00114D7DUL) = 0x01;
            if (module_can_talk() != 0) {
                if (REG8(0x00114DA0UL) == 0) module_talk_end();
                REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
            }
        }
        return;

    case 0x0B:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A612UL) = 0x01;
            link_send_start();
            REG8(0x0011F594UL) = 0x00;
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x0C:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A617UL) = 0x01;
            link_send_start();
            module_run_control(0x01);
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x0D:
        if (!module_link_quiet()) { module_panel_blink(0x01); return; }

        module_run_control(0x00);
        REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        if (REG8(0x0011F594UL) == 0) {
            module_fixed_box(0x01);
            REG8(0x0011F594UL) = 0x01;
        }
        if (REG8(0x0011F530UL) > 0x32) {
            while (REG8(0x0011F530UL) <= 0x64) {
                label_percent_left(REG8(0x0011F530UL));
                REG8(0x0011F530UL) = (u8)(REG8(0x0011F530UL) + 1);
            }
        }
        return;

    case 0x0E:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x04;
            REG8(0x0011F2A2UL) = 0x02;
            link_send_start();
            REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x0F:
        if (REG8(0x00114DB9UL) == 0) {
            if (module_can_talk() != 0)
                REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        }
        return;

    case 0x10:
        if (!module_link_quiet()) { module_panel_blink(0x01); return; }

        if (module_hoop_fits() != 0) {
            if (REG8(0x00114DBBUL) == 0x01) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A613UL) = 0x02;
                link_send_start();
                REG8(0x00114DBAUL) = 0x01;
                REG8(0x00114DBBUL) = 0x05;
            } else {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A614UL) = 0x03;
                link_send_start();
                REG8(0x00114DBAUL) = 0x00;
            }
        } else {
            REG8(0x0011F2A1UL) = 0x03;
            REG8(0x0011A613UL) = 0x02;
            link_send_start();
            REG8(0x00114DBAUL) = 0x01;
            REG8(0x00114D55UL) |= 0x01;
        }
        REG8(0x0011F595UL) = (u8)(REG8(0x0011F595UL) + 1);
        return;

    case 0x05:
    default:
        if (module_link_quiet()) {
            *step = (u8)(*step + 1);
            REG8(0x00114D7DUL) = 0x00;
            REG8(0x0011F595UL) = 0x00;
            if (REG8(0x0011F52CUL) != 0) REG8(0x00114D66UL) = 0x00;
        } else {
            module_panel_blink(0x01);
        }
        return;
    }
}

u8 touch_hit(u16 first, u16 last, u16 *out_value, u16 *out_index);
u8 module_home_request(void);
void module_restart(void);
void screen_store1_clear(void);
void embroidery_panel_save(void);
u8 module_identify(void);
void stitch_reset_current(void);
void module_reset_wait(void);
void module_unpark(void);
void link_delay(u16 units);
void module_screen_step(void);
void pattern_mark_ready(void);
void module_link_lost(void);
void module_wait_pass(void);
void module_state_machine(void);
void module_fetch_step(u8 *step);
void module_run_step(u8 *step);
void module_hoop_check(u8 *step);
void module_ask_time(u8 *step);
void module_buffers_clear(void);
void module_cursor_erase(void);
void module_fixed_box(u8 mode);
void box5_draw(u8 lit);
void label_colours(void);
void label_percent(void);
void label_colours_picture(void);
void label_minutes(short value);
void module_size_labels(short across, short down);
void module_minutes_left(u8 mode);
u8 module_flash_step(u8 what);
u8 module_edge_service(void);
u8 module_lid_check(void);
void module_arrow_fwd_2(u8 lit);
void module_arrow_back_1(u8 lit);
void module_arrow_fwd_3(u8 lit);
void module_arrow_back_2(u8 lit);
void module_colour_bitmap(u8 index);
void module_panel_blink(u8 on);
void pedal_service(void);
void main_motor_service(void);

static void module_give_up(void);
static void module_wait_reset(void);
static void module_lost_home(void);
static void module_state_0D(u8 *wait);
static void module_state_0E(u8 *wait);

/* State H'0D of H'235B0E, written out because it is a third of the routine.
 * The module is running: the lid, the stop button and the hardware state are
 * all watched, and when the hardware settles at five the speed is worked out
 * in floating point and written to H'FFFECB.
 *
 * The speed is sixty over four thousandths of the reading, held down to
 * H'114DC0, less one. Two ways to the reading, by whether H'57FF80 says this
 * is a H'B4 machine: the other kind adds three to the tenths and takes five
 * off at the end. */
static void module_state_0D(u8 *wait)
{
    *wait = 0;

    if (!(REG8(0x00FFFEC1UL) & 0x08)) {
        REG8(0x00114D93UL) = 0x01;
        module_home_request();
    }

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) {
            module_flash_step(0x03);
            if (REG8(0x00FFFEC7UL) & 0x01) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D62UL) = 0x0A;
                link_claim(0x1A);
            }
        }
    }

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) { REG8(0x00FFFECBUL) = 0x1E; return; }
    }

    REG8(0x00114D9AUL) = 0x01;
    REG8(0x00114D98UL) = 0x01;

    if (module_edge_service() == 0) {
        REG8(0x00114D93UL) = 0x01;
        module_home_request();
        module_flash_step(0x01);
    }

    if (REG8(0x00FFFEC0UL) == 0x01) { module_lost_home(); return; }

    if ((REG8(0x00FFFEC4UL) & 0x10) && (REG8(0x00FFFEC7UL) & 0x01)) {
        if (REG8(0x00FFFEC3UL) == 0) REG8(0x00FFFEC7UL) |= 0x40;
        if (REG8(0x00FFFEC1UL) & 0x02) {
            REG8(0x00FFFEC7UL) |= 0x40;
            REG8(0x00FFFEC3UL) = 0x00;
        }
    }

    if (REG8(0x00FFFEC0UL) != 0x05) return;

    if (REG8(0x0011F54BUL) == 0x01) {
        REG8(0x0011F54BUL) = 0x00;
        REG8(0x00114D62UL) = 0x0A;
        REG8(0x00FFFEC7UL) |= 0x40;
        REG8(0x00FFFEC3UL) = 0x00;
        REG8(0x00FFFEC4UL) |= 0x20;
        REG8(0x00114D50UL) &= (u8)~0x02;
        REG8(0x00114D50UL) &= (u8)~0x01;
        return;
    }

    while (REG8(0x00FFFEC0UL) != 0x01) {
        if (*wait == 0xFF) { *wait = 0x00; break; }
        link_delay(0x0001);
        *wait = (u8)(*wait + 1);
    }

    if (REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
        module_lost_home();
        return;
    }

    REG16(0x0011A662UL) = 0x1FFF;
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A616UL) = 0x01;
    link_send_start();
    link_delay(0x0008);

    if (REG16(0x0011A662UL) == 0x1FFF) { module_lost_home(); return; }

    if (REG8(0x00114D5CUL) == 0x02) {
        REG8(0x00114D93UL) = 0x00;
        module_home_request();
        REG8(0x00FFFEC4UL) |= 0x20;
        link_claim(0x10);
        return;
    }

    REG8(0x00FFFEC1UL) &= (u8)~0x20;

    if (REG8(0x00114D5CUL) == 0x04) {
        REG8(0x00FFFECBUL) = 0x1E;
        REG8(0x0011F54CUL) = REG8(0x00FFFED1UL);
        REG8(0x00FFFED1UL) = 0x28;
        REG8(0x00114D62UL) = 0x0F;
        return;
    }

    if (REG8(0x00114D5CUL) == 0x05) REG8(0x00FFFEC1UL) |= 0x20;

    if (REG8(0x00114D5CUL) == 0x01) {
        if (REG8(0x00114D96UL) == 0) {
            REG8(0x00114D7CUL) = 0x01;
            link_delay(0x0032);
            REG8(0x00FFFEC7UL) |= 0x40;
            REG8(0x00FFFEC3UL) = 0x00;
            REG8(0x0011F54EUL) = 0x01;
            module_flash_step(0x01);
        }
        if (REG8(0x00114D7CUL) == 0) {
            REG8(0x00114D4FUL) &= (u8)~0x01;
            REG8(0x00114D89UL) = (u8)(REG8(0x00114D89UL) + 1);
            REG8(0x00FFFEC1UL) |= 0x20;
            label_colours_picture();
        }
        REG8(0x00114D62UL) = 0x0E;
        return;
    }

    {
        float v;

        if (REG8(0x0057FF80UL) < 0xB4)
            v = (float)(long)(short)(u16)((u16)REG8(0x00114DBEUL) + 3) / 10.0f
                  * (float)(u32)REG16(0x0011A662UL)
                + (float)(u32)REG8(0x00114DBFUL) + -5.0f;
        else
            v = (float)(u32)REG8(0x00114DBEUL) / 10.0f
                  * (float)(u32)REG16(0x0011A662UL)
                + (float)(u32)REG8(0x00114DBFUL);

        v = 60.0f / (v / 1000.0f * 4.0f);
        if (!((float)(u32)REG8(0x00114DC0UL) >= v))
            v = (float)(u32)REG8(0x00114DC0UL);

        REG8(0x00FFFECBUL) = (u8)((u8)(int)v - 1);
    }

    if (REG16(0x0011A662UL) == 0x01FF) REG8(0x00FFFECBUL) = 0x1E;
    if (REG8(0x0011F54BUL) != 0) REG8(0x00FFFECBUL) = 0x1E;
    REG8(0x00114D62UL) = 0x0E;
    REG8(0x0011F54BUL) = module_flash_step(0x00);
}

/* State H'0E. Waits for the hardware to reach the state that means one
 * stitch is done -- seven on this machine, three on a H'B4 one -- turning
 * the delay over up to H'FF times, then sends message H'03/H'02 and goes
 * back to H'0D. H'11F54E says this was the last one. */
static void module_state_0E(u8 *wait)
{
    const u8 want = (REG8(0x0057FF80UL) < 0xB4) ? 0x07 : 0x03;

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) return;
    }

    for (;;) {
        if (REG8(0x00FFFEC0UL) == want) break;
        if (*wait == 0xFF) break;
        link_delay(0x0001);
        *wait = (u8)(*wait + 1);
        if (REG8(0x00FFFEC0UL) == want) break;
        if (REG8(0x00FFFEC0UL) == 0x01) break;
        if (REG8(0x00FFFEC0UL) == 0x00) break;
    }

    if (REG8(0x00FFFEC0UL) != want) return;

    if (REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
        module_lost_home();
        return;
    }

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A616UL) = 0x02;
    link_send_start();
    REG8(0x00114D62UL) = 0x0D;

    if (REG8(0x0011F54EUL) != 0) {
        module_minutes_left(0x02);
        REG8(0x0011F54EUL) = 0x00;
        REG8(0x00114D62UL) = 0x0A;
    } else {
        module_minutes_left(0x01);
    }
}

/* H'244DE0's own reset: everything cleared and message H'03/H'04 sent. */
static void module_wait_reset(void)
{
    REG8(0x00114D83UL) = 0x00;
    REG8(0x00114D66UL) = 0x00;
    REG8(0x00114D62UL) = 0x00;
    REG8(0x00114D95UL) = 0x00;
    REG8(0x00114D50UL) &= (u8)~0x01;
    REG8(0x00114D50UL) &= (u8)~0x02;
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61AUL) = 0x04;
    link_send_start();
}

/* The block H'235B0E writes out to give up on the module:
 * everything cleared, the hardware released, message H'03/H'04 sent, and the
 * screen asked for again if a homing was pending. */
static void module_give_up(void)
{
    REG8(0x00114D65UL) = 0x00;
    REG8(0x00114D66UL) = 0x00;
    REG8(0x00114D62UL) = 0x00;
    REG8(0x00114D95UL) = 0x00;
    REG8(0x00FFFEC4UL) |= 0x20;
    REG8(0x00114D50UL) &= (u8)~0x01;
    REG8(0x00114D50UL) &= (u8)~0x02;
    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61AUL) = 0x04;
    link_send_start();

    if (REG8(0x00114D93UL) != 0) {
        REG8(0x00114D66UL) = 0x01;
        REG8(0x00114D62UL) = 0x08;
    }
    module_screen_step();
}

/* The failure every state of H'235B0E shares: bit H'0200 of H'114D4C up,
 * and the module asked to go home. */
static void module_lost_home(void)
{
    REG16(0x00114D4CUL) |= 0x0200;
    REG8(0x00114D94UL) = 0x01;
    REG8(0x00114D93UL) = 0x01;
    module_home_request();
}

/* H'235B0E. The module's own state machine: eighteen states in H'114D62,
 * with a four-step settling sub-machine in H'114D65 in front of them that
 * only runs once the main states are past seven.
 *
 * H'244DE0 turns this over in a loop and this calls H'2315A4, which calls
 * H'23182A, which calls H'244DE0 -- the four of them are one knot and are
 * written together.
 *
 * Two registers the ROM never initialises are reproduced as locals starting
 * at nought: R1, the H'C8 spin count in state H'0A, and R3L, the delay
 * counter states H'0D and H'0E share. Neither survives a call, so neither
 * ever counts in the original either.
 */
void module_state_machine(void)
{
    u16 spin = 0;
    u8 wait = 0;

    if (REG8(0x00114D65UL) != 0 && REG8(0x00114D62UL) > 0x07) {
        const u8 k = REG8(0x00114D65UL);

        if (k == 0x01) {
            if (REG8(0x00114D62UL) < 0x0F) {
                if (REG8(0x00FFFEC0UL) == 0x06) {
                    REG8(0x00FFFECBUL) = 0x1E;
                    REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                    return;
                }
                REG8(0x00FFFECBUL) = 0x1E;
                {
                    const u8 s = REG8(0x00FFFEC6UL);

                    if (s == 0x00 || s == 0x05)
                        REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                }
            }
            if (REG8(0x00114D94UL) != 0) return;
            /* otherwise on into the states */
        } else if (k == 0x02) {
            if (REG8(0x00FFFEC0UL) == 0x07) {
                REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                return;
            }
            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05)
                    REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
            }
            return;
        } else if (k == 0x03) {
            if (REG8(0x00FFFEC0UL) == 0x05) {
                REG8(0x00FFFEC7UL) |= 0x40;
                REG8(0x00FFFEC3UL) = 0x00;
                REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
                return;
            }
            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05)
                    REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);
            }
            return;
        } else {
            link_delay(0x0014);
            REG8(0x00114D94UL) = 0x00;
            REG8(0x00114D65UL) = (u8)(REG8(0x00114D65UL) + 1);

            if (REG8(0x00114D65UL) == 0xC8) module_give_up();
            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05) module_give_up();
            }
            return;
        }
    }

    switch (REG8(0x00114D62UL)) {
    case 0x00:
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x04;
            REG8(0x0011F2A2UL) = 0x02;
            link_send_start();
            REG8(0x0011F54DUL) = 0x7D;
            REG8(0x00114D99UL) = 0x00;
            module_flash_step(0x01);
            REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        } else {
            module_panel_blink(0x01);
        }
        return;

    case 0x01:
        if (REG8(0x0011F54DUL) != 0) {
            REG8(0x0011F54DUL) = (u8)(REG8(0x0011F54DUL) - 1);
            module_panel_blink(0x01);
        } else {
            REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        }
        return;

    case 0x02:
        module_fetch_step((u8 *)0x00114D62UL);
        return;

    case 0x03:
        module_hoop_check((u8 *)0x00114D62UL);
        return;

    case 0x04:
        if (!module_link_quiet()) return;
        module_fixed_box(0x01);
        REG8(0x00114D91UL) = 0x01;
        label_colours();
        box5_draw(REG8(0x00114D96UL));
        label_percent();
        module_arrow_back_2(0x00);
        module_arrow_fwd_3(0x01);
        {
            const u32 pat = (u32)(long)(short)(u16)
                            ((u16)REG8(PAT_B(0x00)) << 1);
            const u16 w = (u16)(REG16(0x00104CCEUL + pat) / 10);
            const u16 h = (u16)(REG16(0x00104D06UL + pat) / 10);

            REG16(0x0011F4E2UL) = h;
            REG16(0x0011F4E0UL) = w;
            module_size_labels((short)w, (short)h);
        }
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x05:
        {
            /* the sum is kept as a full word here and as a byte below */
            const u16 n = (u16)((u16)((u16)REG8(PAT_B(0x05)) * 0x1B)
                                + (u16)REG8(PAT_B(0x00)));

            if (n != (u16)REG8(0x00114D92UL)) {
                REG8(0x00114D7BUL) = 0x01;
                REG8(0x0011F4E6UL) = 0x00;
                REG8(0x00114D7CUL) = 0x00;
            } else {
                REG8(0x00114D7BUL) = 0x14;
            }
        }
        REG8(0x00114D92UL) = (u8)(REG8(PAT_B(0x00))
                                  + (u8)(u16)((u16)REG8(PAT_B(0x05)) * 0x1B));
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x06:
        if (REG8(0x00114D7BUL) != 0) return;
        module_run_step((u8 *)0x00114D62UL);
        return;

    case 0x07:
        if (module_link_quiet() && REG8(0x00114DB9UL) == 0)
            REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        else
            module_panel_blink(0x01);
        return;

    case 0x08:
        if ((REG8(0x00114D50UL) & 0x21) != 0 ||
            REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
            module_fixed_box(0x01);
            return;
        }
        REG8(0x00FFFECBUL) = 0x1E;
        {
            const u32 pat = (u32)(long)(short)(u16)
                            ((u16)REG8(PAT_B(0x00)) << 1);
            const u16 w = (u16)(REG16(0x00104CCEUL + pat) / 10);
            const u16 h = (u16)(REG16(0x00104D06UL + pat) / 10);

            REG16(0x0011F4E2UL) = h;
            REG16(0x0011F4E0UL) = w;
            module_size_labels((short)w, (short)h);
            REG16(0x0011F4E4UL) = REG16(0x00104C96UL + pat);
            if (REG16(0x0011F4E4UL) != 0)
                label_minutes((short)REG16(0x0011F4E4UL));
        }
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61AUL) = 0x01;
        link_send_start();
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x09:
        if ((REG8(0x00114D50UL) & 0x21) != 0 ||
            REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) {
            module_fixed_box(0x01);
            return;
        }
        link_claim(REG8(0x0011F535UL));
        module_panel_blink(0x00);
        REG8(0x00114D62UL) = (u8)(REG8(0x00114D62UL) + 1);
        return;

    case 0x0A:
        if (REG8(0x00114D7CUL) != 0) {
            const u8 k = REG8(0x00114D7CUL);

            if (k == 0x01) {
                spin = 0;
                REG8(0x00114D7CUL) = (u8)(k + 1);
            } else if (k == 0x02) {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s != 0x00 && s != 0x05) {
                    spin = (u16)(spin + 1);
                    if (spin == 0x00C8) REG8(0x00FFFEC4UL) |= 0x20;
                    return;
                }
                REG8(0x00114D7CUL) = (u8)(REG8(0x00114D7CUL) + 1);
            } else if (k == 0x03) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D4FUL) |= 0x01;
                REG8(0x00114D7CUL) = (u8)(REG8(0x00114D7CUL) + 1);
            } else if (k == 0x04) {
                REG8(0x00114D7CUL) = 0x00;
                REG8(0x00114D50UL) &= (u8)~0x01;
                REG8(0x00114D50UL) &= (u8)~0x02;
            }
            return;
        }

        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            REG8(0x00114D7BUL) == 0 && REG8(0x00114D4EUL) != 0 &&
            REG8(0x00114DBAUL) == 0 && REG8(0x00114DB9UL) == 0) {
            if ((REG8(0x00FFFEC1UL) & 0x08) && REG8(0x0011F30EUL) == 0) {
                REG8(0x00114D62UL) = 0x0B;
                REG8(0x0011F54EUL) = 0x00;
                return;
            }
            module_lid_check();
            return;
        }
        REG8(0x00114D98UL) = 0x00;
        return;

    case 0x0B:
        module_ask_time((u8 *)0x00114D62UL);
        if (REG8(0x00114D62UL) == 0x0C) {
            REG8(0x00114D98UL) = 0x01;
            REG8(0x00FFFEC4UL) &= (u8)~0x20;
        }
        return;

    case 0x0C:
        {
            const u8 s = REG8(0x00FFFEC6UL);

            if (s != 0x00 && s != 0x05) {
                REG8(0x00114D62UL) = 0x0D;
                REG8(0x00114D95UL) = 0x01;
                REG8(0x00114D99UL) = 0x00;
            }
        }
        if (REG8(0x0011F30EUL) != 0) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D62UL) = 0x0A;
        }
        if ((REG8(0x00FFFEC4UL) & 0x10) && (REG8(0x00FFFEC7UL) & 0x01)) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D62UL) = 0x0A;
            link_claim(0x1A);
        }
        if ((REG8(0x00FFFEC4UL) & 0x10) && !(REG8(0x00FFFEC1UL) & 0x08)) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D62UL) = 0x0A;
        }
        return;

    case 0x0D:
        module_state_0D(&wait);
        return;

    case 0x0E:
        module_state_0E(&wait);
        return;

    case 0x0F:
        if (REG8(0x00FFFEC0UL) == 0x03) {
            if (REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
                REG8(0x0011F2A1UL) = 0x03;
                REG8(0x0011A616UL) = 0x02;
                link_send_start();
                REG8(0x00114D62UL) = 0x10;
            } else {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00FFFED1UL) = REG8(0x0011F54CUL);
                REG16(0x00114D4CUL) |= 0x0200;
                REG8(0x00114D62UL) = 0x00;
                REG8(0x00114D66UL) = 0x00;
            }
        }
        return;

    case 0x10:
        if (REG8(0x00FFFEC0UL) == 0x04) {
            REG8(0x00FFFECBUL) = 0x00;
            pedal_service();
            main_motor_service();
            REG8(0x00114D62UL) = 0x11;
        }
        return;

    case 0x11:
        link_delay(REG16(0x0011A662UL));
        REG8(0x00FFFED1UL) = REG8(0x0011F54CUL);
        REG8(0x00FFFECBUL) = 0x1E;
        pedal_service();
        main_motor_service();
        REG8(0x00114D62UL) = 0x0D;
        return;

    default:
        return;
    }
}

/* H'2315A4. Which screen the module wants next, by H'114D8A. State four is
 * the only one that does not clear the request afterwards. */
void module_screen_step(void)
{
    switch (REG8(0x00114D8AUL)) {
    case 0x01:
        screen_switch(0x15, 0x01, 0x00);
        REG8(0x00114D72UL) = 0x01;
        REG8(0x00114D8AUL) = 0x00;
        break;

    case 0x02:
        if (REG8(0x00114DA1UL) == 0x01) screen_switch(0x14, 0x01, 0x00);
        else                            screen_switch(0x13, 0x01, 0x00);
        REG8(0x00114D7EUL) = 0x01;
        if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        REG8(0x00114D8AUL) = 0x00;
        break;

    case 0x03:
        screen_switch(0x16, 0x01, 0x00);
        REG8(0x00114D72UL) = 0x02;
        REG8(0x00114D8AUL) = 0x00;
        break;

    case 0x04:
        REG8(0x00114D72UL) = 0x3E;
        screen_switch(0x37, 0x01, 0x00);
        pattern_mark_ready();
        break;

    case 0x05:
        REG8(0x00114D72UL) = 0x2D;
        screen_switch(0x24, 0x01, 0x00);
        REG8(0x00114D8AUL) = 0x00;
        module_arrow_fwd_2(0x01);
        module_arrow_back_1(0x01);
        module_colour_bitmap(REG8(0x00114D89UL));
        break;

    case 0x06:
        module_link_lost();
        break;

    default:
        REG8(0x00114D8AUL) = 0x00;
        break;
    }
}

/* H'23182A. The module has gone: everything cleared out, the screen put back
 * to the one that matches what bit 0 of H'114D51 says was attached, and the
 * whole thing marked as unplugged. If the hardware is still moving it waits
 * a pass instead and asks again next time. */
void module_link_lost(void)
{
    const u8 state = REG8(0x00FFFEC6UL);

    if (state == 0x00 || state == 0x05) {
        module_buffers_clear();

        if (REG8(0x00114D51UL) & 0x01) {
            REG8(0x00114D7EUL) = 0x01;
            REG8(0x00114D8BUL) = 0x00;
            REG8(0x00114D9BUL) = 0x01;
            link_claim(0x05);
            screen_switch(0x14, 0x01, 0x00);
            REG8(0x00114DA1UL) = 0x01;
            REG8(PAT_B(0x03)) = 0x01;
            REG8(0x00114D8EUL) = 0x03;
        } else {
            REG8(0x00114D7EUL) = 0x01;
            link_claim(0x05);
            REG8(0x00114D8BUL) = 0x00;
            REG8(0x00114D9BUL) = 0x01;
            screen_switch(0x13, 0x01, 0x00);
            REG8(0x00114DA1UL) = 0x00;
            REG8(PAT_B(0x03)) = 0x00;
            REG8(0x00114D8EUL) = 0x02;
        }
        REG8(0x00114D8AUL) = 0x00;
        return;
    }

    if (REG8(0x00114D8AUL) == 0) module_wait_pass();
    REG8(0x00114D8AUL) = 0x06;
}

/* H'244DE0. A pass held open while the module finishes moving: the state
 * machine, the pedal and the main motor turned over until H'114D83 comes
 * back to nought. H'114D83 counting to H'C8 gives up and puts everything
 * back, and so does the hardware coming to rest. */
void module_wait_pass(void)
{
    module_cursor_erase();

    {
        const u8 s = REG8(0x00FFFEC6UL);

        if (s == 0x00 || s == 0x05) return;
    }

    do {
        const u8 n = REG8(0x00114D83UL);

        if (n == 0x00) {
            REG8(0x00114D83UL) = (u8)(n + 1);
        } else if (n == 0x01) {
            if (REG8(0x00114D62UL) != 0x0E) {
                REG8(0x00FFFEC4UL) |= 0x20;
                link_delay(0x000A);
                REG8(0x00114D83UL) = (u8)(REG8(0x00114D83UL) + 1);
            }
        } else {
            link_delay(0x0014);
            REG8(0x00114D83UL) = (u8)(REG8(0x00114D83UL) + 1);

            /* this routine's own giving-up block, which is not the one
             * H'235B0E uses: it clears H'114D83 rather than H'114D65, does
             * not touch H'FFFEC4, and does not ask for a screen */
            if (REG8(0x00114D83UL) == 0xC8) module_wait_reset();

            {
                const u8 s = REG8(0x00FFFEC6UL);

                if (s == 0x00 || s == 0x05) module_wait_reset();
            }
        }

        if (REG8(0x00114D66UL) != 0) module_state_machine();
        pedal_service();
        main_motor_service();
    } while (REG8(0x00114D83UL) != 0);

    REG8(0x00114D98UL) = 0x00;
    REG8(0x00114D99UL) = 0x01;
}

/* H'231B32. The module reset held open: H'11F9C walks two steps, the first
 * turning a whole pass over and the second clearing everything out and going
 * back to screen H'12. Nothing happens unless H'114D9F asks for it. */
void module_reset_wait(void)
{
    if (REG8(0x00114D9FUL) == 0) return;

    do {
        const u8 n = REG8(0x00114D9CUL);

        if (n == 0x00) {
            REG8(0x00114D4FUL) &= (u8)~0x08;
            REG8(0x00114D80UL) = 0x00;
            module_wait_pass();
            REG8(0x00114D9CUL) = (u8)(REG8(0x00114D9CUL) + 1);
        } else if (n == 0x01) {
            if (REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
                module_buffers_clear();
                screen_switch(0x12, 0x01, 0x00);
                REG8(0x00114D9CUL) = 0x00;
            }
        }
    } while (REG8(0x00114D9CUL) != 0);
}

/* H'237E3C. A key press while the embroidery module is attached.
 *
 * Twelve keys in a reversed jump table at H'237E7A: the codes H'21F68C
 * hands over. Most of them share the same two guards -- H'114D78 at H'EE
 * means the module is mid-something, and bit 0 of H'FFFEC4 means it is
 * really there -- and then wait a pass for the hardware to settle.
 *
 * Key H'6D is the one that does the work: it is the button that leaves the
 * embroidery screens, and where it goes depends on H'114D8E. Eight or nine
 * puts the panel away and goes to screen H'37; four or five stops the run
 * first; one to three asks the module to name itself and only goes on if it
 * answers.
 */
void module_key(u8 key)
{
    switch (key) {
    case 0x74:
        if (REG8(0x00114D78UL) <= 0x01) return;

        module_wait_pass();
        if (REG8(0x00114D78UL) == 0xEE) {
            module_restart();
            screen_store1_clear();
        }
        if ((REG8(0x00114D4FUL) & 0x08) || REG8(0x00114D9CUL) != 0)
            module_reset_wait();
        else
            module_buffers_clear();

        if (module_link_quiet() && (REG8(0x00114D51UL) & 0x40)) {
            REG8(0x0011F2A1UL) = 0x07;
            REG8(0x00114D51UL) &= (u8)~0x40;
            link_send_start();
            while (!module_link_quiet()) rom_host_service();
        }
        REG8(0x00114D8EUL) = 0x01;
        return;

    case 0x70: case 0x71: case 0x72: case 0x78: case 0x7D:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        REG8(0x00114D78UL) = 0xFF;
        module_unpark();
        REG8(0x00114D8EUL) = 0x00;
        module_wait_pass();
        return;

    case 0x73: case 0x79:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        {
            const u8 s = REG8(0x00FFFEC6UL);

            if (s == 0x00 || s == 0x05) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D66UL) = 0x00;
            }
        }
        module_wait_pass();
        REG8(0x00114D88UL) = 0x01;
        REG8(0x0011F305UL) = 0x01;
        return;

    case 0x75:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        {
            const u8 s = REG8(0x00FFFEC6UL);

            if (s == 0x00 || s == 0x05) {
                REG8(0x00FFFEC4UL) |= 0x20;
                REG8(0x00114D66UL) = 0x00;
            }
        }
        module_wait_pass();
        return;

    case 0x77:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        REG8(0x00114D84UL) = 0x01;
        return;

    case 0x6D:
        if (REG8(0x00114D78UL) == 0xEE) return;
        if (!(REG8(0x00FFFEC4UL) & 0x01)) return;
        if (module_home_request() != 0) return;
        if (REG8(0x00114D72UL) != 0) return;
        if (!module_link_quiet()) return;

        REG8(0x001040BAUL) = REG8(0x00114D8EUL);

        if (REG8(0x00114D8EUL) >= 0x08 && REG8(0x00114D8EUL) <= 0x09) {
            embroidery_panel_save();
            REG8(0x00114D8EUL) = 0x07;
            REG8(0x00114D72UL) = 0x06;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
            return;
        }

        if (REG8(0x00114D8EUL) >= 0x04 && REG8(0x00114D8EUL) <= 0x05) {
            if (REG8(0x00114DACUL) != 0) return;

            REG8(0x00114D93UL) = 0x00;
            module_home_request();
            REG8(0x00114D98UL) = 0x00;
            REG8(0x0011F30EUL) = 0x00;
            REG8(0x00114D8EUL) = 0x00;

            if (REG8(0x00FFFEC6UL) != 0) { REG8(0x00114D8AUL) = 0x04; return; }
            if (!module_link_quiet()) return;

            REG8(0x00114D72UL) = 0x3E;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
            return;
        }

        if (REG8(0x00114D8EUL) >= 0x01 && REG8(0x00114D8EUL) <= 0x03) {
            if (module_identify() == 0) { link_claim(0x0B); return; }

            REG8(0x0011A63CUL) = 0x00;
            REG8(0x0011F4E6UL) = 0x00;
            REG8(0x00114D96UL) = 0x00;
            REG8(0x0011F534UL) = 0x00;
            REG8(0x00114D98UL) = 0x00;
            stitch_reset_current();
            REG16(0x0011F292UL) = 0x0000;
            REG8(0x00114D89UL) = 0x00;
            REG16(0x0011F4DCUL) = 0x0000;
            REG16(0x0011F4DEUL) = 0x0000;
            REG8(0x00114D8EUL) = 0x00;
            REG8(0x00114D72UL) = 0x3F;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
        }
        return;

    case 0x81:
    default:
        return;
    }
}

/* ---- the screen bodies' helpers ---------------------------------------
 * H'22382A's thirty-odd screen bodies are inline blocks behind a table of
 * seventy-nine entries at H'2238B0, and they lean on a set of small drawing
 * helpers. These are the ones with the most callers.
 */

/* H'21752E. The box the sewing screen keeps its picture in: cleared in
 * frame B and then drawn from H'34C6CD into frame A. */
void sew_picture_box(void)
{
    draw_rect(0x006B, 0x0051, 0x00CD, 0x0097, LCD_FRAME_B, 0x00, 0x01);
    bitmap_draw(0x006B, 0x0051, 0x00CD, 0x0097,
                (const u8 *)0x0034C6CDUL, LCD_FRAME_A);
}

/* H'213ABC. The little needle-stop picture, one of two by bit 5 of
 * H'FFFEF6. The bit is rotated down to the bottom of the byte three times
 * rather than shifted, which comes to the same thing here. */
void needle_stop_picture(void)
{
    u8 k = (u8)(REG8(0x00FFFEF6UL) & 0x20);
    u8 i;

    for (i = 0; i < 3; i++) k = (u8)((u8)(k << 1) | (u8)(k >> 7));
    k = (u8)(k & 0x07);

    bitmap_draw(0x002C, 0x000B, 0x0030, 0x0019,
                (const u8 *)REG32(0x00115892UL
                                  + (u32)(long)(short)(u16)((u16)k << 2)),
                LCD_FRAME_A);
}

/* H'21B34C. The number in H'FFFECD drawn top right, and only when it moves.
 * H'11B35E remembers what was drawn; [fresh] forgets it. */
void speed_number_draw(u8 fresh)
{
    char buf[6];

    if (fresh != 0) REG16(0x0011B35EUL) = 0xFFFF;

    if ((u16)REG8(0x00FFFECDUL) != REG16(0x0011B35EUL)) {
        int_to_decimal((short)(u16)REG8(0x00FFFECDUL), buf);
        text_draw(buf, 0x00F3, 0x0087, 0x0100, 0x008D, 0x0002, 0x02,
                  (const u8 *)0x00119DE6UL);
        REG16(0x0011B35EUL) = (u16)REG8(0x00FFFECDUL);
    }
}

/* H'213882. The eleven boxes of the stitch-width strip, from a table of
 * four words each at H'11524E, with the one H'FFFEEA names drawn from
 * H'34BD2D over the top.
 *
 * [fresh] redraws the lot; otherwise only a move repaints, and then only
 * the two boxes that changed. H'11B2D0 remembers which was lit. */
void width_strip_draw(u8 fresh)
{
    const u32 tbl = 0x0011524EUL;
    short i;

    if (fresh != 0) {
        REG16(0x0011B2D0UL) = 0xFFFF;

        for (i = 0; i <= 0x000A; i++) {
            const u32 e = tbl + (u32)(long)(short)(u16)((u16)i << 3);

            draw_rect(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                      LCD_FRAME_B, 0x00, 0x01);
        }
        {
            const u32 e = tbl + (u32)(long)(short)(u16)
                          ((u16)((u16)REG8(0x00FFFEEAUL)) << 3);

            bitmap_draw(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                        (const u8 *)0x0034BD2DUL, LCD_FRAME_B);
        }
        REG16(0x0011B2D0UL) = (u16)REG8(0x00FFFEEAUL);
    }

    if ((u16)REG8(0x00FFFEEAUL) != REG16(0x0011B2D0UL)) {
        {
            const u32 e = tbl + (u32)(long)(short)(u16)
                          ((u16)REG16(0x0011B2D0UL) << 3);

            draw_rect(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                      LCD_FRAME_B, 0x00, 0x01);
        }
        {
            const u32 e = tbl + (u32)(long)(short)(u16)
                          ((u16)((u16)REG8(0x00FFFEEAUL)) << 3);

            bitmap_draw(REG16(e), REG16(e + 2), REG16(e + 4), REG16(e + 6),
                        (const u8 *)0x0034BD2DUL, LCD_FRAME_B);
        }
        REG16(0x0011B2D0UL) = (u16)REG8(0x00FFFEEAUL);
    }
}

/* H'21348C. The two arrows beside a list, lit or dim by where the window
 * sits in it. H'11B0AA remembers the back arrow's state and H'11B0AB the
 * forward one, so each is only repainted when it changes; [fresh] sets both
 * flags and draws nothing, which leaves the next call to do the work.
 *
 * The end of the window is entry one's value plus [span], and the length of
 * the list is the first word the list points at. */
void list_arrows(u16 index, u16 span, u16 back_box, u16 fwd_box, u8 fresh)
{
    const u32 table = REG32(0x0011B0BAUL);
    const u32 entry = table + (u32)(long)(short)(u16)(0x12 * index);
    const short top = (short)REG16(REG32(entry + 0x0C));

    if (fresh != 0) {
        REG8(0x0011B0ABUL) = 0x01;
        REG8(0x0011B0AAUL) = 0x01;
        return;
    }

    {
        const u16 v = REG16(REG32(0x0011B0BAUL)
                            + (u32)(long)(short)(u16)(0x12 * index) + 0x08);

        if (v == 0x0001) {
            if (REG8(0x0011B0AAUL) != 0) {
                REG8(0x0011B0AAUL) = 0x00;
                hitbox_blit(back_box, LCD_FRAME_A, 0x0034E4A8UL);
            }
        } else if ((short)v > (short)0x0001) {
            if (REG8(0x0011B0AAUL) == 0) {
                REG8(0x0011B0AAUL) = 0x01;
                hitbox_blit(back_box, LCD_FRAME_A, 0x0034E46CUL);
            }
        }
    }

    {
        const short end = (short)(REG16(REG32(0x0011B0BAUL) + 0x1A) + span);

        if (end > top) {
            if (REG8(0x0011B0ABUL) != 0) {
                REG8(0x0011B0ABUL) = 0x00;
                hitbox_blit(fwd_box, LCD_FRAME_A, 0x0034E520UL);
            }
        } else {
            if (REG8(0x0011B0ABUL) == 0) {
                REG8(0x0011B0ABUL) = 0x01;
                hitbox_blit(fwd_box, LCD_FRAME_A, 0x0034E4E4UL);
            }
        }
    }
}

/* H'2224E8. The pattern strip put back from the second store and its number
 * redrawn, and H'11A176 asked for a repaint. */
void picker_strip_restore(void)
{
    region_copy(0x0030, 0x00A0, 0x00E7, 0x00C0, 0x00A0,
                0x000F1610UL, LCD_FRAME_A);
    dialog_number_draw(REG16(0x0011A1CEUL));
    REG8(0x0011A176UL) = 0x01;
}

/* H'21BDD6. The demonstration screen: five pictures shown one after another,
 * a second each. H'11B362 counts round them, and which set of five depends
 * on whether H'57FF80 says this is a H'B4 machine -- with picture nought
 * coming out of the configuration block rather than the table.
 *
 * [fresh] just puts the counter back to nought. A press of H'77 leaves for
 * screen H'17 with the foot switch position cleared. */
void demo_screen_step(u8 fresh)
{
    u16 code = 0;

    if (fresh != 0) { REG16(0x0011B362UL) = 0x0000; return; }

    if (screen_leave_check(&code, 0x00) == 0x03) {
        if (code == 0x0077) {
            REG8(0x00FFFEC5UL) = 0x00;
            screen_switch(0x17, 0x01, 0x00);
        }
        return;
    }

    if (REG8(0x0057FF80UL) == 0xB4) {
        if (REG16(0x0011B362UL) == 0)
            image_load(REG32(REG32(0x0011B2AEUL) + 0x10), LCD_SCRATCH);
        else
            image_load(REG32(0x001158BAUL
                             + (u32)(long)(short)(u16)
                               (REG16(0x0011B362UL) << 2)), LCD_SCRATCH);
    } else {
        if (REG16(0x0011B362UL) == 0)
            image_load(REG32(REG32(0x0011B2AEUL) + 0x14), LCD_SCRATCH);
        else
            image_load(REG32(0x001158A6UL
                             + (u32)(long)(short)(u16)
                               (REG16(0x0011B362UL) << 2)), LCD_SCRATCH);
    }

    region_copy(0x0000, 0x0000, 0x013F, 0x00EF, 0x0000,
                LCD_SCRATCH, LCD_FRAME_A);
    hold_start(0x03E8);
    REG16(0x0011B362UL) =
        (u16)((short)(u16)(REG16(0x0011B362UL) + 1) % (short)5);
}

/* H'21EF02 and H'21C672. Two settings screens of the same shape: one flash
 * byte, two boxes that are the yes and the no of it, H'19 to save and H'1A
 * to leave without saving. The first owns H'57EFC6 and the second H'57EFC7,
 * and the second also drives bit 6 of H'FFFEC1 from what it saved.
 *
 * The working copy lives in RAM -- H'11B3C0 and H'11B364 -- and is what the
 * single-byte flash write is given the address of. */
u8 setting_toggle_C6(u8 fresh)
{
    u16 value = 0, index = 0;

    if (fresh != 0) {
        screen_stack_push();
        REG8(0x0011B3C0UL) = REG8(0x0057EFC6UL);
        if (REG8(0x0011B3C0UL) != 0) hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        else                         hitbox_set_state(0x0002, 0x0002, 0x01, 0);
    }

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return 0x00;

    if (value == 0x0042) {
        if (REG8(0x0011B3C0UL) == 0) {
            hitbox_set_state(0x0002, 0x0002, 0x00, 0);
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
            REG8(0x0011B3C0UL) = 0x01;
        }
        return 0x00;
    }

    if (value == 0x0043) {
        if (REG8(0x0011B3C0UL) != 0) {
            hitbox_set_state(0x0001, 0x0001, 0x00, 0);
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
            REG8(0x0011B3C0UL) = 0x00;
        }
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        REG8(0x00114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x0011B3C0UL, 0x0057EFC6UL, 1);
        REG8(0x00114DC7UL) &= (u8)~0x20;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

u8 setting_toggle_C7(u8 fresh)
{
    u16 value = 0, index = 0;

    if (fresh != 0) {
        screen_stack_push();
        hitbox_set_state(0x0001, 0x0002, 0x00, 0);
        if (REG8(0x0057EFC7UL) != 0) hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        else                         hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        REG8(0x0011B364UL) = REG8(0x0057EFC7UL);
    }

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return 0x00;

    if (value == 0x0042) {
        if (REG8(0x0011B364UL) == 0) {
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
            hitbox_set_state(0x0002, 0x0002, 0x00, 0);
            REG8(0x0011B364UL) = 0x01;
        }
        return 0x00;
    }

    if (value == 0x0043) {
        if (REG8(0x0011B364UL) != 0) {
            hitbox_set_state(0x0001, 0x0001, 0x00, 0);
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
            REG8(0x0011B364UL) = 0x00;
        }
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        REG8(0x00114DC7UL) |= 0x20;
        rom_flash_write((const void *)0x0011B364UL, 0x0057EFC7UL, 1);
        REG8(0x00114DC7UL) &= (u8)~0x20;
        if (REG8(0x0057EFC7UL) != 0) REG8(0x00FFFEC1UL) |= 0x40;
        else                         REG8(0x00FFFEC1UL) &= (u8)~0x40;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'222FD2. Where the page holding position H'11B212 starts. The first
 * fifteen are all one page; after that the pages are five long, and the
 * answer is the page's own start counted back from the walk. */
u16 list_page_start(void)
{
    const short at = (short)(REG16(0x0011B212UL) + 1);
    short e;

    if (at <= 0x000F) return 0x0001;

    for (e = 0x0010; ; e += 0x0005) {
        if (e == at) return (u16)(e - 10);
        if (e > at)  return (u16)(e - 15);
    }
}

/* H'21B9CE. A screen with one way out of it: key H'77 for screen H'17, with
 * the foot switch position cleared on the way. */
u8 screen_only_77(void)
{
    u16 code = 0;

    if (screen_leave_check(&code, 0x00) == 0x03) {
        if (code == 0x0077) {
            REG8(0x00FFFEC5UL) = 0x00;
            screen_switch(0x17, 0x01, 0x00);
        }
    }
    return 0x00;
}

/* H'21B6C6. The byte in H'FFFED8 drawn as a number, right-aligned in a box
 * halfway down the screen. */
void needle_number_draw(void)
{
    /* the ROM gives this two bytes of stack, which is one short of what
     * three digits need; the field never holds three */
    char buf[4];

    int_to_decimal((short)(u16)REG8(0x00FFFED8UL), buf);
    text_draw(buf, 0x0057, 0x00B5, 0x0064, 0x00BE, 0x0001, 0x00,
              (const u8 *)0x001196EAUL);
}

/* H'21C466 and H'229F7E. Two more of the yes-and-no screens, but these do
 * not save anything: box one and box two set a RAM byte and go straight back
 * to the slot-four screen. The second sets H'11B0A9 as well, and sets it on
 * both answers rather than only on the yes. */
u8 choice_screen_17A(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    message_show_held(index);

    if (value == 0x0001) {
        screen_stack_pop();
        REG8(0x0011A17AUL) = 0x01;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    } else if (value == 0x0002) {
        screen_stack_pop();
        REG8(0x0011A17AUL) = 0x00;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    }
    return 0x00;
}

u8 choice_screen_1E3(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    message_show_held(index);

    if (value == 0x0001) {
        screen_stack_pop();
        REG8(0x0011A1E3UL) = 0x01;
        REG8(0x0011B0A9UL) = 0x01;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    } else if (value == 0x0002) {
        screen_stack_pop();
        REG8(0x0011A1E3UL) = 0x00;
        REG8(0x0011B0A9UL) = 0x01;
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
    }
    return 0x00;
}

/* H'23033A, H'2180C8 and H'21C3B8. Three menu screens of the same shape: a
 * press is read, the message it carries is held, and what the box stands for
 * says where to go. None of them does anything else.
 */

/* H'23033A. The two ways into the embroidery panel, one with the module's
 * own set-up run and one without. */
void menu_embroidery(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return;

    message_show_held(index);

    if (value == 0x0001) {
        REG8(0x00114D8EUL) = 0x07;
        REG8(0x0011F304UL) = 0x01;
        REG8(0x0011F532UL) = 0x07;
        REG8(0x0011F533UL) = 0x07;
        screen_switch(0x37, 0x01, 0x00);
        screen_stack_pop();
    } else if (value == 0x0002) {
        REG8(0x00114D8EUL) = 0x07;
        REG8(0x0011F304UL) = 0x01;
        REG8(0x0011F532UL) = 0x00;
        REG8(0x0011F533UL) = 0x00;
        screen_switch(0x37, 0x01, 0x00);
        screen_stack_pop();
    }
}

/* H'2180C8. Three categories of pattern: whichever is pressed, the screen
 * moves to H'03 and H'11B108 is set to the first item of that category in
 * the list H'11B096 points at. */
void menu_category(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0003, &value, &index) != 0x03) return;

    message_show_held(index);
    screen_switch(0x03, 0x01, 0x00);

    if (value == 0x0001)
        REG16(0x0011B108UL) = first_item_of_category(0x12, REG32(0x0011B096UL));
    else if (value == 0x0002)
        REG16(0x0011B108UL) = first_item_of_category(0x13, REG32(0x0011B096UL));
    else if (value == 0x0003)
        REG16(0x0011B108UL) = first_item_of_category(0x14, REG32(0x0011B096UL));
}

/* H'21C3B8. Four ways out: three screens by number and H'1A to come back off
 * the stack to H'27. */
void menu_four_ways(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return;

    message_show_held(index);

    if (value == 0x0001)      screen_switch(0x4B, 0x01, 0x00);
    else if (value == 0x0002) screen_switch(0x00, 0x01, 0x00);
    else if (value == 0x0003) screen_switch(0x31, 0x01, 0x00);
    else if (value == 0x001A) {
        screen_stack_pop();
        screen_switch(0x27, 0x01, 0x00);
    }
}

/* H'218292, H'218188 and H'217DE0. Three more menu screens. The first two
 * take the operator to a list of one category; the third is the same again
 * with six categories and answers H'01 rather than H'00.
 *
 * H'218292 is the odd one: its second and third boxes are refused with
 * message H'0A while H'11A178 says the machine is mid-something, and its
 * first box goes to a different screen from the other two. */
u8 menu_three_lists(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0003, &value, &index) != 0x03) return 0x00;

    message_show_held(index);

    if (value == 0x0001) {
        screen_switch(0x02, 0x01, 0x00);
        REG16(0x0011B108UL) = first_item_of_category(0x0F, 0x0011A88EUL);
        return 0x00;
    }
    if (value == 0x0002) {
        if (REG8(0x0011A178UL) != 0) { message_show(0x000A); return 0x00; }
        screen_switch(0x33, 0x01, 0x00);
        REG16(0x0011B108UL) = 0x0001;
        return 0x00;
    }
    if (value == 0x0003) {
        if (REG8(0x0011A178UL) != 0) { message_show(0x000A); return 0x00; }
        screen_switch(0x35, 0x01, 0x00);
        REG16(0x0011B108UL) = 0x0001;
    }
    return 0x00;
}

u8 menu_five_categories(void)
{
    u16 value = 0, index = 0;
    u8 wanted;

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;

    message_show_held(index);
    screen_switch(0x03, 0x01, 0x00);

    if      (value == 0x0001) wanted = 0x12;
    else if (value == 0x0002) wanted = 0x13;
    else if (value == 0x0003) wanted = 0x14;
    else if (value == 0x0004) wanted = 0x15;
    else if (value == 0x0005) wanted = 0x16;
    else                      return 0x00;

    REG16(0x0011B108UL) = first_item_of_category(wanted, REG32(0x0011B096UL));
    return 0x00;
}

u8 menu_six_categories(void)
{
    u16 value = 0, index = 0;
    u8 wanted;

    if (touch_hit(0x0001, 0x0006, &value, &index) != 0x03) return 0x00;

    message_show_held(index);
    screen_switch(0x02, 0x01, 0x00);

    if      (value == 0x0001) wanted = 0x05;
    else if (value == 0x0002) wanted = 0x07;
    else if (value == 0x0003) wanted = 0x08;
    else if (value == 0x0004) wanted = 0x0A;
    else if (value == 0x0005) wanted = 0x0B;
    else if (value == 0x0006) wanted = 0x0D;
    else                      return 0x00;

    REG16(0x0011B108UL) = first_item_of_category(wanted, 0x0011A88EUL);
    return 0x01;
}

/* H'219CC8. Twelve boxes, of which two are ways out and the rest a choice.
 *
 * The chosen box is remembered in H'11A1AA so that the next press can put
 * the old one back to state 0 before lighting the new one, and the choice
 * itself is written to H'11B0FF as the value plus H'9C. Both exits put the
 * message up before they look at the sewing flag, so a press while the
 * machine is running is announced and then ignored.
 *
 * Box H'19 does nothing at all when nothing is chosen yet: the whole body,
 * message included, sits inside the test on H'11A1AA. */
u8 menu_twelve_choice(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x000C, &value, &index) != 0x03) return 0x00;

    if (value == 0x0019) {
        if (REG16(0x0011A1AAUL) != 0xFFFF) {
            message_show_held(index);
            if (REG8(0x00114DC6UL) & 0x80) return 0x00;
            screen_switch(0x11, 0x02, 0x00);
            REG16(0x0011A1AAUL) = 0xFFFF;
        }
        return 0x00;
    }

    if (value == 0x001A) {
        message_show_held(index);
        if (REG8(0x00114DC6UL) & 0x80) return 0x00;
        screen_stack_pop();
        screen_switch(0x0F, 0x01, 0x00);
        REG16(0x0011A1AAUL) = 0xFFFF;
        return 0x00;
    }

    if (index != REG16(0x0011A1AAUL)) {
        const u16 held = REG16(0x0011A1AAUL);

        if (held != 0xFFFF) hitbox_set_state(held, held, 0x00, 0);
        hitbox_set_state(index, index, 0x01, 0);
        REG16(0x0011A1AAUL) = index;
        REG8(0x0011B0FFUL) = (u8)((u8)value + 0x9C);
    }
    return 0x00;
}

/* H'21BBE6. The display test: three full-screen fills, one press apart.
 *
 * H'11B361 counts them. Arriving with the count at 0 paints the screen in
 * colour 3 and puts the count at 1, and each press of the single box paints
 * the next colour. After the third the count goes back to 0, the panel code
 * at H'FFFEC5 is cleared and the screen leaves for H'17.
 *
 * The fills go straight into the visible buffer, not the back one -- there
 * is nothing here for the flip to bring over. */
u8 display_test(void)
{
    u16 value = 0, index = 0;

    if (REG8(0x0011B361UL) == 0x00) {
        draw_rect(0x0000, 0x0000, 0x013F, 0x00EF, LCD_FRAME_A, 0x03, 0x01);
        REG8(0x0011B361UL) = 0x01;
    }

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;
    if (value != 0x0001) return 0x00;

    if (REG8(0x0011B361UL) == 0x01) {
        draw_rect(0x0000, 0x0000, 0x013F, 0x00EF, LCD_FRAME_A, 0x00, 0x01);
        REG8(0x0011B361UL) = 0x02;
        hold_start(0x0064);
        return 0x00;
    }
    if (REG8(0x0011B361UL) == 0x02) {
        draw_rect(0x0000, 0x0000, 0x013F, 0x00EF, LCD_FRAME_A, 0x02, 0x01);
        REG8(0x0011B361UL) = 0x03;
        hold_start(0x0064);
        return 0x00;
    }
    if (REG8(0x0011B361UL) == 0x03) {
        REG8(0x0011B361UL) = 0x00;
        hold_start(0x0064);
        REG8(0xFFFEC5UL) = 0x00;
        screen_switch(0x17, 0x01, 0x00);
    }
    return 0x00;
}

/* H'2227E6. The input trim, adjusted a step at a time.
 *
 * Boxes 1 and 2 are down and up; each lights itself if it is not lit
 * already, moves the trim one step within H'00..H'FF and holds the screen
 * for H'32 ticks, so that leaning on the box repeats. Box 3 accepts, which
 * means one byte written back to the settings block in flash with the
 * flash-busy bit up around it; box 4 cancels by reading the byte back out
 * of flash. Both leave for screen H'27.
 *
 * The H'02 return from the hit test -- a release -- is what puts the two
 * arrow boxes back to normal, and it is the only thing that does. */
u8 trim_screen(void)
{
    u16 value = 0, index = 0;
    const u8 hit = touch_hit(0x0001, 0x0004, &value, &index);

    if (hit != 0x03) {
        if (hit == 0x02) {
            if (hitbox_kind(0x0001) == 0x01) {
                hitbox_set_state(0x0001, 0x0001, 0x00, 0);
            }
            if (hitbox_kind(0x0002) == 0x01) {
                hitbox_set_state(0x0002, 0x0002, 0x00, 0);
            }
        }
        return 0x00;
    }

    if (value == 0x004E) {
        const u8 trim = INPUT_TRIM;

        if (hitbox_kind(0x0001) != 0x01) {
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        }
        if (trim != 0x00) INPUT_TRIM = (u8)(trim - 1);
        hold_start(0x0032);
        return 0x00;
    }

    if (value == 0x004F) {
        const u8 trim = INPUT_TRIM;

        if (hitbox_kind(0x0002) != 0x01) {
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        }
        if (trim < 0xFF) INPUT_TRIM = (u8)(trim + 1);
        hold_start(0x0032);
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0xFFFEDFUL, 0x57FF92UL, 1);
        FLASH_BUSY &= (u8)~0x20;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        message_show_held(index);
        INPUT_TRIM = SETTING_TRIM;
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'21B7DA. The pedal test: the reading and two lamps, redrawn only when
 * something has changed.
 *
 * H'FFFECE is the scaled pedal reading and H'11A1AC the copy of it that is
 * already on the screen; bits 2 and 3 of H'FFFEC4 are the two switches and
 * H'11A1AE and H'11A1B0 their copies, kept as the masked byte widened to a
 * word rather than as a flag. A switch made is a black rectangle and a
 * switch broken a white one, both drawn into the back buffer.
 *
 * All three copies go to H'FFFF on the way out, so that the screen draws
 * itself in full the next time it is entered.
 *
 * The digits go into a six-byte local -- exactly enough for five and the
 * terminator, which is what a word taken as a signed number needs. */
u8 pedal_test_screen(void)
{
    char text[6];
    u16 to = 0;
    const u16 reading = REG16(0x00FFFECEUL);

    if (REG16(0x0011A1ACUL) != reading) {
        int_to_decimal((short)reading, text);
        text_draw(text, 0x0082, 0x00CB, 0x00CD, 0x00D5, 0x0001, 0x02,
                  (const u8 *)0x001196EAUL);
        REG16(0x0011A1ACUL) = REG16(0x00FFFECEUL);
    }

    if ((u16)(REG8(0x00FFFEC4UL) & 0x04) != REG16(0x0011A1AEUL)) {
        draw_rect(0x0083, 0x00A7, 0x0094, 0x00B0, LCD_FRAME_B,
                  (u8)((REG8(0x00FFFEC4UL) & 0x04) ? 0x00 : 0x03), 0x01);
        REG16(0x0011A1AEUL) = (u16)(REG8(0x00FFFEC4UL) & 0x04);
    }

    if ((u16)(REG8(0x00FFFEC4UL) & 0x08) != REG16(0x0011A1B0UL)) {
        draw_rect(0x00C6, 0x00A7, 0x00CE, 0x00B0, LCD_FRAME_B,
                  (u8)((REG8(0x00FFFEC4UL) & 0x08) ? 0x00 : 0x03), 0x01);
        REG16(0x0011A1B0UL) = (u16)(REG8(0x00FFFEC4UL) & 0x08);
    }

    if (screen_leave_check(&to, 0x00) != 0x03) return 0x00;
    if (to != 0x0077) return 0x00;

    REG16(0x0011A1ACUL) = 0xFFFF;
    REG16(0x0011A1AEUL) = 0xFFFF;
    REG16(0x0011A1B0UL) = 0xFFFF;
    REG8(0xFFFEC5UL) = 0x00;
    screen_switch(0x17, 0x01, 0x00);
    return 0x00;
}

/* H'21BA0E. Which machine this is: box 1 says H'AA and box 2 says H'B4, the
 * two values the configuration byte at H'57FF80 takes.
 *
 * Pressing a box only moves the light and the pending value in H'11B360;
 * nothing is written until the screen is left for H'77, and then only if
 * the pending value differs from what is in flash. H'11A1B2 is the "come
 * back and re-light" flag, set on the way out and acted on the next time
 * the screen is entered -- which is what puts the light back on the box
 * flash actually holds.
 *
 * The message number handed to H'211A9E on the way out is the second local,
 * and on that path nothing has written it: it is the box index the hit test
 * below leaves there, from whichever pass last went that way. That is in
 * the original and is reproduced, so the local is deliberately left
 * uninitialised here too. */
u8 variant_screen(void)
{
    u16 value = 0;
    u16 index;      /* read before it is written -- see above */

    if (REG8(0x0011A1B2UL) != 0) {
        hitbox_set_state(0x0001, 0x0001, 0x00, 0);
        hitbox_set_state(0x0002, 0x0002, 0x00, 0);
        REG8(0x0011B360UL) = CONFIG_BLOCK;
        if (REG8(0x0011B360UL) == 0xAA) {
            hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        } else {
            hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        }
        REG8(0x0011A1B2UL) = 0x00;
        return 0x00;
    }

    if (screen_leave_check(&value, 0x00) == 0x03 && value == 0x0077) {
        if (REG8(0x0011B360UL) != CONFIG_BLOCK) {
            sew_picture_box();
            settings_save(0x00);
            FLASH_BUSY |= 0x20;
            rom_flash_write((const void *)0x0011B360UL, 0x0057FF80UL, 1);
            FLASH_BUSY &= (u8)~0x20;
        }
        REG8(0x0011A1B2UL) = 0x01;
        message_show_held(index);
        REG8(0xFFFEC5UL) = 0x00;
        screen_switch(0x17, 0x01, 0x00);
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    if (value == 0x0001) {
        hitbox_set_state(0x0001, 0x0001, 0x01, 0);
        hitbox_set_state(0x0002, 0x0002, 0x00, 0);
        REG8(0x0011B360UL) = 0xAA;
        return 0x00;
    }
    if (value == 0x0002) {
        hitbox_set_state(0x0002, 0x0002, 0x01, 0);
        hitbox_set_state(0x0001, 0x0001, 0x00, 0);
        REG8(0x0011B360UL) = 0xB4;
    }
    return 0x00;
}

/* H'22253E. Calibrating the touch panel: two crosses, and the straight line
 * through them.
 *
 * Entering the screen with H'11A1BC set puts the calibration back to unity,
 * so that the two readings taken here are raw panel counts rather than
 * counts already run through the old line. Box H'50 takes the first cross
 * and box H'51 the second, each once -- H'11B3CA and H'11B3CB say which
 * have been taken -- and each beeps.
 *
 * With both in hand the line is worked out and written to flash. The
 * crosses sit at H'28.8 and H'28.8 + H'F0 across, and H'28.8 and
 * H'28.8 + H'A0 down, which is where the H'F0 and H'A0 come from; H'28.8
 * is 40.5, the half pixel putting the cross on a pixel centre.
 *
 * Afterwards the screen goes to H'4A, or to H'17 if bit 7 of H'FFFEC4 is
 * up -- the flag that says the panel was reached from the service menu. */
u8 touch_cal_screen(void)
{
    u16 value = 0, index = 0;

    if (REG8(0x0011A1BCUL) != 0) {
        TOUCH_CAL_X_SCALE  = f2u(1.0f);
        TOUCH_CAL_Y_SCALE  = f2u(1.0f);
        TOUCH_CAL_X_OFFSET = 0;
        TOUCH_CAL_Y_OFFSET = 0;
        REG8(0x0011A1BCUL) = 0x00;
    }

    if (REG8(0x0011B3CAUL) != 0 && REG8(0x0011B3CBUL) != 0) {
        float scale;

        scale = 240.0f / (float)(long)(short)(u16)
                    (REG16(0x0011B3C6UL) - REG16(0x0011B3C2UL));
        TOUCH_CAL_X_SCALE = f2u(scale);

        scale = 160.0f / (float)(long)(short)(u16)
                    (REG16(0x0011B3C8UL) - REG16(0x0011B3C4UL));
        TOUCH_CAL_Y_SCALE = f2u(scale);

        TOUCH_CAL_X_OFFSET = f2u(40.5f -
            (float)(long)(short)REG16(0x0011B3C2UL) * u2f(TOUCH_CAL_X_SCALE));
        TOUCH_CAL_Y_OFFSET = f2u(40.5f -
            (float)(long)(short)REG16(0x0011B3C4UL) * u2f(TOUCH_CAL_Y_SCALE));

        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0x0011A87EUL, 0x0057FFA0UL, 4);
        rom_flash_write((const void *)0x0011A882UL, 0x0057FFA4UL, 4);
        rom_flash_write((const void *)0x0011A886UL, 0x0057FFA8UL, 4);
        rom_flash_write((const void *)0x0011A88AUL, 0x0057FFACUL, 4);
        FLASH_BUSY &= (u8)~0x20;

        REG8(0x0011B3CAUL) = 0x00;
        REG8(0x0011B3CBUL) = 0x00;
        REG8(0x0011A1BCUL) = 0x01;
        screen_stack_pop();

        if (!(REG8(0x00FFFEC4UL) & 0x80)) {
            screen_switch(0x4A, 0x01, 0x00);
        } else {
            REG8(0xFFFEC5UL) = 0x00;
            screen_switch(0x17, 0x01, 0x00);
        }
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;

    if (value == 0x0050) {
        if (REG8(0x0011B3CAUL) == 0) {
            REG16(0x0011B3C2UL) = REG16(0x0011B102UL);
            REG16(0x0011B3C4UL) = REG16(0x0011B104UL);
            beep(0x001E, 0x0064, 0x01);
            REG8(0x0011B3CAUL) = 0x01;
        }
        return 0x00;
    }
    if (value == 0x0051) {
        if (REG8(0x0011B3CBUL) == 0) {
            REG16(0x0011B3C6UL) = REG16(0x0011B102UL);
            REG16(0x0011B3C8UL) = REG16(0x0011B104UL);
            beep(0x001E, 0x0064, 0x01);
            REG8(0x0011B3CBUL) = 0x01;
        }
    }
    return 0x00;
}

/* H'21CB2C. The top sewing speed, set in steps of ten.
 *
 * H'11B37A holds the value being edited and H'57FF94 the one in flash; the
 * ceiling comes from H'57FF8A, and the floor is H'6E. Box H'15 takes ten
 * off, H'16 puts ten on -- but only as far as ten below the ceiling, tested
 * unsigned -- and H'7F goes straight to the ceiling. H'19 writes the value
 * to flash and H'1A throws it away; both leave for screen H'27.
 *
 * The argument says this is the first pass over the screen, which is when
 * the value is taken out of flash and the number first drawn. The number is
 * redrawn by each of the three boxes that change it, from the same six-byte
 * local the ROM writes it into. */
#define SPEED_CEILING   REG16(0x57FF8AUL)

static void max_speed_draw(u16 v)
{
    char text[6];

    int_to_decimal((short)v, text);
    text_draw(text, 0x00A6, 0x0056, 0x00E7, 0x0060, 0x0001, 0x02,
              (const u8 *)0x001196EAUL);
}

u8 max_speed_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(0x0011B37AUL) = SETTING_LIMIT;
        max_speed_draw(REG16(0x0011B37AUL));
    }

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0015) {
        const u16 now = REG16(0x0011B37AUL);

        if ((short)now >= 0x006E) {
            REG16(0x0011B37AUL) = (u16)(now - 10);
            max_speed_draw((u16)(now - 10));
        }
        return 0x00;
    }

    if (value == 0x0016) {
        const u16 now = REG16(0x0011B37AUL);

        if ((u16)(SPEED_CEILING - 10) >= now) {
            REG16(0x0011B37AUL) = (u16)(now + 10);
            max_speed_draw((u16)(now + 10));
        }
        return 0x00;
    }

    if (value == 0x007F) {
        REG16(0x0011B37AUL) = SPEED_CEILING;
        max_speed_draw(REG16(0x0011B37AUL));
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0x0011B37AUL, 0x0057FF94UL, 2);
        FLASH_BUSY &= (u8)~0x20;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'2303EE. The two boxes on the module's own menu, and the eleven ways the
 * second one can go.
 *
 * H'11F533 is what the module last said it was doing, and it decides where
 * box 2 leads: H'07 is a pattern in hand, and then H'114D8F says whether it
 * came back H'08, H'04 or something else; H'08 and H'09 are the two hoop
 * screens; H'02 to H'04 are a pattern being sewn, which is the long branch;
 * and everything else is a machine that has lost its place, so the buffers
 * are cleared and the screen goes back to H'12.
 *
 * The long branch takes the waiting slot, puts two bytes into its second
 * record, picks the screen by H'114D8E and then sends the module two
 * messages -- H'07 only if bit 6 of H'114D51 is up, then H'01 -- waiting
 * for the link to go quiet after each. Both waits are the whole of
 * H'244D64's quiet test spelled out twice over, the same as everywhere
 * else in this cluster.
 */
void link_send_start(void);
void pattern_mark_ready(void);

void module_menu_screen(void)
{
    u16 value = 0, index = 0;
    u8  kind;

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return;
    message_show_held(index);

    if (value == 0x0001) {
        if (REG8(0x0011F533UL) == 0x07) {
            REG8(0x00114D8EUL) = 0x07;
            screen_switch(0x37, 0x01, 0x00);
            REG8(0x00114D90UL) = 0x01;
            REG8(0x00114D8FUL) = 0x00;
            REG8(0x0011A177UL) = 0x01;
        } else {
            REG8(0x00114D8EUL) = 0x07;
            REG8(0x00114D72UL) = 0x3E;
            screen_switch(0x37, 0x01, 0x00);
            pattern_mark_ready();
        }
        REG8(0x00114D8FUL) = 0x00;
        REG8(0x0011F534UL) = 0x01;
        screen_stack_pop();
        return;
    }

    if (value != 0x0002) return;

    kind = REG8(0x0011F533UL);
    REG8(0x00114D8EUL) = kind;

    if (kind == 0x07) {
        const u8 back = REG8(0x00114D8FUL);

        if (back == 0x08) {
            REG8(0x00114D90UL) = 0x08;
            screen_switch(0x37, 0x01, 0x00);
        } else if (back == 0x04) {
            REG8(0x00114D90UL) = 0x04;
            screen_switch(0x37, 0x01, 0x00);
        } else {
            REG8(0x00114D8EUL) = 0x07;
            screen_switch(0x37, 0x01, 0x00);
            REG8(0x0011A177UL) = 0x01;
        }
        REG8(0x00114D8FUL) = 0x00;
    } else if (kind == 0x08) {
        screen_switch(0x15, 0x01, 0x00);
        REG8(0x0011A177UL) = 0x01;
        if (REG8(0x00114D8FUL) == 0x0F) REG8(0x00114D84UL) = 0x01;
    } else if (kind == 0x09) {
        screen_switch(0x16, 0x01, 0x00);
        REG8(0x0011A177UL) = 0x01;
        if (REG8(0x00114D8FUL) == 0x0F) REG8(0x00114D84UL) = 0x01;
    } else if (kind >= 0x02 && kind <= 0x04) {
        pattern_slot_begin();
        REG8(PAT_B(0x03)) = REG8(0x00114DA1UL);
        REG8(PAT_B(0x05)) = (u8)(REG8(0x00114D8CUL) / 0x1B);

        if (REG8(0x00114D8EUL) == 0x03) screen_switch(0x14, 0x01, 0x00);
        else                            screen_switch(0x13, 0x01, 0x00);

        REG8(0x00114D7EUL) = 0x01;
        if (REG8(0x00114D9BUL) == 0) REG8(0x00114DADUL) = 0x01;
        REG8(0x00114D98UL) = 0x00;

        if (module_link_quiet() && (REG8(0x00114D51UL) & 0x40)) {
            REG8(0x0011F2A1UL) = 0x07;
            REG8(0x00114D51UL) &= (u8)~0x40;
            link_send_start();
            while (!module_link_quiet()) loop_tick();
        }
        if (module_link_quiet()) {
            REG8(0x0011F2A1UL) = 0x01;
            link_send_start();
            while (!module_link_quiet()) loop_tick();
        }
    } else {
        module_buffers_clear();
        REG8(0x00114D8EUL) = 0x01;
        REG8(0x00114D9BUL) = 0x01;
        screen_switch(0x12, 0x01, 0x00);
    }

    REG8(0x00114D8FUL) = 0x00;
    REG8(0x00114D51UL) &= (u8)~0x40;
    screen_stack_pop();
}

/* H'2144CA. The strip along the top of the sewing screen, redrawn only
 * where something has moved.
 *
 * Six words remember what is on the screen already: H'11B2F2 the presser
 * foot in H'FFFEEB, H'11B2F0 bit 2 of H'FFFEE2, H'11B2F8 and H'11B2FA bits
 * 4 and 3 of H'FFFEFA, H'11B2F4 bit 7 of H'FFFEFA and H'11B2F6 bit 3 of
 * H'FFFEF7. The argument puts all six to H'FFFF, which forces the whole
 * strip to be drawn again.
 *
 * The foot number is drawn as digits below H'80 and as a record above it:
 * H'11541E is a table of eight-byte entries, a string and a picture, and
 * the picture is only drawn if it is there. Drawing a foot above H'80 also
 * puts two of the other remembered words back to H'FFFF, so the marks
 * beside it are drawn again over the ground the record just covered.
 *
 * The last two share one patch of screen: each clears it and draws its own
 * mark, and each clears it on the way out only if the other is not there.
 */
#define STATUS_FOOT     ((u16)REG8(0x00FFFEEBUL))
#define STATUS_E2_2     ((u16)(REG8(0x00FFFEE2UL) & 0x04))
#define STATUS_FA_4     ((u16)(REG8(0x00FFFEFAUL) & 0x10))
#define STATUS_FA_3     ((u16)(REG8(0x00FFFEFAUL) & 0x08))
#define STATUS_FA_7     ((u16)(REG8(0x00FFFEFAUL) & 0x80))
#define STATUS_F7_3     ((u16)(REG8(0x00FFFEF7UL) & 0x08))

static u32 status_foot_record(u32 base)
{
    return REG32(base + (u32)(long)(short)(u16)
                     ((u16)(STATUS_FOOT + 0xFF80) << 3));
}

static void status_mark_clear(void)
{
    draw_rect(0x0032, 0x001E, 0x0046, 0x0025, LCD_FRAME_A, 0x00, 0x01);
}

void status_bar_refresh(u8 redraw_all)
{
    char text[6];

    if (redraw_all != 0) {
        REG16(0x0011B2F2UL) = 0xFFFF;
        REG16(0x0011B2F0UL) = 0xFFFF;
        REG16(0x0011B2F4UL) = 0xFFFF;
        REG16(0x0011B2F6UL) = 0xFFFF;
        REG16(0x0011B2F8UL) = 0xFFFF;
        REG16(0x0011B2FAUL) = 0xFFFF;
    }

    if (STATUS_FOOT != REG16(0x0011B2F2UL) ||
        STATUS_E2_2 != REG16(0x0011B2F0UL) ||
        STATUS_FA_4 != REG16(0x0011B2F8UL) ||
        STATUS_FA_3 != REG16(0x0011B2FAUL)) {
        draw_rect(0x0036, 0x0004, 0x0046, 0x001C, LCD_FRAME_A, 0x00, 0x01);
    }

    if (STATUS_FOOT != REG16(0x0011B2F2UL)) {
        if (REG8(0x00FFFEEBUL) < 0x80) {
            int_to_decimal((short)STATUS_FOOT, text);
            text_draw(text, 0x0011, 0x000D, 0x001F, 0x0016, 0x0001, 0x01,
                      (const u8 *)0x001196EAUL);
            REG16(0x0011B2F0UL) = 0xFFFF;
            REG16(0x0011B2F8UL) = 0xFFFF;
        } else {
            const u32 picture = status_foot_record(0x00115422UL);

            text_draw((const char *)status_foot_record(0x0011541EUL),
                      0x0011, 0x000D, 0x001F, 0x0016, 0x0001, 0x01,
                      (const u8 *)0x001196EAUL);

            if (picture != 0) {
                bitmap_draw(0x0038, 0x0004, 0x0041, 0x001C,
                            (const u8 *)picture, LCD_FRAME_A);
            }
        }
        REG16(0x0011B2F2UL) = STATUS_FOOT;
    }

    if (STATUS_E2_2 != REG16(0x0011B2F0UL)) {
        if (REG8(0x00FFFEE2UL) & 0x04) {
            bitmap_draw(0x0036, 0x000B, 0x0046, 0x001B,
                        (const u8 *)0x0034B898UL, LCD_FRAME_A);
        }
        REG16(0x0011B2F0UL) = STATUS_E2_2;
    }

    if (STATUS_FA_4 != REG16(0x0011B2F8UL)) {
        if (REG8(0x00FFFEFAUL) & 0x10) {
            bitmap_draw(0x0036, 0x0005, 0x0046, 0x001B,
                        (const u8 *)0x0034B96FUL, LCD_FRAME_A);
        }
        REG16(0x0011B2F8UL) = STATUS_FA_4;
    }

    if (STATUS_FA_3 != REG16(0x0011B2FAUL)) {
        if (REG8(0x00FFFEFAUL) & 0x08) {
            bitmap_draw(0x0036, 0x0008, 0x0046, 0x0018,
                        (const u8 *)0x0034B9BAUL, LCD_FRAME_A);
        }
        REG16(0x0011B2FAUL) = STATUS_FA_3;
    }

    if (STATUS_FA_7 != REG16(0x0011B2F4UL)) {
        if (REG8(0x00FFFEFAUL) & 0x80) {
            status_mark_clear();
            bitmap_draw(0x0032, 0x001E, 0x0046, 0x0025,
                        (const u8 *)0x0034B8CFUL, LCD_FRAME_A);
        } else if (!(REG8(0x00FFFEF7UL) & 0x08)) {
            status_mark_clear();
        }
        REG16(0x0011B2F4UL) = STATUS_FA_7;
    }

    if (STATUS_F7_3 != REG16(0x0011B2F6UL)) {
        if (REG8(0x00FFFEF7UL) & 0x08) {
            status_mark_clear();
            bitmap_draw(0x0032, 0x0020, 0x0046, 0x0025,
                        (const u8 *)0x0034B920UL, LCD_FRAME_A);
        } else if (!(REG8(0x00FFFEFAUL) & 0x80)) {
            status_mark_clear();
        }
        REG16(0x0011B2F6UL) = STATUS_F7_3;
    }
}

/* H'21B726. The needle position, in steps of four.
 *
 * H'FFFED8 holds it and H'21B6C6 draws it. Box 1 takes four off but not
 * below zero, box 2 puts four on but not above H'10, and neither redraws
 * unless it moved. The screen is left for H'17 the moment the panel asks
 * for H'77.
 *
 * The two locals share a slot: the screen number the leave check writes and
 * the box value the hit test writes are the same two bytes, which is why
 * the hit test's index goes in the *second* local here and the first in the
 * screens that have no leave check. */
u8 needle_pos_screen(void)
{
    u16 value = 0, index = 0;

    if (screen_leave_check(&value, 0x00) == 0x03 && value == 0x0077) {
        REG8(0xFFFEC5UL) = 0x00;
        screen_switch(0x17, 0x01, 0x00);
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0002, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0001) {
        const u8 now = REG8(0x00FFFED8UL);

        if (now != 0) {
            REG8(0x00FFFED8UL) = (u8)(now - 4);
            needle_number_draw();
        }
        return 0x00;
    }
    if (value == 0x0002) {
        const u8 now = REG8(0x00FFFED8UL);

        if (now < 0x10) {
            REG8(0x00FFFED8UL) = (u8)(now + 4);
            needle_number_draw();
        }
    }
    return 0x00;
}

/* H'222DF4. Three runs of screen numbers that want repainting when bit 0 of
 * H'FFFEC4 is up: H'13 to H'16, H'23 to H'24, and H'37 to H'38. Everything
 * else, and a clear bit, leaves H'11A177 alone.
 *
 * The screen number comes back off the stack rather than out of R6L, which
 * is where it arrived -- the routine pushes the register and reads its own
 * low byte again. */
void screen_mark_repaint(u8 screen)
{
    if (!(REG8(0x00FFFEC4UL) & 0x01)) return;

    if (screen < 0x13) return;
    if (screen >= 0x17) {
        if (screen < 0x23) return;
        if (screen >= 0x25) {
            if (screen < 0x37) return;
            if (screen > 0x38) return;
        }
    }
    REG8(0x0011A177UL) = 0x01;
}

/* H'21BD1E. The application's version, drawn once and then left alone.
 *
 * H'11A1B3 is the "not drawn yet" flag: it is cleared on the way in and set
 * again on the way out, so coming back to the screen draws it afresh. The
 * string is the identity block at H'200100 from its fourth byte -- past the
 * "NMM" -- which is six characters and the terminator, and the local it goes
 * into is eight bytes. */
u8 version_screen(void)
{
    char text[8];
    u16 to = 0;

    if (REG8(0x0011A1B3UL) != 0) {
        REG8(0x0011A1B3UL) = 0x00;
        str_copy(text, (const char *)(APP_IDENTITY + 3));
        text_draw(text, 0x0035, 0x0017, 0x005D, 0x001E, 0x0001, 0x01,
                  (const u8 *)0x00119A66UL);
    }

    if (screen_leave_check(&to, 0x00) != 0x03) return 0x00;
    if (to != 0x0077) return 0x00;

    REG8(0x0011A1B3UL) = 0x01;
    REG8(0xFFFEC5UL) = 0x00;
    screen_switch(0x17, 0x01, 0x00);
    return 0x00;
}

/* H'219978. Four boxes: three that go somewhere and one that goes back.
 *
 * Only box H'52 answers 1; the other three answer 0. Going back reads the
 * screen to return to out of H'11B0A6, tells H'222DF4 about it, and puts
 * H'11B114 into H'11B108 on the way. */
u8 menu_four_screens(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0004, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0052) { screen_switch(0x2D, 0x01, 0x00); return 0x01; }
    if (value == 0x0053) { screen_switch(0x39, 0x01, 0x00); return 0x00; }
    if (value == 0x0054) { screen_switch(0x0F, 0x01, 0x00); return 0x00; }

    if (value == 0x001A) {
        if (REG8(0x00114DC6UL) & 0x80) return 0x00;
        screen_stack_pop();
        screen_mark_repaint(REG8(0x0011B0A6UL));
        REG16(0x0011B108UL) = REG16(0x0011B114UL);
        screen_switch(REG8(0x0011B0A6UL), 0x01, 0x00);
    }
    return 0x00;
}

u32 module_reply_buffer(void);

/* H'22298C. The version again, this time with the module's.
 *
 * The application's own comes from the identity block as in H'21BD1E; the
 * module's is whatever H'23E45A points at, and it is only drawn if it is not
 * the empty string at H'250ADF. One box, H'1A, goes back to screen H'4A. */
u8 module_version_screen(u8 first_pass)
{
    char text[8];
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        const char *reply;

        str_copy(text, (const char *)(APP_IDENTITY + 3));
        text_draw(text, 0x007B, 0x004C, 0x00E8, 0x005E, 0x0001, 0x01,
                  (const u8 *)0x0011936EUL);

        reply = (const char *)module_reply_buffer();
        if (str_compare(reply, (const char *)0x00250ADFUL) != 0) {
            text_draw(reply, 0x007B, 0x0099, 0x00E8, 0x00AB, 0x0001, 0x01,
                      (const u8 *)0x0011936EUL);
        }
    }

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;

    if (value == 0x001A) {
        message_show_held(index);
        screen_stack_pop();
        screen_switch(0x4A, 0x01, 0x00);
    }
    return 0x00;
}

/* H'21CF7C and H'21CF86. How deep the screen stack is, and what is at a
 * given depth. Entry zero of the stack is the depth itself, which is why
 * both read from the same address. */
u16 screen_stack_depth(void)
{
    return (u16)REG8(0x0011A18BUL);
}

u8 screen_stack_at(u16 n)
{
    return REG8(0x0011A18BUL + (u32)(long)(short)n);
}

/* H'2220DC. The drawing areas cleared and the queue position taken away. */
void drawing_reset(void)
{
    finish_22950C();
    QUEUE_POS = 0xFFFF;
}

/* H'210544 and H'21056C. The help pictures: two tables of H'24-byte records,
 * nine longwords each, one table per machine. H'219DE0 picks the second when
 * the configuration byte says H'AA -- the machine with the module -- so the
 * first is the one without.
 *
 * The record number is multiplied out as a longword and the field number
 * added to the *low half* of it, so the index is a signed word however far
 * the multiplication carried. */
static u32 help_picture_at(u32 table, u16 entry, u16 part)
{
    return REG32(table + (u32)(long)(short)(u16)
                     ((u16)(entry * 0x24) + (u16)(part << 2)));
}

u32 help_picture(u16 entry, u16 part)
{
    return help_picture_at(0x003CFA2CUL, entry, part);
}

u32 help_picture_module(u16 entry, u16 part)
{
    return help_picture_at(0x003D2024UL, entry, part);
}

/* H'21C150. The version string drawn where H'21BD1E draws it, and with the
 * same font. Like H'20E126 it takes its argument back off the stack rather
 * than out of the register it arrived in. */
void version_text_draw(const char *s)
{
    text_draw(s, 0x0035, 0x0017, 0x005D, 0x001E, 0x0001, 0x01,
              (const u8 *)0x00119A66UL);
}

/* H'218C1A. A cursor that blinks: one pass in ten flips it, and H'11B31C
 * says which way it is. The line is H'1B tall and drawn in the visible
 * buffer, so nothing has to be flipped over for it to show. */
void cursor_blink(u16 x, u16 y)
{
    const u16 n = (u16)(REG16(0x0011B31AUL) + 1);

    REG16(0x0011B31AUL) = n;
    if (n != 0x000A) return;

    draw_line(x, y, x, (u16)(y + 0x1A), LCD_FRAME_A,
              (u8)(REG8(0x0011B31CUL) != 0 ? 0x00 : 0x03));

    REG8(0x0011B31CUL) = (u8)(REG8(0x0011B31CUL) == 0 ? 0x01 : 0x00);
    REG16(0x0011B31AUL) = 0x0000;
}

/* H'24A374. Whether the thing that owns the link is still waiting for what
 * it asked for. Six owners have an answer; every other owner, an owner of
 * zero, a state at H'114D86 that is not three, and a clear bit 0 of
 * H'FFFEC4 all say no.
 *
 * H'114DB9 is re-read before each of the six tests rather than kept, which
 * is in the original and makes no difference here. */
u8 link_owner_waiting(void)
{
    if (!(REG8(0x00FFFEC4UL) & 0x01)) return 0x00;
    if (REG8(0x00114DB9UL) == 0) return 0x00;
    if (REG8(0x00114D86UL) != 0x03) return 0x00;

    if (REG8(0x00114DB9UL) == 0x01 && (MACHINE_FLAGS & 0x08)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x02 && !(MACHINE_FLAGS & 0x08)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x11 && !(REG8(0x00114D51UL) & 0x01)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x18 && (REG8(0x00114D51UL) & 0x01)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x10 && !(MACHINE_FLAGS & 0x08)) return 0x01;
    if (REG8(0x00114DB9UL) == 0x1A && !(REG8(0x00FFFEC7UL) & 0x01)) return 0x01;

    return 0x00;
}

/* H'217CCE. One third of a box marked.
 *
 * The box is cleared in the back buffer and then a narrow rectangle drawn
 * inside it in colour 2: the first third at +2, the second at +H'0E, the
 * third at +H'1A, each six wide and running from two below the top to
 * H'20 below it. Any other third does nothing at all, not even the clear.
 *
 * Like H'212E78 the whole H'12-byte entry is copied into a local first and
 * the coordinates read out of the copy. */
void hitbox_third_mark(u16 box, u8 which)
{
    const u32 e = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    u16 copy[9];
    u16 x0, y0, x1, y1;
    int n;

    for (n = 0; n < 9; n++) copy[n] = REG16(e + (u32)(2 * n));

    if      (which == 0x01) x0 = (u16)(copy[0] + 0x02);
    else if (which == 0x02) x0 = (u16)(copy[0] + 0x0E);
    else if (which == 0x03) x0 = (u16)(copy[0] + 0x1A);
    else                    return;

    y0 = (u16)(copy[1] + 0x02);
    x1 = (u16)(x0 + 0x06);
    y1 = (u16)(copy[1] + 0x20);

    draw_rect(copy[0], copy[1], copy[2], copy[3], LCD_FRAME_B, 0x00, 0x01);
    draw_rect(x0, y0, x1, y1, LCD_FRAME_B, 0x02, 0x01);
}

/* H'212FF0. A run of boxes moved along: the value at +8 and the list at
 * +H'0C copied from one box to another, and the state carried over with
 * H'211518 whenever it differs so that the picture follows.
 *
 * Which way the copy runs depends on which way the run moves, so that a
 * source and destination that overlap do not eat themselves: moving down
 * copies from the first box forward, moving up copies from the last box
 * back. The two ends are the same distance apart either way, which is what
 * [last] - [first] + [dest] works out. */
void hitbox_run_shift(u16 first, u16 last, u16 dest)
{
    const u32 table = HITBOX_TABLE;
    short from, to;

    if ((short)first > (short)dest) {
        for (from = (short)first, to = (short)dest;
             from <= (short)last; from++, to++) {
            const u32 d = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)to);
            const u32 s = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)from);

            REG16(d + 0x08) = REG16(s + 0x08);
            REG32(d + 0x0C) = REG32(s + 0x0C);
            if (REG8(d + 0x10) != REG8(s + 0x10)) {
                hitbox_set_state((u16)to, (u16)to, REG8(s + 0x10), 0);
            }
        }
        return;
    }

    if ((short)first < (short)dest) {
        for (from = (short)last,
             to = (short)(u16)((u16)((u16)last - (u16)first) + (u16)dest);
             from >= (short)first; from--, to--) {
            const u32 d = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)to);
            const u32 s = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)from);

            REG16(d + 0x08) = REG16(s + 0x08);
            REG32(d + 0x0C) = REG32(s + 0x0C);
            if (REG8(d + 0x10) != REG8(s + 0x10)) {
                hitbox_set_state((u16)to, (u16)to, REG8(s + 0x10), 0);
            }
        }
    }
}

/* H'21CFB0. The bar beside the balance setting, drawn from the middle of
 * its track outwards.
 *
 * The track runs from H'38 to H'6A below the origin and the bar's far end
 * is the value put through a straight line -- times minus three and an
 * eighth, plus 106 and a half -- so a value of eight lands exactly on the
 * middle at H'51 and larger values go up. The bar itself is colour 2 and
 * whatever is left of the track is cleared, which takes two rectangles when
 * the bar is short and one when it fills its half. */
void balance_bar_draw(u16 value)
{
    const u16 x0  = (u16)(HITBOX_X0 + 0x89);
    const u16 x1  = (u16)(HITBOX_X0 + 0x9B);
    const u16 top = (u16)(HITBOX_Y0 + 0x38);
    const u16 mid = (u16)(HITBOX_Y0 + 0x51);
    const u16 bot = (u16)(HITBOX_Y0 + 0x6A);
    const u16 end = (u16)(HITBOX_Y0 +
        (u16)(int)((float)(long)(short)value * -3.125f + 106.5f));

    draw_rect(x0, mid, x1, end, LCD_FRAME_A, 0x02, 0x01);

    if ((short)end <= (short)mid) {
        if ((short)end > (short)top) {
            draw_rect(x0, top, x1, (u16)(end - 1), LCD_FRAME_A, 0x00, 0x01);
        }
        draw_rect(x0, (u16)(mid + 1), x1, bot, LCD_FRAME_A, 0x00, 0x01);
    } else {
        draw_rect(x0, top, x1, (u16)(mid - 1), LCD_FRAME_A, 0x00, 0x01);
        if ((short)end < (short)bot) {
            draw_rect(x0, (u16)(end + 1), x1, bot, LCD_FRAME_A, 0x00, 0x01);
        }
    }
}

/* H'218378. Two strokes of a preview drawn or rubbed out.
 *
 * There are two tables of H'10-byte entries in RAM, H'1152DE and H'11531E,
 * four entries each: two line segments per entry, x0 y0 x1 y1 twice over.
 * The second argument picks the table and the third picks the colour -- 3
 * to draw, 0 to rub out -- and everything is offset by H'8C across and H'64
 * down, into the back buffer.
 *
 * The original spells all four branches out in full, sixteen table reads
 * apiece, each one recomputing the index. */
void preview_stroke_draw(u16 index, u8 alt, u8 on)
{
    const u32 e = (alt != 0 ? 0x001152DEUL : 0x0011531EUL) +
        (u32)(long)(short)(u16)((u16)(index << 4));
    const u8 colour = (u8)(on != 0 ? 0x03 : 0x00);

    draw_line((u16)(REG16(e + 0x00) + 0x8C), (u16)(REG16(e + 0x02) + 0x64),
              (u16)(REG16(e + 0x04) + 0x8C), (u16)(REG16(e + 0x06) + 0x64),
              LCD_FRAME_B, colour);
    draw_line((u16)(REG16(e + 0x08) + 0x8C), (u16)(REG16(e + 0x0A) + 0x64),
              (u16)(REG16(e + 0x0C) + 0x8C), (u16)(REG16(e + 0x0E) + 0x64),
              LCD_FRAME_B, colour);
}

/* H'213274 and H'213356. A run of boxes scrolled through its list, on and
 * back.
 *
 * Both move the run with H'212FF0 first and then patch up the [step] boxes
 * left over at the far end: on, their slots go up by [step] and any that
 * lands past the end of the list is greyed; back, their slots come down and
 * any that was grey and is now inside the list is un-greyed. The un-greying
 * writes the state byte straight rather than going through H'211518, but
 * the greying does not, so that the picture is drawn.
 *
 * The list's length comes out of whatever list the *first* box points at,
 * before anything has moved. Both answer with box one's slot. */
u16 hitbox_list_scroll_on(u16 first, u16 last, u16 step)
{
    const u32 table = HITBOX_TABLE;
    const u16 length = REG16(REG32(table +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * first) + 0x0C));
    short i;

    hitbox_run_shift((u16)(step + first), last, first);

    for (i = (short)(u16)((u16)last - (u16)(step - 1));
         i <= (short)last; i++) {
        const u32 e = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        if (REG8(e + 0x10) == 0x02) REG8(e + 0x10) = 0x00;
        REG16(e + 0x08) = (u16)(REG16(e + 0x08) + step);
        if ((short)REG16(e + 0x08) > (short)length) {
            hitbox_set_state((u16)i, (u16)i, 0x02, 0);
        }
    }

    hitbox_redraw_run(first, last);
    return REG16(table + 0x1A);
}

u16 hitbox_list_scroll_back(u16 first, u16 last, u16 step)
{
    const u32 table = HITBOX_TABLE;
    const u16 length = REG16(REG32(table +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * first) + 0x0C));
    short i;

    hitbox_run_shift(first, (u16)(last - step), (u16)(step + first));

    for (i = (short)first; (short)(u16)(step + first) > i; i++) {
        const u32 e = table + (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        REG16(e + 0x08) = (u16)(REG16(e + 0x08) - step);
        if (REG8(e + 0x10) == 0x02 &&
            (short)REG16(e + 0x08) <= (short)length) {
            REG8(e + 0x10) = 0x00;
        }
    }

    hitbox_redraw_run(first, last);
    return REG16(table + 0x1A);
}

/* ---- the module's state machines, and what they call --------------------
 * H'235B0E is the module's own eighteen-state machine, H'2417D4 the nine
 * states that fetch a pattern, and H'2431EE the twelve that start and stop
 * the sewing. They call each other and call back into H'235B0E through
 * H'244DE0, so the whole cluster is written together and declared here.
 */
void module_state_machine(void);
void module_wait_pass(void);
void module_link_lost(void);
void module_screen_step(void);
void module_fetch_step(u8 *step);
void module_run_step(u8 *step);

/* H'244D64. Whether the module is in a state that will take a message. The
 * two states that will are H'04 and H'06, and in H'04 bit 3 of H'FFFEC1
 * takes it away again. Refusals hand the link to an owner first. */
u8 module_can_talk(void)
{
    u8 state;

    if (REG8(0x00114DB9UL) != 0) return 0x00;

    state = REG8(0x00FFFEC0UL);
    if (state != 0x04 && state != 0x06) { link_claim(0x03); return 0x00; }
    if (REG8(0x00FFFEC1UL) & 0x08) { link_claim(0x02); return 0x00; }
    return 0x01;
}

/* H'231E28. A percentage with a "%" after it in the first left-hand label,
 * the same shape as H'231DD2's minutes. */
void label_percent_left(u8 value)
{
    int_to_decimal((short)(u16)value, (char *)0x0011F2D6UL);
    REG8(0x0011F294UL) = 0x25;   /* '%' */
    REG8(0x0011F295UL) = 0x00;
    str_append((char *)0x0011F2D6UL, (const char *)0x0011F294UL);
    text_left_94((const char *)0x0011F2D6UL);
}

/* H'23E510. Stopping the module: four messages sent one after another, with
 * H'11A63E walking the steps and bit H'8000 of H'11A63A held up for the
 * whole run. The caller's step counter moves on at the end.
 *
 * Step three is the odd one: when the link is busy on the H'21 mask it does
 * not wait, it moves on regardless. */
void module_stop_sequence(u8 *step)
{
    const u8 n = REG8(0x0011A63EUL);

    if (n == 0x00) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG16(0x0011A63AUL) |= 0x8000;
        REG8(0x0011F2A1UL) = 0x03;
        REG8(0x0011A61CUL) = 0x09;
        link_send_start();
        REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
        return;
    }

    if (n == 0x01) {
        if ((REG8(0x00114D50UL) & 0x21) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x01;
        link_send_start();
        REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
        return;
    }

    if (n == 0x02) {
        if ((REG8(0x00114D50UL) & 0x22) != 0) return;
        if (REG8(0x0011F29EUL) != 0) return;
        if (REG8(0x0011F2B6UL) != 0) return;

        REG8(0x0011F2A1UL) = 0x02;
        link_send_start();
        REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
        return;
    }

    if (n == 0x03) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            (REG8(0x00114D50UL) & 0x22) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
            REG8(0x0011F2A1UL) = 0x0F;
            REG8(0x0011F2A2UL) = 0x01;
            link_send_start();
            REG8(0x0011A63EUL) = (u8)(REG8(0x0011A63EUL) + 1);
            return;
        }
        /* every refusal here still moves on -- step three never waits */
        REG8(0x0011A63EUL) = (u8)(n + 1);
        return;
    }

    if (REG8(0x0011F29EUL) != 0) return;
    if (REG8(0x0011F2B6UL) != 0) return;

    *step = (u8)(*step + 1);
    REG8(0x0011A63EUL) = 0x00;
    REG16(0x0011A63AUL) = (u16)(REG16(0x0011A63AUL) & ~0x8000);
}

/* H'2317B2. Back to screen H'15 with the panel state set to eight. */
void module_to_screen_15(void)
{
    REG8(0x00114D8EUL) = 0x08;
    screen_switch(0x15, 0x01, 0x00);
}

/* H'23DE04. Message H'03/H'0B sent, waiting for the link both before and
 * after -- a real wait, turning the host service over until it is quiet. The
 * answer handed back is whatever H'11F2B6 held at the end, which is always
 * nought, and the caller stores it anyway. */
u8 module_send_0B(void)
{
    for (;;) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            (REG8(0x00114D50UL) & 0x22) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) break;
        rom_host_service();
    }

    if (REG8(0x00114D8CUL) != 0) {
        REG8(0x00114D8CUL) = 0x00;
        REG8(0x00114D8BUL) = 0x00;
    }

    REG8(0x0011F2A1UL) = 0x03;
    REG8(0x0011A61BUL) = 0x0B;
    link_send_start();

    for (;;) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) break;
        rom_host_service();
    }

    return 0x00;
}

/* H'243A8C. The run control the two big machines share. What arrives says
 * what to do: H'00 arms it, H'01 starts it, and anything else runs one pass
 * of the sub-machine in H'11F52D.
 *
 * The passes fall into one another: H'00 falls into H'01 and H'01 falls into
 * H'02, so one call can do three steps. H'11F52F says why it is stopping --
 * H'01 the module finished, H'02 or H'03 something asked it to -- and that
 * chooses which screen it goes back to. Everything past H'0B, and every
 * number the table does not know, ends in the same reset.
 */
u8 module_run_control(u8 what)
{
    u8 n, v;

    if (what == 0x00) {
        if (REG8(0x0011F52FUL) >= 0x0A) return 0x00;

        REG8(0x00114D4FUL) &= (u8)~0x20;
        if (REG8(0x0011F52FUL) != 0) {
            REG8(0x0011F52DUL) = 0x0A;
            REG8(0x0011F52EUL) = 0x00;
        } else {
            REG8(0x0011F52CUL) = 0x00;
            REG8(0x0011F52DUL) = 0x00;
            REG8(0x0011F52FUL) = 0x00;
            REG8(0x0011F52EUL) = 0x00;
        }
        return 0x01;
    }

    if (what == 0x01) {
        if (REG8(0x0011F52CUL) != 0) return 0x00;

        REG8(0x00114D4FUL) &= (u8)~0x20;
        REG8(0x0011F52CUL) = 0x01;
        REG8(0x0011F52DUL) = 0x00;
        REG8(0x0011F52FUL) = 0x00;
        REG8(0x0011F52EUL) = 0x00;
        REG8(0x0011F530UL) = 0x01;
        return 0x01;
    }

    n = REG8(0x0011F52DUL);

    if (n == 0x00) {
        if (REG8(0x0011F52CUL) == 0) return 0x01;

        if (REG8(0x00114D4FUL) & 0x20) {
            REG8(0x0011F531UL) = 0x01;
            REG8(0x00114D73UL) = 0x01;
            REG8(0x00114D4FUL) &= (u8)~0x20;
            v = REG8(0x0011F530UL);
            if (v < 0x63) { v = (u8)(v + 1); REG8(0x0011F530UL) = v; }
            if (v < 0x64) label_percent_left(v);
        }

        if (!(REG8(0x00FFFEDBUL) & 0x04)) return 0x01;

        label_percent_left(0x62);
        REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        n = 0x01;
    }

    if (n == 0x01) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
            REG8(0x00114D4FUL) &= (u8)~0x20;
            REG8(0x0011F52CUL) = 0x00;
            REG8(0x0011F52DUL) = 0x00;
            REG8(0x0011F52FUL) = 0x00;
            REG8(0x0011F52EUL) = 0x00;
            return 0x01;
        }
        if (!(REG8(0x00114D4FUL) & 0x20)) return 0x01;

        REG8(0x00114D4FUL) &= (u8)~0x20;
        REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        n = 0x02;
    }

    if (n == 0x02) {
        if (REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0) {
            REG8(0x0011F2A1UL) = 0x0E;
            REG8(0x0011F2A2UL) |= 0x04;
            link_send_start();
            label_percent_left(0x64);
            if (REG8(0x0011A63DUL) != 0) REG8(0x0011F52FUL) = 0x02;
            if (REG8(0x00114D7DUL) != 0) REG8(0x0011F52FUL) = 0x03;
            if (REG8(0x001040B4UL) != 0) REG8(0x0011F52FUL) = 0x01;
            REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        }
        return 0x01;
    }

    if (n == 0x03) {
        if ((REG8(0x00114D50UL) & 0x21) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0 &&
            (REG8(0x00114D50UL) & 0x22) == 0 &&
            REG8(0x0011F29EUL) == 0 && REG8(0x0011F2B6UL) == 0)
            REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        return 0x01;
    }

    if (n == 0x04) return 0x01;

    if (n == 0x0A) {
        if (REG8(0x001040B4UL) == 0 && REG8(0x0011A63DUL) == 0 &&
            REG8(0x00114D7DUL) == 0 && REG8(0x00114D66UL) == 0)
            REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
        return 0x01;
    }

    if (n == 0x0B) {
        const u8 why = REG8(0x0011F52FUL);

        if (why == 0x01) {
            REG8(0x00114D8EUL) = 0x00;
            REG8(0x001040B5UL) = module_send_0B();
            REG8(0x00114D72UL) = 0x01;
            module_to_screen_15();
        } else if (why >= 0x02 && why <= 0x03) {
            REG8(0x00114D93UL) = 0x00;
            REG8(0x00114D98UL) = 0x00;
            REG8(0x0011F30EUL) = 0x00;
            REG8(0x00114D72UL) = 0x01;
            module_to_screen_15();
        } else {
            REG8(0x0011F52CUL) = 0x00;
        }

        if (REG8(PAT_B(0x03)) == 0x03) REG8(0x00114D92UL) = 0xFF;
        REG8(0x0011F52DUL) = (u8)(REG8(0x0011F52DUL) + 1);
    }

    REG8(0x00114D4FUL) &= (u8)~0x20;
    REG8(0x0011F52CUL) = 0x00;
    REG8(0x0011F52DUL) = 0x00;
    REG8(0x0011F52FUL) = 0x00;
    REG8(0x0011F52EUL) = 0x00;
    return 0x01;
}

/* ---- the module key, and what it reaches -------------------------------
 * H'237E3C is the handler for a key press when the embroidery module is
 * attached, and it is a jump table of twelve: the key codes H'6D, H'70-H'75,
 * H'77-H'79, H'7D and H'81 that H'21F68C names. Nine routines under it are
 * written here; the handler itself waits on the module's own state machine
 * at H'235B0E, which is not written yet.
 */

void link_delay(u16 units);
void link_send_start(void);

/* H'23E45A. Where the module's replies land. Two instructions. */
u32 module_reply_buffer(void)
{
    return 0x00104C90UL;
}

/* H'230E6E. The first screen store emptied, a word at a time, all H'2580 of
 * them -- one whole screen. */
void screen_store1_clear(void)
{
    u32 p;
    long n;

    p = 0x000ECB10UL;
    for (n = 0; n < 0x2580L; n++) { REG16(p) = 0x0000; p += 2; }
}

/* H'230EA8. The embroidery panel put away into the first store: a box from
 * H'26,H'53 to H'C1,H'EA, and only when H'11F4E6 says there is one. */
void embroidery_panel_save(void)
{
    if (REG8(0x0011F4E6UL) == 0) return;

    region_copy(0x0026, 0x0053, 0x00C1, 0x00EA, 0x0053,
                LCD_FRAME_A, 0x000ECB10UL);
}

/* H'236E9A. The module's cursor rubbed out: a twenty-five pixel box around
 * H'11F4DC, H'11F4DE fetched back from the third store, and the position
 * forgotten. H'114D99 is set on the way in whatever else happens. */
void module_cursor_erase(void)
{
    REG8(0x00114D99UL) = 0x01;

    if (REG8(0x00114D98UL) != 0x01) return;

    REG8(0x00114D98UL) = 0x00;

    if (REG16(0x0011F4DCUL) != 0 && REG16(0x0011F4DEUL) != 0)
        region_copy((u16)(REG16(0x0011F4DCUL) - 0x0C),
                    (u16)(REG16(0x0011F4DEUL) - 0x0C),
                    (u16)(REG16(0x0011F4DCUL) + 0x0C),
                    (u16)(REG16(0x0011F4DEUL) + 0x0C),
                    (u16)(REG16(0x0011F4DEUL) - 0x0C),
                    0x000F6110UL, LCD_FRAME_A);

    REG16(0x0011F4DCUL) = 0x0000;
    REG16(0x0011F4DEUL) = 0x0000;
}

/* H'2426F0. Bit 1 of H'11A63C set when there is a pattern to be going on
 * with: either one waiting in H'11A640, or the current slot's record says
 * kind three. */
void pattern_mark_ready(void)
{
    if (REG8(0x0011A640UL) == 0 && REG8(PAT_B(0x03)) != 0x03) return;

    REG8(0x0011A63CUL) |= 0x02;
}

/* H'23191C. Asks the module to go home, and says whether it was already on
 * its way. H'114D93 is the ask; H'FFFEC6 has to be at rest, meaning zero or
 * five. When there is no ask, a module already homing has its step counter
 * put back to one and the answer is H'01.
 *
 * The two branches share their tail, so a module that is asked while
 * H'FFFEC6 is busy falls into the second test rather than leaving. */
u8 module_home_request(void)
{
    if (REG8(0x00114D93UL) != 0) {
        const u8 state = REG8(0x00FFFEC6UL);

        if (state == 0x00 || state == 0x05) {
            REG8(0x00FFFEC4UL) |= 0x20;
            REG8(0x00114D66UL) = 0x01;
            REG8(0x00114D62UL) = 0x08;
            REG8(0x00114D65UL) = 0x00;
            REG8(0x00114D94UL) = 0x00;
            if (REG8(0x00114D95UL) != 0) {
                REG8(0x00114D95UL) = 0x00;
                REG8(0x00114D50UL) &= (u8)~0x01;
                REG8(0x00114D50UL) &= (u8)~0x02;
            }
            return 0x00;
        }
    }

    if (REG8(0x00114D66UL) != 0) {
        REG8(0x00114D65UL) = 0x01;
        return 0x01;
    }
    return 0x00;
}

/* H'2431C2. The end of a talk to the module, but only from three of the
 * hardware's states. ITU1 is handed back, the machine parked, and ITU1
 * borrowed again -- in that order, which is the order the ROM has. */
void module_talk_end(void)
{
    const u8 state = REG8(0x00FFFEC0UL);

    if (state != 0x04 && state != 0x06 && state != 0x07) return;

    REG8(0x00114DA0UL) = 0x01;
    itu1_return();
    module_park();
    itu1_borrow();
}

/* H'244A2A. The module started again from nothing: the link brought up, a
 * message H'04/H'06 sent, and a quarter-second wait for the answer. Bit 7 of
 * H'114D51 is the answer having arrived. */
void module_restart(void)
{
    if (REG8(0x0011F29EUL) != 0 || REG8(0x0011F2B6UL) != 0) return;

    sci0_module_init();
    link_delay(0x000A);
    REG8(0x0011F2A1UL) = 0x04;
    REG8(0x0011F2A2UL) = 0x06;
    link_send_start();
    link_delay(0x00FA);

    if (!(REG8(0x00114D51UL) & 0x80)) return;

    REG8(0x00FFFEC4UL) |= 0x01;
    REG8(0x00FFFEC4UL) |= 0x20;
    REG8(0x00114D78UL) = 0x03;
    REG8(0x00114DA0UL) = 0x00;
    REG8(0x00114DBAUL) = 0x01;
    REG8(0x00114DBBUL) = 0x00;
    module_talk_end();
}

/* H'249DE8. Waits for the module to name itself: five bytes at the head of
 * the reply buffer against five in the machine's own identity block at
 * H'200103, tried up to H'9C4 times with a delay between. Returns H'01 when
 * they matched and H'00 when the tries ran out.
 *
 * The two sides are compared as words, and the ROM's byte is sign extended
 * while the module's is not, so any expected byte of H'80 or over could
 * never match. None of the five is.
 */
u8 module_identify(void)
{
    short n;

    for (n = 0; n <= 0x09C4; n++) {
        const u32 buf = module_reply_buffer();
        u8 i, same = 0;

        for (i = 0; i <= 0x04; i++) {
            if ((u16)REG8(buf + i) ==
                (u16)(short)(signed char)REG8(0x00200103UL + i)) same++;
        }
        if (same >= 0x05) break;
        link_delay(0x0001);
    }

    return n >= 0x09C4 ? 0x00 : 0x01;
}

/* H'24ADF0. The ROM's memmove, and it is a proper one: an overlap where the
 * source is below the destination is copied backwards so it does not eat its
 * own tail. The loop count is tested before the decrement, so a length of
 * zero copies nothing. */
void *mem_move(void *dst, const void *src, u32 len)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    u32 n = len;

    if ((u32)s <= (u32)d && (u32)s + len >= (u32)d) {
        d += len;
        s += len;
        while (n != 0) { n--; *--d = *--s; }
        return dst;
    }
    while (n != 0) { n--; *d++ = *s++; }
    return dst;
}

/* H'21F36E. One of four stored screens copied to or from the front buffer.
 * They sit at H'0ECB10 and every H'4B00 after it -- one whole screen each --
 * and [out] says which way the copy goes. */
void screen_store(u8 slot, u8 out)
{
    u32 at;

    switch (slot) {
    case 0x01: at = 0x000ECB10UL; break;
    case 0x02: at = 0x000F1610UL; break;
    case 0x03: at = 0x000F6110UL; break;
    case 0x04: at = 0x000FAC10UL; break;
    default: return;
    }

    if (out != 0) region_copy(0x0000, 0x0000, 0x013F, 0x00EF, 0x0000,
                              LCD_FRAME_A, at);
    else          region_copy(0x0000, 0x0000, 0x013F, 0x00EF, 0x0000,
                              at, LCD_FRAME_A);
}

void link_delay(u16 units);

/* H'248668. Waits for the link to go quiet, giving up after a hundred turns
 * of H'0A units each. Returns 1 when it went quiet and 0 when it did not.
 *
 * The test is written out twice in the original, the same three conditions
 * each time, and then a fourth on bit 5 of H'114D50. Reproduced as it is. */
u8 link_wait_idle(void)
{
    u8 n;

    for (n = 0; n < 0x64; n++) {
        if ((REG8(0x114D50UL) & 0x21) == 0 &&
            REG8(0x11F29EUL) == 0 && REG8(0x11F2B6UL) == 0 &&
            (REG8(0x114D50UL) & 0x22) == 0 &&
            REG8(0x11F29EUL) == 0 && REG8(0x11F2B6UL) == 0 &&
            !(REG8(0x114D50UL) & 0x20)) {
            return 0x01;
        }
        link_delay(0x000A);
    }
    return 0x00;
}

/* H'229714. The two arrows beside the pattern strip: one is drawn lit when
 * there is something before the cursor and the other when there is something
 * after it. H'11B3D6 and H'11B3D7 remember which way each is drawn so the
 * pair is only repainted when it changes -- unless [fresh] is set, when both
 * go down whatever they were.
 */
#define ARROW_BACK_ON   0x0034E390UL
#define ARROW_BACK_OFF  0x0034E3C7UL
#define ARROW_ON_ON     0x0034E3FEUL
#define ARROW_ON_OFF    0x0034E435UL

void picker_arrows(u16 back_box, u16 on_box, u8 fresh)
{
    if (fresh != 0) {
        if ((short)PICK_POS > (short)PICK_FIRST) {
            REG8(0x11B3D6UL) = 0x01;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_ON);
        } else {
            REG8(0x11B3D6UL) = 0x00;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_OFF);
        }
        if ((short)PICK_POS < (short)PICK_LAST) {
            REG8(0x11B3D7UL) = 0x01;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_ON);
        } else {
            REG8(0x11B3D7UL) = 0x00;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_OFF);
        }
        return;
    }

    if (PICK_POS == PICK_FIRST) {
        if (REG8(0x11B3D6UL) != 0) {
            REG8(0x11B3D6UL) = 0x00;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_OFF);
        }
    } else if ((short)PICK_POS > (short)PICK_FIRST) {
        if (REG8(0x11B3D6UL) == 0) {
            REG8(0x11B3D6UL) = 0x01;
            hitbox_blit(back_box, LCD_FRAME_A, ARROW_BACK_ON);
        }
    }

    if (PICK_POS == PICK_LAST) {
        if (REG8(0x11B3D7UL) != 0) {
            REG8(0x11B3D7UL) = 0x00;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_OFF);
        }
    } else if ((short)PICK_POS < (short)PICK_LAST) {
        if (REG8(0x11B3D7UL) == 0) {
            REG8(0x11B3D7UL) = 0x01;
            hitbox_blit(on_box, LCD_FRAME_A, ARROW_ON_ON);
        }
    }
}

/* H'21341E. The box carrying the current speed found, lit, and its item
 * drawn in the preview panel. */
void hitbox_select_current(u16 first, u16 last)
{
    const u16 box = hitbox_find(first, last, REG16(0xFFFEE0UL), 0x01);

    if (hitbox_kind(box) == 0x01) hitbox_set_state(box, box, 0x00, 0);
    hitbox_set_state(box, box, 0x01, 0);
    item_preview(REG16(0xFFFEE0UL));
}

/* ---- the touch hit test -----------------------------------------------
 * H'210EE2, and H'211252 beside it. Which box the operator pressed.
 *
 * The panel leaves a raw reading in H'FFFED9 and H'FFFEDA and the two are
 * turned into screen coordinates by a straight line each -- gain at H'11A87E
 * and H'11A882, offset at H'11A886 and H'11A88A, all four single-precision
 * floats. Below 5 on either axis means nothing is being touched.
 *
 * The answer is one of three codes: 2 for "nothing to do", 3 for "this box,
 * act on it". The value the box stands for and its index go back through
 * two pointers.
 *
 * H'211252 is the same thing for a press that arrives down the serial link
 * rather than off the glass: H'11F547 and H'11F548 holding H'CA say a host
 * is driving, and then the box number comes from H'11F549.
 */
static float float_at(u32 a)
{
    union { u32 u; float f; } v;

    v.u = REG32(a);
    return v.f;
}

/* H'21548A. What a press actually does. Written out below the two hit
 * tests, once everything it calls exists. */
void screen_action(u16 value, u16 index, u8 second);

/* The value a box stands for, and the two halves of a range on the screens
 * that edit one. */
static u16 hitbox_value_of(u32 e, u16 *out_value)
{
    const u32 list = REG32(e + 0x0C);

    if (list == 0) {
        *out_value = REG16(e + 0x08);
        return *out_value;
    }

    *out_value = REG16(list +
        (u32)(long)(short)(u16)(REG16(e + 0x08) << 1));

    /* On the three screens that edit a range, a box flagged as one of a
     * pair pushes the old value down into H'11A188 and takes H'11A186 for
     * itself. On every other screen -- and on those three for a box that is
     * not one of a pair -- a flagged box clears both instead. */
    if ((REG8(0x11A169UL) == 0x44 || REG8(0x11A169UL) == 0x30 ||
         REG8(0x11A169UL) == 0x45) && REG8(e + 0x0A) == 0x01) {
        REG16(0x11A188UL) = REG16(0x11A186UL);
        REG16(0x11A186UL) = REG16(e + 0x08);
        return *out_value;
    }
    if (REG8(e + 0x0A) == 0x01) {
        REG16(0x11A186UL) = 0x0000;
        REG16(0x11A188UL) = 0x0000;
    }
    return *out_value;
}

/* What both of them do once a box has been picked: the beep, the action if
 * one is due, and the hold-off that keeps one press from reading as many.
 * [seen] is where the last value is remembered -- the two have one each. */
static u8 hitbox_press(u32 e, u16 value, u16 index, u32 seen)
{
    if (touch_allowed(value) == 0) return 0x02;

    message_beep((u16)((REG8(e + 0x0A) == 0x01) ? 0x0001 : 0x0002));

    if (REG8(0x11A16FUL) != 0) {
        screen_action(value, index,
                      (u8)((REG8(e + 0x0A) == 0x01) ? 0x00 : 0x01));
        REG16(seen) = 0xFFFF;
        return 0x02;
    }

    if (value != REG16(seen)) {
        REG16(seen) = value;
        touch_holdoff_start();
        return 0x03;
    }

    if (touch_holdoff_done() != 0) return 0x03;
    return 0x02;
}

/* H'211252. */
u8 remote_hit(u16 first, u16 last, u16 *out_value, u16 *out_index)
{
    const u16 box = (u16)REG8(0x11F549UL);
    u32 e;

    if (REG8(0x11F547UL) != 0xCA || box == 0) {
        REG8(0x11F549UL) = 0x00;
        REG16(0x11A1A2UL) = 0xFFFF;
        REG8(0x11A1A1UL) = 0x00;
        return 0x02;
    }

    if (REG8(0x11A1A0UL) != REG8(0x11A169UL)) {
        REG8(0x11A1A0UL) = REG8(0x11A169UL);
        REG8(0x11B2CFUL) = 0x01;
    }
    REG8(0x11A1A1UL) = 0x01;

    if ((short)box < (short)first || (short)box > (short)last) {
        REG8(0x11F549UL) = 0x00;
        return 0x02;
    }

    *out_index = box;
    e = HITBOX_TABLE + (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    hitbox_value_of(e, out_value);
    REG8(0x11F549UL) = 0x00;

    return hitbox_press(e, *out_value, box, 0x11A1A2UL);
}

/* H'210EE2. */
u8 touch_hit(u16 first, u16 last, u16 *out_value, u16 *out_index)
{
    u16 ox, oy;
    u32 table;
    short i;

    if (REG8(0x11F547UL) == 0xCA && REG8(0x11F548UL) == 0xCA) {
        return remote_hit(first, last, out_value, out_index);
    }

    if (REG8(0xFFFED9UL) < 0x05 || REG8(0xFFFEDAUL) < 0x05) {
        REG16(0x11A19EUL) = 0xFFFF;
        REG8(0x11A19DUL) = 0x00;
        return 0x02;
    }

    if (REG8(0x11A19CUL) != REG8(0x11A169UL)) {
        REG8(0x11A19CUL) = REG8(0x11A169UL);
        REG8(0x11B2CEUL) = 0x01;
    }

    /* A screen that has just changed swallows the press that is still down
     * from the last one. */
    if (REG8(0x11A19DUL) != 0 && REG8(0x11B2CEUL) != 0) return 0x02;
    if (REG8(0x11A19DUL) == 0 && REG8(0x11B2CEUL) != 0) REG8(0x11B2CEUL) = 0x00;
    REG8(0x11A19DUL) = 0x01;

    REG16(0x11B102UL) = (u16)(int)
        ((float)(u32)REG8(0xFFFED9UL) * float_at(0x11A87EUL) +
         float_at(0x11A886UL));
    REG16(0x11B104UL) = (u16)(int)
        ((float)(u32)REG8(0xFFFEDAUL) * float_at(0x11A882UL) +
         float_at(0x11A88AUL));

    ox = HITBOX_X0;
    oy = HITBOX_Y0;
    table = HITBOX_TABLE;

    for (i = (short)first; i <= (short)last; i++) {
        const u32 e = table +
            (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);

        if (REG8(e + 0x10) == 0x02) continue;
        if ((short)(REG16(e + 0x00) + ox) > (short)REG16(0x11B102UL)) continue;
        if ((short)(REG16(e + 0x04) + ox) < (short)REG16(0x11B102UL)) continue;
        if ((short)(REG16(e + 0x02) + oy) > (short)REG16(0x11B104UL)) continue;
        if ((short)(REG16(e + 0x06) + oy) < (short)REG16(0x11B104UL)) continue;

        *out_index = (u16)i;
        hitbox_value_of(e, out_value);

        return hitbox_press(e, *out_value, (u16)i, 0x11A19EUL);
    }
    return 0x02;
}

/* Every one of these is a leaf of the dispatch above, and every one of them
 * is four instructions that pick a record and branch out. They are the table
 * entries, not routines, so they are written above as numbers:
 *
 * H'21562E  H'2156AE  H'2158D6  H'215912  H'21592C  H'215946  H'215960  H'21597A
 * H'215994  H'2159AE  H'2159C8  H'2159E2  H'2159FC  H'215A16  H'215A30  H'215A6C
 * H'215A86  H'215AA0  H'215ADC  H'215AF6  H'215B10  H'215B2A  H'215B44  H'215B5E
 * H'215B78  H'215B92  H'215BAC  H'215BC6  H'215BE0  H'215BFA  H'215C14  H'215C2E
 * H'215C48  H'215C62  H'215C7C  H'215C96  H'215CB0  H'215CCA  H'215CE4  H'215CFE
 * H'215D18  H'215D32  H'215D4C  H'215D88  H'215DA6  H'215E1E  H'215F1C  H'215FF4
 * H'21606C  H'2160B8  H'2160D2  H'2160EC  H'216106  H'216120  H'21613A  H'216154
 * H'21616E  H'216188  H'2161A2  H'2161BA  H'21623E  H'216258  H'216272  H'21628C
 * H'2162A6  H'2162C0  H'2162DA  H'2162F4  H'21630E  H'216328  H'216342  H'21635C
 * H'216376  H'216390  H'2163C2  H'216446  H'216460  H'21647A  H'216494  H'2164AE
 * H'2164C8  H'2164E2  H'2164FC  H'216516  H'216530  H'21654A  H'216564  H'21657C
 * H'216654  H'21666C  H'216836  H'216850  H'21686A  H'216884  H'21689E  H'2168B8
 * H'2168D2  H'2168EC  H'216906  H'216920  H'21693A  H'216954  H'21696E  H'216988
 * H'2169A2  H'2169BC  H'2169D6  H'2169F0  H'216A0A  H'216A24  H'216A3E  H'216A58
 * H'216A72  H'216A8C  H'216AA6  H'216AC0  H'216ADA  H'216AF4  H'216B0E  H'216B28
 * H'216B42  H'216B5C  H'216B76  H'216B90  H'216BAA  H'216BC4  H'216BDE  H'216BF8
 * H'216C12  H'216C2C  H'216C46  H'216C60  H'216C9C  H'216CFA  H'216D12  H'216D2A
 * H'216D42
 */

/* ---- what a press does ------------------------------------------------
 * H'21548A. Every press that reaches it ends up choosing one help record,
 * and that is all it does: H'115D12 is left pointing at the record and the
 * message screen draws it. Nothing here changes a setting or moves a motor.
 *
 * Getting to the record takes up to four dispatches. First on the screen
 * being left (H'11A16D), then on the value the box carried, and for three
 * of the screens on a table of their own. The other half of the routine --
 * the one a press on the second box of a pair takes -- goes by the *kind* of
 * pattern instead, searching a table of 54 kinds and falling back on the
 * pattern's category.
 *
 * The records are longwords in the table at slot 0, H'11B29E, and what is
 * reconstructed below is those offsets. They are laid out here exactly as
 * the ROM's jump tables lay them out, because that is what they are: the
 * original spends four instructions and a table entry on each where this
 * spends one number.
 */
#define HELP_NONE       0xFFFF    /* no record: the screen is left as it is */
#define HELP_SCREEN35   0xFFF1    /* H'94 on screen H'35, H'98 otherwise */
#define HELP_MODULE_1E0 0xFFF2    /* H'1E0 with the module, H'C8 without */
#define HELP_MODULE_0D4 0xFFF3    /* H'D4 with the module, H'D0 without */
#define HELP_MODULE_1CC 0xFFF4    /* H'1CC with the module, H'1C8 without */
#define HELP_MODULE_1D4 0xFFF5    /* H'1D4 without one, H'7C with */

/* The value on the box, 1 to H'82, on the twenty screens that share
 * one table. H'2156CE. */
static const u16 help_by_value[] = {
    0x00A8, 0x00B4, 0x00A4, 0x00B0, 0x00BC, 0x00C0, 0x00C4, 0xFFF2,
    0x00A0, 0x00AC, 0x00CC, 0x009C, 0x00E8, 0x00DC, 0x00E0, 0x00E4,
    0x0108, 0x00F4, 0x00F8, 0x00FC, 0x00F0, 0xFFFF, 0x0124, 0x0128,
    0x00EC, 0x0110, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x011C,
    0x0120, 0xFFFF, 0xFFFF, 0xFFF3, 0x0104, 0x00D8, 0x0100, 0x010C,
    0x01C0, 0x01B4, 0x01B8, 0x01C4, 0xFFF4, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFF1, 0xFFF1, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0118, 0x01B0,
    0xFFFF, 0x00B8,
};

/* H'21608C. */
static const u16 help_menu_35[] = {
    0x0168, 0x012C, 0x012C, 0x0170, 0x016C, 0x0164, 0x01D8, 0x0174,
    0x01EC, 0xFFFF, 0x01EC,
};

/* H'2161DA. */
static const u16 help_menu_21[] = {
    0x0168, 0x017C, 0x0178, 0x0148, 0x013C, 0x013C, 0x013C, 0x0174,
    0x0130, 0x0134, 0x0138, 0x0140, 0x01DC, 0x0144, 0x0144, 0x0144,
    0x0144, 0x0144, 0x0144, 0x0144, 0x0144, 0x0144, 0x014C, 0x014C,
    0x0150,
};

/* H'2163E2. */
static const u16 help_menu_22[] = {
    0x0168, 0x0154, 0x0154, 0x0154, 0xFFFF, 0xFFFF, 0xFFFF, 0x0164,
    0x0158, 0x0158, 0x0158, 0x0140, 0x015C, 0x015C, 0x015C, 0x0160,
    0x0160, 0x0160, 0x01F0, 0x01F0, 0x01F0, 0xFFFF, 0xFFFF, 0xFFFF,
    0x0150,
};

/* The kinds of pattern, and a record for each. The keys are at
 * H'2166F2 and the handlers at H'21675A, counting down as the search
 * counts up -- the same reversed-table idiom the service dispatcher
 * uses. */
static const u16 help_kind_key[] = {
    0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008,
    0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F, 0x0010,
    0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018,
    0x0019, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D,
    0x002E, 0x002F, 0x0030, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x0146,
    0x0148, 0x015A, 0x015B, 0x015C, 0x015D, 0x015E,
};
static const u16 help_kind_value[] = {
    0x0004, 0x0008, 0x000C, 0x0010, 0x0014, 0x0018, 0x0018, 0x001C,
    0x0020, 0x0024, 0x0028, 0x002C, 0x0030, 0x0034, 0x0038, 0x003C,
    0x0040, 0x0028, 0x0030, 0x0024, 0x0044, 0x0048, 0x0048, 0x004C,
    0x004C, 0x0184, 0x0188, 0x0190, 0x0194, 0x0198, 0x019C, 0x01A0,
    0x01A4, 0x01A8, 0x01AC, 0x0050, 0x0054, 0x0058, 0x005C, 0x0060,
    0x0064, 0x0068, 0x006C, 0x0070, 0x0074, 0x0078, 0x0078, 0x018C,
    0xFFF5, 0xFFF5, 0xFFF5, 0xFFF5, 0xFFF5, 0xFFF5,
};

/* And if the kind is not in that table, the pattern's category,
 * H'05 to H'0F. H'216CCE. */
static const u16 help_by_category[] = {
    0x0088, 0x0090, 0x0088, 0x0088, 0x0090, 0x0088, 0x0088, 0x0090,
    0x0088, 0x0090, 0x008C,
};

/* One record chosen. The five values above H'FF00 are the ones the original
 * spells out with a test rather than a table entry. */
static void help_pick(u16 code)
{
    u16 offset = code;

    switch (code) {
    case HELP_NONE:
        return;
    case HELP_SCREEN35:
        offset = (u16)((REG8(0x11A16DUL) == 0x35) ? 0x0094 : 0x0098);
        break;
    case HELP_MODULE_1E0:
        offset = (u16)((REG8(0x57FF80UL) == 0xAA) ? 0x01E0 : 0x00C8);
        break;
    case HELP_MODULE_0D4:
        offset = (u16)((REG8(0x57FF80UL) == 0xAA) ? 0x00D4 : 0x00D0);
        break;
    case HELP_MODULE_1CC:
        offset = (u16)((REG8(0x57FF80UL) == 0xAA) ? 0x01CC : 0x01C8);
        break;
    case HELP_MODULE_1D4:
        offset = (u16)((REG8(0x57FF80UL) == 0xB4) ? 0x01D4 : 0x007C);
        break;
    default:
        break;
    }
    REG32(0x115D12UL) = REG32(TABLE_SLOT(0) + (u32)offset);
}

/* No record: the screen the press was on is put back, and on most of the
 * paths the picker cursor is set going again. */
static void help_give_up(int cursor)
{
    screen_switch(REG8(0x11A16DUL), 0x04, 0x00);
    if (cursor) picker_cursor(0x03);
}

/* The eleven screens that do not share the big table. */
static void help_from(const u16 *table, u16 n, u16 v, int cursor)
{
    if ((u16)(v - 1) > (u16)(n - 1)) {
        help_give_up(cursor);
        return;
    }
    if (table[v - 1] == HELP_NONE) {
        help_give_up(cursor);
        return;
    }
    help_pick(table[v - 1]);
}

/* H'21562E. Screens H'25 and H'26: values 1 to 4 share one record, and
 * which one depends on whether the module is fitted. */
static void help_screen_25(u16 v)
{
    if ((short)v >= 0x0001 && (short)v <= 0x0004) {
        help_pick((u16)((REG8(0x57FF80UL) == 0xAA) ? 0x01D0 : 0x0084));
        return;
    }
    if (v == 0x0005) {
        help_pick(0x0080);
        return;
    }
    help_give_up(0);
}

/* H'215DA6, H'215E1E, H'215F1C, H'215FF4, H'21657C. Five screens with a
 * handful of values each, written out as tests rather than as a table. */
static void help_screen_13(u16 v)
{
    if (v == 0x0017)      help_pick(0x0124);
    else if (v == 0x0018) help_pick(0x0128);
    else if (v == 0x001A) help_pick(0x0110);
    else                  help_give_up(0);
}

static void help_screen_41(u16 v)
{
    if (v == 0x0017)      help_pick(0x0124);
    else if (v == 0x0018) help_pick(0x0128);
    else if (v == 0x0040) help_pick(0x011C);
    else if (v == 0x0041) help_pick(0x0120);
    else if (v == 0x000E) help_pick(0x00DC);
    else if (v == 0x0019) help_pick(0x00EC);
    else if (v == 0x001A) help_pick(0x0110);
    else                  help_give_up(1);
}

static void help_screen_38(u16 v)
{
    if (v == 0x0010)      help_pick(0x0124);
    else if (v == 0x0011) help_pick(0x0128);
    else if (v == 0x0012) help_pick(0x011C);
    else if (v == 0x0013) help_pick(0x0120);
    else if (v == 0x0014) help_pick(0x00DC);
    else if (v == 0x0015) help_pick(0x0150);
    else                  help_give_up(0);
}

static void help_screen_24(u16 v)
{
    if (v == 0x0001)      help_pick(0x0170);
    else if (v == 0x0002) help_pick(0x0170);
    else if (v == 0x0019) help_pick(0x0150);
    else                  help_give_up(0);
}

static void help_screen_37(u16 v)
{
    if (v == 0x0001)      help_pick(0x0164);
    else if (v == 0x0002) help_pick(0x0180);
    else if (v == 0x0003) help_pick(0x01E4);
    else if (v == 0x0004) help_pick(0x01E8);
    else if (v == 0x0014) help_pick(0x00DC);
    else if (v == 0x0015) help_pick(0x0150);
    else                  help_give_up(0);
}

/* ---- the other half: a press on the second box of a pair ---------------
 * H'21666C. Here the record comes from what kind of pattern the value names
 * rather than from the value itself. Field H'14 of the descriptor is the
 * kind and field H'17 the category, and the category is the fallback.
 */
static void help_by_pattern(u16 v)
{
    const u32 rec = ITEM_TABLE +
        (u32)(long)(short)(u16)(ITEM_STRIDE * v);
    const u16 kind = REG16(rec + 0x14);
    u16 k;

    for (k = 0; k < 54; k++) {
        if (help_kind_key[k] == kind) {
            help_pick(help_kind_value[k]);
            return;
        }
    }

    {
        const u8 cat = REG8(rec + 0x17);

        if ((u8)(cat - 5) > 0x0A) {
            help_give_up(1);
            return;
        }
        help_pick(help_by_category[cat - 5]);
    }
}

static void help_no_flag(u16 v)
{
    const u8 screen = REG8(0x11A16DUL);

    if (screen == 0x13 || screen == 0x14 || screen == 0x38) {
        screen_switch(screen, 0x04, 0x00);
        return;
    }
    if (screen == 0x41) {
        screen_switch(screen, 0x04, 0x00);
        REG8(0x11B0A9UL) = 0x01;
    }
    help_by_pattern(v);
}

/* H'21548A. */
void screen_action(u16 value, u16 index, u8 second)
{
    const u8 from = REG8(0x11A169UL);

    REG8(0x11A16FUL) = 0x00;

    /* The two screens that are themselves the help are not remembered, and
     * a press on one of them does nothing at all. */
    if (from != 0x08 && from != 0x3E) screen_remember(0x04);

    if (hitbox_kind(index) == 0) message_show_held(index);

    if (REG8(0x11A169UL) == 0x08 || REG8(0x11A169UL) == 0x3E) return;

    screen_switch(0x08, 0x04, 0x00);

    if (second == 0) {
        help_no_flag(value);
        return;
    }

    switch (REG8(0x11A16DUL)) {
    case 0x25: case 0x26:
        help_screen_25(value);
        break;
    case 0x13: case 0x14:
        help_screen_13(value);
        break;
    case 0x41:
        help_screen_41(value);
        break;
    case 0x38:
        help_screen_38(value);
        break;
    case 0x24:
        help_screen_24(value);
        break;
    case 0x23:
        help_from(help_menu_35, 11, value, 0);
        break;
    case 0x15:
        help_from(help_menu_21, 0x19, value, 0);
        break;
    case 0x16:
        help_from(help_menu_22, 0x19, value, 0);
        break;
    case 0x37:
        help_screen_37(value);
        break;
    case 0x02: case 0x03: case 0x04: case 0x07: case 0x09:
    case 0x0A: case 0x0C: case 0x0D: case 0x2C: case 0x2D:
    case 0x30: case 0x33: case 0x34: case 0x35: case 0x36:
    case 0x3F: case 0x42: case 0x45: case 0x46: case 0x47:
        if ((u16)(value - 1) > 0x0081) {
            help_give_up(1);
            break;
        }
        if (help_by_value[value - 1] == HELP_NONE) {
            help_give_up(1);
            break;
        }
        help_pick(help_by_value[value - 1]);
        break;
    default:
        /* Everything else, and anything outside H'02 to H'47. */
        screen_switch(REG8(0x11A16DUL), 0x04, 0x00);
        break;
    }
}

/* ---- SCI0, the embroidery module's link -------------------------------
 * Three interrupts -- receive error, receive full, transmit empty -- and
 * about 3,900 instructions of protocol behind them. The link is 57600 baud
 * on channel 0, half duplex, and everything about it lives in H'11F29E
 * upwards:
 *
 *   H'11F29E  the link state: 0 idle, 1 sending, 2 sending the checksum,
 *             3 and 4 turning the line round, 5 a retry, 6 a timeout,
 *             7 abandon, 8 the answer arrived
 *   H'11F29F  how many times round the loop this state has been
 *   H'11F2A1  which message is being sent, and the switch key for it
 *   H'11F2A2  its sub-code, which for message 5 is a second switch key
 *   H'11F2A3  the byte about to go out
 *   H'11F2A4  the running sum of everything sent, which is the checksum
 *   H'11F2A6  which byte of the message this is
 *   H'11F2A8  how many bytes the message has
 *   H'11F2B5  the retry count
 *   H'11F2B6  the receive state
 *   H'11F2B9  which message came back, and the switch key for it
 *   H'11F2BA  its sub-code, and a second switch key
 *   H'11F2BB  the byte that just arrived
 *
 * A frame is one byte of "message and sub-code" packed as (message << 4) |
 * sub-code, then the payload, then the sum. Every message is laid out the
 * same way, and what changes between them is only where the payload is read
 * from and how long it is.
 */

/* H'2350AE. Start sending. The message is already in H'11F2A1; this clears
 * the running state, works out which of the panel's four "busy" bits to
 * raise for it, and turns the transmitter and its interrupt on. */
void link_send_start(void)
{
    REG8(0x11F2A4UL) = 0x00;
    REG16(0x11F2A6UL) = 0x0000;
    REG8(0x11F2A0UL) = 0x00;

    if (REG8(0x11F29EUL) == 0) {
        REG8(0x11F29EUL) = 0x01;
        REG16(0x11F2A8UL) = 0x0000;
        REG8(0x11F29FUL) = 0x00;   /* the register was just zeroed for A8 */

        switch (REG8(0x11F2A1UL)) {
        case 0x03:
            if (REG8(0x11A616UL) == 0) REG8(0x114D50UL) |= 0x01;
            break;
        case 0x04:
        case 0x01:
            REG8(0x114D50UL) |= 0x02;
            break;
        case 0x0D:
            if (REG8(0x11F2A2UL) != 0) REG8(0x114D50UL) |= 0x20;
            break;
        default:
            break;
        }
    }

    SCR0 |= 0x20;   /* transmit enable */
    SCR0 |= 0x80;   /* transmit interrupt enable */
}

/* H'23514A. Stop sending, and put the line back the way the receiver wants
 * it: bits 0 and 1 of P9DR are the direction control. */
void link_send_stop(void)
{
    SCR0 &= (u8)~0x80;
    P9DR = (u8)(P9DR | 0x03);
}

/* H'2351C4. The twelve bytes at H'11A612 cleared, one at a time. */
void link_clear_11A612(void)
{
    u8 i;

    for (i = 0; i < 0x0C; i++) REG8(0x0011A612UL + i) = 0x00;
}

/* H'2351F4. A wait of [units], each about H'1A9 turns of an empty loop, with
 * the host link serviced between them so a download is not starved. */
void link_delay(u16 units)
{
    volatile u16 outer = units;

    while (outer != 0) {
        volatile u16 inner = 0x01A9;

        outer--;
        while (inner != 0) inner--;
        rom_host_service();
    }
}

/* H'23521E, H'235230, H'235242, H'235254. Four byte-gap delays, counted in
 * a register and nothing else. The first two are the same hundred turns
 * written out twice. */
static void link_gap(u16 turns)
{
    volatile u16 n = turns;

    while (n != 0) n--;
}

void link_gap_100(void)  { link_gap(0x0064); }
void link_gap_100b(void) { link_gap(0x0064); }
void link_gap_10(void)   { link_gap(0x000A); }
void link_gap_350(void)  { link_gap(0x015E); }

/* H'235282 and H'235296. Two pairs of bytes in the module's shared area. */
void link_clear_0FFC18(void)
{
    REG8(0x000FFC19UL) = 0x00;
    REG8(0x000FFC18UL) = 0x00;
}

void link_clear_0FFC16(void)
{
    REG8(0x000FFC17UL) = 0x00;
    REG8(0x000FFC16UL) = 0x00;
}

/* ---- the receive-error interrupt --------------------------------------
 * H'233BE4. Framing, parity and overrun all come here.
 *
 * While a message is on its way in, an error costs one byte: the count goes
 * up and the length comes down. In receive states 2 and 4 -- the module
 * answering -- the whole exchange is abandoned and restarted at state 8.
 *
 * The interesting part is the third block. When the link is in state 4 and
 * the receiver is idle, a framing error on an otherwise silent line is how
 * the machine works out whether a module is plugged in at all: it counts the
 * one bits in whatever RDR holds, and more than four of them means the line
 * is idling high with nothing on it.
 */
void isr_sci0_eri_body(void)
{
    u8 ones, i, v;

    if (REG8(0x11F2B6UL) == 0x01) {
        REG16(0x11F2BEUL) = (u16)(REG16(0x11F2BEUL) + 1);
        REG8(0x11F2BCUL) = (u8)(REG8(0x11F2BCUL) - 1);
        REG16(0xFFFF72UL) = 0x0000;
    }

    if (REG8(0x11F2B6UL) == 0x02 || REG8(0x11F2B6UL) == 0x04) {
        REG16(0x11F2BEUL) = 0x0000;
        REG8(0x11F2B6UL) = 0x01;
        REG8(0x11F2BCUL) = 0x00;
        REG16(0xFFFF72UL) = 0x0000;
        REG8(0x11F29EUL) = 0x08;
        REG8(0x11F2B3UL) = 0xFF;
        SSR0 &= (u8)~0x40;
        link_send_start();
    }

    if (REG8(0x11F29EUL) == 0x04 && REG8(0x11F2B6UL) == 0x00) {
        REG8(0xFFFF60UL) &= (u8)~0x02;
        REG16(0xFFFF72UL) = 0x0000;
        SSR0 &= (u8)~0x40;

        REG8(0x11F2BBUL) = RDR0;
        ones = 0;
        for (i = 0; i < 0x08; i++) {
            v = REG8(0x11F2BBUL);
            if (v & 0x01) ones++;
            REG8(0x11F2BBUL) = (u8)(v >> 1);
        }

        REG8(0x11F29EUL) = (u8)((ones > 0x04) ? 0x05 : 0x06);
        link_send_start();
    }

    if (SSR0 & 0x10) REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x04);
    if (SSR0 & 0x20) REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x01);

    SSR0 &= (u8)~0x20;
    SSR0 &= (u8)~0x10;
    SSR0 &= (u8)~0x08;
}


/* ---- the transmit interrupt -------------------------------------------
 * H'2324DA. One byte goes out per interrupt, and which byte it is comes out
 * of the message number in H'11F2A1 and the index in H'11F2A6.
 *
 * Every message starts with the same byte -- the number in the high nibble
 * and the sub-code in the low -- and ends by setting the state to 2, which
 * is how the head of the handler knows to send the checksum next. Between
 * them each message has its own payload, and that is all that differs.
 *
 * The long ones are block transfers: H'11F29F counts which 512-byte block
 * this is, the length that goes out is what is left of the whole thing, and
 * the payload is read straight out of the buffer the pattern lives in.
 */

/* The first byte of a frame. */
static void tx_head_byte(void)
{
    REG8(0x11F2A3UL) = (u8)((u8)(REG8(0x11F2A1UL) << 4) | REG8(0x11F2A2UL));
}

/* Where in the transfer this block starts: the block number doubled as a
 * byte and then put in the high half, so H'200 per block -- and a block
 * number of H'80 or more wraps rather than overflowing. */
static u16 tx_block_offset(void)
{
    return (u16)((u16)(u8)(REG8(0x11F29FUL) << 1) << 8);
}

/* Whatever is left of the transfer, capped at one block. */
static void tx_length_left(u16 total)
{
    u16 left = (u16)(total - tx_block_offset());

    REG16(0x11F2A8UL) = left;
    if (left > 0x0200) {
        left = 0x0200;
        REG16(0x11F2A8UL) = left;
    }
    REG8(0x11F2A3UL) = REG8(0x11F2A8UL);   /* the high byte */
}

/* H'233BAE, and its copy at H'2339F0. The byte out, into the checksum, TDRE
 * cleared, and on to the next index. */
static void tx_byte_out(void)
{
    const u8 b = REG8(0x11F2A3UL);

    TDR0 = b;
    REG8(0x11F2A4UL) = (u8)(REG8(0x11F2A4UL) + b);
    SSR0 &= (u8)~0x80;
    REG16(0x11F2A6UL) = (u16)(REG16(0x11F2A6UL) + 1);
}

/* Message 9. One byte and nothing else. */
static void tx_msg_09(void)
{
    if (REG16(0x11F2A6UL) == 0x0000) {
        tx_head_byte();
        REG8(0x11F29EUL) = 0x02;
    }
}

/* Message 11. Four bytes from H'11A636. */
static void tx_msg_0B(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0002;
        link_gap_100();
    } else if (i <= 0x0004) {
        REG8(0x11F2A3UL) = REG8(0x0011A635UL + i);
        link_gap_100();
        if (i == 0x0004) REG8(0x11F29EUL) = 0x02;
    }
}

/* Message 1. A pattern's H'12-byte record, chosen by H'11A660. */
static void tx_msg_01(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0012;
        link_gap_100();
    } else if (i == 0x0001) {
        REG8(0x11F2A3UL) = REG8(0x11A660UL);
        link_gap_100();
    } else {
        const u32 at = 0x0011A41AUL
                     + (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL))
                     + (u32)(u16)(i - 2);

        REG32(0x11F2AAUL) = at;
        REG8(0x11F2A3UL) = REG8(at);
        if ((u16)(i - 1) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 2. A pattern's H'10-byte block at H'11A25A. */
static void tx_msg_02(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0010;
        link_gap_100();
    } else if (i == 0x0001) {
        REG8(0x11F2A3UL) = REG8(0x11A660UL);
        link_gap_100();
    } else {
        const u32 at = 0x0011A25AUL
                     + (u32)(long)(short)(u16)((u16)REG8(0x11A660UL) << 4)
                     + (u32)(u16)(i - 2);

        REG32(0x11F2AAUL) = at;
        REG8(0x11F2A3UL) = REG8(at);
        if ((u16)(i - 1) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 3. Twelve bytes from H'11A612, and the panel told the link is
 * busy. The gap after each byte is longer while H'11A61A is H'10 or more. */
static void tx_msg_03(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x000C;
        link_gap_100();
        REG8(0x114D50UL) |= 0x01;
    } else if (i == 0x0001) {
        REG32(0x11F2AAUL) = 0x0011A611UL + i;
        REG8(0x11F2A3UL) = REG8(0x0011A611UL + i);
        if (REG8(0x11A61AUL) >= 0x10) link_gap_350();
        else                          link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x0011A611UL + i;
        REG8(0x11F2A3UL) = REG8(0x0011A611UL + i);
        if (REG16(0x11F2A8UL) == i) {
            REG8(0x11F29EUL) = 0x02;
            link_gap_100();
        } else {
            link_gap_10();
        }
    }
}

/* Messages 4 and 6. One byte each. */
static void tx_msg_04(void)
{
    if (REG16(0x11F2A6UL) == 0x0000) {
        tx_head_byte();
        REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 7. Eight bytes of machine state from H'114D4C, or twelve when bit
 * 5 of H'114D51 is up. */
static void tx_msg_07(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0008;
        if (REG8(0x114D51UL) & 0x20) {
            REG16(0x11F2A8UL) = (u16)(REG16(0x11F2A8UL) + 0x0004);
        }
        link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x00114D4BUL + i;
        REG8(0x11F2A3UL) = REG8(0x00114D4BUL + i);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 8. Three bytes from H'11A661, and its sub-code comes from the low
 * nibble of H'114D5C rather than from H'11F2A2. */
static void tx_msg_08(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        const u8 sub = (u8)(REG8(0x114D5CUL) & 0x0F);

        REG8(0x11F2A2UL) = sub;
        REG8(0x11F2A3UL) = (u8)((u8)(REG8(0x11F2A1UL) << 4) | sub);
        link_gap_100();
    } else if (i <= 0x0003) {
        REG8(0x11F2A3UL) = REG8(0x0011A660UL + i);
        if (i == 0x0003) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 12. H'15 bytes from H'104043, with the first two named. */
static void tx_msg_0C(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0015;
        link_gap_100();
    } else if (i == 0x0001) {
        REG8(0x11F2A3UL) = REG8(0x00104044UL);
        link_gap_100();
    } else if (i == 0x0002) {
        REG8(0x11F2A3UL) = REG8(0x00104045UL);
        link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x00104043UL + i;
        REG8(0x11F2A3UL) = REG8(0x00104043UL + i);
        if ((u16)(i - 2) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 13. H'14 bytes from H'104C79. */
static void tx_msg_0D(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0014;
        link_gap_100();
    } else {
        REG32(0x11F2AAUL) = 0x00104C79UL + i;
        REG8(0x11F2A3UL) = REG8(0x00104C79UL + i);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
        link_gap_100();
    }
}

/* Message 14. One byte, and then the whole exchange is dropped: the second
 * time round it clears everything and stops the transmitter without sending
 * anything at all. Returns 0 when nothing is to go out. */
static int tx_msg_0E(void)
{
    if (REG16(0x11F2A6UL) == 0x0000) {
        tx_head_byte();
        link_gap_100();
        return 1;
    }

    REG16(0x11F2A6UL) = 0x0000;
    REG8(0x11F29FUL) = 0x00;
    REG8(0x11F2A2UL) = 0x00;
    link_gap_100();
    link_send_stop();
    REG8(0x11F29EUL) = 0x00;
    return 0;
}

/* ---- message 5, the block transfers -----------------------------------
 * H'232CD6. Fourteen of them, chosen by the sub-code in H'11F2A2. They all
 * have the same three-byte preamble -- the header, then the length of this
 * block high byte and low -- and then up to H'200 bytes of payload.
 */

/* The common tail of a block: one byte out of [base], and the state moved on
 * when the last of the block has gone. [bias] is where the payload starts in
 * the index, which is 3 for most of them and 4 for the one with an extra
 * byte in front. */
static void tx_block_byte(u32 base, u16 bias)
{
    const u16 i  = REG16(0x11F2A6UL);
    const u32 at = base + (u32)(u16)(tx_block_offset() + i - bias);

    REG32(0x11F2AAUL) = at;
    REG8(0x11F2A3UL) = REG8(at);
    if ((u16)(i - (u16)(bias - 1)) == REG16(0x11F2A8UL)) {
        REG8(0x11F29EUL) = 0x02;
    }
}

/* The low byte of the length, and the end of a transfer that has none. */
static void tx_length_low(void)
{
    REG8(0x11F2A3UL) = REG8(0x11F2A9UL);
    if (REG16(0x11F2A8UL) == 0) REG8(0x11F29EUL) = 0x02;
}

/* Sub-codes 1 and 2: a fixed-length buffer. */
static void tx_blk_fixed(u16 total, u32 base)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
    } else if (i == 0x0001) {
        tx_length_left(total);
    } else if (i == 0x0002) {
        tx_length_low();
    } else {
        tx_block_byte(base, 3);
    }
    link_gap_100();
}

/* Sub-code 3. The length is a stitch count out of H'0FFE80, capped at H'1B,
 * multiplied by the two bytes at H'100282 and H'100283 and with H'AE of
 * header added -- and if that comes to more than H'3DB6 it is clamped and
 * the panel is told the pattern was too big. */
static void tx_blk_sub3(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
    } else if (i == 0x0001) {
        const u8 b = REG8(0x000FFE80UL);
        u16 n = (u16)((b > 0x1B) ? 0x001B : (u16)b);
        u32 prod;
        u16 total;

        REG16(0x11F2A8UL) = n;
        prod = (u32)(u16)((u16)REG8(0x00100282UL) *
                          (u16)REG8(0x00100283UL)) * (u32)n;
        total = (u16)((u16)prod + 0x00AE);
        REG16(0x11F2A8UL) = total;
        if (total > 0x3DB6) {
            total = 0x3DB6;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
        link_gap_100();
    } else if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
    } else {
        tx_block_byte(0x00100280UL, 3);
        link_gap_100b();
    }
}

/* Sub-code 4. Nine named bytes and then the payload, which is addressed as a
 * longword: the block number times H'200 plus a base out of H'11F2CC and a
 * pointer out of H'11F2AE, so this one can reach past 64K. Running past the
 * end of the buffer -- more than H'10000 from the pointer -- raises the same
 * "too big" bit. */
static void tx_blk_sub4(void)
{
    const u16 i = REG16(0x11F2A6UL);

    switch (i) {
    case 0x0000:
        tx_head_byte();
        link_gap_100();
        return;
    case 0x0001: {
        u16 left = (u16)(REG16(0x11F2CEUL) - tx_block_offset());

        REG16(0x11F2A8UL) = left;
        if (left > 0x0200) {
            left = 0x0200;
            REG16(0x11F2A8UL) = left;
        }
        REG8(0x11F2A3UL) = (u8)(left >> 8);
        link_gap_100();
        return;
    }
    case 0x0002: REG8(0x11F2A3UL) = REG8(0x11F2A9UL); link_gap_100(); return;
    case 0x0003: REG8(0x11F2A3UL) = REG8(0x11F2B4UL); link_gap_100(); return;
    case 0x0004: REG8(0x11F2A3UL) = REG8(0x11F2CEUL); link_gap_100(); return;
    case 0x0005: REG8(0x11F2A3UL) = REG8(0x11F2CFUL); link_gap_100(); return;
    case 0x0006: REG8(0x11F2A3UL) = REG8(0x001040BEUL); link_gap_100(); return;
    case 0x0007: REG8(0x11F2A3UL) = REG8(0x001040BFUL); link_gap_100(); return;
    case 0x0008: REG8(0x11F2A3UL) = REG8(0x001040C0UL); link_gap_100(); return;
    default: break;
    }

    if (REG16(0x11F2A8UL) == 0) {
        REG8(0x11F29EUL) = 0x02;
        link_gap_100();
        return;
    }

    {
        u32 off = (u32)REG8(0x11F29FUL);
        u32 at, end;
        int n;

        for (n = 0; n < 9; n++) off <<= 1;      /* the block, times H'200 */
        off += (u32)(u16)i;
        off -= 9;

        at  = (u32)REG16(0x11F2CCUL) + off;
        REG32(0x11F2D0UL) = off;
        end = REG32(0x11F2AEUL);
        at += end;
        end += 0x00010000UL;
        REG32(0x11F2AAUL) = at;
        if (end < at) REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);

        REG8(0x11F2A3UL) = REG8(at);
        if ((u16)(i - 8) == REG16(0x11F2A8UL)) REG8(0x11F29EUL) = 0x02;
        link_gap_100b();
    }
}

/* Sub-code 5. Four named bytes, and a length that is the two bytes at
 * H'1040BC multiplied together plus five. */
static void tx_blk_sub5(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
    } else if (i == 0x0001) {
        u16 total = (u16)((u16)((u16)REG8(0x001040BCUL) *
                                (u16)REG8(0x001040BDUL)) + 0x0005);

        REG16(0x11F2A8UL) = total;
        if (total > 0x0BBD) {
            total = 0x0BBD;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
        link_gap_100();
    } else if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
    } else if (i == 0x0003) {
        REG8(0x11F2A3UL) = REG8(0x001040C0UL);
        link_gap_100();
    } else {
        tx_block_byte(0x001040BCUL, 4);
        link_gap_100b();
    }
}

/* Sub-code 6. The same eight-or-twelve bytes of machine state as message 7. */
static void tx_blk_sub6(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0008;
        if (REG8(0x114D51UL) & 0x20) {
            REG16(0x11F2A8UL) = (u16)(REG16(0x11F2A8UL) + 0x0004);
        }
    } else {
        REG32(0x11F2AAUL) = 0x00114D4BUL + i;
        REG8(0x11F2A3UL) = REG8(0x00114D4BUL + i);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
    }
    link_gap_100();
}

/* Sub-code 7. The length is not known in advance: the buffer at H'104D48 is
 * scanned two bytes at a time for the pair H'80 H'81, which is the end of
 * the pattern, and the length is however far that was -- or a whole block if
 * it is not in this one. H'11F2B2 remembers that the marker landed exactly
 * on a block boundary, so the next block does not scan again.
 *
 * H'11F53A is how much of the buffer is real; a block that starts past that
 * sends nothing and raises the "too big" bit. */
static void tx_blk_sub7(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
        return;
    }

    if (i == 0x0001) {
        u32 at, end;

        REG16(0x11F2A8UL) = 0x0000;

        if (REG8(0x11F2B2UL) == 0x01) {
            REG8(0x11F2B2UL) = 0x00;
        } else {
            u16 n = 0;

            while (n < 0x0200) {
                u32 a = 0x00104D48UL + (u32)(u16)(tx_block_offset() + n);
                u8  b;

                REG32(0x11F2AAUL) = a;
                b = REG8(a);
                REG8(0x11F2A3UL) = b;

                if (b == 0x80) {
                    a = 0x00104D48UL +
                        (u32)(u16)(tx_block_offset() + n + 1);
                    REG32(0x11F2AAUL) = a;
                    b = REG8(a);
                    REG8(0x11F2A3UL) = b;
                    if (b == 0x81) {
                        REG16(0x11F2A8UL) =
                            (u16)(REG16(0x11F2A8UL) + 2);
                        if (REG16(0x11F2A8UL) == 0x0200) {
                            REG8(0x11F2B2UL) = 0x01;
                        }
                        break;
                    }
                }
                REG16(0x11F2A8UL) = (u16)(REG16(0x11F2A8UL) + 2);
                n = (u16)(n + 2);
            }
        }

        at  = 0x00104D48UL + (u32)(long)(short)tx_block_offset();
        end = REG32(0x11F53AUL) + 0x00104D48UL;
        REG32(0x11F2AAUL) = at;
        if (end < at) {
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
            REG16(0x11F2A8UL) = 0x0000;
        }
        REG8(0x11F2A3UL) = REG8(0x11F2A8UL);
        return;
    }

    if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
        return;
    }

    tx_block_byte(0x00104D48UL, 3);
    link_gap_100();
}

/* Sub-code 8. A length that lives in the two bytes at H'104D46 and is capped
 * at one. */
static void tx_blk_sub8(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
    } else if (i == 0x0001) {
        u16 total = REG16(0x00104D46UL);

        REG16(0x11F2A8UL) = total;
        if (total > 0x0001) {
            total = 0x0001;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
    } else if (i == 0x0002) {
        tx_length_low();
    } else {
        tx_block_byte(0x00104D49UL, 3);
    }
    link_gap_100();
}

/* Sub-codes 9, 12, 13 and 14: four short fixed blocks. */
static void tx_blk_short(u16 total, u32 base, u16 bias)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = total;
    } else {
        const u32 at = base + (u32)(u16)(i - bias);

        REG32(0x11F2AAUL) = at;
        REG8(0x11F2A3UL) = REG8(at);
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
    }
    link_gap_100();
}

/* Sub-code 13, which is the one that does not keep the pointer. */
static void tx_blk_sub13(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        REG16(0x11F2A8UL) = 0x0007;
    } else {
        REG8(0x11F2A3UL) = REG8(0x00104C90UL + (u32)(u16)(i - 1));
        if (REG16(0x11F2A8UL) == i) REG8(0x11F29EUL) = 0x02;
    }
    link_gap_100();
}

/* Sub-code 11. The length is H'AF for each of a count that is looked up
 * twice: the pattern's record gives an index, and H'0FFE9C gives the count.
 * The clamp here is on the H'20-bit product, not on the word. */
static void tx_blk_sub11(void)
{
    const u16 i = REG16(0x11F2A6UL);

    if (i == 0x0000) {
        tx_head_byte();
        link_gap_100();
    } else if (i == 0x0001) {
        const u32 rec = 0x0011A41AUL +
            (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL));
        const u8  n   = REG8(0x000FFE9CUL + (u32)REG8(rec));
        const u32 prod = 0x00AFUL * (u32)(u16)(n + 1);
        u16 total = (u16)prod;

        REG16(0x11F2A8UL) = total;
        if (prod > 0x00011940UL) {
            total = 0x1940;
            REG16(0x11F2A8UL) = total;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
        tx_length_left(total);
        link_gap_100();
    } else if (i == 0x0002) {
        tx_length_low();
        link_gap_100();
    } else {
        tx_block_byte(0x0004D2D0UL, 3);
        link_gap_100b();
    }
}

static void tx_msg_05(void)
{
    switch (REG8(0x11F2A2UL)) {
    case 0x01: tx_blk_fixed(0x0400, 0x000FFE80UL); break;
    case 0x02: tx_blk_fixed(0x00AC, 0x00104C98UL); break;
    case 0x03: tx_blk_sub3();  break;
    case 0x04: tx_blk_sub4();  break;
    case 0x05: tx_blk_sub5();  break;
    case 0x06: tx_blk_sub6();  break;
    case 0x07: tx_blk_sub7();  break;
    case 0x08: tx_blk_sub8();  break;
    case 0x09: tx_blk_short(0x000C, 0x0011A61EUL, 1); break;
    case 0x0B: tx_blk_sub11(); break;
    case 0x0C: tx_blk_short(0x0004, 0x001040AEUL, 1); break;
    case 0x0D: tx_blk_sub13(); break;
    case 0x0E: tx_blk_short(0x0010, 0x0011A64FUL, 0); break;
    default:   break;   /* including H'0A, which sends the header only */
    }
}

/* ---- message 15 -------------------------------------------------------
 * H'2339F4. Eight bytes about the pattern being stitched, and only when the
 * sub-code is 1. Everything past index 7 sends the same last byte over and
 * over, which is what ends it. */
static u32 tx_pattern_record(void)
{
    return 0x0011A41AUL +
           (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL));
}

static u8 tx_pattern_index(void)
{
    return REG8(tx_pattern_record());
}

static void tx_msg_0F(void)
{
    if (REG8(0x11F2A2UL) != 0x01) return;

    switch (REG16(0x11F2A6UL)) {
    case 0x0000: tx_head_byte(); break;
    case 0x0001: REG8(0x11F2A3UL) = REG8(0x11A642UL); break;
    case 0x0002: REG8(0x11F2A3UL) = REG8(0x11A640UL); break;
    case 0x0003:
        REG8(0x11F2A3UL) = REG8(0x000FFE9CUL + (u32)tx_pattern_index());
        break;
    case 0x0004:
        REG8(0x11F2A3UL) = REG8(0x000FFEB8UL + (u32)tx_pattern_index());
        break;
    case 0x0005:
        REG8(0x11F2A3UL) = REG8(0x00104CCEUL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        break;
    case 0x0006:
        REG8(0x11F2A3UL) = REG8(0x00104CCFUL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        break;
    case 0x0007:
        REG8(0x11F2A3UL) = REG8(0x00104D06UL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        break;
    default:
        REG8(0x11F2A3UL) = REG8(0x00104D07UL +
            (u32)(long)(short)(u16)((u16)tx_pattern_index() << 1));
        REG8(0x11F29EUL) = 0x02;
        break;
    }
    link_gap_100();
}

/* ---- the transmit interrupt itself ------------------------------------- */
void isr_sci0_txi_body(void)
{
    switch (REG8(0x11F29EUL)) {
    case 0x05:
        /* A retry: start the message again, and give up after five. */
        REG8(0x11F29EUL) = 0x01;
        REG16(0x11F2A6UL) = 0x0000;
        REG8(0x11F2B5UL) = (u8)(REG8(0x11F2B5UL) + 1);
        link_delay(0x000A);
        if (REG8(0x11F2B5UL) == 0x05) {
            link_send_stop();
            REG8(0x11F2B5UL) = 0x00;
            return;
        }
        break;

    case 0x06:
        /* A timeout: another block, unless H'200 of them have gone by, in
         * which case the message is abandoned and whatever it was for is
         * put back the way it was. */
        REG8(0x11F29FUL) = (u8)(REG8(0x11F29FUL) + 1);
        REG16(0x11F2A6UL) = 0x0000;
        if (REG16(0x11F2A8UL) >= 0x0200) {
            REG8(0x11F29EUL) = 0x01;
            break;
        }
        REG8(0x11F29FUL) = 0x00;
        REG8(0x11F2A2UL) = 0x00;
        link_send_stop();
        if (REG8(0x11F2A1UL) == 0x03) {
            link_clear_11A612();
        } else if (REG8(0x11F2A1UL) == 0x06) {
            REG8(0x114D60UL) = 0xFD;
        } else if (REG8(0x11F2A1UL) == 0x07) {
            REG8(0x114D55UL) &= (u8)~0x01;
            REG8(0x114D4FUL) &= (u8)~0x01;
            REG8(0x114D4FUL) &= (u8)~0x02;
            REG8(0x114D4FUL) &= (u8)~0x40;
            REG8(0x114D4FUL) &= (u8)~0x80;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) & ~0x0002);
            REG8(0x114D52UL) = (u8)(REG8(0x114D52UL) & 0x03);
            REG8(0x114D51UL) &= (u8)~0x20;
        }
        REG8(0x11F29EUL) = 0x00;
        return;

    case 0x07:
        REG16(0x11F2A6UL) = 0x0000;
        REG8(0x11F29FUL) = 0x00;
        REG8(0x11F29EUL) = 0x00;
        link_send_stop();
        return;

    case 0x02:
        /* The checksum. */
        REG8(0x11F2A3UL) = REG8(0x11F2A4UL);
        REG8(0x11F29EUL) = 0x03;
        break;

    case 0x03:
        /* Turn the line round and wait for the answer. */
        REG8(0x11F29EUL) = 0x04;
        link_send_stop();
        REG16(0xFFFF72UL) = 0x0000;
        REG8(0xFFFF60UL) |= 0x02;
        return;

    case 0x08:
        /* Answering something that came in. */
        if (REG16(0x11F2A6UL) == 0x0000) {
            REG8(0x11F2A3UL) = REG8(0x11F2B3UL);
            link_gap_100();
        } else if (REG16(0x11F2A6UL) == 0x0001) {
            if (REG8(0x11F2B9UL) == 0x06) {
                REG8(0x114D60UL) = 0x01;
                REG8(0x114D63UL) = 0x01;
                SCR0 &= (u8)~0x40;
                rom_select_sci0();
            }
            link_send_stop();
            REG8(0x11F29EUL) = 0x00;
            return;
        }
        break;

    default:
        break;
    }

    if (REG8(0x11F29EUL) == 0x01) {
        const u8 msg = REG8(0x11F2A1UL);

        if ((u8)(msg - 1) <= 0x0E) {
            switch (msg) {
            case 0x01: tx_msg_01(); break;
            case 0x02: tx_msg_02(); break;
            case 0x03: tx_msg_03(); break;
            case 0x04: tx_msg_04(); break;
            case 0x05: tx_msg_05(); break;
            case 0x06: tx_msg_04(); break;   /* H'232A24, the same code */
            case 0x07: tx_msg_07(); break;
            case 0x08: tx_msg_08(); break;
            case 0x09: tx_msg_09(); break;
            case 0x0A: break;                /* straight to the byte out */
            case 0x0B: tx_msg_0B(); break;
            case 0x0C: tx_msg_0C(); break;
            case 0x0D: tx_msg_0D(); break;
            case 0x0E: if (!tx_msg_0E()) return; break;
            case 0x0F: tx_msg_0F(); break;
            default: break;
            }
        }
    }

    tx_byte_out();
}


/* ---- the receive interrupt --------------------------------------------
 * H'233CF8. The mirror image of the transmit side: one byte per interrupt,
 * put where the message says it goes.
 *
 * H'11F2B6 is the receive state -- 0 waiting for the first byte of a frame,
 * 1 taking its payload, 2 and 4 waiting for the checksum -- H'11F2BE is the
 * index, H'11F2C0 the length, and H'11F2BC the running sum, which is
 * compared with the byte that arrives at the end.
 */

/* Every message ends the same way: on to the next index, and the byte into
 * the sum. This is H'234EDC, and its copies at H'234C78 and inside several
 * of the handlers. */
static void rx_byte_done(void)
{
    REG16(0x11F2BEUL) = (u16)(REG16(0x11F2BEUL) + 1);
    REG8(0x11F2BCUL) = (u8)(REG8(0x11F2BCUL) + REG8(0x11F2BBUL));
}

/* One byte into a buffer, with the pointer kept where the protocol keeps
 * it, and the state moved on when the index has reached the length. */
static void rx_store(u32 at)
{
    REG32(0x11F2C2UL) = at;
    REG8(at) = REG8(0x11F2BBUL);
    if (REG16(0x11F2C0UL) == REG16(0x11F2BEUL)) REG8(0x11F2B6UL) = 0x04;
}

/* Where a block of a long transfer starts: H'11F2B7 counts them. */
static u16 rx_block_offset(void)
{
    return (u16)((u16)(u8)(REG8(0x11F2B7UL) << 1) << 8);
}

/* The end of a block: state 4 when the whole thing has arrived, state 2
 * when this was a full block and more is coming. */
static void rx_block_end(u16 reached)
{
    const u16 len = REG16(0x11F2C0UL);

    if (reached != len) return;
    REG8(0x11F2B6UL) = (u8)((len < 0x0200) ? 0x04 : 0x02);
}

/* The two length bytes of a block transfer, high then low. */
static void rx_length_high(void)
{
    REG16(0x11F2C0UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
}

static void rx_length_low(void)
{
    const u16 v = (u16)(REG16(0x11F2C0UL) | (u16)REG8(0x11F2BBUL));

    REG16(0x11F2C0UL) = v;
    if (v == 0) REG8(0x11F2B6UL) = 0x04;
}

/* A plain block transfer into [base]. */
static void rx_blk(u32 base)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        /* the header, already unpacked */
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
    } else {
        const u32 at = base + (u32)(u16)(rx_block_offset() + i - 3);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 2));
    }
}

/* Sub-code 3. The same, with the length capped at H'3DB6. */
static void rx_blk_sub3(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
        if (REG16(0x11F2C0UL) > 0x3DB6) {
            REG16(0x11F2C0UL) = 0x3DB6;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
    } else if (i != 0x0000) {
        const u32 at = 0x00100280UL + (u32)(u16)(rx_block_offset() + i - 3);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 2));
    }
}

/* Sub-code 4. Nine named bytes, then a payload addressed as a longword out
 * of H'11F2C6 -- and one that runs past H'10000 from there is clamped to the
 * last byte rather than being allowed to write outside the buffer. */
static void rx_blk_sub4_arrived(void)
{
    REG8(0x11F2B6UL) = 0x04;
    REG8(0x114D50UL) = REG8(0x001040C0UL);
    REG16(0x11F2CCUL) = (u16)(REG16(0x11F2CCUL) + REG16(0x11F2CEUL));
}

static void rx_blk_sub4(void)
{
    const u16 i = REG16(0x11F2BEUL);
    u32 off, at, end;
    int n;

    switch (i) {
    case 0x0000: return;
    case 0x0001: rx_length_high(); return;
    case 0x0002:
        REG16(0x11F2C0UL) =
            (u16)(REG16(0x11F2C0UL) | (u16)REG8(0x11F2BBUL));
        return;
    case 0x0003:
        REG8(0x11F2CBUL) = REG8(0x11F2BBUL);
        if (REG8(0x11F2BBUL) == 0) REG16(0x11F2CCUL) = 0x0000;
        return;
    case 0x0004: REG16(0x11F2CEUL) = (u16)((u16)REG8(0x11F2BBUL) << 8); return;
    case 0x0005:
        REG16(0x11F2CEUL) =
            (u16)(REG16(0x11F2CEUL) | (u16)REG8(0x11F2BBUL));
        return;
    case 0x0006: REG8(0x001040BEUL) = REG8(0x11F2BBUL); return;
    case 0x0007: REG8(0x001040BFUL) = REG8(0x11F2BBUL); return;
    case 0x0008: REG8(0x001040C0UL) = REG8(0x11F2BBUL); return;
    default: break;
    }

    if (REG16(0x11F2C0UL) == 0) rx_blk_sub4_arrived();

    off = (u32)REG8(0x11F2B7UL);
    for (n = 0; n < 9; n++) off <<= 1;
    off += (u32)(u16)i;
    off -= 9;

    at  = (u32)REG16(0x11F2CCUL) + off;
    REG32(0x11F2D0UL) = off;
    end = REG32(0x11F2C6UL);
    at += end;
    end += 0x00010000UL;
    REG32(0x11F2C2UL) = at;
    if (end < at) {
        REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        REG32(0x11F2C2UL) = REG32(0x11F2C6UL) + 0x0000FFFFUL;
    }

    REG8(REG32(0x11F2C2UL)) = REG8(0x11F2BBUL);

    if ((u16)(i - 8) == REG16(0x11F2C0UL)) {
        if (REG16(0x11F2C0UL) < 0x0200) rx_blk_sub4_arrived();
        else                            REG8(0x11F2B6UL) = 0x02;
    }
}

/* Sub-code 5. One extra byte in front of the payload, which is also copied
 * into the panel's busy byte. */
static void rx_blk_sub5(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        return;
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
    } else if (i == 0x0003) {
        REG8(0x001040C0UL) = REG8(0x11F2BBUL);
        REG8(0x114D50UL) = REG8(0x11F2BBUL);
    } else {
        const u32 at = 0x001040BCUL + (u32)(u16)(rx_block_offset() + i - 4);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 3));
    }
}

/* Sub-code 6. The eight-or-twelve bytes of machine state. */
static void rx_blk_sub6(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0008;
        return;
    }

    if (REG8(0x114D51UL) & 0x20) {
        REG16(0x11F2C0UL) = (u16)(REG16(0x11F2C0UL) + 0x0004);
        REG8(0x114D51UL) &= (u8)~0x20;
    }
    rx_store(0x00114D4BUL + i);
}

/* Sub-code 8. The one that remembers where the pattern ended. */
static void rx_blk_sub8(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        return;
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        rx_length_low();
    } else {
        const u32 at = 0x00104D49UL + (u32)(u16)(rx_block_offset() + i - 3);
        const u16 len = REG16(0x11F2C0UL);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 2) == len) {
            if (len < 0x0200) {
                REG8(0x11F2B6UL) = 0x04;
                REG32(0x00104D44UL) =
                    (u32)(u16)(rx_block_offset() + len);
            } else {
                REG8(0x11F2B6UL) = 0x02;
            }
        }
    }
}

/* Sub-code 11. The length is checked against H'11940 with the block number
 * folded in, so a transfer that would run past the buffer is cut to one. */
static void rx_blk_sub11(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        return;
    } else if (i == 0x0001) {
        rx_length_high();
    } else if (i == 0x0002) {
        u32 total;

        rx_length_low();
        total = (u32)(u16)(rx_block_offset() + REG16(0x11F2C0UL));
        if (total > 0x00011940UL) {
            REG16(0x11F2C0UL) = 0x0001;
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0020);
        }
    } else {
        const u32 at = REG32(0x11F2C6UL) +
                       (u32)(u16)(rx_block_offset() + i - 3);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        rx_block_end((u16)(i - 2));
    }
}

/* Message 5's fourteen sub-codes. H'2342E8. */
static void rx_msg_05(void)
{
    switch (REG8(0x11F2BAUL)) {
    case 0x01: rx_blk(0x000FFE80UL); break;
    case 0x02: rx_blk(0x00104C98UL); break;
    case 0x03: rx_blk_sub3();  break;
    case 0x04: rx_blk_sub4();  break;
    case 0x05: rx_blk_sub5();  break;
    case 0x06: rx_blk_sub6();  break;
    case 0x07: rx_blk(0x00104D48UL); break;
    case 0x08: rx_blk_sub8();  break;
    case 0x09:
        if (REG16(0x11F2BEUL) == 0x0000) REG16(0x11F2C0UL) = 0x000C;
        else rx_store(0x0011A61DUL + REG16(0x11F2BEUL));
        break;
    case 0x0B: rx_blk_sub11(); break;
    case 0x0C:
        if (REG16(0x11F2BEUL) == 0x0000) REG16(0x11F2C0UL) = 0x0004;
        else rx_store(0x001040ADUL + REG16(0x11F2BEUL));
        break;
    case 0x0D:
        if (REG16(0x11F2BEUL) == 0x0000) {
            REG16(0x11F2C0UL) = 0x0007;
        } else {
            REG8(0x00104C90UL + (u32)(u16)(REG16(0x11F2BEUL) - 1)) =
                REG8(0x11F2BBUL);
            if (REG16(0x11F2C0UL) == REG16(0x11F2BEUL)) {
                REG8(0x11F2B6UL) = 0x04;
            }
        }
        break;
    case 0x0E:
        if (REG16(0x11F2BEUL) == 0x0000) REG16(0x11F2C0UL) = 0x0010;
        else rx_store(0x0011A64FUL + REG16(0x11F2BEUL));
        break;
    default: break;   /* H'0A, which is the header alone */
    }
}

/* Message 15. H'234C7C. Eight values about the pattern, and every one of
 * them is only kept when the pattern's record says H'1C -- which is the one
 * kind of pattern the module can send back. */
static u8 rx_pattern_index(void)
{
    return REG8(0x0011A41AUL +
                (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL)));
}

static void rx_msg_0F(void)
{
    if (REG8(0x11F2BAUL) != 0x01) return;

    switch (REG16(0x11F2BEUL)) {
    case 0x0000: break;
    case 0x0001: REG8(0x11A642UL) = REG8(0x11F2BBUL); break;
    case 0x0002: REG8(0x11A640UL) = REG8(0x11F2BBUL); break;
    case 0x0003:
        if (rx_pattern_index() == 0x1C) {
            REG8(0x000FFE9CUL + (u32)rx_pattern_index()) = REG8(0x11F2BBUL);
        }
        break;
    case 0x0004:
        if (rx_pattern_index() == 0x1C) {
            REG8(0x000FFEB8UL + (u32)rx_pattern_index()) = REG8(0x11F2BBUL);
        }
        break;
    case 0x0005:
        if (rx_pattern_index() == 0x1C) {
            REG16(0x00104CCEUL +
                  (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1)) =
                (u16)((u16)REG8(0x11F2BBUL) << 8);
        }
        break;
    case 0x0006:
        if (rx_pattern_index() == 0x1C) {
            const u32 at = 0x00104CCEUL +
                (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1);
            REG16(at) = (u16)(REG16(at) | (u16)REG8(0x11F2BBUL));
        }
        break;
    case 0x0007:
        if (rx_pattern_index() == 0x1C) {
            REG16(0x00104D06UL +
                  (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1)) =
                (u16)((u16)REG8(0x11F2BBUL) << 8);
        }
        break;
    default:
        if (rx_pattern_index() == 0x1C) {
            const u32 at = 0x00104D06UL +
                (u32)(long)(short)(u16)((u16)rx_pattern_index() << 1);
            REG16(at) = (u16)(REG16(at) | (u16)REG8(0x11F2BBUL));
        }
        REG8(0x11F2B6UL) = 0x04;
        break;
    }
}

/* ---- what happens when the checksum arrives ---------------------------
 * Receive states 2 and 4. The byte that just came in is the module's sum;
 * if it matches ours the answer is H'AA and the message is acted on, and if
 * it does not the answer is H'FF and bit 3 of H'114D4C says so.
 */
static void rx_frame_done(void)
{
    REG8(0xFFFF60UL) &= (u8)~0x02;
    REG16(0xFFFF72UL) = 0x0000;

    switch (REG8(0x11F2B9UL)) {
    case 0x03:
        REG8(0x114D5DUL) = 0x01;
        if (REG8(0x11A615UL) != 0) {
            REG8(0x114D70UL) = (u8)((REG8(0x11F2BAUL) & 0x08) ? 0x01 : 0x00);
            if (REG8(0x11F2BAUL) & 0x01) REG8(0x114D76UL) |=        0x01;
            else                         REG8(0x114D76UL) &= (u8)~0x01;
        }
        break;
    case 0x04:
        REG8(0x114D5EUL) = REG8(0x11F2BAUL);
        break;
    case 0x0D:
        if (REG8(0x11F2BAUL) == 0x01)      REG8(0x114D6DUL) = 0x01;
        else if (REG8(0x11F2BAUL) == 0x02) REG8(0x114D6EUL) = 0x01;
        break;
    case 0x05:
        REG8(0x114D50UL) &= (u8)~0x02;
        break;
    case 0x09:
        REG8(0x114D51UL) |= 0x80;
        break;
    case 0x01:
        REG8(0x114D4FUL) |= 0x04;
        break;
    default:
        break;
    }
}

/* ---- the rest of the messages ----------------------------------------- */

/* Message 1: a pattern's H'12-byte record. Message 2: its H'10-byte block.
 * Both take the pattern number in the second byte, and both clamp it: a
 * number of H'1C or more is forced down to H'1B, which is as many as there
 * is room for. */
static void rx_msg_01(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0012;
    } else if (i == 0x0001) {
        REG8(0x11A660UL) = REG8(0x11F2BBUL);
        if (REG8(0x11F2BBUL) >= 0x1C) REG8(0x11A660UL) = 0x1B;
    } else {
        const u32 at = 0x0011A41AUL +
            (u32)(long)(short)(u16)(0x12 * (u16)REG8(0x11A660UL)) +
            (u32)(u16)(i - 2);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 1) == REG16(0x11F2C0UL)) REG8(0x11F2B6UL) = 0x04;
    }
}

static void rx_msg_02(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0010;
    } else if (i == 0x0001) {
        REG8(0x11A660UL) = REG8(0x11F2BBUL);
        if (REG8(0x11F2BBUL) >= 0x1C) REG8(0x11A660UL) = 0x1B;
    } else {
        const u32 at = 0x0011A25AUL +
            (u32)(long)(short)(u16)((u16)REG8(0x11A660UL) << 4) +
            (u32)(u16)(i - 2);

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 1) == REG16(0x11F2C0UL)) REG8(0x11F2B6UL) = 0x04;
    }
}

/* Message 3. Twelve bytes into H'11A612. */
static void rx_msg_03(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) REG16(0x11F2C0UL) = 0x000C;
    else             rx_store(0x0011A611UL + i);
}

/* Messages 4 and 6. The header alone. */
static void rx_msg_04(void)
{
    if (REG16(0x11F2BEUL) == 0x0000) REG8(0x11F2B6UL) = 0x04;
}

/* Message 7. The eight-or-twelve bytes of machine state. */
static void rx_msg_07(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0008;
        return;
    }
    if (REG8(0x114D51UL) & 0x20) {
        REG16(0x11F2C0UL) = (u16)(REG16(0x11F2C0UL) + 0x0004);
        REG8(0x114D51UL) &= (u8)~0x20;
    }
    rx_store(0x00114D4BUL + i);
}

/* Message 8. Its sub-code goes straight into H'114D5C, and the three bytes
 * after it are a byte and a word. */
static void rx_msg_08(void)
{
    switch (REG16(0x11F2BEUL)) {
    case 0x0000: REG8(0x114D5CUL) = REG8(0x11F2BAUL); break;
    case 0x0001: REG8(0x11A661UL) = REG8(0x11F2BBUL); break;
    case 0x0002:
        REG16(0x11A662UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
        break;
    case 0x0003:
        REG16(0x11A662UL) =
            (u16)(REG16(0x11A662UL) | (u16)REG8(0x11F2BBUL));
        REG8(0x11F2B6UL) = 0x04;
        break;
    default: break;
    }
}

/* Message 9. Bit 0 of H'114D51 raised when the sub-code is not zero. */
static void rx_msg_09(void)
{
    if (REG16(0x11F2BEUL) != 0x0000) return;

    if (REG8(0x11F2BAUL) != 0) REG8(0x114D51UL) |= 0x01;
    REG8(0x11F2B6UL) = 0x04;
}

/* Message 11. Two words, each a byte at a time. */
static void rx_msg_0B(void)
{
    switch (REG16(0x11F2BEUL)) {
    case 0x0000: break;
    case 0x0001:
        REG16(0x11A636UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
        break;
    case 0x0002:
        REG16(0x11A636UL) =
            (u16)(REG16(0x11A636UL) | (u16)REG8(0x11F2BBUL));
        break;
    case 0x0003:
        REG16(0x11A638UL) = (u16)((u16)REG8(0x11F2BBUL) << 8);
        break;
    case 0x0004:
        REG16(0x11A638UL) =
            (u16)(REG16(0x11A638UL) | (u16)REG8(0x11F2BBUL));
        REG8(0x11F2B6UL) = 0x04;
        break;
    default: break;
    }
}

/* Message 12. H'15 bytes into H'104043, with the first two named. */
static void rx_msg_0C(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) {
        REG16(0x11F2C0UL) = 0x0015;
    } else if (i == 0x0001) {
        REG8(0x00104044UL) = REG8(0x11F2BBUL);
    } else if (i == 0x0002) {
        REG8(0x00104045UL) = REG8(0x11F2BBUL);
    } else {
        const u32 at = 0x00104043UL + (u32)(u16)i;

        REG32(0x11F2C2UL) = at;
        REG8(at) = REG8(0x11F2BBUL);
        if ((u16)(i - 2) == REG16(0x11F2C0UL)) REG8(0x11F2B6UL) = 0x04;
    }
}

/* Message 13. H'14 bytes into H'104C79. */
static void rx_msg_0D(void)
{
    const u16 i = REG16(0x11F2BEUL);

    if (i == 0x0000) REG16(0x11F2C0UL) = 0x0014;
    else             rx_store(0x00104C79UL + i);
}

/* Message 14. One byte, and the exchange ends there: the line is given back
 * and, if something was waiting on it, H'114D6C is answered. Returns 0 to
 * say that nothing else is to be done with this byte. */
static int rx_msg_0E(void)
{
    if (REG16(0x11F2BEUL) != 0x0000) return 1;

    REG8(0x00104C8EUL) = REG8(0x11F2BAUL);
    REG16(0x11F2BEUL) = 0x0000;
    REG8(0x11F2BCUL) = 0x00;
    REG8(0x11F2B6UL) = 0x00;
    REG8(0xFFFF60UL) &= (u8)~0x02;
    REG16(0xFFFF72UL) = 0x0000;
    if (REG8(0x114D6CUL) != 0) REG8(0x114D6CUL) = 0xBB;
    return 0;
}

/* ---- the receive interrupt itself -------------------------------------- */
void isr_sci0_rxi_body(void)
{
    u8 msg;

    REG8(0x11F2BBUL) = RDR0;
    SSR0 &= (u8)~0x40;
    REG16(0xFFFF72UL) = 0x0000;

    if (REG8(0x11F2B6UL) != 0x01) {
        if (REG8(0x11F2B6UL) != 0x00) {
            /* States 2 and 4 are the checksum; anything else is ignored. */
            if (REG8(0x11F2B6UL) == 0x02) {
                REG8(0x11F2B6UL) = 0x01;
                REG16(0x11F2BEUL) = 0x0000;
                REG8(0x11F29EUL) = 0x08;
                if (REG8(0x11F2BBUL) != REG8(0x11F2BCUL)) {
                    REG8(0x11F2B3UL) = 0xFF;
                    REG16(0x114D4CUL) =
                        (u16)(REG16(0x114D4CUL) | 0x0008);
                } else {
                    REG8(0x11F2B3UL) = 0xAA;
                    REG8(0x11F2B7UL) = (u8)(REG8(0x11F2B7UL) + 1);
                }
                link_send_start();
                REG8(0x11F2BCUL) = 0x00;
            } else if (REG8(0x11F2B6UL) == 0x04) {
                REG16(0x11F2BEUL) = 0x0000;
                REG8(0x11F29EUL) = 0x08;
                if (REG8(0x11F2BBUL) != REG8(0x11F2BCUL)) {
                    REG8(0x11F2B3UL) = 0xFF;
                    REG8(0x11F2B6UL) = 0x01;
                    REG16(0x114D4CUL) =
                        (u16)(REG16(0x114D4CUL) | 0x0008);
                } else {
                    REG8(0x11F2B3UL) = 0xAA;
                    rx_frame_done();
                    REG8(0x11F2B6UL) = 0x00;
                }
                link_send_start();
                REG8(0x11F2BCUL) = 0x00;
            }
            return;
        }

        /* State 0: the first byte of a frame. */
        REG8(0x11F2B6UL) = 0x01;

        if (REG8(0x11F29EUL) != 0x04) {
            const u8 b = REG8(0x11F2BBUL);

            if (b == 0xFF || b == 0x00 || b == 0xAA) {
                REG8(0x11F2B6UL) = 0x00;
                return;
            }
        }

        if (REG8(0x11F29EUL) == 0x04) {
            /* The answer to something we sent. Counting the one bits is how
             * a line with nothing on it is told from a real reply: between
             * three and five of them is a byte, anything else is noise. */
            u8 ones = 0, i;

            REG8(0xFFFF60UL) &= (u8)~0x02;
            REG16(0xFFFF72UL) = 0x0000;

            for (i = 0; i < 0x08; i++) {
                const u8 v = REG8(0x11F2BBUL);

                if (v & 0x01) ones++;
                REG8(0x11F2BBUL) = (u8)(v >> 1);
            }

            REG8(0x11F29EUL) =
                (u8)((ones <= 0x05 && ones >= 0x03) ? 0x06 : 0x05);
            link_send_start();
            REG8(0x11F2B6UL) = 0x00;
            return;
        }

        if (REG8(0x11F29EUL) != 0x00) {
            /* Something arrived in the middle of our own message. */
            link_send_stop();
            link_delay(0x000A);
            link_send_start();
            REG16(0x114D4CUL) = (u16)(REG16(0x114D4CUL) | 0x0010);
            REG8(0x11F2B6UL) = 0x00;
            return;
        }

        /* A fresh frame: unpack the message and its sub-code. */
        REG8(0x11F2B7UL) = 0x00;
        REG8(0x11F2B8UL) = 0x00;
        REG8(0x11F2BCUL) = 0x00;
        REG16(0x11F2BEUL) = 0x0000;
        REG8(0x11F2CAUL) = 0x00;
        REG8(0x11F2B9UL) = (u8)(REG8(0x11F2BBUL) >> 4);
        REG8(0x11F2BAUL) = (u8)(REG8(0x11F2BBUL) & 0x0F);
        REG8(0x11F2B6UL) = 0x01;
        REG8(0xFFFF60UL) |= 0x02;
    }

    msg = REG8(0x11F2B9UL);
    if ((u8)(msg - 1) <= 0x0E) {
        switch (msg) {
        case 0x01: rx_msg_01(); break;
        case 0x02: rx_msg_02(); break;
        case 0x03: rx_msg_03(); break;
        case 0x04: rx_msg_04(); break;
        case 0x05: rx_msg_05(); break;
        case 0x06: rx_msg_04(); break;   /* H'2340D8, the same code */
        case 0x07: rx_msg_07(); break;
        case 0x08: rx_msg_08(); break;
        case 0x09: rx_msg_09(); break;
        case 0x0A: break;
        case 0x0B: rx_msg_0B(); break;
        case 0x0C: rx_msg_0C(); break;
        case 0x0D: rx_msg_0D(); break;
        case 0x0E: if (!rx_msg_0E()) return; break;
        case 0x0F: rx_msg_0F(); break;
        default: break;
        }
    }

    rx_byte_done();
}

/* ---- all ten interrupt slots are now real ------------------------------ */
void isr_sci0_eri(void) __attribute__((interrupt_handler));
void isr_sci0_eri(void) { isr_sci0_eri_body(); }

void isr_sci0_txi(void) __attribute__((interrupt_handler));
void isr_sci0_txi(void) { isr_sci0_txi_body(); }

void isr_sci0_rxi(void) __attribute__((interrupt_handler));
void isr_sci0_rxi(void) { isr_sci0_rxi_body(); }

/* H'21AC2E. One box, and it goes back to whatever the screen stack has on
 * top -- the depth read first and used as the index, which is entry zero of
 * the stack read again as a number. */
u8 screen_back_one(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;
    message_show_held(index);
    if (value != 0x0001) return 0x00;

    if (REG8(0x00114DC6UL) & 0x80) return 0x00;
    screen_stack_pop();
    screen_switch(screen_stack_at(screen_stack_depth()), 0x01, 0x00);
    return 0x00;
}

/* H'216E6C. A message that goes away either when the operator presses its
 * one box or when the thing that put it up stops waiting.
 *
 * Both ways do the same five things: the message held, slot four made
 * current again, the two "a message can go up" bytes set, H'246D7E, and the
 * stack popped. They differ only in what makes H'11A177 go up -- the timed
 * way asks nothing else, the pressed way asks that the stack flag at
 * H'11A17C is down as well -- and in the order of the pop.
 *
 * On the timed way the message number handed to H'211A9E is the first local
 * and nothing has written it, the same read-before-write as H'21BA0E.
 *
 * A clear bit and a waiter that has stopped waiting both fall into the hit
 * test, not just the clear bit: the two tests branch to the same place. */
u8 message_wait_screen(void)
{
    u16 value = 0;
    u16 index;      /* read before it is written on the first path */

    if ((REG8(0x00FFFEC4UL) & 0x01) && link_owner_waiting()) {
        message_show_held(index);
        screen_from_slot(0x04);
        REG8(0x0011A179UL) = 0x01;
        REG8(0x0011B0A9UL) = 0x01;
        message_state_set();
        if (REG8(0x00FFFEC4UL) & 0x01) REG8(0x0011A177UL) = 0x01;
        screen_stack_pop();
        return 0x00;
    }

    if (touch_hit(0x0001, 0x0001, &value, &index) != 0x03) return 0x00;
    if (value != 0x001A) return 0x00;

    message_show_held(index);
    screen_from_slot(0x04);
    REG8(0x0011A179UL) = 0x01;
    REG8(0x0011B0A9UL) = 0x01;
    message_state_set();
    screen_stack_pop();
    if ((REG8(0x00FFFEC4UL) & 0x01) && REG8(0x0011A17CUL) == 0) {
        REG8(0x0011A177UL) = 0x01;
    }
    return 0x00;
}

/* H'21D104. The presser-foot pressure, H'00 to H'10, shown on the balance
 * bar. H'11B382 is the value being edited and H'11B380 the one to go back
 * to; the byte in flash is at H'57FF8F.
 *
 * Box H'17 puts one on as far as H'10, H'18 takes one off as far as zero,
 * H'7F goes to the middle at eight, H'19 accepts and H'1A cancels. Accepting
 * leaves the screen *before* it writes the flash, and copies the value into
 * a local first because the flash writer takes an address to copy from. */
u8 foot_pressure_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        const u16 v = (u16)REG8(0x0057FF8FUL);

        screen_stack_push();
        REG16(0x0011B382UL) = v;
        REG16(0x0011B380UL) = v;
        balance_bar_draw(v);
    }

    if (touch_hit(0x0001, 0x0005, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if (value == 0x0017) {
        if ((short)REG16(0x0011B382UL) < 0x0010) {
            REG16(0x0011B382UL) = (u16)(REG16(0x0011B382UL) + 1);
            balance_bar_draw(REG16(0x0011B382UL));
        }
        return 0x00;
    }
    if (value == 0x0018) {
        if ((short)REG16(0x0011B382UL) > 0) {
            REG16(0x0011B382UL) = (u16)(REG16(0x0011B382UL) - 1);
            balance_bar_draw(REG16(0x0011B382UL));
        }
        return 0x00;
    }
    if (value == 0x007F) {
        REG16(0x0011B382UL) = 0x0008;
        balance_bar_draw(REG16(0x0011B382UL));
        return 0x00;
    }
    if (value == 0x0019) {
        u8 out;

        screen_stack_pop();
        screen_switch(0x27, 0x01, 0x00);
        FLASH_BUSY |= 0x20;
        out = REG8(0x0011B383UL);
        rom_flash_write(&out, 0x0057FF8FUL, 1);
        FLASH_BUSY &= (u8)~0x20;
        return 0x00;
    }
    if (value == 0x001A) {
        screen_stack_pop();
        REG16(0x0011B382UL) = REG16(0x0011B380UL);
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'218780. The two boxes at H'1C and H'1D, and the stroke that goes with
 * them, put up or taken down.
 *
 * The first argument picks which of the two tables H'218378 draws from and,
 * with it, which stitch category the current pattern has to be in for the
 * stroke to mean anything: H'11 for the first table and H'10 for the
 * second. The second argument is the press: down puts both boxes into state
 * 0 and draws the stroke, or greys them if the category is wrong; up is
 * only acted on when the boxes were greyed, and then it rubs the stroke out,
 * clears H'FFFEFD and draws stroke zero in its place.
 *
 * The record index is widened without sign, unlike every other use of the
 * stitch descriptor table. */
void stitch_stroke_toggle(u8 variant, u8 pressed)
{
    const u8 want = (u8)(variant != 0 ? 0x11 : 0x10);
    const u32 rec = ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL));

    if (pressed != 0) {
        hitbox_set_state(0x001C, 0x001D, 0x00, 0);
        if (REG8(rec + 0x17) == want) {
            preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x01);
        } else {
            hitbox_set_state(0x001C, 0x001D, 0x05, 0);
        }
        return;
    }

    if (REG8(rec + 0x17) != want) return;
    if (hitbox_kind(0x001C) != 0x05) return;

    hitbox_set_state(0x001C, 0x001D, 0x00, 0);
    preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x00);
    REG8(0x00FFFEFDUL) = 0x00;
    preview_stroke_draw(0x0000, variant, 0x01);
}

/* H'219A52. A list of twelve boxes with arrows either side, and a choice
 * that is remembered in H'11B328.
 *
 * The two arrows scroll the list three at a time, and each is only live
 * when its flag -- H'11B0AA up, H'11B0AB down -- says there is more list
 * that way. Scrolling puts the lit box out and lights whichever box the
 * choice has moved to afterwards, found by value rather than by index
 * because the boxes have shifted underneath it.
 *
 * Box H'19 goes on to screen H'10 in slot two and H'1A comes back off the
 * stack to H'0E; both forget the choice and both are refused while sewing.
 * Anything else is a choice: the old lit box goes out, the pressed one comes
 * on, and the value less H'25 goes into H'11B0FE. */
u8 pattern_list_screen(void)
{
    u16 value = 0, index = 0;
    u16 box;

    if (touch_hit(0x0001, 0x0010, &value, &index) != 0x03) return 0x00;

    if (value == 0x0017) {
        if (REG8(0x0011B0AAUL) != 0) {
            message_show_held(index);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x00, 0);
            REG16(0x0011B108UL) =
                hitbox_list_scroll_back(0x0001, 0x000C, 0x0003);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x01, 0);
        }
        return 0x00;
    }

    if (value == 0x0018) {
        if (REG8(0x0011B0ABUL) != 0) {
            message_show_held(index);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x00, 0);
            REG16(0x0011B108UL) =
                hitbox_list_scroll_on(0x0001, 0x000C, 0x0003);
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x01);
            hitbox_set_state(box, box, 0x01, 0);
        }
        return 0x00;
    }

    if (value == 0x0019) {
        if (REG16(0x0011B328UL) != 0) {
            message_show_held(index);
            if (REG8(0x00114DC6UL) & 0x80) return 0x00;
            screen_switch(0x10, 0x02, 0x00);
            REG16(0x0011B328UL) = 0x0000;
        }
        return 0x00;
    }

    if (value == 0x001A) {
        message_show_held(index);
        if (REG8(0x00114DC6UL) & 0x80) return 0x00;
        screen_stack_pop();
        screen_switch(0x0E, 0x01, 0x00);
        REG16(0x0011B328UL) = 0x0000;
        return 0x00;
    }

    if (value != REG16(0x0011B328UL)) {
        if (REG16(0x0011B328UL) != 0) {
            box = hitbox_find(0x0001, 0x000C, REG16(0x0011B328UL), 0x00);
            hitbox_set_state(box, box, 0x00, 0);
        }
        hitbox_set_state(index, index, 0x01, 0);
        REG16(0x0011B328UL) = value;
        REG8(0x0011B0FEUL) = (u8)((u8)value + 0xDB);
    }
    return 0x00;
}

/* H'219DE0. The help page for whatever H'11B0FE and H'11B0FF name, drawn
 * out of one of the two picture tables.
 *
 * Which table, and how long a record is, both follow the configuration
 * byte: the machine with the module has nine parts to a record and the one
 * without has ten, even though both tables are laid out the same way. The
 * record number is the page times that stride plus the part, added as a
 * byte with the carry taken into the top half.
 *
 * A record whose first part is there and whose second is not is one picture
 * for the whole page: it goes into the scratch buffer and is copied across
 * from there, which is what stops it appearing a strip at a time. Otherwise
 * the nine parts go into the nine boxes -- the first eight through
 * H'212D8A, and the ninth placed by its own width and height so that its
 * bottom right corner lands at H'0112, H'00EA.
 *
 * The two halves are written out in full in the original, one per table. */
static u32 help_page_part(u8 module, u16 rec, u16 part)
{
    return module ? help_picture_module(rec, part) : help_picture(rec, part);
}

void help_page_draw(void)
{
    const u8  module = (u8)(CONFIG_BLOCK == 0xAA);
    const u16 rec = (u16)((u16)((module ? 9 : 10) * (u16)REG8(0x0011B0FEUL))
                          + (u16)REG8(0x0011B0FFUL));
    short i;

    if (help_page_part(module, rec, 0) != 0 &&
        help_page_part(module, rec, 1) == 0) {
        const u32 p = help_page_part(module, rec, 0);

        bitmap_draw(0x0004, 0x0027, 0x0114, 0x00EC,
                    (const u8 *)p, LCD_SCRATCH);
        region_copy(0x0004, 0x0027, 0x0114, 0x00EC, 0x0027,
                    LCD_SCRATCH, LCD_FRAME_A);
        return;
    }

    for (i = 0; i <= 0x0008; i++) {
        const u32 p = help_page_part(module, rec, (u16)i);

        if (p == 0) continue;

        if (i < 0x0008) {
            hitbox_blit((u16)(i + 1), LCD_FRAME_A, p);
        } else {
            const u16 y0 = (u16)(0x00EA - header_word_1((const u8 *)p));
            const u16 x0 = (u16)(0x0112 - header_word_0((const u8 *)p));

            bitmap_draw(x0, y0, 0x0112, 0x00EA, (const u8 *)p, LCD_FRAME_A);
        }
    }
}

/* H'21C846. The beep settings: eight of them, each a pair of bytes -- on or
 * off, and which of three patterns -- copied out of flash into H'11B368 to
 * be edited and written back on accept.
 *
 * Boxes 1 to 8 pick a setting: the pressed one lights, the last one goes
 * out, H'11A1B6 is pointed at its pair, and boxes 9 and H'0A show the on/off
 * state while box H'0B shows the pattern as one of three thirds. Boxes 9 and
 * H'0A set it, H'0B steps the pattern round 1-2-3, H'19 writes the block to
 * flash and H'1A throws it away.
 *
 * On the way in box 5 is greyed on the machine with the module -- that
 * setting means nothing there -- and the whole run is put back to state 0
 * first. Box H'0B does not check H'11A1B6 before following it, unlike boxes
 * 9 and H'0A, which do. */
u8 beep_settings_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        hitbox_set_state(0x0001, 0x000D, 0x00, 0);
        if (CONFIG_BLOCK == 0xAA) hitbox_set_state(0x0005, 0x0005, 0x02, 0);
        REG16(0x0011B366UL) = 0x0000;
        mem_copy((u8 *)0x0011B368UL, (const u8 *)0x0057EFC8UL, 0x0012);
        return 0x00;
    }

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return 0x00;

    if ((short)index <= 0x0008 && REG16(0x0011B366UL) != index) {
        const u16 was = REG16(0x0011B366UL);
        u32 p;

        hitbox_set_state(index, index, 0x01, 0);
        hitbox_set_state(was, was, 0x00, 0);

        p = 0x0011B368UL + (u32)(long)(short)(u16)((u16)(index << 1));
        REG32(0x0011A1B6UL) = p;

        if (REG8(p) != 0) {
            hitbox_set_state(0x0009, 0x0009, 0x01, 0);
            hitbox_set_state(0x000A, 0x000A, 0x00, 0);
        } else {
            hitbox_set_state(0x0009, 0x0009, 0x00, 0);
            hitbox_set_state(0x000A, 0x000A, 0x01, 0);
        }

        hitbox_third_mark(0x000B, REG8(REG32(0x0011A1B6UL) + 1));
        REG16(0x0011B366UL) = index;
    }

    if (value == 0x0009) {
        const u32 p = REG32(0x0011A1B6UL);

        if (p != 0) {
            REG8(p) = 0x01;
            hitbox_set_state(0x0009, 0x0009, 0x01, 0);
            hitbox_set_state(0x000A, 0x000A, 0x00, 0);
        }
        return 0x00;
    }

    if (value == 0x000A) {
        const u32 p = REG32(0x0011A1B6UL);

        if (p != 0) {
            REG8(p) = 0x00;
            hitbox_set_state(0x0009, 0x0009, 0x00, 0);
            hitbox_set_state(0x000A, 0x000A, 0x01, 0);
        }
        return 0x00;
    }

    if (value == 0x000B) {
        REG8(REG32(0x0011A1B6UL) + 1) =
            (u8)(REG8(REG32(0x0011A1B6UL) + 1) + 1);
        if (REG8(REG32(0x0011A1B6UL) + 1) > 0x03) {
            REG8(REG32(0x0011A1B6UL) + 1) = 0x01;
        }
        hitbox_third_mark(0x000B, REG8(REG32(0x0011A1B6UL) + 1));
        return 0x00;
    }

    if (value == 0x0019) {
        screen_stack_pop();
        message_show_held(index);
        FLASH_BUSY |= 0x20;
        rom_flash_write((const void *)0x0011B368UL, 0x0057EFC8UL, 0x12);
        FLASH_BUSY &= (u8)~0x20;
        screen_switch(0x27, 0x01, 0x00);
        return 0x00;
    }

    if (value == 0x001A) {
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x27, 0x01, 0x00);
    }
    return 0x00;
}

/* H'221B6E. The pattern strip put back on the screen when one of seven
 * screens is returned to.
 *
 * Four shapes between them, and all four end the same way: the cursor put
 * out, the screen made current, its leave hook run with force, and the two
 * "settling" bytes raised. Anything but those seven screens does nothing at
 * all -- not even the tail.
 *
 * The strip itself is the same four rectangles every time, only the width of
 * the top one moving; what changes is which run of boxes carries the list,
 * which run is handed over with state 4, and where the two arrows are. The
 * H'07 and H'34 shapes also slide a run of boxes along first, which is what
 * makes room for the wider strip. */
void pattern_strip_restore(u8 screen)
{
    if (screen == 0x07 || screen == 0x45) {
        hitbox_set_state(0x001F, 0x0020, 0x00, 0);
        hitbox_set_state(0x000B, 0x000F, 0x04, 0);
        hitbox_run_shift(0x000B, 0x000F, 0x0021);
        hitbox_set_state(0x0021, 0x0025, 0x03, 0);
        draw_rect(0x0006, 0x009D, 0x00DD, 0x009E, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x0029, 0x009F, 0x002C, 0x00C1, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x001A, 0x0020, 0x0001, 0x00115A10UL);
        hitbox_set_state(0x0012, 0x0015, 0x04, 0);
        picker_arrows(0x001E, 0x001F, 0x01);
    } else if (screen == 0x34 || screen == 0x36) {
        hitbox_set_state(0x001A, 0x001B, 0x00, 0);
        hitbox_set_state(0x0007, 0x0008, 0x04, 0);
        hitbox_run_shift(0x0007, 0x0008, 0x0013);
        hitbox_set_state(0x0013, 0x0014, 0x03, 0);
        draw_rect(0x0006, 0x009D, 0x00E8, 0x009E, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x0029, 0x009F, 0x002C, 0x00C1, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x0015, 0x001B, 0x0001, 0x00115A10UL);
        hitbox_set_state(0x000F, 0x0012, 0x04, 0);
        picker_arrows(0x0019, 0x001A, 0x01);
    } else if (screen == 0x04) {
        hitbox_set_state(0x0024, 0x0025, 0x00, 0);
        hitbox_set_state(0x0010, 0x0014, 0x04, 0);
        draw_rect(0x0006, 0x009D, 0x00E8, 0x009E, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x0029, 0x009F, 0x002C, 0x00C1, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x001F, 0x0025, 0x0001, 0x00115A10UL);
        hitbox_set_state(0x001B, 0x001E, 0x04, 0);
        picker_arrows(0x0023, 0x0024, 0x01);
    } else if (screen == 0x47) {
        draw_rect(0x0004, 0x009D, 0x0114, 0x00C3, LCD_FRAME_A, 0x02, 0x01);
        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 0x00, 0x01);
        draw_rect(0x0004, 0x00C6, 0x013B, 0x00EC, LCD_FRAME_A, 0x02, 0x01);
        hitbox_fill_from_list(0x000E, 0x0012, 0x0001, 0x00119362UL);
    } else {
        return;
    }

    picker_cursor(0x01);
    REG8(0x0011A169UL) = screen;
    screen_leave(screen, 0x01);
    REG8(0x0011A174UL) = 0x01;
    REG8(0x0011A17BUL) = 0x01;
}

/* H'2189A6 and H'218ADE. The two screens that step the stroke number in
 * H'FFFEFD, one per stroke table: H'2189A6 draws from the first and wraps
 * at three, H'218ADE from the second and wraps at H'0F.
 *
 * Entering either calls H'218780 twice, once as a press and once as a
 * release, which is what puts the two boxes and the stroke up to start with;
 * every later pass calls it once, as a release. Box H'6C steps up and H'6B
 * steps down, and each rubs the old stroke out before drawing the new one.
 * A press on a greyed box is dropped before the message, so a screen whose
 * pattern is in the wrong category says nothing at all. */
static u8 stroke_pick_screen(u8 first_pass, u8 variant, u8 top)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) stitch_stroke_toggle(variant, 0x01);
    stitch_stroke_toggle(variant, 0x00);

    if (touch_hit(0x001C, 0x001D, &value, &index) != 0x03) return 0x00;
    if (hitbox_kind(index) == 0x05) return 0x00;
    message_show_held(index);

    if (value == 0x006C) {
        preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x00);
        if (REG8(0x00FFFEFDUL) < top) {
            const u8 v = (u8)(REG8(0x00FFFEFDUL) + 1);

            REG8(0x00FFFEFDUL) = v;
            preview_stroke_draw((u16)v, variant, 0x01);
        } else {
            REG8(0x00FFFEFDUL) = 0x00;
            preview_stroke_draw(0x0000, variant, 0x01);
        }
        return 0x00;
    }

    if (value == 0x006B) {
        preview_stroke_draw((u16)REG8(0x00FFFEFDUL), variant, 0x00);
        if (REG8(0x00FFFEFDUL) != 0) {
            const u8 v = (u8)(REG8(0x00FFFEFDUL) - 1);

            REG8(0x00FFFEFDUL) = v;
            preview_stroke_draw((u16)v, variant, 0x01);
        } else {
            REG8(0x00FFFEFDUL) = top;
            preview_stroke_draw((u16)top, variant, 0x01);
        }
    }
    return 0x00;
}

u8 stroke_pick_screen_a(u8 first_pass)
{
    return stroke_pick_screen(first_pass, 0x01, 0x03);
}

u8 stroke_pick_screen_b(u8 first_pass)
{
    return stroke_pick_screen(first_pass, 0x00, 0x0F);
}

/* ---- the hoop offsets ---------------------------------------------------
 * H'248FF0 and the nine handlers behind H'230D4E. The screen shows how far
 * the hoop has been moved from where the module thinks it is, in millimetres
 * each way, and lets it be nudged.
 *
 * H'104C7A and H'104C7B are the two offsets in half-millimetre steps, kept
 * as signed bytes. H'248FF0 draws them: each is halved, its sign thrown
 * away, turned into decimal and given "mm", and the two strings go into the
 * two labels down the left edge.
 *
 * The original writes the two halves of that out twice over, once for a
 * negative reading and once for a non-negative one, and the negative half
 * runs the quotient through H'24ADC8 to take the sign off. Both are
 * reproduced: for a non-negative reading the two agree anyway, so the
 * duplication costs nothing but the instructions it is made of.
 */
static void hoop_offset_label(signed char raw, void (*draw)(const char *))
{
    char text[6];
    char suffix[3];

    suffix[0] = 0x6D;   /* "mm" */
    suffix[1] = 0x6D;
    suffix[2] = 0x00;

    if (raw < 0) {
        const short q = (short)((short)raw / 2);

        int_to_decimal((short)(signed char)abs_short((short)(signed char)q),
                       text);
    } else {
        const short q = (short)((short)raw / 2);

        int_to_decimal((short)(signed char)q, text);
    }

    str_append(text, suffix);
    draw(text);
}

void hoop_offsets_draw(void)
{
    hoop_offset_label((signed char)REG8(0x00104C7AUL), text_left_D9);
    hoop_offset_label((signed char)REG8(0x00104C7BUL), text_left_BC);
}

/* H'248AC6 and the seven below it: one nudge of the hoop, one per direction.
 *
 * All eight are the same routine with two constants changed -- the direction
 * code the module is sent in H'11A615, and which of the two offsets moves --
 * and all eight refuse in the same three ways: the link never went quiet, or
 * either offset is already further than H'64 from home. The limit is tested
 * against the offset *before* the step, so a nudge that would take it past
 * H'64 is allowed and the next one is not.
 *
 * The direction codes run clockwise from north: 1 N, 2 NE, 3 E, 4 SE, 5 S,
 * 6 SW, 7 W, 8 NW. North is H'104C7B going down, so the second offset counts
 * away from the operator.
 */
static void hoop_nudge(u8 code, signed char dx, signed char dy)
{
    if (link_wait_idle() == 0) return;
    if (abs_short((short)(signed char)REG8(0x00104C7AUL)) > 0x0064) return;
    if (abs_short((short)(signed char)REG8(0x00104C7BUL)) > 0x0064) return;

    REG8(0x0011F2A1UL)  = 0x03;
    REG8(0x0011A615UL)  = code;
    REG8(0x0011F2A2UL) |= 0x08;
    link_send_start();

    if (dx != 0) REG8(0x00104C7AUL) = (u8)(REG8(0x00104C7AUL) + dx);
    if (dy != 0) REG8(0x00104C7BUL) = (u8)(REG8(0x00104C7BUL) + dy);

    hoop_offsets_draw();
}

/* H'248E5C. The hoop sent home: message H'0D, two seconds to let it get
 * there, the slot's two stored positions zeroed and then message H'02. Each
 * step waits for the link to go quiet first, and the last wait's answer is
 * thrown away -- there is nothing left to do with it either way. */
static void hoop_reset(void)
{
    REG8(0x00114D8EUL) = 0x00;
    if (link_wait_idle() == 0) return;

    REG8(0x0011F2A1UL) = 0x0D;
    REG8(0x0011F2A2UL) = 0x01;
    link_send_start();
    link_delay(0x07D0);
    if (link_wait_idle() == 0) return;

    REG16(PAT_A(0x0C)) = 0x0000;
    REG16(PAT_A(0x0E)) = 0x0000;
    REG8(0x0011F2A1UL) = 0x02;
    link_send_start();
    (void)link_wait_idle();
}

/* H'230D4E. The nine boxes that move the hoop: eight directions and home.
 *
 * The eight nudges return 0 for the two that only move north and 1 for the
 * rest, which is in the original and looks like nothing more than the order
 * the handlers were written in. Home also leaves for screen H'1F. */
u8 hoop_move_screen(void)
{
    u16 value = 0, index = 0;

    if (touch_hit(0x0001, 0x0009, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    switch ((u16)(value - 1)) {
    case 0x0000: hoop_nudge(0x08, -1, -1); return 0x00;
    case 0x0001: hoop_nudge(0x01,  0, -1); return 0x00;
    case 0x0002: hoop_nudge(0x02,  1, -1); return 0x01;
    case 0x0003: hoop_nudge(0x07, -1,  0); return 0x01;
    case 0x0004: hoop_nudge(0x03,  1,  0); return 0x01;
    case 0x0005: hoop_nudge(0x06, -1,  1); return 0x01;
    case 0x0006: hoop_nudge(0x05,  0,  1); return 0x01;
    case 0x0007: hoop_nudge(0x04,  1,  1); return 0x01;
    case 0x0008:
        hoop_reset();
        screen_switch(0x1F, 0x01, 0x00);
        return 0x01;
    default:
        break;
    }
    return 0x00;
}

/* H'217F04. Ten categories down one menu, and an eleventh box that only
 * takes the screen change.
 *
 * The same shape as H'217DE0 further up, but with the ten handlers written
 * out one per box behind a jump table rather than as a chain of compares.
 * Each is the same four instructions with one constant changed, and the
 * constants are the categories H'05 to H'0E in box order, so the arithmetic
 * would collapse to "value plus four" -- it is left as the table it is,
 * because that is what the original holds and a category out of place should
 * be a difference rather than an off-by-one nobody notices.
 *
 * The message and the screen change both happen before the table is reached,
 * so a value the table does not cover still leaves for screen H'02. */
u8 menu_ten_categories(void)
{
    u16 value = 0, index = 0;
    u8  wanted;

    if (touch_hit(0x0001, 0x000A, &value, &index) != 0x03) return 0x00;

    message_show_held(index);
    screen_switch(0x02, 0x01, 0x00);

    switch (value) {
    case 0x0001: wanted = 0x05; break;
    case 0x0002: wanted = 0x06; break;
    case 0x0003: wanted = 0x07; break;
    case 0x0004: wanted = 0x08; break;
    case 0x0005: wanted = 0x09; break;
    case 0x0006: wanted = 0x0A; break;
    case 0x0007: wanted = 0x0B; break;
    case 0x0008: wanted = 0x0C; break;
    case 0x0009: wanted = 0x0D; break;
    case 0x000A: wanted = 0x0E; break;
    case 0x000B: return 0x01;   /* nothing to pick: the screen change is all */
    default:     return 0x00;
    }

    REG16(0x0011B108UL) = first_item_of_category(wanted, 0x0011A88EUL);
    return 0x01;
}

/* ---- the four "pick one of a list" screens ------------------------------
 * H'21A320, H'21A56C, H'21A7F6 and H'21AA42. Four screens with the same
 * skeleton and different handlers hanging off it: on the first pass the
 * screen is pushed and the remembered box cleared, and every pass runs the
 * hit test, lights the box that was pressed, puts the last one out, and then
 * jumps into a table on the box value.
 *
 * The remembered box lives at H'11B32A, H'11B330, H'11B336 and H'11B33C --
 * six bytes apart, one per screen, in the same order as the routines.
 *
 * They differ in more than their handlers: which values are left out of the
 * lighting (two of them name H'04 and their last box, one takes everything
 * below H'0B), and what the table is indexed by (three by value less one,
 * H'21A320 by value less two). Each is written out with its own guard rather
 * than folded into one parameterised routine, because those differences are
 * exactly the sort a shared skeleton would hide.
 */
static void pick_box_light(u32 state, u16 value)
{
    const u16 was = REG16(state);

    hitbox_set_state(was, was, 0x00, 0);
    hitbox_set_state(value, value, 0x01, 0);
    REG16(state) = value;
}

/* H'21AA42. Eight boxes: six pick one of six longwords out of the record at
 * H'11B2AA, box 4 accepts what has been picked and box 8 goes back.
 *
 * The six offsets are H'60 to H'74 in steps of four, which is a run of six
 * with box 4 sitting in the middle of it -- so the offset is not a plain
 * multiple of the box number and the table is kept as a table. */
#define PICK4_STATE  0x0011B33CUL
#define PICK4_HELD   0x0011B33EUL

static void pick_take(u32 held, u16 offset)
{
    REG32(held) = REG32(REG32(0x0011B2AAUL) + (u32)offset);
}

u8 pick_screen_4(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK4_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x0008, &value, &index) != 0x03) return 0x00;

    if (value != 0x0004 && value != 0x0008 && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK4_STATE, value);
    }

    if ((u16)(value - 1) > 0x0007) return 0x00;

    switch (value) {
    case 0x0001: pick_take(PICK4_HELD, 0x60); break;
    case 0x0002: pick_take(PICK4_HELD, 0x64); break;
    case 0x0003: pick_take(PICK4_HELD, 0x68); break;
    case 0x0005: pick_take(PICK4_HELD, 0x6C); break;
    case 0x0006: pick_take(PICK4_HELD, 0x70); break;
    case 0x0007: pick_take(PICK4_HELD, 0x74); break;

    case 0x0004:
        if (REG16(PICK4_STATE) == 0) break;
        message_show_held(index);
        REG32(0x001161B8UL) = REG32(PICK4_HELD);
        screen_remember(0x01);
        screen_switch(0x3A, 0x01, 0x00);
        break;

    case 0x0008:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x39, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'21A7F6. Eleven boxes: nine picks at H'38 to H'58, box 4 accepts and box
 * H'0B goes back. The same shape as H'21AA42 with a wider window on the
 * record. */
#define PICK3_STATE  0x0011B336UL
#define PICK3_HELD   0x0011B338UL

u8 pick_screen_3(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK3_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x000B, &value, &index) != 0x03) return 0x00;

    if (value != 0x0004 && value != 0x000B && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK3_STATE, value);
    }

    if ((u16)(value - 1) > 0x000A) return 0x00;

    switch (value) {
    case 0x0001: pick_take(PICK3_HELD, 0x38); break;
    case 0x0002: pick_take(PICK3_HELD, 0x3C); break;
    case 0x0003: pick_take(PICK3_HELD, 0x40); break;
    case 0x0005: pick_take(PICK3_HELD, 0x44); break;
    case 0x0006: pick_take(PICK3_HELD, 0x48); break;
    case 0x0007: pick_take(PICK3_HELD, 0x4C); break;
    case 0x0008: pick_take(PICK3_HELD, 0x50); break;
    case 0x0009: pick_take(PICK3_HELD, 0x54); break;
    case 0x000A: pick_take(PICK3_HELD, 0x58); break;

    case 0x0004:
        if (REG16(PICK3_STATE) == 0) break;
        message_show_held(index);
        REG32(0x001161B8UL) = REG32(PICK3_HELD);
        screen_remember(0x01);
        screen_switch(0x3A, 0x01, 0x00);
        break;

    case 0x000B:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x39, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'21A56C. Twelve boxes: ten picks at H'04 to H'24, then accept and back.
 *
 * This is the one whose lighting guard is a range rather than two names --
 * everything below H'0B lights, so the last two boxes do not -- and the one
 * whose tenth pick depends on the machine: H'2C with the module and H'28
 * without. */
#define PICK2_STATE  0x0011B330UL
#define PICK2_HELD   0x0011B332UL

u8 pick_screen_2(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK2_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x000C, &value, &index) != 0x03) return 0x00;

    if ((short)value < 0x000B && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK2_STATE, value);
    }

    if ((u16)(value - 1) > 0x000B) return 0x00;

    switch (value) {
    case 0x0001: pick_take(PICK2_HELD, 0x04); break;
    case 0x0002: pick_take(PICK2_HELD, 0x08); break;
    case 0x0003: pick_take(PICK2_HELD, 0x0C); break;
    case 0x0004: pick_take(PICK2_HELD, 0x10); break;
    case 0x0005: pick_take(PICK2_HELD, 0x14); break;
    case 0x0006: pick_take(PICK2_HELD, 0x18); break;
    case 0x0007: pick_take(PICK2_HELD, 0x1C); break;
    case 0x0008: pick_take(PICK2_HELD, 0x20); break;
    case 0x0009: pick_take(PICK2_HELD, 0x24); break;

    case 0x000A:
        if (CONFIG_BLOCK == 0xAA) pick_take(PICK2_HELD, 0x2C);
        else                      pick_take(PICK2_HELD, 0x28);
        break;

    case 0x000B:
        if (REG16(PICK2_STATE) == 0) break;
        message_show_held(index);
        REG32(0x001161B8UL) = REG32(PICK2_HELD);
        screen_remember(0x01);
        screen_switch(0x3A, 0x01, 0x00);
        break;

    case 0x000C:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x39, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'21A320. Eleven boxes, and the odd one out of the four.
 *
 * Its table is indexed by the value less *two*, not less one, so box 1 is
 * not in it at all; two of the ten entries point at the shared tail, so
 * boxes 5 and 7 are in the table and do nothing. Its accept does not
 * remember the screen, and where the other three all leave for H'39 this one
 * leaves for H'0E.
 *
 * Accept is a four-way branch on which box is lit rather than a single
 * screen change: boxes 1, 5 and 7 have screens of their own, nothing lit
 * does nothing, and everything else takes the held longword across and goes
 * to H'3A like the others. Boxes 5 and 7 being both "in the table doing
 * nothing" and "named by accept" is what the two dead entries are for. */
#define PICK1_STATE  0x0011B32AUL
#define PICK1_HELD   0x0011B32CUL

u8 pick_screen_1(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG16(PICK1_STATE) = 0x0000;
    }

    if (touch_hit(0x0001, 0x000B, &value, &index) != 0x03) return 0x00;

    if (value != 0x0004 && value != 0x000B && hitbox_kind(value) != 0x01) {
        pick_box_light(PICK1_STATE, value);
    }

    if ((u16)(value - 2) > 0x0009) return 0x00;

    switch (value) {
    case 0x0002: pick_take(PICK1_HELD, 0x30); break;
    case 0x0003: pick_take(PICK1_HELD, 0x34); break;
    case 0x0006: pick_take(PICK1_HELD, 0x5C); break;
    case 0x0008: pick_take(PICK1_HELD, 0x78); break;
    case 0x0009: pick_take(PICK1_HELD, 0x7C); break;
    case 0x000A: pick_take(PICK1_HELD, 0x80); break;

    case 0x0005:                /* in the table, and doing nothing */
    case 0x0007:
        break;

    case 0x0004: {
        const u16 held = REG16(PICK1_STATE);

        message_show_held(index);
        if      (held == 0x0001) screen_switch(0x3B, 0x01, 0x00);
        else if (held == 0x0005) screen_switch(0x3C, 0x01, 0x00);
        else if (held == 0x0007) screen_switch(0x3D, 0x01, 0x00);
        else if (held != 0x0000) {
            REG32(0x001161B8UL) = REG32(PICK1_HELD);
            screen_switch(0x3A, 0x01, 0x00);
        }
        break;
    }

    case 0x000B:
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        message_show_held(index);
        screen_switch(0x0E, 0x01, 0x00);
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'21B3DE. The main menu: fifteen boxes, most of them a screen change.
 *
 * Three things happen before the hit test. Two keys held together with a
 * third input low clears the pattern queue out to flash and says so -- the
 * only place in the application that offers it -- and leaving for screen
 * H'77 clears the panel code. Both run on every pass.
 *
 * The panel code is set from the low byte of the box value before the table
 * is reached, so it is set even for a value the table does not cover.
 *
 * Most handlers are the same three instructions -- put the message up, go to
 * a screen -- but the two that lead to H'1F and H'22 pass 0 for "remember"
 * where the rest pass 1, and boxes 3, 4, 9 and 15 put the message up and do
 * nothing else. Box H'0C is the odd one: no message at all, and it toggles
 * bit 7 of H'FFFEC1 with the box's own light.
 *
 * The H'02 return from the hit test -- a release -- is what puts box H'0C
 * back down again. */
static void main_menu_goto(u16 index, u8 screen, u8 remember)
{
    message_show_held(index);
    screen_switch(screen, 0x01, remember);
}

u8 main_menu_screen(void)
{
    u16 value = 0, index = 0;
    u8  hit;

    if ((REG8(0x00FFFEDBUL) & 0x04) && (REG8(0x00FFFEDCUL) & 0x04) &&
        (REG8(0x00FFFEDDUL) & 0x10)) {
        sew_picture_box();
        FLASH_BUSY |= 0x20;
        queue_clear_to_flash();
        FLASH_BUSY &= (u8)~0x20;
        message_show(0x001D);
        REG8(0x0011A17DUL) = 0x01;
    }

    if (screen_leave_check(&value, 0x00) == 0x03 && value == 0x0077) {
        REG8(0x00FFFEC5UL) = 0x00;
    }

    hit = touch_hit(0x0001, 0x000F, &value, &index);

    if (hit != 0x03) {
        if (hit == 0x02 && hitbox_kind(0x000C) == 0x01) {
            hitbox_set_state(0x000C, 0x000C, 0x00, 0);
            MACHINE_FLAGS &= (u8)~0x80;
        }
        return 0x00;
    }

    REG8(0x00FFFEC5UL) = (u8)value;

    if ((u16)(value - 1) > 0x000E) return 0x00;

    switch (value) {
    case 0x0001: main_menu_goto(index, 0x18, 0x01); break;
    case 0x0002: main_menu_goto(index, 0x19, 0x01); break;
    case 0x0005: main_menu_goto(index, 0x1D, 0x01); break;
    case 0x0006: main_menu_goto(index, 0x00, 0x01); break;
    case 0x0007: main_menu_goto(index, 0x1C, 0x01); break;
    case 0x0008: main_menu_goto(index, 0x1A, 0x01); break;
    case 0x000A: main_menu_goto(index, 0x1B, 0x01); break;
    case 0x000B: main_menu_goto(index, 0x1E, 0x01); break;

    /* these two do not remember the screen they are leaving */
    case 0x000D: main_menu_goto(index, 0x1F, 0x00); break;
    case 0x000E: main_menu_goto(index, 0x22, 0x00); break;

    case 0x0003:
    case 0x0004:
    case 0x0009:
    case 0x000F:
        message_show_held(index);
        break;

    case 0x000C:
        if (hitbox_kind(0x000C) != 0) break;
        hitbox_set_state(0x000C, 0x000C, 0x01, 0);
        /* bit 7 cleared and set again, which is what the original does */
        MACHINE_FLAGS = (u8)((MACHINE_FLAGS & 0x7F) | 0x80);
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'216DE0. The three "are you sure" screens, put up the same way H'216D6C
 * puts up a message: held off while one is already showing, while the splash
 * is up, while a screen change is still settling, and while the machine is
 * sewing.
 *
 * The first two remember the screen being left in slot 4 so they can go back
 * to it; the third does not, and is the only one that cannot be answered. */
void dialog_show(u16 which)
{
    if (REG8(0x0011A179UL) == 0) return;
    if (REG8(0x0011A173UL) != 0) return;
    if (REG8(0x0011B0A8UL) != 0) return;
    if (REG8(0x00114DC6UL) & 0x80) return;

    REG8(0x0011A179UL) = 0x00;

    if (which == 0x0001) {
        screen_remember(0x04);
        screen_switch(0x48, 0x01, 0x00);
    } else if (which == 0x0002) {
        screen_remember(0x04);
        screen_switch(0x49, 0x01, 0x00);
    } else if (which == 0x0003) {
        screen_switch(0x4D, 0x01, 0x00);
    }
}

/* H'21C19C. The settings menu: thirteen boxes, ten of them a screen change.
 *
 * H'11A17A is the "something was changed" flag: finding it set on the way in
 * writes the whole settings block out to flash, says so, and raises
 * H'11A1B4 so that box H'0D knows to go back to the screen the change came
 * from rather than to the remembered one.
 *
 * Box H'0C is the odd one -- it has no screen number of its own, so the one
 * left in the register from the slot argument is used, which is screen 1.
 * That is in the original and is reproduced; it looks like an omission
 * rather than a choice, but it is what the machine does. */
static void settings_menu_goto(u8 screen)
{
    screen_switch(screen, 0x01, 0x00);
}

u8 settings_menu_screen(u8 first_pass)
{
    u16 value = 0, index = 0;

    if (first_pass != 0) {
        screen_stack_push();
        REG8(0x0011B0A9UL) = 0x01;
    }

    if (REG8(0x0011A17AUL) != 0) {
        sew_picture_box();
        settings_save(0x00);
        REG8(0x0011A17AUL) = 0x00;
        message_show(0x001D);
        REG8(0x0011A17DUL) = 0x01;
        REG8(0x0011A1B4UL) = 0x01;
    }

    if (touch_hit(0x0001, 0x000D, &value, &index) != 0x03) return 0x00;
    message_show_held(index);

    if ((u16)(value - 1) > 0x000C) return 0x00;

    switch (value) {
    case 0x0001:
        settings_menu_goto(0x44);
        REG8(0x0011A178UL) = 0x01;
        REG8(0x0011A1B4UL) = 0x01;
        break;

    case 0x0002: settings_menu_goto(0x2E); break;
    case 0x0003: settings_menu_goto(0x2B); break;
    case 0x0004: settings_menu_goto(0x29); break;
    case 0x0005: settings_menu_goto(0x2A); break;
    case 0x0006: settings_menu_goto(0x2C); break;
    case 0x0007: settings_menu_goto(0x2F); break;
    case 0x0009: settings_menu_goto(0x28); break;
    case 0x000A: settings_menu_goto(0x4A); break;
    case 0x000C: settings_menu_goto(0x01); break;   /* see above */

    case 0x0008: dialog_show(0x0001); break;

    case 0x000B: break;             /* in the table, and doing nothing */

    case 0x000D:
        screen_stack_pop();
        screen_mark_repaint(REG8(0x0011A16CUL));
        if (REG8(0x0011A1B4UL) != 0) {
            screen_switch(REG8(0x0011A16CUL), 0x01, 0x00);
            REG16(0x0011B108UL) = REG16(0x0011B116UL);
            REG16(0x0011B10AUL) = list_page_start();
            REG8(0x0011A1B4UL) = 0x00;
        } else {
            screen_from_slot(0x03);
        }
        break;

    default:
        break;
    }
    return 0x00;
}

/* H'214D24. The little picture at the top left of the screen, and the "F"
 * beside it.
 *
 * A null picture means the box is cleared instead of blitted. The letter is
 * a one-character string in the application's own image at H'250ADD, drawn
 * right-aligned through the small font at H'119DE6 -- one of the few places
 * anything reads a string constant out of the code region. */
void module_letter_box(u32 picture)
{
    if (picture == 0) {
        draw_rect(0x0048, 0x0002, 0x006A, 0x0024, LCD_FRAME_A, 0x00, 0x01);
    } else {
        bitmap_draw(0x0048, 0x0002, 0x006A, 0x0024,
                    (const u8 *)picture, LCD_FRAME_A);
    }

    text_draw((const char *)0x00250ADDUL, 0x0049, 0x001E, 0x004C, 0x0023,
              0x0001, 0x00, (const u8 *)0x00119DE6UL);
}

/* ---- the panel fields ---------------------------------------------------
 * H'214990. Fourteen little readouts along the top of the screen, each one
 * a few bits of a port latched into its own word at H'11B2FC upwards. A
 * field is only redrawn when its bits have changed since last time, which is
 * what the fourteen words are for.
 *
 * Ten of them keep the value and draw nothing -- the drawing is somebody
 * else's job and the word is only there to be compared against. The other
 * four put a picture in the box at the top left: three index a table of
 * their own by the value, and the fourth picks between two fixed pictures by
 * whether the bit is set at all.
 *
 * Called with [fresh] set, it puts all fourteen words to H'FFFF so that the
 * next pass redraws everything, and draws the picture for [which] out of the
 * icon table instead of doing any of the above.
 */
static void panel_field_plain(u16 v, u32 slot)
{
    if (REG16(slot) != v) REG16(slot) = v;
}

static void panel_field_pic(u16 v, u32 slot, u32 table)
{
    if (REG16(slot) == v) return;
    module_letter_box(REG32(table + (u32)(long)(short)(u16)((u16)(v << 2))));
    REG16(slot) = v;
}

void panel_field_update(u16 which, u8 fresh)
{
    if (fresh != 0) {
        REG16(0x0011B2FCUL) = 0xFFFF;
        REG16(0x0011B2FEUL) = 0xFFFF;
        REG16(0x0011B300UL) = 0xFFFF;
        REG16(0x0011B302UL) = 0xFFFF;
        REG16(0x0011B304UL) = 0xFFFF;
        REG16(0x0011B306UL) = 0xFFFF;
        REG16(0x0011B308UL) = 0xFFFF;
        REG16(0x0011B30AUL) = 0xFFFF;
        REG16(0x0011B30CUL) = 0xFFFF;
        REG16(0x0011B30EUL) = 0xFFFF;
        REG16(0x0011B310UL) = 0xFFFF;
        REG16(0x0011B312UL) = 0xFFFF;
        REG16(0x0011B314UL) = 0xFFFF;
        REG16(0x0011B316UL) = 0xFFFF;

        if (which != 0) {
            module_letter_box(REG32(0x001158CEUL +
                (u32)(long)(short)(u16)((u16)(which << 2))));
        } else {
            module_letter_box(0);
        }
        return;
    }

    if (which == 0) return;

    switch (which) {
    case 0x0001:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x40), 0x0011B304UL);
        break;
    case 0x0002:
        panel_field_plain((u16)(REG8(0x00FFFEF6UL) & 0x10), 0x0011B306UL);
        break;
    case 0x0003:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x10), 0x0011B308UL);
        break;
    case 0x0004:
        panel_field_pic((u16)(REG8(0x00FFFEF9UL) & 0x0F),
                        0x0011B302UL, 0x00115852UL);
        break;
    case 0x0005:
        panel_field_plain((u16)(REG8(0x00FFFEF6UL) & 0x40), 0x0011B300UL);
        break;
    case 0x0006:
        panel_field_plain((u16)(REG8(0x00FFFEF6UL) & 0x80), 0x0011B30AUL);
        break;
    case 0x0007:
        panel_field_pic((u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F),
                        0x0011B30CUL, 0x0011587AUL);
        break;
    case 0x0008:
        panel_field_pic((u16)(REG8(0x00FFFEF6UL) & 0x0F),
                        0x0011B2FEUL, 0x0011582EUL);
        break;
    case 0x0009:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x08), 0x0011B30EUL);
        break;
    case 0x000A:
        panel_field_plain((u16)(REG8(0x00FFFEF5UL) & 0x80), 0x0011B310UL);
        break;
    case 0x000C:
        panel_field_pic((u16)(REG8(0x00FFFEF5UL) & 0x03),
                        0x0011B2FCUL, 0x0011581EUL);
        break;

    case 0x0044: {
        const u16 v = (u16)(REG8(0x00FFFEF5UL) & 0x04);

        if (REG16(0x0011B312UL) != v) {
            module_letter_box(v != 0 ? 0x0034E55CUL : 0x0034E5EDUL);
            REG16(0x0011B312UL) = v;
        }
        break;
    }

    case 0x0046:
        panel_field_pic((u16)REG8(0x00FFFEFDUL),
                        0x0011B314UL, 0x0011589AUL);
        break;
    case 0x0047:
        panel_field_plain((u16)(REG8(0x00FFFEF8UL) & 0x01), 0x0011B316UL);
        break;

    default:
        break;
    }
}

/* ---- the panel switches -------------------------------------------------
 * H'214DD4. Twenty fields of the machine's own state, reached by key, and
 * three things that can be done to each: cleared, stepped on, or read.
 *
 * [clear] wins over [step], and with neither the current value comes back --
 * so the same call reads the field or changes it depending on two flags. A
 * key the table does not know answers H'FFFF, and so does key H'14: its
 * table entry points straight at the "not found" tail, which is how a key
 * that exists is made to look like one that does not.
 *
 * Six of the twenty are a single bit toggled in place. The rest each have
 * their own arithmetic -- a count that wraps at nine, one that wraps at
 * three, one that runs 2,3,4,5 and back to 0 with the configuration byte
 * deciding where it wraps, and so on -- and those are written out one by
 * one because that is what they are.
 *
 * [box] is the second argument: a message number to most of them and a box
 * index to key H'47, which asks whether the box is greyed before it does
 * anything.
 */
static u16 panel_bit(u32 port, u8 mask, u8 step, u8 clear)
{
    if (clear != 0) {
        REG8(port) &= (u8)~mask;
        return 0x0000;
    }
    if (step != 0) {
        REG8(port) ^= mask;
        hold_start(0x0064);
        return 0x0000;
    }
    return (u16)(REG8(port) & mask);
}

u16 panel_switch(u8 key, u16 box, u8 step, u8 clear)
{
    switch (key) {
    case 0x01:
        if (step != 0) {
            REG8(0x00FFFEF5UL) ^= 0x40;
            hold_start(0x0064);
        }
        return 0x0000;

    case 0x02: return panel_bit(0x00FFFEF6UL, 0x10, step, clear);
    case 0x03: return panel_bit(0x00FFFEF5UL, 0x10, step, clear);
    case 0x05: return panel_bit(0x00FFFEF6UL, 0x40, step, clear);
    case 0x06: return panel_bit(0x00FFFEF6UL, 0x80, step, clear);
    case 0x09: return panel_bit(0x00FFFEF5UL, 0x08, step, clear);
    case 0x0A: return panel_bit(0x00FFFEF5UL, 0x80, step, clear);

    case 0x04:
        if (clear != 0) {
            REG8(0x00FFFEF9UL) &= 0xF0;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)(REG8(0x00FFFEF9UL) & 0x0F);
            v = (u16)((v == 0x0009) ? 0x0000 : (u16)(v + 1));
            REG8(0x00FFFEF9UL) =
                (u8)((REG8(0x00FFFEF9UL) & 0xF0) | (u8)v);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF9UL) & 0x0F);

    case 0x07:
        if (clear != 0) {
            REG8(0x00FFFEF9UL) &= 0x0F;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F);
            if      (v == 0x0005) v = 0x0000;
            else if (v == 0x0000) v = 0x0002;
            else                  v = (u16)(v + 1);
            REG8(0x00FFFEF9UL) =
                (u8)((u8)((u8)v << 4) | (u8)(REG8(0x00FFFEF9UL) & 0x0F));
            return 0x0000;
        }
        return (u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F);

    case 0x08:
        if (clear != 0) {
            REG8(0x00FFFEF6UL) &= 0xF0;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)(REG8(0x00FFFEF6UL) & 0x0F);
            if      (v == 0x0008 && CONFIG_BLOCK == 0xB4) v = 0x0000;
            else if (v == 0x0005 && CONFIG_BLOCK == 0xAA) v = 0x0000;
            else if ((short)v >= 0x0002)                  v = (u16)(v + 1);
            else                                          v = 0x0002;
            REG8(0x00FFFEF6UL) =
                (u8)((REG8(0x00FFFEF6UL) & 0xF0) | (u8)v);
            return 0x0000;
        }
        if ((u8)(REG8(0x00FFFEF6UL) & 0x0F) < 0x02) return 0x0000;
        return (u16)(REG8(0x00FFFEF6UL) & 0x0F);

    case 0x0C:
        if (clear != 0) {
            REG8(0x00FFFEF5UL) &= 0xFC;
            return 0x0000;
        }
        if (step != 0) {
            u16 v;

            message_show_held(box);
            v = (u16)(REG8(0x00FFFEF5UL) & 0x03);
            v = (u16)((v == 0x0003) ? 0x0000 : (u16)(v + 1));
            REG8(0x00FFFEF5UL) =
                (u8)((REG8(0x00FFFEF5UL) & 0xFC) | (u8)v);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF5UL) & 0x03);

    case 0x49:
        if (clear != 0) {
            REG8(0x00FFFEF6UL) &= 0xF0;
            return 0x0000;
        }
        if (step != 0) {
            if ((u8)(REG8(0x00FFFEF6UL) & 0x0F) == 0x01) {
                REG8(0x00FFFEF6UL) &= 0xF0;
            } else {
                REG8(0x00FFFEF6UL) =
                    (u8)((REG8(0x00FFFEF6UL) & 0xF0) | 0x01);
            }
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)(((u8)(REG8(0x00FFFEF6UL) & 0x0F) == 0x01) ? 1 : 0);

    case 0x47:
        if (clear != 0) {
            REG8(0x00FFFEF8UL) &= (u8)~0x01;
            return 0x0000;
        }
        if (step != 0) {
            if (hitbox_kind(box) == 0x05) return 0x0000;
            if (REG8(0x00114DC6UL) & 0x80) return 0x0000;
            REG8(0x00FFFEF8UL) ^= 0x01;
            REG8(0x00FFFEFDUL) = (u8)(REG8(0x00FFFEF8UL) & 0x01);
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF8UL) & 0x01);

    case 0x44:
        if (clear != 0) {
            REG8(0x00FFFEF5UL) |= 0x04;      /* set, where the rest clear */
            return 0x0000;
        }
        if (step != 0) {
            REG8(0x00FFFEF5UL) ^= 0x04;
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)((REG8(0x00FFFEF5UL) & 0x04) ? 0 : 1);

    case 0x46:
        if (clear != 0) {
            REG8(0x00FFFEFDUL) = 0x00;
            return 0x0000;
        }
        if (step != 0) {
            if (REG8(0x00FFFEFDUL) == 0x02) REG8(0x00FFFEFDUL) = 0x00;
            else REG8(0x00FFFEFDUL) = (u8)(REG8(0x00FFFEFDUL) + 1);
            return 0x0000;
        }
        return (u16)((REG8(0x00FFFEFDUL) == 0x01) ? 0 : 1);

    case 0x6E:
        if (step != 0) {
            if (REG8(0x00FFFEEAUL) != 0) {
                REG8(0x00FFFEEAUL) = (u8)(REG8(0x00FFFEEAUL) - 1);
            }
            return 0x0000;
        }
        return (u16)REG8(0x00FFFEEAUL);

    case 0x6F:
        if (step != 0) {
            if (REG8(0x00FFFEEAUL) < 0x0A) {
                REG8(0x00FFFEEAUL) = (u8)(REG8(0x00FFFEEAUL) + 1);
            }
            return 0x0000;
        }
        return (u16)REG8(0x00FFFEEAUL);

    case 0x76:
        if (step != 0) {
            REG8(0x00FFFEF6UL) ^= 0x20;
            if (REG8(0x0011A169UL) != 0x18) {
                u8 k = (u8)(REG8(0x00FFFEF6UL) & 0x20);
                u8 i;

                for (i = 0; i < 3; i++) k = (u8)((u8)(k << 1) | (u8)(k >> 7));
                k = (u8)(k & 0x07);

                bitmap_draw(0x002C, 0x000B, 0x0030, 0x0019,
                            (const u8 *)REG32(0x00115892UL +
                                (u32)(long)(short)(u16)((u16)(k << 2))),
                            LCD_FRAME_A);
            }
            hold_start(0x0064);
            return 0x0000;
        }
        return (u16)(REG8(0x00FFFEF6UL) & 0x20);

    case 0x7B:
        if (step != 0 && !(REG8(0x00FFFEF8UL) & 0x80)) {
            REG8(0x00FFFEF8UL) |= 0x80;
        }
        return 0x0000;

    case 0x14:                  /* a key that exists and answers "no key" */
    default:
        break;
    }
    return 0xFFFF;
}

/* H'2136A6. Whether anything in a box's list is away from its default, and
 * the little indicator box lit or put out to say so.
 *
 * The box at [box] carries a list at +H'0C -- a count and then that many
 * words, the low byte of each a panel key. Every key is read through
 * H'214DD4 and a positive answer means that field has been changed.
 *
 * Which box is the indicator depends on the screen: H'17 on five of them,
 * H'16 on two more and H'0A on four, and on any other screen the routine
 * does nothing at all.
 *
 * The walk happens only when H'11A17B says the screen has just been set up.
 * It has two halves. The first runs round the list from one past the last
 * box's value, wrapping at the count, and stops when it comes back to this
 * box's own value -- so a value outside 1..count never stops it. The second
 * walks the boxes from [box] to [last] and asks the same question of each,
 * but only counts an answer when the box has been handed over (style 4).
 *
 * H'11A1A4 carries the answer between the walk and the drawing, which is
 * why the drawing can run on a pass that did no walking. Bit 7 of H'FFFEE5
 * decides whether "something changed" lights the box or leaves it alone. */
void panel_any_set(u16 box, u16 last)
{
    const u32 entry = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    const u32 list  = REG32(entry + 0x0C);
    const u16 count = REG16(list);
    const u16 mine  = REG16(entry + 0x08);
    const u16 from  = REG16(HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * last) + 0x08);
    const u8  screen = REG8(0x0011A169UL);
    u16 mark;
    short i;

    if (screen == 0x02 || screen == 0x07 || screen == 0x18 ||
        screen == 0x30 || screen == 0x45) {
        mark = 0x0017;
    } else if (screen >= 0x03 && screen <= 0x04) {
        mark = 0x0016;
    } else if (screen >= 0x33 && screen <= 0x36) {
        mark = 0x000A;
    } else {
        return;
    }

    if (REG8(0x0011A17BUL) != 0) {
        REG8(0x0011A17BUL) = 0x00;

        for (i = (short)(from + 1); ; i++) {
            if (i > (short)count) i = 0x0001;
            if ((u16)i == mine) break;
            if ((short)panel_switch(REG8(list + (u32)(long)(short)
                    (u16)((u16)i << 1) + 1), 0x0000, 0x00, 0x00) > 0) {
                REG8(0x0011A1A4UL) = 0x01;
                goto done;
            }
        }

        for (i = (short)box; i <= (short)last; i++) {
            const u16 v = REG16(HITBOX_TABLE +
                (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i) + 0x08);

            if ((short)panel_switch(REG8(list + (u32)(long)(short)
                    (u16)((u16)(v << 1)) + 1), 0x0000, 0x00, 0x00) > 0) {
                if (hitbox_style((u16)i) == 0x04) {
                    REG8(0x0011A1A4UL) = 0x01;
                    goto done;
                }
            }
        }
        REG8(0x0011A1A4UL) = 0x00;
    }

done:
    if (REG8(0x0011A1A4UL) != 0 && (REG8(0x00FFFEE5UL) & 0x80)) {
        if (hitbox_kind(mark) == 0x00) hitbox_set_state(mark, mark, 0x01, 0);
    } else {
        if (hitbox_kind(mark) == 0x01) hitbox_set_state(mark, mark, 0x00, 0);
    }
}

/* H'200EA4. The first byte of one of the ten-byte records at H'57C6D6, the
 * block settings_save copies out to flash. The index is worked out in
 * sixteen bits and then zero-extended, so a record number past H'1999 wraps
 * rather than running off the end. */
u8 stitch_record_kind(u16 n)
{
    return REG8(0x0057C6D6UL + (u32)(u16)(10 * n));
}

/* ---- the panel strip ----------------------------------------------------
 * H'213B16. The drawing counterpart of H'214DD4: one box of the strip along
 * the top of the screen brought up to date from the field it stands for.
 *
 * Four shapes between the twenty handlers. Six are a lamp -- the box lit or
 * put out by whether a bit is set. Five patch a picture into the icon table
 * at the slot the box's own list entry names and then redraw the box, which
 * is how a box with one entry shows one of several pictures. Five grey the
 * box out or bring it back. The last two are one of each with an extra test.
 *
 * Each remembers what it last drew in its own word at H'11B2D2 upwards and
 * does nothing when nothing has changed. Entering with [fresh] set puts all
 * fifteen words to H'FFFF and does no drawing at all.
 *
 * H'11B0A8 and four screen numbers keep the fields underneath from being
 * updated while a screen change is settling.
 *
 * The three flags the stitch descriptor sets -- taken from its word at +H'14
 * -- decide which boxes are greyed: H'16 and H'17 raise one, H'3C to H'3E
 * raise all three, and anything else lowers them.
 */
static void strip_light(u16 box, u32 slot, u16 v)
{
    if (REG16(slot) == v) return;
    hitbox_set_state(box, box, (u8)(v != 0 ? 0x01 : 0x00), 0);
    REG16(slot) = v;
}

/* The box's list entry names a slot in the icon table; the picture is put
 * there and the box drawn again from it. */
static void strip_patch(u16 box, u32 entry, u32 picture)
{
    const u32 at = REG32(entry + 0x0C) +
        (u32)(long)(short)(u16)((u16)(REG16(entry + 0x08) << 1));

    REG32(0x001158CEUL +
          (u32)(long)(short)(u16)((u16)(REG16(at) << 2))) = picture;
    hitbox_redraw_run(box, box);
}

static void strip_picture(u16 box, u32 entry, u32 slot, u16 v, u32 table)
{
    if (REG16(slot) == v) return;
    strip_patch(box, entry,
                REG32(table + (u32)(long)(short)(u16)((u16)(v << 2))));
    REG16(slot) = v;
}

static void strip_grey(u16 box, u8 want)
{
    if (want != 0) {
        if (hitbox_kind(box) != 0x05) hitbox_set_state(box, box, 0x05, 0);
    } else {
        if (hitbox_kind(box) == 0x05) hitbox_set_state(box, box, 0x00, 0);
    }
}

void panel_strip_box(u8 key, u16 box, u8 fresh)
{
    const u32 entry = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * box);
    const u8  screen = REG8(0x0011A169UL);
    u16 kind;
    u8  a, b, c;

    if (screen != 0x0C && screen != 0x0D && screen != 0x42 &&
        screen != 0x18 && REG8(0x0011B0A8UL) == 0) {
        panel_field_update(REG16(0x0057EFC4UL), fresh);
    }

    if (fresh != 0) {
        REG16(0x0011B2D2UL) = 0xFFFF;
        REG16(0x0011B2D4UL) = 0xFFFF;
        REG16(0x0011B2EEUL) = 0xFFFF;
        REG16(0x0011B2D6UL) = 0xFFFF;
        REG16(0x0011B2D8UL) = 0xFFFF;
        REG16(0x0011B2DAUL) = 0xFFFF;
        REG16(0x0011B2DCUL) = 0xFFFF;
        REG16(0x0011B2DEUL) = 0xFFFF;
        REG16(0x0011B2E0UL) = 0xFFFF;
        REG16(0x0011B2E2UL) = 0xFFFF;
        REG16(0x0011B2E4UL) = 0xFFFF;
        REG16(0x0011B2E6UL) = 0xFFFF;
        REG16(0x0011B2E8UL) = 0xFFFF;
        REG16(0x0011B2EAUL) = 0xFFFF;
        REG16(0x0011B2ECUL) = 0xFFFF;
        return;            /* the fresh path branches past the tail below */
    }

    kind = REG16(ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x14);

    if (kind == 0x0016) {
        a = 0x01; b = 0x00; c = 0x00;
    } else if (kind == 0x0017) {
        c = 0x01; a = 0x01; b = 0x00;
        REG8(0x00FFFEFDUL) = 0x00;
    } else if ((short)kind >= 0x003C && (short)kind <= 0x003E) {
        b = 0x01; c = 0x01; a = 0x01;
        REG8(0x00FFFEFDUL) = 0x00;
    } else {
        b = 0x00; c = 0x00; a = 0x00;
    }

    switch (key) {
    case 0x01: strip_light(box, 0x0011B2DAUL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x40)); break;
    case 0x02: strip_light(box, 0x0011B2DCUL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x10)); break;
    case 0x03: strip_light(box, 0x0011B2DEUL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x10)); break;
    case 0x05: strip_light(box, 0x0011B2D6UL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x40)); break;
    case 0x06: strip_light(box, 0x0011B2E0UL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x80)); break;
    case 0x09: strip_light(box, 0x0011B2E4UL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x08)); break;
    case 0x0A: strip_light(box, 0x0011B2E6UL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x80)); break;

    case 0x04: strip_picture(box, entry, 0x0011B2D8UL,
                   (u16)(REG8(0x00FFFEF9UL) & 0x0F), 0x00115852UL); break;
    case 0x07: strip_picture(box, entry, 0x0011B2E2UL,
                   (u16)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F),
                   0x0011587AUL); break;
    case 0x08: strip_picture(box, entry, 0x0011B2D4UL,
                   (u16)(REG8(0x00FFFEF6UL) & 0x0F), 0x0011582EUL); break;
    case 0x0C: strip_picture(box, entry, 0x0011B2D2UL,
                   (u16)(REG8(0x00FFFEF5UL) & 0x03), 0x0011581EUL); break;
    case 0x46: strip_picture(box, entry, 0x0011B2EAUL,
                   (u16)REG8(0x00FFFEFDUL), 0x0011589AUL); break;

    case 0x44: {
        const u16 v = (u16)(REG8(0x00FFFEF5UL) & 0x04);

        if (REG16(0x0011B2E8UL) != v) {
            strip_patch(box, entry, v != 0 ? 0x0034E55CUL : 0x0034E5EDUL);
            REG16(0x0011B2E8UL) = v;
        }
        break;
    }

    case 0x49: {
        const u16 v = (u16)(REG8(0x00FFFEF6UL) & 0x0F);

        if (REG16(0x0011B2EEUL) != v) {
            hitbox_set_state(box, box, (u8)(v == 0x0001 ? 0x01 : 0x00), 0);
            REG16(0x0011B2EEUL) = v;
        }
        break;
    }

    case 0x12: strip_grey(box, (u8)((REG8(0x00FFFEF8UL) & 0x01) || b)); break;
    case 0x13: strip_grey(box, (u8)((REG8(0x00FFFEF8UL) & 0x01) || a)); break;
    case 0x14: strip_grey(box, (u8)((REG8(0x00FFFEF8UL) & 0x01) || a)); break;

    case 0x4C: {
        const u16 n = (u16)(REG16(0x00FFFEE0UL) +
                            (u16)REG8(0x00FFFEFDUL));

        strip_grey(box, (u8)(stitch_record_kind(n) == 0 ||
                             (REG8(0x00FFFEF8UL) & 0x01)));
        break;
    }

    case 0x4D: {
        const u8 on = (u8)((REG8(0x00FFFEFAUL) & 0x80) ||
                           (REG8(0x00FFFEF7UL) & 0x08));

        strip_grey(box, (u8)(on ? ((REG8(0x00FFFEF8UL) & 0x01) != 0) : 1));
        break;
    }

    case 0x47:
        if (c != 0) {
            if (hitbox_kind(box) != 0x05) {
                hitbox_set_state(box, box, 0x05, 0);
                REG8(0x00FFFEF8UL) &= (u8)~0x01;
            }
        } else {
            if (hitbox_kind(box) == 0x05) {
                hitbox_set_state(box, box, 0x00, 0);
            }
        }
        if (hitbox_kind(box) == 0x05) goto tail;
        strip_light(box, 0x0011B2ECUL, (u16)(REG8(0x00FFFEF8UL) & 0x01));
        break;

    default:
        break;
    }

tail:
    if (REG8(0x00FFFEE2UL) & 0x04) {
        if ((u8)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F) != 0) {
            REG8(0x00FFFEF9UL) &= 0x0F;
        }
    }
}

/* H'2135FE. The whole panel strip brought up to date.
 *
 * With [fresh] set it makes one call with everything zeroed, which is what
 * puts the fifteen remembered words to H'FFFF, and raises H'11A17B so that
 * H'2136A6 walks the list next time. Otherwise it runs the boxes from
 * [first] to [last], and for each one works out which panel key that box
 * stands for: the box's own value indexes its list, and the low byte of the
 * word found there is the key.
 *
 * The original copies the whole H'12-byte hitbox entry into a local frame
 * first and reads the value and the list pointer out of the copy. Nothing
 * changes the entry in between, so the copy makes no difference -- but it is
 * what the original does and the fields are read from the copy here too.
 *
 * Either way it finishes by asking H'2136A6 whether anything in the run is
 * away from its default. */
void panel_strip_draw(u16 first, u16 last, u8 fresh)
{
    if (fresh != 0) {
        panel_strip_box(0x00, 0x0000, 0x01);
        REG8(0x0011A17BUL) = 0x01;
    } else {
        short i;

        for (i = (short)first; i <= (short)last; i++) {
            const u32 e = HITBOX_TABLE +
                (u32)(long)(short)(u16)(HITBOX_STRIDE * (u16)i);
            u16 copy[9];
            u32 list;
            u16 value;
            int n;

            for (n = 0; n < 9; n++) copy[n] = REG16(e + (u32)(2 * n));

            value = copy[4];                              /* the +H'08 field */
            list  = ((u32)copy[6] << 16) | (u32)copy[7];  /* the +H'0C field */

            panel_strip_box(REG8(list +
                (u32)(long)(short)(u16)((u16)(value << 1)) + 1),
                (u16)i, 0x00);
        }
    }

    panel_any_set(first, last);
}

/* ---- which strip the screen wears ---------------------------------------
 * H'21F9D0. The pattern's category decides which of six lists the panel
 * strip is filled from, and H'11A196 remembers which one is up so the work
 * is done once per change rather than once per pass.
 *
 * The category less three indexes a table of twenty-three entries covering
 * seven bodies, so most categories share a strip. Each body is the same
 * four steps -- fill the run from the list, draw it fresh, remember the
 * list, and say the screen wants laying out again -- with its own list, its
 * own run of boxes, and its own idea of which panel bits to put back.
 *
 * The first two bodies are the odd ones. H'21FA6C looks at the pattern's
 * own kind first and picks between two lists on that; H'21FC26 is its
 * second half on its own. Both finish by clearing H'FFFEFD when the pedal
 * bit is down, which none of the others do.
 */
static u16 strip_pattern_kind(void)
{
    return REG16(ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x14);
}

static void strip_switch(u16 first, u16 last, u32 list)
{
    hitbox_fill_from_list(first, last, 0x0001, list);
    panel_strip_draw(first, last, 0x01);
    REG32(0x0011A196UL) = list;
    REG8(0x00FFFEFDUL) = 0x00;
    REG8(0x00FFFEF8UL) &= (u8)~0x01;
    REG16(0x0011B10CUL) = 0x0001;
}

/* The list H'57EEF8 wants six panel bits put back as well, and zeroes the
 * whole of H'FFFEF9 -- written in the original as "and with H'0F, then and
 * with H'F0", which can only give nothing. */
static void strip_switch_eef8(void)
{
    hitbox_fill_from_list(0x0010, 0x0015, 0x0001, 0x0057EEF8UL);
    panel_strip_draw(0x0010, 0x0015, 0x01);
    REG32(0x0011A196UL) = 0x0057EEF8UL;
    REG8(0x00FFFEF9UL) = (u8)((u8)(REG8(0x00FFFEF9UL) & 0x0F) & 0xF0);
    REG8(0x00FFFEF6UL) &= (u8)~0x80;
    REG8(0x00FFFEF6UL) &= (u8)~0x40;
    REG8(0x00FFFEF5UL) &= (u8)~0x10;
    REG8(0x00FFFEF6UL) &= (u8)~0x10;
    REG8(0x00FFFEF5UL) &= (u8)~0x08;
    REG8(0x00FFFEF5UL) &= (u8)~0x80;
    REG16(0x0011B10CUL) = 0x0001;
}

static void strip_pedal_settle(void)
{
    if (REG8(0x00FFFEFDUL) != 0 && !(REG8(0x00FFFEF8UL) & 0x01)) {
        REG8(0x00FFFEFDUL) = 0x00;
    }
}

void panel_strip_choose(void)
{
    const u8 idx = (u8)(REG8(ITEM_TABLE +
        (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x17) + 0xFD);

    if (idx > 0x16) return;

    switch (idx) {
    case 0x00:
        if (strip_pattern_kind() != 0x0016 &&
            strip_pattern_kind() != 0x0017) {
            if (REG32(0x0011A196UL) != 0x0057EED6UL) {
                strip_switch(0x0010, 0x0015, 0x0057EED6UL);
            }
        }
        if (strip_pattern_kind() == 0x0016 ||
            strip_pattern_kind() == 0x0017) {
            if (REG32(0x0011A196UL) != 0x0057EEF8UL) strip_switch_eef8();
            strip_pedal_settle();
        }
        break;

    case 0x01:
        if (REG32(0x0011A196UL) != 0x0057EEF8UL) strip_switch_eef8();
        strip_pedal_settle();
        break;

    case 0x02: case 0x04: case 0x05: case 0x07:
    case 0x08: case 0x0A: case 0x0C:
        if (REG32(0x0011A196UL) != 0x0057EF1AUL) {
            strip_switch(0x0010, 0x0015, 0x0057EF1AUL);
        }
        break;

    case 0x03: case 0x06: case 0x09: case 0x0B:
        if (REG32(0x0011A196UL) != 0x0057EF80UL) {
            strip_switch(0x0010, 0x0015, 0x0057EF80UL);
        }
        break;

    case 0x0D: case 0x0E:
        if (REG32(0x0011A196UL) != 0x0057EF5EUL) {
            hitbox_fill_from_list(0x000D, 0x0012, 0x0001, 0x0057EF5EUL);
            panel_strip_draw(0x000D, 0x0012, 0x01);
            REG32(0x0011A196UL) = 0x0057EF5EUL;
            REG8(0x00FFFEFDUL) = 0x00;
            REG8(0x00FFFEF9UL) &= 0x0F;
            REG8(0x00FFFEF8UL) &= (u8)~0x01;
            REG16(0x0011B10CUL) = 0x0001;
        }
        break;

    case 0x0F: case 0x10: case 0x11: case 0x12:
    case 0x14: case 0x15: case 0x16:
        if (REG32(0x0011A196UL) != 0x0057EF3CUL) {
            strip_switch(0x0019, 0x001E, 0x0057EF3CUL);
        }
        break;

    case 0x13:
        if (REG32(0x0011A196UL) != 0x0057EFA2UL) {
            /* this one puts the two bits back before it fills, not after */
            REG8(0x00FFFEFDUL) = 0x00;
            REG8(0x00FFFEF8UL) &= (u8)~0x01;
            hitbox_fill_from_list(0x0019, 0x001E, 0x0001, 0x0057EFA2UL);
            panel_strip_draw(0x0019, 0x001E, 0x01);
            REG32(0x0011A196UL) = 0x0057EFA2UL;
            REG16(0x0011B10CUL) = 0x0001;
        }
        break;

    default:
        break;
    }
}

/* H'2195F2. Seven pictures to choose between, and four keys that are not
 * pictures at all.
 *
 * The screen answers a press or, failing that, a screen change asked for
 * elsewhere -- H'21F940 writes its answer into the same local the hit test
 * uses, so one key search covers both. The keys are searched rather than
 * indexed, and the handlers are stored back to front behind H'2196B6, the
 * same reversed-table idiom H'214DD4 uses.
 *
 * The seven picture keys, H'88 to H'8E, each blit their own bitmap into the
 * one box and leave their own number in the bottom three bits of H'FFFEFA.
 * The first of them ands those bits away and ors nothing back, which is the
 * only reason it reads as a separate case rather than as "or with zero".
 *
 * Four keys hand straight to H'214DD4 as a step, and the low byte of the box
 * value is the panel key -- so the same word is both the box's value here
 * and a key over there. */
static void picture_choice(u16 index, u32 picture, u8 code)
{
    message_show_held(index);
    bitmap_draw(0x0045, 0x0057, 0x0086, 0x0098,
                (const u8 *)picture, LCD_FRAME_A);
    REG8(0x00FFFEFAUL) = (u8)((u8)(REG8(0x00FFFEFAUL) & 0xF8) | code);
}

u8 picture_choice_screen(void)
{
    u16 value = 0, index = 0;
    u8  hit = touch_hit(0x0001, 0x000B, &value, &index);

    if (hit != 0x03) hit = screen_leave_check(&value, 0x00);
    if (hit != 0x03) return 0x00;

    switch (value) {
    case 0x0008:
    case 0x000C:
    case 0x006E:
    case 0x006F:
        (void)panel_switch((u8)value, index, 0x01, 0x00);
        break;

    case 0x000B:
        message_show_held(index);
        screen_remember(0x01);
        screen_switch(0x3F, 0x01, 0x00);
        break;

    case 0x001A:
        message_show_held(index);
        if (REG8(0x00114DC6UL) & 0x80) break;
        screen_stack_pop();
        screen_switch(REG8(0x0011B0A7UL), 0x01, 0x00);
        REG8(0x0011B0A9UL) = 0x01;
        break;

    case 0x0088: picture_choice(index, 0x0034F208UL, 0x00); break;
    case 0x0089: picture_choice(index, 0x0034F33CUL, 0x01); break;
    case 0x008A: picture_choice(index, 0x0034F463UL, 0x02); break;
    case 0x008B: picture_choice(index, 0x0034F570UL, 0x03); break;
    case 0x008C: picture_choice(index, 0x0034F69CUL, 0x04); break;
    case 0x008D: picture_choice(index, 0x0034F7C8UL, 0x05); break;
    case 0x008E: picture_choice(index, 0x0034F8D5UL, 0x06); break;

    default:
        break;
    }
    return 0x00;
}

/* ---- the queue's own list ----------------------------------------------
 * H'210784, H'21DD6A and the two little stores below them.
 */

/* H'210784. How many entries at [from] and after it are category 1 -- the
 * ones build_item_list leaves out -- plus one. The end of the run is
 * recomputed on every pass because H'11B11C and H'11B28C can move while the
 * queue is being edited. */
u16 queue_run_length(u16 from)
{
    u16 n = 0x0001;
    short at = (short)from;

    for (;;) {
        const short end = (short)(REG16(0x0011B11CUL) + REG16(0x0011B28CUL));

        at++;
        if (at > end) break;
        if (REG8(ITEM_TABLE +
                 (u32)(long)(short)(u16)(ITEM_STRIDE * (u16)at) + 0x17) != 0x01) {
            break;
        }
        n++;
    }
    return n;
}

/* H'21DD6A. One value inserted into a counted list of words: the tail is
 * shifted up by one and the count goes up with it. The length handed to the
 * move is worked out from the count *before* the insert, so the word being
 * overwritten is included in what is moved. */
void list_insert(u16 *list, u16 at, u16 value)
{
    const u32 base = (u32)list;
    const u32 n = (u32)(u16)((u16)(REG16(base) - at + 1) << 1);
    const u32 slot = base + (u32)(long)(short)(u16)((u16)(at << 1));

    mem_move((void *)(slot + 2), (const void *)slot, n);
    REG16(slot) = value;
    REG16(base) = (u16)(REG16(base) + 1);
}

/* H'229054. One byte into the H'0D-byte record at H'11BBB4. */
void beep_record_byte(u16 n, u8 v)
{
    REG8(0x0011BBB4UL + (u32)(long)(short)(u16)(13 * n)) = v;
}

/* H'228C3C. A word split into the two bytes at H'11BBAA, low half first.
 *
 * The high half is read back off the stack rather than out of the register,
 * which is what makes the order look the wrong way round in the listing. */
void beep_pair_store(u16 v)
{
    REG8(0x0011BBAAUL) = (u8)v;
    REG8(0x0011BBABUL) = (u8)(v >> 8);
}

/* ---- one field of a queue record ---------------------------------------
 * The queue is a thousand records of H'0D bytes at H'11BBAA -- the block
 * settings_save writes out to flash in one go -- and thirteen little
 * routines each put one field of one record. They are all the same three
 * steps: work out the record's address, mask the old bits out, or the new
 * ones in.
 *
 * Five take a plain byte and store it whole. The rest each have their own
 * mask and their own shift, and one of them, H'228C90, takes a word and
 * splits it across two bytes -- the ten-bit number the callers pass H'3FF
 * and H'3FE for.
 */
#define QREC(rec) (0x0011BBAAUL + (u32)(long)(short)(u16)(13 * (rec)))

void queue_put_number(u16 rec, u16 v)          /* H'228C90: ten bits, bytes 0-1 */
{
    REG8(QREC(rec) + 0) = (u8)v;
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & 0xFC);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) |
                               (u8)((u8)(v >> 8) & 0x03));
}

void queue_put_mid4(u16 rec, u8 v)             /* H'228CE4: byte 1 bits 2-5 */
{
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & 0xC3);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) |
                               (u8)((u8)(v << 2) & 0x3C));
}

void queue_put_bit6(u16 rec, u8 v)             /* H'228D30: byte 1 bit 6 */
{
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & (u8)~0x40);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) | v);
}

void queue_put_bit7(u16 rec, u8 v)             /* H'228D94: byte 1 bit 7 */
{
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) & (u8)~0x80);
    REG8(QREC(rec) + 1) = (u8)(REG8(QREC(rec) + 1) | v);
}

void queue_put_byte2(u16 rec, u8 v) { REG8(QREC(rec) + 2) = v; }  /* H'228DF8 */
void queue_put_byte3(u16 rec, u8 v) { REG8(QREC(rec) + 3) = v; }  /* H'228E18 */
void queue_put_byte5(u16 rec, u8 v) { REG8(QREC(rec) + 5) = v; }  /* H'228E9E */
void queue_put_byte6(u16 rec, u8 v) { REG8(QREC(rec) + 6) = v; }  /* H'228EDA */
void queue_put_byte10(u16 rec, u8 v) { REG8(QREC(rec) + 10) = v; } /* H'229054 */

void queue_put_low6(u16 rec, u8 v)             /* H'228E38: byte 4 bits 0-5 */
{
    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) & 0xC0);
    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) | (u8)(v & 0x3F));
}

/* H'229074: byte 4 bit 7 -- and only bit 7, though what is or'd in is three
 * bits wide. The value is rotated right three times, so its bottom three
 * bits arrive at the top, and then masked to H'E0. */
void queue_put_top3(u16 rec, u8 v)
{
    u8 k = v;
    u8 i;

    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) & (u8)~0x80);
    for (i = 0; i < 3; i++) k = (u8)((u8)(k >> 1) | (u8)(k << 7));
    REG8(QREC(rec) + 4) = (u8)(REG8(QREC(rec) + 4) | (u8)(k & 0xE0));
}

void queue_put_low2(u16 rec, u8 v)             /* H'228F16: byte 7 bits 0-1 */
{
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) & 0xFC);
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) | v);
}

void queue_put_bit3(u16 rec, u8 v)             /* H'228F7A: byte 7 bit 3 */
{
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) & (u8)~0x08);
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) | (u8)(v >> 1));
}

void queue_put_high4(u16 rec, u8 v)            /* H'228FE0: byte 7 bits 4-7 */
{
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) & 0x0F);
    REG8(QREC(rec) + 7) = (u8)(REG8(QREC(rec) + 7) | (u8)(v << 4));
}

/* H'229100. Six of a pattern's parameters read out of the catalogue and put
 * into a queue record, one getter and one setter each. Every getter is
 * called with its "use the working copy" flag set. */
void queue_put_params(u16 rec, u16 pattern)
{
    queue_put_mid4 (rec, stitch_param_4(pattern, 0x01));
    queue_put_byte2(rec, stitch_param_2(pattern, 0x01));
    queue_put_byte3(rec, stitch_param_1(pattern, 0x01));
    queue_put_low6 (rec, stitch_param_5(pattern, 0x01));
    queue_put_byte5(rec, stitch_param_6(pattern, 0x01));
    queue_put_byte6(rec, stitch_param_7(pattern, 0x01));
}

/* H'2292CC. The first entry's pattern number, one on, put back. */
void queue_number_bump(void)
{
    beep_pair_store((u16)(queue_entry_number_first() + 1));
}

/* H'229198. One queue record filled in from the machine's current state.
 *
 * Two flags at H'11A1E1 and H'11A1E2 stand for "nothing here" and "the end
 * of a group": either one writes the two sentinel numbers H'3FF and H'3FE
 * into the record and into its parameters, clears the flag, and stops. The
 * record is zeroed first either way.
 *
 * Otherwise the pattern number goes in, then the six parameters, then eight
 * more fields straight off the panel ports, and finally the byte H'2290E0
 * answered at the top -- which is why that call comes first and its answer
 * is carried all the way down. */
void queue_record_fill(u16 rec)
{
    const u8 off = queue_entry_offset(rec);

    mem_set(QREC(rec), 0x00, 0x000D);

    if (REG8(0x0011A1E1UL) != 0) {
        queue_put_number(rec, 0x03FF);
        queue_put_params(rec, 0x03FF);
        REG8(0x0011A1E1UL) = 0x00;
        return;
    }

    if (REG8(0x0011A1E2UL) != 0) {
        queue_put_number(rec, 0x03FE);
        queue_put_params(rec, 0x03FE);
        REG8(0x0011A1E2UL) = 0x00;
        return;
    }

    queue_put_number(rec, REG16(0x00FFFEE0UL));
    queue_put_bit6(rec, (u8)(REG8(0x00FFFEF6UL) & 0x40));
    queue_put_bit7(rec, (u8)(REG8(0x00FFFEF6UL) & 0x80));
    queue_put_params(rec, (u16)(REG16(0x00FFFEE0UL) + (u16)off));
    queue_put_low2(rec, (u8)(REG8(0x00FFFEF5UL) & 0x03));
    queue_put_bit3(rec, (u8)(REG8(0x00FFFEF5UL) & 0x10));
    queue_put_high4(rec, (u8)((u8)(REG8(0x00FFFEF9UL) >> 4) & 0x0F));
    queue_put_top3(rec, (u8)(REG8(0x00FFFEF5UL) & 0x04));
    queue_put_byte10(rec, off);
}

/* H'210AB2. Room made in the three lists for a run of entries.
 *
 * H'210784 says how long the run is -- the entry asked for plus every
 * category-1 entry after it -- and the whole run has to fit: adding it to
 * H'11B198 must not take the total past H'3C, and when it would, H'11A18A is
 * raised and nothing is inserted.
 *
 * Otherwise one slot is opened in H'11B212 for the entry itself, and then
 * one slot in each of H'11B198 and H'11B11E for every entry of the run. */
void queue_make_room(u16 at)
{
    const u16 run = queue_run_length(at);
    short i;
    u16 put = at;

    if ((short)(REG16(0x0011B198UL) + run) > 0x003C) {
        REG8(0x0011A18AUL) = 0x01;
        return;
    }

    list_insert((u16 *)0x0011B212UL, (u16)(REG16(0x0011B212UL) + 1), at);
    REG16(0x0011A186UL) = REG16(0x0011B212UL);

    for (i = 1; i <= (short)run; i++) {
        list_insert((u16 *)0x0011B198UL, (u16)(REG16(0x0011B198UL) + 1),
                    (u16)(REG16(0x0011B11CUL) + REG16(0x0011B198UL) + 1));
        list_insert((u16 *)0x0011B11EUL, (u16)(REG16(0x0011B11EUL) + 1), put);
        put++;
    }
}

/* H'228A92. One entry added to the queue.
 *
 * The queue is full at H'3E8 entries and says so with message H'20. Below
 * that the records above the cursor are shifted up by one -- one H'0D-byte
 * record's worth, moved with the ROM's memmove because the two overlap --
 * the number list has a slot opened in it, the new record is filled in from
 * the machine's state, and the picker is redrawn and stepped on by one.
 *
 * The number inserted into the list at H'11B3D8 is the first argument, read
 * back off the stack rather than out of the register it arrived in; the
 * offset stored in the record is the low byte of the second. */
u8 queue_add_entry(u16 number, u16 offset)
{
    const u16 at = REG16(0x0011A1CCUL);

    if ((short)REG16(0x0011B3D8UL) >= 0x03E8) {
        message_show(0x0020);
        return 0x00;
    }

    REG8(0x0011A184UL) = 0x01;

    mem_move((void *)(0x0011BBB7UL + (u32)(long)(short)(u16)(13 * at)),
             (const void *)(0x0011BBAAUL + (u32)(long)(short)(u16)(13 * at)),
             (u32)(long)(short)(u16)
                 (13 * (u16)(queue_entry_number_first() - at + 1)));

    list_insert((u16 *)0x0011B3D8UL, (u16)(at + 1), number);
    queue_put_byte10((u16)(at + 1), (u8)offset);
    REG16(0x0011A1D2UL) = (u16)(REG16(0x0011A1D2UL) + 1);
    queue_number_bump();
    queue_record_fill((u16)(at + 1));
    picker_draw_range((u16)(REG16(0x0011A1C8UL) + 1), (u16)(at + 1),
                      REG16(0x0011A1D2UL));
    picker_forward(0x0001);
    return 0x01;
}

/* ---- taking an entry out again -----------------------------------------
 * The four little readers and the two removers that answer H'21DD6A and
 * H'228A92.
 */

/* H'22B23A. The low byte of the picker position. */
u8 picker_pos_low(void) { return REG8(0x0011A1CDUL); }

/* H'22B202. Whether the picker's first and last are the same position. */
u8 picker_one_only(void)
{
    return (u8)(REG16(0x0011A1D0UL) == REG16(0x0011A1D2UL) ? 1 : 0);
}

/* H'22B21E. Whether the picker is at or before its first position. */
u8 picker_at_start(void)
{
    return (u8)((short)REG16(0x0011A1CCUL) > (short)REG16(0x0011A1D0UL)
                ? 0 : 1);
}

/* H'2292E4. The first entry's number, one off, put back. */
void queue_number_drop(void)
{
    beep_pair_store((u16)(queue_entry_number_first() - 1));
}

/* H'21DD18. One value taken out of a counted list of words -- the mirror of
 * H'21DD6A. The tail comes down by one, the word left at the end is zeroed,
 * and the count comes down with it. The length is worked out from the count
 * before the delete, so the word being removed is part of what moves. */
void list_delete(u16 *list, u16 at)
{
    const u32 base = (u32)list;
    const u16 n = (u16)((u16)(REG16(base) - at) << 1);
    const u32 slot = base + (u32)(long)(short)(u16)((u16)(at << 1));

    mem_copy((u8 *)slot, (const u8 *)(slot + 2), n);
    REG16(base + (u32)(long)(short)(u16)((u16)(REG16(base) << 1))) = 0x0000;
    REG16(base) = (u16)(REG16(base) - 1);
}

/* H'2292FC. One queue record taken out: the records above it come down by
 * one, the record left at the end is zeroed, and the stored number comes
 * down with them.
 *
 * The length of the move is the low word of the product, with no sign
 * extension -- so a position past the first entry's number wraps rather than
 * counting backwards. */
void queue_remove_record(u16 at)
{
    const u16 n = (u16)(13 * (u16)(queue_entry_number_first() - at));

    mem_copy((u8 *)(0x0011BBAAUL + (u32)(long)(short)(u16)(13 * at)),
             (const u8 *)(0x0011BBB7UL + (u32)(long)(short)(u16)(13 * at)), n);
    mem_set(0x0011BBAAUL +
            (u32)(long)(short)(u16)(13 * queue_entry_number_first()),
            0x00, 0x000D);
    queue_number_drop();
}

/* H'20318C. A pattern taken up: its cached flags cleared down to bit 7, its
 * four parameters copied into the live registers, and a handful of state
 * bits put back.
 *
 * The four are copied only when bit 1 of H'114DCA is up *and* the pedal
 * position is not H'FFFF; either failing, the whole set is recomputed from
 * the catalogue instead. */
void pattern_take_up(u16 n)
{
    const u32 flag = 0x000E4010UL + (u32)(u16)((u16)(n << 4));

    REG8(flag) = (u8)(REG8(flag) & 0x80);

    if ((REG8(0x00114DCAUL) & 0x02) && REG16(0x00FFFEFEUL) != 0xFFFF) {
        REG8(0x0011A69EUL) = stitch_param_1(n, 0x01);
        sew_param_a_set(REG8(0x0011A69EUL));
        REG8(0x0011A6A0UL) = stitch_param_2(n, 0x01);
        sew_param_b_set(REG8(0x0011A6A0UL));
        REG8(0x00FFFEEAUL) = stitch_param_4(n, 0x01);
        REG8(0x00FFFEECUL) = stitch_param_5(n, 0x01);
    } else {
        sew_params_for_pattern(n);
        sew_params_publish();
    }

    REG8(0x00114DC7UL) &= (u8)~0x80;
    REG8(0x00FFFEF8UL) &= (u8)~0x20;
    REG8(0x0011A6CFUL) = 0x00;

    if (REG8(0x00114DC6UL) & 0x08) {
        REG8(0x00114DC6UL) &= (u8)~0x08;
        REG8(0x00FFFEFAUL) &= (u8)~0x80;
        REG8(0x00FFFEF7UL) &= (u8)~0x08;
        REG8(0x0011A6AEUL) |= 0x01;
        REG8(0x00114DCDUL) &= (u8)~0x08;
    }
}

/* H'2016F2. Every cached flag in the table cleared down to bit 7, and then
 * the pattern the pedal is on taken up. The loop stops one short of H'400,
 * the same off-by-one item_flags_clear has. */
void pattern_flags_clear_all(void)
{
    const u16 n = (u16)(REG16(0x00FFFEE0UL) + (u16)REG8(0x00FFFEFDUL));
    u16 i;

    for (i = 0; i < 0x03FF; i++) {
        const u32 flag = 0x000E4010UL + (u32)(u16)((u16)(i << 4));

        REG8(flag) = (u8)(REG8(flag) & 0x80);
    }
    pattern_take_up(n);
}

/* H'21C4EC. A screen handed over, with three ways of getting there.
 *
 * While H'11A184 is up the change is only asked for -- dialog 3 puts the
 * question -- and the only preparation done is for screen H'41. Otherwise
 * H'41 and H'12 both go to the screen properly and everything else only
 * makes itself current, which is what leaves the screen change to whoever
 * asked for it. */
void screen_hand_over(u8 screen)
{
    REG8(0x0011B11AUL) = screen;

    if (REG8(0x0011A184UL) != 0) {
        if (REG8(0x0011B11AUL) == 0x41) {
            dialog_backdrop_save(0x01);
            screen_remember(0x01);
            screen_put_away();
        }
        dialog_show(0x0003);
        return;
    }

    if (REG8(0x0011B11AUL) == 0x41) {
        dialog_backdrop_save(0x01);
        screen_remember(0x01);
        screen_switch(REG8(0x0011B11AUL), 0x01, 0x00);
    } else if (REG8(0x0011B11AUL) == 0x12) {
        drawing_reset();
        REG8(0x0011A169UL) = REG8(0x0011B11AUL);
        screen_switch(REG8(0x0011B11AUL), 0x01, 0x00);
    } else {
        drawing_reset();
        REG8(0x0011A169UL) = REG8(0x0011B11AUL);
    }
}

/* H'22A012. The whole queue read back out of flash: the two blocks copied
 * in, the number list rebuilt an entry at a time, and the picker redrawn. */
void queue_reload(void)
{
    short i;

    mem_copy((u8 *)0x0011EE80UL, (const u8 *)0x0057B2D6UL, 0x0400);
    mem_copy((u8 *)0x0011BBAAUL, (const u8 *)0x00578000UL, 0x32D5);

    REG16(0x0011B3D8UL) = queue_entry_number_first();

    for (i = 1; i <= 0x03E8; i++) {
        REG16(0x0011B3D8UL + (u32)(long)(short)(u16)((u16)((u16)i << 1))) =
            queue_entry_number((u16)i);
    }

    picker_rebuild(REG16(0x0011A1CEUL), 0x01, 0x01);
}

/* ---- the queue's ranges -------------------------------------------------
 * H'11EE80 holds H'100 pairs of words -- a first and a last position each --
 * and H'57B2D6 is the copy of them in flash.
 */

/* H'229368. Every range brought into line with the one at H'11A1CE after it
 * has moved: a pair whose two halves are equal is cleared, and a pair that
 * starts after the moved one's old start is shifted by however much the
 * moved one grew. */
void queue_ranges_shift(void)
{
    const u32 at = (u32)(long)(short)(u16)((u16)(REG16(0x0011A1CEUL) << 2));
    const u16 grew = (u16)(REG16(0x0011EE82UL + at) -
                           REG16(0x0057B2D8UL + at));
    const u16 was  = REG16(0x0011EE80UL + at);
    short i;

    for (i = 0; i <= 0x00FF; i++) {
        const u32 e = (u32)(long)(short)(u16)((u16)((u16)i << 2));

        if (REG16(0x0011EE80UL + e) == REG16(0x0011EE82UL + e)) {
            REG16(0x0011EE82UL + e) = 0x0000;
            REG16(0x0011EE80UL + e) = 0x0000;
        } else if ((short)REG16(0x0011EE80UL + e) > (short)was) {
            REG16(0x0011EE80UL + e) = (u16)(REG16(0x0011EE80UL + e) + grew);
            REG16(0x0011EE82UL + e) = (u16)(REG16(0x0011EE82UL + e) + grew);
        }
    }
}

/* H'229468. The queue and its ranges written back out to flash: the range at
 * H'11A1CE takes the picker's own first and last, the rest are brought into
 * line, and both blocks are programmed with the flash-busy bit up. */
void queue_save_ranges(void)
{
    const u32 at = (u32)(long)(short)(u16)((u16)(REG16(0x0011A1CEUL) << 2));

    REG8(0x0011A184UL) = 0x00;
    FLASH_BUSY |= 0x20;
    rom_flash_write((const void *)0x0011BBAAUL, 0x00578000UL, 0x32D5);

    REG16(0x0011EE80UL + at) = REG16(0x0011A1D0UL);
    REG16(0x0011EE82UL + at) = REG16(0x0011A1D2UL);
    queue_ranges_shift();

    rom_flash_write((const void *)0x0011EE80UL, 0x0057B2D6UL, 0x0400);
    FLASH_BUSY &= (u8)~0x20;
}

/* H'228B80. One entry taken out from under the cursor, and only when the
 * cursor is past the first position. Emptying the queue puts the picker's
 * left edge back to H'30, with the cursor taken down and put up again round
 * the move. */
void queue_delete_entry(void)
{
    if (REG16(0x0011A1CCUL) == 0) return;
    if ((short)REG16(0x0011A1CCUL) <= (short)REG16(0x0011A1D0UL)) return;

    REG8(0x0011A184UL) = 0x01;
    picker_back(0x0001);
    REG16(0x0011A1D2UL) = (u16)(REG16(0x0011A1D2UL) - 1);
    list_delete((u16 *)0x0011B3D8UL, (u16)(REG16(0x0011A1CCUL) + 1));

    if (REG16(0x0011B3D8UL) == 0) {
        picker_cursor(0x00);
        REG16(0x0011A1C8UL) = 0x0030;
        picker_cursor(0x01);
    }

    queue_remove_record((u16)(REG16(0x0011A1CCUL) + 1));
    picker_draw_range((u16)(REG16(0x0011A1C8UL) + 1),
                      (u16)(REG16(0x0011A1CCUL) + 1), REG16(0x0011A1D2UL));
}

/* H'222F00. Everything the machine holds about the pattern put back to
 * nothing, ready for the next one.
 *
 * H'FFFEF9 is anded with H'F0 and then with H'0F, which can only leave
 * nothing. That is in the original and is kept. */
void pattern_state_reset(void)
{
    pattern_flags_clear_all();

    REG8(0x00FFFEF5UL) &= (u8)~0x08;
    REG8(0x00FFFEF5UL) &= (u8)~0x10;
    REG8(0x00FFFEF6UL) &= (u8)~0x80;
    REG8(0x00FFFEF6UL) &= (u8)~0x40;
    REG8(0x00FFFEF6UL) &= (u8)~0x10;
    REG8(0x00FFFEF5UL) &= (u8)~0x80;
    REG8(0x00FFFEF9UL) = (u8)((u8)(REG8(0x00FFFEF9UL) & 0xF0) & 0x0F);
    REG8(0x00FFFEF5UL) |= 0x04;

    if (REG8(ITEM_TABLE +
             (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x17) == 0x16) {
        REG8(0x00FFFEFDUL) = 0x00;
    }

    REG8(0x00FFFEFCUL) = 0x00;
    REG8(0x00FFFEFBUL) = 0x00;

    REG8(0x00FFFEF6UL) = (u8)(REG8(0x00FFFEF6UL) & 0xF0);
    if (!(REG8(0x00FFFEF6UL) & 0x20)) {
        REG8(0x00FFFEF6UL) ^= 0x20;
        needle_stop_picture();
    }

    REG8(0x00FFFEF5UL) = (u8)(REG8(0x00FFFEF5UL) & 0xFC);
    REG8(0x0011A17BUL) = 0x01;
}

/* H'200EC0, H'2010EC. The nine settings of one pattern, moved between the
 * ten-byte record at H'57C6D6 in flash and the sixteen-byte working copy at
 * H'0E4010. Byte 0 of the record is the "this slot is in use" flag that
 * H'200E46 clears and that the save below sets; the other nine are the
 * settings, and they do not go into the working copy in order:
 *
 *   record +1 +2 +3 +4 +5 +6 +7 +8 +9
 *   work   +1 +2 +7 +3 +8 +4 +5 +6 +9
 *
 * The load also republishes four of them into the panel's own bytes. Which
 * of record +2/+3 and +4/+5 is used depends on bit 6 of H'11A7BD, the
 * alternate-mode flag: the record carries both sets and the mode picks one.
 */
void stitch_working_load(u16 n, u8 announce)
{
    const u32 src  = 0x0057C6D6UL + (u32)(u16)(10 * n);
    const u32 work = 0x000E4010UL + (u32)(u16)((u16)(n << 4));

    REG8(work + 0x01) = REG8(src + 1);
    REG8(work + 0x02) = REG8(src + 2);
    REG8(work + 0x07) = REG8(src + 3);
    REG8(work + 0x03) = REG8(src + 4);
    REG8(work + 0x08) = REG8(src + 5);
    REG8(work + 0x04) = REG8(src + 6);
    REG8(work + 0x05) = REG8(src + 7);
    REG8(work + 0x06) = REG8(src + 8);
    REG8(work + 0x09) = REG8(src + 9);

    REG8(0x0011A7D9UL) = REG8(src + 1);
    REG8(0x0011A69EUL) = REG8(src + 1);
    sew_param_a_set(REG8(src + 1));

    if (REG8(0x0011A7BDUL) & 0x40) {
        REG8(0x0011A6A0UL) = REG8(src + 4);
        REG8(0x0011A7DAUL) = REG8(src + 4);
        REG8(0x00FFFEFCUL) = REG8(src + 5);
    } else {
        REG8(0x0011A6A0UL) = REG8(src + 2);
        REG8(0x0011A7DAUL) = REG8(src + 2);
        REG8(0x00FFFEFCUL) = REG8(src + 3);
    }
    sew_param_b_set(REG8(0x0011A6A0UL));

    REG8(0x0011A7D8UL) = REG8(src + 6);
    REG8(0x00FFFEEAUL) = REG8(src + 6);
    REG8(0x0011A7DBUL) = REG8(src + 7);
    REG8(0x00FFFEECUL) = REG8(src + 7);
    REG8(0x00FFFEFBUL) = REG8(src + 8);
    REG8(0x0011A7ACUL) = REG8(src + 9);

    if (announce != 0) {
        REG8(0x00114DC6UL) |= 0x08;
        REG8(0x00FFFEF7UL) |= 0x08;
        REG8(0x00FFFEFAUL) &= (u8)~0x80;
        REG8(0x00FFFEF5UL) |= 0x40;
    }
}

/* H'2010EC. The same nine the other way, a byte at a time through the boot
 * rom's flash writer, with bit 5 of H'114DC7 held up for the length of it.
 * Byte 0 of the record goes to 1, which is what marks the slot as used. */
void stitch_working_save(u16 n)
{
    const u32 dst  = 0x0057C6D6UL + (u32)(u16)(10 * n);
    const u32 work = 0x000E4010UL + (u32)(u16)((u16)(n << 4));
    u8 one = 0x01;

    REG8(0x00114DC7UL) |= 0x20;

    rom_flash_write(&one, dst + 0, 1);
    rom_flash_write((const void *)(work + 0x01), dst + 1, 1);
    rom_flash_write((const void *)(work + 0x03), dst + 4, 1);
    rom_flash_write((const void *)(work + 0x08), dst + 5, 1);
    rom_flash_write((const void *)(work + 0x02), dst + 2, 1);
    rom_flash_write((const void *)(work + 0x07), dst + 3, 1);
    rom_flash_write((const void *)(work + 0x04), dst + 6, 1);
    rom_flash_write((const void *)(work + 0x05), dst + 7, 1);
    rom_flash_write((const void *)(work + 0x06), dst + 8, 1);
    rom_flash_write((const void *)(work + 0x09), dst + 9, 1);

    REG8(0x00114DC7UL) &= (u8)~0x20;
}

/* H'213164. A run of boxes scrolled on by one entry.
 *
 * H'212FF0 slides boxes [first]+1..[last] down over [first], which leaves
 * the last two boxes holding the same thing; the last one is then moved on
 * to the next entry of its own list and the whole run drawn again. The
 * wrap-around point is the count word at the head of the *first* box's
 * list, read before the slide -- a run always shows one list, so the count
 * is the same list's either way, but it is read from the box that is about
 * to be overwritten.
 *
 * What the new entry looks like depends on its kind byte: the nine kinds
 * below are drawn unlit, and everything else asks H'214DD4 what the field
 * is set to and lights the box if the answer is positive.
 *
 * The answer is the value the first box ends up with, which is the entry
 * the run now starts at. */
u16 hitbox_run_scroll(u16 first, u16 last)
{
    const u32 head = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * first);
    const u32 tail = HITBOX_TABLE +
        (u32)(long)(short)(u16)(HITBOX_STRIDE * last);
    const u16 count = REG16(REG32(head + 0x0C));
    u16 value;
    u8  kind;

    hitbox_run_shift((u16)(first + 1), last, first);

    value = REG16(tail + 0x08);
    if (value == count) value = 0x0001;
    else                value = (u16)(value + 1);
    REG16(tail + 0x08) = value;

    kind = REG8(REG32(tail + 0x0C) +
                (u32)(long)(short)(u16)((u16)(value << 1)) + 1);

    if (kind == 0x04 ||
        (kind >= 0x07 && kind < 0x09) ||
        (kind >= 0x0B && kind < 0x0D) ||
        kind == 0x44 || kind == 0x46) {
        hitbox_set_state(last, last, 0x00, 0);
    } else {
        const u16 on = panel_switch(kind, last, 0, 0);
        hitbox_set_state(last, last, (u8)(((short)on > 0) ? 0x01 : 0x00), 0);
    }

    hitbox_redraw_run(first, last);
    REG8(0x0011A17BUL) = 0x01;
    return REG16(head + 0x08);
}

/* H'2220F0. Backing out of a sub-screen to the one it was opened from.
 *
 * Four screens know how to do this and the rest do nothing at all. Each
 * names the screen it goes back to -- H'07 and H'45 go to H'02 or H'30
 * depending on which of the two is showing, H'34 and H'36 to H'35 or H'33,
 * H'04 to H'03, H'47 to H'46 -- and then repaints the two panels the
 * sub-screen covered and puts its strip of boxes back into their menu
 * states. H'47 only clears the wide panel and has no boxes to put back.
 *
 * H'11A169 is written before H'21F1DE is called, so what is put away is the
 * screen being *returned to*, not the one being left. That is in the
 * original. */
void screen_back_out(u8 screen)
{
    picker_cursor(0x00);
    REG8(0x0011A174UL) = 0x00;

    if (screen == 0x07 || screen == 0x45) {
        REG8(0x0011A169UL) = (u8)((REG8(0x0011A169UL) == 0x07) ? 0x02 : 0x30);

        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 2, 1);
        hitbox_set_state(0x001F, 0x0020, 0x02, 0);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0, 1);
        hitbox_set_state(0x0021, 0x0025, 0x04, 0);

        hitbox_set_state(0x000B, 0x000F, 0x03, 0);
        hitbox_redraw_run(0x000B, 0x000F);
        hitbox_set_state(0x0012, 0x0015, 0x03, 0);
        hitbox_redraw_run(0x0012, 0x0015);
    } else if (screen == 0x34 || screen == 0x36) {
        REG8(0x0011A169UL) = (u8)((REG8(0x0011A169UL) == 0x36) ? 0x35 : 0x33);

        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 2, 1);
        hitbox_set_state(0x001A, 0x001B, 0x02, 0);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0, 1);
        hitbox_set_state(0x0013, 0x0014, 0x04, 0);

        hitbox_set_state(0x0007, 0x0008, 0x03, 0);
        hitbox_redraw_run(0x0007, 0x0008);
        hitbox_set_state(0x000F, 0x0012, 0x03, 0);
        hitbox_redraw_run(0x000F, 0x0012);
    } else if (screen == 0x04) {
        REG8(0x0011A169UL) = 0x03;

        draw_rect(0x002D, 0x009F, 0x00EB, 0x00C1, LCD_FRAME_A, 2, 1);
        hitbox_set_state(0x0024, 0x0025, 0x02, 0);
        draw_rect(0x0115, 0x009D, 0x013B, 0x00C3, LCD_FRAME_A, 0, 1);

        hitbox_set_state(0x0010, 0x0014, 0x03, 0);
        hitbox_redraw_run(0x0010, 0x0014);
        hitbox_set_state(0x001B, 0x001E, 0x03, 0);
        hitbox_redraw_run(0x001B, 0x001E);
    } else if (screen == 0x47) {
        REG8(0x0011A169UL) = 0x46;

        draw_rect(0x0004, 0x009D, 0x013B, 0x00EC, LCD_FRAME_A, 0, 1);
    } else {
        return;
    }

    screen_leave(REG8(0x0011A169UL), 0x01);
    REG8(0x0011A17BUL) = 0x01;
}

/* ---- what a press does --------------------------------------------------
 *
 * H'21FF3C is the biggest routine in the application: a press turned into
 * a message and the message acted on, over sixty-eight screens. Three
 * helpers first, for the shapes it repeats.
 */

/* The six screens that have a queue under them. */
static u8 queue_screen(u8 screen)
{
    return (u8)(screen == 0x04 || screen == 0x07 || screen == 0x34 ||
                screen == 0x36 || screen == 0x45 || screen == 0x47);
}

/* Whether the picker will let the screen go. Asked only while H'11A175 is
 * up -- that is, while the queue dialog is the thing showing. The first
 * test is on the low byte of the position alone, which is what the original
 * asks for. */
static u8 picker_may_leave(void)
{
    if (picker_pos_low() == 0) return 0;
    if (picker_at_start() != 0) return 0;
    if (picker_one_only() != 0) return 0;
    return 1;
}

/* The body under messages H'17 and H'18: the pattern strip moved a page
 * back or a page on, with the pressed box taken out of its lit state and
 * put back into it round the move. Six copies in the original, differing
 * only in how far the strip runs, how big a page is, and whether a second
 * run of boxes follows it along. */
static void strip_page(u16 index, u16 last, u16 step, u8 forward,
                       u16 shift_first, u16 shift_last, u16 shift_dest,
                       u16 redraw_first, u16 redraw_last)
{
    u16 was;

    message_show_held(index);

    was = hitbox_find(0x0001, last, REG16(0x00FFFEE0UL), 0x01);
    hitbox_set_state(was, was, 0x00, 0);

    REG16(0x0011B108UL) = forward
        ? hitbox_list_scroll_on(0x0001, last, step)
        : hitbox_list_scroll_back(0x0001, last, step);

    was = hitbox_find(0x0001, last, REG16(0x00FFFEE0UL), 0x01);
    hitbox_set_state(was, was, 0x01, 0);

    if (redraw_last != 0) {
        hitbox_run_shift(shift_first, shift_last, shift_dest);
        hitbox_redraw_run(redraw_first, redraw_last);
    }
}

/* H'21FF3C. What a press does, screen by screen: the biggest single routine
 * in the application and the one everything else on the panel hangs off.
 *
 * It is three dispatches deep. The screen picks a prelude, and six of the
 * twelve screens that have one take the press as a *pattern* -- the boxes of
 * the pattern strip -- and are finished with it. Anything the prelude does
 * not claim falls through to the common body, where the screen picks a
 * second range of boxes to hit-test; that hit yields a message number, and
 * the message number picks one of twenty-four bodies out of a table of a
 * hundred and twenty-four. Eighty-one of the hundred and twenty-four do
 * nothing at all.
 *
 * Two words of local are threaded through the whole thing: the value the
 * box carries, which becomes the message number, and the box's own index.
 *
 * Every path returns zero.
 */
u8 screen_touch(void)
{
    u16 value = 0;                    /* the box's value, then the message */
    u16 index = 0;                    /* and which box it was */
    u8  screen = REG8(0x0011A169UL);
    u8  r;

    switch (screen) {

    /* ---- the pattern strip ------------------------------------------- */
    case 0x02: case 0x18: case 0x30: case 0x44:
        if (touch_hit(0x0001, 0x000F, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x000F, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            const u8 s = REG8(0x0011A169UL);

            if (s != 0x44 && s != 0x30) {
                queue_make_room(REG16(0x00FFFEE0UL));
                screen_switch(0x44, 0x01, 0x00);
            }
        }
        return 0;

    case 0x07: case 0x45:
        if (touch_hit(0x0001, 0x000A, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            u16 was = hitbox_find(0x0001, 0x000F, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);

            was = hitbox_find(0x0021, 0x0025, REG16(0x00FFFEE0UL), 0);
            if (was != 0) hitbox_set_state(was, was, 0x00, 0);

            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            const u8 s = REG8(0x0011A169UL);

            if (s != 0x44 && s != 0x30) {
                queue_make_room(REG16(0x00FFFEE0UL));
                screen_switch(0x44, 0x01, 0x00);
                return 0;
            }
        }
        if (REG32(0x0011A196UL) != 0x0057EEF8UL) {
            queue_add_entry(REG16(0x00FFFEE0UL), REG8(0x00FFFEFDUL));
            hold_start(0x0096);
        }
        return 0;

    case 0x03:
        if (touch_hit(0x0001, 0x0014, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x0014, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) screen_switch(0x44, 0x01, 0x00);
        return 0;

    case 0x04:
        if (touch_hit(0x0001, 0x000F, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x0014, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            screen_switch(0x44, 0x01, 0x00);
            return 0;
        }
        queue_add_entry(REG16(0x00FFFEE0UL), REG8(0x00FFFEFDUL));
        hold_start(0x0096);
        return 0;

    case 0x33: case 0x35:
        if (touch_hit(0x0001, 0x0008, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            const u16 was = hitbox_find(0x0001, 0x0008, REG16(0x00FFFEE0UL), 0);

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);
            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) screen_switch(0x44, 0x01, 0x00);
        return 0;

    case 0x34: case 0x36:
        if (touch_hit(0x0001, 0x0006, &value, &index) != 0x03) break;
        if (REG8(0x00114DC6UL) & 0x80) break;

        if (hitbox_kind(index) == 0) {
            u16 was = hitbox_find(0x0001, 0x0008, REG16(0x00FFFEE0UL), 0);
            u8  now, next;

            hitbox_set_state(was, was, 0x00, 0);
            hitbox_set_state(index, index, 0x01, 0);

            was = hitbox_find(0x0013, 0x0014, REG16(0x00FFFEE0UL), 0);
            if (was != 0) hitbox_set_state(was, was, 0x00, 0);

            /* Moving between the two halves of a two-part pattern drops
             * the mirror flag. The category of the one showing is read
             * unsigned and the category of the one pressed signed, which is
             * in the original. */
            now = REG8(ITEM_TABLE +
                       (u32)(u16)(ITEM_STRIDE * REG16(0x00FFFEE0UL)) + 0x17);
            if (now == 0x10) {
                next = REG8(ITEM_TABLE +
                            (u32)(long)(short)(u16)(ITEM_STRIDE * value) + 0x17);
                if (next == 0x11) REG8(0x00FFFEFDUL) = 0x00;
            } else if (now == 0x11) {
                next = REG8(ITEM_TABLE +
                            (u32)(long)(short)(u16)(ITEM_STRIDE * value) + 0x17);
                if (next == 0x10) REG8(0x00FFFEFDUL) = 0x00;
            }

            REG16(0x00FFFEE0UL) = value;
            item_preview(value);
            panel_strip_choose();
        }
        if (REG8(0x0011A178UL) != 0) {
            screen_switch(0x44, 0x01, 0x00);
            return 0;
        }
        queue_add_entry(REG16(0x00FFFEE0UL), REG8(0x00FFFEFDUL));
        hold_start(0x0096);
        return 0;

    default:
        break;
    }

    /* ---- the rest of the screen -------------------------------------- */
    switch (screen) {
    case 0x02: case 0x18: case 0x30:
        r = touch_hit(0x0010, 0x0019, &value, &index);
        break;

    case 0x07: case 0x45:
        r = touch_hit(0x0010, 0x0011, &value, &index);
        if (r != 0x03) r = touch_hit(0x0016, 0x0020, &value, &index);
        break;

    case 0x03:
        r = touch_hit(0x0015, 0x001E, &value, &index);
        break;

    case 0x04:
        r = touch_hit(0x0015, 0x001A, &value, &index);
        if (r != 0x03) r = touch_hit(0x001F, 0x0025, &value, &index);
        break;

    case 0x33: case 0x35:
        r = touch_hit(0x0009, 0x0012, &value, &index);
        break;

    case 0x34: case 0x36:
        r = touch_hit(0x0009, 0x000E, &value, &index);
        if (r != 0x03) r = touch_hit(0x0015, 0x001B, &value, &index);
        break;

    case 0x42: r = touch_hit(0x0008, 0x000A, &value, &index); break;
    case 0x44: r = touch_hit(0x0018, 0x0019, &value, &index); break;
    case 0x46: r = touch_hit(0x0000, 0x0000, &value, &index); break;
    case 0x47: r = touch_hit(0x000E, 0x0012, &value, &index); break;

    default:
        return 0;
    }

    if (r == 0x02) r = screen_leave_check(&value, 0x00);
    if (r != 0x03) {
        /* H'221B48 */
        if (REG8(0x00FFFEF8UL) & 0x80) REG8(0x00FFFEF8UL) &= (u8)~0x80;
        return 0;
    }

    for (;;) {
        if ((u16)(value - 1) > 0x007B) return 0;

        switch (value) {

        /* The panel's own state: twenty of the hundred and twenty-four
         * messages are a field of H'214DD4, stepped on by the press. */
        case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
        case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0C: case 0x44:
        case 0x46: case 0x47: case 0x49: case 0x6E: case 0x6F: case 0x76:
        case 0x7B:
            panel_switch((u8)value, index, 0x01, 0x00);
            return 0;

        /* ---- the pattern strip ---------------------------------------- */
        case 0x17: case 0x18: {
            const u8  forward = (u8)(value == 0x18);
            const u8  screen2 = REG8(0x0011A169UL);
            u16 last, step;
            u16 sf = 0, sl = 0, sd = 0, rf = 0, rl = 0;

            if (REG8(forward ? 0x0011B0ABUL : 0x0011B0AAUL) == 0) return 0;

            switch (screen2) {
            case 0x02: case 0x18: case 0x30: case 0x44:
                last = 0x000F; step = 0x0005; break;
            case 0x07: case 0x45:
                last = 0x000F; step = 0x0005;
                sf = 0x000B; sl = 0x000F; sd = 0x0021;
                rf = 0x0021; rl = 0x0025; break;
            case 0x03: case 0x04:
                last = 0x0014; step = 0x0005; break;
            case 0x33: case 0x35:
                last = 0x0008; step = 0x0002; break;
            case 0x34: case 0x36:
                last = 0x0008; step = 0x0002;
                sf = 0x0007; sl = 0x0008; sd = 0x0013;
                rf = 0x0013; rl = 0x0014; break;
            default:
                return 0;
            }

            strip_page(index, last, step, forward, sf, sl, sd, rf, rl);
            return 0;
        }

        case 0x15: {
            const u8 s = REG8(0x0011A169UL);
            u16 first, add, cap;

            if (s == 0x02 || s == 0x07 || s == 0x18 || s == 0x30 || s == 0x45) {
                first = 0x0010; add = 0x000F; cap = 0x0015;
            } else if (s >= 0x03 && s <= 0x04) {
                first = 0x0019; add = 0x0018; cap = 0x001E;
            } else if (s >= 0x33 && s <= 0x36) {
                first = 0x000D; add = 0x000C; cap = 0x0012;
            } else if (s == 0x42) {
                first = 0x0006; add = 0x0005; cap = 0x0007;
            } else {
                return 0;
            }

            message_show_held(index);
            {
                const u16 t = (u16)(REG16(REG32(0x0011A196UL)) + add);
                const u16 to = ((short)t <= (short)cap) ? t : cap;

                REG16(0x0011B10CUL) = hitbox_run_scroll(first, to);
            }
            return 0;
        }

        /* ---- the queue ------------------------------------------------ */
        case 0x45: {
            const u8 s = REG8(0x0011A169UL);

            message_show_held(index);
            if (REG8(0x0011A178UL) != 0) return 0;

            if (s == 0x30 || s == 0x45) {
                screen_switch(REG8(0x0011B0A4UL), 0x01, 0x00);
                REG16(0x0011B108UL) = REG16(0x0011B112UL);
            } else {
                REG8(0x0011B0A4UL) = s;
                REG16(0x0011B112UL) = REG16(0x0011B108UL);
                screen_switch(0x30, 0x01, 0x00);
                REG16(0x0011B10AUL) = 0x0001;
            }
            return 0;
        }

        case 0x48: {
            const u8 s = REG8(0x0011A169UL);

            if (s == 0x04 || s == 0x07 || s == 0x34 || s == 0x36 || s == 0x45) {
                message_show_held(index);
                REG8(0x0011B0A3UL) = REG8(0x0011A169UL);
                screen_switch(0x43, 0x01, 0x00);
            }
            return 0;
        }

        case 0x0E:
            if (queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                queue_delete_entry();
            }
            return 0;

        case 0x0F:
            if (queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                screen_hand_over(0x41);
            }
            return 0;

        case 0x10:
            if (queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                queue_save_ranges();
            }
            return 0;

        case 0x40:
            if (REG8(0x0011B3D6UL) != 0 && queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                picker_back(0x0001);
            }
            return 0;

        case 0x41:
            if (REG8(0x0011B3D7UL) != 0 && queue_screen(REG8(0x0011A169UL))) {
                message_show_held(index);
                picker_forward(0x0001);
            }
            return 0;

        case 0x11: {
            const u8 s = REG8(0x0011A169UL);

            if (s == 0x04 || s == 0x07 || s == 0x34 || s == 0x36 || s == 0x45) {
                message_show_held(index);
                if (!(REG8(0x00114DC6UL) & 0x80)) {
                    REG8(0x0011B0A2UL) = REG8(0x0011A169UL);
                    screen_switch(0x42, 0x01, 0x00);
                    REG8(0x0011A175UL) = 0x01;
                }
            } else if (s == 0x42) {
                message_show_held(index);
                screen_stack_pop();
                dialog_backdrop_save(0x01);
                screen_switch(REG8(0x0011B0A2UL), 0x01, 0x00);
                REG8(0x0011A175UL) = 0x00;
            }
            return 0;
        }

        case 0x4A:
            message_show_held(index);
            pattern_state_reset();
            return 0;

        case 0x4B:
            message_show_held(index);
            if (REG8(0x0011A175UL) != 0) {
                if (!picker_may_leave()) return 0;
                screen_remember(0x01);
            }
            screen_switch(0x2D, 0x01, 0x00);
            return 0;

        case 0x0B:
            message_show_held(index);
            if (REG8(0x0011A175UL) != 0 && !picker_may_leave()) return 0;
            screen_remember(0x01);
            screen_switch(0x3F, 0x01, 0x00);
            return 0;

        case 0x0D:
            message_show_held(index);
            REG8(0x0011B0A5UL) = REG8(0x0011A169UL);
            REG16(0x0011B118UL) = REG16(0x0011B108UL);
            screen_switch(0x46, 0x01, 0x00);
            return 0;

        /* ---- the boxes that lead somewhere ---------------------------- */
        case 0x12:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            screen_remember(0x01);
            screen_switch(0x09, 0x01, 0x00);
            return 0;

        case 0x13:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            if (!(REG8(0x00114DC6UL) & 0x80)) {
                REG8(0x0011B0A7UL) = REG8(0x0011A169UL);
                REG8(0x00FFFEFDUL) = 0x02;
                screen_switch(0x0B, 0x01, 0x00);
            }
            return 0;

        case 0x14:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            screen_remember(0x01);
            screen_switch(0x0A, 0x01, 0x00);
            return 0;

        case 0x4C:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            stitch_working_load((u16)(REG16(0x00FFFEE0UL) + REG8(0x00FFFEFDUL)),
                                0x01);
            return 0;

        case 0x4D:
            if (hitbox_kind(index) == 0x05) return 0;
            message_show_held(index);
            stitch_working_save((u16)(REG16(0x00FFFEE0UL) + REG8(0x00FFFEFDUL)));
            return 0;

        /* ---- coming back from a sub-screen ---------------------------- */
        case 0x6D: {
            const u8 s = REG8(0x0011A169UL);

            if (REG8(0x00114DC6UL) & 0x80) return 0;

            switch (s) {
            case 0x02: case 0x30:
                pattern_strip_restore((u8)((s == 0x02) ? 0x07 : 0x45));
                queue_reload();
                if (hitbox_find(0x000B, 0x000F, REG16(0x00FFFEE0UL), 0x01) != 0) {
                    value = 0x0018;
                    continue;
                }
                hold_start(0x0064);
                return 0;

            case 0x33: case 0x35:
                pattern_strip_restore((u8)((s == 0x35) ? 0x36 : 0x34));
                queue_reload();
                if (hitbox_find(0x0007, 0x0008, REG16(0x00FFFEE0UL), 0x01) != 0) {
                    value = 0x0018;
                    continue;
                }
                hold_start(0x0064);
                return 0;

            case 0x03:
                pattern_strip_restore(0x04);
                queue_reload();
                if (hitbox_find(0x0010, 0x0014, REG16(0x00FFFEE0UL), 0x01) != 0) {
                    value = 0x0018;
                    continue;
                }
                hold_start(0x0064);
                return 0;

            case 0x46:
                pattern_strip_restore(0x47);
                queue_reload();
                hold_start(0x0064);
                return 0;

            case 0x34: case 0x36:
                screen_back_out(s);
                if (REG8(0x0011A169UL) == 0x35) {
                    bitmap_draw(0x005E, 0x005E, 0x00E8, 0x00C1,
                                (const u8 *)0x00300782UL, LCD_FRAME_A);
                    stitch_stroke_toggle(0x01, 0x01);
                } else {
                    bitmap_draw(0x005E, 0x005E, 0x00E8, 0x00C1,
                                (const u8 *)0x00300A47UL, LCD_FRAME_A);
                    stitch_stroke_toggle(0x00, 0x01);
                }
                screen_hand_over(REG8(0x0011A169UL));
                hold_start(0x0064);
                return 0;

            case 0x07: case 0x45:
            case 0x04:
            case 0x47:
                screen_back_out(s);
                screen_hand_over(REG8(0x0011A169UL));
                hold_start(0x0064);
                return 0;

            default:
                return 0;
            }
        }

        /* H'57EFC4 holds a message the machine wants acting on next, which
         * is taken here and put through the same table. */
        case 0x7A: {
            const u16 next = REG16(0x0057EFC4UL);

            if (next == 0) return 0;
            value = next;
            index = 0x0000;
            REG8(0x0011A17BUL) = 0x01;
            continue;
        }

        case 0x7C:
            REG8(0x00FFFEF8UL) |= 0x20;
            return 0;

        default:
            return 0;
        }
    }
}
