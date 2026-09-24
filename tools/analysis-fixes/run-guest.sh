#!/bin/sh
# On Prose: run afxtest the way its tests need it, from the directory this
# script is in (afxtest next to it). The results file is copied to the
# given directory after every step, so that a kernel panic keeps what was
# done before it.
#
#   1. crashing teams are killed at once: a crash alert waits forever on a
#      headless machine, and the test's child would never exit
#   2. all tests but the FAT one
#   3. the tests whose bug shows only under the guarded heap, under it
#   4. the intrusive tests (window decorator, bold font), unless --gentle
#   5. writev-zero-length on a FAT disk image -- last, because a kernel
#      without patch 0141 panics
#
# Usage: run-guest.sh [--gentle] <copy directory> [tag]
GENTLE=""
if [ "$1" = "--gentle" ]; then GENTLE=1; shift; fi
COPY="$1"
TAG="${2:-run}"
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=/boot/home/afx
OUT=$WORK/results-$TAG.txt

GUARDED="kmessage-set-twice parsedate-dash driver-settings-empty-assignment
	string-escape-in-place url-long-scheme tracker-glob nav-menu-hidden-link
	rating-edit-without-view media-file-descriptors"

copy_out()
{
	[ -n "$COPY" ] && cp "$OUT" "$COPY/results-$TAG.txt" && sync
}

rm -rf $WORK
mkdir -p $WORK /boot/home/config/settings/system/debug_server
printf "default_action kill\n" \
	> /boot/home/config/settings/system/debug_server/settings
cp "$HERE/afxtest" $WORK/
cd $WORK

{
	echo "=== $(uname -a)"
	echo "=== afxtest $(ls -l afxtest | awk '{print $5}') bytes"
	echo "=== all tests"
	./afxtest
} > $OUT 2>&1
copy_out

{
	echo "=== under the guarded heap"
	LD_PRELOAD=/boot/system/lib/libroot_debug.so MALLOC_DEBUG=g \
		./afxtest $GUARDED
} >> $OUT 2>&1
copy_out

if [ -z "$GENTLE" ]; then
	{
		echo "=== intrusive"
		./afxtest --intrusive decorator-wide-border
	} >> $OUT 2>&1
	copy_out
fi

{
	echo "=== FAT"
	dd if=/dev/zero of=$WORK/fat.img bs=1048576 count=64 2>&1 | tail -1
	registered=$(diskimage register $WORK/fat.img 2>&1)
	echo "$registered"
	device=$(echo "$registered" | grep -o '/dev/disk/virtual/files/[0-9]*/raw' \
		| head -1)
	mkfs -q -t fat "$device" AFXFAT 2>&1
	mkdir -p $WORK/fat
	mount -t fat "$device" $WORK/fat 2>&1
	df | grep -F "$WORK/fat"
	./afxtest --fat $WORK/fat writev-zero-length
	unmount $WORK/fat
	diskimage unregister $WORK/fat.img
} >> $OUT 2>&1
copy_out
