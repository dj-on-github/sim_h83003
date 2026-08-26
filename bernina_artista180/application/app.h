/* The artista 180 application, rebuilt in C -- what every part of it shares.
 *
 * The reconstruction is split by what a routine is *for*: one file for the
 * motors, one for the queue, one for the module's link, and so on. The names
 * of the addresses they all work through, and the declaration of every
 * routine one file offers another, are here. Addresses are written as they
 * are in the original listing, so that a name here can be checked against
 * the disassembly without arithmetic.
 *
 * Scale, measured rather than guessed: the code region H'200000-H'250FFF
 * holds 1310 distinct routines in 324K. Booting to an idle screen executes
 * about 56K of that across 66 spans, so the boot path is a fraction of the
 * whole. What has not been reconstructed yet is a stub, and the
 * build/splice/run loop is arranged so that a stub is obvious rather than
 * silently wrong -- a stub interrupt handler returns, it does not pretend to
 * have done something.
 */
#ifndef APP_H
#define APP_H

#include "../bootrom/h8_3003.h"

/* A routine that has not been reconstructed yet. It does nothing, and says
 * which address it stands for, so that a stub is visible in the source
 * rather than being mistaken for a routine that genuinely has no body. */
#define STUB(name, addr)                                                      \
    void name(void) { /* H'addr -- NOT RECONSTRUCTED */ }

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

/* The third display buffer, used as scratch by the mirroring and the
 * splash. Defined here because splash_and_config needs it. */
#define LCD_SCRATCH  0x000E8010UL

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

/* H'21088E. Copies one list to another, leaving out the entries whose
 * category is 1 -- the ones build_item_list never lists either. */
#define ITEM_LIST_IN        0x11B11EUL
#define ITEM_LIST_OUT       0x11B212UL

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

/* H'2119EE. Hold whatever is on the screen for [ticks].
 *
 * Not a delay -- it returns at once. It sets the length of the hold and
 * zeroes the count of how long it has run, and the millisecond tick does the
 * counting. H'211A02 is the other half: it says whether the hold has run
 * out, and clears the screen when it has.
 */
#define HOLD_ELAPSED  REG16(0x114DE0UL)
#define HOLD_LENGTH   REG16(0x11A166UL)

/* H'2007B2. Points the flash page buffer at H'0FFC14.
 *
 * This is the pointer the boot ROM's flash routines read out of H'FFFD10 to
 * find their 256-byte staging area -- see ../bootrom/app.c. The application
 * owns the memory and tells the boot ROM where it is, which is why a byte
 * written through the download protocol lands somewhere sensible. */
#define FLASH_PAGE_BUFFER   0x000FFC14UL

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

#define CONFIG_STAMPED      REG8(0x57FF81UL)
#define CONFIG_VERSION      0x57FFB0UL
#define APP_IDENTITY        0x200100UL

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

/* The trim the analog input applies, at H'FFFEDF: it is added as an offset
 * to a scaled reading before the result is stored. H'32 is the middle of
 * its range and the value a setting outside H'0B..H'F4 falls back to. */
#define INPUT_TRIM      REG8(0xFFFEDFUL)
#define SETTING_TRIM    REG8(0x57FF92UL)

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

#define SPAN_A  REG16(0x11A68AUL)
#define SPAN_B  REG16(0x11A68CUL)
#define SPAN_C  REG16(0x11A68EUL)
#define SPAN_D  REG16(0x11A690UL)
#define STEP_P  REG16(0x11A692UL)
#define STEP_Q  REG16(0x11A694UL)
#define STEP_R  REG16(0x11A696UL)

/* The decoder's state, which every loop below advances the same way. */
typedef struct {
    const u8 *src;
    u16       pos;
    u8        run;
    u8        colour;
} rle_state;

/* ---- the dialog ---------------------------------------------------------
 * The dialog occupies x H'30 to H'E7 and y H'A0 to H'C0 -- a strip across
 * the bottom of the screen. Its contents scroll sideways within that strip,
 * which is what H'20EFE2 and its two callers are for.
 */
#define DIALOG_X0   0x0030
#define DIALOG_X1   0x00E7
#define DIALOG_Y0   0x00A0
#define DIALOG_Y1   0x00C0

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

/* Settings the bring-up reads out of the H'57FF8x window and keeps in RAM.
 * A value above H'10 is not one of the choices, so it falls back to 8. */
#define SETTING_INDEX       REG8(0x57FF8EUL)
#define SETTING_WORD_A      REG32(0x57FF82UL)
#define SETTING_WORD_B      REG32(0x57FF86UL)
#define ACTIVE_SETTING      REG8(0xFFFED8UL)
#define SETTING_COPY_A      REG32(0x11A6E0UL)
#define SETTING_COPY_B      REG32(0x11A6E4UL)

/* ---- the two main loops -----------------------------------------------
 * H'208E10, and it does not return.
 *
 * H'FFFEC4 bit 7 is set at power-on if the service key was held, and picks
 * which of the two loops the machine spends the rest of its life in.
 */
#define SERVICE_MODE_FLAG   REG8(0xFFFEC4UL)

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

/* Pictures for the two arrows, the same four H'229714 uses. */
#define ARROW_BACK_LIT  0x0034E390UL
#define ARROW_BACK_DIM  0x0034E3C7UL
#define ARROW_FWD_LIT   0x0034E3FEUL
#define ARROW_FWD_DIM   0x0034E435UL

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

