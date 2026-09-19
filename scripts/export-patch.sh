#!/bin/sh
# Export the newest commit on the Haiku tree's "prose" branch as the next
# patches/haiku/NNNN-<subject>.patch, so main carries it.
#
# The commit first gets the record apply-patches.sh looks for, "Prose-Patch:
# <file> <hash of the diff>" (scripts/patch-diff-hash.sh), unless it has it
# already; then the patch is written with git format-patch. Only HEAD can be
# amended, so export commits one at a time, oldest first. A commit that was
# exported before keeps its file name, and the file is rewritten (after a
# `git commit --amend`, say); add the row to patches/haiku/README.md by hand.
#
# Usage: export-patch.sh [haiku tree]   (default /Volumes/HaikuSrc/haiku)
set -e
TREE="${1:-/Volumes/HaikuSrc/haiku}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PATCHES="$(cd "$HERE/../patches/haiku" && pwd)"
BRANCH=prose

cd "$TREE"
[ "$(git branch --show-current)" = "$BRANCH" ] \
	|| { echo "export-patch: $TREE is not on $BRANCH" >&2; exit 1; }
[ -z "$(git status --porcelain --untracked-files=no)" ] \
	|| { echo "export-patch: $TREE has uncommitted changes" >&2; exit 1; }

message="$(git log -1 --format=%B)"
name="$(printf '%s\n' "$message" | sed -n 's/^Prose-Patch: \(.*\) [0-9a-f]\{64\}$/\1/p' | tail -1)"
if [ -z "$name" ]; then
	last="$(ls "$PATCHES" | sed -n 's/^\([0-9][0-9][0-9][0-9]\)-.*\.patch$/\1/p' | sort | tail -1)"
	next="$(printf '%04d' $(( $(printf '%s' "$last" | sed 's/^0*//') + 1 )))"
	slug="$(git log -1 --format=%s | tr 'A-Z' 'a-z' | sed 's/[^a-z0-9]\{1,\}/-/g; s/^-//; s/-$//' | cut -c1-60)"
	name="$next-$slug.patch"
fi
sum="$(git format-patch -1 --stdout --no-signature HEAD | "$HERE/patch-diff-hash.sh")"
if ! printf '%s\n' "$message" | grep -qx "Prose-Patch: $name $sum"; then
	# amending the message leaves the diff, and so the hash, as it is
	git -c trailer.ifexists=replace commit -q --amend --no-edit --trailer "Prose-Patch: $name $sum"
fi
git format-patch -1 --stdout --no-signature HEAD > "$PATCHES/$name"
echo "export-patch: $PATCHES/$name  ($(git rev-parse --short HEAD): $(git log -1 --format=%s))"
echo "  now add its row to patches/haiku/README.md"
