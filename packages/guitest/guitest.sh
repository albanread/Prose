#!/bin/bash
# guitest.sh — run a program on Prose under headless QEMU and drive it by
# hand: keys, mouse and screenshots from the host, without taking the Mac's
# keyboard or showing a window. For porting and debugging applications;
# boot-test.sh is for unattended probes.
#
#   guitest.sh start <program> [file...]
#       Boots a clone of the image and starts <program> with the files as
#       arguments. <program> is a file on the Mac (a freshly built binary:
#       copied to /boot/home) or a path on the target (/boot/system/apps/...).
#       The files are copied to /boot/home/test. When the program ends, the
#       target syncs and powers off.
#   guitest.sh ctl <commands>      keys, mouse, screenshots: see vmctl.py
#   guitest.sh stop [target paths]
#       Waits for the power-off (GUITEST_STOP_WAIT seconds, default 40, then
#       ends QEMU), prints the program's output, exit status and any crash
#       report, and copies the named files (relative to /boot, e.g.
#       home/test/x.txt) out to <work>/out/.
#
#   GUITEST_PACKAGES="sisong pe"   ports, packages or .hpkg files to install first
#   GUITEST_SCRIPT=probe.sh        run this script on the target instead of a
#                                  program (it gets PROGRAM=<target path>)
#   GUITEST_NAME=dev               names the work directory
#   GUITEST_KEEP=1                 do not power off when the program ends
#   PW_IMAGE                       the image to clone (default: the built one)
#
# Example:
#   guitest.sh start build/MyApp notes.txt
#   guitest.sh ctl click 300 200 -- type 'hello' -- key alt-s -- shot saved
#   guitest.sh ctl key alt-q
#   guitest.sh stop home/test/notes.txt
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$ROOT/private_workspace/env.sh"
PP=/Volumes/HaikuSrc/prose-packages
WORK=$PP/images/guitest-${GUITEST_NAME:-dev}
IMG=$WORK/haiku.img
BFS_SHELL=$PP/hosttools/bin/bfs_shell
mkdir -p "$WORK"; cd "$WORK"

offsets() { python3 - "$IMG" <<'PY'
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
for i in range(4):
    e = mbr[446 + 16 * i:462 + 16 * i]
    if e[4] == 0xEB:
        lba, n = struct.unpack("<II", e[8:16])
        print(lba * 512, (lba + n) * 512)
PY
}
# fs_shell leaks a vnode (and cannot unmount) after a failing mkdir or a cp
# over an existing file: only issue commands that succeed
bfs() { read -r START END < <(offsets); printf '%s\n' "$@" quit | "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$IMG"; }