/* H'21AA42. Eight boxes: six pick one of six longwords out of the record at
 * H'11B2AA, box 4 accepts what has been picked and box 8 goes back.
 *
 * The six offsets are H'60 to H'74 in steps of four, which is a run of six
 * with box 4 sitting in the middle of it -- so the offset is not a plain
 * multiple of the box number and the table is kept as a table. */
#define PICK4_STATE  0x0011B33CUL
#define PICK4_HELD   0x0011B33EUL

/* H'21A7F6. Eleven boxes: nine picks at H'38 to H'58, box 4 accepts and box
 * H'0B goes back. The same shape as H'21AA42 with a wider window on the
 * record. */
#define PICK3_STATE  0x0011B336UL
#define PICK3_HELD   0x0011B338UL

/* H'21A56C. Twelve boxes: ten picks at H'04 to H'24, then accept and back.
 *
 * This is the one whose lighting guard is a range rather than two names --
 * everything below H'0B lights, so the last two boxes do not -- and the one
 * whose tenth pick depends on the machine: H'2C with the module and H'28
 * without. */
#define PICK2_STATE  0x0011B330UL
#define PICK2_HELD   0x0011B332UL

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

/* ---- what each file offers ---------------------------------------------
 *
 * One block per source file, in the order the files are listed in the
 * Makefile. A routine that only its own file calls is static there and is
 * not named here.
 */

/* app_boot.c -- the boot ROM, the bus, the display's bring-up and the tables it works from */
void rom_host_service(void);
void rom_delay(u16 units);
void rom_flash_write(const void *src, u32 dst, u32 len);
void rom_select_sci0(void);
int rom_host_confirmed(void);
void i2c_init(void);
u8 eeprom_read_current(void);
u8 eeprom_write_verify(u8 address, u8 value);
void bus_init(void);
u8 memory_check(void);
void cold_start(void);
void lcd_controller_init(void);
void service_hook(void);
void mem_fill(u32 dest, u8 value, u32 count);
void lcd_buffer_fill(u32 buffer, u8 value);
void buffer_fill(u32 dest, u8 value);
void input_state_init(void);
void pointer_table_init(void);
void sci0_module_init(void);
void config_to_eeprom(void);
void adc_start_conversion(void);
u8 adc_get_result(u8 index);
u8 adc_convert_polled(u8 channel, u8 samples);
u8 adc_start_channel(u8 channel, u8 mode);
void plot_pixel(u16 x, u16 y, u32 buffer, u8 colour);
u16 first_item_of_category(u8 wanted, u32 list);
void filter_unlisted(void);
void mem_set(u32 dest, u8 value, u16 count);
void mem_set_long(u32 dest, u8 value, u32 count);
u16 first_index_of_category(u8 category);
void build_consecutive_lists(void);
void finish_22950C(void);
void build_tables(void);
void image_load(u32 runs, u32 dest);
void hold_start(u16 ticks);
void splash_and_config(void);
void display_init_223010(void);

/* app_flash.c -- the item index, the configuration block, and writing the settings back */
void item_records_copy(u16 from, u16 to);
void scan_items(void);
void display_init(void);
void set_flash_page_buffer(void);
void port_shadows_init(void);
short str_compare(const char *a, const char *b);
u32 str_length(const char *s);
void item_flags_clear(void);
void item_run_length_set(u16 length);
void user_items_reset(void);
void beep_defaults_write(void);
void stitch_records_clear(void);
void queue_clear_to_flash(void);
void settings_save(u8 clear_queue);
void config_block_check(void);
void pins_to_input_2061A0(void);
void port_b_init(void);
void port_c_bits_high(void);
void port_c_init(void);

/* app_motor.c -- the ports, the timers, the motors, the foot control and the panel keys */
void foot_demand_hold(void);
void foot_demand_restore(void);
void itu0_init(void);
void itu1_init(void);
void itu2_init(void);
void tpc_ports_init(void);
void motor_a_init(void);
void motor_b_init(void);
void motor_c_init(void);
void motor_d_init(void);
void motor_enables_on(void);
void p9_bit5_high(void);
void itu0_start(void);
void itu1_start(void);
void itu2_start(void);
void motors_and_timers_init(void);
void main_motor_pins_init(void);
void pwm4_init(void);
void pwm4_interrupt_enable(void);
void main_motor_init(void);
void pedal_restart_check(void);
void beep(u16 on, u16 off, u8 count);
void pedal_scan(void);
void pedal_hold_service(void);
void speed_target_set(void);
void speed_scale(void);
void speed_service(void);
void pedal_service(void);
void speed_control_init(void);
void interrupts_off(void);
void interrupts_on(void);
void sensor_a_alarm(void);
void sensor_b_alarm(void);
void analog_baseline(void);
void adc_sequence_step(void);
void adc_read_sensor_a(void);
void adc_read_sensor_b(void);
void sensor_a_service(void);
void sensor_b_service(void);
void presser_switch_read(void);
void trim_to_eeprom(void);
void analog_scan(void);
void analog_input_init(void);
void knobs_read(void);
void key_banks_read(void);
void key_scan_compare(void);
void key_scan_first(void);
void key_scan_again(void);
void key_scan_finish(void);
void key_scan_step(void);
void keys_scan_settled(void);

