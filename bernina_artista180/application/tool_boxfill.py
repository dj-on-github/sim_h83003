"""Build a hit-box table fill for comparison cases.

The table lives at H'0E2000 and H'11B0BA points at it. Entry zero's first
word is the count; entry n is H'12 bytes at H'0E2000 + H'12*n:

    +0 x0  +2 y0  +4 x1  +6 y1  +8 value  +A pair  +C list  +10 kind  +11 style

Boxes are laid out in rows of eight, H'20 apart and H'18 wide, so a whole
row fits inside a byte of panel reading and a touch at the middle of one
lands in it and nowhere else. Rows are H'30 apart in y.
"""
TABLE = 0x0E2000

def w16(f, a, v):
    f["%06X:1" % a] = "%02X" % ((v >> 8) & 0xFF)
    f["%06X:1" % (a + 1)] = "%02X" % (v & 0xFF)

def _place(i):
    """Box [i] (1-based) goes at column (i-1) % 8 of row (i-1) // 8."""
    return 0x10 + 0x20 * ((i - 1) % 8), 0x20 + 0x30 * ((i - 1) // 8)

def boxes(values, kinds=None, lists=None, style=0x03):
    """values[i] is box i+1's value. Returns a fill dict."""
    n = len(values)
    f = {"0E2000:200": "00", "11B0BA:1": "00", "11B0BB:1": "0E",
         "11B0BC:1": "20", "11B0BD:1": "00", "11B0B2:4": "00"}
    w16(f, TABLE, n)
    for i, v in enumerate(values, start=1):
        b = TABLE + 0x12 * i
        x0, y0 = _place(i)
        w16(f, b + 0, x0);        w16(f, b + 2, y0)
        w16(f, b + 4, x0 + 0x18); w16(f, b + 6, y0 + 0x20)
        w16(f, b + 8, v)
        f["%06X:1" % (b + 0x0A)] = "00"
        for k in range(4):
            f["%06X:1" % (b + 0x0C + k)] = "%02X" % (
                0 if lists is None or lists[i-1] == 0
                else (lists[i-1] >> (24 - 8*k)) & 0xFF)
        f["%06X:1" % (b + 0x10)] = "%02X" % (0 if kinds is None else kinds[i-1])
        f["%06X:1" % (b + 0x11)] = "%02X" % style
    return f

def touch(f, box):
    """Aim the panel reading at box [box] (1-based)."""
    x0, y0 = _place(box)
    f["FFFED9:1"] = "%02X" % (x0 + 0x0C)
    f["FFFEDA:1"] = "%02X" % (y0 + 0x10)
    return f


# ---- fonts ---------------------------------------------------------------
# H'21700A looks a glyph up at font + ch * 4 - H'84, so the table starts at
# character H'21. A glyph is a four-byte header -- width and height as
# big-endian words -- followed by run-length bytes, and the ones laid down
# here are H'20 apart at H'0E1500 so they can be pointed at from any font.
GLYPHS = 0x0E1500

def font(f, base, chars, widths=None):
    """Give [base] a glyph for each character in [chars].

    [widths] gives each glyph its own width; without it they are 4, 5 and 6
    over and over. A glyph narrower than the box it is drawn in is what makes
    the box's own left edge visible.
    """
    f["%06X:200" % GLYPHS] = "00"
    f["%06X:200" % base] = "00"
    for i, ch in enumerate(chars):
        g = GLYPHS + 0x20 * i
        w = widths[i] if widths else 4 + (i % 3)
        f["%06X:1" % (g + 0)] = "00"; f["%06X:1" % (g + 1)] = "%02X" % w
        f["%06X:1" % (g + 2)] = "00"; f["%06X:1" % (g + 3)] = "06"
        for k in range(0x18):
            f["%06X:1" % (g + 4 + k)] = "%02X" % (0x33 + 7 * k + i & 0xFF)
        p = base + 4 * ord(ch) - 0x84
        for j, b in enumerate(((g >> 24) & 0xFF, (g >> 16) & 0xFF,
                               (g >> 8) & 0xFF, g & 0xFF)):
            f["%06X:1" % (p + j)] = "%02X" % b
    return f
