/* The generic stages (sr_stage.c): what the competition editor's round_robin / groups / knockout /
 * single_match stages need from the engine. Nothing plays them yet, so these are invariants, each with a
 * control that breaks it and must be caught.
 *   codes       SR_TB_* / SR_D_* = swcc's SwcTieBreak / SwcDecider
 *   calendar    n 2..64, legs 1..4: every pair `legs` times, home and away split, one match a club a
 *               matchday, the rest once a leg when n is odd, home matches within one a leg, leg 2 = leg 1
 *               mirrored
 *   table       random seasons: records add up, the order obeys the criteria, the same whatever the input
 *               order; a three-way head-to-head; criteria that need the game's data refused
 *   knockout    swcc's arithmetic (16 -> 8 -> 4 -> 2 -> 1, byes, the wrong round named)
 *   ties        the editor's presets: aggregate, away goals, extra time, penalties, replay, better placed
 *   groups      sizes as even as can be, the larger first
 * Argument: SEASONS (default 500). Prints PASS / FAIL lines; exit 1 on a FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"
#include "../../format/swc_format.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

static unsigned seed_;
static unsigned rnd(unsigned n) { seed_ = seed_ * 1103515245u + 12345u; return (seed_ >> 8) % n; }

/* ---- codes ---- */

static void codes(void)
{
    const int tb = SR_TB_POINTS == SWC_TB_POINTS && SR_TB_H2H_POINTS == SWC_TB_H2H_POINTS &&
                   SR_TB_DRAW_LOTS == SWC_TB_DRAW_LOTS && SR_TB_NAME == SWC_TB_NAME &&
                   SR_TB_FAITHFUL_1996 == SWC_TB_FAITHFUL_1996 && SR_TB_H2H_REAPPLIED == SWC_TB_H2H_REAPPLIED &&
                   SR_TB_H2H_REAPPLIED + 1 == SWC_TB_COUNT;
    const int de = SR_D_AGGREGATE == SWC_D_AGGREGATE && SR_D_PENALTIES == SWC_D_PENALTIES &&
                   SR_D_REPLAY == SWC_D_REPLAY && SR_D_GOLDEN_GOAL == SWC_D_GOLDEN_GOAL &&
                   SR_D_GOLDEN_GOAL + 1 == SWC_D_COUNT;
    check(tb && de, "codes: SR_TB_* / SR_D_* are swcc's SwcTieBreak / SwcDecider");
}

/* ---- the calendar ---- */

/* 0 = a good calendar; else a short reason */
static const char *cal_bad(int n, int legs, const sr_match *m, int nm)
{
    static char why[96];
    const int mds = n - 1 + (n & 1);
    if (nm != legs * n * (n - 1) / 2) return "match count";
    static int pair[64][64], day[256][64], home[4][64];
    memset(pair, 0, sizeof pair); memset(day, 0, sizeof day); memset(home, 0, sizeof home);
    for (int k = 0; k < nm; k++) {
        const sr_match *x = &m[k];
        if (x->home >= n || x->away >= n || x->home == x->away) return "a club outside the stage";
        if (x->md >= legs * mds) return "matchday out of range";
        if (day[x->md][x->home]++ || day[x->md][x->away]++) { snprintf(why, sizeof why, "a club twice on matchday %d", x->md); return why; }
        pair[x->home][x->away]++;
        home[x->md / mds][x->home]++;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
            const int ab = pair[a][b], ba = pair[b][a];
            if (ab + ba != legs || abs(ab - ba) > (legs & 1)) { snprintf(why, sizeof why, "pair %d-%d met %d+%d", a, b, ab, ba); return why; }
        }
    for (int l = 0; l < legs; l++)
        for (int c = 0; c < n; c++)
            if (home[l][c] < (n - 1) / 2 || home[l][c] > n / 2) { snprintf(why, sizeof why, "club %d home %d times in leg %d", c, home[l][c], l); return why; }
    if (n & 1)
        for (int d = 0; d < legs * mds; d++) {
            int rest = 0;
            for (int c = 0; c < n; c++) rest += !day[d][c];
            if (rest != 1) return "not one club resting";
        }
    const int per = n * (n - 1) / 2;
    for (int l = 1; l < legs; l++)
        for (int k = 0; k < per; k++) {
            const sr_match *a = &m[k], *b = &m[l * per + k];
            const int mirror = l & 1;
            if (b->md != a->md + l * mds || (mirror ? b->home != a->away || b->away != a->home : b->home != a->home || b->away != a->away))
                return "a later leg not the first's (mirror)";
        }
    return 0;
}

