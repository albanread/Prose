// hvgpu: run a Haiku arm64 disk image in an Apple Virtualization.framework VM
// whose display is OUR custom virtio device (macOS 27 VZCustomVirtioDevice),
// impersonating virtio-gpu (design doc §3.2, phase S1).
//
// The guest needs no changes: Haiku's own virtio_gpu driver binds device 16,
// gets modes from our EDID, renders into a 2D resource backed by guest memory,
// and TRANSFER_TO_HOST_2D/RESOURCE_FLUSH deliver frames. We copy the rects
// into a page-aligned host surface that the Metal presenter (Sprint 0 T5
// design) samples directly. A VZVirtualMachineView sits underneath the Metal
// layer so keyboard and pointer events reach the guest.
//
// usage: hvgpu <disk.img> [options]
//   --size WxH    scanout size advertised via EDID/pmodes (default 1280x800)
//   --ramconsole-log PATH   where the guest-RAM log goes (default ./ramconsole.log)
//   --no-ramconsole         don't scan guest RAM for Haiku's logs
//   --disk nvme|usb|virtio  boot disk interface (default nvme; virtio works with fork patch 0003)
//   --display s2|s1         our display device: s2 is the shared-surface Prose Display
//                           (default; docs/s2-display-device.md), the only one that follows
//                           the window. s1 impersonates virtio-gpu: a fixed scanout, kept
//                           for comparison and for bisecting display problems.
//   --pool-mib N            s2 surface pool size (default 128)
//   --screenshot PATH       s2: dump the presentation surface as PNG every 10 s (with the stats line)
//   --resize-after N WxH    resize our window after N seconds (tests MODE_HINT / live resize)
//   --resize-drag N WxH     animate the window to WxH from N seconds on, like a hand drag (40 steps)
//   --commit-stats          s2: list the most frequently committed rects with each stats line
//   --no-snow               don't show analogue TV static while the guest has no picture
//   --input own|vz          own: our virtio-input keyboard + tablet, window entirely ours (default);
//                           vz: VZ's USB keyboard/pointer + virtio-gpu + VZVirtualMachineView
//   --input-test            own input: click the Deskbar leaf, Escape, park the pointer (screenshots)
//   --name NAME             the VM's name, shown under the window title (run-vz.sh: the run name)
//   --mac auto|random|MAC   guest MAC; auto (default) derives it from the disk image's path, so the
//                           VM keeps its DHCP lease and IP address across runs
//   --no-toolbar            start without the toolbar (View > Show Toolbar brings it back)
//   --no-statusbar          start without the status bar (View > Show Status Bar)
//   --exit-on-stop          quit when the guest powers off (the default with --headless, --seconds
//                           and --input-test); --stay-on-stop keeps the window, with a Start button
//   --script "T:step,..."   run an automation command T seconds after launch; a step is a
//                           command line ("capture path=/tmp/a.png"), tests; docs/automation.md
//   --automation            allow automation for this run without the menu item
//   --no-sound              no virtio-snd device (default: output+input to the Mac's audio devices)
//   --no-midi               no Prose MIDI device (default: guest MIDI -> Mac's GM synth + CoreMIDI)
//   --no-portal             no Prose Portal device even with automation allowed (docs/automation.md)
//   --no-synth              keep the CoreMIDI endpoints but don't play guest MIDI on the Mac's synth
//   --midi-log              log every MIDI message from the guest
//   --share PATH            HostFS: share a macOS directory read-write; the guest mounts it
//                           as a disk (repeatable; hostfs.swift)
//   --share-ro PATH         HostFS: the same, read-only
//   --share-tag TAG         HostFS: virtio-fs tag = Haiku volume name (default HostFS)
//   (same options as hvz: --efivars --cpus --memory --seconds --grace --serial
//    --nested --no-net --headless)
//
// The window is the Prose app (tools/build.sh bundles hvgpu as build/Prose.app): a menu
// bar (menus.swift), a toolbar with the VM controls and a status bar with activity lights
// (chrome.swift, controls.swift, monitor.swift). Its shortcuts are ⌃⌘ chords; ⌘ is the guest's.
import AppKit
import Metal
import QuartzCore
import Virtualization
import os

setvbuf(stdout, nil, _IONBF, 0)

let args = CommandLine.arguments
func option(_ name: String) -> String? {
    // The last occurrence wins, so wrappers can put defaults first.
    guard let i = args.lastIndex(of: name), i + 1 < args.count else { return nil }
    return args[i + 1]
}
/// The installed machine: ~/Library/Application Support/Prose/Machines/Prose.image.
///
/// An installed copy is double-clicked, not given a disk on a command line, and
/// an application that exits 64 when launched that way is not an application --
/// it also cannot be scripted or opened by anything that asks the system to
/// launch it. The first run copies the machine out of the bundle; after that it
/// is the user's, and reinstalling does not touch it.
func installedMachine() -> String? {
    let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
    let folder = support.appendingPathComponent("Prose/Machines", isDirectory: true)
    let disk = folder.appendingPathComponent("Prose.image")
    if FileManager.default.fileExists(atPath: disk.path) { return disk.path }
    guard let template = Bundle.main.url(forResource: "prose", withExtension: "image") else { return nil }
    do {
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try FileManager.default.copyItem(at: template, to: disk)
        FileManager.default.createFile(atPath: folder.appendingPathComponent(".firstrun").path, contents: nil)
        return disk.path
    } catch {
        return nil
    }
}

/// --write-iconset DIR: the Finder icon, from the same drawing as the Dock icon.
/// The build turns the set into Contents/Resources/Prose.icns; one drawing, one
/// look everywhere, and no image file to keep in step with the code.
if let dir = option("--write-iconset") {
    _ = NSApplication.shared
    let folder = URL(fileURLWithPath: dir, isDirectory: true)
    try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    for points in [16, 32, 128, 256, 512] {
        for scale in [1, 2] {
            let pixels = points * scale
            guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
                                             bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                             colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0),
                  let context = NSGraphicsContext(bitmapImageRep: rep) else { continue }
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = context
            ProseIcon.image(size: CGFloat(pixels)).draw(in: NSRect(x: 0, y: 0, width: pixels, height: pixels))
            NSGraphicsContext.restoreGraphicsState()
            let name = "icon_\(points)x\(points)\(scale == 2 ? "@2x" : "").png"
            try? rep.representation(using: .png, properties: [:])?.write(to: folder.appendingPathComponent(name))
        }
    }
    exit(0)
}

var diskArgument: String? = args.count >= 2 && !args[1].hasPrefix("--") ? args[1] : nil
/// No disk on the command line: this is an installed copy, opened rather than run
/// by a script. It sets up its own folders (hostfs.swift shares one by default).
let usingInstalledMachine = diskArgument == nil
if diskArgument == nil { diskArgument = installedMachine() }
guard let diskPath = diskArgument else {
    print("usage: hvgpu <disk.img> [--size WxH] [--efivars path] [--cpus n] "
        + "[--memory GiB] [--seconds n] [--grace n] [--serial path] [--nested] [--no-net] [--headless]")
    print("with no disk, Prose uses ~/Library/Application Support/Prose/Machines/Prose.image,")
    print("copying it out of the application on the first run. This copy has no machine in it.")
    exit(64)
}

let diskURL = URL(fileURLWithPath: diskPath)
let varsURL = URL(fileURLWithPath: option("--efivars") ?? diskPath + ".efivars")
let cpus = Int(option("--cpus") ?? "4") ?? 4
let memoryGiB = UInt64(option("--memory") ?? "4") ?? 4
let dims = (option("--size") ?? "1280x800").split(separator: "x").compactMap { Int($0) }
let (width, height) = (dims.count == 2 ? dims[0] : 1280, dims.count == 2 ? dims[1] : 800)
let runSeconds = option("--seconds").flatMap(Double.init)
let grace = Double(option("--grace") ?? "8") ?? 8
let headless = args.contains("--headless")
let displayMode = option("--display") ?? "s2"
let ownInput = option("--input") != "vz"
var inputRouter: InputRouter?        // set when our own input devices are in use

let startTime = Date()
func log(_ event: String) {
    print(String(format: "HVZ %7.2f ", Date().timeIntervalSince(startTime)) + event)
}

/// Serializes writes into a display surface (device copies) with the presenter's GPU read
/// of it: the presenter holds it from encoding until the GPU is done, so a frame can never
/// show a half-written surface.
let presentLock = NSLock()

// MARK: - EDID (single detailed timing: WxH@60 reduced blanking)

