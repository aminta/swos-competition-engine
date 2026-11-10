/* swc/1.5 draws (P5, MC 2026-11-10): sr_group_draw_p's protect pairs and sr_ko_pairs_x's two keys + pairs.
 * - brute force, n <= 12: a draw exists (pots, sizes, associations apart, every pair apart) exactly when the library
 *   returns one (-2 otherwise, never -3), and every draw it returns keeps them all;
 * - a chain (KOS-BIH, KOS-SRB): BIH and SRB share a group in some draws, KOS never with either; merged into one class
 *   it would be refused -- the control;
 * - clique pairs = sr_group_draw on the merged classes, draw for draw (König's path unchanged);
 * - sr_ko_pairs_x seeded, same group + association + pairs: -2 exactly when no perfect matching exists (brute
 *   force), every pairing kept; no pairs and one key = sr_ko_pairs. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "swosrules.h"

static uint32_t rs = 12345;
static uint32_t rnd(uint32_t n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }

static int n_, g_, np_;
static uint8_t as_[16], pr_[32], size_[16], grp_[16];
static int pair_bad(int a, int b)
{
    if (as_[a] == as_[b]) return 1;
    for (int i = 0; i < np_; i++)
        if ((pr_[2 * i] == as_[a] && pr_[2 * i + 1] == as_[b]) || (pr_[2 * i] == as_[b] && pr_[2 * i + 1] == as_[a])) return 1;
    return 0;
}
static int valid(const uint8_t *grp)
{
    int cnt[16][16] = { { 0 } };
    for (int c = 0; c < n_; c++) {
        if (grp[c] >= g_) return 0;
        if (cnt[c / g_][grp[c]]++) return 0;                    /* one club of a pot per group */
        for (int d = 0; d < c; d++) if (grp[d] == grp[c] && pair_bad(c, d)) return 0;
    }
    int sz[16] = { 0 };
    for (int c = 0; c < n_; c++) sz[grp[c]]++;
    for (int j = 0; j < g_; j++) if (sz[j] != size_[j]) return 0;
    return 1;
}
static int bf(int c)                                            /* any whole draw from club c on */
{
    if (c == n_) return valid(grp_);
    for (int j = 0; j < g_; j++) {
        int ok = 1;
        for (int d = c - c % g_; d < c && ok; d++) ok = grp_[d] != j;
        for (int d = 0; d < c && ok; d++) ok = grp_[d] != j || !pair_bad(c, d);
        if (!ok) continue;
        grp_[c] = (uint8_t)j;
        if (bf(c + 1)) return 1;
    }
    return 0;
}

