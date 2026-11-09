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
 *   draws       group draws by pots with associations apart (always found when one exists: the associations
 *               come from a hidden draw), the top seed in every group over the seeds; brackets, seeded and
 *               drawn pairs with a group / association kept apart; impossible ones refused
 *   best placed the k-th placed across groups by the criteria; a larger group's extra club's results dropped
 *   cup         end to end: draw 32 into 8 groups, play them, top two to a seeded last 16, drawn rounds, a
 *               one-match final -- one winner every season, the right number of matches
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
                   SR_TB_AVULSA == SWC_TB_AVULSA && SR_TB_LOSSES_FEWER == SWC_TB_LOSSES_FEWER &&
                   SR_TB_HOME_LOSSES_FEWER == SWC_TB_HOME_LOSSES_FEWER && SR_TB_H2H_TABLE_ONCE == SWC_TB_H2H_TABLE_ONCE &&
                   SR_TB_H2H_TABLE_ONCE + 1 == SWC_TB_COUNT;
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

/* ---- draws ---- */

/* a group draw's rules: sizes, one club of a pot per group, the short pot in the larger groups, associations apart */
static int draw_bad(int n, int g, const uint8_t *assoc, const uint8_t *grp)
{
    uint8_t size[64], cnt[64] = { 0 };
    sr_group_sizes(n, g, size);
    for (int c = 0; c < n; c++) {
        if (grp[c] >= g) return 1;
        cnt[grp[c]]++;
        for (int q = 0; q < c; q++) {
            if (q / g == c / g && grp[q] == grp[c]) return 2;
            if (assoc && assoc[q] == assoc[c] && grp[q] == grp[c]) return 3;
        }
    }
    for (int j = 0; j < g; j++) if (cnt[j] != size[j]) return 4;
    return 0;
}

