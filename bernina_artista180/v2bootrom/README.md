# V2 boot ROM, rewritten in C

Reconstruction of the artista 180's **V2** boot ROM — the one in
`../MemoryDump-SewingMachine-NMMV02.08-2026-08-26_20-18-31.bin`, which
identifies itself as `BiosVersion: 1.10 / Mai 97` — as C, built with the
`h8300-elf` GCC toolchain. It is a copy of `../bootrom` (the V3.01 / `1.20`
reconstruction) with the V2 differences applied.

    make            # -> bootrom.bin, and the mergebootrom host tool
    ./mergebootrom -b bootrom.bin \
        -m ../MemoryDump-SewingMachine-NMMV02.08-2026-08-26_20-18-31.bin \
        -o merged.bin

## Status: complete

* All **19 protocol commands answer byte for byte**, with and without the
  simulator's flash devices attached (`tool/protocol_probe.dart`, fixture in
  `protocol_v2.json`).
* The rebuilt ROM boots the V2 application to a **pixel-identical** screen —
  0 of 76,800 differ.
* Nothing is stubbed.

## Where the ROM lives

`H'000000`–`H'0022FF`, 8,960 bytes, mirrored at `H'008000`, `H'010000` and
`H'018000` — a 32K device with only the low fifteen address lines connected,
exactly as in V3. V3's ROM is 9,216 bytes. The extra 256 bytes are the SCI0
support V2 does not have.

A raw byte comparison against V3 is useless — the two are only 20.5%
identical, because everything past the vector table sits at a different
address. Comparing *shapes* instead (opcode sequences with absolute addresses
normalised away) shows what is really going on: of 45 entry points, **39 are
instruction-for-instruction identical to V3** and the rest differ only in the
ways listed below.

## The protocol is V3's protocol

This was the question worth answering, and the answer is that the command
layer is not where V2 and V3 differ:

* The same 19 command letters, `GHIJKLMNPRSTVWXYZrw` at `H'00140F`, with the
  same 19 handler addresses following at `H'001422`.
* The same **backwards** indexing — `letter[i]` is dispatched through
  `addrs[18-i]`.
* The same state constants (`01` read, `09` write, `28` baud digit, `32`
  version, `3C` reset, `50` identify, `5A` download, `79` flash byte, `8C`
  modify, `A0` go, `B4` confirm, `BE` bridge, `C8` checksum, `E6` halt,
  `F0` dump).
* The same dispatcher: a 247-entry jump table indexed by the state byte at
  `H'FFFD1E`, capped at `H'F6`.
* The same announcement (`"BOS"`), the same handshake characters (`"EB"`),
  the same default rate (BRR `H'11`, 19200 8N1), the same `'Q'` for an
  unknown command and `'!'` for a line error.

The whole handler region — 813 instructions, every command and the shared
protocol tail — differs from V3 in exactly **three string addresses and one
`BSR`**. The bridge routine is identical. The vector table has the same six
occupied slots and the trampoline block is byte-for-byte the same, so the
ABI the application calls through is unchanged.

Empirically: `host/artista180_burn_application`, written against V3, burned
206,492 bytes into this ROM over the simulated serial link, the ROM's own
checksum came back `H'00FD5AA5` as expected, and the flash landed
byte-identical to `app.bin`. **No change to the host tool was needed.**

## What actually differs

Four things, and three of them are the same thing.

### 1. There is no SCI0 path

V3 brings both channels up and its handshake polls both in turn — SCI0
silently, SCI1 with an echo — leaving whichever channel completed the match
selected in `CHAN_SELECTION`, so the handshake is also what decides which
port the download protocol runs on.

V2 has only the SCI1 half:

* `hardware_init()` calls `sci_init_channel(0)` and stops. V3 follows it with
  `sci_init_channel(1)`. The two routines that would do it — `H'000718` and
  `H'000734` in V3 — are **not present** in V2 at all.
