// prds.swift: the S2 shared-surface display device ("Prose Display", PRDS) as a
// macOS 27 VZCustomVirtioDevice. Contract: docs/s2-display-device.md.
//
// The surface pool is host memory (mmap, 16 KiB aligned) mapped into the guest as
// virtio shared memory region 0. The guest sets a mode inside the pool, draws,
// and commits dirty rects; we fold those rects into the presentation surface
// the Metal presenter samples, and complete the request once they are copied.
// Events (vsync, mode hints, redraw) go out on the event queue.
import Foundation
import Virtualization
import ImageIO
import Metal
import os

/// What the Metal presenter needs from a display device.
protocol PresentSource: AnyObject {
    var surface: FrameSurface? { get }
    var seq: OSAllocatedUnfairLock<Int> { get }
    var configuration: VZCustomVirtioDeviceConfiguration { get }
    func displayTick(timestamp: Double)
    func windowResized(width: Int, height: Int)
    /// The VM is running: guest RAM exists, shared memory can be mapped.
    func vmDidStart()
}

extension PresentSource {
    func displayTick(timestamp: Double) {}
    func windowResized(width: Int, height: Int) {}
    func vmDidStart() {}
}

extension CustomVirtioGPU: PresentSource {}

enum PRDS {
    static let deviceID: UInt16 = 63
    static let magic: UInt32 = 0x5344_5250          // "PRDS" in little-endian byte order
    static let version: UInt16 = 1
    static let featureVsync: UInt32 = 1 << 0
    static let featureResize: UInt32 = 1 << 1
    static let formatB8G8R8X8: UInt32 = 1
    static let shmAlign = 16384
    static let configSize = 64
    static let eventSize = 32
    static let maxCommitRects = 64

    static let cmdSetMode: UInt32 = 0x0100
    static let cmdCommit: UInt32 = 0x0101
    static let cmdEnableEvents: UInt32 = 0x0102
    static let cmdGetInfo: UInt32 = 0x0103
    static let respOK: UInt32 = 0x1000
    static let errInvalid: UInt32 = 0x1100
    static let errUnsupported: UInt32 = 0x1101
    static let errState: UInt32 = 0x1102
    static let errBounds: UInt32 = 0x1103
    static let evtVsync: UInt32 = 0x2000
    static let evtModeHint: UInt32 = 0x2001
    static let evtRedraw: UInt32 = 0x2002
    static let eventMaskVsync: UInt32 = 1
    static let eventMaskModeHint: UInt32 = 2
    static let eventMaskRedraw: UInt32 = 4
}

struct PRDSMode {
    var width: Int, height: Int, stride: Int, offset: Int
}

