/* The mass simulation of Italy's chain Serie D <-> Eccellenza (42-ecclp; the Mod's notes/CONVENTIONS.md 17b):
 *   Serie D (9 x 18)        light tables (the game's spread, standing for its played matches): the last 4 of each
 *                           group down (36); no moves with Serie C here (the chain's top is closed)
 *   Eccellenza (28 groups)  sr_lp_season_cross: the 28 champions, the regional play-offs, the national play-off (7),
 *                           the cup rule (1): 36 up
 *   summer                  the 36 down take the 36 promoted clubs' Eccellenza places, nearest by ground (both sorted
 *                           north to south); a promoted club takes a relegated club's record (its strength), the
 *                           relegated one its own strength capped at the pool's strongest (rule iii); Serie D
 *                           re-allocated by ground (north to south, 9 groups of 18)
 *   invariants (every summer)  every club in exactly one place; Serie D 9 x 18, each Eccellenza group its real size;
 *                           36 distinct up = 28 champions (each its table's top) + 7 + 1; 36 distinct down; strengths
 *                           within 0 .. 375
 * Usage: italysim [CHAINS [SEASONS]] (default 1000 100: 10^5 summers). Exit 1 on any broken invariant.
 * Controls: ITALYSIM_BREAK=drop (a club lost in the summer), twice (a club up twice), 35 (one up too few) must FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"
#include "ecclp_italy.h"

int32_t sr_lp_table_game(uint32_t seed, int32_t d, const sr_lp_club *c, int32_t n, sr_row *out);

enum { DG = 9, DS = 18, ND = DG * DS, NP = 465, NCLUB = ND + NP };

static uint16_t str_of[NCLUB];
static uint8_t lat_of[NCLUB];
static int dgrp[DG][DS], pgrp[IT_GROUPS][SR_LP_CLUBS], down_season[NCLUB];

static int brk;                 /* the controls: 1 drop, 2 twice, 3 thirty-five */

static uint32_t h32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; return x ^ (x >> 16); }

typedef struct {
    long summers, bad, why[6], bounce_n, bounce_up, how[5], rank_sum, rank_n, top4, nat_place[6], cup_place[20];
    long dec_sum[2][10], dec_cnt[10];
} stats;
static const char *why_txt[6] = { "a club lost or doubled", "a level's size", "not 36 distinct up / 28 champions",
                                  "not 36 distinct down", "a strength out of range", "the pool season refused" };

static int cmp_lat(const void *a, const void *b) { return (int)lat_of[*(const int *)a] - (int)lat_of[*(const int *)b]; }
static int cmp_u16_desc(const void *a, const void *b) { return (int)*(const uint16_t *)b - (int)*(const uint16_t *)a; }

