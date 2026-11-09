/*
 * s2ko.c -- 42-s2ko: the Coppa Italia and the Supercoppa on the rules library (sr_kn.c), host-side.
 *
 * - The 2026-27 numbers 1-44 out of the real 2025-26 season end (C.U. 253 3.1, as applied: tests/rules/
 *   ita_cups_2526.json) = the real ones; the Supercoppa's two (Inter, and Lazio the cup finalist: Inter holds both).
 * - The 2026-27 bracket with the real numbers and its field swap: every tie played so far (pairs and hosts) and the
 *   round of 16's draw; the rest played out at random under the hosts' rules.
 * - The substitutions forced: a cup holder from mid-table (the 8th drops to the 9-20 group), the Coppa Italia Serie
 *   C winner relegated (the next of the national play-off's 2nd round by place, points per match), the play-out won
 *   by the 17th (the 16th, relegated, is not taken); every other substitution (the holder 2nd / promoted / in B,
 *   the Coppa C winner a runner-up / promoted / a group winner) and every ref's 'unknown' fallback (MC Q5).
 * - 10^5 random seasons of random lists, records and eligibility: the numbers' invariants; random brackets: the
 *   hosts' rules, one tie per club and round, one winner.
 * Inputs: $(OUT)/s2ko_gen.h (tests/rules/s2ko.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "swosrules.h"
#include "s2ko_gen.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int club(const char *n)
{
    for (int i = 0; i < S2_NCLUBS; i++)
        if (!strcmp(s2_name[i], n)) return i;
    printf("  no club %s\n", n);
    exit(2);
}

static sr_kn_work work;

/* ---- the real season ---- */
typedef struct { int real, checked, mism; uint32_t rng; } RealCtx;

static int32_t play_real(void *vc, int32_t r, int32_t t, const sr_kn_tie *o)
{
    RealCtx *c = vc;
    (void)t;
    for (int i = 0; i < S2_NTIES; i++) {
        const uint16_t *x = s2_ties[i];
        if (x[0] != r + 1 || !((x[1] == o->home && x[2] == o->away) || (x[1] == o->away && x[2] == o->home))) continue;
        c->checked++;
        if (x[1] != o->home) c->mism++;
        if (x[3] != 0xFFFF) { c->real++; return x[3] == o->away; }
        break;
    }
    c->rng = c->rng * 1103515245u + 12345u;
    return (c->rng >> 16) & 1;
}

/* the hosts' rules on any played bracket: the lower number at home (in two legs, away first), the final neutral */
static int hosts_ok(const sr_kn_round *rd, int nr, const sr_kn_tie *out, const uint8_t *swap)
{
    int bad = 0;
    for (int r = 0; r < nr; r++)
        for (int t = 0; t < rd[r].ties; t++) {
            const sr_kn_tie *o = &out[rd[r].first + t];
            const int sw = swap && swap[rd[r].first + t];
            if (rd[r].home == SR_KN_LOWER) bad += sw ? o->nhome < o->naway : o->nhome > o->naway;
            if (rd[r].home == SR_KN_LOWER_2ND) bad += !(o->flags & SR_KN_F_2ND) || o->nhome < o->naway;
            if (rd[r].home == SR_KN_NEUTRAL) bad += !(o->flags & SR_KN_F_NEUTRAL);
        }
    return bad;
}

/* every club once a round at most, every round's clubs = the entrants + the previous winners, one winner */
static int bracket_ok(const sr_kn_round *rd, int nr, const sr_kn_tie *out, int nn)
{
    static uint8_t in[S2_NCLUBS + SR_KN_CLUBS], gone[S2_NCLUBS + SR_KN_CLUBS];
    int bad = 0, alive = nn;
    memset(gone, 0, sizeof gone);
    for (int r = 0; r < nr; r++) {
        memset(in, 0, sizeof in);
        for (int t = 0; t < rd[r].ties; t++) {
            const sr_kn_tie *o = &out[rd[r].first + t];
            bad += in[o->home]++ + in[o->away]++ + gone[o->home] + gone[o->away] + (o->home == o->away);
            bad += o->winner != o->home && o->winner != o->away;
            gone[o->winner == o->home ? o->away : o->home] = 1;
            alive--;
        }
    }
    return bad + (alive != 1);
}

static void real_season(void)
{
    sr_kn_list lists[S2_NLISTS];
    uint16_t num[SR_KN_NUMBERS];
    uint32_t rng = 1;
    sr_kn_tie out[64];
    memcpy(lists, s2_lists0, sizeof lists);
    int n = sr_kn_number(s2_ci_groups, S2_CI_NG, lists, s2_rec, s2_elig, &rng, &work, num, SR_KN_NUMBERS), same = n == S2_CI_N;
    for (int i = 0; same && i < n; i++)
        if (num[i] != s2_ci_first[i]) {
            printf("  number %d: %s, the real %s\n", i + 1, s2_name[num[i]], s2_name[s2_ci_first[i]]);
            same = 0;
        }
    CHECK(same, "Coppa Italia numbers 1-%d from the 2025-26 season end (%d)", S2_CI_N, n);
    printf("%s: Coppa Italia 2026-27 numbers 1-44 from the real 2025-26 season end = the real ones (C.U. 253 3.1)\n", same ? "PASS" : "FAIL");
    n = sr_kn_number(s2_sc_groups, S2_SC_NG, lists, s2_rec, s2_elig, &rng, &work, num, SR_KN_NUMBERS);
    same = n == 2 && num[0] == s2_sc_first[0] && num[1] == s2_sc_first[1];
    CHECK(same, "Supercoppa: %s v %s", n == 2 ? s2_name[num[0]] : "?", n == 2 ? s2_name[num[1]] : "?");
    printf("%s: Supercoppa 2026-27 = Inter (champion) v Lazio (the cup finalist: Inter holds both; C.U. 43)\n", same ? "PASS" : "FAIL");

    RealCtx c = { 0, 0, 0, 7 };
    int w = sr_kn_bracket(s2_ci_rounds, S2_CI_NR, s2_ci_pairs, s2_ci_first, S2_CI_N, s2_ci_swap, &rng, play_real, &c, out);
    int hb = hosts_ok(s2_ci_rounds, S2_CI_NR, out, s2_ci_swap), bb = bracket_ok(s2_ci_rounds, S2_CI_NR, out, S2_CI_N);
    int ok = w >= 0 && c.checked == S2_NTIES && !c.mism && !hb && !bb;
    CHECK(ok, "the 2026-27 bracket: %d of %d real ties found, %d hosts wrong, rules %d / %d", c.checked, S2_NTIES, c.mism, hb, bb);
    printf("%s: Coppa Italia 2026-27 bracket (Allegato 1): all %d real ties so far (%d played, the round of 16's draw), their hosts, Palermo (25) - Lecce (17) swapped\n",
           ok ? "PASS" : "FAIL", c.checked, c.real);
    RealCtx c2 = { 0, 0, 0, 7 };                    /* (control: without the swap, that one tie's host differs) */
    sr_kn_bracket(s2_ci_rounds, S2_CI_NR, s2_ci_pairs, s2_ci_first, S2_CI_N, NULL, &rng, play_real, &c2, out);
    CHECK(c2.mism == 1, "without the swap: %d hosts differ", c2.mism);
    printf("%s: control: without the first season's swap exactly one real host differs (the lower number, Lecce, at home)\n", c2.mism == 1 ? "PASS" : "FAIL");
}

