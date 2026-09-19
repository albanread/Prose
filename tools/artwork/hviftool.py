#!/usr/bin/env python3
"""hviftool: Haiku vector icons (HVIF) to and from SVG.

Haiku's icons are HVIF ("flat icons"), a compact binary vector format that
Icon-O-Matic writes. This converts between it and SVG, so icons can be drawn
with any SVG editor (or by hand) and Haiku's own artwork can be looked at and
reused. The format follows the Haiku sources:

  src/libs/icon/flat_icon/FlatIconImporter.cpp, FlatIconFormat.cpp,
  PathCommandQueue.cpp                       reading, coordinate encodings
  src/apps/icon-o-matic/import_export/flat_icon/FlatIconExporter.cpp
                                             writing (mirrored here)
  src/libs/icon/IconRenderer.cpp             gradients, visibility

  'ncif' magic, then three tables: styles (colors, gradients), paths (points
  with in/out Bezier handles) and shapes (a style, some paths, a transform,
  a level-of-detail range, transformers such as a stroke). The canvas is
  64x64 units; Haiku renders it at 16, 32, 64 pixels and more.

What HVIF can hold, and so what the SVG side supports:
  - fill with a color or a linear or radial gradient (spread: pad; a radial
    gradient's focal point is ignored), fill-opacity, opacity
  - stroke with a color or gradient: width in whole units of the shape's own
    coordinates (HVIF stores an integer), linejoin, linecap, miterlimit
  - path (all commands, arcs too), rect (rx, ry), circle, ellipse, line,
    polyline, polygon, g, transform, style="" and presentation attributes,
    gradients by id with href inheritance, gradientUnits, gradientTransform
  - fill-rule: HVIF fills non-zero; evenodd is honoured by giving nested
    subpaths alternating directions
  - HVIF only, as data- attributes on an element or a group:
      data-hvif-lod="min max"   visible from scale min to max (the icon's
                                pixel size / 64: 0.25 is 16 px; max 4 or
                                more means no upper limit)
      data-hvif-hinting="1"     snap to pixels when rendering
      data-hvif-gradient="diamond|conic|xy|sqrt-xy"  on a radialGradient
      data-hvif-transformers    a shape's transformer list, as decode wrote
                                it (contour, perspective, several strokes)

A transform that is a similarity (rotation, uniform scale, translation) is
baked into the coordinates; any other becomes the shape's transform.

usage:
  hviftool.py encode icon.svg -o icon.hvif [--rdef out.rdef]
  hviftool.py decode icon.hvif|file.rdef -o icon.svg
  hviftool.py rdef icon.svg|icon.hvif [--id 1] [--name BEOS:ICON]
  hviftool.py preview icon.svg|icon.hvif... -o sheet.png
  hviftool.py test icons-dir     round trip every HVIF file in a directory
"""

import argparse
import base64
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib


MAGIC = 0x6669636E          # 'ficn', "ncif" on disk

STYLE_SOLID_COLOR = 1
STYLE_GRADIENT = 2
STYLE_SOLID_COLOR_NO_ALPHA = 3
STYLE_SOLID_GRAY = 4
STYLE_SOLID_GRAY_NO_ALPHA = 5
SHAPE_PATH_SOURCE = 10
TRANSFORMER_AFFINE = 20
TRANSFORMER_CONTOUR = 21
TRANSFORMER_PERSPECTIVE = 22
TRANSFORMER_STROKE = 23

GRADIENT_FLAG_TRANSFORM = 1 << 1
GRADIENT_FLAG_NO_ALPHA = 1 << 2
GRADIENT_FLAG_GRAYS = 1 << 4
PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_USES_COMMANDS = 1 << 2
PATH_FLAG_NO_CURVES = 1 << 3
SHAPE_FLAG_TRANSFORM = 1 << 1
SHAPE_FLAG_HINTING = 1 << 2
SHAPE_FLAG_LOD_SCALE = 1 << 3
SHAPE_FLAG_HAS_TRANSFORMERS = 1 << 4
SHAPE_FLAG_TRANSLATION = 1 << 5

GRADIENT_TYPES = ['linear', 'circular', 'diamond', 'conic', 'xy', 'sqrt-xy']
# agg's enums
LINE_JOINS = ['miter', 'miter-revert', 'round', 'bevel', 'miter-round']
LINE_CAPS = ['butt', 'square', 'round']

IDENTITY = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


class HvifError(Exception):
    pass


# -- the model --------------------------------------------------------------------------

class Style:
    """A color (r, g, b, a), or a gradient: type index, matrix (sx, shy,
    shx, sy, tx, ty; gradient space to shape space; the base axis runs from
    (-64, 0) to (64, 0), a radial one has radius 64), stops [(0-255, rgba)]."""

    def __init__(self, color=None, gradient_type=None, matrix=None, stops=None):
        self.color = color
        self.gradient_type = gradient_type
        self.matrix = matrix or IDENTITY
        self.stops = stops or []

    def key(self):
        if self.gradient_type is None:
            return ('color', tuple(self.color))
        return ('gradient', self.gradient_type, tuple(round(v, 6) for v in self.matrix),
            tuple((o, tuple(c)) for o, c in self.stops))


class Path:
    """Points [(x, y, in_x, in_y, out_x, out_y)]: the segment from point i to
    point i + 1 is a cubic with control points out(i), in(i + 1)."""

    def __init__(self, points=None, closed=False):
        self.points = points or []
        self.closed = closed

    def key(self):
        return (self.closed, tuple(tuple(p) for p in self.points))


class Shape:
    """A style, paths, and how to draw them: matrix (None for identity),
    translation-only flag, hinting, level of detail (min, max bytes: scale
    * 63.75), transformers [(kind, params)]."""

    def __init__(self, style, paths, matrix=None, hinting=False, lod=None,
            transformers=None):
        self.style = style
        self.paths = paths
        self.matrix = matrix
        self.hinting = hinting
        self.lod = lod
        self.transformers = transformers or []


class Icon:
    def __init__(self):
        self.styles = []
        self.paths = []
        self.shapes = []


# -- the binary format -------------------------------------------------------------------

class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def u8(self):
        if self.pos >= len(self.data):
            raise HvifError('truncated at byte %d' % self.pos)
        value = self.data[self.pos]
        self.pos += 1
        return value

    def u16(self):
        return self.u8() | (self.u8() << 8)

    def u32(self):
        return self.u16() | (self.u16() << 16)

    def f32(self):
        return struct.unpack('<f', bytes(self.u8() for _ in range(4)))[0]

    def skip(self, count):
        self.pos += count

    def coord(self):
        value = self.u8()
        if value & 128:
            return (((value & 127) << 8) | self.u8()) / 102.0 - 128.0
        return float(value - 32)

    def float24(self):
        b0, b1, b2 = self.u8(), self.u8(), self.u8()
        short = (b0 << 16) | (b1 << 8) | b2
        if short == 0:
            return 0.0
        sign = (short & 0x800000) >> 23
        exponent = ((short & 0x7E0000) >> 17) - 32
        mantissa = (short & 0x01FFFF) << 6
        bits = (sign << 31) | ((exponent + 127) << 23) | mantissa
        return struct.unpack('<f', struct.pack('<I', bits))[0]

    def rgba(self, alpha, gray):
        if gray:
            g = self.u8()
            return (g, g, g, self.u8() if alpha else 255)
        r, g, b = self.u8(), self.u8(), self.u8()
        return (r, g, b, self.u8() if alpha else 255)


class Writer:
    def __init__(self):
        self.data = bytearray()

    def u8(self, value):
        if not 0 <= value <= 255:
            raise HvifError('byte out of range: %r' % value)
        self.data.append(value)

    def u32(self, value):
        self.data += struct.pack('<I', value)

    def coord(self, value):
        value = min(max(value, -128.0), 192.0)
        nearest = round(value)
        if abs(value - nearest) < 0.005 and -32 <= nearest <= 95:
            self.u8(int(nearest) + 32)
        else:
            short = int(round((value + 128.0) * 102.0))
            short = min(max(short, 0), 0x7FFF) | 0x8000
            self.u8(short >> 8)
            self.u8(short & 255)

    def float24(self, value):
        bits = struct.unpack('<I', struct.pack('<f', value))[0]
        sign = (bits & 0x80000000) >> 31
        exponent = ((bits & 0x7F800000) >> 23) - 127
        mantissa = bits & 0x007FFFFF
        if exponent >= 32 or exponent < -32:
            self.u8(0)
            self.u8(0)
            self.u8(0)
            return
        short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
        self.u8(short >> 16)
        self.u8((short >> 8) & 0xFF)
        self.u8(short & 0xFF)


