# ProseDraw — plan, sprints, tests

**Goal:** a native diagram/draw application for Prose/Haiku, in the BeOS
tradition: a gridded page, a palette of shapes you drag on, arrange and
align, connect to each other, and inspect — producing diagrams for A4,
US Letter and A3 pages that print as **vector PDF**. Built on the host,
tested in the guest, sharing the ProseWriter harness (vm/, skills) and
every lesson its review paid for.

## Architecture

```
PDDocument   the model: shapes (rect/rounded/ellipse/diamond/text/
             connector) with styles and labels; grid + snap; paper
             metrics; snapshot undo; .draw persistence (flattened
             BMessage, 'pDd&') with content sniffing
PDCanvas     BView: the page (desk + shadow, like ProseWriter), grid,
             shape/selection/handle rendering, the tool state machine
             (select, create, connect, text), rubber-band and connector
             drags
PDApp/PDWindow  application, menus, tool palette, the inspector panel
             (geometry, colours, stroke, label), scripting, selftest
```

Rules inherited from ProseWriter's review, not relearned:
**content decides the loader** (`HMF1` + `'pDd&'` magic), **saves are
atomic and typed** (`BEOS:TYPE` stamped; sniffer + preferred app
registered at launch), **reads are loops** (no single `Read()`), **every
save panel appends its extension** via one tested helper, **scripting
ships with `Activate` from day one** (background-launched windows on
this guest are never activated), **every app has a `--selftest`** whose
checks cover each bug fixed, and paper metrics are points at 72 dpi from
one table.

## Sprints

### Sprint 1 — the drawable page (this delivery)

1. Model: six shape kinds, styles (fill on/off, fill/stroke colours,
   stroke width, dash, label, text size/colour), stable ids, grid (8 pt
   default) with snap on create/move/resize/nudge, align/distribute,
   z-order, snapshot undo/redo, `.draw` save/load (message + atomic
   file), paper table A4/Letter/Legal/A5/A3 × portrait/landscape.
2. Canvas: page render with grid, shape rendering incl. labels and
   dashed strokes, connectors anchored to shape borders (recomputed as
   shapes move) with an end arrowhead, single/rubber-band/shift
   selection, move, 8-handle resize, connector tool (press on A, release
   on B), text tool, Delete/arrow-key editing.
3. Window: menus (File/Edit/Shape/Align/View with paper + orientation +
   grid toggles), tool palette, inspector panel (x/y/w/h, label, fill
   and stroke colours, stroke width, dash, text size), status bar.
4. Scripting: `Activate`, `ShapeCount`, `AddShape` (data:
   `kind x y w h|label`), `Save`, `Open` — the smoke-test surface.
5. Selftest: paper metrics, snapping, connector anchor math, align,
   undo/redo, `.draw` round trip incl. connectors, content sniffing.

**Exit criteria:** selftest green; a diagram built through the guest
(some shapes + a connector via scripting, edits via inspector by human
mouse) renders on the page; the `.draw` round-trips.

**Met — 2026-09-20.** Selftest 35/35; scripted diagram (rrect Input →
diamond Valid? → ellipse Output + connector) verified rendering by
screenshot; `.draw` round trip verified both in selftest and live
(relaunch with file: ShapeCount 4, title `flow.draw`, screenshot
identical); smoke D05 8/8. Still human-owed: palette drags and
inspector edits by mouse.

**Session findings (launch-with-file debugging):**

- The relaunch failure that started the debug session was **not a code
  bug**: the guest copy of the binary predated the fix by an hour, and
  three zombie teams sat blocked in modal alerts from earlier attempts.
  Lesson (now in AGENTS.md): prove guest binary size == host build
  before believing a failure; smoke step 1 deploy-verifies.
- Real bug the smoke then caught: **scripted `Save` left the window
  title stale** (`* Untitled`) because it saved the document without
  telling the window its path; scripted `AddShape` didn't refresh the
  status bar either. Fixed with `PDWindow::NoteSavedTo()` + public
  `UpdateStatus()` called from the scripting handlers.
- `/tmp` resolves to `/boot/system/cache/tmp`; the window title now
  shows the leaf name, not the resolved path.

### Sprint 2 — PDF and polish (next)

Vector PDF export (PDF path operators for shapes, dashes, colours;
Helvetica base-14 for labels; MediaBox = the paper): File ▸ Print to
PDF…, `PDF` scripting property; BPrintJob for real printers; recent
files; duplicate/group; zoom.

**Delivered — 2026-09-20 (PDF + recent files).**

