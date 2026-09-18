#!/bin/sh
# pkg-env.sh — create/refresh the Prose cross-build environment.
#
# The environment (prose-packages/env) is the ONLY interface for building
# Haiku-native software on this host. Decisions live here, not in per-app
# invocations:
#
#   haiku-c++ / haiku-cc   compiler wrappers:
#     - probe invocations (-dumpversion ...) pass through untouched
#     - -B<sysroot>/develop/lib on every invocation: crt objects and every
#       -l flag resolve without any -L, for any build system
#     - private headers, the be/-style compatibility tree, and old-STL
#       shims via -idirafter (searched last: nothing shadows libc)
#   haiku-ar / haiku-ranlib                    archivers
#   finddir findpaths pkg-config mkdepend      redirected to our sysroot/deps
#   xres mimeset settype resattr               no-ops (v0 builds)
#
# Everything is generated from this script; run it after OS/tree changes.
set -e
PROSE=/Volumes/HaikuSrc/prose-packages
ENV=$PROSE/env
SYS=$PROSE/sysroot-arm64
BIN=$ENV/bin
REAL=/Volumes/HaikuSrc/haiku/generated/cross-tools-arm64/bin/aarch64-unknown-haiku
TREE=/Volumes/HaikuSrc/haiku

mkdir -p "$BIN"

cat > "$BIN/haiku-c++" <<EOF
#!/bin/sh
REAL=$REAL-g++
LIB=$SYS/develop/lib
HDR=$SYS/develop/headers
for a in "\$@"; do
	case "\$a" in
	-dumpversion|-dumpmachine|-print-*|-Wl,--version|--version)
		exec "\$REAL" "\$@" ;;
	esac
done
AFTER=""
for d in "\$HDR"/private/*/; do
	AFTER="\$AFTER -idirafter\${d%/}"
done
exec "\$REAL" -B"\$LIB" \$AFTER \\
	-idirafter "\$HDR" \\
	-idirafter $TREE/headers/compatibility \\
	-idirafter $TREE/headers/compatibility/oldstl \\
	"\$@"
EOF
chmod +x "$BIN/haiku-c++"

printf '#!/bin/sh\nexec %s/haiku-c++ "$@"\n' "$BIN" > "$BIN/haiku-cc"
chmod +x "$BIN/haiku-cc"

for t in ar ranlib nm strip objcopy; do
	printf '#!/bin/sh\nexec %s-%s "$@"\n' "$REAL" "$t" > "$BIN/haiku-$t"
	chmod +x "$BIN/haiku-$t"
done

# Haiku shell utilities that makefiles call at parse time.
printf '#!/bin/sh\ncase "$*" in *DEVELOP*) echo %s/develop;; *) echo %s;; esac\n' \
	"$SYS" "$SYS" > "$BIN/finddir"
printf '#!/bin/sh\n[ "$1" = -r ] && shift 2\nfor a in "$@"; do case "$a" in freetype2) echo %s;; esac; done\n' \
	"" > "$BIN/findpaths"
printf '#!/bin/sh\ncase "$*" in\n*--cflags*) echo "-I%s/headers/freetype2";;\n*--libs*) echo "-lfreetype";;\n*) exit 1;;\nesac\n' \
	"$SYS/develop" > "$BIN/pkg-config"
for t in mkdepend xres mimeset settype resattr; do
	printf '#!/bin/sh\nexit 0\n' > "$BIN/$t"
	chmod +x "$BIN/$t"
done
chmod +x "$BIN/finddir" "$BIN/findpaths" "$BIN/pkg-config"

# The engine goes where finddir-resolved includes look for it.
mkdir -p "$SYS/develop/etc"
cp "$TREE/data/develop/makefile-engine" "$SYS/develop/etc/makefile-engine"

echo "env ready: $BIN"
