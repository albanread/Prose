#!/usr/bin/env python3
"""QMP helper for the prose-dev VM: input events, screendump to PNG, HMP passthrough.

Stdlib only. Talks to the QMP unix socket vm/qmp.sock that vm/run.sh opens.

Usage:
  qmp.py ready                 # exit 0 once the socket answers (after VM start)
  qmp.py shot out.png          # screendump the console, converted to PNG
  qmp.py key ctrl-alt-q        # press a qcode combo (see qmp.py keys)
  qmp.py keydown q / keyup q   # press or release a single qcode
  qmp.py move 400 300          # absolute tablet move (guest pixels)
  qmp.py click [left|right]    # button press+release at the last position
  qmp.py down [left|right] / up [left|right]
  qmp.py hmp 'info status'     # any HMP command through human-monitor-command
"""
import json
import socket
import sys
import time
import zlib
from pathlib import Path

SOCK = Path(__file__).resolve().parent / "qmp.sock"

# QMP qcode names (subset; QEMU accepts these spellings)
QCODES = {"ctrl", "shift", "alt", "altgr", "tab", "esc", "enter", "ret",
          "spc",
          "left", "right", "up", "down", "home", "end", "pgup", "pgdn",
          "delete", "backspace", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
          "f8", "f9", "f10", "f11", "f12", "a", "b", "c", "d", "e", "f",
          "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s",
          "t", "u", "v", "w", "x", "y", "z",
          "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
          "minus", "equal", "comma", "dot", "slash", "semicolon",
          "apostrophe", "grave", "backslash", "bracket_left",
          "bracket_right"}


class QMP:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(str(path))
        self.f = self.s.makefile("rwb")
        greeting = self.read()          # {"QMP": {...}}
        self.send({"execute": "qmp_capabilities"})
        self.read()

    def read(self):
        while True:
            line = self.f.readline()
            if not line:
                raise ConnectionError("QMP closed")
            msg = json.loads(line)
            if "event" in msg:
                continue
            return msg

    def send(self, obj):
        self.f.write(json.dumps(obj).encode() + b"\n")
        self.f.flush()

    def cmd(self, name, **args):
        self.send({"execute": name, "arguments": args})
        r = self.read()
        if "error" in r:
            raise RuntimeError(f"{name}: {r['error']}")
        return r.get("return")

    def hmp(self, line):
        return self.cmd("human-monitor-command", **{"command-line": line})


def ppm_to_png(src, dst):
    data = Path(src).read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError("not a binary PPM")
    magic, dims, maxval, raw = data.split(b"\n", 3)
    w, h = map(int, dims.split())
    if maxval != b"255":
        raise ValueError(f"unexpected maxval {maxval!r}")

    def chunk(tag, body):
        c = tag + body
        return (len(body).to_bytes(4, "big") + c
                + zlib.crc32(c).to_bytes(4, "big"))

    scanlines = b"".join(b"\x00" + raw[y * w * 3:(y + 1) * w * 3]
                         for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", w.to_bytes(4, "big") + h.to_bytes(4, "big")
                   + bytes([8, 2, 0, 0, 0]))
           + chunk(b"IDAT", zlib.compress(scanlines, 6))
           + chunk(b"IEND", b""))
    Path(dst).write_bytes(png)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    what, args = sys.argv[1], sys.argv[2:]

    if what == "ready":
        for _ in range(300):
            try:
                QMP(SOCK)
                return 0
            except (FileNotFoundError, ConnectionRefusedError,
                    ConnectionError, OSError):
                time.sleep(1)
        print("QMP never answered", file=sys.stderr)
        return 1

    q = QMP(SOCK)

    if what == "shot":
        out = args[0] if args else "shot.png"
        ppm = str(Path(out).with_suffix(".ppm"))
        q.cmd("screendump", filename=ppm)
        ppm_to_png(ppm, out)
        Path(ppm).unlink()
        print(out)
    elif what == "key":
        combo = "-".join(args)
        keys = [k for part in args for k in part.split("-") if k]
        for code in keys:
            if code not in QCODES:
                print(f"unknown qcode {code}", file=sys.stderr)
                return 1
        for code in keys:
            q.cmd("input-send-event", events=[{"type": "key", "data": {
                "key": {"type": "qcode", "data": code}, "down": True}}])
        for code in reversed(keys):
            q.cmd("input-send-event", events=[{"type": "key", "data": {
                "key": {"type": "qcode", "data": code}, "down": False}}])
    elif what in ("keydown", "keyup"):
        down = what == "keydown"
        q.cmd("input-send-event", events=[{"type": "key", "data": {
            "key": {"type": "qcode", "data": args[0]}, "down": down}}])
    elif what == "move":
        x, y = int(args[0]), int(args[1])
        q.cmd("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": x}},
            {"type": "abs", "data": {"axis": "y", "value": y}}])
    elif what in ("click", "down", "up"):
        btn = args[0] if args else "left"
        if what == "click":
            for d in (True, False):
                q.cmd("input-send-event", events=[
                    {"type": "btn", "data": {"down": d, "button": btn}}])
                time.sleep(0.02)
        else:
            q.cmd("input-send-event", events=[
                {"type": "btn", "data": {"down": what == "down", "button": btn}}])
    elif what == "hmp":
        print(q.hmp(args[0]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
