/*
 * s2ko_edge.c -- 42-s2ko: the edge cases of the bracket numbers, the drawn rounds, the 'KN' block and the step-3
 * play-off, under the sanitizers (random inputs, invariants; what is refused, refused without a write past a buffer).
 *
 * - sr_kn_number: groups over lists with clubs past SR_KN_CLUBS, a club in two lists / two groups, unknown lists
 *   chained (a cycle included), nobody eligible, 'max' too small: -1, or numbers all eligible, distinct, <= max;
 *   the same rng, the same numbers.
 * - sr_kn_cup, drawn rounds: byes (a round whose pool is not 2 x its ties), odd zones, blocks under 2, joiners more
 *   than half the pool: -1; else every round's clubs once each, home != away, the winner one of the two.
 * - The 'KN' block: random bytes and every one-byte change of a real block: read as missing, or within its buffers.
 * - The step-3 play-off: an unknown bracket, fewer than 7 clubs, a season of 0 / 33 divisions: -1; random tables of 7 ..
 *   24 clubs: the winner one of the 2nd .. 7th, the plain form = SR_LP_FIXED.
 *   s2ko_edge [ITERATIONS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint32_t rs = 2026;
static uint32_t rnd(uint32_t n) { rs = rs * 1664525u + 1013904223u; return n ? (uint32_t)(((uint64_t)(rs >> 8) * n) >> 24) : 0; }

static sr_kn_work work;

/* ---- sr_kn_number ---- */
static void numbers(int it)
{
    static uint16_t club[18][40], out[SR_KN_NUMBERS], out2[SR_KN_NUMBERS];
    static uint8_t el[SR_KN_CLUBS], seen[SR_KN_CLUBS];
    static sr_kn_rec rec[SR_KN_CLUBS];
    sr_kn_list lists[18];
    sr_kn_group g[9];
    int bad = 0, refused = 0, dup2 = 0;
    for (int s = 0; s < it; s++) {
        const int nl = 1 + (int)rnd(18), ng = 1 + (int)rnd(9), pool = 8 + (int)rnd(SR_KN_CLUBS + 40 - 8);
        for (int k = 0; k < SR_KN_CLUBS; k++) {
            el[k] = rnd(10) < 8;
            rec[k].pts = (uint16_t)rnd(100); rec[k].pld = (uint16_t)rnd(40); rec[k].won = (uint16_t)rnd(30);
            rec[k].gf = (uint16_t)rnd(90); rec[k].place = (uint8_t)(1 + rnd(20));
        }
        if (rnd(20) == 0) memset(el, 0, sizeof el);                     /* (nobody eligible) */
        for (int l = 0; l < nl; l++) {
            const int n = (int)rnd(41);
            for (int j = 0; j < n; j++) club[l][j] = (uint16_t)rnd((uint32_t)pool);   /* (past SR_KN_CLUBS too) */
            if (l && rnd(4) == 0) club[l][0] = club[l - 1][0];            /* (a club in two lists) */
            lists[l].club = club[l];
            lists[l].n = rnd(6) == 0 ? -1 : n;                           /* (unknown: its fallback) */
            lists[l].unknown = rnd(3) == 0 ? SR_KN_NONE : (int32_t)rnd((uint32_t)nl);   /* (itself: a cycle) */
        }
        memset(g, 0, sizeof g);
        for (int i = 0; i < ng; i++) {
            g[i].take = (uint8_t)rnd(41); g[i].order = (uint8_t)rnd(4); g[i].else_order = (uint8_t)rnd(4);
            g[i].nfrom = (uint8_t)rnd(17); g[i].nelse = (uint8_t)rnd(17);
            for (int k = 0; k < 16; k++) { g[i].from[k] = (uint8_t)rnd((uint32_t)nl); g[i].els[k] = (uint8_t)rnd((uint32_t)nl); }
        }
        const int max = rnd(4) == 0 ? (int)rnd(40) : SR_KN_NUMBERS;
        uint32_t r1 = s * 2654435761u, r2 = r1;
        const int n = sr_kn_number(g, ng, lists, rec, el, &r1, &work, out, max);
        const int n2 = sr_kn_number(g, ng, lists, rec, el, &r2, &work, out2, max);
        int b = n != n2 || (n > 0 && memcmp(out, out2, 2u * (unsigned)n));
        if (n < 0) { refused++; bad += b; continue; }
        int want = 0;
        for (int i = 0; i < ng; i++) want += g[i].take;
        b += n != want || n > max;
        memset(seen, 0, sizeof seen);
        for (int i = 0; i < n; i++) {
            if (out[i] >= SR_KN_CLUBS || !el[out[i]] || seen[out[i]]) { b++; break; }
            seen[out[i]] = 1;
        }
        for (int l = 1; l < nl; l++) dup2 += lists[l].n > 0 && lists[l - 1].n > 0 && club[l][0] == club[l - 1][0];
        if (b && bad++ < 3) printf("  numbers case %d: %d bad\n", s, b);
    }
    CHECK(!bad, "sr_kn_number: %d bad", bad);
    printf("%s: sr_kn_number, %d random groups and lists (clubs past %d, %d clubs in two lists, unknown chains and cycles, nobody eligible, max too small): %d refused, the rest all eligible, distinct, the groups' takes, the same with the same rng\n",
           bad ? "FAIL" : "PASS", it, SR_KN_CLUBS, dup2, refused);
}

