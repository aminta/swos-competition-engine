/* The Coppa Italia Serie C winner's play-off place (sr_dplay cup_place = the Mod's dplay.c 353984f; FIGC C.U. 209/A,
 * NOIF art. 49 II.1 d-e), one host case per place (the Mod's swostest dplay_cup_place): the winner in 11th .. 15th, or
 * in 16th / 17th with its play-out not played (8+ points), keeps the place; in 2nd .. 10th, relegated (20th) or in a
 * play-out place it takes none and the 11th of its group plays instead. Over many seasons: a kept place promotes the
 * winner sometimes and never the 11th; a precluded one the 11th sometimes and never the winner. */
#include <stdio.h>
#include <string.h>
#include "swosrules.h"
static const uint8_t PROM[16] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t REL[16] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static sr_state st;
static int fails;

static void tables(int step, int s)     /* every division by place; Serie C's points `step` apart (2: the play-outs
                                           played; 3, 6 more below the 17th: 15 and 9 points, not played); the clubs' numbers and goals by season s
                                           (the play-offs' draws and ties follow them) */
{
    static const int sz[14] = { 20, 20, 20, 20, 20, 18, 18, 18, 18, 18, 18, 18, 18, 18 };
    st.t.nd = 14;
    for (int d = 0; d < 14; d++) {
        st.t.n[d] = (uint8_t)sz[d];
        for (int k = 0; k < sz[d]; k++) {
            sr_row *r = &st.t.t[d][k];
            memset(r, 0, sizeof *r);
            r->team = (uint16_t)(1 + d * 32 + k + 512 * (s % 97)); r->glob = (uint16_t)(1000 + d * 32 + k + 512 * (s % 97));
            r->pts = (uint16_t)(100 - (d >= 2 && d <= 4 ? step * k + (step == 3 && k >= 17) * 6 : 2 * k) - d);
            r->won = (uint16_t)(30 - k); r->gf = (uint16_t)(60 - k + (s * 7 + k * 13) % 11); r->ga = (uint16_t)(20 + k);
        }
    }
}

static void place(int p, int step, int kept)
{
    const sr_league lg = { 14, PROM, REL, 1, 1, 2, 3 };
    int up_w = 0, up_11 = 0;
    sr_reset(&st);
    for (int s = 0; s < 3000; s++) {
        sr_season se;
        memset(&se, 0, sizeof se);
        tables(step, s);
        const int g = 2 + s % 3;
        se.cup_winner = st.t.t[g][p - 1].team;
        const uint16_t gw = st.t.t[g][p - 1].glob, g11 = st.t.t[g][10].glob;
        sr_italy(&st, &lg, &se);
        for (int i = 0; i < st.nmv; i++) {
            if (st.mv[i].glob == gw && st.mv[i].delta < 0) up_w++;
            if (st.mv[i].glob == g11 && st.mv[i].delta < 0 && p != 11) up_11++;
        }
    }
    const int ok = kept ? up_w > 0 && up_11 == 0 : (p <= 10 || up_w == 0) && up_11 > 0;
    printf("%s: cup winner %dth, play-outs %s: %s (winner up %d, the 11th up %d)\n", ok ? "ok" : "FAIL", p,
           step == 2 ? "played" : "not played", kept ? "keeps the place" : "the 11th plays", up_w, up_11);
    fails += !ok;
}

int main(void)
{
    place(11, 2, 1); place(15, 2, 1); place(2, 2, 0); place(10, 2, 0); place(20, 2, 0); place(19, 2, 0);
    place(18, 2, 0); place(16, 2, 0); place(16, 3, 1); place(17, 2, 0); place(17, 3, 1);
    printf("%s: the Coppa Italia Serie C winner's place, %d case(s) failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
