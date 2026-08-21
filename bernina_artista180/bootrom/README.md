# Boot ROM, rewritten in C

Reconstruction of the artista 180's boot ROM (`H'000000`–`H'002FFF` in the
original) as C, built with the `h8300-elf` GCC toolchain.

    make            # -> bootrom.bin, and the mergebootrom host tool

## Trying it

`mergebootrom` puts a compiled ROM into a full memory dump, which is what the
simulator loads:

    ./mergebootrom -b bootrom.bin -m ../Bernina180_20260816.bin -o merged.bin

Two details there are not a plain `memcpy`, and getting either wrong leaves an
image that looks right and behaves oddly:

* The boot flash is a 32K device. Everything past the new ROM is erased to
  `H'FF`, so a ROM shorter than the one it replaces cannot leave the tail of
  the old one behind for a stale pointer to find.
* Only the low fifteen address lines reach that device, so its contents
  repeat every 32K up to `H'020000`. All four copies are written.

## Status: complete

The rebuilt ROM boots the original application to a **pixel-identical**
screen (0 of 76,800 differ) and answers the download protocol **byte for
byte** — 19 of 19 commands, with and without the simulator's flash devices
attached.

Nothing is stubbed. All nineteen protocol commands, the host handshake, the
flash driver and the vector-slot routines are reconstructed.

## The low vector slots are an ABI

The application calls boot ROM routines through `JSR`/`JMP @@aa:8`, a
memory-indirect call that reads its target from the low 256 bytes — the
vector table. The call sites are a thunk block at `H'250AE8`–`H'250AF4`.

| slot | original | routine |
| --- | --- | --- |
| 1 | `H'001090` | the serial service, and the whole download protocol |
| 2 | `H'000426` | the delay loop |
| 3 | `H'000ADC` | write a block of any length into flash |
| 4 | `H'002122` | bridge the two serial ports back to back |
| 5 | `H'0022FE` | select serial port 0 |
| 6 | `H'002312` | is port 0 selected? |
| 22 | `H'002328` | the flag `'Y'` raises |

The application is the untouched original binary, so it calls these the
original ROM's way: first argument in `ER6`, any others on the stack, result
back in `R6`. GCC uses `ER0`/`ER1`/`ER2` and returns in `R0`, so each slot has
a short assembly shim in `vectors.S` that translates between the two. Only
slot 1 is reached while the machine runs normally.

The table's shape matters too: trampolines occupy vectors 7-21, 24-26, 28-30,
32-34, 36-38, 40-42 and 44-60 — 47 of them, with reserved gaps at 23, 27, 31,
35, 39 and 43, and vector 22 holding a utility rather than a trampoline.

## Two things C cannot be sloppy about

Both cost real bugs during the rebuild, and neither shows up without a
hardware model to catch it:

* **Status flags must be cleared by writing a constant**, never by
  `SSR &= ~mask`. The SCI clears a flag when software writes 0 and ignores
  writes of 1, so a read-modify-write also clears any flag that *became* set
  between the read and the write-back. A character finishing arrival in that
  window is lost.
* **Flash accesses must be `volatile`.** The unlock sequence writes `H'AA`
  and then the command byte to the same address; without `volatile` the
  compiler sees a dead store and drops the `H'AA`, so the device never leaves
  read mode and every identify, erase and program silently does nothing.

## Bugs found in the original ROM

Reproduced as they stand, since this is a reconstruction, but worth fixing
before building on it:

* `H'000EBC` (the Atmel `H'13` download path) writes every byte of a page to
  the same address — the destination is never advanced inside the loop. That
  path cannot ever have worked.
* Ending an `'M'` edit immediately after a page boundary re-commits the wrong
  page, overwriting the page just written with a copy of the next one.
* `flash_identify`'s manufacturer tests fall through into the device tests
  below them, so identify codes 6 and 7 can never be returned.
* The serial bridge's escape matcher compares its counter against `E6`, which
  it never initialises. If `E6` arrives non-zero the escape can never start
  matching. Written as a comparison against zero here, which is the evident
  intent.
