#!/bin/bash
# pkg-sysroot.sh — (re)build the arm64 cross-development environment used by
# scripts/pkg-build.sh. Two sysroots are involved:
#
#  1. prose-packages/sysroot-arm64  — full package content: haiku_devel +
#     gcc_syslibs_devel + haiku (runtime) + gcc_syslibs (runtime) hpkgs from
#     our own build. Source of -B/-L libs and the makefile-engine location.
#  2. the cross-gcc's own configured sysroot
#     (generated/cross-tools-arm64/sysroot) — populated exactly like the
#     bootstrap's build_cross_tools_gcc4 does it (config/os/posix headers),
#     plus headers/private and compatibility shims for third-party apps.
#     With this in place, compilations are FLAGLESS: the compiler's baked
#     search paths resolve <Application.h>, <string.h>, <map> natively.
set -euo pipefail
HAIKU=/Volumes/HaikuSrc/haiku
PKGTOOLS=$HAIKU/generated/objects/darwin/arm64/release/tools/package/package
HPKGS=$HAIKU/generated/objects/haiku/arm64/packaging/packages
DL=$HAIKU/generated/download
PS=/Volumes/HaikuSrc/prose-packages/sysroot-arm64
CROSS=$HAIKU/generated/cross-tools-arm64/sysroot/boot/system

# --- 1. the packages sysroot -------------------------------------------------
rm -rf "$PS"
mkdir -p "$PS"
cd "$PS"
"$PKGTOOLS" extract "$HPKGS/haiku_devel.hpkg"
"$PKGTOOLS" extract "$HPKGS/haiku.hpkg"
"$PKGTOOLS" extract "$DL"/gcc_syslibs-13.3.0_*_bootstrap-1-arm64.hpkg
"$PKGTOOLS" extract "$DL"/gcc_syslibs_devel-13.3.0_*_bootstrap-1-arm64.hpkg
# the devel .so symlinks (../../lib/...) resolve against the runtime lib dir,
# which the two runtime packages just populated. Repair anything still broken.
python3 - <<'EOF'
import os
from pathlib import Path
dev = Path("develop/lib")
for l in sorted(dev.iterdir()):
    if l.is_symlink() and not l.exists():
        t = Path("../..") / "lib" / l.name
        if (dev / t).exists():
            l.unlink(); l.symlink_to(os.readlink(l) if (dev / t).resolve().exists() else t)
print("symlink repair pass done")
EOF

# --- 2. the cross-gcc sysroot (bootstrap layout + app needs) -----------------
mkdir -p "$CROSS/develop/headers/config" "$CROSS/develop/headers/os" \
         "$CROSS/develop/headers/posix" "$CROSS/develop/lib" "$CROSS/lib"
cp -R "$HAIKU/headers/config/." "$CROSS/develop/headers/config/"
cp -R "$HAIKU/headers/os/." "$CROSS/develop/headers/os/"
cp -R "$HAIKU/headers/posix/." "$CROSS/develop/headers/posix/"
cp -R "$HAIKU/headers/private" "$CROSS/develop/headers/"
cp -R "$PS/develop/lib/." "$CROSS/develop/lib/"
cp -R "$PS/lib/." "$CROSS/lib/"

# old-BeOS STL header names (map.h etc.) used by several haikuarchives apps
OLDSTL=$HAIKU/headers/compatibility/oldstl
mkdir -p "$OLDSTL"
for h in map list vector set string queue stack deque algorithm functional utility; do
	printf '#include <%s>\n' "$h" > "$OLDSTL/$h.h"
done

# makefile-engine where finddir/findpaths stubs resolve it
mkdir -p "$PS/develop/etc"
cp "$HAIKU/data/develop/makefile-engine" "$PS/develop/etc/makefile-engine"
echo "sysroots ready (flagless cross compiles enabled)"
