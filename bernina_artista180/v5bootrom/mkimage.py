"""Fill in an image header's length and checksum, which are only knowable
once the image has been linked.

The header is 64 bytes; the checksum is the plain byte sum of everything
after it. Stage-0 recomputes exactly this and refuses the image if it
disagrees, which is what makes a half-written slot harmless rather than
fatal.

    python3 mkimage.py <image.bin>
"""
import struct, sys

HDR = 0x40

def main(path):
    d = bytearray(open(path, 'rb').read())
    if d[:4] != b'B180':
        sys.exit('%s: no B180 magic -- wrong file?' % path)
    body = d[HDR:]
    struct.pack_into('>I', d, 0x08, len(body))
    struct.pack_into('>I', d, 0x0C, sum(body) & 0xFFFFFFFF)
    open(path, 'wb').write(d)
    gen, ln, sm = struct.unpack_from('>III', d, 4)
    print('%s: generation %d, %d bytes, checksum H%08X'
          % (path, gen, ln, sm))

if __name__ == '__main__':
    main(sys.argv[1])
