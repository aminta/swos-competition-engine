/*
 * swosrules.h -- the SWOS rules library: the competition rules the SWOS 96/97 Mod and OpenSWOS
 * share (docs/rules-library.md).
 *
 * Input: a league's final tables and its rules as the Mod's tools compile them. Output: each
 * club's move. No engine: calendars, menus, contests and saves stay each game's own. The code is
 * the Mod's (tools/dplay.c, tools/gplay.c), as OpenSWOS ported it and checked it against the
 * Mod's own C season after season (tests/rules/check.sh), now one copy for both games.
 *
 * Freestanding C99 for the DOS build (clang -target i686, -march=i486, no libc): no globals --
 * one caller-owned state passed by pointer --, no library calls, no compiler-generated
 * memcpy / memset. <stdint.h> is the one include -- or none, when the host's own header has
 * given int8_t .. uint32_t already and says so with SWOSRULES_HAVE_STDINT (the Amiga's toolchain
 * has no <stdint.h>: OpenSWOS defines them itself).
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#ifndef SWOSRULES_H
#define SWOSRULES_H

#ifndef SWOSRULES_HAVE_STDINT
#include <stdint.h>
#endif

#define SWOSRULES_VERSION 1

#define SR_DIVS  16                 /* a league's divisions (gplay.c GD) */
#define SR_CLUBS 32                 /* a division's clubs (gplay.c GT) */
#define SR_MOVES 48

/* A club as both games name it: the Mod's team word (team file | ordinal << 8) and its global
 * number (the league table's index); its record. */
typedef struct { uint16_t team, glob, won, gf, ga, pts; } sr_row;

/* A league's final tables, place order (row 0 the top). */
typedef struct {
    uint8_t nd;                     /* divisions in it */
    uint8_t n[SR_DIVS];             /* clubs in each */
    sr_row  t[SR_DIVS][SR_CLUBS];
} sr_tables;

/* A move: a club's division delta (one level: -1 up, 1 down), the rule that made it. */
enum { SR_RULE_ITALY = 1, SR_RULE_KNOCKOUT = 2 };
typedef struct { uint16_t glob; int8_t delta; uint8_t rule; } sr_move;

/* A league's stage tables (gplay.c's GP_SS bank: gp_stage / gp_keep), kept by the caller through
 * the season and saved with it: r[k] a fixture stage's table (its 'table' k), fresh bit k when this
 * season wrote it; a 'keep' stage's frozen table (Malta's Opening Round) in kept. */
#define SR_STAGE_TABLES 4
typedef struct {
    uint8_t used, key, fresh, pad;  /* key: the caller's (its country) */
    uint8_t n[SR_STAGE_TABLES];
    sr_row  r[SR_STAGE_TABLES][SR_CLUBS];
    uint8_t kept_div, n_kept;       /* the kept table's division; 0 clubs: none */
    sr_row  kept[SR_CLUBS];
} sr_stage_bank;

/* Argentina's promedio history (gplay.c PR_STATE): each top-flight club's points and matches of
 * the two seasons before; the caller's, saved. A new career starts from the league's seed. */
#define SR_PROMEDIO_CLUBS 48
typedef struct { uint16_t glob, p[2]; uint8_t m[2]; } sr_promedio_club;
typedef struct { uint8_t init, n, pad[2]; sr_promedio_club c[SR_PROMEDIO_CLUBS]; } sr_promedio;
typedef struct { uint16_t glob, points2, points1; uint8_t matches2, matches1; } sr_promedio_seed;

/* The SEASON WINNERS page's last season (gplay.c WN_ST), written for the career club's league:
 * 'M' the division the club played in, 'L' each division's 1st, 'K' each play-off's winner,
 * 0x80 | play-off: a title play-off's last three ties' winners (idx: the tie); team words. */
#define SR_WINNERS_MAX 40
typedef struct { uint8_t type, idx; uint16_t team; } sr_winners_entry;
typedef struct { uint8_t season, n, fresh, pad; sr_winners_entry e[SR_WINNERS_MAX]; } sr_winners;

