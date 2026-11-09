/* tests/sim/simdrv.c -- item g (MC 09/11): the mass-simulation driver at match level. Serie A and Serie B (20 clubs
 * each) played match by match every season: sr_rr_calendar's double round robin, Poisson goals from the clubs'
 * strengths (home advantage), the tables from the played matches (sr_rr_table) and again from the game's form (the
 * clubs' rows + the n x n results block, sr_league_rank, 4.2 S1), ranked by the FIGC list of S1.2b (points, avulsa,
 * h2h goal difference, goal difference, goals for, goals against, wins, losses_fewer, away wins, home_losses_fewer,
 * draw lots: tests/specs/ok/v14_figc_tiebreakers.json). Between seasons A's last 3 and B's first 3 change places.
 * Distributions (every season, both leagues):
 *   - which criterion decides the order of two clubs level on points next to each other: the shortest prefix of the
 *     list (+ draw lots) that gives their final order under two different draw-lots seeds;
 *   - Serie A's play-off matches (FIGC since 2022-23): clubs level on points for 1st, or for 17th (the last place
 *     kept: 17th and 18th level);
 *   - the size of each group of clubs level on points;
 *   - points of the champion, of 17th, goals per match, home wins / draws / away wins.
 * Invariants (every season, exit 1 on any): every club 38 matches; 3 * wins + draws = points; the league's goals for
 * = its goals against; wins = losses; sr_rr_table's order = sr_league_rank's (same list, same seed); the order
 * non-increasing in points; a pair decided by a criterion is level on every criterion before it; 20 clubs per
 * division and every club in exactly one after the exchange.
 * Usage: simdrv [SEASONS [SEED]] (default 100000 1). SIMDRV_BREAK=pts (a point lost) / order (two places swapped)
 * must FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "swosrules.h"

enum { N = 20, NM = N * (N - 1), NCLUB = 2 * N, NTB = 11 };
static const uint8_t FIGC[NTB + 1] = { SR_TB_POINTS, SR_TB_AVULSA, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_GOAL_DIFFERENCE,
                                       SR_TB_GOALS_FOR, SR_TB_GOALS_AGAINST, SR_TB_WINS, SR_TB_LOSSES_FEWER,
                                       SR_TB_AWAY_WINS, SR_TB_HOME_LOSSES_FEWER, SR_TB_DRAW_LOTS, SR_TB_END };
static const char *TB_NAME[NTB] = { "points", "avulsa", "h2h_goal_difference", "goal_difference", "goals_for",
                                    "goals_against", "wins", "losses_fewer", "away_wins", "home_losses_fewer",
                                    "draw_lots" };

static unsigned long long rs;
static uint32_t rnd32(void) { rs = rs * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(rs >> 32); }
static double rndu(void) { return (rnd32() + 0.5) / 4294967296.0; }
static int poisson(double l)
{
    const double e = exp(-l);
    double p = 1.0;
    int k = 0;
    do { k++; p *= rndu(); } while (p > e);
    return k - 1;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 10) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint16_t str_of[NCLUB];
static int div_of[NCLUB];                   /* 0 Serie A, 1 Serie B */
static const char *brk;

/* model: home lambda = H0 * e^(dS / C), away = A0 * e^(-dS / C); dS = strength difference */
static double H0 = 1.40, A0 = 1.08, C = 70.0, RHO = 0.12;   /* SIMDRV_MODEL=h0,a0,c,rho */

static long long n_pairs, decided[2][NTB], tie_size[2][N + 1], po_first, po_17, po_any, seasons_run;
static long long hw, dr, aw, goals, matches, champ_pts, p17_pts, multi_deciders[2][NTB];

static int same_pair_order(const uint8_t *ord, int a, int b)   /* 1 if a before b in ord */
{
    for (int k = 0; k < N; k++) {
        if (ord[k] == a) return 1;
        if (ord[k] == b) return 0;
    }
    return -1;
}

/* the value of criterion t for club c (record criteria only; the h2h ones are decided by the prefix test) */
static long rec_val(const sr_srow *r, int t)
{
    switch (t) {
    case SR_TB_POINTS: return r->pts;
    case SR_TB_GOAL_DIFFERENCE: return (long)r->gf - r->ga;
    case SR_TB_GOALS_FOR: return r->gf;
    case SR_TB_GOALS_AGAINST: return -(long)r->ga;
    case SR_TB_WINS: return r->w;
    case SR_TB_LOSSES_FEWER: return -(long)r->l;
    case SR_TB_AWAY_WINS: return r->aw;
    default: return 0x7FFFFFFF;            /* not a record criterion */
    }
}

