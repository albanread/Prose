#!/bin/bash
# Start exactly one Prose machine, and refuse if one is already running.
#
#   scripts/run-machine.sh <image> [hvgpu arguments...]
#
# Two VZ machines on one image file corrupt it, and two on one Mac fight over
# the proxy's port and the hardware-address slots -- a second machine started
# while the first is still exiting comes up as "slot 1" with no proxy at all.
# The trap is that stopping a machine is not instant: a kill returns long before
# the process is gone, so a plain `pkill; start` reliably produces two.
#
# This waits, and refuses rather than racing.
#
# It also never says `tell application "Prose"` -- that name resolves through
# LaunchServices to an installed /Applications/Prose.app, which would start the
# user's own machine. The build is addressed by path, and so is this.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build/Prose.app"
BIN="$APP/Contents/MacOS/hvgpu"
IMAGE="${1:-}"
[ -n "$IMAGE" ] || { echo "usage: run-machine.sh <image> [hvgpu args...]" >&2; exit 64; }
[ -f "$IMAGE" ] || { echo "run-machine: no such image: $IMAGE" >&2; exit 66; }
[ -x "$BIN" ] || { echo "run-machine: build it first (tools/build.sh)" >&2; exit 69; }
shift

# Anything of ours that is running, the user's installed copy included: both
# take a hardware-address slot and both want the proxy's port.
running() { pgrep -f 'Prose\.app/Contents/MacOS/hvgpu' | wc -l | tr -d ' '; }

if [ "$(running)" != "0" ]; then
	echo "run-machine: a machine is already running -- stop it first:" >&2
	ps -o pid,args -ww $(pgrep -f 'Prose\.app/Contents/MacOS/hvgpu') >&2
	exit 75
fi

# A port still held is the previous machine not yet gone, whatever ps says.
for _ in $(seq 1 30); do
	netstat -an -f inet | grep -q '\.8888 .*LISTEN' || break
	sleep 1
done

LOG="${PROSE_LOG:-$(dirname "$IMAGE")/$(basename "${IMAGE%.*}").log}"
nohup "$BIN" "$IMAGE" "$@" > "$LOG" 2>&1 &
PID=$!
echo "run-machine: pid $PID, log $LOG"

# Wait for the guest, by path so the installed copy is never what answers.
for _ in $(seq 1 90); do
	kill -0 "$PID" 2>/dev/null || { echo "run-machine: it exited; see $LOG" >&2; exit 70; }
	if osascript -e "tell application \"$APP\" to execute \"echo up\"" >/dev/null 2>&1; then
		echo "run-machine: the guest is answering"
		exit 0
	fi
	sleep 2
done
echo "run-machine: the guest never answered; see $LOG" >&2
exit 70
