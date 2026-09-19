#!/bin/sh
# NetSurf (patch 0044): the browser must be there with its libraries, start,
# and still be running after a while (no network in the test VM, so it only
# shows its window). Runs on the target from boot-test.sh; the last line is
# PASS or FAIL. Only bash and coreutils are used.
fail=0
say() { echo "$*"; echo "netsurf probe: $*" > /dev/dprintf 2>/dev/null; }
for f in /boot/system/apps/NetSurf /boot/system/lib/libcss.so.0 /boot/system/lib/libdom.so.0 \
		/boot/system/lib/libhubbub.so.0 /boot/system/lib/libcurl.so.4; do
	if [ -e "$f" ]; then say "present: $f"; else say "MISSING: $f"; fail=1; fi
done
/boot/system/apps/NetSurf > /tmp/netsurf.out 2>&1 &
pid=$!
sleep 10
if kill -0 $pid 2>/dev/null; then
	say "runs: NetSurf (10 s)"
	kill -9 $pid 2>/dev/null
	wait $pid 2>/dev/null
else
	wait $pid 2>/dev/null; status=$?
	say "EXITED: NetSurf (status $status): $(head -c 400 /tmp/netsurf.out | tr '\n' ' ')"; fail=1
fi
[ $fail = 0 ] && say PASS || say FAIL
