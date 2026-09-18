#!/bin/bash
# Build the Swift host tools into build/bin.
# Tools that use Virtualization.framework are ad-hoc signed with the
# com.apple.security.virtualization entitlement.
# Set FORCE=1 to rebuild everything.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/bin"
ENT="$ROOT/tools/common/vz.entitlements"
mkdir -p "$OUT"

build() { # name source-file-or-dir [vz] [target]
	local name=$1 src=$2 vz=${3:-} target=${4:-arm64-apple-macos26.0}
	local -a files
	if [ -d "$src" ]; then files=("$src"/*.swift); else files=("$src"); fi
	if [ -z "${FORCE:-}" ]; then
		local fresh=1 f
		for f in "${files[@]}"; do [ "$OUT/$name" -nt "$f" ] || fresh=0; done
		[ $fresh = 1 ] && return
	fi
	echo "building $name"
	# Pin the deployment target: the host compiler otherwise targets the running OS.
	swiftc -O -target "$target" -o "$OUT/$name" "${files[@]}"
	if [ -n "$vz" ]; then
		codesign --force -s - --entitlements "$ENT" "$OUT/$name"
	fi
}

build vzprobe "$ROOT/tools/vzprobe/vzprobe.swift" vz
build hvz "$ROOT/tools/hvz/hvz.swift" vz
# hvgpu uses the macOS 27 custom-virtio API, so it targets 27.0.
build hvgpu "$ROOT/tools/hvgpu" vz arm64-apple-macos27.0
build presenter "$ROOT/tools/presenter/presenter.swift"
