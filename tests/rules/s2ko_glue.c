/*
 * s2ko_glue.c -- 42-s2ko: the KN glue end to end (the season end's numbers into the 'KN' block, the set-up's cup from
 * it) and what a refused cup leaves, under the sanitizers. A refused cup is a clean error: sr_kn_cup -2 with no tie
 * built and play never called (sr_kn_cup_check, run first), or -- a drawn round that does not fit -- -2 before that
 * round's first tie, the earlier rounds as played. A stop (play's -1) stays -1: the caller tells the two apart.
 *
 * - Coppa Italia: 44 idents through the block, the real 2026-27 bracket (Allegato 1): played; one ident twice in the
 *   block: refused, nothing built.
 * - Supercoppa: the champion also the cup winner -- sr_kn_number gives two clubs (the group's next), played; the same
 *   ident twice in the block: refused, nothing built.
 * - Coppa D: the season end's order and preliminary set, the set-up's sets (sr_kns_dpre), the 8 rounds (geographic
 *   blocks, the fixed bracket of winners, drawn two-leg semi-finals and final): played; a club in two of this season's
 *   groups (in the preliminary and the first round, or twice in one): refused, nothing built.
 * - A stop at a random tie: -1, the ties before it built, none after.
 * - A drawn round that does not fit (the semi-finals' geographic block under 2): -2 before its first tie; the earlier
 *   rounds the same as the full run's.
 * - The real Coppa Italia bracket with one side changed (a number or a winner, any value): refused, nothing built,
 *   unless the side is unchanged.
 *   s2ko_glue [ITERATIONS]
 *
 * Control (-DCONTROL, against a sr_kn.c without sr_kn_cup_check): the same cases, the refusals expected -- FAILs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"
#include "s2ko_gen.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint32_t rs = 2027;
static uint32_t rnd(uint32_t n) { rs = rs * 1664525u + 1013904223u; return n ? (uint32_t)(((uint64_t)(rs >> 8) * n) >> 24) : 0; }

static sr_kn_work work;

typedef struct { int calls, stop_at; uint32_t r; } play_ctx;
static int32_t play(void *vc, int32_t r, int32_t t, const sr_kn_tie *o)
{
    play_ctx *c = vc;
    (void)r; (void)t;
    if (o->home == o->away) return 0;       /* (a self-tie: counted by the callers' checks, not here) */
    if (c->calls++ == c->stop_at) return -1;
    c->r = c->r * 1664525u + 1013904223u;
    return (int32_t)(c->r >> 31);
}

#define SENT 0xA5
static int untouched(const sr_kn_tie *out, int from, int to)
{
    const uint8_t *p = (const uint8_t *)&out[from];
    for (size_t i = 0; i < (size_t)(to - from) * sizeof *out; i++) if (p[i] != SENT) return 0;
    return 1;
}
static int self_tie(const sr_kn_tie *out, int n)
{
    for (int t = 0; t < n; t++) if (out[t].home == out[t].away) return 1;
    return 0;
}

/* ---- Coppa Italia and Supercoppa: the idents through the block (the caller's ident -> code: code = ident - BASE) ---- */
#define BASE 0x0700
static int32_t ci_setup(const uint8_t *blk, int32_t len, uint8_t stamp, uint8_t id, const sr_kn_round *rd, int32_t nr,
                        const sr_kn_pair *pairs, int32_t want, play_ctx *c, sr_kn_tie *out)
{
    uint16_t idn[64], code[64];
    const int32_t n = sr_kns_idents(blk, len, stamp, id, idn, 64);
    if (n != want) return -3;                /* (missing: the fallbacks; not these cases) */
    for (int32_t i = 0; i < n; i++) code[i] = (uint16_t)(idn[i] - BASE);
    uint32_t rng = 11;
    return sr_kn_cup(rd, nr, pairs, code, n, NULL, NULL, &rng, play, c, &work, out);
}

