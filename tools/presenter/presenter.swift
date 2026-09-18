// presenter: Sprint 0 prototype of the host-side Metal presenter shared by the
// S1/S2 display devices (see haiku_virtualized.md §3.1).
//
// The surface is a B_RGB32 (BGRX, undefined X byte) framebuffer in 16 KiB-
// aligned mmap'd memory, wrapped with makeBuffer(bytesNoCopy:). A fragment
// shader samples the buffer directly (any stride, any 4-byte-aligned offset),
// forces alpha to 1, and letterboxes it onto a CAMetalLayer paced by
// CADisplayLink. A producer thread animates the surface like app_server's
// back-to-front copies, deliberately writing X bytes of 0.
//
// Before presenting, a self-test renders the surface 1:1 offscreen and checks
// every pixel against the source (colour channels intact, alpha == 255).
//
// usage: presenter [--size WxH] [--stride bytes] [--offset bytes] [--seconds n]
//                  [--fps n] [--present-always]
// Prints a "RESULT {json}" line and exits 0 on PASS, 1 on FAIL.
import AppKit
import Metal
import QuartzCore
import os

setvbuf(stdout, nil, _IONBF, 0)

let args = CommandLine.arguments
func option(_ name: String) -> String? {
    guard let i = args.firstIndex(of: name), i + 1 < args.count else { return nil }
    return args[i + 1]
}
let dims = (option("--size") ?? "1280x800").split(separator: "x").compactMap { Int($0) }
let surfWidth = dims.count == 2 ? dims[0] : 1280
let surfHeight = dims.count == 2 ? dims[1] : 800
let surfStride = Int(option("--stride") ?? "") ?? surfWidth * 4
let surfOffset = Int(option("--offset") ?? "0") ?? 0
let runSeconds = Double(option("--seconds") ?? "5") ?? 5
let producerFPS = Double(option("--fps") ?? "60") ?? 60
let presentAlways = args.contains("--present-always")

guard surfStride >= surfWidth * 4, surfStride % 4 == 0, surfOffset % 4 == 0 else {
    print("stride must be >= width*4 and a multiple of 4; offset a multiple of 4")
    exit(64)
}

let shaderSource = """
#include <metal_stdlib>
using namespace metal;

struct VOut { float4 pos [[position]]; float2 uv; };

struct Params {
    uint width; uint height; uint strideWords; uint offsetWords;
    float2 scale; float2 bias;
};

vertex VOut vmain(uint vid [[vertex_id]]) {
    float2 p = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    VOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return o;
}

// The surface is B_RGB32: bytes B, G, R, X. X is undefined, so alpha is forced to 1.
fragment float4 fmain(VOut in [[stage_in]],
                      device const uint *fb [[buffer(0)]],
                      constant Params &p [[buffer(1)]]) {
    float2 uv = (in.uv - p.bias) / p.scale;
    if (any(uv < 0.0) || any(uv >= 1.0))
        return float4(0.0, 0.0, 0.0, 1.0);
    uint x = min(uint(uv.x * float(p.width)), p.width - 1);
    uint y = min(uint(uv.y * float(p.height)), p.height - 1);
    uint v = fb[p.offsetWords + y * p.strideWords + x];
    return float4(float((v >> 16) & 0xFF), float((v >> 8) & 0xFF), float(v & 0xFF), 255.0) / 255.0;
}
"""

struct Params {
    var width: UInt32
    var height: UInt32
    var strideWords: UInt32
    var offsetWords: UInt32
    var scale: SIMD2<Float>
    var bias: SIMD2<Float>
}

/// A B_RGB32 surface in page-aligned anonymous shared memory.
final class Surface {
    let width = surfWidth, height = surfHeight, stride = surfStride, offset = surfOffset
    let length: Int
    let base: UnsafeMutableRawPointer
    static let bars: [UInt32] = [0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x202020]
    let box = 96

    init() {
        let page = Int(getpagesize())
        length = (offset + stride * height + page - 1) / page * page
        guard let p = mmap(nil, length, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0),
              p != MAP_FAILED else {
            fatalError("mmap failed")
        }
        base = p
    }

    func row(_ y: Int) -> UnsafeMutablePointer<UInt32> {
        (base + offset + y * stride).assumingMemoryBound(to: UInt32.self)
    }

    /// Background colour at (x, y): vertical bars; X byte deliberately 0.
    func background(_ x: Int) -> UInt32 { Surface.bars[x * Surface.bars.count / width] & 0x00FF_FFFF }

    func fillBackground() {
        for y in 0..<height {
            let r = row(y)
            for x in 0..<width { r[x] = background(x) }
        }
    }

    func boxOrigin(_ frame: Int) -> (Int, Int) {
        let spanX = max(1, width - box), spanY = max(1, height - box)
        let t = frame * 4
        let x = (t / spanX) % 2 == 0 ? t % spanX : spanX - t % spanX
        let y = ((t / 2) / spanY) % 2 == 0 ? (t / 2) % spanY : spanY - (t / 2) % spanY
        return (x, y)
    }

