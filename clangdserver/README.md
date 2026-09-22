# clangd_server

clangd, addressed the BeOS way. clangd speaks the Language Server Protocol
as JSON-RPC over its own stdin and stdout — a POSIX pipe contract no editor
can be talked to in. This server owns that: it spawns one clangd per
client, keeps the framing and the JSON inside, and faces editors with
BMessages, which is how BeOS programs talk to servers.

- `app/src/main.cpp` — the server: a single-launch `BApplication`
  (`application/x-vnd.prose.clangd_server`). The roster starts it on demand
  when the first editor addresses its signature, and it stays up. Sessions
  are opened and answered with a token; requests run on their own thread so
  the server's looper keeps answering every editor while clangd thinks;
  results and diagnostics are pushed to the notify messenger the editor
  named, echoing the editor's own `reqid`.
- `app/src/lsp.cpp` — the session machinery: the clangd child (the fork/
  execv pattern of Sisong's build pane, with its lessons), the
  Content-Length framing, the request bookkeeping, and the arithmetic
  between an editor's byte columns and LSP's UTF-16 ones.
- `app/src/json.cpp` — a JSON reader and writer as small as LSP needs.
  Haiku's own JSON classes are private API; a language service should not
  reach into private headers for a format this small.
- `app/src/lsp_protocol.h` — the BMessage protocol, with the field lists.
- `app/src/client.cpp` — `clangd_client`, the example of an editor's
  language-support half, and the probe: it opens a session, sends two
  documents, asks for a completion after `p.x` and receives the
  diagnostics clangd has, printing PASS when both arrived. It is a
  `BApplication` because messaging to and from a process is an
  application's business — a bare BLooper without one parks same-team
  messages that never reach the port (a morning was lost to exactly that).

## Building and testing

Cross-compiled on this Mac against the Prose sysroot (as `prosewriter/app`):

```sh
make -C app                 # clangd_server and clangd_client
```

On the guest, with clangd where the server looks for it
(`/boot/system/bin/clangd`, or non-packaged for a build in progress):

```sh
clangd_server --selftest        # the JSON codec and the UTF-16 arithmetic
clangd_server --selftest-live   # a whole session against a real clangd
clangd_server &                 # or let the roster start it
clangd_client                   # the BMessage conversation, end to end
```

`clangd_client` is also the shape of Sisong's side: open a session when a
C or C++ document is focused, send the text on open and change, post the
completion request where Complete Word is asked, and take the answers as
they arrive on the window looper.

## Shipping

`packages/builder/overlay/haiku-apps/clangd_server` packages the server
(requiring `cmd:clangd`) into `/boot/system/servers`, where its signature is
registered so the roster can start it. Sisong (ProseApps, from 831c322)
speaks to it and starts it on the first question from a C or C++ document:
a messenger addressed by signature only reaches a running application, so
the editor launches it and asks again. One clangd per editor session, no
sharing, no idle expiry: clangd is ~100 MB of libraries away per session
and the machine has 3 GB.
