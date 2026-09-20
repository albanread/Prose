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

### Sprint 3 — beyond

Arrowheads per end, connector routing (orthogonal), image shapes,
multi-page documents, layers.

## Test definitions

| ID | Test | Method | Pass |
|---|---|---|---|
| D01 | selftest | `--selftest` | PASS 35/35 |
| D02 | launch + render | launch, screenshot | PASS |
| D03 | scripted shapes | `AddShape` ×3 + connector, screenshot | PASS |
| D04 | round trip | Save → relaunch with file → screenshot + ShapeCount | PASS |
| D05 | smoke | `prosedraw/tests/guest-smoke.sh` | PASS 8/8 |
