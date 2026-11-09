/* tests/sim/simdrv.c -- item g (MC 09/11): the mass-simulation driver at match level. Serie A and Serie B (20 clubs
 * each) played match by match every season: sr_rr_calendar's double round robin, Poisson goals from the clubs'
 * strengths (home advantage), the tables from the played matches (sr_rr_table) and again from the game's form (the
 * clubs' rows + the n x n results block, sr_league_rank, 4.2 S1), ranked by the FIGC list of S1.2b (points, avulsa,
 * h2h goal difference, goal difference, goals for, goals against, wins, losses_fewer, away wins, home_losses_fewer,
 * draw lots: tests/specs/ok/v14_figc_tiebreakers.json); Serie C's group A (20) the same way.
 * Distributions (every season, each of the three):
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
 * Phase 2 (MC 09/11): A's play-off matches played (title: one match, penalties; 17th: two legs, aggregate, penalties;
 * the better placed hosts / hosts the return), then the season end through the library: sr_italy (B's play-off and
 * play-out, their ties played by the model through sr_force.win), sr_knockouts, sr_final_moves over A, B and Serie C's
 * three groups (group A at match level, groups B and C light: sr_lp_table_sp, spread 40); C's relegated go to a D sink that
 * sends up new clubs. Invariants: A's 18th..20th down and no other A club moves, the play-off losers 2nd / 18th, B up 3
 * (1st, 2nd, a play-off winner from 3rd..8th), B down = C up, 20 clubs per division after the moves.
 * Usage: simdrv [SEASONS [SEED]] (default 100000 1). SIMDRV_BREAK=pts (a point lost) / order (two places swapped) /
 * move (an extra Serie A club down) must FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "swosrules.h"

enum { N = 20, NM = N * (N - 1), NL = 3, NTB = 11 };   /* leagues at match level: Serie A, B, Serie C's group A */
static const uint8_t FIGC[NTB + 1] = { SR_TB_POINTS, SR_TB_AVULSA, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_GOAL_DIFFERENCE,
                                       SR_TB_GOALS_FOR, SR_TB_GOALS_AGAINST, SR_TB_WINS, SR_TB_LOSSES_FEWER,
                                       SR_TB_AWAY_WINS, SR_TB_HOME_LOSSES_FEWER, SR_TB_DRAW_LOTS, SR_TB_END };
static const uint8_t H2HL[NTB + 1] = { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_GOAL_DIFFERENCE,
                                       SR_TB_GOALS_FOR, SR_TB_GOALS_AGAINST, SR_TB_WINS, SR_TB_LOSSES_FEWER,
                                       SR_TB_AWAY_WINS, SR_TB_HOME_LOSSES_FEWER, SR_TB_DRAW_LOTS, SR_TB_END };   /* the same
                                       with per-block head to head instead of the avulsa (MC: where they differ) */
static const char *LG_NAME[NL] = { "Serie A", "Serie B", "Serie C group A (20)" };
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

static const char *brk;

/* model: home lambda = H0 * e^(dS / C), away = A0 * e^(-dS / C); dS = strength difference */
static double H0 = 1.40, A0 = 1.08, C = 70.0, RHO = 0.12;   /* SIMDRV_MODEL=h0,a0,c,rho */

static long long n_pairs, decided[NL][NTB], tie_size[NL][N + 1], blocks3[NL], avdiff[NL], po_first, po_17, po_any, seasons_run;
static long long hw, dr, aw, goals, matches, champ_pts, p17_pts, multi_deciders[NL][NTB];

/* phase 2 (MC 09/11): the season end through the library. Serie C's three groups of 20 are light tables
 * (sr_lp_table_sp, spread 40: C stays light, italyfull is the chain test); Serie D a sink that takes C's relegated
 * and sends up new clubs. */
enum { NC = 3, CBASE = 2 * N, NSLOT = CBASE + NC * N, ND = 2 + NC };
static int cdiv[NSLOT];                     /* slot -> division 0 A, 1 B, 2..4 C groups */
static uint16_t cstr[NSLOT];                /* slot -> strength */
static uint16_t cid[NSLOT];                 /* slot -> the club's identity (a new one when a slot is refilled) */
static uint16_t next_id;
static sr_state S;
static int pts_of[NSLOT];                   /* this season's points (the B final's ET / pens rule) */
static long long po_title_played, po_rel_played, b_po_held, b_out_held, c_up, c_down,
                 b_up_po_place[N], moves_bad;
