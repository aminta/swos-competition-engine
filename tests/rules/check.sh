#!/bin/sh
# tests/rules/check.sh -- the rules library against the SWOS 96/97 Mod's own C: both over the
# same random seasons, every result compared (docs/rules-library.md, "Tests").
#
#   MOD=<a swos-9697-mod checkout> sh tests/rules/check.sh [SEASONS]    (default 20000)
#
# The Mod's tools/*.c are built as they are; skipped (exit 0) without a checkout.
set -eu
N=${1:-20000}
MOD=${MOD:-}
CC=${CC:-cc}
if [ -z "$MOD" ] || [ ! -f "$MOD/tools/dplay.c" ]; then
    echo "SKIP: tests/rules/check.sh needs MOD=<a swos-9697-mod checkout>"
    exit 0
fi
T=tests/rules
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
C="$CC -O1 -w"

# The Italian rules (sr_italy = dplay.c dp_season): Serie B and Serie C's play-offs, play-outs.
$C -DDPLAY_C="\"$MOD/tools/dplay.c\"" -o "$OUT/italy_mod" $T/italy_mod.c
$C -Irules -o "$OUT/italy_ours" $T/italy_ours.c rules/sr_dplay.c
"$OUT/italy_mod" "$N" > "$OUT/italy_mod.txt"
"$OUT/italy_ours" "$N" > "$OUT/italy_ours.txt"
if cmp -s "$OUT/italy_mod.txt" "$OUT/italy_ours.txt"; then
    echo "PASS: the Italian rules, $N seasons, $(awk '{ m += NF - 1 } END { print m }' "$OUT/italy_ours.txt") moves, every one the Mod's"
else
    echo "FAIL: the Italian rules: the first season that differs (Mod, then the library):"
    diff "$OUT/italy_mod.txt" "$OUT/italy_ours.txt" | head -4
    exit 1
fi

# The season end (sr_italy, then sr_knockouts = gplay.c gp_season, whole): Italy 2026-27 first --
# both steps, every move, result, stage title and SEASON WINNERS page --, then every pack's
# knockouts (tests/rules/specs: the Mod's compile_spec bytes of each World 2026-27 league, no
# names) over random tables of that league's shape, its stage tables given.
$C -DDPLAY_C="\"$MOD/tools/dplay.c\"" -c -o "$OUT/kmod_dp.o" $T/knockouts_mod_dp.c
$C -DGPLAY_C="\"$MOD/tools/gplay.c\"" -c -o "$OUT/kmod_gp.o" $T/knockouts_mod_gp.c
$C -o "$OUT/kmod" $T/knockouts_mod.c "$OUT/kmod_dp.o" "$OUT/kmod_gp.o"
$C -Irules -o "$OUT/kours" $T/knockouts_ours.c rules/sr_dplay.c rules/sr_gplay.c
"$OUT/kmod" $T/specs/ita.spec "$N" > "$OUT/k_mod.txt"
"$OUT/kours" $T/specs/ita.spec "$N" > "$OUT/k_ours.txt"
if cmp -s "$OUT/k_mod.txt" "$OUT/k_ours.txt"; then
    echo "PASS: Italy's season end, $N seasons, $(awk '{ m += NF - 1 } END { print m }' "$OUT/k_ours.txt") moves and results, every one the Mod's"
else
    echo "FAIL: Italy's season end: the first season that differs (Mod, then the library):"
    diff "$OUT/k_mod.txt" "$OUT/k_ours.txt" | head -4
    exit 1
fi
PN=$((N / 10)); np=0; bad=""
while read -r code shape; do
    "$OUT/kmod" "$T/specs/$code.spec" "$PN" "$shape" ko > "$OUT/p_mod.txt"
    "$OUT/kours" "$T/specs/$code.spec" "$PN" "$shape" ko > "$OUT/p_ours.txt"
    cat "$OUT/p_ours.txt" >> "$OUT/p_all.txt"
    if cmp -s "$OUT/p_mod.txt" "$OUT/p_ours.txt"; then np=$((np + 1)); else
        bad="$bad $code"
        [ -n "${VERBOSE:-}" ] && { echo "  $code ($shape):"; diff "$OUT/p_mod.txt" "$OUT/p_ours.txt" | head -3; }
    fi
done < $T/specs/shapes.txt
if [ -z "$bad" ]; then
    echo "PASS: every pack's knockouts ($np leagues), $PN seasons each: every move, champion, stage title and SEASON WINNERS page the Mod's; the career club's contest built as its ko_build ($(grep -o "K[0-9]*:" "$OUT"/p_all.txt | wc -l) contests, their round-1 ties simulated at the build), every club's play-off chosen as its gp_need"
else
    echo "FAIL: the knockouts of$bad differ from the Mod's ($np equal; VERBOSE=1 for the first seasons)"
    exit 1
fi

# The season's moves and the B-team rules (sr_final_moves, sr_b_teams = gplay.c sec_post): built
# cases, then random ones against the Mod's sec_post -- Italy (levels of groups) and the other
# countries with B teams (Spain, Germany, the Netherlands, Portugal).
$C -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/bmod" $T/bteams_mod.c
$C -Irules -o "$OUT/bours" $T/bteams_ours.c rules/sr_post.c
"$OUT/bours" || exit 1
"$OUT/bmod" "$N" > "$OUT/b_mod.txt"
"$OUT/bours" random "$N" > "$OUT/b_ours.txt" 2> "$OUT/b.log"
if cmp -s "$OUT/b_mod.txt" "$OUT/b_ours.txt"; then
    echo "PASS: B teams, $N cases in each of Italy, Spain, Germany, the Netherlands, Portugal ($(cut -d' ' -f1 "$OUT/b.log") changed by the rules), every move the Mod's"
else
    echo "FAIL: B teams: the first case that differs (Mod, then the library):"
    diff "$OUT/b_mod.txt" "$OUT/b_ours.txt" | head -4
    exit 1
fi

# A cup's clubs from the second season on (sr_cup_entrants = gplay.c cf_build, CF_SEED 1 as
# released): Italy 2026-27's two cups over random moves, last season's places and career clubs.
$C -DCF_SEED=1 -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/cmod" $T/cups_mod.c
$C -Irules -o "$OUT/cours" $T/cups_ours.c rules/sr_cups.c
"$OUT/cmod" "$N" > "$OUT/c_mod.txt"
"$OUT/cours" "$N" > "$OUT/c_ours.txt"
if cmp -s "$OUT/c_mod.txt" "$OUT/c_ours.txt"; then
    echo "PASS: cups, $N seasons, both cups' clubs the Mod's"
else
    echo "FAIL: cups: the first season that differs (Mod, then the library):"
    diff "$OUT/c_mod.txt" "$OUT/c_ours.txt" | head -4
    exit 1
fi
