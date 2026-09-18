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
# finddir/findpaths: Haiku shell utilities that makefiles call at parse time
# to locate the develop directory. Point them at our sysroot.
printf '#!/bin/sh\ncase "$*" in *DEVELOP*) echo %s/develop;; *) echo %s;; esac\n' \
	"$SYSROOT" "$SYSROOT" > "$STUBS/finddir"
printf '#!/bin/sh\necho %s/develop\n' "$SYSROOT" > "$STUBS/findpaths"
chmod +x "$STUBS/finddir" "$STUBS/findpaths"
# the engine lives where the finddir-resolved include expects it
mkdir -p "$SYSROOT/develop/etc"
cp "$HAIKU/data/develop/makefile-engine" "$SYSROOT/develop/etc/makefile-engine"
export PATH="$STUBS:$PATH"

CXX="$ROOT/packages/bin/haiku-c++"
CC="$ROOT/packages/bin/haiku-cc"
# The cross-gcc sysroot carries all headers/libs (see packages/README.md):
# flagless compiles, only the app libs on the link line. Private headers are
# not part of the compiler's baked search path, so add them explicitly.
PRIV=$(ls -d /Volumes/HaikuSrc/haiku/generated/cross-tools-arm64/sysroot/boot/system/develop/headers/private/*/ 2>/dev/null | sed 's:/$::; s/^/-I/' | tr '\n' ' ')
LIB=""
EXTRAINC="$PRIV -I/Volumes/HaikuSrc/haiku/headers/compatibility -I/Volumes/HaikuSrc/haiku/headers/compatibility/oldstl"

rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$SRC/." "$OUT/"
cp "$HAIKU/data/develop/makefile-engine" "$OUT/makefile-engine"

# v0: replace the engine include (findpaths-based, Haiku-only) with our copy
# and disable resource rules until host rc/xres tooling is wired up.
python3 - "$OUT" <<'EOF'
import re, sys
from pathlib import Path
for mf in sorted(Path(sys.argv[1]).glob("*[Mm]akefile")) + \
          sorted(Path(sys.argv[1]).glob("*/*[Mm]akefile")):
    if not mf.is_file():
        continue
    t = mf.read_text()
    t = re.sub(r"DEVEL_DIRECTORY\s*:?=\s*\\\n.*?\ninclude\s+\$\(DEVEL_DIRECTORY\)/etc/makefile-engine",
               "include makefile-engine", t, flags=re.S)
    t = re.sub(r"include\s+/boot/system/develop/etc/makefile-engine",
               "include makefile-engine", t)
    t = re.sub(r"^RDEFS\s*=.*?(?=^\w+\s*[:?+]?=)", "RDEFS =\n", t, flags=re.S | re.M)
    t = re.sub(r"^RSRCS\s*=.*?(?=^\w+\s*[:?+]?=)", "RSRCS =\n", t, flags=re.S | re.M)
    mf.write_text(t)
    engine = mf.parent / "makefile-engine"
    if not engine.exists():
        engine.write_text(Path(sys.argv[1], "makefile-engine").read_text())
EOF

cd "$OUT"
MF=""
for d in . main sources src app daemon; do
	for f in "$d/makefile" "$d/Makefile"; do
		[ -f "$f" ] && MF="$f" && break 2
	done
done
[ -n "$MF" ] || MF=$(find . -maxdepth 2 \( -name "[Mm]akefile" -o -name GNUmakefile \) | sort | head -1)
[ -n "$MF" ] || { echo "no makefile found for $NAME" >&2; exit 1; }
echo "using $MF"
cd "$(dirname "$MF")"
make -f "$(basename "$MF")" CXX="$CXX" CC="$CC" \
	CFLAGS="-include string.h -Wno-error" CXXFLAGS="$EXTRAINC -include string.h -include new -Wno-error" \
	LDFLAGS="-lbe -lstdc++ -lgcc_s -lroot -llocalestub -lnetwork -lnetservices -lshared -lbnetapi -ltranslation -lmedia -lgame -ltracker -lcolumnlistview -ltextencoding" LIBS="-lbe -lstdc++ -lgcc_s -lroot -llocalestub -lnetwork -lnetservices -lshared -lbnetapi -ltranslation -lmedia -lgame -ltracker -lcolumnlistview -ltextencoding" \
	-j"$(sysctl -n hw.ncpu)"
echo
# The engine parks the binary under objects.<arch>-<cc>/<name>; surface it.
find "$OUT" -path "*objects*" -type f -name "$NAME" -exec cp {} "$OUT/$NAME" \;
file "$OUT/$NAME"
ls -lh "$OUT/$NAME" && echo "BUILT: $OUT/$NAME"
