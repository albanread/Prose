# ProsePaint — plan, sprints, tests

**Goal:** a native paint application for Prose/Haiku: a canvas to
paint on, colour selection for the ink, brush shapes and pen tools
with properties, multiple layers composited over each other, an
eraser, and a smudge tool for mixing the ink around. Built on the
host, tested on the guest, sharing the harness and every lesson the
earlier apps paid for.

## Architecture

```
PPDocument   the model: paper/DPI-derived pixel canvas; layers
             (RGBA B_RGBA32 bitmaps + name/visible/opacity);
             over-operator composite (cached, invalidated on edit);
             dirty-rect stroke undo; .paint persistence (Flate-
             compressed flattened BMessage, sniffed by content)
PPBrush      the stamp engine: masks for round/soft/square/angled
             shapes × size × hardness; stamps blended along the
             stroke path with spacing; blend math owned by the model
             (no drawing-mode tricks) so canvas, PDF and tests share
             one truth
PPCanvas     BView: the composited image (desk + page, ProseDraw
             look), stroke input with interpolation, zoom-ready
             coordinate funnels
PPApp/PPWindow  application, menus, tool palette, brush properties,
             colour selection, the layers panel, scripting, selftest
```

Decisions made up front (user may redirect):

- **Canvas = paper at DPI** (A5/A4/A3/Letter/Legal @ 96 default,
  custom pixel sizes too) — keeps the print story; PDF MediaBox is
  the paper. Layers are straight RGBA, over-composited.
- **New documents open with a white Background layer**, like every
  paint app a user has met.
- **Undo is per-stroke dirty rectangles** — before/after bytes of
  the touched region only, bounded stack.
