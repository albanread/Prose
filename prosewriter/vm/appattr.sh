#!/bin/bash
# Stamp a deployed app with its signature attributes (in the guest, via
# the agent). A binary without BEOS:APP_SIG launches through the roster's
# fallback paths — on this image that dragged FileCropper up alongside
# ProseWriter (2026-09-20). The eventual package does this with an rdef;
# until then, every deploy ends with this.
#
# Usage: vm/appattr.sh [binary] [signature]
set -euo pipefail
VM="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-/boot/home/apps/ProseWriter}"
SIG="${2:-application/x-vnd.prose.ProseWriter}"

"$VM/guest.sh" run "addattr -t mime BEOS:APP_SIG $SIG $BIN; \
	addattr -t mime BEOS:TYPE $SIG $BIN; \
	catattr BEOS:APP_SIG $BIN" | tail -1