/* ---- forced substitutions ---- */
static int ref(const char *r)
{
    for (int i = 0; i < S2_NLISTS; i++)
        if (!strcmp(s2_lref[i], r)) return i;
    printf("  no list %s\n", r);
    exit(2);
}

static void forced(void)
{
    sr_kn_list lists[S2_NLISTS];
    uint16_t num[SR_KN_NUMBERS], holder, bari, sal;
    uint8_t el[S2_NCLUBS];
    uint32_t rng = 3;
    int n, ok;

    memcpy(lists, s2_lists0, sizeof lists);         /* a holder from mid-table: Torino (12th) */
    holder = (uint16_t)club("torino");
    lists[ref("ita.coppa:winner")].club = &holder;
    n = sr_kn_number(s2_ci_groups, S2_CI_NG, lists, s2_rec, s2_elig, &rng, &work, num, SR_KN_NUMBERS);
    ok = n == 44 && num[0] == holder && num[1] == club("inter") && num[7] == club("atalanta") && num[8] == club("bologna")
         && num[9] == club("lazio");
    CHECK(ok, "holder Torino: 1 %s, 8 %s, 9 %s", s2_name[num[0]], s2_name[num[7]], s2_name[num[8]]);
    printf("%s: forced: the cup holder 12th in Serie A -- it is 1, the champion 2, the 8th drops to 9 (C.U. 253 3.1 b.2)\n", ok ? "PASS" : "FAIL");

    memcpy(lists, s2_lists0, sizeof lists);         /* the Coppa C winner relegated (not in this season's leagues) */
    memcpy(el, s2_elig, sizeof el);
    el[club("potenza")] = 0;
    sal = (uint16_t)club("salernitana");
    n = sr_kn_number(s2_ci_groups, S2_CI_NG, lists, s2_rec, el, &rng, &work, num, SR_KN_NUMBERS);
    ok = n == 44 && num[43] == sal;
    CHECK(ok, "Coppa C winner relegated: 44 %s", n == 44 ? s2_name[num[43]] : "?");
    printf("%s: forced: the Coppa Italia Serie C winner relegated -- 44 = the best placed of the national play-off's 2nd round left (Salernitana, 3rd)\n", ok ? "PASS" : "FAIL");

    memcpy(lists, s2_lists0, sizeof lists);         /* the play-out won by the 17th, Bari; the 16th relegated */
    bari = (uint16_t)club("bari");
    lists[ref("ita.b/playout:winner")].club = &bari;
    n = sr_kn_number(s2_ci_groups, S2_CI_NG, lists, s2_rec, s2_elig, &rng, &work, num, SR_KN_NUMBERS);
    ok = n == 44 && num[35] == bari;
    for (int i = 0; i < n; i++) ok &= num[i] != club("sudtirol");
    CHECK(ok, "play-out won by the 17th: 36 %s", n == 44 ? s2_name[num[35]] : "?");
    printf("%s: forced: the play-out won by the 17th -- it is 36, the 16th (relegated) not numbered\n", ok ? "PASS" : "FAIL");

    memcpy(lists, s2_lists0, sizeof lists);         /* no club left for a group: -1 */
    memcpy(el, s2_elig, sizeof el);
    for (int i = 0; i < S2_NCLUBS; i++) el[i] = s2_elig[i] && s2_rec[i].place != 1;
    n = sr_kn_number(s2_ci_groups, S2_CI_NG, lists, s2_rec, el, &rng, &work, num, SR_KN_NUMBERS);
    for (int i = 0; i < S2_NCLUBS; i++) el[i] = 0;
    int n2 = sr_kn_number(s2_ci_groups, S2_CI_NG, lists, s2_rec, el, &rng, &work, num, SR_KN_NUMBERS);
    CHECK(n == -1 && n2 == -1, "short groups: %d %d", n, n2);
    printf("%s: control: a group nobody is left to fill (no first places eligible; no club at all) -- -1\n", n == -1 && n2 == -1 ? "PASS" : "FAIL");
}

/* ---- every C.U. 253 substitution and every unknown fact, forced (MC Q5 conditions 2 and 4) ---- */
typedef struct { const char *ref; const char *const *clubs; int n; } Over;   /* n < 0: the fact unknown */
static int nums(const Over *ov, int nov, const uint8_t *el, const sr_kn_group *g, int ng, uint16_t *num)
{
    static uint16_t buf[8][8];
    sr_kn_list lists[S2_NLISTS];
    uint32_t rng = 11;
    memcpy(lists, s2_lists0, sizeof lists);
    for (int i = 0; i < nov; i++) {
        sr_kn_list *l = &lists[ref(ov[i].ref)];
        l->n = ov[i].n;
        for (int j = 0; j < ov[i].n; j++) buf[i][j] = (uint16_t)club(ov[i].clubs[j]);
        if (ov[i].n > 0) l->club = buf[i];
    }
    return sr_kn_number(g, ng, lists, s2_rec, el ? el : s2_elig, &rng, &work, num, SR_KN_NUMBERS);
}

