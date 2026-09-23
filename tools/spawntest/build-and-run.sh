#!/bin/sh
# On Prose: build the posix_spawn test with Prose's own compiler, and run it.
set -e
cd "$(dirname "$0")"
cc -O2 -o spawnhelper spawnhelper.c
cc -O2 -o spawntest spawntest.c
./spawntest
