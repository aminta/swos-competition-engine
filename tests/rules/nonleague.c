/* England's non-league (Davide 07/11 B): the step-3 light pool (sr_pool.c) and the summer
 * re-allocation of National League North / South (sr_pg_realloc). Nothing in the Mod plays them yet,
 * so these are invariants, each with a control that breaks it and must be caught.
 *   light tables  complete and consistent (42 played, points = 3-1-0, goals balance, ranking),
 *                 the same whatever the clubs' order, the stronger clubs higher on average
 *   play-offs     the winner one of 2nd-7th, every place able to win, 2nd more often than 7th
 *   season        8 distinct clubs up, each division's champion among them
 *   re-allocation 24 / 24, no strict club across, the flexible ones split by rank (north first),
 *                 the same clubs give the same split in any slots, a second pass changes nothing;
 *                 strict clubs past a group's size cross, the southernmost first
 *   chain level   a club whose two choices are full placed one step deeper, every club within its choices
 * Argument: SEASONS (default 2000). Prints PASS / FAIL lines; exit 1 on a FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

static unsigned seed_;
static unsigned rnd(unsigned n) { seed_ = seed_ * 1103515245u + 12345u; return (seed_ >> 8) % n; }

/* ---- light tables ---- */

static int table_ok(const sr_row *t, int n)
{
    int won = 0, pts = 0, gf = 0, ga = 0;
    for (int k = 0; k < n; k++) {
        won += t[k].won; pts += t[k].pts; gf += t[k].gf; ga += t[k].ga;
        if (k) {
            const int dp = t[k - 1].pts - t[k].pts;
            const int dd = (t[k - 1].gf - t[k - 1].ga) - (t[k].gf - t[k].ga);
            if (dp < 0 || (dp == 0 && dd < 0) || (dp == 0 && dd == 0 && t[k - 1].gf < t[k].gf)) return 0;
        }
    }
    const int draws = (pts - 3 * won) / 2;
    return gf == ga && (pts - 3 * won) % 2 == 0 && won + draws == n * (n - 1);
}

static void make_div(sr_lp_club *c, int n, unsigned s)
{
    seed_ = s;
    for (int i = 0; i < n; i++) { c[i].ident = (uint16_t)(1000 + rnd(30000)); c[i].strength = (uint16_t)(250 - i * 250 / (n - 1)); }
    for (int i = 1; i < n; i++) for (int j = 0; j < i; j++) if (c[i].ident == c[j].ident) c[i].ident++;
}

static void light_tables(int seasons)
{
    enum { N = 22 };
    sr_lp_club c[N], p[N];
    sr_row t[N], u[N];
    int ok = 1, same = 1, broken_caught = 1;
    long place_top = 0, place_bottom = 0, draws = 0, matches = 0;
    for (int s = 0; s < seasons; s++) {
        make_div(c, N, 77u + (unsigned)s % 50);
        if (sr_lp_table((uint32_t)s * 2654435761u, s % 4, c, N, t) != 0 || !table_ok(t, N)) ok = 0;
        int won = 0, pts = 0;
        for (int k = 0; k < N; k++) { won += t[k].won; pts += t[k].pts; }
        draws += (pts - 3 * won) / 2; matches += N * (N - 1);
        for (int k = 0; k < N; k++) {           /* (the strongest 5 / the weakest 5: their places) */
            if (t[k].team < 5) place_top += k;
            if (t[k].team >= N - 5) place_bottom += k;
        }
        memcpy(p, c, sizeof c);                 /* the same clubs, shuffled */
        seed_ = (unsigned)s;
        for (int i = N - 1; i > 0; i--) { int j = (int)rnd((unsigned)i + 1); sr_lp_club x = p[i]; p[i] = p[j]; p[j] = x; }
        sr_lp_table((uint32_t)s * 2654435761u, s % 4, p, N, u);
        for (int k = 0; k < N; k++)
            if (u[k].glob != t[k].glob || u[k].pts != t[k].pts || u[k].gf != t[k].gf || u[k].ga != t[k].ga) same = 0;
        u[0] = t[0]; u[0].pts++;                /* control: a point too many */
        memcpy(u + 1, t + 1, sizeof(sr_row) * (N - 1));
        if (table_ok(u, N)) broken_caught = 0;
        u[0].pts--; u[0].ga++;                  /* control: a goal conceded from nowhere */
        if (table_ok(u, N)) broken_caught = 0;
    }
    char m[200];
    snprintf(m, sizeof m, "light tables, %d seasons of 22: complete and consistent (42 played, 3-1-0, goals balance, ranked)", seasons);
    check(ok, m);
    check(same, "light tables: the same clubs in another order give the same table");
    check(place_top * 2 < place_bottom, "light tables: the 5 strongest finish higher than the 5 weakest on average");
    snprintf(m, sizeof m, "light tables: draws %.1f%% of the matches (20-32%%)", 100.0 * draws / matches);
    check(draws * 100 >= matches * 20 && draws * 100 <= matches * 32, m);
    check(broken_caught, "control: a table with a point too many / a stray goal is refused");
    printf("      (mean place of the 5 strongest %.2f, of the 5 weakest %.2f)\n",
           (double)place_top / (5.0 * seasons) + 1, (double)place_bottom / (5.0 * seasons) + 1);
}

