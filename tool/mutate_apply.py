"""Applies one mutation to one routine, from the pristine copy.

    python3 tool/mutate_apply.py <snapshot-dir> <file.c> <signature> <from> <to>

The signature names the routine -- the first line of its definition, matched
at the start of a line -- and the routine's body is taken from there to the
next closing brace in column one. The replacement is made inside that body
only, so an anchor that appears elsewhere in the file is left alone, and it
must appear exactly once inside it: an ambiguous anchor is an error rather
than a silent choice of the wrong one.

The source is always read from the snapshot and written to the tree, so a
mutation is never applied on top of another.
"""
import os, sys

snap, name, sig, old, new = sys.argv[1:6]
app = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   '..', 'bernina_artista180', 'application')

s = open(os.path.join(snap, name)).read()
i = s.index('\n' + sig)
j = s.index('\n}\n', i) + 3
body = s[i:j]

assert body.count(old) == 1, 'anchor %r appears %d times in %s' % (
    old, body.count(old), sig)

open(os.path.join(app, name), 'w').write(s[:i] + body.replace(old, new, 1) + s[j:])
