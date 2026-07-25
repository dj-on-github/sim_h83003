# sim_h83003 — an H8/3003 Simulator

An interactive simulator for the Hitachi/Renesas **H8/3003** microcontroller
(H8/300H CPU core, advanced mode), built with Flutter for desktop, mobile and
web. It follows the same scheme as the companion `sim_6502` application: a
live register panel, a hex memory view, a disassembler, a profiler, and
Pause / Step / Run controls with breakpoints.

Reference: *Hitachi H8/3003 Hardware Manual* (REN_e602055_h83003), section 2
(CPU) and appendix A (instruction set).

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
  targets `H'FFFF00`–`H'FFFFFF` as on the real chip. On-chip peripheral
  registers (`H'FFFF1C`–`H'FFFFFF`) are plain memory for now — GPIO and
  serial interfaces can be layered on in a later step.

## Using it

- **Memory view** — tap a byte to edit it or set a data breakpoint
  (pauses Run on any read/write of that address). The Go to button jumps
  anywhere in the 16-Mbyte space.
- **Disassembly view** — tap a row to set/clear an instruction breakpoint.
  A JSON symbol table (`{"label": address}`) can be loaded to show labels;
  a `<name>_sym.json` next to a loaded hex file is picked up automatically.
- **Profile view** — switch the profiler on (top bar), Run, and see the
  hottest data addresses and instructions.
- **Files** — loads **Intel HEX** (with type 02/04 extended addressing) and
  **Motorola S-records** (S1/S2/S3, with S7/S8/S9 entry points) — the
  formats H8 toolchains emit. Saves allocated memory as Intel HEX. On
  desktop, a file named on the command line is loaded at startup.
- **Registers** — tap any register or CCR flag to edit it.

## The built-in demo

On startup (and via the reload button) a small program is assembled at
`H'000100` with its reset vector at `H'000000`. It initialises SP, sums
1..10 into R0L (= 55 = `H'37`), stores the result at the start of the
on-chip RAM (`H'FFFD10`, allocating that bank on the way), and executes
SLEEP:

```
000100  7A 07 00 FF FF 00   MOV.L  #H'00FFFF00,ER7   ; SP
000106  F8 00               MOV.B  #H'00,R0L         ; sum = 0
000108  F9 01               MOV.B  #H'01,R1L         ; i = 1
00010A  08 98        loop:  ADD.B  R1L,R0L
00010C  0A 09               INC.B  R1L
00010E  A9 0B               CMP.B  #H'0B,R1L
000110  46 F8               BNE    loop
000112  6A A8 00 FF FD 10   MOV.B  R0L,@H'FFFD10:24
000118  01 80        done:  SLEEP
00011A  40 FC               BRA    done
```

## Building

```
flutter pub get
flutter test          # CPU core, disassembler and hex-file unit tests
flutter run -d macos  # or linux, windows, chrome, an Android/iOS device
```

## Code layout

| File | Contents |
| --- | --- |
| `lib/h8300h.dart` | The H8/300H CPU core: registers, bus, decode/execute, exceptions, cycle counting |
| `lib/h8disasm.dart` | The disassembler (Renesas syntax; agrees with the core on lengths) |
| `lib/sparse_memory.dart` | The banked 16-Mbyte sparse memory and sparse profiling counters |
| `lib/hex_files.dart` | Intel HEX / S-record parsing and Intel HEX generation |
| `lib/main.dart` | The Flutter UI (register panel, memory/disassembly/profile views, controls) |
| `test/h8cpu_test.dart` | Unit tests with hand-assembled instruction encodings |