func makeEDID(width: Int, height: Int) -> [UInt8] {
    var e = [UInt8](repeating: 0, count: 128)
    e[0...7] = [0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00]
    e[8] = 0x06; e[9] = 0xAF          // manufacturer "VZT"-ish (unchecked by guests)
    e[10] = 0x11; e[11] = 0x50        // product code
    e[18] = 36                        // model year 2026 (week 0)
    e[20] = 0x01; e[21] = 0x04        // EDID 1.4
    e[22] = 0xA5                      // digital, 8bpc
    e[23] = UInt8(min(255, width / 10))    // horizontal size cm
    e[24] = UInt8(min(255, height / 10))   // vertical size cm
    e[25] = 120                       // gamma 2.2
    e[26] = 0x80                      // features: preferred timing is native
    e[35] = 0x6E                      // established timings (barely used)
    for i in stride(from: 38, to: 54, by: 2) { e[i] = 0x01; e[i + 1] = 0x01 } // unused std timings

    // Detailed timing: CVT reduced blanking, 60 Hz.
    let hFront = 48, hSync = 32, hBack = 80, vFront = 3, vSync = 6, vBack = 14
    let hBlank = hFront + hSync + hBack, vBlank = vFront + vSync + vBack
    let hTotal = width + hBlank, vTotal = height + vBlank
    // EDID pixel clock is in 10 kHz units.
    let clock10k = Int((Double(hTotal) * Double(vTotal) * 60.0 / 10_000.0).rounded())
    var d = Array(e[54..<72])   // copy: a slice would keep indices 54..<72
    d[0] = UInt8(clock10k & 0xFF); d[1] = UInt8((clock10k >> 8) & 0xFF)
    d[2] = UInt8(width & 0xFF); d[3] = UInt8(hBlank & 0xFF)
    d[4] = UInt8(((hBlank >> 8) << 4) | (width >> 8))
    d[5] = UInt8(height & 0xFF); d[6] = UInt8(vBlank & 0xFF)
    d[7] = UInt8(((vBlank >> 8) << 4) | (height >> 8))
    d[8] = UInt8(hFront); d[9] = UInt8(hSync)
    d[10] = UInt8(vSync << 4 | vFront)
    d[11] = 0                          // upper sync/front-porch bits
    d[12] = UInt8(min(255, width / 10)); d[13] = UInt8(min(255, height / 10))
    d[14] = UInt8((((height / 10) >> 8) << 4) | ((width / 10) >> 8)) // image size msb nibbles
    d[17] = 0x18                       // digital, separate sync
    e.replaceSubrange(54..<72, with: d)

    // Descriptor 2: range limits; 3: monitor name; 4: unused.
    e[72] = 0xFD
    e[73] = 30; e[74] = 255            // min/max vertical rate... (informational)
    e[75] = 30; e[76] = 160            // min/max horizontal kHz
    e[77] = 255                        // max pixel clock /10MHz (2550 MHz)
    e[83] = 0x0A
    e[90] = 0xFC
    for (i, c) in "hvgpu".utf8.enumerated() { e[91 + i] = c }
    e[108] = 0x10
    e[126] = 0                         // no extension
    let sum = e.prefix(127).reduce(0) { $0 + Int($1) }
    e[127] = UInt8((256 - sum) & 0xFF)
    return e
}

// MARK: - Metal presenter (Sprint 0 T5 design: shader samples the surface)

let shaderSource = """
#include <metal_stdlib>
using namespace metal;

struct VOut { float4 pos [[position]]; float2 uv; };

struct Params {
    uint width; uint height; uint strideWords; uint offsetWords;
    float2 scale; float2 bias; uint smooth;
};

vertex VOut vmain(uint vid [[vertex_id]]) {
    float2 p = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    VOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return o;
}

// B_RGB32: bytes B, G, R, X with undefined X, so alpha is forced to 1.
static inline float3 texel(device const uint *fb, constant Params &p, uint x, uint y) {
    uint v = fb[p.offsetWords + min(y, p.height - 1) * p.strideWords + min(x, p.width - 1)];
    return float3(float((v >> 16) & 0xFF), float((v >> 8) & 0xFF), float(v & 0xFF)) / 255.0;
}

fragment float4 fmain(VOut in [[stage_in]],
                      device const uint *fb [[buffer(0)]],
                      constant Params &p [[buffer(1)]]) {
    float2 uv = (in.uv - p.bias) / p.scale;
    if (any(uv < 0.0) || any(uv >= 1.0))
        return float4(0.0, 0.0, 0.0, 1.0);
    float2 t = uv * float2(p.width, p.height);
    if (p.smooth == 0) {
        // nearest: every guest pixel is a hard block, which is what you want
        // when the drawable is an exact multiple of the guest's mode
        return float4(texel(fb, p, uint(t.x), uint(t.y)), 1.0);
    }
    // bilinear about the texel centres, so a non-integer scale does not shimmer
    float2 c = t - 0.5;
    float2 f = fract(clamp(c, 0.0, float2(p.width, p.height)));
    uint2 i = uint2(max(c, 0.0));
    float3 a = mix(texel(fb, p, i.x, i.y), texel(fb, p, i.x + 1, i.y), f.x);
    float3 b = mix(texel(fb, p, i.x, i.y + 1), texel(fb, p, i.x + 1, i.y + 1), f.x);
    return float4(mix(a, b, f.y), 1.0);
}

// No signal: an untuned analogue TV. Snow is luminance noise smeared
// horizontally (a scanline is a band-limited continuous signal, so the grain
// is wider than it is tall - that is what makes it read as analogue rather
// than as digital dither), under scanlines, a slow hum bar, rare vertical-hold
// slips and a little corner falloff.
struct SnowParams { float2 size; float time; uint frame; float scale; };

static inline float hash(uint3 v) {
    uint h = v.x * 0x8da6b343u + v.y * 0xd8163841u + v.z * 0xcb1ab31fu;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return float(h & 0x00ffffffu) / float(0x01000000u);
}

fragment float4 fsnow(VOut in [[stage_in]], constant SnowParams &p [[buffer(0)]]) {
    // Work in logical pixels: on a Retina drawable, hashing per device pixel
    // halves the grain and it stops looking like a tube.
    float2 px = in.uv * p.size / max(p.scale, 1.0);

    // vertical hold: every few seconds the picture slips for a frame or two
    float slipSeed = hash(uint3(0u, uint(p.time * 3.0), 99u));
    if (slipSeed > 0.97)
        px.y += (slipSeed - 0.97) * 600.0;

    // snow: three horizontal taps, so the grain is ~3px wide and 1px tall
    uint row = uint(max(px.y, 0.0));
    uint col = uint(max(px.x, 0.0));
    float n = 0.0;
    n += hash(uint3(col, row, p.frame)) * 0.5;
    n += hash(uint3(col - 1u, row, p.frame)) * 0.25;
    n += hash(uint3(col + 1u, row, p.frame)) * 0.25;

    // snow sits in the greys with plenty of contrast, plus bright sparkle
    float luma = 0.10 + pow(n, 0.85) * 0.82;
    float sparkle = hash(uint3(col, row, p.frame ^ 0x5bd1u));
    if (sparkle > 0.990)
        luma = min(1.0, luma + 0.5);

    // hum bar: a wide soft band drifting slowly down the screen
    float bar = fract(in.uv.y - p.time * 0.08);
    luma *= 1.0 + 0.16 * exp(-pow((bar - 0.5) * 3.2, 2.0));

    // scanlines
    luma *= (row & 1u) == 0u ? 1.0 : 0.88;

    // a hint of chroma noise, as a colour decoder guessing at nothing
    float3 rgb = float3(luma);
    float chroma = hash(uint3(col, row, p.frame ^ 0x2f19u));
    if (chroma > 0.90) {
        float3 tint = float3(hash(uint3(col, row, p.frame ^ 1u)),
                             hash(uint3(col, row, p.frame ^ 2u)),
                             hash(uint3(col, row, p.frame ^ 3u)));
        rgb = mix(rgb, rgb * (0.6 + tint * 0.8), 0.35);
    }

    // corner falloff, like light dropping off at the edge of the tube
    float2 c = in.uv * 2.0 - 1.0;
    rgb *= 1.0 - 0.28 * dot(c, c) * 0.5;

    return float4(rgb, 1.0);
}
"""

struct SnowParams {
    var width: Float = 0
    var height: Float = 0
    var time: Float = 0
    var frame: UInt32 = 0
    var scale: Float = 1
}

/// How the presenter maps the guest's screen onto the window's device pixels.
///
/// A Retina drawable is `backingScaleFactor` times the window's size in points,
/// so there is a real choice: either the guest draws at that full resolution --
/// its own font rendering, one guest pixel per screen pixel -- or it draws at
/// the point size and we magnify, which magnifies its antialiasing with it and
/// is why text looks soft. Native is sharpest but halves the apparent size of
/// everything, so the guest's font size wants raising to match.
enum PresenterMode: String, CaseIterable {
    case native, crisp, smooth

