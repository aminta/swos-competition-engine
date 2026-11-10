/*
 * fuzz_rules.c -- the rules library on anything its checks accept (sr_league_check, sr_season_check, the spec through
 * sr_spec_check): a league and a season built from the input, its spec, random final tables; then everything the
 * game calls at a season end -- the preview and the season end (sr_knockouts), each play-off's contest (entrants, the
 * draw, a tie), each club's play-off, the Italian rules, the final moves and the B teams. Nothing accepted may crash,
 * read or write past a buffer or hit undefined behaviour (ASan / UBSan, -fno-sanitize-recover).
 *
 * The input: 16 bytes of league / season / tables, then the spec. Built two ways:
 * - libFuzzer (Homebrew clang: make fuzz [FUZZTIME=seconds]): the corpus each real spec of tests/rules/specs behind
 *   random headers (tests/rules/fuzz_corpus.py).
 * - A seeded random driver (make test): fuzz_rules [ITERATIONS] SPEC... FIND.fz... -- each real spec under random
 *   headers and 0 .. 4 random byte changes, insertions or cuts; each .fz file of tests/rules/fuzz_regress (the
 *   fuzzer's finds, fixed 10/11) replayed as is. Built with -ftrivial-auto-var-init=pattern: a local read before it
 *   is written is a fault.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"

static sr_state st;
static sr_stage_bank bank;
static sr_promedio prom;
static sr_winners win;

static uint32_t hs;
static uint32_t hr(uint32_t n) { hs = hs * 1664525u + 1013904223u; return n ? (hs >> 8) % n : 0; }

static int32_t accepted, played;

/* one input: h[0 .. 16) the header, spec[0 .. len) */
static void one(const uint8_t *h, const uint8_t *spec, uint32_t len)
{
    static uint8_t prom_n[SR_DIVS], rel_n[SR_DIVS], lv_first[SR_DIVS], sec[8][2];
    static uint16_t sec_team[8];
    static sr_row tab[SR_DIVS][SR_CLUBS];
    sr_league lg;
    sr_season se;
    sr_levels lv;
    sr_check_error e;
    const int nd = 1 + h[0] % SR_DIVS;
    memset(&lg, 0, sizeof lg);
    memset(&se, 0, sizeof se);
    hs = (uint32_t)h[2] | (uint32_t)h[3] << 8 | (uint32_t)h[4] << 16 | (uint32_t)h[5] << 24;
    for (int d = 0; d < SR_DIVS; d++) { prom_n[d] = (uint8_t)hr(h[6] & 1 ? 256 : 4); rel_n[d] = (uint8_t)hr(h[6] & 2 ? 256 : 4); }
    lg.divisions = (uint8_t)nd;
    lg.promoted = prom_n; lg.relegated = rel_n;
    if (h[1] & 1) {                             /* the Italian rules (the check bounds the divisions) */
        lg.italy = 1; lg.groups = (uint8_t)(2 + hr(3));          /* (mostly within the league; a few past it) */
        lg.serie_b = (uint8_t)hr((uint32_t)nd + (h[6] & 4 ? 1 : 0));
        lg.group_first = (uint8_t)(nd > lg.groups ? hr((uint32_t)(nd - lg.groups) + (h[6] & 4 ? 2 : 1)) : 1);
    }
    lg.spec = len ? spec : 0;
    lg.spec_len = len;
    lg.team_file_high = (h[1] >> 1) & 1;
    if (h[1] & 4) {                             /* B teams */
        lg.seconds = (uint8_t)(1 + h[7] % 8);
        for (int i = 0; i < lg.seconds; i++) {
            sec[i][0] = (uint8_t)hr(256); sec[i][1] = (uint8_t)(hr(4) ? hr((uint32_t)nd) : 0xFF);
            sec_team[i] = (uint16_t)(hr(2) ? 0x1400 + hr((uint32_t)nd * 32) : 0);
        }
        lg.second = (const uint8_t (*)[2])sec;
        lg.second_team = h[1] & 8 ? sec_team : 0;
        lg.second_top = (uint8_t)(hr(3) ? hr((uint32_t)nd) : 0xFF);
        lg.second_flags = (uint8_t)hr(4);
        lg.second_floor = (uint8_t)(h[1] & 16 ? hr((uint32_t)nd + 2) : 0);
    }
    if (sr_league_check(&lg, &e) != SR_CHECK_OK) return;
    /* the season */
    memset(&bank, 0, sizeof bank);
    memset(&prom, 0, sizeof prom);
    memset(&win, 0, sizeof win);
    if (h[8] & 1) {
        se.bank = &bank;
        bank.used = (uint8_t)hr(16); bank.fresh = (uint8_t)hr(16);
        for (int k = 0; k < SR_STAGE_TABLES; k++) bank.n[k] = (uint8_t)hr(SR_CLUBS + 1);
        bank.kept_div = (uint8_t)hr((uint32_t)nd); bank.n_kept = (uint8_t)hr(SR_CLUBS + 1);
        for (int k = 0; k < SR_STAGE_TABLES; k++)
            for (int i = 0; i < SR_CLUBS; i++) {
                sr_row *r = &bank.r[k][i];
                r->team = (uint16_t)(0x1400 + hr((uint32_t)nd * 32)); r->glob = (uint16_t)hr(1024); r->pts = (uint16_t)hr(100);
                r->won = (uint16_t)hr(40); r->gf = (uint16_t)hr(90); r->ga = (uint16_t)hr(90);
            }
        for (int i = 0; i < SR_CLUBS; i++) bank.kept[i] = bank.r[0][i];
    }
    if (h[8] & 2) {
        se.promedio = &prom;
        prom.init = (uint8_t)hr(2); prom.n = (uint8_t)hr(SR_PROMEDIO_CLUBS + 1);
        for (int i = 0; i < SR_PROMEDIO_CLUBS; i++) {
            prom.c[i].glob = (uint16_t)hr(1024); prom.c[i].p[0] = (uint16_t)hr(120); prom.c[i].p[1] = (uint16_t)hr(120);
            prom.c[i].m[0] = (uint8_t)hr(50); prom.c[i].m[1] = (uint8_t)hr(50);
        }
    }
    if (h[8] & 4) { se.winners = &win; win.n = (uint8_t)hr(SR_WINNERS_MAX + 1); win.season = (uint8_t)hr(256); }
    se.preview = (h[8] >> 3) & 1;
    se.own_pi[0] = (uint8_t)hr(8); se.own_pi[1] = (uint8_t)hr(8);
    se.b_in_ko = (h[8] >> 4) & 1;
    se.base = (uint16_t)hr(600);
    if (h[8] & 32) {                            /* levels: groups of parallel divisions */
        int n = 0;
        for (int d = 0; d < nd; d++) if (d == 0 || hr(3)) lv_first[n++] = (uint8_t)d;
        lv.levels = (uint8_t)n; lv.first = lv_first;
        se.levels = &lv;
    }
    se.cup_winner = (uint16_t)(hr(2) ? 0x1400 + hr((uint32_t)nd * 32) : 0);
    se.cup_finalist = (uint16_t)(hr(2) ? 0x1400 + hr((uint32_t)nd * 32) : 0);
    se.uefa_final[0] = (uint16_t)(hr(2) ? 0x1400 + hr((uint32_t)nd * 32) : 0);
    se.test_held = (uint8_t)hr(2);
    if (sr_season_check(&se, &e) != SR_CHECK_OK) return;
    accepted++;
    /* the final tables */
    sr_reset(&st);
    st.t.nd = (uint8_t)nd;
    for (int d = 0; d < nd; d++) {
        const int n = h[9] & 1 ? (int)hr(SR_CLUBS + 1) : 2 + (int)hr(SR_CLUBS - 1);
        int pts = 90;
        st.t.n[d] = (uint8_t)n;
        for (int k = 0; k < n; k++) {
            sr_row *r = &st.t.t[d][k];
            pts -= (int)hr(4);
            r->team = (uint16_t)(0x1400 + d * 32 + k); r->glob = (uint16_t)(se.base + d * 32 + k);
            r->pts = (uint16_t)(pts > 0 ? pts : 0); r->won = (uint16_t)(r->pts / 3); r->gf = (uint16_t)hr(90); r->ga = (uint16_t)hr(90);
            tab[d][k] = *r;
        }
    }
    se.mgr = (uint16_t)(h[9] & 2 ? 0x1400 + hr((uint32_t)nd * 32) : 0);
    played++;
    if (lg.italy) sr_italy(&st, &lg, &se);
    sr_knockouts(&st, &lg, &se);
    for (int pi = 0; lg.spec && sr_spec_at(&lg, pi); pi++) {
        const int d0 = (int)hr((uint32_t)nd), d1 = (int)hr((uint32_t)nd), d2 = (int)hr((uint32_t)nd);
        const sr_row *tabs[3] = { tab[d0], tab[d1], tab[d2] };
        int32_t divs[3] = { d0, d1, d2 }, ns[3] = { st.t.n[d0], st.t.n[d1], st.t.n[d2] };
        uint16_t team[64], glob[64], pts[64];
        uint8_t prio[64];
        const uint8_t *sp;
        const int32_t ne = sr_contest_entrants(&st, &lg, &se, 1 + (int32_t)hr(3), divs, tabs, ns, pi, team, glob, prio, pts, &sp);
        if (ne > 1) {
            sr_contest_seed(&st, pi, team, ne);
            sr_contest_tie(&st, (int32_t)hr((uint32_t)ne), (int32_t)hr((uint32_t)ne), 1 + (int32_t)hr(3), (int32_t)hr(3), (int32_t)hr(8));
        }
        sr_group_shape_of(sr_spec_at(&lg, pi), 0);
    }
    for (int d = 0; d < nd; d++)
        for (int k = 0; k < st.t.n[d]; k += 1 + (int)hr(4)) {
            uint8_t other[9];
            int32_t no;
            sr_club_playoff(&st, &lg, &se, d, tab[d], st.t.n[d], tab[d][k].team, other, &no);
        }
    sr_final_moves(&st, &lg, se.levels, se.base);
    if (lg.seconds) sr_b_teams(&st, &lg, se.levels, se.base);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    static uint8_t buf[8192];
    if (n < 16 || n - 16 > sizeof buf) return 0;
    memcpy(buf, d + 16, n - 16);                /* (its own buffer: ASan sees a read past the spec's bytes) */
    uint8_t *spec = malloc(n - 16 ? n - 16 : 1);
    memcpy(spec, buf, n - 16);
    one(d, spec, (uint32_t)(n - 16));
    free(spec);
    return 0;
}

