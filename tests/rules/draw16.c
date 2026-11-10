/* swc/1.6 draws (Regia 2026-11-10, CAF CL / CC regulations art. 17): sr_group_draw_k, at most cap clubs of one
 * association per group.
 * - brute force, cap 2, n <= 12: a draw exists (pots, sizes, two at most of an association per group) exactly when the
 *   library returns one (-2 otherwise, never -3), and every draw it returns keeps them all;
 * - cap 1 = sr_group_draw_p, draw for draw (with and without protect pairs);
 * - up to 64 clubs, cap 2: a draw exactly when every association has at most 2g clubs, every draw kept;
 * - a CAF shape (16 clubs, 4 groups, an association with 3): two of its clubs share a group in some draws, not all (cap 1
 *   would forbid it), never three; with 9 clubs (> 2 x 4): -2;
 * - cap 2 with protect pairs, cap 0 / 3: -1. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "swosrules.h"

static uint32_t rs = 4242;
static uint32_t rnd(uint32_t n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }

static int n_, g_;
static uint8_t as_[64], size_[64], grp_[64];
static int valid(const uint8_t *grp, int cap)
{
    int pot[64][32] = { { 0 } }, sz[32] = { 0 };
    for (int c = 0; c < n_; c++) {
        if (grp[c] >= g_) return 0;
        if (pot[c / g_][grp[c]]++) return 0;                    /* one club of a pot per group */
        int k = 0;
        for (int d = 0; d < n_; d++) k += grp[d] == grp[c] && as_[d] == as_[c];
        if (k > cap) return 0;
        sz[grp[c]]++;
    }
    for (int j = 0; j < g_; j++) if (sz[j] != size_[j]) return 0;
    return 1;
}
static int bf(int c)
{
    if (c == n_) return valid(grp_, 2);
    for (int j = 0; j < g_; j++) {
        int ok = 1, k = 0;
        for (int d = c - c % g_; d < c && ok; d++) ok = grp_[d] != j;
        for (int d = 0; d < c; d++) k += grp_[d] == j && as_[d] == as_[c];
        if (!ok || k >= 2) continue;
        grp_[c] = (uint8_t)j;
        if (bf(c + 1)) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int fail = 0, cases = 0, none = 0, same = 0, big = 0, bnone = 0, shared = 0;
    const int N1 = argc > 1 ? atoi(argv[1]) : 30000;
    uint8_t out[64], o2[64];
    for (int it = 0; it < N1; it++) {                           /* cap 2: brute force */
        g_ = 2 + (int)rnd(3);
        n_ = g_ * 2 + (int)rnd((unsigned)(13 - g_ * 2));
        if (n_ > 12) n_ = 12;
        const int na = 1 + (int)rnd(4);
        for (int c = 0; c < n_; c++) as_[c] = (uint8_t)(10 + rnd((unsigned)na));
        sr_group_sizes(n_, g_, size_);
        const uint32_t seed = rnd(1u << 30);
        const int r = sr_group_draw_k(n_, g_, as_, 0, 0, 2, seed, out), want = bf(0);
        cases++;
        if (r == g_) { if (!want || !valid(out, 2)) { fail++; if (fail < 5) printf("FAIL: brute force it %d: drawn %s\n", it, want ? "but invalid" : "but none exists"); } }
        else if (r == -2) { none++; if (want) { fail++; if (fail < 5) printf("FAIL: brute force it %d: -2 but a draw exists\n", it); } }
        else { fail++; if (fail < 5) printf("FAIL: brute force it %d: returned %d\n", it, r); }
    }
    for (int it = 0; it < 3000; it++) {                         /* cap 1 = sr_group_draw_p */
        g_ = 2 + (int)rnd(15);
        n_ = g_ * 2 + (int)rnd((unsigned)(65 - g_ * 2));
        if (n_ > 64) n_ = 64;
        const int na = 2 + (int)rnd(20);
        for (int c = 0; c < n_; c++) as_[c] = (uint8_t)(10 + rnd((unsigned)na));
        uint8_t pr[8];
        int np = (int)rnd(3);
        for (int i = 0; i < np; i++) { pr[2 * i] = (uint8_t)(10 + 2 * i); pr[2 * i + 1] = (uint8_t)(11 + 2 * i); }
        const uint32_t seed = rnd(1u << 30);
        memset(out, 0xEE, sizeof out); memset(o2, 0xEE, sizeof o2);
        const int a = sr_group_draw_k(n_, g_, as_, np ? pr : 0, np, 1, seed, out), b = sr_group_draw_p(n_, g_, as_, np ? pr : 0, np, seed, o2);
        if (a != b || memcmp(out, o2, sizeof out)) { fail++; if (fail < 5) printf("FAIL: cap 1 it %d: %d vs sr_group_draw_p %d\n", it, a, b); }
        else same++;
    }
    for (int it = 0; it < 3000; it++) {                         /* big shapes, cap 2: exact */
        g_ = 2 + (int)rnd(15);
        n_ = g_ * 2 + (int)rnd((unsigned)(65 - g_ * 2));
        if (n_ > 64) n_ = 64;
        const int na = 1 + (int)rnd(12);
        int cnt[32] = { 0 }, mx = 0;
        for (int c = 0; c < n_; c++) { as_[c] = (uint8_t)(10 + rnd((unsigned)na)); if (++cnt[as_[c] - 10] > mx) mx = cnt[as_[c] - 10]; }
        sr_group_sizes(n_, g_, size_);
        const int r = sr_group_draw_k(n_, g_, as_, 0, 0, 2, rnd(1u << 30), out);
        big++;
        if (mx <= 2 * g_ ? r != g_ || !valid(out, 2) : r != -2) {
            fail++; if (fail < 5) printf("FAIL: big it %d: n %d g %d largest %d -> %d\n", it, n_, g_, mx, r);
        }
        bnone += r == -2;
    }
    {                                                           /* a CAF shape: 16 clubs in 4 groups, EGY 5 */
        static const uint8_t caf[16] = { 1, 2, 3, 4, 11, 5, 6, 7, 1, 2, 8, 9, 12, 3, 1, 10 };
        n_ = 16; g_ = 4;
        memcpy(as_, caf, 16);
        sr_group_sizes(n_, g_, size_);
        for (int s = 0; s < 5000; s++) {
            const int r = sr_group_draw_k(16, 4, as_, 0, 0, 2, (uint32_t)s * 2654435761u, out);
            if (r != 4 || !valid(out, 2)) { fail++; printf("FAIL: CAF seed %d: %d\n", s, r); break; }
            int two = 0;
            for (int j = 0; j < 4; j++) { int k = 0; for (int c = 0; c < 16; c++) k += out[c] == j && as_[c] == 1; two |= k == 2; }
            shared += two;
        }
        if (!shared || shared == 5000) { fail++; printf("FAIL: CAF: two clubs of one association share a group in %d of 5000 (0: cap 1 behaviour)\n", shared); }
        static const uint8_t c9[18] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        memcpy(as_, c9, 18);
        if (sr_group_draw_k(18, 4, as_, 0, 0, 2, 7, out) != -2) { fail++; printf("FAIL: 9 clubs of one association in 4 groups not refused\n"); }
        const uint8_t pr[2] = { 1, 2 };
        if (sr_group_draw_k(16, 4, caf, pr, 1, 2, 7, out) != -1 || sr_group_draw_k(16, 4, caf, 0, 0, 3, 7, out) != -1 ||
            sr_group_draw_k(16, 4, caf, 0, 0, 0, 7, out) != -1) { fail++; printf("FAIL: cap 2 + pairs / cap 3 / cap 0 not refused\n"); }
    }
    if (fail) { printf("FAIL: draw16, %d case(s) failed\n", fail); return 1; }
    printf("PASS: draw16 (swc/1.6 cap): %d brute-force shapes cap 2 (%d with no draw), every draw kept and none missed; "
           "cap 1 = sr_group_draw_p in %d draws; %d shapes up to 64 clubs: a draw exactly when no association has over 2g "
           "(%d refused); CAF 16 in 4 (EGY 3): two share a group in %d of 5000, never three; 9 in 4 groups -2; pairs / cap 3 / 0 -1\n",
           cases, none, same, big, bnone, shared);
    return 0;
}