/* A league's rules: the pack's, as the Mod's tools compile them. */
typedef struct {
    uint8_t        divisions;
    const uint8_t *promoted;        /* per division: the record's counts by place */
    const uint8_t *relegated;
    uint8_t        italy;           /* the Italian rules (dplay.c): Serie B and its groups -- */
    uint8_t        serie_b;         /* Serie B's division, the first of Serie C's groups, */
    uint8_t        group_first;     /* how many groups (2..4) */
    uint8_t        groups;
    const uint8_t *spec;            /* the pack's knockouts: gplay.py compile_spec's bytes, 0-ended */
    const sr_promedio_seed *promedio_seed;  /* the promedio's history at a new career (gplay.c */
    uint8_t        promedio_seeds;          /* GP_PROM_TAB, global numbers) */
    uint8_t        seconds;         /* B teams (gplay.c SEC_TAB): each one's (global number - base, */
    const uint8_t (*second)[2];     /* its first team's or FFh); the highest level one may reach */
    uint8_t        second_top;      /* (FFh any); flags: 1 play-offs allowed, 2 it may leave the */
    uint8_t        second_flags;    /* league from the lowest division (the pool) */
    const uint16_t *second_team;    /* each B team's team word, or 0: a row is found by it as well as by
                                       its global number (the Mod 4.0.5's SEC_TAB: rows of a division
                                       simulated early may carry no global number) */
    uint32_t        spec_len;       /* spec's bytes, required with a spec: the rules check it first
                                       (sr_spec_check) and refuse a league whose spec is not sound, or
                                       has no length (SR_CHECK_COUNT, "spec_len") */
    uint8_t         team_file_high; /* how this game lays out a team word (sr_row.team, second_team):
                                       0 the Mod's, file | ordinal << 8 (an x86 word over the two
                                       bytes); 1 file << 8 | ordinal (OpenSWOS). A spec names a club
                                       as its (file, ordinal) bytes -- entrant type 8 -- the same in
                                       both games; the rules make the word from them this way */
} sr_league;

/* A league's levels when some are groups of parallel divisions (Serie C's, Serie D's): where each
 * level starts. levels 0: a level per division. */
typedef struct { uint8_t levels; const uint8_t *first; } sr_levels;

/* What the game around the rules knows (all optional, 0 for none): a tie, a title or a stage
 * title the career club played itself; a line for the game's log. */
typedef struct {
    void *ctx;
    /* the career club's play-off pidx, played: 1 and its winner when it holds the tie a v b */
    int32_t (*played_tie)(void *ctx, int32_t pidx, uint16_t a, uint16_t b, uint16_t *winner);
    /* a title play-off pidx played as a group contest: 1 and its champion, runner-up */
    int32_t (*played_title)(void *ctx, int32_t pidx, uint16_t *champion, uint16_t *runner_up);
    /* the career club's division's stage title k (0..2) decided mid-season: 1 and its champion,
     * runner-up (team words, global numbers) */
    int32_t (*stage_title)(void *ctx, int32_t k, uint16_t team[2], uint16_t glob[2]);
    void    (*log)(void *ctx, const char *fmt, ...);
} sr_host;

/* The season: what the career club played itself (0: simulated) and the results the rules use. */
typedef struct {
    uint16_t po_win, out_win;       /* its Serie B play-off, play-out: the winner's team word */
    uint16_t cpo_win;               /* its Serie C play-off */
    uint16_t cup_winner;            /* the Coppa Italia Serie C winner, 0 unknown */
    uint8_t  test_held;             /* a test: Serie B's play-off and play-out held whatever the points */
    uint16_t mgr;                   /* the career club (team word), 0 none */
    uint16_t tie_a, tie_b, tie_w;   /* a group tie it played (gplay.c pl_w): its clubs, winner */
    sr_stage_bank *bank;            /* the league's stage tables, 0 none */
    sr_promedio   *promedio;        /* its promedio history, 0 none */
    sr_winners    *winners;         /* the SEASON WINNERS page, 0 not kept */
    const sr_host *host;            /* 0: none */
    /* The season-end preview (the Mod 4.0.5 rc19, gplay.c GP_PRE): this season's titles decided
     * at the career club's league end, before the season-end menu -- the same results the season
     * end will reach (each play-off draws from its own entrants), but no promedio, no move of the
     * stage tables and no new SEASON WINNERS page: its entries are appended (the caller marks
     * where they start). The career club's own play-offs are left to it: own_pi, each the play-off's
     * index + 1 (0 none -- a zeroed sr_season previews every play-off). */
    uint8_t        preview;
    uint8_t        own_pi[2];
    /* The B teams in the knockouts (gplay.c place_club / sec_inelig): their global numbers' base
     * (as sr_final_moves takes it) and the league's levels, 0 a level per division. A B team is
     * passed over as a play-off entrant by place (not in a play-out) unless its league's B teams
     * may play play-offs and it may go up: not above second_top, nor into or above its first
     * team's level after this season's record (the places' promotions and relegations). b_in_ko
     * switches it on: 0 (a zeroed sr_season) no B team is passed over in the knockouts, as before
     * 4.0.5's port; 1 B teams found by global number (base + ordinal: base 0 is a league that comes
     * first) or by team word (sr_league.second_team). */
    uint8_t        b_in_ko;
    uint16_t       base;
    const sr_levels *levels;
} sr_season;