/* app_stitch.c -- the stitch database, and making a pattern current */
u32 stitch_record(u16 n);
u32 stitch_work(u16 n);
u8 pattern_byte(u16 n, u16 index);
u8 stitch_param_1(u16 n, u8 use_working);
u8 stitch_param_2(u16 n, u8 use_working);
u8 stitch_param_4(u16 n, u8 use_working);
u8 stitch_param_5(u16 n, u8 use_working);
u8 stitch_param_6(u16 n, u8 use_working);
u8 stitch_param_7(u16 n, u8 use_working);
void stitch_params_set(u8 mark, u16 n, u8 f1, u8 v1, u8 f2, u8 v2, u8 f3, u8 v3, u8 f4, u8 v4, u8 f5, u8 v5, u8 f6, u8 v6, u8 f7, u8 v7);
void stitch_params_reload(void);
u8 sew_param_a_set(u8 v);
u8 sew_param_b_set(u8 v);
u16 stitch_run_head(u16 n);
void sew_clear_busy_3(void);
void sew_clear_busy_4(void);
void sew_stop_flags_clear(void);
void sew_flag_copy_6(void);
u8 bus_byte_10(void);
u8 stitch_length_shown(void);
void stitch_length_choose(u8 shown);
u8 bus_byte_11(void);
void sew_params_publish(void);
void sew_params_save(void);
void sew_params_restore(void);
void sew_params_capture(void);
void sew_variant_load(void);
void sew_mode_arbitrate(void);
void sew_watch_7DE(void);
void sew_watch_7E1(void);
void sew_needle_stop_pin(void);
void sew_watch_67D(void);
void sew_limit_6BB(void);
u8 sew_interpolate(u8 percent, u8 from, u8 to);
void sew_offset_set(void);
u16 sew_limit_overshoot(void);
void sew_display_scale(u8 v);
void sew_counters_tick(void);
void mem_copy_long(u8 *dst, const u8 *src, u32 n);
void mem_copy(u8 *dst, const u8 *src, u16 n);
void stitch_chunk_load(void);
void stitch_chunk_first(void);
u8 stitch_length_limit(void);
void sew_clear_67D_5(void);
void sew_set_67D_5(void);
void sew_variant_select(void);
void sew_variant_service(void);
void sew_mode_bit_service(void);
void sew_params_clamp(u8 max_a, u8 max_b);
void sew_edge_on_a(void);
void sew_edge_off_a(void);
void sew_edge_off_b(void);
void sew_latch_7D4_7(void);
void sew_latch_7DF(void);
void sew_busy_release(void);
void sew_repeat_service(void);

/* app_queue.c -- the queue, stepping through it, and stopping in the right place */
u16 queue_entry_ref(u16 i);
void queue_entry_unpack(void);
u8 pattern_is_group(u16 n);
void queue_flags_reset(void);
void queue_group_start(void);
void queue_scan_service(void);
void sew_params_from_pattern(void);
void sew_lamp_service(void);
void queue_advance(void);
void sew_speed_limit_scale(void);
void sew_service(void);
void sew_mode_dispatch(void);
void sew_running_flags(void);
u16 mode_code(void);
void sew_params_scale_for_mode(void);
void sew_counters_service(void);
void sew_limits_apply(void);
void stitch_variants_build(void);
void home_state_b(void);
void home_state_c(void);
void sew_mechanism_service(void);
void redraw_partial(void);
void redraw_pattern(void);
void needle_position_set(u8 step);
u8 pattern_variant_flag(u16 n);
void stitch_fetch_next(void);
void stitch_begin(void);
void stitch_restart(void);
void queue_step_flags_clear(void);
void queue_step_flags_last(void);
void queue_step_flags_first(void);
void queue_flag_clear_cc4(void);
void queue_flags_group_end(void);
u16 queue_group_count(void);
void queue_step_next(void);
void queue_step_or_stop(void);
void variant_step(void);
void pattern_defaults_load(u16 n, u8 mark);
u8 pattern_variant_flag_b(u16 n);
void stitch_next_or_variant(void);
void stitch_advance(void);
void stop_step_a(void);
void redraw_after_edit(void);
void stop_step_b(void);
void stitch_tick(void);
void sew_params_from_working(u16 n);
void sew_params_for_pattern(u16 n);
void pattern_make_current(void);
void panel_selection_check(void);
void redraw_full(void);
void redraw_running(void);
void panel_service_idle(void);
void panel_service_running(void);
void panel_service(void);
void stop_countdown_set(u8 at_half);
void needle_stop_service(void);

/* app_sew.c -- a stitch turned into motor positions, interpolated, and driven */
void feed_position_set(void);
void needle_position_apply(void);
void stitch_spans_build(void);
u8 needle_step(void);
u8 needle_step_mirrored(void);
void needle_span_set(void);
void sew_pass(void);
void sew_params_writeback(u16 n);
void display_refresh_tick(void);
void display_params_publish(void);
void sew_width_limit_set(u8 fresh);
void variant_advance(void);
void pwm4_matcha_on(void);
void pwm4_matcha_off(void);
void position_capture_init(void);
void position_capture_on(void);
void position_capture_off(void);
void motor_brake_pulse(void);
void main_motor_speed_set(u8 speed);
void main_motor_service(void);
void stitch_state_init(void);