#ifndef FUZZ
int main(int argc, char **argv)
{
    static uint8_t s[64][4096], m[4200 + 16];
    static uint32_t len[64];
    int ns = 0;
    const int it = argc > 1 ? atoi(argv[1]) : 20000;
    int nreg = 0;
    for (int a = 2; a < argc && ns < 64; a++) {
        const size_t al = strlen(argv[a]);
        FILE *f = fopen(argv[a], "rb");
        if (!f) continue;
        if (al > 3 && !strcmp(argv[a] + al - 3, ".fz")) {   /* (a fuzzer's find, header + spec as it was: run as is) */
            static uint8_t raw[4200 + 16];
            const size_t n = fread(raw, 1, sizeof raw, f);
            fclose(f);
            LLVMFuzzerTestOneInput(raw, n);
            nreg++;
            continue;
        }
        len[ns] = (uint32_t)fread(s[ns], 1, sizeof s[ns], f);
        fclose(f);
        ns += len[ns] > 0;
    }
    if (!ns) { printf("FAIL: fuzz_rules: no spec files\n"); return 1; }
    {   /* the fuzzer's first find: Serie C in 2 .. 4 groups of 20 (sr_league_check's range) -- 4 groups wrote past
           serie_c's national-round arrays (6 seeds into 5) */
        static const uint8_t pr[SR_DIVS] = { 0, 3 }, rl[SR_DIVS] = { 3, 4 };
        for (int ng = 2; ng <= 4; ng++) {
            sr_league lg;
            sr_season se;
            memset(&lg, 0, sizeof lg); memset(&se, 0, sizeof se);
            lg.divisions = (uint8_t)(1 + ng); lg.promoted = pr; lg.relegated = rl;
            lg.italy = 1; lg.serie_b = 0; lg.group_first = 1; lg.groups = (uint8_t)ng;
            sr_reset(&st);
            st.t.nd = lg.divisions;
            for (int d = 0; d < lg.divisions; d++) {
                st.t.n[d] = 20;
                for (int k = 0; k < 20; k++) {
                    sr_row *r = &st.t.t[d][k];
                    r->team = (uint16_t)(0x1400 + d * 32 + k); r->glob = (uint16_t)(d * 32 + k);
                    r->pts = (uint16_t)(80 - 3 * k); r->won = (uint16_t)(r->pts / 3); r->gf = 40; r->ga = 30;
                }
            }
            const int32_t mv = sr_italy(&st, &lg, &se);
            if (mv != (ng == 3 ? 3 : 2)) {      /* (B's play-off and play-out; C's play-off only with 3 groups) */
                printf("FAIL: Italy with %d groups: %d moves\n", ng, (int)mv);
                return 1;
            }
        }
    }
    uint32_t r = 2026;
#define R(n) (r = r * 1103515245u + 12345u, (r >> 8) % (n))
    for (int k = 0; k < it; k++) {
        const int i = (int)R((uint32_t)ns);
        uint32_t n = len[i];
        for (int j = 0; j < 16; j++) m[j] = (uint8_t)R(256);
        memcpy(m + 16, s[i], n);
        for (int c = (int)R(5); c > 0; c--) {      /* (0: the real spec under a random header) */
            const uint32_t at = (uint32_t)R(n ? n : 1), op = (uint32_t)R(8);
            if (op < 5 || !n) { if (n) m[16 + at] = (uint8_t)R(256); }
            else if (op < 7 && n < 4200) { memmove(m + 17 + at, m + 16 + at, n - at); m[16 + at] = (uint8_t)R(256); n++; }
            else n = at;                         /* (cut) */
        }
        LLVMFuzzerTestOneInput(m, 16 + n);
    }
    printf("PASS: fuzz_rules, %d finds of the fuzzer replayed; %d random leagues / seasons / specs (%d specs, 0 .. 4 changes each): %d accepted by the checks, all played through the season end (knockouts, contests, club play-offs, Italy, final moves, B teams) without a sanitizer report\n",
           nreg, it, ns, (int)accepted);
    return 0;
}
#endif
