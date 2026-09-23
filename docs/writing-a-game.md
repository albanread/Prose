# Writing a game for Prose

A **game pane** is a window whose pixels are 8-bit palette indices. You write
bytes into a buffer; the Mac's GPU looks them up, scales them into the window,
composites your sprites and runs any filter you wrote. Nothing is copied
between the byte you write and the screen.

This is how to use it. [docs/game-pane.md](game-pane.md) is how it works and
why. The whole of the API is `<game/GamePane.h>`, and you link `-lgame`.

A complete example ships with the machine as
`/boot/system/data/prose-examples/07-game`, and can be built there with `make`.

## The smallest thing that works

```cpp
#include <Application.h>
#include <GamePane.h>

int
main()
{
    BApplication app("application/x-vnd.Example");
    BGamePane* pane = new BGamePane(BRect(0, 0, 639, 399), "Game", 320, 200);
    if (pane->InitCheck() != B_OK)
        return 1;

    rgb_color green = { 40, 200, 90, 255 };
    pane->SetColor(1, green);
    pane->FillRect(40, 40, 120, 80, 1);
    pane->Present();

    pane->Show();
    app.Run();
    return 0;
}
```

A 320×200 world shown in a 640×400 window, one rectangle in it. The window is
an ordinary window: it has a title bar, it moves, it goes behind other windows,
it closes. It is a `BDirectWindow` underneath, which is where it gets its
geometry, but you never touch the pointer that class hands out — see
[game-pane.md](game-pane.md) for why that pointer is not worth having.

`InitCheck()` fails with `B_DEVICE_NOT_FOUND` on a machine without the Prose
display. Check it, and say so plainly rather than drawing into nothing.

## The world

`World()` is the buffer you are drawing into, one byte a pixel, `BytesPerRow()`
apart — which is not the same as the width, so step by rows:

```cpp
uint8* row = pane->RowAt(y);
row[x] = index;
```

`Plot()`, `FillRect()`, `Clear()` and `Blit()` do the obvious things and clip
for you. `Blit()` takes an operation: `B_GAME_COPY_KEYED` is the usual one,
leaving the destination wherever the source is index 0; `B_GAME_COPY`,
`B_GAME_AND`, `B_GAME_OR` and `B_GAME_XOR` are the others.

Blitting is on the CPU, in the guest, on purpose. A byte a pixel in write-back
memory is faster to bang on here than to ask anything else about, and a 320×200
clear is about 64 KB — nothing for the machine this runs on.

The world may be bigger than what the window shows. `SetView(w, h)` says how
much is shown, `SetScroll(x, y)` says from where, and scrolling then costs one
integer a frame — no blit, no copy.

## Two planes

The world goes under the sprites. Ask for `B_GAME_PANE_FRONT_PLANE` and you
get a second one that goes over them, which is where a score belongs — an
alien flying across it should pass behind, not in front. `SetPlane()` picks
which one everything below draws into:

```cpp
pane->SetPlane(B_GAME_FRONT);
pane->Clear(0);
pane->DrawText(10, 8, "SCORE 005050", kWhite);
pane->SetPlane(B_GAME_WORLD);
```

The front plane takes its colours from the global palette only — no
per-scanline trickery, because a HUD does not want any.

## Colours

Index 0 is transparent. The desktop shows through it, or your background
shader if you have one.

- **`SetColor(index, colour)`** — the global palette, 255 colours.
- **`SetScanlineColor(row, index, colour)`** — with the
  `B_GAME_PANE_SCANLINE_PALETTE` flag, indices 1 to 15 come from the palette of
  the row they are on. A sky gradient is then 200 palette writes once, and a
  raster bar moving down the screen is one write a line — not a pixel of
  drawing either way.
- **`SetSpriteColor(palette, index, colour)`** — 63 palettes of sixteen that
  belong to sprites alone, so sprites cost the world none of its 255.

## Sprites

Define a shape once, list the ones you want each frame, present:

```cpp
int32 ship = pane->DefineSprite(pixels, 12, 12, 4);   // 4 bits a pixel
...
pane->ClearSprites();
pane->DrawSprite(ship, x, y, 2.0f, angle, 1.0f, palette);
pane->Present();
```

`pixels` is always one byte a pixel going in; at a depth of 4 the values are 0
to 15 and the kit packs them two to a byte. Position is in world coordinates —
pass `B_GAME_SPRITE_SCREEN` in `flags` for a sprite that ignores the scroll,
which is what a cursor wants.

Art does not have to be an array in your source. `DefineSprite()` also takes a
`BBitmap` or a path to anything the Translation Kit reads:

```cpp
int32 ship = pane->DefineSprite("lander.png", 4, 1);
```

At four bits the picture's colours are written into the sprite palette you name
and the sprite indexes that, so loading it is the whole of setting it up. At
eight they are matched against the global palette as it already stands and
nothing is written, which is for a sprite meant to share the world's colours.
A pixel less than half opaque becomes index 0. More distinct colours than there
is room for are matched to the nearest already taken, so a stray anti-aliased
pixel costs you a shade rather than the sprite.

Scale and rotation are the host's: each fragment is inverse-transformed into
sprite space, so a sprite turns and scales smoothly in a way a byte blitter
cannot. Up to 64 a frame.

Recolouring is a number. A hit flash, a power-up, an enemy in its harder
colours — draw the same shape with a different palette and nothing is redrawn.

