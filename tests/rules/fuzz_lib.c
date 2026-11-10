/* fuzz_lib: the rules library's other entry points on anything (ASan / UBSan), one per input -- byte 0 picks it:
     0 sr_cupd   the Coppa D's later rounds 0-1 from a season end (keep_place / note_arrival / make)
     1 sr_cups   a cup's entrants from the quotas
     2 sr_groups parallel groups at the season end (pg_season) and the two-group re-allocation (pg_realloc)
     3 sr_stage  calendars, tables, league_rank, ko_left / ko_tie, group sizes / draw, best placed, ko_pairs
     4 sr_pool   light tables, play-offs, regional, cross, extra, season_cross, strengths
     5 sr_reg    the registry: build, then season ends (sr_reg_check after each)
   Byte 1's low bit: 'sane' -- the shape's own fields (rounds, levels, files ...) in their ranges, so the fuzzer
   spends its time past the first refusal; the rest stays the fuzzer's.
   The arrays are the sizes the header gives (SR_CLUB_SLOTS, SR_DIVS ...), the counts at most those (the caller's
   part); every value in them is the fuzzer's. Besides no sanitizer report, each target checks what its header
   promises (sides, counts, ranges, a permutation, the registry's invariants). libFuzzer: make fuzz-lib. Without
   -DFUZZ: a seeded driver (make test), random inputs per target, plus the fuzzer's finds
   (tests/rules/fuzz_regress_lib) as they were. */
#include "swosrules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint8_t *in, *end;
static uint32_t U8(void) { return in < end ? *in++ : 0; }
static uint32_t U16(void) { const uint32_t a = U8(); return a << 8 | U8(); }
static uint32_t U32(void) { const uint32_t a = U16(); return a << 16 | U16(); }
static void BYTES(void *p, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t *)p)[i] = (uint8_t)U8(); }

#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "fuzz_lib: " __VA_ARGS__); fputc('\n', stderr); abort(); } } while (0)

static uint64_t ran[6], ok[6];
static int sane;

/* ---- 0: sr_cupd ---- */
static void t_cupd(void)
{
    static sr_cupd_end e;
    static uint8_t division[SR_CLUB_SLOTS];
    static uint16_t word[SR_CLUB_SLOTS];
    static sr_cupd_rounds out;
    uint8_t first = (uint8_t)U8(), groups = (uint8_t)U8(), rounds = (uint8_t)U8();
    if (sane) { first %= 8; groups = (uint8_t)(1 + groups % 9); rounds = (uint8_t)(2 + rounds % 7); }
    const int32_t nslots = (int32_t)(U16() % (SR_CLUB_SLOTS + 1));
    const uint32_t season = U8();
    sr_cupd_end_reset(&e);
    for (int k = (int)U8(); k > 0; k--)
        sr_cupd_keep_place(&e, first, groups, (uint16_t)U16(), (uint16_t)U16(), (int32_t)(int8_t)U8(), (int32_t)U8());
    for (int k = (int)U8() % 16; k > 0; k--) sr_cupd_note_arrival(&e, (uint16_t)U16());
    BYTES(division, sizeof division);
    if (sane) for (int i = 0; i < SR_CLUB_SLOTS; i++) if (division[i] < 0xF0) division[i] = (uint8_t)(first + division[i] % (groups + 1));
    for (int i = 0; i < SR_CLUB_SLOTS; i++) word[i] = (uint16_t)((i & 0xFF) << 8 | (i < 256 ? 144 : 145));   /* (file | ordinal << 8) */
    memset(&out, 0x55, sizeof out);
    const int32_t n = sr_cupd_make(U8() & 1 ? &e : NULL, season, rounds, first, groups, nslots, division, word, &out);
    if (n < 0) return;
    ok[0]++;
    CHECK(out.np <= SR_CUPD_PRE, "cupd: %u preliminary ties", out.np);
    const int32_t n1 = 1 << (rounds - 2);
    for (int t = 0; t < out.np + n1; t++)
        for (int s = 0; s < 2; s++) {
            const uint16_t v = out.side[2 * t + s];
            if (!v) continue;
            if ((v & 0xFF) == 0xFF) CHECK(t >= out.np && (v >> 8) < out.np, "cupd: tie %d side %d a preliminary %u", t, s, v >> 8);
            else CHECK(((v & 0xFF) == 144 || (v & 0xFF) == 145) && (uint32_t)((v & 0xFF) - 144) * 256 + (v >> 8) < (uint32_t)nslots,
                       "cupd: tie %d side %d word %04x", t, s, v);
        }
}