/* A result the knockouts decide beside the moves: a title's champion and runner-up, a continental
 * play-off's winner and loser, a stage title's (kind 5 title, 6 continental, 8 stage). */
enum { SR_KIND_TITLE = 5, SR_KIND_CONTINENTAL = 6, SR_KIND_PROMEDIO = 7, SR_KIND_STAGE = 8 };
typedef struct { uint8_t kind, div; uint16_t team[2], glob[2]; } sr_result;

typedef struct { int32_t c, pts, gd, gf; } sr_gs;      /* a group table's row (gplay.c gs) */

/* The library's working state: the caller's memory, zeroed once (sr_reset). The caller writes
 * the season's tables into `t`, then calls the season end; the moves come back in `mv`. */
typedef struct sr_state {
    sr_tables t;
    sr_move   mv[SR_MOVES];
    int32_t   nmv;
    int32_t   dp_moves;             /* how many of mv the Italian rules made (they come first) */
    /* dplay.c */
    struct {
        uint8_t  ng, n[5];
        sr_row   t[5][24];
        uint16_t po_win, out_win, cpo_win, cupw;
        uint32_t rng;
        uint8_t  lot[5 * 24];
        uint8_t  test_held;
    } dp;
    /* gplay.c */
    struct {
        uint32_t rng;
        int32_t  by_rec, pidx, ss_used, srl_div, wn_on;
        uint16_t mgr, played_a, played_b, played_w;
        sr_result res[4], sres[3], srl[3];
        int32_t  nres;
        uint8_t  nst[2];            /* the kept 'keep' tables (gp_keep) */
        sr_row   st[2][SR_CLUBS];
        sr_stage_bank *ssb, no_bank;
        const sr_season *se;
        sr_gs    ps[64], ms[SR_CLUBS];
        sr_promedio_club nx[SR_PROMEDIO_CLUBS];
        int32_t  ce_club[64], ce_n; /* the last contest's entrants (sr_contest_entrants) */
    } gp;
    int8_t    want[SR_DIVS][SR_CLUBS];      /* each club's move, a level: -1 up, 1 down (sr_final_moves) */
} sr_state;

void     sr_reset(sr_state *st);
uint32_t sr_state_size(void);

/* The season end's first step: Serie B and Serie C's play-offs and play-outs (dplay.c dp_season)
 * over st->t, by the league's Italian rules. The moves start again in st->mv (rule
 * SR_RULE_ITALY); their count, 0 when the league is not Italian, -1 when it is refused
 * (sr_league_check: nothing played). */
int32_t sr_italy(sr_state *st, const sr_league *lg, const sr_season *se);

/* Serie B's play-out is held unless the 16th leads the 17th by more than 4 points; its play-off
 * unless the 3rd leads the 4th by more than 14 (dplay.c OUT_HELD and its play-off rule). */
int32_t sr_out_held(uint16_t pts16, uint16_t pts17);
int32_t sr_playoff_held(uint16_t pts3, uint16_t pts4);

