#!/usr/bin/env python3
"""Client for proseagent (the guest automation daemon) via 127.0.0.1:9000.

Usage:
  guest.sh ping
  guest.sh run '<shell command>'
  guest.sh get <guest-path> [out-file]
  guest.sh put <local-file> <guest-path>
  guest.sh launch <program> [args ...]
  guest.sh wait-boot [timeout]      # ping until the agent answers
  guest.sh screenshot [out-file]    # runs Haiku's screenshot tool in the guest
"""
import os
import socket
import sys
import time
from pathlib import Path

HOST, PORT = "127.0.0.1", 9000


def call(request: bytes, read_reply=True) -> bytes:
    s = socket.create_connection((HOST, PORT), timeout=120)
    s.sendall(request)
    if not read_reply:
        return b""
    out = b""
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        out += chunk
    s.close()
    return out


def get_file(path):
    s = socket.create_connection((HOST, PORT), timeout=120)
    s.sendall(f"get {path}\n".encode())
    f = s.makefile("rb")
    head = f.readline().decode().strip()
    if not head.startswith("OK"):
        print(head or "no reply", file=sys.stderr)
        sys.exit(1)
    length = int(head.split()[1])
    data = f.read(length)
    s.close()
    return data


def put_file(src, dst):
    data = Path(src).read_bytes()
    s = socket.create_connection((HOST, PORT), timeout=120)
    s.sendall(f"put {dst} {len(data)}\n".encode() + data)
    reply = s.recv(100).decode().strip()
    s.close()
    return reply


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd, args = sys.argv[1], sys.argv[2:]

    if cmd == "wait-boot":
        deadline = time.time() + int(args[0] if args else 180)
        while time.time() < deadline:
            try:
                r = call(b"ping\n")
                if r.startswith(b"PONG"):
                    print(r.decode().strip())
                    return 0
            except OSError:
                pass
            time.sleep(3)
        print("agent never answered", file=sys.stderr)
        return 1
    if cmd == "ping":
        print(call(b"ping\n").decode().strip())
    elif cmd == "run":
        out = call(f"run {args[0]}\n".encode())
        sys.stdout.write(out.decode(errors="replace"))
        return 0 if b"\nEXIT 0\n" in out or out.endswith(b"EXIT 0\n") else 1
    elif cmd == "get":
        data = get_file(args[0])
        out = args[1] if len(args) > 1 else os.path.basename(args[0])
        Path(out).write_bytes(data)
        print(f"{len(data)} bytes -> {out}")
    elif cmd == "put":
        print(put_file(args[0], args[1]))
    elif cmd == "deploy":
        # Safe update of a binary the agent may be running: upload beside it,
        # cp over (unlink+create, never touches the running inode), restart.
        local, dst = args[0], args[1]
        tmp = dst + ".new"
        r = put_file(local, tmp)
        if r.strip() != "OK":
            print(r, file=sys.stderr)
            return 1
        call(f"run /bin/mv -f {tmp} {dst}\n".encode())
        if dst.endswith("proseagent"):
            call(b"run /boot/system/bin/killall proseagent\n")
            time.sleep(3)          # the bootscript wrapper restarts it
            while True:
                try:
                    if call(b"ping\n").startswith(b"PONG"):
                        print("agent restarted")
                        break
                except OSError:
                    pass
                time.sleep(2)
        else:
            print(f"deployed {dst}")
    elif cmd == "launch":
        print(call(" ".join(["launch"] + args).encode() + b"\n").decode().strip())
    elif cmd == "screenshot":
        out = args[0] if args else "guest.png"
        guest_png = "/boot/home/prose-shot.png"
        r = call(f"run screenshot -o {guest_png}\n".encode()).decode(errors="replace")
        if "EXIT 0" not in r:
            print(r, file=sys.stderr)
            return 1
        data = get_file(guest_png)
        Path(out).write_bytes(data)
        print(f"{len(data)} bytes -> {out}")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
