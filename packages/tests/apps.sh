#!/bin/sh
# The Prose profile's applications (patch 0042): Haiku's regular apps and
# demos, and the ported ones. Counts what is there, then starts a few in the
# background and checks they are still running after a moment (a program
# that cannot load its libraries, or crashes at startup, is gone by then).
# Runs on the target from boot-test.sh; the last line is PASS or FAIL.
# Only bash and coreutils are used. Progress also goes to the kernel's debug
# output (/dev/dprintf), which boot-test.sh's serial log shows, so a run
# that never finishes still tells how far it got.
fail=0
say() { echo "$*"; echo "apps probe: $*" > /dev/dprintf 2>/dev/null; }
count() { n=0; for f in "$1"/*; do [ -e "$f" ] && n=$((n + 1)); done; echo $n; }
say "apps: $(count /boot/system/apps), demos: $(count /boot/system/demos), preferences: $(count /boot/system/preferences)"
for f in /boot/system/apps/ActivityMonitor /boot/system/apps/Icon-O-Matic /boot/system/apps/MediaPlayer \
		/boot/system/apps/People /boot/system/demos/Chart /boot/system/demos/Mandelbrot \
		/boot/system/apps/Pe/Pe /boot/system/apps/ArtPaint/ArtPaint /boot/system/apps/BeShare \
		/boot/system/apps/Vision/Vision /boot/system/lib/libmail.so /boot/system/lib/libmidi.so \
		/boot/system/data/fonts/otfonts/NotoSansCJKjp-VF.otf; do
	if [ -e "$f" ]; then say "present: $f"; else say "MISSING: $f"; fail=1; fi
done
# start a few and see that they stay up; then they are killed outright, so
# that no "save changes?" alert can hold up the shutdown that follows
for app in /boot/system/demos/Chart /boot/system/demos/Mandelbrot /boot/system/apps/ActivityMonitor \
		/boot/system/apps/Pe/Pe /boot/system/apps/ArtPaint/ArtPaint; do
	[ -e "$app" ] || continue
	"$app" > /tmp/app.out 2>&1 &
	pid=$!
	sleep 5
	if kill -0 $pid 2>/dev/null; then
		say "runs: $app"
		kill -9 $pid 2>/dev/null
		wait $pid 2>/dev/null
	else
		wait $pid 2>/dev/null; status=$?
		say "EXITED: $app (status $status): $(head -c 300 /tmp/app.out | tr '\n' ' ')"; fail=1
	fi
done
sleep 1
[ $fail = 0 ] && say PASS || say FAIL
