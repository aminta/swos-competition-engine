# gen.py uefa|hard|uefachain|chain|uefa32 SEED N: group-draw shapes for bench.c / i486_draw.py (the bench sets: uefa 1
# 2000, hard 2 400). uefachain / chain / uefa32 (swc/1.5): each line ends with np and np protect pairs (BENCH_PAIRS=1,
# PAIRS=1): UEFA-like shapes with chains of 2-5 linked associations; hard ones up to 64 clubs with 1-3 chains; the
# v15_stage_cup shape (32 in 8, RUS-UKR, KOS-BIH, KOS-SRB) with its seeds.
import random, sys
random.seed(int(sys.argv[2])); kind = sys.argv[1]; N = int(sys.argv[3])
for _ in range(N):
    if kind == 'uefa':      # 32/36/48/64 clubs, 8..16 groups, associations of 1..5 clubs
        n = random.choice([16, 24, 32, 36, 40, 48, 64]); g = random.choice([x for x in (4, 6, 8, 10, 12, 16) if n // x >= 2])
        a = []; c = 0
        while len(a) < n: a += [c] * min(random.choice([1, 1, 2, 2, 3, 4, 5]), g); c += 1
        a = a[:n]; random.shuffle(a)
    elif kind == 'cap2':    # swc/1.6 (CAP=2, BENCH_CAP=2): 16..64 clubs, associations of 1..2g clubs (a few over)
        n = random.randint(12, 64); g = random.randint(2, max(2, n // 3))
        a = []; c = 0
        while len(a) < n: a += [c] * random.randint(1, 2 * g + (random.random() < 0.05)); c += 1
        a = a[:n]; random.shuffle(a)
        print(random.randrange(1 << 32), n, g, *a)
        continue
    elif kind == 'over':    # swc/1.7 (MIN=1, BENCH_MIN=1): 12..64 clubs, associations of 1..3g clubs (AFC "unavoidable")
        n = random.randint(12, 64); g = random.randint(2, max(2, n // 3))
        a = []; c = 0
        while len(a) < n: a += [c] * random.randint(1, 3 * g); c += 1
        a = a[:n]; random.shuffle(a)
        print(random.randrange(1 << 32), n, g, *a)
        continue
    elif kind == 'acl2':    # swc/1.7: one ACL Two region, 16 clubs in 4 groups, an association with 3..8 clubs
        a = [0] * random.randint(3, 8); c = 1
        while len(a) < 16: a += [c] * random.randint(1, 3); c += 1
        a = a[:16]; random.shuffle(a)
        print(random.randrange(1 << 32), 16, 4, *a)
        continue
    elif kind == 'caf':     # swc/1.6 (MC): CAF-like, 16 clubs in 4 groups, one association with up to 4 clubs
        a = [0] * random.randint(2, 4); c = 1
        while len(a) < 16: a += [c] * random.randint(1, 3); c += 1
        a = a[:16]; random.shuffle(a)
        print(random.randrange(1 << 32), 16, 4, *a)
        continue
    elif kind == 'uefa32':  # v15_stage_cup: ESP ENG ITA GER 4, FRA POR 3, RUS UKR NED 2, KOS BIH SRB SCO 1, access order
        cnt = [4, 4, 4, 4, 3, 3, 2, 2, 2, 1, 1, 1, 1]
        a = [t for t, c in enumerate(cnt) for _ in range(c)]
        print(random.randrange(1 << 32), 32, 8, *a, 3, 6, 7, 9, 10, 9, 11)
        continue
    elif kind in ('uefachain', 'chain'):
        if kind == 'uefachain':
            n = random.choice([16, 24, 32, 36, 40, 48, 64]); g = random.choice([x for x in (4, 6, 8, 10, 12, 16) if n // x >= 2])
            a = []; c = 0
            while len(a) < n: a += [c] * min(random.choice([1, 1, 2, 2, 3, 4, 5]), g); c += 1
            a = a[:n]; random.shuffle(a)
        else:               # chain: up to 64 clubs, associations of 1..g clubs
            n = random.randint(10, 64); g = random.randint(2, max(2, n // 4))
            a = []; c = 0
            while len(a) < n: a += [c] * random.randint(1, g); c += 1
            a = a[:n]; random.shuffle(a)
        assoc = sorted(set(a)); pairs = set()
        for _ in range(random.randint(1, 3)):   # a chain: a path / star of 3..5 associations
            m = random.sample(assoc, min(len(assoc), random.randint(3, 5)))
            for i in range(1, len(m)):
                x, y = (m[i - 1], m[i]) if random.random() < 0.6 else (m[0], m[i])
                if x != y: pairs.add((min(x, y), max(x, y)))
        pairs = sorted(pairs)[:64]
        print(random.randrange(1 << 32), n, g, *a, len(pairs), *[v for p in pairs for v in p])
        continue
    else:                   # hard: one or two big associations, the rest random
        n = random.randint(10, 64); g = random.randint(2, max(2, n // 2)); big = random.randint(g // 2, g)
        a = [255] * big + [random.choice([0, 1, 2, random.randrange(256)]) for _ in range(n - big)]
        random.shuffle(a)
    print(random.randrange(1 << 32), n, g, *a)
