# gen.py uefa|hard SEED N: group-draw shapes for bench.c / i486_draw.py (the bench sets: uefa 1 2000, hard 2 400)
import random, sys
random.seed(int(sys.argv[2])); kind = sys.argv[1]; N = int(sys.argv[3])
for _ in range(N):
    if kind == 'uefa':      # 32/36/48/64 clubs, 8..16 groups, associations of 1..5 clubs
        n = random.choice([16, 24, 32, 36, 40, 48, 64]); g = random.choice([x for x in (4, 6, 8, 10, 12, 16) if n // x >= 2])
        a = []; c = 0
        while len(a) < n: a += [c] * min(random.choice([1, 1, 2, 2, 3, 4, 5]), g); c += 1
        a = a[:n]; random.shuffle(a)
    else:                   # hard: one or two big associations, the rest random
        n = random.randint(10, 64); g = random.randint(2, max(2, n // 2)); big = random.randint(g // 2, g)
        a = [255] * big + [random.choice([0, 1, 2, random.randrange(256)]) for _ in range(n - big)]
        random.shuffle(a)
    print(random.randrange(1 << 32), n, g, *a)
