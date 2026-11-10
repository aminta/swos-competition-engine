/* England's non-league on real data (4.2, MC 09/11): the chain League Two -> National League -> N / S -> step 3
 * with the 2026-27 clubs (tests/rules/step3real_data.h: area bytes, north-south rank, strength), many summers.
 * nonleague.c checks the same rules on made-up clubs; this one runs them on the clubs the game will hold.
 *   each summer   League Two 2 down; the National League champion + play-off winner up, 4 down; N and S each
 *                 champion + play-off winner up, 4 down; step 3 (sr_lp_season) 8 up; a club down from N / S keeps
 *                 its own strength capped at the pool's strongest (iii), a club up takes its slot's strength
 *   step 3        the 8 clubs down placed by sr_pg_season on the level N C S I (chain): 22 each, every step-3
 *                 club within its own two choices, a second pass changes nothing
 *   N / S         sr_pg_realloc: 24 / 24, every strict club in its group unless it crossed, the flexible ones
 *                 split by rank north first, the crossed ones the strict clubs nearest the other group, a second
 *                 pass changes nothing
 *   identities    every club in exactly one place
 * Forced cases: a summer where the southern strict clubs overflow S (one crosses: the northernmost); a summer
 * where 3 clubs down want step 3's I then S while I and S send nobody up (S clubs whose 2nd choice is C make room:
 * everyone within their choices; 8 such clubs would not fit -- only 3 of S's 22 and 1 of the I-first N / S clubs have
 * C as a choice). Controls: a flexible pair swapped across N / S, a step-3 club outside its choices, a club twice ->
 * each caught.
 * The 2026-27 step-3 play-offs (NPL, SPL Central, SPL South, Isthmian Premier: en.wikipedia "2026-27 ..." tables,
 * "six teams play for one spot"): 2nd-7th, quarter-finals 4v7 5v6, 2nd-3rd into the semi-finals -- sr_lp_playoff's
 * shape.
 * Argument: CHAINS SEASONS (default 50 40). Prints PASS / FAIL lines; exit 1 on a FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"
#include "step3real_data.h"

enum { L2 = 24, NL = 24, NS = 24, PD = 22, PDIV = 4, NSL = 2 * NS, P3 = PDIV * PD };

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

static uint16_t str_of[S3R_N];
static int l2[L2], nl[NL], ns[2][NS], pool[PDIV][PD];

static uint32_t h32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; return x ^ (x >> 16); }
static int flexible(int c) { const int hi = S3R[c].ns >> 4; return S3R[c].rank != SR_RANK_NONE && (hi == 1 || hi == 2 || hi == 14); }
static int in_choices(int c, int g) { return g == (S3R[c].s3 & 15) || g == (S3R[c].s3 >> 4) - 1; }

static void start(void)
{
    int n[11] = { 0 };
    for (int c = 0; c < S3R_N; c++) {
        const int d = S3R[c].div;
        str_of[c] = S3R[c].str;
        if (d == 3) l2[n[d]++] = c;
        else if (d == 4) nl[n[d]++] = c;
        else if (d <= 6) ns[d - 5][n[d]++] = c;
        else pool[d - 7][n[d]++] = c;
    }
}

/* a light table of ids[0 .. n), best first, into ord; the play-off winner's id (po) */
static void table(uint32_t seed, int d, const int *ids, int n, int *ord, int *po)
{
    sr_lp_club c[SR_LP_CLUBS];
    sr_row t[SR_LP_CLUBS];
    for (int i = 0; i < n; i++) c[i].ident = (uint16_t)ids[i], c[i].strength = str_of[ids[i]];
    sr_lp_table(seed, d, c, n, t);
    for (int k = 0; k < n; k++) ord[k] = ids[t[k].team];
    if (po) *po = ids[t[sr_lp_playoff(seed, d, t, n, c)].team];
}

static int pos(const int *a, int n, int v) { for (int i = 0; i < n; i++) if (a[i] == v) return i; return -1; }

/* ---- the checks (each with a control) ---- */

