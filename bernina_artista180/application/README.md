# Application, rewritten in C — work in progress

The other half of the ROM. Where `../bootrom` covers `H'000000`–`H'002FFF`,
this covers the application: the code the boot ROM hands over to.

    make            # -> app.bin, and the mergeapp host tool
    ./mergeapp -a app.bin -m ../Bernina180_20260816.bin -o merged.bin

## Scope, measured rather than estimated

| | |
| --- | --- |
| code region | `H'200000`–`H'250FFF`, 324 KB |
| distinct routines in it | 1,310 |
| executed booting to an idle screen | ~56 KB across 66 spans |
| executed before the display is first touched | ~4,600 instruction addresses |
| `H'251000`–`H'27FFFF` | erased |
| `H'280000`–`H'3EDFFF` | 1.46 MB of pattern data — not code |
| live entry/vector slots | 12 of 64 |

`tool/code_census.dart` produces the execution figures and
`tool/first_display.dart` the path to the display; they are what the ordering
below is based on. For scale: the boot ROM was 12 KB and about 70 routines.
This is not that job again, it is roughly twenty of them.

## Layout

    H'200000   entry table, 64 longwords
    H'200100   identity block: "NMMV03.01", "English", "Bernina
               Electronic AG", "July 98" — fixed-width fields, H'FF filled
    H'200200   code

The table is not the CPU's vector table; the boot ROM owns that, at address
zero. Most of its slots are trampolines that fetch a handler out of *this*
table, so an entry here is what makes the application see an interrupt.
Slot 0 is the entry point and the boot ROM checks that its top byte is zero
before handing over — that is how it decides an application is present at
all. Slot 1 is what the boot ROM's `'G'` command jumps to.

`mergeapp` splices only as far as the built image reaches. Above it sits the
original's own machine code, still at its original addresses, and above that
the pattern data. Both are left exactly as they were.

## Status

**Part 1, scaffold — done.** The boot ROM hands over to the rebuilt entry and
`'G'` reaches the alternate entry and answers `O`, both byte-identical to the
original (`G` → `GO`, `IG` → banner + `GO`).

**Part 2, startup and the RAM map — done.** `memory_check`, `cold_start` and
the two fill primitives. Checked by running both images to the same stage —
the original to `app_init` at `H'208E10`, the rebuild to its own — and
comparing every region the clear is supposed to touch:

| region | bytes | |
| --- | --- | --- |
| `H'11A24A`–`H'11F5A7` | 21,342 | identical |
| `H'FFFEC0`–`H'FFFF0F` | 80 | identical |
| `H'0E0000`–`H'0E17FF` | 6,144 | identical |
| `H'0E4000`–`H'0E800F` | 16,400 | identical |
| `H'0FFE00`–`H'114D49` | 85,834 | identical |
| `H'114D4A`–`H'114DC1` | 120 | identical |
| `H'0E8010`–`H'0FFC12` | 97,283 | identical |
| `H'040000`–`H'044AFF` | 19,200 | identical |
| `H'044B00`–`H'0495FF` | 19,200 | identical |

265 KB of RAM matching. `tool/ram_compare.dart` does this.

Two findings worth recording, because both look like stubs and are not:

* `memory_check` (`H'2007AC`) is two instructions — load 1, return. There is
  no test; every start is a cold start.
* `H'2007B0` is `BRA H'2007B0`. It is not the main loop, it is the trap the
  entry jumps to if `app_init` ever returns — which it never does.

The main loop is `app_init` (`H'208E10`) itself:

```
sub_208D88();                        /* brings the machine up */
if (H'FFFEC4 bit 7) for (;;) sub_20BEE2();
else                for (;;) sub_2086B6();
```

Two loops, chosen by a bit that is very likely the button held at power-on —
which would make the first of them the service mode.

**Part 3 — in progress.** Split into sub-parts; see below.

**Part 4, the display — done.** Everything `H'216D6C` reaches: 217 routines.

**Part 5, the last five stubs — three closed, two turned into subsystems.**
Service mode, the diagnostics screen and the settings save are written and
checked, and both main loops are real. What is left standing behind them is
the screen dispatcher (`H'22382A`, 352 routines) and the embroidery module
(`H'2354AE`, 156).

**Part 6, the interrupt handlers — the seven timer slots.** The four
steppers, the two that measure the sewing speed and the millisecond, and the
forty routines under them. The machine has a timebase in the rebuild for the
first time.

**Part 7, SCI0 — done, and with it all ten interrupt slots.** The embroidery
module's protocol: fifteen messages each way over five nested dispatch
tables.

**Part 8, the screen dispatcher — begun.** Measured at 350 routines and
~47,000 instructions, more than twice parts 1–7 together. The hit-box table
and the five routines with the highest fan-in in it -- the button layer, the
list filler and the two hit tests -- are written and checked. 327 routines
remain, the action dispatcher -- what a press does -- is written, and so are
the two bars every screen draws. **1,465 cases pass over 350 routines**, with
321 routines still behind the dispatcher.

**Part 9, the harness — done.** The suite went from twenty-five minutes to
under five seconds: the memory keeps an undo log instead of the comparer
copying four megabytes a run, and each image is booted once and lent to case
after case instead of twice per case. **1,477 cases pass over 354 routines.**

Nothing draws yet: the last call in the bring-up, `sub_2105C4`, is still a
stub, and that is the one that reaches the LCD.

### Part 3's shape

`tool/call_census.dart` records what is actually called between two points.
From `sub_208D88` to the first touch of the LCD controller: **190 distinct
routines, 549 calls, maximum depth 38**. That is the work list.

`sub_208D88` itself is the spine — twenty calls in order, and every one of
them is now a reconstructed routine in `app.c` rather than a stub:

```
set_flash_page_buffer   port_shadows_init       adc_start_conversion
input_state_init        sci0_module_init        i2c_init
config_to_eeprom        config_block_check      stitch_database_open
port_c_init             pins_to_input_2061A0    port_b_init
main_motor_init         motors_and_timers_init  keys_scan_settled
analog_input_init       speed_control_init      display_init
```

The one qualification is `stitch_database_open` (`H'208826`): its own body is
written out, but the two calls under it are not — see part 3t.

**Part 3a, the I2C EEPROM — done.** Ten routines, about a fifth of all the
calls made on the way to the display. Bit-banged on port 4: SDA on bit 7,
SCL on bit 6, talking to a 24Cxx at H'50/H'51 — the machine's settings
store. Also `bus_init` (`H'2009E8`), which sets the external bus up
differently from the boot ROM.

Checked by waveform. `tool/port_trace.dart` calls a routine and records the
port after every instruction, so the two can be compared on what appears on
the wire rather than on anything about the code — which is just as well,
since they are compiled differently and take their arguments differently:

| | transitions | |
| --- | --- | --- |
| `i2c_init` | 6 | identical |
| `eeprom_write_verify(H'12, H'5A)` | 127 | identical |
| `eeprom_write_verify(H'00, H'FF)` | 119 | identical |
| `eeprom_write_verify(H'7F, H'01)` | 121 | identical |

Return values agree too. Both images have to start from the same pin and
shadow state for this to mean anything — `--poke` does that — because the
original has run more initialisation by the time it gets there.

One thing worth knowing: P4DDR is write-only, so its intended value is kept
in RAM at `H'FFFD30` and written through from there. Reading the port back
to modify it would return pin states, not what was last asked for.

**Part 3b, the display bring-up — done.** `sub_2105C4` and its subtree,
which is far smaller than the whole: **24 routines, maximum depth 4**.

What it does, and what is now written:

* eleven one-instruction accessors, each returning the address of a table.
  Kept as named constants rather than functions, because the addresses are
  the only thing about them that matters. Five point into the pattern data
  above `H'280000`, which stays where it is.
* `lcd_controller_init` (`H'20E062`): fifteen register writes and no loops.
  The second write to register 1 is what turns the controller on — the first
  sets the mode while it is still off.
* `mem_fill` (`H'2104E2`) and `buffer_fill` (`H'20E126`), which clear the
  three 19,200-byte buffers. The fill calls a service hook (`H'20F0FE`)
  between every byte, so that whatever needs attention while the CPU spends
  19K writes clearing a buffer still gets it.
* the touch calibration moved out of flash at `H'57FFA0` into RAM at
  `H'11A87E`, where the rest of the code reads it.

Checked after a full boot, against the original:

| | |
| --- | --- |
| all sixteen LCD controller registers | identical |
| touch calibration, 16 bytes | identical |
| the six table pointers at `H'11B29E` | identical |

The calibration bytes are the IEEE floats behind the scale factor the touch
mapping uses — `3FA9B948` is 1.3259, which is the number the display
coordinates were derived from long before any of this was rewritten.

**Nothing is drawn yet, and that is expected.** After a full boot the
original has 6,818 non-zero bytes in the frame buffer and the rebuild has
none: the controller is programmed and the buffers are cleared, but the
painting happens in the routines still stubbed — `build_tables_210C20`,
`display_init_210CB0`, `display_init_2120A2`, `display_init_223010` and the
tail of `sub_2105C4` — and in the main loop.

**Part 3c, the rest of the display bring-up — mostly done.** The four
routines `sub_2105C4` calls after the controller is running:

* `build_tables` (`H'210C20`) — five more table addresses, then 27 longwords
  copied out of `H'115066` into `H'11595E`. Entry zero is not copied; the
  loop starts at one.
* `splash_and_config` (`H'210CB0`) — the boot splash, and one configuration
  bit. The splash only runs when `H'57EFC6` says so, and in this image it
  does not, which is why the machine goes straight to its normal screen.
  Which of two pictures it would show depends on the configuration byte at
  `H'57FF80` — and `H'B4`, the value that selects the second one, is the same
  value the download protocol reads as "no embroidery session". One byte,
  two jobs.
* `display_init_223010` (`H'223010`) — two calls and nothing else.
* `scan_2120A2` (`H'2120A2`) — **not reconstructed.** A scan of 1024 entries
  of a 24-byte structure at `MEM32(H'114DD2)`, classified against the
  configuration byte. A different kind of job from the rest of this part.

Checked after a full boot:

| | |
| --- | --- |
| the five table addresses at `H'11B2B6` | identical |
| the 27 longwords copied to `H'11595E` | identical |
| `H'FFFEC1` bit 6 | set, as in the original |

`H'FFFEC1` reads `H'54` in the original and `H'40` in the rebuild. Bit 6 is
the one `splash_and_config` sets and it agrees; bits 2 and 4 are set by
routines that have not been written yet.

### What actually paints

Worth recording, because it was not what I expected. None of these four
routines draws anything. `tool/painters.dart` watches the frame buffer and
reports the instructions that write to it: after the start-up clear, the
first real painting happens at step 4.17M, from `H'20F43C` and a family of
primitives around `H'20E1DC`-`H'20E6B8` — well after the display bring-up
finishes at about 3.2M. They are called from the main loop.

So the screen comparison does not come back with the display init. It comes
back with the drawing primitives and enough of the main loop to call them,
and that is the next thing worth doing.

**Part 3d, the pixel plotter — done.** `H'20E154`, and it is the primitive
everything else draws through.

Two bits per pixel, four to a byte, 80 bytes to a line. The original works
the byte out from `x >> 3` — an eight-pixel, two-byte group — and then
dispatches through a table at `H'20E1A0` of eight routines on `x & 7`, one
per pixel position, each with its own shift and mask spelled out. That is
the same arithmetic as one expression, and the table is not reproduced: it
exists because the original was compiled that way, not because anything
depends on it. The service hook runs on every pixel, as it does in the fills.