/* ---- 1: sr_cups ---- */
static void t_cups(void)
{
    static sr_quota q[SR_DIVS];
    static uint16_t slot[SR_CLUB_SLOTS], out[SR_CUP_CLUBS];
    static uint8_t division[SR_CLUB_SLOTS], place[SR_CLUB_SLOTS], level[SR_DIVS];
    const int32_t nq = (int32_t)(U8() % (SR_DIVS + 1));
    int32_t n = (int32_t)(U8() % (SR_CUP_CLUBS + 1));
    const int32_t nclubs = (int32_t)(U16() % (SR_CLUB_SLOTS + 1));
    const int32_t career = (int32_t)(int16_t)U16();
    const int with_place = (int)(U8() & 1);
    for (int g = 0; g < nq; g++) { q[g].division = (uint8_t)U8(); q[g].clubs = (uint8_t)U8(); q[g].byes = (uint8_t)U8(); }
    for (int i = 0; i < nclubs; i++) { slot[i] = (uint16_t)U16(); division[i] = (uint8_t)U8(); }
    if (sane) {
        int32_t tot = 0;
        for (int g = 0; g < nq; g++) {
            q[g].division = (uint8_t)(q[g].division % 6); q[g].clubs %= 24; q[g].byes = (uint8_t)(q[g].clubs ? q[g].byes % (q[g].clubs + 1) : 0);
            tot += q[g].clubs;
        }
        for (int i = 0; i < nclubs; i++) { slot[i] = (uint16_t)i; division[i] = (uint8_t)(division[i] % 7); }
        if (U8() & 1) n = tot > SR_CUP_CLUBS ? SR_CUP_CLUBS : tot;
    }
    BYTES(place, sizeof place);
    BYTES(level, sizeof level);
    const int32_t r = sr_cup_entrants(q, nq, n, nclubs, slot, division, with_place ? place : NULL, level, career, out);
    CHECK(r == 0 || r == n, "cups: %d entrants for %d", r, n);
    if (r) ok[1]++;
}

/* ---- 2: sr_groups ---- */
static void t_groups(void)
{
    static sr_pg_level lv[SR_PG_LEVELS];
    static sr_pg_club club[SR_CLUB_SLOTS];
    static int8_t old_off[SR_CLUB_SLOTS], new_off[SR_CLUB_SLOTS];
    static uint8_t entering[SR_CLUB_SLOTS], rank[SR_CLUB_SLOTS];
    static uint16_t ident[SR_CLUB_SLOTS];
    sr_pg_league pg;
    pg.levels = (uint8_t)U8();
    const int nl = pg.levels < SR_PG_LEVELS ? pg.levels : SR_PG_LEVELS;
    for (int l = 0; l < nl; l++) { lv[l].first = (uint8_t)U8(); lv[l].groups = (uint8_t)U8(); BYTES(lv[l].size, sizeof lv[l].size); lv[l].chain = (uint8_t)U8(); }
    if (sane) {
        pg.levels = (uint8_t)(1 + pg.levels % SR_PG_LEVELS);
        for (int l = 0, d = 0; l < pg.levels; l++) {
            lv[l].groups = (uint8_t)(l < 2 ? 1 : 1 + lv[l].groups % 3);
            lv[l].first = (uint8_t)d;
            d += lv[l].groups;
            for (int g = 0; g < SR_PG_GROUPS; g++) lv[l].size[g] = (uint8_t)(2 + lv[l].size[g] % 20);
            lv[l].chain &= 1;
        }
    }
    pg.level = lv;
    pg.clubs = (uint16_t)(U16() % (SR_CLUB_SLOTS + 1));
    for (int i = 0; i < pg.clubs; i++) { club[i].base = (uint8_t)U8(); BYTES(club[i].area, sizeof club[i].area); }
    pg.club = club;
    BYTES(old_off, sizeof old_off);
    BYTES(new_off, sizeof new_off);
    if (sane) for (int i = 0; i < pg.clubs; i++) {
        club[i].base = 0;
        old_off[i] = (int8_t)((uint8_t)old_off[i] % 12);
        const int mv = (int)((uint8_t)new_off[i] % 5);
        new_off[i] = (int8_t)(old_off[i] + (mv == 3 ? 1 : mv == 4 ? -1 : 0));
    }
    BYTES(entering, sizeof entering);
    const int ent = (int)(U8() & 1);
    const int32_t r = sr_pg_season(&pg, old_off, new_off, ent ? entering : NULL);
    CHECK(r == 0 || r == -1, "pg_season %d", r);
    if (r == 0) ok[2]++;
    BYTES(rank, sizeof rank);
    for (int i = 0; i < SR_CLUB_SLOTS; i++) ident[i] = (uint16_t)U16();
    const int32_t l = (int32_t)(int8_t)U8();
    const int32_t x = sr_pg_realloc(&pg, l, rank, U8() & 1 ? ident : NULL, new_off);
    CHECK(x >= -1, "pg_realloc %d", x);
}

/* ---- 3: sr_stage ---- */
static void tieb_list(uint8_t *t, int n) { BYTES(t, (size_t)n - 1); t[n - 1] = 0; }

