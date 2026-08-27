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
The **touch panel's two axes** come in on the same pass, as analog channels
0 and 1 -- not the knobs, which are the quadrature pairs on port C read by
the interrupt. `H'208E2A` was called `knobs_read` on the first reading of it
and is `touch_read` now: everything it produces ends in H'FFFED9 and
H'FFFEDA, which `touch_hit` scales by the calibration at H'11A87E.

The whole panel is read ten times over. A bank that reads differently from
the pass before is thrown away rather than believed, and a touch reading
that has moved by more than two counts is put back to H'02, which cannot
pass the test at the end. Only what has been still for ten passes survives. That is
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

## Part 10, the embroidery panel

Thirty-four routines under `H'235B0E`, the module's own eighteen-state
machine, which is what `H'237E3C` waits on. The state machine itself is not
written yet -- it and three others (`H'2417D4`, `H'2431EE`, and the
dispatcher's `H'222AAC`) all end in computed jumps into inline blocks -- but
almost everything beneath it now is.

### What the panel is made of

  * **the picture box** -- `H'24A432` puts a H'22 by H'22 box at one of four
    places by `H'114D8E`, cleared, drawn from `H'34C148`, or filled; five of
    the twelve states have no box at all. `H'21747E` is the same box nailed
    to one place. `H'2498BE` blinks it, ten passes on and ten off, and a stop
    key held freezes it wherever it is.
  * **the progress bar** -- `H'217AEA`, a hundred and fifty-one pixels over
    ninety-nine steps. The scale is a float constant, H'3FC33B7A, and the
    zero point is forty less that, worked out at run time by the ROM and
    folded by the compiler to H'4219E624 -- the same number either way.
  * **five labels** -- `H'21759E`, `H'2175E8`, `H'21789C`, `H'217932` and
    `H'21797E`, each one string in one fixed box, all centred. Three run down
    the left edge and two along the top.
  * **the numbers** -- how many millimetres across and down (`H'231C5E`), how
    many stitches (`H'23202A`), how many minutes are left (`H'242FEA`), how
    far through as a percentage (`H'23228A`), and which colour of how many
    (`H'236B5A`, `H'23865A`).
  * **the colour picture** -- `H'238D06` walks the record list at `H'104D4A`
    and `H'238B62` paints the one it lands on, a one-bit stencil, most
    significant bit first.

### Three things worth writing down

`H'249DE8` waits for the module to name itself and compares five bytes of its
reply against `H'200103` -- inside the application's own identity block. The
machine checks the module's firmware against its own version stamp.

`H'238B62` keeps its running offset in sixteen bits, so a long enough record
list wraps round past the end of the window at `H'114D49`. That is exactly
what its one guard tests: reading past it sets bit H'2000 of `H'114D4C` and
skips the byte. The test case for that had to be built by hand -- a first
record whose two size words multiply out to H'FFE8, so the second lands at
`H'114D3A` with its data straddling the edge.

`H'242FEA` works the minutes out in floating point two different ways by
`H'114D96`, and getting the operand order of the divide and the subtract
wrong would have been easy. It was not: the eight cases passed first time,
which pins `H'200530` as *er6 / er5* and `H'2005FE` as *er6 - er5*.

### The same lesson, twice more

Part 9d ended on a failing case that was an unpinned input rather than a
wrong reconstruction. It happened twice more here.

`H'2431C2` failed until `H'11A6C2` was pinned -- that was part 9d. Then five
label cases failed because they use the *real* font tables in RAM at
`H'119A66`, `H'11936E` and `H'1196EA`, and the two boots do not agree on
every entry. A one-character string of "A" drew a glyph on the original side
and nothing on the rebuilt one. The fix was to fill all three tables with a
synthetic font of ten glyphs.

Filling all three with the *same* font then hid a real error: the mutation
that swapped one label's font for another's went unnoticed, because the two
were now identical. They are filled with three different rotations of the
glyph set now.

A third: `H'21759E`'s box moved by a row and nothing changed, because the
frame buffer was filled with nought and clearing a box writes nought. Filling
it with H'55 instead made the mutation visible.

Three separate ways for a case to be quietly empty, all found by mutation and
none by the suite going green.

### The mutations

A hundred and forty-two planted errors over the thirty-four routines. Three
survived and all three are provably so:

  * `H'217AEA`'s scale constant off by one unit in the last place. Computed
    for all ninety-nine inputs: the edge pixel never moves.
  * `H'244A2A`'s quarter-second delay and `H'249DE8`'s H'9C4 tries -- the
    same shape as part 9c's survivor, a count nothing can observe.
  * `H'23865A`'s "drawn once" flag. With it wrong the picture is drawn twice
    instead of once, from the same record, into the same place.
  * `H'23228A` copies six bytes out of `H'25077C` into its buffer and writes
    straight over them with the number. Nothing reads past the terminator, so
    the copy cannot be seen. Reproduced because it is there.

`H'23BB18` is here too: the hoop check that runs before a pattern is
sewn, four steps deep, asking the module which hoop is fitted and comparing
the answer against the three stitch-record bytes it last asked about.

**1,964 cases pass over 420 routines.**

## Part 10b, casing what was already written

Two routines from part 8's dispatcher front had been written and never
compared: `H'21F87A`, which decides whether the screen being left goes on the
stack, and `H'21F940`, which decides what to do about the one being asked
for.

`H'21F940` was wrong. The reconstruction had

    if (to == 0x007B) return 0x02;

where the ROM's `BEQ H'21F992` jumps to the *write*, not to the refusal:

    H'21F96E  CMP.W #H'007B,R6
    H'21F972  BEQ H'21F992        <- the write
    ...
    H'21F992  MOV.L @ER7,ER6
              MOV.W @H'11B10E:24,R5
              MOV.W R5,@ER6
              MOV.B #H'03,R6L

Screen H'7B and a forced move both *skip the "already there" test* rather
than answering "do nothing". The comment above the routine described the
behaviour correctly; the code under it did the opposite. Thirty-six cases
found it in one run.

That is the argument for casing code that is already written, rather than
only casing what is new: eighty-three of the routines in `app.c` still have
no head-to-head case, and at least one of them was wrong.

Three of the mutations against these two needed the cases sharpening before
they were caught, all for the same reason -- the mutant and the original
happened to agree on the inputs chosen:

  * screen H'7B needed `H'11A17E` to *hold* H'7B, or both readings write.
  * a forced move needed the screen to be the one already up, or both
    readings write.
  * the message number needed a message the fill actually has a table entry
    for; two absent ones draw the same nothing.

Three more went the same way: `H'21DFCA`, which resets the user pattern list,
and `H'2104E2`, the byte fill that lets the service hook run between bytes.
One mutation against the fill survives -- swapping the hook to *after* the
store instead of before it -- because in every fill the hook's guard is false
and it does nothing at all. Making it observable means letting `service_tick`
run, and that path's own inputs are not pinned yet: the case built to try it
failed on the unmutated build, which is the "unpinned input" story a third
time. It is dropped rather than left failing.

**2,003 cases pass over 424 routines**, in thirteen seconds.

## Part 11, the state machine cluster

Three state machines call each other and call back into the first through
H'244DE0, so none of them compiles without the rest: `H'235B0E` is the
module's own eighteen states, `H'2417D4` the nine that fetch a run of
patterns, and `H'2431EE` the twelve that start and stop the sewing. Walking
the closure out from them gives eighteen routines and about 3,900
instructions, and nothing beyond it except the compiler's own float library.

Seventeen of them are written now. What is left is `H'244F14` and the two
machines that call it.

### The ROM has its own libm

`H'24ABFE`, `H'24ABEE`, `H'24ADDC` and `H'24ABC4` are a square root, a sine,
a cosine and an arc tangent, with `H'24AF80`, `H'24AE5A`, `H'24AD22` and
`H'24AD62` -- a `frexp` and a `modf` -- underneath them. The application is
linked `-nostdlib`, so there is no `libm` to fall back on and they have to be
right to the last bit.

They are. Every constant is written as the decimal that encodes to the ROM's
exact pattern (H'3F490FDB is `0.7853982f`, H'3FB504F3 is `1.4142135f`, and so
on), every operation is in the order the ROM does it, and the three places
that take a float apart -- halving an exponent, clearing a sign, forcing a
mantissa -- do it on the bit pattern, which is what `ADD.W` and `AND.W` on the
top half of a register amount to.

**A hundred and twenty cases over twenty arguments each, and they passed on
the first run.** That is the strongest evidence yet that the float library at
`H'200530` and its neighbours is the same code GCC ships: if it were not, the
last bit of a rational approximation would not survive twenty inputs, six
routines deep.

The sine and cosine share `H'24AF80`, which reduces the angle by quarter
turns. Above 65532 of them it stops trusting `(float)(int)` and reduces with
two calls to `modf` instead.

### H'2417D4, the fetch

Nine states, and the jump table's shared-tail trick again: states nought and
one enter the *same* body, four bytes apart, with nought only setting the
counter to H'FF on the way in. The pattern records arrive sixteen bytes at a
time from H'11F316 into H'11A25A, one per slot, each followed by a message
H'02 out to the module.

Under it, `H'241FA0` takes the whole run of patterns together and moves the
current slot's centre to the middle of the box that holds them all;
`H'24217A` gives one pattern's reach along each axis once its scale and
rotation are applied, out of the four corners of the rotated rectangle; and
`H'241228` walks the block stream at H'10C27A to find the header.

### Two errors, and one that was not

`H'23E510`'s step three does not wait for the link. Every refusal in it still
moves the sequence on -- all the branches land on the same `11A63E = n + 1`
-- and the reconstruction had them returning instead. One case found it.

`H'2417D4` state one compares "how many patterns, less one" against "which
slot we are on", and the reconstruction read a *word* at H'11A640 where the
ROM reads a byte. The high half of that word is H'11A641, so the comparison
never matched and the machine never left state one.

The third looked like an error and was not. Several cases failed on H'FFFFB4
alone, "original 84, rebuild 80" -- and H'FFFFB4 is SCI0's status register,
not RAM. Bit 2 is TEND, which the model sets when a transmission finishes,
so the two runs disagreed only because the rebuild got there in fewer cycles.
The cases exclude it.

`H'23DE04` cannot be compared at all: it busy-waits for a send to complete,
and the harness runs with interrupts masked, so both images spin for ever.
It is written from the disassembly and left uncased, which is said here
rather than papered over.

**2,244 cases pass over 439 routines**, in fourteen seconds.

## Part 11b, the cluster closed, and the module key with it

`H'244F14`, `H'2431EE` and `H'235B0E` are written, and with them the
`H'244DE0` -> `H'23182A` -> `H'2315A4` chain that closes the knot: the state
machine calls the screen step, which calls the link-lost handler, which calls
the wait pass, which calls the state machine. Four routines, one cycle, and
none of them compiles without the other three.

`H'237E3C` -- the module key handler that started this whole thread -- is
written too, along with `H'231B32` underneath it.

### H'244F14, the hoop check

The same four rotated corners as `H'24217A`, with the hoop's own middle and
the pattern's placement added in, then each corner tested against the hoop's
width and height. Any corner past an edge, or behind the origin, and the
answer is no.

The comparisons go through `H'20074A`, which compares two floats by their bit
patterns and gets it right -- including both-negative, where it masks the
signs and compares the other way round -- so plain C comparison does the same
thing.

The mutation that swaps the two arms of its "which way round is the pattern"
test survives, and provably: the four corners are the same multiset either
way, and the test asks only whether *any* of them is outside.

### Three registers the ROM never initialises

`H'235B0E` uses R1 as a H'C8 spin count in state H'0A and R3L as a delay
counter in states H'0D and H'0E, and initialises neither. Both are saved and
restored by the prologue, so neither survives a call -- the spin count never
reaches H'C8 in the original any more than it does here. They are written as
locals starting at nought, and the H'C8 branch is unreachable in both.

`H'24217A` and `H'244F14` have the same shape: the angle to the corner is
never worked out when the scaled width is nought, and the ROM uses whatever
the stack held. Started at nought here, and no case goes there.

### Two blocks that look the same and are not

`H'235B0E` and `H'244DE0` both write out a "give up on the module" block, and
they are nearly identical: everything cleared, the hardware released, message
H'03/H'04 sent. The reconstruction shared one routine between them and the
cases caught it. `H'244DE0`'s clears H'114D83 rather than H'114D65, does not
raise bit 5 of H'FFFEC4, and does not ask for a screen afterwards.

### A case set that was quietly running the wrong slot

Every `H'235B0E` case had `H'11A660` set to one and then zeroed again, because
a fill inherited four routines back had `11A660:C0` in it -- a range fill,
landing later in the dictionary than the single byte that set it. So all
fifty cases ran with slot nought, and state five's "how many turns" scale was
never exercised: two mutations against it went unnoticed until the byte was
re-appended at the end of the fill.

That is the fourth distinct way a case set has been quietly empty in this
project, and the second caused by fill ordering rather than by a missing
value.

### The mutations

Fifty-six planted errors across the cluster. All were caught except the
corner-swap above.

Three cases had to be dropped as untestable rather than left failing: two
routines busy-wait for a send to finish, which needs the transmit interrupt
the harness masks, and one input to `H'231B32` sends the ROM into an infinite
loop of its own. All three spin identically in both images.

**2,400 cases pass over 447 routines**, in nineteen seconds. The application
now has 91,352 bytes of reconstructed code in it.

## Part 12, the screen bodies -- measured, and begun

`H'22382A`'s dispatch table at `H'2238B0` has **seventy-nine entries** over
fifty-five distinct targets, covering screen numbers H'00 to H'4E. The bodies
are inline blocks running from `H'2239EC` to about `H'2283xx`: **5,022
instructions**, none of them a callable routine.

Between them they call 113 routines, of which 86 were not written. Those 86
come to **10,520 instructions** at the first level alone, before their own
closure. So the screen bodies are roughly three times the size of everything
reconstructed in parts 10 and 11 put together, and they are the last large
piece of the application.

### The first five helpers

Ranked by how many bodies call them:

  * `H'213882` (20 callers) -- the eleven boxes of the stitch-width strip,
    from a table of four words each at `H'11524E`, with the one `H'FFFEEA`
    names drawn over the top. `H'11B2D0` remembers which was lit, so a move
    repaints two boxes rather than twelve.
  * `H'21348C` (12) -- the two arrows beside a list, lit or dim by where the
    window sits in it. The end of the window is *entry one's* value plus the
    span, which is an odd place to keep it; the length of the list is the
    first word the list points at.
  * `H'21B34C` (10) -- the number in `H'FFFECD` drawn top right, and only
    when it moves.
  * `H'213ABC` -- the needle-stop picture, one of two by bit 5 of
    `H'FFFEF6`. The bit is rotated down to the bottom of the byte three times
    rather than shifted, which comes to the same thing.
  * `H'21752E` -- the sewing screen's picture box, cleared in frame B and
    drawn into frame A.

Twenty-one planted errors, all caught but one, and that one provably: in

    if (v == 1) { ... } else if (v > 1) { ... }

the else-if can never see v equal to one, so `>` and `>=` agree on every
value that reaches it.

Five of the twenty-one needed the cases sharpening first. The back arrow's
whole half was never exercised because no case gave the box a value of one,
and the window comparison was never at its boundary; both were fixed by
choosing the span so the window ends exactly on the list's length.

**2,431 cases pass over 452 routines.**

### Four more, and the shape they share

`H'21EF02` and `H'21C672` are the same screen written twice: one flash byte,
two boxes that are the yes and the no of it, H'19 to save and H'1A to leave
without saving. The first owns `H'57EFC6` and the second `H'57EFC7`, and the
second also drives bit 6 of `H'FFFEC1` from what it saved. The working copy
lives in RAM and is what the single-byte flash write is given the address of.

`H'21BDD6` is the demonstration screen: five pictures a second apart, with
`H'11B362` counting round them modulo five, and picture nought coming out of
the configuration block rather than the table. `H'2224E8` puts the pattern
strip back from the second store.

Twelve of the twenty-eight mutations against these needed the cases fixing
first, and the reasons are worth listing because they are the same few every
time:

  * every picture in the table pointed at the *same* image, so which entry
    the code chose could not be seen. Ten distinct run-length pictures had to
    be built by hand -- a word count and then runs of length and colour --
    because the two real images to hand were the only ones that loaded.
  * the box the touch landed on had a value none of the four branches
    matched, so the save and leave paths never ran at all. Six mutations
    were sitting behind that one.
  * the case fill unioned the touch-test fill *under* the screen-switch fill,
    and the second one's `H'11A160` range put the hit test's own state back
    to nought. Reversing the two made the presses work.
  * `H'114DE0`, the millisecond count, is not the same in the two boots, and
    the leave path writes it.

Two mutations survive. One is the `>` in an else-if that can never see the
value the branch above it caught. The other swaps two `hitbox_set_state`
calls on different boxes, which is invisible while the boxes do not overlap
-- true of every real hit-box table, but not a property of the code.

**2,463 cases pass over 456 routines.** Nine of the eighty-six callees are
written.

### The fully closed ones

Of the seventy-seven left, twenty-one call nothing that is missing and have
no jump table of their own -- 2,340 instructions between them. **All
twenty-one are now written.** The first eight:

  * `H'222FD2` -- where the page holding a position starts. The first fifteen
    are one page; after that they are five long.
  * `H'21B9CE` -- a screen with one way out, key H'77 for screen H'17.
  * `H'21B6C6` -- the byte in `H'FFFED8` as a number, right-aligned.
  * `H'21C466` and `H'229F7E` -- two more yes-and-no screens, but these save
    nothing: a RAM byte and straight back to the slot-four screen.
  * `H'23033A`, `H'2180C8`, `H'21C3B8` -- three menu screens. The middle one
    sets `H'11B108` to the first item of whichever category was pressed.

`H'21B6C6` gives its number two bytes of stack, which is one short of what
three digits need. The field never holds three, and the reconstruction gives
it four with a note rather than reproducing an overflow that would land in a
different place on each side.

`H'222FD2` has a boundary that cannot be told apart: at fifteen, both the
early return and the loop that follows it arrive at one.

Thirty-one more mutations, and the pattern from the batch before repeated
almost exactly. Nine needed the cases fixing first, and every one of the nine
was a case that never reached the code it was supposed to test:

  * a box whose value matched none of the branches (four times);
  * a box marked absent in the fill, so the press never landed on it;
  * a box with a *list* behind it, so its value came from the list rather
    than from the field the case had set;
  * a descriptor table where every category answered "position one", which is
    also what the routine returns when it finds nothing;
  * a screen stack that was already empty, so popping it did nothing;
  * `H'114DE0` again -- the millisecond count, which the two boots disagree
    about and the leave path writes.

### Three more menus, and a tool to stop hand-cutting hit boxes

`H'218292`, `H'218188` and `H'217DE0` are three menu screens of the same
shape as `H'2180C8`: a press, a screen change, and the first item of a
category into `H'11B108`.

Nine of the failures in the batch before came from hand-written hit-box
offsets in the case fills, so `tool_boxfill.py` now generates them: a row of
boxes H'20 apart and H'18 wide, eight to a row, with a `touch()` that aims
the panel reading at the middle of one. The row spacing is what lets a whole
row sit inside the single byte the panel leaves in `H'FFFED9`; screens with
more than eight boxes get a second row H'30 lower.

### The last ten

  * `H'219CC8` -- twelve boxes, ten of them a choice. The lit one is
    remembered in `H'11A1AA` so the next press can put it back, and the
    choice is written to `H'11B0FF` as the value plus H'9C.
  * `H'21BBE6` -- the display test: three full-screen fills, one press apart.
  * `H'2227E6` -- the input trim, a step at a time, with one byte written
    back to flash on accept and read back out of it on cancel.
  * `H'21B7DA` -- the pedal test: a number and two lamps, each redrawn only
    when it has moved.
  * `H'21BA0E` -- which machine this is, H'AA or H'B4, written to `H'57FF80`.
  * `H'22253E` -- calibrating the touch panel. Two crosses give a straight
    line each way; the four floats go to `H'57FFA0`.
  * `H'211E9C` -- `H'211C38` with a panel drawn round each picture.
  * `H'21CB2C` -- the top sewing speed, in steps of ten.
  * `H'2303EE` -- the module's own menu: two boxes and eleven ways the second
    can go.
  * `H'2144CA` -- the strip along the top of the sewing screen, redrawn only
    where something has moved.

Three of these needed the harness stretched a little.

**A local read before it is written.** `H'21BA0E` hands `H'211A9E` its
second local on the way out, and on that path nothing has written it: what it
gets is the box index the hit test below leaves there, from whichever pass
last went that way. GCC puts its own second local at the same offset -- four
bytes of saved register then `SUBS #4`, the same frame the original builds --
so pinning the word below the harness's stack pointer pins it on both sides,
and the mutation that replaces the read with a constant dies. The
reconstruction leaves the local uninitialised deliberately, with a note.

**Colour 1 does not exist.** `H'20E826` fills a run with H'00 for colour 0,
H'AA for colour 2 and H'FF for colour 3, and has no case for colour 1 -- so a
full-width fill in colour 1 lays down zeros exactly like colour 0. The
mutation that changes the display test's second fill from 0 to 1 is
equivalent and cannot be killed; 2 and 3 both kill it.

**`EXTS.L` on a byte.** `H'2144CA` scales a foot number into a table with
`SHLL` three times and then `EXTS.L`. The input is a byte, so the sign bit
can never be reached and the sign extension can never bite: replacing it with
a zero extension is a second equivalent mutation. Both are transcribed as the
original has them.

Two things could not be driven from these screens and are recorded rather
than tested:

  * `H'21BA0E`'s flash branch calls `H'21DDC4`, and from this screen's boot
    state that call never returns -- on *both* images, so it is the state and
    not the reconstruction. `H'21DDC4` and `H'250AEC` have their own cases;
    what is untested here is only that this screen reaches them.
  * `H'2303EE`'s long branch ends with two sends and two waits for the link
    to go quiet. With interrupts masked a send never completes, which is the
    same wall the state-machine cluster hit. Every case for that branch
    leaves the link busy so it stops short of the sends.

Ninety-nine mutations across the ten, all but the two equivalent ones killed.
Six needed the cases fixing first, and the by-now-familiar list grew two new
entries:

  * a wide fill written *after* the bytes it covers, so the value the case
    thought it was setting was zeroed again -- three times, once for a word
    and twice for a whole list. `routines.json` fills apply in dictionary
    order, and `dict.update` keeps an existing key where it already was;
  * a font table copied by address prefix rather than by address range, so
    the glyph pointers were left at zero and the text drawing ran off the end
    of the frame buffer;
  * a screen number that the same kind of wide fill had zeroed, so the push
    at the top of `H'21CB2C` found the screen already on the stack and did
    nothing.

**2,640 cases pass over 477 routines.** Twenty-seven of the eighty-six
callees are written; app.bin is 99,812 bytes.

### Counting again, properly

The census that said "twenty-one closed" was done with a linear scan for
`JSR` and `BSR` opcodes. That scan reads data and instruction operands as
though they were instructions, so it invents calls: `H'21B3DE` came out
"one routine away" because of four bytes in the middle of a `MOV.W` that
happen to look like a `BSR`. Redoing the census through the decompiler --
which follows the control-flow graph and knows where instructions start --
gives a different and correct list, and also says which routines end in a
jump table of their own.

Of the fifty-seven, four were genuinely closed and table-free. All four are
now written, along with three of the routines they and their neighbours wait
on:

  * `H'20E126` -- one display buffer filled with a byte, H'4B00 of them.
    Sixty-one of the bodies call it, which makes it the most-called thing
    left. It takes its buffer back off the stack rather than out of the
    register it arrived in, and throws the saved copy away with the
    arguments rather than popping it.
  * `H'21B726` -- the needle position, in steps of four between zero and
    H'10.
  * `H'21BD1E` -- the application's version, drawn once. The string is the
    identity block from its fourth byte, past the "NMM".
  * `H'219978` -- four boxes, three of which go somewhere and one of which
    comes back off the stack to whatever `H'11B0A6` names.
  * `H'22298C` -- the version again with the module's beside it, drawn only
    when the module answered something other than the empty string.
  * `H'24AAA2` -- the ROM's strcpy, beside the strcat that was already
    there. Same idiom: the loop ends on the flags the *store* left, because
    the `ADDS` between does not touch them.
  * `H'222DF4` -- three runs of screen numbers that want repainting.
  * `H'212E78` -- a run of boxes drawn again from whatever list each one
    already points at, rather than from a list handed in. It copies the
    whole H'12-byte entry into a local first and works from the copy.

`H'2104E2` had to stop being `static`. It is a routine in its own right in
the original and has its own cases, but once `H'20E126` started calling it
the compiler inlined every copy and left no symbol for the harness to find.

Fifty-one mutations, all killed. Four cases needed sharpening, and three of
the four were the same thing in different clothes: a font table whose glyph
pointers were identical to another font's, a rectangle cleared to the colour
the buffer already held, and a screen that was already blank. Drawing that
changes nothing is invisible to a comparison of what changed, which is the
drawing-code equivalent of writing zero over zero.

**2,694 cases pass over 485 routines.** app.bin is 100,988 bytes.

### Fifteen more, from the level below

The corrected census also says how shallow the waiting is. Nine of the
thirty-six unclosed helpers are one routine away, and the routines they wait
on are mostly small. Written next, in rough order of what they unlock:

  * `H'21CF7C` and `H'21CF86` -- how deep the screen stack is, and what is at
    a given depth. Entry zero of the stack is the depth itself, which is why
    both read from the same address.
  * `H'24AAD2` -- the ROM's strncpy, beside the strcpy and strcat. The count
    is tested before it is decremented, so a count of zero copies nothing.
  * `H'246D7E` -- the other half of `H'246D8C`: the same byte, set instead of
    cleared.
  * `H'2220DC` -- the drawing areas cleared and the queue position taken
    away.
  * `H'210544` and `H'21056C` -- the help pictures. Two tables of H'24-byte
    records, nine longwords each, one per machine; `H'219DE0` picks the
    second when the configuration byte says H'AA, so the first is the
    machine without the module. The record number is multiplied out as a
    longword and the field number added to the *low half* of it, so the
    index is a signed word however far the multiplication carried.
  * `H'21C150` -- the version string drawn where `H'21BD1E` draws it. Like
    `H'20E126` it takes its argument back off the stack.
  * `H'218C1A` -- a cursor that blinks: one pass in ten flips it.
  * `H'24A374` -- whether the thing that owns the link is still waiting.
    Six owners have an answer and everything else says no. `H'114DB9` is
    re-read before each of the six tests rather than kept.
  * `H'217CCE` -- one third of a box marked: the box cleared and a narrow
    rectangle drawn inside it at +2, +H'0E or +H'1A. Any other third does
    nothing at all, not even the clear.
  * `H'212FF0` -- a run of boxes moved along, values and lists carried over
    and the state carried with `H'211518` where it differs. Which way the
    copy runs depends on which way the run moves, so a source and
    destination that overlap do not eat themselves.
  * `H'21CFB0` -- the bar beside the balance setting, drawn from the middle
    of its track outwards. The far end is the value times minus three and an
    eighth plus a hundred and six and a half, so eight lands exactly on the
    middle.

Fifty-two mutations. Three survived and all three are provably equivalent:

  * `H'212FF0`'s two direction tests, `>` and `<`, at the point where the
    source and the destination are the same box. Copying a box onto itself
    writes nothing and cannot change its state, so it does not matter which
    way the loop is entered -- or whether it is entered at all.
  * `H'21CFB0`'s `<=` against the middle of the track, at the one value that
    lands exactly on it. Both branches then draw the same two rectangles:
    `top..end-1` and `mid+1..bot` on one side, `top..mid-1` and `end+1..bot`
    on the other, and with `end == mid` those are the same two.

**2,758 cases pass over 498 routines.** app.bin is 102,508 bytes.

### And seven more, as the cascade ran

Each of those opened others. `H'218378` closed `H'218780`, which closed two
more; `H'212FF0` closed `H'213274` and `H'213356`, and those two closed
`H'219A52`. Written next:

  * `H'218378` -- two strokes of a preview drawn or rubbed out, from two
    tables of H'10-byte entries in RAM, four entries each: two line segments
    per entry, offset H'8C across and H'64 down. The original spells all four
    branches out in full, sixteen table reads apiece.
  * `H'213274` and `H'213356` -- a run of boxes scrolled through its list, on
    and back. Both move the run with `H'212FF0` and then patch up the boxes
    left over at the far end. The un-greying writes the state byte straight
    rather than going through `H'211518`, but the greying does not, so that
    the picture is drawn.
  * `H'21AC2E` -- one box, and it goes back to whatever the screen stack has
    on top: the depth read first and used as the index into the stack, which
    is the same address read again as a number.
  * `H'216E6C` -- a message that goes away either when its box is pressed or
    when the thing that put it up stops waiting. A clear bit *and* a waiter
    that has stopped both fall into the hit test, which the pseudocode does
    not show: the two tests branch to the same place.
  * `H'21D104` -- the presser-foot pressure on the balance bar. Accepting
    leaves the screen before it writes the flash.
  * `H'218780` -- the two boxes at H'1C and H'1D and the stroke that goes
    with them. The record index here is widened *without* sign, unlike every
    other use of the stitch descriptor table.

`H'216E6C` reads its first local before writing it, exactly as `H'21BA0E`
does, and this time the compiler put its own first local somewhere else in
the frame. Both slots are pinned in the fill instead of one.

Forty-eight mutations, all killed. Two needed a case adding rather than
fixing: a run extended by one box only shows when the extra box is in a
state the call would change, so the greyed-boxes case had to exist before
extending the run from H'1D to H'1E could be caught.

`H'219A52` came with them: a list of twelve boxes with arrows either side
and a choice remembered in `H'11B328`. Scrolling puts the lit box out and
lights whichever box the choice has moved to *afterwards*, found by value
rather than by index because the boxes have shifted underneath it. That
routine is the first user of both scrollers and of `H'211B26`, and it is what
the whole cascade was for.

**2,823 cases pass over 506 routines.** app.bin is 104,420 bytes.

### The last three closed ones

  * `H'219DE0` -- the help page for whatever `H'11B0FE` and `H'11B0FF` name.
    Which of the two picture tables, and how long a record is, both follow
    the configuration byte: the machine with the module has nine parts to a
    record and the one without has ten, though both tables are laid out the
    same way. A record whose first part is there and whose second is not is
    one picture for the whole page: it goes into the scratch buffer and is
    copied across from there, which is what stops it appearing a strip at a
    time. Otherwise the nine parts go into the nine boxes, the ninth placed
    by its own width and height so its bottom right corner lands at
    H'0112, H'00EA.
  * `H'21C846` -- the beep settings: eight of them, each a pair of bytes,
    copied out of flash to be edited and written back on accept. Boxes 9 and
    H'0A check `H'11A1B6` before following it; box H'0B does not.
  * `H'221B6E` -- the pattern strip put back when one of seven screens is
    returned to. Four shapes, all ending the same way, and anything but
    those seven screens does nothing at all -- not even the tail. The strip
    is the same four rectangles every time; what changes is which run
    carries the list, which run is handed over, and where the arrows are.
    Two of the four slide a run of boxes along first, which is what makes
    room for the wider strip.

Forty-six mutations, all killed. Twelve needed the cases sharpening, and
they were all one thought: **a mutation can only die if the code it changes
had something to change.** A run extended by one box needs that box to be in
a state the call would alter; a shift moved by one box needs the boxes to
hold different values; `fresh` on the arrows needs them already drawn the
way they should be; `force` on the leave hook needs the pattern number to
match the one it would write. Each of those started out invisible because
every box in the fill was identical.

Two of the twelve were the fill-ordering trap again -- `H'11A1BB` was left
at whatever the boot had put there because the key that set it did not match
the prefix the case builder filtered on, and `H'11A169` was zeroed by a wide
key written after it.

Writing `H'218780` had already closed two more of the fifty-seven, and they
turned out to be one routine twice over: `H'2189A6` and `H'218ADE` step the
stroke number in H'FFFEFD, one per stroke table, wrapping at three and at
H'0F. Entering either calls `H'218780` twice, once as a press and once as a
release, which is what puts the two boxes and the stroke up to start with; a
press on a greyed box is dropped before the message, so a screen whose
pattern is in the wrong category says nothing at all.

Their fourteen mutations found five more cases that reached the code without
being able to see it. Two are worth naming because they are new shapes:

  * `hitbox_kind` was read *after* `H'218780` had already un-greyed the box,
    so by the time the test ran the box was in state 0 and no comparison
    against it could matter. It takes a screen where the category is wrong
    -- so the un-greying never happens -- for the test to bite.
  * The down box carried the up box's value. The case pressed the box it
    meant to and the routine took the other branch, and every mutation of
    the down branch lived.

**2,881 cases pass over 511 routines.** app.bin is 107,044 bytes.

### What a mutation needs before it can die

Three of `H'219A52`'s fifteen mutations survived their first run, and all
three for the same underlying reason: the case reached the code but the code
had nothing to change.

  * Swapping the `second` argument to `H'211B26` picks a different box, but
    both boxes were already in the state the call sets, so the call did
    nothing either way. It takes a screen where the boxes are lit before the
    difference is visible.
  * Swapping the slot in a `screen_switch` matters only on the short path,
    which needs `H'11B0A8` up and the slot already holding that screen.
  * Storing the index instead of the value is invisible while the list is
    built so that a box's value equals its index. Making the list hold
    `k + 5` fixed it.

The first of those also needed `H'11A169` moved to the end of the fill: a
wide `H'11A160:40` zeroing sits later in the dictionary than the byte it
covers, so setting the screen number early set nothing at all. That is the
fill-ordering trap for the fourth time this part, and by now the rule is
worth stating plainly: **in a `routines.json` fill, a wide key overwrites
every narrow key written before it, and `dict.update` does not move a key
that already exists.**

### What the rest of it looks like

Fifty-seven helpers are left. They are the same shape as these -- small
drawing routines with a handful of callers each -- with a few larger ones
underneath: `H'21AC9E` at 496 instructions, `H'221B6E` at 441, `H'2299A6` at
432. Three of them (`H'21F9D0`, `H'21FF3C` and the bodies themselves) are
further jump tables with their own inline blocks, so the count above is a
floor rather than a total.

Fourteen of the fifty-seven are written and forty-three are left. Twenty of
those end in a jump table of their own -- a handful of instructions plus a
table of inline blocks that has to be measured before it can even be
counted -- and twenty-three are plain routines waiting on between one and
eleven others. **None of the twenty-three is closed**: every one waits on
something. The two that would open the most, `H'24610A` with five and
`H'214DD4` with four, are themselves dispatchers with inline blocks, so each
has to be measured before it can be started: `H'214DD4` is twenty keys and
twenty handlers over 1,652 bytes, `H'24610A` twelve handlers over 766.

So the part that could be done by following the call graph downwards is
done. What is left needs the tables opened, which is the same job as the
screen bodies themselves.

## 13. The first table opened: the hoop

Eleven of the forty-three are blocked by nothing but their own jump table.
The smallest of them, `H'230D4E`, turned out to be the whole of one screen
and to close cleanly, so it went first: a nine-way table whose handlers are
eight instructions each, nine routines behind those, and one shared helper.
About 460 instructions for a complete screen body.

The screen moves the embroidery hoop. `H'104C7A` and `H'104C7B` are how far
it has been moved from where the module thinks it is, kept as signed bytes in
half-millimetre steps. `H'248FF0` draws them: each is halved, its sign thrown
away, turned into decimal, given "mm" and put into one of the labels down the
left edge. The eight nudge handlers are one routine with two constants
changed -- the direction code sent to the module in `H'11A615`, and which
offset moves -- and the codes run clockwise from north: 1 N, 2 NE, 3 E, 4 SE,
5 S, 6 SW, 7 W, 8 NW. All eight refuse in the same three ways: the link never
went quiet, or either offset is already further than H'64 from home. The
limit is tested against the offset *before* the step, so a nudge that would
take it past H'64 is allowed and the next one is not. The ninth box sends the
hoop home and leaves for screen H'1F.

The eight return 0 for the two that only move north and 1 for the rest, which
looks like nothing more than the order the handlers were written in.

### The address list that was in the wrong order

`H'248FF0` draws its two labels through `H'21759E` and `H'2175E8`, and the
first reconstruction called `text_left_94` and `text_left_BC` for them --
from a comment in part 8 that listed five addresses and five names in what
looked like matching order. It was not matching: `H'21759E` draws at y H'D9,
not y H'94. The comparison caught it at once, and the comment now carries the
pairing as a table rather than as two lists the reader has to line up.

### What could not be reached

`hoop_reset` sends the module message H'0D, waits two seconds, and only then
zeroes the slot's two stored positions and sends message H'02. Nothing past
that first send can be reached in the harness, and the reason is structural
rather than a gap in the cases: `link_send_start` sets `H'11F29E`, only the
transmit interrupt clears it, and the comparer masks interrupts on both sides
on purpose -- otherwise the two images, which are never at the same point in
the boot after the same number of steps, take different interrupts and every
counter the handler touches shows up as a difference. So four mutations in
that tail survive and always will. They are recorded here rather than papered
over.

One other survivor is genuinely equivalent: in `hoop_offset_label` the test
`raw < 0` can be widened to `raw <= 0` with no effect, because the two
branches differ only by the absolute value and H'24ADC8 leaves zero alone.

Twenty-nine mutations over the three routines, twenty-four killed, four
unreachable for the reason above and one equivalent.

**2,905 cases pass over 513 routines.** app.bin is 107,668 bytes.

### The second one: ten categories

`H'217F04` was the next up and cost nothing at all beyond reading it: its
eleven handlers call one routine, and that routine is `H'212994` --
`first_item_of_category`, written back in part 4. So the table opened without
adding a single new callee.

Ten of the eleven boxes are one category each, H'05 to H'0E in box order, and
the eleventh only takes the screen change. Every handler is the same four
instructions with one constant changed, which means the whole table would
collapse to "category equals value plus four". It is left as the table it is:
that is what the original holds, and a category out of place should show up
as a difference rather than as an off-by-one nobody notices. The eighteen
mutations bear that out -- each of the ten constants dies on its own.

Both the message and the screen change happen *before* the table is reached,
so a value the table does not cover still leaves for screen H'02. That is
what the "value 0C", "value 00" and "value FFFF" cases are for.

**2,920 cases pass over 514 routines.** app.bin is 107,864 bytes.

### Measuring five together, and writing four

The five at sixty-odd instructions were measured as a batch before any was
started, and the measurement changed the plan. Four of them -- `H'21A320`,
`H'21A56C`, `H'21A7F6` and `H'21AA42` -- call nothing that is not already
written: `touch_hit`, `hitbox_set_state`, `hitbox_kind`, `message_show_held`,
`screen_stack_push` and `pop`, `screen_switch`, `screen_remember`, and
nothing else. Two hundred and fifty-four dispatcher instructions and 1,254
bytes of inline bodies between them, and not one new callee. The fifth,
`H'2195F2`, is blocked by `H'214DD4` and has the largest bodies of the five,
so it was left where it was.

The four are one screen written four times: pick one of a list of longwords
out of the record at `H'11B2AA`, light the box that was pressed, put the last
one out, and on accept carry the pick across to `H'1161B8` and leave for
screen H'3A. The remembered box lives at `H'11B32A`, `H'11B330`, `H'11B336`
and `H'11B33C` -- six bytes apart, one per screen, in the order the routines
appear. Byte-level similarity between pairs runs 40 to 60 per cent: variants,
not copies.

Only the lighting is shared here, as `pick_box_light`. Everything else is
written out four times, because what differs between them is exactly the sort
of thing a shared skeleton would hide:

  * the guard on which boxes light -- two name H'04 and their last box,
    `H'21A56C` takes a range instead (everything below H'0B);
  * what the table is indexed by -- three by the value less one, `H'21A320`
    by the value less *two*, so its box 1 is not in the table at all;
  * where "back" goes -- H'39 for three of them, H'0E for `H'21A320`;
  * whether accept remembers the screen -- three do, `H'21A320` does not.

`H'21A320` is the odd one throughout. Two of its ten table entries point at
the shared tail, so boxes 5 and 7 are in the table and do nothing -- and
those same two boxes are named by its accept, which is a four-way branch on
which box is lit rather than a single screen change. Boxes 1, 5 and 7 have
screens of their own, nothing lit does nothing, everything else goes to H'3A.

Between them the four tile a table of longwords: `H'21A56C` takes H'04 to
H'24, `H'21A320` H'30, H'34, H'5C, H'78 to H'80, `H'21A7F6` H'38 to H'58 and
`H'21AA42` H'60 to H'74. `H'21A56C`'s tenth pick is the only one that depends
on the machine: H'2C with the module and H'28 without.

Fifty-one mutations, fifty killed. The one survivor is equivalent and worth
naming, because it is a shape that will recur: the original guards its
computed jump with a bounds test, and widening that test by one lets a value
through that the table does not cover. The reconstruction is a `switch`,
which has no such hazard -- the extra value falls to `default` and returns
the same 0. The bound is real in the original and cannot be observed in the
rebuild, so the mutant lives.

**2,990 cases pass over 518 routines.** app.bin is 109,280 bytes.

### The two menus

`H'21B3DE` was closed already and `H'21C19C` wanted one routine, `H'216DE0`,
which turned out to be fifty-two instructions and closed. Both are menus, and
between them they are most of what the machine's front page offers.

`H'21B3DE` is the main menu: fifteen boxes, and three things that happen
before the hit test is even reached. Two keys held together with a third
input low clears the pattern queue out to flash and says so -- the only place
in the application that offers it. Leaving for screen H'77 clears the panel
code. And the panel code is set from the low byte of the box value *before*
the table is reached, so it is set even for a value the table does not cover.
Most handlers are three instructions, but the two leading to H'1F and H'22
pass 0 for "remember" where the rest pass 1, and box H'0C is the odd one: no
message at all, and it toggles bit 7 of H'FFFEC1 with the box's own light.
Its release is what puts that box back down.

`H'21C19C` is the settings menu. H'11A17A is the "something was changed"
flag: finding it set writes the whole settings block out to flash and raises
H'11A1B4, so that box H'0D knows to go back to the screen the change came
from rather than to the remembered one. Box H'0C has no screen number of its
own -- the value left in the register from the slot argument is used, which
makes it screen 1. That is an omission rather than a choice, but it is what
the machine does and it is reproduced.

`H'216DE0` puts up the three "are you sure" screens, held off the same four
ways H'216D6C holds off a message. Two of them remember the screen being left
in slot 4; the third does not, and is the only one that cannot be answered.

### Three fills that measured nothing

Between them these three routines produced nine surviving mutations on the
first pass, and every one was the fill rather than the code. Three of the
causes have been seen before; two are new, and both are the same underlying
mistake seen from different angles -- **a key written into the fill is only
worth what its position makes it worth.**

  * `H'11A169` was set to H'10 and then zeroed by the wide `11A160:40` that
    the base fill applies later, so the screen number `screen_switch` copies
    into its slot was 0 and remembering it wrote 0 over 0. Three mutations
    on "remember" and one on the slot number lived on that.
  * `H'11B213` was set to H'14 and then zeroed by a wide `11B11E:200` --
    a range added *for a different purpose entirely*, two hundred bytes long,
    which happens to reach H'11B212. `list_page_start` therefore always took
    its early return and answered 1, and replacing the whole call with the
    constant 1 could not be told apart.

The other four were the familiar shapes: a message number with no entry in
the table, a box whose value equalled its own index so the two could not be
distinguished, a screen that H'222DF4 does not act on, and a bit that was
never set.

Fifty mutations over the three routines, all fifty killed once the fills
said what they were meant to say.

`H'21C19C`'s "something was changed" branch has no case: `settings_save`
cannot be driven from a screen's boot state, for the reason part 12 records.
The guard itself is still covered -- flipping the test makes every other case
run `settings_save` and hang, which the comparer reports as a failure.

**3,042 cases pass over 521 routines.** app.bin is 110,208 bytes.

### The panel, top to bottom

`H'2135FE` and `H'21F9D0` took eight routines between them and turned out to
be the whole of the strip of little readouts along the top of the screen.
From the bottom up:

  * `H'200EA4` -- one byte out of a ten-byte record in the settings block.
  * `H'214D24` -- the picture box at the top left and the "F" beside it, the
    letter read as a string constant out of the code region.
  * `H'214990` -- fourteen fields, each a few bits of a port latched into its
    own word, redrawn only when the bits change.
  * `H'214DD4` -- twenty **panel switches**, reached by key: cleared, stepped
    on, or read, with `clear` winning over `step`. Six are one bit toggled in
    place; the rest each have their own arithmetic. Key H'14 is the joke of
    the set -- its table entry points straight at the "not found" tail, so a
    key that exists answers H'FFFF exactly like one that does not.
  * `H'213B16` -- the drawing counterpart, twenty handlers in four shapes:
    a lamp, a picture patched into the icon table at the slot the box's own
    list entry names, a box greyed out, or one of each with an extra test.
  * `H'2136A6` -- whether anything in a box's list is away from its default,
    walked in two halves, with the indicator box lit or put out to say so.
  * `H'2135FE` -- the run of boxes brought up to date, one call per box.
  * `H'21F9D0` -- which of six lists the strip is filled from, chosen by the
    pattern's category less three through a twenty-three entry table.

Two hundred and thirty-two mutations over the eight, of which 225 died. The
seven that lived are all equivalent, and five of them are the same shape as
the one part 13 named: **the original guards a computed jump with a bounds
test, and the reconstruction is a `switch`, which has no such hazard.** The
other two are a dead argument and a dead copy: `H'2135FE` copies the whole
H'12-byte hitbox entry into a frame and reads three fields out of it, and
passes a key that the callee ignores on that path. Both are reproduced
because they are what the original holds, and neither can be observed.

### What the fills kept hiding

Nine of the eight routines' first mutation runs failed for want of a better
fill rather than better code, and two causes are worth adding to the list:

  * **The six picture tables abut and overlap** -- index 10 of one is index 0
    of the next. Building them separately, each with its own wide zero, had
    them wiping one another, so half the table mutations could not be seen.
    They are filled as one region now, one distinct picture per slot, on a
    seven-slot cycle chosen because the gaps between the tables are 4, 8, 9,
    10 and 13 slots and none of those is a multiple of seven.
  * **A picture that draws nothing.** One case selected a bitmap that leaves
    the test frame exactly as it found it, so a branch that skipped the draw
    and one that took it wrote the same bytes. The case now picks the other
    picture.

`H'11A169` was zeroed by a wide key applied after it for the third and fourth
time. It is worth saying plainly: **a key is only worth what its position
makes it worth**, and every wide fill in this suite is a hazard to every
narrow one written before it.

**3,364 cases pass over 529 routines.** app.bin is 115,108 bytes.

### One key search covering two questions

`H'2195F2` came free once `H'214DD4` was written. It is seven pictures to
choose between and four keys that are not pictures at all, and its neat
trick is that it answers a press *or* a screen change asked for elsewhere
with one search: `H'21F940` writes its answer into the same local the hit
test uses, so the key it looks up is whichever of the two arrived.

The seven picture keys, H'88 to H'8E, each blit their own bitmap into one box
and leave their own number in the bottom three bits of H'FFFEFA. The first of
them ands those bits away and ors nothing back, which is the only reason it
is a separate case rather than "or with zero". Four more keys hand straight
to H'214DD4 as a step, and **the low byte of the box value is the panel key**
-- the same word is the box's value here and a key over there.

Its handlers are stored back to front behind H'2196B6: the reversed-table
idiom again, thirteen word keys searched rather than indexed.

Sixteen mutations, all sixteen killed -- but the last one only after the
cases were fixed. The three "leaving for" cases were reaching
`screen_leave_check` and being told no, because `H'21F87A` answers "no" while
the screen stack flag is up, so the whole second half of the search was
never entered and removing it changed nothing. Putting H'11A17C down let
those cases through.

**3,387 cases pass over 530 routines.** app.bin is 115,544 bytes.

### The queue's own record

`H'210AB2` and `H'228A92` needed nineteen routines under them, and all
nineteen turned out to be the queue: a thousand records of H'0D bytes at
H'11BBAA, the block `settings_save` writes out to flash in one go.

Thirteen of the nineteen each put one field of one record. Five take a plain
byte and store it whole; the rest each have their own mask and their own
shift, and `H'228C90` takes a word and splits it across two bytes -- the
ten-bit number whose sentinel values H'3FF and H'3FE mean "nothing here" and
"the end of a group". Above them, `H'229198` fills a whole record from the
machine's current state, `H'229100` copies six of a pattern's parameters
into it, and `H'21DD6A` opens a slot in a counted list of words by shifting
the tail up and taking the count with it.

`H'210AB2` makes room in three lists at once for a run of entries -- the
entry asked for plus every category-1 entry after it -- and refuses when the
run would take the total past H'3C. `H'228A92` adds one entry: the queue is
full at H'3E8 and says so with message H'20, and below that the records
above the cursor are shifted up by one with the ROM's memmove, because
source and destination overlap by exactly one record.

Ninety-one mutations over the nineteen, all ninety-one killed. Six needed
the fills sharpening first, and one of those is worth naming because it is
the trap in its purest form: a wide `11BBA0:200` zero landed *after* the
byte values it was meant to precede, so the whole record area was zero --
and moving thirteen zero bytes onto thirteen zero bytes changes nothing, so
the length of the move could not be told from any other length.

**3,605 cases pass over 553 routines.** app.bin is 116,896 bytes.

### `H'21FF3C` measured properly

The last table-blocked routine was mis-measured in part 13 and the figure
was repeated twice: "2,340 bytes with two key-searched tables". That came
from scanning to H'220860, where the second key search ends. It does not end
there -- it goes on to H'221B6E.

The true figure is **7,218 bytes and six computed jumps**, with a dependency
tree of eighteen routines and 1,216 instructions beneath it. That tree is
closed and none of it holds a table of its own, so it is tractable; but it
is a job of the same size as the whole panel-strip chain in part 13, not the
afternoon the earlier number implied.

### The last four of the tree, and a fill that reached too far

`H'229368`, `H'229468`, `H'228B80` and `H'222F00` finished the eighteen. All
four passed first time and then survived their first mutations, which is
always the same story: the cases were vacuous and the fills said so.

Three traps, all the same trap. A `11BBAA:3300` zero -- the queue, thirteen
thousand bytes of it -- reaches to `H'11EEAA`, which is ten entries into the
range table at `H'11EE80`, and it was landing *after* the entries had been
written. A pair of `11A1D0:2` and `11A1D2:2` zeros at the end of the
queue-delete fills wiped the first and last positions the cases were built
around, which is why "at the first" and "before the first" had identical
step counts and neither could tell `<=` from `<`. And the catalogue pointer
`H'114DD2` was being seeded to the same address as the hit-box table, so a
routine reading a pattern's parameters was reading box coordinates.

The rule this batch settles: **sort every fill so that no key covers a range
a later key writes into.** The builder now puts every key longer than two
bytes at the front, in its original order, and the narrow writes after.

`queue_delete_entry` also has a branch that cannot be reached with a
well-formed list -- emptying the queue needs a one-entry list deleted at
position two, and that wraps the length of the move. The case was dropped
rather than fabricated.

Fifty-one mutations over the four; all fifty-one killed.

### The soft-float five

The dependency scan under the last four routines listed five more that are
not in `app.c`: `H'20046C`, `H'2005DA`, `H'2005E6`, `H'2006C8` and
`H'20070C`. They are the H8 single-precision library the original's compiler
emitted -- add, subtract, float-to-int, int-to-float. The reconstruction
writes `float` and lets GCC link its own; `speed_scale` and
`sew_speed_limit_scale` already compare clean through them. Nothing to
reconstruct.

### The working record, both ways

`H'200EC0` and `H'2010EC` move one pattern's nine settings between the
ten-byte record at `H'57C6D6` and the sixteen-byte working copy at
`H'0E4010`, and they do not move them in order:

    record  +1 +2 +3 +4 +5 +6 +7 +8 +9
    work    +1 +2 +7 +3 +8 +4 +5 +6 +9

The load also republishes four of them into the panel's own bytes, choosing
between record +2/+3 and +4/+5 on bit 6 of `H'11A7BD`. The save goes back a
byte at a time through the boot rom's flash writer with bit 5 of `H'114DC7`
held up, and writes 1 into byte 0, which is the flag `H'200E46` clears.
Thirteen mutations, all killed.

### `H'213164`, and a mutant that cannot be killed

A run of boxes scrolled on by one: `H'212FF0` slides the run down over its
first box, the last box moves to the next entry of its list, and the whole
run is drawn again. The wrap point is the count at the head of the *first*
box's list, read before the slide -- from the box that is about to be
overwritten.

The new entry's kind byte decides what it looks like. Nine kinds are drawn
unlit; everything else asks `H'214DD4` what the field is set to and lights
the box if the answer is positive. Getting the boundaries of that test
killed took a fill where `H'FFFEF5`, `H'FFFEF6`, `H'FFFEF8` and `H'FFFEF9`
all answer *yes* for the keys either side of each edge -- kind 9 reads
`H'FFFEF5` bit 3, kind H'0A reads bit 7, kind 4 reads the low nibble of
`H'FFFEF9`, and kind H'44 answers 1 only when `H'FFFEF5` bit 2 is down.

One edge stays open. `kind < H'0D` widened to `kind < H'0E` only moves kind
H'0D, and `H'214DD4` has no entry for key H'0D: it answers H'FFFF, the box
goes unlit, and that is exactly what the other side of the branch does. The
mutant is equivalent, not surviving.

### `H'2220F0`, and boxes that had to be handed over first

Backing out of a sub-screen: four screens know how to, the rest do nothing.
Each names the screen it returns to -- H'07 and H'45 to H'02 or H'30, H'34
and H'36 to H'35 or H'33, H'04 to H'03, H'47 to H'46 -- repaints the two
panels the sub-screen covered, and puts its strip of boxes back into their
menu states. `H'11A169` is written before `H'21F1DE` is called, so what is
put away is the screen being *returned to*.

Twenty-two mutations. Two needed the fill sharpening in ways worth naming.
`hitbox_set_state(..., 3, ...)` -- "handed back" -- does nothing to a box
already in style 3, so a case where every box is style 3 cannot tell one
range of boxes from another; the ranges that get state 3 now start in style
4. And the second rectangle of each screen is drawn in colour 0 on a frame
filled with 0, which writes nothing; the frame is now filled H'55. All
twenty-two killed.

### `H'21FF3C`

The biggest routine in the application, and the one everything else on the
panel hangs off: 7,218 bytes, 2,470 instructions, six computed jumps.

It is three dispatches deep. The screen picks a prelude out of a table of
twelve keys and six bodies, and six of the twelve screens take the press as
a *pattern* -- the boxes of the pattern strip -- and are finished with it.
Anything a prelude does not claim falls through to a common body, where a
second table of fifteen keys and ten bodies picks another range of boxes to
hit-test. That hit yields a message number, and the message number indexes a
straight table of a hundred and twenty-four entries with twenty-five
distinct targets. Eighty-one of the hundred and twenty-four do nothing at
all. Three of the twenty-five open tables of their own -- the six screen
bodies under messages H'17, H'18 and H'6D -- which is where the other three
computed jumps are.

Two words of local are threaded through all of it: the value the box
carries, which becomes the message, and the box's own index. Message H'7A
takes a message out of `H'57EFC4` and puts it through the same table, and
message H'6D can turn itself into message H'18; both are written as a loop
round the message switch rather than as a jump back into it, which is what
the original does. Every one of the sixty-two exits returns zero.

Nineteen of the twenty-five targets are one line: the message is a field of
`H'214DD4`, stepped on by the press. The rest are the queue, the picker, the
screens that lead somewhere, and the strip.

**764 cases and 136 mutations.** The fills took longer than the code.
Four things had to be got right before any of it measured anything:

* Boxes past `H'0F` must have **no list**, or the value the press yields is
  read out of a list that is not there and every message comes out zero.
  Boxes at `H'0F` and below must **have** one, because that is how the strip
  works. The cases that need a message from a low box use a hundred-and-
  twenty-eight-entry identity list instead, so a box's value is its own.
* `114DC0:14` covers `H'114DD2`, which is the catalogue pointer. Twenty
  bytes instead of eighteen and the catalogue is at `H'00005000`.
* The catalogue must not be seeded on top of the hit-box table. It was, and
  a queue record was being filled from box coordinates.
* **The two sides do not boot to the same RAM.** Each side boots its own
  image, and the images differ. Anything the routine reads that the fill
  does not pin can differ between them, and three separate "failures" --
  `H'11B0A8`, `H'11A1BA`, `H'114DE0` -- were exactly that. This is the one
  that is easy to mistake for a bug in the reconstruction.

A hundred and thirty of the 136 are killed, and the six that are not are
equivalent -- there is no case left to write for any of them:

* `s != H'30` in the H'07/H'45 prelude. The original tests it in a handler
  that only ever runs on H'07 and H'45, so it can only go one way. Dead in
  the original -- and `s != H'44` beside it is *not*, because H'45 is one of
  the two screens the handler serves, which is what says the pair is a copy
  of the H'02/H'18/H'30/H'44 handler rather than a test that means
  something here.
* `s <= H'04` widened to `s <= H'05` under message H'15. Screen H'05 is not
  in the second key table, so it never reaches the message switch.
* `t <= cap ? t : cap` narrowed to `t < cap`. The same function.
* The `second` flag of the three `hitbox_find` calls under message H'6D.
  `H'211B26` looks at that flag only on screen H'44 and only for a box
  flagged as one of a pair; each of the three bodies has just written H'07,
  H'45, H'36, H'34 or H'04 into `H'11A169`, so the flag cannot be reached
  from any of them.

### Closing the four that were open

The four `hitbox_find` bounds under message H'6D were reported as a gap in
the cases, and they were -- but not for the reason given. The run of boxes
the search walks is *not* rewritten before it: `pattern_strip_restore`
changes those boxes' style and copies them elsewhere, and `queue_reload`
rebuilds the picker, but neither touches the values the search reads. The
cases that were supposed to put the match on a boundary set **every** box's
value, including the box being pressed -- so the press stopped carrying
message H'6D and none of those cases ever reached the body at all. That is
why a sweep of forty-seven pattern numbers moved nothing: nothing was
running.

Setting only the run the search walks, and leaving the pressed box alone,
puts the match where it is wanted. Three cases per screen -- the match at
the first box, at the last, and nowhere -- and all four bounds fall, along
with the two on the H'33/H'35 body beside them. The "nowhere" case is worth
having on its own: it is the only one that takes the H'6D body's other
branch, and it runs 210,000 steps shorter, which is what says the branch is
being taken.

**4,457 cases pass over 572 routines.** app.bin is 124,728 bytes.

## 14. One file into seventeen

`app.c` had reached 22,681 lines. It is now `app.h` and seventeen sources,
split by what a routine is *for*:

| file | lines | what is in it |
|---|---:|---|
| `app_boot.c` | 807 | the boot ROM calls, the I2C bus, entry, the display's bring-up |
| `app_flash.c` | 539 | the item index, the configuration block, writing the settings |
| `app_motor.c` | 1101 | the ports, the timers, the motors, the foot control, the keys |
| `app_stitch.c` | 808 | the stitch database, and making a pattern current |
| `app_queue.c` | 1848 | the queue, stepping through it, stopping in the right place |
| `app_sew.c` | 1270 | a stitch turned into motor positions, interpolated, driven |
| `app_screen.c` | 1282 | the display subsystem, bitmaps, screens, the dialog, the picker |
| `app_service.c` | 1187 | bringing the machine up, the diagnostics screen, service mode |
| `app_isr.c` | 1543 | the two main loops, the timebase, the five interrupt handlers |
| `app_hitbox.c` | 913 | the item preview, the two bars, the hit-box table |
| `app_module.c` | 1199 | the module's panel: its slate, its labels, its numbers |
| `app_modmath.c` | 1794 | the module's floating point, and the geometry it works out |
| `app_body.c` | 1638 | the screen bodies' helpers |
| `app_press.c` | 1105 | the module's state machines, the hit test, what a press does |
| `app_sci.c` | 1716 | SCI0: the module's link and its three interrupts |
| `app_panel.c` | 2053 | the screens, the hoop, the panel's fields, switches and strip |
| `app_queuerec.c` | 1341 | the queue's own list and records, its ranges, and a press |

`app.h` is the 200 addresses and constants they share, with the comments
that explain them, and then a block of declarations per file -- 658 of them,
every routine one file offers another. A routine only its own file calls
stays `static` there and is not named in the header.

The split was done mechanically rather than by hand: a script parses the old
file into top-level items -- comment, `#define`, declaration, definition --
assigns each to a file by line range, and emits the seventeen sources plus
the header, keeping the original blank-line spacing so that a doc comment
still sits against the routine it documents. The check that it lost nothing
is a token-level diff of the old file against the new ones: 138,137 tokens
in, and the only thing missing on the other side is the word `static`,
thirteen times.

Those thirteen are eleven routines that turned out to have callers in more
than one file -- `stitch_record`, `stitch_work`, `service_tick`,
`service_hook`, `build_tables`, `buffer_fill`, `splash_and_config`,
`display_init_223010`, `module_link_quiet`, `f2u` and `u2f` -- plus the
`STUB` macro, whose two stand-ins are called from two files each.

### The trap: `--gc-sections`

One object file became eighteen, and that changes what `--gc-sections` can
do. Without `-ffunction-sections` the linker collects whole `.text` sections,
one per object; with everything in a single `app.o` there was nothing it
could drop, because `_start` reached into that one section. Split up, an
object whose routines are all reached from the *original's* jump tables --
never from anything the linker can see -- is unreferenced, and goes.

Measured rather than assumed. Without `KEEP`:

    without KEEP:  115,328 bytes,  42 of 572 case symbols missing
    with KEEP:     124,448 bytes,   0 of 572 case symbols missing

So `app.ld` now `KEEP`s the application's own objects by name and leaves
`--gc-sections` to do what it was there for in the first place, which is to
drop the parts of libgcc the application does not use.

### What it cost

Nothing, and 280 bytes. The image went from 124,728 bytes to 124,448 --
smaller, because the eleven routines that stopped being `static` stopped
being inlined into several callers apiece. **All 4,457 cases still pass over
all 572 routines**, which is the only check that matters: the harness
resolves every case through `app.sym`, so a routine that had been dropped,
renamed or quietly changed would show up as a failure rather than as a
smaller binary.

## 15. Why the merged image showed nothing, and the road back

The rebuilt image, spliced into the dump with `mergeapp`, cleared the screen
and then left it blank. Two separate things were wrong, and neither was the
file split -- building the pre-split `app.c` from `HEAD~1` and merging it the
same way gives the identical blank screen.

### The half of the C runtime that was missing

`cold_start` (H'200220) reproduced every one of the original's zero fills and
none of its copies. The original ends with six copy calls, and one of them
does real work: H'24B2D0 to H'250758, H'5488 bytes, into H'114DC2. That is
the initialised data of the original's own build, and the reconstruction
reads the same RAM by absolute address, so it needs the same bytes in the
same places.

The arithmetic says so plainly. The fill above the copy stops at H'114DC2;
the copy is H'5488 long; H'114DC2 + H'5488 is H'11A24A, which is exactly
where the next fill -- the bss -- begins.

In the simulator the omission looked harmless, because the memory dump the
image is spliced into already holds post-boot values. But they are the
*dump's* values, taken with screen H'06 showing, and the rebuilt machine
therefore sat on screen H'06 while the original sat on H'02. On a real
machine, with power-up rubbish in RAM, it would have been running on nothing
at all.

    original                screen H'11A169 = H'02   H'114DD2 = H'500000
    rebuilt, before         screen H'11A169 = H'06   H'114DD2 = H'500000
    rebuilt, with the copy  screen H'11A169 = H'02   H'114DD2 = H'500000

`cold_start` now has `copy16` and `copy32` and the original's whole list,
four of whose six calls copy nothing -- kept, as the no-op fills already
were. Its comparison case compares 286,373 changed bytes.

### The screen dispatcher

The other half is simpler and larger: `screen_dispatch` (H'22382A) is a
`STUB()`, and it has been one since `app.c`'s first commit. Stubbing that one
routine out of the *original* image reproduces the symptom exactly --

    original, untouched            pixel levels {0: 58376, 2: 14784, 3: 3640}
    original, H'22382A stubbed     pixel levels {0: 76800}
    the rebuilt image              pixel levels {0: 76800}

-- and everything else about the rebuilt machine is healthy: twenty-five
million steps without halting, never once leaving the rebuilt region, both
frame buffers cleared exactly as the original clears them, and its main loop
turning eleven thousand times.

H'22382A itself is a shell: four tests, a call to H'222AAC, then a jump
through a 79-entry table into 71 screen bodies between H'2239EC and H'228202
-- 5,075 instructions, none of them reconstructed. Writing the shell alone
changes nothing on the screen.

Nor can the shell simply call the original still sitting above the rebuilt
image. Those bodies reach fifty routines at addresses *below* the rebuilt
top, which now hold different code; and the rebuilt routines take their
arguments the way GCC passes them while the original's callers pass the first
in ER6 and the rest on the stack. The rebuilt application is a closed system,
and it runs as far as its stubs and no further.

### H'222AAC and everything under it

The prologue the dispatcher calls before it dispatches: the screen the panel
is asking for, when it is one of the eighteen H'70 to H'81. Fifteen routines,
about 1,600 instructions, written bottom-up:

| | |
|---|---|
| `queue_get_bit6` .. `queue_get_high4` | the six record readers that pair with the `queue_put_*` writers |
| `pattern_category_12_to_19`, `pattern_is_16` | two questions about a pattern's category |
| `foot_demand_hold`, `foot_demand_restore` | the presser-foot demand put aside and given back |
| `pattern_params_publish` | four catalogue parameters published to the panel and the motors |
| `screen_state_park` | the pattern parked for the screens that carry one, or asked back |
| `picker_preview` | the big picture of one queue position, or the box painted out |
| `queue_panel_draw` | the six controls of the queue's editing panel |
| `queue_entry_reset` | the entry under the cursor put back to its plain settings |
| `pattern_reset_current` | that, or the pattern itself, depending on what is showing |
| `screen_request` | H'222AAC itself: eighteen keys, twelve bodies, two of which run into the next |

**181 cases and 106 mutations, every one of them killed.** Two of the bodies
fall through into the following one when the configuration block names
neither machine, which is in the original and is reproduced.

### Three fills that were lying, again

* **The catalogue on top of the hit-box table.** A case seeded H'114DD2 to
  the same address as the boxes, so a queue record was being filled out of
  box coordinates. It passed, because both sides read the same rubbish.
* **A thumbnail pointer of zero.** With the catalogue zeroed, `picker_thumb`
  hands back a null picture and its header comes out of the reset vector: a
  height of 1,024. Both sides then draw far outside the frame buffer, and
  they do it *differently* -- the rebuild wrote into H'050000 and H'530000
  where the original stayed inside. Giving the record a real picture made six
  cases agree. What the two do with a null picture is not something worth
  reconstructing.
* **The fourth argument.** `picker_preview` takes four, and GCC passes only
  three in registers. The case gave the rebuilt side `er3` and the routine
  read the stack, so the redraw never happened and the diff looked like a
  missing call.

**4,652 cases pass over 590 routines.** app.bin is 127,460 bytes.

## 16. The dispatcher, and the first screen bodies

`H'22382A` is the routine the main loop calls once a pass, and it had been a
stub since the beginning. It does four things that are the same whichever
screen the machine is on -- blink the cursor, take up a screen change
something parked, give a held message its time, read the panel -- and then
the screen number picks one of seventy-one bodies out of a table of
seventy-nine at `H'2238B0`.

Five routines went in together, in dependency order:

* `H'211A02`, `message_hold_done`. Whether the message being held has been up
  long enough: `H'11A166` is the length in the ticks `H'114DE0` counts, and
  when the time is up the box the message lit goes back to plain and, if the
  screen was covered, all three buffers are wiped.
* `H'212B5E`, `menu_list_fill`. The menu at `H'11A88E` put into a run of
  boxes. `H'211C38` does the same for any list; this one is hard-wired to the
  menu and leaves out the state changes that one makes.
* `H'212C60`, `menu_repick`. The strip filled again after a menu key has
  asked for a different category. The two sewing screens fill the same run;
  `H'07` also has the queue's second run beside it.
* `H'223A50`, `screen_body_02` -- screens `H'02` and `H'07`. Three parts: a
  full lay-out when `H'11B0A8` says the screen was just arrived at, a panel
  lay-out when `H'11B0A9` asks for one, and then the part that runs every
  pass and keeps the bars, the strip and the arrows up to date.
* `H'22382A`, `screen_dispatch` itself, with a `switch` over the screen
  number. Sixty-nine of the seventy-one bodies are not written yet and fall
  through to `default`, which is not what the original does -- it jumps to a
  body for every screen it knows -- and is noted in the code as such. Each
  body added takes one more screen out of it.

### The body cannot be called on its own

`H'223A50` does not end in `RTS`. It branches to `H'2281F6`, which is the
dispatcher's tail: it writes `H'11A17E` and then pops the five registers
`H'22382A`'s prologue pushed. Called as a subroutine it therefore returns to
whatever eighteen bytes happen to be above the stack pointer, which in the
comparison harness is nothing at all -- the original side simply wandered off
and wrote nothing, and every case read as "the rebuild drew and the original
did not."

So the body is compared through the dispatcher, which is how the machine
reaches it anyway. Thirty-seven cases cover the five routines between them.

### A trace tool, because the diff stopped being enough

Up to here a failing case could be read straight off the list of differing
addresses. A routine that calls fifteen others cannot: the diff says the
picture came out wrong, not which of the fifteen did it.

`tool/trace_case.dart` runs a case on both images and records the calls each
side makes, at whatever depth is asked for, with the arguments each was
given and a checksum of a watched range taken at every call. Three failures
came straight out of it and would have been hard to find any other way:

* **A routine named for its neighbour.** `screen_body_02` called
  `hitbox_fill_boxed_from_list` three times where the original calls
  `H'211C38`. The two are next to each other in the source and the boxed one
  is `H'211E9C`; the trace lined the calls up and the wrong one was obvious.
* **A real bug in `hitbox_fill_from_list`.** With the right routine called,
  the first fill still diverged. `H'211CD8` branches to `H'211D54` when the
  slot is empty *and* `H'211CDE` branches to the same place when the run has
  gone past the end of the list -- both grey the box. The reconstruction had
  the second one falling through instead, with a comment saying the box was
  left alone. Nine cases had passed over it because none of them had a run
  longer than its list. Fixed, and the fix is what the four "menu_list_fill
  past the end" cases now pin.
* **An origin made of a background pointer.** The four longwords the body
  copies out of `H'116A1A` land on `H'11B0AE`, and the middle of that block
  is the screen origin every box is drawn relative to -- while the last
  longword is the hit-box table pointer itself. Seeded with a picture
  address, the origin came out as `H'000E,H'9800` and every box was drawn
  eight hundred rows down the screen, at a different place on each side.

### What the cases could not pin

Mutation testing caught thirty-seven of forty-one deliberate changes. The
four that survive are worth naming rather than papering over:

* `REG8(H'11A169) <= H'4E` widened to `<= H'4F`. While sixty-nine bodies are
  `default:` this changes nothing; it will start to matter as they are
  written.
* The *fresh* `bar_length` reading `H'FFFEE7` instead of `H'FFFEE4`. The pass
  that follows calls it again without `fresh`, and that call redraws the
  difference away: the final picture is the same either way.
* The *fresh* `panel_strip_draw` flag. Same shape -- the tail's call rebuilds
  what the fresh one set.
* `picker_cursor(3)` in place of `picker_cursor(4)`. Both settle into "the
  cursor is already put away" unless the picker is actually running, which
  screen `H'02` never sets up. Making the cursor run means seeding the
  picker's scroll position, and a wrong seed sends `picker_goto` into a
  search that does not end.

All four are confirmed by reading the disassembly instead.

### The second body, and what it cost

`H'223CCA` -- screens `H'03` and `H'04`, the menu and the menu with the
queue's strip beside it -- is the same three-part shape as `H'223A50` with
different furniture: the background block comes from `H'116FD0`, the strip is
the twenty boxes `H'01`--`H'14` filled from the *second* menu list at
`REG32(H'11B096)` rather than fifteen from the first, the panel is `H'19`
--`H'1E`, the arrows are `H'17`/`H'18` and `H'23`/`H'24`, and the queue's
strip is restored with `H'04` rather than `H'07`. The one thing it does not
do is take up a waiting menu key: `H'223A50` ends by calling `H'212C60` when
`H'11A170` is up, and this screen *is* the menu, so there is no strip to
re-pick.

It went in at one sitting: eight cases, all passing first time, and
thirty-one of thirty-two mutations caught. That is the whole point of the
work above -- the calls a screen body makes were already reconstructed and
pinned, so the second body was a transcription rather than an investigation.
The one mutation that survives is the last box of the panel run
(`H'19`--`H'1E` narrowed to `H'19`--`H'1D`), which the `panel_strip_draw`
immediately after it redraws; the same shape as the `H'16`--`H'19` case on
screen `H'02`.

### The third: the queue, and six routines under it

`H'223F2A` is screens `H'30`, `H'44` and `H'45` -- the queue shown, the queue
being edited, and the queue with the pattern strip beside it. It is the same
three-part shape again over the queue's own list at `H'11B212`, and `H'44` is
the one that differs: it starts the strip at the page the walk is on, pushes
the screen it came from, puts up three keys of its own, has no pattern strip,
and hands its press to `H'22301A`.

Unlike the first two, this body was not a transcription: six routines under
it were still missing, and they are the machinery for editing the queue.

* `H'2107D4`, `queue_run_extra` -- what the runs at a range of positions cost
  beyond their own places.
* `H'210A22`, `item_descriptor_copy` -- one pattern's `H'18`-byte descriptor
  written over another's, through the boot ROM's flash writer.
* `H'200D44`, `item_records_copy` -- the same for a pattern's three side
  records: four bytes at `H'57B6D6`, ten at `H'57C6D6` and sixteen at
  `H'0E4010`. The first two are flash and go through the writer; the third is
  RAM and is copied straight.
* `H'2109AE`, `queue_items_renumber` -- both of those run down the display
  list, so that the patterns the queue names end up in the slots it gives
  them.
* `H'210B58`, `queue_entry_delete` -- one position out of the queue, which
  moves three lists at once: the strip's, the patterns behind it, and the
  slots they are written to.
* `H'22301A`, `queue_edit_press` -- the editing screen's press: the strip
  first, and the three keys if the press was not there.

**Two traps, both about lists that do not agree with each other.**
`queue_entry_delete` indexes the pattern lists by the position plus what the
runs before it cost. A case whose runs added up to more than the lists were
long made `list_delete` compute `count - at` as a *word*, underflow, and copy
sixty-four kilobytes -- landing in memory the two images do not share. And
`H'223010`, which the done key calls, rebuilds both item lists from the
table's terminator; with no entry of category two in the fill,
`first_index_of_category` answers `H'FFFF` and the length is read from
`H'18` * `H'FFFF` past the table. Both showed up as enormous diffs a long way
from anything the routine names.

The trace tool learned something here too. It had been following call depth
by counting `RTS` instructions, and several routines on this path leave
through a jump instead; the depth went wrong and the trace claimed a call
that plainly is in the compiled code had not happened. It now follows the
stack pointer instead -- a frame is finished when the stack comes back above
where the call left it -- which cannot be fooled that way.

Twelve cases for the body and fourteen for the press handler, ninety-three
in all with the routines under them. Of eighty-eight mutations, eighty-two
were caught. The six that survive are the shapes already named above --
the last box of a run that something redraws afterwards, and a search whose
answer the shared fill's wiped boxes cannot distinguish.

### The rest of the table, as far as it goes on its own

After the first three, most of the remaining bodies turned out to be short
and highly stereotyped. Four shapes cover nearly all of them:

* **block, unpack, copy** -- four longwords out of a block into H'11B0AE,
  the picture unpacked into the scratch buffer and the rectangle the block
  names copied into the front one, then a press handler. The block's last
  longword *is* the hit-box table pointer, which is why the copy has to
  happen before anything reads a box.
* **the same with a bitmap** -- a picture that is already a bitmap is
  blitted rather than unpacked, and the back buffer is blacked out over the
  same rectangle rather than wiped whole.
* **no block at all** -- both buffers wiped and one constant picture put
  straight into the front one. Screens H'1A and H'1B.
* **not a screen** -- H'0B reads two bits of H'FFFEFA and switches to H'0C
  or H'0D; H'40's table entry *is* the dispatcher's tail.

Thirty-three more bodies went in this way, covering forty-seven of the
seventy-nine screens. Two of them needed something beyond a `break` in the
switch: H'49 ends through the tail that remembers where the machine is or
the one that does not, depending on the module's bit, so its body answers
which; and H'40 needed an explicit empty case so that the `default` no
longer claims it.

**What the cases needed.** Three things bit repeatedly, and all three are
about the shared fill rather than the code:

* The fill wipes the hit-box table part-way through, so the first four boxes
  are blank. Once "put every box back" has given a blank box style three,
  drawing it takes the inset two pixels outside a zero-sized rectangle and
  off the buffer -- at a different address on each image. These screens got
  a table of their own at H'0E5000 with real boxes in it.
* The panel strip's lists hold real pattern numbers, so a descriptor is read
  a long way past what the narrow pins cover. The catalogue is now zeroed
  wholesale rather than record by record.
* A preview whose category scales a picture into the panel puts a null
  pointer's H'400-row header outside the buffer. The item the fill points at
  is one whose category draws at fixed coordinates.

### Starting on the leaves

What was left after that was not more bodies but the routines underneath
them, so the next pass went at the cheapest of those -- the ones that unlock
a whole screen on their own.

* `H'20076C`, `mem_set_long` -- the byte fill again, entered with a longword
  count. The original has two entry points into one loop: the word one
  widens its count and falls into this.
* `H'21935E`, `needle_choice_screen` -- screen H'0C's press. Nine boxes;
  H'83 to H'87 are the five needle positions, each blitting its own picture
  into the same rectangle and writing its number into the bottom three bits
  of `H'FFFEFA`.
* `H'21C592`, `screen_slot_two_screen` -- screen H'4D's press: two boxes,
  both of which pop the screen stack, and then whatever `H'11B11A` names.
  Three arms in the original and two of them identical: only H'41 goes
  without the drawing being reset first.

That gave screens H'0C, H'4B and H'4D. `H'24AB2A` turned out to be already
written -- it is the `strcmp` in app_flash.c, which had no address comment
on it, so the reachability tool had been reporting it missing all along; it
has one now.

**A caution about that tool.** `_reach` counts an address as done when the
string appears anywhere in the sources, so a routine named only in a comment
reads as written. It also missed `H'21D88A` and the three routines under it
outright. Anything it reports should be checked against a grep for the
definition, not the mention.

### The H'21D88A cluster: screen H'3F

Screen H'3F is where the length and the width of one stitch are trimmed. Its
cluster is four routines:

* `H'228EBE` and `H'228EFA`, `queue_get_byte5` and `queue_get_byte6` -- the
  two getters beside the putters that were already written.
* `H'21D55C`, `offset_number_draw` -- one of the two numbers drawn, in one of
  two places. Each of the three sets of boxes has a left-hand place and a
  right-hand one and the sign says which: a negative number goes on the left
  with the right-hand box cleared, a positive one the other way round. The
  original writes all six pairs out longhand and only the coordinates differ,
  so they are a table here.
* `H'21D88A`, `stitch_size_screen` -- the press, called once with H'01 on the
  way in and once with H'00 every pass. The first call takes the two numbers
  from wherever they live -- bytes 5 and 6 of the queue record when H'11A175
  says the queue is being edited, H'FFFEFB and H'FFFEFC otherwise -- and
  keeps a second copy so that the cancel key has something to put back.
  H'11B0AC says whether the pattern is a group: a group has both numbers and
  seven boxes, one number and five otherwise.

And then the body, `H'2279F4`, which decides the group question and picks
one of two blocks and two picture sources from it.

**`char` is unsigned here.** The four arrow keys each stop at a limit, and
the limits are negative: `CMP.B #H'CF` with `BLT` is a signed test. Writing
that as `(char)v >= (char)0xCF` compiles to nothing on this target -- GCC for
the H8 makes plain `char` unsigned, so the comparison is always true and the
compiler says so. `signed char` is what the rest of the file uses and what
this needed.

**Three fill-ordering bugs, all the same shape.** A key the fill does not
already have is appended at the end, so a wide zero a case adds lands *after*
its own narrow pins and wipes them. The catalogue zero these cases add did
exactly that: the picture every block points at, and every item descriptor,
were being zeroed again after being set. An unpacked picture of no runs
writes nothing at all, which is why a wrong `image_load` destination had been
looking the same as the right one. The generator now moves that one wide key
back to where it belongs, and the surviving mutations dropped from five to
one.

### The H'218FCE cluster: screen H'0A

Screen H'0A is the stitch length, with a wedge beside the number that grows
and shrinks as the length changes. Four routines:

* `H'20669A` and `H'2066CC`, `stitch_length_shown` and
  `stitch_length_choose` -- the round trip between H'11A7AC, which holds the
  length in fifteenths offset by a block byte, and the whole units the screen
  works in. The reachability tool did not name `H'2066CC` at all; it turned
  up only in the disassembly of the routine that calls it.
* `H'218FCE`, `stitch_length_screen` -- the press and the drawing. The first
  call puts up the limit, works out how far up the wedge that is, and
  remembers it; each pass draws the length and steps the wedge to match, one
  row at a time, erasing on the way up and drawing on the way down. Each step
  is two lines: one along the top of the wedge and one down its right-hand
  side.
* `H'224846`, `screen_body_0A` -- the plain block-and-picture shape around it.

**The soft-float library, named at last.** This is the first reconstruction
that had to work out which routine is which: `H'200700` is int-to-float,
`H'20070C` its unsigned twin, `H'20046C` multiply, `H'200608` add, `H'200530`
divide and `H'2006C8` float-to-int. The way to tell was to take a routine
whose C was already written -- `bar_width`, which computes `v * 1.01f + 0.5f`
-- and read which helper the original calls with `H'3F8147AE` in ER5. The
trace tool now carries that map, because without it the two sides' call
sequences differ by name at the first float and the diff is useless.

**A day lost to a missing glyph.** The number is drawn with `"mm"` appended,
and the font these cases build -- borrowed from the `text_draw` cases -- has
digits and nothing else. `text_draw` took the null glyph pointer, read a
header from address zero claiming a thousand rows, and wrote a long way
outside the frame buffer; the two images have different rubbish out there, so
the case failed with two thousand differences a long way from anything the
routine names. Everything about the call was right. The fix was to make the
unit an empty string in the fill.

That failure looked exactly like a wrong argument, and the trace tool said
the divergence appeared during `text_draw` -- which was true and unhelpful.
What settled it was checking the *destination*: the differing bytes were at
the top-right corner of the screen and the text box is in the middle, so
whatever was writing them was not drawing text in its box.

### The H'230E2C cluster: screen H'21, and one of screen H'2E's

Screen H'21 is the module's version, and its cluster is the smallest yet:

* `H'248EEE`, `module_version_text_draw` -- twenty bytes: the version text
  drawn from the block at H'104C90 that the link fills in.
* `H'230E2C`, `module_version_press` -- sixty-six: the first call draws the
  text, and after that the only thing the screen answers to is the panel
  asking for H'77.
* `H'22643A`, `screen_body_21` -- two wiped buffers and one picture.

Screen H'2E's cluster is two routines and only one of them is small, so its
small half went in with this one:

* `H'21DC56`, `panel_marks_match` -- two counted lists of panel keys
  compared. Every box whose key is wanted is blanked with the background,
  and every box whose key is not goes back to plain -- and, with the third
  argument set, is handed to the panel with the clear flag up, so that a
  setting the pattern has just dropped is switched off as well as unmarked.

The font trap from the last cluster came straight back: the version text was
`"1.02A"` in the fill, the test font has digits and nothing else, and the
case failed with two and a half thousand differences. Digits only, and it
passes. That is twice now, so it is worth stating plainly: **a case that
draws text must only use characters the test font has glyphs for**, or
`text_draw` walks off the buffer and the two images differ in the rubbish it
lands in.

Of fifteen mutations, thirteen were caught. One of the two survivors is
provably equivalent: `panel_marks_match` always passes `clear` set, and
`panel_bit` returns before it looks at `step`, so the step argument is dead
for every key this caller uses.

**4,990 cases pass over 659 routines.** app.bin is 146,680 bytes.
Forty-six of seventy-one screen bodies are written, covering fifty-three of
the seventy-nine screens.

## 17. Screen H'2E, the panel strip

`H'21E082` is the biggest single screen routine written so far -- 2,526
bytes -- and it is the whole of screen H'2E: both the lay-out and the press,
in one routine the body calls twice.

The screen edits one of seven *strips of marks*. A strip is a counted list
of panel keys living in flash at `H'57EED6` and every H'22 bytes after it,
and `H'11A196` says which of the seven the machine is on. Boxes H'01 to
H'10 are the keys on offer, boxes H'16 to H'25 the strip as it stands, and
the four between them are the field the panel shows, move-across, take-out,
accept and cancel.

Four routines went in:

* `H'21EEC8`, `list_position` -- where a value is in a counted list, counting
  from one, or zero when it is not there. Nine instructions.
* `H'21EA60` and `H'21EC80`, `panel_strips_add_45` and `panel_strips_drop_45`
  -- key H'45 put into, or taken out of, all seven strips at once. The
  original writes the seven out one after another, unrolled, with the flash
  held busy for the whole run; a loop over the seven addresses does the same
  thing and the comparison does not care which.
* `H'21E082`, `panel_marks_screen` -- the screen itself.
* `H'22711E`, `screen_body_2E` -- the ordinary block-and-picture shape, with
  `panel_marks_screen` called once on the way in and once every pass.

Three things about it were worth the reading:

**The jump table dispatches on the box, not on the key.** `touch_hit` fills
in two locals, the value the box carries and the box's own index, and the
table at `H'21E3AA` is indexed by the second. Thirty-seven entries, seven
distinct bodies -- sixteen of the entries share one and sixteen more share
another, which is what makes the two runs of boxes runs at all.

**Key H'45 is not a mark of its own.** Moving it across or taking it out
does not touch the strip: it sets one of two flags at `H'11B3BC` and
`H'11B3BD`, and only when the screen is *accepted* are the seven strips
rewritten. Each flag cancels the other, so putting it in and taking it out
again in one visit leaves flash alone.

**`H'11B36C` is the working copy, read from a different end.** Case H'13
reads `REG16(0x11B36C + 2*box)` with the box in H'16..H'25 -- and
`0x11B396 - 0x2A == 0x11B36C`, so that is entry `box - H'15` of the copy the
lay-out took. Two names for the same bytes.

Forty-five mutations, thirty-five caught on the first pass. Chasing the ten
survivors was the useful half of the work, and every one of them was a fill
that was too tidy rather than a case that was missing:

* Three strips of only three marks meant the last two bytes of every H'22
  byte copy and every flash write were zero on both sides, so a length of
  H'20 and a length of H'22 could not be told apart. Fifteen marks -- as
  many as H'22 bytes hold once one more has been put in front -- and both
  mutations die.
* The lists of keys on offer held nothing the panel actually switches, so
  `panel_marks_match` with its third argument set did the same as without
  it. Putting H'02, H'03 and H'05 in the lists -- keys that are really
  fields, and that are in no strip -- fixed it.
* A search over boxes H'01 to H'10 could not be told from one over H'01 to
  H'16 while no key in the copy matched a box in between. H'13 in the copy,
  which only the boxes past the run carry, tells them apart.
* Boxes all in the same state meant "put the lit box back to what it was"
  and "put it back to nothing" were the same thing. One box given a state
  of its own settles it.

Two survivors are equivalent and will stay that way. `(u16)(index - 1) >
H'24` cannot be told from `> H'25`, because `touch_hit` was asked for boxes
H'01 to H'25 and cannot answer outside them -- the guard is the original's
own belt and braces. And in `(a >= b) ? a : b` the `>=` cannot be told from
`>`, because the two branches agree when the two are equal.

**5,049 cases pass over 663 routines.** app.bin is 149,128 bytes.
Forty-seven of seventy-one screen bodies are written, covering fifty-four of
the seventy-nine screens.

## 18. The other kind of picture: LZW

`H'20F4DC` had been sitting on the list as "a bit-aligned blitter, 906
bytes" for a fortnight. It is not one. Read next to `bitmap_draw`
(`H'20F192`) it is the *same routine*, instruction shape for instruction
shape -- the same full-width fast path, the same lead/middle/trail split of
each row, the same three cases of the two end shifts -- with one thing
changed: where `bitmap_draw` pulls its next pixel out of a run length,
`H'20F4DC` pulls it out of a decompression buffer.

`H'20F866` is what fills that buffer, and it is LZW.

* The dictionary is H'101 entries of four bytes at `H'0FF710` -- a prefix
  code and one character each -- and the H'100 bytes directly above it,
  `H'0FFB14` to `H'0FFC13`, are where a step leaves what it decoded.
* The alphabet is three characters, the pixel values H'00, H'02 and H'03,
  which are codes H'00, H'01 and H'02. Value H'01 never appears.
* A string is walked prefix-first and so comes out backwards, which is why
  it is written down from the top of the buffer and a step reports where it
  *starts* rather than how long it is.
* Three words carry between steps: `H'11A858` the position in the stream,
  `H'11A85A` the next code to hand out, `H'11A85C` the code the step before
  ended on.

Two details are worth writing down.

**The buffer is exactly the right size, and not a byte more.** The next code
runs 3, 4, ... up to H'100 and then starts again at 3, so the longest string
the dictionary can hold is 254 characters; the "the code is the entry this
step is about to make" case adds one more; and `H'0FFC13` less 255 is
`H'0FFB14`, the first byte of the buffer and the byte after the last
dictionary entry. Nothing is checked at run time -- the sizes are simply
chosen to meet.

**`mid` is a flag, not a count.** Both blitters work out how many whole
bytes are in a row and then never use the number: the row loop runs from one
address to another, and the count is only ever tested against zero. That
makes `mid + 1` and `mid + 2` the same program, which is one of the two
mutations below that cannot be killed.

Two screen bodies came with it:

* `H'224540`, `screen_body_08` -- remembers where it came from, decodes its
  picture into the scratch buffer, blacks out the same rectangle in the back
  one and copies it forward.
* `H'2251F0`, `screen_body_3A` -- the same, with a picture that is the whole
  screen: H'0000 to H'013F across, which is the straight-run path, 76,800
  pixels, and the only place the dictionary really wraps.

### Testing a decompressor

A case needs a stream, and a stream of made-up bytes is not safe here: a
code below the next one is walked through the dictionary, and after the
dictionary has wrapped once the stale entries can point at each other in a
circle. So the harness got a model instead -- the decoder written out again
in Python, byte for byte, next to an encoder that feeds it -- and every
stream in a case is something the encoder produced and the model checked by
decoding it again. That also means a picture in a case is a picture, not
noise, and the stream for the whole screen comes to 3,470 bytes.

The same model produces the *state* a step starts from: run it forward n
steps and write out the dictionary, the three words and the buffer as the
fill. That is how a step in the middle of a long run, a step at the wrap,
and the awkward case can each be reached directly rather than by decoding a
whole picture up to them.

Forty-four mutations, forty-one caught. The three survivors:

* `mid + 1` against `mid + 2`, equivalent as above.
* `x1 - x0 == H'13F` against `== H'13E`, which lived only because a
  full-width rectangle comes out the same down the ragged path as down the
  straight one -- the two paths *agree* on the input the test picks out. A
  case one pixel narrower, where the answer differs, kills it.
* One of the two `code >= next` tests, which is an artifact of writing the
  routine as one branch where the original has two: the second test undoes
  what the first one changed. Changing both together kills nineteen cases,
  so the branch is covered; and the two halves of it -- where the walk
  starts, and the character put back on the end -- are each killed on their
  own.

**5,088 cases pass over 665 routines.** app.bin is 150,980 bytes.
Forty-nine of seventy-one screen bodies are written, covering fifty-six of
the seventy-nine screens.

## 19. What is actually left, and the first two of it

The line above -- "one cluster left, `H'24610A` plus one each" -- was wrong,
and wrong by a wide margin. It came from `tool/decompile.dart`, which stops
tracing at a `JMP @ER6`; every routine that dispatches through a jump table
was therefore measured as its first forty instructions and nothing else. Two
of the twenty-two remaining screens want a routine of six *thousand* bytes
that decompile reported as thirty-seven.

Measured properly -- a walker that follows branches, reads the jump tables
out of the image and stops at the RTS past the last of them, in
`/tmp/extent.py` -- **the twenty-two screens still to do want 185 routines
and about 92,000 bytes between them**, which is most of what is left of the
application. Sized per screen, cheapest first:

| screen | routines | bytes |
|---|---|---|
| H'43 | 10 | 2,686 |
| H'42 | 15 | 5,068 |
| H'17 | 37 | 7,306 |
| H'41 | 21 | 7,734 |
| H'18 | 43 | 9,724 |
| H'31 | 31 | 10,490 |
| H'38 | 60 | 12,878 |
| H'2D | 35 | 14,144 |
| H'2B | 47 | 15,902 |
| H'1F | 54 | 19,774 |
| H'37, H'24, H'4E, H'23, H'16, H'15, H'09, H'14, H'11, H'13, H'12 | 115-133 each | 37,000-60,000 each |

The eleven at the bottom overlap almost entirely: they are eleven doors into
one large shared body of module code, so the total is 92,000 bytes rather
than the sum of the column.

### The module's busy test

`H'24610A` is what every one of those screens asks before it does anything:
`H'114D8E` says which of twelve things the module is in the middle of, and
each of the twelve has its own list of bytes that mean "not yet". A jump
table of twelve, and the answer is one if any of the list is set.

It is not the pure question it looks like. Three of the twelve clear a byte
or two on the way past, and one -- the state that shows the module's switch
settings -- does a piece of work of its own, which brought in five more
routines:

* `H'248614`, `module_link_wake` -- the link brought up once, remembered in
  `H'114DA2`, and skipped entirely while a download is in progress.
* `H'21BF2C`, `H'21C000`, `H'21C070`, `H'21C0E0` -- four little lamps in a
  row at y H'22, one per switch bit. The first has two squares and lights
  whichever the bit picks; the other three have one each.
* `H'2486C4`, `module_switches_show` -- `H'114DA3` alternates: one visit
  turns the module's reporting on and gives it five seconds to answer, the
  next turns it off again.

Those five seconds are `link_delay(H'1388)`, which is longer than the
harness's eight-million-step limit. The case file grew a `"steps"` field for
it; the case runs to about eleven million and passes.

### Going to a pattern by its number

Screens H'46 and H'47 -- the keypad the operator types a pattern number into
-- turned out to be independent of all that, and cheap. Five routines:

* `H'212A44`, `list_holding` -- which of the three lists holds the pattern
  whose *number* (the word at offset H'14 of its descriptor, not its
  position) is the one asked for.
* `H'21A246`, `goto_pattern_number` -- that pattern made current and the
  screen for its category gone to. `H'11B108` is which page of five it sits
  on, counted from the first of its own category.
* `H'24B10A`, `str_to_long` -- the ROM's own `strtol`, and the only place
  the character-class table at `H'250783` is reached from the application.
  Everything the C library asks of it is there: leading space, an optional
  sign, base zero working the prefix out, `0x` allowed at base sixteen, and
  overflow clamped with H'22 -- ERANGE -- left in `H'11F5A6`. Two details
  are its own: the class table is indexed by the character *signed*, so
  anything above H'7F reads below the table; and `endptr` is put back to the
  start of the string not only when no digit was found but also when the
  value overflowed.
* `H'22323A`, `number_keypad_screen` -- thirteen boxes, four digits at most,
  and a leading zero refused because a zero can only be appended to
  something already there. H'19 acts on what was typed, and does something
  different on each of the two screens.
* `H'2280CA`, `screen_body_46` -- both screens.

`H'22323A` has no return value: the value in R6L at its tail is whatever the
last thing it called left there, and the caller ignores it. Written as `u8`
it failed every case on the result register alone; written as `void`, with
the cases not comparing a result, it passes. **A routine whose tail is not
reached by a common `SUB.B R6L,R6L` has no result to compare.**

### What the mutations found this time

Sixty-four mutations, forty-nine caught on the first pass. Every one of the
fifteen survivors was a fill that was too tidy, and four fixes killed twelve
of them:

* **State four is busy whenever `H'114D62` is short**, and the fill left it
  at zero -- so state four was busy no matter what else the case set, and
  half its tests were invisible. Pinning the count at H'0A in every state
  four case brought them back.
* **A byte cleared to the value it already held.** Three states clear
  `H'114D99`, `H'114D9A` and their neighbours, and the fill zeroed the whole
  block first. Pinning them to distinct non-zero bytes made the clears
  visible.
* **The shared fill sets the picker's position, and a wide zero of my own
  wiped it.** `11A1BE:40` covers `H'11A1CC` to `H'11A1D3` -- where the
  picker's position and its two ends live -- so both arrows had nothing to
  say and the screen body's last call wrote nothing at all. This is the
  fill-ordering trap from part 17 in another dress: a *new* wide key is
  appended at the end of the fill and therefore covers everything the base
  set. Narrowing the wipe to four bytes fixed it.
* **A number that was not big enough to overflow.** `9999999999` wraps to
  H'540BE3FF, which is *larger* than the accumulator before it, so the
  carry test never fired. Twenty digits does fire it.

Three survivors are equivalent. `return (u8)(busy != 0 ? 1 : 0)` against
`return busy`, where busy is only ever 0 or 1. Three rounds of bringing the
link up against two, where the rounds are idempotent and only the cycle
count differs. And `base == 1` dropped from `strtol`'s validity test: with a
base of one no digit can ever be accepted -- zeros are skipped before the
loop and everything else is at least one -- so the answer is zero and
`endptr` is the start of the string either way.

**5,291 cases pass over 676 routines.** app.bin is 154,068 bytes. Fifty of
seventy-one screen bodies are written, covering fifty-eight of the
seventy-nine screens.

## 20. Screen H'43, the queue as a strip

The cheapest of the twenty-one, and the first of them done: the queue seen
all at once, every entry's picture drawn side by side, wrapping to a new row
when the next one will not fit, with a cursor under the current entry and an
arrow at each end.

Five words at `H'11A1D4` describe the area -- left, right, top, bottom, and
the height of one row -- and three at `H'11B3CE` carry where the drawing has
got to: the x the next picture goes at, the baseline of the row it is on,
and which entry the cursor is under. Ten routines:

| | |
|---|---|
| `H'22B4B4` `queue_strip_run_draw` | a run of entries drawn along the strip |
| `H'22B592` `queue_strip_arrows` | the two arrows, redrawn only when they change |
| `H'22B698`, `H'22B7B8` | the cursor one entry on, one entry back |
| `H'22B8AE`, `H'22B94A` | the strip scrolled a row up, a row down |
| `H'22B9E8` `queue_row_back` | where a row begins, walking the widths backwards |
| `H'22BA86` `queue_row_first` | the same thing forwards |
| `H'22B3A2` `queue_strip_screen` | the screen |
| `H'227FC4` `screen_body_43` | the body |

The lay-out is worth a note: it draws the row the cursor's entry is on and
then *steps the cursor forward to it one entry at a time*, which is how the
three words at `H'11B3CE` end up holding where it really is. There is no
arithmetic for it -- the machine walks.

### Two traps in the fill, and a new tool for finding them

Fifty-seven mutations, forty-four caught. Thirteen survivors, and chasing
them turned up the same failure twice in different clothes.

**A copy onto ground the colour it is writing.** The two scroll routines
move the whole strip a row with `region_copy`, and the frame buffer in the
fill was all one value -- so moving it by the wrong number of rows put the
same byte back where it came from. Three mutations of the row arithmetic
lived on that. The fix is a pattern that *changes from one line to the
next*: one fill entry per line, its value derived from the line number.

**The catalogue is inside the scratch buffer.** This one cost hours. The
case failed with six and a half thousand differing bytes scattered over both
frame buffers and past the end of them; the original was drawing a picture
`H'FC30` rows tall and the rebuild was not.

The shared fill puts the catalogue at `H'0E9000`. `LCD_SCRATCH` is
`H'0E8010` and a buffer is `H'4B00` bytes, so the catalogue is *inside it* --
and screen H'43 unpacks a picture over the whole of that buffer before it
draws the strip. By the time the strip is drawn every descriptor is zero,
every picture pointer with it, and `header_word_0` is reading address zero:
the reset vector, which is the one place the two images are *meant* to
differ. Both sides went wild; they went wild differently.

Moving the catalogue to `H'0E7000` for these cases -- below the scratch
rather than inside it -- fixed it. The rule to carry forward: **a case for a
screen that unpacks a full-buffer picture cannot leave anything it needs
inside `H'0E8010` to `H'0ECB0F`.**

Finding it needed something the harness did not have.
`compare_routines` reports differing addresses *sorted by address*, and the
first address is hardly ever the first write; `trace_case` compares call
sequences, which stopped being alignable once the rebuild inlined
differently. So `tool/trace_writes.dart` was written: every write into a
given range, in the order it happened, with the program counter that made
it, for both sides. Two runs of it said "the original writes here from
inside `plot_pixel`, the rebuild does not", and one more with the range
widened to the catalogue said "`bitmap_draw` wrote over descriptor twelve" --
which is the whole answer.

`compare_routines` also grew a `COMPARE_SHOW_DIFFS` environment variable, so
the twelve-address cap can be lifted when the shape of a difference is the
thing worth seeing.

**5,361 cases pass over 685 routines.** app.bin is 156,288 bytes. Fifty-one
of seventy-one screen bodies are written, covering fifty-nine of the
seventy-nine screens. Twenty screens are left.

Twenty-one screens are left, and the table at the top of this part says what
each costs. The order to take them in is that table: H'43, H'42, H'17, H'41,
H'18 are five separate afternoons; after that the eleven module screens are
one long piece of work that has to be done bottom-up, and finishing any one
of them finishes most of the rest.

## 21. Screen H'42, the queue's editing panel

The panel down the right of the queue: the two bars, the eleven-box width
strip, the arrows, and eleven keys that change the entry the cursor is on.

Its lay-out is unlike the others. There is no `lcd_buffer_fill`: instead the
top `H'9F` rows from x `H'5B` across -- the panel's own rectangle -- are
blacked in the back buffer, the picture is drawn into the scratch buffer over
the same rectangle, and that rectangle alone is copied forward. The left of
the screen is left exactly as the screen underneath had it, which is the
point: the panel opens *over* the queue.

The body sets `H'11B0A9` on its way out of the arrival branch, so the second
branch runs on the same pass. Five routines were needed under it:

| | |
|---|---|
| `H'22A570` `queue_settings_track` | the three live settings watched, and the entry written when one moves |
| `H'22AA8C` `queue_edit_screen` | the panel's own press: eleven keys |
| `H'228E80` `queue_get_low6` | the low six bits of a record's fifth byte |
| `H'227DBE` `screen_body_42` | the body |

`queue_settings_track` is the one worth reading. `H'11F282` to `H'11F287`
hold what the panel last saw of the width, the first parameter and the
second; on every pass it compares them with the live bytes and, when one has
moved, writes the new value down onto the queue entry the cursor is on -- so
that turning a dial edits the entry rather than the machine.

### The catalogue, again, and a bigger table

Two of this screen's keys add the markers `H'3FE` and `H'3FF` to the
catalogue, which means the table has to be `H'400` entries -- `H'6000`
bytes. There is nowhere in RAM that fits below `H'0E8010`, and the screen
unpacks over the whole scratch buffer. The answer is to put the catalogue
where the machine really keeps it: `H'500000`, in the flash window. The rule
from part 20 stands, but the escape from it is now "move the table out of RAM
altogether" rather than "find a lower address".

## 22. Screens H'17 and H'18, the two service screens

The service menu and the service menu with the pattern strip. Both have the
ordinary block-and-picture lay-out, both fill their boxes from the same two
lists at `H'115A20` and `H'115A06`, and both draw the two bars, the width
strip, the ten service marks and the speed number with the flag up before
settling into a pass that draws the marks, the number, the strip and the two
bars again.

| | |
|---|---|
| `H'21AC9E` `service_marks_draw` | the ten marks, each drawn only when its input has moved |
| `H'216FA4` `long_to_decimal` | a longword as digits, built backwards and reversed |
| `H'21148A` `hitbox_flag` | the flag byte of a box |
| `H'20FCD6` `bar_needle` | the needle-position bar, `H'21` to `H'A1` |
| `H'21CF9C` `screen_stack_clear` | |
| `H'225C30`, `H'225DC6` | the two bodies |

The ten marks are ten unrelated readings -- two switch positions, a foot
number, the two hour counters -- each with its own remembered value at
`H'11B340` so that a pass with nothing moved draws nothing at all.

### The font trap, in its worst form

`H'119DE6` is the font the speed number is drawn from, given to `text_draw`
as a constant. A fill that does not pin it leaves a glyph pointer of nought,
whose header is read from address zero -- the reset vector, the one place the
two images differ -- and which then claims about `H'400` rows and writes far
outside the buffer.

Pinning it is not enough on its own. `H'119DE6` plus `H'3DA` reaches
`H'11A160`, which is where the screen number itself lives; a font copy that
wide wipes `H'11A169` and both screens become the same screen. `H'100` of it
is enough: the digits sit at four times their own code, well inside that.

That is the fifth time the same shape of mistake has cost an afternoon, and
it is always the same shape: **a wide zero written after the value it
covers.** `audit()` in the case generator now walks every fill and says which
narrow pin a later wide key shadows, which turns an afternoon into a line of
output.

## 23. The picker strip, and the five screens that share it

Five screens -- `H'41`, `H'31`, `H'1F`, `H'2B`, `H'2D` -- reach the same
place: a strip of fifteen numbered boxes with a page of the queue's *ranges*
in them. `H'11A1EC` is which range the first box stands for, `H'11A1EE`
which box the cursor is on, and `H'11EE80` the table of ranges themselves,
two words each: first entry and last.

| | |
|---|---|
| `H'2299A6` `picker_strip_screen` | the strip: lay-out, paging, and twenty-two boxes of press |
| `H'2298E4` `hitbox_numbers_draw` | a run of boxes given their own numbers as digits |
| `H'22A2BE` `picker_range_mark` | each box marked with whether its range is one entry or more |
| `H'22A33C` `picker_range_close` | a range closed up: everything above its first taken out |
| `H'22A400` `percent_bar_draw` | how full the machine's own store is, as a bar |
| `H'227CC6`, `H'227898`, `H'2262EE`, `H'226D6E`, `H'227020` | the five bodies |

Paging is by five and is done with the display, not with the drawing: `H'17`
and `H'18` copy the strip's rectangle up or down `H'27` rows in *both*
buffers and then draw only the five boxes that have come into view. The
cursor's box is moved five with it, and a cursor that has gone off the end is
simply not put back.

`percent_bar_draw` is the only floating-point drawing in the machine:
`45.0 - 1.5252523` as a base and `1.5252523` a step, which is `H'C4 - H'2D`
over a hundred. Nought and a hundred are drawn as whole rectangles; anything
outside is left alone.

Three of the five bodies are three lines each -- lay the background out, then
call one screen. `H'2B` is the one with something over the top: both bars,
the width strip and the item preview, and it is the only one of the five that
stows the screen it came from in store `H'03`.

### An equivalent mutant, and why it is equivalent

`screen_body_2B` draws the width bar from `H'FFFEE7` with the flag up and
then from `H'FFFEE7` again without it. Changing the *first* of those to
`H'FFFEE4` cannot be caught, and it took three attempts to see why: the
second call is not idempotent by accident but by design. Given a remembered
value `v0` and a true value `v1` it paints exactly the band between them, so
whatever the first call drew, the second restores the bar the true value
would have made. The only part of the bar the second call cannot repair is
the black above it, and the fresh call skips that only for values of `H'64`
and over -- for which the black band is empty anyway.

So the mutation is genuinely unobservable in the final state. The line is
right because the disassembly says `MOV.B @H'FFFEE7:24,R6L` before
`JSR @H'20FA18` and `MOV.B @H'FFFEE4:24,R6L` before `JSR @H'20FF7A`; the
harness simply has nothing to see.

**5,686 cases pass over 687 routines.** app.bin is 168,228 bytes.
Fifty-eight of seventy-one screen bodies are written, covering sixty-seven of
the seventy-nine screens. Twelve screens are left, and they are the module
cluster: `H'38`, `H'37`, `H'24`, `H'23`, `H'4E`, `H'16`, `H'15`, `H'14`,
`H'13`, `H'12`, `H'11`, `H'09`.


## 24. Screen H'38, the module's pattern list

The first of the twelve module screens, and the cheapest: a page of fifteen
thumbnails to pick from and a strip along the bottom holding the ones picked,
in the order they will be sewn.

Twenty-three routines under one body, and none of them had been seen before.

| | |
|---|---|
| `H'23A336` `module_thumb_row_draw` | one row of five cells of the picking grid |
| `H'23ABC2` `module_thumb_draw` | one bitmap, where it is asked for |
| `H'23A990`, `H'23AA8A` | the grid paged back and on by a row of five |
| `H'23AFEE`, `H'23B0C6` | the strip scrolled sideways, two ways of finding its left edge |
| `H'23B81C` `module_cursor_line` | the line that says where the next pattern goes |
| `H'23B408` `module_list_insert` | one pattern put into the list at the cursor |
| `H'23B4DE` `module_list_remove_draw` | one taken out, and the strip closed up behind it |
| `H'23AD2A` `module_pattern_add` | a thumbnail pressed |
| `H'23B1A2`, `H'23B2CE`, `H'23B67A` | the cursor back, on, and the entry deleted |
| `H'23B938` `module_send_step` | the list sent to the module, a step a pass |
| `H'24A21A` `module_box_clear` | a rectangle of the front buffer back to black |
| `H'249B86`, `H'249BD2`, `H'249C1E`, `H'249C6A` | the four arrows, lit and not |
| `H'231776`, `H'231794`, `H'23180C` | three ways out, each saying what the module does next |
| `H'2309EC` `module_pattern_screen` | the press |
| `H'225B8A` `screen_body_38` | the body |

### The bitmaps, and the marker row

The thumbnails are one-bit bitmaps, `H'23` by `H'23`, five bytes to a row and
`H'AF` bytes each, kept in RAM from `H'104D4A` on -- they come down the link
from the module rather than out of flash. `H'0FFE9C` plus the design's own
number says how many there are.

The first row is not picture. It is one set bit, and the column it is in is
how wide the pattern is. Finding it fixes two things for the rest of the
bitmap: the columns from there on are not drawn, and half the distance from
there to `H'22` becomes the offset every pixel is drawn at, which is what
centres a narrow pattern in its cell. Both drawing routines read it that way;
they differ by one column in where they stop, which is not symmetry but is
what the two say.

The wide drawing -- the preview at the top of the screen -- writes the width
it found into `H'104036`, and that is the number the insert carries into the
list. So the width of an entry is discovered by drawing it.

### The strip, and why the fill had to change again

The strip is a window `H'2F` to `H'EA` across and `H'9E` to `H'C1` down, and
it is scrolled *sideways*, a pixel at a time, by reading the front buffer back
with `read_pixel` and writing it with `plot_pixel`. Part 20 learned to give
the buffers a value that changes from one line to the next, because the queue
strip scrolls up and down. That is exactly the wrong fill here: a row of one
repeated byte scrolls sideways into itself and leaves nothing to compare.
Seven mutations of the scroll arithmetic lived on that until the fill was
given a byte that changes along the row as well as down it.

The same shape a third time: **make the test data vary in every direction the
routine can move it.**

### Two mutants that cannot be killed, and why

`module_pattern_add` and `module_list_delete` each ask whether the new entry
still fits in what is showing, and each mutation of `<= 0` to `< 0` survives.
Both are genuinely equivalent. The test only differs when the entry fits
*exactly*, and at that point the two arms compute the same thing: one moves
the cursor on by the width, the other puts it at the far end and works the
same position back out of it. The boundary is where the two arms meet.

`module_list_back` has a clamp -- "if what is over is more than the width,
take the width" -- that cannot be reached at all: what is over is the width
less how far along the strip the cursor is, so it is never the larger. It is
in the original and it is reproduced.

And the two scrollers are the same routine for a leftward move: they differ
only in where a rightward move stops. A mutation that swaps one for the other
in a leftward call is invisible, and correctly so.

**5,813 cases pass over 711 routines.** app.bin is 173,332 bytes. Fifty-nine
of seventy-one screen bodies are written, covering sixty-eight of the
seventy-nine screens. Eleven screens are left: `H'37`, `H'24`, `H'23`,
`H'4E`, `H'16`, `H'15`, `H'14`, `H'13`, `H'12`, `H'11` and `H'09`, and they
share about a hundred routines between them.

## 25. Screen H'37, and the furniture eleven screens share

Screen H'37 is the next cheapest of the eleven left, and its own body is four
lines. What is under it is not: sixty-two routines, twenty-five thousand
bytes, and none of them seen before. They are being taken in layers, leaves
first, and thirty-eight are done.

### The leaves

Twenty-six small routines that the module screens all reach: six labels drawn
in six fixed places, four arrows lit and not, boxes put in and out of their
states, two rectangle clears and an outline, the strip's own scroll, and the
three-line answers to "is the hoop one that can be sewn" and "is this slot
the plain one".

| | |
|---|---|
| `H'241480` `stream_clear` | the stitch stream, H'10C27A to H'1137A9 |
| `H'2172B6` and five beside it | the six fixed labels |
| `H'244CA2`, `H'244CB4`, `H'24654E` | three questions with one-line answers |
| `H'23C570` `module_colour_check` | the colour asked for against the count |
| `H'24A1EA`, `H'249FC2`, `H'249FFE`, `H'24A25A`, `H'24A29A` | five little draws |
| `H'24A336`, `H'232394`, `H'217C32` | a box repressed, and the lit one moved |
| `H'217A26`, `H'217A82`, `H'2323AA`, `H'2323F0` | boxes three, four and H'0A |
| `H'2317D0`, `H'2317EE`, `H'231544` | two ways out and the screen put back |
| `H'2352AA` `link_line_release` | one bit of H'FFFD1C put down |

### The second layer

| | |
|---|---|
| `H'230EA8` `module_area_save` | the module's rectangle put away |
| `H'2316C4` `module_go_settings` | the way out, with a message first |
| `H'2321B6` `module_label_speed` | the speed under the bar, or "off" |
| `H'23C5EA` `module_colours_show` | the colour strip and the percentage |
| `H'246654` `module_fault_report` | a message that takes the screen away |
| `H'2414AE` `module_slots_clear` | all twenty-eight slots put back |
| `H'24A03A`, `H'24A112` | the hoop's outline as four lines |
| `H'23E026` `module_colour_swatch` | the colour drawn big |
| `H'23DE8E` `module_start_step` | the four steps that start a colour |
| `H'23C2FA`, `H'23C450` | the hoop's two corners measured |

### Three traps, two of them new

**The font has ten glyphs.** The test font pinned into the fills covers the
digits and the letter `m` -- nothing else. Six of the label routines drew
strings the case generator had made up out of `A` and `b`, and a character
with no glyph reads its pointer as nought and its bitmap header from address
zero. Fixed by drawing only what the font has, and by adding `o`, `f` and `%`
where a routine draws a fixed word.

**Vary the data in every direction the routine can move it.** Part 20 gave
the buffers a value that changes from one line to the next, because the queue
strip scrolls up and down. The module's strip scrolls *sideways*, and a row
of one repeated byte scrolls into itself: seven mutations of the scroll
arithmetic survived on that until the fill was given a byte that changes
along the row as well as down it.

**A box under something drawn later cannot be seen.** The colour strip paints
fifteen boxes and then draws the percentage bar over the same part of the
buffer. With the box table's origin at the corner the second row of boxes
lands under the bar; left where the boot puts it, that row falls off the
bottom of the buffer. Either way seven of the fifteen boxes were being
painted into a place nothing could read back, and a mutation that skipped
them survived. Moving the origin down clear of the bar killed it.

### Waits only an interrupt can end

Several of these routines send a message to the module and then wait for the
link to go quiet. `link_send_start` raises the busy flag itself, so the wait
can only be ended by the link's own interrupt -- which a comparison case,
running one routine with no traffic, never gets. Those routines are covered
only along the paths that turn back before the first message; the rest is
read from the disassembly and reproduced, but not exercised. It is the same
limit the module's switch screens hit in part 19.

### Equivalent mutants worth naming

`module_colours_show` computes the percentage as `100 * count / 15` and takes
100 when the count is over fifteen. At exactly fifteen the two arms give the
same answer, so `>` and `>=` cannot be told apart. `module_label_speed`
copies six bytes out of `H'250776` to start its buffer, and those six bytes
are nought, so copying from `H'250778` instead is invisible. And the two
strip scrollers are the same routine for a leftward move.

### The layer after that

| | |
|---|---|
| `H'231450`, `H'23128C` | the hoop's numbers, as a size and as a scale |
| `H'23A7B0` `module_grid_draw` | the whole picking grid laid out |
| `H'2369C4` `module_hoop_pictures` | the three hoops the design still fits |
| `H'236BE4` `module_hoop_outline` | the hoop as a rectangle, singly or doubly drawn |
| `H'242868` `module_run_fetch` | thirteen steps that fetch a run of patterns |

`module_run_fetch` is the largest single routine in the cluster: thirteen
states, each waiting for the link and sending one message. Step eight is the
one that does not end where the others do -- it falls straight through into
step nine -- which is in the original and is reproduced.

Its cases needed a new tool in the generator. The fills are built by layering
one `extra` on another, and a wide zero added late shadows the narrow pins
underneath it; `audit()` has said so since part 22. What was missing was a way
to *fix* it rather than only be told: `drop` takes every narrow pin out of a
range before the wide key is applied, and `pin` puts the handful that have to
survive back on the very end. The first attempt at these cases dropped the
step number itself and every one of the twenty-eight ran the same step --
which the harness reported as twenty-eight passes, because both sides did the
same nothing. **A case that passes with no bytes written has not tested
anything**, and the step counts being identical across cases is the tell.

**6,005 cases pass over 752 routines.** app.bin is 180,120 bytes. Screen H'37
needs seventeen more routines; the eleven module screens together need
fifty-eight, and every one of them is now in the two-hundred-to-four-thousand
byte range -- the small ones are gone.

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
   `H'FFFEC4` bit selects. *Done, along with service mode. The screen
   dispatcher is written now too; twenty of its seventy-one screen
   bodies are not, and the embroidery module is still a stub.*
7. **The twelve interrupt handlers** — timers, and SCI0's ERI/RXI/TXI, which
   is how the embroidery module is served. *Done: all ten slots the original
   fills in.*
8. **The application's command protocol** — the counterpart of the boot
   ROM's, including the session flag at `H'57FF80` that EMB-Serial reads.
9. **The long tail** — menus, stitch generation, embroidery.

## 26. H'246EEC, the design's outline

The biggest single routine in the cluster -- 3,670 bytes -- and the only
drawing in the machine that needs a sine and a cosine of its own.

It draws the design's rectangle on top of the hoop's, turned by the angle the
slot is set to. The rectangle is the design's size out of the two tables at
`H'104CCE` and `H'104D06`, scaled by the slot's own two percentages; the four
corners are taken about the centre, turned by *minus* the angle, and put on
the screen at `H'73` across and `H'9E` down with the design's offset added. A
three-point arrow at the far corner says which way up it is, and `H'11A25F`
mirrors it.

The corners of the last one drawn are kept in the twelve words at `H'11F2E4`,
and the first thing it does with them is draw the same six lines again in
black. That is the whole of how the old outline is taken off -- nothing else
in the buffer is touched.

Turning by an angle leaves corners a pixel out of square, so before anything
is drawn each pair is compared and one snapped to the other when they differ
by exactly one. Eight comparisons, in a fixed order, each seeing whatever the
one before it changed.

`H'24A62E` came with it: the marks inside the hoop -- a dashed cross through
the middle, four more dashed lines a quarter in for hoop `H'AC`, and for hoops
`H'07` and `H'03` a dashed circle of thirty-six five-degree chords.

### Reading a routine with a hundred and fifty locals

The frame is `H'9C` bytes and every local is reached as `@(H'NN:16,ER7)` --
but `ER7` moves. A `PUSH.L` before the read shifts every displacement by four,
and this routine pushes and pops around almost every call. Reading the
offsets straight off the listing gives the wrong local about a third of the
time, which is a slow and confusing way to be wrong.

`tool/frame.py` walks the
listing tracking the stack depth through every `PUSH`, `POP`, `ADDS`, `SUBS`
and `ADD.L #imm,ER7`, and rewrites each displacement as an offset from the
frame base. `@(H'0064:16,ER7)` at one depth and `@(H'0068:16,ER7)` at another
both come out as `@L100`, and the four corner computations then read as the
same four lines with different inputs, which is what they are.

Two mutations survived and both are the original being redundant. The angle
is negated four times over and the sine and cosine each computed twice from
the same value; and the negation before the *cosine* cannot be seen at all,
because cosine is even -- which is exactly the reason `float_cos` at
`H'24ADDC` drops the sign before it starts.

## 27. H'24073E, the same sum without the drawing

`H'24073E` is 2,794 bytes and `H'246EEC` is 3,670, and the difference between
them is exactly the drawing. Everything else -- the angle, the sine and the
cosine, the scaling, the four corners, the three arrow points, the eight
snapping comparisons and the twelve words written at `H'11F2E4` -- is the same
code twice. The screens that only need to know *where* the design is call the
short one; the screen that shows it calls the long one.

So the reconstruction has it once, as `module_design_corners`, and
`module_design_outline` became the six black lines that take the old one off,
a call, the six coloured lines that put the new one on, and the size labels.
The one thing the factoring moves is *when* the trigonometry runs -- the
original works the angle out before it erases, and the reconstruction after --
and since the erase only draws and the trigonometry only reads, the memory
that comes out is the same. The thirty-four cases that already covered
`H'246EEC` were re-run to say so.

That is the first time in this reconstruction that two ROM routines have been
written as one. It is worth being careful about: the case for it here is that
the second routine is a strict prefix of the first, instruction for
instruction, and both are covered by cases of their own -- twenty-four
mutations, all killed, against the two sets together.

**6,052 cases pass over 755 routines.** app.bin is 184,288 bytes -- forty
bytes more than before, for a routine of 2,794. Screen H'37 needs fourteen
more, and it is down to 8,200 bytes.


## 28. The rest of screen H'37

Four routines finish it.

| | |
|---|---|
| `H'23D150` `module_colour_run` | seven steps, one colour forward |
| `H'23D66A` `module_colour_back` | two walks, nine steps and eleven |
| `H'23078A` `module_sew_screen` | twenty-one keys |
| `H'225AE4` `screen_body_37` | the plain lay-out, and the press |

### One call is the whole walk

The two colour walks look like the state machines everywhere else in this
cluster -- a step number in `H'1040B3`, a body for each -- but they are not
called once per step. Each step ends by servicing the host and blinking the
panel, and then, while the step number is still under its limit, *the same
call* goes round again. The routine returns only when the walk reaches its
last step or one of the steps gives up.

That changes what a case can reach. A step that sends a message raises the
busy flag, and the step after it waits for the flag to drop -- which only the
link's interrupt does. So the loop does not merely stall, it spins, and the
case runs to its step limit. What a case can run is the turns back near the
top, the steps that give up on their own, and any run that reaches the last
step without sending. For `H'23D150` that is five ways back, four steps that
walk out, and the step past the last; `H'23D66A` holds two walks that share
an opening -- `H'114DAB` picks between giving the whole run up and taking
back only the last colour -- and between them thirty-seven cases.

`H'23D66A`'s short walk unwinds the row of colour boxes one at a time with a
count of `H'7530` turns of an empty loop between one box and the next, so the
row goes out at a pace the eye can follow. It is the step no case can run:
the step after it sends.

### The counter that is not initialised

Steps three to six of `H'23D150` carry a counter in `E3` that step two puts
to nought. It marks the colour's hitbox at ten and unmarks it at twenty, so
the box winks while the wait goes on. A call that starts at step four finds
in `E3` whatever the caller left there -- the original never does that, but a
comparison case can. The reconstruction declares it nought, and every case
that starts inside those steps hands the original a nought in `ER3` to say
so.

### A step budget is a test tool

The first mutation run against `module_colour_back` took its two loop bounds
`H'08` and `H'0B` up by one, which leaves the mutant spinning for ever. Every
case then ran to its limit -- and the limit in these cases was four hundred
million steps, forty minutes for one mutation. The cases themselves need a
hundred and ten thousand steps at the very most.

So the mutation runs now cap every case at four million steps. A mutant that
spins is still killed, because "did not return" is a difference like any
other; it is killed in ten seconds instead of forty minutes. The generous
budget belongs in the suite, where a case that runs long is a case that is
doing work; it does not belong in a harness whose whole purpose is to make
the routine behave wrongly.

### Mutants that survived, and why

**The bar and the percentage cannot be seen -- in one of the two.** Step six
of `H'23D150` draws the progress bar and the percentage label, and then
clears the slots and shows the colours again over the same part of the
buffer. Four mutations of the percentage arithmetic survived there: dividing
by sixteen instead of fifteen, multiplying by ninety-nine instead of a
hundred, and dropping either of the two `max` comparisons. The step counts
change, so the drawing really is different; the memory that comes out is not.
The same sum in `H'23D66A`'s long walk *is* the last thing drawn, and there
all four are killed. It is the part-25 trap again -- a box under something
drawn later cannot be seen -- and this time the same code in a second place
is what pins it.

**A store the ROM makes twice.** `H'1040B9` is set from `H'11F56F` and the
colour check on the very next line writes the same byte itself. Setting it to
nought instead is invisible, because it is dead in the original too.

**A code that can only be told apart on a path that never returns.**
`module_start_step` is called with `H'0A` in step four of `H'23D150` and with
`H'09` in step six. The two differ only when the hoop is one that can be
sewn -- and a sewable hoop at step four sets the colour walk going, whose
next step sends. At step six the same substitution *is* killed, by a case
with a sewable hoop, because step six is the last step before the walk ends.

**A guard whose two halves cannot be separated.** Step three of `H'23D66A`'s
long walk turns back only when `H'114D8E` is seven *and* `H'11A177` is
nought. Weakening the `and` to an `or` cannot be caught: on every input that
tells them apart it is the *original* that fails to return, not the mutant,
and a case the original cannot run is not a case.

### The press, and where its cases stop

The press is a jump table of twenty-one over the value the hit test returns.
`H'02` runs a colour and `H'14` takes one back; `H'03` and `H'04` measure the
two corners of the hoop; everything from `H'05` to `H'13` is a box on the
colour strip. `H'01` and `H'15` take the whole run away, one to the stitch
screen and one to the plain sewing screen.

Those last two are covered only as far as `module_sew_step`. That routine
answers "not yet" on every path a comparison case can reach -- the one path
where it answers "go" is its own step nought, which begins by sending a
message and waiting. Everything after it in both arms is read from the
disassembly and reproduced but not exercised: ten mutations of it survive and
every one of them is in that tail.

### The furniture moves the glass

The module screens' fill moves the box table's origin down to `H'50` to keep
the colour boxes clear of the percentage bar (part 25). The hit test adds
that origin to every box before it compares, so a press on box one has to be
at `H'74` down the glass, not `H'24`. The first set of press cases used the
plain coordinates, and every one of them passed -- because `touch_hit` said
"nothing pressed" on both sides. The tell was three bytes written by cases
that should have drawn a colour strip; `tool/trace_case.dart --all` said the
routine made one call and stopped.

**6,232 cases pass over 766 routines.** app.bin is 190,692 bytes. Screen H'37
is done; ten of the eleven module screens are left.

## 29. Screen H'24, the design turned and mirrored

Three routines, and after screen H'37 they are cheap: the cluster's leaves
are all written, so what is left of each module screen is its own press and
whatever one-line helper it alone reaches.

| | |
|---|---|
| `H'2465E0` `module_machine_running` | seven instructions |
| `H'230110` `module_turn_screen` | three boxes |
| `H'225A3E` `screen_body_24` | the plain lay-out, and the press |

`module_machine_running` is the bare half of `H'244D64`'s question -- is the
hardware state at `H'FFFEC0` one of the two that will take a message -- with
no claim on the link and nothing else looked at.

The press is the first routine in this cluster that a case can run all the
way through. Box `H'01` turns the design and box `H'02` mirrors it: both send
the module the same message, differing only in bit 7 of `H'11A618`, and light
the matching arrow. Neither waits for a reply, so both arms end where the
comparison can see them. Box `H'19` is the way out, to screen `H'23`.

Thirty-one mutations, twenty-nine killed. The two survivors are the same
thing twice: each arm finishes by masking `H'11A618` -- `AND #H'3F` in one and
`AND #H'BF` in the other -- and the line above has just put `H'0A` or `H'01`
into that byte. Neither value has bit 6 or bit 7, so neither mask can clear
anything. They are in the reconstruction because they are in the original.

### The pin that was not last

The first run of these cases reported the "module busy" case passing, and it
had tested nothing: `H'114DB9` was pinned to one and then wiped back to nought
by three wide zeros later in the same fill -- `H'114D40:80`, `H'114D90:40` and
`H'114DB0:20`. Both sides did the same wrong thing, so the case passed.

`audit()` has caught this shape since part 22, but it prints each
key-and-wide-key pair once for the whole run, and this pair had already been
printed against another routine. The tell was the step counts: "the module
busy" and "the turn" agreed to the instruction.

The fix is the one part 24 already had for `module_run_fetch` -- `furn`'s
`pin=`, which pops a key and puts it back at the very end. `mod3` now takes
`pin=` and hands it on, and the four module-screen case builders pass their
own extras straight to it, so nothing they set can be covered by the shared
fill.

**6,256 cases pass over 767 routines.** app.bin is 191,368 bytes. Nine module
screens are left. (`_reach` said each needed its press and at most two
routines beside it. Part 30 found out why that was an undercount.)

## 30. Screen H'23, the module's sewing panel

Five routines, and the first of the cluster where a press can be run all the
way through.

| | |
|---|---|
| `H'23E464` `module_box10_live` | seven instructions |
| `H'239FAA` `module_colours_dither` | the colour picture rubbed out |
| `H'2385A6` `module_colour_step_service` | the two colour-step asks |
| `H'231BB0` `module_reset_walk` | two steps, walked round |
| `H'22F962` `module_panel_screen` | eleven boxes |
| `H'225998` `screen_body_23` | the plain lay-out, and the press |

Eleven boxes: start the sewing, step the colour on and back, go to the
turning screen, toggle the needle, two ways out, pause, screen `H'4E`, and
the three speed bytes at `H'114DBE` walked down and up. The two arms that
send a message do not wait afterwards, so both ends are where a case can see
them.

### `_reach` cannot see through a jump table

Part 29 said each remaining module screen needed its press and at most two
routines beside it. That was wrong, and `tool/_reach.dart` is why: it follows
`JSR` and `BSR` but stops at `JMP @ERn`, which is exactly how every one of
these presses dispatches. It had only walked the code up to the jump table
and never entered a single arm. Screen `H'23` needed four routines under its
press, not none.

### Three fills that made a case test nothing

**The colour records.** The colour step draws the colour's own picture, and
the records live at `H'104D4A` -- the same RAM the picking grid's thumbnails
use, which is what the module-screen fill puts there. With the thumbnails in
place the unpack walks off into memory the two images do not agree about, and
that is a hang rather than a difference: the first run of these cases sat on
one case until it was killed. `COLOUR_RECORDS` was already in the generator
for `module_screen_tick`; the helper cases take it too. Only records nought
to three are pinned, so every case has to leave `H'114D89` under four once
its step has been taken.

**`H'114D62` under `H'0A` is "busy".** Every one of the fifty-two press cases
passed on the first run, and every one of them wrote six bytes and took the
same eight hundred steps: `H'24610A` calls the module busy on screen four
while `H'114D62` is under `H'0A`, and the fill left it at nought. The tell
was the byte count, not a failure.

**The screen slots.** `H'21F09E` takes a short path home when the slot it is
asked for already holds the screen being asked for, and `H'11A16A` is not
something either image's boot leaves in a known state. One side took the
short path and the other did not. The slots are pinned now.

### The four survivors, and why each is equivalent

Fifty-one mutations, forty-seven killed.

`module_wait_pass` taken out of the reset walk: with the machine at rest that
call does one thing, `H'236E9A` putting `H'114D99` up, and step one's buffer
clear zeroes `H'114D7A` to `H'114DB6` over the top of it.

The press's range test `H'0A` widened to `H'0B`: there is no arm for value
`H'0C`, so both the early turn-back and the fall-through the mutation allows
end in the same return.

Two of the three speed clamps: `H'114DBF` is put to `H'38` when it is under
`H'38`, and `H'114DC0` to `H'C8` when it is over `H'C8`. Widening either by
one only makes the clamp fire on the value it clamps to. The third clamp and
both of the `H'114DBE` tests *are* killed, by cases that put one byte at its
limit and leave the other two free -- which is what the first set of
boundary cases failed to do, having moved all three at once.

**6,338 cases pass over 772 routines.** app.bin is 193,396 bytes. Eight
module screens are left.

## 31. Auditing the suite: cases that compare nothing

A case passes when the two images wrote the same bytes and, where the case
asks for one, ended with the same result. A case that writes no bytes and
asks for no result has agreed about nothing -- part 24's trap. Of 6,339 cases,
**439 are that shape**, which looked alarming enough to go through them.

Almost all of them are fine, and the reason is worth stating: a guard doing
its job *is* the behaviour under test. A case named "the module busy" that
writes nothing has pinned the fact that a busy module makes the routine
return, and a mutation that takes the guard out breaks it. The step counts
prove the routine was reached -- they differ case by case within every family
looked at, which they could not do if the fill were stopping short.

`tool/blind_cases.py` does the audit now, against a full run's output. It
separates the two shapes that *are* faults:

**Blind routines** -- every case of a routine compares nothing. Twelve of
them. Eleven are right: four `link_gap` delay loops, `link_delay` with a
count of nought, two single-RTS hooks, `motor_brake_pulse` (which pulses a
port bit off and straight back on, so nothing has changed by the time it
returns), and three send-and-wait routines whose only runnable paths are the
turn-backs part 25 describes.

The twelfth was a real hole. `sew_counters_service` had one case, and it sat
on the guard: the two lifetime totals written back into the settings block
through `rom_flash_write` had never been run at all. A second case with
`H'114DC8` bit 4 up and the RAM copies made to differ from the flash reaches
it -- 263 bytes written where there were none, and it kills four mutations of
the body that nothing was killing before.

**Same-path groups** -- blind cases of one routine that run for exactly the
same number of steps on both sides, so they took the same turn. Fifty groups,
ninety-six cases beyond the first of their group. Every one looked at is
honest but redundant: `module_panel_box` has sixteen cases for four states
that have no box, four modes each, and the mode is never read; `hitbox_set_state
what=03` turns back on the style before it ever looks at the state, so its
four `was=` variants are one test. They are left alone -- the cost of a
redundant case is seconds -- but the report is the place to look first when a
family of cases has quietly collapsed onto one guard, which is exactly what
happened to screen `H'23`'s press in part 30 and what the step counts said
there too.

The tool also reports **duplicate case names**, and found one: two different
`screen_touch (02, current at 0F)` cases pressing different points. That is
its own hazard rather than a coverage one -- `subspec.py` picks by name and
`merge_cases.py` replaces by name, so the second of a pair can be thrown away
by a merge without anything saying so. Renamed.

**6,339 cases pass over 772 routines.**

## 32. Screen H'4E, which does nothing

Two routines, and the interesting thing about them is what is not there.

| | |
|---|---|
| `H'22F82A` `module_extra_screen` | twenty-five keys, none of them wired up |
| `H'2258F2` `screen_body_4E` | the plain lay-out, and the press |

The press dispatches on a jump table of twenty-five, and every one of the
twenty-five arms is the same two instructions -- the answer put to nought and
a branch to the return. The table is not a stub written once and pointed at
twenty-five times: it is twenty-five *distinct* four-byte stubs, laid out end
to end, which is what a screen looks like when someone has drawn the boxes
and filled none of them in. Box ten of the sewing panel is what reaches it,
and only when bit 0 of `H'11F538` says the box is offered at all.

So the whole of what the routine does happens before the table: it marks the
screen state at `H'10`, shows the held message, and asks whether the module
is busy. Even that last question is dead -- `H'24610A` answers "not busy" for
every state past `H'0B`, and this one is `H'10`.

Seven mutations, five killed. The two survivors are the busy question
inverted and the range test widened by one, and neither can be killed by any
case: both gate only the arms that do nothing. That is a pleasant change from
guessing at why a mutant lived -- here the reason is the shape of the routine
and there is nothing to look for.

**6,353 cases pass over 773 routines.** app.bin is 193,780 bytes. Seven
module screens are left: `H'16`, `H'15`, `H'14`, `H'13`, `H'12`, `H'11` and
`H'09`.

## 33. Screen H'16, the module's sizes and speed

The biggest screen in the cluster: a press of twenty-five keys over six
numbers, and three routines under it that nothing else had needed.

| | |
|---|---|
| `H'230EF4` `embroidery_panel_save_b` | `H'230EA8` again, byte for byte |
| `H'23A06A` `module_size_shrink` | the held size brought down to the slot's |
| `H'245CE6` `module_slot_changed` | five fields against a snapshot |
| `H'22DBFA` `module_sizes_screen` | twenty-five keys |
| `H'22584C` `screen_body_16` | the plain lay-out, and the press |

Six numbers, each with a key to move it down, a key to put it back to the
middle, and a key to move it up. Two are percentages kept in a single byte
between `H'0A` and `H'64`; two are pairs of bytes that move together; one is
the speed. The eighteen arms are the same three routines over and over with
the field and the label changed, which is the shape `H'248AC6`'s eight hoop
nudges already have, so they are written once each and given the field as an
argument.

`H'245CE6` writes its five-byte snapshot out six times over, once at the end
of each of the six ways it can answer yes. That is one helper called six
times here -- the same five stores in the same order, and the only factoring
in the routine.

### Three arms that cannot be reached

Values five, six and seven are turned back by a guard *before* the jump
table, and the table has three distinct arms for them. They are real dead
code in the ROM -- about seven hundred bytes of it -- and they are not
written here, because nothing can call them and no case could ever cover
them.

### The instruction the filter ate

Twenty-five arms is too much to read line by line, so they were read through
a script that strips the boilerplate -- the link-quiet chain, the slot-offset
shift, the branches -- and prints what is left. That is what made the shape
of the six families visible in one screen of output, and it is also what put
four errors into the first draft.

The slot offset is built with four `SHLL.W R6` in a row, so `SHLL.W R6` went
into the strip list. But the paired-byte arms *also* double the byte before
they show it: the number beside them is `H'C8` less **twice** the first byte,
and the doubling is one more `SHLL.W R6` that the filter took out along with
the four. The reconstruction showed 150 where the original showed 102, and
the two only part company inside `int_to_decimal`'s argument -- everything
either side of it agrees, which is why the case failed on a handful of pixels
in a label rather than anywhere that would point at the cause.

The same filter hid three more:

* the speed keys read the byte and branch on it -- a byte of nought is one
  that has never been set, and the first press puts `H'23` in it and steps no
  further -- where the stripped listing made it look like an unconditional
  store followed by a step;
* the redraw key is an if/else on whether the slot has moved, not a run of
  statements, and `module_size_shrink` inside it is gated on
  `pattern_attr_bit3`;
* the start key's quiet test skips only the *message*; the screen changes
  either way.

All four were found by cases rather than by re-reading, and all four are the
same lesson from the other end: **a filter that makes a listing readable is
also a filter that can hide an instruction**, and the four `SHLL.W` of a slot
offset look exactly like the one `SHLL.W` of a doubling.

### What the mutations say

Twenty mutations, one for each distinct shape in the press, and thirteen are
killed -- including the doubling itself, so the fix is pinned rather than
merely made. The seven that live are all the same kind of gap: a boundary or
a gate the fifty-three cases do not straddle.

* the range test widened by one, which has no arm to reach and is the same
  equivalent mutant screen `H'4E` has;
* the `pattern_attr_bit3` gate on the redraw, and the middle-key list, which
  need a case on the other side of each;
* three of the four limits -- `H'64` on a percentage going up, `H'03` on a
  pair going up, `H'05` on the speed going down -- which need the byte
  sitting exactly on the limit *and* the hoop still fitting, and the cases
  put it on the limit only;
* the undo when the hoop does not fit, which needs a fill where it does not.

These are named rather than closed because a mutation run costs about half an
hour a mutation on this machine and the seven together are an afternoon. They
are gaps in the cases, not doubts about the code: every one of them is a
condition whose two sides the reading is sure of and the suite has only seen
one of.

**6,429 cases pass over 777 routines.** app.bin is 196,544 bytes. Six module
screens are left: `H'15`, `H'14`, `H'13`, `H'12`, `H'11` and `H'09`.

## 34. Screen H'15, where the design sits in the hoop

Twenty-five keys again, but this time every one of the twenty-five has an arm
of its own and every one can be reached.

| | |
|---|---|
| `H'245848` `module_turn_fits` | whether a turn would still fit the hoop |
| `H'22C24C` `module_hoop_screen` | twenty-five keys |
| `H'2257A6` `screen_body_15` | the plain lay-out, and the press |

Most of the press is shapes screen H'16 already had. Its value H'0C redraw is
the same routine byte for byte; the two ways out differ only in the screen
they go to; the start key is the same twelve steps. The nine keys that move
the hoop are one helper with the direction as an argument -- H'0E to H'16
carry H'08, H'01, H'02, H'07, H'09, H'03, H'06, H'05 and H'04, and the middle
one, which puts the hoop back where it started, is the only one that does not
count the step. The three size keys move both percentages together instead of
one each, so the label shows their sum -- which is twice either one, the same
doubled figure screen H'16 works out with a shift.

### H'245848, the only new routine

Eleven hundred and eighty bytes of floating point that answer one question:
if the design were turned, would it still be inside the hoop?

The design's width and height come from `H'104CCE` and `H'104D06`, scaled by
the slot's two percentages. Those give a diagonal and the angle to its
corner. The rotation to try is either the one the slot holds at `H'11A260` or
that stepped by an argument -- five degrees a step, minus a hundred and
eighty, into radians -- and `H'11A25F` turns it back the other way. The
corner is then swung to each side of the angle and both corners are asked, on
each axis, to keep clear of `H'11A626`/`H'11A628` by the design's own offset
in tenths and two millimetres more.

Two of its locals are never written on some paths -- the corner angle when
the design has no width, and the rotation when the first argument is above
one -- and the ROM reads whatever the stack held. Both start at nought here,
the same hole and the same answer as `H'24217A` in part 21, and no caller
goes to either.

### Two mutations that cannot be killed, and why that is a finding

Twenty-eight mutations on `H'245848`, twenty-six killed. The two that live
are not gaps in the cases; they are equivalences in the routine, and both
took an argument to see:

* **`if (f2u(w) != 0) corner = float_atan(h / w);` against `f2u(h)`.** With a
  width the two agree. With a height of nought the original still takes the
  branch and `float_atan(0 / w)` is nought, which is what the mutant leaves
  behind by not taking it. Only a width of nought separates them, and that is
  the path where the ROM reads the stack.
* **the mirror byte's test.** Turning the sign of the angle over swaps the
  two corners: `cos(corner + a)` becomes `cos(corner - a)`, which is
  `cos(a - corner)`, which is the other corner's. Both corners are tested
  against the same limits, so the answer cannot change -- only the order in
  which the four checks fail, which is a step count and not a result.

The first twenty cases killed only fourteen of the twenty-eight. They all
left the design a long way inside the hoop, where nothing much changes the
answer. Eleven more, worked out from a model of the routine in Python that
searched for parameters where each surviving mutation flips the result, took
it to twenty-six. **A case that exercises a path is not the same as a case
that can see it go wrong**: the four corner checks needed a design that is
not square, a rotation that is not nought, and a hoop cut down until one step
either way turns the answer over.

That model then paid for itself twice. The press's own sweep left the six
steps the held turn key tries alive -- `module_turn_fits(1, up, 6)` against
`(1, up, 5)` -- because the six is only visible as a *number* where six
would not fit and five would; everywhere else the step it goes on to take
covers for it. The same search found the hoop: a sixty by eighty design in a
two-hundred square.

### The press's own mutations

Fifty-three mutations, forty-nine killed. Five of the nine that survived the
first pass were real gaps and closed with seven more cases:

* the two ceilings on the size keys need the byte exactly on the limit on
  *one* of the pair with the other below it, and a hoop the bigger size still
  fits -- on both at the limit the second test hides the first;
* `H'11F299` and `H'11F29A` need the two percentages kept apart before
  anything can tell which is put by where;
* the nudge's two bits in `H'11F2A2` need the byte to start with both up,
  since the fill leaves it at nought and neither setting one nor clearing
  another shows;
* the held turn key's six, above.

The four that remain are equivalent mutants, and three of them are one shape:

* the hit test's range widened past the last box, which no value can reach --
  the same equivalence screens H'4E and H'16 have;
* the three rotation clamps -- `>= H'06` going down, `<= H'42` and `<= H'47`
  going up -- each widened by one. At the boundary the step and the clamp
  produce the same byte: `H'06` less six is nought, which is what the else
  branch stores, and `H'42` plus six and `H'47` plus one are both `H'48`,
  which the wrap turns into nought. **A clamp that fires on the value it
  clamps to cannot be seen from outside**, and this routine has three.

### The arm that cannot be tested from here

Value H'0B calls `H'2431C2` when `H'114DA0` is nought, and that goes on into
the stitch machinery, which the module screens' fill does not set up: the two
images part company inside `H'11A67D` and the timers, not in anything the
press does. All four cases for that key therefore have the talk already
ended, which is the branch that skips the call. `H'2431C2` has six cases of
its own under the stitch fill.

**6,595 cases pass over 781 routines.** app.bin is 199,844 bytes -- past
`H'230300`, so `mergeapp` now hands the rebuilt image everything up to
`H'230CA3`. Five module screens are left: `H'14`, `H'13`, `H'12`, `H'11` and
`H'09`.

## 35. The fills shared out

`routines.json` had grown to 391 MB, which is past what GitHub will take in a
single object, and it grows again with every screen. Nearly all of it was one
thing: **every case carried its own full copy of a fill it shared with its
family.**

A fill is a map from `ADDR:LENGTH` to a byte value, and the biggest of them
runs to 11,716 entries -- the whole of the module screens' furniture. A
family of cases is built from one of those with a handful of values changed
on top, and each of the 6,595 cases was storing the lot. The mean case was
36 KB and the largest 188 KB, against deltas that are usually under ten
entries.

The numbers said the shape of the fix outright:

| | |
|---|---|
| cases with a fill | 6,544 |
| distinct fills | 4,785 |
| **distinct *ordered key sequences*** | **1,003** |

Only a thousand distinct key orders across six and a half thousand cases:
inside a family every case has not merely the same keys but the same keys in
the same order, and differs only in values. So the file now keeps one base
fill per distinct key order in a `fills` table, and each case names its base
and carries only what it overrides:

```json
"fills": {"f000": {"11A25A:1": "32", ...}, ...},
"cases": [{"name": "...", "fillBase": "f000", "fill": {"11A25A:1": "64"}}]
```

A case with no `fillBase` keeps its whole fill, so the hand-written cases
needed nothing done to them.

| | |
|---|---|
| before | 391 MB |
| compact separators instead of `indent=2` | 241 MB |
| fills shared out | **30.3 MB** |

### Order is part of the meaning

The one thing that could have gone quietly wrong here is the fill-ordering
trap from part 17: fills apply **in order**, and an address named twice takes
the value named last. Composing a case on its base therefore has to give back
the same *sequence* of pairs, not merely the same set.

Python's `dict.update` and Dart's `Map` both follow the same rule -- a key
already present keeps its place and takes the new value, a key not present
goes on the end -- which is the rule the fills were built under in the first
place, so `compose` is that and nothing more. `tool/spec_fills.py` checks it
per case while factoring: the base and the overrides put back together must
equal the original pair list, order included, or that case keeps its fill
whole. Nothing was quietly changed; the whole spec was reloaded and compared
equal to the original, all 6,595 cases, before anything was written.

Prefix-chaining the bases against each other was measured and dropped: it
recovers 4.8 MB of the remaining 28.4 MB and would cost a recursive schema.

### What it cost elsewhere

`composeFill` went into `lib/routine_compare.dart` rather than into the
comparison tool, because `tool/trace_case.dart` and `tool/trace_writes.dart`
read `case['fill']` too and would otherwise have traced with only the
overrides -- silently, and with a fill that reaches nothing. `subspec.py`
prunes the `fills` table to what its chosen cases name, so a three-case
mutation spec stays small instead of carrying all thirty megabytes.

## 36. Screen H'14, the grid of patterns to pick from

Not a jump table this time: the press is a run of comparisons, and it
dispatches on the box's *value* rather than its number, so the values run
well past the thirteen boxes the hit test covers.

| | |
|---|---|
| `H'23A012` `module_pick_past_end` | whether a cell reaches past the last pattern |
| `H'249AEE` `module_arrow_10`, `H'249B3A` `module_arrow_11` | two more arrows |
| `H'23A716` `module_stitch_marks_clear` | the stream buffer and its marks |
| `H'231F72` `module_number_label` | a number, and a mark beside it |
| `H'239390` `module_pick_row` | one row of three cells, thumbnails and numbers |
| `H'239678` `module_page_back`, `H'2397D2` `module_page_on` | the two paging keys |
| `H'244ADA` `module_pattern_send` | the pattern handed to the module |
| `H'22BF8C` `module_pick_screen` | the press |
| `H'2256AC` `screen_body_14` | the lay-out, with two extra pictures |

The paging keys are the interesting pair. Neither simply moves the page:
each first looks for a colour boundary between where the page is and where
the step would put it -- colours are `H'1B` patterns apart -- and if one
falls inside the step the page does not move at all. Instead `H'114D8C` goes
onto the boundary and the slot's record byte takes the colour's number, so
the next press starts the page from there. Only when nothing is in the way
do the three rows slide by a region copy and the one row that has come into
view get drawn.

`H'239390` draws that row: three cells across, each a thumbnail of `H'3E`
rows of nine bytes with a bit to a pixel, drawn a pixel at a time, and the
pattern's number beside it. The number gains a mark after it only when the
module holds more patterns than the machine knows about and this cell is one
of the extra ones. The routine came out right first time, all seventeen
cases -- which is what reading the geometry carefully rather than guessing
buys.

### Two traps in the cases, neither in the code

Both cost more than the routines did.

**Stack offsets in a case are hexadecimal.** Writing `'10'` and `'12'` for
the fifth and sixth arguments put them at `H'10` and `H'12` instead of ten
and twelve, so two arguments landed in the wrong place. `'4'`, `'6'` and
`'8'` read the same either way, which is why five screens went by before
this could bite.

**The test font defines only `!` and the ten digits.** A table slot left
undefined is a pointer of nought, and `text_draw` follows it -- into the code
region, which the two images differ in *by construction*. So a routine that
draws any other character has its two sides part company for reasons that
have nothing to do with the routine, and the failure looks like a drawing
bug: a tall stripe of pixels down the frame at a column neither box is at.
The mark at `H'6D` now gets `'0'`'s glyph. The same trap took a negative
number with it, since `int_to_decimal` has no minus sign and turns one into
characters outside the digits -- that is the number formatter's business, and
has cases of its own, so the case went rather than the font growing a
work-round.

**And one blind case caught by its own step count.** The first pair of arrow
cases passed while running 280 steps against their four siblings' 20,000,
because the fill those siblings use has a box table that stops short of box
ten. Regenerated on the module screens' furniture they write 21 and 27 bytes
and tell the two pictures apart. The lesson is the one from part 31 read
backwards: **a case that passes suspiciously cheaply is a case that is not
running the routine.**

### The arm that cannot be reached, measured rather than assumed

`H'244ADA` sends two messages one after the other and waits on each. The
wait ends only when the SCI interrupt clears `H'11F29E`, and nothing in a
comparison run does that; unlike the other send-and-wait routines there is no
path through it that turns back before the first message. Both sides were
run to **four hundred million steps** and neither returned. It is therefore
written from the listing and has no cases, and the press's own cell arm --
which ends in it -- is covered only as far as its five guards. Those five are
each turned back by a case of their own.

## 37. Screen H'13, the same grid reached the other way

The shortest screen in the cluster, because it is screen H'14 over again.

| | |
|---|---|
| `H'22BCCC` `module_pick_screen_b` | the press, a near-twin of `H'22BF8C` |
| `H'2255B2` `screen_body_13` | the same lay-out with its own two pictures |

**No new routines at all.** The press calls exactly the same twelve routines
as screen H'14's and is exactly as long, so it was read by diffing the two
listings with the local branch targets blanked out: 206 instructions each and
thirty-two differing lines between them. That is a fifth of the reading the
screen would otherwise have taken, and it is worth doing whenever two presses
turn out to have the same callee set -- which the reach tool will say in a
line.

The four differences:

* the state byte it leaves behind is `H'02` rather than `H'03`;
* the `H'55` key is the one that wants the module answering, so bit 0 of
  `H'114D51` is tested there instead of on the cell arm;
* that key hands on the *other* kind of pattern -- `H'114DA1` and the slot's
  `H'11A41D` both take one rather than nought -- and goes to screen `H'14`,
  where screen `H'14`'s own equivalent goes to `H'13`;
* the `H'1A` key writes its two bytes the other way round.

The guard that moved is worth a case each way round, and the one that says
so is `a cell with the module not answering, which it does not look at`: it
matches the `something already asked for` case step for step, which is the
proof that the test really has gone from that arm.

The cell arm ends in the same `H'244ADA` that cannot be reached in a
comparison run, so it too is covered only as far as its guards.

## 38. Screen H'12, which kind of pattern to pick

Three boxes, and the third does nothing at all.

| | |
|---|---|
| `H'22BB2A` `module_kind_screen` | the press |
| `H'2254D2` `screen_body_12` | the lay-out, and the preview it owes |

No new routines. The two live keys do the same eight things in the same
order and differ only in what they hand on: key one leaves `H'114DA1` and the
slot's `H'11A41D` at nought and goes to screen `H'13`, key two puts one in
both and goes to `H'14`. That is the pair of picking screens from parts 36
and 37, and it closes the circle -- each of them sends the *other* kind on,
and this is where the choice is first made.

Only the second key also wants bit 0 of `H'114D51`, which is the module
having said how many patterns it holds.

The body is the plain lay-out with two additions: box three is put into
state `H'02` as the screen is laid out, and `H'11B0A9` is raised so the same
pass goes on to draw the preview of whatever item `H'FFFEE0` names.

### Twelve cases that were all the same case, twice over

This screen's cases had to be fixed twice, and both times the tell was the
step count rather than a failure.

**First:** all twelve arm cases passed at 805 steps and 11 bytes -- every one
of them identical. `H'24610A` reports state `H'01` busy unless **bit 7** of
`H'114D51` is up, and the fill had only bit 0, so the three-key dispatch was
never reached at all.

**Then:** with that fixed they all ran 4.65 *million* steps and still wrote
the same 12 bytes. `H'249DE8` polls the module's reply buffer at `H'104C90`
for the five bytes of the identity block at `H'200103` -- `"V03.0"` -- two and
a half thousand times, with a delay between tries, before giving up. Left
empty it always gives up, so every key ended in the same `link_claim(H'0B)`.
Pre-loading those five bytes makes it match on the first try.

Only then did the cases separate: seventeen bytes for the first key against
eighteen for the second, and the guard that only the second has proven both
ways -- turned back at 922 steps, through at 1001.

**A case that passes cheaply and a case that passes expensively can both be
the same case.** The first fix cost nothing and the second cost four and a
half million steps, and neither was doing anything a mutation could have
been caught by.

## 39. Screen H'11, a pattern by its number

The number pad's own screen: two digits typed, and the pattern they name.

| | |
|---|---|
| `H'2382EE` `module_pedal_pass` | the pedal noticed, once |
| `H'206724` `queue_record_set` | a pattern's cached parameters, sixteen arguments |
| `H'21A070` `goto_number_screen` | the press |
| `H'225046` `screen_body_11` | the lay-out, pushed on the stack |

Only boxes nine and ten are live. Box ten goes back one screen. Box nine
makes a number from the two digits at `H'11B0FE` and `H'11B0FF`, indexes the
word table `H'11B2BA` points at, and hands the word there to
`goto_pattern_number`. A nought in the table, or a pattern the machine will
not go to, and it gives up to screen `H'0F`.

### Sixteen arguments, and how they were checked

`H'206724` takes `mark`, `slot` and then **seven flag-and-value pairs**, each
flag saying whether to write its field. Two of the seven fields *move* when
bit 6 of `H'11A7BD` is down -- to offsets `H'02` and `H'07` instead of `H'03`
and `H'08`, which is the same pair of fields for the other kind of stitch --
and byte nought's low six bits are rewritten last, keeping only bit 7 of
whatever was there.

Getting the order of sixteen arguments right from a listing is the sort of
thing that is easy to be quietly wrong about, so the cases are built to say
so: one case per pair, with that pair's flag up and the other six down.
Each writes four bytes, and each writes them **at a different address**. A
pair read into the wrong position would land in another field and the
comparison would fail on the address, not merely on a value. Both settings of
bit 6 are run for all of them, which is twenty-eight cases for one routine
and worth it.

The press's own two call sites turn on exactly one of the seven flags and
pass `H'FF` for every other value, which is a second, independent check of
the same thing: a mis-ordered argument would have put `H'FF` into a field.

### A pointer that had to come out of the machine's dump

`screen_body_11` does not load a constant picture the way the rest of the
cluster does; it loads the fifth long of whatever `H'11B2B6` points at. Left
as the boot leaves it that is a wild pointer, `image_load` runs off into
nothing and never comes back, and both sides hang identically -- which is a
pass the harness rightly refuses to give.

`allnewdump.bin` settles it: `H'11B2B6` holds `H'11510E`, and that block's
fifth long is the picture at `H'3B4352`. Both are pinned, and the case now
draws fifty-three thousand bytes. **The first case this cluster has needed
that could not be derived from the listing alone** -- everything else has come
from reading the code, and this one had to be looked up in a picture of the
real machine's memory.

### The arm that the machine cannot be in

The press's second call to `H'206724` is gated on the looked-up pattern being
number one, and pattern one is in none of the item lists, so
`goto_pattern_number` turns back before that line is ever reached. The other
call site is covered, so the call and its argument order are checked; only
that one constant is not. Contriving a fill in which pattern one resolves
would be testing a state the machine cannot be in, which is worth less than
saying so here.

## 40. Screen H'09, a stitch length typed in

The last of the cluster, and a single routine: `H'218CBE` is the whole
screen, called with one as it lays out and with nought on every pass after.
All sixteen of its callees were already written.

| | |
|---|---|
| `H'218CBE` `stitch_number_screen` | the whole screen |
| `H'22474C` `screen_body_09` | the lay-out |

The typed digits live as a string at `H'11A1A5`. Box value `H'0E` rubs the
last one out, `H'1A` leaves without changing anything, `H'19` takes what has
been typed -- four up to the ceiling `H'11B31E` holds -- and every other value
is a digit, the box value plus `H'15`. A leading nought is refused and so is
a third digit.

Two things in the listing read as noise on the way down and only made sense
on the way back up:

* the two buffers copied out of `H'250758` and `H'25075E` at the top are
  both written over before they are read, and both sources are all noughts
  anyway. They are the compiler's, not the screen's;
* a single byte is set to nought at the very top, seven bytes into the
  frame, for no visible reason. It is the terminator of the one-character
  string a digit is appended through, planted a hundred instructions before
  the character is.

### A name that was already taken

The font this screen's first box draws with lives at `H'1196EA`, and the
string after the number is `"mm"` from `H'250AE0` -- the same character
`H'6D` that screen H'15's mark needed, and the same trap, since the test font
defines only `!` and the digits.

The first attempt at a fix defined a `FONT4` for it. There already **was** a
`FONT4` at that address, built four thousand lines earlier, and redefining it
would have quietly handed every existing case that uses it a narrower,
half-built font. Nothing would have complained.

What caught it was the fill-ordering audit naming `1196EA:400` when the line
just written said `1196EA:140` -- a span that had not been typed, which meant
something else owned the name.

The machinery that was already there turns out to have met this exact
problem and written down what it looks like:

> a character with no glyph reads its bitmap header from address zero and
> paints a column down the screen

which is precisely the tall stripe that took so long to diagnose from first
principles in part 36. It also already had the convention for fixing it.
**The codebase had solved this before and said so; the cost of not reading
around the machinery before duplicating it was one near-miss and an
afternoon of part 36.**

### The module screens are done

`H'37`, `H'24`, `H'23`, `H'4E`, `H'16`, `H'15`, `H'14`, `H'13`, `H'12`,
`H'11` and `H'09` are all reconstructed and compared.

## 41. Mutating the five screens, and the blind spot it found

Screens `H'14` down to `H'09` were reconstructed and compared before any of
them was mutated. **A hundred and twenty mutations, a hundred and eighteen
killed.**

They were run a routine at a time, each mutation against only the cases that
can see it, rather than one spec of every case for every mutation; that is
several times quicker and no weaker. Every anchor was checked against the
sources first -- two of the hundred and twenty did not match, one a typo and
one an anchor that appeared *twice* in its routine, which `mutate_apply`
refuses. Ten minutes of checking saved two hours of running.

### The uniform fill

Fourteen kills' worth of blind spot came from one line of the case
generator: the thumbnails the picking grid draws were filled with `A5`
repeated seventeen hundred and sixty times.

A block that reads the same at every offset hides **every** mutation of the
arithmetic that works out where to read. Three separate routines were
affected -- the `H'022E` between one thumbnail and the next, the nine bytes
between one row of a thumbnail and the next, and the row index one of the
paging keys hands on. All seventeen `module_pick_row` cases passed either
way; the comparison suite could not have found this, and did not.

The fill now goes down in `H'20`-byte bands of differing values. The same
change took the `module_page_on` survivor with it, once the block was also
made big enough that moving the index reads *pinned* data rather than two
equally unpinned bytes -- a shorter block had both versions reading past the
end, where the two images agree by accident.

**A fill is part of the test, not scenery.** A case can exercise a line, pass,
and still be unable to see it go wrong.

### Three that were testing the wrong guard

Three more survived because the case aimed at them was stopped by a
*different* test first:

* the `H'06` window a colour boundary is looked for in could not be probed
  while `H'114D8C` sat at nought, because the equality test after it skipped
  the arm for both versions;
* `module_pick_screen` and `module_kind_screen`'s hit-test bounds needed a
  press on the last box each covers, and every case pressed box one;
* `queue_record_set`'s two masks on byte nought disagree about **bit 6
  alone**, and no record in any case had it set.

The region copy that slides the grid up needed the rows of the front buffer
to differ from one another before moving its source row could show; with a
value per row it fails ten cases.

### The two that are left

* **`rows > 3` widened to `rows > 4`** is equivalent, and provably so:
  `module_pick_row` loops `c < 3` whatever it is told, and uses `count` only
  in `count <= c`. No clamp above three can be told from three when a row
  has three cells.
* **`H'114D51 & 0x01` widened to `& 0x02`** in `module_pick_screen` is fenced
  in by the send-and-wait limit of part 36. Every case that reaches that
  guard is turned back by the `H'114D72` test on the very next line, so both
  versions return having written the same bytes; and a case that passes both
  runs into `H'244ADA`, which does not come back at any step budget. It is
  the same gap seen from a second direction.

## 42. The fill-ordering backlog

The audit in part 31 had been printing warnings all along and nobody had
read past the last line of them. There were **three hundred and thirty-nine**
-- and that is the count *after* it drops repeats of the same pin and the
same wide key. Counted properly, **1,952 of the 1,970 generated cases carried
at least one wiped pin, 176,157 in all**, from about nineteen root causes in
the shared fill builders.

They are all gone, and fixing them changed what ninety-seven cases do.

### Three tables pinned past their ends

The biggest cause was three picture tables given spans that ran over the
structures after them. The machine's own memory dump settles where each one
really ends, and it is exact:

| table | span used | what the dump shows |
|---|---|---|
| `H'11581E` | `H'100`, 64 entries | **44**, ending at `H'1158CE` |
| `H'1158CE` | `H'200` and `H'400` | **78**, ending at `H'115A06` |

Entry 43 of the first table is the last picture pointer and entry 44 *is*
`H'1158CE`. Entry 77 of the icon table is the last pointer and entry 78 holds
`0004000D` -- a count and its values, a list, not a pointer. And the highest
key any real strip carries is `H'4D`, which is 77. The table fits the data it
serves exactly.

So the oversized spans were wiping the two lists at `H'115A06` and `H'115A20`
that the same builders went on to set, and the keys the generator had
invented above `H'4D` -- up to `H'66` -- had no picture at all and would have
blitted from address nought.

### The base case had it frozen in

`base_fill` starts every generated fill from one hand-written case, and that
case's own fill has `11581E:100` at position 172, *after* the sixteen icon
pins it sets at positions 122 to 152. It was wiping its own work, and
because it is hand-written and not generated, no amount of regenerating
could ever have fixed it. It had to be corrected in `routines.json` itself,
which touched two thousand and seventy-four cases.

### A remedy that already existed, applied by hand

`move_wipe` has been in this file for a long time and forty-five cases call
it. It does one thing: moves the wide catalogue zero back to before the pins
it would otherwise cover. Some builders never called it, and the proof is
pleasing -- fixing the first one made the warnings reappear against the
*next* builder with the same omission.

So `hoist_wipes` now does what `move_wipe` does, for every wide key and
every case, as a pass over the generated cases before the audit runs. Only
the ordering moves; no value changes. It costs `gen_cases.py` about a minute
and a half.

**It is a net, not a cure.** All three causes above would have been hidden by
it rather than found. It belongs next to the audit, not instead of it -- and
the audit is worth having again now that it prints nothing, because the next
warning will be a new mistake rather than one of three hundred.

### What it was worth

Ninety-seven cases changed behaviour and seventy-three of them write a
different number of bytes. Not one of them started or stopped passing: they
were all quite happy before, because both images were doing the same thing
with the same wiped data.

The best evidence is a comment that was already in the file, above the cases
for `H'212B5E`:

> Boxes 5 to 9 rather than 1 to 5: the shared fill wipes the box table again
> part-way through, so only the boxes defined after that survive it.

**The defect was known, and worked around instead of fixed.** Whoever wrote
that avoided boxes 1 to 5 because the fill destroyed them, and left a case
called `boxes 1 to 5, none of them drawn` to record the symptom. With the
ordering right those boxes survive, that case draws them, and it writes four
and a half times as many bytes as it did. The workaround is gone and so is
the name.

## 43. What was left when the screens ran out

All seventy-nine screens the dispatch table holds are reconstructed --
`H'00` to `H'4E`, which is where the table stops; the entry after it is not a
pointer at all. The `default` arm of the dispatch still said *the other
eighteen, not written yet*, which had been true once and was not any more.

What remains is the tail. Of the five hundred and twelve places the
application calls into, four hundred and thirty-one have a case. Of the
eighty-one that do not:

* eight are the soft-float helpers -- `__mulsf3` alone is called from two
  hundred and thirty-one places -- which are libgcc and never reconstructed;
* four are above `H'250000`, which is data that happens to look like a call
  target to a scan of the encoding;
* one is `H'244ADA`, which cannot be reached in a comparison run at all;
* **forty-seven are called from a single place each**, which is the long tail
  the census describes: code down paths a boot never takes.

`H'231F14` was the one worth doing next, being called from fourteen places
and the most-called routine still without a case. It is the plain sister of
`H'231F72` from part 36: a number centred in a box, in the other font, with
no mark after it.

It reads all four of its coordinates through **the same displacement**,
`@(H'18,ER7)`, four times over -- each `PUSH` between them moves the stack
pointer down two, so a fixed displacement walks up the caller's arguments a
slot at a time. Read without `tool/frame.py` that looks like one value
fetched four times; resolved, it is `L10, L8, L6, L4`, the push order
backwards, and the arguments pass straight through.

Nine cases, and all eight mutations killed -- including the three that swap
the alignment, which is the one thing here that could have been misread and
still drawn something plausible.

## The tools

Everything the comparison suite needs now lives in `tool/`, run from the
repository root. It used to live in `/tmp`, which is not a place to keep the
only copy of the thing that can rebuild `routines.json`.

| | |
|---|---|
| `tool/gen_cases.py` | builds every generated case; writes `/tmp/newcases.json` |
| `tool/lzwlib.py` | the LZW model the packed-picture cases encode with |
| `tool/merge_cases.py` | folds those cases into `routines.json` |
| `tool/subspec.py` | cuts the suite down to one routine's cases by name |
| `tool/spec_fills.py` | shares the fills out, and puts them back together |
| `tool/compare_routines.dart` | runs the cases |
| `tool/mutate.sh` | runs a list of mutations against the chosen cases |
| `tool/mutate_one.sh`, `tool/mutate_apply.py` | one mutation, and the edit itself |
| `tool/trace_writes.dart` | every write into a range, in order, with its PC |
| `tool/frame.py` | a listing's `@(NN,ER7)` rewritten as frame-base locals |
| `tool/blind_cases.py` | cases that compare nothing, and cases that repeat each other |

A run goes:

```
python3 tool/gen_cases.py && python3 tool/merge_cases.py
python3 tool/subspec.py 'module_colours_show '
dart run tool/compare_routines.dart /tmp/mut_spec.json
tool/mutate.sh                       # against the same /tmp/mut_spec.json
```

`tool/mutate.sh` takes its own copy of the application sources at the start of
a run and puts them back on the way out, from a trap, so an interrupt or a
mutation that will not compile leaves the tree as it found it. The copy is
thrown away when the run ends. The version this replaced kept its pristine
copy in `/tmp/good` between runs, and a stale one silently reverted an
afternoon's work: **a snapshot must not outlive the run that took it.**

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