Checked against the original by calling both with the same pixel and
comparing the byte that comes out, which is the only way to compare them —
they take their arguments quite differently (the original: x in `R6` and the
rest on the stack; the rebuild: `r0`, `r1`, `er2` and a stack slot):

| x, y, colour | byte written | |
| --- | --- | --- |
| 0,0,3 | `C0` at `H'040000` | identical |
| 1,0,2 | `20` | identical |
| 2,0,1 | `04` | identical |
| 3,0,3 | `03` | identical |
| 4,0,3 | `C0` at `H'040001` | identical |
| 7,0,1 | `01` | identical |
| 8,0,3 | `C0` at `H'040002` | identical |
| 0,1,2 | `80` at `H'040050` | identical |
| 319,239,3 | `03` at `H'044AFF` | identical |

The last is the bottom-right corner landing on the last byte of the buffer.

`tool/call_at.dart` does this: it calls a routine at an address with
registers and stack words placed as asked, and dumps what changed.

### How much main loop the picture needs

More than expected, and worth knowing before starting. From `sub_2086B6` to
the first paint: **151 routines, 34,390 calls, maximum depth 251**. That is
larger than the display bring-up and larger than everything reconstructed so
far put together, and it is not a chain that can be cut short — the picture
is what the main loop exists to produce.

`sub_20F192`, the routine that does most of the drawing, is itself a large
one with a deep call graph.

## Checking a routine against the original

Most of the application is too deep in the call graph for anything
observable from outside to judge. A routine three hundred calls below the
main loop cannot be checked by looking at the screen, and waiting until
enough of the machine works to draw one would mean writing hundreds of
routines on nothing but hope.

So each routine is checked on its own. `tool/compare_routines.dart` calls the
original and its replacement with equivalent inputs and compares what each
one did:

    dart run tool/compare_routines.dart bernina_artista180/application/routines.json \
        --rebuilt merged.bin

"Equivalent inputs" has to be spelled out per routine, because the two do not
agree on where arguments go. The original puts the first in `ER6` and the
rest on the stack; GCC uses `ER0`, `ER1`, `ER2` and then the stack — and a
byte argument that lands on the stack occupies a four-byte slot, with the
value in the last of them. So `routines.json` says where each side's
arguments belong and the harness places them.

Two things it compares:

* **the result**, named by register and width — `r6l` is a byte, `r6` a word,
  `er6` the whole register. A routine returning a byte leaves working values
  in the rest of the register, and comparing those would fail on rubbish. A
  routine that returns nothing says so, and then only memory is compared.
* **every byte of memory that changed**, found by snapshot rather than by
  naming ranges, so a routine that writes somewhere it should not is caught
  without anyone having to predict where. The application's own code region
  is excluded, since the two images differ there by construction, and so is
  the stack below the pointer, where each side leaves its own frame.

Routines are named by symbol and resolved through `app.sym`, which the build
writes, so cases keep working when addresses move — which they do on every
build.

**The harness is checked against a deliberate mistake**, because one that
only ever passes proves nothing. Changing the plotter's bit position from
`6 - 2 * (x & 3)` to `2 * (x & 3)` makes all nine of its cases fail, each
naming the address and both values:

    FAIL  plot_pixel x=3 y=0 colour=3
          H'040000: original 03, rebuild C0

Two ways a case can pass without testing anything, both of which have
happened here:

A case whose comparison reports **0 bytes written** is worth suspecting. The
dump already holds the result of the original having run, so a routine that
recomputes the same answer changes nothing and the comparison passes whether
or not either side did any work. `"fill"` puts a known pattern over the range
first, which forces both sides to write it and makes the comparison mean
something. `scan_items` went from "0 bytes written" to 1,144 that way, and
only then did it catch anything.

And a case can pass because *both sides do nothing*. The first A/D cases
seeded the converter's result registers and compared the average that came
back — which was zero on both sides, because the simulator's own converter
model overwrites those registers as soon as a conversion runs. `"analog"`
drives the model's inputs instead, and the same cases then return `H'5A`,
`H'7E` and `H'90` for three different channels.

The lesson in both is the same: a comparison is only worth what the inputs
make it worth, and it is worth looking at what a passing case actually
produced.

**Part 3e, `scan_items` (`H'2120A2`) — done.** The menu index.

A table of `H'18`-byte descriptors at the address in `H'114DD2` is walked
four times, once per list, taking a different range of the category byte each
time — below `H'10`, `H'12` and above, exactly `H'11`, exactly `H'10`. Each
list is a count followed by that many table indices, laid down immediately
after the one before, with the addresses chained through `H'11B096`,
`H'11B09A` and `H'11B09E`. Entries are grouped in fives, a menu row, and a
change of category pads out to a row boundary first.

Three things the decompiler rendered in a way that read as something else,
each caught by the comparison rather than by re-reading:

* **Category 2 ends the table**, it is not an entry to pass over. Its branch
  goes to the loop's exit, not its increment, and the decompiler showed that
  as an empty `if` with the body in the `else`. On this machine the
  terminator is at index 959, so the walk stops well short of the 1024 the
  loop bound allows — and the stale entries beyond it are what a `continue`
  wrongly pulls in. That alone put five extra entries in the first list.
* **Both the `category == 1` and the `previous == 1` tests jump to the same
  place**, not to two arms of an if/else. The decompiler split them, which
  made one path look unreachable.
* **There are four passes, not two.** The third and fourth only became
  visible once the first two matched and the comparison pointed at bytes
  past the end of the second list.

Checked with the lists clobbered first, so both sides have to rebuild them:
**1,144 bytes written, identical**, and the same in situ after a full boot —
list 1 holds `H'123` entries and the three chained pointers agree.

**Part 3f, the tail of `sub_2105C4` — done**, along with the two leaf
routines it needs:

* `first_item_of_category` (`H'212994`) — the position in a list of the first
  entry with a given category that has data behind it, counting from one, or
  1 when there is none. The tail uses it to choose what the machine starts
  on. The original copies each descriptor into a frame before looking at it,
  which changes nothing outside that frame, so the rebuild reads the table
  directly.
* `filter_unlisted` (`H'21088E`) — copies one list to another, leaving out
  the category-1 entries.
* The tail itself: the chosen position, stored twice for the display and the
  menu, and a dozen settings gathered into the `H'11B2xx` block.

Worth noting, because the two are written the other way round: the pointer
test in `first_item_of_category` is "`H'B4` selects the second pointer" while
in `build_item_list` it is "`H'AA` selects the first". They agree on this
machine, whose configuration byte is `H'B4`, and both are reproduced as they
stand rather than made to match.

21 cases now pass, including three for `first_item_of_category` that return
genuinely different answers — category 3 gives 1, category `H'0E` gives
`H'105`, and a category that is not there gives 1.

### A difference that is not a fault

After a full boot the `H'11B2xx` block does not match the original, and it
should not yet. The tail copies from `H'FFFEE0`, `H'FFFEE7`, `H'FFFEEA` and
`H'11A169`, and those are set by bring-up routines that are still stubs — so
the sources differ and the copies differ with them. Checked directly: every
destination holds exactly what its own machine's source holds. The chosen
position, which the tail works out for itself, agrees.

**Part 3g, the last two of the display bring-up — done**, and with them the
whole of it can be compared in one go.

* `mem_set` (`H'200762`) — a byte fill with a word count, widened before the
  loop; a count of zero fills nothing.
* `first_index_of_category` (`H'21073E`) — the table index of the first entry
  with a given category, or `H'FFFF`. Unlike the list builder this does *not*
  stop at the category-2 terminator; it scans all 1024. That is how the next
  routine finds the terminator itself, by asking for category 2.
* `build_consecutive_lists` (`H'210808`) — two identical lists of consecutive
  indices starting just past the terminator. Its length comes out of the
  terminator's own descriptor, at offset `H'14`: the entry that marks the end
  of the table also says how many follow it.
* `finish_22950C` (`H'22950C`) — clears the two working areas the drawing
  code builds into and sets three values it starts from.

### The whole of `display_init`, compared in one case

With nothing left stubbed on the path, `sub_2105C4` and its replacement can
be called head to head:

    pass  display_init -- the whole of sub_2105C4
          (1206959 vs 892394 steps, 15467 bytes written)

**15,467 bytes, identical**, over 1.2M instructions of the original — the
tables, both display buffers, the menu lists and their chained pointers, the
touch calibration, and the settings block. That is a much stronger statement
than the individual cases: it catches ordering, and anything one routine does
that another depends on.

The same holds in situ after a full boot.

29 cases now pass over 17 routines.

**Part 3h, the first two calls of the bring-up — done.**

* `set_flash_page_buffer` (`H'2007B2`) — points `H'FFFD10` at `H'0FFC14`.
  That is the pointer the *boot ROM's* flash routines read to find their
  256-byte staging area. The application owns the memory and tells the boot
  ROM where it is, which is what makes a byte written through the download
  protocol land somewhere sensible. Verified in situ: both machines hold
  `00 0F FC 14` after a full boot.
* `port_shadows_init` (`H'208D22`) — the external bus, then the starting set
  of shadows from `H'FFFD30` up, and two device latches out on the bus at
  `H'0A0000` and `H'0C0000`. Registers that cannot be read back have their
  intended value kept in RAM; `H'FFFD30` is port 4's data direction, which
  the I2C driver reads and writes on every transfer.

The shadows do not match in situ yet, and should not: later bring-up
routines write them again. The rebuild's `H'FFFD30` reads `H'C0`, which is
the `H'00` this routine lays down plus the two bits `i2c_init` sets on top —
so both routines are doing their part, and the rest of the difference is
routines that have not been written.

31 cases now pass over 19 routines.

**Part 3i, the A/D block — done.** Five routines, and the first real
peripheral driver in the application:

* `adc_start_conversion` (`H'20083E`) — sets ADST and SCAN, leaving the rest
  of the control register alone.
* `adc_get_result` (`H'20084A`) — one reading out of the array at `H'11A251`
  that the scan fills.
* `adc_convert_polled` (`H'200860`) — averages N readings from one channel.
  Two details are worth keeping: the wait for each conversion is bounded, and
  on running out it reads the register anyway rather than failing, so a stuck
  converter gives a stale number instead of a hang; and the budget is spent
  across the whole call rather than reset per sample, so a slow first reading
  leaves less room for the rest.
* `adc_start_channel` (`H'200914`) — points the converter at a channel and
  starts it, handing back what that channel's register still held.

Four result registers serve eight inputs — channels 0 and 4 both land in
ADDRA, 1 and 5 in ADDRB, and so on — and only the high byte is ever read, so
a reading is a byte. The original dispatches on the channel through a jump
table in each of these; the tables are not reproduced, because they decide
nothing an index cannot.

The build now links `libgcc`. The H8 has no 32-bit divide, and
`adc_convert_polled` needs one; the floating point the coordinate maths uses
will need more of it.

39 cases now pass over 24 routines.

**Part 3j, `input_state_init` (`H'208B3C`) — done.** The machine's input and
state block, `H'FFFEC0` to `H'FFFF0F`.

Almost all of it goes to zero; the few values that do not are the
interesting ones. This is where the raw touch coordinates, the A/D samples
and the mode flags live — the same cells the display bring-up copies
settings out of, which is why those copies read zero until this runs.

The comparison writes 79 bytes of an 80-byte range, which is a useful
detail in itself: `H'FFFF0E` is the one byte in the block neither side
touches, and the byte-for-byte comparison is what shows that rather than
anyone having to notice it in the listing.

40 cases now pass over 25 routines.

**Part 3k, `sci0_module_init` (`H'24491C`) — done**, with the pointer table
it sets up.