static void cups(int it)
{
    static sr_kn_tie out[64];
    static uint8_t blk[SR_KNS_MAX];
    uint16_t ci[44], sc[2];
    int ok = 0, ref = 0, bad = 0, sc_ok = 0, sc_ref = 0;
    for (int s = 0; s < it; s++) {
        const uint8_t stamp = (uint8_t)(s % 255 + 1);
        for (int i = 0; i < 44; i++) {       /* (44 distinct clubs) */
            int again;
            do { ci[i] = (uint16_t)(BASE + rnd(SR_KN_CLUBS)); again = 0; for (int k = 0; k < i; k++) again |= ci[k] == ci[i]; } while (again);
        }
        sc[0] = ci[rnd(44)]; do sc[1] = ci[rnd(44)]; while (sc[1] == sc[0]);
        const int dup = (int)rnd(2);         /* (half the seasons: one ident twice) */
        if (dup) { const int i = (int)rnd(44); int j; do j = (int)rnd(44); while (j == i); ci[j] = ci[i]; sc[1] = sc[0]; }
        const int32_t len = sr_kns_build(stamp, ci, 44, sc, 2, NULL, 0, NULL, 0, blk, SR_KNS_MAX);
        if (len < 0) { bad++; continue; }
        play_ctx c = { 0, -1, (uint32_t)s };
        memset(out, SENT, sizeof out);
        int32_t w = ci_setup(blk, len, stamp, SR_KNS_CI, s2_ci_rounds, S2_CI_NR, s2_ci_pairs, 44, &c, out);
        if (dup) { ref += w == -2; bad += w != -2 || c.calls || !untouched(out, 0, 64); }
        else { ok += w >= 0; bad += w < 0 || c.calls != S2_CI_NP || self_tie(out, S2_CI_NP); }
        play_ctx c2 = { 0, -1, (uint32_t)s };
        memset(out, SENT, sizeof out);
        w = ci_setup(blk, len, stamp, SR_KNS_SC, s2_sc_rounds, S2_SC_NR, s2_sc_pairs, 2, &c2, out);
        if (dup) { sc_ref += w == -2; bad += w != -2 || c2.calls || !untouched(out, 0, 64); }
        else { sc_ok += w >= 0; bad += w < 0 || c2.calls != 1 || out[0].home == out[0].away; }
    }
    /* the champion also the cup winner: the numbers give the Supercoppa two clubs (the group's next) */
    static uint16_t champ[1], cupw[2], num[4];
    static uint8_t el[SR_KN_CLUBS];
    static sr_kn_rec rec[SR_KN_CLUBS];
    sr_kn_list lists[2];
    sr_kn_group g[2] = { { 1, SR_KN_LIST, SR_KN_LIST, 1, 0, { 0 }, { 0 } }, { 1, SR_KN_LIST, SR_KN_LIST, 1, 0, { 1 }, { 0 } } };
    champ[0] = 7; cupw[0] = 7; cupw[1] = 9;  /* (the double winner; then the cup's finalist) */
    lists[0].club = champ; lists[0].n = 1; lists[0].unknown = SR_KN_NONE;
    lists[1].club = cupw; lists[1].n = 2; lists[1].unknown = SR_KN_NONE;
    memset(el, 1, sizeof el);
    uint32_t rng = 1;
    const int32_t n = sr_kn_number(g, 2, lists, rec, el, &rng, &work, num, 4);
    play_ctx c = { 0, -1, 1 };
    memset(out, SENT, sizeof out);
    const int32_t w = n == 2 ? sr_kn_cup(s2_sc_rounds, S2_SC_NR, s2_sc_pairs, num, 2, NULL, NULL, &rng, play, &c, &work, out) : -9;
    const int dbl = n == 2 && num[0] == 7 && num[1] == 9 && w >= 0 && c.calls == 1;
    CHECK(!bad && dbl, "cups: %d bad, double winner %d (n %d w %d)", bad, dbl, n, w);
    printf("%s: Coppa Italia / Supercoppa through the 'KN' block, %d seasons: %d / %d played (the real bracket, no self-tie), %d / %d with one ident twice refused with nothing built and play never called; a double winner: the Supercoppa's second club the cup's finalist, played\n",
           !bad && dbl ? "PASS" : "FAIL", it, ok, sc_ok, ref, sc_ref);
}

