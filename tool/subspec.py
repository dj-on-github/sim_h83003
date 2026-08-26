"""Cuts routines.json down to the cases whose names match a pattern.

Run from the repository root:

    python3 tool/subspec.py 'module_colours_show '
    dart run tool/compare_routines.dart /tmp/mut_spec.json

The pattern is a Python regular expression matched at the *start* of the case
name, so a trailing space picks one routine's cases without catching the ones
whose names begin the same way. Writes /tmp/mut_spec.json, which is what makes
running a single routine -- and mutation testing it -- take seconds rather
than the whole suite.
"""
import json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spec_fills

base = json.load(open('bernina_artista180/application/routines.json'))
pat = re.compile(sys.argv[1])

spec = dict(base)
spec['cases'] = [c for c in base['cases'] if pat.match(c['name'])]

# Only the fills the chosen cases name: carrying the whole table would put
# thirty megabytes into a three-case spec and slow every mutation run down.
wanted = {c['fillBase'] for c in spec['cases'] if 'fillBase' in c}
spec['fills'] = {k: v for k, v in (base.get('fills') or {}).items()
                 if k in wanted}

json.dump(spec, open('/tmp/mut_spec.json', 'w'), separators=(',', ':'))
print(len(spec['cases']), 'cases')