static void calendar(void)
{
    static sr_match m[4 * 64 * 63 / 2];
    int bad = 0;
    char msg[160] = "";
    for (int n = 2; n <= SR_STAGE_CLUBS; n++)
        for (int legs = 1; legs <= 4; legs++) {
            const int nm = sr_rr_calendar(n, legs, m, (int)(sizeof m / sizeof *m));
            const char *why = cal_bad(n, legs, m, nm);
            if (why && !bad++) snprintf(msg, sizeof msg, " (first: n %d legs %d: %s)", n, legs, why);
        }
    char t[256];
    snprintf(t, sizeof t, "calendar: n 2..64 x legs 1..4 -- pairs, one match a day, rests, home balance, mirrored legs%s", msg);
    check(!bad, t);
    check(sr_rr_calendar(1, 2, m, 9999) == -1 && sr_rr_calendar(65, 1, m, 99999) == -1 && sr_rr_calendar(10, 5, m, 9999) == -1 &&
          sr_rr_calendar(10, 2, m, 89) == -1 && sr_rr_calendar(10, 2, m, 90) == 90, "calendar: n 1 / 65, legs 5, room 89 for 90 refused; room 90 taken");
    /* controls: the checker catches a club twice on a day, a pair's sides, a broken mirror */
    int nm = sr_rr_calendar(18, 2, m, 9999);
    sr_match keep = m[3];
    m[3].away = m[4].home;
    const int c1 = cal_bad(18, 2, m, nm) != 0;
    m[3] = keep;
    { uint8_t h = m[200].home; m[200].home = m[200].away; m[200].away = h; }
    const int c2 = cal_bad(18, 2, m, nm) != 0;
    check(c1 && c2 && cal_bad(18, 2, m, nm - 1) != 0, "control: a club twice on a day, a match's sides swapped, a match missing -- each caught");
}

/* ---- the table ---- */

static void play(sr_match *m, int nm, const int *str)
{
    for (int k = 0; k < nm; k++) {
        m[k].played = 1;
        m[k].hg = (uint8_t)rnd(3 + str[m[k].home] / 40);
        m[k].ag = (uint8_t)rnd(2 + str[m[k].away] / 40);
    }
}

static int cmp_key(const sr_srow *r, int t)
{
    switch (t) {
    case SR_TB_POINTS: return r->pts;
    case SR_TB_GOAL_DIFFERENCE: return r->gf - r->ga;
    case SR_TB_GOALS_FOR: return r->gf;
    case SR_TB_GOALS_AGAINST: return -r->ga;
    case SR_TB_WINS: return r->w;
    case SR_TB_AWAY_WINS: return r->aw;
    case SR_TB_AWAY_GOALS_FOR: return r->agf;
    }
    return 0;
}

/* the table's rows consistent and in the order of a criteria list without head to head */
static int table_bad(const sr_srow *t, int n, int nm, const uint8_t *tb)
{
    int pl = 0, gf = 0, ga = 0, w = 0, l = 0, d = 0;
    for (int k = 0; k < n; k++) {
        const sr_srow *r = &t[k];
        pl += r->pl; gf += r->gf; ga += r->ga; w += r->w; l += r->l; d += r->d;
        if (r->pl != r->w + r->d + r->l || r->pts != 3 * r->w + r->d || r->aw > r->w) return 1;
        if (k) {
            int cmp = 0;
            for (const uint8_t *c = tb; *c && !cmp; c++)
                cmp = *c == SR_TB_NAME ? t[k].club - t[k - 1].club : cmp_key(&t[k - 1], *c) - cmp_key(&t[k], *c);
            if (cmp < 0) return 2;
        }
    }
    return pl != 2 * nm || gf != ga || w != l || d % 2 ? 3 : 0;
}

