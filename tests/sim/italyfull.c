/* The mass simulation of Italy's whole chain (42-italysim-full; MC, internal/handoff/mc-ecclp-answers-2.md):
 *   Serie A, B (20 each)    light tables; their moves are the library's (dplay's Serie B play-off / play-out); they are
 *                           not reported: B is closed in this item, a fixed count down (3 + the play-out's loser)
 *   Serie C (3 x 20)        light tables; the champions + dplay's Serie C play-off up (4), the 20th + the play-outs'
 *                           losers down (9: the spec's cX_playout, gap 8)
 *   Serie D (9 x 18)        light tables; the champions up (9), the 17th, 18th + the play-outs' losers down (36: the
 *                           spec's dX_playout, gap 8); the play-offs (placement) and the Poule Scudetto played
 *   Eccellenza (28 groups)  sr_lp_season_cross as in italysim (tests/sim/ecclp_italy.h): 36 up
 * The season end is the library's, whole: sr_italy, sr_knockouts over the 4.2 spec (tests/sim/italyfull_data.h,
 * tests/sim/mkitalyfull.py: the pack's knockouts with every Serie D group at 18), sr_final_moves with the B-team rules
 * (the Mod's SEC_TAB for 'ita': up to Serie B, play-offs allowed, out of the league from Serie D), then sr_pg_season
 * places every move in its group by the clubs' real areas; the 36 down from D take the 36 promoted clubs' Eccellenza
 * places (nearest by ground, as italysim), each promoted club the relegated one's slot and record (entering Serie D by
 * its group's area).
 * The light tables of A .. D use the game's spread (sr_pool_game.o, standing for its played matches); SPREAD=n sets the
 * Eccellenza's, as italysim.
 * Invariants (every summer): every club in exactly one place; every division its size; up = down at A/B, B/C, C/D and
 * D/Eccellenza (36); no B team in its first team's level or above, nor above Serie B; strengths 0 .. 375; the library
 * refusing nothing.
 * B teams: the 4.2 rules by default (sr_league.second_floor = Serie D; MC internal/handoff/mc-bteams-gaps-20261109.md
 * F2-F4): order-free stays, the final guard, out of the league below the floor (each exit: one more up from the pool,
 * up.extra's rule), a pool's B team whose first team plays Serie D or lower barred from going up -- any B-team
 * violation fails. BFLOOR=0 runs the Mod 4.1's rules (= sec_post) and counts their gaps instead (reported to MC
 * 2026-11-09). The Coppa Italia Serie C winner is drawn from all of Serie C:
 * relegated or in a play-out it takes no place (F1, the Mod's dplay.c 353984f).
 * GAME_SPREAD=n: the light tables of A .. D with sr_lp_table_sp at spread n instead of the game's (40). No value brings
 * the play-out gaps to the real ones (80: C 3.9, D 3.6 points v 5.8, 4.8): they come from the in-group strength
 * dispersion the light tables don't model -- a recorded model limit (MC 2026-11-09), 40 kept; the Mod plays A .. D.
 * Two team files (42-s7reg, sr_reg): file 102 Serie A + B + C, file 144 Serie D + 36 pool rows; every summer the
 * registry's swap list (the exe's mode: only clubs changing file move), replayed by records as the exe would, must
 * equal the registry; every club in one row, its division byte = its table division, a club staying in its file
 * keeps its record, the clubs that changed file = C/D's down + up, files <= 256, the B teams resolved through the
 * registry. Controls regdrop (a swap dropped), regdup (a slot twice), regdiv (a wrong division byte) must FAIL.
 * Real (tests/sim/itwiki_rates.py, it.wikipedia 2014-15 .. 2025-26): play-out ties the 8-point gap skips, Serie C
 * 31.6% (12 of 38), Serie D 22.1% (30 of 136); straight back up C/D 12.3% (9 of 73).
 * Usage: [SPREAD=n] [GAME_SPREAD=n] [BFLOOR=n] italyfull [CHAINS [SEASONS]] (default 1000 100). Exit 1 on any broken invariant.
 * Controls: ITALYFULL_BREAK=drop (a club lost), bteam (a B team into its first team's division), updown (one club too
 * many up from Serie D) must FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"
#include "ecclp_italy.h"
#include "italyfull_data.h"

int32_t sr_lp_table_game(uint32_t seed, int32_t d, const sr_lp_club *c, int32_t n, sr_row *out);

enum { NF = ITF_NCLUB, NP = 465, NI = NF + NP, LEVELS = 4, FIRST_C = 2, FIRST_D = 5, DG = 9, DS = 18 };
static const uint8_t LEVEL_FIRST[LEVELS] = { 0, 1, FIRST_C, FIRST_D };
static int level_of(int d) { return d >= FIRST_D ? 3 : d >= FIRST_C ? 2 : d; }

static uint16_t str_of[NI];
static uint8_t area_c[NI], area_d[NI], lat_of[NI];
static int first_of[NI];                    /* a B team's first team (ident), -1 */
static int slot_ident[NF], slot_div[NF];
static int pgrp[IT_GROUPS][SR_LP_CLUBS];
static int down_cd[NI], down_de[NI];
static uint8_t viol[NI];                    /* a B team in / above its first team's level last summer */        /* the summer a club went down C -> D, D -> Eccellenza */
static uint8_t ecc_letter[IT_GROUPS] = { 0, 0, 0, 1, 1, 0, 2, 2, 2, 2, 3, 3, 4, 4, 4, 5, 6, 6, 5, 6, 6, 7, 5, 7, 7, 8,
                                         8, 8 };   /* each Eccellenza group's Serie D group (test data, by region) */
