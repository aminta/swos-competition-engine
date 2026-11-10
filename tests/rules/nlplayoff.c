/* 4.2 EN non-league: the National League, North and South promotion play-offs as gplay bytes
 * (tests/rules/specs/eng_nl.spec: the Mod's nonleague.nl_playoff through gplay.compile_spec, make_eng_nl.py) against
 * the real format (the 2025-26 schedule, unchanged in 2026-27; source in the Mod's tools/nonleague.py):
 *   eliminators   A 5th v 6th, B 4th v 7th, one match at the better placed
 *   semi-finals   fixed: 2nd v A's winner, 3rd v B's winner, at the 2nd / 3rd
 *   final         the National League at Wembley (neutral: its spec byte); North / South at the better placed
 *   promotion     the winner to its winner_div (League Two; the National League: from S two rows up, the parallel
 *                 N / S), the moves sr_knockouts adds
 * Every combination of winners is forced (sr_force): 2^5 per play-off, each tie's clubs and host checked.
 * Control: the 'best v worst' semi-finals (re-seeded, the Mod's spec before 42-nlpo) differ in some combinations.
 *   nlplayoff SPEC */
#include <stdio.h>
#include <string.h>
#include "swosrules.h"

#define ND 7
#define TEAM(d, k) ((uint16_t)(0x1000 + (d) * 32 + (k)))         /* (division d, place k + 1) */
static const uint8_t PROM[ND] = { 0, 3, 3, 3, 2, 2, 2 }, REL[ND] = { 3, 3, 3, 2, 4, 0, 0 };
static uint8_t SPEC[512];
static uint32_t LEN;

static int pick, nt;                    /* (bit k % 5 of pick: a play-off's tie k goes to its away club) */
static uint16_t th[32], ta[32];
static uint16_t f_win(void *ctx, uint16_t home, uint16_t away, int32_t legs)
{
    (void)ctx; (void)legs;
    const int k = nt++;
    if (k < 32) th[k] = home, ta[k] = away;
    return pick >> (k % 5) & 1 ? away : home;
}
static void f_draw(void *ctx, int32_t n, uint16_t *g) { (void)ctx; (void)n; (void)g; }
static uint8_t f_lot(void *ctx, uint16_t g) { (void)ctx; return (uint8_t)g; }
static const sr_force FORCE = { 0, f_win, f_draw, f_lot };

static int place(int d, uint16_t g) { return g >= d * 24 && g < d * 24 + 24 ? g - d * 24 + 1 : -1; }   /* (sr_force sees globs) */

/* play-off p's ties (from th / ta + 5 p) against the format: 0 when right, else the reason in why */
static int check(int p, int reseed, int neutral_final, char *why)
{
    const int d = 4 + p, a = pick & 1 ? 6 : 5, b = pick & 2 ? 7 : 4;
    const int s1a = reseed ? (a > b ? a : b) : a, s2a = reseed ? (a > b ? b : a) : b;
    const int w1 = pick & 4 ? s1a : 2, w2 = pick & 8 ? s2a : 3;
    const int want[5][2] = { { 5, 6 }, { 4, 7 }, { 2, s1a }, { 3, s2a }, { w1 < w2 ? w1 : w2, w1 < w2 ? w2 : w1 } };
    for (int k = 0; k < 5; k++) {
        int h = place(d, th[5 * p + k]), w = place(d, ta[5 * p + k]);
        if (k == 4 && neutral_final && h > w) { const int x = h; h = w; w = x; }    /* (neutral: either order) */
        if (h != want[k][0] || w != want[k][1]) {
            snprintf(why, 160, "division %d, winners %d: tie %d %d v %d, the real format %d v %d", d, pick, k + 1, h, w,
                    want[k][0], want[k][1]);
            return 1;
        }
    }
    return 0;
}

/* the 32 combinations: every tie as the format says (reseed 1: the control's format); 0 when all right */
static int run(int reseed, char *why)
{
    for (pick = 0; pick < 32; pick++) {
        static sr_state st;
        sr_league lg;
        sr_season se;
        memset(&st, 0, sizeof st); memset(&lg, 0, sizeof lg); memset(&se, 0, sizeof se);
        st.t.nd = ND;
        for (int d = 0; d < ND; d++) {
            st.t.n[d] = d == 0 ? 20 : 24;
            for (int k = 0; k < st.t.n[d]; k++) {
                sr_row *r = &st.t.t[d][k];
                r->team = TEAM(d, k); r->glob = (uint16_t)(d * 24 + k); r->pts = (uint16_t)(100 - 3 * k); r->won = r->pts / 3;
            }
        }
        lg.divisions = ND; lg.promoted = PROM; lg.relegated = REL; lg.spec = SPEC; lg.spec_len = LEN;
        se.force = &FORCE;
        nt = 0;
        sr_knockouts(&st, &lg, &se);
        if (nt != 15) { snprintf(why, 160, "winners %d: %d ties (want 3 x 5)", pick, nt); return 1; }
        for (int p = 0; p < 3; p++) {
            if (check(p, reseed, p == 0, why)) return 1;
            /* the winner: one level up */
            const uint16_t w = pick & 16 ? ta[5 * p + 4] : th[5 * p + 4];
            int up = 0;
            for (int i = 0; i < st.nmv; i++) if (st.mv[i].glob == w && st.mv[i].delta == (p ? 4 : 3) - (4 + p)) up = 1;   /* (S: two rows up, the parallel N / S) */
            if (!up && !reseed) { snprintf(why, 160, "winners %d: division %d's play-off winner not moved up", pick, 4 + p); return 1; }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) { printf("FAIL: nlplayoff SPEC\n"); return 1; }
    LEN = (uint32_t)fread(SPEC, 1, sizeof SPEC, f);
    fclose(f);
    sr_check_error e;
    if (sr_spec_check(SPEC, LEN, ND, &e) != SR_CHECK_OK) { printf("FAIL: the spec refused at byte %u\n", e.offset); return 1; }
    int fails = 0;
    /* the finals' hosts as bytes: header 6, entrants 6 x 4, rounds of 6 + members (4, 4, 2); the final's host byte */
    const uint32_t len1 = 6 + 24 + (6 + 4) + (6 + 4) + (6 + 2), fin = 6 + 24 + 10 + 10 + 2;
    const int hosts_ok = LEN == 3 * len1 + 1 && SPEC[fin] == 3 && SPEC[len1 + fin] == 1 && SPEC[2 * len1 + fin] == 1;
    printf("%s: the finals' hosts: the National League's neutral (Wembley), North / South's the better placed\n",
           hosts_ok ? "PASS" : "FAIL");
    fails += !hosts_ok;
    char why[160] = "";
    const int bad = run(0, why);
    printf("%s: the National League, North and South play-offs, 32 forced combinations each: A 5v6, B 4v7, 2nd v A's "
           "winner, 3rd v B's, the final, the winner promoted%s%s\n", bad ? "FAIL" : "PASS", bad ? " -- " : "", bad ? why : "");
    fails += bad;
    char why2[160] = "";
    const int caught = run(1, why2);
    printf("%s: control: re-seeded semi-finals (2nd v the lower placed winner) differ -> caught (%s)\n",
           caught ? "PASS" : "FAIL", caught ? why2 : "not caught");
    fails += !caught;
    return fails != 0;
}
