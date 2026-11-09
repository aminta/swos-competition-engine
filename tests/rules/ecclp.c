/* 42-ecclp: the light pool with a national play-off (Italy's Eccellenza 2026-27): regional play-offs, the national
 * play-off, the declared cup rule (the Mod's italy42.md 5d), a whole season of the 28 real groups; controls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"
#include "../sim/ecclp_italy.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

static unsigned seed_;
static unsigned rnd(unsigned n) { seed_ = seed_ * 1103515245u + 12345u; return (seed_ >> 8) % n; }

static void make_div(sr_lp_club *c, int n, unsigned s, int base)
{
    seed_ = s;
    for (int i = 0; i < n; i++) { c[i].ident = (uint16_t)(base + i); c[i].strength = (uint16_t)(20 + rnd(220)); }
}

/* a season's up list: 0 or what is wrong (a club lost or twice, not 36, a champion not its table's top) */
static const char *up_bad(const sr_lp_up *up, int nup, int nd, const sr_lp_club *const *c, const int32_t *n,
                          uint32_t seed)
{
    static unsigned char seen[SR_LP_DIVS][SR_LP_CLUBS];
    int champ = 0, nat = 0, cup = 0;
    if (nup != IT_UP) return "not 36 up";
    memset(seen, 0, sizeof seen);
    for (int k = 0; k < nup; k++) {
        if (up[k].div < 0 || up[k].div >= nd || up[k].idx < 0 || up[k].idx >= n[up[k].div]) return "a club out of the pool";
        if (seen[up[k].div][up[k].idx]++) return "a club up twice";
        champ += up[k].how == SR_LP_UP_CHAMPION; nat += up[k].how == SR_LP_UP_CROSS;
        cup += up[k].how == SR_LP_UP_EXTRA || up[k].how == SR_LP_UP_OVERRIDE;
    }
    if (champ != nd || nat != 7 || cup != 1) return "not 28 + 7 + 1";
    for (int d = 0; d < nd; d++) {
        sr_row t[SR_LP_CLUBS];
        sr_lp_table(seed, d, c[d], n[d], t);
        if (up[d].how != SR_LP_UP_CHAMPION || up[d].div != d || up[d].idx != t[0].team) return "a champion not up";
    }
    return 0;
}

static sr_lp_club pool[IT_GROUPS][SR_LP_CLUBS];
static const sr_lp_club *pp[IT_GROUPS];
static int32_t pn[IT_GROUPS];

static void make_pool(unsigned s)
{
    int base = 0;
    for (int g = 0; g < IT_GROUPS; g++) {
        make_div(pool[g], IT_G[g].size, s * 31u + (unsigned)g, base);
        base += IT_G[g].size; pp[g] = pool[g]; pn[g] = IT_G[g].size;
    }
}

