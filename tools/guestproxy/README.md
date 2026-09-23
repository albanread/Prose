# guestproxy — let the guest out while a VPN owns the Mac's default route

**This is now built into Prose.app and on by default** (`guestproxy.swift`,
Settings ▸ "Let the guest reach the internet through this Mac"). It starts with
the machine, listens on the bridge address, and stops when the machine does.
The Python script here is the same thing standalone, kept because it is easy to
read and easy to run against a machine Prose.app is not managing.

Note that a connected VPN also stops DHCP working, so the guest may need
Machine ▸ Network ▸ Assign a Static Address before it can reach the proxy at
all. See `docs/networking.md`.

A VPN on the Mac takes the guest off the internet: vmnet still NATs, so the
guest reaches the gateway and the LAN, but nothing routed through the tunnel
comes back. See `docs/networking.md` for the measurements.

This is the way round it that needs no VPN reconfiguration. The proxy runs on
the Mac and opens ordinary host sockets, so its traffic takes whatever route
the Mac has — tunnel included — and the guest only ever talks to the bridge.

```
tools/guestproxy/guestproxy.py            # binds 192.168.64.1:8888
tools/guestproxy/guestproxy.py 10.0.0.1   # a different bridge address
```

It binds to the bridge address only, so nothing outside the machine can reach
it, and it dies with the terminal. It handles `CONNECT` (https) and
absolute-URI requests (http).

## Pointing the guest at it

NetSurf, in `/boot/home/config/settings/NetSurf/Choices` — it is libcurl
underneath, so https works through `CONNECT`:

```
http_proxy:1
http_proxy_host:192.168.64.1
http_proxy_port:8888
http_proxy_auth:0
```

Anything else in the guest that links libcurl takes the environment variable:

```
export http_proxy=http://192.168.64.1:8888
export https_proxy=http://192.168.64.1:8888
```

## What cannot use it

Haiku's own `BUrlRequest`/`BHttpRequest` has no proxy support and Network
preferences has no proxy UI, so **`pkgman` and HaikuDepot cannot go through
this** — `FetchFileJob` fetches with `BUrlRequest`. Package work still needs the
VPN down.

`BProxySecureSocket` looks like the exception and is not: it is an explicit
class nothing consults automatically, and its `Connect()` cannot succeed as
written —

```c
int matches = sscanf(buffer, "HTTP/1.0 %d %*[^\r\n]\r\n\r\n", &httpStatus);
if (matches != 2)
    return B_BAD_DATA;
```

`%*[...]` is assignment-suppressed, so `sscanf` returns at most 1 and the check
always fails. It also only matches `HTTP/1.0` replies where most proxies answer
`HTTP/1.1`.

## Measured 2026-09-23, NordVPN connected

| from the guest | direct | via this proxy |
|---|---|---|
| `ping 1.1.1.1` | 100% loss | — |
| `curl http://example.com` | 000 | **200** |
| `curl https://www.wikipedia.org` | — | **200** |
| NetSurf on `https://example.com/` | — | **renders, Done (0.0s)** |
