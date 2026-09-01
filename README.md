# sim_h83003 — an H8/3003 Simulator

An interactive simulator for the Hitachi/Renesas **H8/3003** microcontroller
(H8/300H CPU core, advanced mode), built with Flutter for desktop, mobile and
web. It follows the same scheme as the companion `sim_6502` application: a
live register panel, a hex memory view, a disassembler, a profiler, and
Pause / Step / Run controls with breakpoints.

Reference: *Hitachi H8/3003 Hardware Manual* (REN_e602055_h83003), section 2
(CPU) and appendix A (instruction set).

The purpose is to try and reverse engineer the Bernina Artista 180 sewing
machine which uses the H8/3003 CPU. It is to the point where it can run
the Artista 180 firmware dumped directly from an Artista 180.

## The simulated machine

- **CPU** — the full H8/300H instruction set in advanced mode (the only mode
  the H8/3003 supports): eight 32-bit general registers ER0–ER7 (each usable
  as E/R 16-bit halves and RH/RL 8-bit quarters; ER7 is the stack pointer),
  a 24-bit program counter, and the CCR (I UI H U N Z V C). All 62
  instruction types are implemented, including MOV in every addressing mode,
  8/16/32-bit arithmetic and logic, signed and unsigned multiply/divide,
  DAA/DAS, the full bit-manipulation set, shifts/rotates, Bcc/JMP/JSR/BSR/
  RTS, TRAPA/RTE, LDC/STC/ANDC/ORC/XORC, SLEEP and EEPMOV.
  MOVFPE/MOVTPE are treated as illegal, as on the real H8/3003.
- **Cycle counting** — per-instruction state counts from table A-1
  (advanced mode, on-chip two-state accesses).
- **Memory** — the 16-Mbyte (24-bit) address space is *sparse*: it is
  divided into 256 banks of 64 Kbytes, and a bank is only allocated the
  first time it is written. Unallocated memory reads as `H'00`. The memory
  view dims unallocated rows; the disassembly view sweeps only allocated
  regions and shows the gaps between them; the profiler's counters use the
  same banked scheme.
- **Exceptions** — the advanced-mode vector table at `H'000000` (32-bit
  entries): reset (vector 0), NMI (7), TRAPA #0–3 (8–11), IRQ0–7 (12–19).
  Exception entry pushes CCR:PC24 as one longword and sets the I bit; RTE
  restores it. The NMI and IRQ0 buttons on the control bar raise external
  interrupts; SLEEP halts the CPU until an interrupt wakes it.
- **On-chip RAM** — `H'FFFD10`–`H'FFFF0F` (512 bytes, pre-allocated), per
  the mode 3/4 memory map. The 8-bit absolute addressing mode (`@aa:8`)
  targets `H'FFFF00`–`H'FFFFFF` as on the real chip.
- **A/D converter** — simulated (manual section 14): registers at
  `H'FFFFE0`–`H'FFFFE9`, eight inputs AN0–AN7 multiplexed into the four
  result registers (AN0/AN4 to ADDRA, AN1/AN5 to ADDRB, and so on), 10-bit
  results left-justified, single and scan modes, the 266/134-state
  conversion time selected by `CKS`, and the ADI interrupt. Each input's
  voltage is settable, which is how the touch panel is driven.
- **DMA (DMAC)** — all four channels are simulated (manual section 8):
  registers at `H'FFFF20`–`H'FFFF5F`, short address mode (I/O and repeat
  modes, increment or decrement, byte or word) and full address mode
  (memory to memory, auto-request burst or per-trigger, with independent
  source and destination address control). Transfers fire on the programmed
  activation source — an ITU compare match, or an SCI transmit-empty or
  receive-full — and clear that source as the hardware does, so an
  SCI channel driven by DMA really moves bytes. `DEND` is raised when the
  count runs out and `DTIE` is set. Transfers happen between instructions
  rather than by stealing exact bus cycles.
- **Timers (ITU)** — all five channels are simulated (manual section 10):
  registers at `H'FFFF60`–`H'FFFF9F`, counters clocked at φ, φ/2, φ/4 or φ/8
  per `TCR`, compare match against `GRA`/`GRB` with the counter-clear modes,
  overflow, and the three interrupts per channel. External clock inputs
  (TCLKA–TCLKD) are not modelled, so a channel selecting one stays still.