/* ---- Coppa D: the season end's block, the set-up's 8 rounds ---- */
static const sr_kn_round d_rd[8] = {
    { 34, 0, 1, SR_KN_DRAW, SR_KN_PAIR_GEO, 2, 0, 68 }, { 64, 34, 1, SR_KN_DRAW, SR_KN_PAIR_GEO, 4, 68, 94 },
    { 32, 98, 1, SR_KN_DRAW, SR_KN_PAIR_FIXED, 0, 0, 0 }, { 16, 130, 1, SR_KN_ALTERNATE, SR_KN_PAIR_FIXED, 0, 0, 0 },
    { 8, 146, 1, SR_KN_ALTERNATE, SR_KN_PAIR_FIXED, 0, 0, 0 }, { 4, 154, 1, SR_KN_ALTERNATE, SR_KN_PAIR_FIXED, 0, 0, 0 },
    { 2, 158, 2, SR_KN_DRAW, SR_KN_PAIR_DRAW, 0, 0, 0 }, { 1, 160, 2, SR_KN_DRAW, SR_KN_PAIR_DRAW, 0, 0, 0 } };
static sr_kn_pair d_pairs[161];

static void coppa_d(int it)
{
    static sr_kn_geo geo[SR_KN_CLUBS];
    static uint16_t tab[9][18], nw[9][5], now[9][18], all[162], num[SR_KN_NUMBERS], ord[SR_KN_NUMBERS];
    static uint16_t pre[SR_KN_NUMBERS], rest[SR_KN_NUMBERS];
    static sr_kn_rec rec[SR_KN_CLUBS];
    static uint8_t el[SR_KN_CLUBS], blk[SR_KNS_MAX];
    static sr_kn_tie out[176], full[176];
    static sr_kn_entry join[162];
    sr_kn_list lists[18], groups[9];
    sr_kn_group g[2];
    sr_kn_round rd[8];
    int ok = 0, ref = 0, stops = 0, mid = 0, bad = 0;
    for (int r = 2; r < 6; r++)
        for (int t = 0; t < d_rd[r].ties; t++) {
            d_pairs[d_rd[r].first + t].a = SR_KN_WIN(r - 1, 2 * t);
            d_pairs[d_rd[r].first + t].b = SR_KN_WIN(r - 1, 2 * t + 1);
        }
    memset(&g, 0, sizeof g);
    g[0].take = 68; g[0].order = SR_KN_LIST; g[0].else_order = SR_KN_PLACE_DESC; g[0].nfrom = 9; g[0].nelse = 9;
    g[1].take = 94; g[1].order = SR_KN_LIST; g[1].else_order = SR_KN_LIST; g[1].nfrom = 9; g[1].nelse = 9;
    for (int i = 0; i < 9; i++) { g[0].from[i] = (uint8_t)i; g[0].els[i] = (uint8_t)(9 + i); g[1].from[i] = (uint8_t)(9 + i); g[1].els[i] = (uint8_t)i; }
    work.geo = geo;
    for (int s = 0; s < it; s++) {
        uint32_t rng = (uint32_t)s * 40503u + 5;
        const uint8_t stamp = (uint8_t)(s % 255 + 1);
        int b = 0;
        memset(el, 0, sizeof el); memset(rec, 0, sizeof rec);
        for (int i = 0; i < 207; i++) {
            const sr_kn_geo *x = &s2c_geo[s2d_num[rnd(162)]];
            geo[i].lat = (int16_t)(x->lat + (int)rnd(41) - 20); geo[i].lon = (int16_t)(x->lon + (int)rnd(41) - 20);
        }
        for (int i = 0; i < 162; i++) tab[i / 18][i % 18] = (uint16_t)i;
        for (int i = 161; i > 0; i--) { const int j = (int)rnd((uint32_t)i + 1); const uint16_t x = tab[i / 18][i % 18]; tab[i / 18][i % 18] = tab[j / 18][j % 18]; tab[j / 18][j % 18] = x; }
        for (int L = 0; L < 9; L++) {
            for (int p = 0; p < 18; p++) { rec[tab[L][p]].place = (uint8_t)(p + 1); rec[tab[L][p]].pld = 34; el[tab[L][p]] = p > 0 && p < 14; }
            for (int q = 0; q < 5; q++) { nw[L][q] = (uint16_t)(162 + L * 5 + q); el[nw[L][q]] = 1; }
            lists[L].club = nw[L]; lists[L].n = 5; lists[L].unknown = SR_KN_NONE;
            lists[9 + L].club = tab[L]; lists[9 + L].n = 18; lists[9 + L].unknown = SR_KN_NONE;
        }
        int m = 0;                              /* (this season's groups: the stayers and the new, shuffled) */
        for (int L = 0; L < 9; L++) for (int p = 1; p < 14; p++) all[m++] = tab[L][p];
        for (int L = 0; L < 9; L++) for (int q = 0; q < 5; q++) all[m++] = nw[L][q];
        for (int i = m - 1; i > 0; i--) { const int j = (int)rnd((uint32_t)i + 1); const uint16_t x = all[i]; all[i] = all[j]; all[j] = x; }
        for (int L = 0; L < 9; L++) {
            for (int p = 0; p < 18; p++) now[L][p] = all[L * 18 + p];
            groups[L].club = now[L]; groups[L].n = 18; groups[L].unknown = SR_KN_NONE;
        }
        /* the season end */
        const int32_t n = sr_kn_number(g, 2, lists, rec, el, &rng, &work, num, SR_KN_NUMBERS);
        int32_t nd = sr_kn_dorder(groups, 9, ord, SR_KN_NUMBERS);
        const int32_t len = sr_kns_build(stamp, NULL, 0, NULL, 0, ord, nd, num, 68, blk, SR_KNS_MAX);
        if (n != 162 || nd != 162 || len < 0) { bad++; continue; }
        /* the set-up: a third of the seasons with a club in two of this season's groups (the file's slip) */
        const int two = s % 3 == 0;
        if (two) {
            const int a = (int)rnd(162);
            int c2;
            do c2 = (int)rnd(162); while (c2 / 18 == a / 18);
            now[c2 / 18][c2 % 18] = now[a / 18][a % 18];
            nd = sr_kn_dorder(groups, 9, ord, SR_KN_NUMBERS);
        }
        int32_t npre = 0;
        if (sr_kns_dpre(blk, len, stamp, ord, nd, pre, rest, &npre) != 162 || npre != 68) { bad++; continue; }
        for (int i = 0; i < 68; i++) { join[i].club = pre[i]; join[i].zone = 0; }
        for (int i = 0; i < 94; i++) { join[68 + i].club = rest[i]; join[68 + i].zone = 0; }
        uint32_t r1 = rng;
        play_ctx c = { 0, -1, (uint32_t)s };
        memset(full, SENT, sizeof full);
        const int32_t w = sr_kn_cup(d_rd, 8, d_pairs, NULL, 0, join, NULL, &r1, play, &c, &work, full);
        if (two) { ref += w == -2; b += w != -2 || c.calls || !untouched(full, 0, 176); }
        else { ok += w >= 0; b += w < 0 || c.calls != 161 || self_tie(full, 161); }
        if (!two && w >= 0) {
            /* a stop at a random tie: -1, the ties before it built, none after */
            const int k = (int)rnd(161);
            uint32_t r2 = rng;
            play_ctx cs = { 0, k, (uint32_t)s };
            memset(out, SENT, sizeof out);
            const int32_t ws = sr_kn_cup(d_rd, 8, d_pairs, NULL, 0, join, NULL, &r2, play, &cs, &work, out);
            int at = 0, end = 0;                /* (the stopped tie's index, ties in round order; a drawn round's
                                                   ties all drawn before its first is played) */
            for (int r = 0, seen = 0; r < 8; r++) {
                if (k < seen + d_rd[r].ties) {
                    at = d_rd[r].first + (k - seen);
                    end = d_rd[r].pairing != SR_KN_PAIR_FIXED ? d_rd[r].first + d_rd[r].ties : at + 1;
                    break;
                }
                seen += d_rd[r].ties;
            }
            stops += ws == -1;
            b += ws != -1 || cs.calls != k + 1 || !untouched(out, end, 176) || memcmp(out, full, sizeof *out * (size_t)at);
            /* the semi-finals' draw does not fit (a geographic block under 2): -2 before their first tie */
            memcpy(rd, d_rd, sizeof rd);
            rd[6].pairing = SR_KN_PAIR_GEO; rd[6].block = 0;
            uint32_t r3 = rng;
            play_ctx cm = { 0, -1, (uint32_t)s };
            memset(out, SENT, sizeof out);
            const int32_t wm = sr_kn_cup(rd, 8, d_pairs, NULL, 0, join, NULL, &r3, play, &cm, &work, out);
            mid += wm == -2;
            b += wm != -2 || cm.calls != 158 || !untouched(out, 158, 176) || memcmp(out, full, sizeof *out * 158);
        }
        if (b && bad++ < 3) printf("  Coppa D season %d: %d bad (two %d)\n", s, b, two);
    }
    CHECK(!bad, "Coppa D glue: %d bad", bad);
    printf("%s: Coppa Italia Serie D through the 'KN' block, %d seasons: %d played (161 ties, no self-tie); %d with a club in two of this season's groups refused with nothing built and play never called; %d stops at a random tie: -1, the ties before it as in the full run, none after; %d semi-final draws that do not fit: -2 before their first tie, rounds 1-6 as in the full run\n",
           bad ? "FAIL" : "PASS", it, ok, ref, stops, mid);
}