* `host_handshake()` never touches `CHAN_SELECTION` and never looks at SCI0.
* SCI0 is left at its reset state, receiver and transmitter both disabled.

Everything *below* the handshake is still channel-aware in V2: the read at
`H'0004EE`, the bit-rate change at `H'00064E` and the error check at
`H'00049A` all still switch on bit 1 of `CHAN_SELECTION`. Only the two places
that would ever *select* SCI0 are gone.

Since both ROMs clear the channel bit at init, both announce on SCI1, so this
only bites a host that talks blind. V3's SCI0 arm matches `"EB"` **silently**,
so a tool that simply sends `"EB"` at the machine without waiting for `"BOS"`
will capture a V3 machine on SCI0 and then run the entire protocol there. The
same tool gets nothing from V2.

### 2. Line errors are not cleared during the handshake

V3's handshake calls `serial_clear_rx_errors()` on every round in which
nothing is waiting — that is the standalone routine at `H'0004EE` in V3,
whose only two callers are those two arms. V2 has neither the calls nor the
routine; the loop just goes round again.

This is the difference most likely to bite in the field, because a latched
receive error is the *ordinary* state of a serial line at power-up: the
machine comes out of reset into whatever the host's transmitter happened to
be doing, and a break or a partial character sets FER or ORER before the host
has said anything deliberate.

`tool/handshake_error.dart` latches ORER inside the handshake window and
watches what each ROM does with it:

| | V3 | V2 |
| --- | --- | --- |
| answers the host | `EBM` | `EBM!` |

V2 answers, but with a stray `'!'` — H'21, the NAK from `serial_rx_error_check`
at `H'00049A`, which is the first thing that clears the error, and it does so
from *inside the service loop* rather than during the handshake. V3 has
already cleared it silently and sends nothing extra. A host that expects
exactly `EBM` reads one byte too few and is out of step for everything after.

One caveat, stated plainly: the simulator's SCI does not gate reception on
ORER — `lib/sci.dart` checks only RDRF before delivering a queued byte — so
the demonstration above shows the stray NAK but *not* the more serious
consequence. On the real H8/3003 a set ORER stops the receiver until it is
cleared, which means a V2 machine that latches an error at reset is **deaf
for the whole 500-round handshake window** and then starts its application,
while a V3 machine clears the flag each round and recovers. That part follows
from the hardware manual, not from a run here.

### 3. The version byte and the banner

`H'002266`, the byte immediately before `"BOS"`, is `H'0B` (11) against V3's
`H'0C` (12). The `'V'` command reports it as two hex digits, so V2 answers
`V0B` where V3 answers `V0C`, and the `'I'` banner reads

    BERNINA Electronic AG
    BiosVersion: 1.10
    Mai 97

against V3's `1.20 / July 97`. Byte and banner move together, so `'V'` is a
reliable way to tell the two ROMs apart over the wire.

## Reproducing the checks

    # all 19 commands, against the original V2 dump
    dart run tool/protocol_probe.dart \
        bernina_artista180/MemoryDump-SewingMachine-NMMV02.08-2026-08-26_20-18-31.bin \
        --capture bernina_artista180/v2bootrom/protocol_v2.json
    dart run tool/protocol_probe.dart merged.bin \
        --compare bernina_artista180/v2bootrom/protocol_v2.json
    # ... and again with --flash machine

    # the handshake under a latched line error
    dart run tool/handshake_error.dart <image.bin> [--clean]

    # burning firmware through the V2 ROM
    python3 tool/burn_test.py \
        bernina_artista180/MemoryDump-SewingMachine-NMMV02.08-2026-08-26_20-18-31.bin \
        bernina_artista180/application/app.bin --channel 1

`v2boot.bin` is the 8,960 bytes lifted out of the dump and `v2boot.lst` its
disassembly, both kept so the comparisons above can be re-run without
re-extracting.