static void t_stage(void)
{
    static sr_match m[4 * SR_STAGE_CLUBS * SR_STAGE_CLUBS / 2 + 64];
    static sr_srow tab[SR_STAGE_CLUBS], row[SR_STAGE_CLUBS];
    static uint8_t pairs[SR_STAGE_CLUBS * SR_STAGE_CLUBS], namek[SR_STAGE_CLUBS], ord[SR_STAGE_CLUBS], tieb[16],
                   deci[12], grp[SR_STAGE_CLUBS], assoc[SR_STAGE_CLUBS], size[SR_STAGE_CLUBS], out[SR_STAGE_CLUBS];
    static sr_round_size rs[16];
    const int what = (int)(U8() % 8);
    const uint32_t seed = U32();
    const int32_t win = (int32_t)(U8() % 8), draw = (int32_t)(U8() % 4), loss = (int32_t)(int8_t)U8() % 3;
    switch (what) {
    case 0: {                                   /* a calendar, then its table */
        const int32_t n = (int32_t)(int8_t)U8(), legs = (int32_t)(int8_t)U8() % 6;
        const int32_t room = (int32_t)(U16() % (sizeof m / sizeof m[0] + 1));
        const int32_t nm = sr_rr_calendar(n, legs, m, room);
        if (nm < 0) return;
        ok[3]++;
        CHECK(nm == legs * n * (n - 1) / 2, "rr_calendar %d clubs %d legs: %d matches", n, legs, nm);
        for (int i = 0; i < nm; i++) {
            CHECK(m[i].home < n && m[i].away < n && m[i].home != m[i].away, "rr_calendar match %d", i);
            m[i].played = (uint8_t)(U8() & 1); m[i].hg = (uint8_t)(U8() % 10); m[i].ag = (uint8_t)(U8() % 10);
        }
        tieb_list(tieb, sizeof tieb);
        const int32_t r = sr_rr_table(n, m, nm, win, draw, loss, tieb, seed, tab);
        if (r < 0) return;
        CHECK(r == n, "rr_table %d of %d", r, n);
        uint64_t seen = 0;
        for (int i = 0; i < n; i++) { CHECK(tab[i].club < n && !(seen >> tab[i].club & 1), "rr_table row %d club %u", i, tab[i].club); seen |= 1ull << tab[i].club; }
        return;
    }
    case 1: {                                   /* a table from any matches */
        const int32_t n = (int32_t)(U8() % (SR_STAGE_CLUBS + 2)), nm = (int32_t)(U16() % 512);
        BYTES(m, (size_t)nm * sizeof m[0]);
        tieb_list(tieb, sizeof tieb);
        const int32_t r = sr_rr_table(n, m, nm, win, draw, loss, tieb, seed, tab);
        CHECK(r == -1 || r == n, "rr_table %d of %d", r, n);
        if (r >= 0) ok[3]++;
        return;
    }
    case 2: {                                   /* league_rank */
        const int32_t n = (int32_t)(U8() % (SR_STAGE_CLUBS + 2));
        BYTES(row, sizeof row);
        for (int i = 0; i < SR_STAGE_CLUBS; i++) row[i].club = (uint8_t)i;
        BYTES(pairs, sizeof pairs);
        BYTES(namek, sizeof namek);
        tieb_list(tieb, sizeof tieb);
        const int f = (int)U8();
        const int32_t r = sr_league_rank(n, row, f & 1 ? pairs : NULL, f & 2 ? namek : NULL, win, draw, loss, tieb, seed, ord);
        if (r < 0) return;
        ok[3]++;
        CHECK(r == n, "league_rank %d of %d", r, n);
        uint64_t seen = 0;
        for (int i = 0; i < n; i++) { CHECK(ord[i] < n && !(seen >> ord[i] & 1), "league_rank ord[%d] %u", i, ord[i]); seen |= 1ull << ord[i]; }
        return;
    }
    case 3: {                                   /* ko_left, ko_tie */
        const int32_t nr = (int32_t)(U8() % 17);
        for (int i = 0; i < nr; i++) { rs[i].ties = (uint16_t)U16(); rs[i].byes = (uint16_t)U16(); }
        (void)sr_ko_left((int32_t)U32(), rs, nr);
        sr_leg leg[8];
        sr_tie_extra x;
        const int32_t nl = (int32_t)(U8() % 9);
        BYTES(leg, sizeof leg);
        BYTES(&x, sizeof x);
        tieb_list(deci, sizeof deci);
        const int32_t w = sr_ko_tie(leg, nl, &x, deci, seed);
        CHECK(w == 0 || w == 1 || w == SR_TIE_REPLAY || w == SR_TIE_OPEN, "ko_tie %d", w);
        ok[3]++;
        return;
    }
    case 4: {                                   /* group sizes and draw */
        const int32_t n = (int32_t)(int8_t)U8(), g = (int32_t)(int8_t)U8();
        BYTES(assoc, sizeof assoc);
        const int32_t s = sr_group_sizes(n, g, size);
        if (s < 0 || n > SR_STAGE_CLUBS) return;
        int tot = 0;
        for (int i = 0; i < g; i++) tot += size[i];
        CHECK(tot == n, "group_sizes %d in %d: %d", n, g, tot);
        const int with_assoc = (int)(U8() & 1);
        const clock_t t0 = clock();
        const int32_t r = sr_group_draw(n, g, with_assoc ? assoc : NULL, seed, grp);
        const double t = (double)(clock() - t0) / CLOCKS_PER_SEC;
        /* the draw's time: its search has a budget (~40 ms at -O2 on an M1 for the worst shapes found) */
        CHECK(t < 1.0, "group_draw %d clubs in %d groups: %.2f s", n, g, t);
        int most = 0;                                           /* a draw exists iff no association has more clubs
                                                                   than there are groups */
        for (int i = 0; with_assoc && i < n; i++) {
            int k = 0;
            for (int j = 0; j < n; j++) k += assoc[j] == assoc[i];
            if (k > most) most = k;
        }
        CHECK(r == (most > g ? -2 : g), "group_draw %d clubs in %d groups, an association of %d: %d", n, g, most, r);
        if (r == g) {
            ok[3]++;
            int cnt[SR_STAGE_CLUBS] = { 0 };
            for (int i = 0; i < n; i++) { CHECK(grp[i] < g, "group_draw club %d group %u", i, grp[i]); cnt[grp[i]]++; }
            for (int i = 0; i < g; i++) CHECK(cnt[i] == size[i], "group_draw group %d: %d clubs for %u", i, cnt[i], size[i]);
            if (with_assoc)
                for (int i = 0; i < n; i++)
                    for (int j = 0; j < i; j++)
                        CHECK(grp[i] != grp[j] || assoc[i] != assoc[j], "group_draw: clubs %d and %d of one association in group %u", j, i, grp[i]);
        }
        {                                                       /* swc/1.5: with protect pairs (chains too) */
            uint8_t pr[2 * SR_PROT_PAIRS];
            const int32_t np = (int32_t)(U8() % 9);
            for (int i = 0; i < 2 * np; i++) pr[i] = assoc[U8() % (n > 0 ? n : 1)];
            const clock_t t1 = clock();
            const int32_t r2 = sr_group_draw_p(n, g, assoc, pr, np, seed, grp);
            CHECK((double)(clock() - t1) / CLOCKS_PER_SEC < 1.0, "group_draw_p %d clubs in %d groups: slow", n, g);
            CHECK(r2 == g || r2 == -2 || r2 == -3, "group_draw_p %d", r2);
            if (r2 == g) {
                ok[3]++;
                for (int i = 0; i < n; i++)
                    for (int j = 0; j < i; j++) {
                        if (grp[i] != grp[j]) continue;
                        CHECK(assoc[i] != assoc[j], "group_draw_p: one association twice in group %u", grp[i]);
                        for (int k = 0; k < np; k++)
                            CHECK(!((pr[2 * k] == assoc[i] && pr[2 * k + 1] == assoc[j]) || (pr[2 * k] == assoc[j] && pr[2 * k + 1] == assoc[i])),
                                  "group_draw_p: protect pair %u-%u in group %u", pr[2 * k], pr[2 * k + 1], grp[i]);
                    }
            }
        }
        return;
    }
    case 5: {                                   /* best placed */
        static sr_srow gt[8][SR_STAGE_CLUBS];
        static sr_match gm[8][64];
        sr_group g[8];
        const int32_t ng = (int32_t)(U8() % 9);
        for (int k = 0; k < ng; k++) {
            g[k].n = (int32_t)(U8() % 9);
            g[k].nm = (int32_t)(U8() % 65);
            BYTES(gm[k], (size_t)g[k].nm * sizeof gm[k][0]);
            BYTES(gt[k], sizeof gt[k]);
            g[k].m = gm[k];
            g[k].table = gt[k];
        }
        tieb_list(tieb, sizeof tieb);
        const int32_t r = sr_best_placed(g, ng, (int32_t)(U8() % 10), win, draw, loss, tieb, seed, ord);
        CHECK(r >= -1 && r <= ng, "best_placed %d of %d", r, ng);
        if (r >= 0) ok[3]++;
        for (int i = 0; i < r; i++) CHECK(ord[i] < ng, "best_placed ord[%d] %u", i, ord[i]);
        return;
    }
    default: {                                  /* ko_pairs */
        const int32_t mode = (int32_t)(U8() % 5), n = (int32_t)(int8_t)U8();
        BYTES(assoc, sizeof assoc);
        const int32_t r = sr_ko_pairs(mode, n, U8() & 1 ? assoc : NULL, seed, out);
        CHECK(r >= -3, "ko_pairs %d", r);
        {                                                       /* swc/1.5: two keys + protect pairs */
            uint8_t av2[SR_STAGE_CLUBS], pr[2 * SR_PROT_PAIRS], o2[SR_STAGE_CLUBS];
            const int32_t np = (int32_t)(U8() % 5);
            BYTES(av2, sizeof av2);
            for (int i = 0; i < 2 * np; i++) pr[i] = U8();
            const int32_t r2 = sr_ko_pairs_x(mode, n, U8() & 1 ? av2 : NULL, assoc, pr, np, seed, o2);
            CHECK(r2 >= -3, "ko_pairs_x %d", r2);
            if (r2 > 0) {
                uint64_t s2 = 0;
                CHECK(r2 == n / 2 && n <= SR_STAGE_CLUBS, "ko_pairs_x %d of %d", r2, n);
                for (int i = 0; i < n; i++) { CHECK(o2[i] < n && !(s2 >> o2[i] & 1), "ko_pairs_x out[%d] %u", i, o2[i]); s2 |= 1ull << o2[i]; }
                for (int i = 0; i < n; i += 2) CHECK(assoc[o2[i]] != assoc[o2[i + 1]], "ko_pairs_x: one association paired");
            }
        }
        if (r < 0) return;
        ok[3]++;
        CHECK(r == n / 2 && n <= SR_STAGE_CLUBS, "ko_pairs %d of %d", r, n);
        uint64_t seen = 0;
        for (int i = 0; i < n; i++) { CHECK(out[i] < n && !(seen >> out[i] & 1), "ko_pairs out[%d] %u", i, out[i]); seen |= 1ull << out[i]; }
        return;
    }
    }
}

