#!/bin/bash
# ProsePaint guest smoke — P05 in docs/plan.md. Run from the repo root
# with the VM already up. The scripting surface IS the brush on this
# guest (its pointer is dead): strokes, layers, colours and exact
# pixel reads, end to end.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GUEST="$ROOT/prosewriter/vm/guest.sh"
QMP="$ROOT/prosewriter/vm/qmp.py"
APP=/boot/home/apps/ProsePaint
SIG=application/x-vnd.prose.ProsePaint
PWQ=/boot/home/apps/pwquery
OUT="$ROOT/prosewriter/vm/run"
mkdir -p "$OUT"
pass=0; fail=0; n=0
ok()  { echo "ok $n - $1"; pass=$((pass+1)); }
bad() { echo "not ok $n - $1"; fail=$((fail+1)); }

killapp()
{
	"$GUEST" run 'ps' 2>/dev/null | grep -a "apps/ProsePaint" \
		| awk '{for (i = 2; i <= NF; i++) if ($i ~ /^[0-9]+$/) {print $i; exit}}' \
		| while read tid; do "$GUEST" run "kill $tid" >/dev/null 2>&1; done
	sleep 1
}

pixel()
{
	"$GUEST" run "$PWQ $SIG Pixel X get '$1'" 2>/dev/null \
		| grep -a "^result:" | head -1 | tr -d '\r'
}

layers()
{
	"$GUEST" run "hey $SIG get LayerCount" 2>/dev/null | grep -a result \
		| sed -n 's/.*: \([0-9]*\).*/\1/p'
}

# 1 - selftest on the deployed binary + fresh-deploy guard
n=1
r=$("$GUEST" run "$APP --selftest" 2>&1 | grep -a "SELFTEST")
host=$(stat -f%z "$ROOT/prosepaint/app/ProsePaint" 2>/dev/null)
guest=$("$GUEST" run "ls -l $APP" 2>/dev/null | awk '{print $5}' | head -1 \
	| tr -d '\r\n')
[ -n "$host" ] && [ "$host" = "$guest" ] && r="$r deploy-fresh($host)"
echo "$r" | grep -aq "PASS" && echo "$r" | grep -aq "deploy-fresh" \
	&& ok "selftest + fresh deploy ($r)" || bad "selftest/deploy ($r h=$host g=$guest)"

# 2 - launch + activate: one white background layer
killapp
n=2
"$GUEST" launch $APP >/dev/null && sleep 3
"$GUEST" run "hey $SIG do Activate" >/dev/null 2>&1
c=$(layers)
p=$(pixel '50 50')
if [ "$c" = "1" ] && echo "$p" | grep -aq "255 255 255 255"; then
	ok "launch + white background"
else
	bad "launch (c=$c p=$p)"
fi

# 3 - scripted painting: exact colours on the canvas
n=3
"$GUEST" run "$PWQ $SIG Brush X set 'round 12 255'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG Colour X set '216 40 40'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG StrokeLine X do '100 100 500 100'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG Colour X set '40 40 216'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG StrokeLine X do '100 140 500 140'" >/dev/null 2>&1
sleep 1
p1=$(pixel '300 100')
p2=$(pixel '300 140')
p3=$(pixel '300 200')
if echo "$p1" | grep -aq "216 40 40 255" \
	&& echo "$p2" | grep -aq "40 40 216 255" \
	&& echo "$p3" | grep -aq "255 255 255 255"; then
	ok "strokes paint exact colours"
else
	bad "strokes ($p1/$p2/$p3)"
fi

# 4 - eraser takes ink back off
n=4
"$GUEST" run "$PWQ $SIG Tool X set eraser" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG StrokeLine X do '290 100 310 100'" >/dev/null 2>&1
sleep 1
p=$(pixel '300 100')
if echo "$p" | grep -aq "0 0 0 0"; then
	ok "eraser removes ink (to transparency)"
else
	bad "eraser ($p)"
fi
"$GUEST" run "$PWQ $SIG Tool X set brush" >/dev/null 2>&1

# 5 - layers: a green stroke above, hidden below
n=5
"$GUEST" run "$PWQ $SIG Layer X do add" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG Colour X set '70 170 70'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG StrokeLine X do '300 60 300 400'" >/dev/null 2>&1
sleep 1
over=$(pixel '300 140')
"$GUEST" run "$PWQ $SIG Layer X do 'visible 0'" >/dev/null 2>&1
sleep 1
under=$(pixel '300 140')
c=$(layers)
if echo "$over" | grep -aq "70 170 70 255" \
	&& echo "$under" | grep -aq "40 40 216 255" && [ "$c" = "2" ]; then
	ok "layers composite and hide"