/* The season end's second step: the pack's knockouts (gplay.c gp_season, whole: promotion,
 * play-out, placement, title, continental, promedio and stage-title play-offs; every entrant kind
 * -- by place, the stage tables, the Trofeo's finalists, the title deciders' seeds, the previous
 * play-off's loser --, group rounds, the drawn pot) over st->t: their moves after the Italian ones
 * (rule SR_RULE_KNOCKOUT), the results in st->gp.res[0 .. nres) (titles, continental places) and
 * st->gp.sres (the career club's division's stage titles); the SEASON WINNERS page in
 * se->winners when the career club is in the league. The count of moves it added. */
int32_t sr_knockouts(sr_state *st, const sr_league *lg, const sr_season *se);

/* ---- The career club's own play-off, played as a contest (gplay.c ko_build, gp_need, t2_shape)
 * -- the contest itself is the game's ---- */

#define SR_CONTEST_MAX 32           /* a contest's entrants */

/* Play-off pidx of the league's knockouts (its bytes), 0: none. */
const uint8_t *sr_spec_at(const sr_league *lg, int32_t pidx);

/* The career club's play-off at its league's end (gp_need, a career): the first playable one in
 * spec order with `club` (in division d's final table, best first) among its entrants -- its index,
 * the other divisions its entrants come from (to simulate now: at most two; nine for a group
 * contest) in other[9]; -2 - its index when it is a group contest; -1 none. Uses st->t. */
int32_t sr_club_playoff(sr_state *st, const sr_league *lg, const sr_season *se, int32_t d, const sr_row *table,
                        int32_t n, uint16_t club, uint8_t *other, int32_t *nother);

/* Play-off pidx's entrants as the season end would resolve them (ko_build), from nt tables
 * (divs[0] the club's division, then the ones simulated early) and the stage tables: in entrant
 * order their team words, global numbers, points and ranks (a stage entrant: its stage place;
 * 'host_by_record': by record), *spec its bytes; their count, 0 when one is not known. */
int32_t sr_contest_entrants(sr_state *st, const sr_league *lg, const sr_season *se, int32_t nt,
                            const int32_t *divs, const sr_row *const *tables, const int32_t *ns, int32_t pidx,
                            uint16_t *team, uint16_t *glob, uint8_t *prio, uint16_t *pts, const uint8_t **spec);

/* Right after sr_contest_entrants: a round-1 tie simulated at the contest's build, the career club
 * waiting (ko_build's presim): the seed (hseed of game round 0), then each tie of entrants a, b
 * (legs 3: a best of three); the winner, a or b. */
void    sr_contest_seed(sr_state *st, int32_t pidx, const uint16_t *team, int32_t n);
int32_t sr_contest_tie(sr_state *st, int32_t a, int32_t b, int32_t legs, int32_t host, int32_t level);

/* A title of groups then knockouts (gplay.c t2_shape: the Poule Scudetto), played as a group
 * contest: g groups of m entrants, meet meetings, then nk knockout stages of kt ties, the first
 * taking q (the groups' winners and best runners-up), kb their round bytes. 0: not one. */
typedef struct { uint8_t g, m, meet, nk, q, kt[4], kb[4]; } sr_group_shape;
int32_t sr_group_shape_of(const uint8_t *spec, sr_group_shape *shape);

/* ---- The season's moves (sr_post.c) ---- */

/* Each club's move, one level, into st->want[d][k] (k its place in st->t): the record's by place
 * (the top promoted[d] up, the bottom relegated[d] down), the play-offs' moves (st->mv) on top,
 * then the B-team rules. base: the league's first global number (a B team's is base + its
 * ordinal). */
void sr_final_moves(sr_state *st, const sr_league *lg, const sr_levels *lv, uint16_t base);

/* The B-team rules alone (gplay.c sec_post), over the moves already in st->want: a B team may not
 * go up above second_top, nor into or above its first team's level after this season's moves --
 * held, the next club of its division goes up instead; it goes down when its first team comes
 * into its level, the best placed relegated of its division staying. */
void sr_b_teams(sr_state *st, const sr_league *lg, const sr_levels *lv, uint16_t base);

/* ---- A cup's clubs from the second season on (sr_cups.c: gplay.c cf_build, the Mod 4.0.1) ---- */

#define SR_CUP_CLUBS 64             /* a cup's entrants */
#define SR_CLUB_SLOTS 512           /* a country's clubs (its slots: two team files' at most) */

