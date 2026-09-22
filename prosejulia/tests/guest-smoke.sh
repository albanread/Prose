#!/bin/bash
# ProseJulia guest smoke — harness-level matrix, run from the repo root
# with the VM already up (prosewriter/vm/run.sh + wait-boot):
#
#   prosejulia/tests/guest-smoke.sh
#
# Covers: fresh deploy, selftest, launch+activate, Frame round trip,
# pixel probes (inside black / escaping palette-dependent), postcard
# PDF, animation advancing and pausing, palette switch, clean quit.
# TAP-style lines; end-state screenshot under prosewriter/vm/run/.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GUEST="$ROOT/prosewriter/vm/guest.sh"
QMP="$ROOT/prosewriter/vm/qmp.py"
APP=/boot/home/apps/ProseJulia
SIG=application/x-vnd.prose.ProseJulia
BIN="$ROOT/prosejulia/app/ProseJulia"
OUT="$ROOT/prosewriter/vm/run"
mkdir -p "$OUT"
pass=0; fail=0; n=0
ok()  { echo "ok $n - $1"; pass=$((pass+1)); }
bad() { echo "not ok $n - $1"; fail=$((fail+1)); }

# team id = first numeric field after the command name on the ps line;
# never a fixed column, never the path itself
killapp()
{
	"$GUEST" run 'ps' 2>/dev/null | grep -a "apps/ProseJulia" \
		| awk '{for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+$/) {print $i; break}}' \
		| while read tid; do "$GUEST" run "kill $tid" >/dev/null 2>&1; done
	sleep 1
}

# 1 - deploy the host build and prove the guest runs it (a stale binary
#     or a stale team poisons every later probe)
n=1
killapp
"$GUEST" put "$BIN" $APP >/dev/null 2>&1
hsz=$(stat -f%z "$BIN")
gsz=$("$GUEST" run 'stat -c "%s" /boot/home/apps/ProseJulia' 2>/dev/null \
	| head -1 | tr -d '\0\r\n ')
[ "$hsz" = "$gsz" ] && ok "deploy verified ($hsz bytes)" \
	|| bad "deploy ($hsz vs $gsz)"

# 2 - in-process selftest
n=2
r=$("$GUEST" run "$APP --selftest" 2>&1 | grep -a "SELFTEST")
echo "$r" | grep -aq "PASS" && ok "selftest ($r)" || bad "selftest ($r)"

# 3 - launch + activate + Frame reads a sane angle
killapp
n=3
"$GUEST" launch $APP >/dev/null && sleep 3
"$GUEST" run "hey $SIG do Activate" >/dev/null 2>&1
r=$("$GUEST" run "hey $SIG get Frame" 2>/dev/null | grep -a result)
v=$(echo "$r" | tr -d '\0' | grep -aoE ': [0-9]+' | grep -aoE '[0-9]+')
[ -n "$v" ] && [ "$v" -ge 0 ] && [ "$v" -le 359 ] \
	&& ok "launch + Frame ($v deg)" || bad "launch ($r)"

# 4 - Frame set/get round trip (paused first: an animating frame keeps
#     advancing, so an unprompted read-back races the pulse)
n=4
"$GUEST" run "/boot/home/apps/pwquery $SIG Paused X set true" >/dev/null 2>&1
"$GUEST" run "/boot/home/apps/pwquery $SIG Frame X set 180" >/dev/null 2>&1
sleep 1.5
r=$("$GUEST" run "hey $SIG get Frame" 2>/dev/null | grep -a result)
echo "$r" | grep -aq ": 180" && ok "Frame set/get 180 (paused)" \
	|| bad "Frame ($r)"

# 5 - Pixel at theta=180 (paused above): centre is interior (black),
#     corner escapes with a real colour — and the read is deterministic
n=5
c=$("$GUEST" run "/boot/home/apps/pwquery $SIG Pixel X get \"280 240\"" \
	2>/dev/null | grep -a result | tr -d '\0')
c2=$("$GUEST" run "/boot/home/apps/pwquery $SIG Pixel X get \"280 240\"" \
	2>/dev/null | grep -a result | tr -d '\0')
e=$("$GUEST" run "/boot/home/apps/pwquery $SIG Pixel X get \"5 5\"" \
	2>/dev/null | grep -a result | tr -d '\0')
echo "$c" | grep -aq "result: 0 0 0" && [ "$c" = "$c2" ] \
	&& echo "$e" | grep -aqE "result: [0-9]+ [0-9]+ [0-9]+" \
	&& ! echo "$e" | grep -aq "result: 0 0 0" \
	&& ok "pixel inside/deterministic/escaping" || bad "pixel ($c/$e)"

