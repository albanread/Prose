#!/usr/bin/env python3
"""Decode EDK2 UEFI Shell dumps captured by the platform probes.

Subcommands (all read ASCII files written by the Shell with `>a`):
  memmap-acpi <memmap.txt>    print "START SIZE" (hex) spanning the ACPI_Recl ranges
  cfgtab <dmem.txt>           print "ADDR SIZE" (hex) of the configuration table array
  pci-devs <pci.txt>          print the PCI device numbers (hex) on bus 0
  gpu-regs <pcidetail.txt>    print "COMMON DEVICE" register addresses of virtio-gpu
  cfgtab-guids <dump.txt>     print the UEFI configuration table GUIDs (ACPI20, FDT, ...)
  report <dir> [--baseline]   decode pass1/ pass2/ pass3/ under <dir>, write facts.json,
                              and with --baseline compare against the macOS 26.5.1 baseline
                              (exit 1 on any difference)
"""
import json
import re
import struct
import sys
import uuid
from pathlib import Path

HEXLINE = re.compile(r"^\s*([0-9A-F]{8,16}):\s+((?:[0-9A-F]{2}[ -]){1,16})", re.I | re.M)

KNOWN_GUIDS = {
    "8868e871-e4f1-11d3-bc22-0080c73c8881": "ACPI20",
    "eb9d2d30-2d88-11d3-9a16-0090273fc14d": "ACPI10",
    "b1b621d5-f19c-41a5-830b-d9152c69aae0": "FDT",
    "eb9d2d31-2d88-11d3-9a16-0090273fc14d": "SMBIOS",
    "f2fd1544-9794-4a2c-992e-e5bbcf20e394": "SMBIOS3",
}

# What VZ presented on macOS 26.5.1 (Mac Studio M4 Max), 2026-09-18.
BASELINE = {
    "firmware_vendor": "EDK II",
    "acpi": True,
    "fdt": False,
    "fadt_hw_reduced": True,
    "psci": True,
    "psci_hvc": True,
    "gic_version": 3,
    "gicv2m_frames": 1,
    "its_count": 0,
    "virtual_timer_gsiv": 27,
    "tables_missing_spcr": True,
    "tables_missing_dbg2": True,
    "ecam_segments": 1,
    "gop_pixel_format": "PixelBltOnly",
    "gop_framebuffer_base": 0,
    "virtio_all_modern": True,
    "virtio_all_msix": True,
    "virtio_gpu_device_features": 0,
    "virtio_gpu_num_scanouts": 1,
    "virtio_gpu_num_capsets": 0,
}


def hexbytes(text):
    """Parse a dmem/pci hex dump into {address: byte}."""
    mem = {}
    for addr, hx in HEXLINE.findall(text):
        base = int(addr, 16)
        for i, b in enumerate(re.findall(r"[0-9A-F]{2}", hx, re.I)[:16]):
            mem[base + i] = int(b, 16)
    return mem


def contiguous(mem):
    base = min(mem)
    buf = bytearray(max(mem) - base + 1)
    for a, v in mem.items():
        buf[a - base] = v
    return base, bytes(buf)


# --- small helpers used by the probe scripts -----------------------------------

def memmap_acpi(path):
    ranges = [(int(s, 16), int(e, 16)) for s, e in
              re.findall(r"^ACPI_Recl\s+([0-9A-F]+)-([0-9A-F]+)", Path(path).read_text(), re.M)]
    if not ranges:
        sys.exit("no ACPI_Recl range in memory map")
    start, end = min(r[0] for r in ranges), max(r[1] for r in ranges)
    print(f"{start:X} {end - start + 1:X}")


def cfgtab(path):
    base, st = contiguous(hexbytes(Path(path).read_text()))
    count = int.from_bytes(st[0x68:0x70], "little")
    ptr = int.from_bytes(st[0x70:0x78], "little")
    print(f"{ptr:X} {count * 24:X}")


def pci_devs(path):
    devs = re.findall(r"^\s+[0-9A-F]{2}\s+00\s+([0-9A-F]{2})\s+00 ==>", Path(path).read_text(), re.M)
    print(" ".join(d.lstrip("0") or "0" for d in devs))


