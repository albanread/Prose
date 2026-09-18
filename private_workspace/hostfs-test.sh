#!/bin/bash
# HostFS end-to-end test under VZ: a scratch share with known content, the
# guest probe (hostfsprobe.sh) as UserBootscript, then both views compared.
# usage: hostfs-test.sh [name] [hvgpu options...]
#   e.g. hostfs-test.sh hostfs --display s2 --screenshot private_workspace/work/hostfs/shot.png
set -euo pipefail
source "$(dirname "$0")/env.sh"
NAME=${1:-hostfs}
[ $# -gt 0 ] && shift

SHARE="$PW_WORK/$NAME-share"
rm -rf "$SHARE"
mkdir -p "$SHARE/tree/a/b"
echo "Hello from the Mac" > "$SHARE/hello.txt"
head -c $((64 << 20)) /dev/urandom > "$SHARE/random.bin"
shasum -a 256 "$SHARE/random.bin" | cut -d' ' -f1 > "$SHARE/random.bin.sha256"
for i in $(seq 1 50); do echo "file $i" > "$SHARE/tree/a/f$i.txt"; done
echo deep > "$SHARE/tree/a/b/deep.txt"

PW_INJECT_SCRIPT="$PW_ROOT/private_workspace/hostfsprobe.sh" \
	"$PW_ROOT/private_workspace/run-vz.sh" "$NAME" --headless --seconds 120 \
	--share "$SHARE" "$@"

echo ">>> HostFS kernel log:"
grep -aE 'virtio_fs|hostfs' "$PW_WORK/$NAME/ramconsole.log" | awk '!seen[$0]++' | cut -c1-200 || true
echo ">>> the guest's report (written into the share):"
cat "$SHARE/probe-result.txt" 2>/dev/null || echo "no probe-result.txt in the share"
echo ">>> the share as the Mac sees it:"
ls -la "$SHARE"
if [ -f "$SHARE/random-copy.bin" ]; then
	echo "random.bin      $(cat "$SHARE/random.bin.sha256")"
	echo "random-copy.bin $(shasum -a 256 "$SHARE/random-copy.bin" | cut -d' ' -f1)"
fi
[ -f "$SHARE/from-haiku-renamed.txt" ] && cat "$SHARE/from-haiku-renamed.txt"
exit 0
