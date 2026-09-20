// portal.swift: the host end of the Prose Portal — asking the guest to do
// something and hearing what happened. docs/automation.md; the wire protocol
// is the guest's headers/private/prose/prose_portal.h.
//
// A custom virtio device, ID 61, the same shape as the Prose MIDI port: queue 0
// carries frames from the guest, queue 1 carries requests to it. The guest
// posts writable buffers on queue 1; a request is one buffer, filled here. A
// reply is one frame, except a RUN result, whose body may continue in
// RESULT_MORE frames with the same id.
//
// There is no connection and nothing to hold open. A request either gets its
// reply, or times out and says so. The device is attached only when the owner
// has allowed automation: with it off, the guest has no device, its driver
// finds nothing, and the daemon exits — an absent path, not a refused one.
import Foundation
import Virtualization
import os

enum Portal {
    static let magic: UInt32 = 0x50525054          // 'PRPT'
    static let version: UInt16 = 2
    static let requestSize = 4096
    static let ping: UInt16 = 1, run: UInt16 = 2, info: UInt16 = 3
    static let hello: UInt16 = 0x81, pong: UInt16 = 0x82, result: UInt16 = 0x83
    static let infoReply: UInt16 = 0x84, resultMore: UInt16 = 0x85, error: UInt16 = 0xFF
    static let truncated: UInt32 = 1
}

/// What the guest said. `status` is the command's own exit status; a portal that
/// could not be reached is a failure, not a status, so the two never mix.
struct PortalReply {
    let type: UInt16
    let status: Int32
    let out: String
    let error: String
    let text: String            // info replies and error messages
    let truncated: Bool
}

enum PortalFailure: Error, CustomStringConvertible {
    case noDevice               // --no-portal: the machine has no portal device
    case notAnswering           // the guest has not said hello since it started
    case timedOut(TimeInterval)
    case busy                   // eight requests outstanding and none coming back
    case guestError(String)     // the guest answered, and the answer is bad news
    case malformed

    var description: String {
        switch self {
        case .noDevice: return "the portal device is not attached: automation was off when the machine started"
        case .notAnswering: return "the guest's portal has not answered since the machine started"
        case .timedOut(let s): return "no reply in \(Int(s)) s"
        case .busy: return "the guest has stopped taking requests"
        case .guestError(let message): return message
        case .malformed: return "the guest answered with something that is not a frame"
        }
    }
}

