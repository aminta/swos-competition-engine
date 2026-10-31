/* The library's season end (sr_italy, then sr_knockouts) over the same random final tables
 * (tables.h): each season's moves, results, stage titles and SEASON WINNERS page, as
 * knockouts_mod.c prints the Mod's. Arguments: SPEC [SEASONS [SHAPE]]. */
#include <stdio.h>
#include <stdlib.h>
#include "tables.h"
#include "swosrules.h"
static const uint8_t PROM[16] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t REL[16] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static sr_state st;
static sr_stage_bank bank;
static sr_promedio prom;                /* (the history: kept season to season, no seed) */
static sr_winners wn;
static void copy_row(sr_row *y, const struct trow *x)
{
    y->team = x->team; y->glob = x->glob; y->won = x->won; y->gf = x->gf; y->ga = x->ga; y->pts = x->pts;
}
int main(int argc, char **argv)
{
    unsigned N = argc > 2 ? (unsigned)atoi(argv[2]) : 20000;
    struct trow t[16][32]; int n[16];
    sr_league lg = { 14, PROM, REL, 1, 1, 2, 3, spec, 0, 0 };
    load_spec(argv[1]);
    if (argc > 3) { set_shape(argv[3]); lg.divisions = (uint8_t)ND; lg.italy = 0; }
    sr_reset(&st);
    for (unsigned it = 0; it < N; it++) {
        sr_season se = { 0 };
        make_tables(it, t, n);
        se.cup_winner = cup_winner(it, t, n);
        st.t.nd = (uint8_t)ND;
        for (int d = 0; d < ND; d++) {
            st.t.n[d] = (uint8_t)n[d];
            for (int k = 0; k < n[d]; k++) copy_row(&st.t.t[d][k], &t[d][k]);
        }
        {
            struct stages s;
            make_stages(it, t, n, &s);
            bank.used = 1; bank.fresh = (uint8_t)s.fresh;
            for (int k = 0; k < 4; k++) {
                bank.n[k] = (uint8_t)s.n[k];
                for (int i = 0; i < s.n[k]; i++) copy_row(&bank.r[k][i], &s.r[k][i]);
            }
            bank.kept_div = 0; bank.n_kept = (uint8_t)s.nkeep;
            for (int i = 0; i < s.nkeep; i++) {
                bank.kept[i].team = s.keep[i].team; bank.kept[i].glob = s.keep[i].glob;
                bank.kept[i].won = bank.kept[i].gf = bank.kept[i].ga = 0; bank.kept[i].pts = s.keep[i].pts;
            }
        }
        {                                       /* (the career club: a club of each division in turn) */
            const int d = (int)(it % ND);
            se.mgr = n[d] ? t[d][(it / ND) % n[d]].team : 0;
        }
        se.bank = &bank;
        se.promedio = &prom;
        se.winners = &wn;
        sr_italy(&st, &lg, &se);
        sr_knockouts(&st, &lg, &se);
        printf("%u", it);
        for (int i = 0; i < st.nmv; i++)
            printf(" %c%u:%d", i < st.dp_moves ? 'd' : 'g', st.mv[i].glob, st.mv[i].delta);
        for (int i = 0; i < st.gp.nres; i++)
            printf(" T%u:%u/%u", st.gp.res[i].kind, st.gp.res[i].glob[0], st.gp.res[i].glob[1]);
        for (int k = 0; k < 3; k++)
            if (st.gp.sres[k].kind) printf(" S%d:%u/%u", k, st.gp.sres[k].glob[0], st.gp.sres[k].glob[1]);
        if (se.mgr && wn.fresh) {               /* (the SEASON WINNERS page's last season) */
            printf(" W");
            for (int i = 0; i < wn.n; i++) printf("%02X.%u.%04X,", wn.e[i].type, wn.e[i].idx, wn.e[i].team);
            wn.fresh = 0;
        }
        printf("\n");
    }
    return 0;
}
