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
      palette       (BGRA)          ───┤  surface pool  ──► Metal: layer 0,
      sprite list                   ───┤  (shared memory)      palette lookup,
      layer 0 source (MSL)          ───┘                       scroll, sprites,
      clip list from DirectConnected ──► PRDS pane commands ──►     CRT
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

The palette block is 256 global BGRA entries, then `world_height` rows of 16
for the scanline palette, then 64 palettes of 16 for the sprites. With
`PRDS_PANE_F_SCANLINE` clear, 1–15 come from the global palette too and the
rows are ignored.

## Scroll and overscan

The world buffer may be larger than the view. `scroll_x` / `scroll_y` pick the
top-left of the visible window, in world pixels, and the shader offsets its
sample. Scrolling costs nothing: no blit, no copy, one integer per frame.

## Shaders of your own

Two fragment functions the program itself wrote, one under the world and one
over everything. It sends the Metal source — in its own allocation, so only an
offset crosses the queue — and the host compiles it.

    float3 background(pane p)                // layer 0, under the world
    float4 overlay(float4 colour, pane p)    // layer 3, over it

(`overlay` and not `filter`: MSL already has `metal::filter`, the sampler
enumeration, and the two are ambiguous at the call site.)

`pane` carries where the fragment is and when — `p.uv` 0 to 1 across the view,
`p.size` that view in pane pixels, `p.time`, `p.frame` — and three ways of
looking at the pane: `p.colour(at)` and `p.smooth(at)` for the finished
picture, `p.index(at)` for the raw palette index, `p.palette(i)` for a colour.

The overlay being handed the picture, and free to resample it anywhere, is what
makes a heat haze, a reflection, a bloom, chromatic aberration or a screen
curvature the program's own business rather than something this has to offer as
an option. To have something to resample, an overlaid pane is drawn into a
picture of its own first, at the pane's own resolution — so a haze moves in the
pane's pixels rather than the Mac's, and a pane with no overlay pays nothing.

Metal's standard library is there and nothing else: neither function can reach
the pool at large, the other panes or the host. Compilation is synchronous, so
the call answers yes or no; when the answer is no, the compiler's complaint is
written back into the buffer the source came from and `ShaderError()` reads it.

That is the division of labour the whole design is for. Smooth things — skies,
gradients, plasma, water — are what a shader is good at and what an indexed
buffer is worst at. Sharp things — tiles, text, sprites, anything that must land
on an exact pixel in an exact colour — are what the indexed buffer is for. A
program gets both in one window and neither has to imitate the other.

## Sprites

Up to 64 per present. A sprite carries position, size, scale, rotation and
alpha, and the host composites them in the fragment shader after the world,
inverse-transforming each fragment into sprite space — at retro resolutions a
64-sprite loop is nothing, and it buys rotation and sub-pixel scale that a byte
blitter cannot do.

A sprite is 8 or 4 bits a pixel. At four, two pixels share a byte, low nibble
first; art is written the way it is drawn, one byte a pixel, and the kit packs
it. Index 0 is transparent at either depth.

Sprites have **63 palettes of their own**, sixteen colours each, appended to the
palette block. A four-bit sprite names one of them. Palette 0 is the global
palette, which is the whole of the rule: an eight-bit sprite indexes the 256 as
before, a four-bit sprite on palette 0 takes the first fifteen — and with
per-scanline palettes on, those are the entries the row palette shadows anyway,
so palette 0 costs the world nothing.

The point is not the four kilobytes it saves. It is that recolouring a sprite —
a hit flash, a power-up, an enemy in its harder colours — becomes one number a
frame, and recolouring a group becomes sixteen writes, with no pixels touched.
That is what the eight-bit machines used their palettes for. A sprite never
reads the scanline palette, so one crossing a raster split does not change
colour halfway down.

## Text

In the guest, in the kit, out of the system's own font engine: `SetTextFont()`
renders a family and size once into glyph shapes, and `DrawText()` blits them
into the world with the same byte blitter as everything else. So text is part
of the scene rather than a layer over it — the overlay filters it, it sits
under the sprites, and it costs none of the 64 a frame. No font data in the
program, and no text in the wire protocol.

## Blitting

In the guest, in the kit, on the CPU. The world buffer is one byte per pixel in
write-back memory: a copy, a colour-keyed copy, AND, OR, XOR and clear over a
320×200 region are memory-bandwidth operations that an M-series core does
faster than any round trip to the host would. The GPU is for what the GPU is
good at — palette lookup, scaling, rotation, filters — and the byte-banging
stays where the program is.

## The built-in filter

Per-pane, chosen by the client, applied by the host after compositing and
before the overlay sees it: none, scanlines, or a CRT (scanlines, aperture mask, a little bloom and corner
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
- `PRDS_CMD_PANE_SHADER` — which slot, where its source is, and how long.

CONFIG is sent when the window moves; PRESENT once a frame. Both are small:
PRESENT is 32 bytes and never carries pixels.

## Driver ioctls

- `PRDS_PANE_ALLOC` — bytes in, pane id and pool offset out.
- `PRDS_PANE_CONFIGURE`, `PRDS_PANE_PRESENT`, `PRDS_PANE_SHADER`, `PRDS_PANE_FREE`.
- `PRDS_PANE_WAIT_RETRACE` — its own semaphore, so a game waiting for vsync
  does not eat app_server's.

The driver owns the arena and the pane ids, and sweeps panes whose team has
died, so a crashed game does not leave a picture on the screen.

## The kit

`BGamePane` in `libgame.so`, `<game/GamePane.h>`. It is a `BDirectWindow`, so
it is an ordinary window: it has a title bar, it moves, it goes behind other
windows, it quits. `DirectConnected` forwards the geometry; the program gets
`World()`, `SetColor()`, `SetScanlineColor()`, `SetSpriteColor()`, the
blitters, `SetBackgroundShader()`, `SetOverlayShader()`, `DefineSprite()`,
`DrawSprite()`, `Present()` and `WaitForRetrace()`.

[writing-a-game.md](writing-a-game.md) is the guide to using it.