static int ord_l[NL][N];                   /* this season's final orders (slots), A after its play-off matches */
static sr_srow rec[NSLOT];                  /* this season's record per slot (match-level leagues) */              /* this season's final orders (slots), after A's play-off matches */

static int match_goals(double l) { int g = poisson(l); return g > 14 ? 14 : g; }
static void play(int h, int a, int *hg, int *ag)    /* one match, slots */
{
    const double ds = (double)cstr[h] - cstr[a];
    *hg = match_goals(H0 * exp(ds / C)), *ag = match_goals(A0 * exp(-ds / C));
    if (*hg != *ag && abs(*hg - *ag) == 1 && rndu() < RHO) *hg = *ag = *hg < *ag ? *hg : *ag;
}
static int pens(int h, int a) { (void)h; (void)a; return rndu() < 0.5 ? h : a; }

static int slot_of_glob(uint16_t g) { return g; }   /* S.t's glob = the slot */

/* sr_force.win: B's play-off / play-out ties played by the model; home = the better placed (it hosts the single
 * match / the return leg); level on aggregate: the better placed through, ET + pens only when level on points (the
 * play-off final, dp_tie's level -1) */
static uint16_t force_win(void *ctx, uint16_t home, uint16_t away, int32_t legs)
{
    (void)ctx;
    const int h = slot_of_glob(home), a = slot_of_glob(away);
    int gh = 0, ga = 0, x, y;
    if (legs == 2) { play(a, h, &x, &y); ga += x, gh += y; }
    play(h, a, &x, &y); gh += x, ga += y;
    if (gh != ga) return gh > ga ? home : away;
    if (pts_of[h] == pts_of[a]) { play(h, a, &x, &y); if (x != y) return x > y ? home : away; return (uint16_t)pens(h, a); }
    return home;
}
static const sr_force FORCE = { NULL, force_win, NULL, NULL };

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

/* Serie A's play-off matches (FIGC since 2022-23), when level on points: for 1st, the avulsa's top two, one
 * match at the better placed's ground, no extra time, penalties; for 17th (17th and 18th level), two legs, the
 * better placed hosting the return, aggregate, no away goals, penalties. The loser takes the lower place: the
 * title's runner-up 2nd, the relegation's loser 18th (relegated). */
static long long po_t_pens, po_r_pens;
static void playoff_matches(const sr_srow *row, const uint8_t *ord)
{
    int *o = ord_l[0], x, y;
    if (row[ord[0]].pts == row[ord[1]].pts) {
        const int h = o[0], a = o[1];
        play(h, a, &x, &y);
        int w = x > y ? h : x < y ? a : (po_t_pens++, pens(h, a));
        po_title_played++;
        if (w != o[0]) o[0] = a, o[1] = h;
        CHECK(o[0] == w && (o[1] == h || o[1] == a) && o[1] != w, "title play-off: the loser not 2nd");
    }
    if (row[ord[16]].pts == row[ord[17]].pts) {
        const int h = o[16], a = o[17];       /* h better placed: hosts the return */
        int gh = 0, ga = 0;
        play(a, h, &x, &y); ga += x, gh += y;
        play(h, a, &x, &y); gh += x, ga += y;
        int w = gh > ga ? h : gh < ga ? a : (po_r_pens++, pens(h, a));
        po_rel_played++;
        if (w != o[16]) o[16] = a, o[17] = h;
        CHECK(o[16] == w && o[17] != w, "relegation play-off: the loser not 18th");
    }
}

