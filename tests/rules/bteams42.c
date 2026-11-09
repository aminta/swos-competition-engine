/* The 4.2 B-team rules (sr_b_teams with second_floor; MC internal/handoff/mc-bteams-gaps-20261109.md F2-F4) on built
 * cases: Italy's 14 divisions, the floor Serie D (level 3), the pool below it (flag 2). Each case also in the other
 * order of the B-team table (F2: the same moves), and the counts kept (up = down, plus the B teams that leave).
 * The Mod 4.1's rules (second_floor 0) stay as they are: tests/rules/bteams_ours.c, check.sh. */
#include <stdio.h>
#include <string.h>
#include "swosrules.h"
static const uint8_t PROM[14] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t REL[14] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static const uint8_t FIRST[4] = { 0, 1, 2, 5 };
static const sr_levels LV = { 4, FIRST };
enum { BASE = 424, JUVE = 5, JUVENG = 44, ATA = 6, ATAU23 = 67, INTER = 0, INTERU23 = 89 };
static const uint8_t SEC[3][2] = { { JUVENG, JUVE }, { ATAU23, ATA }, { INTERU23, INTER } };
static const uint8_t SEC_R[3][2] = { { INTERU23, INTER }, { ATAU23, ATA }, { JUVENG, JUVE } };
static sr_state st;
static int fails;

static sr_league league(int reversed, int floor)
{
    sr_league lp;
    memset(&lp, 0, sizeof lp);
    lp.divisions = 14; lp.promoted = PROM; lp.relegated = REL; lp.italy = 1; lp.serie_b = 1; lp.group_first = 2;
    lp.groups = 3; lp.seconds = 3; lp.second = reversed ? SEC_R : SEC; lp.second_top = 1; lp.second_flags = 3;
    lp.second_floor = (uint8_t)floor;
    return lp;
}
static void tables(void)
{
    static const int sz[14] = { 20, 20, 20, 20, 20, 18, 18, 18, 18, 18, 18, 18, 18, 18 };
    memset(&st, 0, sizeof st);
    st.t.nd = 14;
    for (int d = 0; d < 14; d++) {
        st.t.n[d] = (uint8_t)sz[d];
        for (int k = 0; k < sz[d]; k++) st.t.t[d][k].glob = (uint16_t)(1000 + d * 32 + k);
    }
}
static void put(int d, int place, int ordinal) { st.t.t[d][place - 1].glob = (uint16_t)(BASE + ordinal); }
static void expect(const char *what, int d, int place, int want)
{
    const int got = st.want[d][place - 1];
    if (got != want) { printf("FAIL %s: division %d place %d moves %d, not %d\n", what, d, place, got, want); fails++; }
}
static int exits(int ordinal)
{
    for (int i = 0; i < st.b_exits; i++) if (st.b_exit[i] == BASE + ordinal) return 1;
    return 0;
}
static void counts(const char *what, int extra_div)  /* each division's ups and downs as by place (one more down
                                                          in extra_div: a B team out from the floor) */
{
    for (int d = 0; d < 14; d++) {
        int down = 0, up = 0;
        for (int k = 0; k < st.t.n[d]; k++) { down += st.want[d][k] > 0; up += st.want[d][k] < 0; }
        if (up != (d ? PROM[d] : 0) || down != REL[d] + (d == extra_div)) {
            printf("FAIL %s: division %d up %d down %d\n", what, d, up, down);
            fails++;
        }
    }
}