def decode(data):
    """An Icon from HVIF data, read as FlatIconImporter reads it."""
    r = Reader(data)
    if r.u32() != MAGIC:
        raise HvifError('not an HVIF icon (no "ncif")')
    icon = Icon()
    style_map = {}
    for i in range(r.u8()):
        kind = r.u8()
        if kind == STYLE_SOLID_COLOR:
            style = Style(r.rgba(True, False))
        elif kind == STYLE_SOLID_COLOR_NO_ALPHA:
            style = Style(r.rgba(False, False))
        elif kind == STYLE_SOLID_GRAY:
            style = Style(r.rgba(True, True))
        elif kind == STYLE_SOLID_GRAY_NO_ALPHA:
            style = Style(r.rgba(False, True))
        elif kind == STYLE_GRADIENT:
            gradient_type, flags, count = r.u8(), r.u8(), r.u8()
            matrix = IDENTITY
            if flags & GRADIENT_FLAG_TRANSFORM:
                matrix = tuple(r.float24() for _ in range(6))
            alpha = not flags & GRADIENT_FLAG_NO_ALPHA
            gray = bool(flags & GRADIENT_FLAG_GRAYS)
            stops = []
            for _ in range(count):
                offset = r.u8()
                stops.append((offset, r.rgba(alpha, gray)))
            # Gradient::AddColor() keeps the stops sorted by offset
            stops.sort(key=lambda stop: stop[0])
            style = Style(None, gradient_type, matrix, stops)
        else:
            r.skip(r.u16())
            continue
        style_map[i] = len(icon.styles)
        icon.styles.append(style)
    for i in range(r.u8()):
        flags, count = r.u8(), r.u8()
        points = []
        if flags & PATH_FLAG_NO_CURVES:
            for _ in range(count):
                x, y = r.coord(), r.coord()
                points.append((x, y, x, y, x, y))
        elif flags & PATH_FLAG_USES_COMMANDS:
            commands = [r.u8() for _ in range((count + 3) // 4)]
            last = (0.0, 0.0)
            for p in range(count):
                command = (commands[p // 4] >> (2 * (p % 4))) & 3
                if command == 0:
                    x, y = r.coord(), last[1]
                    points.append((x, y, x, y, x, y))
                elif command == 1:
                    x, y = last[0], r.coord()
                    points.append((x, y, x, y, x, y))
                elif command == 2:
                    x, y = r.coord(), r.coord()
                    points.append((x, y, x, y, x, y))
                else:
                    points.append(tuple(r.coord() for _ in range(6)))
                last = points[-1][:2]
        else:
            for _ in range(count):
                points.append(tuple(r.coord() for _ in range(6)))
        icon.paths.append(Path(clean_up(points), bool(flags & PATH_FLAG_CLOSED)))
    for _ in range(r.u8()):
        kind = r.u8()
        if kind != SHAPE_PATH_SOURCE:
            r.skip(r.u16())
            continue
        style_index = r.u8()
        paths = [r.u8() for _ in range(r.u8())]
        flags = r.u8()
        matrix = None
        if flags & SHAPE_FLAG_TRANSFORM:
            matrix = tuple(r.float24() for _ in range(6))
        elif flags & SHAPE_FLAG_TRANSLATION:
            matrix = (1.0, 0.0, 0.0, 1.0, r.coord(), r.coord())
        lod = None
        if flags & SHAPE_FLAG_LOD_SCALE:
            lod = (r.u8(), r.u8())
        transformers = []
        if flags & SHAPE_FLAG_HAS_TRANSFORMERS:
            for _ in range(r.u8()):
                kind = r.u8()
                if kind == TRANSFORMER_AFFINE:
                    # the importer reads four-byte floats here (the exporter
                    # writes 24-bit ones; hviftool writes none)
                    transformers.append(('affine', tuple(r.f32() for _ in range(6))))
                elif kind == TRANSFORMER_CONTOUR:
                    transformers.append(('contour', (r.u8() - 128, r.u8(), r.u8())))
                elif kind == TRANSFORMER_PERSPECTIVE:
                    transformers.append(('perspective', tuple(r.float24() for _ in range(9))))
                elif kind == TRANSFORMER_STROKE:
                    width, options, miter = r.u8() - 128, r.u8(), r.u8()
                    transformers.append(('stroke', (width, options & 15, options >> 4, miter)))
                else:
                    r.skip(r.u16())
        if style_index not in style_map:
            raise HvifError('shape uses a style that is not there: %d' % style_index)
        paths = [p for p in paths if p < len(icon.paths)]
        icon.shapes.append(Shape(style_map[style_index], paths, matrix,
            bool(flags & SHAPE_FLAG_HINTING), lod, transformers))
    return icon


def clean_up(points):
    """VectorPath::CleanUp() as the importer runs it (before the path is
    closed): of two equal points joined by a straight segment, the first
    goes, and the second takes its in handle."""
    points = [list(p) for p in points]
    i = 0
    while i < len(points):
        if i > 0:
            a, b = points[i - 1], points[i]
            if a[:2] == b[:2] and a[:2] == a[4:6] and b[:2] == b[2:4]:
                handle = a[2:4]
                del points[i - 1]
                i -= 1
                points[i][2:4] = handle
        i += 1
    return [tuple(p) for p in points]


def is_line_point(p):
    return p[0] == p[2] == p[4] and p[1] == p[3] == p[5]


def encode(icon):
    """HVIF data for an Icon, written as FlatIconExporter writes it."""
    if max(len(icon.styles), len(icon.paths), len(icon.shapes)) > 255:
        raise HvifError('more than 255 styles, paths or shapes')
    w = Writer()
    w.u32(MAGIC)
    w.u8(len(icon.styles))
    for style in icon.styles:
        if style.gradient_type is not None:
            w.u8(STYLE_GRADIENT)
            alpha = any(c[3] < 255 for _, c in style.stops)
            gray = all(c[0] == c[1] == c[2] for _, c in style.stops)
            flags = 0
            identity = all(abs(a - b) < 1e-7 for a, b in zip(style.matrix, IDENTITY))
            if not identity:
                flags |= GRADIENT_FLAG_TRANSFORM
            if not alpha:
                flags |= GRADIENT_FLAG_NO_ALPHA
            if gray:
                flags |= GRADIENT_FLAG_GRAYS
            if len(style.stops) > 255:
                raise HvifError('more than 255 gradient stops')
            w.u8(style.gradient_type)
            w.u8(flags)
            w.u8(len(style.stops))
            if not identity:
                for value in style.matrix:
                    w.float24(value)
            for offset, c in style.stops:
                w.u8(offset)
                write_color(w, c, alpha, gray)
        else:
            c = style.color
            gray = c[0] == c[1] == c[2]
            alpha = c[3] < 255
            w.u8({(True, True): STYLE_SOLID_GRAY, (True, False): STYLE_SOLID_GRAY_NO_ALPHA,
                (False, True): STYLE_SOLID_COLOR,
                (False, False): STYLE_SOLID_COLOR_NO_ALPHA}[(gray, alpha)])
            write_color(w, c, alpha, gray)
    w.u8(len(icon.paths))
    for path in icon.paths:
        points = path.points
        if len(points) > 255:
            raise HvifError('a path with more than 255 points')
        straight = line = curve = 0
        last = (0.0, 0.0)
        for p in points:
            if is_line_point(p):
                if p[0] == last[0] or p[1] == last[1]:
                    straight += 1
                else:
                    line += 1
            else:
                curve += 1
            last = p[:2]
        flags = PATH_FLAG_CLOSED if path.closed else 0
        if len(points) + straight * 2 + line * 4 + curve * 12 < len(points) * 12:
            flags |= PATH_FLAG_NO_CURVES if curve == 0 else PATH_FLAG_USES_COMMANDS
        w.u8(flags)
        w.u8(len(points))
        if flags & PATH_FLAG_NO_CURVES:
            for p in points:
                w.coord(p[0])
                w.coord(p[1])
        elif flags & PATH_FLAG_USES_COMMANDS:
            commands = []
            coords = Writer()
            last = (0.0, 0.0)
            for p in points:
                if is_line_point(p):
                    if p[0] == last[0]:
                        commands.append(1)
                        coords.coord(p[1])
                    elif p[1] == last[1]:
                        commands.append(0)
                        coords.coord(p[0])
                    else:
                        commands.append(2)
                        coords.coord(p[0])
                        coords.coord(p[1])
                else:
                    commands.append(3)
                    for value in p:
                        coords.coord(value)
                last = p[:2]
            for i in range(0, len(commands), 4):
                byte = 0
                for j, command in enumerate(commands[i:i + 4]):
                    byte |= command << (2 * j)
                w.u8(byte)
            w.data += coords.data
        else:
            for p in points:
                for value in p:
                    w.coord(value)
    w.u8(len(icon.shapes))
    for shape in icon.shapes:
        if len(shape.paths) > 255 or len(shape.transformers) > 255:
            raise HvifError('a shape with more than 255 paths or transformers')
        w.u8(SHAPE_PATH_SOURCE)
        w.u8(shape.style)
        w.u8(len(shape.paths))
        for index in shape.paths:
            w.u8(index)
        flags = 0
        m = shape.matrix
        if m is not None and any(abs(a - b) > 1e-7 for a, b in zip(m, IDENTITY)):
            translation_only = all(abs(a - b) < 1e-7 for a, b in zip(m[:4], IDENTITY[:4]))
            flags |= SHAPE_FLAG_TRANSLATION if translation_only else SHAPE_FLAG_TRANSFORM
        else:
            m = None
        if shape.hinting:
            flags |= SHAPE_FLAG_HINTING
        if shape.lod is not None and tuple(shape.lod) != (0, 255):
            flags |= SHAPE_FLAG_LOD_SCALE
        if shape.transformers:
            flags |= SHAPE_FLAG_HAS_TRANSFORMERS
        w.u8(flags)
        if flags & SHAPE_FLAG_TRANSFORM:
            for value in m:
                w.float24(value)
        elif flags & SHAPE_FLAG_TRANSLATION:
            w.coord(m[4])
            w.coord(m[5])
        if flags & SHAPE_FLAG_LOD_SCALE:
            w.u8(shape.lod[0])
            w.u8(shape.lod[1])
        if shape.transformers:
            w.u8(len(shape.transformers))
            for kind, params in shape.transformers:
                if kind == 'contour':
                    w.u8(TRANSFORMER_CONTOUR)
                    w.u8(params[0] + 128)
                    w.u8(params[1])
                    w.u8(params[2])
                elif kind == 'perspective':
                    w.u8(TRANSFORMER_PERSPECTIVE)
                    for value in params:
                        w.float24(value)
                elif kind == 'stroke':
                    w.u8(TRANSFORMER_STROKE)
                    w.u8(params[0] + 128)
                    w.u8(params[1] | (params[2] << 4))
                    w.u8(params[3])
                else:
                    raise HvifError('cannot write a %s transformer' % kind)
    return bytes(w.data)


def write_color(w, c, alpha, gray):
    if gray:
        w.u8(c[0])
    else:
        w.u8(c[0])
        w.u8(c[1])
        w.u8(c[2])
    if alpha:
        w.u8(c[3])


# -- geometry ----------------------------------------------------------------------------

def multiply(a, b):
    """The matrix that applies b, then a (agg order: sx, shy, shx, sy, tx, ty)."""
    return (a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
        a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
        a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5])


def apply(m, x, y):
    return (m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5])


def invert(m):
    det = m[0] * m[3] - m[1] * m[2]
    if abs(det) < 1e-12:
        raise HvifError('a transform that cannot be inverted')
    a, b, c, d = m[3] / det, -m[1] / det, -m[2] / det, m[0] / det
    return (a, b, c, d, -(a * m[4] + c * m[5]), -(b * m[4] + d * m[5]))


def similarity_scale(m):
    """The scale of a rotation + uniform scale + translation, or None."""
    sx = math.hypot(m[0], m[1])
    sy = math.hypot(m[2], m[3])
    if abs(sx - sy) > 1e-6 * max(sx, 1) or abs(m[0] * m[2] + m[1] * m[3]) > 1e-6 * max(sx * sy, 1):
        return None
    return sx


def is_identity(m):
    return all(abs(a - b) < 1e-9 for a, b in zip(m, IDENTITY))


def transform_path(path, m):
    points = []
    for p in path.points:
        x, y = apply(m, p[0], p[1])
        ix, iy = apply(m, p[2], p[3])
        ox, oy = apply(m, p[4], p[5])
        points.append((x, y, ix, iy, ox, oy))
    return Path(points, path.closed)


def reverse_path(path):
    return Path([(p[0], p[1], p[4], p[5], p[2], p[3]) for p in reversed(path.points)],
        path.closed)


def bezier(p0, p1, p2, p3, t):
    u = 1 - t
    return (u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
        u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1])


def flatten(path, steps=8):
    """The path as a polygon."""
    pts = path.points
    out = []
    count = len(pts) if path.closed else len(pts) - 1
    if not pts:
        return out
    out.append(pts[0][:2])
    for i in range(count):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        if (a[4], a[5]) == (a[0], a[1]) and (b[2], b[3]) == (b[0], b[1]):
            out.append(b[:2])
            continue
        for s in range(1, steps + 1):
            out.append(bezier(a[:2], a[4:6], b[2:4], b[:2], s / steps))
    return out


def signed_area(polygon):
    area = 0.0
    for i in range(len(polygon)):
        x0, y0 = polygon[i]
        x1, y1 = polygon[(i + 1) % len(polygon)]
        area += x0 * y1 - x1 * y0
    return area / 2


def inside(point, polygon):
    x, y = point
    result = False
    for i in range(len(polygon)):
        x0, y0 = polygon[i]
        x1, y1 = polygon[i - 1]
        if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0) + x0:
            result = not result
    return result


