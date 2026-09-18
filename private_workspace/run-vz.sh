#!/bin/bash
# Boot a fresh copy of the private image under Virtualization.framework with hvgpu.
# usage: run-vz.sh <name> [hvgpu options...]
#   defaults: --cpus 8 --disk nvme, windowed, RAM console -> work/<name>/ramconsole.log
#   e.g. run-vz.sh smoke
#        run-vz.sh blk --headless --seconds 100 --disk virtio
#        PW_PACKAGES=@codecs run-vz.sh codecs   # with prosepkg packages installed
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:?usage: run-vz.sh <name> [--keep] [hvgpu options...]}
shift
# --keep: reuse this run's disk and EFI variables, so guest changes (settings,
# files, the desktop background Tracker writes at first boot) survive the next
# run. Without it every run starts from an identical first boot, which is what
# the tests want but makes the VM useless as a machine you actually use.
KEEP=0
if [ "${1:-}" = "--keep" ]; then KEEP=1; shift; fi
[ -f "$PW_IMAGE" ] || { echo "no image at $PW_IMAGE: run build.sh" >&2; exit 1; }
[ -x "$HVGPU" ] || "$PW_ROOT/tools/build.sh"
D="$PW_WORK/$NAME"
mkdir -p "$D"
if [ "$KEEP" = 1 ] && [ -f "$D/haiku.img" ]; then
	echo ">>> keeping $D/haiku.img (guest changes persist; delete it to start over)"
else
	cp "$PW_IMAGE" "$D/haiku.img"
	rm -f "$D/efivars"
fi
# PW_PACKAGES="<ports|packages|@set>": install packages built by prosepkg into the copy
if [ -n "${PW_PACKAGES:-}" ]; then
	"$PW_ROOT/scripts/prosepkg" install "$D/haiku.img" $PW_PACKAGES
fi
# PW_INJECT_SCRIPT=<file>: install it as the guest's UserBootscript (runs at boot as root)
if [ -n "${PW_INJECT_SCRIPT:-}" ] && [ "$KEEP" != 1 ]; then
	read -r START END < <(python3 - "$D/haiku.img" <<'PY'
import struct, sys
mbr = open(sys.argv[1], "rb").read(512)
for i in range(4):
    entry = mbr[446 + 16 * i:462 + 16 * i]
    if entry[4] == 0xEB:
        lba, count = struct.unpack("<II", entry[8:16])
        print(lba * 512, (lba + count) * 512)
        break
PY
)
	printf '%s\n' "mkdir /myfs/home/config/settings/boot" \
		"cp :$(cd "$(dirname "$PW_INJECT_SCRIPT")" && pwd)/$(basename "$PW_INJECT_SCRIPT") /myfs/home/config/settings/boot/UserBootscript" \
		"sync" "quit" \
		| "$BFS_SHELL" --start-offset "$START" --end-offset "$END" "$D/haiku.img" > "$D/inject.log" 2>&1 || true
	echo ">>> injected $PW_INJECT_SCRIPT as UserBootscript"
fi
echo ">>> $D/haiku.img (fresh copy of $(ls -l "$PW_IMAGE" | awk '{print $6, $7, $8}') build)"
"$HVGPU" "$D/haiku.img" --efivars "$D/efivars" --ramconsole-log "$D/ramconsole.log" \
	--cpus 8 --disk nvme "$@" | tee "$D/hvgpu.log"
echo ">>> markers:"
grep -aE 'UEFI time|GetTime' "$D/ramconsole.log" | head -1 | cut -c1-120 || true
awk '/----- HAIKU-RAMLOG-V1/{f=1} f' "$D/ramconsole.log" \
	| grep -aE 'logical cpus|rtc:|Mounted boot|first login|virtio_gpu: acc|acpi_gpio_events|acpi_button|arch_cpu_shutdown|PANIC|ISR 0|virtio_block:' \
	| awk '!seen[$0]++' | cut -c1-120 || true
grep -E 'guest powered off|force stopped' "$D/hvgpu.log" | tail -1 || true
