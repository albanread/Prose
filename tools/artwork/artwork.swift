// artwork: renders the PROSE desktop artwork.
//
//   swift tools/artwork/artwork.swift <output-directory>
//
// Design brief: a desktop background is a backdrop, not a poster. Icons sit
// top-left and the Deskbar top-right, so both corners stay quiet and
// low-contrast; everything interesting happens low and to the right, cropped
// by the edge so it reads as a mark rather than a sticker. No full-screen
// ruled lines - they fight icon labels and window text at every size.
//
// The motif is a single calligraphic stroke: one sweep of a broad-nib pen,
// thin at the entry, swelling through the belly, thin again as it leaves the
// frame. It is drawn at very low contrast, so it survives being scaled to any
// mode we set and never competes with what the user is doing.
import AppKit
import CoreGraphics
import Foundation

let outDir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
let W = 3840, H = 2160

// MARK: - helpers

func srgb(_ r: Double, _ g: Double, _ b: Double, _ a: Double = 1) -> CGColor {
    CGColor(srgbRed: r / 255, green: g / 255, blue: b / 255, alpha: a)
}

/// A pen stroke: a cubic spine swept by a pressure profile, as a closed path.
/// Offsetting the spine by the profile on both sides gives the broad-nib swell
/// that a plain stroked line cannot.
func penStroke(spine: [CGPoint], width: (Double) -> Double, nibAngle: Double) -> CGPath {
    let steps = 240
    var left: [CGPoint] = [], right: [CGPoint] = []
    func bezier(_ t: Double) -> (CGPoint, CGPoint) {
        let p = spine, u = 1 - t
        // position on the cubic
        let x = u*u*u * p[0].x + 3*u*u*t * p[1].x + 3*u*t*t * p[2].x + t*t*t * p[3].x
        let y = u*u*u * p[0].y + 3*u*u*t * p[1].y + 3*u*t*t * p[2].y + t*t*t * p[3].y
        // tangent
        let dx = 3*u*u * (p[1].x - p[0].x) + 6*u*t * (p[2].x - p[1].x) + 3*t*t * (p[3].x - p[2].x)
        let dy = 3*u*u * (p[1].y - p[0].y) + 6*u*t * (p[2].y - p[1].y) + 3*t*t * (p[3].y - p[2].y)
        return (CGPoint(x: x, y: y), CGPoint(x: dx, y: dy))
    }
    for i in 0...steps {
        let t = Double(i) / Double(steps)
        let (pt, tan) = bezier(t)
        // a broad nib is held at a fixed angle, so the offset direction is the
        // nib normal, not the curve normal: that is what varies the weight
        let n = CGPoint(x: cos(nibAngle), y: sin(nibAngle))
        _ = tan
        let w = width(t)
        left.append(CGPoint(x: pt.x + n.x * w, y: pt.y + n.y * w))
        right.append(CGPoint(x: pt.x - n.x * w, y: pt.y - n.y * w))
    }
    let path = CGMutablePath()
    path.move(to: left[0])
    for p in left.dropFirst() { path.addLine(to: p) }
    for p in right.reversed() { path.addLine(to: p) }
    path.closeSubpath()
    return path
}

/// Very fine grain, so a flat field does not look like a gradient test card on
/// a good display. Rendered once into a tile and repeated.
func grainImage(_ size: Int, alpha: Double, seed: UInt64) -> CGImage {
    var rng = SystemRandomNumberGenerator()
    _ = rng
    var state = seed
    func rand() -> Double {
        state = state &* 6364136223846793005 &+ 1442695040888963407
        return Double((state >> 33) & 0xFFFF) / 65535.0
    }
    let cs = CGColorSpaceCreateDeviceRGB()
    let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
                        bytesPerRow: size * 4, space: cs,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    let buf = ctx.data!.bindMemory(to: UInt8.self, capacity: size * size * 4)
    for i in 0..<(size * size) {
        let v = rand()
        let a = UInt8(min(255, max(0, alpha * 255 * v)))
        buf[i * 4 + 0] = a; buf[i * 4 + 1] = a; buf[i * 4 + 2] = a; buf[i * 4 + 3] = a
    }
    return ctx.makeImage()!
}

