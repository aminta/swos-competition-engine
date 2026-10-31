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
 * memcpy / memset. <stdint.h> is the one include.
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#ifndef SWOSRULES_H
#define SWOSRULES_H

#include <stdint.h>

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

/* A league's rules: the pack's, as the Mod's tools compile them. */
typedef struct {
    uint8_t        divisions;
    const uint8_t *promoted;        /* per division: the record's counts by place */
    const uint8_t *relegated;
    uint8_t        italy;           /* the Italian rules (dplay.c): Serie B and its groups -- */
    uint8_t        serie_b;         /* Serie B's division, the first of Serie C's groups, */
    uint8_t        group_first;     /* how many groups (2..4) */
    uint8_t        groups;
} sr_league;

/* The season: what the career club played itself (0: simulated) and the results the rules use. */
typedef struct {
    uint16_t po_win, out_win;       /* its Serie B play-off, play-out: the winner's team word */
    uint16_t cpo_win;               /* its Serie C play-off */
    uint16_t cup_winner;            /* the Coppa Italia Serie C winner, 0 unknown */
    uint8_t  test_held;             /* a test: Serie B's play-off and play-out held whatever the points */
} sr_season;

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
} sr_state;

void     sr_reset(sr_state *st);
uint32_t sr_state_size(void);

/* The season end's first step: Serie B and Serie C's play-offs and play-outs (dplay.c dp_season)
 * over st->t, by the league's Italian rules. The moves start again in st->mv (rule
 * SR_RULE_ITALY); their count, 0 when the league is not Italian. */
int32_t sr_italy(sr_state *st, const sr_league *lg, const sr_season *se);

/* Serie B's play-out is held unless the 16th leads the 17th by more than 4 points; its play-off
 * unless the 3rd leads the 4th by more than 14 (dplay.c OUT_HELD and its play-off rule). */
int32_t sr_out_held(uint16_t pts16, uint16_t pts17);
int32_t sr_playoff_held(uint16_t pts3, uint16_t pts4);

#endif