def bounds(paths):
    xs, ys = [], []
    for path in paths:
        for x, y in flatten(path, 16):
            xs.append(x)
            ys.append(y)
    if not xs:
        return (0, 0, 0, 0)
    return (min(xs), min(ys), max(xs), max(ys))


# -- SVG in ------------------------------------------------------------------------------

NAMED_COLORS = {
    'black': (0, 0, 0), 'white': (255, 255, 255), 'red': (255, 0, 0), 'green': (0, 128, 0),
    'lime': (0, 255, 0), 'blue': (0, 0, 255), 'yellow': (255, 255, 0), 'cyan': (0, 255, 255),
    'aqua': (0, 255, 255), 'magenta': (255, 0, 255), 'fuchsia': (255, 0, 255),
    'gray': (128, 128, 128), 'grey': (128, 128, 128), 'silver': (192, 192, 192),
    'maroon': (128, 0, 0), 'olive': (128, 128, 0), 'navy': (0, 0, 128),
    'purple': (128, 0, 128), 'teal': (0, 128, 128), 'orange': (255, 165, 0),
    'brown': (165, 42, 42), 'pink': (255, 192, 203), 'gold': (255, 215, 0),
}

INHERITED = ('fill', 'fill-opacity', 'fill-rule', 'stroke', 'stroke-width', 'stroke-opacity',
    'stroke-linejoin', 'stroke-linecap', 'stroke-miterlimit', 'data-hvif-lod',
    'data-hvif-hinting', 'visibility')

NUMBER = r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?'


def local(tag):
    return tag.rsplit('}', 1)[-1]


def numbers(text):
    return [float(v) for v in re.findall(NUMBER, text or '')]


def parse_color(text):
    """(r, g, b) or None for none; 'url' ids come back as ('url', id)."""
    text = (text or '').strip()
    if not text or text == 'none' or text == 'transparent':
        return None
    m = re.match(r'url\(\s*#([^)\s]+)\s*\)', text)
    if m:
        return ('url', m.group(1))
    if text.startswith('#'):
        h = text[1:]
        if len(h) in (3, 4):
            h = ''.join(c * 2 for c in h)
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    m = re.match(r'rgba?\(([^)]*)\)', text)
    if m:
        parts = [p.strip() for p in m.group(1).split(',')]
        return tuple(int(round(float(p[:-1]) * 2.55)) if p.endswith('%') else int(float(p))
            for p in parts[:3])
    if text == 'currentColor':
        return (0, 0, 0)
    if text.lower() in NAMED_COLORS:
        return NAMED_COLORS[text.lower()]
    raise HvifError('a color hviftool does not know: %s' % text)


def parse_transform(text):
    m = IDENTITY
    for name, args in re.findall(r'(\w+)\s*\(([^)]*)\)', text or ''):
        a = numbers(args)
        if name == 'matrix':
            t = tuple(a[:6])
        elif name == 'translate':
            t = (1, 0, 0, 1, a[0], a[1] if len(a) > 1 else 0)
        elif name == 'scale':
            t = (a[0], 0, 0, a[1] if len(a) > 1 else a[0], 0, 0)
        elif name == 'rotate':
            r = math.radians(a[0])
            t = (math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0)
            if len(a) >= 3:
                t = multiply((1, 0, 0, 1, a[1], a[2]), multiply(t, (1, 0, 0, 1, -a[1], -a[2])))
        elif name == 'skewX':
            t = (1, 0, math.tan(math.radians(a[0])), 1, 0, 0)
        elif name == 'skewY':
            t = (1, math.tan(math.radians(a[0])), 0, 1, 0, 0)
        else:
            raise HvifError('a transform hviftool does not know: %s' % name)
        m = multiply(m, t)
    return m


def element_style(element, parent, names=INHERITED + ('opacity', 'display')):
    style = dict(parent)
    for name in names:
        if element.get(name) is not None:
            style[name] = element.get(name)
    for declaration in (element.get('style') or '').split(';'):
        if ':' in declaration:
            name, value = declaration.split(':', 1)
            style[name.strip()] = value.strip()
    if 'data-hvif-lod' in parent and element.get('data-hvif-lod') is not None:
        # visible where both ranges say so
        outer = (numbers(parent['data-hvif-lod']) + [4.0])[:2]
        inner = (numbers(element.get('data-hvif-lod')) + [4.0])[:2]
        top = lambda v: 4.0 if v >= 4.0 else v
        style['data-hvif-lod'] = '%s %s' % (fmt(max(outer[0], inner[0])),
            fmt(min(top(outer[1]), top(inner[1]))))
    return style


