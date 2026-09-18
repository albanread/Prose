#!/bin/bash
# pkg-batch.sh — build every cloned package, recording pass/fail with the
# first error line into packages/RESULTS.md.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/packages/src"
: > "$ROOT/packages/RESULTS.md"
pass=0 fail=0
for d in */; do
	n=${d%/}
	if [ -f "$ROOT/packages/out/$n/$n" ]; then
		printf '| %s | prebuilt |\n' "$n" >> "$ROOT/packages/RESULTS.md"
		continue
	fi
	out=$(timeout 300 "$ROOT/scripts/pkg-build.sh" "$n" 2>&1)
	if [ -f "$ROOT/packages/out/$n/$n" ]; then
		printf '| %s | PASS |\n' "$n" >> "$ROOT/packages/RESULTS.md"
		pass=$((pass+1))
	else
		err=$(echo "$out" | grep -m1 -E "error:|No rule to make|No such file|not found|Stop\." | head -c 120)
		printf '| %s | FAIL | %s |\n' "$n" "$err" >> "$ROOT/packages/RESULTS.md"
		fail=$((fail+1))
	fi
	echo "$n done ($pass pass / $fail fail so far)"
done
{
	echo "# Batch build results ($(date '+%Y-%m-%d %H:%M'))"
	echo
	echo "| package | result | first error |"
	echo "|---|---|---|"
	cat "$ROOT/packages/RESULTS.md.tmp" 2>/dev/null
} > /dev/null
echo "BATCH DONE: pass=$pass fail=$fail"
