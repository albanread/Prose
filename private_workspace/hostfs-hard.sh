#!/bin/bash
# HostFS under the conditions that used to break it:
#   1. a program run from the share keeps running while the Mac rewrites it
#      (the file cache cannot drop mapped pages: no panic, old size until the
#      program exits, then the Mac's version)
#   2. two appenders on one file (no lost or zeroed data)
#   3. truncating right after a big write (nothing written back afterwards
#      re-extends the file on the Mac)
# usage: hostfs-hard.sh [name]
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:-hostfs-hard}
SHARE="$PW_WORK/$NAME-share"
GUEST="$PW_WORK/$NAME-guest.sh"
rm -rf "$SHARE"
mkdir -p "$SHARE"

cat > "$GUEST" <<'EOF'
(
	sleep 25
	H=/HostFS
	{
		echo "== 1. a program from the share keeps running while the Mac rewrites it"
		cp /boot/system/bin/sleep $H/sleepcopy && ls -l $H/sleepcopy
		$H/sleepcopy 30 &
		echo started > $H/ready
		n=0; while [ ! -e $H/modified ] && [ $n -lt 60 ]; do sleep 1; n=$((n+1)); done
		echo "the Mac changed it after ${n}s; the old program still runs"
		stat -c "stat: %s bytes" $H/sleepcopy; echo "read: $(wc -c < $H/sleepcopy) bytes"
		$H/sleepcopy 1; echo "a second copy ran, exit $?"
		wait
		echo "old program finished; now: $(wc -c < $H/sleepcopy) bytes, ends with: $(tail -c 19 $H/sleepcopy)"
		echo "== 2. two appenders, 400 lines each"
		( i=0; while [ $i -lt 400 ]; do echo "A$i" >> $H/log.txt; i=$((i+1)); done ) &
		( i=0; while [ $i -lt 400 ]; do echo "B$i" >> $H/log.txt; i=$((i+1)); done ) &
		wait
		echo "lines: $(wc -l < $H/log.txt), NUL bytes: $(tr -cd '\000' < $H/log.txt | wc -c)"
		echo "== 3. truncate right after a 32 MiB write"
		dd if=/dev/urandom of=$H/trunc.bin bs=1048576 count=32 2>/dev/null
		truncate -s 1000 $H/trunc.bin && sleep 3 && stat -c "%s bytes" $H/trunc.bin
		echo "== done"
	} > $H/hard.txt 2>&1
) &
EOF

PW_INJECT_SCRIPT="$GUEST" "$PW_ROOT/private_workspace/run-vz.sh" "$NAME" --headless \
	--seconds 110 --share "$SHARE" > "$PW_WORK/$NAME.log" 2>&1 &
VM=$!

# the Mac's part: rewrite the running program once the guest says so
n=0
while [ ! -e "$SHARE/ready" ] && [ $n -lt 240 ]; do sleep 0.5; n=$((n + 1)); done
chmod u+w "$SHARE/sleepcopy"
printf 'appended-by-the-mac' >> "$SHARE/sleepcopy"
touch "$SHARE/modified"
echo ">>> the Mac appended to the running program: $(stat -f %z "$SHARE/sleepcopy") bytes"

wait $VM || true
cat "$SHARE/hard.txt" 2>/dev/null || echo "no report in the share"
echo ">>> the Mac's view: trunc.bin $(stat -f %z "$SHARE/trunc.bin" 2>/dev/null) bytes," \
	"log.txt $(wc -l < "$SHARE/log.txt" 2>/dev/null) lines," \
	"duplicates: $(sort "$SHARE/log.txt" 2>/dev/null | uniq -d | wc -l)"
if grep -aqE "PANIC|KERNEL PANIC" "$PW_WORK/$NAME/ramconsole.log"; then
	echo ">>> KERNEL PANIC (see $PW_WORK/$NAME/ramconsole.log)"
fi
