#!/usr/bin/env python3
"""Rebuild tests/rules/specs from the Mod's packs: each World 2026-27 league's knockouts as the
Mod's own tools build them (mkseason.py's Pack.gplay: gplay.py compile_spec, a 'club' entrant
by its team word), and shapes.txt (each division's size). Bytes of the format only -- entrant
places, rounds, legs, team words --, no names.

    python3 -I tests/rules/specs/make_specs.py MOD PACKS [OUT]

MOD: a swos-9697-mod checkout (its tools/gplay.py); PACKS: the folder of <code>/pack.json (the
Mod's world5tm); OUT: default tests/rules/specs. The packs are not in this repository."""
import glob
import json
import os
import sys

if len(sys.argv) < 3:
    sys.exit(__doc__)
mod, packs = sys.argv[1], sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(mod, 'tools'))
import mkseason  # noqa: E402  (the Mod's)

shapes = []
for f in sorted(glob.glob(os.path.join(packs, '*', 'pack.json'))):
    code = os.path.basename(os.path.dirname(f))
    for c in json.load(open(f, encoding='utf-8'))['competitions']:
        if c.get('key') != 'league' or not c.get('ko_playoffs'):
            continue
        nd = len(c.get('divisions') or [1])
        sizes = [d['teams'] for d in c.get('divisions') or []] or [len(c.get('clubs') or []) or 16]
        built = mkseason.Pack(os.path.dirname(f)).gplay()
        if not built:
            sys.exit('%s: the Mod builds no knockouts for it' % code)
        open(os.path.join(out, code + '.spec'), 'wb').write(bytes(built[2]))
        shapes.append('%s %s\n' % (code, ','.join(map(str, sizes))))
open(os.path.join(out, 'shapes.txt'), 'w').write(''.join(shapes))
print('%d leagues -> %s' % (len(shapes), out))