/* numbers "N=club ..." expected; the rest unchecked, but 44, all distinct */
static int expect(const uint16_t *num, int n, int want_n, const char *spec)
{
    char buf[512], *t, *save;
    int ok = n == want_n;
    for (int i = 0; ok && i < n; i++)
        for (int j = 0; j < i; j++) ok &= num[i] != num[j];
    snprintf(buf, sizeof buf, "%s", spec);
    for (t = strtok_r(buf, " ", &save); ok && t; t = strtok_r(NULL, " ", &save)) {
        char *eq = strchr(t, '=');
        int k = atoi(t);
        *eq = 0;
        if (num[k - 1] != club(eq + 1)) { printf("  number %d: %s, want %s\n", k, s2_name[num[k - 1]], eq + 1); ok = 0; }
    }
    return ok;
}

static void same_as_real(const uint16_t *num, int n, int *ok)
{
    *ok &= n == S2_CI_N;
    for (int i = 0; *ok && i < n; i++) *ok &= num[i] == s2_ci_first[i];
}

static void forced_all(void)
{
    static const char *napoli[] = { "napoli" }, *venezia[] = { "venezia" }, *palermo[] = { "palermo" },
        *catania[] = { "catania" }, *ascoli[] = { "ascoli" }, *vicenza[] = { "vicenza" },
        *up_pal[] = { "venezia", "frosinone", "palermo" }, *down_rev[] = { "pisa", "verona", "cremonese" };
    uint16_t num[SR_KN_NUMBERS];
    int n, ok;
#define CASE(text, cond) do { ok = (cond); CHECK(ok, "%s", text); printf("%s: forced: %s\n", ok ? "PASS" : "FAIL", text); } while (0)
#define CI(...) (n = nums((const Over[]){ __VA_ARGS__ }, (int)(sizeof (Over[]){ __VA_ARGS__ } / sizeof(Over)), NULL, s2_ci_groups, S2_CI_NG, num))

    /* the substitutions (C.U. 253 3.1 a-g; 2026-27 itself: the holder = the champion, a C runner-up promoted) */
    CI({ "ita.coppa:winner", napoli, 1 });
    CASE("the holder 2nd in Serie A -- 1 Napoli, 2 Inter, 8 the 8th Bologna, 9 Lazio", expect(num, n, 44, "1=napoli 2=inter 3=roma 8=bologna 9=lazio"));
    CI({ "ita.coppa:winner", venezia, 1 });
    CASE("the holder promoted from Serie B -- 1 Venezia, 2-8 Serie A 1st-7th, 9-18 its 8th-17th, 19-20 the other promoted",
         expect(num, n, 44, "1=venezia 2=inter 8=atalanta 9=bologna 18=lecce 19=frosinone 20=monza 21=cremonese"));
    CI({ "ita.coppa:winner", palermo, 1 });
    CASE("the holder staying in Serie B (not in the C.U.: the list order) -- 1 Palermo, 9-20 full, the 3rd promoted Monza 25",
         expect(num, n, 44, "1=palermo 19=venezia 20=frosinone 24=catanzaro 25=monza 36=sudtirol"));
    CI({ "ita.coppa-c:winner", catania, 1 });
    CASE("the Coppa C winner a C runner-up, a runner-up promoted too -- two of the play-off's 2nd round: 43 Ravenna, 44 Salernitana",
         expect(num, n, 44, "41=catania 42=unionbrescia 43=ravenna 44=salernitana"));
    CI({ "ita.coppa-c:winner", ascoli, 1 });
    CASE("the Coppa C winner promoted through the play-off -- 40 Ascoli, 44 Salernitana", expect(num, n, 44, "40=ascoli 43=ravenna 44=salernitana"));
    CI({ "ita.coppa-c:winner", vicenza, 1 });
    CASE("the Coppa C winner a C group winner -- 37 Vicenza, 44 Salernitana", expect(num, n, 44, "37=vicenza 44=salernitana"));

    /* every ref's 'unknown' (the spec's fallbacks) */
    CI({ "ita.coppa:winner", NULL, -1 });
    ok = 1; same_as_real(num, n, &ok);
    CASE("the holder unknown -> the champion (= 2026-27: Inter holds both): the real numbers", ok);
    CI({ "ita.b:up", up_pal, 3 });
    ok = expect(num, n, 44, "20=palermo");
    CI({ "ita.b:up", NULL, -1 }, { "ita.b/playoff:finalist", palermo, 1 });
    CASE("the promoted unknown -> Serie B 1st-3rd by place (known: the play-off winner Palermo 20; unknown: Monza 20)",
         ok && expect(num, n, 44, "18=venezia 19=frosinone 20=monza 24=palermo"));
    CI({ "ita.a:down", down_rev, 3 });
    ok = expect(num, n, 44, "21=pisa 23=cremonese");
    CI({ "ita.a:down", NULL, -1 });
    CASE("the relegated unknown -> Serie A 18th-20th by place", ok && expect(num, n, 44, "21=cremonese 22=verona 23=pisa"));
    CI({ "ita.b/playoff:finalist", NULL, -1 });
    CASE("the play-off final's loser unknown -> the best placed non-promoted of B 3rd-8th: 24 Palermo (4th)",
         expect(num, n, 44, "24=palermo 25=catanzaro"));
    CI({ "ita.b/playout:winner", NULL, -1 });
    ok = 1; same_as_real(num, n, &ok);
    CASE("the play-out winner unknown -> the 16th (Sudtirol, = 2026-27)", ok && expect(num, n, 44, "36=sudtirol"));
    CI({ "ita.c-po:winner", NULL, -1 });
    CASE("the C play-off winner unknown -> 'none': its group's else, the 2nd round's best (Ascoli, 2nd, 2.14 points per match)",
         expect(num, n, 44, "40=ascoli 41=catania 42=unionbrescia 43=ravenna"));
    CI({ "ita.c-po:round4", NULL, -1 });
    ok = 1; same_as_real(num, n, &ok);
    CASE("the play-off's 2nd round unknown -> C places 2-10 merged, place then points per match: Ravenna 43 (= 2026-27)", ok);
    CI({ "ita.c-po:round4", NULL, -1 }, { "ita.coppa-c:winner", NULL, -1 });
    CASE("the 2nd round and the Coppa C winner unknown -> 44 the next 3rd, Salernitana", expect(num, n, 44, "43=ravenna 44=salernitana"));
    CI({ "ita.coppa-c:winner", NULL, -1 });
    CASE("the Coppa C winner unknown (C.U. 253: 'not played') -> the 2nd round's best left: 44 Salernitana", expect(num, n, 44, "44=salernitana"));
#undef CI
#define SC(...) (n = nums((const Over[]){ __VA_ARGS__ }, (int)(sizeof (Over[]){ __VA_ARGS__ } / sizeof(Over)), NULL, s2_sc_groups, S2_SC_NG, num))
    SC({ "sc:ita.coppa:winner", NULL, -1 });
    CASE("Supercoppa: the cup winner unknown -> Serie A's 2nd: Inter v Napoli", expect(num, n, 2, "1=inter 2=napoli"));
    SC({ "sc:ita.coppa:finalist", NULL, -1 });
    CASE("Supercoppa: the finalist unknown, the winner the champion -> Inter v Napoli", expect(num, n, 2, "1=inter 2=napoli"));
    SC({ "sc:ita.coppa:winner", NULL, -1 }, { "sc:ita.coppa:finalist", NULL, -1 });
    CASE("Supercoppa: both unknown -> Inter v Napoli", expect(num, n, 2, "1=inter 2=napoli"));
#undef SC

    /* the library alone: a chain of unknowns, a loop cut, 'none' */
    {
        static const uint16_t a[] = { 1 }, b[] = { 2 }, cc[] = { 3, 4 };
        sr_kn_list l[5] = { { a, -1, 1 }, { b, -1, 2 }, { cc, 2, -1 }, { a, -1, 4 }, { b, -1, 3 } };
        sr_kn_group g1 = { 2, SR_KN_LIST, SR_KN_LIST, 1, 0, { 0 }, { 0 } }, g2 = { 1, SR_KN_LIST, SR_KN_LIST, 1, 1, { 3 }, { 2 } };
        uint8_t el[SR_KN_CLUBS];
        uint32_t rng = 1;
        memset(el, 1, sizeof el);
        n = sr_kn_number(&g1, 1, l, s2_rec, el, &rng, &work, num, 8);
        int n2 = sr_kn_number(&g2, 1, l, s2_rec, el, &rng, &work, num + 4, 4);
        CASE("library: unknown -> unknown -> a list (3, 4); a loop of unknowns ends as none, the else fills (3)",
             n == 2 && num[0] == 3 && num[1] == 4 && n2 == 1 && num[4] == 3);
    }
#undef CASE
}

