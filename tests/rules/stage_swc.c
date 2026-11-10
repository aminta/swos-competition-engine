/* T3 (sr_stage host plan, Motore 10/11): a groups-then-knockout cup read from a .SWC, played by sr_stage.
 *   spec  tests/specs/ok/v14_stage_cup.json -> swcc -> SWC: 32 clubs of 8 associations, 8 groups of 4 (2 legs), the
 *         top two to a seeded last 16, drawn rounds, a one-match final
 *   path  swc_spec_open; the comp's clubs (ENTR) in seeding order, their associations (CLUB.country); the groups
 *         stage: sr_group_draw, sr_rr_calendar (its legs), simulated results, sr_rr_table (its TIEB list, the comp's
 *         points); the knockout: its entrants (the .SWC keeps their count only: the top `teams / groups` of each
 *         group, winners first, as the spec's "groups#1", "groups#2"), each ROUN record's pairing, legs, home rule
 *         and DECI list -> sr_ko_pairs, sr_ko_tie
 *   seed  P4: every draw / lots seed from swc_rng_stream(world seed, season, the stage's purpose), never the results'
 *         random stream; a draw replays the same whatever was played before it
 * Arguments: SWC [SEASONS] (default 300). Prints PASS / FAIL lines; exit 1 on a FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swosrules.h"
#include "../../format/swc_format.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

/* the results' own stream (the game's matches): nothing a draw may depend on */
static unsigned res_;
static unsigned rnd(unsigned n) { res_ = res_ * 1103515245u + 12345u; return (res_ >> 8) % n; }

enum { WORLD = 0x5EEDu, NC = 32 };

typedef struct {
    const SwcSpec *s;
    const SwcComp *comp;
    const SwcStage *gs, *ko;
    int club[NC], str[NC];
    uint8_t assoc[NC];
} Cup;

/* the stage's seeds: stream (world, season, purpose), k-th draw of it */
static uint32_t stage_seed(uint32_t season, const SwcStage *st, int k)
{
    SwcRng r;
    swc_rng_stream(&r, WORLD, season, st->purpose);
    uint32_t v = 0;
    for (int i = 0; i <= k; i++) v = swc_rng_next(&r);
    return v;
}

typedef struct {
    uint8_t grp[NC];            /* club (seeding index) -> group */
    int winner, matches, bad;
    uint8_t pairs0[16];         /* the last 16's draw (seeding indices of the entrants) */
} Season;

static void play_group(sr_match *m, int nm, const int *str)
{
    for (int k = 0; k < nm; k++) {
        m[k].hg = (uint8_t)rnd(3 + (unsigned)str[m[k].home] / 40);
        m[k].ag = (uint8_t)rnd(3 + (unsigned)str[m[k].away] / 40);
        m[k].played = 1;
    }
}

/* a tie between entrants a (side 0) and b (side 1), side 0 the better placed; home rule h (swcc HOMES) */
static int play_tie(const Cup *c, int a, int b, const SwcRound *r, uint32_t seed)
{
    sr_leg l[2];
    const uint8_t *deci = c->s->deci + r->deci;
    for (int k = 0; k < r->legs; k++) {
        if (r->legs == 1 || r->home == 3) l[k].home = 2;                          /* neutral */
        else if (r->home == 2) l[k].home = (uint8_t)(k == 0 ? 1 : 0);             /* better placed: second leg */
        else if (r->home == 1) l[k].home = (uint8_t)(k == 0 ? 0 : 1);             /* better placed: first leg */
        else l[k].home = (uint8_t)(k == 0 ? 0 : 1);                               /* draw: the first drawn first */
        l[k].g[0] = (uint8_t)rnd(3 + (unsigned)c->str[a] / 40);
        l[k].g[1] = (uint8_t)rnd(3 + (unsigned)c->str[b] / 40);
    }
    sr_tie_extra x = { 0, { 0, 0 }, 0, { 0, 0 }, { 0, 1 } };
    int w = sr_ko_tie(l, r->legs, &x, deci, seed);
    if (w == SR_TIE_OPEN) { x.et_played = 1; x.et[0] = (uint8_t)rnd(2); x.et[1] = (uint8_t)rnd(2); w = sr_ko_tie(l, r->legs, &x, deci, seed); }
    if (w == SR_TIE_OPEN) {
        x.pens_played = 1; x.pens[0] = (uint8_t)(3 + rnd(3));
        do x.pens[1] = (uint8_t)(3 + rnd(3)); while (x.pens[1] == x.pens[0]);
        w = sr_ko_tie(l, r->legs, &x, deci, seed);
    }
    return w == 0 ? a : w == 1 ? b : -1;
}

