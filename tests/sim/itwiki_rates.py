#!/usr/bin/env python3
"""The real rates italyfull is compared with (42-italysim-full): Serie C and Serie D's final tables from it.wikipedia,
2014-15 .. 2025-26 (the seasons of Serie C's 3 groups of 20 and play-out 16th v 19th, 17th v 18th).

- the play-out ties the 8-point gap would skip (the spec's rule, applied to every real table of the shape: Serie C
  groups of 20, 16th v 19th and 17th v 18th; Serie D groups of 18, 13th v 16th and 14th v 15th);
- straight back up: of the clubs relegated from Serie C (the bottom of its tables: 20th, and the play-out places),
  how many are in Serie C again the season after next.

    python3 -I tests/sim/itwiki_rates.py CACHE        (CACHE: the pages as tests/sim's fetch wrote them: one JSON per
                                                       page, the MediaWiki API's action=parse wikitext + revid)
Pages: "Lega Pro 2014-2015" .. "Lega Pro 2016-2017", "Serie C 2017-2018" .. "Serie C 2025-2026", "Serie D <season>
(gironi A-B-C | D-E-F | G-H-I)". The revids are printed with the result."""
import glob
import json
import os
import re
import sys

ROW = re.compile(r"\|\|\s*(\d+)\.?\s*\|\|[^\n]*?\{\{\s*([^}|]+?)\s*[|}][^\n]*\n\|+\s*(?:''')?\s*(-?\d+)")


def tables(w):
    """[[(place, club, points)]]: each 'wikitable' after a 'Classifica' heading, rows in place order"""
    out = []
    for m in re.finditer(r'=+\s*Classifica[^=\n]*=+', w):
        rest = w[m.end():]
        t0 = rest.find('{|')
        t1 = rest.find('\n|}', t0)
        if t0 < 0 or t1 < 0:
            continue
        rows = [(int(a), b.strip(), int(c)) for a, b, c in ROW.findall(rest[t0:t1])]
        if rows and [r[0] for r in rows] == list(range(1, len(rows) + 1)):
            out.append(rows)
    return out


def load(cache, page):
    f = os.path.join(cache, page.replace(' ', '_') + '.json')
    if not os.path.exists(f):
        return None, None
    j = json.load(open(f, encoding='utf-8'))
    if 'error' in j:
        return None, None
    return j['parse']['wikitext'], j['parse'].get('revid')


def main(cache):
    seasons = ['%d-%d' % (y, y + 1) for y in range(2014, 2026)]
    c_tabs, d_tabs, revs = {}, {}, []
    for s in seasons:
        y = int(s[:4])
        p = ('Serie C %s' if y >= 2017 else 'Lega Pro %s') % s
        w, r = load(cache, p)
        if w:
            c_tabs[s] = [t for t in tables(w)][:3]
            revs.append('%s rev %s' % (p, r))
        d_tabs[s] = []
        for g in ('A-B-C', 'D-E-F', 'G-H-I'):
            p = 'Serie D %s (gironi %s)' % (s, g)
            w, r = load(cache, p)
            if w:
                d_tabs[s] += tables(w)[:3]
                revs.append('%s rev %s' % (p, r))
    po = {'C': [0, 0], 'D': [0, 0]}
    gaps = {'C': 0, 'D': 0}
    shape = {'C': (20, 15), 'D': (18, 12)}
    for lvl, tabs in (('C', c_tabs), ('D', d_tabs)):
        for s, ts in sorted(tabs.items()):
            if s == '2019-2020':
                continue                    # (no play-outs: the season stopped)
            for t in ts:
                n, a = shape[lvl]
                if len(t) != n:
                    continue
                for x in range(2):
                    po[lvl][t[a + x][2] - t[a + 3 - x][2] >= 8] += 1
                    gaps[lvl] += t[a + x][2] - t[a + 3 - x][2]
    print('play-out ties the 8-point gap skips (real tables of the shape, %s .. %s, not 2019-20):' % (seasons[0], seasons[-1]))
    for lvl in 'CD':
        n = po[lvl][0] + po[lvl][1]
        print('  Serie %s: %d of %d = %.1f%%, mean gap %.2f points' % (lvl, po[lvl][1], n, 100.0 * po[lvl][1] / (n or 1),
                                                               gaps[lvl] / (n or 1)))
    # straight back up C/D: the clubs in a season's C bottom 5 (20th + the play-out places) not in next season's C,
    # back in C the season after (by the page's club template)
    members = {s: {r[1] for t in ts for r in t} for s, ts in c_tabs.items()}
    down = back = 0
    for i, s in enumerate(seasons[:-2]):
        if s not in c_tabs or seasons[i + 1] not in members or seasons[i + 2] not in members:
            continue
        for t in c_tabs[s]:
            for _, club, _ in t[-5:]:
                if club not in members[seasons[i + 1]]:
                    down += 1
                    back += club in members[seasons[i + 2]]
    print('straight back up C/D: %d of %d relegated = %.1f%% (club templates; a club refounded under another name '
          'is missed)' % (back, down, 100.0 * back / (down or 1)))
    print('tables: C %s, D %s' % (sum(len(v) for v in c_tabs.values()), sum(len(v) for v in d_tabs.values())))
    print('\n'.join(revs))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1])