/* ---- random seasons ---- */
static uint32_t rs = 12345;
static uint32_t rnd(uint32_t n) { rs = rs * 1664525u + 1013904223u; return (uint32_t)(((uint64_t)(rs >> 8) * n) >> 24); }

static int32_t play_rand(void *vc, int32_t r, int32_t t, const sr_kn_tie *o)
{
    (void)vc; (void)r; (void)t; (void)o;
    return (int32_t)rnd(2);
}

static void random_seasons(int seasons)
{
    static uint16_t pool[16][24];
    static sr_kn_rec rec[SR_KN_CLUBS];
    static uint8_t el[SR_KN_CLUBS], inl[SR_KN_CLUBS];
    sr_kn_list lists[16];
    sr_kn_group g[6];
    uint16_t num[SR_KN_NUMBERS];
    sr_kn_tie out[64];
    int bad_n = 0, bad_b = 0, shorts = 0, full = 0;
    for (int s = 0; s < seasons; s++) {
        uint32_t rng = (uint32_t)s * 2654435761u;
        int total = 0;
        for (int k = 0; k < SR_KN_CLUBS; k++) {
            rec[k].pts = (uint16_t)rnd(100); rec[k].pld = (uint16_t)(30 + rnd(9)); rec[k].won = (uint16_t)rnd(30);
            rec[k].gf = (uint16_t)rnd(90); rec[k].place = (uint8_t)(1 + rnd(20)); el[k] = rnd(10) != 0;
        }
        for (int i = 0; i < 16; i++) {
            lists[i].n = (int32_t)rnd(24);
            for (int j = 0; j < lists[i].n; j++) pool[i][j] = (uint16_t)rnd(160);
            lists[i].club = pool[i];
            lists[i].unknown = SR_KN_NONE;
        }
        int ng = 1 + (int)rnd(6);
        for (int i = 0; i < ng; i++) {
            g[i].take = (uint8_t)(1 + rnd(8)); g[i].order = (uint8_t)rnd(2); g[i].else_order = (uint8_t)(rnd(2) * 2);
            g[i].nfrom = (uint8_t)(1 + rnd(3)); g[i].nelse = (uint8_t)rnd(3);
            for (int j = 0; j < 8; j++) { g[i].from[j] = (uint8_t)rnd(16); g[i].els[j] = (uint8_t)rnd(16); }
            total += g[i].take;
        }
        int n = sr_kn_number(g, ng, lists, rec, el, &rng, &work, num, SR_KN_NUMBERS);
        if (n < 0) { shorts++; continue; }
        full++;
        bad_n += n != total;
        for (int i = 0, at = 0; i < ng && at < n; i++) {     /* each from its group's lists, eligible, once, in order */
            memset(inl, 0, sizeof inl);
            for (int j = 0; j < g[i].nfrom; j++) for (int q = 0; q < lists[g[i].from[j]].n; q++) inl[lists[g[i].from[j]].club[q]] |= 1;
            for (int j = 0; j < g[i].nelse; j++) for (int q = 0; q < lists[g[i].els[j]].n; q++) inl[lists[g[i].els[j]].club[q]] |= 2;
            int prev = -1, inelse = 0;
            for (int k = 0; k < g[i].take; k++, at++) {
                const uint16_t c = num[at];
                bad_n += !el[c] || !inl[c];
                for (int q = 0; q < at; q++) bad_n += num[q] == c;
                if (!(inl[c] & 1)) inelse = 1;
                bad_n += inelse && (inl[c] & 1) && prev >= 0;   /* (the 'else' only after the 'from' ran out) */
                if (prev >= 0 && !inelse && g[i].order == SR_KN_RECORD)
                    bad_n += (uint32_t)rec[c].pts * rec[prev].pld > (uint32_t)rec[prev].pts * rec[c].pld;
                prev = c;
            }
        }
        if (n >= 2 && !(s % 4)) {                           /* a random bracket of n: the tennis pairs, lower at home */
            int m = 1;
            while (m * 2 <= n) m *= 2;
            sr_kn_round rd[8];
            memset(rd, 0, sizeof rd);
            sr_kn_pair pr[64];
            int nr = 0, np = 0, pre = n - m;            /* (a preliminary round brings n down to m) */
            if (pre) { rd[nr].ties = (uint16_t)pre; rd[nr].first = 0; rd[nr].legs = 1; rd[nr].home = SR_KN_LOWER; nr++;
                       for (int t = 0; t < pre; t++, np++) { pr[np].a = (uint16_t)(m - pre + 1 + t); pr[np].b = (uint16_t)(n - t); } }
            for (int sz = m, r0 = 0; sz >= 2; sz /= 2, r0++) {
                rd[nr].ties = (uint16_t)(sz / 2); rd[nr].first = (uint16_t)np; rd[nr].legs = sz == 4 ? 2 : 1;
                rd[nr].home = sz == 2 ? SR_KN_NEUTRAL : sz == 4 ? SR_KN_LOWER_2ND : SR_KN_LOWER;
                for (int t = 0; t < sz / 2; t++, np++) {
                    if (sz == m) {
                        const int a = t + 1, b = m - t;     /* (a slot past m - pre: its preliminary's winner) */
                        pr[np].a = pre && a > m - pre ? SR_KN_WIN(0, a - (m - pre + 1)) : (uint16_t)a;
                        pr[np].b = pre && b > m - pre ? SR_KN_WIN(0, b - (m - pre + 1)) : (uint16_t)b;
                    } else { pr[np].a = SR_KN_WIN(nr - 1, 2 * t); pr[np].b = SR_KN_WIN(nr - 1, 2 * t + 1); }
                }
                nr++;
                (void)r0;
            }
            int w = sr_kn_bracket(rd, nr, pr, num, n, NULL, &rng, play_rand, NULL, out);
            bad_b += w < 0 || hosts_ok(rd, nr, out, NULL) || bracket_ok(rd, nr, out, n);
        }
    }
    CHECK(!bad_n && !bad_b, "random: numbers %d, brackets %d", bad_n, bad_b);
    printf("%s: random: %d seasons of random lists, records, eligibility (%d numbered in full, %d short = -1) -- every club eligible, once, from its group's lists, the 'else' only after them, the record's order; a quarter played as a bracket of n with a preliminary: hosts, one tie a round, one winner (%d bad)\n",
           !bad_n && !bad_b ? "PASS" : "FAIL", seasons, full, shorts, bad_n + bad_b);
}