class PathBuilder:
    """SVG path data to HVIF paths."""

    def __init__(self):
        self.paths = []
        self.current = None
        self.start = (0.0, 0.0)
        self.pos = (0.0, 0.0)

    def move(self, x, y):
        self.finish(False)
        self.current = [[x, y, x, y, x, y]]
        self.start = self.pos = (x, y)

    def line(self, x, y):
        if self.current is None:
            self.move(*self.pos)
        self.current.append([x, y, x, y, x, y])
        self.pos = (x, y)

    def cubic(self, x1, y1, x2, y2, x, y):
        if self.current is None:
            self.move(*self.pos)
        last = self.current[-1]
        last[4], last[5] = x1, y1
        self.current.append([x, y, x2, y2, x, y])
        self.pos = (x, y)

    def close(self):
        if self.current is None:
            return
        points = self.current
        if len(points) > 1 and abs(points[-1][0] - points[0][0]) < 1e-6 \
                and abs(points[-1][1] - points[0][1]) < 1e-6:
            # back at the start: that point is the first one
            points[0][2], points[0][3] = points[-1][2], points[-1][3]
            points.pop()
        self.finish(True)
        self.pos = self.start

    def finish(self, closed):
        if self.current and len(self.current) > 1:
            self.paths.append(Path([tuple(p) for p in self.current], closed))
        self.current = None