def pci_blocks(text):
    """Yield (dev, cfg[256]) for each `pci B D F -i` block."""
    for block in re.split(r"(?=\s*PCI Segment)", text):
        m = re.search(r"Bus\s+([0-9A-F]+)\s+Device\s+([0-9A-F]+)\s+Func\s+([0-9A-F]+)", block, re.I)
        if not m:
            continue
        cfg = bytearray(256)
        for addr, hx in HEXLINE.findall(block):
            off = int(addr, 16)
            for i, b in enumerate(re.findall(r"[0-9A-F]{2}", hx, re.I)[:16]):
                if off + i < 256:
                    cfg[off + i] = int(b, 16)
        yield int(m.group(2), 16), bytes(cfg)


def bars(cfg):
    out, i = {}, 0
    while i < 6:
        lo = int.from_bytes(cfg[0x10 + 4 * i:0x14 + 4 * i], "little")
        if lo & 1:
            i += 1
            continue
        if (lo >> 1) & 3 == 2:
            hi = int.from_bytes(cfg[0x14 + 4 * i:0x18 + 4 * i], "little")
            out[i] = (hi << 32) | (lo & ~0xF)
            i += 2
        else:
            out[i] = lo & ~0xF
            i += 1
    return out


def caps(cfg):
    """Return [(cap_id, offset)] following the capability list."""
    out, p, seen = [], cfg[0x34], set()
    while p and p not in seen and p < 0xFF:
        seen.add(p)
        out.append((cfg[p], p))
        p = cfg[p + 1]
    return out


def virtio_cap_addr(cfg, cfg_type):
    b = bars(cfg)
    for cid, p in caps(cfg):
        if cid == 0x09 and cfg[p + 3] == cfg_type:
            return b.get(cfg[p + 4], 0) + int.from_bytes(cfg[p + 8:p + 12], "little")
    return None


def gpu_regs(path):
    for dev, cfg in pci_blocks(Path(path).read_text()):
        if cfg[0:4] == bytes.fromhex("f4 1a 50 10"):
            print(f"{virtio_cap_addr(cfg, 1):X} {virtio_cap_addr(cfg, 4):X}")
            return
    sys.exit("virtio-gpu (1af4:1050) not found")


# --- ACPI ------------------------------------------------------------------------

class Mem:
    def __init__(self, text):
        self.base, self.buf = contiguous(hexbytes(text))

    def rd(self, addr, n):
        off = addr - self.base
        if off < 0 or off + n > len(self.buf):
            raise ValueError(f"{addr:#x} outside dump")
        return self.buf[off:off + n]

    def u(self, addr, n):
        return int.from_bytes(self.rd(addr, n), "little")


def aml_hids(aml):
    """_HID values in an AML blob: strings (0x0D) and EISA IDs (DWord 0x0C)."""
    hids = set()
    for m in re.finditer(rb"_HID", aml):
        p = m.end()
        if p >= len(aml):
            continue
        if aml[p] == 0x0D:
            end = aml.find(b"\0", p + 1)
            hids.add(aml[p + 1:end].decode(errors="replace"))
        elif aml[p] == 0x0C:
            v = int.from_bytes(aml[p + 1:p + 5], "big")
            hids.add("".join(chr(((v >> s) & 0x1F) + 64) for s in (26, 21, 16)) + f"{v & 0xFFFF:04X}")
    return sorted(hids)


def cfgtab_guids(path):
    buf = contiguous(hexbytes(Path(path).read_text()))[1]
    for i in range(0, len(buf) - 23, 24):
        g = str(uuid.UUID(bytes_le=bytes(buf[i:i + 16])))
        print(KNOWN_GUIDS.get(g, g))