This brings up SCI0, the embroidery module's link: rate divisor `H'05`,
which is 57600 baud at this machine's clock — the same rate the download
protocol switches the *other* channel to — and the control register set to
receive with the receive interrupt enabled, which is what the application's
vector slots 52 to 54 are for. `pointer_table_init` (`H'2439A2`) lays down
sixteen addresses at `H'0FFE00`: a table of where the link's variables are,
rather than of what to do.

One instruction here cannot be written as C. The original clears RDRF with
`BCLR #6,@SSR0:8`, and neither obvious translation is right:

* `SSR0 &= ~SSR_RDRF` spreads over several instructions, and a character
  finishing between the read and the write-back is lost — the bug that cost
  a debugging session in the boot ROM.
* Writing a constant instead avoids that, but also writes the multiprocessor
  bit, which `BCLR` leaves alone. The comparison caught exactly this: the
  rebuild left `H'FFFFB4` at `H'85` where the original left it unchanged at
  `H'84`.

So it is one line of inline assembly, with the reasoning next to it.

42 cases now pass over 28 routines.

**Part 3l, `config_to_eeprom` (`H'209B0C`) — done.** Small, but it is the
first routine that writes to the settings store rather than reading it, so
it is the first real exercise of the part 3a driver in the direction that
can go wrong.

It takes two bytes from the configuration block in flash at `H'57FF90` and
`H'57FF91` — both `H'53` in this machine's image — and puts each in three
places: a working copy in the input block at `H'FFFEF3`, the serial EEPROM
at `H'A9`, and a second copy at `H'11A812`. Copying flash into the EEPROM at
every start-up is how the settings store is kept in step with the block in
flash, which is the half of the pair the download protocol can rewrite.
Whether the write verified is not looked at.

Fifty-two calls and about four thousand instructions for four bytes of
result, all of it the bit-banged I2C from part 3a.

43 cases now pass over 29 routines.

**Part 3m, `config_block_check` (`H'20888C`) — done**, with the two string
routines it uses.

This is the only part of the bring-up that writes to flash, so it is the
first use of boot ROM vector slot 3. The application reaches it through a
one-instruction thunk at `H'250AEC` — `JMP @@H'0C:8` — and because that is a
jump rather than a call, the two stack arguments sit where the boot ROM's
shim expects without an extra frame. `rom_flash_write` in `app.c` lays them
down in the same order.

`H'57FF80` is the machine's settings block. `H'57FF81` is a stamp: if it
does not read `H'A5` the block has never been written — virgin flash, or a
chip the download protocol has just erased — and the factory values go down,
the stamp last of all, so a machine interrupted part way through comes back
here rather than running on half a block. The factory values against this
machine's:

| | `80` | `81` | `82`–`85` | `86`–`89` | `8A` | `8C` | `8D` | `8E` | `8F` | `90` | `91` | `92` | `94` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| factory | B4 | A5 | 0 | 0 | 0370 | 0F | 8C | 08 | 08 | 40 | 40 | 32 | 0370 |
| this machine | B4 | A5 | 212B | 2BEFC0 | 0370 | 0F | CE | 0C | 08 | 53 | 53 | 32 | 0370 |

So two counters have run and four of the choices have been changed by use.
`H'57FF90` and `H'57FF91` are the pair part 3l copies into the EEPROM, which
is a useful cross-check on both readings: they default to `H'40`, they read
`H'53` here, and `H'53` is what the EEPROM write puts at `H'A9`.

If the stamp is there the only thing checked is `H'57FFB0`, a copy of the
version string of the application that last wrote the block. This machine
holds `NMMV03.01`, which is what `H'200100` holds, so nothing is written.
When they differ the new string is recorded — that is how the machine knows
it has been updated, and it is why a rebuilt application with a changed
identity block would rewrite this on its first run.

The two string routines are the ROM's own `strcmp` (`H'24AB2A`) and `strlen`
(`H'24AB9A`), both with the arguments the original passes them. `strcmp`
returns the difference of the first differing pair taken unsigned, widened
to a signed word, which is what the four cases check: `H'0009`, `H'FFF7`,
`H'FFF8` and zero. Reversing the subtraction in the rebuild fails three of
the four with those exact values, which is what says they are testing
anything.

One thing in the original does not work. After the factory values are
programmed there is a settle loop that was meant to count to 50000. What it
tests is the counter's value *before* the increment, which on the first pass
is zero, so it leaves immediately; the comparison against 50000 is still
computed, into a result nothing reads. It is written out in `app.c` with the
same control flow and the compiler deletes it, which is the correct outcome
either way — the flash has finished by the time the loop is reached.

**What is not checked.** Only the stamped path runs on this machine. The
factory-values path calls `sub_21DDC4`, which saves the settings out to
flash — seven blocks of `H'22` bytes and more — and that is a subsystem of
its own, left as a named stub. Forcing the stamp would just run the rebuild
into the stub, so the harness case covers the taken path and the flag at
`H'114DC7` bit 5, filled beforehand so the clear on the way out is a visible
change rather than a write of what was already there.

50 cases now pass over 32 routines.

**Part 3n, the two port routines (`H'2061A0`, `H'20A38C`, `H'208FE8`) --
done.** Small, and they settle the shadow scheme for good: a write-only
register is changed by taking its shadow at H'FFFD3x, changing one bit,
writing the register, and putting the shadow back -- and the original writes
the register once per bit rather than assembling the byte first, which is
reproduced because it matters to anything watching the pin rather than
sampling it.

They also corrected a register name. H'FFFFD5 is port C's direction
register, not port B's data register: on this part the ports run
PADDR H'FFFFD1, PBDDR H'FFFFD4, PCDDR H'FFFFD5, PADR H'FFFFD3, PBDR
H'FFFFD6, PCDR H'FFFFD7. `port_c_init` drives three port C data bits high
straight after making them inputs, which is how the pin's level is decided
before it becomes an output again.

`port_b_init` also loads the input trim from H'57FF92 and falls back to
H'32 when it is outside H'0B..H'F4.

**Part 3o, the timers and the steppers (`H'20D8AE`) -- done.** Thirteen
calls with nothing below them.

ITU channels 0, 1 and 2 are set to count at the system clock and clear on a
GRA match, with GRA at H'2AF8 on the first two -- a tick a millisecond at
this machine's clock -- and H'044C on the third. GRB is parked at H'FFFF so
it never matches.

Then four stepper motors. Three of them drive their phases through the
timing pattern controller, which shifts a byte out of NDRA or NDRB onto the
port pins on a compare match rather than under the program's foot: motor A
on the top half of port A, motor B on the bottom half, motor C on the top
half of port B. The fourth is the odd one out -- its phases are in the low
bits of the external latch at H'0C0000, so its phase and its two enable bits
go out in the same write. Each has a phase table in the code region
(H'25080C, H'250924, H'250A24, H'250A90) and a small state block at
H'11A83x.

**Part 3p, the main motor (`H'20E054`) -- done.** The sewing motor is not
stepped. It runs from ITU channel 4 in PWM mode, GRB the period and GRA the
mark. The period goes down as H'2B34, a kilohertz; the mark goes down as
H'2B98, which is longer than the period and so never matches -- the motor
starts stopped and the first real speed is whatever asks for one.

`H'20E054` itself has no case. It ends by enabling the period interrupt, and
in the few instructions between that and its return the original takes the
interrupt and its handler does real work while the rebuild's stub does not.
The three routines under it are compared individually instead.

**Part 3q, the foot control (`H'209FC0`) -- done**, and this one has a
compiler runtime in it.

The pedal is an analog input like any other. Its reading is put into one of
six zones, which is the mode at H'FFFEC3, with a hold in bit 0 of H'114DD7
so a pedal resting on a boundary does not chatter. Three of the zones mean
one thing when every stepper is idle and another when one is still moving.
Holding the pedal back runs a counter at H'114DDC: past about half a second
it beeps three times to say so, and past a second it forces the mode.

The zone is then scaled by two ratios out of the settings block and written
to the PWM -- and the original does that in single-precision floating point,
through a library at H'200530 and its neighbours. The format is IEEE-754,
which is what the compiler here uses, so this is written as plain `float`
and the library goes away. The head-to-head passes on every case, which is
the evidence that the two libraries agree over the range that matters.

**Part 3r, the front panel (`H'20ACC8`) -- done.** The keys are a matrix:
three strobes on port C, eight returns from a latch at H'060000, active low.
The two knobs come in on the same pass as analog channels 0 and 1.

The whole panel is read ten times over. A bank that reads differently from
the pass before is thrown away rather than believed, and a knob that has
moved by more than two counts is put back to H'02, which cannot pass the
test at the end. Only what has been still for ten passes survives. That is
why the bring-up spends a scan here before it trusts anything.

Three of the ten dispatch targets cannot be called on their own: H'209072,
H'209104 and their siblings are reached by a `JMP @ER6` out of the
dispatcher and share its epilogue, so they pop registers they never pushed.
They are compared through H'20901A with the pass counter set instead.

**Part 3s, the analog inputs (`H'20A030`) -- done.** Everything the machine
measures rather than switches goes through one converter, taken in a fixed
order -- 0, 2, 3, 1, 5, 7 -- with one conversion started and the previous
one's result collected on the same pass, so nothing ever waits. Bit 1 of the
H'0A0000 latch selects what is in front of the converter and moves with the
sequence; that latch is shared with the motor enables, so it is changed with
interrupts off.

The handwheel on channel 7 is turned into the position the rest of the
machine works from, and the original's arithmetic there overflows: H'E1
minus the reading times H'A0 reaches 36000, which the sign extension before
the divide then reads as negative. Written the same way here, with the casts
that make a 32-bit compiler do what a 16-bit one did.

**A harness change and a stub change came out of this part.**

The comparison now takes an optional `"exclude"` list of address ranges per
case. It is for counters that run on their own: once a routine has started a
timer, its count depends on how many instructions have gone by since, and
the two sides never execute the same number. Nothing else belongs there -- a
range listed is a range not being checked -- and the only ranges listed are
the five ITU status-and-counter triples.

The timer interrupt stubs now acknowledge. A stub that only returns leaves
the flag that raised the interrupt still raised, so the moment anything
lifts the interrupt mask the machine takes the same interrupt again and
never gets any further -- which is exactly what happens the first time the
analog scan unmasks. Clearing the flag is not what the real handler does
with it, and no comparison of a window containing one of these means
anything; it only stops an unreconstructed handler from killing the machine.

**Part 3t, the stitch database (`H'208826`) -- the data model and the
service layer done; the pattern loader and the panel work not.**

This is the machine's subject matter rather than another peripheral, and it
is big: eighty-two routines and about 6,900 instructions under one call in
the bring-up spine. It is being taken bottom-up.

**What a stitch is.** Three things hold a pattern between them.

* The *catalogue* is a table of 24-byte descriptors based at the pointer in
  H'114DD2 -- H'500000 in this machine's image -- indexed by pattern number.
  Two of its fields point at the pattern's own data, one at +0 and one at
  +4, and H'57FF80 in the settings block decides which is used: H'AA takes
  the first, anything else the second, and this machine holds H'B4. The
  descriptor also carries a *kind* at +H'16 and a continuation marker at
  +H'17.
* The *data* is a record read by index. Bit 7 of its byte 7 says it is
  indirect, in which case the record proper is at the pointer in its byte
  H'14 and the index applies there.
* The *working copy* is 16 bytes a pattern at H'0E4010, addressed by the
  pattern number shifted left four -- H'0E4010 to H'0E8000, which is exactly
  the block part 2 found the start-up clearing. Byte 0 is its own kind, and
  that is what decides whether the working copy or the catalogue is
  believed.