def arc_to_cubics(x0, y0, rx, ry, angle, large, sweep, x, y):
    """Bezier segments [(c1, c2, end)] for an SVG elliptical arc."""
    if (x0, y0) == (x, y):
        return []
    if rx == 0 or ry == 0:
        return [((x0, y0), (x, y), (x, y))]
    rx, ry = abs(rx), abs(ry)
    phi = math.radians(angle)
    cos, sin = math.cos(phi), math.sin(phi)
    dx, dy = (x0 - x) / 2, (y0 - y) / 2
    x1 = cos * dx + sin * dy
    y1 = -sin * dx + cos * dy
    scale = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
    if scale > 1:
        rx *= math.sqrt(scale)
        ry *= math.sqrt(scale)
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    factor = math.sqrt(max(num / den, 0)) if den else 0
    if large == sweep:
        factor = -factor
    cx1 = factor * rx * y1 / ry
    cy1 = -factor * ry * x1 / rx
    cx = cos * cx1 - sin * cy1 + (x0 + x) / 2
    cy = sin * cx1 + cos * cy1 + (y0 + y) / 2

    def angle_of(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    theta = angle_of(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    delta = angle_of((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry)
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    elif sweep and delta < 0:
        delta += 2 * math.pi
    count = max(1, int(math.ceil(abs(delta) / (math.pi / 2) - 1e-9)))
    step = delta / count
    k = 4 / 3 * math.tan(step / 4)
    out = []

    def point(t):
        return (cx + rx * math.cos(t) * cos - ry * math.sin(t) * sin,
            cy + rx * math.cos(t) * sin + ry * math.sin(t) * cos)

    def deriv(t):
        return (-rx * math.sin(t) * cos - ry * math.cos(t) * sin,
            -rx * math.sin(t) * sin + ry * math.cos(t) * cos)

    t = theta
    for _ in range(count):
        p1, d1 = point(t), deriv(t)
        p2, d2 = point(t + step), deriv(t + step)
        out.append(((p1[0] + k * d1[0], p1[1] + k * d1[1]),
            (p2[0] - k * d2[0], p2[1] - k * d2[1]), p2))
        t += step
    out[-1] = (out[-1][0], out[-1][1], (x, y))
    return out


def parse_path_data(d):
    builder = PathBuilder()
    tokens = re.findall(r'[MmLlHhVvCcSsQqTtAaZz]|' + NUMBER, d or '')
    i = 0
    command = None
    last_control = None
    last_quad = None
    while i < len(tokens):
        if re.match(r'[A-Za-z]', tokens[i]):
            command = tokens[i]
            i += 1
            if command in 'Zz':
                builder.close()
                last_control = last_quad = None
                command = None
                continue
        if command is None:
            raise HvifError('path data does not start with a command')
        rel = command.islower()
        c = command.upper()
        ox, oy = builder.pos if rel else (0.0, 0.0)
        counts = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6, 'S': 4, 'Q': 4, 'T': 2, 'A': 7}
        args = [float(v) for v in tokens[i:i + counts[c]]]
        if len(args) < counts[c]:
            raise HvifError('path data ends in the middle of a command')
        i += counts[c]
        control, quad = None, None
        if c == 'M':
            builder.move(ox + args[0], oy + args[1])
            command = 'l' if rel else 'L'
        elif c == 'L':
            builder.line(ox + args[0], oy + args[1])
        elif c == 'H':
            builder.line(ox + args[0], builder.pos[1])
        elif c == 'V':
            builder.line(builder.pos[0], oy + args[0])
        elif c == 'C':
            x1, y1, x2, y2, x, y = (ox + args[0], oy + args[1], ox + args[2], oy + args[3],
                ox + args[4], oy + args[5])
            builder.cubic(x1, y1, x2, y2, x, y)
            control = (x2, y2)
        elif c == 'S':
            px, py = builder.pos
            x1, y1 = (2 * px - last_control[0], 2 * py - last_control[1]) if last_control \
                else (px, py)
            x2, y2, x, y = ox + args[0], oy + args[1], ox + args[2], oy + args[3]
            builder.cubic(x1, y1, x2, y2, x, y)
            control = (x2, y2)
        elif c in 'QT':
            px, py = builder.pos
            if c == 'Q':
                qx, qy, x, y = ox + args[0], oy + args[1], ox + args[2], oy + args[3]
            else:
                qx, qy = (2 * px - last_quad[0], 2 * py - last_quad[1]) if last_quad else (px, py)
                x, y = ox + args[0], oy + args[1]
            builder.cubic(px + 2 / 3 * (qx - px), py + 2 / 3 * (qy - py),
                x + 2 / 3 * (qx - x), y + 2 / 3 * (qy - y), x, y)
            quad = (qx, qy)
        elif c == 'A':
            x, y = ox + args[5], oy + args[6]
            for c1, c2, end in arc_to_cubics(builder.pos[0], builder.pos[1], args[0], args[1],
                    args[2], bool(args[3]), bool(args[4]), x, y):
                builder.cubic(c1[0], c1[1], c2[0], c2[1], end[0], end[1])
        last_control, last_quad = control, quad
    builder.finish(False)
    return builder.paths


KAPPA = 4 * (math.sqrt(2) - 1) / 3


def ellipse_path(cx, cy, rx, ry):
    k = KAPPA
    d = ('M %r %r C %r %r %r %r %r %r C %r %r %r %r %r %r C %r %r %r %r %r %r '
        'C %r %r %r %r %r %r Z') % (cx + rx, cy,
        cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry,
        cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy,
        cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry,
        cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy)
    return parse_path_data(d)


def element_paths(element):
    tag = local(element.tag)
    g = lambda name, default=0.0: float(numbers(element.get(name, str(default)))[0]) \
        if numbers(element.get(name, str(default))) else default
    if tag == 'path':
        return parse_path_data(element.get('d'))
    if tag == 'rect':
        x, y, w, h = g('x'), g('y'), g('width'), g('height')
        rx, ry = element.get('rx'), element.get('ry')
        rx = g('rx') if rx is not None else (g('ry') if ry is not None else 0.0)
        ry = g('ry') if ry is not None else rx
        rx, ry = min(rx, w / 2), min(ry, h / 2)
        if rx <= 0 or ry <= 0:
            return parse_path_data('M %r %r H %r V %r H %r Z' % (x, y, x + w, y + h, x))
        k = KAPPA
        return parse_path_data(
            'M %r %r H %r C %r %r %r %r %r %r V %r C %r %r %r %r %r %r H %r '
            'C %r %r %r %r %r %r V %r C %r %r %r %r %r %r Z' % (
            x + rx, y, x + w - rx,
            x + w - rx + k * rx, y, x + w, y + ry - k * ry, x + w, y + ry,
            y + h - ry,
            x + w, y + h - ry + k * ry, x + w - rx + k * rx, y + h, x + w - rx, y + h,
            x + rx,
            x + rx - k * rx, y + h, x, y + h - ry + k * ry, x, y + h - ry,
            y + ry,
            x, y + ry - k * ry, x + rx - k * rx, y, x + rx, y))
    if tag == 'circle':
        return ellipse_path(g('cx'), g('cy'), g('r'), g('r'))
    if tag == 'ellipse':
        return ellipse_path(g('cx'), g('cy'), g('rx'), g('ry'))
    if tag == 'line':
        return parse_path_data('M %r %r L %r %r' % (g('x1'), g('y1'), g('x2'), g('y2')))
    if tag in ('polyline', 'polygon'):
        values = numbers(element.get('points'))
        pairs = ['%r %r' % (values[i], values[i + 1]) for i in range(0, len(values) - 1, 2)]
        if not pairs:
            return []
        return parse_path_data('M ' + ' L '.join(pairs) + (' Z' if tag == 'polygon' else ''))
    return []


def fix_winding(paths):
    """Directions for fill-rule evenodd: a subpath inside an odd number of
    others runs against the outermost ones."""
    polygons = [flatten(p) for p in paths]
    result = []
    for i, path in enumerate(paths):
        if not path.closed or len(polygons[i]) < 3:
            result.append(path)
            continue
        probe = polygons[i][0]
        depth = sum(1 for j, other in enumerate(polygons)
            if j != i and len(other) > 2 and paths[j].closed and inside(probe, other))
        positive = signed_area(polygons[i]) > 0
        result.append(path if positive == (depth % 2 == 0) else reverse_path(path))
    return result


class SvgReader:
    def __init__(self, root, name):
        self.root = root
        self.name = name
        self.icon = Icon()
        self.styles = {}
        self.paths = {}
        self.gradients = {}
        for element in root.iter():
            if local(element.tag) in ('linearGradient', 'radialGradient') and element.get('id'):
                self.gradients[element.get('id')] = element

    def warn(self, text):
        sys.stderr.write('hviftool: %s: %s\n' % (self.name, text))

    def read(self):
        vb = numbers(self.root.get('viewBox'))
        if len(vb) == 4:
            x, y, w, h = vb
        else:
            x, y = 0.0, 0.0
            w = numbers(self.root.get('width', '64'))[0]
            h = numbers(self.root.get('height', '64'))[0]
        scale = 64.0 / max(w, h)
        ctm = multiply((scale, 0, 0, scale, 0, 0), (1, 0, 0, 1, -x, -y))
        self.walk(self.root, ctm, {'fill': 'black'}, 1.0)
        return self.icon

    def walk(self, element, ctm, parent_style, opacity):
        for child in element:
            tag = local(child.tag)
            if tag in ('defs', 'title', 'desc', 'metadata', 'linearGradient',
                    'radialGradient', 'style'):
                continue
            style = element_style(child, parent_style)
            if style.get('display') == 'none':
                continue
            m = multiply(ctm, parse_transform(child.get('transform')))
            child_opacity = opacity * float(style.pop('opacity', 1.0))
            if tag in ('g', 'svg', 'a', 'switch'):
                self.walk(child, m, style, child_opacity)
            elif tag in ('path', 'rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon'):
                if style.get('visibility') not in (None, 'visible'):
                    continue
                self.add_element(child, m, style, child_opacity)
            elif tag == 'use':
                self.warn('<use> is not supported; left out')
            elif not tag.startswith('namedview') and tag not in ('text',):
                pass

    def add_element(self, element, ctm, style, opacity):
        paths = element_paths(element)
        if not paths:
            return
        if style.get('fill-rule') == 'evenodd':
            paths = fix_winding(paths)
        lod = None
        if style.get('data-hvif-lod'):
            lo, hi = (numbers(style['data-hvif-lod']) + [4.0])[:2]
            lod = (int(lo * 63.75 + 0.5), min(255, int(hi * 63.75 + 0.5)))
        hinting = style.get('data-hvif-hinting') in ('1', 'true', 'yes')
        transformers_text = element.get('data-hvif-transformers')
        fill = parse_color(style.get('fill'))
        stroke = parse_color(style.get('stroke'))
        if transformers_text:
            # decode's own record of the shape: its paint is in fill or stroke
            paint = stroke if fill is None else fill
            alpha = float(style.get('stroke-opacity' if fill is None else 'fill-opacity', 1))
            self.add_shape(paths, ctm, paint, alpha * opacity, hinting, lod,
                parse_transformers(transformers_text), None)
            return
        if fill is not None:
            self.add_shape(paths, ctm, fill, float(style.get('fill-opacity', 1)) * opacity,
                hinting, lod, [], None)
        width = numbers(style.get('stroke-width', '1'))
        width = width[0] if width else 1.0
        if stroke is not None and width > 0:
            join = LINE_JOINS.index(style.get('stroke-linejoin', 'miter')) \
                if style.get('stroke-linejoin', 'miter') in LINE_JOINS else 0
            cap = LINE_CAPS.index(style.get('stroke-linecap', 'butt')) \
                if style.get('stroke-linecap', 'butt') in LINE_CAPS else 0
            miter = numbers(style.get('stroke-miterlimit', '4'))
            self.add_shape(paths, ctm, stroke,
                float(style.get('stroke-opacity', 1)) * opacity, hinting, lod, [],
                (width, join, cap, int(miter[0]) if miter else 4))

    def add_shape(self, paths, ctm, paint, alpha, hinting, lod, transformers, stroke):
        box = bounds(paths)
        # bake the transform into the coordinates when it is a similarity and
        # a stroke stays a whole number of units wide; keep it otherwise
        scale = similarity_scale(ctm)
        bake = scale is not None
        if bake and stroke and abs(stroke[0] * scale - round(stroke[0] * scale)) > 0.01:
            bake = False
        for kind, params in transformers:
            if kind == 'stroke' and bake and abs(params[0] * scale - round(params[0] * scale)) > 0.01:
                bake = False
        shape_matrix = None
        local = IDENTITY
        if bake:
            local = ctm
            paths = [transform_path(p, ctm) for p in paths]
            transformers = [(k, (int(round(p[0] * scale)),) + tuple(p[1:])) if k == 'stroke'
                else (k, p) for k, p in transformers]
            if stroke:
                stroke = (stroke[0] * scale,) + stroke[1:]
        elif not is_identity(ctm):
            shape_matrix = ctm
        if stroke:
            width = int(round(stroke[0]))
            if abs(stroke[0] - width) > 0.01:
                self.warn('stroke width %.3g is not a whole number of units: %d' % (stroke[0], width))
            transformers = [('stroke', (max(width, 1), stroke[1], stroke[2], stroke[3]))]
        style = self.make_style(paint, alpha, box, local)
        if style is None:
            return
        indices = []
        for path in paths:
            key = path.key()
            if key not in self.paths:
                self.paths[key] = len(self.icon.paths)
                self.icon.paths.append(path)
            indices.append(self.paths[key])
        self.icon.shapes.append(Shape(style, indices, shape_matrix, hinting, lod, transformers))

    def make_style(self, paint, alpha, box, baked):
        if paint is None:
            return None
        if paint[0] == 'url':
            style = self.gradient_style(paint[1], alpha, box, baked)
            if style is None:
                return None
        else:
            style = Style(tuple(paint) + (int(round(255 * max(0, min(1, alpha)))),))
        key = style.key()
        if key not in self.styles:
            self.styles[key] = len(self.icon.styles)
            self.icon.styles.append(style)
        return self.styles[key]

    def gradient_attributes(self, gradient_id):
        """A gradient's attributes and stops, href chain included."""
        attributes, stops, seen = {}, None, set()
        element = self.gradients.get(gradient_id)
        while element is not None and element.get('id') not in seen:
            seen.add(element.get('id'))
            for name, value in element.attrib.items():
                attributes.setdefault(local(name), value)
            if stops is None:
                found = [s for s in element if local(s.tag) == 'stop']
                if found:
                    stops = found
            href = element.get('href') or element.get('{http://www.w3.org/1999/xlink}href')
            element = self.gradients.get(href[1:]) if href and href.startswith('#') else None
        return attributes, stops or []

    def gradient_style(self, gradient_id, alpha, box, baked):
        if gradient_id not in self.gradients:
            self.warn('no gradient %s; left out' % gradient_id)
            return None
        element = self.gradients[gradient_id]
        attributes, stop_elements = self.gradient_attributes(gradient_id)
        units = attributes.get('gradientUnits', 'objectBoundingBox')
        bbox_units = units == 'objectBoundingBox'

        def value(name, default):
            text = attributes.get(name)
            if text is None:
                return default
            text = text.strip()
            if text.endswith('%'):
                return float(text[:-1]) / 100 * (1 if bbox_units else 64)
            return float(text)

        linear = local(element.tag) == 'linearGradient'
        if linear:
            x1, y1 = value('x1', 0.0), value('y1', 0.0)
            x2, y2 = value('x2', 1.0 if bbox_units else 64.0), value('y2', 0.0)
            length = math.hypot(x2 - x1, y2 - y1) or 1e-6
            angle = math.atan2(y2 - y1, x2 - x1)
            s = length / 128.0
            base = multiply((1, 0, 0, 1, (x1 + x2) / 2, (y1 + y2) / 2),
                (s * math.cos(angle), s * math.sin(angle), -s * math.sin(angle),
                    s * math.cos(angle), 0, 0))
            kind = 0
        else:
            cx = value('cx', 0.5 if bbox_units else 32.0)
            cy = value('cy', 0.5 if bbox_units else 32.0)
            r = value('r', 0.5 if bbox_units else 32.0)
            base = (r / 64.0, 0, 0, r / 64.0, cx, cy)
            name = attributes.get('data-hvif-gradient', 'circular')
            kind = GRADIENT_TYPES.index(name) if name in GRADIENT_TYPES else 1
        # gradient space to the element's own coordinates (box: its bounding
        # box there), then on into the icon if the transform was baked
        m = multiply(parse_transform(attributes.get('gradientTransform')), base)
        if bbox_units:
            x0, y0, x1b, y1b = box
            m = multiply((max(x1b - x0, 1e-6), 0, 0, max(y1b - y0, 1e-6), x0, y0), m)
        m = multiply(baked, m)
        stops = []
        for stop in stop_elements:
            st = element_style(stop, {}, ('stop-color', 'stop-opacity'))
            offset_text = stop.get('offset', '0').strip()
            offset = float(offset_text[:-1]) / 100 if offset_text.endswith('%') \
                else float(offset_text)
            offset = min(max(offset, stops[-1][0] / 255 if stops else 0.0), 1.0)
            color = parse_color(st.get('stop-color', 'black')) or (0, 0, 0)
            a = float(st.get('stop-opacity', 1)) * alpha
            stops.append((int(offset * 255 + 0.5), tuple(color) + (int(round(255 * max(0, min(1, a)))),)))
        if not stops:
            self.warn('gradient %s has no stops; left out' % gradient_id)
            return None
        return Style(None, kind, m, stops)


def parse_transformers(text):
    result = []
    for item in (text or '').split(';'):
        item = item.strip()
        if not item:
            continue
        kind, _, args = item.partition(':')
        values = numbers(args)
        if kind == 'stroke':
            result.append(('stroke', (int(values[0]), int(values[1]), int(values[2]), int(values[3]))))
        elif kind == 'contour':
            result.append(('contour', (int(values[0]), int(values[1]), int(values[2]))))
        elif kind == 'perspective':
            result.append(('perspective', tuple(values[:9])))
        else:
            raise HvifError('a transformer hviftool does not know: %s' % kind)
    return result


def svg_to_icon(path):
    tree = ET.parse(path)
    return SvgReader(tree.getroot(), os.path.basename(path)).read()


# -- SVG out -----------------------------------------------------------------------------

def fmt(value):
    text = ('%.4f' % value).rstrip('0').rstrip('.')
    return '0' if text in ('-0', '') else text


def fmt_matrix(value):
    """Enough digits for a 24-bit float (17 bits of mantissa)."""
    text = '%.8g' % value
    return '0' if float(text) == 0 else text


def path_data(path):
    pts = path.points
    if not pts:
        return ''
    out = ['M%s %s' % (fmt(pts[0][0]), fmt(pts[0][1]))]
    count = len(pts) if path.closed else len(pts) - 1
    for i in range(count):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        straight = (a[4], a[5]) == (a[0], a[1]) and (b[2], b[3]) == (b[0], b[1])
        if straight and i == len(pts) - 1:
            break
        if straight:
            out.append('L%s %s' % (fmt(b[0]), fmt(b[1])))
        else:
            out.append('C%s %s %s %s %s %s' % tuple(fmt(v) for v in (a[4], a[5], b[2], b[3], b[0], b[1])))
    if path.closed:
        out.append('Z')
    return ' '.join(out)


def color_attributes(c, prefix):
    text = '#%02x%02x%02x' % tuple(c[:3])
    attrs = '%s="%s"' % (prefix, text)
    if c[3] < 255:
        attrs += ' %s-opacity="%s"' % (prefix, fmt(c[3] / 255))
    return attrs


def icon_to_svg(icon, size=64, scale=None):
    """SVG for an Icon. With scale, only the shapes visible at that scale
    (what Haiku draws at 64 * scale pixels)."""
    defs, body = icon_to_svg_parts(icon, scale)
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" '
        'width="%s" height="%s">' % (size, size)]
    if defs:
        out.append('<defs>' + ''.join(defs) + '</defs>')
    out += body
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def icon_to_svg_parts(icon, scale=None, prefix='s'):
    """The gradients and the shape elements of an Icon's SVG."""
    out = []
    defs = []
    for i, style in enumerate(icon.styles):
        if style.gradient_type is None:
            continue
        m = ' '.join(fmt_matrix(v) for v in style.matrix)
        name = GRADIENT_TYPES[style.gradient_type] if style.gradient_type < len(GRADIENT_TYPES) \
            else 'linear'
        stops = ''.join('<stop offset="%s" stop-color="#%02x%02x%02x"%s/>' % (
            fmt(o / 255), c[0], c[1], c[2],
            ' stop-opacity="%s"' % fmt(c[3] / 255) if c[3] < 255 else '') for o, c in style.stops)
        if style.gradient_type == 0:
            defs.append('<linearGradient id="%s%d" gradientUnits="userSpaceOnUse" x1="-64" y1="0" '
                'x2="64" y2="0" gradientTransform="matrix(%s)">%s</linearGradient>' % (prefix, i, m, stops))
        else:
            extra = '' if name == 'circular' else ' data-hvif-gradient="%s"' % name
            defs.append('<radialGradient id="%s%d" gradientUnits="userSpaceOnUse" cx="0" cy="0" '
                'r="64" gradientTransform="matrix(%s)"%s>%s</radialGradient>' % (prefix, i, m, extra, stops))
    for shape in icon.shapes:
        if scale is not None and not visible(shape, scale):
            continue
        style = icon.styles[shape.style]
        d = ' '.join(path_data(icon.paths[i]) for i in shape.paths)
        attrs = ['d="%s"' % d]
        stroke = next((p for k, p in shape.transformers if k == 'stroke'), None)
        others = [(k, p) for k, p in shape.transformers if k != 'stroke']
        paint_prefix = 'stroke' if stroke else 'fill'
        if style.gradient_type is None:
            paint = color_attributes(style.color, paint_prefix)
        else:
            paint = '%s="url(#%s%d)"' % (paint_prefix, prefix, shape.style)
        if stroke:
            attrs.append('fill="none"')
            attrs.append(paint)
            attrs.append('stroke-width="%d" stroke-linejoin="%s" stroke-linecap="%s" '
                'stroke-miterlimit="%d"' % (stroke[0], LINE_JOINS[stroke[1]] if stroke[1] < 5
                    else 'miter', LINE_CAPS[stroke[2]] if stroke[2] < 3 else 'butt', stroke[3]))
        else:
            attrs.append(paint)
        if others or len([k for k, _ in shape.transformers if k == 'stroke']) > 1:
            attrs.append('data-hvif-transformers="%s"' % '; '.join(
                '%s:%s' % (k, ','.join(fmt(v) if isinstance(v, float) else str(v) for v in p))
                for k, p in shape.transformers))
            contour = next((p for k, p in others if k == 'contour'), None)
            if contour and contour[0] > 0 and not stroke:
                # a preview of the outline: the fill, stroked twice as wide
                attrs.append('%s stroke-width="%d" stroke-linejoin="%s"' % (
                    color_attributes(style.color, 'stroke') if style.gradient_type is None
                    else 'stroke="url(#%s%d)"' % (prefix, shape.style), 2 * contour[0],
                    LINE_JOINS[contour[1]] if contour[1] < 5 else 'miter'))
        if shape.matrix is not None:
            attrs.append('transform="matrix(%s)"' % ' '.join(fmt_matrix(v) for v in shape.matrix))
        if shape.hinting:
            attrs.append('data-hvif-hinting="1"')
        if shape.lod is not None:
            attrs.append('data-hvif-lod="%s %s"' % (fmt(shape.lod[0] / 63.75), fmt(shape.lod[1] / 63.75)))
        out.append('<path %s/>' % ' '.join(attrs))
    return defs, out


def icon_bounds(icon):
    """The box an icon draws in, strokes included."""
    boxes = []
    for shape in icon.shapes:
        m = shape.matrix or IDENTITY
        paths = [transform_path(icon.paths[i], m) for i in shape.paths]
        if not paths:
            continue
        x0, y0, x1, y1 = bounds(paths)
        grow = max([p[0] / 2 * (similarity_scale(m) or 1) for k, p in shape.transformers
            if k in ('stroke', 'contour')] + [0])
        boxes.append((x0 - grow, y0 - grow, x1 + grow, y1 + grow))
    if not boxes:
        return (0, 0, 0, 0)
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
        max(b[2] for b in boxes), max(b[3] for b in boxes))