static void playoffs(int seasons)
{
    enum { N = 22 };
    sr_lp_club c[N];
    sr_row t[N];
    long won[7] = { 0 };
    int ok = 1, up_ok = 1;
    for (int s = 0; s < seasons; s++) {
        make_div(c, N, 900u + (unsigned)s);
        sr_lp_table((uint32_t)s, 1, c, N, t);
        const int w = sr_lp_playoff((uint32_t)s, 1, t, N, c);
        if (w < 1 || w > 6) ok = 0; else won[w]++;
        const sr_lp_club *cc[SR_LP_DIVS];
        sr_lp_club d[SR_LP_DIVS][N];
        int32_t n[SR_LP_DIVS], up[2 * SR_LP_DIVS];
        for (int k = 0; k < SR_LP_DIVS; k++) { make_div(d[k], N, 5000u + (unsigned)(s * 4 + k)); for (int i = 0; i < N; i++) d[k][i].ident = (uint16_t)(k * 100 + i); cc[k] = d[k]; n[k] = N; }
        if (sr_lp_season((uint32_t)s, SR_LP_DIVS, cc, n, up) != 0) { up_ok = 0; continue; }
        for (int k = 0; k < SR_LP_DIVS; k++) {
            sr_lp_table((uint32_t)s, k, d[k], N, t);
            if (up[2 * k] != t[0].team || up[2 * k + 1] == up[2 * k] || up[2 * k + 1] < 0 || up[2 * k + 1] >= N) up_ok = 0;
            int pl = -1;
            for (int j = 0; j < N; j++) if (t[j].team == up[2 * k + 1]) pl = j;
            if (pl < 1 || pl > 6) up_ok = 0;
        }
    }
    int every = 1;
    for (int k = 1; k <= 6; k++) if (!won[k]) every = 0;
    char m[200];
    snprintf(m, sizeof m, "play-offs, %d: the winner one of 2nd-7th, each place won at least once", seasons);
    check(ok && every, m);
    check(won[1] > won[6], "play-offs: 2nd wins more often than 7th");
    printf("      (wins by place 2..7: %ld %ld %ld %ld %ld %ld)\n", won[1], won[2], won[3], won[4], won[5], won[6]);
    check(up_ok, "pool seasons: 8 distinct clubs up, each division's champion and a 2nd-7th play-off winner");
    sr_row t6[6];
    check(sr_lp_playoff(1, 0, t6, 6, c) == -1, "control: no play-off below 7 clubs");
}

/* ---- the re-allocation ---- */

enum { NC = 60 };                               /* N 24 + S 24 + 12 elsewhere (NL above, pool below) */
static sr_pg_level lv[3] = { { 0, 1, { 24 }, 0 }, { 1, 2, { 24, 24 }, 0 }, { 3, 1, { 12 }, 0 } };
static sr_pg_club cl[NC];
static uint8_t rk[NC];
static uint16_t id[NC];

/* 48 clubs at level 1: strict north 0x00, strict south 0x01, flexible 0x20 (1st N, 2nd S) / 0x11
 * (1st S, 2nd N), a second team 0xE0 now and then; unranked ones (strict then) */
