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
  (`HMF1` + `&tPp`), `.paint` extension; launch-with-file opens;
  Open Recent (8 entries, persisted in user settings).
- **Print to PDF (Sprint 2):** the flattened painting as one
  Flate-compressed image page, MediaBox = the paper (A4 painting →
  A4 PDF); transparent canvas reads as white paper. Via File ▸ Print
  to PDF… or the `PDF` scripting property; exports are typed
  `application/pdf`.
- **Zoom (Sprint 2):** 25–400% (View ▸ Zoom presets; `Zoom`
  scripting property, factor or percent) — the render scales, input
  and the composite stay 1:1 data.
- **Opacity, the paint contract (Sprint 3):** colour and erase
  strokes render into a buffer and land ONCE at the ink strength —
  stamps within a stroke never build up; separate passes build.
  Ink control in the panel, `Opacity` scripting property.
- **Airbrush (Sprint 3):** low-flow spray that builds with
  repetition and keeps spraying while the button is held.
- **Shape strokes (Sprint 3):** line/rectangle/ellipse tools with
  rubber-band preview, stroked with the current brush (ink, hardness
  and brush shape all apply); `Shape` scripting property.
- **Canvas resize (Sprint 3):** Canvas ▸ Resize… or `Canvas set
  "w h"` — content anchored top-left, padding transparent, cropping
  on shrink (undo history clears).
- **Scripting (the harness's brush):** `Tool`/`Brush`/`Colour`/
  `Zoom` set, `StrokeLine`/`Dab` do, `Layer` do (add/delete/up/down/
  visible/opacity/rename), `Pixel` get (exact composite colour),
  `LayerCount`, `Activate`, `Save`, `Open`, `PDF`, `Quit`.
- **Selftest:** 71 checks (composite math, layer ops, dirty undo,
  brush masks, stroke/erase/fill/smudge, PDF structure, persistence,
  sniffing, recent list).

## Test status

| ID | Test | Result |
|---|---|---|
| P01 | selftest | PASS 71/71 |
| P02 | launch + render | PASS |
| P03 | scripted strokes + pixel reads | PASS |
| P04 | save → relaunch-with-file round trip | PASS |
| P05 | `tests/guest-smoke.sh` | PASS 13/13 |
| P06 | PDF export (structure + host pixel verify) | PASS |
| P07 | zoom (scripting + 200% render) | PASS |
| P08 | recent files persisted | PASS |

## Not yet (planned — see docs/plan.md)

Painting by real mouse — and the rubber-band previews by hand — are
human-owed on this guest (the pointer is dead; everything above is
driven and verified through the scripting surface).