/* ---- sr_kn_cup: drawn rounds ---- */
static int32_t play_any(void *c, int32_t r, int32_t t, const sr_kn_tie *o) { (void)c; (void)r; (void)t; (void)o; return (int32_t)rnd(2); }

static void drawn(int it)
{
    static sr_kn_geo geo[SR_KN_CLUBS];
    static sr_kn_entry join[SR_KN_NUMBERS];
    static sr_kn_tie out[SR_KN_NUMBERS + 8];
    static uint8_t seen[SR_KN_CLUBS];
    const uint8_t homes[] = { SR_KN_DRAW, SR_KN_NEUTRAL, SR_KN_LOWER, SR_KN_JOINER, SR_KN_ALTERNATE };
    const uint8_t pairs[] = { SR_KN_PAIR_DRAW, SR_KN_PAIR_ZONE, SR_KN_PAIR_GEO };
    int bad = 0, ok = 0, refused = 0, byes = 0, byes_ok = 0;
    for (int k = 0; k < SR_KN_CLUBS; k++) { geo[k].lat = (int16_t)(3700 + rnd(1000)); geo[k].lon = (int16_t)(700 + rnd(1100)); }
    work.geo = geo;
    for (int s = 0; s < it; s++) {
        sr_kn_round rd[5];
        const int nr = 1 + (int)rnd(5);
        int first = 0, jf = 0, prev = 0, bye = 0, nj_all = 0;
        for (int r = 0; r < nr; r++) {
            int ties = r == 0 ? 1 + (int)rnd(40) : prev / 2 + (int)rnd(3);
            if (ties < 1) ties = 1;
            int nj = 2 * ties - (r ? prev : 0);
            if (rnd(6) == 0) nj += (int)rnd(5) - 2;                       /* (a bye: the pool not 2 x the ties) */
            if (nj < 0) nj = 0;
            if (jf + nj > SR_KN_NUMBERS || first + ties > SR_KN_NUMBERS) { nj = 0; ties = 1; }
            bye |= nj + (r ? prev : 0) != 2 * ties;
            rd[r].ties = (uint16_t)ties; rd[r].first = (uint16_t)first; rd[r].legs = (uint8_t)(1 + rnd(2));
            rd[r].home = homes[rnd(sizeof homes)]; rd[r].pairing = pairs[rnd(sizeof pairs)];
            rd[r].block = (uint8_t)(rnd(4) == 0 ? rnd(2) : 2 * (1 + rnd(3)));   /* (under 2: refused) */
            rd[r].jfirst = (uint16_t)jf; rd[r].njoin = (uint16_t)nj;
            first += ties; jf += nj; prev = ties;
        }
        nj_all = jf;
        for (int i = 0; i < nj_all; i++) {                               /* (distinct clubs) */
            join[i].club = (uint16_t)i; join[i].zone = (uint8_t)rnd(4);
        }
        for (int i = nj_all - 1; i > 0; i--) { const int j = (int)rnd((uint32_t)i + 1); const uint16_t x = join[i].club; join[i].club = join[j].club; join[j].club = x; }
        memset(out, 0, sizeof out);
        uint32_t rng = (uint32_t)s * 40503u + 1;
        const int w = sr_kn_cup(rd, nr, NULL, NULL, 0, join, NULL, &rng, play_any, NULL, &work, out);
        byes += bye;
        if (w < 0) { refused++; continue; }
        ok++;
        int b = 0;
        byes_ok += bye;
        b += bye;                                                         /* (a bye accepted) */
        for (int r = 0; r < nr && !b; r++) {
            memset(seen, 0, sizeof seen);
            for (int t = 0; t < rd[r].ties; t++) {
                const sr_kn_tie *o = &out[rd[r].first + t];
                if (o->home == o->away || o->home >= SR_KN_CLUBS || o->away >= SR_KN_CLUBS || seen[o->home] || seen[o->away]
                    || (o->winner != o->home && o->winner != o->away)) { b++; break; }
                seen[o->home] = seen[o->away] = 1;
            }
        }
        if (b && bad++ < 3) printf("  drawn case %d: %d bad (bye %d)\n", s, b, bye);
    }
    CHECK(!bad, "sr_kn_cup drawn: %d bad", bad);
    printf("%s: sr_kn_cup, %d random drawn cups (draw / zone / geo, hosts drawn / neutral / lower / joiner / alternate, %d with a bye): %d played, %d refused, no bye accepted (%d), every round's clubs once, home != away, the winner one of the two\n",
           bad ? "FAIL" : "PASS", it, byes, ok, refused, byes_ok);
}