static uint8_t letter_c[DG];                /* the Serie C area most real clubs of each Serie D group have */

static int brk;
static int spread = SR_LP_SPREAD;
#define FLOOR_LEVEL (LEVELS - 1)
static int floor_lv = LEVELS - 1;           /* the B teams' floor (sr_league.second_floor): Serie D; BFLOOR=0 the Mod 4.1's */
static int game_spread = -1;              /* A .. D: GAME_SPREAD=n, else the game copy (sr_pool_game.o, 40) */
static sr_state S;
/* 4.2's two team files (42-s7reg): file 102 Serie A + B + C, file 144 Serie D + 36 pool rows (the stand-ins: clubs of the
 * Eccellenza on the file) -- the registry over the slots' moves; rec_at the exe's side, by the swap lists */
enum { FILE_A = 102, FILE_B = 144, POOL_ROWS = 36 };
static sr_registry REG;
static uint16_t rec_at[2][256];
static sr_reg_swap SW[1024];
static sr_reg_work WK;

static uint32_t h32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; return x ^ (x >> 16); }

enum { W_LOST, W_SIZE, W_UPDOWN, W_BTEAM, W_STR, W_REFUSED, W_REG, W_N };
static const char *why_txt[W_N] = { "a club lost or doubled", "a division's size", "up != down at a boundary",
                                    "a B team in or above its first team's level (or above Serie B)",
                                    "a strength out of range", "the library refused a season",
                                    "the two team files (registry, swaps, rows)" };
typedef struct {
    long summers, bad, why[W_N];
    long bound[LEVELS][2];                  /* down from level l, up from level l + 1 (l = 3: D down, the pool's up) */
    long cd_n, cd_back, de_n, de_back;      /* relegated C -> D / D -> Ecc, straight back up the next summer */
    long rank_sum[2], rank_n[2], top4[2];   /* D's promoted, Eccellenza's promoted: strength rank in their group */
    long po_c[2], po_d[2], gap_c, gap_d;                  /* play-out ties: played, skipped by the gap (C, D) */

    long barred, bt_ko, gap_same, gap_below, gap_two, gap_carry, surplus, surplus_n, exits_d, exits_above;                    /* B-team rule: moves it changed (want differs from places) */
    long dec_sum[LEVELS + 1][10], dec_cnt[10];
    long reg_swaps, reg_across, reg_off, reg_crossed, reg_max;   /* the two files: swaps, across files, with the
                                                                    off-file pool; clubs that changed file; most swaps */
} stats;

static void reg_init(void)
{
    static uint8_t div[NI];
    sr_reg_file f[2];
    memset(f, 0, sizeof f);
    f[0].file = FILE_A; f[0].ndiv = FIRST_D;
    for (int d = 0; d < FIRST_D; d++) f[0].div[d] = (uint8_t)d, f[0].size[d] = ITF_SIZE[d];
    f[1].file = FILE_B; f[1].ndiv = DG; f[1].pool = POOL_ROWS;
    for (int g = 0; g < DG; g++) f[1].div[g] = (uint8_t)(FIRST_D + g), f[1].size[g] = ITF_SIZE[FIRST_D + g];
    for (int i = 0; i < NI; i++) div[i] = i < NF ? ITF_CLUB[i].div : i < NF + POOL_ROWS ? SR_REG_POOL : SR_REG_OUT;
    if (sr_reg_build(&REG, 2, f, NI, div)) { printf("italyfull: the two-file layout refused\n"); exit(2); }
    for (int i = 0; i < 2; i++) for (int o = 0; o < 256; o++) rec_at[i][o] = REG.at[i][o];
}

/* the summer's places in the two files: every club of A .. D in its new division, the pool rows' clubs still in the
 * Eccellenza kept, the rows freed by the ones promoted given to this summer's relegated (best placed first); the
 * swap list applied to rec_at as the exe would. The broken rules (0 none). */