/* ---- the real Coppa Italia bracket, one side changed ---- */
static void sides(int it)
{
    static sr_kn_pair p[64];
    static sr_kn_tie out[64];
    uint16_t code[44];
    int ref = 0, same = 0, bad = 0, chk = 0;
    for (int i = 0; i < 44; i++) code[i] = (uint16_t)i;
    for (int s = 0; s < it; s++) {
        memcpy(p, s2_ci_pairs, sizeof s2_ci_pairs);
        const int at = (int)rnd(S2_CI_NP), side = (int)rnd(2);
        uint16_t *v = side ? &p[at].b : &p[at].a;
        const uint16_t was = *v;
        const int r = (int)rnd(S2_CI_NR);       /* (a number 0 .. 45, or a winner of any round / tie) */
        *v = rnd(2) ? (uint16_t)rnd(46) : SR_KN_WIN(r, rnd(s2_ci_rounds[r].ties + 1));
        play_ctx c = { 0, -1, (uint32_t)s };
        uint32_t rng = 3;
        memset(out, SENT, sizeof out);
        const int32_t w = sr_kn_cup(s2_ci_rounds, S2_CI_NR, p, code, 44, NULL, NULL, &rng, play, &c, &work, out);
        if (*v == was) { same++; bad += w < 0 || self_tie(out, S2_CI_NP); continue; }
        ref += w == -2;
        bad += w != -2 || c.calls || !untouched(out, 0, 64);
#ifndef CONTROL
        chk += sr_kn_cup_check(s2_ci_rounds, S2_CI_NR, p, code, 44, NULL) != -1;
#endif
    }
#ifndef CONTROL
    chk += sr_kn_cup_check(s2_ci_rounds, S2_CI_NR, s2_ci_pairs, code, 44, NULL) != 0;
    chk += sr_kn_cup_check(d_rd, 8, d_pairs, NULL, 0, NULL) != -1;           /* (drawn rounds without their joiners) */
#endif
    CHECK(!bad && !chk, "sides: %d bad, check %d", bad, chk);
    printf("%s: the real Coppa Italia bracket with one side changed, %d cases: %d refused with nothing built and play never called (a number twice, out of 1 .. 44, a winner taken twice or of a later round), %d unchanged played; sr_kn_cup_check agrees\n",
           !bad && !chk ? "PASS" : "FAIL", it, ref, same);
}

int main(int argc, char **argv)
{
    const int it = argc > 1 ? atoi(argv[1]) : 2000;
    cups(it);
    coppa_d(it);
    sides(it * 5);
    return fails ? 1 : 0;
}