    static let defaultsKey = "prose.presenterMode"

    var title: String {
        switch self {
        case .native: return "Native Resolution"
        case .crisp: return "Magnified (Crisp)"
        case .smooth: return "Magnified (Smooth)"
        }
    }

    var detail: String {
        switch self {
        case .native:
            return "One guest pixel per screen pixel: Prose draws its own text at the "
                 + "display's full resolution. Everything is half the size, so raise the "
                 + "font size in Prose to match."
        case .crisp:
            return "Prose draws at the window's point size and each of its pixels becomes "
                 + "a block on a Retina display."
        case .smooth:
            return "As magnified, but interpolated: soft edges rather than blocks."
        }
    }

    /// Guest pixels per point.
    func guestScale(_ backing: CGFloat) -> CGFloat { self == .native ? backing : 1 }

    static var current = PresenterMode(
        rawValue: UserDefaults.standard.string(forKey: defaultsKey) ?? "") ?? .crisp
}

/// The guest's first mode, in its own pixels. --size gives the window its size in
/// points; Native mode hands the guest every screen pixel inside them, so it must
/// start at that size -- the driver takes its first mode from the device config,
/// and nothing resizes the window on the way up to correct it later.
func initialGuestSize() -> (width: Int, height: Int) {
    guard !headless else { return (width, height) }
    let scale = PresenterMode.current.guestScale(NSScreen.main?.backingScaleFactor ?? 1)
    return (Int(CGFloat(width) * scale), Int(CGFloat(height) * scale))
}

struct ShaderParams {
    var width: UInt32
    var height: UInt32
    var strideWords: UInt32
    var offsetWords: UInt32
    var scale: SIMD2<Float>
    var bias: SIMD2<Float>
    var smooth: UInt32
}

/// The host-side frame surface: page-aligned, Metal-wrapped, written by the
/// device on TRANSFER, sampled by the shader on present.
final class FrameSurface {
    var width: Int          // the current mode; 0 = nothing to show (present black)
    var height: Int
    var stride: Int
    var offset: Int = 0     // byte offset of the image inside the buffer (s2: pool offset)
    let length: Int
    let base: UnsafeMutableRawPointer

    /// A surface with its own page-aligned buffer (S1: filled by TRANSFER_TO_HOST_2D).
    init?(width: Int, height: Int) {
        self.width = width
        self.height = height
        stride = width * 4
        let page = Int(getpagesize())
        length = (stride * height + page - 1) / page * page
        guard let p = mmap(nil, length, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0),
              p != UnsafeMutableRawPointer(bitPattern: -1) else { return nil }
        base = p
        memset(base, 0, length)
    }

    /// A view onto memory owned by someone else (S2: the surface pool the guest draws into).
    init(base: UnsafeMutableRawPointer, length: Int) {
        self.base = base
        self.length = length
        width = 0
        height = 0
        stride = 0
    }
}

// MARK: - The custom virtio-gpu device (S1)

// Numbering per the virtio 1.3 spec and Haiku's viogpu.h (a sequential enum).
let VIRTIO_GPU_CMD_GET_DISPLAY_INFO: UInt32 = 0x0100
let VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: UInt32 = 0x0101
let VIRTIO_GPU_CMD_RESOURCE_UNREF: UInt32 = 0x0102
let VIRTIO_GPU_CMD_SET_SCANOUT: UInt32 = 0x0103
let VIRTIO_GPU_CMD_RESOURCE_FLUSH: UInt32 = 0x0104
let VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: UInt32 = 0x0105
let VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: UInt32 = 0x0106
let VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING: UInt32 = 0x0107
let VIRTIO_GPU_CMD_GET_EDID: UInt32 = 0x010A
let VIRTIO_GPU_CMD_UPDATE_CURSOR: UInt32 = 0x0300
let VIRTIO_GPU_CMD_MOVE_CURSOR: UInt32 = 0x0301
let VIRTIO_GPU_RESP_OK_NODATA: UInt32 = 0x1100
let VIRTIO_GPU_RESP_OK_DISPLAY_INFO: UInt32 = 0x1101
let VIRTIO_GPU_RESP_OK_EDID: UInt32 = 0x1104
let VIRTIO_GPU_RESP_ERR_UNSPEC: UInt32 = 0x1200
let VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID: UInt32 = 0x1203
let VIRTIO_GPU_FLAG_FENCE: UInt32 = 1 << 0
let VIRTIO_GPU_F_EDID: UInt32 = 1 << 1

struct CtrlHeader {
    var type: UInt32 = 0
    var flags: UInt32 = 0
    var fenceID: UInt64 = 0
    var ctxID: UInt32 = 0
    var ringIdx: UInt32 = 0
    static let size = 24

    init(_ data: Data) {
        guard data.count >= CtrlHeader.size else { return }
        type = leU32(data, 0)
        flags = leU32(data, 4)
        fenceID = leU64(data, 8)
        ctxID = leU32(data, 16)
        ringIdx = leU32(data, 20)
    }

    /// Response header: same fence (flag and id) and context as the request.
    func response(type: UInt32) -> [UInt8] {
        le32(type) + le32(flags & VIRTIO_GPU_FLAG_FENCE) + le64(fenceID) + le32(ctxID) + le32(0)
    }
}

func le32(_ v: UInt32) -> [UInt8] { (0..<4).map { UInt8((v >> (8 * $0)) & 0xFF) } }
func le64(_ v: UInt64) -> [UInt8] { (0..<8).map { UInt8((v >> (8 * $0)) & 0xFF) } }
func leU32(_ d: Data, _ o: Int) -> UInt32 {
    guard d.count >= o + 4 else { return 0 }
    let b = d.startIndex + o
    return UInt32(d[b]) | UInt32(d[b + 1]) << 8 | UInt32(d[b + 2]) << 16 | UInt32(d[b + 3]) << 24
}
func leU64(_ d: Data, _ o: Int) -> UInt64 { UInt64(leU32(d, o)) | UInt64(leU32(d, o + 4)) << 32 }

final class Resource {
    let id: UInt32
    let width: Int, height: Int
    var mapping: VZGuestMemoryMapping?
    var backingAddr: UInt64 = 0
    var backingLength: Int = 0
    var backingPtr: UnsafeMutableRawPointer?

    init(id: UInt32, width: Int, height: Int) {
        self.id = id
        self.width = width
        self.height = height
    }

    /// Resolve the backing pointer through a guest memory mapping, if possible.
    func resolveBacking(device: VZCustomVirtioDevice) {
        if backingPtr == nil, backingLength > 0,
           let m = device.guestMemoryMapping(atPhysicalAddress: backingAddr, length: backingLength) {
            mapping = m
            backingPtr = m.mutableBytes
        }
    }
}

final class CustomVirtioGPU: NSObject, VZCustomVirtioDeviceConfigurationDelegate, VZCustomVirtioDeviceDelegate {
    static let sharedQueue = DispatchQueue(label: "hvgpu.device")
    let edid = makeEDID(width: width, height: height)
    let surface: FrameSurface?
    private(set) var device: VZCustomVirtioDevice?
    var resources: [UInt32: Resource] = [:]
    var scanoutResource: UInt32 = 0
    let seq = OSAllocatedUnfairLock(initialState: 0)
    var frames = 0
    weak var presenter: Presenter?
    var ramConsole: RAMConsole?

    override init() {
        surface = FrameSurface(width: width, height: height)
        super.init()
        if surface == nil { log("FATAL: surface allocation failed") }
    }

    var configuration: VZCustomVirtioDeviceConfiguration {
        let cfg = VZCustomVirtioDeviceConfiguration()
        cfg.deviceID = UInt16(option("--probe-id") ?? "") ?? 16
        cfg.pciClassID = UInt8(option("--probe-class") ?? "", radix: 16) ?? 0x03
        cfg.pciSubclassID = UInt8(option("--probe-subclass") ?? "", radix: 16) ?? 0x80
        cfg.virtioQueueCount = UInt16(option("--probe-queues") ?? "") ?? 2
        // Bisect switches: --no-edid / --no-devcfg drop the GPU extras so the
        // framework's negotiation with EDK2 can be narrowed down.
        if !args.contains("--no-edid") {
            cfg.optionalFeatures.subset0 = VIRTIO_GPU_F_EDID // VZ always offers VERSION_1
        }
        if !args.contains("--no-devcfg") {
            var config = [UInt32](repeating: 0, count: 4)
            config[2] = 1                          // num_scanouts
            cfg.deviceSpecificConfiguration = VZVirtioDeviceSpecificConfiguration(
                configurationData: Data(bytes: &config, count: 16))
        }
        cfg.provider = VZCustomVirtioDeviceDelegateProvider(deviceQueue: CustomVirtioGPU.sharedQueue,
                                                             delegate: self)
        return cfg
    }

