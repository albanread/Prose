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
# trip, vector PDF export, clean quit. TAP-style output, end-state
# screenshot in vm/run/.
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

# 8 - vector PDF export via scripting: magic, A4 MediaBox (the paper),
#     the shape ops and the escaped label text
n=8
"$GUEST" run "$PWQ $SIG PDF X do /tmp/pd-smoke.pdf" >/dev/null 2>&1
sleep 1
magic=$("$GUEST" run 'head -c 8 /tmp/pd-smoke.pdf; echo' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
box=$("$GUEST" run 'grep -a MediaBox /tmp/pd-smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r')
# n.b. this diagram is rrect + diamond + ellipse + connector — the
# vector proof is in the Bézier curve ops (" c"), not " re"
ops=$("$GUEST" run 'grep -ac " c$" /tmp/pd-smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
label=$("$GUEST" run 'grep -ac "Tj" /tmp/pd-smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
[ "$magic" = "%PDF-1.4" ] && echo "$box" | grep -aq "\[0 0 595 842\]" \
	&& [ "$ops" -ge 4 ] && [ "$label" -ge 1 ] \
	&& ok "pdf export (A4, vector, labelled)" \
	|| bad "pdf ($magic/$box/ops=$ops labels=$label)"

# 9 - elbow connectors + zoom: an orthogonal route through scripting
#     (more line ops in the PDF than the straight connector), and the
#     Zoom property round trip
n=9
"$GUEST" run "$PWQ $SIG AddShape X do 'rect 80 500 120 60|Source'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG AddShape X do 'rect 320 560 120 60|Sink'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG AddShape X do 'connect elbow'" >/dev/null 2>&1
sleep 1
c=$(count)
"$GUEST" run "$PWQ $SIG Save X do /tmp/pd-elbow.draw" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG PDF X do /tmp/pd-elbow.pdf" >/dev/null 2>&1
sleep 1
# the connector paths are emitted one-per-line: an elbow route is the
# only line with three or more " l " segment ops on it
elbows=$("$GUEST" run 'grep -acE " l .* l .* l " /tmp/pd-elbow.pdf' \
	2>/dev/null | head -1 | tr -d '\0\r\n')
z1=$("$GUEST" run "hey $SIG set Zoom to 2" 2>/dev/null | grep -a result \
	| sed -n 's/.*: \([0-9]*\).*/\1/p')
z0=$("$GUEST" run "hey $SIG set Zoom to 1" 2>/dev/null | grep -a result \
	| sed -n 's/.*: \([0-9]*\).*/\1/p')
zq=$("$GUEST" run "hey $SIG get Zoom" 2>/dev/null | grep -a result \
	| sed -n 's/.*: \([0-9]*\).*/\1/p')
[ "$c" = "7" ] && [ "$elbows" -ge 1 ] && [ "$z1" = "200" ] && [ "$z0" = "100" ] \
	&& [ "$zq" = "100" ] \
	&& ok "elbow route + zoom" \
	|| bad "elbow/zoom (c=$c elbows=$elbows z=$z1/$z0/$zq)"

# 12 - in-place label editing: keyboard only (this guest's mouse cannot
#      deliver to windows). Haiku's Command modifier is ALT. Clear the
#      diagram first (select all + delete) so the editor opens on a
#      single selection; typing replaces (standard text field), Enter
#      commits, the label shows in the PDF export
n=12
"$GUEST" run "hey $SIG do Activate" >/dev/null 2>&1
"$QMP" hmp "sendkey alt-a" >/dev/null; sleep 1
"$QMP" hmp "sendkey delete" >/dev/null; sleep 1
"$GUEST" run "$PWQ $SIG AddShape X do 'rect 10 10 90 60|Orig'" >/dev/null 2>&1
"$GUEST" run "hey $SIG do Activate" >/dev/null 2>&1
"$QMP" hmp "sendkey alt-a" >/dev/null; sleep 1
"$QMP" hmp "sendkey ret" >/dev/null; sleep 1
"$QMP" hmp "sendkey x" >/dev/null; sleep 0.5
"$QMP" hmp "sendkey ret" >/dev/null; sleep 1
"$GUEST" run "$PWQ $SIG PDF X do /tmp/pd-edit.pdf" >/dev/null 2>&1
label=$("$GUEST" run 'grep -a "Tj" /tmp/pd-edit.pdf' 2>/dev/null | head -2)
echo "$label" | grep -aq "(x) Tj" && ok "in-place label edit" \
	|| bad "label edit ($label)"

# 13 - the drop cores via the Drop property: stencil-style shape drop
#      and colour swatch drop run the identical code a real drag drops
#      into (the physical gesture is human-owed: no mouse on this guest)
n=13
before=$(count)
"$GUEST" run "$PWQ $SIG Drop X do 'rect 300 300'" >/dev/null 2>&1
"$GUEST" run "$PWQ $SIG Drop X do 'colour 216 40 40 340 330'" >/dev/null 2>&1
sleep 1
after=$(count)
"$GUEST" run "$PWQ $SIG PDF X do /tmp/pd-drop.pdf" >/dev/null 2>&1
red=$("$GUEST" run 'grep -ac "0.847 0.157 0.157 rg" /tmp/pd-drop.pdf' \
	2>/dev/null | head -1 | tr -d '\0\r\n')
[ "$after" = "$((before + 1))" ] && [ "$red" -ge 1 ] \
	&& ok "drop cores (shape + colour)" || bad "drop (b=$before a=$after red=$red)"
# 14 - image shapes: a generated BMP loads through the Translation
#      Kit, draws on the canvas and embeds as a Flate image XObject
n=14
python3 - "$OUT/pd-smoke-logo.bmp" <<'PYEOF'
import struct, sys
W = H = 32
row = (W*3 + 3) & ~3
data = bytearray()
for y in range(H):
    for x in range(W):
        data += bytes((40, 40, 216) if x < W//2 else (216, 40, 40))
    data += b'\0' * (row - W*3)
hdr = b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54)
hdr += struct.pack('<IiiHHIIiiII', 40, W, H, 1, 24, 0, len(data),
    2835, 2835, 0, 0)
open(sys.argv[1], 'wb').write(hdr + bytes(data))
PYEOF
"$GUEST" put "$OUT/pd-smoke-logo.bmp" /tmp/pd-smoke-logo.bmp >/dev/null 2>&1
before=$(count)
"$GUEST" run "$PWQ $SIG AddShape X do 'image 60 500 100 100|/tmp/pd-smoke-logo.bmp'" \
	>/dev/null 2>&1
sleep 1
after=$(count)
"$GUEST" run "$PWQ $SIG PDF X do /tmp/pd-img-smoke.pdf" >/dev/null 2>&1
imgobj=$("$GUEST" run 'grep -ac "Subtype /Image" /tmp/pd-img-smoke.pdf' \
	2>/dev/null | head -1 | tr -d '\0\r\n')
drawn=$("$GUEST" run 'grep -ac "/Im0 Do" /tmp/pd-img-smoke.pdf' 2>/dev/null \
	| head -1 | tr -d '\0\r\n')
[ "$after" = "$((before + 1))" ] && [ "$imgobj" -ge 1 ] && [ "$drawn" -ge 1 ] \
	&& ok "image shape (load + embed)" \
	|| bad "image (b=$before a=$after obj=$imgobj do=$drawn)"
# leave the document clean for the quit step
"$GUEST" run "$PWQ $SIG Save X do /tmp/pd-final.draw" >/dev/null 2>&1
sleep 1

# 15 - clean quit (unmodified document) exits the app
n=15
"$GUEST" run "$PWQ $SIG Quit X do" >/dev/null 2>&1
sleep 2
left=$("$GUEST" run 'ps' 2>/dev/null | grep -ac "apps/ProseDraw")
[ "$left" = "0" ] && ok "clean quit exits" || bad "quit (teams left: $left)"

# 16 - Open Recent persisted: the settings file exists, lists the
#      documents this run opened, most recent first (the final save)
n=16
recent=$("$GUEST" run 'cat /boot/home/config/settings/ProseDraw/recent_files' \
	2>/dev/null | tr -d '\r')
first=$(echo "$recent" | head -1 | tr -d '\0')
echo "$recent" | grep -aq "pd-smoke.draw" && echo "$first" | grep -aq "pd-final.draw" \
	&& ok "recent files persisted" || bad "recent ($first)"

"$QMP" shot "$OUT/pd-smoke-desk.png" >/dev/null 2>&1
echo "final screenshot: $OUT/pd-smoke-desk.png"

if [ $fail = 0 ]; then
	echo "=== PD GUEST SMOKE PASS $pass/$pass ==="
	exit 0
fi
echo "=== PD GUEST SMOKE FAIL ($fail failed, $pass passed) ==="
exit 1
