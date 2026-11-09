#!/bin/sh
# tests/rules/frames.sh -- the library's stack frames on the DOS build (MC 09/11: the engine runs on the game's
# small DOS stack): every function's locals <= 512 bytes, the exceptions listed in tests/rules/frames.txt with
# their size today (a listed one may shrink, not grow; a new one over 512 fails). The format: "function bytes", one a
# line (lines starting with # are notes). Exceptions as of 2026-11-09: the Mod ports and the light pools (their
# tables on the stack, as in the Mod), reported to MC; sr_reg_season takes a caller-owned sr_reg_work instead.
#   sh tests/rules/frames.sh CC CFLAGS...
set -eu
CC=$1; shift
fail=0; n=0
for f in rules/*.c; do
    "$CC" "$@" -Wframe-larger-than=512 -c -o /dev/null "$f" 2>&1 |
        sed -n "s/.*stack frame size (\([0-9]*\)) exceeds limit (512) in '\([A-Za-z0-9_]*\)'.*/\2 \1/p"
done > "${TMPDIR:-/tmp}/frames.$$"
while read -r fn sz; do
    n=$((n + 1))
    max=$(awk -v f="$fn" '$1 == f && $1 !~ /^#/ { print $2 }' tests/rules/frames.txt)
    if [ -z "$max" ]; then echo "FAIL: $fn keeps $sz bytes of locals (at most 512; not a listed exception)"; fail=1
    elif [ "$sz" -gt "$max" ]; then echo "FAIL: $fn keeps $sz bytes of locals (listed at $max)"; fail=1; fi
done < "${TMPDIR:-/tmp}/frames.$$"
rm -f "${TMPDIR:-/tmp}/frames.$$"
[ $fail = 0 ] && echo "PASS: stack frames <= 512 bytes, $n listed exception(s) (tests/rules/frames.txt)"
exit $fail