static void league(int d, uint32_t seed)
{
    static sr_match m[NM];
    int club[N], k = 0;
    for (int i = 0; i < NCLUB; i++) if (div_of[i] == d) club[k++] = i;
    CHECK(k == N, "division %d has %d clubs", d, k);
    const int nm = sr_rr_calendar(N, 2, m, NM);
    CHECK(nm == NM, "calendar %d matches", nm);
    uint8_t pairs[N * N];
    memset(pairs, SR_PAIR_NONE, sizeof pairs);
    for (int i = 0; i < nm; i++) {
        const double ds = (double)str_of[club[m[i].home]] - str_of[club[m[i].away]];
        int hg = poisson(H0 * exp(ds / C)), ag = poisson(A0 * exp(-ds / C));
        if (hg != ag && abs(hg - ag) == 1 && rndu() < RHO) ag = hg = hg < ag ? hg : ag;   /* draw inflation: a one-goal
                                                                                     margin levelled (independent Poisson
                                                                                     draws too few: Dixon-Coles' point) */
        if (hg > 14) hg = 14;
        if (ag > 14) ag = 14;
        m[i].hg = (uint8_t)hg, m[i].ag = (uint8_t)ag, m[i].played = 1;
        pairs[m[i].home * N + m[i].away] = (uint8_t)(hg << 4 | ag);
        if (d == 0) { hw += hg > ag; dr += hg == ag; aw += hg < ag; goals += hg + ag; matches++; }
    }
    sr_srow t[N], row[N];
    CHECK(sr_rr_table(N, m, nm, 3, 1, 0, FIGC, seed, t) == N, "sr_rr_table refused");
    for (int i = 0; i < N; i++) row[t[i].club] = t[i];
    if (brk && !strcmp(brk, "pts")) row[3].pts--;
    long gf = 0, ga = 0, w = 0, l = 0;
    for (int c = 0; c < N; c++) {
        const sr_srow *r = &row[c];
        CHECK(r->pl == 2 * (N - 1), "club %d: %u matches", c, r->pl);
        CHECK(r->pts == 3 * r->w + r->d, "club %d: %d points, %u-%u-%u", c, r->pts, r->w, r->d, r->l);
        gf += r->gf, ga += r->ga, w += r->w, l += r->l;
    }
    CHECK(gf == ga && w == l, "league %d: goals %ld / %ld, wins %ld / losses %ld", d, gf, ga, w, l);
    uint8_t ord[N];
    CHECK(sr_league_rank(N, row, pairs, NULL, 3, 1, 0, FIGC, seed, ord) == N, "sr_league_rank refused");
    if (brk && !strcmp(brk, "order")) { const uint8_t x = ord[5]; ord[5] = ord[6]; ord[6] = x; }
    for (int i = 0; i < N; i++) CHECK(ord[i] == t[i].club, "league %d place %d: sr_league_rank %u, sr_rr_table %u", d, i, ord[i], t[i].club);
    for (int i = 0; i + 1 < N; i++) CHECK(row[ord[i]].pts >= row[ord[i + 1]].pts, "league %d place %d: points rise", d, i);
    /* groups level on points */
    for (int i = 0; i < N;) {
        int j = i;
        while (j + 1 < N && row[ord[j + 1]].pts == row[ord[i]].pts) j++;
        tie_size[d][j - i + 1]++;
        i = j + 1;
    }
    /* the deciding criterion of each adjacent pair level on points */
    for (int i = 0; i + 1 < N; i++) {
        const int a = ord[i], b = ord[i + 1];
        if (row[a].pts != row[b].pts) continue;
        n_pairs++;
        int dec = NTB - 1;
        for (int p = 1; p < NTB - 1; p++) {          /* prefix FIGC[0..p] + draw lots */
            uint8_t tb[NTB + 1], o1[N], o2[N];
            memcpy(tb, FIGC, (size_t)p + 1);
            tb[p + 1] = SR_TB_DRAW_LOTS, tb[p + 2] = SR_TB_END;
            sr_league_rank(N, row, pairs, NULL, 3, 1, 0, tb, seed ^ 0x9E3779B9u, o1);
            sr_league_rank(N, row, pairs, NULL, 3, 1, 0, tb, seed ^ 0x7F4A7C15u, o2);
            if (same_pair_order(o1, a, b) == 1 && same_pair_order(o2, a, b) == 1) { dec = p; break; }
        }
        decided[d][dec]++;
        int lvl = 0;                                 /* clubs level with them: more than 2 */
        lvl = (i > 0 && row[ord[i - 1]].pts == row[a].pts) || (i + 2 < N && row[ord[i + 2]].pts == row[a].pts);
        if (lvl) multi_deciders[d][dec]++;
        for (int q = 1; q < dec; q++) {              /* a record criterion before the decider: level */
            const long va = rec_val(&row[a], FIGC[q]), vb = rec_val(&row[b], FIGC[q]);
            if (va != 0x7FFFFFFF && !lvl) CHECK(va == vb, "league %d place %d: %s decided but %s differs", d, i, TB_NAME[dec], TB_NAME[q]);
        }
    }
    if (d == 0) {
        const int f = row[ord[0]].pts == row[ord[1]].pts, s = row[ord[16]].pts == row[ord[17]].pts;
        po_first += f, po_17 += s, po_any += f || s;
        champ_pts += row[ord[0]].pts, p17_pts += row[ord[16]].pts;
    }
    /* the exchange: A's 18th..20th down, B's 1st..3rd up (B's places of a fixed 3, as the A/B line moves 3 / 3) */
    static int down[3], up[3];
    if (d == 0) for (int i = 0; i < 3; i++) down[i] = club[ord[17 + i]];
    else {
        for (int i = 0; i < 3; i++) up[i] = club[ord[i]];
        for (int i = 0; i < 3; i++) div_of[down[i]] = 1, div_of[up[i]] = 0;
    }
}