def fit(icon, box):
    """The transform that puts an icon in box (x0, y0, x1, y1): as large as
    it fits, centred across, standing on the bottom."""
    x0, y0, x1, y1 = icon_bounds(icon)
    scale = min((box[2] - box[0]) / max(x1 - x0, 1e-6), (box[3] - box[1]) / max(y1 - y0, 1e-6))
    tx = box[0] + ((box[2] - box[0]) - (x1 - x0) * scale) / 2 - x0 * scale
    ty = box[3] - y1 * scale
    return (scale, 0.0, 0.0, scale, tx, ty)


def compose(layers):
    """One SVG from layers drawn in order: (source, transform, lod), a
    source being an Icon, or the text of an SVG drawn on the 64 grid, a
    transform a matrix or None, lod a "min max" range or None."""
    defs, body = [], []
    for n, (source, matrix, lod) in enumerate(layers):
        attrs = []
        if matrix is not None:
            attrs.append('transform="matrix(%s)"' % ' '.join(fmt_matrix(v) for v in matrix))
        if lod:
            attrs.append('data-hvif-lod="%s"' % lod)
        if isinstance(source, Icon):
            layer_defs, layer_body = icon_to_svg_parts(source, None, 'l%ds' % n)
        else:
            root = ET.fromstring(source)
            layer_defs, layer_body = [], []
            ids = {}
            for element in root.iter():
                if element.get('id'):
                    ids[element.get('id')] = 'l%d%s' % (n, element.get('id'))
            text = source
            for old, new in ids.items():
                text = re.sub(r'(id="|#)%s(["\)])' % re.escape(old), r'\g<1>%s\2' % new, text)
            ET.register_namespace('', 'http://www.w3.org/2000/svg')
            ET.register_namespace('xlink', 'http://www.w3.org/1999/xlink')
            root = ET.fromstring(text)
            for child in root:
                parts = list(child) if local(child.tag) == 'defs' else [child]
                for part in parts:
                    part.tail = None
                    markup = ET.tostring(part, encoding='unicode')
                    (layer_defs if local(child.tag) == 'defs' else layer_body).append(markup)
        defs += layer_defs
        body.append('<g %s>' % ' '.join(attrs) if attrs else '<g>')
        body += layer_body
        body.append('</g>')
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">']
    if defs:
        out.append('<defs>' + ''.join(defs) + '</defs>')
    out += body
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def visible(shape, scale):
    """PathSourceShape::Visible()"""
    if shape.lod is None:
        return True
    lo, hi = shape.lod[0] / 63.75, shape.lod[1] / 63.75
    return scale >= lo and (scale <= hi or hi >= 4.0)


