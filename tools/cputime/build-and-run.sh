#!/bin/sh
# On Prose: build the CPU time accounting test with Prose's compiler, and run it.
set -e
cd "$(dirname "$0")"
cc -O1 -o cputime cputime.c
./cputime