else
	bad "layers ($over/$under/$c)"
fi
"$GUEST" run "$PWQ $SIG Layer X do 'visible 1'" >/dev/null 2>&1

# 6 - save: typed, magic, and it round-trips through a relaunch
killapp
n=6
# (relaunch fresh, paint one mark, save)
"$GUEST" launch $APP >/dev/null && sleep 3
"$GUEST" run "$PWQ $SIG Colour X set '216 40 40'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG StrokeLine X do '200 200 260 200'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG Save X do /tmp/pp-smoke.paint" >/dev/null 2>&1
sleep 1
magic=$("$GUEST" run 'head -c 8 /tmp/pp-smoke.paint; echo' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
attr=$("$GUEST" run 'catattr BEOS:TYPE /tmp/pp-smoke.paint' 2>/dev/null \
	| head -1 | tr -d '\0\r')
t=$("$GUEST" run "hey $SIG get Title of Window 0" 2>/dev/null | grep -a result)
if [ "$magic" = "HMF1&tPp" ] && echo "$attr" | grep -aqi "prosepaint-doc"; then
	ok "save typed + magic"
else
	bad "save ($magic/$attr)"
fi

# 7 - relaunch with the file: pixels and layers come back
killapp
n=7
"$GUEST" launch $APP /tmp/pp-smoke.paint >/dev/null && sleep 3
p=$(pixel '230 200')
c=$(layers)
t=$("$GUEST" run "hey $SIG get Title of Window 0" 2>/dev/null | grep -a result)
if echo "$p" | grep -aq "216 40 40 255" && [ "$c" = "1" ] \
	&& echo "$t" | grep -aq "pp-smoke"; then
	ok "reopen round trip"
else
	bad "reopen ($p/$c/$t)"
fi

# 8 - PDF export: image page at paper size, painted and well-formed
n=8
"$GUEST" run "$PWQ $SIG PDF X do /tmp/pp-smoke.pdf" >/dev/null 2>&1
sleep 1
magic=$("$GUEST" run 'head -c 8 /tmp/pp-smoke.pdf; echo' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
box=$("$GUEST" run 'grep -a MediaBox /tmp/pp-smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r')
img=$("$GUEST" run 'grep -ac "Subtype /Image" /tmp/pp-smoke.pdf' \
	2>/dev/null | head -1 | tr -d '\0\r\n')
if [ "$magic" = "%PDF-1.4" ] \
	&& echo "$box" | grep -aq "\[0 0 595 842\]" && [ "$img" -ge 1 ]; then
	ok "pdf export (A4 image page)"
else
	bad "pdf ($magic/$box/$img)"
fi

# 9 - zoom round trip through the scripting property
n=9
z1=$("$GUEST" run "hey $SIG set Zoom to 2" 2>/dev/null | grep -a result \
	| sed -n 's/.*: \([0-9]*\).*/\1/p')
z0=$("$GUEST" run "hey $SIG set Zoom to 1" 2>/dev/null | grep -a result \
	| sed -n 's/.*: \([0-9]*\).*/\1/p')
zq=$("$GUEST" run "hey $SIG get Zoom" 2>/dev/null | grep -a result \
	| sed -n 's/.*: \([0-9]*\).*/\1/p')
if [ "$z1" = "200" ] && [ "$z0" = "100" ] && [ "$zq" = "100" ]; then
	ok "zoom scripting"
else
	bad "zoom ($z1/$z0/$zq)"
fi

# 10 - clean quit (unmodified after save)
n=10
"$GUEST" run "$PWQ $SIG Quit X do" >/dev/null 2>&1
sleep 2
left=$("$GUEST" run 'ps' 2>/dev/null | grep -ac "apps/ProsePaint")
[ "$left" = "0" ] && ok "clean quit exits" || bad "quit (teams left: $left)"

# 11 - Open Recent persisted (this run's save is in the list)
n=11
recent=$("$GUEST" run 'cat /boot/home/config/settings/ProsePaint/recent_files' \
	2>/dev/null | tr -d '\r')
first=$(echo "$recent" | head -1 | tr -d '\0')
echo "$recent" | grep -aq "pp-smoke.paint" \
	&& ok "recent files persisted" || bad "recent ($first)"

"$QMP" shot "$OUT/pp-smoke-desk.png" >/dev/null 2>&1
echo "final screenshot: $OUT/pp-smoke-desk.png"

if [ $fail = 0 ]; then
	echo "=== PP GUEST SMOKE PASS $pass/$pass ==="
	exit 0
fi
echo "=== PP GUEST SMOKE FAIL ($fail failed, $pass passed) ==="
exit 1
