# ProseDraw

Grid-based diagram editor for Prose (Haiku arm64): drag shapes from a
palette onto a page, connect them, align and arrange, and tune every
attribute in an inspector. Documents target real paper (A4, Letter,
Legal, A5, A3 — portrait or landscape) for later PDF printing.

Built and tested like all Prose software: cross-compiled on the host,
run and verified in the QEMU guest (see the repo `AGENTS.md` and the
`prose-qemu-dev` skill).

## Layout

```
prosedraw/
  app/          PDDocument (model) · PDCanvas (view) · PDApp (window)
  docs/plan.md  sprints, exit criteria, test matrix, session findings
  tests/        guest-smoke.sh — the harness-level matrix (D05)
```

## What works (Sprint 1)

- **Shapes:** rectangle, rounded rectangle, ellipse, diamond, text;
  connectors between shapes (press on A, release on B); deleting a
  shape deletes its connectors.
- **Connectors (Sprint 3):** straight or elbow (orthogonal) routes and
  arrows at either end — per connector, set in the inspector or via
  `AddShape do "connect elbow"`; screen, hit-testing and PDF all share
  one router.
- **Grid:** 8 pt grid drawn on the page, snapping on by default
  (View ▸ toggles), snap-delta drags so existing shapes keep alignment.
- **Zoom (Sprint 3):** 25%–400%, View ▸ Zoom presets or the `Zoom`
  scripting property (factor or percent); everything scales, PDF
  export stays 1:1 vector.
- **Inspector:** label, X/Y/W/H, fill (none + 9 colours), stroke,
  line width, dashed, text size, connector arrows/route — applies to
  the selection live.
- **Arrange:** 6 alignments + 2 distributions, front/back z-order,
  duplicate (offset), delete, arrow-key nudge, full undo/redo.
- **Paper:** A4/Letter/Legal/A5/A3, portrait/landscape, page metrics
  drive the canvas page (and Sprint 2's PDF MediaBox).
- **Files:** atomic saves, `BEOS:TYPE` stamped on write, content-sniffed
  loads (`HMF1` + `&dDp` — extensionless files open fine), save panel
  appends `.draw`; launch-with-file and Tracker-style refs both open;
  Open Recent (8 entries, persisted in user settings).
- **Print to PDF (Sprint 2):** true vector export — path operators per
  shape, Bézier ellipses and rounded rects, dashed strokes,
  arrowheaded connectors, Helvetica-set labels, MediaBox = the paper
  (A4/Letter/Legal/A5/A3, portrait/landscape). Via File ▸ Print to
  PDF… or the `PDF` scripting property; exported files are typed
  `application/pdf`.
- **Scripting:** `Activate`, `ShapeCount`/`Zoom` (get/set), `AddShape`
  (do, data `"kind x y w h|label"`, `"connect"`, `"connect elbow"`),
  `Save`, `Open`, `PDF`, `Quit` — this is the smoke-test surface.
- **Selftest:** `ProseDraw --selftest` — 71 checks (paper metrics,
  snapping, connector anchor math and routing, align, undo,
  persistence, sniffing, PDF structure, recent list).

## Test status

| ID | Test | Result |
|---|---|---|
| D01 | selftest | PASS 71/71 |
| D02 | launch + render (screenshot) | PASS |
| D03 | scripted shapes + connector (screenshot) | PASS |
| D04 | save → relaunch-with-file round trip | PASS |
| D05 | `tests/guest-smoke.sh` | PASS 11/11 |
| D06 | PDF export (guest checks + host eyeball) | PASS |
| D07 | recent files persisted | PASS |
| D08 | elbow routing + zoom (PDF eyeball + scripting) | PASS |

## Not yet (planned — see docs/plan.md)

BPrintJob for real printers (no printer on the test guest to verify
against — PDF is the printable path); image shapes, multi-page
documents, layers, obstacle-avoiding connector routing.