/* From the cup's real (first-season) list: per division, how many of its clubs, how many of
 * them with a bye. */
typedef struct { uint8_t division, clubs, byes; } sr_quota;

/* A cup's clubs this season. The league's nclubs clubs are each a slot and this season's division
 * (after the moves); place[slot] is last season's (place | level << 5, 0 none) or the whole table 0
 * when last season's places are not this league's last season; level[d] a division's level (a
 * level of groups one). Each division's quota is taken in order -- relegated into it, stayed,
 * promoted, without a place; then the place, the slot --, the career club first when it is in it.
 * The slots in order (round 1's clubs, then the byes); their count, or 0 when it must play the
 * real list (a division with fewer clubs than its quota, or not n in all). */
int32_t sr_cup_entrants(const sr_quota *q, int32_t nq, int32_t n, int32_t nclubs, const uint16_t *slot,
                        const uint8_t *division, const uint8_t *place, const uint8_t *level,
                        int32_t career_slot, uint16_t *out);

/* ---- Untrusted input (sr_check.c): a spec written by hand, a damaged save -- checked once, up
 * front, so the rules' code (the Mod's, line for line, its fixed buffers) needs no check ---- */

enum {
    SR_CHECK_OK = 0,
    SR_CHECK_TRUNCATED,     /* the bytes end inside a field */
    SR_CHECK_COUNT,         /* a count over its buffer (entrants, ties, a table's clubs ...) */
    SR_CHECK_INDEX,         /* an index past what exists (a division, a member, a stage table ...) */
    SR_CHECK_VALUE          /* a byte with no meaning (a kind, an entrant type, legs, host, level ...) */
};
typedef struct {
    int32_t     code;       /* SR_CHECK_* */
    int32_t     pidx;       /* the play-off, -1 none */
    uint32_t    offset;     /* the byte (spec) or the element (state) */
    const char *field;      /* its name */
} sr_check_error;

/* A pack's knockouts, len bytes, for a league of `divisions` (0: any up to SR_DIVS): SR_CHECK_OK, or
 * the first fault (err, optional). */
int32_t sr_spec_check(const uint8_t *spec, uint32_t len, int32_t divisions, sr_check_error *err);
/* A league's rules (its spec too, which needs its spec_len) and a season's saved state (the stage
 * tables, the promedio, the SEASON WINNERS page, the levels). */
int32_t sr_league_check(const sr_league *lg, sr_check_error *err);
int32_t sr_season_check(const sr_season *se, sr_check_error *err);

/* ---- The Coppa Italia Serie D: a later season's first two rounds (sr_cupd.c: gplay.c cd_rank,
 * cd_season, cd_make) ---- */

#define SR_CUPD_PRE 41              /* a generated preliminary round's ties at most (gplay.c CDP) */
#define SR_CUPD_R1  64              /* a generated first round's ties (CD1) */

/* The season end the entry order is made from: each slot's division and, in Serie D, its place
 * from the bottom of its group + 2; the slots that took a pool club. The caller's, one season. */
typedef struct {
    uint8_t last_div[SR_CLUB_SLOTS];    /* FFh: in no table */
    uint8_t place[SR_CLUB_SLOTS];       /* 0 none */
    uint8_t arrival[SR_CLUB_SLOTS / 8];
} sr_cupd_end;

/* The cup's rounds 0-1 generated: np preliminary ties, then the first round's 64; tie t's sides
 * side[2t], side[2t + 1] -- a team word, 0 (a bye), or a preliminary tie's winner (t << 8 | FFh). */
typedef struct {
    uint8_t  np;
    uint16_t side[2 * (SR_CUPD_PRE + SR_CUPD_R1)];
} sr_cupd_rounds;

void sr_cupd_end_reset(sr_cupd_end *e);
/* A club's final place (0 the top of `clubs`) in `division` (Serie D: first .. first + groups - 1). */
void sr_cupd_keep_place(sr_cupd_end *e, uint8_t first, uint8_t groups, uint16_t division, uint16_t slot,
                        int32_t place, int32_t clubs);
void sr_cupd_note_arrival(sr_cupd_end *e, uint16_t slot);

