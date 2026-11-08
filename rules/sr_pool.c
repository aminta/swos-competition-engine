/*
 * sr_pool.c -- a light pool's divisions at the season end: England's step 3 (the Northern Premier
 * League, the Southern League Premier Central and South, the Isthmian Premier; Davide 07/11 B).
 * No matches are played: each division's table comes from the clubs' strengths and the season's
 * seed, then its champion and its play-off winner (2nd-7th, the National League's shape) go up.
 *
 * Every draw is from a hash of the seed, the division and the clubs' idents (or the tie), never a
 * running generator: the same clubs give the same table whatever the order they come in, and a club
 * swapped in or out changes only its own matches.
 *
 * Copyright (c) 2026 Davide Lorigliola. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint32_t u32;

static u32 mix(u32 h, u32 v)
{
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    return h ^ (h >> 16);
}

typedef struct { u32 h; } rng;
static u32 below(rng *r, u32 n)                 /* 0 .. n-1 */
{
    r->h = r->h * 1103515245u + 12345u;
    return ((r->h >> 16) * n) >> 16;
}

static u32 str2(int strength, int home)         /* 30 .. 30 + SPREAD for 0.00 .. 2.50, linear on to 3.75 */
{
    int s = 30 + (strength > 375 ? 375 : strength) * SR_LP_SPREAD / 250;
    if (home) s = s * 11 / 10;
    return (u32)(s * s);
}

typedef struct { int pts, gd, gf, gp, won; } fig;

int32_t sr_lp_table(uint32_t seed, int32_t d, const sr_lp_club *c, int32_t n, sr_row *out)
{
    fig f[SR_LP_CLUBS];
    int ord[SR_LP_CLUBS];
    if (!c || !out || n < 2 || n > SR_LP_CLUBS) return -1;
    for (int i = 0; i < n; i++) { f[i].pts = f[i].gd = f[i].gf = f[i].gp = f[i].won = 0; ord[i] = i; }
    for (int h = 0; h < n; h++)
        for (int a = 0; a < n; a++) {
            if (h == a) continue;
            rng r = { mix(mix(mix(seed, (u32)d), c[h].ident), (u32)c[a].ident << 16 | 1u) };
            const u32 sh = str2(c[h].strength, 1), sa = str2(c[a].strength, 0);
            int gh, ga;
            if (below(&r, 100) < 26) { gh = ga = (int)below(&r, 3); }
            else {
                const int w = below(&r, sh + sa) < sh, gw = 1 + (int)below(&r, 3), gl = (int)below(&r, (u32)gw);
                gh = w ? gw : gl; ga = w ? gl : gw;
            }
            f[h].gf += gh; f[h].gd += gh - ga; f[a].gf += ga; f[a].gd += ga - gh;
            f[h].gp++; f[a].gp++;
            if (gh > ga) { f[h].pts += 3; f[h].won++; }
            else if (gh < ga) { f[a].pts += 3; f[a].won++; }
            else { f[h].pts++; f[a].pts++; }
        }
    for (int i = 1; i < n; i++)                 /* points, goal difference, goals, strength, ident */
        for (int j = i; j > 0; j--) {
            const int x = ord[j], y = ord[j - 1];
            const int before = f[x].pts != f[y].pts ? f[x].pts > f[y].pts
                             : f[x].gd != f[y].gd ? f[x].gd > f[y].gd
                             : f[x].gf != f[y].gf ? f[x].gf > f[y].gf
                             : c[x].strength != c[y].strength ? c[x].strength > c[y].strength
                             : c[x].ident < c[y].ident;
            if (!before) break;
            ord[j] = y; ord[j - 1] = x;
        }
    for (int k = 0; k < n; k++) {
        const int i = ord[k];
        out[k].team = (uint16_t)i;
        out[k].glob = c[i].ident;
        out[k].won = (uint16_t)f[i].won;
        out[k].gf = (uint16_t)f[i].gf;
        out[k].ga = (uint16_t)(f[i].gf - f[i].gd);
        out[k].pts = (uint16_t)f[i].pts;
    }
    return 0;
}

/* one match, places a < b (a the better placed, at home): the winner's place */
static int tie(rng *r, const sr_row *t, const sr_lp_club *c, int a, int b)
{
    const int x = c[t[a].team].strength, y = c[t[b].team].strength;
    u32 sa = str2(x, 1), sb = str2(y, 0);
    if (below(r, 100) < 22) { const u32 p = str2(x, 0), q = str2(y, 0); sa = 2 * p + q; sb = p + 2 * q; }
    return below(r, sa + sb) < sa ? a : b;
}

int32_t sr_lp_playoff(uint32_t seed, int32_t d, const sr_row *table, int32_t n, const sr_lp_club *c)
{
    if (!table || !c || n < 7) return -1;
    rng r = { mix(mix(seed, (u32)d), 0x504Fu) };
    const int q1 = tie(&r, table, c, 3, 6), q2 = tie(&r, table, c, 4, 5);   /* 4v7, 5v6 */
    const int lo = q1 > q2 ? q1 : q2, hi = q1 > q2 ? q2 : q1;
    const int s1 = tie(&r, table, c, 1, lo), s2 = tie(&r, table, c, 2, hi);  /* 2 v the lower placed */
    return s1 < s2 ? tie(&r, table, c, s1, s2) : tie(&r, table, c, s2, s1);
}

int32_t sr_lp_season(uint32_t seed, int32_t nd, const sr_lp_club *const *c, const int32_t *n, int32_t *up)
{
    sr_row t[SR_LP_CLUBS];
    if (!c || !n || !up || nd < 1 || nd > SR_LP_DIVS) return -1;
    for (int d = 0; d < nd; d++) {
        if (n[d] < 7 || sr_lp_table(seed, d, c[d], n[d], t) != 0) return -1;
        up[2 * d] = t[0].team;
        up[2 * d + 1] = t[sr_lp_playoff(seed, d, t, n[d], c[d])].team;
    }
    return 0;
}

/* the value at position x / 1000 of a list sorted strongest first (0 .. n - 1, linear between neighbours) */
static int at_pos(const uint16_t *v, int n, int x)
{
    const int i = x / 1000 < n - 1 ? x / 1000 : n - 2, f = x - i * 1000;
    return v[i] + ((int)v[i + 1] - (int)v[i]) * f / 1000;
}

int32_t sr_lp_rank_strength(uint16_t own, const uint16_t *above, int32_t na, const uint16_t *pool, int32_t np)
{
    int p;                                      /* (own's position in `above`, 0 .. 1000) */
    if (!above || !pool || na < 2 || np < 2) return -1;
    if (own >= above[0]) p = 0;
    else if (own <= above[na - 1]) p = 1000;
    else {
        int i = 0;
        while (i < na - 2 && !(above[i] >= own && own >= above[i + 1])) i++;
        const int d = above[i] - above[i + 1];
        p = (i * 1000 + (d ? (above[i] - own) * 1000 / d : 0)) / (na - 1);
    }
    return at_pos(pool, np, p * (np - 1));
}

int32_t sr_lp_newcomer_strength(uint16_t own, const uint16_t *pool, int32_t np)
{
    if (!pool || np < 1) return -1;
    int top = 0;
    for (int i = 0; i < np; i++) if (pool[i] > top) top = pool[i];
    return own < top ? own : top;
}