static unsigned reg_summer(int s, const uint16_t *order, int norder, const int *down, const int *up, stats *st)
{
    static uint8_t nd[NI], od[NI];
    static uint16_t oldw[NI];
    unsigned bad = 0;
    int free_rows = 0;
    for (int i = 0; i < NI; i++) nd[i] = SR_REG_OUT, od[i] = (uint8_t)sr_reg_div(&REG, i), oldw[i] = REG.word[i];
    for (int i = 0; i < NF; i++) nd[slot_ident[i]] = (uint8_t)slot_div[i];
    for (int i = 0; i < NI; i++)
        if (od[i] == SR_REG_POOL) { if (nd[i] == SR_REG_OUT) nd[i] = SR_REG_POOL; else free_rows++; }
    for (int k = 0; k < norder && free_rows; k++)
        if (nd[order[k]] == SR_REG_OUT && down_de[order[k]] == s) nd[order[k]] = SR_REG_POOL, free_rows--;
    for (int i = 0; i < NI && free_rows; i++) if (nd[i] == SR_REG_OUT && od[i] == SR_REG_OUT) nd[i] = SR_REG_POOL, free_rows--;
    const int n = sr_reg_season(&REG, nd, order, norder, 0, &WK, SW, (int)(sizeof SW / sizeof SW[0]));
    if (n < 0) return 1u << W_REG;
    for (int k = 0; k < n; k++) {               /* (the exe's side) */
        if (brk == 4 && s == 1 && k == 0) continue;
        const int fa = (SW[k].a & 0xFF) == FILE_B, oa = SW[k].a >> 8;
        if (SW[k].b == SR_REG_NONE) { rec_at[fa][oa] = SW[k].on; st->reg_off++; continue; }
        const int fb = (SW[k].b & 0xFF) == FILE_B, ob = SW[k].b >> 8;
        const uint16_t x = rec_at[fa][oa];
        rec_at[fa][oa] = rec_at[fb][ob]; rec_at[fb][ob] = x;
        st->reg_across += fa != fb;
    }
    st->reg_swaps += n;
    if (n > st->reg_max) st->reg_max = n;
    if (brk == 5 && s == 1) REG.at[1][0] = REG.at[1][1];                /* (a slot twice) */
    if (brk == 6 && s == 1) REG.rdiv[1][0] = (uint8_t)(REG.rdiv[1][0] == FIRST_D ? FIRST_D + 1 : FIRST_D);   /* (a wrong division byte) */
    if (sr_reg_check(&REG)) bad |= 1u << W_REG;
    for (int i = 0; i < 2; i++)
        for (int o = 0; o < REG.rows[i]; o++) if (rec_at[i][o] != REG.at[i][o]) bad |= 1u << W_REG;
    int a2b = 0, b2a = 0;
    for (int i = 0; i < NI; i++) {
        if (sr_reg_div(&REG, i) != nd[i]) bad |= 1u << W_REG;               /* (its division byte = its table division) */
        if (oldw[i] != SR_REG_NONE && REG.word[i] != SR_REG_NONE && (oldw[i] & 0xFF) == (REG.word[i] & 0xFF) &&
            REG.word[i] != oldw[i]) bad |= 1u << W_REG;                     /* (in its file: its record stays) */
        if (oldw[i] != SR_REG_NONE && REG.word[i] != SR_REG_NONE && (oldw[i] & 0xFF) != (REG.word[i] & 0xFF)) {
            st->reg_crossed++;
            if ((oldw[i] & 0xFF) == FILE_A) a2b++; else b2a++;
        }
        if (first_of[i] >= 0 && REG.word[i] != SR_REG_NONE &&             /* (a B team, through the registry) */
            (sr_reg_ident(&REG, REG.word[i]) != i || ((REG.word[i] & 0xFF) == FILE_A) != (nd[i] < FIRST_D)))
            bad |= 1u << W_REG;
    }
    if (a2b != down[2] || b2a != up[3]) bad |= 1u << W_REG;               /* (the C/D boundary's moves, no more) */
    for (int i = 0; i < 2; i++) if (REG.rows[i] > 256) bad |= 1u << W_REG;
    return bad;
}


/* another B team in B team i's division at the season end */
static int other_b_in(const int *bdiv, int i)
{
    for (int j = 0; j < NI; j++) if (j != i && first_of[j] >= 0 && bdiv[i] >= 0 && bdiv[j] == bdiv[i]) return 1;
    return 0;
}

static void ident_areas_from_ecc(int id, int g)
{
    const int L = ecc_letter[g];
    area_d[id] = (uint8_t)(0xF0 | L);
    area_c[id] = letter_c[L];
    lat_of[id] = (uint8_t)(L * 28 + (int)(h32((uint32_t)id) % 28u));
}

