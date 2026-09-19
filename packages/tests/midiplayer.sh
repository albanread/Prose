#!/bin/sh
# MidiPlayer (patch 0048): plays a demo tune, stays up, and quits cleanly
# with a song loaded and playing -- the old MidiPlayer crashed there (a
# double free when its window closed). Also with an output that is not
# there (--output Mac under QEMU): it falls back to the built-in
# synthesizer. Runs on the target from boot-test.sh; the last line is PASS
# or FAIL. Only bash, coreutils and hey are used.
fail=0
say() { echo "$*"; echo "midiplayer probe: $*" > /dev/dprintf 2>/dev/null; }
tune="/boot/system/data/music/Beethoven - Ode to Joy.mid"
[ -f "$tune" ] && say "tune: $tune" || { say "MISSING: $tune"; echo FAIL; exit; }

run() {
	# run <label> <MidiPlayer arguments...>
	label=$1; shift
	/boot/system/apps/MidiPlayer "$@" > /tmp/midiplayer.out 2>&1 &
	pid=$!
	sleep 8
	if ! kill -0 $pid 2>/dev/null; then
		wait $pid 2>/dev/null; status=$?
		say "$label: EXITED while playing (status $status): $(head -c 300 /tmp/midiplayer.out | tr '\n' ' ')"
		fail=1
		return
	fi
	say "$label: plays (8 s)"
	hey MidiPlayer quit > /dev/null 2>&1
	for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $pid 2>/dev/null || break; sleep 1; done
	if kill -0 $pid 2>/dev/null; then
		say "$label: did not quit within 10 s"; kill -9 $pid 2>/dev/null; fail=1
		return
	fi
	wait $pid 2>/dev/null; status=$?
	if [ $status = 0 ]; then
		say "$label: quit cleanly (status 0)"
	else
		say "$label: quit with status $status: $(head -c 300 /tmp/midiplayer.out | tr '\n' ' ')"; fail=1
	fi
	out=$(head -c 200 /tmp/midiplayer.out | tr '\n' ' ')
	[ -n "$out" ] && say "$label: said: $out"
}

run "built-in" "$tune"
run "--output Mac" --output Mac "$tune"
[ $fail = 0 ] && say PASS || say FAIL