/* a club in two chains: a drawn round's pool with one club twice -- refused, never a tie of a club with itself */
static void twice(int it)
{
    static sr_kn_geo geo[SR_KN_CLUBS];
    const uint8_t pr[3] = { SR_KN_PAIR_DRAW, SR_KN_PAIR_GEO, SR_KN_PAIR_ZONE };
    int acc = 0, ctl = 0;
    for (int i = 0; i < SR_KN_CLUBS; i++) { geo[i].lat = (int16_t)(3700 + 3 * i); geo[i].lon = (int16_t)(900 + (7 * i) % 800); }
    work.geo = geo;
    for (int s = 0; s < it; s++) {
        /* round 0: 8 ties of 16 joiners; round 1: its 8 winners + 8 joiners -- the duplicate in either round */
        sr_kn_round rd[2] = { { 8, 0, 1, SR_KN_DRAW, pr[s % 3], 2, 0, 16 }, { 8, 8, 1, SR_KN_DRAW, pr[(s / 3) % 3], 2, 16, 8 } };
        sr_kn_entry j[24];
        sr_kn_tie out[16];
        for (int i = 0; i < 24; i++) { j[i].club = (uint16_t)(100 + i); j[i].zone = 0; }
        const int a = (int)rnd(24), b = (int)rnd(24);
        if (a != b) j[a].club = j[b].club;
        uint32_t rng = (uint32_t)s + 1;
        const int w = sr_kn_cup(rd, 2, NULL, NULL, 0, j, NULL, &rng, play_any, NULL, &work, out);
        if (a == b) { ctl += w >= 0; continue; }
        if (w >= 0) {             /* (only when the duplicate's first copy went out in round 0: then distinct) */
            int self = 0;
            for (int t = 0; t < 16; t++) self += out[t].home == out[t].away;
            acc += self != 0;
        }
    }
    CHECK(!acc && ctl > 0, "a club twice: %d cups with a club against itself, %d controls played", acc, ctl);
    printf("%s: a club in two chains, %d drawn cups with one club twice in a round's pool: never a tie of a club with itself (%d); control: no duplicate, %d played\n",
           !acc && ctl > 0 ? "PASS" : "FAIL", it, acc, ctl);
}

/* ---- the 'KN' block ---- */
static void block(int it)
{
    static uint8_t b[SR_KNS_MAX + 16], blk[SR_KNS_MAX];
    static uint16_t ci[44], sc[2], ord[162], pre[256], rest[256], got[256];
    int bad = 0, read = 0;
    for (int i = 0; i < 44; i++) ci[i] = (uint16_t)(0x700 + i);
    sc[0] = ci[3]; sc[1] = ci[9];
    for (int i = 0; i < 162; i++) ord[i] = (uint16_t)(1000 + i);
    const int len = sr_kns_build(7, ci, 44, sc, 2, ord, 162, ord + 94, 68, blk, SR_KNS_MAX);
    CHECK(len == 121, "the real block: %d B", len);
    for (int s = 0; s < it; s++) {
        int n, np = 0, bl;
        const uint8_t stamp = (uint8_t)rnd(256);
        if (s & 1) {                                                      /* (random bytes) */
            bl = (int)rnd(sizeof b);
            for (int i = 0; i < bl; i++) b[i] = (uint8_t)rnd(256);
            if (bl > 0 && rnd(2)) b[0] = stamp;
            if (bl > 1 && rnd(2)) b[1] = (uint8_t)rnd(4);
        } else {                                                          /* (the real block, one byte changed) */
            memcpy(b, blk, (size_t)len);
            bl = len - (int)rnd(3);
            b[rnd((uint32_t)len)] = (uint8_t)rnd(256);
            if (rnd(2)) b[0] = 7;
        }
        n = sr_kns_idents(b, bl, b[0], SR_KNS_CI, got, 64);
        bad += n > 64;
        n = sr_kns_idents(b, bl, b[0], SR_KNS_SC, got, 2);
        bad += n > 2;
        n = sr_kns_dpre(b, bl, b[0], ord, 162, pre, rest, &np);
        bad += n >= 0 && (n != 162 || np < 0 || np > 162);
        n = sr_kns_dpre(b, bl, b[0], ord, (int32_t)rnd(163), pre, rest, &np);
        read += n >= 0;
        bad += sr_kns_idents(b, bl, b[0], 0, got, 64) != -1 || sr_kns_idents(b, bl, b[0], SR_KNS_D, got, 64) != -1;
    }
    CHECK(!bad, "KN block: %d bad", bad);
    printf("%s: the 'KN' block, %d random blocks and one-byte changes of a real one (121 B): every read within its buffers, the counts within their bounds (%d D reads accepted), a part id other than Coppa Italia / Supercoppa refused by sr_kns_idents\n",
           bad ? "FAIL" : "PASS", it, read);
}

