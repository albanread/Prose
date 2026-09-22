# ProseJulia — plan and session ledger

An animated Julia-set fractal: z ← z² + c, with c riding a circle of
radius 0.7885 around the origin, so the picture morphs continuously
between connected snowflakes (c inside the Mandelbrot set, near θ=180°)
and Cantor dust (c outside, near θ=0°). Simple and fun, per request
(2026-09-22) — and a workout for exact-colour scripting: the harness has
no mouse, so the fractal must be *measurable* through `hey`/`pwquery`
alone.

## Design

- **PJFractal** (`app/src/PJFractal.{h,cpp}`) — pure math, no UI: view
  window (centre + span), c-orbit (θ in [0,360), radius 0.7885),
  escape-time iteration with a **smooth** (normalised) count
  `i + 1 − log₂(log₂|z|)`, 4 hand-built 256-entry colour ramps
  (inferno/ocean/ember/ultraviolet) wrapped at 256. `SmoothIter` returns
  −1 for interior points; `PixelColour` paints interior black in every
  palette — that invariant is what the smoke's palette test leans on.
- **PJView** — renders into an offscreen B_RGBA32 bitmap; `Pulse` at
  ~20 fps advances θ and re-renders at **1/3 resolution** while
  animating, full resolution once paused (the "beauty pass"). Keyboard:
  space pause, +/− zoom 0.7×, arrows pan span/10.
- **PJWindow** — Fractal menu (pause/reset/palette/detail/speed radios),
  File menu (postcard PDF, quit); title bar carries θ, c, zoom, palette.
- **Scripting is the harness's eye**: Frame (get/set), Paused
  (set-not-toggle), Palette by name, Iterations, Zoom factor, Centre
  "re im", **Pixel get "x y" → "r g b" read from the rendered bitmap**,
  Postcard do <path>, Activate (locks, refocuses), Quit.

## Sprints

### Sprint 1 — the whole toy (delivered 2026-09-22)

Math library, view/window/menus, Pulse animation with dual-resolution
rendering, full scripting surface, postcard PDF (512² Flate image PDF,
same writer shape as ProseWriter's), `--selftest`, guest smoke.

Exit criteria — met:

- selftest **19/19** in the guest (orbit wrap at 0°/90°, far-point fast
  escape, interior −1 at θ=180° where c=−0.7885 is inside the M-set,
  determinism, zoom/span/iteration clamps, render opacity, render↔
  PixelColour agreement, frame/palette change the picture)
- guest smoke **10/10** (deploy-verify, selftest, launch+activate,
  Frame round trip paused, inside-black/deterministic/escaping pixels,
  palette recolour, postcard magic/MediaBox/size, animate→freeze,
  zoom+centre, clean quit)
- visual: screenshot at θ=180 cropped and confirmed — black bulbs with
  warm gradient bands, menus present

### Sprint 2 — candidates, not started

Bookmarks of c positions, a thumbnail gallery of orbit positions,
BPrintJob for real printing, per-palette iteration-colour cycling.

## Still owed

- **Mouse on real hardware.** The guest never delivers mouse events to
  windows (AGENTS.md), so zoom/pan were exercised by keyboard and
  scripting only. Wheel-zoom and drag-pan need a hardware pass.
- **Menu-item paths** (palette/detail/speed radios, the settings-dir
  postcard) are the same handlers the scripted properties drive, but the
  menu round trips themselves were not harness-tested — menus are not
  keyboard-reachable headless without more sendkey choreography.
- **Animation smoothness on hardware.** ~20 fps at 1/3 res is a QEMU/
  software-rendering number; nothing here says what a real GPU does.

## Session findings (2026-09-22)

- **A reply sent from a carried copy of a message never arrives.**
  `m.AddMessage("orig", message)` flattens the message; the flattened
  form has no header, so the unflattened copy has no reply address and
  `SendReply` goes nowhere — the requester unblocks with `B_NO_REPLY`
  (`'NONE'`, AppDefs.h), which is the tell. (A live copy via
  `BMessage orig = *message` *would* keep the reply address —
  `operator=` memcpys the header — but AddMessage is not a live copy.)
  Fix: answer synchronously under `window->Lock()` from the original
  message (`Pixel`, `Postcard` in `HandleScripting`). Both handlers now
  take the window lock on the app looper; `SavePostcard` moved to
  public for exactly that.
- **The stale-team signature hijack, again, with a twist:** after a
  failed kill sweep, two ProseJulia teams were alive and `BMessenger
  (signature)` delivered every scripted message to the OLD instance —
  every probe "failed" while the fix was actually correct. The kill
  swept `awk '{print $1}'`, which on this ps output is the command
  path, not the id. The smoke's `killapp` now extracts the first
  numeric field after the command name.
- **c = 0.7885 e^{iθ} is outside the Mandelbrot set at θ=0** (real
  interval is [−2, 0.25]): the Julia set at small θ is Cantor dust with
  no interior, so "is the centre black" is θ-dependent — the selftest's
  interior check pins θ=180°, and the smoke's centre-is-black probe
  must run paused at 180° (an animating Frame set races the pulse:
  set 180, read 202).
- macOS bash 3.2 parsed `$v°` as a variable named `v°` — keep smoke
  scripts ASCII.