case "${1:-}" in
start)
	shift
	PROGRAM=${1:?usage: guitest.sh start <program> [file...]}; shift
	[ -S qmp.sock ] && { echo "guitest: $WORK has a machine running (guitest.sh stop)" >&2; exit 1; }
	rm -f haiku.img serial.log ./*.png; rm -rf out stage; mkdir stage
	cp -c "$PW_IMAGE" "$IMG"
	# shellcheck disable=SC2086
	[ -z "${GUITEST_PACKAGES:-}" ] || "$ROOT/scripts/prosepkg" install "$IMG" $GUITEST_PACKAGES | sed -n '1p'

	cmds=("mkdir /myfs/home/test")
	case "$PROGRAM" in
	/boot/*) TARGET=$PROGRAM ;;
	*)	TARGET=/boot/home/$(basename "$PROGRAM")
		cmds+=("cp :$(cd "$(dirname "$PROGRAM")" && pwd)/$(basename "$PROGRAM") /myfs/home/$(basename "$PROGRAM")") ;;
	esac
	ARGS=""
	for f in "$@"; do
		cmds+=("cp :$(cd "$(dirname "$f")" && pwd)/$(basename "$f") /myfs/home/test/$(basename "$f")")
		ARGS="$ARGS /boot/home/test/$(basename "$f")"
	done
	NAME=$(basename "$TARGET")
	{
		echo '#!/bin/sh'
		echo 'say() { echo "$*"; echo "guitest: $*" > /dev/dprintf 2>/dev/null; }'
		# a crash must not wait in the debugger's alert: write a report instead
		echo 'mkdir -p /boot/home/config/settings/system/debug_server'
		echo "printf 'executable_actions {\\n\\t$NAME report\\n}\\n' > /boot/home/config/settings/system/debug_server/settings"
		echo "sleep ${GUITEST_SETTLE:-6}"
		echo 'cd /boot/home'
		if [ -n "${GUITEST_SCRIPT:-}" ]; then
			echo 'say started'
			echo "PROGRAM=$TARGET sh /boot/home/guitest-script.sh > /boot/home/guitest.out 2>&1"
			echo 'say "script ended: $?"'
		else
			echo "chmod +x $TARGET 2>/dev/null"
			echo "$TARGET$ARGS > /boot/home/guitest.out 2>&1 &"
			echo 'pid=$!; say started'
			echo 'wait $pid; say "exit status $?"'
		fi
		echo 'ls /boot/home/Desktop > /boot/home/guitest-desktop.txt 2>&1'
		echo 'sync'
		[ -n "${GUITEST_KEEP:-}" ] || echo 'shutdown -q'
	} > stage/guitest.sh
	# run through sh: a file copied in by bfs_shell need not be executable
	printf '#!/bin/sh\nsh /boot/home/guitest.sh > /boot/home/guitest.log 2>&1 &\n' > stage/UserBootscript
	chmod +x stage/guitest.sh stage/UserBootscript
	cmds+=("cp :$WORK/stage/guitest.sh /myfs/home/guitest.sh")
	[ -z "${GUITEST_SCRIPT:-}" ] || { cp "$GUITEST_SCRIPT" stage/guitest-script.sh; cmds+=("cp :$WORK/stage/guitest-script.sh /myfs/home/guitest-script.sh"); }
	listing=$(bfs "ls /myfs/home/config/settings/boot" 2>&1 || true)
	case "$listing" in
	*"No such file"*) bfs "mkdir -p /myfs/home/config/settings/boot" >/dev/null ;;
	*UserBootscript*) bfs "rm /myfs/home/config/settings/boot/UserBootscript" >/dev/null ;;
	esac
	bfs "${cmds[@]}" "cp :$WORK/stage/UserBootscript /myfs/home/config/settings/boot/UserBootscript" sync > /dev/null
	"$ROOT/private_workspace/skip-first-boot-prompt.sh" "$IMG" > /dev/null

	# usb-kbd, not virtio-keyboard: only the stock keyboard add-on opens the
	# Deskbar menu on the Menu key. usb-tablet: absolute pointer positions.
	# The sockets are relative: a UNIX socket path is limited to 104 bytes.
	nohup qemu-system-aarch64 -M virt -cpu host -accel hvf -smp 4 -m 2G \
		-bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd -no-reboot \
		-drive "if=none,file=$IMG,format=raw,id=hd0" -device virtio-blk-pci,drive=hd0 \
		-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 \
		-device ramfb -nic none -serial file:serial.log -display none \
		-qmp unix:qmp.sock,server,nowait > qemu.out 2>&1 &
	echo $! > qemu.pid
	for _ in $(seq 1 120); do grep -aq 'guitest: started' serial.log 2>/dev/null && break; sleep 1; done
	grep -aq 'guitest: started' serial.log || { echo "guitest: the target did not start the program (see $WORK/serial.log)" >&2; exit 1; }
	sleep "${GUITEST_APP_SETTLE:-4}"
	echo "guitest: $NAME is running on the target; work directory $WORK"
	;;
ctl)
	shift; exec python3 "$HERE/vmctl.py" "$@" ;;
stop)
	shift
	pid=$(cat qemu.pid 2>/dev/null || true)
	if [ -n "$pid" ]; then
		for _ in $(seq 1 "${GUITEST_STOP_WAIT:-40}"); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
		if kill -0 "$pid" 2>/dev/null; then
			echo "guitest: the target is still up: ending QEMU (files written since the last sync may be missing)"
			python3 "$HERE/vmctl.py" hmp quit > /dev/null 2>&1 || true
			sleep 1; kill "$pid" 2>/dev/null || true
		fi
	fi
	rm -f qmp.sock qemu.pid
	mkdir -p out
	for f in home/guitest.log home/guitest.out home/guitest-desktop.txt "$@"; do
		bfs "cp /myfs/$f :$WORK/out/$(basename "$f")" > /dev/null 2>&1 || true
		[ -e "out/$(basename "$f")" ] || echo "guitest: no $f on the target"
	done
	echo "--- target log"; cat out/guitest.log 2>/dev/null || true
	echo "--- program output"; head -c 4000 out/guitest.out 2>/dev/null || true
	if grep -q 'debug.*\.report' out/guitest-desktop.txt 2>/dev/null; then
		echo "--- CRASH REPORT on the target's Desktop:"; grep 'report' out/guitest-desktop.txt
	fi
	;;
*) sed -n '2,33p' "$0" ;;
esac
