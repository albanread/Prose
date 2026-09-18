#!/bin/bash
# Boot a fresh copy of the private image under Virtualization.framework with hvgpu.
# usage: run-vz.sh <name> [hvgpu options...]
#   defaults: --cpus 8 --disk nvme, windowed, RAM console -> work/<name>/ramconsole.log
#   e.g. run-vz.sh smoke
#        run-vz.sh blk --headless --seconds 100 --disk virtio
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:?usage: run-vz.sh <name> [hvgpu options...]}
shift
[ -f "$PW_IMAGE" ] || { echo "no image at $PW_IMAGE: run build.sh" >&2; exit 1; }
[ -x "$HVGPU" ] || "$PW_ROOT/tools/build.sh"
D="$PW_WORK/$NAME"
mkdir -p "$D"
cp "$PW_IMAGE" "$D/haiku.img"
rm -f "$D/efivars"
echo ">>> $D/haiku.img (fresh copy of $(ls -l "$PW_IMAGE" | awk '{print $6, $7, $8}') build)"
"$HVGPU" "$D/haiku.img" --efivars "$D/efivars" --ramconsole-log "$D/ramconsole.log" \
	--cpus 8 --disk nvme "$@" | tee "$D/hvgpu.log"
echo ">>> markers:"
awk '/----- HAIKU-RAMLOG-V1/{f=1} f' "$D/ramconsole.log" \
	| grep -aE 'logical cpus|Mounted boot|first login|virtio_gpu: acc|PANIC|ISR 0|virtio_block:' \
	| awk '!seen[$0]++' | cut -c1-120 || true