    // VZCustomVirtioDeviceConfigurationDelegate
    func customVirtioConfiguration(_ deviceConfiguration: VZCustomVirtioDeviceConfiguration,
                                   didCreateDevice device: VZCustomVirtioDevice) {
        self.device = device
        device.delegate = self
        log("virtio-gpu device created")
        if !args.contains("--no-ramconsole") {
            ramConsole = RAMConsole(device: device)
        }
    }

    // VZCustomVirtioDeviceDelegate
    func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
        let f = device.negotiatedFeatures
        log("virtio-gpu DRIVER_OK (driver accepted features: "
            + (f.map { String(format: "0x%08x_%08x", $0.subset1, $0.subset0) } ?? "none") + ")")
    }

    func customVirtioDeviceWillReset(_ device: VZCustomVirtioDevice) {
        resources.removeAll()
        mappingReleaseAll()
        scanoutResource = 0
    }

    func customVirtioDeviceWillStop(_ device: VZCustomVirtioDevice) {
        mappingReleaseAll()
    }

    private func mappingReleaseAll() {
        for r in resources.values { r.mapping = nil; r.backingPtr = nil }
    }

    func vmDidStart() {
        ramConsole?.activate()
    }

    /// The machine is off: no picture until the next boot draws one.
    func vmDidStop() {
        CustomVirtioGPU.sharedQueue.async { [self] in
            resources.removeAll()
            scanoutResource = 0
        }
        presentLock.withLock { if let surface { memset(surface.base, 0, surface.length) } }
        seq.withLock { $0 = 0 }
        ramConsole?.reset()
    }

    func customVirtioDevice(_ device: VZCustomVirtioDevice,
                            didReceiveNotificationFor queue: VZVirtioQueue) {
        guard let surface else { return }
        if queue.queueIndex == 1 {          // cursorq: Haiku draws a software cursor
            while let element = queue.nextElement() { element.returnToQueue() }
            return
        }
        while let element = queue.nextElement() {
            let readLength = element.readBuffersByteCount
            var cmdData = [UInt8](repeating: 0, count: readLength)
            guard readLength >= CtrlHeader.size,
                  (try? element.readBytes(intoBuffer: &cmdData, exactLength: readLength)) != nil else {
                element.returnToQueue()
                continue
            }
            let cmd = Data(cmdData)
            let hdr = CtrlHeader(cmd)
            var response: [UInt8]

            switch hdr.type {
            case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
                var r = hdr.response(type: VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
                r += le32(0) + le32(0) + le32(UInt32(width)) + le32(UInt32(height)) // rect
                r += le32(1) + le32(0)                                              // enabled, flags
                r += [UInt8](repeating: 0, count: 15 * 24)                          // 15 empty modes
                response = r
                log("GET_DISPLAY_INFO -> \(width)x\(height)")

            case VIRTIO_GPU_CMD_GET_EDID:
                var r = hdr.response(type: VIRTIO_GPU_RESP_OK_EDID)
                r += le32(UInt32(edid.count)) + le32(0)
                r += edid
                r += [UInt8](repeating: 0, count: 1024 - edid.count)
                response = r
                log("GET_EDID -> \(edid.count) bytes")

            case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
                // hdr, resource_id, format, width, height
                let id = u32(cmd, 24), w = u32(cmd, 32), h = u32(cmd, 36)
                resources[id] = Resource(id: id, width: Int(w), height: Int(h))
                response = hdr.response(type: VIRTIO_GPU_RESP_OK_NODATA)
                log("RESOURCE_CREATE_2D id=\(id) \(w)x\(h)")

            case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
                // hdr, resource_id, nr_entries, then entries {addr u64, length u32, pad u32}
                let id = u32(cmd, 24)
                let nr = Int(u32(cmd, 28))
                if let res = resources[id], nr >= 1, cmd.count >= 32 + nr * 16 {
                    res.backingAddr = u64(cmd, 32)
                    res.backingLength = Int(u32(cmd, 40))
                    res.resolveBacking(device: device)
                    log("ATTACH_BACKING id=\(id) addr=0x\(String(res.backingAddr, radix: 16)) "
                        + "len=\(res.backingLength) mapped=\(res.backingPtr != nil)")
                }
                response = hdr.response(type: VIRTIO_GPU_RESP_OK_NODATA)

            case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
                let id = u32(cmd, 24)
                resources[id]?.mapping = nil
                resources[id]?.backingPtr = nil
                response = hdr.response(type: VIRTIO_GPU_RESP_OK_NODATA)

            case VIRTIO_GPU_CMD_RESOURCE_UNREF:
                let id = u32(cmd, 24)
                resources[id] = nil
                response = hdr.response(type: VIRTIO_GPU_RESP_OK_NODATA)

            case VIRTIO_GPU_CMD_SET_SCANOUT:
                // hdr, rect(x,y,w,h), scanout_id, resource_id
                let id = u32(cmd, 44)
                scanoutResource = id
                response = hdr.response(type: VIRTIO_GPU_RESP_OK_NODATA)
                log("SET_SCANOUT resource=\(id)")

            case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
                // hdr, rect(x,y,w,h), offset u64, resource_id
                let id = u32(cmd, 48)
                if let res = resources[id] {
                    if id == scanoutResource {
                        transfer(res: res, cmd: cmd, surface: surface, device: device)
                    }
                    response = hdr.response(type: VIRTIO_GPU_RESP_OK_NODATA)
                } else {
                    response = hdr.response(type: VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID)
                }

            case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
                frames += 1
                seq.withLock { $0 += 1 }
                response = hdr.response(type: VIRTIO_GPU_RESP_OK_NODATA)

            default:
                log("unhandled ctrl type 0x\(String(hdr.type, radix: 16)) (\(readLength)B)")
                response = hdr.response(type: VIRTIO_GPU_RESP_ERR_UNSPEC)
            }

            _ = try? element.write(Data(response))
            element.returnToQueue()
        }
    }

    private func u32(_ d: Data, _ o: Int) -> UInt32 {
        guard d.count >= o + 4 else { return 0 }
        return UInt32(d[o]) | UInt32(d[o + 1]) << 8 | UInt32(d[o + 2]) << 16 | UInt32(d[o + 3]) << 24
    }

    private func u64(_ d: Data, _ o: Int) -> UInt64 {
        guard d.count >= o + 8 else { return 0 }
        var v: UInt64 = 0
        for i in (0..<8).reversed() { v = v << 8 | UInt64(d[o + i]) }
        return v
    }

    /// Copy the transferred rect from the resource's guest backing into the
    /// host surface. S1 uses one copy per rect; zero-copy aliasing comes later.
    private func transfer(res: Resource?, cmd: Data, surface: FrameSurface, device: VZCustomVirtioDevice) {
        guard let res else { return }
        res.resolveBacking(device: device)
        guard let srcBase = res.backingPtr else { return }
        let x = Int(u32(cmd, 24)), y = Int(u32(cmd, 28))
        let w = min(Int(u32(cmd, 32)), min(res.width, surface.width) - x)
        let h = min(Int(u32(cmd, 36)), min(res.height, surface.height) - y)
        let offset = u64(cmd, 40)
        let srcStride = res.width * 4
        let dstStride = surface.stride
        guard w > 0, h > 0, Int(offset) + (h - 1) * srcStride + w * 4 <= res.backingLength else { return }
        // Per the spec (and QEMU): row r of the rect starts at backing + offset + r * stride.
        presentLock.withLock {
            for row in 0..<h {
                let s = srcBase + Int(offset) + row * srcStride
                let d = surface.base + (y + row) * dstStride + x * 4
                memcpy(d, s, w * 4)
            }
        }
    }
}

// MARK: - RAM console: Haiku's boot/kernel logs read straight out of guest RAM