static int nk_;
static uint8_t av_[16];
static int kbad(int a, int b) { return av_[a] == av_[b] || pair_bad(a, b); }
static int kbf(int t, unsigned used)                            /* top clubs t.. each to a free bottom club */
{
    if (t == nk_ / 2) return 1;
    for (int b = nk_ / 2; b < nk_; b++)
        if (!(used >> b & 1) && !kbad(t, b) && kbf(t + 1, used | 1u << b)) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    int fail = 0, cases = 0, none = 0, chained = 0;
    const int N1 = argc > 1 ? atoi(argv[1]) : 30000;
    for (int it = 0; it < N1; it++) {                           /* groups: brute force */
        g_ = 2 + (int)rnd(3);
        n_ = g_ * 2 + (int)rnd((unsigned)(13 - g_ * 2));
        if (n_ > 12) n_ = 12;
        const int na = 4 + (int)rnd(6);
        for (int c = 0; c < n_; c++) as_[c] = (uint8_t)(10 + rnd((unsigned)na));
        np_ = (int)rnd(5);
        if (np_ > na * (na - 1) / 2) np_ = na * (na - 1) / 2;
        for (int i = 0; i < np_; i++) {
            pr_[2 * i] = (uint8_t)(10 + rnd((unsigned)na));
            do pr_[2 * i + 1] = (uint8_t)(10 + rnd((unsigned)na)); while (pr_[2 * i + 1] == pr_[2 * i]);
            for (int k = 0; k < i; k++)                         /* (distinct pairs, as swcc writes them) */
                if ((pr_[2 * k] == pr_[2 * i] && pr_[2 * k + 1] == pr_[2 * i + 1]) ||
                    (pr_[2 * k] == pr_[2 * i + 1] && pr_[2 * k + 1] == pr_[2 * i])) { i--; break; }
        }
        sr_group_sizes(n_, g_, size_);
        const int exists = bf(0);
        uint8_t grp[16];
        const int r = sr_group_draw_p(n_, g_, as_, pr_, np_, rs, grp);
        cases++;
        none += !exists;
        if (exists ? r != g_ || !valid(grp) : r != -2) {
            if (fail++ < 5) printf("FAIL: groups n %d g %d pairs %d: exists %d, returned %d%s\n", n_, g_, np_, exists, r,
                                   r == g_ && !valid(grp) ? " (a draw breaking a constraint)" : "");
            if (fail == 1 && getenv("D15_DEBUG")) {
                for (int c = 0; c < n_; c++) printf(" %d:a%d g%d", c, as_[c], r > 0 ? grp[c] : -1);
                printf("\n pairs");
                for (int i = 0; i < np_; i++) printf(" %d-%d", pr_[2 * i], pr_[2 * i + 1]);
                printf("\n sizes");
                for (int j = 0; j < g_; j++) printf(" %d", size_[j]);
                printf("\n");
            }
        }
    }
    {   /* a chain: KOS, BIH, SRB one club each, 5 others, 2 groups: as pairs it draws (BIH and SRB together, apart
         * from KOS), merged into one class it could not (3 > 2) */
        uint8_t a[8] = { 1, 2, 3, 4, 5, 4, 5, 6 }, p[4] = { 1, 2, 1, 3 }, grp[8];
        int together = 0, bad = 0;
        for (uint32_t s = 0; s < 1000; s++) {
            if (sr_group_draw_p(8, 2, a, p, 2, s * 2654435761u, grp) != 2) { bad++; continue; }
            if (grp[0] == grp[1] || grp[0] == grp[2] || grp[3] == grp[5] || grp[4] == grp[6]) bad++;
            together += grp[1] == grp[2];
        }
        uint8_t m[8] = { 1, 1, 1, 4, 5, 4, 5, 6 };              /* (the control: KOS, BIH, SRB one class, 3 > 2) */
        if (bad || together != 1000 || sr_group_draw(8, 2, m, 7, grp) != -2) {
            printf("FAIL: chain KOS-BIH + KOS-SRB: %d bad draws, BIH with SRB in %d of 1000\n", bad, together);
            fail++;
        } else
            chained = together;
    }
    int uefa = 0, ub = 0, apart = 0;
    {   /* the v15_stage_cup shape: 32 clubs, 8 groups, ESP ENG ITA GER 4, FRA POR 3, RUS UKR NED 2, KOS BIH SRB SCO 1, in
         * SRB first, the rest in access order; RUS-UKR, KOS-BIH, KOS-SRB: always a draw, every pair apart, BIH and SRB sometimes together */
        static const uint8_t cnt[13] = { 4, 4, 4, 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 };
        uint8_t a[32], grp[32], p[6] = { 6, 7, 9, 10, 9, 11 };
        int k = 0;
        for (int t = 0; t < 13; t++) for (int i = 0; i < cnt[t]; i++) a[k++] = (uint8_t)t;
        a[30] = a[0]; a[0] = 11;                                /* (SRB in pot 1: BIH's pot is 4) */
        int bih = -1, srb = -1, kos = -1, rus[2], ukr[2], nr = 0, nu = 0;
        for (int c = 0; c < 32; c++) {
            if (a[c] == 9) kos = c; else if (a[c] == 10) bih = c; else if (a[c] == 11) srb = c;
            else if (a[c] == 6) rus[nr++] = c; else if (a[c] == 7) ukr[nu++] = c;
        }
        for (uint32_t s = 0; s < 1000; s++) {
            const int r = sr_group_draw_p(32, 8, a, p, 3, s * 2654435761u + 1, grp);
            if (r != 8) { ub++; continue; }
            int bad = grp[kos] == grp[bih] || grp[kos] == grp[srb];
            for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) bad |= grp[rus[i]] == grp[ukr[j]];
            for (int c = 0; c < 32; c++) for (int d = 0; d < c; d++) bad |= a[c] == a[d] && grp[c] == grp[d];
            ub += bad;
            uefa += grp[bih] == grp[srb];
            apart += grp[bih] != grp[srb];
        }
        if (ub || !uefa || !apart) { printf("FAIL: the v15_stage_cup shape: %d bad of 1000, BIH with SRB %d\n", ub, uefa); fail++; }
    }
    int same = 0;
    for (int it = 0; it < 3000; it++) {                         /* clique pairs = the merged classes, draw for draw */
        const int g = 4 + (int)rnd(5), n = g * (2 + (int)rnd(4));
        uint8_t a[64], m[64], g1[64], g2[64], p[2] = { 20, 21 };
        for (int c = 0; c < n && c < 64; c++) {
            a[c] = (uint8_t)(20 + rnd(12));
            m[c] = a[c] == 21 ? 20 : a[c];
        }
        const uint32_t s = rnd(1u << 30);
        const int r1 = sr_group_draw_p(n, g, a, p, 1, s, g1), r2 = sr_group_draw(n, g, m, s, g2);
        if (r1 != r2 || (r1 > 0 && memcmp(g1, g2, (size_t)n))) { if (fail++ < 5) printf("FAIL: clique pair != merged class (n %d g %d)\n", n, g); }
        else same++;
    }
    int kc = 0, knone = 0;
    for (int it = 0; it < N1; it++) {                           /* sr_ko_pairs_x seeded: brute force */
        nk_ = 2 * (1 + (int)rnd(6));
        const int na = 3 + (int)rnd(5), ng = 2 + (int)rnd(4);
        n_ = nk_;
        for (int c = 0; c < nk_; c++) { as_[c] = (uint8_t)(10 + rnd((unsigned)na)); av_[c] = (uint8_t)rnd((unsigned)ng); }
        np_ = (int)rnd(3);
        for (int i = 0; i < np_; i++) {
            pr_[2 * i] = (uint8_t)(10 + rnd((unsigned)na));
            do pr_[2 * i + 1] = (uint8_t)(10 + rnd((unsigned)na)); while (pr_[2 * i + 1] == pr_[2 * i] && na > 1);
        }
        uint8_t out[16];
        const int exists = kbf(0, 0), r = sr_ko_pairs_x(SR_PAIR_SEEDED, nk_, av_, as_, pr_, np_, rs, out);
        int ok = r == nk_ / 2;
        unsigned seen = 0;
        for (int k = 0; ok && k < nk_; k += 2) {
            ok = out[k] < nk_ / 2 && out[k + 1] >= nk_ / 2 && !kbad(out[k], out[k + 1]);
            seen |= 1u << out[k] | 1u << out[k + 1];
        }
        kc++;
        knone += !exists;
        if (exists ? !ok || seen != (1u << nk_) - 1 : r != -2)
            if (fail++ < 5) printf("FAIL: ko seeded n %d pairs %d: exists %d, returned %d\n", nk_, np_, exists, r);
        uint8_t o1[16], o2[16];                                 /* one key, no pairs: sr_ko_pairs's own */
        const int r1 = sr_ko_pairs_x(SR_PAIR_DRAW, nk_, 0, as_, 0, 0, 9, o1), r2 = sr_ko_pairs(SR_PAIR_DRAW, nk_, as_, 9, o2);
        if (r1 != r2 || (r1 > 0 && memcmp(o1, o2, (size_t)nk_))) if (fail++ < 5) printf("FAIL: one key != sr_ko_pairs\n");
    }
    if (sr_ko_pairs_x(SR_PAIR_DRAW, 4, av_, as_, 0, 0, 1, av_ + 8) != -1) { printf("FAIL: draw with two keys not refused\n"); fail++; }
    if (fail) return 1;
    printf("PASS: sr_group_draw_p = brute force on %d shapes (n <= 12, protect pairs and chains; %d with no draw: -2), every draw keeps every pair apart\n", cases, none);
    printf("PASS: a protect chain KOS-BIH + KOS-SRB: drawn 1000 times, KOS never with either, BIH with SRB in %d, as they must be (merged into one class: refused, the control)\n", chained);
    printf("PASS: the v15_stage_cup shape (32 clubs, 8 groups, RUS-UKR, KOS-BIH, KOS-SRB): 1000 draws, every one whole and keeping every pair and association apart; BIH with SRB in %d, apart in %d\n", uefa, apart);
    printf("PASS: clique pairs = sr_group_draw on the merged classes, draw for draw (%d shapes)\n", same);
    printf("PASS: sr_ko_pairs_x seeded (same group + association + pairs) = brute-force perfect matching on %d rounds (%d with none: -2); one key = sr_ko_pairs; draw with two keys refused\n", kc, knone);
    return 0;
}
