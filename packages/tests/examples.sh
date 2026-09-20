#!/bin/sh
# The examples (patch 0078) must be there and must build on the machine with
# the compiler the machine carries -- that is what they are for. Each is
# built with its own Makefile, the two that print something are run and their
# output checked, and the ones that open a window are started and have to
# still be running a moment later.
# Runs on the target from boot-test.sh; the last line is PASS or FAIL.
#   packages/boot-test.sh packages/tests/examples.sh prose_examples
fail=0
say() { echo "$*"; echo "examples probe: $*" > /dev/dprintf 2>/dev/null; }

src=/boot/system/data/prose-examples
if [ -d "$src" ]; then say "present: $src"; else say "MISSING: $src"; say FAIL; exit 1; fi

# as the README says to: they are read-only where they are installed
work=/boot/home/examples
rm -rf $work
cp -r $src $work
cd $work

say "$(clang --version | head -1)"

for example in 01-hello 02-hello-c++ 03-window 04-drawing 05-attributes 06-threads; do
	if [ ! -d "$example" ]; then say "MISSING: $example"; fail=1; continue; fi

	if (cd $example && make > build.log 2>&1); then
		name=$(ls $example | grep -v -E '\.(c|cpp|log)$|Makefile' | head -1)
		if [ -x "$example/$name" ]; then
			say "$example: built $name"
		else
			say "$example: make said nothing was wrong but made no program"; fail=1
		fi
	else
		say "$example: FAILED to build: $(head -c 300 $example/build.log | tr '\n' ' ')"
		fail=1
		continue
	fi
done

# the three that print something: run them and look at what they said
out=$(cd 01-hello && ./hello)
case "$out" in
*"Hello from Prose"*) say "01-hello ran: $out" ;;
*) say "01-hello said '$out'"; fail=1 ;;
esac

out=$(cd 02-hello-c++ && ./hello | head -1)
case "$out" in
*compiles*its*own*) say "02-hello-c++ ran: $out" ;;
*) say "02-hello-c++ said '$out'"; fail=1 ;;
esac

# the attributes come back in whatever order the file system keeps them,
# so look for the one written, not for a particular line
out=$(cd 05-attributes && ./attributes /boot/home/attr-test 2>&1)
case "$out" in
*Media:Title*"An example"*) say "05-attributes ran: $(echo "$out" | grep Media:Title)" ;;
*) say "05-attributes said '$(echo $out | head -c 200)'"; fail=1 ;;
esac

out=$(cd 06-threads && ./threads | tail -1)
case "$out" in
*"lock did its job"*) say "06-threads ran: $out" ;;
*) say "06-threads said '$out'"; fail=1 ;;
esac

# the two that open a window: start them, and they must still be there
for example in 03-window 04-drawing; do
	name=${example#*-}
	(cd $example && ./$name > run.log 2>&1) &
	pid=$!
	sleep 5
	if kill -0 $pid 2>/dev/null; then
		say "$example: opened its window and stayed up"
		kill -9 $pid 2>/dev/null
		wait $pid 2>/dev/null
	else
		wait $pid 2>/dev/null; status=$?
		say "$example: EXITED at once (status $status): $(head -c 200 $example/run.log | tr '\n' ' ')"
		fail=1
	fi
done

[ $fail = 0 ] && say PASS || say FAIL