static void regional(int seasons)
{
    long won[8] = { 0 }, ladder[8] = { 0 }, better = 0, worse = 0;
    int ok = 1;
    for (int s = 0; s < seasons; s++) {
        sr_lp_club c[20];
        sr_row t[20];
        const int n = 14 + s % 7;                               /* group sizes 14 .. 20 */
        make_div(c, n, 70u + (unsigned)s, 0);
        sr_lp_table((uint32_t)s, 3, c, n, t);
        const sr_lp_rpo four = { SR_LP_RPO_FOUR, 1, 0, 0, 0, 0, 0 }, lad = { SR_LP_RPO_LADDER, 1, 0, 0, 0, 0, 0 },
                        dir = { SR_LP_RPO_DIRECT, 1, 0, 0, 0, 0, 0 }, db = { SR_LP_RPO_FOUR, 1, 0, 0, 0, 1, 0 };
        const int w = sr_lp_regional((uint32_t)s, 3, t, n, c, &four), l = sr_lp_regional((uint32_t)s, 3, t, n, c, &lad);
        if (w < 1 || w > 4 || l < 1 || l > 3 || sr_lp_regional((uint32_t)s, 3, t, n, c, &dir) != 1) ok = 0;
        else { won[w]++; ladder[l]++; }
        const int b = sr_lp_regional((uint32_t)s, 3, t, n, c, &db);
        better += b == 1 || b == 2; worse += b == 3 || b == 4;
    }
    char m[200];
    snprintf(m, sizeof m, "regional play-offs, %d groups of 14..20: 2v5 / 3v4 + final a 2nd..5th, the ladder a 2nd..4th, "
             "direct the 2nd", seasons);
    check(ok && won[1] && won[2] && won[3] && won[4] && ladder[1] && ladder[2] && ladder[3], m);
    printf("      (2v5/3v4 wins by place 2..5: %ld %ld %ld %ld; ladder 2..4: %ld %ld %ld)\n", won[1], won[2], won[3],
           won[4], ladder[1], ladder[2], ladder[3]);
    check(won[1] > won[4] && ladder[1] > ladder[3], "regional play-offs: the 2nd wins more often than the last entrant");
    check(better > worse, "regional play-offs: a draw to the better placed favours the 2nd / 3rd");

    /* the gaps on a table written by hand: points 70 60 55 52 40 ... */
    sr_lp_club c[16];
    sr_row t[16];
    for (int i = 0; i < 16; i++) { c[i].ident = (uint16_t)i; c[i].strength = (uint16_t)(i == 4 ? 375 : 0); t[i].team = (uint16_t)i; t[i].pts = (uint16_t)(80 - 5 * i); }
    t[1].pts = 60; t[2].pts = 50; t[3].pts = 49; t[4].pts = 40;      /* 2nd 60, 3rd 50, 4th 49, 5th 40 */
    int skip = 1, direct = 1, fin = 1;
    for (uint32_t s = 0; s < 200; s++) {
        const sr_lp_rpo g = { SR_LP_RPO_FOUR, 1, 20, 0, 0, 0, 0 };          /* 2v5 (20 apart) not played */
        const sr_lp_rpo d = { SR_LP_RPO_FOUR, 1, 0, 0, 10, 0, 0 };          /* 2nd 10 ahead of the 3rd: direct */
        const sr_lp_rpo f = { SR_LP_RPO_FOUR, 1, 20, 10, 0, 0, 0 };         /* the final 2 v (3 or 4): 10+ apart */
        if (sr_lp_regional(s, 0, t, 16, c, &g) == 4) skip = 0;
        if (sr_lp_regional(s, 0, t, 16, c, &d) != 1) direct = 0;
        if (sr_lp_regional(s, 0, t, 16, c, &f) != 1) fin = 0;
    }
    check(skip && direct && fin, "regional gaps: a first-round tie 20 points apart not played, the 2nd 10 ahead goes "
          "direct, a final 10 apart not played");
    const sr_lp_rpo bad = { SR_LP_RPO_FOUR, 1, 0, 0, 0, 0, 0 };
    check(sr_lp_regional(1, 0, t, 4, c, &bad) == -1, "control: 2v5 refused in a table of 4");
}

static void national(int seasons)
{
    sr_lp_club e[IT_GROUPS];
    uint8_t urn[IT_GROUPS];
    int32_t up[IT_GROUPS];
    int ok = 1;
    long top = 0;
    for (int s = 0; s < seasons; s++) {
        for (int g = 0; g < IT_GROUPS; g++) { e[g].ident = (uint16_t)g; e[g].strength = (uint16_t)(50 + rnd(150)); urn[g] = IT_G[g].urn; }
        const int nw = sr_lp_cross((uint32_t)s, e, IT_GROUPS, urn, IT_URNS, IT_PAIR, 2, 2, 2, up);
        int seen[IT_GROUPS] = { 0 }, nu[3] = { 0 };
        if (nw != 7) { ok = 0; continue; }
        for (int k = 0; k < nw; k++) { if (seen[up[k]]++) ok = 0; nu[urn[up[k]]]++; }
        if (nu[2] != 2 || nu[0] + nu[1] != 5) ok = 0;            /* 2 South v South ties, 5 North v Centre */
        for (int k = 0; k < nw; k++) { int r = 0; for (int g = 0; g < IT_GROUPS; g++) r += e[g].strength > e[up[k]].strength; top += r < 14; }
    }
    char m[160];
    snprintf(m, sizeof m, "national play-off, %d seasons: 28 in urns 10 / 10 / 8 -> 14 -> 7 distinct, 2 from the South", seasons);
    check(ok, m);
    check(top > 7L * seasons * 6 / 10, "national play-off: the stronger half wins most places");
    static const uint8_t odd_urn[4] = { 0, 0, 0, 1 }, only_nc[1][2] = { { 0, 1 } };
    check(sr_lp_cross(1, e, 4, odd_urn, 2, IT_PAIR, 2, 2, 2, up) == -1, "control: an urn of odd size refused");
    for (int g = 0; g < IT_GROUPS; g++) urn[g] = IT_G[g].urn;
    check(sr_lp_cross(1, e, IT_GROUPS, urn, IT_URNS, only_nc, 1, 2, 2, up) == -1,
          "control: the South's winners left without a pair refused");
}