static void chain_run(uint32_t chain, int seasons, stats *st)
{
    int id = 0;
    for (int g = 0; g < DG; g++)
        for (int i = 0; i < DS; i++, id++) {
            const uint32_t h = h32(chain * 7919u + (uint32_t)id * 104729u);
            lat_of[id] = (uint8_t)(h % 255u); str_of[id] = (uint16_t)(110 + (h >> 12) % 141u); dgrp[g][i] = id;
        }
    {   /* D by ground at the start too */
        int all[ND];
        memcpy(all, dgrp, sizeof all);
        qsort(all, ND, sizeof all[0], cmp_lat);
        memcpy(dgrp, all, sizeof all);
    }
    for (int g = 0; g < IT_GROUPS; g++)        /* the groups north to south, as IT_G lists them */
        for (int i = 0; i < IT_G[g].size; i++, id++) {
            const uint32_t h = h32(chain * 7919u + (uint32_t)id * 104729u);
            lat_of[id] = (uint8_t)(g * 9 + (int)(h % 9u)); str_of[id] = (uint16_t)(20 + (h >> 12) % 201u); pgrp[g][i] = id;
        }
    for (int i = 0; i < NCLUB; i++) down_season[i] = -100;
    sr_lp_shape sh;
    sr_lp_rpo rpo[IT_GROUPS];
    uint8_t urn[IT_GROUPS];
    it_shape(&sh, rpo, urn, NULL, 0);
    for (int s = 0; s < seasons; s++) {
        const uint32_t seed = h32(chain * 1000003u + (uint32_t)s);
        unsigned bad = 0;
        int down[36], nd = 0, upc[36];
        /* Serie D */
        for (int g = 0; g < DG; g++) {
            sr_lp_club c[DS];
            sr_row t[DS];
            for (int i = 0; i < DS; i++) c[i].ident = (uint16_t)dgrp[g][i], c[i].strength = str_of[dgrp[g][i]];
            sr_lp_table_game(seed, 40 + g, c, DS, t);
            for (int k = 0; k < 4; k++) down[nd++] = dgrp[g][t[DS - 1 - k].team];
        }
        /* Eccellenza */
        sr_lp_club pc[IT_GROUPS][SR_LP_CLUBS];
        const sr_lp_club *pp[IT_GROUPS];
        int32_t pn[IT_GROUPS];
        sr_lp_up up[48];
        for (int g = 0; g < IT_GROUPS; g++) {
            for (int i = 0; i < IT_G[g].size; i++) pc[g][i].ident = (uint16_t)pgrp[g][i], pc[g][i].strength = str_of[pgrp[g][i]];
            pp[g] = pc[g]; pn[g] = IT_G[g].size;
        }
        int nup = sr_lp_season_cross(seed, IT_GROUPS, pp, pn, &sh, up);
        if (brk == 2 && s == 1) up[IT_GROUPS + 1] = up[IT_GROUPS];
        if (brk == 3 && s == 1) nup--;
        if (nup != IT_UP) { const int w = nup < 0 ? 5 : 2; st->bad++; st->why[w]++; st->summers++; continue; }
        {
            static unsigned char seen[NCLUB];
            memset(seen, 0, sizeof seen);
            for (int k = 0; k < nup; k++) { upc[k] = pgrp[up[k].div][up[k].idx]; if (seen[upc[k]]++) bad |= 1u << 2; }
            for (int g = 0; g < IT_GROUPS; g++) {
                sr_row t[SR_LP_CLUBS];
                sr_lp_table(seed, g, pc[g], pn[g], t);
                if (up[g].how != SR_LP_UP_CHAMPION || up[g].div != g || up[g].idx != t[0].team) bad |= 1u << 2;
                for (int k = IT_GROUPS; k < nup; k++) {
                    if (up[k].div != g) continue;
                    int pl = 0;
                    while (pl < pn[g] && t[pl].team != up[k].idx) pl++;
                    if (up[k].how == SR_LP_UP_CROSS) st->nat_place[pl < 5 ? pl : 5]++;
                    else st->cup_place[pl < 19 ? pl : 19]++;
                }
            }
            memset(seen, 0, sizeof seen);
            for (int k = 0; k < nd; k++) if (seen[down[k]]++) bad |= 1u << 3;
        }
        for (int k = 0; k < nup; k++) {
            st->how[up[k].how]++;
            int r = 0;
            for (int i = 0; i < pn[up[k].div]; i++) r += pc[up[k].div][i].strength > str_of[upc[k]];
            st->rank_sum += r; st->rank_n++; st->top4 += r < 4;
            if (down_season[upc[k]] == s - 1) st->bounce_up++;
        }
        /* the summer: the 36 down into the promoted clubs' places, nearest by ground */
        {
            int dn[36], uc[36];
            uint16_t pl[NP];
            int np = 0;
            memcpy(dn, down, sizeof dn); memcpy(uc, upc, sizeof uc);
            qsort(dn, 36, sizeof dn[0], cmp_lat); qsort(uc, 36, sizeof uc[0], cmp_lat);
            for (int g = 0; g < IT_GROUPS; g++) for (int i = 0; i < IT_G[g].size; i++) pl[np++] = str_of[pgrp[g][i]];
            qsort(pl, (size_t)np, sizeof pl[0], cmp_u16_desc);
            for (int k = 0; k < 36; k++) {
                int *slot = NULL;
                for (int g = 0; g < IT_GROUPS && !slot; g++) for (int i = 0; i < IT_G[g].size; i++) if (pgrp[g][i] == uc[k]) slot = &pgrp[g][i];
                if (!slot) { bad |= 1u << 2; continue; }      /* (a club up twice: its place already taken) */
                const uint16_t slot_str = str_of[dn[k]];
                str_of[dn[k]] = (uint16_t)sr_lp_newcomer_strength(slot_str, pl, np);   /* (iii) */
                str_of[uc[k]] = slot_str;
                *slot = dn[k];
                down_season[dn[k]] = s;
                st->bounce_n++;
            }
            /* Serie D: out the 36 down, in the 36 up, re-allocated by ground */
            int all[ND], na = 0;
            for (int g = 0; g < DG; g++)
                for (int i = 0; i < DS; i++) {
                    int out = 0;
                    for (int k = 0; k < 36; k++) out |= dgrp[g][i] == down[k];
                    if (!out) all[na++] = dgrp[g][i];
                }
            for (int k = 0; k < 36; k++) if (na < ND) all[na++] = upc[k]; else bad |= 1u << 1;
            if (na != ND) bad |= 1u << 1;
            else { qsort(all, ND, sizeof all[0], cmp_lat); memcpy(dgrp, all, sizeof all); }
        }
        if (brk == 1 && s == 1) pgrp[3][2] = pgrp[3][1];
        /* every club once, the strengths */
        {
            static unsigned char seen[NCLUB];
            memset(seen, 0, sizeof seen);
            for (int g = 0; g < DG; g++) for (int i = 0; i < DS; i++) seen[dgrp[g][i]]++;
            for (int g = 0; g < IT_GROUPS; g++) for (int i = 0; i < IT_G[g].size; i++) seen[pgrp[g][i]]++;
            for (int i = 0; i < NCLUB; i++) { if (seen[i] != 1) bad |= 1u; if (str_of[i] > 375) bad |= 1u << 4; }
        }
        {
            const int dz = s * 10 / seasons;
            long a = 0, b = 0;
            for (int g = 0; g < DG; g++) for (int i = 0; i < DS; i++) a += str_of[dgrp[g][i]];
            for (int g = 0; g < IT_GROUPS; g++) for (int i = 0; i < IT_G[g].size; i++) b += str_of[pgrp[g][i]];
            st->dec_sum[0][dz] += a / ND; st->dec_sum[1][dz] += b / NP; st->dec_cnt[dz]++;
        }
        st->summers++;
        if (bad) { st->bad++; for (int k = 0; k < 6; k++) if (bad & (1u << k)) st->why[k]++; }
    }
}