- **The scripting surface is the brush**: Stroke/StrokeLine/Tool/
  Brush/Colour/Layer/Pixel make every tool drivable headlessly (the
  guest's pointer is dead); Pixel get returns the composite colour,
  so smoke assertions are exact.

Inherited rules (AGENTS.md): content decides the loader; saves are
atomic and typed; reads are loops; every app ships --selftest;
Activate needs the window lock; kills by numeric team id; sync after
deploys; smoke deploy-verifies in step 1.

## Sprints

### Sprint 1 — the painting core (this delivery)

1. Model: paper presets (A5/A4/A3/Letter/Legal @96, custom px),
   layers (add/delete/reorder/visible/opacity/rename), over-
   composite with cache, dirty-rect undo/redo, `.paint` persistence
   (Flate) + sniffing, paper→PDF page mapping.
2. Brush engine: round/square/soft/angled masks, size 1–64,
   hardness, tool opacity; pen/brush/eraser stamp along interpolated
   stroke paths with spacing; eraser = destination alpha-out.
3. Canvas: desk + page render of the composite, stroke capture,
   live invalidation of the dirty rect only.
4. Window: tool palette (pen, brush, eraser, smudge, fill,
   eyedropper), brush properties (shape menu, size, hardness,
   opacity), colour selection (swatches + RGB fields), layers panel
   (list, add/delete/up/down, visibility, opacity), status bar.
5. Scripting: Activate, Tool set, Brush set, Colour set, Stroke/
   StrokeLine do, Layer do/get, Pixel get, Save/Open/Quit.
6. Selftest: composite math, layer ops, dirty-undo, brush masks
   (symmetry, hardness monotonicity, square corners), stroke
   rasterization, eraser, fill tolerance, smudge mixing, persistence
   round trip, sniff, paper presets.

### Sprint 1 — the painting core (delivered 2026-09-22)

**Met.** Selftest 46/46 (composite math, layer ops, dirty-rect undo,
brush mask shapes/symmetry/falloff, dab/erase/fill/smudge strokes,
persistence round trip, sniffing, paper presets); guest smoke 8/8
(exact pixel reads after scripted strokes, eraser to transparency,
layer hide/composite, typed save + magic, relaunch round trip, clean
quit); live screen render pixel-counted (red/blue/green strokes).

Bugs found and fixed on the way, with their checks:

- **Fill's seed was a pointer, not a copy** — the first run paints
  the seed pixel, so every later `near()` compared against the NEW
  colour and the flood stopped after one row.
- **Smudge's lerp was backwards** — strength as the blend weight
  meant full strength carried nothing; strength is now the survival
  of the carried footprint (full = pure drag, half = smear). The
  mixing shows on the stroke's flanks: the centre line keeps
  reading its own trail.
- **The app outlived its last window** (the ProseDraw 'pdWc' race):
  the auto quit-request arrives while the window is still listed;
  the override now ignores windows already quitting, read under the
  window lock.

Still human-owed: painting by real mouse (the guest's pointer is
dead — the scripting surface is the brush and covers everything
above); the layers panel and property controls by hand.

### Sprint 2 — the finishing kit

Smudge tool (footprint-lerp along the path), fill (scanline flood,
tolerance), eyedropper, PDF export (flattened Flate image page —
PWPDF pattern), zoom (25–400%, the ProseDraw mapping), recent files,
smoke steps for each.

### Sprint 2 — the finishing kit (delivered 2026-09-22)

**Met.**

- **PDF export:** `PPPDF` writes the flattened composite over white
  as one Flate image page — MediaBox is the paper in points, so an
  A4 painting prints on A4. File ▸ Print to PDF… panel (suggests
  name.pdf) and the `PDF` scripting property; exports are typed
  `application/pdf` and never touch document state. Selftest grew
  46 → 58 (PDF structure: magic, MediaBox, image XObject, xref
  offset; recent list ordering/round trip). The exported PDF was
  fetched to the host and pixel-verified: strokes land at their
  document coordinates, transparent canvas reads as white paper.
- **Zoom:** 25–400% through the canvas's single mapping point —
  render scales, input and the composite stay 1:1 data. View ▸ Zoom
  presets (radio marks) and a `Zoom` get/set property that accepts a
  factor or a percent. Verified live at 200% (pixel-counted strokes)
  and through the smoke's round trip.
- **Open Recent:** `PPRecent` (the ProseDraw pattern: most-recent-
  first, deduped, capped at 8, persisted in user settings). Every
  path adoption — open, panel save, scripted save — goes through one
  funnel. Verified by the smoke's settings-file check.

Smoke 8 → 11 steps, all green.

### Sprint 3 — polish (delivered 2026-09-22)

**Met.**

- **Stroke-buffer opacity — the paint-app contract:** colour and
  erase strokes render into a transparent stroke buffer and land on
  the layer ONCE, at the ink strength. Stamps within a stroke never
  build up no matter how slow you draw or how often they overlap;
  separate passes build toward full. Erase accumulates its mask the
  same way. Ink control (25/50/75/100%) in the properties panel and
  an `Opacity` scripting property.
- **Airbrush (Spray):** low-flow dabs that build with repetition —
  and a real spray: `Pulse` keeps dabbing while the button is held.
  Verified numerically: three flow-40 dabs land at alpha ≈ 103
  (40 → 74 → 102, the exact geometric build-up).
- **Shape strokes:** line, rectangle, ellipse tools with rubber-band
  preview on drag, stroked with the current brush through the same
  dab pipeline (so opacity, hardness and shape all apply). Scripting:
  `Shape do "line x y x y" / "rect|ellipse x y w h"`.
- **Canvas resize:** model-level `Resize` (content anchored top-left,
  growing pads transparent, shrinking crops; undo history clears —
  pixel steps no longer map). Canvas ▸ Resize… panel and a `Canvas
  set "w h"` scripting property.

Selftest 58 → 71 (opacity contract, erase-at-opacity, airbrush
build-up, shape edges vs interior, resize semantics); smoke 11 → 13
steps, all green. One real bug the live check caught: the shape tools
stamped nothing because `PaintDab`'s switch didn't route them — the
first probe "passed" against an older stroke underneath; isolated
probes exposed it.

Still human-owed: real-mouse painting and the rubber-band previews
by hand.

Airbrush (flow + accumulation), stroke-buffer opacity (proper
per-stroke alpha), canvas resize, palette/settings persistence,
straight-line and shape strokes (shift-constrained), performance
pass on the composite.

## Test definitions

| ID | Test | Method | Pass |
|---|---|---|---|
| P01 | selftest | `--selftest` | PASS 71/71 |
| P02 | launch + render | launch, screenshot | PASS |
| P03 | scripted strokes | StrokeLine → Pixel gets | PASS |
| P04 | round trip | Save → relaunch with file → Pixel gets | PASS |
| P05 | smoke | `prosepaint/tests/guest-smoke.sh` | PASS 13/13 |
| P06 | PDF export | `PDF do` → structure greps + host pixel verify | PASS |
| P07 | zoom | scripting round trip + 200% pixel count | PASS |
| P08 | recent files | settings file lists the run's saves | PASS |
| P09 | opacity/shapes/airbrush/resize | smoke step 10–11, exact pixels | PASS |
