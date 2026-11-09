/* 42-s3shape: Italy's Serie B and Serie C play-offs as data -- the pack's knockout bytes (sr_gplay) with the new
 * codes (kind | 0x40 extension: direct, a level of groups, seeded rounds, best first; levels 6 / 7; entrant type 9,
 * the Coppa Italia Serie C's place) -- against the Mod's dplay.c port (sr_dplay), the reference.
 *
 * Structural differential: every tie's winner, every draw's order and every lot forced the same way in both
 * (sr_force), the same random tables: per season the same ties (home club, away club, legs) and the same moves.
 * Odds (unforced): the play-offs' winners by place, both paths -- reported, not compared.
 * The real C.U. 209/A rule (cup_finalist known; sr_dplay has no such branch): forced cases against a hand-written
 * table (MC internal/handoff/mc-s3shape-answers.md (b)).
 *
 *   s3shape [SEASONS]      (default 100000)
 *   s3shape -spec FILE R   the spec's bytes into FILE (R 1: the real rule) -- tests/rules/specs/ita_s3{,r}.spec */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tables.h"
#include "swosrules.h"

static const uint8_t PROM[16] = { 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t REL[16] = { 3, 3, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static uint8_t SPEC[512];
static uint32_t SPEC_LEN;
static int fails;
#define FAIL(...) do { if (fails++ < 20) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the spec: Serie B's play-off and play-out, Serie C's play-off (divisions: 0 A, 1 B, 2..4 C's groups) ---- */
static uint32_t at;
static void b(int x) { SPEC[at++] = (uint8_t)x; }
static void ent(int ty, int d, int p, int x) { b(ty); b(d); b(p); b(x); }
static void round_(int ties, int legs, int host, int level, int pairing, int gap) { b(ties); b(legs); b(host); b(level); b(pairing); b(gap); }
enum { LISTED = 0, BETTER = 1, LV_BETTER = 0, LV_ET_PENS = 1, LV_BETTER_PTS = 6, LV_SECOND = 7, W = 0x80 };
static void build_spec(int real_rule)
{
    at = 0;
    /* Serie B's play-off: 3rd-8th; the 3rd 15+ points ahead of the 4th goes straight (dplay: "more than 14");
     * 5v8 and 6v7 at the better placed, level -> it; 3 v W(6v7), 4 v W(5v8), two legs, return at the 3rd / 4th;
     * the final two legs, return at the better placed, level -> it unless level on points (e.t. + penalties) */
    b(1 | 0x40); b(6); b(3); b(0); b(1); b(0);
    b(2); b(1); b(15);                          /* (extension: direct 15) */
    for (int p = 3; p <= 8; p++) ent(0, 1, p, 0);
    round_(2, 1, LISTED, LV_BETTER, 0, 0); b(2); b(5); b(3); b(4);
    round_(2, 2, LISTED, LV_BETTER, 0, 0); b(W | 1); b(0); b(W | 0); b(1);
    round_(1, 2, BETTER, LV_BETTER_PTS, 0, 0); b(W | 2); b(W | 3);
    /* Serie B's play-out: 16th v 17th, held within 4 points (gap 5: else the 17th down); two legs, return at the 16th,
     * level -> it unless level on points */
    b(4); b(2); b(1); b(1); b(2); b(0);
    ent(0, 1, 16, 0); ent(0, 1, 17, 0);
    round_(1, 2, BETTER, LV_BETTER_PTS, 0, 5); b(0); b(1);
    /* Serie C's play-off: per group 2nd, 3rd, 4th, 5th-10th (9 entrants, group g at 9g), then the cup place (27) */
    b(1 | 0x40); b(28); b(10); b(1); b(2); b(0);
    b(13); b(2); b(4); b(6); b(4); b(10); b(4); b(7); b(3); b(8); b(3); b(6); b(3); b(7);   /* (level; best first 6 4..9,
                                                                                         7 3..7; seeded 6, 7) */
    for (int g = 0; g < 3; g++) for (int p = 2; p <= 10; p++) ent(0, 2 + g, p, 0);
    ent(9, 2, 3, 8 | (real_rule ? 0x80 : 0));
    for (int g = 0; g < 3; g++) {               /* group round 1: 5v10, 6v9, 7v8 (W 3g ..) */
        round_(3, 1, BETTER, LV_BETTER, 6, 0);
        for (int p = 3; p <= 8; p++) b(9 * g + p);
    }
    for (int g = 0; g < 3; g++) {               /* group round 2: the 4th + the three winners, best v worst (W 9 + 2g ..) */
        round_(2, 1, BETTER, LV_BETTER, 4, 0);
        b(9 * g + 2); b(W | (3 * g)); b(W | (3 * g + 1)); b(W | (3 * g + 2));
    }
    round_(5, 2, LISTED, LV_SECOND, 0, 0);      /* national round 1: the 3rds, the cup place, the best qualifier v the
                                                   other five drawn (W 15 ..) */
    b(1); b(10); b(19); b(27); for (int k = 9; k < 15; k++) b(W | k);
    round_(4, 2, LISTED, LV_SECOND, 0, 0);      /* national round 2: the 2nds, the best R1 winner v the other four (W 20 ..) */
    b(0); b(9); b(18); for (int k = 15; k < 20; k++) b(W | k);
    round_(2, 2, LISTED, LV_ET_PENS, 0x84, 0);  /* the Final Four: drawn, return at the second drawn (W 24, 25) */
    for (int k = 20; k < 24; k++) b(W | k);
    round_(1, 2, LISTED, LV_ET_PENS, 0, 0); b(W | 24); b(W | 25);
    b(0);
    SPEC_LEN = at;
}

/* ---- the hook: forced winners, draws, lots, and every tie recorded ---- */
static uint32_t hseed;
static uint32_t hh(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; return x ^ (x >> 16); }
typedef struct { uint16_t home, away; int32_t legs; } tie_t;
static tie_t TIES[64];
static int nties;
static uint16_t f_win(void *ctx, uint16_t home, uint16_t away, int32_t legs)
{
    (void)ctx;
    if (nties < 64) { TIES[nties].home = home; TIES[nties].away = away; TIES[nties].legs = legs; nties++; }
    const uint16_t lo = home < away ? home : away, hi = home ^ away ^ lo;
    return hh(hseed ^ (uint32_t)lo * 65537u ^ (uint32_t)hi * 31u) & 1 ? lo : hi;
}
static void f_draw(void *ctx, int32_t n, uint16_t *g)
{
    (void)ctx;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && hh(hseed ^ g[j]) < hh(hseed ^ g[j - 1]); j--) { uint16_t x = g[j]; g[j] = g[j - 1]; g[j - 1] = x; }
}
static uint8_t f_lot(void *ctx, uint16_t g) { (void)ctx; return (uint8_t)(hh(hseed * 3u ^ g) >> 24); }
static const sr_force FORCE = { 0, f_win, f_draw, f_lot };

static int tie_cmp(const void *a, const void *b)
{
    const tie_t *x = a, *y = b;
    return x->home != y->home ? x->home - y->home : x->away != y->away ? x->away - y->away : x->legs - y->legs;
}
static int mv_cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static sr_state SA, SB;
static void fill(sr_state *st, struct trow t[16][32], const int n[16])
{
    st->t.nd = (uint8_t)ND; st->nmv = 0;
    for (int d = 0; d < ND; d++) {
        st->t.n[d] = (uint8_t)n[d];
        for (int k = 0; k < n[d]; k++) {
            sr_row *r = &st->t.t[d][k];
            r->team = t[d][k].team; r->glob = t[d][k].glob; r->won = t[d][k].won; r->gf = t[d][k].gf; r->ga = t[d][k].ga;
            r->pts = t[d][k].pts;
        }
    }
}
/* the moves as "glob * 4 + 1 + sign" sorted */
static int moves(const sr_state *st, int *out)
{
    int m = 0;
    for (int i = 0; i < st->nmv; i++) out[m++] = st->mv[i].glob * 4 + 1 + (st->mv[i].delta > 0) - (st->mv[i].delta < 0);
    qsort(out, (size_t)m, sizeof *out, mv_cmp);
    return m;
}

/* ---- the differential, one season: both paths forced alike (or not), the same tables ---- */
static sr_league LG, LK;
static long win_b[2][21], win_c[2][21];
static int diff_season(unsigned it, struct trow t[16][32], const int n[16], uint16_t cupw, int force)
{
    static int ma[64], mb[64];
    static tie_t ta[64];
    sr_season se;
    memset(&se, 0, sizeof se);
    se.cup_winner = cupw;
    if (force) se.force = &FORCE;
    hseed = hh(it + 1);
    fill(&SA, t, n); fill(&SB, t, n);
    nties = 0;
    sr_italy(&SA, &LG, &se);
    const int na = nties;
    memcpy(ta, TIES, sizeof ta);
    nties = 0;
    sr_knockouts(&SB, &LK, &se);
    const int nb = nties;
    if (!force) {                               /* (odds: the winners' places) */
        for (int x = 0; x < 2; x++) {
            const sr_state *st = x ? &SB : &SA;
            for (int i = 0; i < st->nmv; i++) {
                const int g = st->mv[i].glob - 424, d = g / 20, p = g % 20 + 1;
                if (st->mv[i].delta < 0 && d == 1) win_b[x][p]++;
                if (st->mv[i].delta < 0 && d >= 2 && d <= 4) win_c[x][p]++;
            }
        }
        return 1;
    }
    const int ka = moves(&SA, ma), kb = moves(&SB, mb);
    qsort(ta, (size_t)na, sizeof *ta, tie_cmp);
    qsort(TIES, (size_t)nb, sizeof *TIES, tie_cmp);
    int ok = na == nb && ka == kb && !memcmp(ma, mb, sizeof(int) * (size_t)ka);
    for (int i = 0; ok && i < na; i++) ok = !tie_cmp(&ta[i], &TIES[i]);
    if (!ok && fails < 3) {
        printf("FAIL season %u (cup winner %04X): dplay %d ties %d moves, the spec %d ties %d moves\n", it, cupw, na, ka, nb, kb);
        for (int i = 0; i < na || i < nb; i++)
            printf("   %3d: %s%04u-%04u/%d   %s%04u-%04u/%d\n", i, i < na ? "" : "-", i < na ? ta[i].home : 0,
                   i < na ? ta[i].away : 0, i < na ? ta[i].legs : 0, i < nb ? "" : "-", i < nb ? TIES[i].home : 0,
                   i < nb ? TIES[i].away : 0, i < nb ? TIES[i].legs : 0);
        printf("   moves:");
        for (int i = 0; i < SA.nmv; i++) printf(" %u%+d", SA.mv[i].glob, SA.mv[i].delta);
        printf("\n   moves:");
        for (int i = 0; i < SB.nmv; i++) printf(" %u%+d", SB.mv[i].glob, SB.mv[i].delta);
        printf("\n");
    }
    if (!ok) fails++;
    return ok ? na : -1;
}

/* ---- the real rule (C.U. 209/A): forced cases v a hand-written table ----
 * Serie C's groups 20 clubs, a point a place: both play-outs held (16v19 3 points apart, 17v18 1; under 8). "not held":
 * the 18th-20th 10 points lower (16v19 13 apart, 17v18 11). Every tie to the better placed (the lower global number),
 * no draw. Each entry: the place that takes the cup's seat, and the place its group's play-off scrolls from (0 none). */
static const signed char WIN_HELD[20][2] = {    /* the winner at 1..20, the finalist precluded (another group's 1st):
                                                   1-3 precluded -> the 4th of the winner's group, scroll from 4;
                                                   4-10 itself, scroll from its place; 11-15 itself; 16-17 in a held
                                                   play-out and 18-20 relegated: precluded -> the 4th */
    { 4, 4 }, { 4, 4 }, { 4, 4 }, { 4, 4 }, { 5, 5 }, { 6, 6 }, { 7, 7 }, { 8, 8 }, { 9, 9 }, { 10, 10 },
    { 11, 0 }, { 12, 0 }, { 13, 0 }, { 14, 0 }, { 15, 0 }, { 4, 4 }, { 4, 4 }, { 4, 4 }, { 4, 4 }, { 4, 4 } };
static const signed char WIN_FREE[20][2] = {    /* the same, play-outs not held: the 16th / 17th take the seat, no scroll */
    { 4, 4 }, { 4, 4 }, { 4, 4 }, { 4, 4 }, { 5, 5 }, { 6, 6 }, { 7, 7 }, { 8, 8 }, { 9, 9 }, { 10, 10 },
    { 11, 0 }, { 12, 0 }, { 13, 0 }, { 14, 0 }, { 15, 0 }, { 16, 0 }, { 17, 0 }, { 4, 4 }, { 4, 4 }, { 4, 4 } };
/* the finalist at 1..20, the winner precluded (its group's 1st): 'F' the finalist's group, 'W' the winner's */
static const char FIN_HELD[20][3] = {
    { 'W', 4, 4 }, { 'W', 4, 4 }, { 'W', 4, 4 }, { 'F', 4, 4 }, { 'F', 5, 5 }, { 'F', 6, 6 }, { 'F', 7, 7 }, { 'F', 8, 8 },
    { 'F', 9, 9 }, { 'F', 10, 10 }, { 'F', 11, 0 }, { 'F', 12, 0 }, { 'F', 13, 0 }, { 'F', 14, 0 }, { 'F', 15, 0 },
    { 'W', 4, 4 }, { 'W', 4, 4 }, { 'W', 4, 4 }, { 'W', 4, 4 }, { 'W', 4, 4 } };
static const char FIN_FREE[20][3] = {
    { 'W', 4, 4 }, { 'W', 4, 4 }, { 'W', 4, 4 }, { 'F', 4, 4 }, { 'F', 5, 5 }, { 'F', 6, 6 }, { 'F', 7, 7 }, { 'F', 8, 8 },
    { 'F', 9, 9 }, { 'F', 10, 10 }, { 'F', 11, 0 }, { 'F', 12, 0 }, { 'F', 13, 0 }, { 'F', 14, 0 }, { 'F', 15, 0 },
    { 'F', 16, 0 }, { 'F', 17, 0 }, { 'W', 4, 4 }, { 'W', 4, 4 }, { 'W', 4, 4 } };
/* the Mod rule (no finalist, or a spec without the real rule): the winner at 11-15 (16-17 not held) itself, else the
 * 11th of its group, no scroll */
static const signed char MOD_HELD[20] = { 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 13, 14, 15, 11, 11, 11, 11, 11 };
static const signed char MOD_FREE[20] = { 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 13, 14, 15, 16, 17, 11, 11, 11 };

static uint16_t b_win(void *ctx, uint16_t home, uint16_t away, int32_t legs)
{
    (void)ctx;
    if (nties < 64) { TIES[nties].home = home; TIES[nties].away = away; TIES[nties].legs = legs; nties++; }
    return home < away ? home : away;
}
static void b_draw(void *ctx, int32_t n, uint16_t *g) { (void)ctx; (void)n; (void)g; }
static uint8_t b_lot(void *ctx, uint16_t g) { (void)ctx; return (uint8_t)g; }
static const sr_force BETTER_WINS = { 0, b_win, b_draw, b_lot };
#define GLOB(g, p) (424 + (2 + (g)) * 20 + (p) - 1)

/* one case: winner (group wg, place wp), finalist (fg, fp; fp 0 none); the expectation: the seat's (group, place),
 * the scrolled group and its first place (0 none). Checks the cup's seat among national round 1's seeds and every
 * group's round 1 / round 2 ties. */
static void real_case(const char *what, int real, int free_, int wg, int wp, int fg, int fp, int eg, int ep, int sg, int sf)
{
    static sr_state st;
    sr_season se;
    sr_league lg = LK;
    build_spec(real);
    lg.spec_len = SPEC_LEN;
    memset(&se, 0, sizeof se);
    memset(&st, 0, sizeof st);
    st.t.nd = 5;
    for (int d = 0; d < 5; d++) {
        st.t.n[d] = 20;
        for (int k = 0; k < 20; k++) {
            sr_row *r = &st.t.t[d][k];
            r->team = (uint16_t)(0x1400 + d * 32 + k); r->glob = (uint16_t)(424 + d * 20 + k);
            r->pts = (uint16_t)(80 - k - (free_ && k >= 17 ? 10 : 0)); r->won = r->pts / 3; r->gf = 40; r->ga = 40;
        }
    }
    se.cup_winner = (uint16_t)(0x1400 + (2 + wg) * 32 + wp - 1);
    se.cup_finalist = fp ? (uint16_t)(0x1400 + (2 + fg) * 32 + fp - 1) : 0;
    se.force = &BETTER_WINS;
    nties = 0;
    lg.divisions = 5;
    sr_knockouts(&st, &lg, &se);
    int c0 = 0, bad = 0;
    while (c0 < nties && TIES[c0].home < GLOB(0, 1)) c0++;
    if (nties - c0 != 27) bad = 1;              /* (9 + 6 group ties, 5 + 4, 2 + 1 national) */
    for (int g = 0; g < 3 && !bad; g++) {
        int pl[8], m = 0;
        for (int p = 4; m < 7; p++) if (!(g == sg && sf && p == sf)) pl[m++] = p;
        for (int i = 0; i < 3; i++)
            if (TIES[c0 + 3 * g + i].home != GLOB(g, pl[1 + i]) || TIES[c0 + 3 * g + i].away != GLOB(g, pl[6 - i])) bad = 2;
        if (TIES[c0 + 9 + 2 * g].home != GLOB(g, pl[0]) || TIES[c0 + 9 + 2 * g].away != GLOB(g, pl[3])) bad = 3;
    }
    int seat = 0;
    for (int i = c0 + 15; i < c0 + 20 && !bad; i++) seat |= TIES[i].home == GLOB(eg, ep);
    if (!bad && !seat) bad = 4;
    if (bad) {
        FAIL("real rule, %s: winner %d#%d finalist %d#%d%s -- want the seat %d#%d, scroll %d#%d (%s)", what, wg, wp, fg, fp,
             free_ ? " (play-outs not held)" : "", eg, ep, sg, sf,
             bad == 1 ? "ties missing" : bad == 2 ? "a group round 1" : bad == 3 ? "a group round 2" : "not seeded");
        if (fails <= 3)
            for (int i = c0; i < nties; i++) printf("   %2d %u-%u/%d\n", i - c0, TIES[i].home, TIES[i].away, TIES[i].legs);
    }
}
static int real_cases(void)
{
    int n = 0;
    for (int fr = 0; fr < 2; fr++) {
        const signed char (*wt)[2] = fr ? WIN_FREE : WIN_HELD;
        const char (*ft)[3] = fr ? FIN_FREE : FIN_HELD;
        for (int p = 1; p <= 20; p++, n++)      /* the winner at each place (group 0), the finalist group 1's 1st */
            real_case("winner", 1, fr, 0, p, 1, 1, 0, wt[p - 1][0], wt[p - 1][1] ? 0 : -1, wt[p - 1][1]);
        for (int p = 1; p <= 20; p++, n++) {    /* the finalist at each place in another group (1), the winner 0#1 */
            const int g = ft[p - 1][0] == 'F' ? 1 : 0;
            real_case("finalist", 1, fr, 0, 1, 1, p, g, ft[p - 1][1], ft[p - 1][2] ? g : -1, ft[p - 1][2]);
        }
        for (int p = 3; p <= 20; p++, n++) {    /* the finalist in the winner's group (2), the winner 2#2 */
            real_case("finalist, same group", 1, fr, 2, 2, 2, p, 2, ft[p - 1][1], ft[p - 1][2] ? 2 : -1, ft[p - 1][2]);
        }
        for (int p = 1; p <= 20; p++, n += 2) { /* the Mod rule: no finalist; a finalist, the spec without the real rule */
            real_case("Mod rule, no finalist", 1, fr, 1, p, 0, 0, 1, (fr ? MOD_FREE : MOD_HELD)[p - 1], -1, 0);
            real_case("Mod rule, the spec's", 0, fr, 1, p, 2, 12, 1, (fr ? MOD_FREE : MOD_HELD)[p - 1], -1, 0);
        }
    }
    build_spec(0);
    return n;
}

int main(int argc, char **argv)
{
    if (argc > 3 && !strcmp(argv[1], "-spec")) {
        FILE *f = fopen(argv[2], "wb");
        build_spec(atoi(argv[3]));
        if (!f || fwrite(SPEC, 1, SPEC_LEN, f) != SPEC_LEN || fclose(f)) return 2;
        return 0;
    }
    const unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 100000;
    static struct trow t[16][32];
    int n[16];
    long same = 0, nt_total = 0, direct = 0, out_skip = 0, edges = 0;
    build_spec(0);
    LG.divisions = 14; LG.promoted = PROM; LG.relegated = REL; LG.italy = 1; LG.serie_b = 1; LG.group_first = 2; LG.groups = 3;
    LK.divisions = 14; LK.promoted = PROM; LK.relegated = REL; LK.spec = SPEC; LK.spec_len = SPEC_LEN;
    {
        sr_check_error e;
        if (sr_spec_check(SPEC, SPEC_LEN, 14, &e) != SR_CHECK_OK) { printf("FAIL the spec refused at byte %u\n", e.offset); return 1; }
        build_spec(1);
        if (sr_spec_check(SPEC, SPEC_LEN, 14, &e) != SR_CHECK_OK) { printf("FAIL the real-rule spec refused at byte %u\n", e.offset); return 1; }
        build_spec(0);
    }
    for (unsigned it = 0; it < N; it++) {
        make_tables(it, t, n);
        /* wider gaps than tables.h's at the top of Serie B, so the 3rd's 15 points happen often */
        if (it % 5 == 0) for (int k = 0; k < 3; k++) t[1][k].pts = (unsigned short)(t[1][k].pts + 25);
        const uint16_t cupw = cup_winner(it, t, n);
        diff_season(it, t, n, cupw, 0);
        const int k = diff_season(it, t, n, cupw, 1);
        direct += t[1][2].pts >= t[1][3].pts + 15;
        out_skip += t[1][15].pts >= t[1][16].pts + 5;
        if (k >= 0) same++, nt_total += k;
    }
    /* forced edges: the 3rd 14 / 15 points ahead; the play-out 0 / 4 / 5 apart; the cup winner at each place of each
     * group (the Mod rule) -- each over 20 tables */
    for (unsigned it = 0; it < 20; it++) {
        make_tables(N + it, t, n);
        for (int g3 = 14; g3 <= 15; g3++)
            for (int g16 = 0; g16 <= 5; g16 += g16 ? 1 : 4) {
                for (int k = 0; k < 20; k++) t[1][k].pts = (unsigned short)(k < 2 ? 200 : 100 - 2 * k);
                t[1][2].pts = (unsigned short)(t[1][3].pts + g3);
                for (int k = 16; k < 20; k++) t[1][k].pts = (unsigned short)(66 - k);
                t[1][15].pts = (unsigned short)(t[1][16].pts + g16);
                edges++;
                if (diff_season(N + it, t, n, cup_winner(N + it, t, n), 1) < 0) printf("   (edge: 3rd +%d, play-out %d apart)\n", g3, g16);
            }
        for (int g = 0; g < 3; g++)
            for (int p = 1; p <= 20; p++) {
                edges++;
                if (diff_season(N + it, t, n, t[2 + g][p - 1].team, 1) < 0) printf("   (edge: the cup winner %d#%d)\n", g, p);
            }
    }
    printf("structural differential (forced): %ld of %u seasons the same ties (%.1f a season: home, away, legs) and moves; "
           "the 3rd straight up %ld, the play-out not held %ld; %ld forced edges\n", same, N, (double)nt_total / (same ? same : 1),
           direct, out_skip, edges);
    printf("odds (unforced, the winners' places; dplay / the spec):\n  Serie B play-off:");
    for (int p = 3; p <= 8; p++) printf(" %d %.1f/%.1f%%", p, 100.0 * win_b[0][p] / N, 100.0 * win_b[1][p] / N);
    printf("\n  Serie C play-off:");
    for (int p = 2; p <= 15; p++) printf(" %d %.1f/%.1f%%", p, 100.0 * win_c[0][p] / N, 100.0 * win_c[1][p] / N);
    printf("\n");
    const int nr = real_cases();
    printf("the real rule (C.U. 209/A, cup_finalist known): %d forced cases v the hand-written table\n", nr);
    printf("%s: Serie B / C play-offs as data v sr_dplay, %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
