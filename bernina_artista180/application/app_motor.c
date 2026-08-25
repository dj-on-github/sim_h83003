/* The artista 180 application, rebuilt in C: the ports, the timers, the
 * motors, the foot control and the panel keys.
 *
 * Part of the reconstruction. app.h holds the addresses these
 * routines work through and the declaration of every one of them
 * that another file calls.
 */
#include "app.h"

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