# -- Icon-O-Matic documents ------------------------------------------------------------
#
# Icon-O-Matic saves its documents (Haiku's data/artwork/icons) as 'IMSG' and
# an archived BMessage, flattened in Haiku's format ('1FMH'; see
# headers/private/app/MessagePrivate.h) or, in older ones, R5's ('FOB1';
# src/kits/app/MessageAdapter.cpp), read by src/libs/icon/message/
# MessageImporter.cpp. This turns one into an Icon the way Icon-O-Matic's
# HVIF export would (FlatIconExporter): stroke and contour widths and miter
# limits truncated to whole numbers, stop offsets to 0-255.

def r5_message(data, pos=0):
    """{field name: (type code, [item bytes])} of a flattened BMessage."""
    magic = data[pos:pos + 4]
    if magic == b'HMF1':
        return haiku_message(data, pos)
    if magic == b'2BOF':
        raise HvifError('a BMessage flattened in the Dano format, which hviftool does not read')
    _, _, _, what, flags = struct.unpack_from('<4sIiiB', data, pos)
    if magic != b'1BOF':
        raise HvifError('not a flattened BMessage')
    p = pos + 17
    if flags & 2:
        p += 4
    if flags & 4:
        p += 16
    fields = {'': (b'what', [struct.pack('<i', what)])}
    while True:
        field_flags = data[p]
        p += 1
        if not field_flags & 1:
            break
        kind = data[p:p + 4][::-1]
        p += 4
        mini = field_flags & 2
        if field_flags & 8:
            count = 1
        elif mini:
            count = data[p]
            p += 1
        else:
            count = struct.unpack_from('<i', data, p)[0]
            p += 4
        if mini:
            size = data[p]
            p += 1
        else:
            size = struct.unpack_from('<i', data, p)[0]
            p += 4
        name_length = data[p]
        name = data[p + 1:p + 1 + name_length].decode('utf-8', 'replace')
        p += 1 + name_length
        body = data[p:p + size]
        p += size
        items = []
        q = 0
        for _ in range(count):
            if field_flags & 4:
                item_size = size // count
                items.append(body[q:q + item_size])
                q += item_size
            else:
                item_size = struct.unpack_from('<i', body, q)[0]
                items.append(body[q + 4:q + 4 + item_size])
                q += ((item_size + 4 + 7) & ~7)
        fields[name] = (kind, items)
    return fields


def haiku_message(data, pos=0):
    """A BMessage flattened in Haiku's own format: a 68 byte header, the
    field headers, then the data, where a field's name comes first."""
    (_, what, _, _, _, _, _, _, _, data_size, field_count, _) = struct.unpack_from('<4sIIiiiiiiIIi',
        data, pos)
    fields = {'': (b'what', [struct.pack('<I', what)])}
    headers = pos + 68
    body = headers + field_count * 24
    for i in range(field_count):
        flags, name_length, kind, count, size, offset, _ = struct.unpack_from('<HHIIIIi',
            data, headers + i * 24)
        start = body + offset
        name = data[start:start + name_length].rstrip(b'\0').decode('utf-8', 'replace')
        values = data[start + name_length:start + name_length + size]
        items = []
        if flags & 2:
            step = size // count if count else 0
            items = [values[j * step:(j + 1) * step] for j in range(count)]
        else:
            q = 0
            for _ in range(count):
                item_size = struct.unpack_from('<I', values, q)[0]
                items.append(values[q + 4:q + 4 + item_size])
                q += 4 + item_size
        fields[name] = (struct.pack('>I', kind), items)
    return fields


def r5_values(fields, name, fmt):
    kind, items = fields.get(name, (None, []))
    return [struct.unpack('<' + fmt, item[:struct.calcsize('<' + fmt)]) for item in items]


def r5_messages(fields, name):
    return [r5_message(item) for item in fields.get(name, (None, []))[1]]


def uint32_color(value):
    return tuple(struct.pack('<I', value & 0xFFFFFFFF))


def native_to_icon(data):
    if data[:4] != b'IMSG':
        raise HvifError('not an Icon-O-Matic document')
    archive = r5_message(data, 4)
    icon = Icon()
    for container in r5_messages(archive, 'paths'):
        for p in r5_messages(container, 'path'):
            points = [a + b + c for a, b, c in zip(r5_values(p, 'point', 'ff'),
                r5_values(p, 'point in', 'ff'), r5_values(p, 'point out', 'ff'))]
            closed = bool((r5_values(p, 'path closed', '?') or [(False,)])[0][0])
            icon.paths.append(Path([tuple(float(v) for v in pt) for pt in points], closed))
    for container in r5_messages(archive, 'styles'):
        for s in r5_messages(container, 'style'):
            color = uint32_color((r5_values(s, 'color', 'I') or [(0xFFFFFFFF,)])[0][0])
            gradients = r5_messages(s, 'gradient')
            if not gradients:
                icon.styles.append(Style(color))
                continue
            g = gradients[0]
            matrix = r5_values(g, 'transformation', 'dddddd')
            offsets = [v[0] for v in r5_values(g, 'offset', 'f')]
            colors = [uint32_color(v[0]) for v in r5_values(g, 'color', 'I')]
            stops = sorted(((int(o * 255.0), c) for o, c in zip(offsets, colors)),
                key=lambda stop: stop[0])
            kind = (r5_values(g, 'type', 'i') or [(0,)])[0][0]
            icon.styles.append(Style(None, kind, matrix[0] if matrix else IDENTITY, stops))
    for container in r5_messages(archive, 'shapes'):
        for s in r5_messages(container, 'shape'):
            kind = r5_values(s, 'type', 'I')
            if kind and kind[0][0] != 0x73687073:      # 'shps', not a reference image
                continue
            style = r5_values(s, 'style ref', 'i')
            if not style or style[0][0] >= len(icon.styles):
                continue
            paths = [v[0] for v in r5_values(s, 'path ref', 'i') if v[0] < len(icon.paths)]
            matrix = r5_values(s, 'transformation', 'dddddd')
            matrix = matrix[0] if matrix and not is_identity(matrix[0]) else None
            hinting = bool((r5_values(s, 'hinting', '?') or [(False,)])[0][0])
            lo = min(max((r5_values(s, 'min visibility scale', 'f') or [(0.0,)])[0][0], 0.0), 4.0)
            hi = min(max((r5_values(s, 'max visibility scale', 'f') or [(4.0,)])[0][0], 0.0), 4.0)
            lod = (int(lo * 63.75 + 0.5), int(hi * 63.75 + 0.5))
            transformers = []
            for t in r5_messages(s, 'transformer'):
                what = struct.unpack('<i', t[''][1][0])[0] & 0xFFFFFFFF
                double = lambda name, default: (r5_values(t, name, 'd') or [(default,)])[0][0]
                integer = lambda name, default: (r5_values(t, name, 'i') or [(default,)])[0][0]
                if what == 0x7374726B:          # 'strk'
                    transformers.append(('stroke', (int(double('width', 1.0)),
                        integer('line join', 0), integer('line cap', 0),
                        int(double('miter limit', 4.0)))))
                elif what == 0x636E7472:        # 'cntr'
                    transformers.append(('contour', (int(double('width', 1.0)),
                        integer('line join', 0), int(double('miter limit', 4.0)))))
                elif what == 0x70727370:        # 'prsp'
                    transformers.append(('perspective', tuple(v[0] for v in r5_values(t, 'matrix', 'd'))))
                elif what == 0x6166666E:        # 'affn': HVIF's is unreadable; fold it in
                    m = r5_values(t, 'matrix', 'dddddd')
                    if m:
                        matrix = multiply(matrix or IDENTITY, m[0])
            icon.shapes.append(Shape(style[0][0], paths, matrix, hinting, lod, transformers))
    return icon


# -- rdef --------------------------------------------------------------------------------

def rdef_array(data, indent='\t'):
    return '\n'.join('%s$"%s"' % (indent, data[i:i + 32].hex().upper())
        for i in range(0, len(data), 32))


def rdef_resource(data, resource_id=1, name='BEOS:ICON'):
    return 'resource(%d, "%s") #\'VICN\' array {\n%s\n};\n' % (resource_id, name, rdef_array(data))


