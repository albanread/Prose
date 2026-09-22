// gamepane.swift: retro mode. A guest program puts 8-bit palette indices in the
// surface pool; this composites them over the desktop on the GPU — palette
// lookup, per-scanline palettes, scroll, sprites and a CRT filter — with no
// copy anywhere between the guest's bytes and the drawable.
//
// Contract: docs/game-pane.md. The guest side is the PRDS pane commands
// (0x0200-0x0203) and BGamePane in libgame.
import Foundation
import Metal
import simd

enum Pane {
    static let maxPanes = 8
    static let maxClipRects = 32
    static let maxSprites = 64
    static let maxBuffers = 3

    static let formatIndexed8: UInt32 = 1
    static let formatB8G8R8X8: UInt32 = 2

    static let flagVisible: UInt32 = 1 << 0
    static let flagScanline: UInt32 = 1 << 1
    static let flagFullscreen: UInt32 = 1 << 2

    static let spriteStride = 48
}

/// What the device knows about one pane. Written on the device queue, read by
/// the presenter on the main thread, both under `presentLock`.
struct PaneState {
    var live = false
    var format: UInt32 = Pane.formatIndexed8
    var worldWidth = 0, worldHeight = 0, stride = 0
    var buffers = 1
    var offset = 0, bufferStride = 0, paletteOffset = 0

    var flags: UInt32 = 0
    var destX = 0, destY = 0, destWidth = 0, destHeight = 0
    var viewWidth = 0, viewHeight = 0
    var scrollX = 0, scrollY = 0
    var effect: UInt32 = 0
    var clip: [(x: Int, y: Int, w: Int, h: Int)] = []

    var bufferIndex = 0
    var spriteOffset = 0, spriteCount = 0

    var visible: Bool {
        live && flags & Pane.flagVisible != 0 && !clip.isEmpty
            && destWidth > 0 && destHeight > 0 && viewWidth > 0 && viewHeight > 0
    }

    /// Byte offset in the pool of the buffer the guest last presented.
    var liveOffset: Int { offset + min(bufferIndex, max(buffers - 1, 0)) * bufferStride }
}

/// The fragment shader's view of a pane. Guest pixels in, drawable pixels out.
struct PaneParams {
    var worldWidth: UInt32 = 0
    var worldHeight: UInt32 = 0
    var strideBytes: UInt32 = 0
    var bufferOffset: UInt32 = 0        // bytes into the pool
    var paletteWords: UInt32 = 0        // words into the pool
    var format: UInt32 = 0
    var flags: UInt32 = 0
    var effect: UInt32 = 0

    var scroll = SIMD2<Int32>(0, 0)
    var view = SIMD2<UInt32>(0, 0)
    var dest = SIMD4<Float>(0, 0, 0, 0)     // guest pixels: x, y, width, height
    var scale = SIMD2<Float>(1, 1)          // the presenter's aspect fit
    var bias = SIMD2<Float>(0, 0)
    var guest = SIMD2<Float>(0, 0)          // the guest's screen, in its own pixels
    var clipCount: UInt32 = 0
    var spriteCount: UInt32 = 0
    var spriteWords: UInt32 = 0             // words into the pool
    var pad: UInt32 = 0
}