int main(int argc, char **argv)
{
    const long ns = argc > 1 ? atol(argv[1]) : 100000;
    rs = argc > 2 ? strtoull(argv[2], NULL, 10) : 1;
    brk = getenv("SIMDRV_BREAK");
    if (getenv("SIMDRV_MODEL")) sscanf(getenv("SIMDRV_MODEL"), "%lf,%lf,%lf,%lf", &H0, &A0, &C, &RHO);
    for (int i = 0; i < NCLUB; i++) {
        div_of[i] = i < N ? 0 : 1;
        str_of[i] = (uint16_t)(i < N ? 290 + rnd32() % 86 : 240 + rnd32() % 81);
    }
    for (long s = 0; s < ns && fails < 10; s++) {
        const uint32_t seed = rnd32();
        league(0, seed);
        league(1, seed ^ 0x1234567u);
        int na = 0;
        for (int i = 0; i < NCLUB; i++) { CHECK(div_of[i] == 0 || div_of[i] == 1, "club %d nowhere", i); na += div_of[i] == 0; }
        CHECK(na == N, "season %ld: Serie A %d clubs", s, na);
        seasons_run++;
    }
    printf("simdrv: %lld seasons, Serie A + B match by match (FIGC tie-breakers, S1.2b)\n", seasons_run);
    if (matches)
        printf("Serie A: %.2f goals per match, home %.1f%% draw %.1f%% away %.1f%%; champion %.1f points, 17th %.1f\n",
               (double)goals / matches, 100.0 * hw / matches, 100.0 * dr / matches, 100.0 * aw / matches,
               (double)champ_pts / seasons_run, (double)p17_pts / seasons_run);
    printf("Serie A play-off matches (level on points): for 1st %.2f%% of seasons, for 17th %.2f%%, either %.2f%%\n",
           100.0 * po_first / seasons_run, 100.0 * po_17 / seasons_run, 100.0 * po_any / seasons_run);
    for (int d = 0; d < 2; d++) {
        long long np = 0;
        for (int q = 0; q < NTB; q++) np += decided[d][q];
        printf("%s: %lld adjacent pairs level on points (%.2f per season); decided by:\n", d ? "Serie B" : "Serie A", np,
               (double)np / seasons_run);
        for (int q = 1; q < NTB; q++)
            if (decided[d][q])
                printf("  %-20s %6.2f%%  (of them in a group of 3+: %.1f%%)\n", TB_NAME[q], 100.0 * decided[d][q] / np,
                       100.0 * multi_deciders[d][q] / decided[d][q]);
        printf("  groups level on points per season:");
        for (int g = 2; g <= N; g++) if (tie_size[d][g]) printf(" %d clubs %.3f", g, (double)tie_size[d][g] / seasons_run);
        printf("\n");
    }
    printf(fails ? "simdrv: %d invariant(s) broken\n" : "simdrv: every invariant held\n", fails);
    return fails != 0;
}