/// VZ gives Haiku no serial port, but a custom virtio device can map guest RAM.
/// The loader keeps its log in a memory buffer, and the kernel keeps its debug
/// output in the syslog ring buffer; both are plain text. Every second, find
/// them by their opening lines and print whatever text is new.
///
/// Searching all of guest RAM (4 GiB by default) takes ~0.1 s, so it isn't done every
/// second: each second re-reads only the places the last search found a marker at and
/// follows the longest buffer, as a full search would. RAM is searched again at once when
/// the followed buffer's marker is gone; every second while a marker is missing in the first
/// minute; when a followed buffer stops growing, at most every 5 s (the loader's log moves
/// into the kernel's syslog buffer that way); and otherwise at intervals that start at 1 s
/// after a change of buffer and double up to 30 s.
final class RAMConsole {
    static let ramBase: UInt64 = 0x7000_0000        // VZ generic platform (Sprint 0 T3)
    // The kernel's RAM log (patches/haiku/0001) holds all kernel debug output from the start.
    static let markers = ["Welcome to the Haiku boot loader!", "HAIKU-RAMLOG-V1"]
    // A search is one memchr pass for a byte every marker contains, each hit checked against
    // the markers: ~0.1 s for 4 GiB, where memmem takes 1.5-2 s per marker.
    static let pivot = UInt8(ascii: "H")
    static let needles = markers.map { Array($0.utf8) }
    static let pivotOffsets = needles.map { $0.firstIndex(of: pivot)! }
    static let maxSearchWait = 30.0
    weak var device: VZCustomVirtioDevice?
    var mapping: VZGuestMemoryMapping?
    let queue = DispatchQueue(label: "hvgpu.ramconsole", qos: .utility)
    var timer: DispatchSourceTimer?
    var printed: [String: Int] = [:]                // marker -> bytes already printed
    var bufferAddress: [String: Int] = [:]          // marker -> offset of the buffer being followed
    var candidates: [String: [Int]] = [:]           // marker -> offsets the last search found it at
    var grown: Set<String> = []                     // markers whose buffer grew since the last search
    var mappedAt = 0.0                              // uptime when this boot's RAM was mapped
    var lastSearch = -Double.infinity
    var searchWait = 1.0                            // seconds from the last search to the next
    var attempts = 0
    var logHandle: FileHandle?
    let logURL = URL(fileURLWithPath: option("--ramconsole-log") ?? "ramconsole.log")

    static var logStarted = false       // one log per run: a restarted VM's boot is appended
    static weak var current: RAMConsole?    // the console to flush when hvgpu exits

    init(device: VZCustomVirtioDevice) {
        self.device = device
        if !RAMConsole.logStarted {
            FileManager.default.createFile(atPath: logURL.path, contents: nil)
            RAMConsole.logStarted = true
            // hvgpu exits as soon as the guest powers off, before the next scan could read
            // the guest's last lines ("arch_cpu_shutdown: PSCI SYSTEM_OFF").
            atexit { RAMConsole.current?.flushBeforeExit() }
        }
        RAMConsole.current = self
        logHandle = FileHandle(forWritingAtPath: logURL.path)
        logHandle?.seekToEndOfFile()
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now() + 1, repeating: 1)
        t.setEventHandler { [weak self] in self?.scan() }
        t.resume()
        timer = t
    }

    deinit { timer?.cancel() }

    /// The machine stopped: print its last lines, then stop scanning until activate(). The
    /// next boot runs in new RAM (a mapping can't outlive a shutdown), so it is mapped and
    /// followed afresh.
    func reset() {
        queue.async { [self] in
            flush()
            active = false
            mapping = nil
            printed.removeAll()
            bufferAddress.removeAll()
            candidates.removeAll()
            grown.removeAll()
            lastSearch = -.infinity
            searchWait = 1
            attempts = 0
        }
    }

    /// Print what the guest wrote since the last scan. Called when it has stopped: the
    /// mapping still shows its RAM as it was left.
    private func flush() {
        guard active, let mapping else { return }
        let base = mapping.mutableBytes.assumingMemoryBound(to: UInt8.self)
        _ = dropMoved(base)
        _ = follow(base, mapping.length)
    }

    /// At exit, on the main thread. Waits at most 2 s, so a scan stuck mapping guest RAM
    /// can't hold up the exit.
    func flushBeforeExit() {
        let done = DispatchSemaphore(value: 0)
        queue.async { [self] in
            flush()
            done.signal()
        }
        _ = done.wait(timeout: .now() + 2)
    }

    func activate() {
        queue.async { [self] in active = true }
    }

    private var active = true

    /// Guest RAM only exists once the VM runs; map it on the device's queue.
    func ensureMapping() -> VZGuestMemoryMapping? {
        if let mapping { return mapping }
        guard let device else { return nil }
        attempts += 1
        let m = device.deviceQueue.sync {
            device.guestMemoryMapping(atPhysicalAddress: RAMConsole.ramBase, length: Int(memoryGiB << 30))
        }
        if let m {
            mapping = m
            mappedAt = ProcessInfo.processInfo.systemUptime
            log("ramconsole: mapped \(m.length >> 20) MiB of guest RAM; log -> \(logURL.path)")
        } else if attempts == 5 {
            log("ramconsole: still cannot map guest RAM at 0x\(String(RAMConsole.ramBase, radix: 16))")
        }
        return m
    }

    func scan() {
        guard active, let mapping = ensureMapping() else { return }
        let base = mapping.mutableBytes.assumingMemoryBound(to: UInt8.self)
        let length = mapping.length
        let now = ProcessInfo.processInfo.systemUptime
        let lost = dropMoved(base)
        let missing = RAMConsole.markers.contains { candidates[$0, default: []].isEmpty }
        let stalled = grown.contains { marker in
            bufferAddress[marker].map { textEnd(base, length, $0) - $0 <= printed[marker] ?? 0 } ?? false
        }
        // When to search: see the class comment. Half a second of slack: the timer jitters.
        let search = lost || (missing && now - mappedAt < 60) || (stalled && now - lastSearch > 4.5)
            || now - lastSearch > searchWait - 0.5
        if search {
            let hits = findMarkers(base, length)
            for (i, marker) in RAMConsole.markers.enumerated() { candidates[marker] = hits[i] }
            lastSearch = now
            grown.removeAll()
        }
        if follow(base, length) {
            searchWait = 1                  // buffers are moving: look again soon
        } else if search {
            searchWait = min(2 * searchWait, RAMConsole.maxSearchWait)
        }
    }

    /// Forget the places that no longer hold their marker. True if the followed buffer lost its.
    private func dropMoved(_ base: UnsafeMutablePointer<UInt8>) -> Bool {
        var lost = false
        for (i, marker) in RAMConsole.markers.enumerated() {
            guard let known = candidates[marker] else { continue }
            let needle = RAMConsole.needles[i]
            let kept = known.filter { memcmp(base + $0, needle, needle.count) == 0 }
            if let followed = bufferAddress[marker], known.contains(followed), !kept.contains(followed) {
                lost = true
            }
            candidates[marker] = kept
        }
        return lost
    }

    /// Every place in guest RAM that holds a marker, in address order, for each marker.
    private func findMarkers(_ base: UnsafeMutablePointer<UInt8>, _ length: Int) -> [[Int]] {
        let needles = RAMConsole.needles, offsets = RAMConsole.pivotOffsets
        var hits = [[Int]](repeating: [], count: needles.count)
        var from = 0
        while from < length, let p = memchr(base + from, Int32(RAMConsole.pivot), length - from) {
            let at = base.distance(to: p.assumingMemoryBound(to: UInt8.self))
            for i in needles.indices {
                let start = at - offsets[i]
                if start >= 0, start + needles[i].count <= length,
                   memcmp(base + start, needles[i], needles[i].count) == 0 {
                    hits[i].append(start)
                }
            }
            from = at + 1
        }
        return hits
    }

    /// Where the text after a marker at `start` ends: at the first NUL, at most 1 MiB on.
    private func textEnd(_ base: UnsafeMutablePointer<UInt8>, _ length: Int, _ start: Int) -> Int {
        let limit = min(length, start + (1 << 20))
        return memchr(base + start, 0, limit - start)
            .map { base.distance(to: $0.assumingMemoryBound(to: UInt8.self)) } ?? limit
    }

    /// Follow each marker's buffer and print its new text. True if a marker changed buffer.
    private func follow(_ base: UnsafeMutablePointer<UInt8>, _ length: Int) -> Bool {
        var changed = false
        for marker in RAMConsole.markers {
            // The marker also exists as a string constant inside the loaded binaries (followed
            // by a NUL). The live log buffer is the occurrence followed by the most text.
            var best: (start: Int, end: Int)?
            for start in candidates[marker] ?? [] {
                let end = textEnd(base, length, start)
                if best == nil || end - start > best!.end - best!.start { best = (start, end) }
            }
            guard let (start, end) = best else { continue }
            if bufferAddress[marker] != start {        // a different (longer) buffer: start over
                bufferAddress[marker] = start
                printed[marker] = 0
                changed = true
                emit("----- \(marker) buffer at guest 0x\(String(RAMConsole.ramBase + UInt64(start), radix: 16))\n")
            } else if end - start > printed[marker] ?? 0 {
                grown.insert(marker)
            }
            let done = printed[marker] ?? 0
            guard end - start > done else { continue }
            let bytes = UnsafeBufferPointer(start: base + start + done, count: end - start - done)
            printed[marker] = end - start
            // Keep printable text only (the buffers can hold escape sequences).
            let text = String(decoding: bytes.map { ($0 >= 0x20 && $0 < 0x7F) || $0 == 0x0A || $0 == 0x09 ? $0 : 0x2E },
                              as: UTF8.self)
            emit(text)
        }
        return changed
    }

    func emit(_ text: String) {
        logHandle?.write(Data(text.utf8))
        for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
            print("RAM| " + line)
        }
    }
}