/* one season: res0 the results stream's start (P4: any value, the draws the same) */
static Season season(const Cup *c, uint32_t s, unsigned res0, int game_rng_draw)
{
    Season out;
    memset(&out, 0, sizeof out);
    out.winner = -1;
    res_ = res0;
    const SwcStage *gs = c->gs, *ko = c->ko;
    const int n = gs->teams, g = gs->groups, per = n / g, q = ko->teams / g;
    const uint32_t dseed = game_rng_draw ? rnd(1u << 30) : stage_seed(s, gs, 0);   /* (control: the game's stream) */
    if (sr_group_draw(n, g, c->assoc, dseed, out.grp) != g) { out.bad++; return out; }
    static sr_match gm[64][64];
    sr_srow t[64][8];
    int member[64][8], cnt[64] = { 0 };
    for (int k = 0; k < n; k++) member[out.grp[k]][cnt[out.grp[k]]++] = k;
    const uint8_t *tieb = c->s->tieb + gs->tieb;
    int ent[64], ne = 0;
    for (int j = 0; j < g; j++) {
        int str[8];
        for (int k = 0; k < cnt[j]; k++) str[k] = c->str[member[j][k]];
        const int nm = sr_rr_calendar(cnt[j], gs->legs, gm[j], 64);
        if (nm != cnt[j] * (cnt[j] - 1) / 2 * gs->legs) out.bad++;
        play_group(gm[j], nm, str);
        out.matches += nm;
        if (sr_rr_table(cnt[j], gm[j], nm, c->comp->win, c->comp->draw, c->comp->loss, tieb, stage_seed(s, gs, 1 + j), t[j]) != cnt[j])
            out.bad++;
    }
    for (int p = 0; p < q; p++)                           /* "groups#1" (the winners), then "groups#2" */
        for (int j = 0; j < g; j++) ent[ne++] = member[j][t[j][p].club];
    if (ne != ko->teams || per < q) out.bad++;
    int left = ne;
    for (int r = 0; r < ko->nrounds; r++) {
        const SwcRound *rd = &c->s->rounds[ko->first_round + r];
        uint8_t o[64];
        if (rd->ties * 2 != left || rd->pairing > SR_PAIR_FIXED ||
            sr_ko_pairs(rd->pairing, left, NULL, stage_seed(s, ko, 2 * r), o) != left / 2) { out.bad++; return out; }
        if (r == 0) memcpy(out.pairs0, o, 16);
        int nx[64], nn = 0, seen[64] = { 0 };
        for (int k = 0; k < left; k += 2) {
            seen[o[k]]++; seen[o[k + 1]]++;
            if (rd->pairing == SR_PAIR_SEEDED && (o[k] >= left / 2 || o[k + 1] < left / 2)) out.bad++;
            const int w = play_tie(c, ent[o[k]], ent[o[k + 1]], rd, stage_seed(s, ko, 2 * r + 1));
            if (w < 0) out.bad++;
            nx[nn++] = w; out.matches += rd->legs;
        }
        for (int k = 0; k < left; k++) out.bad += seen[k] != 1;
        for (int k = 0; k < nn; k++) ent[k] = nx[k];
        left = nn;
    }
    out.winner = left == 1 ? ent[0] : -1;
    return out;
}

