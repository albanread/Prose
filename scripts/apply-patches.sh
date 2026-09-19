#!/bin/sh
# Bring a Haiku tree up to main's changes: patches/haiku/NNNN-*.patch, one
# commit each, on the branch "prose" (upstream's master stays as it is).
# scripts/build-image.sh runs this before it builds.
#
# Every commit records which patch file it came from and the hash of that
# file's diff ("Prose-Patch:" in the message; scripts/patch-diff-hash.sh), so
# a second run applies only what is missing, and a patch whose diff changed
# in main after it was applied is reported instead of silently left out.
# Commits made on the branch and exported with scripts/export-patch.sh carry
# the record already. A tree with uncommitted changes to tracked files is
# refused. --dry-run applies the series to a scratch index and reports; the
# tree is not touched.
#
# Usage: apply-patches.sh [--dry-run] [haiku tree]   (default /Volumes/HaikuSrc/haiku)
set -e
DRY=""
if [ "$1" = "--dry-run" ] || [ "$1" = "-n" ]; then DRY=1; shift; fi
TREE="${1:-/Volumes/HaikuSrc/haiku}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PATCHES="$(cd "$HERE/../patches/haiku" && pwd)"
HASH="$HERE/patch-diff-hash.sh"
BRANCH=prose

cd "$TREE"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 \
	|| { echo "apply-patches: $TREE is not a git tree" >&2; exit 1; }
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "apply-patches: $TREE has uncommitted changes; commit or stash them first:" >&2
	git status --short --untracked-files=no >&2
	exit 1
fi
if [ -d "$(git rev-parse --git-path rebase-apply)" ] || [ -d "$(git rev-parse --git-path rebase-merge)" ]; then
	echo "apply-patches: $TREE has a rebase or am in progress" >&2
	exit 1
fi

# the branch: existing, or new from where the tree is (upstream's master, normally)
current="$(git branch --show-current)"
if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
	base="$BRANCH"
	[ -n "$DRY" ] || [ "$current" = "$BRANCH" ] || git checkout -q "$BRANCH"
else
	base=HEAD
	echo "apply-patches: branch $BRANCH from $current ($(git rev-parse --short HEAD))"
	[ -n "$DRY" ] || git checkout -q -b "$BRANCH"
fi

# what is applied already: the Prose-Patch records in the branch's commits
recorded="$(git log -n 500 --format=%B "$base" | grep '^Prose-Patch: ' || true)"

if [ -n "$DRY" ]; then
	# a scratch index holding the branch's tree: each patch is applied to it,
	# so the next one is checked against the right state
	GIT_INDEX_FILE="$(mktemp -t apply-patches)"
	export GIT_INDEX_FILE
	trap 'rm -f "$GIT_INDEX_FILE"' EXIT
	git read-tree "$base"
fi

applied=0; present=0
for p in "$PATCHES"/[0-9][0-9][0-9][0-9]-*.patch; do
	name="$(basename "$p")"
	sum="$("$HASH" "$p")"
	record="$(printf '%s\n' "$recorded" | grep " $name " || true)"
	if [ -n "$record" ]; then
		case "$record" in
			*" $sum") present=$((present + 1)); continue ;;
		esac
		echo "apply-patches: $name changed in main after it was applied to $TREE" >&2
		echo "  applied: $record" >&2
		echo "  now:     Prose-Patch: $name $sum" >&2
		echo "  refresh the patch from the commit (scripts/export-patch.sh), or rebuild the branch:" >&2
		echo "  git checkout master && git branch -D $BRANCH, then run this again" >&2
		exit 1
	fi
	cached=""; [ -n "$DRY" ] && cached="--cached"
	if ! git apply $cached --check "$p" 2>/dev/null; then
		if git apply $cached --reverse --check "$p" 2>/dev/null; then
			# in the tree, but no commit records it (made by hand, exported
			# without export-patch.sh)
			echo "  present $name (not recorded in a commit)"
			present=$((present + 1))
			continue
		fi
		echo "apply-patches: $name does not apply to $TREE ($base) and is not recorded as applied:" >&2
		git apply $cached --check "$p" 2>&1 | head -5 >&2
		exit 1
	fi
	echo "  apply  $name"
	if [ -n "$DRY" ]; then
		git apply --cached "$p"
		applied=$((applied + 1))
		continue
	fi
	if head -1 "$p" | grep -q '^From [0-9a-f]\{40\} Mon Sep 17 00:00:00 2001'; then
		# git format-patch output: author, date and message come with it,
		# and the record too if export-patch.sh made the file
		git am -q "$p" || { git am --abort; exit 1; }
		git log -1 --format=%B | grep -qx "Prose-Patch: $name $sum" \
			|| git commit -q --amend --no-edit --trailer "Prose-Patch: $name $sum"
	else
		# a plain diff (0001-0008): the file name is the subject
		subject="$(echo "$name" | sed 's/^[0-9]*-//; s/\.patch$//; s/-/ /g')"
		git apply --index "$p"
		git commit -q -m "$subject" -m "patches/haiku/$name from HaikuArmQemu, applied by scripts/apply-patches.sh" \
			--trailer "Prose-Patch: $name $sum"
	fi
	applied=$((applied + 1))
done

if [ -n "$DRY" ]; then
	echo "apply-patches: $TREE ($BRANCH): $applied to apply, $present applied already; tree $(git write-tree | cut -c1-7) after; dry run, nothing done"
else
	echo "apply-patches: $TREE ($BRANCH, $(git rev-parse --short HEAD), tree $(git rev-parse 'HEAD^{tree}' | cut -c1-7)): $applied applied, $present applied already"
fi
