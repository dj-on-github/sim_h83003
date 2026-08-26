"""Cases that compare nothing, and cases that compare the same nothing twice.

Run from the repository root, against the output of a full suite run:

    dart run tool/compare_routines.dart \
        bernina_artista180/application/routines.json > /tmp/full.txt
    python3 tool/blind_cases.py /tmp/full.txt

A case passes when the two images wrote the same bytes and, if the case asks
for one, ended with the same value in the result register. A case that writes
no bytes and asks for no result has agreed about nothing at all -- part 24's
trap, "a case that passes with no bytes written has not tested anything".

That is not by itself a fault. Most of them are a guard doing its job: the
routine was reached, took the turn the case is named for, and correctly drew
nothing. Such a case still kills a mutation that takes the guard out. Two
shapes *are* faults, and this is what the report separates out:

  BLIND ROUTINES  -- every case of a routine writes nothing and none asks for
  a result. Either the routine really is empty (several are: a lone RTS, a
  delay loop) or its fill never reaches the body, which is what happened to
  screen H'23's press when H'114D62 left the module looking busy.

  SAME-PATH GROUPS -- two or more of a routine's blind cases run for exactly
  the same number of steps on both sides. They took the same turn, so all but
  one of them is testing what the first one already tested. Harmless, but it
  is also the signature of a fill that collapses a family of cases onto one
  guard, so it is worth looking at whenever a group is large or its case
  names claim different inputs.
"""
import collections
import json
import re
import sys

SPEC = 'bernina_artista180/application/routines.json'
PASS = re.compile(r'  pass  (.*?)  \((\d+) vs (\d+) steps, (\d+) bytes written\)')


def main(path):
    cases = {c['name']: c for c in json.load(open(SPEC))['cases']}
    ran = {}
    for line in open(path):
        m = PASS.match(line)
        if m:
            ran[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))

    blind = [n for n, v in ran.items()
             if n in cases and v[2] == 0 and 'result' not in cases[n]['original']]
    print('%d cases ran, %d of them compared nothing' % (len(ran), len(blind)))

    # Two cases under one name is its own fault: subspec.py picks by name and
    # merge_cases.py replaces by name, so the second of a pair can be thrown
    # away by a merge without anything saying so.
    names = [c['name'] for c in json.load(open(SPEC))['cases']]
    twice = sorted(n for n, k in collections.Counter(names).items() if k > 1)
    if twice:
        print('\nDUPLICATE CASE NAMES -- name-based tooling cannot tell these apart')
        for n in twice:
            print('  %s' % n)

    by_addr = collections.defaultdict(list)
    for n in ran:
        c = cases.get(n)
        if c:
            by_addr[c['original'].get('addr')].append(n)

    print('\nBLIND ROUTINES -- no case of these writes a byte or asks a result')
    n_blind_routines = 0
    for addr in sorted(by_addr):
        ns = by_addr[addr]
        if all(ran[n][2] == 0 and 'result' not in cases[n]['original'] for n in ns):
            n_blind_routines += 1
            print('  H\'%s  %2d case(s)  %s' % (addr, len(ns), sorted(ns)[0]))
    if not n_blind_routines:
        print('  (none)')

    print('\nSAME-PATH GROUPS -- blind cases of one routine, identical step counts')
    groups = []
    for addr, ns in by_addr.items():
        g = collections.defaultdict(list)
        for n in ns:
            if n in blind:
                g[ran[n][:2]].append(n)
        for k, v in g.items():
            if len(v) > 1:
                groups.append((len(v), addr, k, sorted(v)))
    groups.sort(reverse=True)
    for count, addr, k, v in groups:
        print('  H\'%s  x%d  %d/%d steps' % (addr, count, k[0], k[1]))
        for n in v:
            print('        %s' % n)
    if not groups:
        print('  (none)')
    print('\n%d group(s), %d case(s) beyond the first of their group'
          % (len(groups), sum(c - 1 for c, _, _, _ in groups)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '/tmp/full.txt')