/* A later season's rounds 0-1: Serie D's clubs this season (base[slot] + their move: division[slot],
 * FFh none) in entry order -- from the season end e (0: unknown, a random order) -- relegated
 * from Serie C and the pool's new clubs first, then from the bottom of each group --, paired
 * within their groups where they can. season: the Mod's season byte (the year from 1900), its
 * generator's seed; word[slot] a slot's team word; rounds the cup's. The number of Serie D's
 * clubs. */
int32_t sr_cupd_make(const sr_cupd_end *e, uint32_t season, uint8_t rounds, uint8_t first, uint8_t groups,
                  int32_t nslots, const uint8_t *division, const uint16_t *word, sr_cupd_rounds *out);
/* (-1: rounds outside 2..8 -- a first round of more than 64 ties --, or first + groups past the
 * league's divisions: nothing made) */

/* ---- Parallel groups at the season end (sr_groups.c: gplay.c pg_one / pg_season, the Mod 4.0.5,
 * with 4.0.6's pg_a2 guard) ---- */

#define SR_PG_LEVELS   8            /* a league's levels (gplay.c PL) */
#define SR_PG_GROUPS   9            /* a level's groups side by side (PG: Serie D 2026-27's nine) */
#define SR_PG_PARALLEL 4            /* levels of groups whose areas a club carries */

/* One level of the league's chain, top-down: a single division (groups 1) or groups side by side,
 * divisions first .. first + groups - 1, each size[g] clubs. */
/* chain (not in the Mod: 0 for its leagues): when both choices of a club coming in are full and no club
 * there can step aside, a club of a full choice may go to its other choice by making room there in turn
 * (one step deeper), before the nearest-group fallback -- for a level whose groups are not in a line
 * (England's step 3: N C S I). */
typedef struct { uint8_t first, groups; uint8_t size[SR_PG_GROUPS]; uint8_t chain; } sr_pg_level;

/* A club that can play at a level of groups: its division in its team file (`base`: a division
 * offset is from it) and its area byte at each level of groups, top-down (the Mod's area_byte):
 * low nibble its 1st choice of group (A = 0), high nibble its 2nd + 1 -- 0 or Fh no 2nd, Eh any
 * group (a second team), + 8 never another group ('C,B!'); FFh no area. A swapped pool slot gives
 * its new identity's areas (the Mod's ripool ri_ida). */
typedef struct { uint8_t base; uint8_t area[SR_PG_PARALLEL]; } sr_pg_club;

typedef struct {
    uint8_t            levels;
    const sr_pg_level *level;
    uint16_t           clubs;       /* at most SR_CLUB_SLOTS, in the table's order (it decides) */
    const sr_pg_club  *club;
} sr_pg_league;

/* The game has moved every club one division up or down the league's chain (its divisions'
 * promoted / relegated counts). Each move becomes one LEVEL: into a single division as it is;
 * into a level of groups, the club's 1st choice, then its 2nd -- each with room, else room made by
 * moving a club that may go elsewhere --, else the group nearest to its 1st choice with room
 * (schema/v1.md, "Parallel leagues by area"). Clubs that stay are counted in their group first.
 * old_off[i] / new_off[i]: club i's division offset (division - base) before the season end and
 * after the game's move -- new_off rewritten. entering (0: none): nonzero for a slot taking a club
 * from below the chain this season end (the off-file pool's swap, the Mod's ri_og), which enters
 * the lowest level. 0; -1 a league past the limits above (nothing changed). */
int32_t sr_pg_season(const sr_pg_league *pg, const int8_t *old_off, int8_t *new_off, const uint8_t *entering);

/* The summer re-allocation of a level of two groups (England's National League North / South, Davide
 * 07/11 B): after sr_pg_season's moves, every club of level l -- the ones that stay too -- placed
 * again by its ground. Strict clubs (no 2nd choice in the level, or a ban) go to their 1st choice;
 * the flexible ones (a 2nd choice, or any group) by rank[i] -- the ground's north-south rank, 0 the
 * northernmost, SR_RANK_NONE none: placed as a strict club -- north first, ties by ident[i] (the club's
 * identity, NULL: its index), group 0 filled to its size, the rest into group 1. A group's strict
 * clubs past its size cross by rank too (the ones nearest the other group). Same clubs, same split,
 * whatever their slots. new_off rewritten for the level's clubs. The count of strict clubs that
 * crossed (0 with real data); -1 a level that is not one of two groups, or more clubs than room. */