static void make_level(unsigned s, int8_t *off, int strict_north)
{
    seed_ = s;
    for (int i = 0; i < NC; i++) {
        cl[i].base = 0;
        for (int p = 0; p < SR_PG_PARALLEL; p++) cl[i].area[p] = 0xFF;
        id[i] = (uint16_t)(100 + i * 7 % NC * 13 + rnd(5) * 1000);
        if (i >= 48) { off[i] = i < 54 ? 0 : 3; rk[i] = (uint8_t)rnd(255); continue; }
        off[i] = (int8_t)(1 + rnd(2));
        const unsigned k = rnd(10);
        int lat = (int)rnd(240);                /* rank: north 0 */
        if (i < strict_north) { cl[i].area[0] = 0x00; lat = (int)rnd(90); }
        else if (k < 3) { cl[i].area[0] = 0x00; lat = (int)rnd(110); }
        else if (k < 6) { cl[i].area[0] = 0x01; lat = 130 + (int)rnd(110); }
        else if (k < 9) { cl[i].area[0] = rnd(2) ? 0x20 : 0x11; lat = 80 + (int)rnd(80); }
        else cl[i].area[0] = 0xE0;
        rk[i] = rnd(25) ? (uint8_t)lat : SR_RANK_NONE;
    }
}

static int is_flex(int i)
{
    const int b = cl[i].area[0], h = b >> 4;
    return rk[i] != SR_RANK_NONE && (h == 14 || h == 1 || h == 2);
}

/* the invariants over a placement; returns a reason or NULL */
static const char *realloc_bad(const int8_t *off, const uint8_t *rank)
{
    int n[2] = { 0, 0 }, maxn = -1, mins = 256;
    for (int i = 0; i < 48; i++) {
        const int g = off[i] - 1;
        if (g < 0 || g > 1) return "a club left the level";
        n[g]++;
    }
    if (n[0] != 24 || n[1] != 24) return "not 24 / 24";
    for (int i = 0; i < 48; i++) {
        const int g = off[i] - 1;
        if (!is_flex(i) && (cl[i].area[0] & 15) < 2 && g != (cl[i].area[0] & 15)) return "a strict club across";
        if (is_flex(i)) {
            if (g == 0 && rank[i] > maxn) maxn = rank[i];
            if (g == 1 && rank[i] < mins) mins = rank[i];
        }
    }
    if (maxn > mins) return "a flexible club north of one placed in the north, but placed south";
    for (int i = 48; i < NC; i++) if (off[i] != (i < 54 ? 0 : 3)) return "a club of another level moved";
    return NULL;
}

