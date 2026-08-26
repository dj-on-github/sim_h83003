"""The fills in routines.json, shared out and put back together again.

A case's fill is a map from "ADDR:LENGTH" to a byte value, and it is applied
in order: the same address named twice takes the value named last. That order
is part of the meaning, so anything done to the fills here has to give the
same sequence of pairs back, not merely the same set.

Cases in a family are built from one base fill with a few values changed on
top, so they share their key order exactly and differ only in a handful of
values -- 6,595 cases have 1,003 distinct key orders between them. The file
therefore keeps one base fill per distinct key order in a "fills" table, and
each case names its base and carries only what it overrides:

    "fills": {"f000": {"11A25A:1": "32", ...}, ...}
    "cases": [{"name": ..., "fillBase": "f000", "fill": {"11A25A:1": "64"}}]

A case with no "fillBase" keeps its whole fill in "fill", as before, so a
hand-written case needs nothing done to it.

    load(path)   -- the spec with every fill expanded, as the tools want it
    factor(spec) -- the same spec with the fills shared out again

`factor` checks every case: the base and the overrides put back together must
give the original pair sequence exactly, order included, or it raises. A fill
that will not reconstruct is left whole rather than quietly changed.
"""
import collections, hashlib, json


def compose(base, over):
    """`base` with `over` applied, in Python's own dict.update order: a key
    the base already has keeps its place and takes the new value, and a key
    it does not have goes on the end. That is the order the fills were built
    in, and it is what the Dart side reproduces."""
    f = collections.OrderedDict(base)
    f.update(over)
    return f


def expand(spec):
    """Every case's fill written out in full, and the "fills" table dropped."""
    fills = spec.get('fills') or {}
    for c in spec['cases']:
        base = c.pop('fillBase', None)
        if base is None:
            continue
        c['fill'] = dict(compose(fills[base], c.get('fill') or {}))
    spec.pop('fills', None)
    return spec


def load(path):
    return expand(json.load(open(path)))


def factor(spec):
    """The fills shared out: one base per distinct key order, and each case
    left with only the values it changes."""
    groups = collections.OrderedDict()
    for c in spec['cases']:
        f = c.get('fill')
        if not f:
            continue
        groups.setdefault(hashlib.sha1('\n'.join(f).encode()).hexdigest(),
                          []).append(c)

    # The biggest families first, so the shortest names go to the fills most
    # often named.
    order = sorted(groups.values(), key=len, reverse=True)
    fills = collections.OrderedDict()
    for n, members in enumerate(order):
        name = 'f%03d' % n
        # The base is whichever fill the family holds most often, so that the
        # commonest case in it carries no overrides at all.
        counts = collections.Counter(
            json.dumps(c['fill'], separators=(',', ':')) for c in members)
        base = json.loads(counts.most_common(1)[0][0])
        fills[name] = base
        for c in members:
            want = list(c['fill'].items())
            over = collections.OrderedDict(
                (k, v) for k, v in c['fill'].items() if base[k] != v)
            if list(compose(base, over).items()) != want:
                continue            # will not reconstruct: leave it whole
            c['fillBase'] = name
            if over:
                c['fill'] = dict(over)
            else:
                c.pop('fill', None)

    spec['fills'] = fills
    return spec
