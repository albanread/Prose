#!/bin/bash
# Make locally built packages available to the Haiku build.
#
#   private_workspace/addpkg.sh libjpeg_turbo tiff libwebp giflib
#
# For each name it takes <name> and <name>_devel from prosepkg's repository
# (PW_PACKAGE_SOURCE: another directory of .hpkg files), rewrites the vendor to
# the one the repository expects ("Haiku Project" - package_repo rejects
# anything else), and drops the result in generated/download/. It then prints
# the lines to add to build/jam/repositories/HaikuPorts/arm64.
#
# The build resolves packages from that directory rather than the network
# because HAIKU_NO_DOWNLOADS is set; see README.md.
set -euo pipefail
source "$(dirname "$0")/env.sh"

# only the repository: the rest of prose-packages holds work dirs and the
# quarantined packages of 2026-09-18
SRC=${PW_PACKAGE_SOURCE:-/Volumes/HaikuSrc/prose-packages/repo}
DOWNLOAD="$PW_HAIKU/generated/download"
TOOLS="$PW_HAIKU/generated/objects/darwin/arm64/release/tools"
export DYLD_LIBRARY_PATH="$PW_HAIKU/generated/objects/darwin/lib"
PACKAGE="$TOOLS/package/package"

[ $# -gt 0 ] || { echo "usage: addpkg.sh <package>... (base names, _devel is added too)" >&2; exit 64; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
entries=()

for base in "$@"; do
	for name in "$base" "${base}_devel"; do
		# (no grep in this pipeline: under pipefail a name with no match,
		# e.g. a port without a _devel package, would end the script)
		file=$(find "$SRC" -maxdepth 1 -name "$name-*-arm64.hpkg" | sort | tail -1)
		if [ -z "$file" ]; then
			echo "  $name: not built in $SRC, skipping" >&2
			continue
		fi
		leaf=$(basename "$file")
		version=${leaf#"$name-"}; version=${version%-arm64.hpkg}
		d="$work/$name"; mkdir -p "$d"
		( cd "$d" && "$PACKAGE" extract "$file" )
		# the repository only accepts its own vendor
		sed -i '' 's/^vendor.*$/vendor\t\t"Haiku Project"/' "$d/.PackageInfo"
		( cd "$d" && "$PACKAGE" create -q "$DOWNLOAD/$leaf" )
		echo "  $leaf"
		entries+=("	$name-$version")
	done
done

echo
echo "Add to build/jam/repositories/HaikuPorts/$(basename "${HAIKU_PACKAGING_ARCH:-arm64}"):"
printf '%s\n' "${entries[@]}"
