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
//   --display s1|s2         our display device: s1 impersonates virtio-gpu (default),
//                           s2 is the shared-surface Prose Display (docs/s2-display-device.md)
//   --pool-mib N            s2 surface pool size (default 64)
//   (same options as hvz: --efivars --cpus --memory --seconds --grace --serial
//    --nested --no-net --headless)
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
guard args.count >= 2, !args[1].hasPrefix("--") else {
    print("usage: hvgpu <disk.img> [--size WxH] [--efivars path] [--cpus n] "
        + "[--memory GiB] [--seconds n] [--grace n] [--serial path] [--nested] [--no-net] [--headless]")
    exit(64)
}

let diskURL = URL(fileURLWithPath: args[1])
let varsURL = URL(fileURLWithPath: option("--efivars") ?? args[1] + ".efivars")
let cpus = Int(option("--cpus") ?? "4") ?? 4
let memoryGiB = UInt64(option("--memory") ?? "4") ?? 4
let dims = (option("--size") ?? "1280x800").split(separator: "x").compactMap { Int($0) }
let (width, height) = (dims.count == 2 ? dims[0] : 1280, dims.count == 2 ? dims[1] : 800)
let runSeconds = option("--seconds").flatMap(Double.init)
let grace = Double(option("--grace") ?? "8") ?? 8
let headless = args.contains("--headless")
let displayMode = option("--display") ?? "s1"

let startTime = Date()
func log(_ event: String) {
    print(String(format: "HVZ %7.2f ", Date().timeIntervalSince(startTime)) + event)
}

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
    float2 scale; float2 bias;
};

vertex VOut vmain(uint vid [[vertex_id]]) {
    float2 p = float2(vid == 1 ? 3.0 : -1.0, vid == 2 ? 3.0 : -1.0);
    VOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return o;
}

// B_RGB32: bytes B, G, R, X with undefined X, so alpha is forced to 1.
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

struct ShaderParams {
    var width: UInt32
    var height: UInt32
    var strideWords: UInt32
    var offsetWords: UInt32
    var scale: SIMD2<Float>
    var bias: SIMD2<Float>
}

/// The host-side frame surface: page-aligned, Metal-wrapped, written by the
/// device on TRANSFER, sampled by the shader on present.
final class FrameSurface {
    var width: Int          // the current mode (s2 changes it; the buffer holds the maximum)
    var height: Int
    let stride: Int
    let length: Int
    let base: UnsafeMutableRawPointer

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
        for row in 0..<h {
            let s = srcBase + Int(offset) + row * srcStride
            let d = surface.base + (y + row) * dstStride + x * 4
            memcpy(d, s, w * 4)
        }
    }
}

// MARK: - RAM console: Haiku's boot/kernel logs read straight out of guest RAM

/// VZ gives Haiku no serial port, but a custom virtio device can map guest RAM.
/// The loader keeps its log in a memory buffer, and the kernel keeps its debug
/// output in the syslog ring buffer; both are plain text. Every second, find
/// them by their opening lines and print whatever text is new.
final class RAMConsole {
    static let ramBase: UInt64 = 0x7000_0000        // VZ generic platform (Sprint 0 T3)
    // The kernel's RAM log (patches/haiku/0001) holds all kernel debug output from the start.
    static let markers = ["Welcome to the Haiku boot loader!", "HAIKU-RAMLOG-V1"]
    weak var device: VZCustomVirtioDevice?
    var mapping: VZGuestMemoryMapping?
    let queue = DispatchQueue(label: "hvgpu.ramconsole", qos: .utility)
    var timer: DispatchSourceTimer?
    var printed: [String: Int] = [:]                // marker -> bytes already printed
    var bufferAddress: [String: Int] = [:]          // marker -> offset of the buffer being followed
    var attempts = 0
    var logHandle: FileHandle?
    let logURL = URL(fileURLWithPath: option("--ramconsole-log") ?? "ramconsole.log")