static void cup_rule(void)
{
    sr_lp_club a[3] = { { 10, 200 }, { 11, 250 }, { 12, 250 } }, b[3] = { { 20, 250 }, { 21, 100 }, { 22, 250 } };
    const sr_lp_club *c[2] = { a, b };
    const int32_t n[2] = { 3, 3 };
    uint8_t pa[3] = { 0, 2, 1 }, pb[3] = { 1, 0, 2 }, ua[3] = { 0 }, ub[3] = { 0 };
    const uint8_t *pl[2] = { pa, pb }, *iu[2] = { ua, ub };
    int32_t d, i, v;
    /* 250: a[1] (place 2), a[2] (place 1), b[0] (place 1, ident 20), b[2] (place 2): a[2] (place 1, ident 12 < 20) */
    int ok = sr_lp_extra(2, c, n, pl, iu, SR_LP_NONE, &d, &i, &v) == 0 && d == 0 && i == 2 && !v;
    ua[2] = 1;                                       /* a[2] up: b[0] (place 1) */
    ok &= sr_lp_extra(2, c, n, pl, iu, SR_LP_NONE, &d, &i, &v) == 0 && d == 1 && i == 0;
    pb[0] = 2; ua[1] = 1;                            /* b[0] and b[2] both 250 and place 2: the lower ident, 20 */
    pb[2] = 2;
    ok &= sr_lp_extra(2, c, n, pl, iu, SR_LP_NONE, &d, &i, &v) == 0 && d == 1 && i == 0;
    check(ok, "cup rule: the strongest not up; ties by the better place, then the lower ident");
    ok = sr_lp_extra(2, c, n, pl, iu, 21, &d, &i, &v) == 0 && d == 1 && i == 1 && v;
    ub[1] = 1;
    ok &= sr_lp_extra(2, c, n, pl, iu, 21, &d, &i, &v) == 0 && !v && !(d == 1 && i == 1);
    ok &= sr_lp_extra(2, c, n, pl, iu, 999, &d, &i, &v) == 0 && !v;
    check(ok, "cup rule: the override takes the place; already up or not in the pool, the rule");
    memset(ua, 1, 3); memset(ub, 1, 3);
    check(sr_lp_extra(2, c, n, pl, iu, SR_LP_NONE, &d, &i, &v) == -1, "control: no club left refused");
}

