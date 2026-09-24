#!/bin/sh
# On Prose: build the allocator's regression test and benchmark with Prose's
# compiler, and run them.
set -e
cd "$(dirname "$0")"
cc -O2 -o malloctest malloctest.c
cc -O2 -o mallocbench mallocbench.c
./malloctest "$@"
for threads in 1 2 4 8; do
	./mallocbench -t $threads -n 4000000
done