// MARK: - Presenter window (Metal layer over a VZ view for input)

final class MetalView: NSView {
    override func makeBackingLayer() -> CALayer { CAMetalLayer() }
    override var wantsUpdateLayer: Bool { true }
    // With VZ's input devices, events fall through to the VZ view underneath; with our
    // own devices (input.swift) this view is the one that takes them.
    override func hitTest(_ point: NSPoint) -> NSView? { inputRouter == nil ? nil : super.hitTest(point) }
    override var acceptsFirstResponder: Bool { inputRouter != nil }

    private var tracking: NSTrackingArea?
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking { removeTrackingArea(tracking) }
        let t = NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                               owner: self, userInfo: nil)
        addTrackingArea(t)
        tracking = t
    }

    override func mouseMoved(with e: NSEvent) { inputRouter?.pointer(e, in: self) }
    override func mouseDragged(with e: NSEvent) { inputRouter?.pointer(e, in: self) }
    override func rightMouseDragged(with e: NSEvent) { inputRouter?.pointer(e, in: self) }
    override func otherMouseDragged(with e: NSEvent) { inputRouter?.pointer(e, in: self) }
    override func mouseDown(with e: NSEvent) { inputRouter?.button(e, in: self) }
    override func mouseUp(with e: NSEvent) { inputRouter?.button(e, in: self) }
    override func rightMouseDown(with e: NSEvent) { inputRouter?.button(e, in: self) }
    override func rightMouseUp(with e: NSEvent) { inputRouter?.button(e, in: self) }
    override func otherMouseDown(with e: NSEvent) { inputRouter?.button(e, in: self) }
    override func otherMouseUp(with e: NSEvent) { inputRouter?.button(e, in: self) }
    override func scrollWheel(with e: NSEvent) { inputRouter?.scroll(e) }
    override func mouseEntered(with e: NSEvent) { inputRouter?.entered() }
    override func mouseExited(with e: NSEvent) { inputRouter?.exited() }
    override func keyDown(with e: NSEvent) { inputRouter?.key(e, pressed: true) }
    override func keyUp(with e: NSEvent) { inputRouter?.key(e, pressed: false) }
    override func flagsChanged(with e: NSEvent) { inputRouter?.flags(e) }
    // ⌘-combinations would otherwise be taken as menu equivalents: the guest gets them.
    // ⌃⌘ chords are the window's own shortcuts (menus.swift): the menu bar gets those first.
    override func performKeyEquivalent(with e: NSEvent) -> Bool {
        guard let router = inputRouter, router.enabled else { return false }
        if e.type == .keyDown, e.modifierFlags.intersection(.deviceIndependentFlagsMask).isSuperset(of: [.control, .command]),
           NSApp.mainMenu?.performKeyEquivalent(with: e) == true {
            return true
        }
        if e.type == .keyDown { router.key(e, pressed: true) } else if e.type == .keyUp { router.key(e, pressed: false) }
        return true
    }
}

final class Presenter: NSObject {
    let device = MTLCreateSystemDefaultDevice()!
    var queue: MTLCommandQueue!
    var buffer: MTLBuffer!
    var pipeline: MTLRenderPipelineState!
    var snowPipeline: MTLRenderPipelineState!
    var layer: CAMetalLayer!
    var window: NSWindow!
    var view: MetalView!
    var vmView: VZVirtualMachineView!
    var content: VMContentView!        // display, status bar, overlay (chrome.swift)
    var lastPresentedSeq = -1
    /// The machine is off: present black once, not the no-signal static.
    var poweredOff = false {
        didSet {
            blackShown = false
            lastPresentedSeq = -1
        }
    }
    private var blackShown = false

    /// decorate: the toolbar and status bar go on before the window is shown.
    func makeWindow(vm: VZVirtualMachine, source: PresentSource, decorate: (NSWindow, VMContentView) -> Void) {
        guard let surface = source.surface else { return }
        queue = device.makeCommandQueue()
        buffer = device.makeBuffer(bytesNoCopy: surface.base, length: surface.length,
                                    options: .storageModeShared, deallocator: nil)
        do {
            let library = try device.makeLibrary(source: shaderSource, options: nil)
            let desc = MTLRenderPipelineDescriptor()
            desc.vertexFunction = library.makeFunction(name: "vmain")
            desc.fragmentFunction = library.makeFunction(name: "fmain")
            desc.colorAttachments[0].pixelFormat = .bgra8Unorm
            pipeline = try device.makeRenderPipelineState(descriptor: desc)
            desc.fragmentFunction = library.makeFunction(name: "fsnow")
            snowPipeline = try device.makeRenderPipelineState(descriptor: desc)
        } catch {
            log("FATAL: shader: \(error)")
            exit(1)
        }

        let frame = NSRect(x: 0, y: 0, width: CGFloat(width), height: CGFloat(height))
        var displayViews: [NSView] = []
        if !ownInput {
            // --input vz: the VZ view sits underneath: it takes keyboard/pointer input and
            // shows VZ's own display. Our Metal view is an overlay on top, hidden until our
            // device presents. With our own input devices there is no VZ view at all.
            vmView = VZVirtualMachineView()
            vmView.virtualMachine = vm
            vmView.capturesSystemKeys = true
            displayViews.append(vmView)
        }

        view = MetalView(frame: frame)
        view.wantsLayer = true
        layer = (view.layer as! CAMetalLayer)
        layer.device = device
        layer.pixelFormat = .bgra8Unorm
        layer.isOpaque = true
        layer.framebufferOnly = true
        view.isHidden = !ownInput        // nothing underneath to show with our own input
        displayViews.append(view)        // above vmView
        // The display keeps the scanout size; the status bar goes underneath it.
        content = VMContentView(displaySize: frame.size, displayViews: displayViews,
                                statusBarShown: WindowChrome.statusBarInitiallyShown)
        let container = content!

        window = ProseWindow(contentRect: container.frame, styleMask: [.titled, .closable, .miniaturizable, .resizable],
                             backing: .buffered, defer: false)
        window.title = "Prose"
        if let name = option("--name") { window.subtitle = name }
        window.tabbingMode = .disallowed
        window.collectionBehavior.insert(.fullScreenPrimary)
        window.contentView = container
        window.contentMinSize = NSSize(width: 320, height: 200 + StatusBar.height)
        decorate(window, container)
        window.center()
        window.makeFirstResponder(ownInput ? view : vmView)
        if args.contains("--script") {
            // a scripted test run shows itself but leaves the keyboard with whoever is typing
            window.orderFrontRegardless()
        } else {
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
        }
        log("window \(width)x\(height) shown")
        updateDrawableSize()

        // Drive presentation from the container: a hidden view gets no display-link callbacks.
        let link = container.displayLink(target: self, selector: #selector(tick(_:)))
        link.add(to: .main, forMode: .common)
    }

    /// The rectangle (view points) the guest image occupies: the same aspect-fit the shader
    /// uses; the whole view while there is no image. Pointer coordinates map through it.
    func imageRect() -> NSRect {
        let b = view.bounds
        guard let s = presenterGPU?.surface, s.width > 0, s.height > 0, b.width > 0, b.height > 0 else { return b }
        let surfAspect = Double(s.width) / Double(s.height), targetAspect = Double(b.width) / Double(b.height)
        var w = Double(b.width), h = Double(b.height)
        if targetAspect > surfAspect { w = h * surfAspect } else { h = w / surfAspect }
        return NSRect(x: Double(b.midX) - w / 2, y: Double(b.midY) - h / 2, width: w, height: h)
    }

    func updateDrawableSize() {
        let scale = window.backingScaleFactor
        layer.contentsScale = scale
        layer.drawableSize = CGSize(width: view.bounds.width * scale, height: view.bounds.height * scale)
    }

    private func params(targetWidth: Double, targetHeight: Double, surface: FrameSurface) -> ShaderParams {
        let surfAspect = Double(surface.width) / Double(surface.height)
        let targetAspect = targetWidth / targetHeight
        var scale = SIMD2<Float>(1, 1)
        if targetAspect > surfAspect {
            scale.x = Float(surfAspect / targetAspect)
        } else {
            scale.y = Float(targetAspect / surfAspect)
        }
        return ShaderParams(width: UInt32(surface.width), height: UInt32(surface.height),
                            strideWords: UInt32(surface.stride / 4), offsetWords: UInt32(surface.offset / 4),
                            scale: scale, bias: (SIMD2<Float>(1, 1) - scale) / 2,
                            smooth: PresenterMode.current == .smooth ? 1 : 0)
    }

    private var ticks = 0
    private var noSignalFrame: UInt32 = 0

    @objc func tick(_ link: CADisplayLink) {
        ticks += 1
        if ticks == 1 { log("display link running (source attached: \(presenterGPU != nil))") }
        presenterGPU?.displayTick(timestamp: link.timestamp)
        // --no-overlay: never cover VZ's own display (for testing VZ's virtio-gpu).
        guard !args.contains("--no-overlay") else { return }
        guard let gpu = presenterGPU, let surface = gpu.surface else { return }
        if layer.drawableSize.width != view.bounds.width * window.backingScaleFactor {
            updateDrawableSize()
        }
        if poweredOff {
            guard !blackShown, let drawable = layer.nextDrawable() else { return }
            blackShown = true
            let rp = MTLRenderPassDescriptor()
            rp.colorAttachments[0].texture = drawable.texture
            rp.colorAttachments[0].loadAction = .clear
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1)
            rp.colorAttachments[0].storeAction = .store
            let cb = queue.makeCommandBuffer()!
            cb.makeRenderCommandEncoder(descriptor: rp)?.endEncoding()
            cb.present(drawable)
            cb.commit()
            return
        }
        // A picture needs a mode and at least one commit; anything else is no signal.
        let flushed = gpu.seq.withLock { $0 }
        let hasPicture = flushed > 0 && surface.width > 0 && surface.height > 0
        // No signal showsstatic, which animates, so it draws every frame; a picture
        // is only redrawn when the guest has committed something new. With VZ's own
        // display underneath (--input vz) we stay out of the way until it has.
        let showNoSignal = !hasPicture && ownInput && !args.contains("--no-snow")
        if !hasPicture && !showNoSignal { return }
        if hasPicture && flushed == lastPresentedSeq { return }
        if view.isHidden {
            view.isHidden = false
            log("first frame from our display device: showing the Metal overlay")
        }
        // nextDrawable() can block for a frame: get it before taking the lock commits wait on
        guard let drawable = layer.nextDrawable() else { return }
        presentLock.lock()
        defer { presentLock.unlock() }
        lastPresentedSeq = flushed
        let cb = queue.makeCommandBuffer()!
        let rp = MTLRenderPassDescriptor()
        rp.colorAttachments[0].texture = drawable.texture
        rp.colorAttachments[0].storeAction = .store
        rp.colorAttachments[0].loadAction = .dontCare
        let enc = cb.makeRenderCommandEncoder(descriptor: rp)!
        if hasPicture {
            var p = params(targetWidth: Double(layer.drawableSize.width),
                           targetHeight: Double(layer.drawableSize.height), surface: surface)
            enc.setRenderPipelineState(pipeline)
            enc.setFragmentBuffer(buffer, offset: 0, index: 0)
            enc.setFragmentBytes(&p, length: MemoryLayout<ShaderParams>.stride, index: 1)
        } else {
            noSignalFrame &+= 1
            var p = SnowParams(width: Float(layer.drawableSize.width),
                               height: Float(layer.drawableSize.height),
                               time: Float(link.timestamp - startTime.timeIntervalSince1970),
                               frame: noSignalFrame,
                               scale: Float(window.backingScaleFactor))
            enc.setRenderPipelineState(snowPipeline)
            enc.setFragmentBytes(&p, length: MemoryLayout<SnowParams>.stride, index: 0)
        }
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cb.present(drawable)
        cb.commit()
        cb.waitUntilCompleted()     // the surface may be written again only after the GPU read it
    }
}

