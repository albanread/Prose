#!/bin/sh
# Scan the Prose Haiku tree with the analyzers on the Mac. Builds nothing and
# writes nothing in the tree: jam only prints its commands (-n -a -dx).
#
# Usage: tools/static-analysis/run.sh <output dir> [core|all] [quick]
#   core  the kernel, libroot, runtime_loader, the core kits, the servers,
#         Tracker, Deskbar, the main file systems and our drivers (default)
#   all   everything in the @prose-mmc image
#   quick skip clang-tidy's static analyzer (the slowest step: 2 min for the
#         core on 16 cores)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TREE=/Volumes/HaikuSrc/haiku
OUT="$(mkdir -p "$1" && cd "$1" && pwd)"
SCOPE="${2:-core}"
QUICK="$3"
LLVM=/opt/homebrew/opt/llvm/bin
AST_GREP="$(command -v ast-grep || echo "$HOME/Library/Application Support/NewReview/ToolCache/ast-grep/0.42.1-r1/ast-grep")"
JOBS=$(sysctl -n hw.ncpu)
export PATH="$HOME/bin:$PATH"
ulimit -n 1024

cd "$TREE"
if [ "$SCOPE" = all ]; then
	set -- @prose-mmc
else
	set -- kernel_arm64 runtime_loader libroot.so libbe.so libnetwork.so \
		libbnetapi.so libtracker.so libpackage.so libmedia.so libtranslation.so \
		libgame.so libdevice.so libtextencoding.so app_server registrar \
		mount_server input_server net_server launch_daemon debug_server \
		package_daemon Tracker Deskbar bfs packagefs hostfs virtio_fs \
		prose_display prose_chip prose_midi
fi
echo ">>> compile commands ($SCOPE)"
jam -n -a -dx -q "$@" > "$OUT/jam.txt" 2>&1
python3 "$HERE/mkcdb.py" "$OUT/jam.txt" "$OUT/compile_commands.json"

echo ">>> clang parses"
python3 "$HERE/parsecheck.py" "$OUT/compile_commands.json" "$OUT/parse-failures.json"
echo ">>> clang's warnings"
python3 "$HERE/warncheck.py" "$OUT/compile_commands.json" "$OUT/clang-warnings.txt"
echo ">>> matchers: pointers into temporaries; byte counts compared with B_OK"
python3 "$HERE/runquery.py" "$OUT" "$HERE/queries/dangling.query" \
	| grep 'note: "pointer' | sort -u > "$OUT/dangling.txt" || true
python3 "$HERE/runquery.py" "$OUT" "$HERE/queries/ioresult.query" \
	| grep 'note: "byte-count' | sort -u > "$OUT/ioresult.txt" || true
wc -l "$OUT/dangling.txt" "$OUT/ioresult.txt"
echo ">>> ast-grep rules"
"$AST_GREP" scan -c "$HERE/sgconfig.yml" --json=stream "$TREE/src" \
	> "$OUT/ast-grep.json" 2> "$OUT/ast-grep.err" || true
wc -l < "$OUT/ast-grep.json"
echo ">>> GCC -fanalyzer (C files of the kernel and libroot)"
python3 "$HERE/gccanalyzer.py" "$OUT/jam.txt" "$OUT/gcc-analyzer.txt"

echo ">>> flawfinder (level 4 and up)"
if command -v flawfinder > /dev/null; then
	flawfinder --minlevel=4 --csv --quiet src/system/kernel src/system/libroot \
		src/system/runtime_loader src/system/libnetwork src/kits src/servers \
		src/add-ons/kernel src/apps/deskbar > "$OUT/flawfinder.csv" 2> /dev/null || true
fi

if [ "$QUICK" != quick ] && command -v cppcheck > /dev/null; then
	# cppcheck does not know the target: it must be told the architecture
	# (HaikuConfig.h stops at "Unsupported architecture!" otherwise) and that
	# the environment is hosted (libstdc++ checks __STDC_HOSTED__)
	echo ">>> cppcheck"
	mkdir -p "$OUT/cppcheck" "$OUT/cppcheck-build"
	python3 "$HERE/cppcheckdb.py" "$OUT/compile_commands.json" \
		"$OUT/cppcheck/compile_commands.json"
	cppcheck --project="$OUT/cppcheck/compile_commands.json" \
		-D__aarch64__=1 -D__ARM_ARCH=8 -D__HAIKU__=1 -D__LP64__=1 -D__ELF__=1 \
		-D__GNUC__=13 -D__GNUC_MINOR__=3 -D__STDC_HOSTED__=1 --library=posix \
		-j "$JOBS" --enable=warning,portability --platform=unix64 \
		--cppcheck-build-dir="$OUT/cppcheck-build" \
		--suppress=missingInclude --suppress=missingIncludeSystem \
		--suppress=unmatchedSuppression --suppress=toomanyconfigs \
		--suppress=normalCheckLevelMaxBranches --suppress=checkersReport \
		--suppress=uninitMemberVarNoCtor --suppress=dangerousTypeCast \
		--template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
		--quiet 2> "$OUT/cppcheck.txt" > /dev/null || true
fi

if [ "$QUICK" != quick ]; then
	echo ">>> clang-tidy and the clang static analyzer"
	"$LLVM/run-clang-tidy" -clang-tidy-binary "$LLVM/clang-tidy" -p "$OUT" \
		-j "$JOBS" -quiet -checks="$(cat "$HERE/clang-tidy-checks.txt")" \
		> "$OUT/clang-tidy.txt" 2> "$OUT/clang-tidy.err" || true
	python3 "$HERE/tidysum.py" "$OUT/clang-tidy.txt" "$OUT/clang-tidy-findings.json"
fi
echo ">>> results in $OUT"
