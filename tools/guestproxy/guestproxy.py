#!/usr/bin/env python3
"""A minimal HTTP/HTTPS proxy for the guest to reach the world through the Mac.

Binds to the vmnet bridge so only the guest can use it. Handles CONNECT (for
https) and absolute-URI requests (for http). Everything it opens is a plain
socket from this Mac, so it follows whatever route the Mac has -- including a
VPN tunnel the guest cannot use itself.
"""
import selectors, socket, sys, threading

HOST, PORT = sys.argv[1] if len(sys.argv) > 1 else "192.168.64.1", 8888

def pump(a, b):
    sel = selectors.DefaultSelector()
    sel.register(a, selectors.EVENT_READ, b)
    sel.register(b, selectors.EVENT_READ, a)
    try:
        while True:
            ready = sel.select(timeout=120)
            if not ready:
                return
            for key, _ in ready:
                data = key.fileobj.recv(65536)
                if not data:
                    return
                key.data.sendall(data)
    except OSError:
        return
    finally:
        sel.close()

def serve(client):
    try:
        client.settimeout(30)
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = client.recv(65536)
            if not chunk:
                return
            buf += chunk
        head, rest = buf.split(b"\r\n\r\n", 1)
        lines = head.split(b"\r\n")
        method, target, _ = lines[0].split(b" ", 2)

        if method == b"CONNECT":
            host, _, port = target.decode().rpartition(":")
            upstream = socket.create_connection((host, int(port or 443)), 20)
            client.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
        else:
            url = target.decode()
            if not url.startswith("http://"):
                client.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n")
                return
            rest_url = url[len("http://"):]
            hostport, _, path = rest_url.partition("/")
            host, _, port = hostport.partition(":")
            upstream = socket.create_connection((host, int(port or 80)), 20)
            out = [b" ".join([method, ("/" + path).encode(), b"HTTP/1.1"])]
            for line in lines[1:]:
                if line.lower().startswith((b"proxy-connection:", b"connection:")):
                    continue
                out.append(line)
            out += [b"Connection: close", b"", b""]
            upstream.sendall(b"\r\n".join(out) + rest)
        client.settimeout(None)
        upstream.settimeout(None)
        pump(client, upstream)
        upstream.close()
    except Exception as e:
        print(f"  ! {e}", flush=True)
    finally:
        client.close()

listener = socket.socket()
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind((HOST, PORT))
listener.listen(64)
print(f"proxy listening on {HOST}:{PORT}", flush=True)
while True:
    conn, addr = listener.accept()
    print(f"  {addr[0]} connected", flush=True)
    threading.Thread(target=serve, args=(conn,), daemon=True).start()