/* app_screen.c -- the display subsystem, bitmaps, screens, the dialog and the picker */
void picker_preview(u16 x, u16 y, u16 pos, u8 redraw);
void service_tick(void);
u16 header_word_0(const u8 *p);
u16 header_word_1(const u8 *p);
void pixel_address(u16 x, u16 y, u32 base, u32 *addr, u8 *shift);
void draw_vline(u16 x, u16 y0, u16 y1, u32 buffer, u8 colour);
u8 *copy_forward(u8 *dst, const u8 *src, u32 n);
u8 *copy_overlapped(u8 *dst, const u8 *src, u32 n);
void message_state_clear(void);
void message_state_set(void);
void screen_remember(u8 slot);
u8 read_pixel(u16 x, u16 y, u32 buffer);
void draw_hline(u16 x0, u16 x1, u16 y, u32 buffer, u8 colour);
void draw_line_bresenham(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer, u8 colour);
void draw_line(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer, u8 colour);
void int_to_decimal(short v, char *out);
short abs_short(short v);
void bitmap_draw(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src, u32 dst);
void bitmap_draw_mirrored(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src, u32 dst, u8 mode);
void lzw_step(const u8 *stream, u16 *out, u8 first);
void bitmap_draw_lzw(u16 x0, u16 y0, u16 x1, u16 y1, const u8 *src, u32 dst);
void draw_rect(u16 x0, u16 y0, u16 x1, u16 y1, u32 buffer, u8 colour, u8 filled);
void region_copy(u16 x0, u16 y0, u16 x1, u16 y1, u16 dst_y, u32 from, u32 to);
void screen_leave(u8 screen, u8 force);
void dialog_backdrop_save(u8 force);
void screen_switch(u8 screen, u8 slot, u8 remember);
void message_show(u16 msg);
void scroll_rect(u16 x_from, u16 y0, u16 x_end, u16 y1, u16 x_to);
void dialog_scroll_left(u16 n);
void dialog_scroll_right(u16 n);
u16 dialog_shift_to_left_edge(u16 x);
u16 dialog_shift_to_right_edge(u16 x);
u16 queue_entry_number_first(void);
u16 queue_entry_number(u16 i);
u8 queue_entry_offset(u16 i);
u8 pattern_not_16(u16 n);
u8 queue_entry_facing(u16 i);
void text_draw(const char *str, u16 x0, u16 y0, u16 x1, u16 y1, u16 gap, u8 align, const u8 *font);
void dialog_number_draw(u16 n);
void picker_draw_range(u16 x, u16 from, u16 to);
void picker_cursor(u8 mode);
void picker_goto(u16 pos);
void picker_forward(u16 n);
void picker_back(u16 n);
void picker_rebuild(u16 n, u8 show_number, u8 redraw);
void message_beep(u16 msg);
void queue_group_restart(void);
void speed_target_service(void);
void stitch_database_open(void);

/* app_service.c -- bringing the machine up, the diagnostics screen and service mode */
void machine_init(void);
void embroidery_service(void);
void diag_screen(u16 a, u8 b, u8 c);
void key_and_diag(void);
void loop_tick(void);
void number_draw(u16 value, u16 x, u16 y);
void foot_calibration_save(void);
void service_pass(void);
void service_exit(void);
void module_park(void);
void module_speed_idle(void);
void module_unpark(void);
void itu1_borrow(void);
void itu1_return(void);
void module_link_once(void);
void service_module_pass(void);
void service_readings(void);
void motor_1_off(void);
void motor_2_off(void);
void motor_3_off(void);
void motor_4_off(void);
void motors_off_stepped(void);
void motor_1_pos_FC(void);
void motor_2_pos_FC(void);
void motor_3_pos_FC(void);
void motor_4_pos_FC(void);
void motors_pos_FC(void);
void motor_1_pos_FD(void);
void motor_2_pos_FD(void);
void motor_3_pos_FD(void);
void motor_4_pos_FD(void);
void motors_pos_FD(void);
void motor_1_pos_FE(void);
void motor_2_pos_FE(void);
void motor_3_pos_FE(void);
void motor_4_pos_FE(void);
void motors_pos_FE(void);
void motor_1_pos_FF(void);
void motor_2_pos_FF(void);
void motor_3_pos_FF(void);
void motor_4_pos_FF(void);
void motors_pos_FF(void);
void hall_target_save(void);
void hall_step_settle(void);
void hall_trim(void);
void motor_preset(u8 step);
void motor_1_cycle(void);
void motor_2_cycle(void);
void motor_3_cycle(void);
void motor_4_cycle(void);
void motors_cycle_all(void);
void motor_1_wait_ready(void);
void motor_2_wait_ready(void);
void motor_3_wait_ready(void);
void motor_4_wait_ready(void);
void foot_zero(void);
void foot_offset_apply(void);
void foot_ramp(void);
void foot_test_step(void);
void service_sew_slow(void);
void service_sew_fast(void);
void service_motor_cycle(void);
void service_motor_home(void);
void hall_window_set(void);
void service_hall_trim(void);
void service_analog_show(void);
void service_hall_watch(void);
void service_module_test(void);
void service_foot_calibrate(void);
void service_dispatch(void);

/* app_isr.c -- the two main loops, the timebase, and the five interrupt handlers */
void main_loop_service(void);
void main_loop_normal(void);
void app_init(void);
void app_main(void);
void alt_entry(void);
void motor_1_step(void);
void motor_1_step_home(void);
void motor_2_step(void);
void motor_2_step_home(void);
void motor_3_step(void);
void motor_3_step_home(void);
void motor_4_step(void);
void motor_4_step_home(void);
void isr_motors_12_body(void);
void isr_motor_3_body(void);
void isr_motor_4_body(void);
void isr_tacho_overflow_body(void);
void isr_tacho_capture_body(void);
void isr_itu4_a_body(void);
void needle_demand_apply(void);
void feed_demand_apply(void);
void hook_width_apply(void);
void hook_pos_apply(void);
void thread_demand_apply(void);
void sew_phase_settle(void);
void sew_phase_step(void);
void sew_phase_tick(void);
void ms_counters_tick(void);
void motor_pwm_tick(void);
u8 knob_a_track(u8 count, u16 limit);
u8 knob_b_track(u8 count, u16 limit);
u16 handwheel_track(u16 position);
void beep_tick(void);
void sew_position_sense(void);
void tick_slice_1(void);
void tick_slice_2(void);
void tick_slice_3(void);
void tick_all(void);
void isr_millisecond_body(void);
void isr_24(void);
void isr_28(void);
void isr_32(void);
void isr_36(void);
void isr_38(void);
void isr_40(void);
void isr_41(void);

