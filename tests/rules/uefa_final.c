/* 4.2 S1.3: the FIGC C.U. 244/A exception (iv) -- a Serie A play-off match (title 1st v 2nd, play-out 17th v 18th)
 * level on points is played, unless one of its clubs is already in a UEFA competition's final (sr_season.uefa_final,
 * host bit 0x40, pack "skip_if_uefa_finalist"): then the better placed goes through, no match. Forced cases. */
#include <stdio.h>
#include <string.h>
#include "swosrules.h"

static uint8_t SPEC[64];
static uint32_t at;
static void b(int x) { SPEC[at++] = (uint8_t)x; }
static int fails, nties;
static uint16_t f_win(void *ctx, uint16_t home, uint16_t away, int32_t legs)
{
    (void)ctx; (void)legs;
    nties++;
    return home > away ? home : away;           /* (every tie to the worse placed: a played tie shows) */
}
static void f_draw(void *ctx, int32_t n, uint16_t *g) { (void)ctx; (void)n; (void)g; }
static uint8_t f_lot(void *ctx, uint16_t g) { (void)ctx; return (uint8_t)g; }
static const sr_force FORCE = { 0, f_win, f_draw, f_lot };
static const uint8_t PROM[2] = { 0, 2 }, REL[2] = { 2, 3 };

#define TEAM(k) ((uint16_t)(0x1400 + (k)))
static void one(const char *what, int flag, int gap2, uint16_t f0, uint16_t f1, int want_ties, int want_champ)
{
    static sr_state st;
    sr_league lg;
    sr_season se;
    memset(&st, 0, sizeof st); memset(&lg, 0, sizeof lg); memset(&se, 0, sizeof se);
    at = 0;
    b(5); b(2); b(1); b(0); b(0); b(0);         /* title: 1st v 2nd, one match at the better placed, penalties, gap 1 */
    b(0); b(0); b(1); b(0); b(0); b(0); b(2); b(0);
    b(1); b(1); b(1 | (flag ? 0x40 : 0)); b(3); b(0); b(1); b(0); b(1);
    b(0);
    st.t.nd = 2;
    st.t.n[0] = st.t.n[1] = 20;
    for (int d = 0; d < 2; d++)
        for (int k = 0; k < 20; k++) {
            sr_row *r = &st.t.t[d][k];
            r->team = TEAM(d * 32 + k); r->glob = (uint16_t)(d * 20 + k); r->pts = (uint16_t)(90 - 3 * k); r->won = r->pts / 3;
        }
    st.t.t[0][1].pts = (uint16_t)(st.t.t[0][0].pts - gap2);
    lg.divisions = 2; lg.promoted = PROM; lg.relegated = REL; lg.spec = SPEC; lg.spec_len = at;
    se.force = &FORCE;
    se.uefa_final[0] = f0; se.uefa_final[1] = f1;
    sr_check_error e;
    if (sr_spec_check(SPEC, at, 2, &e) != SR_CHECK_OK) { printf("FAIL %s: the spec refused at byte %u\n", what, e.offset); fails++; return; }
    nties = 0;
    sr_knockouts(&st, &lg, &se);
    int champ = -1;
    for (int i = 0; i < st.gp.nres; i++)
        if (st.gp.res[i].kind == SR_KIND_TITLE) champ = st.gp.res[i].team[0] == TEAM(0) ? 1 : st.gp.res[i].team[0] == TEAM(1) ? 2 : 0;
    const int ok = nties == want_ties && champ == want_champ;
    printf("%s %s: %d match(es), champion %d%s\n", ok ? "ok  " : "FAIL", what, nties, champ, ok ? "" : " (want other)");
    fails += !ok;
}

int main(void)
{
    one("level, no finalist", 1, 0, 0, 0, 1, 2);
    one("level, the 2nd a UEFA finalist", 1, 0, TEAM(1), 0, 0, 1);
    one("level, the 1st a UEFA finalist", 1, 0, 0, TEAM(0), 0, 1);
    one("level, another club a finalist", 1, 0, TEAM(5), TEAM(33), 1, 2);
    one("level, the 2nd a finalist, no flag", 0, 0, TEAM(1), 0, 1, 2);
    one("3 apart (gap 1), no finalist", 1, 3, 0, 0, 0, 1);
    printf("%s uefa_final: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