func render(dark: Bool, to path: String) {
    let cs = CGColorSpaceCreateDeviceRGB()
    let ctx = CGContext(data: nil, width: W, height: H, bitsPerComponent: 8,
                        bytesPerRow: W * 4, space: cs,
                        bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!

    // 1. the field: ink or paper, a slow vertical gradient, darker at the foot
    let top    = dark ? srgb(18, 30, 46)   : srgb(246, 242, 233)
    let bottom = dark ? srgb(7, 12, 20)    : srgb(231, 224, 210)
    let grad = CGGradient(colorsSpace: cs, colors: [top, bottom] as CFArray,
                          locations: [0, 1])!
    ctx.drawLinearGradient(grad, start: CGPoint(x: 0, y: CGFloat(H)),
                           end: CGPoint(x: 0, y: 0), options: [])

    // 2. the stroke: entering low-left, swelling through the belly, leaving
    //    through the right edge. Kept below the icon row and clear of the
    //    Deskbar corner.
    let w = Double(W), h = Double(H)
    let spine = [
        CGPoint(x: w * 0.06, y: h * 0.16),
        CGPoint(x: w * 0.34, y: h * 0.02),
        CGPoint(x: w * 0.72, y: h * 0.62),
        CGPoint(x: w * 1.06, y: h * 0.40),
    ]
    let maxWidth = w * 0.055
    let stroke = penStroke(spine: spine, width: { t in
        // pressure: thin in, full through the middle, thin out
        let p = sin(pow(t, 0.85) * .pi)
        return maxWidth * (0.12 + 0.88 * pow(p, 1.4))
    }, nibAngle: -0.62)
    ctx.saveGState()
    ctx.addPath(stroke)
    ctx.setFillColor(dark ? srgb(150, 195, 255, 0.075) : srgb(60, 80, 110, 0.085))
    ctx.fillPath()
    ctx.restoreGState()

    // a second, much finer trailing stroke - a pen lifting - for depth
    let hair = penStroke(spine: [
        CGPoint(x: w * 0.10, y: h * 0.10),
        CGPoint(x: w * 0.40, y: h * 0.05),
        CGPoint(x: w * 0.74, y: h * 0.44),
        CGPoint(x: w * 1.04, y: h * 0.30),
    ], width: { t in maxWidth * 0.10 * sin(pow(t, 0.9) * .pi) }, nibAngle: -0.62)
    ctx.addPath(hair)
    ctx.setFillColor(dark ? srgb(150, 195, 255, 0.05) : srgb(60, 80, 110, 0.05))
    ctx.fillPath()

    // 3. grain, so large flat areas keep some life
    let tile = grainImage(256, alpha: dark ? 0.035 : 0.030, seed: 0x5eed_1234)
    ctx.saveGState()
    ctx.setBlendMode(dark ? .plusLighter : .multiply)
    ctx.draw(tile, in: CGRect(x: 0, y: 0, width: 256, height: 256), byTiling: true)
    ctx.restoreGState()

    // 4. a soft vignette: light falls off at the edges, and it keeps the
    //    corners quiet where icons and the Deskbar live
    let vignette = CGGradient(colorsSpace: cs,
        colors: [srgb(0, 0, 0, 0), srgb(0, 0, 0, dark ? 0.55 : 0.16)] as CFArray,
        locations: [0.45, 1])!
    ctx.drawRadialGradient(vignette,
        startCenter: CGPoint(x: w * 0.42, y: h * 0.52), startRadius: 0,
        endCenter: CGPoint(x: w * 0.42, y: h * 0.52), endRadius: CGFloat(w * 0.78),
        options: [.drawsAfterEndLocation])

    let image = ctx.makeImage()!
    let url = URL(fileURLWithPath: path)
    let rep = NSBitmapImageRep(cgImage: image)
    try! rep.representation(using: .png, properties: [:])!.write(to: url)
    print("wrote \(path) (\(W)x\(H))")
}

render(dark: true, to: "\(outDir)/PROSE wallpaper - ink dark.png")
render(dark: false, to: "\(outDir)/PROSE wallpaper - paper light.png")