let paneShaderSource = """
#include <metal_stdlib>
using namespace metal;

struct VOut { float4 pos [[position]]; float2 uv; };

struct PaneParams {
    uint worldWidth, worldHeight, strideBytes, bufferOffset;
    uint paletteWords, format, flags, effect;
    int2 scroll; uint2 view;
    float4 dest; float2 scale, bias, guest;
    uint clipCount, spriteCount, spriteWords, pad;
};

constant uint kFlagScanline = 2;

vertex VOut pane_vmain(uint vid [[vertex_id]]) {
    float2 p = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    VOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return o;
}

// 1-15 come from this row's sixteen, 16-255 from the global palette, unless
// per-scanline palettes are off -- then everything is global. That split is
// the whole trick: a raster bar costs one palette write, not a repaint.
static inline float4 lookup(device const uint *pool, constant PaneParams &p, uint index, uint row) {
    if (index == 0u) return float4(0.0);
    uint at = (index < 16u && (p.flags & kFlagScanline) != 0u)
        ? p.paletteWords + 256u + min(row, p.worldHeight - 1u) * 16u + index
        : p.paletteWords + index;
    uint v = pool[at];
    return float4(float((v >> 16) & 0xFF), float((v >> 8) & 0xFF), float(v & 0xFF), 255.0) / 255.0;
}

// Sprites are 48 bytes in the pool, drawn after the world, last on top. Each
// fragment is inverse-transformed into sprite space, which is what buys
// rotation and fractional scale over a byte blitter.
static inline uint spriteIndexAt(device const uint *pool, constant PaneParams &p,
                                 device const uchar *bytes, float2 world, float2 viewPos,
                                 thread uint &base, thread float &alpha) {
    for (int i = int(p.spriteCount) - 1; i >= 0; i--) {
        uint w = p.spriteWords + uint(i) * 12u;
        float2 origin = float2(float(int(pool[w])), float(int(pool[w + 1])));
        float2 size = float2(float(pool[w + 2]), float(pool[w + 3]));
        uint offset = pool[w + 4];
        uint stride = pool[w + 6];
        uint flags = pool[w + 7];
        float scale = as_type<float>(pool[w + 8]);
        float rotation = as_type<float>(pool[w + 9]);
        float a = as_type<float>(pool[w + 10]);
        if (size.x == 0.0 || size.y == 0.0 || scale <= 0.0 || a <= 0.0) continue;

        float2 here = (flags & 1u) != 0u ? viewPos : world;
        float2 centre = origin + size * scale * 0.5;
        float2 d = here - centre;
        if (rotation != 0.0) {
            float c = cos(-rotation), s = sin(-rotation);
            d = float2(d.x * c - d.y * s, d.x * s + d.y * c);
        }
        float2 local = d / scale + size * 0.5;
        if (any(local < 0.0) || any(local >= size)) continue;
        if ((flags & 2u) != 0u) local.x = size.x - 1.0 - local.x;
        if ((flags & 4u) != 0u) local.y = size.y - 1.0 - local.y;
        uint index = bytes[offset + uint(local.y) * stride + uint(local.x)];
        if (index == 0u) continue;
        base = pool[w + 11];
        alpha = a;
        return index;
    }
    return 0u;
}

fragment float4 pane_fmain(VOut in [[stage_in]],
                           device const uchar *bytes [[buffer(0)]],
                           constant PaneParams &p [[buffer(1)]],
                           device const uint *pool [[buffer(2)]],
                           constant uint4 *clip [[buffer(3)]]) {
    // the drawable, back through the presenter's aspect fit, into guest pixels
    float2 uv = (in.uv - p.bias) / p.scale;
    if (any(uv < 0.0) || any(uv >= 1.0)) discard_fragment();
    float2 g = uv * p.guest;

    bool inside = false;
    for (uint i = 0; i < p.clipCount; i++) {
        uint4 r = clip[i];
        if (g.x >= float(r.x) && g.y >= float(r.y)
            && g.x < float(r.x + r.z) && g.y < float(r.y + r.w)) { inside = true; break; }
    }
    if (!inside) discard_fragment();

    float2 viewPos = (g - p.dest.xy) / p.dest.zw * float2(p.view);
    if (any(viewPos < 0.0) || any(viewPos >= float2(p.view))) discard_fragment();
    float2 world = viewPos + float2(p.scroll);

    float4 colour = float4(0.0);
    uint row = uint(max(world.y, 0.0));
    if (all(world >= 0.0) && world.x < float(p.worldWidth) && world.y < float(p.worldHeight)) {
        if (p.format == 2u) {
            uint v = pool[(p.bufferOffset + row * p.strideBytes) / 4u + uint(world.x)];
            colour = float4(float((v >> 16) & 0xFF), float((v >> 8) & 0xFF), float(v & 0xFF), 255.0) / 255.0;
        } else {
            uint index = bytes[p.bufferOffset + row * p.strideBytes + uint(world.x)];
            colour = lookup(pool, p, index, row);
        }
    }

    uint base = 0; float spriteAlpha = 1.0;
    uint s = spriteIndexAt(pool, p, bytes, world, viewPos, base, spriteAlpha);
    if (s != 0u) {
        float4 sc = lookup(pool, p, min(s + base, 255u), row);
        colour = float4(mix(colour.rgb, sc.rgb, spriteAlpha * sc.a),
                        max(colour.a, sc.a * spriteAlpha));
    }
    if (colour.a <= 0.0) discard_fragment();     // index 0: the desktop shows through

    if (p.effect != 0u) {
        // scanlines are counted in the pane's own pixels, so they stay put
        // when the window is scaled rather than crawling with the zoom
        colour.rgb *= (uint(world.y) & 1u) == 0u ? 1.0 : 0.72;
        if (p.effect == 2u) {
            // a tube: an aperture mask across, a little bloom, corners falling off
            float phase = fmod(viewPos.x, 3.0);
            float3 mask = float3(phase < 1.0 ? 1.0 : 0.80, (phase >= 1.0 && phase < 2.0) ? 1.0 : 0.80,
                                 phase >= 2.0 ? 1.0 : 0.80);
            colour.rgb *= mask;
            colour.rgb += colour.rgb * colour.rgb * 0.18;
            float2 c = viewPos / float2(p.view) * 2.0 - 1.0;
            colour.rgb *= 1.0 - 0.22 * dot(c, c) * 0.5;
            colour.rgb = min(colour.rgb, 1.0);
        }
    }
    return colour;
}
"""