static int draw_bad(int n, int g, const uint8_t *assoc, const uint8_t *grp)
{
    uint8_t size[64], cnt[64] = { 0 };
    sr_group_sizes(n, g, size);
    for (int c = 0; c < n; c++) {
        if (grp[c] >= g) return 1;
        cnt[grp[c]]++;
        for (int q = 0; q < c; q++) {
            if (q / g == c / g && grp[q] == grp[c]) return 2;          /* one club of a pot a group */
            if (assoc[q] == assoc[c] && grp[q] == grp[c]) return 3;    /* associations apart */
        }
    }
    for (int j = 0; j < g; j++) if (cnt[j] != size[j]) return 4;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: stage_swc SWC [SEASONS]\n"); return 2; }
    const int seasons = argc > 2 ? atoi(argv[2]) : 300;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint8_t bin[1 << 16];
    const uint32_t len = (uint32_t)fread(bin, 1, sizeof bin, f);
    fclose(f);
    SwcSpec s;
    SwcArena a;
    static uint8_t mem[1 << 16];
    swc_init(&a, mem, sizeof mem);
    char msg[300];
    const SwcStatus st = swc_spec_open(&s, bin, len, &a);
    snprintf(msg, sizeof msg, "the .SWC opens (status %d, %u bytes, arena %u of %u used)", (int)st, (unsigned)len,
             (unsigned)a.used, (unsigned)s.h.arena);
    check(st == SWC_OK && a.used <= s.h.arena, msg);
    if (st != SWC_OK) return 1;

    Cup c;
    memset(&c, 0, sizeof c);
    c.s = &s;
    c.comp = &s.comps[0];
    c.gs = &s.stages[c.comp->first_stage];
    c.ko = &s.stages[c.comp->first_stage + 1];
    snprintf(msg, sizeof msg, "the cup's records: %u clubs, stages %d (%u clubs, %u groups, %u legs) + %d (%u clubs, %u rounds), points %u/%u/%u",
             c.comp->nentr, c.gs->type, c.gs->teams, c.gs->groups, c.gs->legs, c.ko->type, c.ko->teams, c.ko->nrounds,
             c.comp->win, c.comp->draw, c.comp->loss);
    check(s.ncomps == 1 && c.comp->nstages == 2 && c.comp->nentr == NC && c.gs->type == SWC_ST_GROUPS &&
          c.ko->type == SWC_ST_KNOCKOUT && c.gs->teams == NC && c.gs->groups == 8 && c.gs->legs == 2 && c.ko->teams == 16 &&
          c.ko->nrounds == 4 && c.gs->purpose != c.ko->purpose, msg);
    {                                           /* the spec's lists, as sr_stage's codes */
        const uint8_t tb[] = { SR_TB_POINTS, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_H2H_GOALS_FOR,
                               SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_DRAW_LOTS, 0 };
        const uint8_t two[] = { SR_D_AGGREGATE, SR_D_EXTRA_TIME, SR_D_PENALTIES, 0 }, one[] = { SR_D_EXTRA_TIME, SR_D_PENALTIES, 0 };
        const SwcRound *r = &s.rounds[c.ko->first_round];
        int ok = !memcmp(s.tieb + c.gs->tieb, tb, sizeof tb);
        for (int k = 0; k < 4; k++)
            ok &= !strcmp((const char *)s.deci + r[k].deci, (const char *)(k == 3 ? one : two)) && r[k].ties == 8 >> k &&
                  r[k].legs == (k == 3 ? 1 : 2) && r[k].pairing == (k ? SR_PAIR_DRAW : SR_PAIR_SEEDED) && r[k].home == (k == 3 ? 3 : k ? 0 : 2);
        check(ok, "the .SWC's TIEB / DECI lists and ROUN records are the spec's (points, h2h x3, GD, GF, lots; aggregate, ET, pens; seeded then drawn; better placed home second, neutral final)");
    }
    for (int k = 0; k < NC; k++) {
        c.club[k] = s.entr[c.comp->entr + k];
        c.str[k] = 160 - 4 * k;
        c.assoc[k] = (uint8_t)s.clubs[c.club[k]].country;
    }

    int bad = 0, dbad = 0, matches = 0, top8 = 0, nwin = 0, same = 0, moved = 0, ctrl = 0;
    for (int y = 0; y < seasons; y++) {
        const Season a1 = season(&c, (uint32_t)y, 1000u + (unsigned)y, 0);
        bad += a1.bad; matches += a1.matches;
        dbad += draw_bad(NC, 8, c.assoc, a1.grp) != 0;
        nwin += a1.winner >= 0; top8 += a1.winner >= 0 && a1.winner < 8;
        /* P4: another results stream, the same draws (groups and the last 16's pairs: drawn before any knockout
           result, after the groups -- their tables differ, so only the groups' draw is compared there) */
        const Season a2 = season(&c, (uint32_t)y, 777777u + 3u * (unsigned)y, 0);
        same += !memcmp(a1.grp, a2.grp, NC);
        const Season a3 = season(&c, (uint32_t)y + 1, 1000u + (unsigned)y, 0);   /* the next season: drawn anew */
        moved += !!memcmp(a1.grp, a3.grp, NC);
        const Season c1 = season(&c, (uint32_t)y, 1000u + (unsigned)y, 1);
        const Season c2 = season(&c, (uint32_t)y, 777777u + 3u * (unsigned)y, 1);
        ctrl += !!memcmp(c1.grp, c2.grp, NC);
    }
    const int per = 8 * 12 + 16 + 8 + 4 + 1;
    snprintf(msg, sizeof msg, "the cup from the .SWC, %d seasons: one winner each (%d), %d matches each (%d bad, %d matches)",
             seasons, nwin, per, bad, matches);
    check(!bad && nwin == seasons && matches == seasons * per, msg);
    snprintf(msg, sizeof msg, "the group draws keep the .SWC's associations apart, one club of a pot a group (%d bad)", dbad);
    check(!dbad, msg);
    snprintf(msg, sizeof msg, "the top eight seeds win it more often than the other 24 (%d of %d)", top8, seasons);
    check(top8 * 2 > seasons, msg);
    snprintf(msg, sizeof msg, "P4: the draw from (world, season, the stage's purpose) -- another results stream, the same groups (%d of %d); the next season another draw (%d of %d)",
             same, seasons, moved, seasons);
    check(same == seasons && moved * 10 >= seasons * 9, msg);
    snprintf(msg, sizeof msg, "control: the draw seeded from the results' stream changes with the matches played (%d of %d differ)", ctrl, seasons);
    check(ctrl * 10 >= seasons * 9, msg);
    /* control: a knockout round's ties not matching the clubs that reach it (the .SWC's ROUN record edited) */
    SwcRound *r0 = (SwcRound *)&s.rounds[c.ko->first_round];
    const uint16_t keep = r0->ties;
    r0->ties = 7;
    const Season e = season(&c, 1u, 1u, 0);
    r0->ties = keep;
    check(e.bad && e.winner < 0, "control: a ROUN record with 7 ties for 16 clubs -- caught, no winner");
    return fails ? 1 : 0;
}
