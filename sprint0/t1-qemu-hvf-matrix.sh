#!/bin/bash
# Sprint 0 / T1: what QEMU + HVF accepts on this host, and what the firmware
# hands the OS.
#   Part A: interrupt-controller / MSI / EL2 options. Each configuration boots
#           the bundled EDK2 firmware for a few seconds; "boots" means the UEFI
#           banner appeared on serial, "rejects" means QEMU refused to start.
#   Part B: the boot framebuffer (UEFI GOP) each display device provides, and
#           whether the OS gets ACPI or a device tree, probed with the UEFI Shell.
# Output: work/t1/report.txt. Exit 1 if any result differs from expectations.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${WORK:-$ROOT/work}/t1"
QEMU="${QEMU:-qemu-system-aarch64}"
FW="${EDK2_FW:-/opt/homebrew/share/qemu/edk2-aarch64-code.fd}"
ESP="$ROOT/tools/uefi/mkesp.sh"
DEC=(python3 "$ROOT/tools/uefi/probe_decode.py")
TIMEOUT=$(command -v gtimeout || command -v timeout) || {
	echo "T1 needs coreutils timeout (brew install coreutils)"
	exit 1
}
rm -rf "$W"
mkdir -p "$W"
status=0
REPORT="$W/report.txt"
: > "$REPORT"
say() { echo "$*" | tee -a "$REPORT"; }

say "T1 host: $(sysctl -n machdep.cpu.brand_string), macOS $(sw_vers -productVersion), $("$QEMU" --version | head -1)"
say "Accelerators: $("$QEMU" -accel help | tail -n +2 | tr '\n' ' ')"

# --- Part A ---------------------------------------------------------------------
say ""
say "Part A: interrupt controller / MSI / EL2 options under HVF"
check() { # label accel machine expect [message]
	local label=$1 accel=$2 machine=$3 expect=$4 msg=${5:-} got rc
	rm -f "$W/a.log"
	set +e
	"$TIMEOUT" 8 "$QEMU" -M "$machine" -accel "$accel" -cpu host -smp 2 -m 2G -bios "$FW" \
		-nic none -display none -monitor none -serial "file:$W/a.log" > "$W/a.err" 2>&1
	rc=$?
	set -e
	if grep -q "UEFI firmware" "$W/a.log" 2>/dev/null; then
		got=boots
	elif [ $rc -ne 124 ]; then
		got=rejects
	else
		got=hangs
	fi
	local detail
	detail=$(grep -v "terminating on signal" "$W/a.err" | head -1 | sed 's/^qemu-system-aarch64: //' || true)
	if [ "$got" = "$expect" ] && { [ -z "$msg" ] || grep -q "$msg" "$W/a.err"; }; then
		say "  PASS $label: $got${detail:+ ($detail)}"
	else
		say "  DIFF $label: $got, expected $expect${detail:+ ($detail)}"
		status=1
	fi
}
check "default virt"                   hvf                   virt                               boots
check "gic-version=3"                  hvf                   virt,gic-version=3                 boots
check "gic-version=2"                  hvf                   virt,gic-version=2                 rejects "does not support GICv2"
check "gic-version=4"                  hvf                   virt,gic-version=4                 rejects "does not support GICv4"
check "its=on"                         hvf                   virt,gic-version=3,its=on          rejects "ITS not supported"
check "msi=gicv2m"                     hvf                   virt,gic-version=3,msi=gicv2m      boots
check "virtualization=on (EL2)"        hvf                   virt,gic-version=3,virtualization=on boots
check "kernel-irqchip=off gicv2"       hvf,kernel-irqchip=off virt,gic-version=2                boots
check "kernel-irqchip=off gicv3+its"   hvf,kernel-irqchip=off virt,gic-version=3,its=on         boots

# --- Part B ---------------------------------------------------------------------
say ""
say "Part B: boot framebuffer (GOP) and hardware description handed to the OS"
python3 "$ROOT/tools/uefi/extract_shell.py" "$FW" "$W/Shell.efi" >/dev/null
cat > "$W/gop.nsh" <<'EOF'
@echo -off
fs0:
dh -v -p GraphicsOutput >a fs0:\out\gop.txt
dmem >a fs0:\out\dmem.txt
reset -s
EOF
"$ESP" create "$W/esp.img" "$W/Shell.efi" "$W/gop.nsh"

boot_probe() { # machine device-args...
	local machine=$1
	shift
	"$TIMEOUT" 60 "$QEMU" -M "$machine" -accel hvf -cpu host -smp 2 -m 2G -bios "$FW" -nic none \
		-display none -monitor none -serial none \
		-drive if=none,id=esp,format=raw,file="$W/esp.img" -device virtio-blk-pci,drive=esp "$@" \
		> "$W/b.err" 2>&1 || true
}

gop() { # label expect(linear|bltonly|none) device-args...
	local label=$1 expect=$2 got fmt base
	shift 2
	"$ESP" script "$W/esp.img" "$W/gop.nsh"
	boot_probe virt "$@"
	rm -rf "$W/b"
	"$ESP" collect "$W/esp.img" "$W/b"
	fmt=$(grep -m1 -oE 'Pixel Format\.*: *[A-Za-z0-9]+' "$W/b/gop.txt" 2>/dev/null | awk '{print $NF}' || true)
	base=$(grep -m1 -oE 'Frame Buffer Base\.*: *0x[0-9A-Fa-f]+' "$W/b/gop.txt" 2>/dev/null | awk '{print $NF}' || true)
	if [ -z "$fmt" ]; then got=none
	elif [ "$fmt" = PixelBltOnly ]; then got=bltonly
	else got=linear; fi
	if [ "$got" = "$expect" ]; then
		say "  PASS $label: $got${fmt:+ ($fmt, base $base)}"
	else
		say "  DIFF $label: $got, expected $expect${fmt:+ ($fmt, base $base)}"
		status=1
	fi
}
gop "ramfb"             linear  -device ramfb
gop "virtio-gpu-pci"    bltonly -device virtio-gpu-pci
gop "virtio-gpu-device" none    -device virtio-gpu-device   # measured: no GOP over virtio-mmio
gop "bochs-display"     none    -device bochs-display

describe() { # label machine expect(ACPI20|FDT)
	local label=$1 machine=$2 expect=$3 addr size got
	"$ESP" script "$W/esp.img" "$W/gop.nsh"
	boot_probe "$machine" -device ramfb
	rm -rf "$W/d1"
	"$ESP" collect "$W/esp.img" "$W/d1"
	read -r addr size < <("${DEC[@]}" cfgtab "$W/d1/dmem.txt")
	printf '@echo -off\nfs0:\ndmem %s %s >a fs0:\\out\\cfgtab.txt\nreset -s\n' "$addr" "$size" > "$W/cfg.nsh"
	"$ESP" script "$W/esp.img" "$W/cfg.nsh"
	boot_probe "$machine" -device ramfb
	rm -rf "$W/d2"
	"$ESP" collect "$W/esp.img" "$W/d2"
	got=$("${DEC[@]}" cfgtab-guids "$W/d2/cfgtab.txt" | grep -E '^(ACPI20|FDT)$' | tr '\n' ' ' | sed 's/ $//' || true)
	if [ "$got" = "$expect" ]; then
		say "  PASS $label: OS gets $got"
	else
		say "  DIFF $label: OS gets '${got:-nothing}', expected $expect"
		status=1
	fi
}
describe "-M virt (default)" virt ACPI20
describe "-M virt,acpi=off"  virt,acpi=off FDT

say ""
[ $status -eq 0 ] && say "T1 PASS" || say "T1 DIFF (see $REPORT)"
exit $status