// The presenter needs the gpu without owning it.
private weak var presenterGPURef: PresentSource?
var presenterGPU: PresentSource? {
    get { presenterGPURef }
    set { presenterGPURef = newValue }
}

// MARK: - Bisect probe: minimal custom device on a driver EDK2 also has (rng)

final class CustomVirtioRNG: NSObject, VZCustomVirtioDeviceConfigurationDelegate, VZCustomVirtioDeviceDelegate {
    var configuration: VZCustomVirtioDeviceConfiguration {
        let cfg = VZCustomVirtioDeviceConfiguration()
        cfg.deviceID = 4                      // VIRTIO_ID_RNG
        cfg.pciClassID = 0xFF                 // miscellaneous
        cfg.pciSubclassID = 0x00
        cfg.virtioQueueCount = 1
        cfg.provider = VZCustomVirtioDeviceDelegateProvider(deviceQueue: CustomVirtioGPU.sharedQueue,
                                                             delegate: self)
        return cfg
    }

    func customVirtioConfiguration(_ deviceConfiguration: VZCustomVirtioDeviceConfiguration,
                                   didCreateDevice device: VZCustomVirtioDevice) {
        device.delegate = self
        log("virtio-rng device created")
    }

    func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
        log("virtio-rng DRIVER_OK (bisect probe)")
    }

    func customVirtioDevice(_ device: VZCustomVirtioDevice,
                            didReceiveNotificationFor queue: VZVirtioQueue) {
        // Fill with fixed bytes; we only care about lifecycle here.
        while let element = queue.nextElement() {
            let buf = [UInt8](repeating: 0x42, count: element.writeBuffersByteCount)
            _ = try? element.write(Data(buf))
            element.returnToQueue()
        }
    }
}

// MARK: - VM controller (adapted from hvz)

final class Controller: NSObject, NSApplicationDelegate, NSWindowDelegate, VZVirtualMachineDelegate {
    var vm: VZVirtualMachine!
    let gpu = CustomVirtioGPU()
    let prds = PRDSDevice(width: initialGuestSize().width, height: initialGuestSize().height,
                          poolMiB: Int(option("--pool-mib") ?? "128") ?? 128)
    lazy var router = InputRouter(presenter: presenter)
    lazy var automation = Automation(controller: self)      // automation.swift
    let midi = ProseMIDIDevice()
    let portal = ProsePortalDevice()           // portal.swift: the guest answers the host
    var displaySource: PresentSource { displayMode == "s2" ? prds : gpu }
    let rngProbe = CustomVirtioRNG()   // --rng-probe: bisect custom-device support
    let presenter = Presenter()
    var stateObservation: NSKeyValueObservation?
    var stopping = false               // quitting: exit once the guest is off
    // The window's VM controls (controls.swift, chrome.swift, monitor.swift)
    var chrome: WindowChrome?
    var monitor: VMMonitor?
    lazy var macAddress = makeMACAddress()
    var shutdownRequested: Date?       // Shut Down or Restart pressed the guest's power button
    var restartPending = false
    var runningSince: Date?
    var failure: String?               // why the machine couldn't start

