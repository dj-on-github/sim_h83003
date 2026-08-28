"""Lay the v5 pieces out in a 32K boot-device image.

    H'000000  stage0.bin   permanent: vectors, trampolines, loader
    H'000800  imageA.bin   generation 1
    H'004000  imageB.bin   generation 2

Anything not covered is left erased. The two slots are deliberately the same
code at different addresses: a machine running A is updated by writing B and
vice versa, and stage-0 decides which at every reset.
"""
import sys

DEVICE = 0x8000
PIECES = [(0x000000, 'stage0.bin', 0x800),
          (0x000800, 'imageA.bin', 0x3800),
          (0x004000, 'imageB.bin', 0x4000)]

def main(out):
    dev = bytearray(b'\xFF' * DEVICE)
    for base, name, limit in PIECES:
        d = open(name, 'rb').read()
        if len(d) > limit:
            sys.exit('%s is %d bytes, %d too many for its slot'
                     % (name, len(d), len(d) - limit))
        dev[base:base + len(d)] = d
        print('  H%06X  %-12s %6d bytes  (%d spare)'
              % (base, name, len(d), limit - len(d)))
    open(out, 'wb').write(dev)
    print('wrote %s, %d bytes' % (out, len(dev)))

if __name__ == '__main__':
    main(sys.argv[1])
