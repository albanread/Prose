#!/bin/sh
# Slayer (overlay haiku-apps/slayer/slayer-1.0-deleted-items.patch): when
# threads end, its refresh deleted their rows and then dynamic_cast them.
# Runs it under the guarded heap -- where reading a freed block faults --
# while short-lived teams come and go through several of its refreshes, and
# checks that it is still running afterwards.
# Runs on the target from boot-test.sh; the last line is PASS or FAIL.
# Only bash and coreutils are used.
fail=0
say() { echo "$*"; echo "slayer probe: $*" > /dev/dprintf 2>/dev/null; }
app=${SLAYER:-/boot/system/apps/Slayer}
lib=/boot/system/lib/libroot_debug.so
for f in "$app" "$lib"; do
	[ -e "$f" ] || { say "MISSING: $f"; say FAIL; exit 0; }
done

# a crash must end the team at once, not wait in an alert
mkdir -p /boot/home/config/settings/system/debug_server
printf "default_action kill\n" \
	> /boot/home/config/settings/system/debug_server/settings

LD_PRELOAD=$lib MALLOC_DEBUG=g "$app" > /tmp/slayer.out 2>&1 &
pid=$!
sleep 3

# teams and their threads end for ten seconds; Slayer refreshes every second
i=0
while [ $i -lt 20 ]; do
	sleep 0.1 &
	sleep 0.2 &
	sleep 0.3 &
	sleep 0.5
	i=$((i + 1))
done
sleep 2

if kill -0 $pid 2>/dev/null; then
	say "runs after ten seconds of teams ending: $app"
	kill -9 $pid 2>/dev/null
	wait $pid 2>/dev/null
else
	wait $pid 2>/dev/null; status=$?
	say "EXITED: $app (status $status): $(head -c 300 /tmp/slayer.out | tr '\n' ' ')"
	fail=1
fi
[ $fail = 0 ] && say PASS || say FAIL
