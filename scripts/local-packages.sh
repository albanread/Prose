#!/bin/sh
# The fork's own packages for a Haiku tree. The packages its HaikuPorts list
# names beyond upstream's (the image codecs and the translators' libraries,
# patches 0033, 0037, 0040) are built by prosepkg; the package server has
# none of them. This copies them into the tree's generated/download/ with the
# repository's vendor and turns downloads off (HAIKU_NO_DOWNLOADS in the
# tree's BuildConfig). Packages already there and unchanged are left alone,
# and a tree whose list is upstream's is not touched at all.
#
# Usage: local-packages.sh [--dry-run] [haiku tree]   (default /Volumes/HaikuSrc/haiku)
exec "$(dirname "$0")/prosepkg" local-packages "$@"