static void table(int seasons)
{
    static sr_match m[2 * 64 * 63 / 2];
    const uint8_t tb[] = { SR_TB_POINTS, SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_WINS, SR_TB_AWAY_GOALS_FOR, SR_TB_NAME, 0 };
    int bad = 0, perm = 0, top_strong = 0;
    for (int s = 0; s < seasons; s++) {
        seed_ = 77u + (unsigned)s;
        const int n = 4 + (int)rnd(21), legs = 1 + (int)rnd(2);
        int str[64];
        for (int c = 0; c < n; c++) str[c] = 200 - c * 200 / n;
        const int nm = sr_rr_calendar(n, legs, m, (int)(sizeof m / sizeof *m));
        play(m, nm, str);
        sr_srow t[64], u[64];
        if (sr_rr_table(n, m, nm, 3, 1, 0, tb, (unsigned)s, t) != n || table_bad(t, n, nm, tb)) bad++;
        top_strong += t[0].club < n / 2;
        /* the same matches in another order: the same table */
        for (int k = nm - 1; k > 0; k--) { const int j = (int)rnd((unsigned)k + 1); sr_match x = m[k]; m[k] = m[j]; m[j] = x; }
        const uint8_t tl[] = { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_DRAW_LOTS, 0 };
        sr_rr_table(n, m, nm, 3, 1, 0, tl, 9u, t);
        for (int k = nm - 1; k > 0; k--) { const int j = (int)rnd((unsigned)k + 1); sr_match x = m[k]; m[k] = m[j]; m[j] = x; }
        sr_rr_table(n, m, nm, 3, 1, 0, tl, 9u, u);
        for (int k = 0; k < n; k++) perm += t[k].club != u[k].club;
    }
    char msg[160];
    snprintf(msg, sizeof msg, "table: %d random seasons (4-24 clubs, 1-2 legs) -- records add up, order by points, gd, gf, wins, away gf, name (%d bad)", seasons, bad);
    check(!bad, msg);
    snprintf(msg, sizeof msg, "table: points / head to head / draw lots -- the same order whatever the matches' order (%d rows differ)", perm);
    check(!perm, msg);
    snprintf(msg, sizeof msg, "table: the stronger half tops the table more often (%d of %d)", top_strong, seasons);
    check(top_strong * 10 > seasons * 7, msg);

    /* three clubs level on points (each beats 3 and one of the others): 0 beat 1 3-0, 1 beat 2 1-0, 2 beat 0 2-1;
     * head-to-head points 3 each, head-to-head goal difference 0 +2, 2 0, 1 -2: 0, 2, 1, then 3 */
    sr_match h[] = {
        { 0, 0, 3, 1, 1, 0 }, { 0, 1, 3, 1, 5, 0 }, { 0, 2, 3, 1, 3, 0 },
        { 1, 0, 1, 1, 3, 0 }, { 1, 1, 2, 1, 1, 0 }, { 1, 2, 0, 1, 2, 1 },
    };
    sr_srow t[4];
    const uint8_t hh[] = { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_NAME, 0 };
    sr_rr_table(4, h, 6, 3, 1, 0, hh, 1u, t);
    snprintf(msg, sizeof msg, "table: a three-way tie on 6 points split by head to head -- %d %d %d %d (want 0 2 1 3)", t[0].club, t[1].club, t[2].club, t[3].club);
    check(t[0].club == 0 && t[1].club == 2 && t[2].club == 1 && t[3].club == 3 && t[0].pts == 6 && t[2].pts == 6, msg);
    const uint8_t gd_first[] = { SR_TB_POINTS, SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_NAME, 0 };
    sr_rr_table(4, h, 6, 3, 1, 0, gd_first, 1u, t);
    snprintf(msg, sizeof msg, "table: the same three by overall goal difference (all +3), then goals for (6 5 5), then name -- %d %d %d (want 1 0 2)", t[0].club, t[1].club, t[2].club);
    check(t[0].club == 1 && t[1].club == 0 && t[2].club == 2, msg);

    /* UEFA's re-application: four clubs on 4 points (one win, one draw, one loss each), all level on head-to-head
     * goal difference (0); head-to-head goals for 1 2 2 1 leaves {1, 2} and {0, 3}. Once (the default): name inside
     * them, 1 2 0 3. Re-applied: 2 beat 1 2-1, 3 beat 0 1-0, so 2 1 3 0 */
    sr_match u[] = {
        { 0, 0, 1, 1, 0, 0 }, { 0, 2, 3, 1, 0, 0 }, { 1, 0, 2, 1, 1, 0 },
        { 1, 1, 3, 1, 1, 0 }, { 2, 0, 3, 1, 0, 1 }, { 2, 1, 2, 1, 1, 2 },
    };
    const uint8_t once[] = { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_H2H_GOALS_FOR, SR_TB_NAME, 0 };
    const uint8_t again[] = { SR_TB_POINTS, SR_TB_H2H_REAPPLIED, SR_TB_NAME, 0 };
    sr_rr_table(4, u, 6, 3, 1, 0, once, 1u, t);
    snprintf(msg, sizeof msg, "table: four level, head to head once -- %d %d %d %d (want 1 2 0 3)", t[0].club, t[1].club, t[2].club, t[3].club);
    check(t[0].club == 1 && t[1].club == 2 && t[2].club == 0 && t[3].club == 3, msg);
    sr_rr_table(4, u, 6, 3, 1, 0, again, 1u, t);
    snprintf(msg, sizeof msg, "table: the same four, head_to_head_reapplied -- %d %d %d %d (want 2 1 3 0)", t[0].club, t[1].club, t[2].club, t[3].club);
    check(t[0].club == 2 && t[1].club == 1 && t[2].club == 3 && t[3].club == 0, msg);
    /* the three-way case above: re-applying changes nothing there (the first pass splits all three) */
    const uint8_t again3[] = { SR_TB_POINTS, SR_TB_H2H_REAPPLIED, SR_TB_NAME, 0 };
    sr_rr_table(4, h, 6, 3, 1, 0, again3, 1u, t);
    check(t[0].club == 0 && t[1].club == 2 && t[2].club == 1 && t[3].club == 3, "table: head_to_head_reapplied on the three-way tie -- 0 2 1 3 as once");

    int refused = 0;
    const uint8_t no[] = { SR_TB_FAIR_PLAY, SR_TB_COEFFICIENT, SR_TB_PREVIOUS_STAGE_RANK, SR_TB_PLAY_OFF_MATCH, SR_TB_FAITHFUL_1996, 99 };
    for (int k = 0; k < 6; k++) { const uint8_t one[] = { SR_TB_POINTS, no[k], 0 }; refused += sr_rr_table(4, h, 6, 3, 1, 0, one, 1u, t) == -1; }
    sr_match out_of[] = { { 0, 0, 7, 1, 1, 0 } };
    refused += sr_rr_table(4, out_of, 1, 3, 1, 0, hh, 1u, t) == -1;
    check(refused == 7, "table: fair play, coefficient, previous stage rank, play-off match, faithful_1996, an unknown code, a club outside the stage -- refused");
    /* control: a table with two rows swapped is caught */
    const int nm = sr_rr_calendar(10, 2, m, 9999);
    seed_ = 5; { int str[10] = { 0 }; play(m, nm, str); }
    sr_srow c[10];
    sr_rr_table(10, m, nm, 3, 1, 0, tb, 1u, c);
    int k = 0;
    while (k < 9 && c[k].pts == c[k + 1].pts) k++;
    sr_srow x = c[k]; c[k] = c[k + 1]; c[k + 1] = x;
    check(table_bad(c, 10, nm, tb) == 2, "control: two rows of different points swapped -- caught");
}