#define SR_RANK_NONE 0xFF
int32_t sr_pg_realloc(const sr_pg_league *pg, int32_t l, const uint8_t *rank, const uint16_t *ident,
                      int8_t *new_off);

/* ---- A light pool's divisions (sr_pool.c: England's step 3, Davide 07/11 B) ----
 * Clubs with no league slot: a name, a strength, an area and a prebuilt record. Each season their
 * divisions are simulated from strength and the season's seed (no matches played), then each sends
 * up its champion and its play-off winner (2nd-7th, the National League's shape). */

#define SR_LP_DIVS  4               /* a pool's divisions */
#define SR_LP_CLUBS 24              /* a pool division's clubs */

/* strength: hundredths of the club's built first-16 mean (step 3 2.50 .. 0.00; a club down from N / S
 * keeps its own, 3.06 and up: the favourite it is), the scale linear to 3.75; ident: the club's
 * identity (its pool record), which also seeds its matches */
typedef struct { uint16_t ident; uint16_t strength; } sr_lp_club;

/* The light tables' spread: a match's strength 30 .. 30 + SR_LP_SPREAD over 0.00 .. 2.50 (squared). Calibrated
 * (Davide 08/11, D) so that a club relegated from N / S goes straight back up as often as in reality: 7.3%, 4 of 55
 * over 2011-12 .. 2023-24 (en.wikipedia; the Mod's internal/nonleague2627/bounce). */
#ifndef SR_LP_SPREAD
#define SR_LP_SPREAD 40
#endif

/* Division d's light table of n clubs (n <= SR_LP_CLUBS), a double round robin: each match's goals by
 * the two strengths (home advantage, draws ~ 1 in 4) from seed, d and the two idents only -- the same
 * clubs give the same table whatever their order. out[0 .. n), best first (points, goal difference,
 * goals, then the higher strength, then the lower ident): team = the club's index in c, glob = its
 * ident. 0; -1 n out of range. */
int32_t sr_lp_table(uint32_t seed, int32_t d, const sr_lp_club *c, int32_t n, sr_row *out);

/* The play-off of a light table (2nd-7th: quarter-finals 4v7 and 5v6, semi-finals 2 and 3 v the
 * winners -- 2 v the lower placed --, the final; one match each, the better placed at home, extra time
 * and penalties near a coin toss): the winner's place in the table (1..6). n < 7: no play-off, -1. */
int32_t sr_lp_playoff(uint32_t seed, int32_t d, const sr_row *table, int32_t n, const sr_lp_club *c);

/* A pool season: nd divisions' light tables, then each one's champion and play-off winner up.
 * up[2 * d] / up[2 * d + 1]: their indices in division d's c[d] (champion, then play-off winner).
 * 0; -1 a division out of range. */
/* A club down from the level above (N / S) into the pool (Davide 08/11, D): the strength at its own position in
 * the level above's strength order (above[0 .. na), strongest first) taken at the same position of the pool's
 * (pool[0 .. np), strongest first), both linear between neighbours. -1 lists of fewer than 2. */
int32_t sr_lp_rank_strength(uint16_t own, const uint16_t *above, int32_t na, const uint16_t *pool, int32_t np);

int32_t sr_lp_season(uint32_t seed, int32_t nd, const sr_lp_club *const *c, const int32_t *n, int32_t *up);

/* ---- sr_stage.c: the generic stages a spec's competitions are made of (swc/1.2: round_robin, groups,
 * knockout / playoff rounds, single_match), as the competition editor writes them. Nothing in either
 * game plays them yet; the codes are swcc's dense ones (format/swc_format.h SwcTieBreak / SwcDecider,
 * checked equal by tests/rules/stage.c). A stage's clubs are 0 .. n-1 in the caller's order. */
