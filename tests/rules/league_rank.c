/* tests/rules/league_rank.c -- 4.2 S1: sr_league_rank, a league table ranked by its tie-breaker list from the game's
 * rows and the season's results block.
 *   1. faithful_1996 = the 1996 game's own sort (cseg_883DD, ported below as the reference: an exchange sort on
 *      points, goal difference, goals for, then the names by unsigned bytes) -- on 10^5 random tables with heavy ties
 *      and on the two real tables of the S1 fixture (World 2026-27, INTER: SERIE A season 1, SERIE B season 2);
 *   2. head to head from the results block = sr_rr_table's from the match list, on random double round robins;
 *   3. no results block (an old save): the head-to-head criteria left out;
 *   4. refused: the criteria that need the game's data; n out of range.
 *   league_rank [N]   (N random tables, default 100000) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../rules/swosrules.h"

static unsigned long long rs = 0x5EA50001ull;
static unsigned rnd(unsigned n) { rs = rs * 6364136223846793005ull + 1442695040888963407ull; return (unsigned)(rs >> 33) % n; }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 10) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* the game's cseg_883DD over one group: pos[] row indexes, rows with pts / gf / ga, names */
static void game_sort(int n, const sr_srow *row, char names[][24], int *pos)
{
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++) {
            const sr_srow *a = &row[pos[i]], *b = &row[pos[j]];
            int swap = 0;
            if ((unsigned)(uint16_t)a->pts > (unsigned)(uint16_t)b->pts) continue;
            if ((unsigned)(uint16_t)a->pts < (unsigned)(uint16_t)b->pts) swap = 1;
            else {
                const int16_t da = (int16_t)(a->gf - a->ga), db = (int16_t)(b->gf - b->ga);
                if (da > db) continue;
                if (da < db) swap = 1;
                else if (a->gf > b->gf) continue;
                else if (a->gf < b->gf) swap = 1;
                else {
                    const unsigned char *p = (const unsigned char *)names[pos[i]], *q = (const unsigned char *)names[pos[j]];
                    for (;; p++, q++) {
                        if (!*p) break;
                        if (*p < *q) break;
                        if (*p > *q) { swap = 1; break; }
                    }
                }
            }
            if (swap) { int t = pos[i]; pos[i] = pos[j]; pos[j] = t; }
        }
}

static void name_keys(int n, char names[][24], uint8_t *namek)
{
    for (int c = 0; c < n; c++) {
        int k = 0;
        for (int d = 0; d < n; d++) {
            const int s = strcmp(names[d], names[c]);  /* (unsigned bytes: C's strcmp) */
            if (s < 0 || (s == 0 && d < c)) k++;
        }
        namek[c] = (uint8_t)k;
    }
}

static const uint8_t F96[] = { SR_TB_FAITHFUL_1996, 0 };

static void faithful_random(int nt)
{
    int bad = 0, ctl = 0;
    for (int t = 0; t < nt; t++) {
        const int n = 2 + (int)rnd(23);
        sr_srow row[24];
        char names[24][24];
        uint8_t namek[24], ord[24];
        int pos[24];
        for (int c = 0; c < n; c++) {
            memset(&row[c], 0, sizeof row[c]);
            row[c].club = (uint8_t)c;
            row[c].pts = (int16_t)rnd(4 + t % 60);          /* (few values: ties on points, goals, ...) */
            row[c].gf = (uint16_t)rnd(1 + t % 9);
            row[c].ga = (uint16_t)rnd(1 + t % 7);
            int len = 1 + (int)rnd(4);                       /* (short names: shared prefixes; all distinct) */
            for (;;) {
                for (int k = 0; k < len; k++) names[c][k] = "AB ZaÈ"[rnd(6)];
                names[c][len] = 0;
                int dup = 0;
                for (int d = 0; d < c; d++) dup |= !strcmp(names[d], names[c]);
                if (!dup) break;
                len++;
            }
            pos[c] = c;
        }
        name_keys(n, names, namek);
        game_sort(n, row, names, pos);
        if (sr_league_rank(n, row, NULL, namek, 3, 1, 0, F96, (uint32_t)t, ord) != n) { bad++; continue; }
        int same = 1;
        for (int k = 0; k < n; k++) same &= ord[k] == pos[k];
        if (!same) bad++;
        static const uint8_t SWAPPED[] = { SR_TB_POINTS, SR_TB_GOALS_FOR, SR_TB_GOAL_DIFFERENCE, SR_TB_NAME, 0 };
        sr_league_rank(n, row, NULL, namek, 3, 1, 0, SWAPPED, (uint32_t)t, ord);   /* (the control) */
        for (int k = 0; k < n; k++) if (ord[k] != pos[k]) { ctl++; break; }
    }
    CHECK(ctl > 0, "control: goals for before goal difference never differs from the game's sort");
    CHECK(!bad, "faithful_1996: %d of %d random tables ranked unlike the game's cseg_883DD", bad, nt);
    if (!bad) printf("PASS: faithful_1996 = the game's own sort (cseg_883DD: points, goal difference, goals for, the names "
                     "by unsigned bytes) on %d random tables of 2-24 clubs, heavy ties, shared name prefixes "
                     "(control: goals for before goal difference differs on %d)\n", nt, ctl);
}

