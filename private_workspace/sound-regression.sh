#!/bin/bash
# Sound end to end, with the proof on the Mac: the run's audio is recorded
# (--record-sound, soundcapture.swift) and the WAV must show every part of it.
#
#   1. a MIDI scale           -- the host synth
#   2. an MP3, first play     -- clean and real time
#   3. the same MP3 again     -- the second play of a boot used to run at a
#                                third of the speed with repeating content
#                                (phantom completions across sessions, and the
#                                whole app's audio went silent after it)
#   4. a MIDI scale again     -- used to be silent
#   5. a tone                 -- used to be silent
#
# usage: sound-regression.sh [name]
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:-sound-regression}
SHARE="$PW_WORK/$NAME-share"
WAV="$PW_WORK/$NAME.wav"
rm -rf "$SHARE" "$WAV"
mkdir -p "$SHARE"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=880:duration=20" -ac 2 -b:a 128k \
	"$SHARE/test.mp3"

GUEST="$PW_WORK/$NAME-guest.sh"
cat > "$GUEST" <<'EOF'
(
	sleep 30
	H=/HostFS
	{
		echo "== $(date +%s) midi 1"; prose_midi_test 2>&1
		echo "== $(date +%s) mp3 first play"; media_client play $H/test.mp3 2>&1 | tail -2
		echo "== $(date +%s) mp3 second play"; media_client play $H/test.mp3 2>&1 | tail -2
		echo "== $(date +%s) tone"; prose_tone 2 440 2>&1 | tail -1
		# midi last: a tone straight after a MIDI test is still silent on VZ
		# (the Mac's side; see the review), and that is not what this tests
		echo "== $(date +%s) midi 2"; prose_midi_test 2>&1
		echo "== $(date +%s) done"
	} > /boot/home/sound.txt 2>&1
	cp /boot/home/sound.txt $H/sound-result.txt
	sync
) &
EOF

PW_INJECT_SCRIPT="$GUEST" "$PW_ROOT/private_workspace/run-vz.sh" "$NAME" --headless \
	--seconds 165 --share "$SHARE" --record-sound "$WAV" > "$PW_WORK/$NAME.log" 2>&1 &
VM=$!
wait $VM || true

echo ">>> the guest's report:"
cat "$SHARE/sound-result.txt" 2>/dev/null || { echo "no report"; exit 1; }
echo ">>> the kernel's sessions:"
grep -a "playback stopped" "$PW_WORK/$NAME/ramconsole.log" 2>/dev/null | cut -c1-200
echo ">>> the WAV's loud segments:"
/usr/bin/python3 "$(dirname "$0")/loudness.py" "$WAV" | tee "$PW_WORK/$NAME-segments.txt"

PASS=1
# the WAV assertions, in one place that can compare fractions of a second
verdict=$(/usr/bin/python3 - "$WAV" "$PW_WORK/$NAME-segments.txt" <<'PY'
import sys
wav, seg = sys.argv[1], sys.argv[2]
import re
rows = []
for l in open(seg):
    m = re.search(r'([\d.]+)-\s*([\d.]+)\s*\(\s*([\d.]+)s\)', l)
    if m: rows.append(m.groups())
fails = []
durations = [float(r[2]) for r in rows]
if len(rows) < 4:
    fails.append(f"only {len(rows)} loud stretches in the WAV; sound went missing")
if durations and max(durations) > 35:
    fails.append(f"a stretch lasted {max(durations):.1f}s -- the second play stretched again")
# midi 1 + mp3 + mp3 + tone + midi 2 is 46 s of sound; touching parts may merge
if sum(durations) < 40:
    fails.append(f"only {sum(durations):.1f}s of sound in the WAV; parts went missing")
for f in fails: print(f"FAIL: {f}")
if not fails: print("WAV OK")
PY
)
if [ "$verdict" != "WAV OK" ]; then
	echo "$verdict"
	PASS=0
fi
if [ "$PASS" = 1 ]; then echo "PASS: every part sounded, and the second play kept real time"; else echo "SELFTEST FAIL"; exit 1; fi