/* ---- knockouts ---- */

static void knockout(void)
{
    const sr_round_size cup[] = { { 8, 0 }, { 4, 0 }, { 2, 0 }, { 1, 0 } };
    const sr_round_size byes[] = { { 2, 6 }, { 4, 0 }, { 2, 0 }, { 1, 0 } };
    const sr_round_size bad[] = { { 7, 0 }, { 4, 0 } };
    const sr_round_size bad2[] = { { 8, 0 }, { 3, 0 } };
    check(sr_ko_left(16, cup, 4) == 1 && sr_ko_left(10, byes, 4) == 1, "knockout: 16 -> 8 -> 4 -> 2 -> 1; 10 clubs with 6 byes -> 8 -> ... -> 1");
    check(sr_ko_left(16, bad, 2) == -1 && sr_ko_left(16, bad2, 2) == -2, "knockout: 7 ties for 16 refused at round 1, 3 ties for 8 at round 2 (swcc's message)");
}

static int tie(int g00, int g01, int home0, int g10, int g11, int home1, int nl, int etp, int e0, int e1, int pp, int p0, int p1,
               const uint8_t *d)
{
    sr_leg l[2] = { { { (uint8_t)g00, (uint8_t)g01 }, (uint8_t)home0 }, { { (uint8_t)g10, (uint8_t)g11 }, (uint8_t)home1 } };
    sr_tie_extra x = { (uint8_t)etp, { (uint8_t)e0, (uint8_t)e1 }, (uint8_t)pp, { (uint8_t)p0, (uint8_t)p1 }, { 1, 2 } };
    return sr_ko_tie(l, nl, &x, d, 3u);
}

