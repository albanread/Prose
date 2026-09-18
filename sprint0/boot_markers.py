#!/usr/bin/env python3
"""Summarize Haiku boot progress from a serial log or syslog, and check
QEMU screendumps (PPM) for content.

usage: boot_markers.py <log> [screendump.ppm ...]
Prints one line of key=value results; exit status 0 if the boot reached the
first-login script with no disk errors or panics.
"""
import sys
from pathlib import Path

# Milestones in boot order (BUILDING.md §6), plus failure signatures.
MILESTONES = [
    ("loader", "Welcome to the Haiku boot loader!"),
    ("kernel", "debug level:"),
    ("boot_volume", "Mounted boot partition"),
    ("framebuffer", "framebuffer_init() completed successfully"),
    ("virtio_gpu", "virtio_gpu"),
    ("first_login", "Running first login script"),
]
FAILURES = [
    ("io_errors", "I/O error"),
    ("panics", "PANIC"),
    ("kdl", "Welcome to Kernel Debugging Land"),
]


def ppm_content(path):
    """Number of distinct colours in a sample of a P6 PPM (0 if unreadable)."""
    try:
        data = Path(path).read_bytes()
        parts, pos = [], 0
        while len(parts) < 4:
            while data[pos:pos + 1].isspace():
                pos += 1
            if data[pos:pos + 1] == b"#":
                pos = data.index(b"\n", pos)
                continue
            end = pos
            while not data[end:end + 1].isspace():
                end += 1
            parts.append(data[pos:end])
            pos = end
        if parts[0] != b"P6":
            return 0
        w, h = int(parts[1]), int(parts[2])
        pixels = data[pos + 1:]
        step = max(1, (w * h) // 4096)
        return len({pixels[i * 3:i * 3 + 3] for i in range(0, w * h, step)})
    except (OSError, ValueError, IndexError):
        return 0


def main():
    log = Path(sys.argv[1]).read_bytes().decode("latin-1") if Path(sys.argv[1]).exists() else ""
    out = {}
    for key, needle in MILESTONES:
        out[key] = needle in log
    for key, needle in FAILURES:
        out[key] = log.count(needle)
    for shot in sys.argv[2:]:
        out[Path(shot).stem + "_colours"] = ppm_content(shot)
    print(" ".join(f"{k}={v}" for k, v in out.items()))
    ok = out["first_login"] and out["io_errors"] == 0 and out["panics"] == 0 and out["kdl"] == 0
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