/* N / S after the re-allocation: off[i] 1 N, 2 S for the 48 clubs lv[i]; NULL when right */
static const char *ns_bad(const int *lv, const int8_t *off, int crossed)
{
    int n[2] = { 0, 0 }, maxn = -1, mins = 256, cross[2] = { 0, 0 };
    for (int i = 0; i < NSL; i++) {
        const int c = lv[i], g = off[i] - 1;
        if (g < 0 || g > 1) return "a club outside N / S";
        n[g]++;
        if (flexible(c)) { if (g == 0 && S3R[c].rank > maxn) maxn = S3R[c].rank; if (g == 1 && S3R[c].rank < mins) mins = S3R[c].rank; }
        else if ((S3R[c].ns & 15) != g) cross[g]++;
    }
    if (n[0] != NS || n[1] != NS) return "N / S not 24 / 24";
    if (maxn > mins) return "the flexible clubs not split by rank (north first)";
    if (cross[0] + cross[1] != crossed) return "a strict club in the other group, not counted as crossed";
    for (int g = 0; g < 2; g++) {          /* the crossed: the strict clubs of the other side nearest this group */
        if (!cross[g]) continue;
        int far = g == 0 ? -1 : 256, near = g == 0 ? 256 : -1;   /* (into N: the crossed S clubs north of those staying) */
        for (int i = 0; i < NSL; i++) {
            const int c = lv[i], h = off[i] - 1;
            if (flexible(c) || (S3R[c].ns & 15) != 1 - g) continue;
            const int r = S3R[c].rank;
            if (h == g) { if (g == 0 ? r > far : r < far) far = r; }
            else if (g == 0 ? r < near : r > near) near = r;
        }
        if (g == 0 ? far > near : far < near) return "a crossed strict club not the nearest the other group";
    }
    return NULL;
}

/* step 3: off[i] 1..4 for the 88 clubs p[i]; NULL when right */
static const char *s3_bad(const int *p, const int8_t *off)
{
    int n[PDIV] = { 0 };
    for (int i = 0; i < P3; i++) {
        const int g = off[i] - 1;
        if (g < 0 || g >= PDIV) return "a club outside step 3";
        n[g]++;
        if (!in_choices(p[i], g)) return "a step-3 club outside its two choices";
    }
    for (int g = 0; g < PDIV; g++) if (n[g] != PD) return "a step-3 division not 22";
    return NULL;
}

static const char *ids_bad(void)
{
    static unsigned char seen[S3R_N];
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < L2; i++) seen[l2[i]]++;
    for (int i = 0; i < NL; i++) seen[nl[i]]++;
    for (int g = 0; g < 2; g++) for (int i = 0; i < NS; i++) seen[ns[g][i]]++;
    for (int d = 0; d < PDIV; d++) for (int i = 0; i < PD; i++) seen[pool[d][i]]++;
    for (int c = 0; c < S3R_N; c++) if (seen[c] != 1) return "a club lost or twice";
    return NULL;
}

/* ---- one summer ---- */

typedef struct {
    long summers, bad, crossed, cross_summers, chain_moved, idem;
    const char *why;
} stats;

enum { F_NONE, F_SOUTH_OVERFLOW, F_I_FULL };

static int cmp_rank_desc(const void *a, const void *b) { return S3R[*(const int *)b].rank - S3R[*(const int *)a].rank; }