/* the S1 fixture's two real tables (pl, w, d, l, gf, ga, pts) and the game's own order */
static const int REAL[2][20][7] = {
    { {38,6,9,23,35,73,27},{38,25,10,3,88,23,85},{38,22,12,4,68,25,78},{38,21,13,4,55,20,76},{38,20,5,13,77,42,65},
      {38,20,11,7,66,23,71},{38,20,12,6,56,28,72},{38,21,6,11,48,48,69},{38,14,11,13,58,36,53},{38,18,9,11,58,34,63},
      {38,17,11,10,57,38,62},{38,12,8,18,38,42,44},{38,12,11,15,48,50,47},{38,12,9,17,35,54,45},{38,6,14,18,25,65,32},
      {38,9,9,20,38,69,36},{38,7,8,23,30,79,29},{38,8,5,25,35,79,29},{38,4,10,24,26,84,22},{38,11,7,20,36,65,40} },
    { {38,24,10,4,90,24,82},{38,19,8,11,55,32,65},{38,15,14,9,50,46,59},{38,21,13,4,60,27,76},{38,21,9,8,69,29,72},
      {38,7,10,21,24,61,31},{38,20,7,11,51,32,67},{38,19,7,12,55,39,64},{38,10,12,16,35,52,42},{38,15,7,16,49,63,52},
      {38,7,10,21,27,61,31},{38,13,8,17,45,55,47},{38,19,11,8,53,29,68},{38,12,11,15,44,53,47},{38,11,8,19,37,52,41},
      {38,11,12,15,43,50,45},{38,4,14,20,32,64,26},{38,10,9,19,35,63,39},{38,10,15,13,38,44,45},{38,5,19,14,38,54,34} } };
static const int REAL_ORD[2][20] = { { 1,2,3,6,5,7,4,9,10,8,12,13,11,19,15,14,17,16,0,18 },
                                     { 0,3,4,12,6,1,7,2,9,13,11,18,15,8,14,17,19,10,5,16 } };

static void faithful_real(void)
{
    for (int s = 0; s < 2; s++) {
        sr_srow row[20];
        uint8_t ord[20];
        for (int c = 0; c < 20; c++) {
            memset(&row[c], 0, sizeof row[c]);
            row[c].club = (uint8_t)c; row[c].pl = (uint16_t)REAL[s][c][0]; row[c].w = (uint16_t)REAL[s][c][1];
            row[c].d = (uint16_t)REAL[s][c][2]; row[c].l = (uint16_t)REAL[s][c][3]; row[c].gf = (uint16_t)REAL[s][c][4];
            row[c].ga = (uint16_t)REAL[s][c][5]; row[c].pts = (int16_t)REAL[s][c][6];
        }
        sr_league_rank(20, row, NULL, NULL, 3, 1, 0, F96, 1, ord);
        int same = 1;
        for (int k = 0; k < 20; k++) same &= ord[k] == REAL_ORD[s][k];
        CHECK(same, "faithful_1996: the S1 fixture's season %d table not in the game's order", s + 1);
    }
    printf("PASS: faithful_1996 on the S1 fixture's real tables (World 2026-27, INTER: SERIE A season 1, SERIE B season 2):"
           " the game's own order\n");
}

/* a random double round robin: its match list (sr_rr_table) and its results block (sr_league_rank) */
static int season(int n, int maxg, sr_match *m, uint8_t *pairs)
{
    int nm = sr_rr_calendar(n, 2, m, 24 * 23);
    memset(pairs, SR_PAIR_NONE, (size_t)(n * n));
    for (int k = 0; k < nm; k++) {
        m[k].played = rnd(20) != 0;                         /* (some not played yet: mid-season) */
        m[k].hg = (uint8_t)rnd((unsigned)maxg + 1); m[k].ag = (uint8_t)rnd((unsigned)maxg + 1);
        if (m[k].played) pairs[m[k].home * n + m[k].away] = (uint8_t)(m[k].hg << 4 | m[k].ag);
    }
    return nm;
}

