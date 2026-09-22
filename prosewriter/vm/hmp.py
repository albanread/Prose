#!/usr/bin/env python3
# Minimal HMP client for the VM's monitor socket — a fallback when the
# QMP socket is wedged (one client only, sometimes left held).
#   hmp.py "sendkey m"          — run an HMP command
#   hmp.py shot out.png         — screendump (PPM converted with sips)
import socket, subprocess, sys, time
from pathlib import Path

SOCK = '/Volumes/xb/HaikuArmQemu/prosewriter/vm/monitor.sock'

def hmp(cmd, timeout=3.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(SOCK)
    time.sleep(0.15)
    try:
        s.recv(65536)  # banner
    except socket.timeout:
        pass
    s.sendall(cmd.encode() + b'\n')
    time.sleep(0.25)
    out = b''
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            out += chunk
            if b'(qemu)' in out[-20:]:
                break
    except socket.timeout:
        pass
    s.close()
    return out.decode('latin1')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    if sys.argv[1] == 'shot':
        out = sys.argv[2] if len(sys.argv) > 2 else 'shot.png'
        ppm = str(Path(out).with_suffix('.ppm'))
        print(hmp(f'screendump {ppm}').strip())
        time.sleep(0.2)
        subprocess.run(['sips', '-s', 'format', 'png', ppm, '--out', out],
            check=True, capture_output=True)
        Path(ppm).unlink()
        print(out)
    else:
        print(hmp(' '.join(sys.argv[1:])).strip())
