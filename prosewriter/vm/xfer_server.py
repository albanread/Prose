#!/usr/bin/env python3
"""Tiny HTTP file server for the prose-dev guest (10.0.2.2 = host).

GET  /<file>        serves files from vm/www/          (guest pulls)
POST /up/<name>     saves the body to vm/www/incoming/ (guest pushes, wget --post-file)

Stdlib only, single-threaded, binds 0.0.0.0:8000 (QEMU user-net forwards
the guest's 10.0.2.2 to this host).
"""
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "www"


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(ROOT), **kw)

    def do_POST(self):
        name = self.path.rsplit("/", 1)[-1] or "upload.bin"
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        dest = ROOT / "incoming" / name
        dest.parent.mkdir(exist_ok=True)
        dest.write_bytes(body)
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")
        print(f"[xfer] POST {name} ({length} bytes) -> {dest}", file=sys.stderr)

    def log_message(self, fmt, *args):
        print("[xfer] " + (fmt % args), file=sys.stderr)


if __name__ == "__main__":
    (ROOT / "incoming").mkdir(parents=True, exist_ok=True)
    srv = ThreadingHTTPServer(("0.0.0.0", 8000), Handler)
    print(f"[xfer] serving {ROOT} on :8000", file=sys.stderr)
    srv.serve_forever()
