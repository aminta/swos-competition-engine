# swos-competitions

A deterministic football competition engine (`engine/`, C99 freestanding) and its spec compiler / validator
(`swcc/`): competitions are declared in JSON (schema `swc/1`, `schema/v1.md`), checked against a host profile
(`profiles/`), and compiled into a compact binary spec the engine reads in place.

Hosts: the SWOS 96/97 Mod (the 1996 DOS exe, i486, no libc) and the OpenSWOS fork. The same engine source is
compiled for each host and natively for tests.

```
make            # swcc, libswcc.so, tests (engine unit tests, valid / broken specs, i486 object check)
build/swcc validate SPEC.json [--profile profiles/swos9697.json]
build/swcc compile  SPEC.json -o OUT.SWC
build/swcc dump     OUT.SWC
```

Status: S0 (foundation) — arena, PCG32, binary format v1, spec reader, validator. Plan: `PLAN.md` in the SWOS 96/97
Mod (`notes/engine/PLAN.md`).

Licence: MIT (`LICENSE`). Written from scratch: contains no OpenSWOS code and no game data.
