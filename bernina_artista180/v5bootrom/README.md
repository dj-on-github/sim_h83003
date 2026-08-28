# v5 boot ROM — a permanent page 0, and an updater that cannot brick

A boot ROM for the artista 180 that can be reprogrammed in the field without
ever rewriting the page the machine boots from, and the updater that does it.
Both are tested in the simulator, including the failure cases.

    make        # -> v5boot.bin (the whole 32K device), updater.bin, imageN.bin

The code is the V2 reconstruction from `../v2bootrom` unchanged except where
noted; what is new is the layout around it.

## The problem

The boot flash is a 32K Atmel AT29C-style part on bus area 0, separate from
the 2M application device on area 1. It is programmed 256 bytes at a time
with an auto-erase page write — no bulk erase, each page one self-contained
operation. That part is good news.

The bad news is that the stock ROM cannot use it on itself. `'M'` and the
`'P'` downloader both take an address from the host and **neither checks
it**, so both will aim a page-program at bank 0. The moment the command
latches, the device answers reads with toggling status instead of data, and
the CPU is fetching its next instruction from that device.

`tool/boot_selfwrite.dart` drives exactly that, with the simulated part busy
for 400 reads after each write:

| `'M'` aimed at | pages written | CPU halted | boot ROM bytes changed |
| --- | --- | --- | --- |
| `H'200000`, the application device | 1 | no | 0 |
| `H'000000`, its own device | 1 | **yes** | **4, starting at H'000000** |

The four bytes are the reset vector. The machine writes over its own reset
vector and then derails — 2,097,027 distinct PCs before it stops. This is
deterministic, not bad luck.

## The layout

```
H'000000-H'0000FF   page 0, the vector table          PERMANENT
H'000100-H'0003FF   the interrupt trampolines         PERMANENT
H'000400-H'0007FF   stage-0 and its thunks            PERMANENT
H'000800-H'003FFF   image slot A
H'004000-H'007FFF   image slot B
```

An update writes the inactive slot and nothing else. There is no moment at
which the machine has no bootable ROM.

Each slot starts with a 64-byte header: magic `"B180"`, a generation number,
the body length, a checksum over the body, the entry point, and seven export
addresses. Stage-0 validates both slots and jumps to the valid one with the
higher generation.

**There is no selector byte and no "active slot" flag**, which is the point:
an update is "write the other slot with a generation one higher", and if it
is interrupted the half-written slot fails its checksum and stage-0 boots the
one that was already there. Nothing has to be committed afterwards.

### Why page 0 can be permanent

Slots 1-6 and 22 of the vector table hold boot ROM routine addresses, which
the application calls through `JSR @@aa:8`. Those are image-relative, so page
0 could not hold them directly. Instead they point at **thunks** in the
permanent block, each of which loads the active image's base from
`H'FFF710` — published by stage-0 — and indirects through the header's
export table. One extra indirection; ER0 is the only register touched and it
is restored before the target runs, so the ROM calling convention passes
through untouched.

The 47 interrupt trampolines need no thunking at all: each already fetches
its handler from the application's own table at `H'200000`. That is why they
are byte-for-byte identical between V2 and V3.

Three longwords of on-chip RAM are reserved: `H'FFF710` the active base,
`H'FFF714` the thunks' scratch, `H'FFFD0C` the loader's own entry (so `'X'`
can re-run the image selection). Booting the original application writes
nothing below `H'FFFD1A` — 1546 bytes of the 2K are never touched — so these
are clear of both the stack, which grows down from `H'FFFDF4`, and the state
block the ROM and application share.

## The updater

`updater.c` runs **from the application flash**, not from RAM. Area 0 and
area 1 are different devices, so while the boot flash is busy the CPU keeps
fetching instructions, and the new image, out of area 1. That is simpler than
a RAM-resident updater and leaves 97K of spare application flash to work in
rather than 1.8K of on-chip RAM.

It is reached the way anything is: it sits at `H'3E8000`, the entry longword
at `H'200004` is pointed at it, and `'G'` jumps there. Getting it into the
application flash in the first place is the ROM's ordinary `'P'` path, which
is safe because it targets the other device.

Interrupts are masked throughout — the vector table is in area 0, so a vector
fetch during a page write would read status too.

**The body is written first and the header page last**, so a slot goes from
"not an image" to "the new image" in a single 256-byte page write.

## What was tested

    dart run tool/boot_selfwrite.dart <image.bin> --target 200000|000000
    dart run tool/boot_update.dart <v5image.bin> <this-dir> [--cut N] [--out F]

* v5 as built answers **19 of 19 protocol commands** byte for byte against
  the original V2 ROM's fixture, with and without flash devices attached, and
  boots the application to a **pixel-identical** screen (0 of 76,800).
* A complete update writes 21 pages, verifies identical, and after a reset
  the machine runs image A generation 3 — `'I'` now reports `BiosVersion:
  1.11`, and the screen is still pixel-identical.
* An update interrupted after 1, 5, 10, 15 or 20 of its 20 body pages leaves
  the machine booting **image B generation 2**, the image it started on,
  every time.

## Three traps this ran into

Worth recording, because all three were silent.

**`BSR` with an 8-bit displacement wraps.** It reaches −128..+127 and gas
does not diagnose an overflow — it truncates. Adding fourteen bytes to
`_stage0` pushed the second call to `_validate` to −138, so it went to
`H'0005BA` instead of `H'0004BA`, image B failed to validate, the machine
booted image A, and the only symptom was a different screen. Both calls now
say `:16` explicitly.

**The linker chooses which function lands at ORIGIN.** `updater_main` was not
first in `.text`; `program_page` was. `'G'` jumped into the middle of the
page writer with whatever was in the argument registers, which wrote pages of
rubbish over the *other* slot and left the machine with no valid image at
all — the one thing this design exists to prevent, caused by section
ordering. `updater_main` is now pinned to `.text.entry`, placed first.

**Two images built from the same source have the same body.** Only the header
differed, so a half-written slot still checksummed correctly and the
power-cut test passed for the wrong reason. The image an update installs now
carries a different banner, so its body genuinely differs — and that doubles
as the way to see over the wire whether the update took.

## Not covered here

NMI is non-maskable. Slot 7 holds `H'000108`, inside the trampoline block, so
it is not a real handler and is almost certainly tied inactive — but if
anything does assert it, a vector fetch during a page write reads status from
a busy device. This is being checked on the CPU board.

The simulated flash does not model the *other* consequence of a busy device:
`lib/flash.dart` gates reception on RDRF alone, so a set ORER does not stop
the receiver the way it does on real hardware. That affects the V2 handshake
analysis in `../v2bootrom/README.md`, not this layout.