/* ---- 42-s2ko M2: the Coppa Italia Serie C ---- */
static int32_t play_c(void *vc, int32_t r, int32_t t, const sr_kn_tie *o)
{
    RealCtx *c = vc;
    (void)t;
    for (int i = 0; i < S2C_NRES; i++) {
        const uint16_t *x = s2c_res[i];
        if (x[0] != r + 1 || !((x[1] == o->home && x[2] == o->away) || (x[1] == o->away && x[2] == o->home))) continue;
        c->checked++;
        if (x[1] != o->home) c->mism++;
        c->real++;
        return x[3] == o->away;
    }
    c->rng = c->rng * 1103515245u + 12345u;
    return (c->rng >> 16) & 1;
}

static void coppa_c(void)
{
    uint8_t zone[64], sz[8];
    sr_kn_tie out[64];
    uint32_t rng = 5;
    int same = 0, bad = 0;
    char miss[200] = "";
    /* the zones by proximity on the real 2026-27 clubs, against the real groups (C.U. 1/CIT) */
    int rc = sr_kn_zones(s2c_geo, s2c_r1, 56, s2c_centre, S2C_NZ, 14, &work, zone);
    memset(sz, 0, sizeof sz);
    for (int i = 0; i < 56; i++) {
        sz[zone[i]]++;
        if (zone[i] == s2c_r1zone[i]) same++;
        else { strcat(miss, " "); strcat(miss, s2_name[s2c_r1[i]]); }
    }
    for (int z = 0; z < S2C_NZ; z++) bad += sz[z] != 14;
    int ok = rc == 0 && !bad && same == 53 && !strcmp(miss, " forli picerno grosseto");
    CHECK(ok, "Coppa C zones: %d of 56 in their real group, misplaced:%s", same, miss);
    printf("%s: Coppa Italia Serie C 2026-27: the groups by proximity -- %d of 56 clubs in their real group (C.U. 1/CIT; not:%s), 4 x 14\n",
           ok ? "PASS" : "FAIL", same, miss);
    rc = sr_kn_zones(s2c_geo, s2c_seed, 4, s2c_centre, S2C_NZ, 1, &work, zone);
    ok = rc == 0;
    for (int i = 0; i < 4; i++) ok &= zone[i] == s2c_seedzone[i];
    CHECK(ok, "Coppa C seeds' groups");
    printf("%s: Coppa Italia Serie C 2026-27: the 4 seeds (the Coppa Italia's Serie C clubs) each to its real group by proximity\n", ok ? "PASS" : "FAIL");

    /* season 1: the real draw (rounds 1-3 and the quarter-finals) with the hosts as played, then drawn semi-finals and
       final over two legs */
    RealCtx c = { 0, 0, 0, 9 };
    int w = sr_kn_cup(s2c_rounds, S2C_NR, s2c_pairs, s2c_num, S2C_NNUM, NULL, NULL, &rng, play_c, &c, &work, out);
    int bb = bracket_ok(s2c_rounds, S2C_NR, out, 60);
    for (int t = 0; t < 3; t++) bb += out[56 + t].legs != 2;
    ok = w >= 0 && c.checked == S2C_NRES && !c.mism && !bb;
    CHECK(ok, "Coppa C 2026-27: %d of %d real ties found, %d hosts wrong, rules %d", c.checked, S2C_NRES, c.mism, bb);
    printf("%s: Coppa Italia Serie C 2026-27: all %d real ties played so far (rounds 1-2, last 16) and their hosts as played (three field swaps, art. 5); the quarter-finals as drawn, then drawn two-leg semi-finals and final, one winner\n",
           ok ? "PASS" : "FAIL", c.checked);
}

