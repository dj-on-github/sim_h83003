# A model of the H8 decoder at H'20F866, and an encoder that feeds it.
DICT = 0x0FF710
OUT  = 0x0FFB14
TOP  = 0x0FFC13

class Dec:
    def __init__(self, stream):
        self.s = stream
        self.pre = [0xFFFF]*0x101
        self.suf = [0]*0x101
        self.buf = bytearray(0x100)          # 0x0FFB14 .. 0x0FFC13
        self.reset()
    def reset(self):
        self.nxt = 3
        self.pos = 4
        self.old = self.s[4]
        self.pre[0]=0xFFFF; self.suf[0]=0x00
        self.pre[1]=0xFFFF; self.suf[1]=0x02
        self.pre[2]=0xFFFF; self.suf[2]=0x03
        self.buf[0xFE] = self.suf[self.s[4]]
        self.out = 0xFE
    def step(self):
        self.pos += 1
        code = self.s[self.pos]
        entry = self.nxt
        self.pre[entry] = self.old
        if code >= self.nxt:
            c = self.old; sp = TOP - 1
        else:
            c = code; sp = TOP
        n = 0
        while True:
            ch = self.suf[c]
            sp -= 1
            assert sp >= OUT, 'output underflow'
            self.buf[sp - OUT] = ch
            c = self.pre[c]
            n += 1
            assert n < 0x200, 'runaway chain'
            if c == 0xFFFF: break
        if code >= self.nxt:
            self.buf[TOP - 1 - OUT] = ch
        self.suf[entry] = ch
        self.out = sp - OUT
        self.old = code
        self.nxt += 1
        if self.nxt > 0x100: self.nxt = 3
    def pixels(self, n):
        """The n pixel values the caller would read."""
        got = []
        pos = self.out
        while len(got) < n:
            got.append(self.buf[pos]); pos += 1
            if pos > 0xFE:
                self.step(); pos = self.out
        return got

def encode(px):
    """The code bytes that make the decoder produce px."""
    def fresh():
        return {(0,):0, (2,):1, (3,):2}, 3
    table, nxt = fresh()
    out = []
    w = (px[0],)
    i = 1
    while i < len(px):
        wk = w + (px[i],)
        c = table.get(wk)
        if c is not None and c < nxt:
            w = wk; i += 1; continue
        out.append(table[w])
        table[wk] = nxt
        nxt += 1
        if nxt > 0x100: table, nxt = fresh()
        w = (px[i],)
        i += 1
    out.append(table[w])
    return out

def stream_for(px, w=0, h=0):
    """A whole stream: four header bytes then the codes."""
    codes = encode(px)
    # A few spare codes on the end: the caller takes another step as soon
    # as it has used the last byte of a string, so it reads one past what
    # it needs.
    return (bytes([(w>>8)&0xFF, w&0xFF, (h>>8)&0xFF, h&0xFF])
            + bytes(codes) + bytes(8))