/* ---- the step-3 play-off ---- */
static void step3(int it)
{
    static sr_lp_club c[SR_LP_CLUBS];
    static sr_row t[SR_LP_CLUBS];
    int bad = 0, v2, v3;
    bad += sr_lp_semis(4, 5, 2, &v2, &v3) != -1 || sr_lp_semis(4, 5, SR_LP_FIXED, NULL, &v3) != -1;
    bad += sr_lp_semis(3, 5, SR_LP_FIXED, &v2, &v3) != 0 || v2 != 5 || v3 != 3;
    bad += sr_lp_semis(3, 5, SR_LP_RESEED, &v2, &v3) != 0 || v2 != 5 || v3 != 3;
    bad += sr_lp_semis(6, 4, SR_LP_RESEED, &v2, &v3) != 0 || v2 != 6 || v3 != 4;
    for (int k = 0; k < SR_LP_CLUBS; k++) { c[k].ident = (uint16_t)k; c[k].strength = (uint16_t)(250 + rnd(300)); }
    for (int k = 0; k < SR_LP_CLUBS; k++) t[k].team = (uint16_t)k;
    bad += sr_lp_playoff_b(1, 0, t, 6, c, SR_LP_SPREAD, SR_LP_FIXED) != -1;
    bad += sr_lp_playoff_b(1, 0, t, 22, c, SR_LP_SPREAD, 2) != -1;
    bad += sr_lp_playoff_b(1, 0, NULL, 22, c, SR_LP_SPREAD, SR_LP_FIXED) != -1;
    {
        const sr_lp_club *cc[1] = { c };
        int32_t n6[1] = { 6 }, up[2];
        bad += sr_lp_season_b(1, 0, cc, n6, up, SR_LP_SPREAD, SR_LP_FIXED) != -1;
        bad += sr_lp_season_b(1, SR_LP_DIVS + 1, cc, n6, up, SR_LP_SPREAD, SR_LP_FIXED) != -1;
        bad += sr_lp_season_b(1, 1, cc, n6, up, SR_LP_SPREAD, SR_LP_FIXED) != -1;
    }
    int differ = 0;
    for (int s = 0; s < it; s++) {
        const int n = 7 + (int)rnd(SR_LP_CLUBS - 6);
        for (int k = 0; k < n; k++) { const int j = (int)rnd((uint32_t)k + 1); const uint16_t x = t[k].team; t[k].team = t[j].team; t[j].team = x; }
        for (int k = 0; k < n; k++) if (t[k].team >= n) t[k].team = (uint16_t)k;   /* (only this table's clubs) */
        {
            static uint8_t u[SR_LP_CLUBS];
            memset(u, 0, sizeof u);
            for (int k = 0; k < n; k++) { if (u[t[k].team]) t[k].team = 0xFFFF; else u[t[k].team] = 1; }
            for (int k = 0, f = 0; k < n; k++) if (t[k].team == 0xFFFF) { while (u[f]) f++; t[k].team = (uint16_t)f; u[f] = 1; }
        }
        const uint32_t seed = (uint32_t)s * 7919u;
        const int wf = sr_lp_playoff_b(seed, s & 3, t, n, c, SR_LP_SPREAD, SR_LP_FIXED);
        const int wr = sr_lp_playoff_b(seed, s & 3, t, n, c, SR_LP_SPREAD, SR_LP_RESEED);
        const int wp = sr_lp_playoff(seed, s & 3, t, n, c);
        bad += wf < 1 || wf > 6 || wr < 1 || wr > 6 || wp != wf;
        differ += wf != wr;
    }
    CHECK(!bad, "step 3: %d bad", bad);
    printf("%s: the step-3 play-off: an unknown bracket, no pointer, 6 clubs, a season of 0 / %d divisions refused; the semi-finals by bracket (fixed 2nd v W(5v6), re-seeded 2nd v the lower placed); %d random tables of 7 .. 24 clubs: the winner one of the 2nd .. 7th, the plain form = fixed, the two brackets' winners differ in %d\n",
           bad ? "FAIL" : "PASS", SR_LP_DIVS + 1, it, differ);
}

int main(int argc, char **argv)
{
    const int it = argc > 1 ? atoi(argv[1]) : 20000;
    numbers(it);
    drawn(it);
    twice(it);
    block(it);
    step3(it);
    return fails != 0;
}
