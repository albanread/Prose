#!/usr/bin/env python3
"""Extract the EDK2 UEFI Shell (PE32 image) from a QEMU ArmVirt firmware volume.

Walks every LZMA-compressed GUID-defined section in the flash image, decompresses
it, and looks for the FFS file whose name is the Shell application GUID.
"""
import lzma
import sys
import uuid

LZMA_GUID = uuid.UUID("EE4E5898-3914-4259-9D6E-DC7BD79403CF").bytes_le
SHELL_GUID = uuid.UUID("7C04A583-9E3E-4F1C-AD65-E05268D0B4D1").bytes_le

SEC_GUID_DEFINED = 0x02
SEC_PE32 = 0x10
FV_FILETYPE_APPLICATION = 0x09


def u24(b, o):
    return int.from_bytes(b[o:o + 3], "little")


def lzma_sections(buf):
    """Yield decompressed payloads of all LZMA GUID-defined sections in buf."""
    pos = buf.find(LZMA_GUID)
    while pos != -1:
        # Common header (4 bytes) precedes the GUID; extended header is 8 bytes.
        for hdr in (4, 8):
            start = pos - hdr
            if start < 0 or buf[start + 3] != SEC_GUID_DEFINED:
                continue
            size = u24(buf, start)
            if hdr == 8:
                if size != 0xFFFFFF:
                    continue
                size = int.from_bytes(buf[start + 4:start + 8], "little")
            data_off = int.from_bytes(buf[pos + 16:pos + 18], "little")
            payload = buf[start + data_off:start + size]
            try:
                yield lzma.decompress(payload, format=lzma.FORMAT_ALONE)
            except lzma.LZMAError:
                pass
        pos = buf.find(LZMA_GUID, pos + 1)


def pe32_from_ffs(buf, off):
    """Return the PE32 section of the FFS file at off, or None."""
    ftype, attrs = buf[off + 18], buf[off + 19]
    if ftype != FV_FILETYPE_APPLICATION:
        return None
    size, hdr = u24(buf, off + 20), 24
    if attrs & 0x01:  # FFS_ATTRIB_LARGE_FILE
        size, hdr = int.from_bytes(buf[off + 24:off + 32], "little"), 32
    p, end = off + hdr, off + size
    while p + 4 <= end:
        ssize, stype = u24(buf, p), buf[p + 3]
        if ssize < 4:
            break
        if stype == SEC_PE32:
            return buf[p + 4:p + ssize]
        p = (p + ssize + 3) & ~3
    return None


def search(buf, depth=0):
    idx = buf.find(SHELL_GUID)
    while idx != -1:
        pe = pe32_from_ffs(buf, idx)
        if pe and pe[:2] == b"MZ":
            return pe
        idx = buf.find(SHELL_GUID, idx + 1)
    if depth < 3:
        for inner in lzma_sections(buf):
            pe = search(inner, depth + 1)
            if pe:
                return pe
    return None


def main():
    fw, out = sys.argv[1], sys.argv[2]
    with open(fw, "rb") as f:
        data = f.read()
    pe = search(data)
    if not pe:
        sys.exit("Shell PE32 image not found")
    with open(out, "wb") as f:
        f.write(pe)
    machine = int.from_bytes(pe[pe.find(b"PE\0\0") + 4:][:2], "little")
    print(f"wrote {out}: {len(pe)} bytes, PE machine 0x{machine:04x} (0xaa64 = AArch64)")


if __name__ == "__main__":
    main()