/* app_hitbox.c -- the item preview, the two bars, and the hit-box table */
u8 pattern_category_12_to_19(u16 n);
u8 message_hold_done(void);
u16 menu_list_fill(u16 first, u16 last, u16 value);
void menu_repick(void);
void queue_panel_draw(u16 which);
u8 pattern_is_16(u16 n);
void item_preview_stitch(u16 item);
void item_preview_pattern(u16 item);
void item_preview_plain(u16 item);
void item_preview(u16 item);
void bar_width(u16 value, u8 fresh, u32 buffer, u8 colour);
void bar_length(u16 value, u8 fresh, u32 buffer, u8 colour);
u8 hitbox_kind(u16 index);
u8 hitbox_style(u16 index);
void hitbox_reset_all(void);
u16 hitbox_find(u16 first, u16 last, u16 value, u8 second);
void hitbox_blit(u16 index, u32 buffer, u32 src);
void hitbox_fill(u16 index, u32 buffer);
void hitbox_paint(u16 index, u8 with_picture);
void screen_stack_push(void);
void screen_stack_pop(void);
void screen_from_slot(u8 slot);
void touch_holdoff_start(void);
u8 touch_holdoff_done(void);
u8 touch_allowed(u16 code);
u8 screen_leave_stacks(void);
u8 screen_leave_check(u16 *out, u8 forced);
u8 link_claim(u8 owner);
char *str_copy_n(char *dst, const char *src, u32 n);
char *str_copy(char *dst, const char *src);
char *str_append(char *dst, const char *src);
void hitbox_set_state(u16 first, u16 last, u8 what, u32 picture);
u16 hitbox_fill_boxed_from_list(u16 first, u16 last, u16 value, u32 list);
void message_show_held(u16 msg);
u16 hitbox_fill_from_list(u16 first, u16 last, u16 value, u32 list);
void hitbox_redraw_run(u16 first, u16 last);

/* app_module.c -- the embroidery module's panel: its slate, its labels and its numbers */
void screen_state_park(void);
void screen_request(void);
u8 module_is_idle(void);
u8 module_ready(void);
void module_link_wake(void);
void module_lamp_pair(u8 on);
void module_lamp_b(u8 on);
void module_lamp_c(u8 on);
void module_lamp_d(u8 on);
void module_switches_show(void);
u8 module_busy(void);
void screen_restore_pending(void);
u8 touch_settled(void);
void foot_switch_screen(void);
void key_scan(void);
void screen_put_away(void);
u8 pattern_attr_bit3(void);
void stitch_reset_current(void);
void pattern_slot_begin(void);
void link_counters_clear(void);
void module_reset_hook(void);
void pattern_store_clear(void);
void module_state_clear(void);
void module_buffers_clear(void);
void text_left_94(const char *str);
void text_left_BC(const char *str);
void text_left_D9(const char *str);
void text_top_CB(const char *str);
void text_top_102(const char *str);
void module_fixed_box(u8 mode);
void box5_draw(u8 lit);
void label_minutes(short value);
void label_colours(void);
u8 stop_key_down(void);
u8 module_running(void);
u8 module_edge_service(void);
void pause_button_draw(u8 lit);
void module_pause_toggle(void);
u8 module_lid_check(void);
void module_panel_box(u8 mode);
void module_dots_small(u16 x0, u16 y0);
void module_dots_large(u16 x0, u16 y0);
void module_arrow_fwd_3(u8 lit);
void module_arrow_back_2(u8 lit);
void module_arrow_fwd_2(u8 lit);
void module_arrow_back_1(u8 lit);
void module_progress_bar(u16 percent);
void module_panel_blink(u8 on);
void module_speed_show(u16 value, u16 hold);
u8 module_flash_step(u8 what);
void module_minutes_left(u8 mode);
void module_ask_time(u8 *step);
const u8 *module_colour_record(u16 *w, u16 *h, u16 *px, u16 *py, u8 index);
void module_colour_bitmap(u8 index);
void label_colours_picture(void);
void module_count_label(void);
void module_size_labels(short across, short down);
void label_percent(void);
void module_hoop_check(u8 *step);

/* app_modmath.c -- the module's floating point, and the geometry it works out */
u32 f2u(float f);
float u2f(u32 u);
float float_frexp(float x, short *power);
float float_modf(float x, float *ip);
float float_atan_core(float x);
float float_atan(float x);
float float_sincos(float x, u8 quad);
float float_sin(float x);
float float_cos(float x);
float float_sqrt(float x);
u8 pattern_record_at(u8 *rec, u8 index);
void pattern_half_extent(u16 *half_w, u16 *half_h, u8 slot);
void pattern_run_extent(void);
u8 module_link_quiet(void);
void module_fetch_step(u8 *step);
u8 module_hoop_fits(void);
void module_run_step(u8 *step);
void module_state_machine(void);
void module_screen_step(void);
void module_link_lost(void);
void module_wait_pass(void);
void module_reset_wait(void);
void module_key(u8 key);