    init(device: VZCustomVirtioDevice) {
        self.device = device
        FileManager.default.createFile(atPath: logURL.path, contents: nil)
        logHandle = FileHandle(forWritingAtPath: logURL.path)
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now() + 1, repeating: 1)
        t.setEventHandler { [weak self] in self?.scan() }
        t.resume()
        timer = t
    }

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
            log("ramconsole: mapped \(m.length >> 20) MiB of guest RAM; log -> \(logURL.path)")
        } else if attempts == 5 {
            log("ramconsole: still cannot map guest RAM at 0x\(String(RAMConsole.ramBase, radix: 16))")
        }
        return m
    }

    func scan() {
        guard let mapping = ensureMapping() else { return }
        let base = mapping.mutableBytes.assumingMemoryBound(to: UInt8.self)
        let length = mapping.length
        for marker in RAMConsole.markers {
            // The marker also exists as a string constant inside the loaded binaries (followed
            // by a NUL). The live log buffer is the occurrence followed by the most text.
            let needle = Array(marker.utf8)
            var best: (start: Int, end: Int)?
            var from = 0
            while from < length, let hit = needle.withUnsafeBufferPointer({ n -> Int? in
                guard let p = memmem(base + from, length - from, n.baseAddress, n.count) else { return nil }
                return base.distance(to: p.assumingMemoryBound(to: UInt8.self))
            }) {
                var end = hit
                let limit = min(length, hit + (1 << 20))
                while end < limit && base[end] != 0 { end += 1 }
                if best == nil || end - hit > best!.end - best!.start { best = (hit, end) }
                from = hit + needle.count
            }
            guard let (start, end) = best else { continue }
            if bufferAddress[marker] != start {        // a different (longer) buffer: start over
                bufferAddress[marker] = start
                printed[marker] = 0
                emit("----- \(marker) buffer at guest 0x\(String(RAMConsole.ramBase + UInt64(start), radix: 16))\n")
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
    // Mouse events fall through to the VZ view underneath, which feeds the guest.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}

final class Presenter: NSObject {
    let device = MTLCreateSystemDefaultDevice()!
    var queue: MTLCommandQueue!
    var buffer: MTLBuffer!
    var pipeline: MTLRenderPipelineState!
    var layer: CAMetalLayer!
    var window: NSWindow!
    var view: MetalView!
    var vmView: VZVirtualMachineView!
    var lastPresentedSeq = -1

    func makeWindow(vm: VZVirtualMachine, source: PresentSource) {
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
        } catch {
            log("FATAL: shader: \(error)")
            exit(1)
        }

        // The VZ view sits underneath: it takes keyboard/pointer input and shows VZ's own
        // display. Our Metal view is an overlay on top, hidden until our device presents.
        let frame = NSRect(x: 0, y: 0, width: CGFloat(width), height: CGFloat(height))
        let container = NSView(frame: frame)
        container.wantsLayer = true
        vmView = VZVirtualMachineView()
        vmView.virtualMachine = vm
        vmView.capturesSystemKeys = true
        vmView.frame = container.bounds
        vmView.autoresizingMask = [.width, .height]
        container.addSubview(vmView)

        view = MetalView(frame: container.bounds)
        view.autoresizingMask = [.width, .height]
        view.wantsLayer = true
        layer = (view.layer as! CAMetalLayer)
        layer.device = device
        layer.pixelFormat = .bgra8Unorm
        layer.isOpaque = true
        layer.framebufferOnly = true
        view.isHidden = true
        container.addSubview(view)       // above vmView

        window = NSWindow(contentRect: frame, styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered, defer: false)
        window.title = "hvgpu — Haiku \(width)x\(height) (S1)"
        window.contentView = container
        window.center()
        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(vmView)
        log("window \(width)x\(height) shown")
        NSApp.activate(ignoringOtherApps: true)
        updateDrawableSize()

        // Drive presentation from the container: a hidden view gets no display-link callbacks.
        let link = container.displayLink(target: self, selector: #selector(tick(_:)))
        link.add(to: .main, forMode: .common)
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
                            strideWords: UInt32(surface.stride / 4), offsetWords: 0,
                            scale: scale, bias: (SIMD2<Float>(1, 1) - scale) / 2)
    }

    private var ticks = 0

    @objc func tick(_ link: CADisplayLink) {
        ticks += 1
        if ticks == 1 { log("display link running (source attached: \(presenterGPU != nil))") }
        presenterGPU?.displayTick(timestamp: link.timestamp)
        // --no-overlay: never cover VZ's own display (for testing VZ's virtio-gpu).
        guard !args.contains("--no-overlay") else { return }
        guard let gpu = presenterGPU, let surface = gpu.surface,
              surface.width > 0, surface.height > 0 else { return }
        if layer.drawableSize.width != view.bounds.width * window.backingScaleFactor {
            updateDrawableSize()
        }
        let current = gpu.seq.withLock { $0 }
        // Nothing flushed yet: keep the overlay hidden so VZ's own display shows.
        if current == 0 || current == lastPresentedSeq { return }
        lastPresentedSeq = current
        if view.isHidden {
            view.isHidden = false
            log("first frame from our virtio-gpu: showing the Metal overlay")
        }
        guard let drawable = layer.nextDrawable() else { return }
        let cb = queue.makeCommandBuffer()!
        let rp = MTLRenderPassDescriptor()
        rp.colorAttachments[0].texture = drawable.texture
        rp.colorAttachments[0].loadAction = .dontCare
        rp.colorAttachments[0].storeAction = .store
        let enc = cb.makeRenderCommandEncoder(descriptor: rp)!
        var p = params(targetWidth: Double(layer.drawableSize.width),
                       targetHeight: Double(layer.drawableSize.height), surface: surface)
        enc.setRenderPipelineState(pipeline)
        enc.setFragmentBuffer(buffer, offset: 0, index: 0)
        enc.setFragmentBytes(&p, length: MemoryLayout<ShaderParams>.stride, index: 1)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cb.present(drawable)
        cb.commit()
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
    let prds = PRDSDevice(width: width, height: height, poolMiB: Int(option("--pool-mib") ?? "64") ?? 64)
    var displaySource: PresentSource { displayMode == "s2" ? prds : gpu }
    let rngProbe = CustomVirtioRNG()   // --rng-probe: bisect custom-device support
    let presenter = Presenter()
    var stateObservation: NSKeyValueObservation?
    var stopping = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        let config: VZVirtualMachineConfiguration
        do {
            config = try makeConfiguration()
        } catch {
            log("configuration invalid: \(error.localizedDescription)")
            exit(1)
        }
        vm = VZVirtualMachine(configuration: config)
        vm.delegate = self
        stateObservation = vm.observe(\.state, options: [.new]) { vm, _ in
            log("state \(vm.state.rawValue)")
        }

        if !headless {
            presenter.makeWindow(vm: vm, source: displaySource)
        }
        presenterGPU = displaySource

        log("starting \(diskURL.path) cpus=\(cpus) memory=\(memoryGiB)GiB scanout=\(width)x\(height)")
        vm.start { result in
            switch result {
            case .success:
                log("started")
                self.displaySource.vmDidStart()
            case .failure(let error):
                log("start failed: \(error.localizedDescription)")
                exit(1)
            }
        }
        if let seconds = runSeconds {
            DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { [self] in
                stopVM(reason: "time limit \(Int(seconds)) s")
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
            config.networkDevices = [net]
        }

        if args.contains("--rng-probe") {
            config.customVirtioDevices = [rngProbe.configuration]
        } else if !args.contains("--no-custom-gpu") {   // --no-custom-gpu: VZ's GPU only
            config.customVirtioDevices = [displaySource.configuration]
        }
        // VZ's own virtio-gpu stays by default: VZVirtualMachineView maps the absolute pointer
        // onto it, so without it the mouse doesn't move. Stock Haiku opens it first
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
        config.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
        config.memoryBalloonDevices = [VZVirtioTraditionalMemoryBalloonDeviceConfiguration()]

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
        guard !stopping else { return }
        stopping = true
        log("stopping: \(reason)")
        if vm.canRequestStop {
            do {
                try vm.requestStop()
                log("requested guest power-off")
            } catch {
                log("requestStop failed: \(error.localizedDescription)")
            }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + grace) { [self] in
            guard vm.state != .stopped else { return }
            vm.stop { error in
                log("force stopped" + (error.map { ": \($0.localizedDescription)" } ?? ""))
                exit(0)
            }
        }
    }

    func windowDidResize(_ notification: Notification) {
        let size = presenter.view.bounds.size
        presenterGPU?.windowResized(width: Int(size.width), height: Int(size.height))
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
        exit(0)
    }

    func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: Error) {
        log("stopped with error: \(error.localizedDescription)")
        exit(1)
    }
}

let app = NSApplication.shared
app.setActivationPolicy(headless ? .prohibited : .regular)
let mainMenu = NSMenu()
let appItem = NSMenuItem()
mainMenu.addItem(appItem)
let appMenu = NSMenu()
appMenu.addItem(withTitle: "Quit hvgpu", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
appItem.submenu = appMenu
app.mainMenu = mainMenu
let controller = Controller()
app.delegate = controller
app.run()