## Text

```cpp
pane->SetTextFont(NULL, 10.0f);          // once
...
pane->DrawTextInView(4, 4, "SCORE 00120", kInk);
```

`SetTextFont()` renders a system font — any family, any size — once into glyph
shapes of its own, so there is no font data in your program. There are four
slots, so a title size and a score size do not mean asking for the font twice
a frame. With no `BFont` it
takes the first fixed-width family it finds, which is what a grid of characters
wants. ASCII 32 to 126.

`DrawText()` blits those glyphs into the world with everything else, so text is
part of the scene: it sits under the sprites, the overlay filters it along with
the rest, and it costs none of the 64 sprites a frame. `DrawTextInView()` is
the same thing offset by the scroll, which is what a score wants. A string is
one colour — draw it twice, a pixel apart, for a shadow.

## Shaders

Two fragment functions of your own, written in Metal Shading Language and
compiled by the host:

```cpp
pane->SetBackgroundShader(source);   // layer 0: under the world
pane->SetOverlayShader(source);      // layer 3: over everything
```

```c
float3 background(pane p)
float4 overlay(float4 colour, pane p)
```

It is `overlay` and not `filter` because MSL already has `metal::filter`.

`pane` carries where the fragment is and when — `p.uv` 0 to 1 across the view,
`p.size` that view in your own pixels, `p.time` in seconds, `p.frame` — and
three ways of looking at the pane:

| | |
|---|---|
| `float4 p.colour(float2 at)` | the finished pane, nearest |
| `float4 p.smooth(float2 at)` | the same, interpolated |
| `uint p.index(float2 at)` | the palette index there; 0 is transparent |
| `float3 p.palette(uint i)` | a global palette entry |

The background runs before there is a picture, so `colour()` and `smooth()`
are black in it; `index()` and `palette()` work in both.

`p.param(i)` reads one of sixteen floats you set with `SetShaderParam()`. That
is how a running game tells a shader anything — which of twelve skies to draw,
how hard to shake, how far through a fade it is. They take effect on the next
frame and nothing is recompiled.

The division of labour is the point. Smooth things — skies, gradients, water,
plasma — are what a shader is good at and what an indexed buffer is worst at.
Sharp things — tiles, text, sprites, anything that must land on an exact pixel
in an exact colour — are the other way round. Your program gets both in one
window and neither has to imitate the other.

Because the overlay is handed the finished pane and may resample it anywhere,
a heat haze, a reflection, a bloom, chromatic aberration or a screen curvature
is something you write, not something the system has to have thought of:

```c
float4 overlay(float4 colour, pane p)
{
    float2 d = p.uv * 2.0 - 1.0;                   // bend the glass
    float2 uv = p.uv + d * dot(d, d) * 0.012;
    float4 c = p.smooth(uv);
    float4 glow = p.colour(uv + float2(0.01, 0.0))
                + p.colour(uv - float2(0.01, 0.0));
    c.rgb += glow.rgb * 0.12;                      // and let the bright bleed
    return c;
}
```

Neither function can reach anything but its own coordinates and the pane: not
the surface pool at large, not another pane, not the host.

**When it does not compile**, the call returns `B_BAD_DATA` and
`ShaderError()` is the compiler's complaint, with line numbers that match the
source you passed. Print it. Passing `NULL` takes the layer away again.

## Frames

```cpp
pane->Present();
pane->WaitForRetrace();
```

`Present()` makes the buffer you just drew the live one and moves you on to the
next. With two or more buffers — the constructor's third argument, two by
default — the host reads one while you write the other, so nothing waits and
nothing tears. One buffer is legal and tears, which is occasionally what you
want.

`WaitForRetrace()` blocks on the host's own vsync, on a semaphore of its own so
you are not taking app_server's. It can time out; fall back to a snooze rather
than spinning.

Draw from a thread of your own, not from the window's. The window's thread has
messages to deliver.

## The built-in filter

`SetEffect()` takes `B_GAME_PANE_PLAIN`, `B_GAME_PANE_SCANLINES` or
`B_GAME_PANE_CRT`, the last being scanlines, an aperture mask, a little bloom
and light falling off at the corners. It is applied before your overlay sees
the picture, so choose `B_GAME_PANE_PLAIN` if your overlay is doing that job
itself.

## What it costs

At 320×200 with a handful of sprites, essentially nothing. The guest redraws
one byte a pixel; everything else — palette lookup, scaling, sprite transforms,
your shaders, the filter — happens on the Mac's GPU at display rate. A pane
with an overlay costs one small offscreen texture, because the overlay has to
have a finished picture to resample.

The limits worth knowing: 8 panes on the machine at once, 128 sprites a frame
over 128 shapes, 63 sprite palettes, 32 clip rectangles, 16 KB of source per
shader, 16 shader parameters, 4 font slots, and 3 world buffers.

**Galaxigans**, in the Deskbar's Games, is a whole game on all of this: 640×360,
a formation of forty over fourteen species, twelve shader backdrops, and a
tractor beam whose colour flows through the per-scanline palette without a
pixel being redrawn.

## Building it

On the machine, with the clang that ships with it:

```
clang++ -O2 -Wall -std=c++17 -o game game.cpp -lbe -lgame
```

Or cross-compile on the Mac and copy it in. `07-game` in the examples folder
has a Makefile that does the former.
