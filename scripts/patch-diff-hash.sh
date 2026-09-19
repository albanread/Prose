#!/bin/sh
# The hash that identifies a patch's diff: sha256 of everything from the first
# "diff --git" line on, without "index" lines (their abbreviated blob hashes
# vary with the repository) and without git format-patch's trailing signature.
# It is the same for a patch file and for `git format-patch -1 --stdout` of the
# commit it was applied as, whatever the commit message says, so
# apply-patches.sh can record it in the commit and export-patch.sh can put it
# there before the file exists.
#
# Usage: patch-diff-hash.sh [file]   (stdin without a file)
awk '
	/^diff --git / { on = 1 }
	on && !/^index / { n++; line[n] = $0 }
	END {
		while (n > 0 && line[n] == "") n--
		if (n >= 2 && line[n - 1] == "-- " && line[n] ~ /^[0-9]+\.[0-9]+/) n -= 2
		for (i = 1; i <= n; i++) print line[i]
	}' "$@" | shasum -a 256 | cut -c1-64
