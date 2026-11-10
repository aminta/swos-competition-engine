#!/bin/sh
# swc/1.5.1 mutant test of the linked-entrant quota bound (swcc check_draw_links): each mutant must change a verdict.
#   M1 the plain sum of places (no min with the sender's clubs of the country): v151_linked_ok refused
#   M2 the country bound compared with groups + 1: v151_linked_quota accepted
#   M3 the clique bound compared with groups + 1: v151_linked_clique accepted
#   M4 (v1.6) max_per_group 2 bounded as 1 (g, not 2g): v16_caf_cup refused
#   M5 (v1.6) max_per_group 2 bounded at 2g + 1: v16_cap_too_big accepted
# usage: sh tests/specs/qb_mutant.sh "CC CFLAGS" PROFILE FORMAT_SOURCES
CC=$1; PROFILE=$2; shift 2; FORMAT="$*"
TMP=${TMPDIR:-/tmp}/qb_mutant.$$; mkdir -p "$TMP"; fail=0
mut() {   # name, sed program, spec, expect (valid / refused)
  sed "$2" swcc/swcc.c > "$TMP/swcc.c"
  if cmp -s swcc/swcc.c "$TMP/swcc.c"; then echo "FAIL: mutant $1 did not apply"; fail=1; return; fi
  $CC -DSWCC_DEFAULT_PROFILE="\"$PROFILE\"" -Iswcc -Iformat -o "$TMP/swcc" "$TMP/swcc.c" swcc/json.c swcc/swcc_main.c $FORMAT 2>"$TMP/cc" || { echo "FAIL: mutant $1 does not build"; cat "$TMP/cc"; fail=1; return; }
  if "$TMP/swcc" validate "$3" >/dev/null 2>&1; then got=valid; else got=refused; fi
  if [ "$got" = "$4" ]; then echo "  mutant $1: $3 $got (caught)"; else echo "FAIL: mutant $1: $3 $got, the tests would not see it"; fail=1; fi
}
mut M1 '/check_draw_links/,/^}/s/sum += n < h ? n : h;/sum += n;/' tests/specs/ok/v151_linked_ok.json refused
mut M2 '/check_draw_links/,/^}/s/: bnd\[j\] > g)/: bnd[j] > g + 1)/' tests/specs/bad/v151_linked_quota.json valid
mut M3 '/check_draw_links/,/^}/s/if (pairs == m \* (m - 1) \/ 2 \&\& sum > g)/if (pairs == m * (m - 1) \/ 2 \&\& sum > g + 1)/' tests/specs/bad/v151_linked_clique.json valid
mut M4 's/if (d.separate \& SWC_SEP_CAP2 ? cnt\[j\] > 2 \* g : cnt\[j\] > g)/if (cnt[j] > g)/' tests/specs/ok/v16_caf_cup.json refused
mut M5 's/if (d.separate \& SWC_SEP_CAP2 ? cnt\[j\] > 2 \* g : cnt\[j\] > g)/if (d.separate \& SWC_SEP_CAP2 ? cnt[j] > 2 * g + 1 : cnt[j] > g)/' tests/specs/bad/v16_cap_too_big.json valid
rm -rf "$TMP"
[ $fail = 0 ] && echo "qb_mutant: 5 mutants of the 1.5.1 quota bound and the 1.6 cap bound, each caught by its spec"
exit $fail
