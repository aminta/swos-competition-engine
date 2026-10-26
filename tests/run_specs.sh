#!/bin/sh
# Every tests/specs/ok/*.json must validate, compile and read back; every tests/specs/bad/*.json must fail with the
# message in its "_expect" member (bad/syntax.json: a syntax error).
SWCC=$1; TMP=${TMPDIR:-/tmp}/swcc-tests.$$; mkdir -p "$TMP"; fail=0
for f in tests/specs/ok/*.json; do
  if ! "$SWCC" compile "$f" -o "$TMP/out.swc" >/dev/null 2>"$TMP/err" || ! "$SWCC" dump "$TMP/out.swc" >/dev/null; then
    echo "FAIL (should be valid): $f"; cat "$TMP/err"; fail=1; fi
done
for f in tests/specs/bad/*.json; do
  exp=$(sed -n 's/.*"_expect": "\(.*\)".*/\1/p' "$f" | sed 's/\\"/"/g'); [ -z "$exp" ] && exp="(syntax)"
  if "$SWCC" validate "$f" >/dev/null 2>"$TMP/err"; then echo "FAIL (should be refused): $f"; fail=1
  elif ! grep -qF -- "$exp" "$TMP/err"; then echo "FAIL (message): $f: expected: $exp"; cat "$TMP/err"; fail=1; fi
done
rm -rf "$TMP"
[ $fail = 0 ] && echo "run_specs: $(ls tests/specs/ok/*.json | wc -l | tr -d ' ') valid, $(ls tests/specs/bad/*.json | wc -l | tr -d ' ') refused with the expected message"
exit $fail
