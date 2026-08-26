/* The artista 180 application, rebuilt in C: the boot ROM, the bus, the
 * display's bring-up and the tables it works from.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

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

/* H'200376 and H'200382. Copies the bytes from [src] up to [end] to [dest].
 *
 * Two versions again, and for the same reason as the two fills: the first
 * compares only the low half of the source pointer against the end, so it
 * cannot cross a 64K boundary, and the second compares all 24 bits. The
 * comparison comes before the first byte, so a range whose ends are equal
 * copies nothing.
 */
static void copy16(u32 src, u16 end, u32 dest)
{
    while ((u16)src != end) {
        *(volatile u8 *)dest = *(const volatile u8 *)src;
        src++;
        dest++;
    }
}

static void copy32(u32 src, u32 end, u32 dest)
{
    while (src != end) {
        *(volatile u8 *)dest = *(const volatile u8 *)src;
        src++;
        dest++;
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

    /* And then the other half of a C runtime's start-up, which is where the
     * variables that are not zero get their values.
     *
     * H'24B2D0 to H'250758 is the original's initialised data -- H'5488
     * bytes of it -- and it goes to H'114DC2, which is exactly where the
     * zero fill above stopped and exactly H'5488 below where the next one
     * started. The reconstruction reads that RAM by absolute address, so it
     * needs the same bytes in the same places; the block itself sits above
     * everything rebuilt so far, so it is still the original's.
     *
     * Four of the six calls copy nothing, and the table walk between them
     * has nothing in it. They are the original's, kept for the same reason
     * the no-op fills above are kept. */
    copy16(0x0024B2D0UL, 0xB2D0, 0x0011F5A8UL);   /* copies nothing */
    copy16(0x0024B2D0UL, 0xB2D0, 0x0011F5A8UL);   /* copies nothing */
    copy32(0x0024B2D0UL, 0x0024B2D0UL, 0x0011F5A8UL); /* nor this */

    copy32(0x0024B2D0UL, 0x00250758UL, 0x00114DC2UL);

    {
        u32 at = 0x00250758UL;

        while (at != 0x00250758UL) {
            const u32 from = REG32(at);
            const u32 to = REG32(at + 4);

            at += 8;
            fill32(from, to);
        }
    }

    copy32(0x00250AE8UL, 0x00250AE8UL, 0x0011F5A8UL); /* nor this */
}

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

/* Defined with the rest of the display, further down. */
void region_copy(u16 x0, u16 y0, u16 x1, u16 y1, u16 dst_y,
                 u32 from, u32 to);
void message_show(u16 msg);
void picker_cursor(u8 mode);
void number_draw(u16 value, u16 x, u16 y);

/* H'208698. Defined near the end, once everything it calls exists. */
void service_tick(void);

/* H'20F0FE. Called from inside the long fills, so that whatever needs
 * attention while the CPU is busy clearing 19K still gets it. */
void service_hook(void)
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
void buffer_fill(u32 dest, u8 value)
{
    mem_fill(dest, value, LCD_BUFFER_BYTES);
}

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

/* H'20076C. The same byte fill as H'200762 above, entered with a longword
 * count rather than a word one. The two share a loop in the original: the
 * word entry widens its count and falls into this one. */
void mem_set_long(u32 dest, u8 value, u32 count)
{
    volatile u8 *p = (volatile u8 *)dest;

    while (count != 0) {
        *p++ = value;
        count--;
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

/* H'210C20. Five more table addresses, then 27 longwords copied out of the
 * table at H'115066 into H'11595E. Entry zero is not copied; the loop runs
 * from one. */
void build_tables(void)
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
void splash_and_config(void)
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
void display_init_223010(void)
{
    build_consecutive_lists();
    filter_unlisted();
}