/* app_body.c -- the screen bodies' helpers */
void screen_body_02(void);
void screen_body_03(void);
void screen_body_30(void);
void screen_body_00(void);
void screen_body_05(void);
void screen_body_06(void);
void screen_body_25(void);
void screen_body_26(void);
void screen_body_3E(void);
void screen_body_0D(void);
void screen_body_0B(void);
void screen_body_0E(void);
void screen_body_0F(void);
void screen_body_39(void);
void screen_body_3B(void);
void screen_body_3C(void);
void screen_body_3D(void);
void screen_body_19(void);
void screen_body_1A(void);
void screen_body_1B(void);
void screen_body_1D(void);
void screen_body_1E(void);
void screen_body_20(void);
void screen_body_22(void);
void screen_body_4A(void);
void screen_body_48(void);
void screen_body_10(void);
u8 screen_body_49(void);
void screen_body_4C(void);
void screen_body_28(void);
void screen_body_29(void);
void screen_body_2A(void);
void screen_body_2C(void);
void screen_body_2B(void);
void screen_body_2D(void);
void screen_body_1F(void);
void screen_body_31(void);
void screen_body_41(void);
u16 list_holding(u16 number, u32 *which);
u8 goto_pattern_number(u16 number);
long str_to_long(const char *nptr, const char **endptr, short base);
void number_keypad_screen(u8 fresh);
void screen_body_08(void);
void queue_strip_run_draw(u16 y, u16 first, u16 last);
void queue_strip_arrows(void);
void queue_strip_scroll_up(void);
void queue_strip_scroll_down(void);
u16 queue_row_back(u16 *first, u16 from);
u16 queue_row_first(u16 upto);
void queue_strip_forward(void);
void queue_strip_back(void);
void queue_strip_screen(u8 fresh);
u8 sew_param_a_get(void);
void sew_param_a_load(void);
void sew_param_b_load(void);
void stitch_width_load(void);
u8 hitbox_flag(u16 index);
void long_to_decimal(u32 v, char *out);
u8 queue_get_low6(u16 rec);
void screen_stack_clear(void);
u8 module_screen_free(void);
void module_hoop_up(void);
void module_hoop_up_right(void);
void module_hoop_right(void);
void module_hoop_down_right(void);
void module_hoop_down(void);
void module_hoop_down_left(void);
void module_hoop_left(void);
void module_step_picture(u8 which, u8 on);
void module_switches_ask_12(void);
void module_switches_ask_13(void);
void module_switches_toggle(void);
u8 module_hoop_home(void);
void module_hoop_leave(void);
void module_hoop_up_left(void);
void module_hoop_reset(void);
void stream_clear(void);
void module_label_right_top(const char *str);
void module_label_right_mid(const char *str);
void module_label_right_low(const char *str);
void module_label_right_foot(const char *str);
void module_label_mid_top(const char *str);
void module_label_mid_second(const char *str);
u8 module_nothing_to_report(void);
u8 module_hoop_sewable(void);
u8 module_slot_is_plain(void);
u8 module_colour_check(u8 asked);
void plot_pixel_back(u16 x, u16 y, u8 colour);
void module_area_clear_front(void);
void module_area_clear_back(void);
void module_box_clear_back(u16 x0, u16 y0, u16 x1, u16 y1);
void module_box_outline(u16 x0, u16 y0, u16 x1, u16 y1);
void hitbox_repress(u8 box);
void module_strip_press(u8 box);
void module_go_check(void);
void module_go_report(void);
void module_area_restore(void);
void module_box4_press(u8 on);
void module_box34_pick(u8 three);
void module_lit_box(u16 box);
void module_box3_grey(void);
void module_boxA_grey(void);
void module_area_save(void);
void module_go_settings(void);
void module_label_speed(void);
void module_colours_show(void);
u8 module_fault_report(u8 code);
void module_frame_front(u8 colour);
void module_frame_back(u8 colour);
void module_colour_swatch(void);
void module_start_step(u8 code);
void module_colour_run(void);
void module_colour_back(void);
void module_sew_screen(void);
void module_turn_screen(void);
void module_panel_screen(void);
void module_extra_screen(void);
void module_size_shrink(void);
u8 module_slot_changed(void);
void module_sizes_screen(void);
u8 module_turn_fits(u8 turn, u8 up, u8 by);
void module_hoop_screen(void);
void embroidery_panel_save_b(void);
u8 module_box10_live(void);
void module_colours_dither(void);
void module_colour_step_service(void);
void module_reset_walk(void);
u8 module_machine_running(void);
void module_measure_second(void);
void module_measure_first(void);
void module_label_hook(void);
void module_size_show(u16 across, u16 down);
void module_scale_show(u16 across, u16 down);
void module_grid_draw(void);
void module_hoop_pictures(void);
void module_hoop_outline(void);
u8 module_run_fetch(u16 which);
void module_hoop_marks(void);
void module_block_draw(const u8 *rec, u8 colour, u8 mode);
void module_stitches_walk(u8 which, u8 colour, u8 mode, u8 skip);
void module_stitches_draw(void);
void module_screen_tick(void);
u8 module_sew_step(u8 which);
void module_design_corners(void);
void module_design_outline(void);
void module_slots_clear(void);
void link_line_release(void);
void module_box_clear(u16 x0, u16 y0, u16 x1, u16 y1);
void module_page_arrow_back(u8 on);
void module_page_arrow_on(u8 on);
void module_list_arrow_on(u8 on);
void module_list_arrow_back(u8 on);
void module_thumb_row_draw(u8 row, u16 first, u8 count);
void module_thumb_draw(u16 x, u16 y, u16 wide, const u8 *bits);
void module_strip_scroll(u8 n, u8 right);
void module_strip_scroll_at(u8 n, u8 right);
void module_cursor_line(u8 on);
void module_list_insert(u8 width, u16 number);
void module_list_remove_draw(u8 n, u8 from);
void module_thumb_page_back(void);
void module_thumb_page_on(void);
void module_list_back(void);
void module_list_forward(void);
void module_list_delete(void);
void module_pattern_add(u8 box);
void module_go_stitch(void);
void module_go_menu(void);
void module_go_sewing(void);
void module_send_step(void);
void module_pattern_screen(void);
void screen_body_38(void);
void screen_body_37(void);
void screen_body_24(void);
void screen_body_23(void);
void screen_body_4E(void);
void screen_body_15(void);
void screen_body_16(void);
u8 hoop_nudge_screen(void);
u8 module_settings_screen(void);
u8 stitch_width_get2(void);
u8 sew_param_a_get2(void);
u8 sew_param_b_get2(void);
void stitch_width_put(u8 v);
void sew_param_a_put(u8 v);
void sew_param_b_put(u8 v);
void bar_needle(u16 value, u16 limit, u8 fresh, u32 buffer, u8 colour);
void pattern_settings_write(u16 n);
void pattern_settings_store(void);
u8 module_busy_screen(void);
u8 needle_position_screen(u8 fresh);
void service_marks_draw(u8 fresh);
u8 sew_settings_screen(u8 fresh);
void module_switches_stop(void);
u8 sew_param_b_get(void);
u8 stitch_width_get(void);
void hitbox_numbers_draw(u16 first, u16 last, u16 start);
void picker_range_mark(u16 first, u16 last, u16 which);
void picker_range_close(u16 box, u16 which);
void percent_bar_draw(void);
void picker_strip_screen(u16 mode);
void queue_settings_track(u8 fresh);
void queue_edit_screen(u8 fresh);
void screen_body_17(void);
void screen_body_18(void);
void screen_body_42(void);
void screen_body_43(void);
void screen_body_46(void);
void screen_body_2E(void);
void screen_body_2F(void);
void screen_body_3A(void);
void screen_body_01(void);
void screen_body_32(void);
void screen_body_33(void);
void screen_body_1C(void);
void screen_body_27(void);
void screen_body_0C(void);
void screen_body_4B(void);
void screen_body_4D(void);
void screen_body_3F(void);
void screen_body_0A(void);
void screen_body_21(void);
void queue_edit_press(void);
void screen_dispatch(void);
void sew_picture_box(void);
void needle_stop_picture(void);
void speed_number_draw(u8 fresh);
void width_strip_draw(u8 fresh);
void list_arrows(u16 index, u16 span, u16 back_box, u16 fwd_box, u8 fresh);
void picker_strip_restore(void);
void demo_screen_step(u8 fresh);
u8 setting_toggle_C6(u8 fresh);
u8 setting_toggle_C7(u8 fresh);
u16 list_page_start(void);
u8 screen_only_77(void);
void needle_number_draw(void);
u8 choice_screen_17A(void);
u8 choice_screen_1E3(void);
void menu_embroidery(void);
void menu_category(void);
void menu_four_ways(void);
u8 menu_three_lists(void);
u8 menu_five_categories(void);
u8 menu_six_categories(void);
u8 menu_twelve_choice(void);
u8 display_test(void);
u8 trim_screen(void);
u8 pedal_test_screen(void);
u8 variant_screen(void);
u8 needle_choice_screen(void);
void screen_slot_two_screen(void);
void offset_number_draw(u8 value, u8 which);
void stitch_size_screen(u8 fresh);
u8 stitch_length_screen(u8 fresh);
void panel_marks_match(const u16 *shown, const u16 *wanted, u8 tell);
void module_version_text_draw(void);
u8 module_version_press(u8 fresh);
u8 touch_cal_screen(void);
u8 max_speed_screen(u8 first_pass);
void module_menu_screen(void);
void status_bar_refresh(u8 redraw_all);
u8 needle_pos_screen(void);
void screen_mark_repaint(u8 screen);
u8 version_screen(void);
u8 menu_four_screens(void);
u8 module_version_screen(u8 first_pass);
u16 screen_stack_depth(void);
u8 screen_stack_at(u16 n);
void drawing_reset(void);
u32 help_picture(u16 entry, u16 part);
u32 help_picture_module(u16 entry, u16 part);
void version_text_draw(const char *s);
void cursor_blink(u16 x, u16 y);
u8 link_owner_waiting(void);
void hitbox_third_mark(u16 box, u8 which);
void hitbox_run_shift(u16 first, u16 last, u16 dest);
void balance_bar_draw(u16 value);
void preview_stroke_draw(u16 index, u8 alt, u8 on);
u16 hitbox_list_scroll_on(u16 first, u16 last, u16 step);
u16 hitbox_list_scroll_back(u16 first, u16 last, u16 step);