- **Serial EEPROM** — the artista 180 keeps its settings in a small 24Cxx-
  style part hung off two port pins, and the firmware bit-bangs I2C at it:
  SDA on P4 bit 7, SCL on bit 6. There is no I2C controller in the chip, so
  every edge is a write to the port's data or direction register, and the
  model answers from the bus side: start and stop conditions, the control
  byte, the word address, page-buffered writes committed on the stop,
  current-address and sequential reads, and the acknowledge -- which it
  gives by pulling SDA down through the same external-pin layer a switch
  would use, since SDA is open-drain and the CPU's own pull-up holds it up
  otherwise. Off by default; see the EEPROM view below.
- **Serial (SCI)** — both channels are simulated (manual section 13):
  registers at `H'FFFFB0`–`H'FFFFB5` and `H'FFFFB8`–`H'FFFFBD`, transmit
  timing derived from `BRR` and the clock select, clear-only status flags,
  and the four interrupts per channel. Register values are mirrored into
  memory so the memory view and disassembler show what the CPU reads. The
  remaining on-chip peripheral registers are still plain memory.

## Using it

- **Memory view** — tap a byte to edit it or set a data breakpoint
  (pauses Run on any read/write of that address). The Go to button jumps
  anywhere in the 16-Mbyte space.
- **Disassembly view** — tap a row to set/clear an instruction breakpoint.
  A JSON symbol table (`{"label": address}`) can be loaded to show labels;
  a `<name>_sym.json` next to a loaded hex file is picked up automatically.
- **Screen view** — the machine's LCD: 320×240 pixels at 2 bits each, four
  grey levels, four pixels per byte with the most significant bits leftmost,
  80 bytes per scan line, 19200 (`H'4B00`) bytes in all. The panel is
  positive-mode, as the real machine is: a stored 0 leaves the cell undriven
  and shows as the light background, 3 is full black, so a blank buffer
  renders as a blank screen. The Invert button renders it the other way up.
  The frame buffer defaults to `H'040000` and can be pointed elsewhere from
  the header, since the SED1351's start-address registers can move it. The
  panel repaints every Run tick, independently of the throttled memory and
  disassembly views.

  **Clicking the panel presses the touch screen.** The artista 180 reads a
  4-wire resistive panel through the A/D converter — the X axis on AN4 and
  the Y axis on AN6, using the top eight bits of each conversion — so a
  click feeds those two channels and the firmware sees a finger. Dragging
  moves the touch; releasing drives both channels to zero, which is what the
  firmware reads as no touch (it ignores anything below `H'4C`). The strip
  under the panel shows the readings being injected, and **Calibrate** sets
  the endpoints of the linear pixel-to-reading map — the panel's real
  calibration is not known, so those are adjustable rather than guessed at.
- **SCI view** — the two serial channels. Shows `SMR`, `BRR`, `SCR`, `TDR`,
  `SSR` and `RDR` with their addresses and values, the `SCR` and `SSR` bits
  as labelled flags, the framing and bit rate the registers currently
  describe, and a hex dump of every byte the program has put on the wire.
  The SCI is simulated rather than faked: `TDRE` and `TEND` follow a real
  character time computed from `BRR` and the clock select, the status flags
  are clear-only as the hardware makes them, and the channel raises
  ERI/RXI/TXI/TEI when its interrupt enables are set — so a polled transmit
  loop completes instead of spinning, and the bytes show up here.
- **ITU view** — the 16-bit integrated timer unit. Shows the shared registers
  (`TSTR`, `TSNC`, `TMDR`, `TFCR`, `TOER`, `TOCR`) with the per-channel start
  bits, then a card for each of the five channels: whether it is running, its
  clock source and counter-clear source, the live `TCNT` with its `GRA`/`GRB`
  targets (and `BRA`/`BRB` on channels 3 and 4), a progress bar towards the
  GRA compare match, the raw `TCR`/`TIOR`/`TIER`/`TSR`, and the status and
  enable flags. The timers really count: `TCNT` advances at the prescaler
  rate selected in `TCR`, compare matches set `IMFA`/`IMFB` and can clear the
  counter, overflow sets `OVF`, and each channel raises its IMIA/IMIB/OVI
  interrupts when `TIER` enables them.
- **DMA view** — the four DMA controller channels. Each card shows the mode
  (short or full address), whether it is enabled, and the live state: in full
  address mode the source and destination addresses with a progress bar over
  the transfer count; in short address mode each half's memory address, its
  fixed I/O address and the direction, its activation source and mode, and
  the DTE/DTIE flags. The header counts transfers performed.