- `PDPDF`: true vector output — path operators per shape (rect /
  Bézier ellipse / Bézier rounded-rect / diamond), connectors with
  arrowhead triangles (same geometry as the canvas), Helvetica
  base-14 labels with the real AFM width table for centring,
  UTF-8→WinAnsi with octal escaping, dash patterns, MediaBox from
  the paper table (A4/Letter/Legal/A5/A3, portrait and landscape).
  Selftest grew 35 → 61 checks; smoke gained the PDF step (8) and
  the recent-files step (10) — 10/10. The exported PDF was fetched
  to the host and eyeballed: A4 portrait, three labelled shapes,
  arrowhead connector (the P5-style check, but vector).
- File ▸ Print to PDF… (save panel, suggests name.pdf) and the
  `PDF` scripting property; exports stamp `BEOS:TYPE
  application/pdf` and never touch document state.
- Arrowhead flags: the model always had `arrowEnd`/`arrowStart`;
  the canvas ignored them (always drew the end head). Canvas and
  PDF now both respect the flags.
- Open Recent: `PDRecent` (most-recent-first, deduped, capped at 8,
  persisted in `~/config/settings/ProseDraw/recent_files`) — the
  ordering and round trip are selftested, the persistence verified
  in-guest by the smoke. Every path adoption — open, save, save-as,
  scripted save — funnels through `NoteSavedTo`, so title, recent
  list and status can't drift apart again.
- **Deferred, honestly:** BPrintJob (no printer exists on this
  guest to verify against — the PDF is the printable path); zoom
  (a canvas-wide coordinate change, next with the polish pass);
  duplicate/group refinements beyond Sprint 1's Duplicate.

**Still human-owed:** the Print to PDF and Open Recent menu items by
mouse, palette drags, inspector edits by mouse.

### Sprint 3 — beyond

Arrowheads per end, connector routing (orthogonal), image shapes,
multi-page documents, layers.

**Delivered — 2026-09-21 (routing, arrow controls, zoom; images/
multi-page/layers remain future work).**

- **Elbow connectors:** per-connector `orthogonal` flag; the route
  (straight `[a,b]` or one-bend elbow `[a, m1, m2, b]`, dominant-axis
  midpoint split — no obstacle avoidance, Sprint 4 says so) lives in
  one place, `PDDocument::ConnectorWaypoints`, shared by canvas,
  hit-testing and PDF. Toggle via the inspector ("Elbow route") or
  scripting (`AddShape do "connect elbow"`). Eyeballed in the
  exported PDF: Source→Sink leaves horizontally, bends down, enters
  horizontally.
- **Arrow controls:** "Arrow at end" / "Arrow at start" inspector
  checkboxes (enabled when the selection holds a connector); both
  flags render on screen and in PDF along the meeting segment.
  Duplicate copies all flags now.
- **Zoom:** 25%–400%, one mapping point (DocToView/ViewToDoc) —
  every interaction, the grid, pen widths, corner radii and label
  sizes scale; PDF export is doc-space and unaffected. View ▸ Zoom
  presets (50–200%, radio marks) and a `Zoom` get/set scripting
  property that accepts a factor (2) or a percent (200) — hey sends
  ints, pwquery strings, both work.
- Selftest 61 → 71 (routing block: waypoints, axis-aligned segments,
  polyline hit test on the mid-segment, persistence, flag undo);
  smoke 10 → 11 steps (elbow PDF structure + zoom round trip).

**Session findings (two traps worth their price):**

- A hung selftest was my own new test repeating the Sprint 1
  dangling-pointer lesson: `PDShape*` held across `AddShape`
  reallocs, then `ShapeById(garbage)->kind` → NULL deref → the
  Haiku crash alert, which waits forever headless. Worse: a
  debugger-suspended team **ignores kill -9** — five zombies later
  the roster sent scripting to dead instances and everything
  "failed". Cure: QEMU system_reset (disk persists, RAM clears).
  Both lessons now in AGENTS.md.
- The same reset exposed a deploy hazard: `guest.sh put` verifies
  against the guest's page cache, not the drive — an unflushed
  deploy is lost to a reset. Sync after deploying (AGENTS.md).

**Still human-owed:** inspector checkboxes and the View ▸ Zoom menu
by mouse; palette drags.

## Test definitions

| ID | Test | Method | Pass |
|---|---|---|---|
| D01 | selftest | `--selftest` | PASS 61/61 |
| D02 | launch + render | launch, screenshot | PASS |
| D03 | scripted shapes | `AddShape` ×3 + connector, screenshot | PASS |
| D04 | round trip | Save → relaunch with file → screenshot + ShapeCount | PASS |
| D05 | smoke | `prosedraw/tests/guest-smoke.sh` | PASS 11/11 |
| D06 | PDF export | `PDF do` → magic/MediaBox/vector ops in guest; fetched PDF eyeballed on host | PASS |
| D07 | recent files | settings file lists opened docs, most recent first | PASS |
| D08 | elbow + zoom | orthogonal route in PDF (eyeballed), zoom scripting round trip, 200% screenshot | PASS |