static void chain_init(uint32_t chain)
{
    static const uint16_t lo[LEVELS] = { 290, 240, 180, 110 }, span[LEVELS] = { 86, 81, 101, 141 };
    for (int i = 0; i < NF; i++) {
        const uint32_t h = h32(chain * 7919u + (uint32_t)i * 104729u);
        const int l = level_of(ITF_CLUB[i].div);
        slot_ident[i] = i; slot_div[i] = ITF_CLUB[i].div;
        str_of[i] = (uint16_t)(lo[l] + (h >> 12) % span[l]);
        area_c[i] = ITF_CLUB[i].area_c; area_d[i] = ITF_CLUB[i].area_d;
        first_of[i] = ITF_CLUB[i].first == 0xFFFF ? -1 : ITF_CLUB[i].first;
        lat_of[i] = (uint8_t)((area_d[i] & 15) * 28 + (int)(h % 28u));
    }
    int id = NF;
    for (int g = 0; g < IT_GROUPS; g++)
        for (int i = 0; i < IT_G[g].size; i++, id++) {
            const uint32_t h = h32(chain * 7919u + (uint32_t)id * 104729u);
            str_of[id] = (uint16_t)(20 + (h >> 12) % 201u); first_of[id] = -1; pgrp[g][i] = id;
            ident_areas_from_ecc(id, g);
        }
    for (int i = 0; i < NI; i++) down_cd[i] = down_de[i] = -100, viol[i] = 0;
    reg_init();
}