static void h2h_random(int nt)
{
    static const uint8_t LISTS[][8] = {
        { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_DRAW_LOTS, 0 },
        { SR_TB_POINTS, SR_TB_H2H_REAPPLIED, SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_WINS, SR_TB_DRAW_LOTS, 0 },
        { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_H2H_GOALS_FOR, SR_TB_H2H_AWAY_GOALS, SR_TB_NAME, 0 },
        { SR_TB_POINTS, SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_AWAY_GOALS_FOR, SR_TB_AWAY_WINS, SR_TB_NAME, 0 } };
    int bad = 0, skip_bad = 0;
    static sr_match m[24 * 23];
    static uint8_t pairs[24 * 24];
    for (int t = 0; t < nt; t++) {
        const int n = 2 + (int)rnd(23), L = t % 4;
        const int nm = season(n, t % 3 ? 3 : 14, m, pairs);
        sr_srow tab[24], row[24];
        uint8_t ord[24];
        if (sr_rr_table(n, m, nm, 3, 1, 0, LISTS[L], (uint32_t)t, tab) != n) { bad++; continue; }
        for (int k = 0; k < n; k++) row[tab[k].club] = tab[k];
        sr_league_rank(n, row, pairs, NULL, 3, 1, 0, LISTS[L], (uint32_t)t, ord);
        int same = 1;
        for (int k = 0; k < n; k++) same &= ord[k] == tab[k].club;
        if (!same) bad++;
        /* no results block: the same as the list without its head-to-head criteria */
        uint8_t nolist[8], o2[24];
        int j = 0;
        for (const uint8_t *c = LISTS[L]; *c; c++)
            if (!((*c >= SR_TB_H2H_POINTS && *c <= SR_TB_H2H_AWAY_GOALS) || *c == SR_TB_H2H_REAPPLIED)) nolist[j++] = *c;
        nolist[j] = 0;
        sr_league_rank(n, row, NULL, NULL, 3, 1, 0, LISTS[L], (uint32_t)t, ord);
        sr_league_rank(n, row, pairs, NULL, 3, 1, 0, nolist, (uint32_t)t, o2);
        for (int k = 0; k < n; k++) skip_bad += ord[k] != o2[k];
    }
    CHECK(!bad, "head to head: %d of %d seasons ranked unlike sr_rr_table", bad, nt);
    CHECK(!skip_bad, "no results block: %d places unlike the list without head to head", skip_bad);
    if (!bad && !skip_bad)
        printf("PASS: head to head from the results block = sr_rr_table from the matches on %d random double round robins "
               "(2-24 clubs, 1 in 20 not played yet, goals 0-3 / 0-14, the block's range; h2h points / goal difference / goals / away goals, "
               "reapplied); no block: the head-to-head criteria left out\n", nt);
}

static void refused(void)
{
    sr_srow row[2] = { { 0 }, { 0 } };
    uint8_t ord[2];
    static const uint8_t FP[] = { SR_TB_POINTS, SR_TB_FAIR_PLAY, 0 }, PO[] = { SR_TB_POINTS, SR_TB_PLAY_OFF_MATCH, 0 };
    row[1].club = 1;
    CHECK(sr_league_rank(2, row, NULL, NULL, 3, 1, 0, FP, 0, ord) == -1, "fair play not refused");
    CHECK(sr_league_rank(2, row, NULL, NULL, 3, 1, 0, PO, 0, ord) == -1, "play-off match not refused");
    CHECK(sr_league_rank(0, row, NULL, NULL, 3, 1, 0, F96, 0, ord) == -1, "n 0 not refused");
    CHECK(sr_league_rank(SR_STAGE_CLUBS + 1, row, NULL, NULL, 3, 1, 0, F96, 0, ord) == -1, "n 65 not refused");
    printf("PASS: refused: fair play, a play-off match (they need the game's data), n 0 and 65\n");
}

int main(int argc, char **argv)
{
    const int nt = argc > 1 ? atoi(argv[1]) : 100000;
    faithful_random(nt);
    faithful_real();
    h2h_random(nt / 5);
    refused();
    if (fails) printf("FAIL: %d check(s)\n", fails);
    return fails != 0;
}