# 6 - palette switch recolours an escaping point (interior stays black)
n=6
a=$("$GUEST" run "/boot/home/apps/pwquery $SIG Pixel X get \"100 60\"" \
	2>/dev/null | grep -a result | tr -d '\0')
"$GUEST" run "/boot/home/apps/pwquery $SIG Palette X set ocean" >/dev/null 2>&1
sleep 1
b=$("$GUEST" run "/boot/home/apps/pwquery $SIG Pixel X get \"100 60\"" \
	2>/dev/null | grep -a result | tr -d '\0')
[ -n "$a" ] && [ "$a" != "$b" ] && ok "palette recolours ($a -> $b)" \
	|| bad "palette ($a/$b)"

# 7 - postcard PDF: magic, one page, non-trivial size
n=7
"$GUEST" run "/boot/home/apps/pwquery $SIG Postcard X do /tmp/pj-smoke.pdf" \
	>/dev/null 2>&1
sleep 1
magic=$("$GUEST" run 'head -c 8 /tmp/pj-smoke.pdf; echo' 2>/dev/null \
	| head -1 | tr -d '\0\r')
pages=$("$GUEST" run 'grep -ac MediaBox /tmp/pj-smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r\n ')
sz=$("$GUEST" run 'stat -c "%s" /tmp/pj-smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r\n ')
[ "$magic" = "%PDF-1.4" ] && [ "$pages" = "1" ] && [ "$sz" -gt 10000 ] \
	&& ok "postcard pdf ($sz bytes)" || bad "postcard ($magic/$pages/$sz)"

# 8 - animation advances while playing and freezes when paused
n=8
"$GUEST" run "/boot/home/apps/pwquery $SIG Paused X set false" >/dev/null 2>&1
sleep 1
f1=$("$GUEST" run "hey $SIG get Frame" 2>/dev/null | grep -a result \
	| tr -d '\0' | grep -aoE ': [0-9]+' | grep -aoE '[0-9]+')
sleep 1
f2=$("$GUEST" run "hey $SIG get Frame" 2>/dev/null | grep -a result \
	| tr -d '\0' | grep -aoE ': [0-9]+' | grep -aoE '[0-9]+')
"$GUEST" run "/boot/home/apps/pwquery $SIG Paused X set true" >/dev/null 2>&1
sleep 1
f3=$("$GUEST" run "hey $SIG get Frame" 2>/dev/null | grep -a result \
	| tr -d '\0' | grep -aoE ': [0-9]+' | grep -aoE '[0-9]+')
sleep 1
f4=$("$GUEST" run "hey $SIG get Frame" 2>/dev/null | grep -a result \
	| tr -d '\0' | grep -aoE ': [0-9]+' | grep -aoE '[0-9]+')
[ -n "$f1" ] && [ -n "$f2" ] && [ "$f1" != "$f2" ] && [ "$f3" = "$f4" ] \
	&& ok "animate ($f1->$f2) then freeze ($f3)" \
	|| bad "animation ($f1/$f2/$f3/$f4)"

# 9 - scripted zoom + recentre answer without error and keep rendering
n=9
z=$("$GUEST" run "/boot/home/apps/pwquery $SIG Zoom X set 2" 2>/dev/null \
	| grep -a result | tr -d '\0')
ct=$("$GUEST" run "/boot/home/apps/pwquery $SIG Centre X set \"-0.1 0\"" \
	2>/dev/null | grep -a result | tr -d '\0')
echo "$z" | grep -aq result: && echo "$ct" | grep -aq result: \
	&& ok "zoom + centre" || bad "zoom/centre ($z/$ct)"

# 10 - clean quit exits
n=10
"$GUEST" run "hey $SIG do Quit" >/dev/null 2>&1
sleep 2
c=$("$GUEST" run 'ps' 2>/dev/null | grep -ac "apps/ProseJulia" | tr -d '\0\r\n ')
[ "$c" = "0" ] && ok "clean quit exits" || bad "quit (teams left: $c)"

"$QMP" shot "$OUT/pj-smoke.png" >/dev/null
echo "final screenshot: $OUT/pj-smoke.png"

if [ $fail = 0 ]; then
	echo "=== GUEST SMOKE PASS $pass/$pass ==="
	exit 0
fi
echo "=== GUEST SMOKE FAIL ($fail failed, $pass passed) ==="
exit 1
