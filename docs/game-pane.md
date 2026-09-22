# The game pane

A window whose pixels are 8-bit palette indices, composited by the host's GPU.

## Why not BDirectWindow

BDirectWindow is not broken on Prose. A client connects, gets four
`DirectConnected` calls, a live `bits` pointer, a row stride, and a clip list;
pixels written through that pointer do appear on screen. What it cannot do is
*present*. The pointer it hands out is app_server's front buffer, which lives
in the surface pool; the host only copies a rectangle of that pool into the
display buffer when the guest issues `PRDS_CMD_COMMIT`, and app_server issues
those only for drawing it did itself. A direct client's pixels therefore sit in
the pool until some unrelated commit happens to sweep over them — and the next
time app_server repaints that area from its back buffer they are gone.

So a direct window gives you a pointer and nothing else: no present, no vsync
that means anything, no way to say "this frame is finished". That is the whole
of what it is. A game needs the other half.

The useful half of BDirectWindow is its *geometry*: it already tells a client
where its window is on screen, in screen pixels, and hands it a fresh clip list
every time the window moves, resizes, is obscured or is hidden. That is exactly
what a host-side compositor needs to know. The game pane keeps that half and
throws away the pointer.

## Shape

    guest                                     host (Prose.app)

    BGamePane : BDirectWindow
      world buffer  (8-bit indices) ───┐
      palette       (BGRA)          ───┤  surface pool  ──► Metal: palette
      sprite list                   ───┘  (shared memory)      lookup, scroll,
      clip list from DirectConnected ──► PRDS pane commands ──► sprites, CRT
                                                                 │
    app_server's framebuffer ──► PRDS_CMD_COMMIT ──► display buffer ──► same
                                                                    drawable

The pane's pixels never touch app_server, are never copied by a CPU, and are
never converted from indices to colour in the guest. The guest writes bytes;
the host's fragment shader reads those same bytes over shared memory, looks up
the palette, and draws them into the window's rectangle on the drawable, on top
of the desktop the presenter has just drawn and clipped to the rects the direct
connection supplied.

Fullscreen is the same path with the window made full-screen: the clip list
becomes the whole screen and the presenter skips the desktop pass entirely.

## Pool layout

The pool is host memory mapped into the guest as virtio shared memory region 0.
The driver already divides it in two; panes get the rest.

| offset | size | what |
|---|---|---|
| 0 | frontMax | app_server's front buffer |
| frontMax | frontMax | app_server's back buffer |
| 2 × frontMax | the rest | the pane arena |

`frontMax` is one 3840×2160 B8G8R8X8 surface rounded to 16 KiB — 31.6 MiB — so
a 128 MiB pool leaves about 64 MiB of arena. A pane asks the driver for one
contiguous allocation and lays its own buffers, palette and sprites out inside
it; every offset on the wire is a pool offset, so the host needs no per-object
bookkeeping.

The client reaches the arena with `PRDS_CLONE_POOL`, which already exists: the
whole pool arrives read/write in the caller's address space, as it does for the
accelerant. A game can therefore see app_server's framebuffer — so could any
BDirectWindow client, and on a one-person retro machine that is not a boundary
worth a second mapping mechanism.

## Buffers and tearing

A pane allocates one, two or three world buffers. The guest draws into one and
`Present()`s its index; the host samples the index last presented and no other.
Nothing is copied and nothing is locked: the host reads buffer *n* while the
guest writes buffer *n+1*. One buffer is legal and tears, which is sometimes
what a retro program wants.

`WaitForRetrace()` blocks on the driver's retrace semaphore, released from the
host's display link — the same vsync app_server uses.

## Indices

Index 0 is transparent: the desktop shows through. Indices 1–15 come from a
*per-scanline* palette — sixteen entries for each row of the world buffer — and
16–255 from a single global palette. That is the arrangement that made 8-bit
machines look like more than they were: a raster split every line, for free,
because the palette is a texture the fragment shader indexes by row.

The palette block is 256 global BGRA entries followed by `world_height` rows of
16. With `PRDS_PANE_F_SCANLINE` clear, 1–15 come from the global palette too
and the rows are ignored.

## Scroll and overscan

The world buffer may be larger than the view. `scroll_x` / `scroll_y` pick the
top-left of the visible window, in world pixels, and the shader offsets its
sample. Scrolling costs nothing: no blit, no copy, one integer per frame.

## Sprites

Up to 64 per present. A sprite is 8-bit indexed like the world, with a palette
base added to its non-zero indices, and carries position, size, scale, rotation
and alpha. The host composites them in the fragment shader after the world,
inverse-transforming each fragment into sprite space — at retro resolutions a
64-sprite loop is nothing, and it buys rotation and sub-pixel scale that a
byte blitter cannot do.

## Blitting

In the guest, in the kit, on the CPU. The world buffer is one byte per pixel in
write-back memory: a copy, a colour-keyed copy, AND, OR, XOR and clear over a
320×200 region are memory-bandwidth operations that an M-series core does
faster than any round trip to the host would. The GPU is for what the GPU is
good at — palette lookup, scaling, rotation, filters — and the byte-banging
stays where the program is.

## Effects

Per-pane, chosen by the client, applied by the host after compositing: none,
scanlines, or a CRT (scanlines, aperture mask, a little bloom and corner
falloff). They are fragment-shader passes over the pane's rectangle only; the
desktop around it is untouched.

## Wire protocol

Four commands on the PRDS control queue, alongside SET_MODE and COMMIT.

- `PRDS_CMD_PANE_CREATE` — format, world size, stride, buffer count and stride,
  pool offsets of the buffers and the palette.
- `PRDS_CMD_PANE_CONFIG` — where it goes on the guest's screen, what part of
  the world is shown, scroll, flags, effect, and the clip list.
- `PRDS_CMD_PANE_PRESENT` — which buffer is live, and the sprite list.
- `PRDS_CMD_PANE_DESTROY`.

CONFIG is sent when the window moves; PRESENT once a frame. Both are small:
PRESENT is 32 bytes and never carries pixels.

## Driver ioctls

- `PRDS_PANE_ALLOC` — bytes in, pane id and pool offset out.
- `PRDS_PANE_CONFIGURE`, `PRDS_PANE_PRESENT`, `PRDS_PANE_FREE`.

The driver owns the arena and the pane ids, and sweeps panes whose team has
died, so a crashed game does not leave a picture on the screen.

## The kit

`BGamePane` in `libgame.so`, `<game/GamePane.h>`. It is a `BDirectWindow`, so
it is an ordinary window: it has a title bar, it moves, it goes behind other
windows, it quits. `DirectConnected` forwards the geometry; the program gets
`World()`, `Palette()`, `ScanlinePalette()`, the blitters, `Sprite()` and
`Present()`.
