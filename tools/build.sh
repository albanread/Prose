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

# hvgpu is the Prose app: build/Prose.app, so the menu bar, Dock and About panel say
# Prose. It uses the macOS 27 custom-virtio API, so it targets 27.0. A bundle's
# executable run through a symlink loses its bundle, so build/bin/hvgpu, the
# command-line entry point, is a two-line script that execs the one in the bundle.
build_app() {
	local app="$ROOT/build/Prose.app" src="$ROOT/tools/hvgpu"
	local exe="$app/Contents/MacOS/hvgpu" fresh=1 f
	if [ -z "${FORCE:-}" ] && [ -x "$exe" ] && [ -f "$OUT/hvgpu" ]; then
		for f in "$src"/*.swift "$src/Info.plist" "$0"; do [ "$exe" -nt "$f" ] || fresh=0; done
		[ $fresh = 1 ] && return
	fi
	echo "building Prose.app (hvgpu)"
	mkdir -p "$app/Contents/MacOS"
	# build beside it and rename: a running VM keeps its (old) executable intact
	swiftc -O -target arm64-apple-macos27.0 -o "$exe.new" "$src"/*.swift
	mv -f "$exe.new" "$exe"
	sed "s/@VERSION@/$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo dev)/" \
		"$src/Info.plist" > "$app/Contents/Info.plist"
	# the scripting dictionary: AppleScript, Shortcuts and osascript read this
	mkdir -p "$app/Contents/Resources"
	cp "$src/Prose.sdef" "$app/Contents/Resources/Prose.sdef"
	codesign --force -s - --entitlements "$ENT" "$app"
	rm -f "$OUT/hvgpu"
	printf '#!/bin/sh\n# hvgpu runs as the Prose app: see tools/build.sh\nexec "$(dirname "$0")/../Prose.app/Contents/MacOS/hvgpu" "$@"\n' \
		> "$OUT/hvgpu"
	chmod +x "$OUT/hvgpu"
}

build vzprobe "$ROOT/tools/vzprobe/vzprobe.swift" vz
build hvz "$ROOT/tools/hvz/hvz.swift" vz
build_app
build presenter "$ROOT/tools/presenter/presenter.swift"
