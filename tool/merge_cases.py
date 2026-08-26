"""Folds the generated cases into routines.json.

Run from the repository root, after tool/gen_cases.py:

    python3 tool/merge_cases.py

Every case whose name matches a generated one is replaced; the hand-written
cases keep their place at the front of the list and are never touched. The
generated ones go on the end, in the order gen_cases.py wrote them.
"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spec_fills

P = 'bernina_artista180/application/routines.json'
# Read expanded and write factored: the file on disk shares one base fill per
# distinct key order, and everything in here works on whole fills.
d = spec_fills.load(P)
newc = json.load(open('/tmp/newcases.json'))

names = {c['name'] for c in newc}
kept = [c for c in d['cases'] if c['name'] not in names]
d['cases'] = kept + newc

# Written compact rather than indented: the fills are millions of short keys,
# and the pretty-printer's whitespace was two fifths of the file.
spec_fills.factor(d)
json.dump(d, open(P, 'w'), separators=(',', ':'))
print('kept', len(kept), '+ new', len(newc), '=', len(d['cases']),
      'over', len(d['fills']), 'fills')