int main(int argc, char **argv)
{
    const int chains = argc > 1 ? atoi(argv[1]) : 1000, seasons = argc > 2 ? atoi(argv[2]) : 100;
    static stats st;
    const char *b = getenv("ITALYSIM_BREAK");
    brk = !b ? 0 : !strcmp(b, "drop") ? 1 : !strcmp(b, "twice") ? 2 : !strcmp(b, "35") ? 3 : 0;
    for (int c = 0; c < chains; c++) chain_run((uint32_t)c, seasons, &st);
    printf("italysim: %d chains x %d seasons = %ld summers (Serie D 9 x 18 <-> Eccellenza 28 groups, 465 clubs; "
           "SR_LP_SPREAD %d)\n", chains, seasons, st.summers, SR_LP_SPREAD);
    printf("  invariants: %ld summers broken\n", st.bad);
    for (int k = 0; k < 6; k++) if (st.why[k]) printf("    %s: %ld\n", why_txt[k], st.why[k]);
    printf("  up by: champion %ld, national play-off %ld, cup rule %ld, override %ld (per summer %.2f / %.2f / %.2f)\n",
           st.how[1], st.how[2], st.how[3], st.how[4], (double)st.how[1] / st.summers, (double)st.how[2] / st.summers,
           (double)(st.how[3] + st.how[4]) / st.summers);
    printf("  straight back up (down from D last summer, up now): %.1f%% of %ld\n",
           100.0 * st.bounce_up / (st.bounce_n ? st.bounce_n : 1), st.bounce_n);
    printf("  the promoted clubs' strength rank in their group: mean %.2f (0 = strongest), top 4: %.1f%%\n",
           (double)st.rank_sum / st.rank_n, 100.0 * st.top4 / st.rank_n);
    long nt = 0;
    for (int k = 0; k < 6; k++) nt += st.nat_place[k];
    printf("  national play-off winners by group place 1st..6th+: ");
    for (int k = 0; k < 6; k++) printf("%.1f%% ", 100.0 * st.nat_place[k] / (nt ? nt : 1));
    long ct = 0, cm = 0;
    for (int k = 0; k < 20; k++) ct += st.cup_place[k], cm += st.cup_place[k] * (k + 1);
    printf("\n  the cup rule's club: mean group place %.2f\n", (double)cm / (ct ? ct : 1));
    printf("  drift (mean strength by tenth of the chain) D / Eccellenza:\n   ");
    for (int z = 0; z < 10; z++) printf(" %ld/%ld", st.dec_sum[0][z] / (st.dec_cnt[z] ? st.dec_cnt[z] : 1),
                                         st.dec_sum[1][z] / (st.dec_cnt[z] ? st.dec_cnt[z] : 1));
    printf("\n%s italysim\n", st.bad ? "FAIL" : "PASS");
    return st.bad ? 1 : 0;
}