final class PRDSDevice: NSObject, PresentSource, VZCustomVirtioDeviceConfigurationDelegate,
    VZCustomVirtioDeviceDelegate {
    static let queue = DispatchQueue(label: "hvgpu.prds")
    let maxWidth = 3840, maxHeight = 2160, strideAlign = 64, refreshMHz: UInt32 = 60000
    let poolSize: Int
    let pool: UnsafeMutableRawPointer
    let surface: FrameSurface?              // the display buffer: only completed commits land here
    private(set) var poolBuffer: MTLBuffer? // the pool as the GPU sees it (fb and bb), for blit kernels
    let seq = OSAllocatedUnfairLock(initialState: 0)

    private(set) var device: VZCustomVirtioDevice?
    private var region: VZVirtioSharedMemoryRegion?
    private var ramConsole: RAMConsole?
    private var poolMapped = false
    private var mode: PRDSMode?
    private var presented = false           // a commit has arrived for the current mode
    private var eventMask: UInt32 = PRDS.eventMaskRedraw
    private var eventElements: [VZVirtioQueueElement] = []
    private var prefWidth: Int, prefHeight: Int
    private var vsyncSeq: UInt64 = 0, hintSeq: UInt64 = 0, redrawSeq: UInt64 = 0
    private var commits = 0, commitRects = 0, commitBytes = 0, droppedEvents = 0
    private var copyNanos: UInt64 = 0, copyMaxNanos: UInt64 = 0
    private let commitStats = args.contains("--commit-stats")
    private var rectHistogram: [String: Int] = [:]
    private var lastStats = Date()

    init(width: Int, height: Int, poolMiB: Int) {
        prefWidth = width
        prefHeight = height
        poolSize = max(poolMiB, 1) << 20
        guard let p = mmap(nil, poolSize, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0),
              p != UnsafeMutableRawPointer(bitPattern: -1) else {
            fatalError("prds: mmap of the surface pool failed")
        }
        pool = p
        surface = FrameSurface(width: maxWidth, height: maxHeight)
        super.init()
        surface?.width = 0          // nothing to show until the guest commits
        surface?.height = 0
        // Metal maps the pool too: the guest's front and back buffers become GPU
        // accessible, which is what a blit kernel (bb -> fb -> display) will use.
        poolBuffer = MTLCreateSystemDefaultDevice()?.makeBuffer(bytesNoCopy: p, length: poolSize,
                                                                options: .storageModeShared, deallocator: nil)
        log("prds: pool mapped for Metal: \(poolBuffer != nil)")
        log("prds: pool \(poolSize >> 20) MiB at \(pool) (16 KiB aligned: \(Int(bitPattern: pool) % PRDS.shmAlign == 0))")
    }

    // MARK: configuration

    private func configData() -> Data {
        var d = Data()
        d.append(contentsOf: le32(PRDS.magic))
        d.append(contentsOf: [UInt8(PRDS.version & 0xff), UInt8(PRDS.version >> 8), 1, 0])  // flags bit0: resizable
        for v in [maxWidth, maxHeight, prefWidth, prefHeight] { d.append(contentsOf: le32(UInt32(v))) }
        d.append(contentsOf: le32(refreshMHz))
        d.append(contentsOf: le32(UInt32(strideAlign)))
        d.append(contentsOf: le32(1))          // formats: B8G8R8X8
        d.append(contentsOf: le32(1))          // max_surfaces
        d.append(contentsOf: [0, 0, 0, 0])     // shm_region_id, reserved0
        d.append(contentsOf: le64(UInt64(poolSize)))
        d.append(contentsOf: [UInt8](repeating: 0, count: PRDS.configSize - d.count))
        precondition(d.count == PRDS.configSize)
        return d
    }

    var configuration: VZCustomVirtioDeviceConfiguration {
        let cfg = VZCustomVirtioDeviceConfiguration()
        cfg.deviceID = PRDS.deviceID
        cfg.pciClassID = 0x03
        cfg.pciSubclassID = 0x80
        cfg.virtioQueueCount = 2
        cfg.optionalFeatures.subset0 = PRDS.featureVsync | PRDS.featureResize
        cfg.deviceSpecificConfiguration = VZVirtioDeviceSpecificConfiguration(configurationData: configData())
        cfg.sharedMemoryRegions = [VZVirtioSharedMemoryRegionConfiguration(regionID: 0, size: UInt64(poolSize))]
        cfg.provider = VZCustomVirtioDeviceDelegateProvider(deviceQueue: PRDSDevice.queue, delegate: self)
        log("prds: VZ allows \(VZCustomVirtioDeviceConfiguration.maximumAllowedSharedMemoryRegionCount) shared memory regions")
        return cfg
    }

    // MARK: VZCustomVirtioDeviceConfigurationDelegate / VZCustomVirtioDeviceDelegate

    func customVirtioConfiguration(_ configuration: VZCustomVirtioDeviceConfiguration,
                                   didCreateDevice device: VZCustomVirtioDevice) {
        self.device = device
        device.delegate = self
        guard let region = device.sharedMemoryRegions.first else {
            log("prds: device created but it has no shared memory region")
            return
        }
        self.region = region
        log("prds: device created; region \(region.regionID) size \(region.size >> 20) MiB")
        if !args.contains("--no-ramconsole") {
            ramConsole = RAMConsole(device: device)
        }
    }

    /// mapMemory needs a live VM ("The virtual machine is not live" before start) and must
    /// run on the device queue, so it happens once the VM has started, not at device creation.
    func vmDidStart() {
        PRDSDevice.queue.async { [self] in
            guard let region, !poolMapped else { return }
            log("prds: mapping the pool into the guest...")
            region.mapMemory(pool, atOffset: 0, size: UInt64(poolSize)) { [weak self] error in
                PRDSDevice.queue.async {
                    if let error {
                        log("prds: mapMemory failed: \(error.localizedDescription)")
                    } else {
                        self?.poolMapped = true
                        log("prds: pool mapped into the guest")
                    }
                }
            }
        }
    }

    func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
        let f = device.negotiatedFeatures
        log("prds: DRIVER_OK (driver accepted features "
            + (f.map { String(format: "0x%08x_%08x", $0.subset1, $0.subset0) } ?? "none") + ")")
    }

    func customVirtioDeviceWillReset(_ device: VZCustomVirtioDevice) {
        log("prds: reset")
        mode = nil
        eventMask = PRDS.eventMaskRedraw
        eventElements.removeAll()
        clearPresentation()
    }

    func customVirtioDeviceWillStop(_ device: VZCustomVirtioDevice) {
        eventElements.removeAll()
    }

    func customVirtioDevice(_ device: VZCustomVirtioDevice, didReceiveNotificationFor queue: VZVirtioQueue) {
        if queue.queueIndex == 1 {
            while let element = queue.nextElement() { eventElements.append(element) }
            return
        }
        while let element = queue.nextElement() { handle(element) }
        maybeLogStats()
    }

    // MARK: control queue

    private func handle(_ element: VZVirtioQueueElement) {
        let length = element.readBuffersByteCount
        var bytes = [UInt8](repeating: 0, count: length)
        guard length >= 16, (try? element.readBytes(intoBuffer: &bytes, exactLength: length)) != nil else {
            element.returnToQueue()
            return
        }
        let req = Data(bytes)
        let type = leU32(req, 0), flags = leU32(req, 4), seqNo = leU64(req, 8)
        var status = PRDS.respOK
        var payload: [UInt8] = []

        switch type {
        case PRDS.cmdSetMode:
            status = setMode(req)
        case PRDS.cmdCommit:
            status = commit(req)
        case PRDS.cmdEnableEvents:
            let mask = leU32(req, 16)
            if mask & ~(PRDS.eventMaskVsync | PRDS.eventMaskModeHint | PRDS.eventMaskRedraw) != 0 {
                status = PRDS.errInvalid
            } else {
                eventMask = mask | PRDS.eventMaskRedraw
                log("prds: events enabled: mask \(mask)")
            }
        case PRDS.cmdGetInfo:
            payload = [UInt8](configData())
        default:
            log("prds: unsupported command 0x\(String(type, radix: 16)) (\(length) bytes)")
            status = PRDS.errUnsupported
        }
        _ = flags
        let response = le32(status) + le32(0) + le64(seqNo) + payload
        if element.writeBuffersByteCount < response.count {
            _ = try? element.write(Data(le32(PRDS.errInvalid) + le32(0) + le64(seqNo)))
        } else {
            _ = try? element.write(Data(response))
        }
        element.returnToQueue()
    }

    private func setMode(_ req: Data) -> UInt32 {
        guard req.count >= 48 else { return PRDS.errInvalid }
        let surfaceID = leU32(req, 16)
        let width = Int(leU32(req, 20)), height = Int(leU32(req, 24))
        let stride = Int(leU32(req, 28)), format = leU32(req, 32)
        let offset = Int(leU64(req, 40))
        guard poolMapped else {
            log("prds: SET_MODE before the pool is mapped")
            return PRDS.errState
        }
        if width == 0 && height == 0 {
            mode = nil
            clearPresentation()
            log("prds: scanout disabled")
            return PRDS.respOK
        }
        guard surfaceID == 0, format == PRDS.formatB8G8R8X8, width >= 1, height >= 1,
              stride >= width * 4, stride % strideAlign == 0 else { return PRDS.errInvalid }
        guard width <= maxWidth, height <= maxHeight, offset % PRDS.shmAlign == 0,
              offset + stride * height <= poolSize else { return PRDS.errBounds }
        mode = PRDSMode(width: width, height: height, stride: stride, offset: offset)
        clearPresentation()     // black until the first commit of the new mode
        log("prds: mode \(width)x\(height) stride \(stride) at pool offset \(offset)")
        return PRDS.respOK
    }

    /// The ready signal: app_server has finished these rects in the front buffer (the pool),
    /// and will not touch them again until we answer. We copy them into the display buffer
    /// under the presentation lock, so the presenter only ever shows completed commits and
    /// never reads while we write. Completion — returnToQueue by the caller — means the
    /// copy is done and the guest may write the front buffer again.
    private func commit(_ req: Data) -> UInt32 {
        guard let mode, let surface else { return PRDS.errState }
        guard req.count >= 24 else { return PRDS.errInvalid }
        let count = Int(leU32(req, 20))
        guard count <= PRDS.maxCommitRects, req.count >= 24 + count * 16 else { return PRDS.errInvalid }
        var rects: [(x0: Int, y0: Int, x1: Int, y1: Int)] = []
        if count == 0 {
            rects.append((0, 0, mode.width, mode.height))
        } else {
            for i in 0..<count {
                let o = 24 + i * 16
                let x0 = max(0, Int(leU32(req, o))), y0 = max(0, Int(leU32(req, o + 4)))
                let x1 = min(mode.width, x0 + Int(leU32(req, o + 8)))
                let y1 = min(mode.height, y0 + Int(leU32(req, o + 12)))
                if x1 > x0 && y1 > y0 { rects.append((x0, y0, x1, y1)) }
            }
        }
        if commitStats {
            for r in rects { rectHistogram["\(r.x0),\(r.y0) \(r.x1 - r.x0)x\(r.y1 - r.y0)", default: 0] += 1 }
        }
        let start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW)
        presentLock.withLock {
            let src = pool + mode.offset
            for r in rects {
                let bytes = (r.x1 - r.x0) * 4
                for y in r.y0..<r.y1 {
                    memcpy(surface.base + y * surface.stride + r.x0 * 4, src + y * mode.stride + r.x0 * 4, bytes)
                }
                commitRects += 1
                commitBytes += bytes * (r.y1 - r.y0)
            }
            if !presented {
                presented = true
                surface.width = mode.width
                surface.height = mode.height
            }
        }
        let elapsed = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - start
        copyNanos += elapsed
        copyMaxNanos = max(copyMaxNanos, elapsed)
        commits += 1
        seq.withLock { $0 += 1 }
        return PRDS.respOK
    }

    /// Nothing to show until the guest commits: the presenter paints black, and the display
    /// buffer is cleared so uncommitted parts of a new mode do not show stale pixels.
    private func clearPresentation() {
        presentLock.withLock {
            presented = false
            if let surface {
                surface.width = 0
                surface.height = 0
                memset(surface.base, 0, surface.length)
            }
        }
        seq.withLock { $0 += 1 }
    }

    // MARK: events

    @discardableResult
    private func emit(_ event: [UInt8]) -> Bool {
        guard !eventElements.isEmpty else {
            droppedEvents += 1
            return false
        }
        let element = eventElements.removeFirst()
        _ = try? element.write(Data(event + [UInt8](repeating: 0, count: max(0, PRDS.eventSize - event.count))))
        element.returnToQueue()
        return true
    }

    func displayTick(timestamp: Double) {
        PRDSDevice.queue.async { [self] in
            guard eventMask & PRDS.eventMaskVsync != 0, mode != nil else { return }
            vsyncSeq += 1
            emit(le32(PRDS.evtVsync) + le32(0) + le64(vsyncSeq) + le64(UInt64(timestamp * 1e9)) + le64(0))
        }
    }

    func windowResized(width: Int, height: Int) {
        PRDSDevice.queue.async { [self] in
            guard width > 0, height > 0, width != prefWidth || height != prefHeight else { return }
            prefWidth = min(width, maxWidth)
            prefHeight = min(height, maxHeight)
            let config = VZVirtioDeviceSpecificConfiguration(configurationData: configData())
            device?.update(config) { error in
                if let error { log("prds: config update failed: \(error.localizedDescription)") }
            }
            if eventMask & PRDS.eventMaskModeHint != 0 {
                hintSeq += 1
                emit(le32(PRDS.evtModeHint) + le32(0) + le64(hintSeq)
                    + le32(UInt32(prefWidth)) + le32(UInt32(prefHeight)) + le64(0))
            }
            log("prds: window resized, hinting \(prefWidth)x\(prefHeight)")
        }
    }

    private func maybeLogStats() {
        let now = Date()
        guard now.timeIntervalSince(lastStats) >= 10 else { return }
        lastStats = now
        let avg = commits > 0 ? copyNanos / UInt64(commits) / 1000 : 0
        log("prds: \(commits) commits, \(commitRects) rects, \(commitBytes >> 10) KiB copied "
            + "(avg \(avg) µs, max \(copyMaxNanos / 1000) µs per commit), "
            + "\(vsyncSeq) vsyncs, \(droppedEvents) events dropped, \(eventElements.count) event buffers posted")
        if commitStats {
            let top = rectHistogram.sorted { $0.value > $1.value }.prefix(6)
            log("prds: top rects: " + top.map { "\($0.key) ×\($0.value)" }.joined(separator: ", "))
            rectHistogram.removeAll()
        }
        if let path = option("--screenshot") { screenshot(to: path) }
    }

    /// --screenshot PATH: write the presentation surface as PNG with each stats line,
    /// so a headless run can be checked without anyone looking at the window.
    private func screenshot(to path: String) {
        guard let surface, surface.width > 0, surface.height > 0, mode != nil else { return }
        presentLock.lock()
        defer { presentLock.unlock() }
        let space = CGColorSpaceCreateDeviceRGB()
        let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue
            | CGBitmapInfo.byteOrder32Little.rawValue)
        guard let ctx = CGContext(data: surface.base + surface.offset, width: surface.width, height: surface.height,
                                  bitsPerComponent: 8, bytesPerRow: surface.stride, space: space,
                                  bitmapInfo: info.rawValue),
              let image = ctx.makeImage(),
              let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL,
                                                         "public.png" as CFString, 1, nil)
        else { log("prds: screenshot failed"); return }
        CGImageDestinationAddImage(dest, image, nil)
        CGImageDestinationFinalize(dest)
    }
}