    /// Move the box from its previous position: repaint the old rect, draw the new one.
    func animate(frame: Int) {
        if frame > 0 {
            let (ox, oy) = boxOrigin(frame - 1)
            for y in oy..<min(oy + box, height) {
                let r = row(y)
                for x in ox..<min(ox + box, width) { r[x] = background(x) }
            }
        }
        let (nx, ny) = boxOrigin(frame)
        for y in ny..<min(ny + box, height) {
            let r = row(y)
            for x in nx..<min(nx + box, width) { r[x] = 0x00FF_8000 }
        }
    }
}

final class MetalView: NSView {
    override func makeBackingLayer() -> CALayer { CAMetalLayer() }
    override var wantsUpdateLayer: Bool { true }
}

final class Presenter: NSObject, NSApplicationDelegate {
    let device = MTLCreateSystemDefaultDevice()!
    lazy var queue = device.makeCommandQueue()!
    let surface = Surface()
    var buffer: MTLBuffer!
    var pipeline: MTLRenderPipelineState!
    var layer: CAMetalLayer!
    var window: NSWindow!
    var view: MetalView!

    let seq = OSAllocatedUnfairLock(initialState: 0)
    var lastPresentedSeq = -1
    var ticks = 0, changedTicks = 0, presents = 0, noDrawable = 0
    var frameDurations: [Double] = []
    let gpuTimes = OSAllocatedUnfairLock(initialState: [Double]())
    var selfTestMismatches = -1
    var running = true
    var linkStart = 0.0, linkEnd = 0.0

    func applicationDidFinishLaunching(_ notification: Notification) {
        guard let b = device.makeBuffer(bytesNoCopy: surface.base, length: surface.length,
                                        options: .storageModeShared, deallocator: nil) else {
            finish(error: "makeBuffer(bytesNoCopy:) failed")
            return
        }
        buffer = b
        do {
            let library = try device.makeLibrary(source: shaderSource, options: nil)
            let desc = MTLRenderPipelineDescriptor()
            desc.vertexFunction = library.makeFunction(name: "vmain")
            desc.fragmentFunction = library.makeFunction(name: "fmain")
            desc.colorAttachments[0].pixelFormat = .bgra8Unorm
            pipeline = try device.makeRenderPipelineState(descriptor: desc)
        } catch {
            finish(error: "shader: \(error)")
            return
        }

        surface.fillBackground()
        surface.animate(frame: 0)
        selfTestMismatches = selfTest()

        view = MetalView(frame: NSRect(x: 0, y: 0, width: 960, height: 600))
        view.wantsLayer = true
        layer = (view.layer as! CAMetalLayer)
        layer.device = device
        layer.pixelFormat = .bgra8Unorm
        layer.isOpaque = true
        layer.framebufferOnly = true
        window = NSWindow(contentRect: view.frame, styleMask: [.titled, .closable, .resizable],
                          backing: .buffered, defer: false)
        window.title = "presenter \(surface.width)x\(surface.height) stride \(surface.stride) offset \(surface.offset)"
        window.contentView = view
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        updateDrawableSize()

        Thread.detachNewThread { [self] in produce() }
        let link = view.displayLink(target: self, selector: #selector(tick(_:)))
        link.add(to: .main, forMode: .common)
        linkStart = CACurrentMediaTime()
        DispatchQueue.main.asyncAfter(deadline: .now() + runSeconds) { [self] in
            linkEnd = CACurrentMediaTime()
            link.invalidate()
            running = false
            finish(error: nil)
        }
    }

    func updateDrawableSize() {
        let scale = window.backingScaleFactor
        layer.contentsScale = scale
        layer.drawableSize = CGSize(width: view.bounds.width * scale, height: view.bounds.height * scale)
    }

    func params(targetWidth: Double, targetHeight: Double) -> Params {
        let surfAspect = Double(surface.width) / Double(surface.height)
        let targetAspect = targetWidth / targetHeight
        var scale = SIMD2<Float>(1, 1)
        if targetAspect > surfAspect {
            scale.x = Float(surfAspect / targetAspect)
        } else {
            scale.y = Float(targetAspect / surfAspect)
        }
        return Params(width: UInt32(surface.width), height: UInt32(surface.height),
                      strideWords: UInt32(surface.stride / 4), offsetWords: UInt32(surface.offset / 4),
                      scale: scale, bias: (SIMD2<Float>(1, 1) - scale) / 2)
    }

    func encode(_ cb: MTLCommandBuffer, target: MTLTexture, params p: Params) {
        let rp = MTLRenderPassDescriptor()
        rp.colorAttachments[0].texture = target
        rp.colorAttachments[0].loadAction = .dontCare
        rp.colorAttachments[0].storeAction = .store
        let enc = cb.makeRenderCommandEncoder(descriptor: rp)!
        var params = p
        enc.setRenderPipelineState(pipeline)
        enc.setFragmentBuffer(buffer, offset: 0, index: 0)
        enc.setFragmentBytes(&params, length: MemoryLayout<Params>.stride, index: 1)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
    }

    /// Render the surface 1:1 offscreen and compare every pixel with the source.
    func selfTest() -> Int {
        let w = surface.width, h = surface.height
        let desc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: w, height: h,
                                                            mipmapped: false)
        desc.usage = .renderTarget
        desc.storageMode = .shared
        let tex = device.makeTexture(descriptor: desc)!
        let cb = queue.makeCommandBuffer()!
        encode(cb, target: tex, params: params(targetWidth: Double(w), targetHeight: Double(h)))
        cb.commit()
        cb.waitUntilCompleted()
        var out = [UInt32](repeating: 0, count: w * h)
        out.withUnsafeMutableBytes {
            tex.getBytes($0.baseAddress!, bytesPerRow: w * 4, from: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0)
        }
        var mismatches = 0
        for y in 0..<h {
            let src = surface.row(y)
            for x in 0..<w where out[y * w + x] != (src[x] & 0x00FF_FFFF) | 0xFF00_0000 {
                mismatches += 1
            }
        }
        return mismatches
    }