/* later seasons at random: 60 clubs anywhere in Italy, 4 seeds -- zones of 14 (and 1 seed each), rounds 1, 2 and the
   last 16 within a zone, the seeds at home against non-seeds, then open draws; every club once a round, one winner */
static void coppa_c_random(int seasons)
{
    static sr_kn_geo geo[SR_KN_CLUBS];
    sr_kn_round rd[6] = {
        { 28, 0, 1, SR_KN_DRAW, SR_KN_PAIR_ZONE, 0, 0, 56 }, { 16, 28, 1, SR_KN_JOINER, SR_KN_PAIR_ZONE, 0, 56, 4 },
        { 8, 44, 1, SR_KN_DRAW, SR_KN_PAIR_ZONE, 0, 0, 0 }, { 4, 52, 1, SR_KN_DRAW, SR_KN_PAIR_DRAW, 0, 0, 0 },
        { 2, 56, 2, SR_KN_DRAW, SR_KN_PAIR_DRAW, 0, 0, 0 }, { 1, 58, 2, SR_KN_DRAW, SR_KN_PAIR_DRAW, 0, 0, 0 } };
    sr_kn_entry join[60];
    sr_kn_tie out[64];
    uint16_t cl[60];
    uint8_t zone[60], isseed[SR_KN_CLUBS];
    int bad = 0, fails0 = 0;
    for (int s = 0; s < seasons; s++) {
        uint32_t rng = (uint32_t)s * 2246822519u + 1;
        int b = 0;
        for (int i = 0; i < 60; i++) {
            cl[i] = (uint16_t)(i * 7 + (s % 7));
            geo[cl[i]].lat = (int16_t)(3660 + rnd(1100)); geo[cl[i]].lon = (int16_t)(700 + rnd(1150));
            isseed[cl[i]] = i >= 56;
        }
        if (sr_kn_zones(geo, cl, 56, s2c_centre, 4, 14, &work, zone) || sr_kn_zones(geo, cl + 56, 4, s2c_centre, 4, 1, &work, zone + 56)) { bad++; continue; }
        int sz[4] = { 0 }, ss[4] = { 0 };
        for (int i = 0; i < 56; i++) sz[zone[i]]++;
        for (int i = 56; i < 60; i++) ss[zone[i]]++;
        for (int z = 0; z < 4; z++) b += sz[z] != 14 || ss[z] != 1;
        for (int i = 0; i < 60; i++) { join[i].club = cl[i]; join[i].zone = zone[i]; }
        int w = sr_kn_cup(rd, 6, NULL, NULL, 0, join, NULL, &rng, play_rand, NULL, &work, out);
        b += w < 0 || bracket_ok(rd, 6, out, 60);
        for (int r = 0; r < 3; r++)                 /* (a zone round: both sides of one zone) */
            for (int t = 0; t < rd[r].ties; t++) {
                const sr_kn_tie *o = &out[rd[r].first + t];
                int zh = -1, za = -1;
                for (int i = 0; i < 60; i++) { if (cl[i] == o->home) zh = zone[i]; if (cl[i] == o->away) za = zone[i]; }
                b += zh != za;
                if (r == 1) b += isseed[o->away] || (isseed[o->home] && isseed[o->away]);
            }
        int seeds_home = 0;
        for (int t = 0; t < 16; t++) seeds_home += isseed[out[28 + t].home];
        b += seeds_home != 4;
        for (int t = 0; t < 3; t++) b += out[56 + t].legs != 2;
        if (b && fails0++ < 3) printf("  random Coppa C season %d: %d bad\n", s, b);
        bad += b != 0;
    }
    CHECK(!bad, "Coppa C random: %d bad", bad);
    printf("%s: Coppa Italia Serie C, %d random seasons: zones of 14 and one seed each by proximity, rounds 1-2 and the last 16 within a zone, every seed at home v a non-seed, open draws after, two-leg semi-finals and final, one winner (%d bad)\n",
           !bad ? "PASS" : "FAIL", seasons, bad);
}

/* ---- 42-s2ko M2b: the Coppa Italia Serie D ---- */
static uint8_t d_real[8][64];                   /* (season 1: the ties with a real result) */
static int32_t play_d(void *vc, int32_t r, int32_t t, const sr_kn_tie *o)
{
    RealCtx *c = vc;
    for (int i = 0; i < S2D_NRES; i++) {
        const uint16_t *x = s2d_res[i];
        if (x[0] != r + 1 || !((x[1] == o->home && x[2] == o->away) || (x[1] == o->away && x[2] == o->home))) continue;
        c->checked++;
        if (x[1] != o->home) c->mism++;
        if (r < 8 && t < 64) d_real[r][t] = 1;
        return x[3] == o->away;
    }
    c->rng = c->rng * 1103515245u + 12345u;
    return (c->rng >> 16) & 1;
}

static double d_km(const sr_kn_geo *geo, uint16_t a, uint16_t b)
{
    const double dy = geo[a].lat - geo[b].lat, dx = (geo[a].lon - geo[b].lon) * 0.7431;
    return sqrt(dy * dy + dx * dx) * 1.112;     /* (1/100 degree of latitude = 1.112 km) */
}

/* the side away in its previous tie hosts (both alike: drawn) -- the hosts against the rule in rounds r0 .. r1 of a
   bracket whose fixed pairs are winner refs; *decided: the ties the rule decides */