static void draws(int seasons)
{
    char msg[200];
    uint8_t grp[64], assoc[64], out[64];
    int bad = 0, refused = 0, tries = 0;
    for (int s = 0; s < seasons; s++) {
        seed_ = 1000u + (unsigned)s;
        const int g = 2 + (int)rnd(15), n = 2 * g + (int)rnd(64 - 2 * g + 1);
        /* the associations from a hidden draw (pot k's clubs to groups in a shuffled order, then distinct
         * associations inside each group): a draw that keeps them apart always exists */
        int hid[64], gi[16], ga[16][64];
        for (int p = 0; p * g < n; p++) {
            for (int j = 0; j < g; j++) gi[j] = j;
            for (int j = g - 1; j > 0; j--) { const int r = (int)rnd((unsigned)j + 1), x = gi[j]; gi[j] = gi[r]; gi[r] = x; }
            for (int k = 0; k < g && p * g + k < n; k++) hid[p * g + k] = gi[k];
        }
        uint8_t sz[64];
        sr_group_sizes(n, g, sz);
        for (int c = 0; c < n; c++) if (hid[c] >= 0 && sz[hid[c]] <= c / g) hid[c] = -1;   /* the short pot: larger groups */
        {
            int used[16] = { 0 }, k = 0;
            for (int c = (n / g) * g; c < n; c++) {             /* re-place the short pot's clubs in the larger groups */
                while (used[k]) k++;
                hid[c] = k; used[k] = 1;
            }
        }
        int na = 8 + (int)rnd(57);
        if (na < sz[0]) na = sz[0];
        for (int j = 0; j < g; j++) {                           /* distinct associations inside a group */
            for (int a = 0; a < na; a++) ga[j][a] = a;
            for (int a = na - 1; a > 0; a--) { const int r = (int)rnd((unsigned)a + 1), x = ga[j][a]; ga[j][a] = ga[j][r]; ga[j][r] = x; }
        }
        int cnt[16] = { 0 };
        for (int c = 0; c < n; c++) assoc[c] = (uint8_t)ga[hid[c]][cnt[hid[c]]++];
        tries++;
        const int r = sr_group_draw(n, g, assoc, (uint32_t)s * 7919u, grp);
        if (r == -2) { refused++; continue; }
        if (r != g || draw_bad(n, g, assoc, grp)) bad++;
    }
    snprintf(msg, sizeof msg, "group draw: %d draws, n 4..64 in 2..16 groups, associations a hidden draw keeps apart -- a draw found and the rules held in every one (%d bad, %d found none)", tries, bad, refused);
    check(!bad && !refused, msg);
    int where[8] = { 0 }, same = 1;
    for (int s = 0; s < 400; s++) { sr_group_draw(32, 8, NULL, (uint32_t)s, grp); where[grp[0]]++; }
    for (int j = 0; j < 8; j++) same &= where[j] > 20;
    uint8_t g2[64];
    sr_group_draw(32, 8, NULL, 77u, grp); sr_group_draw(32, 8, NULL, 77u, g2);
    check(same && !memcmp(grp, g2, 32), "group draw: the top seed in every one of 8 groups over 400 seeds (more than 20 each); the same seed, the same draw");
    for (int c = 0; c < 8; c++) assoc[c] = c < 3 ? 0 : (uint8_t)c;
    check(sr_group_draw(8, 2, assoc, 1u, grp) == -2 && sr_group_draw(7, 4, NULL, 1u, grp) == -1,
          "control: three clubs of one association in two groups -- no draw (-2); 7 clubs in 4 groups refused (-1)");
    /* pairings */
    sr_ko_pairs(SR_PAIR_BRACKET, 8, NULL, 0, out);
    check(out[0] == 0 && out[1] == 7 && out[2] == 3 && out[3] == 4 && out[4] == 1 && out[5] == 6 && out[6] == 2 && out[7] == 5,
          "pairs: bracket of 8 -- 0 v 7, 3 v 4, 1 v 6, 2 v 5");
    int bk = 0;
    for (int n = 2; n <= 64; n *= 2) {
        sr_ko_pairs(SR_PAIR_BRACKET, n, NULL, 0, out);
        int seen[64] = { 0 };
        for (int k = 0; k < n; k += 2) { bk += out[k] + out[k + 1] != n - 1; seen[out[k]]++; seen[out[k + 1]]++; }
        for (int k = 0; k < n; k++) bk += seen[k] != 1;
        if (n >= 4) bk += out[0] != 0 || (out[n / 2] != 1 && out[n / 2 + 1] != 1);   /* 0 and 1 in opposite halves */
    }
    check(!bk, "pairs: brackets 2..64 -- seed k v n-1-k, every club once, the top two in opposite halves");
    for (int c = 0; c < 16; c++) assoc[c] = (uint8_t)(c % 8);   /* winner k and runner-up k: one group */
    int pb = 0, mix_ = 0;
    for (int s = 0; s < seasons; s++) {
        const int mode = s & 1 ? SR_PAIR_DRAW : SR_PAIR_SEEDED;
        if (sr_ko_pairs(mode, 16, assoc, (uint32_t)s, out) != 8) { pb++; continue; }
        int seen[16] = { 0 };
        for (int k = 0; k < 16; k += 2) {
            seen[out[k]]++; seen[out[k + 1]]++;
            pb += assoc[out[k]] == assoc[out[k + 1]];
            if (mode == SR_PAIR_SEEDED) pb += out[k] >= 8 || out[k + 1] < 8;
            mix_ += mode == SR_PAIR_DRAW && out[k] < 8 && out[k + 1] < 8;
        }
        for (int k = 0; k < 16; k++) pb += seen[k] != 1;
    }
    snprintf(msg, sizeof msg, "pairs: %d seeded / drawn rounds of 16 (winners v runners-up, never two of one group) -- every club once, seeded top v bottom (%d bad; drawn pairs of two winners %d)", seasons, pb, mix_);
    check(!pb && mix_ > 0, msg);
    uint8_t one[4] = { 0, 0, 0, 0 }, ab[4] = { 0, 0, 1, 1 };
    check(sr_ko_pairs(SR_PAIR_DRAW, 4, one, 1u, out) == -2 && sr_ko_pairs(SR_PAIR_BRACKET, 6, NULL, 1u, out) == -1 &&
          sr_ko_pairs(SR_PAIR_FIXED, 4, ab, 1u, out) == -3 && sr_ko_pairs(SR_PAIR_SEEDED, 5, NULL, 1u, out) == -1,
          "control: four of one association drawn (-2), a bracket of 6 (-1), fixed pairs of one association (-3), 5 clubs (-1)");
}

/* ---- groups end to end ---- */

static int leg_goals(int str) { return (int)rnd(3 + (unsigned)str / 40); }