Six parameters travel together, each with a reader and all six written by
one setter:

| working | data | default table | reader |
| --- | --- | --- | --- |
| +H'01 | +H'09 | H'57B6D6 | `stitch_param_1` |
| +H'02/3 | +H'0B | H'57B6D7 | `stitch_param_2` |
| +H'04 | +H'0D | H'57B6D9 | `stitch_param_4` (low nibble) |
| +H'05 | +H'0F | -- | `stitch_param_5` |
| +H'06 | -- | -- | `stitch_param_6` |
| +H'07/8 | -- | -- | `stitch_param_7` |

Two of the six have a second working byte and bit 6 of H'11A7BD picks
between the pair, so switching modes does not lose either. What the six
*are* -- which is the stitch width, which the length -- is not established
here, and they are named for where they sit rather than guessed at.

**The queue.** The machine sews a sequence, not a pattern. H'11BBAA is a row
of 13-byte entries: the first two bytes carry a pattern number in ten bits,
H'03FE marks the end of a group, and the rest are that position's own
parameters, packed. H'FFFEFE is the position being worked on, H'11A6C8 the
start of its group, H'11A1D0 and H'11A1D2 the ends of the whole.

**Done and checked.** `H'2086FC` whole -- it reloads the six parameters of
the two reserved patterns H'03FE and H'03FF out of the catalogue -- and
about seventy routines under `H'208210`: the accessors and the setter, the
queue layer, the parameter plumbing between the three RAM blocks, the
watchers that raise redraw bits, the hour and stitch counters and their
write-back to flash, the chunk loader that streams a pattern into H'0E0000,
and `sew_service`, the whole once-a-pass pass.

Two of them use floating point again, and again it is plain `float` here:
the speed ceiling is scaled by a quarter, a half or three quarters
depending on the pattern's class, and every case passes head to head.

**The loader.** `H'206348` is done. The chunk loader brings a pattern into
H'0E0000 as one block; this walks it. Byte 7 bit 7 says whether there is a
variant table -- a pattern is not one shape but a set of them, the same
stitch at several widths or the same letter in several sizes -- byte 8 says
how many, capped at H'30, and from H'0E0014 there is a pointer per variant.
Each variant is copied up above the pointer array, packed end to end, and
its pointer rewritten to where it landed. The length comes out of the
variant's own header: a big-endian count at +H'10, two or three bytes a
stitch depending on bit 2 of its byte 6, plus H'13 of header. H'11A6E8 ends
up holding the same pointers, which is the table `sew_variant_load` reads.
With no variant table the count is forced to one and all H'30 slots point at
the start of the block.

**The fetch.** `H'204264` is done, and at 609 instructions it is the largest
routine reconstructed in this project. It reads the next four stitches out
of the current variant into H'11A7C8..H'11A7D3, where the motor handlers
pick them up. There are four ways round it, because a stitch is two bytes or
three and because a mirrored pattern is walked backwards; the index runs
from H'11A684 and wraps between stitches, H'11A686 being the far end and
H'11A682 the near one. In the three-byte mirrored form the middle two bytes
of each stitch come out swapped, which is the mirroring itself rather than
an accident of the loop -- all four shapes are checked separately.

Ahead of the fetch, bit 3 of H'114DC7 means "do not advance", and a fixed
stitch goes in instead depending on how far a stop sequence has got. After
it, the codes H'38 to H'3F in a stitch's second byte turn out not to be
stitches at all but marks against the free-running counter at H'FFFF00, and
the tail turns them into the four limits at H'11A7AE that
`sew_limit_overshoot` measures against.

`H'205B72` sets a pattern up to be sewn from the beginning: it works out the
two ends the fetch walks between and the first byte at each, which is what
the hold path puts out when the machine is stopping.

**The panel.** The panel is a little state machine of its own, at
H'11A6AD, and the bits in H'11A6AE say what it is being asked for. A pass
picks one of four things -- a partial redraw, a pattern redraw, a full
rebuild, or one stitch's worth of running -- does it, and parks so the next
pass starts again. There are two versions of the pass, and they differ only
in which bits take precedence: `panel_service_running` puts the stitch work
first, `panel_service_idle` puts the selection first.

Under them:

* `panel_selection_check` (`H'207342`, 231 instructions) asks whether the
  operator has changed anything. Three cases: H'FFFEFE reading H'FFFF means
  there is no queue and the pattern is whatever was keyed in; a queue that
  has just appeared has to be taken up, its bounds noted and its group start
  found; and a queue already running only needs looking at when the
  position, the variant or the pattern has moved.
* `pattern_make_current` (`H'206B00`, 245 instructions) makes the selected
  pattern the one being sewn -- streams its data in, unpacks its variants,
  resets the four counter limits to H'1000, works the stitch time out from
  two bytes on the bus, and then decides where the parameters come from:
  the working copy with no queue, the queue entry with one.
* `sew_params_from_working` (`H'2026B2`, 234 instructions) is that second
  half's reader, with a different set of fields for each kind of working
  copy.
* `stitch_advance` and `stitch_tick` are what a running machine calls a
  stitch at a time, and the two `stop_step` routines are the stopping
  sequences counted in H'11A6AF.

**A bug the harness caught, and how.** `sew_lamp_service` was reconstructed
two parts ago with its first test written as an early return. It is not: the
test *sets* the lamp and falls into the shared tail. Six cases covering that
routine all passed, because every one of them had H'11A7C8 reading zero and
so never took the path. It only showed up when `pattern_make_current` drove
it with a real stitch in H'11A7C8 and one byte came out different. That is
the third time in this project a green case turned out to be testing
nothing, and the first time a *caller* rather than a mutation exposed it.

**The geometry -- done.** A stitch in the data is a pair of numbers, and
neither goes to a motor as it stands.

`feed_position_set` (`H'205628`) and `needle_span_set` (`H'205266`) each read
a six-bit code out of the stitch and dispatch on it through a thirteen-entry
table. Most of the codes are not distances at all: they are fixed steps, a
stop, or a step worked out from how far past a limit the counter has run.
Only the default is a plain proportion. The jump index is
`(code & H'3F) + H'CD`, so only codes H'33 to H'3F reach the thirteen -- a
detail that matters, because three test cases were sitting on fixed codes
and never exercising the proportional path until the arithmetic was checked
against the index.

`needle_position_apply` (`H'2058D8`) turns the result into the two ends of
the swing either side of H'18, `stitch_spans_build` (`H'203C32`) works out
the three step sizes a repeat walks through in 8.8 fixed point, and
`needle_step` (`H'204CCA`) and `needle_step_mirrored` (`H'204E6A`) walk them
-- five steps for a plain repeat, ten for a mirrored one.

`needle_stop_service` (`H'203A6E`) decides whether this stitch is one the
machine may stop on, and `stop_countdown_set` arms the countdown. On some
patterns the half-way point counts as a stopping place as well as the end.

**The display group -- done.** Smaller than it looked. `H'206EB4` sets a
refresh bit on three numbers once a second, `H'204C86` publishes the two knob
values, `H'207F74` writes changed parameters back into the working copies --
watching eight of them so that only a real change is written, and applying it
to a whole run of patterns when the run's head is marked H'10 -- and
`H'207A58` is three tables of the widest swing each presser foot allows.

**`H'203EBC` -- done.** Steps the variant on by itself, either against the
free-running counter and the four limits or against a count of stitches, and
sets a warning three steps ahead so the display has notice.

**`H'20DB24` -- done**, with the six routines under it. It is the main
motor's state machine: H'FFFEC6 holds what the motor is doing -- stopped,
running, slowing, hunting for the needle-up position, braking, the winder,
the thread cutter -- and each state watches an analog channel or a timer and
moves on. ITU channel 3 captures the position sensor; channel 4 is the PWM,
and both are stopped while their registers change, which is why TSTR bit 4
goes down and up around every write to GRA4.

**`H'208210` -- done, and checked whole.** The routine everything else in
this part exists for. It decides whether the selection has moved far enough
to rebuild for, moves the parameters between the live set and the pattern's
own in whichever direction H'11A175 asks, and runs the sewing pass. Against
the original it executes about a hundred thousand instructions and writes
**10,466 bytes identically**.

**And so `H'208826` has a comparison case at last**, and passes. The part
that opened with a routine that could not be compared at all now compares end
to end.

**Two more harness lessons.**

H'FFFD18 to H'FFFD1B is the boot ROM's interrupt trampoline: its vector table
is at address 0 and most of its slots write the application's handler address
there and jump to it, so every interrupt taken leaves four bytes behind.
Whether an interrupt lands inside a window depends on how long the window is,
and the rebuild's timing is its own -- the reconstructed floating point is
three times slower than the ROM's library, which is enough to catch one. Now
excluded by default, for the same reason the timer counters are.

Three cases were dropped rather than made to pass. `position_capture_on` and
two of the motor states enable the ITU3 capture interrupt, whose handler is
still a stub; the original's handler writes H'FFFECE and clears a bit in
H'11A852 inside the window, and no comparison across that means anything. The
other five motor states are checked.

## Part 4, the display

**How big it actually is.** The earlier estimate of 650 routines was made
with a crude scan that ran past the end of every routine. Bounding each
routine by the next call target instead gives the real figure: `H'216D6C`
reaches **217 routines and about 13,350 instructions**, and 139 of those are
already reconstructed -- the stitch database and the bring-up are underneath
it. What is genuinely new is **78 routines, about 4,775 instructions**. That
is a part, not a project.

**The primitive layer -- done.** The screen is 320 by 240 at two bits a
pixel: four pixels to a byte, 80 bytes to a line, H'4B00 bytes to a buffer,
and there are three buffers. `plot_pixel` (`H'20E154`) was already here from
part 3; the rest of the bottom now is too.

* `pixel_address` (`H'20E48E`) -- the byte a pixel lives in and which of the
  four pairs in that byte it is.
* `read_pixel` (`H'20E310`) -- the mirror of plot_pixel.
* `draw_vline` (`H'20E562`), `draw_hline` (`H'20E5B6`),
  `draw_line_bresenham` (`H'20E764`) and `draw_line` (`H'20E4E4`), which
  sends a line to whichever of the three fits it.
* `copy_forward` (`H'210428`) and `copy_overlapped` (`H'210470`) -- both
  running the service hook between bytes, so a long copy does not starve the
  machine.
* `header_word_0` and `header_word_1` (`H'20E0D8`, `H'20E0FE`) -- a bitmap's
  width and height out of its four-byte header.
* `int_to_decimal` (`H'216F3C`), `abs_short` (`H'24ADC8`),
  `screen_remember` (`H'21F40E`), `message_state_clear` (`H'246D8C`).

`draw_hline` is the only primitive that is not a loop over plot_pixel: the
whole bytes in the middle are filled at once and only the ragged ends are
done a pixel at a time. Its fill byte comes from the colour, and **colour 1
has no case** -- the original leaves the byte holding whatever the caller's
register did, so a line of colour 1 long enough to reach the byte fill writes
rubbish. That is a fault in the original, and the one thing here that cannot
be matched.

**And `service_tick` (`H'208698`) is no longer a stub.** It was left empty in
part 3 because the four routines it calls did not exist. They do now, so the
service hook -- which runs between the bytes of every fill and every copy --
reaches the analog scan, the stitch state, the pedal and the motor. That is
why clearing 19K of frame buffer does not stop the machine.

