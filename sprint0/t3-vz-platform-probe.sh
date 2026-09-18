#!/bin/bash
# Sprint 0 / T3: measure the platform Apple's Virtualization.framework presents
# to a non-macOS arm64 guest, and compare it with the macOS 26.5.1 baseline.
# Re-run after every macOS update, and first thing after upgrading to macOS 27.
#
# Boots the EDK2 UEFI Shell (extracted from QEMU's firmware) in a headless VZ
# generic-platform VM three times; each pass dumps data the next pass needs:
#   pass1: firmware, memory map, UEFI system table, PCI list, GOP, drivers
#   pass2: raw ACPI tables, UEFI configuration tables, PCI config spaces
#   pass3: virtio-gpu feature/config registers
# Output: work/t3/report.txt and work/t3/facts.json. Exit 1 if anything differs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${WORK:-$ROOT/work}/t3"
FW="${EDK2_FW:-/opt/homebrew/share/qemu/edk2-aarch64-code.fd}"
ESP="$ROOT/tools/uefi/mkesp.sh"
DEC=(python3 "$ROOT/tools/uefi/probe_decode.py")
VZPROBE="$ROOT/build/bin/vzprobe"

"$ROOT/tools/build.sh"
rm -rf "$W"
mkdir -p "$W"
python3 "$ROOT/tools/uefi/extract_shell.py" "$FW" "$W/Shell.efi" >/dev/null
dd if=/dev/zero of="$W/nvme.img" bs=1m count=16 2>/dev/null

probe() { # pass-name [extra vzprobe args...]
	local name=$1
	shift
	"$VZPROBE" "$W/esp.img" "$W/efivars" "$W/serial.log" --nvme "$W/nvme.img" --timeout 120 "$@" \
		> "$W/$name.vzprobe.log" 2>&1 || true
	"$ESP" collect "$W/esp.img" "$W/$name"
}

echo "T3 pass 1: firmware, memory map, PCI, GOP"
cat > "$W/pass1.nsh" <<'EOF'
@echo -off
fs0:
ver >a fs0:\out\ver.txt
dmem >a fs0:\out\dmem.txt
memmap >a fs0:\out\memmap.txt
pci >a fs0:\out\pci.txt
dh -v -p GraphicsOutput >a fs0:\out\gop.txt
devtree >a fs0:\out\devtree.txt
drivers >a fs0:\out\drivers.txt
smbiosview -t 1 >a fs0:\out\smbios.txt
reset -s
EOF
"$ESP" create "$W/esp.img" "$W/Shell.efi" "$W/pass1.nsh"
probe pass1 --list-private
[ -s "$W/pass1/pci.txt" ] || { echo "T3 FAIL: pass 1 produced no output (see $W/pass1.vzprobe.log)"; exit 1; }

echo "T3 pass 2: ACPI tables, configuration tables, PCI config spaces"
read -r ACPI_START ACPI_SIZE < <("${DEC[@]}" memmap-acpi "$W/pass1/memmap.txt")
read -r CT_ADDR CT_SIZE < <("${DEC[@]}" cfgtab "$W/pass1/dmem.txt")
DEVS=$("${DEC[@]}" pci-devs "$W/pass1/pci.txt")
{
	echo '@echo -off'
	echo 'fs0:'
	echo "dmem $ACPI_START $ACPI_SIZE >a fs0:\\out\\acpi.txt"
	echo "dmem $CT_ADDR $CT_SIZE >a fs0:\\out\\cfgtab.txt"
	echo "for %d in $DEVS"
	echo '  pci 0 %d 0 -i >>a fs0:\out\pcidetail.txt'
	echo 'endfor'
	echo 'reset -s'
} > "$W/pass2.nsh"
"$ESP" script "$W/esp.img" "$W/pass2.nsh"
probe pass2

echo "T3 pass 3: virtio-gpu registers"
read -r GPU_COMMON GPU_DEVCFG < <("${DEC[@]}" gpu-regs "$W/pass2/pcidetail.txt")
hx() { printf '%X' $((16#$1 + $2)); }
{
	echo '@echo -off'
	echo 'fs0:'
	echo "mm $(hx "$GPU_COMMON" 0) 0 -w 4 -MMIO -n"
	echo "mm $(hx "$GPU_COMMON" 4) -w 4 -MMIO -n >a fs0:\\out\\feat0.txt"
	echo "mm $(hx "$GPU_COMMON" 0) 1 -w 4 -MMIO -n"
	echo "mm $(hx "$GPU_COMMON" 4) -w 4 -MMIO -n >a fs0:\\out\\feat1.txt"
	echo "mm $(hx "$GPU_DEVCFG" 8) -w 4 -MMIO -n >a fs0:\\out\\scanouts.txt"
	echo "mm $(hx "$GPU_DEVCFG" 12) -w 4 -MMIO -n >a fs0:\\out\\capsets.txt"
	echo 'reset -s'
} > "$W/pass3.nsh"
"$ESP" script "$W/esp.img" "$W/pass3.nsh"
probe pass3

echo "T3 pass 4: restricted (private) devices"
"$ESP" script "$W/esp.img" "$W/pass3.nsh"
restricted=""
for dev in "--pl011" "--linear-fb 1280x800 --no-virtio-gpu"; do
	# shellcheck disable=SC2086
	"$VZPROBE" "$W/esp.img" "$W/efivars" "$W/serial.log" --timeout 30 $dev > "$W/private.log" 2>&1 || true
	if grep -q "start failed" "$W/private.log"; then
		restricted+="  PASS private device $dev: VM start refused (restricted, as baseline)"$'\n'
	else
		restricted+="  DIFF private device $dev: VM started (baseline: refused)"$'\n'
	fi
done

status=0
{
	"${DEC[@]}" report "$W" --baseline || status=1
	echo
	echo "Host checks:"
	grep -h -E "nested virtualization supported|custom virtio API|VZMacGraphicsDevice" "$W/pass1.vzprobe.log" | sed 's/^/  /'
	printf '%s' "$restricted"
	if grep -q "private device.*DIFF" <<<"$restricted"; then status=1; fi
} | tee "$W/report.txt"
grep -q "DIFF" "$W/report.txt" && status=1
echo
[ $status -eq 0 ] && echo "T3 PASS: platform matches the macOS 26.5.1 baseline" \
	|| echo "T3 DIFF: platform differs from the macOS 26.5.1 baseline (see $W/report.txt)"
exit $status
