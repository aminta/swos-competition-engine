"""fuzz_swcc's libFuzzer corpus and dictionary: every JSON spec of the repo as mode 0 (profile swos9697) and mode 2
(openswos), and the bytes swcc compiles from it as mode 1; the dictionary = the schema's keys and values (string
literals of swcc.c).
Usage: python3 fuzz_swcc_corpus.py SWCC OUT_DIR DICT SPEC.json..."""
import os
import re
import subprocess
import sys

swcc, out, dict_path = sys.argv[1:4]
for i, f in enumerate(sys.argv[4:]):
    d = open(f, 'rb').read()
    for mode in (0, 2):
        open(os.path.join(out, f'j{i}.{mode}'), 'wb').write(bytes([mode]) + d)
    b = os.path.join(out, f'tmp{i}.swc')
    if subprocess.run([swcc, 'compile', f, '-o', b], capture_output=True).returncode == 0:
        open(os.path.join(out, f'b{i}'), 'wb').write(b'\x01' + open(b, 'rb').read())
    if os.path.exists(b):
        os.remove(b)
words = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_:/#.-]{1,40})"', open(os.path.join(os.path.dirname(__file__), '..', 'swcc', 'swcc.c')).read()))
with open(dict_path, 'w') as w:
    for x in sorted(words):
        w.write('"\\"%s\\""\n' % x)
