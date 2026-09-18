#!/bin/bash
# pkg-build.sh <name> — cross-build a HaikuArchives app for arm64 (Prose).
#
# Handles the "Haiku Generic Makefile" layout most haikuarchives apps use:
# the engine include is redirected to our copy, and (v0) resource rules are
# disabled until host rc/xres tooling is wired up.
set -euo pipefail
NAME=${1:?usage: pkg-build.sh <name>}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/packages/src/$NAME"
HAIKU=/Volumes/HaikuSrc/haiku
SYSROOT=/Volumes/HaikuSrc/prose-packages/sysroot-arm64
OUT="$ROOT/packages/out/$NAME"

[ -d "$SRC" ] || { echo "no such package: $SRC" >&2; exit 1; }

# Guest-only post-link tools (resource merge/mime) are no-ops in v0 builds.
STUBS="$ROOT/packages/bin"
mkdir -p "$STUBS"
for t in xres mimeset settype resattr; do
	printf '#!/bin/sh\nexit 0\n' > "$STUBS/$t"
	chmod +x "$STUBS/$t"
done
export PATH="$STUBS:$PATH"

CXX="$HAIKU/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++"
CC="$HAIKU/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-gcc"
INC=$(cat "$ROOT/packages/incflags.txt")
LIB="-B$SYSROOT/develop/lib -L$SYSROOT/develop/lib"

rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$SRC/." "$OUT/"
cp "$HAIKU/data/develop/makefile-engine" "$OUT/makefile-engine"

# v0: replace the engine include (findpaths-based, Haiku-only) with our copy
# and disable resource rules until host rc/xres tooling is wired up.
python3 - "$OUT" <<'EOF'
import re, sys
from pathlib import Path
for mf in (Path(sys.argv[1]) / "Makefile", Path(sys.argv[1]) / "makefile"):
    if not mf.exists():
        continue
    t = mf.read_text()
    t = re.sub(r"DEVEL_DIRECTORY\s*:?=\s*\\\n.*?\ninclude\s+\$\(DEVEL_DIRECTORY\)/etc/makefile-engine",
               "include makefile-engine", t, flags=re.S)
    t = re.sub(r"^RDEFS\s*=.*?(?=^\w+\s*[:?+]?=)", "RDEFS =\n", t, flags=re.S | re.M)
    t = re.sub(r"^RSRCS\s*=.*?(?=^\w+\s*[:?+]?=)", "RSRCS =\n", t, flags=re.S | re.M)
    mf.write_text(t)
EOF

cd "$OUT"
make -f Makefile CXX="$CXX" CC="$CC" \
	CXXFLAGS="$INC -Wno-error" \
	LDFLAGS="$LIB -lbe -lstdc++ -lgcc_s -lroot -llocalestub" LIBS="-lbe -lstdc++ -lgcc_s -lroot -llocalestub" \
	-j"$(sysctl -n hw.ncpu)"
echo
# The engine parks the binary under objects.<arch>-<cc>/<name>; surface it.
find "$OUT" -path "*objects*" -type f -name "$NAME" -exec cp {} "$OUT/$NAME" \;
file "$OUT/$NAME"
ls -lh "$OUT/$NAME" && echo "BUILT: $OUT/$NAME"