static void reallocation(int seasons)
{
    static int8_t off[NC], a[NC], b[NC];
    sr_pg_league pg = { 3, lv, NC, cl };
    int ok = 1, same = 1, idem = 1, over = 0, ctl_rank = 0, ctl_strict = 0, ctl_count = 0, ctl_n = 0;
    const char *why = NULL;
    for (int s = 0; s < seasons; s++) {
        make_level(31u + (unsigned)s, off, 0);
        memcpy(a, off, sizeof off);
        const int rc = sr_pg_realloc(&pg, 1, rk, id, a);
        if (rc < 0) { ok = 0; why = "refused"; continue; }
        if (rc > 0) { over++; continue; }       /* (strict clubs past a group's room: the case below) */
        const char *r = realloc_bad(a, rk);
        if (r) { ok = 0; why = r; }
        memcpy(b, a, sizeof a);                 /* a second pass: nothing moves */
        sr_pg_realloc(&pg, 1, rk, id, b);
        if (memcmp(a, b, sizeof a)) idem = 0;
        /* the same clubs in other slots (and other groups before the pass): the same split by ident */
        static sr_pg_club cl2[NC];
        static uint8_t rk2[NC];
        static uint16_t id2[NC];
        static int8_t o2[NC];
        int perm[48];
        for (int i = 0; i < 48; i++) perm[i] = i;
        seed_ = (unsigned)s * 7u + 3u;
        for (int i = 47; i > 0; i--) { int j = (int)rnd((unsigned)i + 1), x = perm[i]; perm[i] = perm[j]; perm[j] = x; }
        memcpy(cl2, cl, sizeof cl); memcpy(rk2, rk, sizeof rk); memcpy(id2, id, sizeof id); memcpy(o2, off, sizeof off);
        for (int i = 0; i < 48; i++) { cl2[i] = cl[perm[i]]; rk2[i] = rk[perm[i]]; id2[i] = id[perm[i]]; o2[i] = (int8_t)(1 + rnd(2)); }
        sr_pg_league pg2 = { 3, lv, NC, cl2 };
        sr_pg_realloc(&pg2, 1, rk2, id2, o2);
        for (int i = 0; i < 48; i++) if (o2[i] != a[perm[i]]) same = 0;
        /* controls: each must be caught */
        int fn = -1, fs = -1;
        for (int i = 0; i < 48; i++) if (is_flex(i)) { if (a[i] == 1 && (fn < 0 || rk[i] < rk[fn])) fn = i; if (a[i] == 2 && (fs < 0 || rk[i] > rk[fs])) fs = i; }
        if (fn >= 0 && fs >= 0 && rk[fn] != rk[fs]) {   /* the northernmost north flex <-> the southernmost south flex */
            memcpy(b, a, sizeof a); b[fn] = 2; b[fs] = 1; ctl_n++;
            if (realloc_bad(b, rk)) ctl_rank++;
        }
        static uint8_t bad_rank[NC];            /* broken ranks: the pass run on reversed ranks */
        for (int i = 0; i < NC; i++) bad_rank[i] = rk[i] == SR_RANK_NONE ? rk[i] : (uint8_t)(254 - rk[i]);
        memcpy(b, off, sizeof off);
        sr_pg_realloc(&pg, 1, bad_rank, id, b);
        int any_flex_n = 0, any_flex_s = 0;
        for (int i = 0; i < 48; i++) if (is_flex(i)) { if (a[i] == 1) any_flex_n = 1; else any_flex_s = 1; }
        if (any_flex_n && any_flex_s && realloc_bad(b, rk)) ctl_rank++;
        else if (any_flex_n && any_flex_s) ctl_rank--;
        for (int i = 0; i < 48; i++)            /* a strict club moved across */
            if (!is_flex(i) && (cl[i].area[0] & 15) < 2) { memcpy(b, a, sizeof a); b[i] = (int8_t)(3 - b[i]); if (realloc_bad(b, rk)) ctl_strict++; break; }
        memcpy(b, a, sizeof a);                 /* 25 / 23 */
        for (int i = 0; i < 48; i++) if (b[i] == 2) { b[i] = 1; break; }
        if (realloc_bad(b, rk)) ctl_count++;
    }
    printf("      (%d summers with strict clubs past a group's room, left to the case below)\n", over);
    char m[240];
    snprintf(m, sizeof m, "re-allocation, %d summers: 24 / 24, no strict club across, flexible clubs split by rank north first%s%s",
             seasons, why ? " -- " : "", why ? why : "");
    check(ok && over < seasons / 100, m);
    check(same, "re-allocation: the same clubs in other slots and groups give the same split");
    check(idem, "re-allocation: a second pass changes nothing");
    snprintf(m, sizeof m, "control: a flexible pair swapped across the rank line / broken (reversed) ranks caught (%d / %d + reversed)", ctl_rank, ctl_n);
    check(ctl_rank >= ctl_n && ctl_n > seasons / 2, m);
    check(ctl_strict == seasons - over, "control: a strict club moved across caught");
    check(ctl_count == seasons - over, "control: 25 / 23 caught");

    /* strict northerners past the room: 27 of them -> 3 cross, the southernmost */
    make_level(4242u, off, 27);
    memcpy(a, off, sizeof off);
    const int crossed = sr_pg_realloc(&pg, 1, rk, id, a);
    int n0 = 0, worst_ok = 1, maxin = -1, mincross = 256;
    for (int i = 0; i < 48; i++) n0 += a[i] == 1;
    for (int i = 0; i < 48; i++)
        if (!is_flex(i) && (cl[i].area[0] & 15) == 0) {
            if (a[i] == 1 && rk[i] > maxin) maxin = rk[i];
            if (a[i] == 2 && rk[i] < mincross) mincross = rk[i];
        }
    if (crossed > 0 && maxin > mincross) worst_ok = 0;
    snprintf(m, sizeof m, "re-allocation: strict northerners past the room cross, the southernmost (%d crossed, north %d)", crossed, n0);
    check(crossed > 0 && n0 == 24 && worst_ok, m);
    sr_pg_level one[1] = { { 0, 1, { 24 }, 0 } };
    sr_pg_league bad = { 1, one, NC, cl };
    check(sr_pg_realloc(&bad, 0, rk, id, a) == -1, "control: a level that is not two groups is refused");
}

/* a level of 4 groups not in a line (N C S I, 2 each): a club coming in whose two choices are full,
 * room only one step deeper. With chain: every club within its own choices; without (the Mod): the
 * nearest-group fallback puts someone outside them (the control). */
