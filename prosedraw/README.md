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
  connectors between shapes (press on A, release on B) with an
  arrowhead; deleting a shape deletes its connectors.
- **Grid:** 8 pt grid drawn on the page, snapping on by default
  (View ▸ toggles), snap-delta drags so existing shapes keep alignment.
- **Inspector:** label, X/Y/W/H, fill (none + 9 colours), stroke,
  line width, dashed, text size — applies to the selection live.
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
- **Scripting:** `Activate`, `ShapeCount` (get), `AddShape` (do, data
  `"kind x y w h|label"` or `"connect"`), `Save`, `Open`, `PDF`,
  `Quit` — this is the smoke-test surface.
- **Selftest:** `ProseDraw --selftest` — 61 checks (paper metrics,
  snapping, connector anchor math, align, undo, persistence, sniffing,
  PDF structure, recent list).

## Test status

| ID | Test | Result |
|---|---|---|
| D01 | selftest | PASS 61/61 |
| D02 | launch + render (screenshot) | PASS |
| D03 | scripted shapes + connector (screenshot) | PASS |
| D04 | save → relaunch-with-file round trip | PASS |
| D05 | `tests/guest-smoke.sh` | PASS 10/10 |
| D06 | PDF export (guest checks + host eyeball) | PASS |
| D07 | recent files persisted | PASS |

## Not yet (planned — see docs/plan.md)

BPrintJob for real printers (no printer on the test guest to verify
against — PDF is the printable path); zoom (canvas is 1:1); Sprint 3:
orthogonal connector routing, arrowheads per end (modelled, exposed
next), image shapes, multi-page, layers.