int main(void)
{
    for (int r = 0; r < 2; r++) {
        const sr_league lp = league(r, 3);
        char tag[64];
        /* F2: Juventus NG and Atalanta U23 both mid-table in Serie B, Juventus and Atalanta relegated from Serie A:
         * both B teams down, the two best placed relegated of Serie B (not a B team) stay -- either order */
        snprintf(tag, sizeof tag, "F2 two B teams of Serie B (order %d)", r);
        tables(); put(0, 18, JUVE); put(0, 19, ATA); put(1, 8, JUVENG); put(1, 12, ATAU23);
        sr_final_moves(&st, &lp, &LV, BASE);
        expect(tag, 1, 8, 1); expect(tag, 1, 12, 1); expect(tag, 1, 18, 0); expect(tag, 1, 19, 0); expect(tag, 1, 20, 1);
        if (st.b_exits) { printf("FAIL %s: %d exits\n", tag, st.b_exits); fails++; }
        counts(tag, -1);
        /* F3 (b): Inter relegated from Serie C group C (20th) and Inter U23 19th of group B, both into Serie D: the
         * guard sends Inter U23 out of the league (below the floor), its group's count kept */
        snprintf(tag, sizeof tag, "F3 B and first team down together (order %d)", r);
        tables(); put(4, 20, INTER); put(3, 20, INTERU23);
        sr_final_moves(&st, &lp, &LV, BASE);
        expect(tag, 3, 20, 1); expect(tag, 4, 20, 1);
        if (!exits(INTERU23) || st.b_exits != 1) { printf("FAIL %s: Inter U23 not out (%d exits)\n", tag, st.b_exits); fails++; }
        counts(tag, -1);
        /* F3 illegal start: Juventus NG in Serie B over Juventus in Serie C staying: one down, the best placed
         * relegated of Serie B stays */
        snprintf(tag, sizeof tag, "F3 illegal start (order %d)", r);
        tables(); put(2, 10, JUVE); put(1, 10, JUVENG);
        sr_final_moves(&st, &lp, &LV, BASE);
        expect(tag, 1, 10, 1); expect(tag, 1, 18, 0); expect(tag, 1, 19, 1);
        counts(tag, -1);
        /* F4 (c): Atalanta below the league (the pool), Atalanta U23 6th of a Serie C group: out of the league */
        snprintf(tag, sizeof tag, "F4 first team below the league (order %d)", r);
        tables(); put(3, 6, ATAU23);
        sr_final_moves(&st, &lp, &LV, BASE);
        expect(tag, 3, 6, 1); expect(tag, 3, 20, 0);
        if (!exits(ATAU23)) { printf("FAIL %s: Atalanta U23 not out\n", tag); fails++; }
        counts(tag, -1);
        /* F4 (a): Milan-like case -- Inter relegated into Serie D, Inter U23 mid-table in a D group: out from the floor
         * (one exit: the pool sends one more up), no relegated club of that group stays */
        snprintf(tag, sizeof tag, "F4 out from the floor (order %d)", r);
        tables(); put(4, 20, INTER); put(6, 9, INTERU23);
        sr_final_moves(&st, &lp, &LV, BASE);
        expect(tag, 6, 9, 1); expect(tag, 6, 17, 1); expect(tag, 6, 18, 1);
        if (!exits(INTERU23) || st.b_exits != 1) { printf("FAIL %s: %d exits\n", tag, st.b_exits); fails++; }
        counts(tag, 6);
        /* a B team relegated by place from the floor is not an exit */
        snprintf(tag, sizeof tag, "relegated by place (order %d)", r);
        tables(); put(0, 3, INTER); put(6, 18, INTERU23);
        sr_final_moves(&st, &lp, &LV, BASE);
        expect(tag, 6, 18, 1);
        if (st.b_exits) { printf("FAIL %s: %d exits\n", tag, st.b_exits); fails++; }
    }
    /* the Mod 4.1's rules (floor 0) keep their order dependence: the F2 case gives different moves by order */
    {
        int m[2];
        for (int r = 0; r < 2; r++) {
            const sr_league lp = league(r, 0);
            tables(); put(0, 18, JUVE); put(0, 19, ATA); put(1, 8, JUVENG); put(1, 12, ATAU23);
            sr_final_moves(&st, &lp, &LV, BASE);
            m[r] = st.want[1][7] * 10 + st.want[1][11];
        }
        if (m[0] == m[1]) { printf("FAIL floor 0: the Mod 4.1's order dependence not reproduced (%d)\n", m[0]); fails++; }
    }
    /* more B teams than b_exit holds, with a floor: refused (a silent cap would lose an exit); without one: accepted */
    {
        static const uint8_t NINE[9][2] = { { 40, 0 }, { 41, 1 }, { 42, 2 }, { 43, 3 }, { 44, 4 }, { 45, 5 }, { 46, 6 },
                                            { 47, 7 }, { 48, 8 } };
        sr_league lp = league(0, 3);
        sr_check_error e;
        lp.seconds = 9; lp.second = NINE;
        if (sr_league_check(&lp, &e) != SR_CHECK_COUNT) { printf("FAIL 9 B teams with a floor: not refused\n"); fails++; }
        lp.second_floor = 0;
        if (sr_league_check(&lp, &e) != SR_CHECK_OK) { printf("FAIL 9 B teams, floor 0: refused\n"); fails++; }
        lp = league(0, 3);
        if (sr_league_check(&lp, &e) != SR_CHECK_OK) { printf("FAIL 3 B teams with a floor: refused\n"); fails++; }
    }
    printf("%s: the 4.2 B-team rules (F2 order-free, F3 guard, F4 floor and exits), %d case(s) failed\n",
           fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