static void ties(void)
{
    const uint8_t two[] = { SR_D_AGGREGATE, SR_D_AWAY_GOALS, SR_D_EXTRA_TIME, SR_D_PENALTIES, 0 };   /* "andata e ritorno" */
    const uint8_t one[] = { SR_D_EXTRA_TIME, SR_D_PENALTIES, 0 };                                    /* "partita secca" */
    /* side 0 at home first: 2-1, then 1-2 at side 1's: 3-3, away goals 1 each */
    check(tie(2, 1, 0, 1, 2, 1, 2, 0, 0, 0, 0, 0, 0, two) == SR_TIE_OPEN, "tie: 2-1 / 1-2 (away goals level), extra time not played yet -- open");
    check(tie(2, 1, 0, 1, 2, 1, 2, 1, 0, 1, 0, 0, 0, two) == 1, "tie: ... extra time 0-1 -- side 1");
    check(tie(2, 1, 0, 1, 2, 1, 2, 1, 1, 1, 1, 4, 5, two) == 1,
          "tie: ... extra time 1-1 (away goals come before extra time in the list: not looked at again), penalties 4-5 -- side 1");
    check(tie(3, 2, 0, 0, 1, 1, 2, 0, 0, 0, 0, 0, 0, two) == 1, "tie: 3-2 / 1-0 at side 1's: 3-3, side 1 two away goals -- side 1");
    check(tie(1, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0, 0, 0, two) == 1, "tie: 1-0 / 0-2 -- side 1 on aggregate");
    check(tie(1, 1, 2, 0, 0, 0, 1, 1, 0, 0, 1, 3, 4, one) == 1, "tie: single match 1-1, extra time 0-0, penalties 3-4 -- side 1");
    check(tie(1, 1, 2, 0, 0, 0, 1, 1, 2, 1, 0, 0, 0, one) == 0, "tie: single match 1-1, extra time 2-1 -- side 0");
    const uint8_t rep[] = { SR_D_REPLAY, 0 }, bp[] = { SR_D_BETTER_PLACED, 0 }, none[] = { 0 };
    check(tie(0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, rep) == SR_TIE_REPLAY, "tie: 0-0 with 'replay' -- a replay");
    check(tie(2, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, bp) == 0, "tie: 2-2, better placed (places 1 v 2) -- side 0");
    check(tie(2, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, none) == SR_TIE_OPEN, "control: level with no decider (swcc's \"missing 'decider'\") -- open, never a winner");
}

/* ---- groups ---- */

static void groups(void)
{
    int bad = 0;
    uint8_t s[64];
    for (int n = 4; n <= 64; n++)
        for (int g = 1; g <= n / 2; g++) {
            if (sr_group_sizes(n, g, s) != g) { bad++; continue; }
            int sum = 0;
            for (int k = 0; k < g; k++) { sum += s[k]; if (s[k] < 2 || (k && (s[k] > s[k - 1] || s[0] - s[k] > 1))) bad++; }
            bad += sum != n;
        }
    sr_group_sizes(29, 3, s);
    check(!bad && s[0] == 10 && s[1] == 10 && s[2] == 9, "groups: n 4..64 in 1..n/2 groups -- sizes add up, differ by one at most, the larger first (29 in 3: 10 10 9)");
    check(sr_group_sizes(7, 4, s) == -1 && sr_group_sizes(8, 0, s) == -1, "groups: 7 clubs in 4 groups (a group of 1) and 0 groups refused");
}

int main(int argc, char **argv)
{
    const int seasons = argc > 1 ? atoi(argv[1]) : 500;
    codes();
    calendar();
    table(seasons);
    knockout();
    ties();
    groups();
    return fails ? 1 : 0;
}
