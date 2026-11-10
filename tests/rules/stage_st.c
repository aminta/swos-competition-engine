/* T4 (sr_stage host plan): the ledger item 'ST' (rules/sr_stled.c) round trip, on the groups stage of the T3 cup
 * (tests/specs/ok/v14_stage_cup.json -> swcc -> SWC: 32 clubs of 8 associations, 8 groups of 4, 2 legs).
 *   unbroken  sr_st_begin (the draw seeded from (world, season, the stage's purpose): P4), every matchday's results
 *             through sr_st_record, the tables from sr_st_group + sr_rr_table
 *   broken    the same, saved after half the matchdays (the block copied into an 'EN'-like item, the memory wiped),
 *             loaded (sr_st_check 1), finished: the same block and tables as unbroken
 *   old save  no item (zeroed): sr_st_check 0 -> begun at the first week: the same groups, then the same season
 *   not this  a block of another season, another stage, another shape: 0; a broken body (a group out of range,
 *             the groups' sizes wrong, a goal nibble 15): -1
 * A match's result is a hash of (season, group, match): the same whatever was played or saved before it.
 * Arguments: SWC [SEASONS] (default 200). PASS / FAIL lines; exit 1 on a FAIL. */
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

enum { WORLD = 0x5EEDu, NC = 32, ROOM = 4096 };

static uint32_t stage_seed(uint32_t season, const SwcStage *st, int k)
{
    SwcRng r;
    swc_rng_stream(&r, WORLD, season, st->purpose);
    uint32_t v = 0;
    for (int i = 0; i <= k; i++) v = swc_rng_next(&r);
    return v;
}

static uint32_t goals(uint32_t season, int g, int i, int side)
{
    uint32_t h = season * 2654435761u ^ (uint32_t)(g * 131 + i) * 40503u ^ (uint32_t)side * 0x9E3779B9u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return h % (side ? 3u : 4u);
}

typedef struct {
    const SwcSpec *s;
    const SwcComp *comp;
    const SwcStage *gs;
    uint8_t assoc[NC];
    uint16_t word[NC];          /* the entrants' team words (the key): their spec club indices + 0x100 */
} Stage;

typedef struct { uint8_t blk[ROOM]; sr_srow t[8][8]; int size, bad; } Run;

/* matchdays [md0, md1) of every group into the block */
static void play(const Stage *c, uint8_t *blk, uint32_t y, int md0, int md1, int *bad)
{
    static sr_match m[256], w[256];
    uint8_t club[64];
    for (int g = 0; g < c->gs->groups; g++) {
        const int nm = sr_st_group(blk, g, club, m, 256);
        if (nm < 0) { (*bad)++; continue; }
        for (int i = 0; i < nm; i++)
            if (m[i].md >= md0 && m[i].md < md1 &&
                sr_st_record(blk, club[m[i].home], club[m[i].away], (int)goals(y, g, i, 0), (int)goals(y, g, i, 1), w, 256) < 0)
                (*bad)++;
    }
}

static void tables(const Stage *c, Run *r, uint32_t y)
{
    static sr_match m[256];
    uint8_t club[64];
    const uint8_t *tieb = c->s->tieb + c->gs->tieb;
    for (int g = 0; g < c->gs->groups; g++) {
        const int nm = sr_st_group(r->blk, g, club, m, 256);
        int played = 0;
        for (int i = 0; i < nm; i++) played += m[i].played;
        if (nm < 0 || played != nm ||
            sr_rr_table(4, m, nm, c->comp->win, c->comp->draw, c->comp->loss, tieb, stage_seed(y, c->gs, 1 + g), r->t[g]) != 4)
            r->bad++;
        for (int k = 0; k < 4; k++) r->t[g][k].club = club[r->t[g][k].club];   /* (stage indices) */
    }
}

/* how = 0 unbroken, 1 saved after matchday `half` and loaded, 2 an older save: no item at the first week,
   3 (control) saved after `half`, the item lost at the load */