**Two argument-layout corrections came out of the tests.** `H'20E764` and
`H'20E4E4` take four words before their buffer, so the buffer is at +H'0A and
the colour at +H'0E, not +H'0C and +H'10. Getting that wrong made
`draw_line (vertical)` pass while writing nothing; with it right the same
case writes eleven bytes. And the rebuilt side of a routine with more than
three arguments needs its stack filled in too -- `pixel_address` was
dereferencing whatever happened to be there, and wrote to address zero.

**The bitmap and blit group -- done.**

`bitmap_draw` (`H'20F192`) is the decoder. A bitmap here is run-length coded:
four header bytes and then a stream in which each byte carries a colour in
its low two bits and a run length in the other six. Four pixels to an output
byte, so a full-width bitmap -- H'013F, the screen less one -- is a straight
run of bytes and takes a fast path, while anything narrower is done a row at
a time: the pixels before the first whole byte, the whole bytes, then the
pixels after the last one. All five paths are checked separately.

`region_copy` (`H'20EC12`) is the blit, and it was the stub `blit_20EC12`
from part 3 onwards. It copies a rectangle between buffers or to a different
place in the same one, and that is why it tests the direction: rows go top
down when the destination is above the source and bottom up when it is
below, so an overlapping move does not eat its own tail. Same lead/middle/
trail shape as the decoder, except that only the leading edge shifts the row
pointers -- the trailing byte is left in the run. **The splash screen's blit
call is now a real call**, which leaves `image_load_20F128` and
`delay_2119EE` as the only two stubs on that path.

`bitmap_draw_mirrored` (`H'2102B8`) does not mirror in the decoder: it draws
into the third buffer at H'0E8010 the ordinary way and copies out pixel by
pixel with one or both axes reversed. `draw_rect` (`H'20E826`) is a stack of
horizontal lines when filled and four sides when not.

**The screen layer -- done.** H'11A169 is the screen showing now, H'11A168
the one before, and H'11A16A to H'11A16D are four slots a screen can be
remembered in, so that "back" means different things depending on how the
operator arrived. `screen_switch` (`H'21F09E`) does nothing at all while the
machine is sewing; going back to a screen a slot already holds is a return
rather than a move and takes a short path. `screen_leave` (`H'21F1DE`)
dispatches sixty-eight screens through a table to one of three exit hooks,
and most of them want none. `message_show` (`H'216D6C`) -- the routine this
part was named for -- goes to screen H'3E and points H'115D12 at the
message's record.

**A convention worth writing down.** A stack argument occupies a four-byte
slot with its value right-aligned in it: a `u16` fifth argument sits at
+H'06, not +H'04. That is the same rule as the byte-at-+3 one from the boot
ROM, and it is why the rebuilt side of a routine with more than three
arguments needs its stack filled in as longs even when the arguments are
words.

### The bug behind the backdrop save

The failing `dialog_backdrop_save` case was worth chasing, because it was not
a bad test. Driving `region_copy` directly with the same arguments failed the
same way, which put the fault in the copy rather than its caller; a probe
that stopped at the first write to the offending address pointed at
`copy_forward`, called with a destination four kilobytes below where it
belonged.

The cause was in `pixel_address`, three parts back. The original multiplies
with **MULXS.W** -- a signed 16 x 16 into a full 32-bit result. The
reconstruction had `(short)0x140 * (short)y` cast to long, which GCC compiles
as MULXU.W followed by EXTS.L: a 16-bit product, sign-extended afterwards.
The two agree until H'140 * y passes H'8000, and then they do not. That is
y = 102 -- so **every drawing routine in this project was placing pixels
correctly in the top two fifths of the screen and wrongly in the rest**, and
every test had happened to use a small y.

The fix is one line, `(long)(short)0x0140 * (long)(short)y`, and a case at
y = H'A0 to hold it. It is the most consequential single fault found so far,
and it took a failing case with a large coordinate to expose it: eleven
drawing cases had passed over it.

### The dialog

The dialog is a strip across the bottom of the screen, x H'30 to H'E7 and y
H'A0 to H'C0, whose contents scroll sideways. `H'20EFE2` is what moves them,
and it is now in -- the two things that had kept it out both turned out to be
misreadings rather than gaps.

Its three output pointers are computed *between* the pushes that pass them,
so `MOV.L ER7,ER5 / ADDS #4,ER5` after a push means frame+0, not frame+4;
the three addresses land at frame+0, +4 and +8 rather than +4, +8 and +H'0C.
And the copy's destination is the second address, not the first, which makes
the register argument the place the run is taken *from*. With both corrected
it moves 1,386 bytes exactly as the original does, and its two wrappers
turned out to be named backwards as well: `H'2289EE` shifts the strip left
and `H'228A5E` right.

Also done: the two round-up-to-a-byte helpers, the queue-entry readers --
`H'228C20` and `H'228C50` assemble a pattern number **low byte first**, the
other way round from `queue_entry_ref` in part 3, and both are in the ROM as
they stand -- the per-entry offset, the H'16 test, and `queue_entry_facing`.

### The pattern picker -- done, and with it the display

The strip shows a row of stitch thumbnails with a cursor under one of them.
H'11A1CC is the position the cursor is on, H'11A1C8 where it sits on the
screen, H'11A1D0 and H'11A1D2 the ends of the row, and H'11B3D8 a cache of
the pattern number for each of a thousand positions so a redraw does not go
back to the queue for every thumbnail.

The six routines call each other in a ring -- the cursor calls the goto, the
goto calls the two scrolls, the scrolls call the cursor -- so they went in
together and were checked afterwards, working outwards from the two that do
not call back into it.

* `picker_forward` and `picker_back` add up the widths of everything stepped
  over. If that carries the cursor past an edge the strip is scrolled by a
  whole number of bytes; if it would have to scroll further than the strip is
  wide, the row is redrawn from the new position instead.
* `picker_cursor` is a short vertical line in the second buffer, blinked by a
  counter that reaches H'64. Screen H'43 keeps its position somewhere else
  and draws it upwards.
* `picker_rebuild` copies the whole queue out of flash -- H'32D5 bytes --
  caches a thousand pattern numbers and redraws. Checked whole: 240,000
  instructions and 1,624 bytes written identically.
* `text_draw` (`H'21700A`) draws a string from a font table in three
  alignments; the glyph for a character is the pointer at
  `font + ch * 4 - H'84`, so the table starts at H'21. The centred alignment
  has to measure the whole string first.

**The fall-through that the tests caught.** `picker_cursor`'s mode 1 does not
return: it puts the cursor where it belongs and then *falls into* the blink,
where mode 0 and mode 2 both branch to the exit. Written with a `return` it
left the blink counter one behind, and every routine that reaches the cursor
-- six of the thirteen cases -- failed on that single byte.

**And the test data caught the other one.** `picker_draw_range` passed while
writing nothing, because the frame buffer was filled with the colour it draws
in. Filled with the opposite it writes 66 bytes. That is the fourth time a
green case in this project turned out to be testing nothing, and every one of
them was found by making the data disagree with the answer rather than by
reading the code again.

**Three stubs closed on the way past.** `H'210DC6` is the noise a message
makes -- H'57EFC8 is a pair of bytes a message, whether and how many times.
`H'20966A` is the speed target in the service mode, where the handwheel sets
it instead of the pedal. And `H'20369A` sends a running queue back to the
start of its group, which corrects a guess: `H'2037DC` was named
`sound_click` in part 3q from the setting bit it tests, and it is nothing of
the sort. Both are renamed.

**The display subsystem is complete.** Everything `H'216D6C` reaches -- 217
routines -- is now reconstructed. Fifteen of them are named in the source
under other headings: the compiler's floating-point runtime, which is C
operators here, and a handful of I2C and queue helpers from parts 3a and 3t.

**506 cases now pass over 239 routines**, and five stubs are left in the whole
application: `H'20F128` and `H'2119EE` on the splash path, `H'21DDC4` which
saves the settings out to flash, and the two main loops. Part 5 closes them.



**A note on the test data.** The first set of accessor cases passed a
deliberate mutation -- the offsets they were meant to tell apart held equal
values in the dump, so reading the wrong one gave the right answer. They now
seed a ramp through the working copy, the data record and the three default
tables, and the same mutation fails seven of seven with the exact values.
That is the second time in this project a passing case turned out to be
testing nothing, and both times only a mutation showed it.

**Where part 3 stands.** Every call in the bring-up spine is a named,
reconstructed routine. Eight of the nine that were outstanding are checked
routine by routine; the ninth is this part. It is done, apart from
H'216D6C -- the display subsystem, which is its own part. What is left after
that is the two main loops.

**506 cases now pass over 239 routines**, from

```
dart run tool/compare_routines.dart bernina_artista180/application/routines.json \
  --rebuilt <merged image>
``` Each can now be
written and checked on its own, in any order, without waiting for the
machine to reach a state where it draws.

## Part 5, the last five stubs

Three of the five closed outright; the other two were the two main loops,
and closing those turned out to mean something much larger than their bodies.

### The three small ones

* **`H'20F128`** is a run-length picture unpacked into a buffer. The first
  word says how many words there are, itself included, and every word after
  it is one run -- the length in the high byte, the colour in the low. It is
  a byte fill repeated; nothing in it knows about the screen.
* **`H'2119EE`** is not a delay. It sets how long a screen is to be held and
  zeroes the count of how long it has been held, and the millisecond tick
  does the counting. `H'211A02` is the other half, which says whether the
  hold has run out.
* **`H'21DDC4`** writes the settings back to flash: seven H'22-byte blocks
  from `H'115A20` to `H'57EED6`, the beep table, and a handful of single
  bytes, with bit 5 of `H'114DC7` held up across all of it. Its argument
  decides how far the reset goes, and both of its callers pass 1 -- so a
  firmware update with a changed version string clears the pattern queue,
  which is worth knowing before flashing one.

Five routines came in with it, and one of them, `H'201556`, stops one short
of the full 1024 descriptors: `CMP.W #H'3FF` followed by `BCC` leaves at
index H'3FF, so the last one keeps its bit. `H'200E46`, doing the same walk
over a different table four instructions later, uses `BHI` and does all 1024.

### Service mode, and what a stub was hiding

`H'20BEE2` is two calls. The second, `H'20BC9A`, is a dispatch on `H'FFFEC5`
through a pair of tables -- 44 keys at `H'20BCD8` and their handlers at
`H'20BDB0`, laid out so the handler index counts *down* as the key search
counts up, which is why the handler list is in the reverse order of the keys.

Behind it is a bench: each of the four steppers driven to fixed positions or
cycled between two, the main motor run at fixed speeds, the hall sensor
trimmed against the settings block, and the presser-foot lift calibrated into
the EEPROM at `H'A9` and `H'AA`. Forty-odd routines, all of them now written
and checked.

Three things worth recording:

* **Some of the settle loops settle and some do not.** `H'20B0A8` zeroes the
  tick counter and then leaves as soon as it is *below* H'FA -- which it
  already is -- so the quarter second between releasing one motor and the
  next is not waited for. `H'2085B2` does the same thing with `BHI` instead
  of `BCS` and genuinely waits. Both are in the source as they are.
* **The needle motor's middle two positions write only the shadow.**
  `H'20B15E` and `H'20B1B8` set `H'11A6B7` and not `H'FFFED3`, where the
  other three motors and the needle's own H'FC and H'FF positions write both.
* **`H'20BBF4`'s delays count from an uninitialised register.** R6H is not
  set before the first `CMP.B #H'FA,R6H`, so the gap between motors is
  whatever the caller left there, and after the first loop R6H is H'FA and
  the other three do not run at all.