static void league(int d, uint32_t seed)
{
    static sr_match m[NM];
    int club[N], k = 0;
    for (int i = 0; i < NSLOT; i++) if (cdiv[i] == d) club[k++] = i;
    CHECK(k == N, "division %d has %d clubs", d, k);
    const int nm = sr_rr_calendar(N, 2, m, NM);
    CHECK(nm == NM, "calendar %d matches", nm);
    uint8_t pairs[N * N];
    memset(pairs, SR_PAIR_NONE, sizeof pairs);
    for (int i = 0; i < nm; i++) {
        int hg, ag;
        play(club[m[i].home], club[m[i].away], &hg, &ag);   /* (draw inflation: a one-goal margin levelled with RHO;
                                                               independent Poisson draws too few, Dixon-Coles' point) */
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
    /* blocks of 3+ level on points: the avulsa's order v per-block head to head's */
    uint8_t oh[N];
    sr_league_rank(N, row, pairs, NULL, 3, 1, 0, H2HL, seed, oh);
    for (int i = 0; i < N;) {
        int j = i;
        while (j + 1 < N && row[ord[j + 1]].pts == row[ord[i]].pts) j++;
        if (j - i >= 2) {
            blocks3[d]++;
            int pos = 0;                             /* oh's places of the same points are the same range i..j */
            while (row[oh[pos]].pts != row[ord[i]].pts) pos++;
            CHECK(pos == i, "league %d: the per-block h2h order puts the %d-point block at %d, not %d", d, row[ord[i]].pts, pos, i);
            avdiff[d] += memcmp(ord + i, oh + i, (size_t)(j - i + 1)) != 0;
        }
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
    for (int i = 0; i < N; i++) ord_l[d][i] = club[ord[i]], rec[club[ord[i]]] = row[ord[i]], pts_of[club[ord[i]]] = row[ord[i]].pts;
    if (d == 0) playoff_matches(row, ord);
}

/* the season end through the library: sr_italy (B's play-off / play-out, C's play-offs / play-outs: ties by
 * force_win), sr_knockouts, sr_final_moves; then the moves applied (C's relegated to the D sink, new clubs up) */
static const uint8_t PROM[ND] = { 0, 2, 1, 1, 1 }, REL[ND] = { 3, 3, 1, 1, 1 }, LVF[3] = { 0, 1, 2 };
static void season_end(uint32_t seed, long sn)
{
    sr_reset(&S);
    for (int d = 0; d < ND; d++) {
        int sl[N], n = 0;
        if (d < NL) for (int k = 0; k < N; k++) sl[n++] = ord_l[d][k];
        else {                                   /* the light C groups */
            sr_lp_club c[N];
            int idx[N];
            sr_row t[N];
            for (int i = 0; i < NSLOT; i++) if (cdiv[i] == d) { idx[n] = i; c[n].ident = cid[i]; c[n].strength = cstr[i]; n++; }
            CHECK(n == N, "season %ld: C group %d has %d clubs", sn, d - 2, n);
            sr_lp_table_sp(seed, 100 + d, c, n, t, 40);
            for (int k = 0; k < n; k++) {
                sl[k] = idx[t[k].team];
                rec[sl[k]].pts = (int16_t)t[k].pts, rec[sl[k]].w = t[k].won, rec[sl[k]].gf = t[k].gf, rec[sl[k]].ga = t[k].ga;
                pts_of[sl[k]] = t[k].pts;
            }
        }
        S.t.n[d] = (uint8_t)n;
        for (int k = 0; k < n; k++) {
            sr_row *r = &S.t.t[d][k];
            const sr_srow *q = &rec[sl[k]];
            r->glob = (uint16_t)sl[k], r->team = (uint16_t)(sl[k] + 1);
            r->won = q->w, r->gf = q->gf, r->ga = q->ga, r->pts = (uint16_t)q->pts;
        }
    }
    S.t.nd = ND;
    sr_league lg;
    memset(&lg, 0, sizeof lg);
    lg.divisions = ND; lg.promoted = PROM; lg.relegated = REL;
    lg.italy = 1; lg.serie_b = 1; lg.group_first = 2; lg.groups = NC;
    const sr_levels lv = { 3, LVF };
    sr_season se;
    memset(&se, 0, sizeof se);
    se.levels = &lv; se.force = &FORCE;
    se.cup_winner = S.t.t[2 + seed % NC][(seed >> 4) % N].team;
    CHECK(sr_italy(&S, &lg, &se) >= 0, "season %ld: sr_italy refused", sn);
    sr_knockouts(&S, &lg, &se);
    sr_final_moves(&S, &lg, &lv, 0);
    if (brk && !strcmp(brk, "move") && sn == 1) S.want[0][5] = 1;
    /* the moves: invariants, then applied */
    int up[3] = { 0 }, down[3] = { 0 };
    for (int d = 0; d < ND; d++)
        for (int k = 0; k < S.t.n[d]; k++) {
            const int w = S.want[d][k], l = d < 2 ? d : 2, sl = S.t.t[d][k].glob;
            if (w < 0) up[l]++;
            if (w > 0) down[l]++;
            if (d == 0) CHECK(w == (k >= 17 ? 1 : 0), "season %ld: Serie A place %d move %d (18th..20th down, no other)", sn, k + 1, w);
            if (d == 1 && w < 0) {

                CHECK(k < 2 || (k >= 2 && k <= 7), "season %ld: Serie B place %d up", sn, k + 1);
                if (k >= 2) b_up_po_place[k]++;
            }
            (void)sl;
        }
    CHECK(down[0] == 3 && up[1] == 3, "season %ld: A down %d, B up %d (3 / 3)", sn, down[0], up[1]);
    CHECK(down[1] == up[2], "season %ld: B down %d, C up %d", sn, down[1], up[2]);
    if (down[0] != 3 || up[1] != 3 || down[1] != up[2]) moves_bad++;
    b_po_held += sr_playoff_held(S.t.t[1][2].pts, S.t.t[1][3].pts) != 0;   /* (dplay.c's rules: 3rd > 4th + 14 up direct; */
    b_out_held += sr_out_held(S.t.t[1][15].pts, S.t.t[1][16].pts) != 0;    /*  16th > 17th + 4: 17th down direct) */
    c_up += up[2], c_down += down[2];
    /* apply: A <-> B, B <-> C (the C holes filled by B's relegated, in group order), C's relegated -> D sink */
    int hole[NC * N], nh = 0, bdown[N], nbd = 0;
    for (int d = 0; d < ND; d++)
        for (int k = 0; k < S.t.n[d]; k++) {
            const int w = S.want[d][k], sl = S.t.t[d][k].glob;
            if (!w) continue;
            if (d == 0) cdiv[sl] = 1;
            else if (d == 1) { if (w < 0) cdiv[sl] = 0; else bdown[nbd++] = sl; }
            else if (w < 0) { cdiv[sl] = 1; hole[nh++] = d; }
            else {                               /* down to the D sink: a new club from D in its place */
                cid[sl] = next_id++; cstr[sl] = (uint16_t)(170 + rnd32() % 81);
            }
        }
    CHECK(nh == nbd, "season %ld: %d C holes, %d B relegated", sn, nh, nbd);
    for (int i = 0; i < nbd && i < nh; i++) cdiv[bdown[i]] = hole[i];
    int nd[ND] = { 0 };
    for (int i = 0; i < NSLOT; i++) { CHECK(cdiv[i] >= 0 && cdiv[i] < ND, "slot %d nowhere", i); if (cdiv[i] >= 0 && cdiv[i] < ND) nd[cdiv[i]]++; }
    for (int d = 0; d < ND; d++) CHECK(nd[d] == N, "season %ld: division %d has %d clubs after the moves", sn, d, nd[d]);
}

int main(int argc, char **argv)
{
    const long ns = argc > 1 ? atol(argv[1]) : 100000;
    rs = argc > 2 ? strtoull(argv[2], NULL, 10) : 1;
    brk = getenv("SIMDRV_BREAK");
    if (getenv("SIMDRV_MODEL")) sscanf(getenv("SIMDRV_MODEL"), "%lf,%lf,%lf,%lf", &H0, &A0, &C, &RHO);
    for (int i = 0; i < NSLOT; i++) {
        static const uint16_t lo[3] = { 290, 240, 180 }, span[3] = { 86, 81, 101 };   /* italyfull's levels */
        const int l = i < CBASE ? i / N : 2;
        cdiv[i] = i < CBASE ? i / N : 2 + (i - CBASE) / N;
        cstr[i] = (uint16_t)(lo[l] + rnd32() % span[l]);
        cid[i] = (uint16_t)i;
    }
    next_id = NSLOT;
    for (long s = 0; s < ns && fails < 10; s++) {
        const uint32_t seed = rnd32();
        league(0, seed);
        league(1, seed ^ 0x1234567u);
        league(2, seed ^ 0x2468ACEu);
        season_end(seed, s);
        seasons_run++;
    }
    printf("simdrv: %lld seasons, Serie A + B + a Serie C group of 20 match by match (FIGC tie-breakers, S1.2b)\n", seasons_run);
    if (matches)
        printf("Serie A: %.2f goals per match, home %.1f%% draw %.1f%% away %.1f%%; champion %.1f points, 17th %.1f\n",
               (double)goals / matches, 100.0 * hw / matches, 100.0 * dr / matches, 100.0 * aw / matches,
               (double)champ_pts / seasons_run, (double)p17_pts / seasons_run);
    printf("Serie A play-off matches (level on points): for 1st %.2f%% of seasons, for 17th %.2f%%, either %.2f%%\n",
           100.0 * po_first / seasons_run, 100.0 * po_17 / seasons_run, 100.0 * po_any / seasons_run);
    printf("Serie A play-off matches played: title %.2f%% of seasons (to penalties %.1f%% of them), relegation %.2f%% (to"
           " penalties %.1f%%)\n", 100.0 * po_title_played / seasons_run, po_title_played ? 100.0 * po_t_pens / po_title_played : 0.0,
           100.0 * po_rel_played / seasons_run, po_rel_played ? 100.0 * po_r_pens / po_rel_played : 0.0);
    printf("season end (sr_italy + sr_knockouts + sr_final_moves): Serie B play-off held %.1f%% of seasons, play-out held %.1f%%;"
           " the play-off winner's place:", 100.0 * b_po_held / seasons_run, 100.0 * b_out_held / seasons_run);
    for (int k = 2; k < 8; k++) printf(" %d. %.1f%%", k + 1, 100.0 * b_up_po_place[k] / seasons_run);
    printf("\n  Serie C: %.2f up, %.2f down per season (no spec: its play-outs are the 4.2 spec's knockouts, not run here -- the"
           " 20th only); seasons with a wrong A/B/C count: %lld\n",
           (double)c_up / seasons_run, (double)c_down / seasons_run, moves_bad);
    for (int d = 0; d < NL; d++) {
        long long np = 0, past_gf = 0;
        for (int q = 0; q < NTB; q++) np += decided[d][q], past_gf += q > 4 ? decided[d][q] : 0;
        printf("%s: %lld adjacent pairs level on points (%.2f per season); decided by:\n", LG_NAME[d], np,
               (double)np / seasons_run);
        for (int q = 1; q < NTB; q++)
            if (decided[d][q])
                printf("  %-20s %6.2f%%  (of them in a group of 3+: %.1f%%)\n", TB_NAME[q], 100.0 * decided[d][q] / np,
                       100.0 * multi_deciders[d][q] / decided[d][q]);
        printf("  past goals_for: %.3f%% of the pairs (%.4f per season); to the draw: %.4f%% (%lld pairs, %.5f per season)\n",
               100.0 * past_gf / np, (double)past_gf / seasons_run, 100.0 * decided[d][NTB - 1] / np, decided[d][NTB - 1],
               (double)decided[d][NTB - 1] / seasons_run);
        printf("  blocks of 3+ level on points: %lld (%.3f per season); the avulsa orders %lld differently from per-block"
               " h2h (%.1f%% of them; %.0f per 10^4 tables)\n", blocks3[d], (double)blocks3[d] / seasons_run, avdiff[d],
               blocks3[d] ? 100.0 * avdiff[d] / blocks3[d] : 0.0, 1e4 * avdiff[d] / seasons_run);
        printf("  groups level on points per season:");
        for (int g = 2; g <= N; g++) if (tie_size[d][g]) printf(" %d clubs %.3f", g, (double)tie_size[d][g] / seasons_run);
        printf("\n");
    }
    printf(fails ? "simdrv: %d invariant(s) broken\n" : "simdrv: every invariant held\n", fails);
    return fails != 0;
}
