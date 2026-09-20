#!/bin/bash
# ProseWriter guest smoke — the harness-level test matrix, run from the
# repo root with the VM already up (prosewriter/vm/run.sh + wait-boot):
#
#   prosewriter/tests/guest-smoke.sh
#
# Covers: selftest, launch+activate, set/get Text, save (title + clean),
# relaunch-with-file, clean quit, no stale teams. Prints TAP-style lines
# and leaves a screenshot of the end state under vm/run/.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GUEST="$ROOT/prosewriter/vm/guest.sh"
QMP="$ROOT/prosewriter/vm/qmp.py"
APP=/boot/home/apps/ProseWriter
SIG=application/x-vnd.prose.ProseWriter
OUT="$ROOT/prosewriter/vm/run"
mkdir -p "$OUT"
pass=0; fail=0; n=0
ok()  { echo "ok $n - $1"; pass=$((pass+1)); }
bad() { echo "not ok $n - $1"; fail=$((fail+1)); }

killapp()
{
	"$GUEST" run 'ps' 2>/dev/null | grep -a "apps/ProseWriter" | awk '{print $2}' \
		| while read tid; do "$GUEST" run "kill $tid" >/dev/null 2>&1; done
	sleep 1
}

# 1 - in-process selftest
n=1
r=$("$GUEST" run "$APP --selftest" 2>&1 | grep -a "SELFTEST")
echo "$r" | grep -aq "PASS" && ok "selftest ($r)" || bad "selftest ($r)"

# 2 - launch + activate (background-launched windows are never activated
#     on this guest; Activate is the harness's way in)
killapp
n=2
"$GUEST" launch $APP >/dev/null && sleep 3
"$GUEST" run "hey $SIG do Activate" >/dev/null 2>&1
r=$("$GUEST" run "hey $SIG get Title" 2>/dev/null | grep -a result)
echo "$r" | grep -aq Untitled && ok "launch + activate" || bad "launch ($r)"

# 3 - set/get Text (pwquery: mode is argv[4], data is argv[5])
n=3
"$GUEST" run "/boot/home/apps/pwquery $SIG Text X set 'smoke test one'" \
	>/dev/null 2>&1
sleep 1
r=$("$GUEST" run "hey $SIG get Text" 2>/dev/null | grep -a result)
echo "$r" | grep -aq "smoke test one" && ok "set/get Text" \
	|| bad "set/get Text ($r)"

# 4 - save to a path: file written, title takes the name, Modified
#     clears, and the file is TYPED (Tracker can find us without mimeset)
n=4
"$GUEST" run "/boot/home/apps/pwquery $SIG Save X do /tmp/smoke.prose" \
	>/dev/null 2>&1
sleep 1
t=$("$GUEST" run "hey $SIG get Title" 2>/dev/null | grep -a result)
m=$("$GUEST" run "hey $SIG get Modified" 2>/dev/null | grep -a result)
attr=$("$GUEST" run 'catattr BEOS:TYPE /tmp/smoke.prose' 2>/dev/null \
	| head -1 | tr -d '\0\r')
echo "$t" | grep -aq "smoke.prose" && echo "$m" | grep -aq ": 0" \
	&& echo "$attr" | grep -aqi "prosewriter-doc" \
	&& ok "save + title + clean + typed" || bad "save ($t $m $attr)"

# 5 - relaunch with the file as argument: content loads
killapp
n=5
"$GUEST" launch $APP /tmp/smoke.prose >/dev/null && sleep 3
r=$("$GUEST" run "hey $SIG get Text" 2>/dev/null | grep -a result)
echo "$r" | grep -aq "smoke test one" && ok "reopen saved file" \
	|| bad "reopen ($r)"

# 6 - open a second document via the Open property — the exact message
#     the Open panel sends — and back: text + title follow
n=6
"$GUEST" run "/boot/home/apps/pwquery $SIG Text X set 'second document'" \
	>/dev/null 2>&1
"$GUEST" run "/boot/home/apps/pwquery $SIG Save X do /tmp/smoke2.prose" \
	>/dev/null 2>&1
sleep 1
"$GUEST" run "/boot/home/apps/pwquery $SIG Open X do /tmp/smoke.prose" \
	>/dev/null 2>&1
sleep 2
r=$("$GUEST" run "hey $SIG get Text" 2>/dev/null | grep -a result)
t=$("$GUEST" run "hey $SIG get Title" 2>/dev/null | grep -a result)
echo "$r" | grep -aq "smoke test one" && echo "$t" | grep -aq "smoke.prose" \
	&& ok "open via property (panel path)" || bad "open ($t)"

# 7 - a file saved WITHOUT extension must reopen as the document it is
#     (content sniffing, not the name, decides the loader)
n=7
"$GUEST" run "/boot/home/apps/pwquery $SIG Text X set 'no ext test'" \
	>/dev/null 2>&1
"$GUEST" run "/boot/home/apps/pwquery $SIG Save X do /tmp/smoke-noext" \
	>/dev/null 2>&1
sleep 1
"$GUEST" run "/boot/home/apps/pwquery $SIG Open X do /tmp/smoke-noext" \
	>/dev/null 2>&1
sleep 2
r=$("$GUEST" run "hey $SIG get Text" 2>/dev/null | grep -a result)
t=$("$GUEST" run "hey $SIG get Title" 2>/dev/null | grep -a result)
echo "$r" | grep -aq "no ext test" && echo "$t" | grep -aq "smoke-noext" \
	&& ok "extensionless round trip" || bad "noext round trip ($t)"

# 8 - PDF export via scripting (Sprint 10): magic + at least one page
n=8
"$GUEST" run "/boot/home/apps/pwquery $SIG PDF X do /tmp/smoke.pdf" \
	>/dev/null 2>&1
sleep 1
magic=$("$GUEST" run 'head -c 8 /tmp/smoke.pdf; echo' 2>/dev/null \
	| head -1 | tr -d '\0\r')
pages=$("$GUEST" run 'grep -ac MediaBox /tmp/smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
[ "$magic" = "%PDF-1.4" ] && [ "$pages" -ge 1 ] \
	&& ok "pdf export ($pages page(s))" || bad "pdf export ($magic/$pages)"

# 9 - clean quit (unmodified document) exits the app
n=9
"$GUEST" run "hey $SIG do Quit" >/dev/null 2>&1
sleep 2
c=$("$GUEST" run 'ps' 2>/dev/null | grep -ac "apps/ProseWriter")
[ "$c" = "0" ] && ok "clean quit exits" || bad "quit (teams left: $c)"

"$QMP" shot "$OUT/smoke-desk.png" >/dev/null
echo "final screenshot: $OUT/smoke-desk.png"

if [ $fail = 0 ]; then
	echo "=== GUEST SMOKE PASS $pass/$pass ==="
	exit 0
fi
echo "=== GUEST SMOKE FAIL ($fail failed, $pass passed) ==="
exit 1