static void chain_run(uint32_t chain, int seasons, stats *st)
{
    chain_init(chain);
    sr_reset(&S);
    uint8_t second[8][2];
    int ns = 0;
    for (int i = 0; i < NF; i++) if (first_of[i] >= 0) { second[ns][0] = (uint8_t)i; second[ns][1] = (uint8_t)first_of[i]; ns++; }
    sr_league lg;
    memset(&lg, 0, sizeof lg);
    lg.divisions = ITF_NDIV; lg.promoted = ITF_PROM; lg.relegated = ITF_REL;
    lg.italy = 1; lg.serie_b = 1; lg.group_first = FIRST_C; lg.groups = 3;
    lg.spec = ITF_SPEC; lg.spec_len = sizeof ITF_SPEC;
    lg.seconds = (uint8_t)ns; lg.second = (const uint8_t (*)[2])second; lg.second_top = ITF_SECOND_TOP;
    lg.second_flags = (uint8_t)(ITF_SECOND_PO | 2);
    lg.second_floor = (uint8_t)floor_lv;
    const sr_levels lv = { LEVELS, LEVEL_FIRST };
    sr_pg_level pl[LEVELS];
    memset(pl, 0, sizeof pl);
    pl[0].first = 0; pl[0].groups = 1; pl[0].size[0] = ITF_SIZE[0];
    pl[1].first = 1; pl[1].groups = 1; pl[1].size[0] = ITF_SIZE[1];
    pl[2].first = FIRST_C; pl[2].groups = 3; for (int g = 0; g < 3; g++) pl[2].size[g] = ITF_SIZE[FIRST_C + g];
    pl[3].first = FIRST_D; pl[3].groups = DG; for (int g = 0; g < DG; g++) pl[3].size[g] = ITF_SIZE[FIRST_D + g];
    static sr_pg_club pc[NF];
    const sr_pg_league pg = { LEVELS, pl, NF, pc };
    sr_lp_shape sh;
    sr_lp_rpo rpo[IT_GROUPS];
    uint8_t urn[IT_GROUPS];
    it_shape(&sh, rpo, urn, NULL, 0);
    sh.spread = spread;

    for (int s = 0; s < seasons; s++) {
        const uint32_t seed = h32(chain * 1000003u + (uint32_t)s);
        unsigned bad = 0;
        static int tslot[ITF_NDIV][SR_CLUBS];
        /* the tables */
        for (int d = 0; d < ITF_NDIV; d++) {
            sr_lp_club c[SR_LP_CLUBS];
            sr_row t[SR_LP_CLUBS];
            int sl[SR_LP_CLUBS], n = 0;
            for (int i = 0; i < NF; i++)
                if (slot_div[i] == d) {
                    if (n == SR_LP_CLUBS) { bad |= 1u << W_SIZE; break; }
                    sl[n] = i; c[n].ident = (uint16_t)slot_ident[i]; c[n].strength = str_of[slot_ident[i]]; n++;
                }
            if (n != ITF_SIZE[d]) bad |= 1u << W_SIZE;
            if (game_spread < 0) sr_lp_table_game(seed, 100 + d, c, n, t);
            else sr_lp_table_sp(seed, 100 + d, c, n, t, game_spread);
            S.t.n[d] = (uint8_t)n;
            for (int k = 0; k < n; k++) {
                sr_row *r = &S.t.t[d][k];
                *r = t[k];
                r->glob = c[t[k].team].ident; r->team = (uint16_t)(r->glob + 1);
                tslot[d][k] = sl[t[k].team];
            }
        }
        S.t.nd = ITF_NDIV;
        if (bad) { st->summers++; st->bad++; for (int k = 0; k < W_N; k++) if (bad & (1u << k)) st->why[k]++; continue; }
        /* the play-outs: ties played or skipped by the gap (8: the spec's), from the tables */
        for (int d = FIRST_C; d < ITF_NDIV; d++) {
            const sr_row *t = S.t.t[d];
            const int c = d < FIRST_D, a = c ? 15 : 12;     /* (16th v 19th, 17th v 18th; 13th v 16th, 14th v 15th) */
            for (int x = 0; x < 2; x++) {
                const int gap = t[a + x].pts - t[a + 3 - x].pts, skip = gap >= 8;
                if (c) st->po_c[skip]++, st->gap_c += gap; else st->po_d[skip]++, st->gap_d += gap;
            }
        }
        /* the season end */
        sr_season se;
        memset(&se, 0, sizeof se);
        se.b_in_ko = 1; se.base = 0; se.levels = &lv;
        {
            const int g = FIRST_C + (int)(seed % 3u), k = (int)((seed >> 4) % 20u);
            se.cup_winner = S.t.t[g][k].team;   /* (the Coppa Italia Serie C: any club of Serie C -- relegated or in a
                                                   play-out it takes no place, F1) */
        }
        if (sr_italy(&S, &lg, &se) < 0) bad |= 1u << W_REFUSED;
        sr_knockouts(&S, &lg, &se);
        sr_final_moves(&S, &lg, &lv, 0);
        if (brk == 3 && s == 1) S.want[FIRST_D][1] = -1;
        int down[LEVELS] = { 0 }, up[LEVELS] = { 0 };
        for (int d = 0; d < ITF_NDIV; d++)
            for (int k = 0; k < S.t.n[d]; k++) {
                const int w = S.want[d][k], l = level_of(d);
                if (w > 0) down[l]++;
                if (w < 0) up[l]++;
                const int byplace = (k < ITF_PROM[d] && d > 0) ? -1 : k >= S.t.n[d] - ITF_REL[d] ? 1 : 0;
                if (w != byplace && first_of[S.t.t[d][k].glob] >= 0) st->bt_ko++;
            }
        for (int l = 0; l + 1 < LEVELS; l++) {
            st->bound[l][0] += down[l]; st->bound[l][1] += up[l + 1];
            if (down[l] != up[l + 1]) bad |= 1u << W_UPDOWN;
        }
        int ex_d = 0, ex_above = 0;             /* the B teams out of the league (4.2): from Serie D, from above it */
        for (int e = 0; e < S.b_exits; e++)
            for (int d = 0; d < ITF_NDIV; d++) for (int k = 0; k < S.t.n[d]; k++)
                if (S.t.t[d][k].glob == S.b_exit[e]) { if (d >= FIRST_D) ex_d++; else ex_above++; }
        if (floor_lv ? down[3] - ex_d != IT_UP : down[3] < IT_UP) bad |= 1u << W_UPDOWN;
        const int need = down[3] + ex_above;    /* (the pool's up = Serie D's down + every B team out) */
        if (need > IT_UP) st->surplus += need - IT_UP, st->surplus_n++;
        st->exits_d += ex_d; st->exits_above += ex_above;
        st->bound[3][0] += down[3];
        sh.extra = 1 + need - IT_UP;            /* (up.extra: one more for each B team out) */
        static int bdiv[NI];                    /* each club's division at the season end, -1 out of the league */
        for (int i = 0; i < NI; i++) bdiv[i] = -1;
        for (int d = 0; d < ITF_NDIV; d++) for (int k = 0; k < S.t.n[d]; k++) bdiv[S.t.t[d][k].glob] = d;
        /* the Eccellenza */
        sr_lp_club pcl[IT_GROUPS][SR_LP_CLUBS];
        const sr_lp_club *pp[IT_GROUPS];
        int32_t pn[IT_GROUPS];
        sr_lp_up pu[48];
        for (int g = 0; g < IT_GROUPS; g++) {
            for (int i = 0; i < IT_G[g].size; i++) pcl[g][i].ident = (uint16_t)pgrp[g][i], pcl[g][i].strength = str_of[pgrp[g][i]];
            pp[g] = pcl[g]; pn[g] = IT_G[g].size;
        }
        uint16_t barred[8];                     /* (4.2: a B team of the pool whose first team ends at Serie D or lower) */
        int nbar = 0;
        if (floor_lv)
            for (int i = 0; i < NI && nbar < 8; i++) {
                if (first_of[i] < 0 || bdiv[i] >= 0) continue;
                const int f = first_of[i], fd = bdiv[f];
                int fl = LEVELS;
                if (fd >= 0) {
                    for (int k = 0; k < S.t.n[fd]; k++)
                        if (S.t.t[fd][k].glob == f) fl = level_of(fd) + S.want[fd][k];
                    for (int e = 0; e < S.b_exits; e++) if (S.b_exit[e] == f) fl = LEVELS;
                }
                if (fl >= FLOOR_LEVEL) barred[nbar++] = (uint16_t)i;
            }
        sh.barred = barred; sh.nbarred = nbar;
        st->barred += nbar;
        const int nup = sr_lp_season_cross(seed, IT_GROUPS, pp, pn, &sh, pu);
        if (nup != need) bad |= 1u << (nup < 0 ? W_REFUSED : W_UPDOWN);
        st->bound[3][1] += nup > 0 ? nup : 0;
        if (bad & ((1u << W_UPDOWN) | (1u << W_REFUSED))) {
            st->summers++; st->bad++; for (int k = 0; k < W_N; k++) if (bad & (1u << k)) st->why[k]++;
            continue;
        }
        /* the stats: promoted clubs' rank, straight back up */
        for (int g = 0; g < DG; g++) {
            const int d = FIRST_D + g;
            for (int k = 0; k < S.t.n[d]; k++) {
                if (S.want[d][k] >= 0) continue;
                const int id = S.t.t[d][k].glob;
                int r = 0;
                for (int j = 0; j < S.t.n[d]; j++) r += str_of[S.t.t[d][j].glob] > str_of[id];
                st->rank_sum[0] += r; st->rank_n[0]++; st->top4[0] += r < 4;
                if (down_cd[id] == s - 1) st->cd_back++;
            }
        }
        int upid[48], vg[48], vi[48];
        for (int k = 0; k < nup; k++) {
            upid[k] = pgrp[pu[k].div][pu[k].idx]; vg[k] = pu[k].div; vi[k] = pu[k].idx;
            int r = 0;
            for (int i = 0; i < pn[pu[k].div]; i++) r += pcl[pu[k].div][i].strength > str_of[upid[k]];
            st->rank_sum[1] += r; st->rank_n[1]++; st->top4[1] += r < 4;
            if (down_de[upid[k]] == s - 1) st->de_back++;
        }
        /* the summer: the game's moves, the pool's swap, then the groups */
        static int8_t old_off[NF], new_off[NF];
        static uint8_t entering[NF];
        int rel[48], nrel = 0;
        for (int d = 0; d < ITF_NDIV; d++)
            for (int k = 0; k < S.t.n[d]; k++) {
                const int i = tslot[d][k];
                old_off[i] = (int8_t)d; new_off[i] = (int8_t)(d + S.want[d][k]); entering[i] = 0;
                int out = d >= FIRST_D && S.want[d][k] > 0;
                for (int e = 0; e < S.b_exits && !out; e++) out = S.t.t[d][k].glob == S.b_exit[e];
                if (out && nrel < 48) rel[nrel++] = i;
                if (d >= FIRST_C && d < FIRST_D && S.want[d][k] > 0) { down_cd[slot_ident[i]] = s; st->cd_n++; }
            }
        {   /* the 36 down into the promoted clubs' Eccellenza places, nearest by ground (both sorted north to south) */
            int dn[48], ord[48];
            uint16_t plist[NP];
            int np = 0;
            for (int k = 0; k < nup; k++) dn[k] = rel[k], ord[k] = k;
            for (int a = 1; a < nup; a++)         /* (slots by their club's ground; places by the promoted club's) */
                for (int b = a; b > 0 && lat_of[slot_ident[dn[b]]] < lat_of[slot_ident[dn[b - 1]]]; b--) { int x = dn[b]; dn[b] = dn[b - 1]; dn[b - 1] = x; }
            for (int a = 1; a < nup; a++)
                for (int b = a; b > 0 && lat_of[upid[ord[b]]] < lat_of[upid[ord[b - 1]]]; b--) { int x = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = x; }
            for (int g = 0; g < IT_GROUPS; g++) for (int i = 0; i < IT_G[g].size; i++) plist[np++] = str_of[pgrp[g][i]];
            for (int k = 0; k < nup; k++) {
                const int slot = dn[k], u = upid[ord[k]], g = vg[ord[k]], down_id = slot_ident[slot];
                const uint16_t rec = str_of[down_id];
                str_of[down_id] = (uint16_t)sr_lp_newcomer_strength(rec, plist, np);      /* (iii) */
                str_of[u] = rec;
                pgrp[g][vi[ord[k]]] = down_id;
                slot_ident[slot] = u; entering[slot] = 1;
                down_de[down_id] = s; st->de_n++;
                (void)g;
            }
        }
        for (int i = 0; i < NF; i++) {
            const int id = slot_ident[i];
            pc[i].base = 0; pc[i].area[0] = area_c[id]; pc[i].area[1] = area_d[id]; pc[i].area[2] = pc[i].area[3] = 0xFF;
        }
        if (sr_pg_season(&pg, old_off, new_off, entering) != 0) bad |= 1u << W_REFUSED;
        for (int i = 0; i < NF; i++) slot_div[i] = new_off[i];
        if (brk == 1 && s == 1) pgrp[3][2] = pgrp[3][1];
        if (brk == 2 && s == 1)                     /* (a B team swapped into its first team's division) */
            for (int i = 0; i < NF; i++) {
                const int f = first_of[slot_ident[i]];
                if (f < 0) continue;
                for (int j = 0; j < NF; j++)
                    if (slot_ident[j] == f) {
                        for (int k = 0; k < NF; k++)
                            if (slot_div[k] == slot_div[j] && k != j) { const int x = slot_div[i]; slot_div[i] = slot_div[k]; slot_div[k] = x; break; }
                        break;
                    }
                break;
            }
        {   /* the two team files */
            static uint16_t order[NI];
            int no = 0;
            for (int d = 0; d < ITF_NDIV; d++) for (int k = 0; k < S.t.n[d]; k++) order[no++] = S.t.t[d][k].glob;
            bad |= reg_summer(s, order, no, down, up, st);
        }
        /* the invariants */
        {
            static unsigned char seen[NI];
            int size[ITF_NDIV] = { 0 }, lev_of_id[NI];
            memset(seen, 0, sizeof seen);
            for (int i = 0; i < NI; i++) lev_of_id[i] = LEVELS;    /* (the pool: below the chain) */
            for (int i = 0; i < NF; i++) {
                seen[slot_ident[i]]++;
                if (slot_div[i] < 0 || slot_div[i] >= ITF_NDIV) { bad |= 1u << W_SIZE; continue; }
                size[slot_div[i]]++; lev_of_id[slot_ident[i]] = level_of(slot_div[i]);
            }
            for (int g = 0; g < IT_GROUPS; g++) for (int i = 0; i < IT_G[g].size; i++) seen[pgrp[g][i]]++;
            for (int i = 0; i < NI; i++) { if (seen[i] != 1) bad |= 1u << W_LOST; if (str_of[i] > 375) bad |= 1u << W_STR; }
            for (int d = 0; d < ITF_NDIV; d++) if (size[d] != ITF_SIZE[d]) bad |= 1u << W_SIZE;
            for (int i = 0; i < NI; i++) {
                if (first_of[i] < 0) continue;
                const int lb = lev_of_id[i], lf = lev_of_id[first_of[i]], was = viol[i];
                viol[i] = lb < LEVELS && (lb <= lf || lb < ITF_SECOND_TOP);
                if (!viol[i]) continue;
                if (floor_lv) bad |= 1u << W_BTEAM;    /* (4.2's rules: none of the gaps below) */
                else if (was) st->gap_carry++;           /* (the library acts only when the first team comes into its level) */
                else if (lb == LEVELS - 1 && lf == LEVELS - 1) st->gap_same++;     /* (library gaps: reported, see the head) */
                else if (lf == LEVELS || bdiv[first_of[i]] < 0) st->gap_below++;   /* (its first team in the pool now or at the season end) */
                else if (lb == lf && other_b_in(bdiv, i))
                    st->gap_two++;
                else bad |= 1u << W_BTEAM;
            }
            const int dz = s * 10 / seasons;
            long sum[LEVELS + 1] = { 0 };
            for (int i = 0; i < NF; i++) sum[level_of(slot_div[i])] += str_of[slot_ident[i]];
            for (int g = 0; g < IT_GROUPS; g++) for (int i = 0; i < IT_G[g].size; i++) sum[LEVELS] += str_of[pgrp[g][i]];
            st->dec_sum[0][dz] += sum[0] / 20; st->dec_sum[1][dz] += sum[1] / 20; st->dec_sum[2][dz] += sum[2] / 60;
            st->dec_sum[3][dz] += sum[3] / (DG * DS); st->dec_sum[4][dz] += sum[4] / NP; st->dec_cnt[dz]++;
        }
        st->summers++;
        if (bad) { st->bad++; for (int k = 0; k < W_N; k++) if (bad & (1u << k)) st->why[k]++; }
    }
}

