#!/bin/sh
# The Midi Kit's file player deleted while it plays, and by its own song
# hook, and stopped at once (patches 0050, 0051): runs midikit_check
# (packages/builder/overlay/prose-tests/midikit_check) on a demo tune, then
# again on the guarded heap (MALLOC_DEBUG=g, libroot_debug.so from
# haiku_devel), where a write to freed memory faults. Needs a sound card --
# the built-in synthesizer only paces its notes when it has an audio
# output -- and haiku_devel of the same build, so run it as
#   BOOT_TEST_SOUND=1 packages/boot-test.sh packages/tests/midikit.sh midikit_check \
#     /Volumes/HaikuSrc/haiku/generated/objects/haiku/arm64/packaging/packages/haiku_devel.hpkg
# A crash does not wait for a click: debug_server saves a report (printed
# here) and kills the team. Only bash, coreutils and ps are used.
tune="/boot/system/data/music/Beethoven - Ode to Joy.mid"
fail=0
say() { echo "$*"; echo "midikit probe: $*" > /dev/dprintf 2>/dev/null; }

settings=/boot/home/config/settings/system/debug_server
mkdir -p $settings
printf 'executable_actions {\n\tmidikit_check report\n}\n' > $settings/settings

# the synthesizer needs the media server: the boot script runs early
for i in $(seq 30); do
	ps | grep -q media_addon_server && break
	sleep 1
done
sleep 5
say "media: $(ps | grep -c 'media_server\|media_addon_server') servers up"

run() {
	# run <label> <rounds> <last-client rounds> [environment...]
	label=$1; rounds=$2; last=$3; shift 3
	env "$@" midikit_check "$tune" $rounds $last 2>&1
	status=$?
	say "$label: exit status $status"
	[ $status = 0 ] || fail=1
}

run "midikit_check" 12 4
debuglib=/boot/system/lib/libroot_debug.so
if [ -f $debuglib ]; then
	echo "--- again on the guarded heap"
	run "midikit_check on the guarded heap" 4 2 MALLOC_DEBUG=g LD_PRELOAD=$debuglib
else
	say "no $debuglib: no guarded heap run"
fi

for report in /boot/home/Desktop/midikit_check*.report; do
	[ -f "$report" ] || continue
	say "debug report: $report"
	head -c 6000 "$report"
	fail=1
done
[ $fail = 0 ] && echo PASS || echo FAIL
