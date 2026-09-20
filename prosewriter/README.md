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
| ProseWriter | **Sprint 5 complete, guest-verified**: `hey` scripting finished — `get/set Text`, `Header`, `Footer`, `Selection`, `Modified`, `WordCount` all round-trip (SerialApp's `BPropertyInfo` suite pattern; sets are applied on the window looper via a private forward); named styles — define once from the selection, apply everywhere, stored in the document (panel: Document ▸ Styles…); measurements: 96-page layout 183 ms, single-key relayout 183 ms (incremental relayout is the honest next optimization), 234k-word dictionary loads in 63 ms. `--selftest` **71/71 PASS** |
| ProseWriter | **Sprint 6 (1 of 1 scoped this round), guest-verified**: incremental relayout — 96-page keystroke 183 ms → 9 ms, verified identical to cold layout; inline images — U+FFFC marker + per-paragraph bitmap table, File ▸ Insert image… via translators, .prose persistence carries pixels, image lines grow and share the line with text (verified in the GUI with a gradient PNG). `--selftest` **83/83 PASS**. Tables moved to their own sprint (see plan) |
| ProseWriter | **Sprint 7 complete, guest-verified: tables.** A table is a run of paragraphs containing 0x1D cell separators — rows are paragraphs, so editing, undo, styling, spell, images and incremental relayout all work inside cells with no special cases. Proportional columns, one synthesized line per row for page flow, caret mapping through cell geometry, grid rendering, Tab/Enter/Backspace cell semantics, File ▸ Insert table, .prose persistence for free. `--selftest` **97/97 PASS**; seeded table verified on screen (Widget/Qty/Price grid) |
| ProseWriter | **Sprint 9 — the fix pass, complete and guest-verified.** Every Part-1 bug from `docs/review-2026-09-20.md` plus the second review's additions: open actually opens (the `IFindLast` bug), UTF-8 and Delete-key typing, atomic save with a real status (the old success path was dead code), format undo, revision-based layout cache (setup/bold/same-length edits re-measure; tables follow shifts), shared-ownership images, panel/window lifecycle (no dangling pointers, app quits with only panels left), RTF multi-font tables + `\cf0` + tab-separated cells, justified/tab/print/scroll geometry. `--selftest` **144/144 PASS**; typing, save, reopen, quit-guard and clean exit all verified live in the guest. Document MIME type registered (sniffer on the file's first 8 bytes; `mimeset` types saved files to us). New scripting property `Activate` — background-launched windows on this guest are never activated without it. `tests/guest-smoke.sh`: **6/6** |
| ProseWriter | **Sprint 10 complete, guest-verified: print to PDF.** One PDF page per layout page, WYSIWYG by construction — `PWPageSetup` is the single truth (paper size → layout → PDF MediaBox; A4 yields a true 595×842 A4). Rendering goes through the view's print-mode path into `BPicture` → off-screen bitmap → Flate-compressed image XObject (system `libz.so.1`). File ▸ Print to PDF… (save panel) and a `PDF` scripting property; paper table now A4/Letter/Legal/A5/A3/B5. 18,000 words → 28-page 414 KB PDF in 0.8 s; QuickLook-verified typesetting. Known v1 cost: raster pages (non-selectable text). `--selftest` **164/164**; `tests/guest-smoke.sh` **9/9**. Saves stamp `BEOS:TYPE` and the open path dispatches by CONTENT (a just-saved file reopens as the document it is, extension or not); save panels prefill and append the right extension; `Open` scripting property drives the panel path |
| Next | SQLite document format (Part 2 of the review: schema designed, step 1 is a drop-in for Save/LoadFromFile), the PWApp.cpp split, selectable-text PDF (font embedding), mouse-owed UI checks |

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