/* app_press.c -- the module's state machines, the touch hit test, and what a press does */
u8 module_can_talk(void);
void label_percent_left(u8 value);
void module_stop_sequence(u8 *step);
void module_to_screen_15(void);
u8 module_send_0B(void);
u8 module_run_control(u8 what);
u32 module_reply_buffer(void);
void screen_store1_clear(void);
void embroidery_panel_save(void);
void module_cursor_erase(void);
void pattern_mark_ready(void);
u8 module_home_request(void);
void module_talk_end(void);
void module_restart(void);
u8 module_identify(void);
void *mem_move(void *dst, const void *src, u32 len);
void screen_store(u8 slot, u8 out);
u8 link_wait_idle(void);
void picker_arrows(u16 back_box, u16 on_box, u8 fresh);
void hitbox_select_current(u16 first, u16 last);
u8 remote_hit(u16 first, u16 last, u16 *out_value, u16 *out_index);
u8 touch_hit(u16 first, u16 last, u16 *out_value, u16 *out_index);
void screen_action(u16 value, u16 index, u8 second);

/* app_sci.c -- SCI0: the embroidery module's link, and its three interrupts */
void link_send_start(void);
void link_send_stop(void);
void link_clear_11A612(void);
void link_delay(u16 units);
void link_gap_100(void);
void link_gap_100b(void);
void link_gap_10(void);
void link_gap_350(void);
void link_clear_0FFC18(void);
void link_clear_0FFC16(void);
void isr_sci0_eri_body(void);
void isr_sci0_txi_body(void);
void isr_sci0_rxi_body(void);