final class ProsePortalDevice: NSObject, VZCustomVirtioDeviceConfigurationDelegate,
    VZCustomVirtioDeviceDelegate {
    static let queue = DispatchQueue(label: "hvgpu.portal")
    static let deviceID: UInt16 = 61

    /// One request in flight: where its reply goes, and the body of a RUN
    /// result as it arrives across frames.
    private final class Pending {
        let type: UInt16
        let completion: (Result<PortalReply, PortalFailure>) -> Void
        let timer: DispatchWorkItem
        var result: (status: Int32, outLength: Int, errorLength: Int, truncated: Bool)?
        var body = Data()
        init(type: UInt16, timer: DispatchWorkItem, completion: @escaping (Result<PortalReply, PortalFailure>) -> Void) {
            self.type = type; self.timer = timer; self.completion = completion
        }
    }

    private(set) var device: VZCustomVirtioDevice?
    private var requestElements: [VZVirtioQueueElement] = []     // the guest's buffers for our requests
    private var waiting: [(UInt32, Data)] = []                    // frames with no buffer to go in yet
    private var pending: [UInt32: Pending] = [:]
    private var nextID: UInt32 = 1
    /// Read from any thread: has the guest's daemon said hello since the last reset?
    let alive = OSAllocatedUnfairLock(initialState: false)
    private(set) var sent = 0, received = 0

    var attached: Bool { device != nil }
    /// Called on the device queue when the guest says hello (each boot).
    var onHello: (() -> Void)?

    var configuration: VZCustomVirtioDeviceConfiguration {
        let cfg = VZCustomVirtioDeviceConfiguration()
        cfg.deviceID = ProsePortalDevice.deviceID
        cfg.pciClassID = 0x04                     // multimedia, like the MIDI port: the guest
        cfg.pciSubclassID = 0x01                  // probes drivers for it during its /dev/audio scan
        cfg.virtioQueueCount = 2
        cfg.deviceSpecificConfiguration = VZVirtioDeviceSpecificConfiguration(
            configurationData: Data(Array("PRPT".utf8) + le32(UInt32(Portal.version)) + [UInt8](repeating: 0, count: 8)))
        cfg.provider = VZCustomVirtioDeviceDelegateProvider(deviceQueue: ProsePortalDevice.queue, delegate: self)
        return cfg
    }

    // MARK: requests

    /// Ask the guest. The completion runs on the device queue; the caller hops to
    /// wherever it needs to be.
    func send(_ type: UInt16, payload: Data = Data(), timeout: TimeInterval,
              completion: @escaping (Result<PortalReply, PortalFailure>) -> Void) {
        ProsePortalDevice.queue.async { [self] in
            guard device != nil else { return completion(.failure(.noDevice)) }
            guard alive.withLock({ $0 }) else { return completion(.failure(.notAnswering)) }
            guard payload.count + 16 <= Portal.requestSize else {
                return completion(.failure(.guestError("the request does not fit in a frame")))
            }
            let id = nextID
            nextID &+= 1
            if nextID == 0 { nextID = 1 }
            let timer = DispatchWorkItem { [weak self] in
                guard let self, let p = self.pending.removeValue(forKey: id) else { return }
                p.completion(.failure(.timedOut(timeout)))
            }
            pending[id] = Pending(type: type, timer: timer, completion: completion)
            ProsePortalDevice.queue.asyncAfter(deadline: .now() + timeout, execute: timer)

            var frame = Data()
            frame.append(contentsOf: le32(Portal.magic))
            frame.append(contentsOf: [UInt8(Portal.version & 0xff), UInt8(Portal.version >> 8)])
            frame.append(contentsOf: [UInt8(type & 0xff), UInt8(type >> 8)])
            frame.append(contentsOf: le32(id))
            frame.append(contentsOf: le32(UInt32(payload.count)))
            frame.append(payload)
            waiting.append((id, frame))
            flushRequests()
        }
    }

    /// Every waiting request into a buffer the guest has posted. If the guest
    /// has posted none — its daemon is not reading — the request waits here and
    /// its own timer decides.
    private func flushRequests() {
        while !waiting.isEmpty, !requestElements.isEmpty {
            let element = requestElements.removeFirst()
            let (_, frame) = waiting.removeFirst()
            _ = try? element.write(frame)
            element.returnToQueue()
            sent += 1
        }
    }

    // MARK: replies

    private func handle(fromGuest data: Data) {
        guard data.count >= 16 else { return }
        let magic = data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 0, as: UInt32.self) }
        let version = data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 4, as: UInt16.self) }
        let type = data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 6, as: UInt16.self) }
        let id = data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 8, as: UInt32.self) }
        let length = Int(data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 12, as: UInt32.self) })
        guard magic == Portal.magic, version == Portal.version, 16 + length <= data.count else {
            log("portal: a frame that is not one (\(data.count) bytes)")
            return
        }
        let payload = data.subdata(in: 16..<(16 + length))
        received += 1

        if type == Portal.hello {
            alive.withLock { $0 = true }
            log("portal: the guest is answering")
            onHello?()
            return
        }
        guard let p = pending[id] else { return }       // a reply to a request that timed out

        switch type {
        case Portal.pong, Portal.infoReply:
            finish(id, p, .success(PortalReply(type: type, status: 0, out: "", error: "",
                                                text: String(decoding: payload, as: UTF8.self), truncated: false)))
        case Portal.error:
            finish(id, p, .failure(.guestError(String(decoding: payload, as: UTF8.self))))
        case Portal.result:
            guard payload.count >= 16 else { return finish(id, p, .failure(.malformed)) }
            let status = payload.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 0, as: Int32.self) }
            let outLength = Int(payload.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 4, as: UInt32.self) })
            let errorLength = Int(payload.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 8, as: UInt32.self) })
            let flags = payload.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 12, as: UInt32.self) }
            p.result = (status, outLength, errorLength, flags & Portal.truncated != 0)
            p.body.append(payload.subdata(in: 16..<payload.count))
            finishIfComplete(id, p)
        case Portal.resultMore:
            guard p.result != nil else { return finish(id, p, .failure(.malformed)) }
            p.body.append(payload)
            finishIfComplete(id, p)
        default:
            finish(id, p, .failure(.malformed))
        }
    }

    private func finishIfComplete(_ id: UInt32, _ p: Pending) {
        guard let r = p.result, p.body.count >= r.outLength + r.errorLength else { return }
        let out = p.body.subdata(in: 0..<r.outLength)
        let err = p.body.subdata(in: r.outLength..<(r.outLength + r.errorLength))
        finish(id, p, .success(PortalReply(type: Portal.result, status: r.status,
                                           out: String(decoding: out, as: UTF8.self),
                                           error: String(decoding: err, as: UTF8.self),
                                           text: "", truncated: r.truncated)))
    }

    private func finish(_ id: UInt32, _ p: Pending, _ result: Result<PortalReply, PortalFailure>) {
        p.timer.cancel()
        pending.removeValue(forKey: id)
        p.completion(result)
    }

    private func failAll(_ failure: PortalFailure) {
        let all = pending
        pending.removeAll()
        for (_, p) in all { p.timer.cancel(); p.completion(.failure(failure)) }
        waiting.removeAll()
    }

    // MARK: delegate

    func customVirtioConfiguration(_ configuration: VZCustomVirtioDeviceConfiguration,
                                   didCreateDevice device: VZCustomVirtioDevice) {
        self.device = device
        device.delegate = self
        log("portal: device created")
    }

    func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
        log("portal: DRIVER_OK")
    }

    func customVirtioDeviceWillReset(_ device: VZCustomVirtioDevice) {
        alive.withLock { $0 = false }
        requestElements.removeAll()
        failAll(.notAnswering)
    }

    func customVirtioDeviceWillStop(_ device: VZCustomVirtioDevice) {
        alive.withLock { $0 = false }
        requestElements.removeAll()
        failAll(.notAnswering)
    }

    func customVirtioDevice(_ device: VZCustomVirtioDevice, didReceiveNotificationFor queue: VZVirtioQueue) {
        if queue.queueIndex == 0 {
            while let element = queue.nextElement() {
                let length = element.readBuffersByteCount
                var bytes = [UInt8](repeating: 0, count: length)
                if length > 0, (try? element.readBytes(intoBuffer: &bytes, exactLength: length)) != nil {
                    handle(fromGuest: Data(bytes))
                }
                element.returnToQueue()
            }
        } else {
            while let element = queue.nextElement() { requestElements.append(element) }
            flushRequests()
        }
    }

    var stats: String {
        "portal: \(sent) requests, \(received) frames back, \(pending.count) outstanding, "
            + "guest \(alive.withLock { $0 } ? "answering" : "silent")"
    }
}
