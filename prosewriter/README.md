# ProseWriter — a native word processor for Prose/Haiku, developed on QEMU

This folder is a self-contained development line. Its host is this repository's
Prose distribution of Haiku arm64; its machine is **QEMU** (HVF, Apple
Silicon); its product is **ProseWriter**, a native word processor, plus the
research, tooling and skills needed to build and test it.

## Ground rules (agreed with the repo owner)

1. **Build on the host, test on the guest.** Nothing is run in the guest
   except what we install into *our own image copy*.
2. **No interference with the Prose work in the rest of this repo.**
   - We never write to `/Volumes/HaikuSrc/haiku` (the build tree on branch
     `prose`) or to `/Volumes/HaikuSrc/haiku-mmc.image`.
   - We boot `vm/prose-dev.image`, an APFS clone (copy-on-write) of the
     built image. The original is never attached to our QEMU.
   - We use the repo's cross-compiler (`generated/cross-tools-arm64`) and
     `bfs_shell` **read-only**. We never run `jam` in their tree and never
     apply or export patches there.
   - Our reference source is a separate shallow clone at
     `/Volumes/HaikuSrc/haiku-reference` (upstream `master`); it exists for
     reading and indexing only.
   - Our ports, sockets and scratch files live under `prosewriter/vm/`.
3. **When finished, ProseWriter becomes a prose package** (see
   `packages/README.md` for how prose packages are built). Until then it is
   installed into the dev image by hand, under `/boot/home/`.

## Layout

| | |
|---|---|
| `docs/` | The plan, sprint and test definitions, UI design, QEMU notes |
| `research/` | The BeBook and Be Newsletters mirrors, Haiku docs, GoBe Productive research |
| `api-db/` | The SQL index of the Be/Haiku API and docs, and its MCP server |
| `skill/` | The `prose-qemu-dev` skill: how an agent develops and tests apps here |
| `vm/` | Our QEMU machine: image copy, run/automation scripts, scratch |
| `app/` | ProseWriter source |
| `tests/` | Guest-side test scripts and expected results |

## Where things stand (2026-09-19)

| | |
|---|---|
| Research | BeBook (433 pages) + Be Newsletters (230) mirrored under `research/`; Haiku HIG/API book/userguide indexed; GoBe Productive research with screenshots in `research/gobe-productive.md` |
| API index | `api-db/prose_api.sqlite` — 16,086 symbols from the arm64 devel headers + 980 doc pages, FTS5; MCP server `api-db/mcp_server.py` (registered as `prose-api-db` in `.zcode/config.json`, skills in `.zcode/skills/`) |
| VM harness | `vm/` boots our image clone headless under QEMU/HVF; **proseagent** in the guest gives `run`/`put`/`get`/`launch` over `127.0.0.1:9000`; QMP gives screendump + keyboard/tablet input; `vm/guest.sh`, `vm/qmp.py` |
| ProseWriter | **Sprint 1 complete and guest-verified**: model (runs, undo, `.prose` round-trip), layout (greedy wrap, A4 pagination, offset↔xy), page view (render, caret, selection, keyboard/mouse), window (menus, clipboard, save/open), `--selftest` **20/20 PASS**, 96 pages layout in ~180 ms; typed text via QEMU keyboard renders on the page |
| ProseWriter | **Sprint 2 complete and guest-verified**: RTF import/export (selftest round trip incl. fonts/colour/alignment), find & replace bar (wrap, case toggle, replace all), justified alignment with correct caret mapping, zoom 50–200 % + fit width (deterministic view scaling), Text menu (family/size/colour/alignment), ruler with draggable page margins, styled clipboard, window fits the screen. `--selftest` **34/34 PASS** |
| ProseWriter | **Sprint 3 complete and guest-verified**: headers/footers with `{page}`/`{pages}` fields (persisted, rendered per page), page setup (paper/orientation/margins), BPrintJob printing with print-mode rendering, recent documents, perf pass (cached line offsets, binary-search lookups, contiguous line ownership, per-page paint). `--selftest` **41/41 PASS**; landscape/header/footer/print-alert all verified in the GUI via `--seed/--paper/--set-header/--print` harness flags |
| ProseWriter | **Sprint 4 complete, guest-verified**: paragraph indents (left/right/first-line), line spacing, space before/after, tab stops with layout-engine advance, bulleted & numbered lists — all round-tripping through RTF (`\li \ri \fi \sl \sb \sa \tx`); spell check as-you-type with red squiggles over a 234k-word dictionary (1997's finest, user-requested); harness hardening earned the hard way: SIGPIPE-proofed agent with 90s run watchdog, CLOEXEC'd listening socket, rename-based updates, thread-confined harness flags. `--selftest` **65/65 PASS**. Scripting (hey) implemented but **experimental** — see `docs/plan.md` |
| Next | Sprint 5 — see `docs/plan.md` |

## The dev loop

```
app/ProseWriter.cpp                host: edit
  make -C app                      host: cross-compile (arm64)
  vm/install.sh                    host: bfs_shell → /boot/home/apps/ in prose-dev.image
  vm/run.sh (already running? restart)   QEMU boots the image
  vm/guest.sh run "ProseWriter &"  guest: over the automation agent
  vm/shot.sh                       host: QMP screendump → PNG
```

See `docs/qemu-notes.md` for how the harness works.
