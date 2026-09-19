#!/bin/sh
# NetSurf (patch 0044): the browser must be there with its libraries, start,
# stay up, and quit cleanly when asked (hey ... quit): 3.11's Haiku front end
# double-freed two option strings at exit until the overlay patch. Runs on
# the target from boot-test.sh; the last line is PASS or FAIL. Only bash,
# coreutils and hey are used.
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
	hey NetSurf quit > /dev/null 2>&1
	for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $pid 2>/dev/null || break; sleep 1; done
	if kill -0 $pid 2>/dev/null; then
		say "did not quit within 10 s of hey quit"; kill -9 $pid 2>/dev/null; fail=1
	else
		wait $pid 2>/dev/null; status=$?
		if [ $status = 0 ]; then say "quit cleanly (status 0)"; else say "quit with status $status: $(head -c 300 /tmp/netsurf.out | tr '\n' ' ')"; fail=1; fi
	fi
else
	wait $pid 2>/dev/null; status=$?
	say "EXITED: NetSurf (status $status): $(head -c 400 /tmp/netsurf.out | tr '\n' ' ')"; fail=1
fi
[ $fail = 0 ] && say PASS || say FAIL
