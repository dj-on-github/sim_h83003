"""Folds the generated cases into routines.json.

Run from the repository root, after tool/gen_cases.py:

    python3 tool/merge_cases.py

Every case whose name matches a generated one is replaced; the hand-written
cases keep their place at the front of the list and are never touched. The
generated ones go on the end, in the order gen_cases.py wrote them.
"""
import json

P = 'bernina_artista180/application/routines.json'
d = json.load(open(P))
newc = json.load(open('/tmp/newcases.json'))

names = {c['name'] for c in newc}
kept = [c for c in d['cases'] if c['name'] not in names]
d['cases'] = kept + newc

json.dump(d, open(P, 'w'), indent=2)
print('kept', len(kept), '+ new', len(newc), '=', len(d['cases']))