- **IO view** — a graphical picture of the GPIO ports (4–9, A–C, plus the
  input-only port 7): one row of abutting bit boxes per port, bit numbers
  above. Bits configured as outputs in the port's DDR are red with the
  driven DR value (0/1) inside; input bits are green; positions without a
  pin are dimmed. Register values are read live from the on-chip register
  addresses (`H'FFFFC5`…`H'FFFFD7`), which a reset initializes to the
  manual's mode 3/4 values.
- **EEPROM view** — the serial EEPROM on port 4. Switch the model on and the
  device answers; leave it off and both pins are ordinary port bits, which
  is how the simulator behaved before this existed.

  The array is kept in a **JSON file**, `eeprom.json` by default, chosen with
  Browse. It is written back whenever the machine commits a write, so what
  one session put in is there in the next; it is meant to be read and edited
  by hand, sixteen bytes to a row in hex, and a flat `{"bytes": {"A9": "3C"}}`
  map is accepted as well for when only an address or two matters. A file
  that is not there yet is not an error — the part starts blank and the file
  appears on the first write — but a file that is there and cannot be parsed
  stops the model coming on, rather than being overwritten with a blank
  array.

  The view shows the contents as a hex dump, the addresses the firmware is
  known to use with what they mean and their values decoded, the live bus
  state and address counter, and a log of what has gone past. Booting the
  artista 180 firmware fills it in: `H'A9` and `H'AA` are the two end stops
  of the presser-foot lift, written from the configuration block in flash at
  start-up and again by the handwheel trim, which adds `H'28` before storing.

  One thing the log makes plain: each write is followed by a read of the
  address *after* the one written. The firmware's write-and-verify reads back
  through the device's own address counter, and a real part leaves that one
  past the byte just written — so the check compares the wrong byte and says
  no, which is why none of its twelve callers looks at the answer. There is a
  tick-box to leave the counter where the verify expects it, for when that
  matters more than matching the datasheet.
- **Profile view** — switch the profiler on (top bar), Run, and see the
  hottest data addresses and instructions.
- **Watch view** — two questions a breakpoint cannot answer. A breakpoint
  says "stop here"; these say **stop when this is true** and **who wrote
  that**. Between them they replace most of what `tool/` used to be: a
  program written again with a different test compiled into it.

  **Stop when** takes a condition and pauses Run the moment it holds,
  *before* the instruction it holds at, so "stopped" means "about to execute
  this". Numbers are hex, because that is how every address in this machine
  is written everywhere else; a leading `#` makes one decimal.

  ```
  [11B10E].w == 77            the panel is asking for clr
  pc >= 208E7A && pc < 208F00 execution has entered key_scan
  [FFFFC7] & 4                the sewing light has come on
  er0 != 0 && [200004].l == 3F0000
  cycles > #25000000          a fixed distance into the boot
  ```

  `[addr]` reads a byte, `.w` a word and `.l` a long, big-endian as the H8
  is; the address can be worked out (`[11B10E + 1]`). Registers are read by
  name — `pc`, `ccr`, `cycles`, `sp`, `er0`-`er7`, `r0`-`r7`, `e0`-`e7` and
  the flag letters. `&&` and `||` short-circuit, so a condition can guard
  its own reads. A condition that will not parse says why under the field
  rather than in a snack that has gone by the time you look back at it, and
  one that cannot be evaluated at run time stops rather than throwing out of
  the run loop. Resuming runs the instruction it stopped on before asking
  again, so Run does not sit on the same instruction for ever.

  **Record writes to** takes an address or a range (`11B10E-11B10F`) and
  logs every write that lands in it: the cycle count, the address of the
  instruction that did it — its own address, not wherever the fetch had
  reached — the address written, and what was displaced. A write that
  changed nothing is marked as such, because the firmware rewrites the same
  value constantly and a log that does not say so is mostly noise. The log
  is capped and shows newest first. Both the condition and the watch list
  are saved to `~/.h8simrc` with the rest of the session.
- **Responsiveness while running** — Run executes in short batches inside a
  16 ms frame timer with a time budget, so the window stays interactive and
  Pause takes effect immediately. The disassembly is only re-swept when the
  contents of memory actually change (an edit, a file load, the demo), not on
  every repaint: sweeping a loaded firmware image is a ~400 ms job over
  roughly two million instructions, and doing that each frame is what makes
  an emulator feel hung. If a program writes code at run time, the
  Disassembly pane's **Re-scan** button forces a fresh sweep.