static void summer(uint32_t seed, int force, stats *st)
{
    int ord[SR_LP_CLUBS], nl_up[2], nl_down[4], ns_up[4], ns_down[8], l2_down[2], p_up[8];
    const char *bad = NULL;
    table(seed, 30, l2, L2, ord, NULL);
    l2_down[0] = ord[L2 - 1]; l2_down[1] = ord[L2 - 2];
    table(seed, 31, nl, NL, ord, &nl_up[1]);
    nl_up[0] = ord[0];
    if (nl_up[1] == nl_up[0]) nl_up[1] = ord[1];
    for (int k = 0; k < 4; k++) nl_down[k] = ord[NL - 1 - k];
    for (int g = 0; g < 2; g++) {
        table(seed, 32 + g, ns[g], NS, ord, &ns_up[2 * g + 1]);
        ns_up[2 * g] = ord[0];
        for (int k = 0; k < 4; k++) ns_down[4 * g + k] = ord[NS - 1 - k];
    }
    {
        sr_lp_club pc[PDIV][SR_LP_CLUBS];
        const sr_lp_club *pcp[PDIV];
        int32_t pn[PDIV], up[2 * PDIV];
        for (int d = 0; d < PDIV; d++) {
            for (int i = 0; i < PD; i++) pc[d][i].ident = (uint16_t)pool[d][i], pc[d][i].strength = str_of[pool[d][i]];
            pcp[d] = pc[d]; pn[d] = PD;
        }
        if (sr_lp_season(seed, PDIV, pcp, pn, up) != 0) bad = "sr_lp_season refused";
        for (int k = 0; k < 8; k++) p_up[k] = pool[k / 2][up[k]];
    }
    if (force == F_SOUTH_OVERFLOW) {
        /* N / S: the 4 clubs down from the NL and the 8 up from step 3 all strict southern, the 8 down from N / S
           all flexible or northern -- S's strict clubs past 24 */
        int k = 0;
        for (int i = 0; i < NL && k < 4; i++)
            if (S3R[nl[i]].ns == 0xF1 && pos(nl_up, 2, nl[i]) < 0) nl_down[k++] = nl[i];
        for (int i = 0; i < NL && k < 4; i++)
            if (pos(nl_down, k, nl[i]) < 0 && pos(nl_up, 2, nl[i]) < 0) nl_down[k++] = nl[i];
        k = 0;
        for (int d = 0; d < PDIV; d++)
            for (int i = 0; i < PD && k < 8; i++) if (S3R[pool[d][i]].ns == 0xF1) p_up[k++] = pool[d][i];
        int m = 0;
        for (int g = 0; g < 2; g++)
            for (int i = 0; i < NS && m < 8; i++) {
                const int c = ns[g][i];
                if ((flexible(c) || (S3R[c].ns & 15) == 0) && pos(ns_up, 4, c) < 0) ns_down[m++] = c;
            }
    }
    if (force == F_I_FULL) {
        /* step 3: 3 of the 8 clubs down want I then S, the other 5 only N / C; step 3's 8 up from N and C only --
           I and S stay full, so the 3 fit within their choices only when S clubs whose 2nd choice is C make room */
        int m = 0;
        for (int g = 0; g < 2; g++)
            for (int i = 0; i < NS && m < 3; i++)
                if (S3R[ns[g][i]].s3 == 0x33 && pos(ns_up, 4, ns[g][i]) < 0) ns_down[m++] = ns[g][i];
        for (int g = 0; g < 2; g++)
            for (int i = 0; i < NS && m < 8; i++) {
                const int c = ns[g][i], a = S3R[c].s3 & 15, b = (S3R[c].s3 >> 4) - 1;
                if (a < 2 && b < 2 && pos(ns_down, m, c) < 0 && pos(ns_up, 4, c) < 0) ns_down[m++] = c;
            }
        for (int k = 0; k < 8; k++) p_up[k] = pool[k / 4][k % 4 + 3];
    }
    /* the moves: L2 <-> NL */
    for (int k = 0; k < 2; k++) {
        l2[pos(l2, L2, l2_down[k])] = nl_up[k];
        nl[pos(nl, NL, nl_up[k])] = l2_down[k];
    }
    /* NL <-> N / S: the level of 48 after the moves */
    int lv[NSL], nlv = 0;
    for (int g = 0; g < 2; g++)
        for (int i = 0; i < NS; i++)
            if (pos(ns_up, 4, ns[g][i]) < 0 && pos(ns_down, 8, ns[g][i]) < 0) lv[nlv++] = ns[g][i];
    for (int k = 0; k < 4; k++) { nl[pos(nl, NL, nl_down[k])] = ns_up[k]; lv[nlv++] = nl_down[k]; }
    /* N / S <-> step 3: sr_pg_season on [N / S as one division of 48][N C S I, chain] */
    {
        static sr_pg_club cl[NSL + 8 + P3];
        static int who[NSL + 8 + P3];
        static int8_t old_off[NSL + 8 + P3], new_off[NSL + 8 + P3], again[NSL + 8 + P3];
        static const sr_pg_level lv3[2] = { { 0, 1, { NSL }, 0 }, { 1, 4, { PD, PD, PD, PD }, 1 } };
        int n = 0;
        for (int i = 0; i < nlv; i++) { who[n] = lv[i]; old_off[n] = new_off[n] = 0; n++; }
        for (int k = 0; k < 8; k++) { who[n] = ns_down[k]; old_off[n] = 0; new_off[n] = 1; n++; }
        for (int d = 0; d < PDIV; d++)
            for (int i = 0; i < PD; i++) {
                const int c = pool[d][i], up = pos(p_up, 8, c) >= 0;
                who[n] = c; old_off[n] = (int8_t)(1 + d); new_off[n] = (int8_t)(up ? 0 : 1 + d); n++;
            }
        for (int i = 0; i < n; i++) {
            cl[i].base = 0; memset(cl[i].area, 0xFF, sizeof cl[i].area);
            cl[i].area[0] = S3R[who[i]].s3;
        }
        const sr_pg_league pg = { 2, lv3, (uint16_t)n, cl };
        if (sr_pg_season(&pg, old_off, new_off, NULL) != 0) bad = "sr_pg_season refused";
        int p[P3], np = 0, cnt[PDIV] = { 0 };
        int8_t po[P3];
        for (int i = 0; i < n; i++) {
            if (new_off[i] == 0) continue;
            if (np == P3) { bad = "step 3 over 88"; break; }
            if (old_off[i] && old_off[i] != new_off[i]) st->chain_moved++;
            p[np] = who[i]; po[np++] = new_off[i];
        }
        if (!bad && np != P3) bad = "step 3 not 88";
        if (!bad && (bad = s3_bad(p, po)) == NULL) {
            memcpy(again, new_off, (size_t)n);          /* a second pass: the moves done, nothing changes */
            for (int i = 0; i < n; i++) old_off[i] = new_off[i];
            if (sr_pg_season(&pg, old_off, again, NULL) != 0 || memcmp(again, new_off, (size_t)n)) st->idem++;
            for (int i = 0; i < np; i++) { const int g = po[i] - 1; pool[g][cnt[g]++] = p[i]; }
        }
        /* strengths: down keeps its own, capped at the pool's strongest (iii); up takes its slot's */
        uint16_t pl[P3];
        int npl = 0;
        for (int i = 0; i < np; i++) if (pos(ns_down, 8, p[i]) < 0) pl[npl++] = str_of[p[i]];
        for (int k = 0; k < 8; k++) {
            const uint16_t slot = str_of[ns_down[k]];
            str_of[ns_down[k]] = (uint16_t)sr_lp_newcomer_strength(slot, pl, npl);
            str_of[p_up[k]] = slot;
        }
    }
    for (int k = 0; k < 8; k++) lv[nlv++] = p_up[k];
    if (!bad && nlv != NSL) bad = "N / S not 48";
    /* the summer re-allocation of N / S */
    if (!bad) {
        static sr_pg_club cl[NL + NSL];
        static uint8_t rk[NL + NSL];
        static uint16_t id[NL + NSL];
        static int8_t off[NL + NSL], off2[NL + NSL];
        static const sr_pg_level lv2[2] = { { 0, 1, { NL }, 0 }, { 1, 2, { NS, NS }, 0 } };
        qsort(lv, NSL, sizeof lv[0], cmp_rank_desc);    /* (any slots: the same clubs give the same split) */
        for (int i = 0; i < NL + NSL; i++) {
            const int c = i < NL ? nl[i] : lv[i - NL];
            cl[i].base = 0; memset(cl[i].area, 0xFF, sizeof cl[i].area);
            if (i >= NL) cl[i].area[0] = S3R[c].ns;
            rk[i] = S3R[c].rank; id[i] = (uint16_t)c; off[i] = (int8_t)(i < NL ? 0 : 1 + (i & 1));
        }
        const sr_pg_league pg = { 2, lv2, NL + NSL, cl };
        const int rc = sr_pg_realloc(&pg, 1, rk, id, off);
        if (rc < 0) bad = "sr_pg_realloc refused";
        else {
            st->crossed += rc; st->cross_summers += rc > 0;
            if (force == F_SOUTH_OVERFLOW && rc == 0) bad = "forced: S's strict clubs past 24 and none crossed";
            if (!bad) bad = ns_bad(lv, off + NL, rc);
            memcpy(off2, off, sizeof off);
            if (sr_pg_realloc(&pg, 1, rk, id, off2) != rc || memcmp(off, off2, sizeof off)) st->idem++;
            int k[2] = { 0, 0 };
            if (!bad) for (int i = 0; i < NSL; i++) { const int g = off[NL + i] - 1; ns[g][k[g]++] = lv[i]; }
        }
    }
    if (!bad) bad = ids_bad();
    st->summers++;
    if (bad) { st->bad++; if (!st->why) st->why = bad; }
}

