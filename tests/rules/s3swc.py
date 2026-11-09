#!/usr/bin/env python3
"""42-s3shape: Italy's play-offs written in swc (tests/specs/ok/v14_ita_playoffs.json) bridged by swcc/swc_gplay.py
-- the Mod rule's cup place, then the real C.U. 209/A rule ("instead": ["finalist", winner_group#4 scroll]).

    python3 -I tests/rules/s3swc.py SWCC [MOD_TOOLS]

Each variant: swcc validates it; the bridge's play-offs = tests/rules/specs/ita_s3.json (entrants, divisions, kinds,
rounds, direct_gap, rank); with MOD_TOOLS (a Mod with the 4.2 codes), the Mod's compile_spec of them = ita_s3.spec /
ita_s3r.spec byte for byte. Then the shapes the bytes cannot carry, refused by the bridge."""
import copy
import json
import os
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
sys.path.insert(0, os.path.join(root, 'swcc'))
import swc_gplay  # noqa: E402

swcc, mod = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None
if mod:
    sys.path.insert(0, mod)
    import gplay  # noqa: E402  (the Mod's)
spec = json.load(open(os.path.join(root, 'tests/specs/ok/v14_ita_playoffs.json'), encoding='utf-8'))
ref = json.load(open(os.path.join(here, 'specs/ita_s3.json')))
KEYS = ('kind', 'entrants', 'winner_div', 'loser_div', 'rounds', 'direct_gap', 'rank')
fails = 0


def cup(sp):
    po = next(c for c in sp['competitions'] if c['id'] == 'ita.c-po')
    return po['stages'][0]['entrants'][-1]['cup_place']


for name, real in (('ita_s3', False), ('ita_s3r', True)):
    sp = copy.deepcopy(spec)
    if real:
        cup(sp)['instead'] = ['finalist', {'place': 'winner_group#4', 'scroll': True}]
    with tempfile.NamedTemporaryFile('w', suffix='.json', delete=False) as f:
        json.dump(sp, f)
    r = subprocess.run([swcc, 'validate', f.name], capture_output=True, text=True)
    os.unlink(f.name)
    if r.returncode:
        print('FAIL: %s: swcc refuses it: %s' % (name, r.stderr.strip())); fails += 1; continue
    pos = swc_gplay.bridge(sp, 'ITA')
    want = copy.deepcopy(ref)
    want[2]['entrants'][-1]['cup_place']['real'] = real
    for x, y in zip(pos, want):
        for k in KEYS:
            if x.get(k) != y.get(k):
                print('FAIL: %s: %s %s differs from ita_s3.json' % (name, x['key'], k)); fails += 1
    if len(pos) != len(want):
        print('FAIL: %s: %d play-offs, ita_s3.json has %d' % (name, len(pos), len(want))); fails += 1
    if mod and bytes(gplay.compile_spec(pos, 5)) != open(os.path.join(here, 'specs/%s.spec' % name), 'rb').read():
        print('FAIL: %s: the Mod\'s compile_spec of the bridged play-offs is not %s.spec' % (name, name)); fails += 1

# the shapes the bytes cannot carry: refused
BAD = [
    ('a loser reference', lambda sp: sp['competitions'][1]['stages'][1]['rounds'][1]['fixed'].__setitem__(0, ['loser:1.2', 'table#3'])),
    ('a cup place of other places', lambda sp: cup(sp).__setitem__('takes', 'table#12-15')),
    ('another cup rule', lambda sp: cup(sp).__setitem__('instead', ['finalist'])),
    ('a rest_of without its best_of', lambda sp: next(c for c in sp['competitions'] if c['id'] == 'ita.c-po')['stages'][0]
     ['rounds'][3]['seeds'].__setitem__(3, 'ita.c-a#4')),
]
for what, f in BAD:
    sp = copy.deepcopy(spec)
    f(sp)
    try:
        swc_gplay.bridge(sp, 'ITA')
        print('FAIL: the bridge takes %s' % what); fails += 1
    except swc_gplay.Refused:
        pass
print('%s: Italy\'s play-offs in swc -> the bridge = ita_s3.json (Mod rule, real rule)%s; %d shapes refused' %
      ('FAIL' if fails else 'PASS', ', the Mod\'s compile_spec = ita_s3 / ita_s3r.spec byte for byte' if mod else '', len(BAD)))
sys.exit(1 if fails else 0)