static Run season(const Stage *c, uint32_t y, int how, int half, int *chk)
{
    static Run r;
    memset(&r, 0, sizeof r);
    const SwcStage *gs = c->gs;
    const int mds = gs->legs * 3;
    if (how == 2) {
        memset(r.blk, 0, ROOM);                         /* the ledger zeroes an item the save lacks */
        *chk = sr_st_check(r.blk, ROOM, gs->teams, gs->groups, gs->legs, y, gs->purpose, c->word);
    }
    r.size = sr_st_begin(r.blk, ROOM, gs->teams, gs->groups, gs->legs, y, gs->purpose, c->word, c->assoc, stage_seed(y, gs, 0));
    if (r.size < 0) { r.bad++; return r; }
    if (how == 1 || how == 3) {
        play(c, r.blk, y, 0, half, &r.bad);
        static uint8_t save[ROOM + 4];                  /* the save: id, length, the block */
        save[0] = 'S'; save[1] = 'T'; save[2] = (uint8_t)r.size; save[3] = (uint8_t)(r.size >> 8);
        memcpy(save + 4, r.blk, (size_t)r.size);
        memset(r.blk, 0xA5, ROOM);                      /* the game quits */
        if (how == 3) memset(r.blk, 0, ROOM);           /* (control: the item lost) */
        else memcpy(r.blk, save + 4, (size_t)(save[2] | save[3] << 8));
        *chk = sr_st_check(r.blk, ROOM, gs->teams, gs->groups, gs->legs, y, gs->purpose, c->word);
        if (*chk != 1) r.size = sr_st_begin(r.blk, ROOM, gs->teams, gs->groups, gs->legs, y, gs->purpose, c->word, c->assoc, stage_seed(y, gs, 0));
        play(c, r.blk, y, half, mds, &r.bad);
    } else
        play(c, r.blk, y, 0, mds, &r.bad);
    tables(c, &r, y);
    return r;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: stage_st SWC [SEASONS]\n"); return 2; }
    const int seasons = argc > 2 ? atoi(argv[2]) : 200;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint8_t bin[1 << 16], mem[1 << 16];
    const uint32_t len = (uint32_t)fread(bin, 1, sizeof bin, f);
    fclose(f);
    SwcSpec s;
    SwcArena a;
    swc_init(&a, mem, sizeof mem);
    check(swc_spec_open(&s, bin, len, &a) == SWC_OK, "the .SWC opens");
    Stage c;
    memset(&c, 0, sizeof c);
    c.s = &s;
    c.comp = &s.comps[0];
    c.gs = &s.stages[c.comp->first_stage];
    for (int k = 0; k < NC; k++) {
        c.assoc[k] = (uint8_t)s.clubs[s.entr[c.comp->entr + k]].country;
        c.word[k] = (uint16_t)(0x100 + s.entr[c.comp->entr + k]);
    }
    const SwcStage *gs = c.gs;
    char msg[300];
    const int size = sr_st_size(gs->teams, gs->groups, gs->legs);
    snprintf(msg, sizeof msg, "the item's size for 32 clubs, 8 groups, 2 legs: %d B (16 + 32 groups + 96 results)", size);
    check(gs->type == SWC_ST_GROUPS && gs->teams == NC && size == 16 + 32 + 96, msg);

    int bad = 0, same = 0, chk_ok = 0, old_same = 0, old_chk = 0, other = 0, broken = 0;
    for (int y = 0; y < seasons; y++) {
        int chk = -9;
        Run u = season(&c, (uint32_t)y, 0, 0, &chk);
        static Run u0;
        u0 = u;
        bad += u0.bad;
        Run b = season(&c, (uint32_t)y, 1, 1 + y % 5, &chk);    /* saved after matchday 1 .. 5 of 6 */
        bad += b.bad;
        chk_ok += chk == 1;
        same += b.size == u0.size && !memcmp(b.blk, u0.blk, (size_t)u0.size) && !memcmp(b.t, u0.t, sizeof b.t);
        Run o = season(&c, (uint32_t)y, 2, 0, &chk);
        bad += o.bad;
        old_chk += chk == 0;
        old_same += !memcmp(o.blk, u0.blk, (size_t)u0.size) && !memcmp(o.t, u0.t, sizeof o.t);
        /* not this stage's: the next season, another purpose, another shape (1 leg) */
        other += sr_st_check(u0.blk, ROOM, gs->teams, gs->groups, gs->legs, (uint32_t)y + 1, gs->purpose, c.word) == 0;
        other += sr_st_check(u0.blk, ROOM, gs->teams, gs->groups, gs->legs, (uint32_t)y, gs->purpose ^ 1, c.word) == 0;
        other += sr_st_check(u0.blk, ROOM, gs->teams, gs->groups, 1, (uint32_t)y, gs->purpose, c.word) == 0;
        {                                       /* (R1) another entrant in one place: a stale block */
            uint16_t w2[NC];
            memcpy(w2, c.word, sizeof w2);
            w2[y % NC] ^= 0x200;
            other += sr_st_check(u0.blk, ROOM, gs->teams, gs->groups, gs->legs, (uint32_t)y, gs->purpose, w2) == 0;
        }
        /* broken bodies of this stage's key */
        static uint8_t x[ROOM];
        memcpy(x, u0.blk, (size_t)u0.size); x[16 + y % NC] = 8;                     /* a group out of range */
        broken += sr_st_check(x, ROOM, gs->teams, gs->groups, gs->legs, (uint32_t)y, gs->purpose, c.word) == -1;
        memcpy(x, u0.blk, (size_t)u0.size); x[16 + y % NC] = (uint8_t)((x[16 + y % NC] + 1) % 8);   /* sizes 5 / 3 */
        broken += sr_st_check(x, ROOM, gs->teams, gs->groups, gs->legs, (uint32_t)y, gs->purpose, c.word) == -1;
        memcpy(x, u0.blk, (size_t)u0.size); x[16 + NC + y % 96] = 0xF2;             /* a goal nibble 15 */
        broken += sr_st_check(x, ROOM, gs->teams, gs->groups, gs->legs, (uint32_t)y, gs->purpose, c.word) == -1;
    }
    snprintf(msg, sizeof msg, "%d seasons played through the block: every result recorded, every table complete (%d bad)", seasons, bad);
    check(!bad, msg);
    snprintf(msg, sizeof msg, "T4: saved after matchday 1..5 and loaded (check 1: %d of %d), finished: the same block and tables as unbroken (%d of %d)",
             chk_ok, seasons, same, seasons);
    check(chk_ok == seasons && same == seasons, msg);
    snprintf(msg, sizeof msg, "T4 old save: no item -> check 0 (%d of %d) -> begun at the first week: the same groups and season (%d of %d)",
             old_chk, seasons, old_same, seasons);
    check(old_chk == seasons && old_same == seasons, msg);
    snprintf(msg, sizeof msg, "another season / stage / shape / entrants' block: not started (%d of %d)", other, 4 * seasons);
    check(other == 4 * seasons, msg);
    snprintf(msg, sizeof msg, "this stage's key with a broken body (group out of range, sizes, nibble 15): -1 (%d of %d)", broken, 3 * seasons);
    check(broken == 3 * seasons, msg);

    {                                           /* control: the item lost mid-stage */
        int chk = -9, lost = 0;
        for (int y = 0; y < 20; y++) {
            Run l = season(&c, (uint32_t)y, 3, 3, &chk);
            lost += chk == 0 && l.bad > 0;
        }
        snprintf(msg, sizeof msg, "control: the item lost after matchday 3 -> begun again, the first half's results missing: caught (%d of 20)", lost);
        check(lost == 20, msg);
    }
    {                                           /* the record rules */
        static uint8_t blk[ROOM];
        static sr_match m[256], w[256];
        uint8_t club[64];
        sr_st_begin(blk, ROOM, gs->teams, gs->groups, gs->legs, 7, gs->purpose, c.word, c.assoc, stage_seed(7, gs, 0));
        const int nm = sr_st_group(blk, 0, club, m, 256);
        const int h = club[m[0].home], v = club[m[0].away];
        int other_g = -1;
        for (int k = 0; k < NC; k++) if (blk[16 + k] != 0) { other_g = k; break; }
        const int r1 = sr_st_record(blk, h, v, 15, 0, w, 256);
        const int r2 = sr_st_record(blk, h, v, 1, 1, w, 256);          /* 2 legs: h at home to v once */
        const int r3 = sr_st_record(blk, v, h, 0, 0, w, 256);          /* the return leg */
        const int r4 = sr_st_record(blk, h, other_g, 1, 0, w, 256);    /* another group */
        const int r5 = sr_st_record(blk, h, v, 1, 0, w, 4);            /* room short */
        sr_st_group(blk, 0, club, m, 256);
        snprintf(msg, sizeof msg, "record: the first match (index %d, 15-0 kept as 14-0: %u-%u), not twice (%d), the return leg (%d), not across groups (%d), room short (%d); %d matches a group",
                 r1, m[0].hg, m[0].ag, r2, r3, r4, r5, nm);
        check(r1 == 0 && m[0].hg == 14 && m[0].ag == 0 && r2 == -1 && r3 > 0 && r4 == -1 && r5 == -1 && nm == 12, msg);
        check(sr_st_size(1, 1, 1) == -1 && sr_st_size(65, 8, 1) == -1 && sr_st_size(8, 2, 5) == -1 && sr_st_size(8, 5, 1) == -1 &&
              sr_st_begin(blk, size - 1, gs->teams, gs->groups, gs->legs, 7, gs->purpose, c.word, c.assoc, 1) == -1 &&
              sr_st_check(blk, size - 1, gs->teams, gs->groups, gs->legs, 7, gs->purpose, c.word) == -2 &&
              sr_st_check(blk, ROOM, gs->teams, gs->groups, 5, 7, gs->purpose, c.word) == -2,
              "shapes refused (1 club, 65 clubs, 5 legs, groups of 1), room short refused; check: -2 for both");
        uint8_t one[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        check(sr_st_begin(blk, ROOM, 8, 2, 1, 7, 1, c.word, one, 1) == -2, "a draw refused (8 clubs of one association in 2 groups): -2");
        sr_st_begin(blk, ROOM, gs->teams, gs->groups, gs->legs, 7, gs->purpose, c.word, c.assoc, 3);
        blk[2] = SR_ST_VERSION + 1;
        check(sr_st_check(blk, ROOM, gs->teams, gs->groups, gs->legs, 7, gs->purpose, c.word) == 2,
              "m1: an 'ST' block of another layout version: 2 (begun again, the results so far lost)");
    }
    return fails ? 1 : 0;
}