/* ---- 4: sr_pool ---- */
static void t_pool(void)
{
    static sr_lp_club cl[SR_LP_DIVS][SR_LP_CLUBS];
    static sr_row t[SR_LP_CLUBS];
    static uint8_t place[SR_LP_DIVS][SR_LP_CLUBS], isup[SR_LP_DIVS][SR_LP_CLUBS], urn[SR_LP_DIVS], pair[8][2];
    static uint16_t ov[8], barred[8], st[64];
    static sr_lp_rpo rpo[SR_LP_DIVS];
    static sr_lp_up up[4 * SR_LP_DIVS + 16];
    const sr_lp_club *c[SR_LP_DIVS];
    const uint8_t *pl[SR_LP_DIVS], *iu[SR_LP_DIVS];
    int32_t n[SR_LP_DIVS], upi[2 * SR_LP_DIVS], cup[SR_LP_DIVS];
    const int what = (int)(U8() % 6);
    const uint32_t seed = U32();
    const int32_t spread = (int32_t)(int8_t)U8();
    const int32_t nd = (int32_t)(int8_t)U8();
    const int32_t ndc = nd < 0 ? 0 : nd > SR_LP_DIVS ? SR_LP_DIVS : nd;
    for (int d = 0; d < ndc; d++) {
        n[d] = (int32_t)(int8_t)U8();
        const int32_t k = n[d] < 0 ? 0 : n[d] > SR_LP_CLUBS ? SR_LP_CLUBS : n[d];
        for (int i = 0; i < k; i++) { cl[d][i].ident = (uint16_t)U16(); cl[d][i].strength = (uint16_t)U16(); }
        c[d] = cl[d];
        if (n[d] > SR_LP_CLUBS) n[d] = SR_LP_CLUBS;     /* (the caller's arrays: at most a division's clubs) */
    }
    switch (what) {
    case 0: {                                   /* table + play-off + regional */
        if (!ndc) return;
        const int32_t k = n[0];
        if (sr_lp_table_sp(seed, 0, c[0], k, t, spread) != 0) return;
        ok[4]++;
        uint32_t seen = 0;
        for (int i = 0; i < k; i++) { CHECK(t[i].team < k && !(seen >> t[i].team & 1), "lp_table row %d team %u", i, t[i].team); seen |= 1u << t[i].team; }
        const int32_t p = sr_lp_playoff_b(seed, 0, t, k, c[0], spread, (int32_t)(int8_t)U8());
        CHECK(p == -1 || (p >= 1 && p <= 6 && k >= 7), "lp_playoff %d (%d clubs)", p, k);
        BYTES(&rpo[0], sizeof rpo[0]);
        const int32_t r = sr_lp_regional_sp(seed, 0, t, k, c[0], &rpo[0], spread);
        CHECK(r >= -1 && r < k, "lp_regional %d of %d", r, k);
        return;
    }
    case 1: {                                   /* season */
        const int32_t r = sr_lp_season_b(seed, nd, c, n, upi, spread, (int32_t)(int8_t)U8());
        if (r < 0) return;
        ok[4]++;
        for (int d = 0; d < nd; d++) CHECK(upi[2 * d] >= 0 && upi[2 * d] < n[d], "lp_season div %d champion %d", d, upi[2 * d]);
        return;
    }
    case 2: {                                   /* cross */
        const int32_t ne = (int32_t)(int8_t)U8(), nu = (int32_t)(int8_t)U8(), np = (int32_t)(U8() % 9);
        BYTES(urn, sizeof urn);
        BYTES(pair, sizeof pair);
        const int32_t r = sr_lp_cross_sp(seed, cl[0], ne, urn, nu, (const uint8_t (*)[2])pair, np, (int32_t)(int8_t)U8(),
                                         (int32_t)(int8_t)U8(), cup, spread);
        if (r < 0) return;
        ok[4]++;
        CHECK(r <= ne, "lp_cross %d of %d", r, ne);
        for (int i = 0; i < r; i++) CHECK(cup[i] >= 0 && cup[i] < ne, "lp_cross up[%d] %d", i, cup[i]);
        return;
    }
    case 3: {                                   /* extra */
        BYTES(place, sizeof place);
        BYTES(isup, sizeof isup);
        for (int d = 0; d < SR_LP_DIVS; d++) { pl[d] = place[d]; iu[d] = isup[d]; }
        int32_t od = -9, oi = -9, ovf = -9;
        const int32_t r = sr_lp_extra(nd, c, n, pl, iu, (uint16_t)U16(), &od, &oi, &ovf);
        if (r < 0) return;
        ok[4]++;
        CHECK(od >= 0 && od < nd && oi >= 0 && oi < n[od] && !isup[od][oi], "lp_extra %d / %d", od, oi);
        return;
    }
    case 4: {                                   /* season_cross */
        sr_lp_shape s;
        BYTES(rpo, sizeof rpo);
        BYTES(urn, sizeof urn);
        BYTES(pair, sizeof pair);
        for (int i = 0; i < 8; i++) { ov[i] = (uint16_t)U16(); barred[i] = (uint16_t)U16(); }
        s.rpo = rpo; s.urn = urn; s.nu = (int32_t)(int8_t)U8(); s.pair = (const uint8_t (*)[2])pair;
        s.np = (int32_t)(U8() % 9); s.legs1 = (int32_t)(U8() % 4); s.legs2 = (int32_t)(U8() % 4);
        s.extra = (int32_t)(int8_t)U8() % 8; s.override = ov; s.nov = (int32_t)(U8() % 9);
        s.spread = spread; s.barred = barred; s.nbarred = (int32_t)(U8() % 9);
        const int32_t r = sr_lp_season_cross(seed, nd, c, n, &s, up);
        if (r < 0) return;
        ok[4]++;
        CHECK(r <= (int32_t)(sizeof up / sizeof up[0]), "lp_season_cross %d", r);
        for (int i = 0; i < r; i++)
            CHECK(up[i].div >= 0 && up[i].div < nd && up[i].idx >= 0 && up[i].idx < n[up[i].div] && up[i].how >= SR_LP_UP_CHAMPION
                  && up[i].how <= SR_LP_UP_OVERRIDE, "lp_season_cross up[%d] %d / %d how %u", i, up[i].div, up[i].idx, up[i].how);
        for (int i = 0; i < r; i++)
            for (int j = 0; j < i; j++) CHECK(up[i].div != up[j].div || up[i].idx != up[j].idx, "lp_season_cross: a club up twice (%d, %d)", j, i);
        return;
    }
    default: {                                  /* strengths */
        const int32_t na = (int32_t)(U8() % 33), np = (int32_t)(U8() % 33);
        for (int i = 0; i < 64; i++) st[i] = (uint16_t)U16();
        (void)sr_lp_rank_strength((uint16_t)U16(), st, na, st + 32, np);
        (void)sr_lp_newcomer_strength((uint16_t)U16(), st, np);
        int32_t v2 = -9, v3 = -9;
        if (sr_lp_semis((int32_t)(int8_t)U8(), (int32_t)(int8_t)U8(), (int32_t)(int8_t)U8(), &v2, &v3) == 0) ok[4]++;
        return;
    }
    }
}