static void seasons_it(int seasons)
{
    sr_lp_shape sh;
    sr_lp_rpo rpo[IT_GROUPS];
    uint8_t urn[IT_GROUPS];
    sr_lp_up up[48], up2[48];
    int ok = 1, total = 0;
    for (int g = 0; g < IT_GROUPS; g++) total += IT_G[g].size;
    it_shape(&sh, rpo, urn, NULL, 0);
    for (int s = 0; s < seasons; s++) {
        make_pool(900u + (unsigned)s);
        const int nup = sr_lp_season_cross((uint32_t)s, IT_GROUPS, pp, pn, &sh, up);
        const char *why = nup < 0 ? "refused" : up_bad(up, nup, IT_GROUPS, pp, pn, (uint32_t)s);
        if (why) { ok = 0; printf("      season %d: %s\n", s, why); continue; }
        if (sr_lp_season_cross((uint32_t)s, IT_GROUPS, pp, pn, &sh, up2) != nup || memcmp(up, up2, sizeof up[0] * (size_t)nup)) ok = 0;
    }
    char m[200];
    snprintf(m, sizeof m, "Eccellenza seasons, %d: the 28 real groups (%d clubs, 15..18): 36 distinct up = 28 champions + "
             "7 national + 1 cup rule, the same for the same seed", seasons, total);
    check(ok && total == 465, m);
    /* the override */
    make_pool(5);
    const uint16_t ov1[1] = { pool[22][14].ident };                        /* a weak Molise club */
    it_shape(&sh, rpo, urn, ov1, 1);
    int nup = sr_lp_season_cross(5, IT_GROUPS, pp, pn, &sh, up);
    check(nup == IT_UP && up[35].how == SR_LP_UP_OVERRIDE && up[35].div == 22 && up[35].idx == 14,
          "Eccellenza season: the real cup winner (override) takes the 36th place");
    {   /* a list: an id not in the pool and a club already up are passed over, two extra places: override + rule */
        const uint16_t ov3[3] = { 9999, (uint16_t)pool[up[0].div][up[0].idx].ident, pool[22][13].ident };
        it_shape(&sh, rpo, urn, ov3, 3);
        sh.extra = 2;
        const int n2 = sr_lp_season_cross(5, IT_GROUPS, pp, pn, &sh, up2);
        check(n2 == IT_UP + 1 && up2[35].how == SR_LP_UP_OVERRIDE && up2[35].div == 22 && up2[35].idx == 13
              && up2[36].how == SR_LP_UP_EXTRA, "override list: passed over when not in the pool or up, then the rule");
    }
    {   /* barred (4.2, a B team whose first team plays Serie D or lower): group 0's champion and the national play-off's
         * first winner may not go up -- the next club of group 0 is its champion, neither barred club is up, 36 up */
        it_shape(&sh, rpo, urn, NULL, 0);
        const int n0 = sr_lp_season_cross(5, IT_GROUPS, pp, pn, &sh, up);
        sr_row t[SR_LP_CLUBS];
        sr_lp_table(5, 0, pp[0], pn[0], t);
        const uint16_t bar[2] = { pp[0][t[0].team].ident, pp[up[IT_GROUPS].div][up[IT_GROUPS].idx].ident };
        sh.barred = bar; sh.nbarred = 2;
        const int n2 = sr_lp_season_cross(5, IT_GROUPS, pp, pn, &sh, up2);
        int seen = 0;
        for (int k = 0; k < n2; k++) for (int b = 0; b < 2; b++) seen |= pp[up2[k].div][up2[k].idx].ident == bar[b];
        check(n0 == IT_UP && n2 == IT_UP && !seen && up2[0].idx == t[1].team,
              "barred clubs (4.2 B teams): passed over -- the next club the champion, neither up, still 36");
    }
    /* controls: up_bad on broken copies */
    it_shape(&sh, rpo, urn, NULL, 0);
    nup = sr_lp_season_cross(5, IT_GROUPS, pp, pn, &sh, up);
    memcpy(up2, up, sizeof up);
    up2[30] = up2[3];                                /* a club up twice (and one lost) */
    const char *w1 = up_bad(up2, nup, IT_GROUPS, pp, pn, 5), *w2 = up_bad(up, nup - 1, IT_GROUPS, pp, pn, 5);
    up[nup] = up[nup - 1]; up[nup].idx = (int16_t)((up[nup].idx + 1) % pn[up[nup].div]);
    const char *w3 = up_bad(up, nup + 1, IT_GROUPS, pp, pn, 5);
    memcpy(up2, up, sizeof up); up2[0].idx = (int16_t)((up2[0].idx + 1) % pn[0]);
    const char *w4 = up_bad(up2, nup, IT_GROUPS, pp, pn, 5);
    check(w1 && !strcmp(w1, "a club up twice") && w2 && w3 && !strcmp(w2, "not 36 up") && !strcmp(w3, "not 36 up") && w4,
          "controls: a club up twice, 35 up, 37 up, a champion swapped -> caught");
    sh.legs1 = 3;
    check(sr_lp_season_cross(5, IT_GROUPS, pp, pn, &sh, up) == -1, "control: three legs refused");
    printf("      (sizes: sr_lp_rpo %zu B, sr_lp_shape %zu B, sr_lp_up %zu B; sr_lp_season_cross's own frame ~%zu B)\n",
           sizeof(sr_lp_rpo), sizeof(sr_lp_shape), sizeof(sr_lp_up),
           sizeof(sr_row) * SR_LP_CLUBS + 2 * SR_LP_DIVS * SR_LP_CLUBS + SR_LP_DIVS * (sizeof(sr_lp_club) + 8 + 2 * sizeof(void *)));
}

int main(int argc, char **argv)
{
    const int seasons = argc > 1 ? atoi(argv[1]) : 500;
    regional(seasons);
    national(seasons);
    cup_rule();
    seasons_it(seasons);
    return fails ? 1 : 0;
}