static int chain_case(int chain, int *outside)
{
    /* area bytes: low nibble the 1st choice, high nibble the 2nd + 1; N 0, C 1, S 2, I 3 */
    static const uint8_t ar[8] = { 0x20, 0x00, 0x31, 0x01, 0x42, 0x02, 0x03, 0x20 };
    /* n1 (N, C), n2 (N), c1 (C, S), c2 (C), s1 (S, I), s2 (S), i1 (I), x (N, C) coming up from below */
    sr_pg_level lv4[2] = { { 0, 1, { 8 }, 0 }, { 1, 4, { 2, 2, 2, 2 }, (uint8_t)chain } };
    sr_pg_club c4[8];
    int8_t old_off[8] = { 1, 1, 2, 2, 3, 3, 4, 5 }, new_off[8] = { 1, 1, 2, 2, 3, 3, 4, 1 };
    for (int i = 0; i < 8; i++) {
        c4[i].base = 0;
        c4[i].area[0] = ar[i];
        for (int p = 1; p < SR_PG_PARALLEL; p++) c4[i].area[p] = 0xFF;
    }
    sr_pg_league pg = { 2, lv4, 8, c4 };
    if (sr_pg_season(&pg, old_off, new_off, NULL) != 0) return -1;
    int n[4] = { 0 }, out = 0;
    for (int i = 0; i < 8; i++) {
        const int g = new_off[i] - 1, h1 = ar[i] & 15, h2 = (ar[i] >> 4) - 1;
        if (g < 0 || g > 3) return -1;
        n[g]++;
        if (g != h1 && g != h2) out++;
    }
    *outside = out;
    return n[0] == 2 && n[1] == 2 && n[2] == 2 && n[3] == 2;
}

/* D (Davide 08/11): a club down from N / S takes the pool strength at its own N / S position */
static void rank_strength(void)
{
    const uint16_t above[5] = { 500, 450, 400, 350, 306 }, pool[3] = { 262, 200, 100 };
    int ok = sr_lp_rank_strength(500, above, 5, pool, 3) == 262 && sr_lp_rank_strength(306, above, 5, pool, 3) == 100
          && sr_lp_rank_strength(600, above, 5, pool, 3) == 262 && sr_lp_rank_strength(250, above, 5, pool, 3) == 100
          && sr_lp_rank_strength(400, above, 5, pool, 3) == 200                 /* (the middle -> the middle) */
          && sr_lp_rank_strength(425, above, 5, pool, 3) == 216;                /* (1.5 of 4 -> 0.75 of 2: 262 - 46, truncated) */
    for (int v = 300, last = 0; v <= 510; v++) {            /* monotone: stronger above, stronger below */
        const int x = sr_lp_rank_strength((uint16_t)v, above, 5, pool, 3);
        if (x < last) ok = 0;
        last = x;
    }
    check(ok, "D: a club down from N / S takes the pool's strength at its N / S position (ends, middle, between, monotone)");
    check(sr_lp_rank_strength(400, above, 1, pool, 3) == -1, "control: D refuses a list of fewer than 2");
    const uint16_t mixed[3] = { 100, 262, 200 };     /* (iii, Davide 09/11): own strength, capped at the pool's strongest */
    check(sr_lp_newcomer_strength(150, mixed, 3) == 150 && sr_lp_newcomer_strength(262, mixed, 3) == 262
          && sr_lp_newcomer_strength(306, mixed, 3) == 262 && sr_lp_newcomer_strength(0, mixed, 3) == 0,
          "iii: a club down from N / S keeps its own strength, capped at the pool's strongest (any order)");
    check(sr_lp_newcomer_strength(150, mixed, 0) == -1, "control: iii refuses an empty pool");
}

int main(int argc, char **argv)
{
    const int n = argc > 1 ? atoi(argv[1]) : 2000;
    light_tables(n);
    playoffs(n);
    reallocation(n);
    rank_strength();
    int out_c = -1, out_m = -1;
    const int ok_c = chain_case(1, &out_c), ok_m = chain_case(0, &out_m);
    char m[160];
    snprintf(m, sizeof m, "chain level (N C S I): both choices full, room one step deeper -- every club within its choices (%d outside)", out_c);
    check(ok_c == 1 && out_c == 0, m);
    snprintf(m, sizeof m, "control: the same without chain (the Mod's fallback) puts a club outside its choices (%d)", out_m);
    check(ok_m == 1 && out_m > 0, m);
    return fails ? 1 : 0;
}