Two failures during the checking were real, not test artefacts. `H'20AF5A`
puts the hall window byte into the *high* half of the word before shifting
it, so the window is the byte times four times H'100 and a setting above H'3F
wraps rather than grows -- written the obvious way it was H'100 times too
small. And `H'20B948` looked like an early return where it is a fall-through:
one foot stop finished and the other not carries on into the ramp, which is
what keeps the second one moving.

### The diagnostics screen

`H'201802` is 47 numbers in four columns and one temperature, and `H'207DFC`
is the key combination that turns it on -- bit 1 of `H'FFFEC1` with bit 5 of
`H'FFFEDC` -- after which it redraws itself once every 256 passes and there
is no way back out of it. Its three arguments are put on the screen and
nothing else is done with them.

Checked whole, with every byte it reads seeded: 1,584 bytes written
identically over 224,000 instructions.

### What is left, measured

Both main loops are now real, and each is four calls. Two of those calls are
still stubs, and they are the whole of the rest of the application:

| stub | what it is | routines it reaches | code |
|---|---|---|---|
| `H'22382A` | the screen dispatcher: one switch on `H'11A169` over 79 screens through the table at `H'2238B0` | 352 | ~200 KB |
| `H'2354AE` | the embroidery module's state machine, 420 instructions of sequencing | 156 | -- |

Since that table was written, part 7 has taken the module's *protocol* --
about 3,900 instructions of it -- out of the second row. What is left there
is the sequencing that decides which message to send and when.

For scale, everything reconstructed so far -- 280 routines over five parts --
is 47,420 bytes. So what those two stubs stand for is about five times the
whole project to date, and neither is a stub in the sense the other five
were: they are subsystems, and each wants its own part.

Returning from them leaves the machine running with a blank screen and no
module, which is what a stub has to do. Nothing else in either loop depends
on either one.

A third gap turned up on the way and is worth naming, because several of the
waits above depend on it: **the interrupt handlers are still stubs**, so the
millisecond counters at `H'114DDA`, `H'114DDE`, `H'114DE0` and `H'114DE2`
never advance in the rebuild. `H'20DFB0` -- vector 41 -- reaches `H'20AAE0`,
which is what increments them. Until that is written, any loop that genuinely
waits for a tick cannot be compared, which is why `H'2085B2` and everything
above it are reconstructed but untested. Part 6 closes it.

**578 cases now pass over 280 routines.**

## Part 6, the interrupt handlers

Ten slots, of which seven are now written: the four stepper handlers, the two
that measure the sewing speed, and the millisecond. The three SCI0 handlers
are the embroidery module's protocol and are left with the rest of it.

This is what a stub was costing everywhere else. Nothing in the machine waits
on anything without these -- H'114DDA, H'114DDE, H'114DE0, H'114DE2 and
H'114DE4 are all counted here, and so is every step of every motor -- and
until they were written, any routine that genuinely waited for a tick could
not be run at all in the rebuild, let alone compared.

### The four steppers

ITU0 drives two motors, not one. While bit 7 of H'11A835 is clear the
interrupt belongs to the needle-position motor; when that reaches its target
the bit goes up and the same interrupt drives the feed instead. ITU1 has the
hook width to itself and ITU2 the hook position.

All four run the same shape. A move is split in half, and the phase index
walks *up* through an acceleration table for the first half and back *down*
through a deceleration table for the second, so the motor ramps inside a
single move; a move shorter than the ramp sets a flag that holds the index
still. What is written to the compare register each time is the interval to
the next step, so the tables are times, not positions. The coil patterns are
data in the code region:

| motor | back | forward | homing | drive |
| --- | --- | --- | --- | --- |
| needle | `H'250804` | `H'25080C` | `H'250814` | `H'FFFFA5` high nibble |
| feed | `H'25091C` | `H'250924` | `H'25092C` | `H'FFFFA5` low nibble |
| hook width | `H'250A1C` | `H'250A24` | `H'250A2C` | `H'FFFFA4` |
| hook position | `H'250A8C` | `H'250A90` | `H'250A94` | `H'0C0000` low six bits |

Three things in there are worth writing down.

* **Targets H'FC to H'FF are not positions.** They are park commands: hold,
  coast, and two fixed coil patterns, each with its own single byte just past
  the phase table.
* **The feed motor steps by two and halves its distance**, and before it
  starts it snaps an odd position onto an even one -- the low nibble of where
  it *is*, not of where it is going. Its state-4 exit does the opposite: it
  reads the low nibble of the *target* and moves the position the other way.
* **The last two arms of a three-way test are the same code.** In the feed's
  arrival test the register holding the constant is H'0002, left over from
  the divide, so what looks like four cases is really "distance 0, distance
  1, anything else".

### The millisecond

H'20DFB0 is ITU4's compare B, and it has two modes. Normally it does all of
the work in one interrupt at an interval of H'2B34. While the presser-foot
calibration is running -- bit 2 of H'114DCD -- the same work is split across
three interrupts at H'0E67 apiece, with H'11A853 saying which slice is next,
so that no one interrupt is long enough to disturb the lift.

What it does, once round: the handwheel and the needle sensors read, the six
counters moved, the main motor's chopper stepped, the beeper stepped, the
sewing phase advanced, and the two knobs read.

* The **chopper** is H'11A826 counting up to H'11A827, with bit 7 of the
  H'0A0000 latch following it -- so the ratio of the two is the duty, and
  past H'32 the count restarts and the on-time is reloaded. The latch is
  shared with the stepper enables, which is why the interrupt mask goes up
  around each change.
* The **knobs** are quadrature pairs on port C, bits 2-3 and 4-5. Turning one
  below zero shows up as H'FE or above and is clamped back to nothing, and
  the ceiling is an argument -- H'C8 normally, H'FF on screen H'0A.
* The **handwheel** is two sensors read as a two-bit code, and a jump of two,
  where a step was missed, is taken in whichever direction the wheel was last
  going. Bit 7 of H'11A82B is that memory.
* The **sewing phase** is H'FFFEC0, three sensors packed into a byte. While
  the machine is running the ticks in each phase are counted, and the count
  at the moment phase 5 arrives from phase 1 is left at H'114DD8 -- which is
  how long one stitch took.

### The speed

ITU3 captures the main motor's tacho. H'20D99A counts whole counter periods
and H'20D9CC turns the pair into the speed at H'FFFECE:

    H'FFFECE = H'27B0F0 / ((overflows << 8) + capture >> 8)

An overflow with no capture behind it raises bit 4 of H'11A852, and the
capture that follows skips the division and reports nothing rather than
dividing by whatever is there. That is the reading the service mode's hall
trim works against.

### Two changes to the harness, and why they were needed

Reconstructing these broke 356 of the 578 cases at once, and the failures
were the harness's, not the code's.

The two images are never at the same point in the boot after the same number
of steps -- the rebuild is smaller and gets further. Probed at the boot step
this comparison uses, the original is sitting with its interrupt mask up and
ITU4 not yet started; the rebuild has its mask down, its timers running, and
H'3F milliseconds already counted. While the handler was a stub that wrote
nothing this did not show. With a real handler, every counter it touches
appeared as a difference in every case.

So the harness now **puts the mask up and clears the five TIERs** after the
boot and before the routine runs. The mask alone is not enough: a routine
that lowers it itself -- the analog scan does -- would take whichever
interrupts happen to be armed on that side.

And it **stops the timers**. The counters were already excluded from the
comparison, but the stepper handlers set their next interval to TCNT plus a
constant, which writes the drift into a register that is not excluded.
Stopped, both sides read the same counter and land on the same interval.

Neither change weakens anything: a handler that is itself under test is
called directly, and every one of them now is.

### What that leaves

**806 cases pass over 314 routines**, and five stubs are left: the screen
dispatcher, the embroidery module, and SCI0's three interrupts -- which are
the module's protocol, about 3,600 instructions over two fifteen-entry tables
at `H'2326D6` and `H'233E70`, and belong with it.

## Part 7, the SCI0 handlers

The last three interrupt slots, and with them the whole of how the machine
and the embroidery module talk to each other: about 3,900 instructions over
five nested dispatch tables. **All ten interrupt slots are now real.**

### The protocol

Half duplex at 57600 baud on channel 0, one direction at a time, with bit 1
of ITU0's start register turning the line round. A frame is:

    (message << 4) | sub-code, then the payload, then the sum of all of it

and the answer is one byte: H'AA when the sum matched, H'FF when it did not.
Every state either side of it lives in H'11F29E upwards -- the link state,
the message number, the index into it, the length, and the running sum.

There are fifteen messages each way, and they are the same fifteen: the two
sides use one table each, at `H'2326D6` for sending and `H'233E70` for
receiving, and the handlers are mirror images. Message 5 is a further
fourteen sub-codes -- the block transfers -- and one of those is a further
nine, so a byte of a big pattern going out passes through four dispatches.

Long transfers are cut into H'200-byte blocks: H'11F29F counts them going
out, H'11F2B7 coming in, and a block that is not full is the last one. That
is the difference between receive state 4 and receive state 2 at the end of
a block, and it is why several handlers test the length against H'200 rather
than against zero.

### Three things worth writing down

* **The one-bit count is how the machine tells whether a module is there at
  all.** With the line idle and nothing plugged in, the receiver sees a
  framing error and whatever noise was on the wire. Both `H'233BE4` and
  `H'233CF8` respond by counting the one bits in what arrived: the error
  handler calls anything with more than four of them noise, and the receive
  handler wants between three and five before it will believe it. Different
  thresholds in the two, in the same image.

* **The buffer bounds are checked, and the check writes a bit the panel
  reads.** Every block transfer works out where its payload would land and
  compares that with the end of the buffer -- H'11F53A going out, H'11F2C6
  plus H'10000 coming in. Past the end, bit 5 of H'114D4C goes up, which is
  what puts "pattern too large" on the screen, and the transfer is clamped
  rather than allowed to run over.

* **Sub-code 7 does not know its own length.** It scans the buffer at
  H'104D48 two bytes at a time looking for the pair H'80 H'81, which is how a
  pattern ends, and the length of the block is however far that was. When the
  marker lands exactly on a block boundary H'11F2B2 remembers it, so the next
  block does not scan again and sends nothing.

### A fourth thing the harness needed

The receive register is read-only to the CPU and the simulator's serial model
holds it, so poking H'FFFFB5 in memory did nothing: every case read a zero
byte and took the "nothing there" exit. Six of them passed while proving
nothing. Cases can now carry a `"serial"` field that puts a byte into the
model, the way `"analog"` does for the converter, and the same six now walk
the whole frame.

### Checking it

**296 cases over the three handlers**, covering every message, every
sub-code, and the boundary of every count in them. Seven deliberate errors
were planted to check the cases had teeth -- a shifted header, a base address
one byte out, a retry limit, both one-bit windows, the received-pattern
clamp, and the checksum answer. Four were caught at once; the other three
were not, and each named a boundary the cases were not standing on. Cases
were added for exactly those, and all seven are caught now.

**1,116 cases pass over 327 routines**, and two stubs are left: the screen
dispatcher and the embroidery module's state machine.

## Part 8, the screen dispatcher -- begun, and measured

`H'22382A` is one switch on H'11A169 over seventy-nine screens through the
table at `H'2238B0`. Measured before starting: **350 routines and about
47,000 instructions**, which is more than twice everything in parts 1 to 7
put together. It is not one job. What follows is the first piece of it.

### What the screens are made of

Every screen is a list of boxes. H'11B0BA points at the list and each entry
is H'12 bytes:

| offset | |
| --- | --- |
| `+H'00`, `+H'02`, `+H'04`, `+H'06` | x0, y0, x1, y1, all relative |
| `+H'08` | the value the box stands for |
| `+H'0A` | 1 when the box is one of a pair that share a value |
| `+H'0C` | a list to look the value up in, or zero |
| `+H'10` | what the box is: 2 means "not there" |
| `+H'11` | how it is drawn |

The boxes are relative to an origin in H'11B0B2 and H'11B0B4, so a whole
screen can be moved without touching its table, and entry zero is not a box
at all -- its first word is how many there are. Everything above sits on
this: the touch hit test walks it, the painters draw one box at a time from
it, and the search finds the box carrying a given value.

Seventeen routines, now written and checked: the table's accessors and its
reset, the search, the two painters and the one that chooses between them,
the screen stack that "back" walks, the touch hold-off, and the two that
decide whether leaving a screen should stack it.

### Three things in there

* **The search has a special case for one screen.** A box with a list of its
  own normally matches on what the list holds at the box's own offset. On
  screen H'44, a box flagged as one of a pair is matched against H'11A186 or
  H'11A188 instead, depending on an argument -- which is how the two ends of
  a range are edited separately. A box that is *not* one of a pair falls
  through to the ordinary list comparison; getting that wrong made the
  search return nothing where the original returns box 3, which is what the
  case caught.

* **The buffer comes before the picture.** `H'212D8A` takes its arguments in
  the opposite order from `bitmap_draw`, which it calls. Written the obvious
  way it drew the right thing into the wrong place.

* **The ROM's `strcat` ends on the store, not the increment.** `ADDS` does
  not touch the flags on this CPU, so the `BEQ` after it is testing the byte
  that was just written. It works, and it reads like it should not.

### And one more test-setup lesson

The search takes four arguments, and the fourth goes on the stack under the
GCC convention. Only the first three were being placed, so the rebuilt side
read rubbish for it -- and both of the cases meant to tell the two halves of
a pair apart happened to agree anyway, because the box they landed on had no
list and never reached the comparison. A mutation that swapped H'11A186 and
H'11A188 went unnoticed. Fixed by placing the fourth argument and by giving
the box a list, and the mutation is caught now. Six of the seven planted
errors were caught at once; that was the seventh.

**1,160 cases pass over 342 routines.**

### What is left, measured again

**333 routines and about 46,500 instructions**, still all behind
`H'22382A`. The shape of it is clear from the fan-in: `H'211518`, the
message box, has 250 callers; `H'211A9E` 179; `H'210EE2`, the touch hit
test, 88. Those three and the twenty-odd routines under them are the next
piece -- after which each individual screen becomes small.

## Part 8b, the button layer and the touch hit test

The four routines with the highest fan-in in the whole dispatcher, and one
more beside them.

### What a button looks like

`H'211518` has 250 callers -- more than any other routine in the
application. It walks a run of boxes and puts each into a state, drawing
whatever that state looks like on the way; a box already in the state asked
for is left alone, which is what keeps the screen from being repainted on
every pass. There are four states and two styles, and the six things a
caller can ask for are: plain, pressed, greyed, blanked, and the two that
hand a box over to someone else's drawing and take it back again.

Two shapes of rectangle appear in it, and they are not the same shape. A
picture is blitted at the box's own coordinates with the screen origin *not*
added; a plain fill is drawn at the box shifted by the origin and inset by
two pixels all round, so the fill sits inside the border the picture drew.
Both are in the original and both are reproduced.

`H'211C38` fills a run of boxes from a list. Five lists -- the pattern
lists -- take each picture out of the stitch descriptor at H'114DD2;
everything else indexes the icon table at H'1158CE. A run longer than its
list goes grey past the end, unless the list is marked as wrapping, in which
case it starts again and repeats across the row. The wrap is tested against
the list's real length and writes back a *different* length, in a local the
test does not read -- so it happens exactly once.

### Where the finger is

`H'210EE2` turns the panel's raw reading into a box. H'FFFED9 and H'FFFEDA
are the two axes, and each is put through a straight line -- gain at
H'11A87E and H'11A882, offset at H'11A886 and H'11A88A, all four
single-precision floats. Below 5 on either axis means nothing is being
touched. Then the boxes are walked until one contains the point.

`H'211252` is the same thing for a press that arrives down the serial link
rather than off the glass: H'11F547 and H'11F548 holding H'CA say a host is
driving, and the box number comes from H'11F549 instead of from the screen.
That is how EMB-Serial drives the machine.

**The pair rule, which the tests corrected.** A box can be flagged as one of
a pair sharing a value. On the three screens that edit a range -- H'44, H'30
and H'45 -- such a box pushes the old value down into H'11A188 and takes
H'11A186 for itself. I read the code as doing nothing anywhere else. It does
not: on every *other* screen, a flagged box clears both instead, and the case
that pressed a flagged box on screen H'10 is what said so.

### The cases had to be sharpened twice

Nine deliberate errors were planted. Five were caught at once and four were
not, and all four named a boundary the cases were not standing on: the
threshold that decides whether the panel is being touched at all, the right
edge of a box, the beep that tells a flagged box from a plain one, and the
pair rule above. Sixteen cases were added for exactly those -- pressing at
H'04 and H'05, pressing on each edge and one pixel past it, seeding the two
beep patterns so they differ, and giving a box both a list and the pair flag.
All nine are caught now, and two of the new cases then failed against the
real build, which is how the pair rule came out.

A test-setup lesson too: the fourth argument of a GCC-compiled routine sits
at entry+4, not entry+0. Placing it at 0 overwrote the sentinel return
address, and the rebuild ran away instead of returning.

**1,255 cases pass over 347 routines.**

### What is left

**327 routines, about 45,300 instructions.** Every screen from here on is
built out of the five routines above, so the individual screens should be
much smaller than their count suggests -- but there are seventy-nine of them
and the action dispatcher, `H'21548A`, is another seventy-way table of its
own. It is the one stub inside this part.

## Part 8c, the action dispatcher

`H'21548A` is what a press actually *does*, and the answer is a surprise:
nothing. Every press that reaches it ends up choosing one help record and
leaving H'115D12 pointing at it, and the message screen draws it. No setting
is changed and no motor moves. The machine's controls are elsewhere; this is
the help system.

### Four dispatches to reach a number

The routine is 137 leaf addresses and about 4,300 bytes of ROM, and almost
all of it is table:

| | |
| --- | --- |
| `H'215516` | 70 entries, on the screen being left (`H'11A16D`) -- 11 distinct handlers |
| `H'2156CE` | 130 entries, on the value the box carried -- 42 distinct, and 20 screens share it |
| `H'21608C`, `H'2161DA`, `H'2163E2` | three screens with a table of their own |
| `H'2166F2` / `H'21675A` | 54 kinds of pattern, keys and handlers, the handlers reversed |
| `H'216CCE` | 11 pattern categories, the fallback for a kind that is not listed |

Every leaf is four instructions: load the table base at `H'11B29E`, index it
by a fixed offset, store the pointer, branch out. They are table entries, not
routines, so what is written here is the offsets -- one number where the ROM
spends four instructions and a jump-table slot. The addresses are all listed
in a comment above the tables so a reader can still find any of them.

Six of the 137 are not that shape, and each is one test: four choose between
two records on whether the embroidery module is fitted (`H'57FF80`), one on
whether the screen is `H'35`, and one is the "no record" exit.

**The other half.** A press on the *second* box of a pair takes a different
route entirely: not the value, but what kind of pattern the value names.
Field `H'14` of the descriptor is searched against the 54 keys, and field
`H'17` -- the category -- is the fallback. Three screens short-circuit it and
one of them, `H'41`, also raises `H'11B0A9` on the way past.

### The cases had to be rebuilt once

The first set of 87 cases put the screen number in `H'11A16D` and pressed.
All 87 passed, and then nine of ten planted errors went unnoticed -- which is
the only reason the mistake came out. `screen_action` *starts* by calling
`screen_remember(4)`, which copies `H'11A169` into `H'11A16D`, so every case
was dispatching on the same screen and the tables were never read. Setting
`H'11A169` instead, and adding one case per distinct table entry rather than
per screen, took it to 176 cases -- and all ten errors are caught.

Two boot-state differences had to be pinned down as well before the cases
would agree: `H'11B0A8`, which `screen_switch` clears, and the pair
`H'FFFEE0`/`H'11A1BA`, whose comparison decides whether leaving a screen puts
the old one aside.

**1,431 cases pass over 348 routines**, and the last two stubs are the screen
dispatcher itself and the embroidery module's state machine.

## Part 8d, the two bars

`H'20FA18` and `H'20FF7A`: the stitch-width bar down the right-hand edge of
the screen and the stitch-length bar across the top. Twenty callers each --
every screen that lets either be changed draws them.

They are drawn **incrementally**, which is why each keeps five words of its
own state: the value it was last drawn at, where that put the end of it, the
limit the mark was last drawn at, whether the mark was drawn, and where the
mark is. A call that is not a fresh redraw paints only the strip between the
old end and the new one -- in the bar's colour when it has grown and in
nothing when it has shrunk.

The scaling is floating point over a range that never leaves a byte, the
same shape as the speed calculation in part 3q: for the width bar a byte
times H'1.01 plus a half, subtracted from H'95 because the bar runs upwards;
for the length bar just plus a half, added to H'D3 because it runs right.
The limit mark is a second number scaled the same way and drawn as a line
across the bar, put on and taken off as bit 7 of `H'FFFEE5` changes.

### Where the cases stop, and why

A bar's value is a percentage: the callers pass 0 to 100 and the drawing is
laid out for that. Feed it H'C8 and the scaled coordinate goes negative, the
rectangle is drawn from y = -53, and where those writes land depends on
24-bit address wrap-around that the simulator and the hardware do not have to
agree about. **That case is not claimed.** The cases run to H'93, which is
the largest value that still puts the end of the bar on the screen, and the
boundary at H'64 -- where the bar stops clearing the space above it -- is
covered from both sides.

### Eight planted errors, six caught, two provably not

Six of eight mutations were caught at once; the seventh needed a case with
the mark landing exactly on the end of the bar, which was added.

The eighth is **not catchable, and should not be**: changing `>` to `>=` in
the "has the bar grown or shrunk" test cannot show, because the enclosing
test is `value != last` and the two differ only when `value == last`. Same
for the length bar's `<`. Recorded here rather than papered over with a case
that could not fail either way.

**1,465 cases pass over 350 routines.** 325 routines and about 44,800
instructions are still behind `H'22382A`.

## Part 9, the harness: 25 minutes to 5 seconds

By the end of part 8 the comparison suite was 1,465 cases and took about
twenty-five minutes. With three hundred routines still to write, and every
one of them wanting a run, that had become the thing slowing the work down.

Two changes took it to **4.6 seconds**.

### The memory keeps its own undo log

`RoutineComparer` used to copy every allocated byte before a run and compare
every allocated byte after it, to find what the routine changed. That is four
megabytes each way for a routine that usually touches a few dozen bytes --
and it was done through `peek()`, one call per byte.

`SparseMemory` now takes an optional `undoLog`: a map that records, for every
byte written, the value it held *first*. What a run changed is then exactly
that map, filtered to the addresses whose value actually differs at the end.
It is the same answer, it costs nothing when nothing is written, and an
address written and then written back still counts as unchanged.

Nested runs chain rather than clobber: a run installs its own log and folds
it into the caller's on the way out, keeping whichever old value was seen
first. That is what lets the second change work.

### The machine is booted once, not two thousand times

Every case booted both images 1.9M instructions to reach the same state --
five and a half billion steps per suite, all of it identical. Now each
(image, boot step) is booted once and lent to case after case:

* memory goes back from the undo log,
* everything else -- registers, flags, cycle count, and the SCI, ITU, A/D and
  DMA models -- goes back from a `saveState()` the machine now offers.

Two details had to be right. The cached state is taken **after** the mask,
the timer enables and the counters have been dealt with, not before: with the
timers still running, restoring would leave the counters to jump the moment
anything read them. And the timer unit's `_lastCycles` is part of its state,
because the counters advance by the difference between it and the CPU's cycle
count -- restore one without the other and the next read leaps.

### Proving it

Two switches, both kept:

* `--verify-restore` compares the whole of memory against a copy taken at the
  boot, after every single case. The full suite passes under it in 34
  seconds. That is the expensive way, and it is the one that found both
  details above.
* `--no-boot-cache` boots afresh for every case, exactly as before.

A warning learned the hard way: `--no-boot-cache` re-reads the image file per
case, so rebuilding the image while such a run is going produces a run in
which different cases saw different code. Sixty-three "failures" came from
exactly that and none of them were real.

**1,477 cases pass over 354 routines**, in 4.6 seconds.

## Part 9b, the item preview

`H'2125B0` and the three routines under it. When the operator moves through a
list, the item under the cursor is drawn large in a panel at the top of the
screen, and which of three ways depends on byte H'17 of its descriptor: below
H'05 or H'10-H'11 one way, H'05 to H'0F another, H'12 and above a third.

Two of the three go through the scratch buffer at `H'0E8010` and copy it out
**a pixel at a time**, which is how the picture is turned: the source is read
along one axis and written along the other. A stitch pattern comes out ninety
degrees round from how it is stored, because it is stored the way it is sewn.
The third blits straight into the panel, centred on H'9A, H'14 from the width
and height in the picture's own header.

One case was failing until the font table was left alone: filling it with
zeroes made every glyph pointer zero, both sides drew from address 0, and the
result was a thousand bytes of rubbish in a place neither of them should have
been writing. The boot leaves a real table there; the case now uses it.

## Part 9c, the dispatcher's front half, and the module's slate

Twenty routines, in three groups.

### Six under the picker and the store

`H'24ADF0` is the ROM's `memmove`, and a proper one: when the source lies
below the destination and the two overlap it copies backwards so the copy
does not eat its own tail. `H'21F36E` moves a whole screen between the front
buffer and one of four stores at `H'0ECB10`. `H'248668` waits for the link to
go quiet, a hundred turns of a delay, and `H'229714` draws the two arrows
beside the pattern strip -- each one lit or not, and repainted only when it
changes. `H'21341E` finds the box carrying the current speed, lights it, and
draws its item in the preview panel.

### Eight that wipe the module's slate

`H'244578` is called from sixteen places, all of them the start of some piece
of embroidery work, and it is the one routine that puts the module back to a
known state. Reaching it meant writing the seven routines under it as well.

Two records describe the pattern in the slot named by `H'11A660`: sixteen
bytes at `H'11A25A` indexed by slot << 4, and eighteen bytes at `H'11A41A`
indexed by slot * H'12. Both indices are worked out afresh for every single
field -- fourteen multiplications to write fourteen bytes -- and the
reconstruction does the same, because doing it once would be a different
program.

`H'231994` and part of `H'2445F6` write the same stitch defaults, and the
difference between them is the interesting part: `H'231994` leaves `H'11A263`
alone and writes `H'11A265` before `H'11A264`. Two versions of the same block
that drifted apart.

### Seven from the top of the dispatcher

`H'22382A` starts with the pending screen change (`H'2237D0`), the touch
settling (`H'210E02`), the foot switch (`H'215448`), the key scan
(`H'21F68C`), and the screen save and restore (`H'21F4C6`), with
`H'249D6C` and `H'244C62` under them saying whether the module will take an
order.

The key scan is eighteen bits over four ports tested in a fixed order, the
first one down named in `H'11B10E` from H'6D up. One key is special: H'75
starts the module, and when the module is present but busy the key is not
reported at all -- the scan carries straight on into the last two tests below
it. The first reading of that had it returning early and leaving `H'11B10E`
untouched, which is a different thing, and the comparison caught it.

Two more readings were wrong and were caught the same way. `H'244C62` and
`H'249D6C` looked like they returned whichever byte they found set -- the
decompiler puts the `r6l = 0` in the wrong arm -- and both actually branch to
a shared `SUB.B R6L,R6L`. Clean booleans.

### The mutations, and the stale build that hid three of them

Sixty-nine planted errors across the twenty routines. All but six were caught
straight away, and the six were:

  * `memmove`'s `s <= d` and `s + len >= d` at their exact boundaries. When
    the source and destination are equal both directions are the identity;
    when they exactly touch there is no overlap and both are correct. Not
    detectable by any test, because there is nothing to detect.
  * `H'248668`'s second mask, `& H'22` against `& H'02`. The H'20 bit it
    drops is tested on its own two lines later.
  * `H'248668`'s hundred turns. The routine writes nothing, so only the
    result is compared, and both counts give the same result.
  * `H'229714`'s `<` against `<=`, guarded by an equality test above it.
  * the order of the last three writes in `H'244578`, which the net-write
    comparison cannot see and which nothing else can either.

Three others *looked* like survivors and were not. `make` compares
timestamps at one-second resolution, and a mutation written and compiled
inside the same second as the previous one left the old `app.o` in place, so
the harness was measuring the wrong binary. Every mutation run now removes
`app.o` first. That is worth writing down because the failure mode is silent
and it points the wrong way: a stale build makes a real error look like a
proven equivalence.

**1,659 cases pass over 374 routines**, in eight seconds.

### Where the dispatcher actually stands

The call graph below `H'22382A` reaches 206 routines. All but three are now
written. The three are `H'22382A` itself and `H'222AAC` and `H'237E3C`, and
all three end in a computed jump into a table of *inline* blocks -- the
screen bodies are not separate routines at all, which is why walking the call
graph makes the dispatcher look almost finished when it is not. Fifteen of
the 206 are written but have no head-to-head case yet.

Across the whole application there are 1,388 call targets. 404 are written
and 374 of those have cases.

## Part 9d, the module key

`H'237E3C` is where a key press goes when the embroidery module is attached,
and it is the reversed jump table again: twelve key codes at `H'237E7A` --
H'6D, H'70-H'75, H'77-H'79, H'7D and H'81, exactly the ones `H'21F68C` names
-- walked forward while the index counts *down* from H'18 in twos, so the
handler pointers at `H'237E82` are stored back to front.

The handler itself is not written yet: it waits on the module's own state
machine at `H'235B0E`, which is 145 instructions of its own with an
eighteen-way jump table under it. Nine routines beneath it are written here:

  * `H'23E45A` -- two instructions, the address of the module's reply buffer
  * `H'230E6E` and `H'230EA8` -- the first screen store emptied, and the
    embroidery panel put into it
  * `H'236E9A` -- the module's cursor rubbed out, a twenty-five pixel box
    fetched back from the third store
  * `H'2426F0`, `H'23191C` -- a pattern marked ready, and the module asked to
    go home
  * `H'2431C2`, `H'244A2A` -- the end of a talk to the module, and the module
    started again from nothing
  * `H'249DE8` -- waiting for the module to name itself

That last one is the nicest find. It reads five bytes from the module's reply
buffer and compares them against five bytes at `H'200103` -- which is inside
the *application's own* identity block, the thing the linker script calls
`appinfo`. The machine checks the module's firmware against its own version
stamp. The two sides are compared as words with the ROM's byte sign extended
and the module's not, so an expected byte of H'80 or over could never match;
none of the five is.

### A failing case that was not a bug

`H'2431C2` parks the machine, and the case for it failed with four hundred
bytes of difference in the panel drawing area. Following it down: `H'2085B2`
parks, `H'207988` is a whole sewing pass, and inside that `H'205266` came out
with H'11A6B6 = H'19 in the rebuild and H'00 in the original.

`H'205266` ends by interpolating through `H'2051AC`, which adds `H'11A6C2` to
its result. Nothing in the case set `H'11A6C2` -- so each side read whatever
its own boot had left there, and the two boots do not agree on that byte. The
reconstruction was right; the case was under-specified. Pinning `H'11A6C2`,
and merging in the fills that the existing `panel_service` and `sew_service`
cases use, made all of it pass.

That is worth stating plainly: **a failing comparison is not proof of a wrong
reconstruction.** It is proof that the two runs differ, and the difference can
just as easily be an input the case forgot to hold still. The only way to tell
is to follow it down to the byte, which took five rounds of bisection here.

Two written-but-untested routines picked up real cases out of it: `H'2085B2`
and `H'207988`.

### The mutations

Forty planted errors over the eleven routines. All were caught except two,
and both are the same shape as the survivor in part 9c: `H'244A2A`'s
quarter-second delay and `H'249DE8`'s H'9C4 tries. Neither routine writes
anything while it waits, and nothing outside changes underneath it, so the
count is invisible to a comparison that looks at memory.

**1,702 cases pass over 385 routines**, in twelve seconds.

## The parts, in order

Ordered to reach a drawing screen as early as possible: until the display
comes up there is nothing to compare but RAM contents, and once it does,
every part after it can be checked by looking at the picture.

Two measurements set that order. The LCD controller is first touched 1.9M
instructions into the boot, from `H'20E066`, on the path

    _start -> app_init (H'208E10) -> sub_208D88 -> ... -> sub_2105C4
           -> lcd_controller_init (H'20E062)

and the frame buffer is first touched by `cold_start` clearing it. So the
display hangs off the *initialisation*, not the main loop — the main loop is
not on the critical path at all, and has moved down the list accordingly.

1. **Scaffold.** *Done.*
2. **Startup and the RAM map.** *Done.*
3. **The chain from `app_init` to the display**: `sub_208D88` and what it
   reaches. Measured at ~4,600 distinct instruction addresses, about half the
   boot path and roughly twice the whole boot ROM. This is the big one.
4. **The LCD driver.** `lcd_controller_init` (`H'20E062`), reached through
   `sub_2105C4`. The screen comparison comes back here, and with it a real
   check on everything already written.
5. **Touch and the A/D converter.** Partly named already: `adc_prime_an4_an6`
   (`H'2091F0`), `adc_sample_an4` (`H'209C44`), `touch_calibration_apply`
   (`H'210FB4`).
6. **The main loops** — `sub_2086B6`, and `sub_20BEE2` for whatever the
   `H'FFFEC4` bit selects. *Done, along with service mode; the screen
   dispatcher and the embroidery module remain as the two named stubs.*
7. **The twelve interrupt handlers** — timers, and SCI0's ERI/RXI/TXI, which
   is how the embroidery module is served. *Done: all ten slots the original
   fills in.*
8. **The application's command protocol** — the counterpart of the boot
   ROM's, including the session flag at `H'57FF80` that EMB-Serial reads.
9. **The long tail** — menus, stitch generation, embroidery.

## Data

The code region holds code and its data together. A full rewrite relocates
both, so most data can move and be referenced by C symbol instead of by
address. Three things cannot move:

* the entry table at `H'200000`, because the boot ROM reads it;
* the identity block at `H'200100`, because the configuration screen does;
* anything the download protocol addresses absolutely — including the
  `H'204000`–`H'207FFF` window the boot ROM refuses to read, and the session
  flag at `H'57FF80`.

## Calling back into the boot ROM

The application reaches boot ROM routines through the low vector slots with
`JSR @@aa:8`, using the original's convention — first argument in `ER6`,
result in `R6` — which is why those wrappers in `app.c` are assembly. The
boot ROM's own shims translate in the other direction. See
`../bootrom/README.md`.
