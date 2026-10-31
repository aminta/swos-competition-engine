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