int main(int argc, char **argv)
{
    const int chains = argc > 1 ? atoi(argv[1]) : 50, seasons = argc > 2 ? atoi(argv[2]) : 40;
    char m[220];
    stats st = { 0 };
    for (int ch = 0; ch < chains; ch++) {
        start();
        for (int s = 0; s < seasons; s++) summer(h32((uint32_t)ch * 1000003u + (uint32_t)s), F_NONE, &st);
    }
    snprintf(m, sizeof m, "the 2026-27 chain, %ld summers: identities, sizes, step 3 within choices (chain: %ld clubs "
             "moved within theirs), N / S by ground (%ld crossed in %ld summers)%s%s", st.summers, st.chain_moved,
             st.crossed, st.cross_summers, st.why ? " -- " : "", st.why ? st.why : "");
    check(st.bad == 0, m);
    check(st.idem == 0, "a second pass of the step-3 placement and of the N / S re-allocation changes nothing");

    stats f = { 0 };
    start();
    summer(h32(7), F_SOUTH_OVERFLOW, &f);
    snprintf(m, sizeof m, "forced: S's strict clubs past 24 -- %ld crossed, the strict clubs nearest N%s%s", f.crossed,
             f.why ? " -- " : "", f.why ? f.why : "");
    check(f.bad == 0 && f.crossed > 0, m);
    stats g = { 0 };
    start();
    summer(h32(11), F_I_FULL, &g);
    snprintf(m, sizeof m, "forced: 3 down want I then S, I and S send nobody up -- every step-3 club within its choices "
             "(%ld moved by the chain)%s%s", g.chain_moved, g.why ? " -- " : "", g.why ? g.why : "");
    check(g.bad == 0 && g.chain_moved > 0, m);

    /* controls: each broken state caught */
    {
        int lv[NSL], fn = -1, fs = -1;
        int8_t off[NSL];
        start();
        for (int g2 = 0; g2 < 2; g2++) for (int i = 0; i < NS; i++) { lv[g2 * NS + i] = ns[g2][i]; off[g2 * NS + i] = (int8_t)(1 + g2); }
        const char *before = ns_bad(lv, off, 0);
        for (int i = 0; i < NSL; i++) if (flexible(lv[i])) { if (off[i] == 1 && (fn < 0 || S3R[lv[i]].rank < S3R[lv[fn]].rank)) fn = i; }
        for (int i = 0; i < NSL; i++) if (flexible(lv[i]) && off[i] == 2) { fs = i; break; }
        int caught = fn >= 0 && fs >= 0;
        if (caught) { off[fn] = 2; off[fs] = 1; caught = ns_bad(lv, off, 0) != NULL; }
        snprintf(m, sizeof m, "control: the 2026-27 N / S as listed %s; a flexible pair swapped across -> caught",
                 before ? before : "pass");
        check(!before && caught, m);
    }
    {
        int p[P3];
        int8_t po[P3];
        start();
        for (int d = 0; d < PDIV; d++) for (int i = 0; i < PD; i++) p[d * PD + i] = pool[d][i], po[d * PD + i] = (int8_t)(1 + d);
        const char *before = s3_bad(p, po);
        for (int i = 0; i < P3; i++)
            if (!in_choices(p[i], 3)) { po[i] = 4; break; }
        check(!before && s3_bad(p, po) != NULL, "control: the 2026-27 step 3 as listed passes; a club put outside its choices -> caught");
        start();
        nl[0] = nl[1];
        check(ids_bad() != NULL, "control: a club twice -> caught");
    }
    return fails ? 1 : 0;
}
