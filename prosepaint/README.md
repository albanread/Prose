# ProsePaint

A native paint application for Prose (Haiku): a canvas to paint on,
colour selection for the ink, brush shapes and pen tools with
properties, multiple layers composited over each other, an eraser,
and a smudge tool for mixing the ink around.

Built and tested like all Prose software: cross-compiled on the
host, run and verified in the QEMU guest (see the repo `AGENTS.md`).

## Layout

```
prosepaint/
  app/          PPDocument (model) · PPBrush (stamps) · PPCanvas
                (view) · PPApp (window/panels/scripting/selftest)
  docs/plan.md  design, sprints, test matrix
  tests/        guest-smoke.sh — the harness-level matrix (P05)
```

## What works (Sprint 1)

- **Canvas:** paper-sized at DPI (A4 @ 96 ≈ 793×1123 default; A5/A3/
  Letter/Legal via Canvas menu), rendered desk + page like the suite.
- **Tools:** pen, brush (round/soft/square/angled masks, size 1–64,
  hardness), eraser (alpha-out), smudge (footprint drag; strength =
  how much of the carried ink survives each step), fill (scanline,
  tolerance), eyedropper.
- **Layers:** RGBA bitmaps, over-composited with visibility and
  opacity; add/delete/reorder/rename from the panel or Layer menu;
  the last layer never deletes. New documents open on a white
  Background layer.
- **Undo/redo:** per-stroke dirty rectangles (before/after bytes of
  the touched region only — one full-layer snapshot during a stroke).
- **Files:** atomic saves, `BEOS:TYPE` stamped, content sniffed
  (`HMF1` + `&tPp`), `.paint` extension; launch-with-file opens.
- **Scripting (the harness's brush):** `Tool`/`Brush`/`Colour` set,
  `StrokeLine`/`Dab` do, `Layer` do (add/delete/up/down/visible/
  opacity/rename), `Pixel` get (exact composite colour), `LayerCount`,
  `Activate`, `Save`, `Open`, `Quit`.
- **Selftest:** 46 checks (composite math, layer ops, dirty undo,
  brush masks, stroke/erase/fill/smudge, persistence, sniffing).

## Test status

| ID | Test | Result |
|---|---|---|
| P01 | selftest | PASS 46/46 |
| P02 | launch + render | PASS |
| P03 | scripted strokes + pixel reads | PASS |
| P04 | save → relaunch-with-file round trip | PASS |
| P05 | `tests/guest-smoke.sh` | PASS 8/8 |

## Not yet (planned — see docs/plan.md)

PDF export (flattened image page), zoom, recent files (Sprint 2);
airbrush/flow, stroke-buffer opacity, canvas resize, shape strokes
(Sprint 3). Painting by real mouse is human-owed on this guest
(the pointer is dead — everything above is driven through the
scripting surface).