- **Files** — loads **Intel HEX** (with type 02/04 extended addressing),
  **Motorola S-records** (S1/S2/S3, with S7/S8/S9 entry points) and **flat
  binaries** such as raw memory dumps. The format is detected from the file's
  contents, so there is nothing to select. A binary carries no address of its
  own, so the app asks where to put it — a file exactly the size of the
  address space is recognised as a full dump and offered at 0. All-zero 64K
  blocks are skipped rather than allocated, which keeps a 16-Mbyte dump from
  reserving every bank. Saves allocated memory as Intel HEX. On desktop, a
  file named on the command line is loaded at startup (binaries at 0):

  ```bash
  flutter run -d macos --args dump.bin
  ```
- **Back — stepping backwards.** Switch on *Keep history* in Settings and
  the **Back** button walks the machine backwards an instruction at a time.
  It is enabled while the CPU is halted, unlike Step: sitting on a fault and
  wanting to see what led to it is the reason it exists.

  Most H8 instructions change one register and the flags and touch no memory
  at all, so a record is small. It is kept as flat words rather than an object
  per instruction, which works out at about **25 bytes an instruction** —
  a million steps of history in 24 MB, where an object apiece would be six
  times that. The depth is chosen in Settings, which also shows how many
  instructions are held and what they are costing.

  The journal alone cannot put back a *peripheral*: a timer counts into its
  own counter rather than working it out from the clock, so winding the clock
  back leaves the timer where it was — and a machine that looks right and
  then takes an interrupt that never happened is worse than no rewind at all.
  So the machine's state outside memory, which is small, is also kept every
  128 instructions. A step back winds memory and the registers to the last
  kept point, puts that state back, and replays the few instructions in
  between. What comes out is the machine as it actually was, peripherals
  included, and stepping forward again retraces the same run exactly.

  When the history has been trimmed past the last kept state, a step back
  over a peripheral write says so rather than quietly being approximate.

  Recording makes the machine run about **1.4× slower** (measured over five
  million instructions of the artista 180 boot), so it is off unless asked
  for — in Settings, or `"history": {"enabled": true}` in `~/.h8simrc`.
- **Registers** — tap any register or CCR flag to edit it.

## Comparing two images

`tool/lockstep.dart` runs two machines side by side and stops at the first
instruction where they part company. It is the whole-machine complement to
`compare_routines`, which does the same thing one routine at a time.

```bash
dart run tool/lockstep.dart old.bin new.bin --compare state
```

Three things can be compared, and which one to use depends on how alike the
two images are meant to be:

- **`state`** (the default) — the PC, the registers and the flags. A
  difference is caught at the instruction that *caused* it rather than at the
  branch that later revealed it.
- **`pc`** — the code only. Right when the two images genuinely differ
  somewhere you do not care about; `--settle N` and `--ignore FROM:TO` step
  over it.
- **`writes`** — not the code at all, only what each side writes to a
  `--watch` range, in order. Two implementations of the same routine make the
  same writes however they get there, so this is the comparison that survives
  a rewrite.

That last one is the answer to "does the C version behave the same". The
rebuilt application and the original paint the screen identically:

```bash
dart run tool/lockstep.dart original.bin mergedapp.bin \
    --compare writes --watch 040000:044AFF
```

```
In step for all 57600 writes compared.
Both sides then stopped writing, having agreed about everything either of
them wrote (6055990 and 5887647 instructions run).
```

Three full screen paints, byte for byte, through a different instruction
stream and about 3% fewer instructions.

Comparing an image against **itself** with a key held on one side is how to
ask what a button actually does — the answer is wherever the two runs first
disagree:

```bash
dart run tool/lockstep.dart dump.bin dump.bin --hold-b 77
```

```
Parted company at step 1902064, on er5
  002507FF against 002507FB

  H'208E8C  MOV.B @H'060000:24,R5L
  H'208E92  NOT.B R5L
```

which is the key-matrix return latch, read by the key scanner — found without
knowing where the scanner was.

A report says how it ended as well as what it found: a run that reached its
instruction limit has not proved anything past that limit, and it says so
rather than reading like agreement.

## The built-in demo

On startup (and via the reload button) a small program is assembled at
`H'000100` with its reset vector at `H'000000`. It initialises SP, sums
1..10 into R0L (= 55 = `H'37`), stores the result at the start of the
on-chip RAM (`H'FFFD10`, allocating that bank on the way), drives it out
on port 4 (watch the IO tab), and executes SLEEP:

