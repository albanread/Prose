# ProseJulia

An animated Julia-set fractal for Prose (Haiku arm64). The constant c
rides a circle of radius 0.7885 around the origin; the set morphs
continuously between connected snowflakes (c inside the Mandelbrot set)
and drifting Cantor dust, rendered in escape-time smooth-iteration
colour.

Built on the host with the cross-compiler, tested headless in the QEMU
guest (`prosewriter/vm/run.sh` + `guest.sh`). The harness has no mouse,
so everything below is also scriptable — that is how it is tested.

## Controls (keyboard)

| Key | Action |
| --- | --- |
| space | pause / resume (resume triggers the full-res beauty pass) |
| + / − | zoom in / out (0.7× per press) |
| arrows | pan by span/10 |

Menus: **Fractal** — Pause, Reset, Palette (inferno/ocean/ember/
ultraviolet), Detail (100…1600 iterations), Speed; **File** — Postcard
PDF (512², current frame, full detail), Quit.

## Scripting surface

`hey`/`pwquery` against `application/x-vnd.prose.ProseJulia`:

| Property | Mode | Meaning |
| --- | --- | --- |
| Frame | get/set | orbit angle θ in degrees [0,360) |
| Paused | set | pause (set, not toggle); pausing runs the beauty pass |
| Palette | set | by name: inferno, ocean, ember, ultraviolet |
| Iterations | set | 16..2048, clamped |
| Zoom | set | factor (2 = twice as close) |
| Centre | set | "re im" |
| Pixel | get | "x y" → "r g b" from the rendered bitmap |
| Postcard | do | write the current frame as a PDF (data: path) |
| Activate | do | bring forward and focus the view (harness entry) |
| Quit | do | clean quit |

`Pixel` reads the *render* resolution (1/3 size while animating, full
size when paused) and pauses the app for a deterministic read.

## Build, test

```sh
cd prosejulia/app && make          # cross-builds, warning-clean
prosejulia/tests/guest-smoke.sh    # with the VM up: 10 checks
```

Selftest: `ProseJulia --selftest` — 19 checks (orbit math, interior at
θ=180°, clamps, determinism, render↔pixel agreement).

## Status

| Sprint | Scope | State |
| --- | --- | --- |
| 1 | math + animated view + menus + scripting + postcard PDF + selftest/smoke | **done 2026-09-22** — selftest 19/19, smoke 10/10 |

Owed: mouse zoom/pan on real hardware; menu-item round trips (the
handlers are smoke-covered via the scripted properties); animation
smoothness off QEMU. Details in [docs/plan.md](docs/plan.md).