/* a tie of `legs` legs between a (home first) and b, played to a winner */
static int play_tie(int a, int b, int legs, const int *str, const uint8_t *deci)
{
    sr_leg l[2];
    for (int k = 0; k < legs; k++) {
        const int h = k == 0 ? 0 : 1;
        l[k].home = legs == 1 ? 2 : (uint8_t)h;
        l[k].g[0] = (uint8_t)leg_goals(str[a]); l[k].g[1] = (uint8_t)leg_goals(str[b]);
    }
    sr_tie_extra x = { 0, { 0, 0 }, 0, { 0, 0 }, { 0, 0 } };
    int w = sr_ko_tie(l, legs, &x, deci, 1u);
    if (w == SR_TIE_OPEN) { x.et_played = 1; x.et[0] = (uint8_t)rnd(2); x.et[1] = (uint8_t)rnd(2); w = sr_ko_tie(l, legs, &x, deci, 1u); }
    if (w == SR_TIE_OPEN) { x.pens_played = 1; x.pens[0] = (uint8_t)(3 + rnd(3)); do x.pens[1] = (uint8_t)(3 + rnd(3)); while (x.pens[1] == x.pens[0]); w = sr_ko_tie(l, legs, &x, deci, 1u); }
    return w < 0 || w > 1 ? -1 : w ? b : a;
}

static void cup(int seasons)
{
    char msg[220];
    static sr_match gm[8][12];
    const uint8_t tb[] = { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_H2H_GOALS_FOR,
                           SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_DRAW_LOTS, 0 };
    const uint8_t two[] = { SR_D_AGGREGATE, SR_D_EXTRA_TIME, SR_D_PENALTIES, 0 }, one[] = { SR_D_EXTRA_TIME, SR_D_PENALTIES, 0 };
    int bad = 0, top8 = 0, matches = 0;
    for (int s = 0; s < seasons; s++) {
        seed_ = 50000u + (unsigned)s;
        int str[32];
        uint8_t assoc[32], grp[32];
        for (int c = 0; c < 32; c++) { str[c] = 160 - 4 * c; assoc[c] = (uint8_t)((c * 5 + s) % 8); }
        if (sr_group_draw(32, 8, assoc, (uint32_t)s, grp) != 8 || draw_bad(32, 8, assoc, grp)) { bad++; continue; }
        int club[8][4], cn[8] = { 0 };
        for (int c = 0; c < 32; c++) club[grp[c]][cn[grp[c]]++] = c;
        uint8_t qual[16], qg[16];
        sr_srow t[8][4];
        for (int j = 0; j < 8; j++) {
            const int nm = sr_rr_calendar(4, 2, gm[j], 12);
            int gs[4];
            for (int k = 0; k < 4; k++) gs[k] = str[club[j][k]];
            play(gm[j], nm, gs);
            matches += nm;
            if (sr_rr_table(4, gm[j], nm, 3, 1, 0, tb, (uint32_t)s, t[j]) != 4) bad++;
            qual[j] = (uint8_t)club[j][t[j][0].club]; qual[8 + j] = (uint8_t)club[j][t[j][1].club];
        }
        /* last 16: the winners seeded v the runners-up, never two of one group */
        for (int k = 0; k < 16; k++) qg[k] = grp[qual[k]];
        uint8_t avoid[16], out[16];
        for (int k = 0; k < 16; k++) avoid[k] = qg[k];
        if (sr_ko_pairs(SR_PAIR_SEEDED, 16, avoid, (uint32_t)s, out) != 8) { bad++; continue; }
        int left[16], nl = 0;
        for (int k = 0; k < 16; k += 2) {
            const int w = play_tie(qual[out[k]], qual[out[k + 1]], 2, str, two);
            bad += w < 0 || qg[out[k]] == qg[out[k + 1]] || out[k] >= 8;
            left[nl++] = w; matches += 2;
        }
        /* quarter-finals and semi-finals drawn, a one-match final */
        while (nl > 1) {
            uint8_t o[16];
            if (sr_ko_pairs(SR_PAIR_DRAW, nl, NULL, (uint32_t)(s * 31 + nl), o) != nl / 2) { bad++; break; }
            int nx[8], nn = 0, seen[16] = { 0 };
            for (int k = 0; k < nl; k += 2) {
                seen[o[k]]++; seen[o[k + 1]]++;
                const int legs = nl == 2 ? 1 : 2;
                const int w = play_tie(left[o[k]], left[o[k + 1]], legs, str, legs == 1 ? one : two);
                bad += w < 0; nx[nn++] = w; matches += legs;
            }
            for (int k = 0; k < nl; k++) bad += seen[k] != 1;
            for (int k = 0; k < nn; k++) left[k] = nx[k];
            nl = nn;
        }
        top8 += left[0] < 8;
    }
    const sr_round_size r[] = { { 8, 0 }, { 4, 0 }, { 2, 0 }, { 1, 0 } };
    snprintf(msg, sizeof msg, "cup: %d seasons of 32 clubs -- drawn into 8 groups of 4 (associations apart), 2 legs, top two to a seeded last 16 (no group rematch), drawn rounds, a one-match final: one winner each (%d bad, %d matches)",
             seasons, bad, matches);
    check(!bad && matches == seasons * (96 + 16 + 8 + 4 + 1) && sr_ko_left(16, r, 4) == 1, msg);
    snprintf(msg, sizeof msg, "cup: the top eight seeds win it more often than the other 24 (%d of %d)", top8, seasons);
    check(top8 * 2 > seasons, msg);
}