static int alt_bad(const sr_kn_round *rd, int r0, int r1, const sr_kn_pair *pairs, const sr_kn_tie *out, int *decided, int real_only)
{
    int bad = 0;
    for (int r = r0; r <= r1; r++)
        for (int t = 0; t < rd[r].ties; t++) {
            const sr_kn_pair *p = &pairs[rd[r].first + t];
            const sr_kn_tie *o = &out[rd[r].first + t], *x = &out[rd[(p->a >> 9) & 0x3F].first + (p->a & 0x1FF)],
                            *y = &out[rd[(p->b >> 9) & 0x3F].first + (p->b & 0x1FF)];
            const int wa = x->winner == x->home, wb = y->winner == y->home;
            if (real_only && !(d_real[(p->a >> 9) & 0x3F][p->a & 0x1FF] && d_real[(p->b >> 9) & 0x3F][p->b & 0x1FF])) continue;
            if (wa == wb) continue;
            (*decided)++;
            bad += o->home != (wa ? y->winner : x->winner);
        }
    return bad;
}

static void coppa_d(void)
{
    static sr_kn_tie out[256];
    static sr_kn_pair pairs[256];
    uint32_t rng = 3;
    RealCtx c = { 0, 0, 0, 11 };
    /* season 1: the real bracket (preliminary .. quarter-finals) with the hosts as played, then drawn two-leg
       semi-finals and final */
    memset(d_real, 0, sizeof d_real);
    int w = sr_kn_cup(s2d_rounds, S2D_NR, s2d_pairs, s2d_num, 162, NULL, NULL, &rng, play_d, &c, &work, out);
    int bb = bracket_ok(s2d_rounds, S2D_NR, out, 162);
    for (int t = 0; t < 3; t++) bb += out[s2d_rounds[6].first + t].legs != 2;
    int ok = w >= 0 && c.checked == S2D_NRES && !c.mism && !bb;
    CHECK(ok, "Coppa D 2026-27: %d of %d real ties, %d hosts wrong, rules %d", c.checked, S2D_NRES, c.mism, bb);
    printf("%s: Coppa Italia Serie D 2026-27: all %d real ties played so far (preliminary, first round, round of 64) with their hosts as played; the bracket of the last 32 / 16 / quarter-finals as drawn, then drawn two-leg semi-finals and final, one winner\n",
           ok ? "PASS" : "FAIL", c.checked);
    /* the rule of alternation (art. 4) on the real last 32: where both sides' round-of-64 ties are played and one was
       at home, the other away, the club away hosts -- the LND's own schedule (TM) against the rule */
    int dec = 0, bad = alt_bad(s2d_rounds, 3, 3, s2d_pairs, out, &dec, 1);
    ok = dec >= 4 && !bad;
    CHECK(ok, "Coppa D alternation: %d decided, %d against", dec, bad);
    printf("%s: Coppa Italia Serie D 2026-27: alternation (art. 4) -- the %d ties of the last 32 it decides, the LND's listed host each time (%d against)\n",
           ok ? "PASS" : "FAIL", dec, bad);
    /* the geographic blocks on the real entrants: the preliminary's 68 (blocks of 2) and round 1's real 128 (the
       preliminary's real winners, the 94 others; blocks of 4), 1000 draws each: the km of all ties against the
       calibration (2740 / 3977 km; the real 2610 / 4048) */
    sr_kn_round g1 = { 34, 0, 1, SR_KN_DRAW, SR_KN_PAIR_GEO, 2, 0, 68 }, g2 = { 64, 0, 1, SR_KN_DRAW, SR_KN_PAIR_GEO, 4, 0, 128 };
    sr_kn_entry join[128];
    double k1 = 0, k2 = 0;
    int b1 = 0, n = 0;
    for (int i = 0; i < 68; i++) { join[i].club = s2d_num[i]; join[i].zone = 0; }
    work.geo = s2c_geo;
    for (int it = 0; it < 1000; it++) {
        uint32_t r2 = (uint32_t)it * 7919u + 1;
        b1 += sr_kn_cup(&g1, 1, NULL, NULL, 0, join, NULL, &r2, play_rand, NULL, &work, out) < 0;
        for (int t = 0; t < 34; t++) k1 += d_km(s2c_geo, out[t].home, out[t].away);
    }
    for (int i = 0; i < S2D_NRES; i++) if (s2d_res[i][0] == 1) { join[n].club = s2d_res[i][3]; join[n++].zone = 0; }
    for (int i = 68; i < 162; i++) { join[n].club = s2d_num[i]; join[n++].zone = 0; }
    for (int it = 0; it < 1000 && n == 128; it++) {
        uint32_t r2 = (uint32_t)it * 104729u + 5;
        b1 += sr_kn_cup(&g2, 1, NULL, NULL, 0, join, NULL, &r2, play_rand, NULL, &work, out) < 0;
        for (int t = 0; t < 64; t++) k2 += d_km(s2c_geo, out[t].home, out[t].away);
    }
    k1 /= 1000; k2 /= 1000;
    ok = !b1 && n == 128 && k1 > 2500 && k1 < 3000 && k2 > 3600 && k2 < 4400;
    CHECK(ok, "Coppa D blocks: %d bad, %.0f km / %.0f km", b1, k1, k2);
    printf("%s: Coppa Italia Serie D: the geographic blocks on the real 2026-27 entrants, 1000 draws -- the preliminary %.0f km in all (real 2610), round 1 %.0f km (real 4048)\n",
           ok ? "PASS" : "FAIL", k1, k2);
    (void)pairs;
}

/* later seasons at random: 9 groups of 18 last season (1st up, 15th-18th down: 45 gone), 45 new clubs; the numbers
   as the spec (the preliminary: the new, then from the bottom of the tables; the others), then the rounds -- every
   club enters once, the preliminary's old clubs placed no higher than any other's, geographic blocks, the bracket of
   winners, alternation from the last 32, one winner */
