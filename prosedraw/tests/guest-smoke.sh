#!/bin/bash
# ProseDraw guest smoke — D05 in docs/plan.md. Run from the repo root
# with the VM already up (prosewriter/vm/run.sh + wait-boot):
#
#   prosedraw/tests/guest-smoke.sh
#
# Covers: selftest on the DEPLOYED binary (with a stale-deploy guard —
# the 2026-09-20 session lost an hour to testing a guest copy that
# predated the fix), launch+activate, scripted diagram, save (typed
# HMF1&dDp), relaunch-with-file, Open property, extensionless round
# trip, clean quit. TAP-style output, end-state screenshot in vm/run/.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GUEST="$ROOT/prosewriter/vm/guest.sh"
QMP="$ROOT/prosewriter/vm/qmp.py"
APP=/boot/home/apps/ProseDraw
SIG=application/x-vnd.prose.ProseDraw
PWQ=/boot/home/apps/pwquery
OUT="$ROOT/prosewriter/vm/run"
mkdir -p "$OUT"
pass=0; fail=0; n=0
ok()  { echo "ok $n - $1"; pass=$((pass+1)); }
bad() { echo "not ok $n - $1"; fail=$((fail+1)); }

killapp()
{
	# the ps line may carry a file argument, so the team id is the first
	# all-numeric field after the path, not a fixed column
	"$GUEST" run 'ps' 2>/dev/null | grep -a "apps/ProseDraw" \
		| awk '{for (i = 2; i <= NF; i++) if ($i ~ /^[0-9]+$/) {print $i; exit}}' \
		| while read tid; do "$GUEST" run "kill $tid" >/dev/null 2>&1; done
	sleep 1
}

count()
{
	# the reply line is `"result" (B_INT32_TYPE) : N (0xNNNN)` — take the
	# number after the colon, or "32" inside "INT32" wins
	"$GUEST" run "hey $SIG get ShapeCount" 2>/dev/null | grep -a result \
		| sed -n 's/.*: \([0-9][0-9]*\).*/\1/p' | head -1
}

title()
{
	"$GUEST" run "hey $SIG get Title of Window 0" 2>/dev/null | grep -a result
}

# 1 - selftest on the deployed binary, and the deployed binary IS the
#     host build (sizes must match: a stale guest copy invalidates
#     every later step)
n=1
r=$("$GUEST" run "$APP --selftest" 2>&1 | grep -a "SELFTEST")
host=$(stat -f%z "$ROOT/prosedraw/app/ProseDraw" 2>/dev/null)
guest=$("$GUEST" run "ls -l $APP" 2>/dev/null | awk '{print $5}' | head -1 \
	| tr -d '\r\n')
[ -n "$host" ] && [ "$host" = "$guest" ] && r="$r deploy-fresh($host)"
echo "$r" | grep -aq "PASS" && echo "$r" | grep -aq "deploy-fresh" \
	&& ok "selftest + fresh deploy ($r)" || bad "selftest/deploy ($r h=$host g=$guest)"

# 2 - launch + activate + empty document (background-launched windows
#     are never activated on this guest; Activate is the harness's way in)
killapp
n=2
"$GUEST" launch $APP >/dev/null && sleep 3
"$GUEST" run "hey $SIG do Activate" >/dev/null 2>&1
c=$(count); t=$(title)
[ "$c" = "0" ] && echo "$t" | grep -aq Untitled && ok "launch + activate" \
	|| bad "launch (count=$c title=$t)"

# 3 - scripted diagram: three shapes + one connector (D03 again, scripted)
n=3
"$GUEST" run "$PWQ $SIG AddShape X do 'rrect 80 120 120 64|Input'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG AddShape X do 'diamond 260 110 140 84|Valid?'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG AddShape X do 'ellipse 460 120 100 64|Output'" >/dev/null 2>&1
sleep 1
"$GUEST" run "$PWQ $SIG AddShape X do connect" >/dev/null 2>&1
sleep 1
c=$(count)
[ "$c" = "4" ] && ok "scripted diagram (4 shapes incl. connector)" \
	|| bad "diagram (count=$c)"

# 4 - save: HMF1&dDp magic, typed attribute, title takes the leaf name
n=4
"$GUEST" run "$PWQ $SIG Save X do /tmp/pd-smoke.draw" >/dev/null 2>&1
sleep 1
magic=$("$GUEST" run 'head -c 8 /tmp/pd-smoke.draw; echo' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
attr=$("$GUEST" run 'catattr BEOS:TYPE /tmp/pd-smoke.draw' 2>/dev/null \
	| head -1 | tr -d '\0\r')
t=$(title)
[ "$magic" = "HMF1&dDp" ] && echo "$attr" | grep -aqi "prosedraw-doc" \
	&& echo "$t" | grep -aq "pd-smoke.draw" \
	&& ok "save + typed + title" || bad "save ($magic/$attr/$t)"

# 5 - relaunch with the file as argument: the document comes back (D04)
killapp
n=5
"$GUEST" launch $APP /tmp/pd-smoke.draw >/dev/null && sleep 3
"$GUEST" run "hey $SIG do Activate" >/dev/null 2>&1
c=$(count); t=$(title)
[ "$c" = "4" ] && echo "$t" | grep -aq "pd-smoke.draw" \
	&& ok "reopen saved file" || bad "reopen (count=$c title=$t)"

# 6 - Open property (the exact message the Open panel sends): swap to a
#     second document and back — count and title follow
n=6
"$GUEST" run "$PWQ $SIG AddShape X do 'rect 80 700 120 60|Footer'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG Save X do /tmp/pd-smoke2.draw" >/dev/null 2>&1
sleep 1
"$GUEST" run "$PWQ $SIG Open X do /tmp/pd-smoke.draw" >/dev/null 2>&1
sleep 2
c=$(count); t=$(title)
[ "$c" = "4" ] && echo "$t" | grep -aq "pd-smoke.draw" \
	&& ok "open via property (panel path)" || bad "open (count=$c title=$t)"

# 7 - a file saved WITHOUT extension must reopen as the diagram it is
#     (content sniffing, not the name, decides the loader)
n=7
"$GUEST" run "$PWQ $SIG Save X do /tmp/pd-noext" >/dev/null 2>&1
sleep 1
"$GUEST" run "$PWQ $SIG Open X do /tmp/pd-noext" >/dev/null 2>&1
sleep 2
c=$(count); t=$(title)
[ "$c" = "4" ] && echo "$t" | grep -aq "pd-noext" \
	&& ok "extensionless round trip" || bad "noext (count=$c title=$t)"

# 8 - clean quit (unmodified document) exits the app
n=8
"$GUEST" run "$PWQ $SIG Quit X do" >/dev/null 2>&1
sleep 2
left=$("$GUEST" run 'ps' 2>/dev/null | grep -ac "apps/ProseDraw")
[ "$left" = "0" ] && ok "clean quit exits" || bad "quit (teams left: $left)"

"$QMP" shot "$OUT/pd-smoke-desk.png" >/dev/null 2>&1
echo "final screenshot: $OUT/pd-smoke-desk.png"

if [ $fail = 0 ]; then
	echo "=== PD GUEST SMOKE PASS $pass/$pass ==="
	exit 0
fi
echo "=== PD GUEST SMOKE FAIL ($fail failed, $pass passed) ==="
exit 1
