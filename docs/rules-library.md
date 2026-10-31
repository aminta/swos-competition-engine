# The rules library (`rules/`)

> **In breve (IT).** Le regole di fine stagione e delle coppe che il Mod e OpenSWOS condividono, in
> una sola copia C: playoff e playout, i knockout dei pack, le squadre B, l'ingresso nelle coppe, la
> Coppa Italia Serie D. Ognuna è il codice del Mod come OpenSWOS l'ha portato, confrontato con il C
> del Mod stagione per stagione (`make rules-check MOD=...`).

Status: 2026-10-31, version 1 being moved in, one module per pull request (§4).

## 1. What it is

The competition rules the SWOS 96/97 Mod and OpenSWOS both play, as one C library both games link:

- **Input**: a league's final tables and its rules as the Mod's tools compile them (`compile_spec`).
- **Output**: each club's move (and the results the rules decide: champions, cup entrants).
- **No engine**: calendars, menus, contests, saves stay each game's own.

Agreed between Davide Lorigliola (the Mod: he owns the rules and their tests) and OpenSWOS (it keeps
the C portable and the interface clean), 2026-10-27; the interface was approved 2026-10-29
(OpenSWOS `docs/RULES_LIBRARY.md` §7).

It is not the declarative engine of `docs/competition-engine-proposal.md`: it is the rules both
games play **today**, the Mod's own code, kept identical between them. The engine can take them as
its reference behaviour.

## 2. The code

Each module is the Mod's tool C (`tools/dplay.c`, `tools/gplay.c`, ...) as OpenSWOS ported it and
checked it against the Mod's own C, season after season: its random numbers, seeds, tie model,
order and types (`int` is 32 bits on every target), so it reads against the Mod's line by line.
What changed in the move here: the file statics are one caller-owned state, `sr_state`.

**The DOS build's limits** (the Mod's `tools/cobj.py`: clang `-target i686`, freestanding,
`-Os -march=i486`, no SSE, cdecl, linked into the exe):

- no undefined symbols: **no libc**, no compiler-generated `memcpy` / `memset` -- `make test`
  builds every module as one i486 object and fails on any undefined symbol;
- **one caller-owned state passed by pointer**, no globals (few absolute references: each is a
  fixup);
- `<stdint.h>` the one include.

## 3. Interface (`rules/swosrules.h`)

```c
sr_state st;            /* the caller's memory */
sr_reset(&st);          /* once */
/* each season end: the final tables into st.t, then */
sr_italy(&st, &league, &season);    /* Serie B, Serie C: st.mv[0 .. st.nmv) */
sr_knockouts(&st, &league, &season);    /* the pack's knockouts: their moves after those */
sr_final_moves(&st, &league, &levels, base);    /* each club's one-level move: st.want[d][k] */
```

The caller keeps, between seasons and in its saves, what the Mod keeps in its state: a league's
stage tables (`sr_stage_bank`, written at each fixture stage's end), Argentina's promedio history
(`sr_promedio`) and the SEASON WINNERS page (`sr_winners`); `sr_season` points at them. What the
career club played itself comes back through `sr_host`'s callbacks (all optional).

| Function | The Mod's | What it does |
|---|---|---|
| `sr_italy` | `dplay.c` `dp_season` | Serie B's play-off and play-out, Serie C's play-off (the career club's played ties and the Coppa Italia Serie C winner given in `sr_season`) |
| `sr_club_playoff` | `gplay.c` `gp_need` | the career club's play-off at its league's end, and the divisions to simulate early for it |
| `sr_contest_entrants`, `sr_contest_seed`, `sr_contest_tie` | `gplay.c` `ko_build` | that play-off's entrants (ranks, points) for the game's contest, and its round-1 ties simulated at the build (presim) |
| `sr_group_shape_of` | `gplay.c` `t2_shape` | a title of groups then knockouts (the Poule Scudetto): its shape, for a group contest |
| `sr_spec_at` | `gplay.c` `po_at` | play-off pidx's bytes |
| `sr_final_moves` | -- | each club's one-level move: the record's by place, the play-offs' on top, then the B-team rules |
| `sr_b_teams` | `gplay.c` `sec_post` | the B-team rules alone (in levels, a level of groups counting as one; the Mod's division deltas are the same moves) |
| `sr_knockouts` | `gplay.c` `gp_season` | every pack's knockouts, whole: each kind and entrant, group rounds, the drawn pot, the promedio; titles, continental places and stage titles in `st.gp.res` / `st.gp.sres`; the SEASON WINNERS page |

## 4. Modules, one pull request each

| # | Module | The Mod's | Test (`tests/rules/check.sh`) |
|---|---|---|---|
| 1 | The Italian rules | `dplay.c` `dp_season` | 20,000 random seasons of Italy 2026-27's divisions: every move the Mod's |
| 2 | The pack's knockouts | `gplay.c` `gp_season` | Italy's whole season end, 20,000 seasons; then each of the 34 leagues with knockouts, 2,000 seasons of its own shape with stage tables: every move, result, stage title and SEASON WINNERS page the Mod's |
| 3 | The career club's contests | `gplay.c` `ko_build`, `gp_need`, `t2_shape` | the same seasons: each play-off's contest built for a waiting club (30,667: entrants, ranks, the round-1 ties simulated), every club's play-off chosen, the Mod's |
| 4 | B teams, the season's moves | `gplay.c` `sec_post` | 5 built cases, then 20,000 random ones in each of Italy (levels of groups), Spain, Germany, the Netherlands, Portugal: every move the Mod's |

## 5. Tests

`make rules-check MOD=<a swos-9697-mod checkout> [SEASONS=n]` builds the Mod's `tools/*.c` as they
are and the library, gives both the same random seasons (`tests/rules/tables.h`: their own LCG, not
`rand()`) and compares every result. Without a checkout it is skipped. `make test` checks the i486
object.

`tests/rules/specs/` holds each World 2026-27 league's knockouts as the Mod's `gplay.py
compile_spec` makes them (`shapes.txt`: their divisions' sizes): bytes of the format only --
entrant places, rounds, legs --, no names, made from the Mod's packs so the test needs no world
data.

## 6. Licence and credits

MIT (`LICENSE`). The rules and their original C: Davide Lorigliola (the SWOS 96/97 Mod, MIT). The
port, the state and the tests: Ben Baker (OpenSWOS), who contributes them here under MIT.