static void coppa_d_random(int seasons)
{
    static sr_kn_geo geo[SR_KN_CLUBS];
    static uint16_t tab[9][18], nw[9][5], num[SR_KN_NUMBERS];
    static sr_kn_rec rec[SR_KN_CLUBS];
    static uint8_t el[SR_KN_CLUBS], isnew[SR_KN_CLUBS];
    static sr_kn_tie out[256];
    static sr_kn_pair pairs[256];
    sr_kn_list lists[18];
    sr_kn_group g[2];
    sr_kn_entry join[162];
    sr_kn_round rd[8] = {
        { 34, 0, 1, SR_KN_DRAW, SR_KN_PAIR_GEO, 2, 0, 68 }, { 64, 34, 1, SR_KN_DRAW, SR_KN_PAIR_GEO, 4, 68, 94 },
        { 32, 98, 1, SR_KN_DRAW, SR_KN_PAIR_FIXED, 0, 0, 0 }, { 16, 130, 1, SR_KN_ALTERNATE, SR_KN_PAIR_FIXED, 0, 0, 0 },
        { 8, 146, 1, SR_KN_ALTERNATE, SR_KN_PAIR_FIXED, 0, 0, 0 }, { 4, 154, 1, SR_KN_ALTERNATE, SR_KN_PAIR_FIXED, 0, 0, 0 },
        { 2, 158, 2, SR_KN_DRAW, SR_KN_PAIR_DRAW, 0, 0, 0 }, { 1, 160, 2, SR_KN_DRAW, SR_KN_PAIR_DRAW, 0, 0, 0 } };
    int bad = 0, fails0 = 0, dec = 0;
    double km = 0;
    for (int r = 2; r < 6; r++)
        for (int t = 0; t < rd[r].ties; t++) {
            pairs[rd[r].first + t].a = SR_KN_WIN(r - 1, 2 * t);
            pairs[rd[r].first + t].b = SR_KN_WIN(r - 1, 2 * t + 1);
        }
    memset(&g, 0, sizeof g);
    g[0].take = 68; g[0].order = SR_KN_LIST; g[0].else_order = SR_KN_PLACE_DESC; g[0].nfrom = 9; g[0].nelse = 9;
    g[1].take = 94; g[1].order = SR_KN_LIST; g[1].else_order = SR_KN_LIST; g[1].nfrom = 9; g[1].nelse = 9;
    for (int i = 0; i < 9; i++) { g[0].from[i] = (uint8_t)i; g[0].els[i] = (uint8_t)(9 + i); g[1].from[i] = (uint8_t)(9 + i); g[1].els[i] = (uint8_t)i; }
    work.geo = geo;
    for (int s = 0; s < seasons; s++) {
        uint32_t rng = (uint32_t)s * 2654435761u + 7;
        int b = 0, n;
        memset(el, 0, sizeof el); memset(isnew, 0, sizeof isnew); memset(rec, 0, sizeof rec);
        for (int i = 0; i < 207; i++) {         /* (a club: a real Serie D town, moved a little) */
            const sr_kn_geo *x = &s2c_geo[s2d_num[rnd(162)]];
            geo[i].lat = (int16_t)(x->lat + (int)rnd(41) - 20); geo[i].lon = (int16_t)(x->lon + (int)rnd(41) - 20);
        }
        for (int i = 0; i < 162; i++) { tab[i / 18][i % 18] = (uint16_t)i; }
        for (int i = 161; i > 0; i--) { const int j = (int)rnd((uint32_t)i + 1); const uint16_t x = tab[i / 18][i % 18]; tab[i / 18][i % 18] = tab[j / 18][j % 18]; tab[j / 18][j % 18] = x; }
        for (int L = 0; L < 9; L++) {
            for (int p = 0; p < 18; p++) { rec[tab[L][p]].place = (uint8_t)(p + 1); rec[tab[L][p]].pld = 34; el[tab[L][p]] = p > 0 && p < 14; }
            for (int k = 0; k < 5; k++) { nw[L][k] = (uint16_t)(162 + L * 5 + k); el[nw[L][k]] = 1; isnew[nw[L][k]] = 1; }
            lists[L].club = nw[L]; lists[L].n = 5; lists[L].unknown = SR_KN_NONE;
            lists[9 + L].club = tab[L]; lists[9 + L].n = 18; lists[9 + L].unknown = SR_KN_NONE;
        }
        n = sr_kn_number(g, 2, lists, rec, el, &rng, &work, num, SR_KN_NUMBERS);
        if (n != 162) { bad++; continue; }
        int maxp = 99, minp = 0;                /* (the preliminary's old clubs placed no higher than the others) */
        for (int i = 0; i < 162; i++) {
            const int p = rec[num[i]].place;
            b += !el[num[i]];
            if (i < 45) b += !isnew[num[i]];    /* (the 45 new first) */
            else if (i < 68) { if (p < maxp) maxp = p; }
            else if (p > minp) minp = p;
        }
        b += maxp < minp;
        for (int i = 0; i < 162; i++) { join[i].club = num[i]; join[i].zone = 0; }
        int w = sr_kn_cup(rd, 8, pairs, NULL, 0, join, NULL, &rng, play_rand, NULL, &work, out);
        b += w < 0 || bracket_ok(rd, 8, out, 162);
        b += alt_bad(rd, 3, 5, pairs, out, &dec, 0);
        for (int t = 0; t < 34; t++) km += d_km(geo, out[t].home, out[t].away);
        for (int t = 0; t < 3; t++) b += out[158 + t].legs != 2;
        if (b && fails0++ < 3) printf("  random Coppa D season %d: %d bad\n", s, b);
        bad += b != 0;
    }
    km /= seasons > 0 ? seasons : 1;
    CHECK(!bad && km < 4000, "Coppa D random: %d bad, preliminary %.0f km", bad, km);
    printf("%s: Coppa Italia Serie D, %d random seasons: the numbers (the 45 new clubs, then from the bottom of the tables: no preliminary club placed higher than an exempt one), the preliminary and round 1 in geographic blocks (%.0f km a preliminary), the fixed bracket of winners, alternation in the %d ties it decides, two-leg semi-finals and final, one winner (%d bad)\n",
           !bad && km < 4000 ? "PASS" : "FAIL", seasons, km, dec, bad);
}

int main(int argc, char **argv)
{
    real_season();
    forced();
    forced_all();
    random_seasons(argc > 1 ? atoi(argv[1]) : 100000);
    coppa_c();
    coppa_c_random(argc > 1 ? atoi(argv[1]) : 100000);
    coppa_d();
    coppa_d_random(argc > 1 ? atoi(argv[1]) : 100000);
    return fails != 0;
}
