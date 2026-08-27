/* The artista 180 application, rebuilt in C: bringing the machine up, the
 * diagnostics screen and service mode.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"
#include "app_keys.h"

/* ---- bringing the machine up -------------------------------------------
 * H'208D88, the spine of the whole start-up. Twenty calls in order, ending
 * with the one that brings the display alive. Each is named for its address
 * until it is reconstructed, and a stub does nothing rather than pretending.
 *
 * Reconstructed so far: the I2C bus (H'200CB6), the settings copy and
 * the configuration block check.
 */

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
