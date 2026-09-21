#!/bin/bash
# HostFS: the Mac's changes reach the guest (patch 0081). The guest watches
# /HostFS with node monitors (monwatch.cpp, built in the guest with its own
# clang) while the Mac drops a file into the share, changes one and removes
# one. Before the poller every one of those was invisible in the guest; the
# test fails unless all three arrive as node-monitor messages.
# usage: hostfs-appear.sh [name]
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:-hostfs-appear}
SHARE="$PW_WORK/$NAME-share"
rm -rf "$SHARE"
mkdir -p "$SHARE"
echo "from the start" > "$SHARE/stays.txt"
echo "to be changed" > "$SHARE/changes.txt"
echo "to be removed" > "$SHARE/goes.txt"
cp "$PW_ROOT/private_workspace/monwatch.cpp" "$SHARE/monwatch.cpp"

GUEST="$PW_WORK/$NAME-guest.sh"
cat > "$GUEST" <<'EOF'
(
	sleep 25
	H=/HostFS
	clang++ -o /boot/home/monwatch $H/monwatch.cpp -lbe 2> /boot/home/monwatch-build.log \
		&& echo built || { echo "build failed:"; cat /boot/home/monwatch-build.log; }
	/boot/home/monwatch $H 60 $H/changes.txt > /boot/home/mon.txt 2>&1 &
	sleep 1
	grep -q "watching $H" /boot/home/mon.txt || echo "monwatch did not start" >&2
	echo ready > $H/ready
	sleep 65
	cp /boot/home/mon.txt $H/mon-result.txt
	sync
) &
EOF

PW_INJECT_SCRIPT="$GUEST" "$PW_ROOT/private_workspace/run-vz.sh" "$NAME" --headless \
	--seconds 115 --share "$SHARE" > "$PW_WORK/$NAME.log" 2>&1 &
VM=$!

# the Mac's part: once the guest is watching, change the share behind its back
n=0
while [ ! -e "$SHARE/ready" ] && [ $n -lt 240 ]; do sleep 0.5; n=$((n + 1)); done
sleep 6			# one poll passes with nothing happening: the baseline is quiet
echo "dropped by the Mac" > "$SHARE/from-mac.txt"
echo ", and by the Mac too" >> "$SHARE/changes.txt"
rm "$SHARE/goes.txt"
echo ">>> the Mac changed the share at $(date +%H:%M:%S)"

wait $VM || true
echo ">>> what the guest's node monitors said:"
cat "$SHARE/mon-result.txt" 2>/dev/null || { echo "no mon-result.txt"; exit 1; }
PASS=1
grep -q "MON created from-mac.txt" "$SHARE/mon-result.txt" || { echo "FAIL: from-mac.txt never arrived"; PASS=0; }
grep -q "MON removed goes.txt" "$SHARE/mon-result.txt" || { echo "FAIL: goes.txt removal never arrived"; PASS=0; }
grep -q "MON stat-changed.*changes.txt" "$SHARE/mon-result.txt" || { echo "FAIL: the change to changes.txt never arrived"; PASS=0; }
# the baseline is quiet: nothing said about the file nobody touched
if grep -q "stays.txt" "$SHARE/mon-result.txt"; then
	echo "FAIL: stays.txt was reported"; PASS=0
fi
# the guest's own file is announced once (its own create), not twice (a host
# change on top): the poller must not echo what the guest itself did
count=$(grep -c "created ready" "$SHARE/mon-result.txt" || true)
if [ "$count" != "1" ]; then
	echo "FAIL: the guest's own ready file was announced $count times, not once"; PASS=0
fi
if [ "$PASS" = 1 ]; then echo "PASS: the Mac's changes reached the guest"; else echo "SELFTEST FAIL"; exit 1; fi