def decode_acpi(text, facts, lines):
    m = Mem(text)
    rsdp = m.base + m.buf.find(b"RSD PTR ")
    xsdt = m.u(rsdp + 24, 8)
    lines.append(f"RSDP @ {rsdp:#x} OEM={m.rd(rsdp + 9, 6).decode()!r} rev={m.u(rsdp + 15, 1)} XSDT={xsdt:#x}")
    length = m.u(xsdt + 4, 4)
    tables = {}
    for i in range((length - 36) // 8):
        t = m.u(xsdt + 36 + 8 * i, 8)
        tables[m.rd(t, 4).decode()] = t
    facts["acpi_tables"] = sorted(tables)
    lines.append("XSDT tables: " + " ".join(sorted(tables)))
    facts["tables_missing_spcr"] = "SPCR" not in tables
    facts["tables_missing_dbg2"] = "DBG2" not in tables

    if "FACP" in tables:
        f = tables["FACP"]
        flags, arm = m.u(f + 112, 4), m.u(f + 129, 2)
        facts["fadt_hw_reduced"] = bool(flags & (1 << 20))
        facts["psci"] = bool(arm & 1)
        facts["psci_hvc"] = bool(arm & 2)
        lines.append(f"FADT rev {m.u(f + 8, 1)}.{m.u(f + 131, 1)} HW_REDUCED={facts['fadt_hw_reduced']} "
                     f"PSCI={facts['psci']} HVC={facts['psci_hvc']}")
        dsdt = m.u(f + 140, 8) or m.u(f + 40, 4)
        hids = aml_hids(m.rd(dsdt, m.u(dsdt + 4, 4)))
        facts["dsdt_hids"] = hids
        lines.append("DSDT _HIDs: " + ", ".join(hids))

    if "APIC" in tables:
        a = tables["APIC"]
        end, p = a + m.u(a + 4, 4), a + 44
        gicd_ver, v2m, its, gicc = None, 0, 0, 0
        while p < end:
            t, ln = m.u(p, 1), m.u(p + 1, 1)
            if ln == 0:
                break
            if t == 0x0B:
                gicc += 1
            elif t == 0x0C:
                gicd_ver = m.u(p + 20, 1)
                lines.append(f"GICD base={m.u(p + 8, 8):#x} version={gicd_ver}")
            elif t == 0x0D:
                v2m += 1
                lines.append(f"GICv2m frame base={m.u(p + 8, 8):#x} spis={m.u(p + 20, 2)} base_spi={m.u(p + 22, 2)}")
            elif t == 0x0E:
                lines.append(f"GICR base={m.u(p + 4, 8):#x} length={m.u(p + 12, 4):#x}")
            elif t == 0x0F:
                its += 1
                lines.append(f"GIC ITS base={m.u(p + 8, 8):#x}")
            p += ln
        facts.update(gic_version=gicd_ver, gicv2m_frames=v2m, its_count=its, cpus=gicc)
        lines.append(f"MADT: {gicc} CPU interfaces")

    if "GTDT" in tables:
        g = tables["GTDT"]
        names = [("secure_el1", 48), ("ns_el1_physical", 56), ("virtual", 64), ("ns_el2_physical", 72)]
        timers = {n: m.u(g + off, 4) for n, off in names}
        facts["virtual_timer_gsiv"] = timers["virtual"]
        lines.append("GTDT GSIVs: " + ", ".join(f"{k}={v}" for k, v in timers.items()))

    if "MCFG" in tables:
        c = tables["MCFG"]
        segs = []
        p = c + 44
        while p + 16 <= c + m.u(c + 4, 4):
            base, seg, sb, eb = struct.unpack("<QHBB", m.rd(p, 12))
            segs.append(f"seg{seg} base={base:#x} buses {sb}-{eb}")
            p += 16
        facts["ecam_segments"] = len(segs)
        lines.append("MCFG: " + "; ".join(segs))


def decode_cfgtab(text, facts, lines):
    buf = contiguous(hexbytes(text))[1]
    names = []
    for i in range(0, len(buf) - 23, 24):
        g = str(uuid.UUID(bytes_le=bytes(buf[i:i + 16])))
        names.append(KNOWN_GUIDS.get(g, g))
    facts["acpi"] = "ACPI20" in names
    facts["fdt"] = "FDT" in names
    lines.append("UEFI config tables: " + ", ".join(names))


def decode_pci(text, facts, lines):
    modern, msix = True, True
    for dev, cfg in pci_blocks(text):
        vid, did = int.from_bytes(cfg[0:2], "little"), int.from_bytes(cfg[2:4], "little")
        if vid in (0, 0xFFFF):
            continue
        cl = [c for c, _ in caps(cfg)]
        if vid == 0x1AF4:
            modern &= did >= 0x1040
            msix &= 0x11 in cl
        kinds = {0x05: "MSI", 0x09: "vendor", 0x10: "PCIe", 0x11: "MSI-X"}
        lines.append(f"PCI 00:{dev:02x}.0 {vid:04x}:{did:04x} INTx pin={cfg[0x3D]} caps="
                     + ",".join(kinds.get(c, hex(c)) for c in cl))
    facts["virtio_all_modern"] = modern
    facts["virtio_all_msix"] = msix


def mm_value(path):
    vals = re.findall(r"0x([0-9A-F]+)", Path(path).read_text(), re.I)
    return int(vals[-1], 16) if vals else None


def report(d, baseline):
    d = Path(d)
    facts, lines = {}, []
    ver = (d / "pass1/ver.txt").read_text()
    m = re.search(r"UEFI v[\d.]+ \(([^,]+),", ver)
    facts["firmware_vendor"] = m.group(1) if m else None
    lines.append("firmware: " + ver.strip().splitlines()[-1])

    gop = (d / "pass1/gop.txt").read_text()
    pf = re.search(r"Pixel Format\.*:\s*(\S+)", gop)
    fb = re.search(r"Frame Buffer Base\.*:\s*0x([0-9A-F]+)", gop, re.I)
    facts["gop_pixel_format"] = pf.group(1) if pf else None
    facts["gop_framebuffer_base"] = int(fb.group(1), 16) if fb else None
    lines.append(f"GOP: {facts['gop_pixel_format']} framebuffer={facts['gop_framebuffer_base']:#x}"
                 if fb else "GOP: none")

    decode_cfgtab((d / "pass2/cfgtab.txt").read_text(), facts, lines)
    decode_acpi((d / "pass2/acpi.txt").read_text(), facts, lines)
    decode_pci((d / "pass2/pcidetail.txt").read_text(), facts, lines)

    feat = (mm_value(d / "pass3/feat1.txt") << 32) | mm_value(d / "pass3/feat0.txt")
    facts["virtio_gpu_features"] = feat
    facts["virtio_gpu_device_features"] = feat & 0xFFFFFF
    facts["virtio_gpu_num_scanouts"] = mm_value(d / "pass3/scanouts.txt")
    facts["virtio_gpu_num_capsets"] = mm_value(d / "pass3/capsets.txt")
    names = {0: "VIRGL", 1: "EDID", 2: "RESOURCE_UUID", 3: "RESOURCE_BLOB", 4: "CONTEXT_INIT",
             28: "INDIRECT_DESC", 29: "EVENT_IDX", 32: "VERSION_1", 33: "ACCESS_PLATFORM",
             34: "RING_PACKED", 35: "IN_ORDER"}
    lines.append("virtio-gpu features: " + " ".join(names.get(b, f"bit{b}") for b in range(64) if feat >> b & 1)
                 + f"; scanouts={facts['virtio_gpu_num_scanouts']} capsets={facts['virtio_gpu_num_capsets']}")

    (d / "facts.json").write_text(json.dumps(facts, indent=2, sort_keys=True) + "\n")
    print("\n".join(lines))
    if not baseline:
        return 0
    print("\nBaseline comparison (macOS 26.5.1):")
    diffs = 0
    for k, want in BASELINE.items():
        got = facts.get(k)
        ok = got == want
        diffs += not ok
        print(f"  {'PASS' if ok else 'DIFF'} {k}: {got!r}" + ("" if ok else f" (baseline {want!r})"))
    print(f"{len(BASELINE) - diffs}/{len(BASELINE)} match")
    return 1 if diffs else 0


def main():
    cmd, rest = sys.argv[1], sys.argv[2:]
    if cmd == "memmap-acpi":
        memmap_acpi(rest[0])
    elif cmd == "cfgtab":
        cfgtab(rest[0])
    elif cmd == "pci-devs":
        pci_devs(rest[0])
    elif cmd == "gpu-regs":
        gpu_regs(rest[0])
    elif cmd == "cfgtab-guids":
        cfgtab_guids(rest[0])
    elif cmd == "report":
        sys.exit(report(rest[0], "--baseline" in rest))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
