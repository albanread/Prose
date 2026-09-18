#!/bin/bash
# Manage small GPT/FAT32 disk images that boot the EDK2 UEFI Shell as
# \EFI\BOOT\BOOTAA64.EFI and run a startup.nsh probe script. The script writes
# its results as ASCII files into \out on the same volume.
#
#   mkesp.sh create  <image> <Shell.efi> <startup.nsh>   new 64 MiB image
#   mkesp.sh script  <image> <startup.nsh>               replace the script, clear \out
#   mkesp.sh collect <image> <dest-dir>                  copy \out/*.txt (CR stripped)
set -euo pipefail

VOLNAME=S0PROBE

attach() { # image -> prints "device<TAB>mountpoint"
	local out dev mp
	out=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage "$1")
	dev=$(echo "$out" | head -1 | awk '{print $1}')
	mp=$(echo "$out" | awk -F'\t' '/\/Volumes\//{gsub(/^[ \t]+|[ \t]+$/, "", $NF); print $NF}' | head -1)
	printf '%s\t%s\n' "$dev" "$mp"
}

detach() {
	hdiutil detach "$1" >/dev/null 2>&1 || hdiutil detach -force "$1" >/dev/null
}

install_script() { # mountpoint startup.nsh
	cp -X "$2" "$1/startup.nsh"
	cp -X "$2" "$1/EFI/BOOT/startup.nsh"
	rm -f "$1"/out/*.txt
}

cmd=${1:-}
case "$cmd" in
create)
	image=$2 shell=$3 script=$4
	rm -f "$image"
	dd if=/dev/zero of="$image" bs=1m count=64 2>/dev/null
	dev=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$image" | head -1 | awk '{print $1}')
	diskutil eraseDisk "MS-DOS FAT32" "$VOLNAME" GPT "$dev" >/dev/null
	mp=$(diskutil info "${dev}s1" | awk -F': *' '/Mount Point/{print $2}')
	mdutil -i off "$mp" >/dev/null 2>&1 || true
	mkdir -p "$mp/EFI/BOOT" "$mp/out"
	cp -X "$shell" "$mp/EFI/BOOT/BOOTAA64.EFI"
	install_script "$mp" "$script"
	detach "$dev"
	;;
script)
	IFS=$'\t' read -r dev mp < <(attach "$2")
	install_script "$mp" "$3"
	detach "$dev"
	;;
collect)
	IFS=$'\t' read -r dev mp < <(attach "$2")
	mkdir -p "$3"
	for f in "$mp"/out/*.txt; do
		[ -e "$f" ] || continue
		tr -d '\r' < "$f" > "$3/$(basename "$f")"
	done
	detach "$dev"
	;;
*)
	echo "usage: $0 create <image> <Shell.efi> <startup.nsh> | script <image> <startup.nsh> | collect <image> <dest-dir>" >&2
	exit 64
	;;
esac