/* ---- 5: sr_reg ---- */
static void t_reg(void)
{
    static sr_registry r;
    static sr_reg_work w;
    static sr_reg_file f[SR_REG_FILES];
    static uint8_t div[SR_REG_CLUBS], nd[SR_REG_CLUBS];
    static uint16_t order[SR_REG_CLUBS];
    static sr_reg_swap sw[2 * SR_REG_CLUBS];
    const int32_t nf = (int32_t)(int8_t)U8();
    const int32_t nfc = nf < 0 ? 0 : nf > SR_REG_FILES ? SR_REG_FILES : nf;
    for (int i = 0; i < nfc; i++) {
        f[i].file = (uint8_t)U8();
        f[i].ndiv = (uint8_t)U8();
        BYTES(f[i].div, sizeof f[i].div);
        BYTES(f[i].size, sizeof f[i].size);
        f[i].pool = (uint16_t)U16();
    }
    if (sane) {                                 /* (unique file numbers, each division in one file) */
        int d = 0;
        for (int i = 0; i < nfc; i++) {
            f[i].file = (uint8_t)(100 + i);
            f[i].ndiv = (uint8_t)(f[i].ndiv % 4);
            for (int k = 0; k < f[i].ndiv; k++) { f[i].div[k] = (uint8_t)(d++ % SR_DIVS); f[i].size[k] = (uint8_t)(1 + f[i].size[k] % 40); }
            f[i].pool = (uint16_t)(i == 0 ? f[i].pool % 30 : 0);
        }
    }
    const int32_t n = (int32_t)(U16() % (SR_REG_CLUBS + 1));
    if (sane || (U8() & 1)) {                             /* (a layout that fits: clubs to divisions by size) */
        int k = 0;
        for (int i = 0; i < nfc; i++) {
            for (int d = 0; d < f[i].ndiv && d < SR_DIVS; d++) for (int j = 0; j < f[i].size[d] && k < n; j++) div[k++] = f[i].div[d];
            for (int j = 0; j < f[i].pool && k < n; j++) div[k++] = SR_REG_POOL;
        }
        while (k < n) div[k++] = SR_REG_OUT;
    } else BYTES(div, (size_t)n);
    if (sr_reg_build(&r, nf, f, n, div) != 0) return;
    ok[5]++;
    CHECK(sr_reg_check(&r) == 0, "reg_build: a registry that breaks its invariants");
    for (int i = 0; i < n; i++) {
        const uint16_t wd = sr_reg_word(&r, i);
        CHECK(wd == SR_REG_NONE || sr_reg_ident(&r, wd) == i, "reg_build: club %d's word %04x", i, wd);
    }
    for (int season = (int)(U8() % 4); season > 0; season--) {
        for (int i = 0; i < n; i++) {           /* (mostly the same division, some moves) */
            const uint32_t b = U8();
            nd[i] = b < 200 ? (uint8_t)sr_reg_div(&r, i) : (uint8_t)U8();
        }
        const int32_t norder = (int32_t)(U16() % (SR_REG_CLUBS + 1));
        for (int i = 0; i < norder; i++) order[i] = (uint16_t)U16();
        const int32_t max = (int32_t)(U16() % (2 * SR_REG_CLUBS + 1));
        const int32_t c = sr_reg_season(&r, nd, order, norder, (int32_t)(U8() & 1), &w, sw, max);
        CHECK(c >= -2 && c <= max, "reg_season %d (max %d)", c, max);
        CHECK(sr_reg_check(&r) == 0, "reg_season (%d): a registry that breaks its invariants", c);
        if (c < 0) continue;
        for (int i = 0; i < n; i++) {
            const int32_t dv = sr_reg_div(&r, i);
            CHECK(dv == nd[i], "reg_season: club %d in division %d, wanted %u", i, dv, nd[i]);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    if (n < 1) return 0;
    in = d + 1;
    end = d + n;
    const int t = d[0] % 6;
    sane = (int)(U8() & 1);
    ran[t]++;
    switch (t) {
    case 0: t_cupd(); break;
    case 1: t_cups(); break;
    case 2: t_groups(); break;
    case 3: t_stage(); break;
    case 4: t_pool(); break;
    default: t_reg(); break;
    }
    return 0;
}

#ifndef FUZZ
int main(int argc, char **argv)
{
    static uint8_t m[1 << 16];
    const int it = argc > 1 ? atoi(argv[1]) : 6000;
    int nreg = 0;
    for (int a = 2; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) continue;
        const size_t k = fread(m, 1, sizeof m, f);
        fclose(f);
        LLVMFuzzerTestOneInput(m, k);
        nreg++;
    }
    {   /* the fuzzer's find in sr_cupd_make: more Serie D clubs than 4 x the first round's ties (24 clubs, 2 ties) --
           20 preliminary ties wanted 40 sides of 24 clubs (16 read past them) and 20 winners for 4 places */
        static uint8_t division[SR_CLUB_SLOTS];
        static uint16_t word[SR_CLUB_SLOTS];
        static sr_cupd_rounds out;
        memset(division, 0xFF, sizeof division);
        for (int i = 0; i < 24; i++) { division[i] = (uint8_t)(1 + i % 3); word[i] = (uint16_t)(i << 8 | 144); }
        memset(&out, 0, sizeof out);
        const int32_t n = sr_cupd_make(NULL, 126, 3, 1, 3, 24, division, word, &out);
        int bad = n != 24 || out.np != 4, seen[24] = { 0 };
        for (int t = 0; t < out.np; t++)
            for (int s = 0; s < 2; s++) {
                const uint16_t v = out.side[2 * t + s];
                if ((v & 0xFF) != 144 || (v >> 8) >= 24 || seen[v >> 8]++) bad = 1;
            }
        int won[4] = { 0 };                     /* (round 1: the 4 preliminary winners, once each) */
        for (int s = 0; s < 4; s++) {
            const uint16_t v = out.side[2 * out.np + s];
            if ((v & 0xFF) != 0xFF || (v >> 8) >= 4 || won[v >> 8]++) bad = 1;
        }
        if (bad) { printf("FAIL: sr_cupd_make, 24 clubs for a first round of 2 ties: %d clubs, %u preliminary ties\n", (int)n, out.np); return 1; }
    }
    {   /* sr_cup_entrants: a quota's division past the levels' SR_DIVS (the Mod's cf_build: level 0), a slot past
           SR_CLUB_SLOTS (no place) -- read past level[] and place[] */
        uint8_t *level = malloc(SR_DIVS), *place = malloc(SR_CLUB_SLOTS), division[4] = { 20, 20, 20, 20 };
        const sr_quota q = { 20, 2, 0 };
        const uint16_t slot[4] = { 600, 1, 2, 3 };
        uint16_t out[SR_CUP_CLUBS];
        memset(level, 1, SR_DIVS); memset(place, 0, SR_CLUB_SLOTS);
        const int32_t r = sr_cup_entrants(&q, 1, 2, 4, slot, division, place, level, -1, out);
        free(level); free(place);
        if (r != 2) { printf("FAIL: sr_cup_entrants, a quota's division 20: %d\n", (int)r); return 1; }
    }
    {   /* sr_best_placed: a group's table naming a club outside it (refused, as sr_rr_table refuses one) */
        sr_srow t[2] = { { 0 }, { 0 } };
        sr_match mm[1] = { { 0, 0, 1, 1, 2, 0 } };
        sr_group g[1] = { { mm, 1, 2, t } };
        uint8_t o[1], tb[2] = { SR_TB_POINTS, 0 };
        t[0].club = 200; t[1].club = 1;
        if (sr_best_placed(g, 1, 0, 3, 1, 0, tb, 1, o) != -1) { printf("FAIL: sr_best_placed, club 200 in a group of 2 not refused\n"); return 1; }
    }
    {   /* sr_pg_realloc: more levels than SR_PG_LEVELS (sr_pg_season refuses them) */
        sr_pg_level *lv = calloc(SR_PG_LEVELS, sizeof *lv);
        sr_pg_club cl[1] = { { 0, { 0 } } };
        uint8_t rk[1] = { 0 };
        int8_t off[1] = { 0 };
        const sr_pg_league pg = { 9, lv, 1, cl };
        const int32_t r = sr_pg_realloc(&pg, 8, rk, NULL, off);
        free(lv);
        if (r != -1) { printf("FAIL: sr_pg_realloc, 9 levels: %d\n", (int)r); return 1; }
    }
    {   /* sr_pg_season: a club above the chain (its division before the first level's) -- counted at level 0 as
           staying, its group o - first < 0: written before occ[0] */
        const sr_pg_level lv[2] = { { 2, 1, { 20 }, 0 }, { 3, 2, { 18, 18 }, 0 } };
        const sr_pg_club cl[3] = { { 0, { 0 } }, { 0, { 0 } }, { 0, { 0x10 } } };
        const int8_t old_off[3] = { 0, 2, 3 };
        int8_t new_off[3] = { 0, 2, 3 };
        const sr_pg_league pg = { 2, lv, 3, cl };
        if (sr_pg_season(&pg, old_off, new_off, NULL) != 0 || new_off[0] != 0 || new_off[1] != 2 || new_off[2] != 3) {
            printf("FAIL: sr_pg_season, a club above the chain: %d %d %d\n", new_off[0], new_off[1], new_off[2]);
            return 1;
        }
    }
    {   /* sr_reg_build: team file 255 with 256 rows -- row 255's word 0xFFFF = SR_REG_NONE (off the files): a placed
           club read as off the files, the registry's invariants broken (sr_reg_check) */
        static sr_registry rg;
        static uint8_t dv[256];
        sr_reg_file f[1];
        memset(f, 0, sizeof f);
        f[0].file = 255; f[0].ndiv = 1; f[0].div[0] = 0; f[0].size[0] = 255; f[0].pool = 1;
        memset(dv, 0, 255); dv[255] = SR_REG_POOL;
        if (sr_reg_build(&rg, 1, f, 256, dv) != -1) { printf("FAIL: sr_reg_build, team file 255 not refused\n"); return 1; }
        f[0].file = 254;
        if (sr_reg_build(&rg, 1, f, 256, dv) != 0 || sr_reg_check(&rg) != 0) { printf("FAIL: sr_reg_build, team file 254\n"); return 1; }
    }
    {   /* sr_ko_pairs: a draw that exists refused -- 64 clubs, 32 of one association (a pairing exists whenever no
           class holds more than half; refused from 21 of 64 on some seeds, always from 30): the search spent its
           budget and answered -2; seeded, 16 + 16 of one class in a round of 32 pairs, the same */
        uint8_t av[64], out[64];
        for (int i = 0; i < 64; i++) av[i] = (uint8_t)(i < 32 ? 0 : i);
        for (int mode = SR_PAIR_DRAW; mode <= SR_PAIR_SEEDED; mode++) {
            if (mode == SR_PAIR_SEEDED) for (int i = 0; i < 64; i++) av[i] = (uint8_t)(i % 32 < 16 ? 0 : i);
            const int32_t r = sr_ko_pairs(mode, 64, av, 7, out);
            int bad = r != 32;
            for (int k = 0; !bad && k < 64; k += 2) bad = av[out[k]] == av[out[k + 1]];
            if (bad) { printf("FAIL: sr_ko_pairs mode %d, a pairing that exists: %d\n", mode, (int)r); return 1; }
        }
        for (int i = 0; i < 64; i++) av[i] = (uint8_t)(i < 33 ? 0 : i);   /* (33 of 64: none, at once) */
        if (sr_ko_pairs(SR_PAIR_DRAW, 64, av, 7, out) != -2) { printf("FAIL: sr_ko_pairs, 33 of one class of 64\n"); return 1; }
    }
    {   /* sr_group_draw: a draw that exists refused (the fuzzer's: 59 clubs in 22 groups, 21 of association 255) --
           the search spent its 200000 steps (0.8 s on an M1, 50 M steps found one) and answered -2. Now the witness
           (gking) shows one at once; the fuzzer's slow shapes are in fuzz_regress_lib (gdraw_*.fz). */
        static const uint8_t av[59] = { 192, 3, 0, 160, 10, 126, 4, 2, 10, 0, 57, 252, 2, 10, 0, 255, 252, 133, 0, 7, 39,
            212, 21, 1, 255, 117, 11, 247, 110, 1, 0, 0, 0, 255, 255, 8, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            65, 255, 255, 255, 255, 255, 255, 255, 1, 33, 1, 241, 1 };
        uint8_t gr[64], a2[64];
        if (sr_group_draw(59, 22, av, 84214021u, gr) != 22) { printf("FAIL: sr_group_draw, a draw that exists refused\n"); return 1; }
        for (int i = 0; i < 59; i++)
            for (int j = 0; j < i; j++)
                if (gr[i] == gr[j] && av[i] == av[j]) { printf("FAIL: sr_group_draw, one association twice in a group\n"); return 1; }
        /* 64 clubs in 30 groups (the last pot's 4 in groups 0..3): association 7 = 29 of pot 1 and one of the last
           pot, a club in every group -- exists (pot 1's other club in that larger group); one more of it: none */
        for (int i = 0; i < 64; i++) a2[i] = (uint8_t)(i >= 30 && i < 59 ? 7 : 100 + i);
        a2[61] = 7;
        if (sr_group_draw(64, 30, a2, 1u, gr) != 30 || gr[61] > 3) { printf("FAIL: sr_group_draw, 30 of one association in 30 groups\n"); return 1; }
        a2[0] = 7;
        if (sr_group_draw(64, 30, a2, 1u, gr) != -2) { printf("FAIL: sr_group_draw, 31 of one association in 30 groups\n"); return 1; }
    }
    uint32_t r = 2026;
    for (int k = 0; k < it; k++) {
        const size_t len = 1 + (size_t)((r = r * 1103515245u + 12345u) >> 8) % 8192;
        for (size_t i = 0; i < len; i++) m[i] = (uint8_t)((r = r * 1103515245u + 12345u) >> 16);
        m[0] = (uint8_t)(k % 6);
        LLVMFuzzerTestOneInput(m, len);
    }
    printf("PASS: fuzz_lib, %d finds of the fuzzer replayed; %d random inputs: cupd %llu/%llu, cups %llu/%llu, groups %llu/%llu, stage %llu/%llu, pool %llu/%llu, reg %llu/%llu (accepted / run), no sanitizer report, every promise held\n",
           nreg, it, (unsigned long long)ok[0], (unsigned long long)ran[0], (unsigned long long)ok[1], (unsigned long long)ran[1],
           (unsigned long long)ok[2], (unsigned long long)ran[2], (unsigned long long)ok[3], (unsigned long long)ran[3],
           (unsigned long long)ok[4], (unsigned long long)ran[4], (unsigned long long)ok[5], (unsigned long long)ran[5]);
    return 0;
}
#endif
