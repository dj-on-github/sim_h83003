"""Annotates a routine's listing with frame-base-relative local offsets.

    dart run tool/disasm.dart <rom> --start <a> --end <b> --no-bytes > r.txt
    python3 tool/frame.py r.txt [first-address] [locals-frame-size]

Reading @(H'NN:16,ER7) straight off a listing gives the wrong local whenever
ER7 has moved, which in a routine that pushes around its calls is most of the
time. Pass the frame size the prologue reserves (the ADD.L #-imm,ER7 plus the
saved registers) as the third argument and every displacement comes out as
@L<offset from the frame base>, the same name at every stack depth.

Tracks the stack depth against ER7 through PUSH/POP/ADDS/SUBS/ADD.L and
rewrites every @(H'NN:16,ER7) as L<offset from the frame base>, so the
offsets can be read straight off instead of being adjusted by hand.
"""
import re, sys

lines = open(sys.argv[1]).read().splitlines()
start = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
BASE = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0

depth = 0          # bytes ER7 is below the frame base
prologue = True
out = []
for ln in lines:
    m = re.match(r"\s+H'([0-9A-F]{6})\s\s(.*)$", ln)
    if not m:
        out.append(ln); continue
    addr, t = int(m.group(1), 16), m.group(2).rstrip()
    if start is not None and addr < start:
        out.append(ln); continue

    shown = t
    for mm in re.finditer(r"@\(H'([0-9A-F]{2,4}):16,ER7\)", t):
        off = int(mm.group(1), 16) - depth + BASE
        shown = shown.replace(mm.group(0), '@L%+d' % off if off < 0 else '@L%d' % off)
    if re.search(r"@ER7\b", t) and '@(' not in t:
        shown = shown.replace('@ER7', '@L%d' % (BASE - depth))
    out.append("  H'%06X  %-46s ; sp-%X" % (addr, shown, depth))

    if re.match(r'PUSH\.L', t):   depth += 4
    elif re.match(r'PUSH\.W', t): depth += 2
    elif re.match(r'POP\.L', t):  depth -= 4
    elif re.match(r'POP\.W', t):  depth -= 2
    elif re.match(r'SUBS #(\d),ER7', t): depth += int(re.match(r'SUBS #(\d)', t).group(1))
    elif re.match(r'ADDS #(\d),ER7', t): depth -= int(re.match(r'ADDS #(\d)', t).group(1))
    else:
        mm = re.match(r"ADD\.L #H'([0-9A-F]{8}),ER7", t)
        if mm:
            v = int(mm.group(1), 16)
            if v & 0x80000000: depth += (0x100000000 - v)
            else: depth -= v
print('\n'.join(out))