/* app_panel.c -- the screens, the hoop, the panel's fields and switches, and its strip */
void isr_sci0_eri(void);
void isr_sci0_txi(void);
void isr_sci0_rxi(void);
u8 screen_back_one(void);
u8 message_wait_screen(void);
u8 foot_pressure_screen(u8 first_pass);
void stitch_stroke_toggle(u8 variant, u8 pressed);
u8 pattern_list_screen(void);
void help_page_draw(void);
u8 beep_settings_screen(u8 first_pass);
void pattern_strip_restore(u8 screen);
u8 stroke_pick_screen_a(u8 first_pass);
u8 stroke_pick_screen_b(u8 first_pass);
void hoop_offsets_draw(void);
u8 hoop_move_screen(void);
u8 menu_ten_categories(void);
u8 pick_screen_4(u8 first_pass);
u8 pick_screen_3(u8 first_pass);
u8 pick_screen_2(u8 first_pass);
u8 pick_screen_1(u8 first_pass);
u8 main_menu_screen(void);
void dialog_show(u16 which);
u8 settings_menu_screen(u8 first_pass);
void module_letter_box(u32 picture);
void panel_field_update(u16 which, u8 fresh);
u16 panel_switch(u8 key, u16 box, u8 step, u8 clear);
u16 list_position(const u16 *list, u16 value);
void panel_strips_add_45(void);
void panel_strips_drop_45(void);
u8 panel_marks_screen(u8 fresh);
void panel_any_set(u16 box, u16 last);
u8 stitch_record_kind(u16 n);
void panel_strip_box(u8 key, u16 box, u8 fresh);
void panel_strip_draw(u16 first, u16 last, u8 fresh);
void panel_strip_choose(void);
u8 picture_choice_screen(void);

/* app_queuerec.c -- the queue's own list and records, its ranges, and what a press does */
u8 picker_may_leave(void);
u8 queue_get_bit6(u16 rec);
u8 queue_get_bit7(u16 rec);
u8 queue_get_top3(u16 rec);
u8 queue_get_low2(u16 rec);
u8 queue_get_bit3(u16 rec);
u8 queue_get_high4(u16 rec);
void pattern_params_publish(u16 n);
void queue_entry_reset(void);
void pattern_reset_current(void);
u16 queue_run_length(u16 from);
u16 queue_run_extra(u16 first, u16 last);
void item_descriptor_copy(u16 from, u16 to);
void queue_items_renumber(void);
u16 queue_entry_delete(void);
void list_insert(u16 *list, u16 at, u16 value);
void beep_record_byte(u16 n, u8 v);
void beep_pair_store(u16 v);
void queue_put_number(u16 rec, u16 v);
void queue_put_mid4(u16 rec, u8 v);
void queue_put_bit6(u16 rec, u8 v);
void queue_put_bit7(u16 rec, u8 v);
void queue_put_byte2(u16 rec, u8 v);
void queue_put_byte3(u16 rec, u8 v);
void queue_put_byte5(u16 rec, u8 v);
void queue_put_byte6(u16 rec, u8 v);
u8 queue_get_byte5(u16 rec);
u8 queue_get_byte6(u16 rec);
void queue_put_byte10(u16 rec, u8 v);
void queue_put_low6(u16 rec, u8 v);
void queue_put_top3(u16 rec, u8 v);
void queue_put_low2(u16 rec, u8 v);
void queue_put_bit3(u16 rec, u8 v);
void queue_put_high4(u16 rec, u8 v);
void queue_put_params(u16 rec, u16 pattern);
void queue_number_bump(void);
void queue_record_fill(u16 rec);
void queue_make_room(u16 at);
u8 queue_add_entry(u16 number, u16 offset);
u8 picker_pos_low(void);
u8 picker_one_only(void);
u8 picker_at_start(void);
void queue_number_drop(void);
void list_delete(u16 *list, u16 at);
void queue_remove_record(u16 at);
void pattern_take_up(u16 n);
void pattern_flags_clear_all(void);
void screen_hand_over(u8 screen);
void queue_reload(void);
void queue_ranges_shift(void);
void queue_save_ranges(void);
void queue_delete_entry(void);
void pattern_state_reset(void);
void stitch_working_load(u16 n, u8 announce);
void stitch_working_save(u16 n);
u16 hitbox_run_scroll(u16 first, u16 last);
void screen_back_out(u8 screen);
u8 screen_touch(void);

#endif /* APP_H */