    func produce() {
        var frame = 1
        let interval = 1.0 / producerFPS
        var next = CACurrentMediaTime()
        while running {
            surface.animate(frame: frame)
            frame += 1
            seq.withLock { $0 += 1 }
            next += interval
            let delay = next - CACurrentMediaTime()
            if delay > 0 { Thread.sleep(forTimeInterval: delay) }
        }
    }

    @objc func tick(_ link: CADisplayLink) {
        ticks += 1
        frameDurations.append(link.duration)
        if layer.drawableSize.width != view.bounds.width * window.backingScaleFactor {
            updateDrawableSize()
        }
        let current = seq.withLock { $0 }
        if !presentAlways && current == lastPresentedSeq { return }
        changedTicks += 1
        guard let drawable = layer.nextDrawable() else {
            noDrawable += 1
            return
        }
        lastPresentedSeq = current
        let cb = queue.makeCommandBuffer()!
        encode(cb, target: drawable.texture,
               params: params(targetWidth: Double(layer.drawableSize.width),
                              targetHeight: Double(layer.drawableSize.height)))
        cb.present(drawable)
        cb.addCompletedHandler { [gpuTimes] cb in
            let t = cb.gpuEndTime - cb.gpuStartTime
            if t > 0 { gpuTimes.withLock { $0.append(t) } }
        }
        cb.commit()
        presents += 1
    }

    func finish(error: String?) {
        let elapsed = max(linkEnd - linkStart, 0.001)
        let meanDuration = frameDurations.isEmpty ? 0 : frameDurations.reduce(0, +) / Double(frameDurations.count)
        let displayHz = meanDuration > 0 ? 1 / meanDuration : 0
        let tickHz = Double(ticks) / elapsed
        let presentHz = Double(presents) / elapsed
        let gpu = gpuTimes.withLock { $0 }
        let gpuMs = gpu.isEmpty ? 0 : gpu.reduce(0, +) / Double(gpu.count) * 1000
        let producerFrames = seq.withLock { $0 }
        // The presenter's job: present on every display tick that has new content, never
        // starve for drawables. Frames a free-running producer overwrites between ticks
        // ("superseded") are informational: pacing the producer is the guest's job (VSYNC).
        var failures: [String] = []
        if let error { failures.append(error) }
        if selfTestMismatches != 0 { failures.append("self-test mismatches: \(selfTestMismatches)") }
        if error == nil && tickHz < 0.9 * displayHz { failures.append("display link ran at \(tickHz) Hz") }
        if noDrawable > 0 { failures.append("no drawable on \(noDrawable) ticks") }
        if presentAlways && Double(presents) < 0.95 * Double(ticks) {
            failures.append("presented \(presents) of \(ticks) ticks")
        }
        let result: [String: Any] = [
            "size": "\(surface.width)x\(surface.height)", "stride": surface.stride, "offset": surface.offset,
            "mapped_bytes": surface.length, "base_page_aligned": Int(bitPattern: surface.base) % Int(getpagesize()) == 0,
            "selftest_mismatches": selfTestMismatches, "display_hz": round(displayHz * 10) / 10,
            "tick_hz": round(tickHz * 10) / 10, "present_hz": round(presentHz * 10) / 10,
            "no_drawable": noDrawable, "gpu_ms_avg": round(gpuMs * 1000) / 1000,
            "changed_ticks": changedTicks, "producer_frames": producerFrames,
            "superseded_frames": presentAlways ? 0 : max(0, producerFrames - presents),
            "pass": failures.isEmpty, "failures": failures,
        ]
        let json = try! JSONSerialization.data(withJSONObject: result, options: [.sortedKeys])
        print("RESULT " + String(data: json, encoding: .utf8)!)
        exit(failures.isEmpty ? 0 : 1)
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let presenter = Presenter()
app.delegate = presenter
app.run()