```
000100  7A 07 00 FF FF 00   MOV.L  #H'00FFFF00,ER7   ; SP
000106  F8 00               MOV.B  #H'00,R0L         ; sum = 0
000108  F9 01               MOV.B  #H'01,R1L         ; i = 1
00010A  08 98        loop:  ADD.B  R1L,R0L
00010C  0A 09               INC.B  R1L
00010E  A9 0B               CMP.B  #H'0B,R1L
000110  46 F8               BNE    loop
000112  6A A8 00 FF FD 10   MOV.B  R0L,@H'FFFD10:24
000118  F1 FF               MOV.B  #H'FF,R1H
00011A  31 C5               MOV.B  R1H,@H'FFFFC5:8   ; P4DDR: all outputs
00011C  38 C7               MOV.B  R0L,@H'FFFFC7:8   ; P4DR: the sum
00011E  01 80        done:  SLEEP
000120  40 FC               BRA    done
```

## Building

```
flutter pub get
flutter test          # CPU core, disassembler and hex-file unit tests
flutter run -d macos  # or linux, windows, chrome, an Android/iOS device
```

On macOS the app sandbox is deliberately switched off in both
`macos/Runner/DebugProfile.entitlements` and `Release.entitlements`, with
the user-selected file entitlements granted — the same arrangement as
sim_6502. The file picker needs those entitlements to open at all, and two
other features need access beyond the single file the user picks: loading a
hex file named on the command line, and auto-loading the `<name>_sym.json`
symbol table sitting beside a loaded hex file. This rules out Mac App Store
distribution, which the app is not aimed at.

## Code layout

| File | Contents |
| --- | --- |
| `lib/h8300h.dart` | The H8/300H CPU core: registers, bus, decode/execute, exceptions, cycle counting |
| `lib/h8disasm.dart` | The disassembler (Renesas syntax; agrees with the core on lengths) |
| `lib/sparse_memory.dart` | The banked 16-Mbyte sparse memory and sparse profiling counters |
| `lib/hex_files.dart` | Intel HEX / S-record parsing and Intel HEX generation |
| `lib/lcd.dart` | Frame buffer decoding: 320×240, 2 bits per pixel, MSB first |
| `lib/sci.dart` | The serial channels: registers, character timing, interrupts |
| `lib/itu.dart` | The 16-bit timer unit: five channels, prescalers, compare match |
| `lib/dmac.dart` | The DMA controller: short and full address modes, activation sources |
| `lib/adc.dart` | The A/D converter: eight inputs, conversion timing, ADI |
| `lib/flash.dart` | JEDEC flash devices on the external bus: unlock sequences, program, erase |
| `lib/i2c_eeprom.dart` | The bit-banged serial EEPROM on port 4, and the JSON file it lives in |
| `lib/condition.dart` | The stop-condition language: lexer, parser and evaluator, with no CPU in it |
| `lib/sim_config.dart` | `~/.h8simrc`: the settings a session needs before it is any use |
| `lib/snapshot.dart` | Whole-machine save and restore, EEPROM included |
| `lib/main.dart` | The Flutter UI (register panel, memory/disassembly/screen/IO/profile views, controls) |
| `test/h8cpu_test.dart` | Unit tests with hand-assembled instruction encodings |
| `lib/lockstep.dart` | Two machines run side by side, stopped where they part company |
| `lib/undo.dart` | What each instruction changed, kept so it can be put back |
| `tool/lockstep.dart` | The whole-machine comparison: two images, the first instruction they disagree on |
| `tool/compare_routines.dart` | Runs every case in a spec: one routine on both images, results and memory compared |
| `tool/trace_case.dart` | One case's call sequence on both images, with arguments and a watched range, to find where two runs part company |
| `tool/make_icons.py` | Regenerates the app icon and every platform variant |

## The icon

The icon matches the sim_6502 one so the two simulators read as a family: a
black DIP package with silver pins, a top notch, a pin-1 dot and the part
number across the body in heavy sans (with the same dotted zeros). The
background is blue rather than green, matching this app's accent colour and
keeping the two easy to tell apart in a dock or launcher.

It is generated, not hand-drawn — `tool/make_icons.py` (Python + Pillow)
writes the 1024px master to `assets/icon/app_icon_1024.png` and every
platform variant: the macOS and iOS icon sets, the Android mipmaps, the web
favicon and PWA icons (including maskable versions with a safe margin), and
the multi-size Windows `.ico`. Rerun it after changing the artwork:

```bash
python3 tool/make_icons.py
```