/* best placed: equal groups ranked by the criteria; unequal groups drop the results against the extra club */
static void best(int seasons)
{
    char msg[200];
    static sr_match gm[6][12];
    const uint8_t tb[] = { SR_TB_POINTS, SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_WINS, SR_TB_NAME, 0 };
    int bad = 0;
    for (int s = 0; s < seasons; s++) {
        seed_ = 90000u + (unsigned)s;
        sr_srow t[6][4], th[6];
        sr_group g[6];
        int str[4] = { 0, 0, 0, 0 };
        for (int j = 0; j < 6; j++) {
            const int nm = sr_rr_calendar(4, 1, gm[j], 12);
            play(gm[j], nm, str);
            sr_rr_table(4, gm[j], nm, 3, 1, 0, tb, 1u, t[j]);
            g[j].m = gm[j]; g[j].nm = nm; g[j].n = 4; g[j].table = t[j];
        }
        uint8_t ord[6];
        if (sr_best_placed(g, 6, 2, 3, 1, 0, tb, 1u, ord) != 6) { bad++; continue; }
        for (int k = 0; k < 6; k++) { th[k] = t[ord[k]][2]; th[k].club = ord[k]; }
        bad += table_bad(th, 6, 0, tb) == 2;
    }
    snprintf(msg, sizeof msg, "best placed: %d seasons of six groups of 4 -- the thirds ranked by points, GD, GF, wins, group order (%d bad)", seasons, bad);
    check(!bad, msg);
    /* group 0 of 4: its third (club 2, 3 points) won only against the last (club 3) and lost the rest; group 1 of 3:
     * its third (club 2, 1 point) drew with club 1. The results v group 0's extra club dropped: 0 points v 1, group 1's
     * third first (kept, it would be 3 v 1) */
    sr_match a[] = { { 0, 2, 3, 1, 4, 0 }, { 0, 0, 2, 1, 1, 0 }, { 0, 1, 2, 1, 1, 0 }, { 0, 0, 1, 1, 2, 0 }, { 0, 0, 3, 1, 2, 0 }, { 0, 1, 3, 1, 2, 0 } };
    sr_match b[] = { { 0, 0, 1, 1, 1, 0 }, { 0, 0, 2, 1, 1, 0 }, { 0, 1, 2, 1, 0, 0 } };
    sr_srow ta[4], tb3[3];
    sr_rr_table(4, a, 6, 3, 1, 0, tb, 1u, ta);
    sr_rr_table(3, b, 3, 3, 1, 0, tb, 1u, tb3);
    sr_group g2[2] = { { a, 6, 4, ta }, { b, 3, 3, tb3 } };
    uint8_t ord[2];
    const int dropped = sr_best_placed(g2, 2, 2, 3, 1, 0, tb, 1u, ord) == 2 && ord[0] == 1 && ord[1] == 0;
    const int n4 = sr_best_placed(g2, 2, 3, 3, 1, 0, tb, 1u, ord);
    const uint8_t h2h[] = { SR_TB_POINTS, SR_TB_H2H_POINTS, 0 };
    check(ta[2].club == 2 && ta[2].pts == 3 && tb3[2].club == 2 && tb3[2].pts == 1 && dropped && n4 == 1 && ord[0] == 0 &&
          sr_best_placed(g2, 2, 2, 3, 1, 0, h2h, 1u, ord) == -1,
          "best placed: a third of a group of 4 whose only win came against the last ranks below a third of 3 with two draws (results v the extra club dropped); a fourth place only group 0 has; head to head refused");
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
    draws(seasons);
    best(seasons);
    cup(seasons);
    return fails ? 1 : 0;
}
