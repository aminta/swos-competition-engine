"""fuzz_rules' libFuzzer corpus: each real spec of DIR behind 4 random 16-byte headers (league / season / tables).
Usage: python3 fuzz_corpus.py SPEC_DIR OUT_DIR"""
import glob
import os
import random
import sys

random.seed(1)
for f in sorted(glob.glob(os.path.join(sys.argv[1], '*.spec'))):
    d = open(f, 'rb').read()
    for k in range(4):
        h = bytes(random.randrange(256) for _ in range(16))
        open(os.path.join(sys.argv[2], f'{os.path.basename(f)}.{k}'), 'wb').write(h + d)
