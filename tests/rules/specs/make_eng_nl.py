#!/usr/bin/env python3
"""Rebuild tests/rules/specs/eng_nl.spec: the National League, North and South promotion play-offs (4.2 EN
non-league) as the Mod builds them (tools/nonleague.py nl_playoff -> tools/gplay.py compile_spec), divisions
0 Premier League .. 3 League Two, 4 National League, 5 North, 6 South. Bytes of the format only.

    python3 -I tests/rules/specs/make_eng_nl.py MOD [OUT]

MOD: a swos-9697-mod checkout with tools/nonleague.py (42-step3spec or later)."""
import os
import sys

if len(sys.argv) < 2:
    sys.exit(__doc__)
sys.path.insert(0, os.path.join(sys.argv[1], 'tools'))
import gplay  # noqa: E402  (the Mod's)
import nonleague  # noqa: E402

pos = [nonleague.nl_playoff(f'{c.lower()}_po', 4 + k, {'*': [c, c]}, 3 if c == 'NL' else 4)
       for k, c in enumerate(('NL', 'NLN', 'NLS'))]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'eng_nl.spec')
open(out, 'wb').write(bytes(gplay.compile_spec(pos, 7)))
