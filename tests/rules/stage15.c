/* swc/1.5 end to end (P5): a groups-then-knockout cup read from its .SWC alone -- the draw from GDRW / PROT
 * (sr_group_draw_p), the groups played (sr_rr_calendar, sr_rr_table on the spec's TIEB), the knockout's entrants from
 * KENT in seeding order (item, place, group A..H), each round paired by its own pairing and avoid (sr_ko_pairs_x) and
 * played by its legs and deciders. Over N seasons: every draw whole and keeping every association and protect pair
 * apart, no avoided pair ever met, one champion each season.
 *   stage15 SPEC.SWC SEASONS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swc_format.h"
#include "swosrules.h"

static uint32_t rs;
static uint32_t rnd(uint32_t n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }

static int play_tie(int legs, const uint8_t *deci)              /* 0: the first club through, 1: the second */
{
    sr_leg l[2];
    for (int k = 0; k < legs; k++) { l[k].home = legs == 1 ? 2 : (uint8_t)k; l[k].g[0] = (uint8_t)rnd(4); l[k].g[1] = (uint8_t)rnd(4); }
    sr_tie_extra x = { 0, { 0, 0 }, 0, { 0, 0 }, { 0, 0 } };
    int w = sr_ko_tie(l, legs, &x, deci, rs);
    if (w == SR_TIE_OPEN) { x.et_played = 1; x.et[0] = (uint8_t)rnd(2); x.et[1] = (uint8_t)rnd(2); w = sr_ko_tie(l, legs, &x, deci, rs); }
    if (w == SR_TIE_OPEN) { x.pens_played = 1; x.pens[0] = 4; x.pens[1] = (uint8_t)(rnd(2) ? 3 : 5); w = sr_ko_tie(l, legs, &x, deci, rs); }
    return w;
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: stage15 SPEC.SWC SEASONS\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    static uint8_t bin[1 << 20], mem[1 << 20];
    const uint32_t len = f ? (uint32_t)fread(bin, 1, sizeof bin, f) : 0;
    if (f) fclose(f);
    SwcArena a;
    SwcSpec s;
    swc_init(&a, mem, sizeof mem);
    if (swc_spec_open(&s, bin, len, &a) != SWC_OK) { printf("FAIL: %s: not a spec\n", argv[1]); return 1; }
    const int seasons = atoi(argv[2]);
    const SwcComp *c = &s.comps[s.ncomps - 1];                  /* (the cup: the spec's last competition) */
    const SwcStage *gs = &s.stages[c->first_stage], *ko = &s.stages[c->first_stage + 1];
    const SwcDraw *dr = NULL;
    for (uint32_t i = 0; i < s.ngdrw; i++) if (s.gdrw[i].stage == c->first_stage) dr = &s.gdrw[i];
    if (gs->type != SWC_ST_GROUPS || !dr || !c->nentr) { printf("FAIL: %s: no drawn groups stage of fixed clubs\n", argv[1]); return 1; }
    const int n = c->nentr, g = gs->groups;
    uint16_t cty[64];
    uint8_t as[64], prot[2 * SR_PROT_PAIRS];
    int nc = 0;
    for (int k = 0; k < n; k++) {                               /* associations: CLUB.country, dense */
        const uint16_t ct = s.clubs[s.entr[c->entr + k]].country;
        int j = 0;
        while (j < nc && cty[j] != ct) j++;
        if (j == nc) cty[nc++] = ct;
        as[k] = (uint8_t)j;
    }
    int np = 0;
    for (int i = 0; i < dr->nprotect; i++) {                    /* PROT: country pairs, onto the same values */
        int x = -1, y = -1;
        for (int j = 0; j < nc; j++) { if (cty[j] == s.prot[dr->protect + i].a) x = j; if (cty[j] == s.prot[dr->protect + i].b) y = j; }
        if (x >= 0 && y >= 0) { prot[2 * np] = (uint8_t)x; prot[2 * np + 1] = (uint8_t)y; np++; }
    }
    const int sep = dr->separate & SWC_SEP_ASSOCIATION;
    int bad = 0, champions = 0, together = 0, matches = 0;
    char why[200] = "";
    for (int se = 0; se < seasons; se++) {
        rs = 777u + (uint32_t)se * 2654435761u;
        uint8_t grp[64], size[64];
        if (sr_group_draw_p(n, g, sep ? as : NULL, prot, np, (uint32_t)se * 2246822519u + 3, grp) != g) {
            bad++; snprintf(why, sizeof why, "season %d: no draw", se); continue;
        }
        sr_group_sizes(n, g, size);
        int club[64][64], cn[64] = { 0 };
        for (int k = 0; k < n; k++) club[grp[k]][cn[grp[k]]++] = k;
        for (int k = 0; k < n; k++)
            for (int j = 0; j < k; j++) {
                if (grp[k] != grp[j]) continue;
                int b = sep && as[k] == as[j];
                for (int i = 0; i < np; i++)
                    b |= (prot[2 * i] == as[k] && prot[2 * i + 1] == as[j]) || (prot[2 * i] == as[j] && prot[2 * i + 1] == as[k]);
                if (b) { bad++; snprintf(why, sizeof why, "season %d: clubs %d and %d drawn together", se, j, k); }
            }
        for (int j = 0; j < g; j++) if (cn[j] != size[j]) { bad++; snprintf(why, sizeof why, "season %d: group %d size", se, j); }
        sr_srow t[64][64];
        static sr_match m[4096];
        for (int j = 0; j < g; j++) {                           /* the groups played */
            const int nm = sr_rr_calendar(cn[j], gs->legs, m, 4096);
            for (int i = 0; i < nm; i++) { m[i].played = 1; m[i].hg = (uint8_t)rnd(4); m[i].ag = (uint8_t)rnd(4); }
            matches += nm;
            if (sr_rr_table(cn[j], m, nm, 3, 1, 0, s.tieb + gs->tieb, (uint32_t)se, t[j]) != cn[j]) { bad++; snprintf(why, sizeof why, "season %d: table", se); }
        }
        int ent[64], eg[64], ne = 0;                            /* KENT: item, place, group A..H */
        for (uint32_t i = 0; i < s.nkent; i++) {
            const SwcKoEntrant *e = &s.kent[i];
            if (e->stage != c->first_stage + 1) continue;
            if (e->src != c->first_stage || e->kind != SWC_KE_PLACES) { printf("FAIL: KENT kind %u from stage %u: not this test's\n", e->kind, e->src); return 1; }
            for (int p = e->from; p <= e->to; p++)
                for (int j = 0; j < g; j++) {
                    if (e->group != SWC_KE_ALL && e->group != j) continue;
                    const int pl = p > 128 ? cn[j] - (p - 128) : p - 1;
                    ent[ne] = club[j][t[j][pl].club]; eg[ne] = j; ne++;
                }
        }
        if (ne != ko->teams) { bad++; snprintf(why, sizeof why, "season %d: %d entrants, the stage says %u", se, ne, ko->teams); continue; }
        int left = ne;
        for (int r = 0; r < ko->nrounds && left > 1; r++) {
            const SwcRound *rr = &s.rounds[ko->first_round + r];
            uint8_t av[64], aa[64], out[64];
            for (int k = 0; k < left; k++) { av[k] = (uint8_t)eg[k]; aa[k] = as[ent[k]]; }
            const int mode = rr->pairing == 1 ? SR_PAIR_SEEDED : SR_PAIR_DRAW;
            const int pr = sr_ko_pairs_x(mode, left, rr->avoid & SWC_AV_SAME_GROUP ? av : NULL,
                                         rr->avoid & SWC_AV_ASSOCIATION ? aa : NULL, prot, rr->avoid & SWC_AV_ASSOCIATION ? np : 0,
                                         (uint32_t)(se * 97 + r), out);
            if (pr != rr->ties) { bad++; snprintf(why, sizeof why, "season %d round %d: pairs %d", se, r + 1, pr); break; }
            int nx[64], ng[64], nn = 0;
            for (int k = 0; k < left; k += 2) {
                const int x = out[k], y = out[k + 1];
                int b = 0;
                if (rr->avoid & SWC_AV_SAME_GROUP) b |= eg[x] == eg[y];
                if (rr->avoid & SWC_AV_ASSOCIATION) {
                    b |= as[ent[x]] == as[ent[y]];
                    for (int i = 0; i < np; i++)
                        b |= (prot[2 * i] == as[ent[x]] && prot[2 * i + 1] == as[ent[y]]) || (prot[2 * i] == as[ent[y]] && prot[2 * i + 1] == as[ent[x]]);
                }
                if (mode == SR_PAIR_SEEDED) b |= x >= left / 2 || y < left / 2;
                if (b) { bad++; snprintf(why, sizeof why, "season %d round %d: an avoided pair met", se, r + 1); }
                const int w = play_tie(rr->legs, s.deci + rr->deci);
                if (w != 0 && w != 1) { bad++; snprintf(why, sizeof why, "season %d round %d: tie open", se, r + 1); }
                nx[nn] = ent[w ? y : x]; ng[nn] = eg[w ? y : x]; nn++;
                matches += rr->legs;
            }
            for (int k = 0; k < nn; k++) { ent[k] = nx[k]; eg[k] = ng[k]; }
            left = nn;
        }
        champions += left == 1;
        for (int k = 0; k < n; k++)                             /* (the chain's sign: two protect-chain ends together) */
            for (int j = 0; j < k; j++)
                if (grp[k] == grp[j] && as[k] != as[j] && np >= 2) {
                    int ck = 0, cj = 0;
                    for (int i = 0; i < np; i++) { ck |= prot[2 * i] == as[k] || prot[2 * i + 1] == as[k]; cj |= prot[2 * i] == as[j] || prot[2 * i + 1] == as[j]; }
                    together += ck && cj;
                }
    }
    if (bad || champions != seasons) { printf("FAIL: %s: %d bad (%s), %d champions of %d\n", argv[1], bad, why, champions, seasons); return 1; }
    printf("PASS: %s end to end from the .SWC: %d seasons of %d clubs drawn into %d groups (GDRW: separate %d, %d protect pairs; "
           "clubs of two protect countries no pair forbids together: %d times), groups played, KENT entrants, %u rounds "
           "with their avoid -- no constraint broken, one champion each (%d matches)\n",
           argv[1], seasons, n, g, sep, np, together, ko->nrounds, matches);
    return 0;
}