def hvif_from_rdef(text, name='BEOS:ICON'):
    """The data of a VICN resource in rdef text."""
    m = re.search(r'resource\s*\([^)]*"%s"\s*\)\s*#\'VICN\'\s*array\s*\{(.*?)\}' % re.escape(name),
        text, re.S)
    if not m:
        m = re.search(r'#\'VICN\'\s*array\s*\{(.*?)\}', text, re.S)
    if not m:
        raise HvifError('no VICN resource in the rdef')
    return bytes.fromhex(''.join(re.findall(r'\$"([0-9A-Fa-f]*)"', m.group(1))))


def load(path):
    """HVIF data from an .hvif file, an Icon-O-Matic document, an .rdef, or
    an .svg."""
    if path.endswith('.svg'):
        return encode(svg_to_icon(path))
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] == b'ncif':
        return data
    if data[:4] == b'IMSG':
        return encode(native_to_icon(data))
    return hvif_from_rdef(data.decode('utf-8', 'replace'))


# -- previews ----------------------------------------------------------------------------

def render_png(svg_text, size):
    """PNG bytes of an SVG at size x size, by rsvg-convert."""
    proc = subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size)],
        input=svg_text.encode(), capture_output=True)
    if proc.returncode != 0:
        raise HvifError('rsvg-convert failed: %s' % proc.stderr.decode(errors='replace'))
    return proc.stdout


def png_rgba(data):
    """(width, height, rows of RGBA bytes) of an 8-bit RGBA or RGB PNG."""
    pos = 8
    width = height = None
    idat = b''
    color_type = 6
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color_type = struct.unpack('>IIBB', chunk[:10])
            if depth != 8 or color_type not in (2, 6):
                raise HvifError('a PNG hviftool cannot read (depth %d, type %d)' % (depth, color_type))
        elif kind == b'IDAT':
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    bpp = 4 if color_type == 6 else 3
    stride = width * bpp
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if bpp == 3:
            line = bytearray(b''.join(bytes(line[i:i + 3]) + b'\xff' for i in range(0, stride, 3)))
        rows.append(line)
    return width, height, rows


def png_write(width, height, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    chunk = lambda kind, body: struct.pack('>I', len(body)) + kind + body + \
        struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def sheet(icons, sizes, zoom, background, columns=1):
    """A contact sheet: each icon at each size, drawn and then zoomed
    (nearest neighbour) so a 16 pixel icon can be seen pixel by pixel;
    columns icons side by side."""
    cell = max(s * z for s, z in zip(sizes, zoom)) + 8
    rows = (len(icons) + columns - 1) // columns
    width = 8 + columns * len(sizes) * (cell + 8)
    height = 8 + rows * (cell + 8)
    canvas = [bytearray(background * width) for _ in range(height)]
    for index, icon in enumerate(icons):
        row, first = index // columns, (index % columns) * len(sizes)
        for col, (size, z) in enumerate(zip(sizes, zoom)):
            png = render_png(icon_to_svg(icon, size, size / 64.0), size)
            w, h, pixels = png_rgba(png)
            ox = 8 + (first + col) * (cell + 8) + (cell - w * z) // 2
            oy = 8 + row * (cell + 8) + (cell - h * z) // 2
            for y in range(h):
                for x in range(w):
                    r, g, b, a = pixels[y][4 * x:4 * x + 4]
                    for dy in range(z):
                        line = canvas[oy + y * z + dy]
                        for dx in range(z):
                            i = 4 * (ox + x * z + dx)
                            br, bg, bb = line[i], line[i + 1], line[i + 2]
                            line[i:i + 4] = bytes((
                                (r * a + br * (255 - a)) // 255,
                                (g * a + bg * (255 - a)) // 255,
                                (b * a + bb * (255 - a)) // 255, 255))
    return png_write(width, height, canvas)


# -- round trip test --------------------------------------------------------------------

def normalized(icon):
    """What an icon draws, independent of how its file puts it: per shape,
    the style, the paths in icon coordinates, transformers, visibility."""
    result = []
    for shape in icon.shapes:
        if all(len(icon.paths[i].points) < 2 for i in shape.paths) \
                or any(k == 'stroke' and p[0] <= 0 for k, p in shape.transformers):
            continue    # draws nothing
        m = shape.matrix or IDENTITY
        style = icon.styles[shape.style]
        paths = []
        for index in shape.paths:
            path = transform_path(icon.paths[index], m)
            points = [list(p) for p in path.points]
            if path.closed and len(points) > 1 and points[-1][:2] == points[0][:2]:
                points[0][2:4] = points[-1][2:4]
                points.pop()
            if points and not path.closed:
                # handles that no segment uses
                points[0][2:4] = points[0][:2]
                points[-1][4:6] = points[-1][:2]
            paths.append((path.closed, points))
        if style.gradient_type is None:
            paint = ('color', style.color)
        else:
            paint = ('gradient', style.gradient_type, multiply(m, style.matrix), style.stops)
        scale = similarity_scale(m) or 1.0
        transformers = [(k, (int(round(p[0] * scale)),) + tuple(p[1:])) if k == 'stroke' else (k, p)
            for k, p in shape.transformers]
        result.append((paint, paths, shape.hinting, tuple(shape.lod or (0, 255)), transformers))
    return result


def same(a, b, tolerance=0.02):
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        return len(a) == len(b) and all(same(x, y, tolerance) for x, y in zip(a, b))
    if isinstance(a, float) or isinstance(b, float):
        return abs(a - b) <= tolerance + 1e-4 * max(abs(a), abs(b))
    return a == b


def run_test(directory):
    names = sorted(n for n in os.listdir(directory) if not n.startswith('.'))
    failed = identical = checked = unreadable = 0
    for name in names:
        path = os.path.join(directory, name)
        if os.path.isdir(path):
            continue
        with open(path, 'rb') as f:
            data = f.read()
        if data[:4] != b'ncif':
            continue
        try:
            first = decode(data)
        except HvifError as e:
            # Haiku's importer cannot read it either
            unreadable += 1
            print('UNREADABLE: %s: %s' % (name, e))
            continue
        checked += 1
        try:
            if encode(first) == data:
                identical += 1
            with tempfile.NamedTemporaryFile('w', suffix='.svg', delete=False) as f:
                f.write(icon_to_svg(first))
            try:
                second = decode(encode(svg_to_icon(f.name)))
            finally:
                os.unlink(f.name)
            if not same(normalized(first), normalized(second)):
                failed += 1
                print('DIFFERS: %s' % name)
        except (HvifError, ET.ParseError) as e:
            failed += 1
            print('ERROR: %s: %s' % (name, e))
    print('%d icons: %d re-encode byte for byte, %d through SVG and back draw the same, '
        '%d differ%s' % (checked, identical, checked - failed, failed,
        '; %d not readable as HVIF' % unreadable if unreadable else ''))
    return failed == 0


# -- command line ------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='Haiku vector icons (HVIF) to and from SVG')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('encode', help='SVG to HVIF')
    p.add_argument('svg')
    p.add_argument('-o', '--output')
    p.add_argument('--rdef', help='also write an rdef with the icon as a BEOS:ICON resource')
    p = sub.add_parser('decode', help='HVIF (or an rdef with one) to SVG')
    p.add_argument('icon')
    p.add_argument('-o', '--output')
    p.add_argument('--scale', type=float, help='only the shapes visible at this scale')
    p = sub.add_parser('rdef', help='an rdef resource for an icon')
    p.add_argument('icon')
    p.add_argument('--id', type=int, default=1)
    p.add_argument('--name', default='BEOS:ICON')
    p = sub.add_parser('preview', help='a PNG contact sheet at several sizes')
    p.add_argument('icons', nargs='+')
    p.add_argument('-o', '--output', required=True)
    p.add_argument('--sizes', default='16,32,64')
    p.add_argument('--zoom', default='4,2,1')
    p.add_argument('--background', default='d8d8d8', help='hex RGB behind the icons')
    p.add_argument('--columns', type=int, default=1, help='icons side by side')
    p = sub.add_parser('test', help='round trip every HVIF file in a directory')
    p.add_argument('directory')
    args = parser.parse_args()
    try:
        if args.command == 'encode':
            data = encode(svg_to_icon(args.svg))
            if args.output:
                with open(args.output, 'wb') as f:
                    f.write(data)
            if args.rdef:
                with open(args.rdef, 'w') as f:
                    f.write(rdef_resource(data))
            sys.stderr.write('%s: %d bytes\n' % (args.svg, len(data)))
        elif args.command == 'decode':
            text = icon_to_svg(decode(load(args.icon)), 64, args.scale)
            if args.output:
                with open(args.output, 'w') as f:
                    f.write(text)
            else:
                sys.stdout.write(text)
        elif args.command == 'rdef':
            sys.stdout.write(rdef_resource(load(args.icon), args.id, args.name))
        elif args.command == 'preview':
            sizes = [int(v) for v in args.sizes.split(',')]
            zoom = [int(v) for v in args.zoom.split(',')]
            background = bytes.fromhex(args.background) + b'\xff'
            icons = [decode(load(path)) for path in args.icons]
            with open(args.output, 'wb') as f:
                f.write(sheet(icons, sizes, zoom, background, args.columns))
        elif args.command == 'test':
            sys.exit(0 if run_test(args.directory) else 1)
    except (HvifError, ET.ParseError, OSError) as e:
        sys.exit('hviftool: %s' % e)


if __name__ == '__main__':
    main()