int main(int argc, char **argv)
{
    const int chains = argc > 1 ? atoi(argv[1]) : 1000, seasons = argc > 2 ? atoi(argv[2]) : 100;
    static stats st;
    const char *b = getenv("ITALYFULL_BREAK"), *spv = getenv("SPREAD");
    if (spv) spread = atoi(spv);
    if (getenv("GAME_SPREAD")) game_spread = atoi(getenv("GAME_SPREAD"));
    if (getenv("BFLOOR")) floor_lv = atoi(getenv("BFLOOR"));
    brk = !b ? 0 : !strcmp(b, "drop") ? 1 : !strcmp(b, "bteam") ? 2 : !strcmp(b, "updown") ? 3 : !strcmp(b, "regdrop") ? 4 :
          !strcmp(b, "regdup") ? 5 : !strcmp(b, "regdiv") ? 6 : 0;
    {   /* each Serie D group's most common Serie C area among the real clubs */
        int cnt[DG][256];
        memset(cnt, 0, sizeof cnt);
        for (int i = 0; i < NF; i++) if ((ITF_CLUB[i].area_d & 15) < DG) cnt[ITF_CLUB[i].area_d & 15][ITF_CLUB[i].area_c]++;
        for (int L = 0; L < DG; L++) { int m = 0; for (int a = 0; a < 256; a++) if (cnt[L][a] > cnt[L][m]) m = a; letter_c[L] = (uint8_t)m; }
    }
    for (int c = 0; c < chains; c++) chain_run((uint32_t)c, seasons, &st);
    printf("italyfull: %d chains x %d seasons = %ld summers (A 20, B 20, C 3 x 20, D 9 x 18 <-> Eccellenza 28 groups; "
           "%d + %d clubs; Eccellenza spread %d)\n", chains, seasons, st.summers, NF, NP, spread);
    printf("  invariants: %ld summers broken\n", st.bad);
    for (int k = 0; k < W_N; k++) if (st.why[k]) printf("    %s: %ld\n", why_txt[k], st.why[k]);
    static const char *bn[LEVELS] = { "A/B", "B/C", "C/D", "D/Ecc" };
    printf("  per summer, down / up at each boundary:");
    for (int l = 0; l < LEVELS; l++) printf(" %s %.2f/%.2f", bn[l], (double)st.bound[l][0] / st.summers, (double)st.bound[l][1] / st.summers);
    printf("\n  straight back up: C/D %.1f%% of %ld, D/Ecc %.1f%% of %ld\n", 100.0 * st.cd_back / (st.cd_n ? st.cd_n : 1),
           st.cd_n, 100.0 * st.de_back / (st.de_n ? st.de_n : 1), st.de_n);
    printf("  promoted clubs' strength rank in their group (0 = strongest): D -> C %.2f (top 4 %.1f%%), Ecc -> D %.2f "
           "(top 4 %.1f%%)\n", (double)st.rank_sum[0] / st.rank_n[0], 100.0 * st.top4[0] / st.rank_n[0],
           (double)st.rank_sum[1] / st.rank_n[1], 100.0 * st.top4[1] / st.rank_n[1]);
    printf("  play-out ties skipped by the gap (8+ points): Serie C %.1f%% of %ld, Serie D %.1f%% of %ld\n",
           100.0 * st.po_c[1] / (st.po_c[0] + st.po_c[1]), st.po_c[0] + st.po_c[1],
           100.0 * st.po_d[1] / (st.po_d[0] + st.po_d[1]), st.po_d[0] + st.po_d[1]);
    printf("  a play-out tie's mean gap in points: Serie C %.2f, Serie D %.2f\n", (double)st.gap_c / (st.po_c[0] + st.po_c[1]),
           (double)st.gap_d / (st.po_d[0] + st.po_d[1]));
    printf("  B teams (floor %d): moves the B-team rules changed %ld; out of the league %ld from Serie D, %ld from "
           "above; %ld summers the pool's extra places filled them (%ld); B teams of the pool barred from going up %ld\n",
           floor_lv, st.bt_ko, st.exits_d, st.exits_above, st.surplus_n, st.surplus, st.barred);
    if (!floor_lv) printf("  the Mod 4.1's gaps (BFLOOR=0, reported, not failed): a B team and its first team both in Serie D %ld summers, a B team "
           "over its first team in the Eccellenza %ld,\n    two B teams in one division whose first teams both come in (the "
           "second's stay undoes the first's drop) %ld,\n    still over its first team from the summer before %ld\n",
           st.gap_same, st.gap_below, st.gap_two, st.gap_carry);
    printf("  two team files (%d: A, B, C; %d: D + %d pool rows): %.2f swaps a summer (most %ld), %.2f across the files, "
           "%.2f with the off-file pool; %.2f clubs changed file a summer (= C/D's down + up)\n", FILE_A, FILE_B, POOL_ROWS,
           (double)st.reg_swaps / st.summers, st.reg_max, (double)st.reg_across / st.summers,
           (double)st.reg_off / st.summers, (double)st.reg_crossed / st.summers);
    printf("  drift (mean strength by tenth of the chain) A / B / C / D / Ecc:\n   ");
    for (int z = 0; z < 10; z++) {
        const long n = st.dec_cnt[z] ? st.dec_cnt[z] : 1;
        printf(" %ld/%ld/%ld/%ld/%ld", st.dec_sum[0][z] / n, st.dec_sum[1][z] / n, st.dec_sum[2][z] / n, st.dec_sum[3][z] / n,
               st.dec_sum[4][z] / n);
    }
    printf("\n%s italyfull\n", st.bad ? "FAIL" : "PASS");
    return st.bad ? 1 : 0;
}
