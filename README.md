# SWOS Competition Engine

A deterministic, declarative competition engine for Sensible World of Soccer: leagues, cups and continental
competitions of any country and era described as data (JSON specs), played by one portable C engine.

> **In breve (IT).** Motore delle competizioni per SWOS: campionati, coppe e coppe continentali di qualsiasi
> nazione ed epoca descritti come dati (specifiche JSON) e giocati da un unico motore in C. Sarà usato da
> [OpenSWOS](https://github.com/benbaker76/OpenSWOS) come submodule e dal
> [SWOS 96/97 Mod](https://github.com/aminta/swos-9697-mod). Stato: progetto e specifiche; il codice arriva nelle
> prossime settimane.

**Status: S0 (foundation).** Schema v1 primitives frozen (`schema/v1.md`); engine core (arena, PCG32, binary spec
reader) and the spec compiler / validator `swcc`. No season logic yet (S1 next). Not ready for use.

## Layout and build

| Path | Content |
|---|---|
| `engine/` | the engine: C99 freestanding (`swc.h`, `swc_core.c`); compiled into each host and natively |
| `swcc/` | the spec compiler / validator (hosted C11): JSON + host profile -> diagnostics + binary spec `.SWC` |
| `schema/v1.md` | spec schema swc/1: the frozen primitives list, what S0 validates |
| `profiles/` | host limits (`swos9697.json`: the 1996 DOS exe's font, title widths, clubs per file, table rows) |
| `tests/` | engine unit tests, valid and broken specs (each with its expected message) |
| `docs/` | design documents |

```
make            # build/swcc, build/libswcc.so, the tests, the i486 freestanding object check
build/swcc validate SPEC.json [--profile profiles/swos9697.json]
build/swcc compile  SPEC.json -o OUT.SWC
build/swcc dump     OUT.SWC
```

## Who uses it

- **[OpenSWOS](https://github.com/benbaker76/OpenSWOS)** (Ben Baker's C port of SWOS), as a git submodule.
  World 2026-27 is planned as the default there, with SWOS 96/97 one switch away.
- **[SWOS 96/97 Mod](https://github.com/aminta/swos-9697-mod)** (Davide Lorigliola), linked into the original
  1996 DOS executable.
- The engine itself is also built natively for tests and tools.

## Design in short

- **Declarative**: a competition is an ordered list of stages (round robin, groups, league phase, knockout,
  play-off/play-out, split, aggregate table, promedio...), with named tie-breakers, deciders, outcomes and links
  between competitions and seasons. Worlds are lists of **modules** (one per country or confederation) that
  connect only through named **ports**, so a module can be added or removed without touching the others.
- **Deterministic**: same specs + same seed + same results = the same season, byte for byte. Golden-file tests.
- **Portable C99, freestanding**: no libc, fixed-width integers, no floats in decisions; small enough for a
  486 and an Amiga, built into OpenSWOS, into the DOS mod, and natively.
- **Compiled specs**: JSON is the source; a spec compiler produces a compact, versioned binary form for small hosts.
- **Persistent state with versioning**: hosts save the engine state (named JSON fields in OpenSWOS, a versioned
  trailer in the DOS mod); saves record the data pack and its version.
- **User-authored specs**: strict validation with clear, located error messages.
- **Every world in every mode**: a world registers its teams and competitions once; career, friendly, season and
  preset competitions all list them.
- **No original game data** in this repository: data packs contain only new data, each with its own licence and
  credits.

## Documents

| Document | Content |
|---|---|
| [docs/competition-engine-proposal.md](docs/competition-engine-proposal.md) | spec model, API, determinism, persistence, four real formats (Liga MX, Serie C, UEFA Champions League, Argentina) |
| [docs/rule-catalogue.md](docs/rule-catalogue.md) | modules and ports, every rule primitive (from the Mod's World 2026-27), entitlements and precedence, a complete CONCACAF module, the validator |
| [docs/testing-strategy.md](docs/testing-strategy.md) | test pyramid: validation, unit tests per primitive, properties, forced scenarios, golden worlds, few end-to-end tests |
| [docs/lessons-from-the-mod.md](docs/lessons-from-the-mod.md) | real bugs met by the Mod and how the engine prevents each |
| [docs/ai-development-guidelines.md](docs/ai-development-guidelines.md) | how AI-assisted work is done here: contract first, tests, small reviewed PRs |

Background (how OpenSWOS, the original DOS game and the Mod run competitions today):
[OpenSWOS convergence docs](https://github.com/aminta/OpenSWOS/tree/convergence-docs/docs/convergence).
The documents were written for the OpenSWOS convergence and still refer to OpenSWOS paths in places; they will be
adapted as the design is frozen here.

## Licence

[MIT](LICENSE). Compatible with OpenSWOS's GPL-3.0-or-later. Data packs are licensed separately.