#define SR_STAGE_CLUBS 64
enum { SR_TB_END = 0, SR_TB_POINTS, SR_TB_GOAL_DIFFERENCE, SR_TB_GOALS_FOR, SR_TB_GOALS_AGAINST, SR_TB_WINS,
       SR_TB_AWAY_WINS, SR_TB_AWAY_GOALS_FOR, SR_TB_H2H_POINTS, SR_TB_H2H_GOAL_DIFFERENCE, SR_TB_H2H_GOALS_FOR,
       SR_TB_H2H_AWAY_GOALS, SR_TB_FAIR_PLAY, SR_TB_COEFFICIENT, SR_TB_PREVIOUS_STAGE_RANK, SR_TB_PLAY_OFF_MATCH,
       SR_TB_DRAW_LOTS, SR_TB_NAME, SR_TB_FAITHFUL_1996 };
enum { SR_D_END = 0, SR_D_AGGREGATE, SR_D_AWAY_GOALS, SR_D_EXTRA_TIME, SR_D_PENALTIES, SR_D_BETTER_PLACED,
       SR_D_REPLAY, SR_D_DRAW_LOTS, SR_D_GOLDEN_GOAL };

/* A match of a stage: its matchday (0-based), the two clubs; the score once played. */
typedef struct { uint8_t md, home, away, played, hg, ag; } sr_match;

/* A round robin's calendar (the circle method): every pair meets `legs` times (1..4), one match per club
 * per matchday, a club resting each matchday when n is odd; leg 2 the mirror of leg 1 (home and away
 * swapped), legs 3-4 again legs 1-2. Matchdays: legs * (n - 1), legs * n when n is odd. Each club at home
 * (n - 1) / 2 or n / 2 times a leg. Returns the matches written (legs * n * (n - 1) / 2), -1 for n outside
 * 2..SR_STAGE_CLUBS, legs outside 1..4, or room short. */
int32_t sr_rr_calendar(int32_t n, int32_t legs, sr_match *out, int32_t room);

/* A table's row: the club (0 .. n-1) and its record from the played matches. */
typedef struct { uint8_t club, pad; int16_t pts; uint16_t pl, w, d, l, gf, ga, aw, agf; } sr_srow;

/* The table of n clubs from the played matches (points win / draw / loss), ranked by the tie-breaker list
 * (SR_TB_*, 0-ended; the head-to-head criteria compare the tied clubs' matches among themselves, then the
 * next criterion; draw lots from the seed and the club, so the input order never matters; 'name' = the
 * caller's order). Returns n, or -1: a criterion the stage cannot use alone (fair play, coefficient,
 * previous stage rank, play-off match, faithful_1996: they need the game's data), a club outside 0..n-1. */
int32_t sr_rr_table(int32_t n, const sr_match *m, int32_t nm, int32_t win, int32_t draw, int32_t loss,
                    const uint8_t *tieb, uint32_t seed, sr_srow *out);

/* A knockout's rounds: ties and byes per round, entrants the clubs that reach the first. Returns the clubs
 * left at the end; -(k + 1) when round k does not take the clubs that reach it (2 * ties + byes), as swcc. */
typedef struct { uint16_t ties, byes; } sr_round_size;
int32_t sr_ko_left(int32_t entrants, const sr_round_size *r, int32_t nr);

/* One tie: its legs (side 0's and side 1's goals, the home side, 2 = neutral), extra time and penalties if
 * played (after the last leg), the sides' places (better_placed: the lower wins). The aggregate score
 * first; level, the deciders in order. Returns the winning side 0 / 1, SR_TIE_REPLAY when the list says
 * 'replay', SR_TIE_OPEN when still level (a decider not played yet, or none left). */
enum { SR_TIE_REPLAY = 2, SR_TIE_OPEN = -1 };
typedef struct { uint8_t g[2], home; } sr_leg;
typedef struct { uint8_t et_played, et[2], pens_played, pens[2]; uint8_t place[2]; } sr_tie_extra;
int32_t sr_ko_tie(const sr_leg *leg, int32_t nlegs, const sr_tie_extra *x, const uint8_t *deci, uint32_t seed);

/* Group sizes for n clubs in g groups when the spec gives none: as even as can be, the larger first
 * (29 in 3: 10, 10, 9). Returns g, -1 when g < 1 or a group would have fewer than 2 clubs. */
int32_t sr_group_sizes(int32_t n, int32_t g, uint8_t *size);

#endif