    func applicationDidFinishLaunching(_ notification: Notification) {
        do {
            try makeVM()                   // controls.swift
        } catch {
            log("configuration invalid: \(error.localizedDescription)")
            exit(1)
        }

        if !headless {
            NSApp.applicationIconImage = ProseIcon.image()
            presenter.makeWindow(vm: vm, source: displaySource) { [self] window, content in
                chrome = WindowChrome(controller: self, window: window, content: content)
                // the guest's screen follows the display area (status bar, full screen), not just the window
                content.onDisplayResize = { size in
                    // the presenter mode decides how many guest pixels a point is
                    let s = PresenterMode.current.guestScale(self.presenter.window?.backingScaleFactor ?? 1)
                    presenterGPU?.windowResized(width: Int(size.width * s), height: Int(size.height * s))
                }
            }
            presenter.window.delegate = self     // windowShouldClose, full screen (MODE_HINT: onDisplayResize)
        }
        presenterGPU = displaySource
        monitor = VMMonitor(diskImage: diskURL, guestMAC: args.contains("--no-net") ? nil : macAddress.string)

        bootVM()
        if let script = option("--script") { runScript(script) }
        if let seconds = runSeconds {
            DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { [self] in
                stopVM(reason: "time limit \(Int(seconds)) s")
            }
        }
        // --input-test: drive our own input devices without a human: click the Deskbar leaf,
        // Escape, then park the pointer mid-screen; a screenshot after each step.
        if ownInput, args.contains("--input-test") {
            let base = option("--screenshot") ?? "input-test.png"
            func shot(_ n: Int) { prds.screenshot(to: base.replacingOccurrences(of: ".png", with: "-\(n).png")) }
            DispatchQueue.main.asyncAfter(deadline: .now() + 30) { [self] in
                log("input test: click on the Deskbar leaf")
                router.move(to: Int32(0.986 * Double(EV.absMax)), Int32(0.0175 * Double(EV.absMax)))
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [self] in router.click() }
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 33) { shot(1) }
            DispatchQueue.main.asyncAfter(deadline: .now() + 34) { [self] in
                log("input test: Escape")
                router.tap(evdev: 1)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 36) { shot(2) }
            DispatchQueue.main.asyncAfter(deadline: .now() + 37) { [self] in
                log("input test: pointer to the centre")
                router.move(to: Int32(EV.absMax / 2), Int32(EV.absMax / 2))
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 39) { [self] in
                shot(3)
                log(router.stats)
            }
        }
        // --resize-drag N WxH: from N seconds on, animate the window to WxH in 40 steps of
        // 30 ms, like a hand dragging the corner: many hints, ideally one mode switch.
        if !headless, let after = option("--resize-drag").flatMap(Double.init),
           let i = args.lastIndex(of: "--resize-drag"), i + 2 < args.count {
            let parts = args[i + 2].split(separator: "x").compactMap { Double($0) }
            if parts.count == 2 {
                DispatchQueue.main.asyncAfter(deadline: .now() + after) { [self] in
                    let start = presenter.view.bounds.size
                    log("dragging the window from \(Int(start.width))x\(Int(start.height)) to \(Int(parts[0]))x\(Int(parts[1]))")
                    for k in 1...40 {
                        DispatchQueue.main.asyncAfter(deadline: .now() + Double(k) * 0.03) { [self] in
                            let t = Double(k) / 40
                            let bar = presenter.content.statusBarShown ? StatusBar.height : 0   // WxH is the display's
                            presenter.window.setContentSize(NSSize(width: start.width + (parts[0] - start.width) * t,
                                                                   height: start.height + (parts[1] - start.height) * t + bar))
                        }
                    }
                }
            }
        }
        // --resize-after N WxH: resize our window after N seconds (live-resize test)
        if !headless, let after = option("--resize-after").flatMap(Double.init),
           let i = args.lastIndex(of: "--resize-after"), i + 2 < args.count {
            let parts = args[i + 2].split(separator: "x").compactMap { Int($0) }
            if parts.count == 2 {
                DispatchQueue.main.asyncAfter(deadline: .now() + after) { [self] in
                    log("resizing the window to \(parts[0])x\(parts[1])")
                    resizeDisplay(to: NSSize(width: parts[0], height: parts[1]))    // the display's size, bars aside
                }
            }
        }
    }

    func makeConfiguration() throws -> VZVirtualMachineConfiguration {
        guard gpu.surface != nil else {
            throw NSError(domain: "hvgpu", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "surface allocation failed"])
        }
        let config = VZVirtualMachineConfiguration()
        config.cpuCount = cpus
        config.memorySize = memoryGiB << 30

        let platform = VZGenericPlatformConfiguration()
        if args.contains("--nested") {
            platform.isNestedVirtualizationEnabled = true
        }
        config.platform = platform

        let loader = VZEFIBootLoader()
        loader.variableStore = FileManager.default.fileExists(atPath: varsURL.path)
            ? VZEFIVariableStore(url: varsURL)
            : try VZEFIVariableStore(creatingVariableStoreAt: varsURL)
        config.bootLoader = loader

        let disk = try VZDiskImageStorageDeviceAttachment(
            url: diskURL, readOnly: false, cachingMode: .automatic, synchronizationMode: .full)
        // Haiku's virtio_block has I/O errors over virtio-pci (QEMU pci-blk row, and under VZ
        // "reading the partition table failed"); VZ only offers PCI, so boot from NVMe by default.
        switch option("--disk") ?? "nvme" {
        case "virtio": config.storageDevices = [VZVirtioBlockDeviceConfiguration(attachment: disk)]
        case "usb": config.storageDevices = [VZUSBMassStorageDeviceConfiguration(attachment: disk)]
        default: config.storageDevices = [VZNVMExpressControllerDeviceConfiguration(attachment: disk)]
        }

        if !args.contains("--no-net") {
            let net = VZVirtioNetworkDeviceConfiguration()
            net.attachment = VZNATNetworkDeviceAttachment()
            net.macAddress = macAddress        // stable per disk image: same lease, same IP
            config.networkDevices = [net]
        }

        if args.contains("--rng-probe") {
            config.customVirtioDevices = [rngProbe.configuration]
        } else if !args.contains("--no-custom-gpu") {   // --no-custom-gpu: VZ's GPU only
            config.customVirtioDevices = [displaySource.configuration]
        }
        // The Prose Portal (portal.swift) is attached only when the owner allowed
        // automation: absent, the guest has no device and its daemon exits. A
        // change to the setting takes effect at the next start, like any device.
        if Automation.enabled && !args.contains("--no-portal") {
            config.customVirtioDevices += [portal.configuration]
        }
        if !args.contains("--no-midi") {
            // the Prose MIDI port (midi.swift): the guest's MIDI played by the Mac
            config.customVirtioDevices += [midi.configuration]
        }
        if ownInput {
            // Our own virtio-input keyboard and tablet (input.swift): no VZ graphics device,
            // no USB keyboard or pointing device, no VZVirtualMachineView.
            inputRouter = router
            config.customVirtioDevices += [router.keyboard.configuration, router.tablet.configuration]
        } else {
            // --input vz: VZ's own virtio-gpu stays: VZVirtualMachineView maps the absolute
            // pointer onto it, so without it the mouse doesn't move. Stock Haiku opens it first
            // (graphics/virtio/0) and hangs; our Haiku fork skips GPUs without EDID
            // (patches/haiku/0002), so app_server uses ours. --no-vz-gpu drops it.
            if !args.contains("--no-vz-gpu") {
                let vzGpu = VZVirtioGraphicsDeviceConfiguration()
                vzGpu.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels: width,
                                                                        heightInPixels: height)]
                config.graphicsDevices = [vzGpu]
            }
            config.keyboards = [VZUSBKeyboardConfiguration()]
            config.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
        }
        config.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
        // virtio-snd to the Mac's default output/input (Haiku fork: virtio_sound driver)
        if !args.contains("--no-sound") {
            let output = VZVirtioSoundDeviceOutputStreamConfiguration()
            output.sink = VZHostAudioOutputStreamSink()
            let input = VZVirtioSoundDeviceInputStreamConfiguration()
            input.source = VZHostAudioInputStreamSource()
            let sound = VZVirtioSoundDeviceConfiguration()
            sound.streams = [output, input]
            config.audioDevices = [sound]
        }
        config.memoryBalloonDevices = [VZVirtioTraditionalMemoryBalloonDeviceConfiguration()]
        // HostFS (hostfs.swift): --share / --share-ro directories over VZ's virtio-fs
        if let hostFS = try makeHostFSDevice() {
            config.directorySharingDevices = [hostFS]
        }

        if let serialPath = option("--serial") {
            FileManager.default.createFile(atPath: serialPath, contents: nil)
            let serial = VZVirtioConsoleDeviceSerialPortConfiguration()
            serial.attachment = VZFileHandleSerialPortAttachment(
                fileHandleForReading: nil, fileHandleForWriting: FileHandle(forWritingAtPath: serialPath)!)
            config.serialPorts = [serial]
        }

        try config.validate()
        return config
    }

    func stopVM(reason: String) {
        if let router = inputRouter { log(router.stats) }
        if !args.contains("--no-midi") { log(midi.stats) }
        if portal.attached { log(portal.stats) }
        guard !stopping else { return }
        stopping = true
        log("stopping: \(reason)")
        // already off (the window stays after the guest powers off): nothing to wait for
        if vm.state == .stopped || vm.state == .error { exit(0) }
        if vm.state == .paused {
            vm.resume { [self] _ in requestGuestStop() }
        } else {
            requestGuestStop()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + grace) { [self] in
            guard vm.state != .stopped else { return }
            vm.stop { error in
                log("force stopped" + (error.map { ": \($0.localizedDescription)" } ?? ""))
                exit(0)
            }
        }
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        stopVM(reason: "window closed")
        return false
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        stopVM(reason: "quit")
        return .terminateCancel
    }

    func guestDidStop(_ virtualMachine: VZVirtualMachine) {
        log("guest powered off")
        let restarting = restartPending && !stopping
        if !restarting && (stopping || exitOnStop) { exit(0) }
        vmStopped()                        // the window stays, with a Start button
        if restarting { bootVM() }
    }

    func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: Error) {
        log("stopped with error: \(error.localizedDescription)")
        if stopping || exitOnStop { exit(1) }
        vmStopped()
        failure = error.localizedDescription
        stateChanged()
    }
}

ProseCommand.register()      // applescript.swift: keep the scripting class in the binary
if args.contains("--scripting-check") {
    let registry = NSScriptSuiteRegistry.shared()
    log("scripting: suites \(registry.suiteNames)")
    for suite in registry.suiteNames {
        log("scripting:   \(suite): commands \((registry.commandDescriptions(inSuite: suite) ?? [:]).keys.sorted())")
    }
}
let app = NSApplication.shared
app.setActivationPolicy(headless ? .prohibited : .regular)
NSWindow.allowsAutomaticWindowTabbing = false
let controller = Controller()
app.mainMenu = makeMainMenu(controller)    // menus.swift
app.delegate = controller
app.run()
