#!/usr/bin/env python3
"""Write a minimal HVIF (Haiku vector icon) and print it as rdef hex.

Haiku icons are HVIF, and the only authoring tool is Icon-O-Matic, which needs
a running Haiku. This writes the subset a simple mark needs - solid colour
styles and straight-line paths - directly, following the parser in
src/libs/icon/flat_icon/FlatIconImporter.cpp:

    'ficn'
    styles: count, then per style: type(1=solid colour), r, g, b, a
    paths:  count, then per path: flags, pointCount, then points
            (flag 8 = no curves: each coord is 1 or 2 bytes)
    shapes: count, then per shape: type(10=path source), styleIndex,
            pathCount, path indices..., flags

Coordinates live in the 0..64 icon grid. A byte under 128 encodes
value - 32.0, so 0..64 needs the two-byte form for anything above 95-32;
we always use the compact form where it fits.
"""
import struct
import sys


def coord(v):
    """Encode one coordinate the way read_coord() decodes it."""
    # compact: one byte, value - 32.0, so -32.0 .. 95.0 in whole units
    if float(v).is_integer() and -32.0 <= v <= 95.0:
        return bytes([int(v) + 32])
    # wide: 15 bits, (value / 102.0) - 128.0
    c = int(round((v + 128.0) * 102.0))
    c = max(0, min(0x7FFF, c))
    return bytes([0x80 | (c >> 8), c & 0xFF])


def style_solid(r, g, b, a=255):
    return bytes([1, r, g, b, a])


def path_polygon(points, closed=True):
    flags = 8            # PATH_FLAG_NO_CURVES
    if closed:
        flags |= 2       # PATH_FLAG_CLOSED
    out = bytes([flags, len(points)])
    for x, y in points:
        out += coord(x) + coord(y)
    return out


def shape(style_index, path_indices):
    out = bytes([10, style_index, len(path_indices)])
    out += bytes(path_indices)
    out += bytes([0])    # no flags: no transform, no hinting
    return out


def icon(styles, paths, shapes):
    data = struct.pack("<I", 0x6669636E)     # 'ficn', little-endian on disk: 6E 63 69 66
    data += bytes([len(styles)]) + b"".join(styles)
    data += bytes([len(paths)]) + b"".join(paths)
    data += bytes([len(shapes)]) + b"".join(shapes)
    return data


def rdef_hex(data, indent="\t"):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append('%s$"%s"' % (indent, "".join("%02X" % b for b in chunk)))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# The PROSE mark: the same broad-nib sweep as the wallpaper, in miniature.
#
# The Deskbar renders this into a 63x22 bitmap, so the 64x64 grid is squashed
# about 3:1 vertically. Fine detail cannot survive that - a tapered nib turns
# into a slab - so the mark is one bold stroke instead, sampled from the same
# cubic spine and pressure profile the wallpaper uses. It stays legible at 22
# pixels tall and ties the Deskbar to the desktop.
# ---------------------------------------------------------------------------

def swash(samples=44, thickness=13.0, lift=0.0):
    """Sample a cubic spine, offset it by a pressure profile, return a polygon."""
    import math
    p0, p1, p2, p3 = (2.0, 44.0), (18.0, 8.0), (44.0, 56.0), (62.0, 22.0)

    def at(t):
        u = 1 - t
        x = u*u*u*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t*t*t*p3[0]
        y = u*u*u*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t*t*t*p3[1]
        return x, y

    top, bottom = [], []
    for i in range(samples + 1):
        t = i / samples
        x, y = at(t)
        # pressure: thin in, full through the belly, thin out
        w = thickness * (0.16 + 0.84 * math.sin(math.pi * t ** 0.85) ** 1.3)
        top.append((round(x, 1), round(y - w / 2 - lift, 1)))
        bottom.append((round(x, 1), round(y + w / 2 - lift, 1)))
    return path_polygon(top + list(reversed(bottom)))


INK = style_solid(0x2A, 0x4E, 0x7C)          # the stroke
SHEEN = style_solid(0x6E, 0xA8, 0xE0)        # a lighter pass along the top

data = icon(
    styles=[INK, SHEEN],
    paths=[swash(), swash(thickness=4.0, lift=3.4)],
    shapes=[shape(0, [0]), shape(1, [1])],
)

if __name__ == "__main__":
    sys.stderr.write("%d bytes\n" % len(data))
    print(rdef_hex(data))
