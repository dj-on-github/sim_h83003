# Boot ROM, rewritten in C — work in progress

Reconstruction of the artista 180's boot ROM (`H'000000`–`H'002FFF`) as C,
built with the `h8300-elf` GCC toolchain.

    make            # -> bootrom.bin (1638 bytes)

## Status: boots the original application to a matching screen

Verified by splicing `bootrom.bin` over an erased `H'000000`–`H'002FFF` in a
full memory image and running it in the simulator against the untouched
application flash:

| | original | rebuilt |
| --- | --- | --- |
| serial greeting | `BOSN` | `BOSN` |
| hands over to application | instruction 698,493 | instruction 25,986 |
| application still running at 25M | yes, PC `H'000200` | yes, PC `H'20A114` |
| frame buffer | 6,818 non-zero bytes | 6,827 |
| screen difference | — | **12 pixels of 76,800 (0.02%)** |

The twelve pixels are the blinking element in the display, caught at a
different point in its cycle. The handover happens sooner because the
handshake below is stubbed rather than performed.

## The low vector slots are an ABI

The application calls boot ROM routines through `JSR`/`JMP @@aa:8`, a
memory-indirect call that reads its target from the low 256 bytes — the
vector table. The call sites are a thunk block at `H'250AE8`–`H'250AF4`, at
the end of the application code:

| slot | vector | routine |
| --- | --- | --- |
| `H'04` | 1 | `H'001090` |
| `H'08` | 2 | `H'000426` — the delay loop |
| `H'0C` | 3 | `H'000ADC` |
| `H'14` | 5 | `H'0022FE` |
| `H'58` | 22 | `H'002328` |

Only slot 1 is used while the machine runs — 14,750 calls in 25M
instructions, all from `H'250AE8`. Leaving these slots at zero sends the
application to address 0 the first time it uses one.

The table's shape matters too: trampolines occupy vectors 7-21, 24-26,
28-30, 32-34, 36-38, 40-42 and 44-60 — 47 of them, with reserved gaps at 23,
27, 31, 35, 39 and 43, and vector 22 holding a utility rather than a
trampoline.

## Still stubbed

**`H'001090`'s command dispatch.** `serial_service()` implements the two
entry guards faithfully — the transmitter must be free and the line
error-free — but not the 240-entry dispatch on `H'FFFD1E` over about 120
handlers at `H'0014AE`-`H'002031`. That is the host download protocol and
most of the remaining boot ROM. With nothing connected the state stays 0 and
there is nothing for it to do, which is why the machine boots without it. A
firmware update over serial would not work.

**`H'000FB6`** — 218 bytes on the boot path whose result decides whether
the machine starts the application. `host_handshake()` in `boot.c` stubs it
as "no host present", which reaches the right branch but skips whatever
state the real routine leaves behind.

## What is reconstructed

`boot.c`: the serial API (send, read, blocking read, error clearing and NAK,
the ready-bit accessors), the delay loop, bus controller and SCI
initialisation, the boot sequence and the handover.

`vectors.S`: the vector table and all 54 interrupt trampolines. These cannot
be C — each builds a `JMP @aa:24` instruction in on-chip RAM out of the
handler address fetched from the application's table at `H'200000`, then
jumps to it, so that the handler is reached with `ER0` restored. The
trampoline encoding assembles byte-for-byte identically to the original.
