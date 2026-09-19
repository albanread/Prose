// portal.swift: the host end of the Prose Portal — asking the guest to do
// something and hearing what happened. docs/automation.md, and the wire
// protocol in the guest's headers/private/prose_portal.h.
//
// The host can type at a screen and photograph the result without any help from
// the guest, but it cannot learn anything that way: there is no exit status in a
// screenshot, and every host-side guess at "has it booted" is either true long
// before there is a desktop or true again whenever the machine pauses. The
// portal answers instead of being inferred.
//
// The transport is a TCP connection on the virtual machine's own network, which
// under Virtualization.framework reaches no further than this Mac. The framing
// does not depend on that: a virtio device is the eventual home, and only the
// read and write calls change.
import Foundation

enum Portal {
    static let magic: UInt32 = 0x50525054          // 'PRPT'
    static let version: UInt16 = 1
    static let port: UInt16 = 7654
    static let maxPayload = 8 << 20

    static let ping: UInt16 = 1, run: UInt16 = 2, info: UInt16 = 3
    static let hello: UInt16 = 0x81, pong: UInt16 = 0x82
    static let result: UInt16 = 0x83, infoReply: UInt16 = 0x84, error: UInt16 = 0xFF
}

/// What the guest said. `status` is the command's own exit status; a portal that
/// could not be reached is an error, not a status of its own, so the two are
/// never confused.
struct PortalReply {
    let type: UInt16
    let status: Int32
    let out: String
    let error: String
    let text: String            // info replies, and error messages
}

/// One request, one connection. The guest serves one conversation at a time and
/// a caller is waiting for an answer, so there is nothing to gain from keeping
/// the socket open — and much to lose when the guest restarts underneath it.
final class PortalClient {
    private let address: String
    private let timeout: TimeInterval
    private var nextID: UInt32 = 1

    init(address: String, timeout: TimeInterval = 30) {
        self.address = address
        self.timeout = timeout
    }

    enum Failure: Error {
        case cannotConnect(String)
        case closed
        case malformed
        case guestError(String)
    }

    func send(_ type: UInt16, payload: Data = Data()) throws -> PortalReply {
        let fd = try connect()
        defer { close(fd) }
        let id = nextID
        nextID &+= 1

        var header = Data()
        header.append(contentsOf: le32(Portal.magic))
        header.append(contentsOf: [UInt8(Portal.version & 0xff), UInt8(Portal.version >> 8)])
        header.append(contentsOf: [UInt8(type & 0xff), UInt8(type >> 8)])
        header.append(contentsOf: le32(id))
        header.append(contentsOf: le32(UInt32(payload.count)))

        // the guest greets us the moment it accepts; read that before asking
        _ = try readFrame(fd)
        try writeAll(fd, header + payload)
        return try parse(try readFrame(fd))
    }

    // MARK: socket

    private func connect() throws -> Int32 {
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else { throw Failure.cannotConnect(String(cString: strerror(errno))) }
        var seconds = timeval(tv_sec: Int(timeout), tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &seconds, socklen_t(MemoryLayout<timeval>.size))
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &seconds, socklen_t(MemoryLayout<timeval>.size))
        var on: Int32 = 1
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, socklen_t(MemoryLayout<Int32>.size))

        var target = sockaddr_in()
        target.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        target.sin_family = sa_family_t(AF_INET)
        target.sin_port = Portal.port.bigEndian
        guard inet_pton(AF_INET, address, &target.sin_addr) == 1 else {
            close(fd)
            throw Failure.cannotConnect("\(address) is not an address")
        }
        let connected = withUnsafePointer(to: &target) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard connected == 0 else {
            let why = String(cString: strerror(errno))
            close(fd)
            throw Failure.cannotConnect(why)
        }
        return fd
    }

    private func writeAll(_ fd: Int32, _ data: Data) throws {
        try data.withUnsafeBytes { raw in
            var at = raw.baseAddress!, left = raw.count
            while left > 0 {
                let sent = write(fd, at, left)
                guard sent > 0 else { throw Failure.closed }
                at += sent
                left -= sent
            }
        }
    }

    private func readAll(_ fd: Int32, _ count: Int) throws -> Data {
        var data = Data(count: count)
        guard count > 0 else { return data }
        try data.withUnsafeMutableBytes { raw in
            var at = raw.baseAddress!, left = raw.count
            while left > 0 {
                let got = read(fd, at, left)
                guard got > 0 else { throw Failure.closed }
                at += got
                left -= got
            }
        }
        return data
    }

    private func readFrame(_ fd: Int32) throws -> (UInt16, Data) {
        let header = try readAll(fd, 16)
        let magic = header.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 0, as: UInt32.self) }
        let version = header.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 4, as: UInt16.self) }
        let type = header.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 6, as: UInt16.self) }
        let length = header.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 12, as: UInt32.self) }
        guard magic == Portal.magic, version == Portal.version,
              Int(length) <= Portal.maxPayload else { throw Failure.malformed }
        return (type, try readAll(fd, Int(length)))
    }

    private func parse(_ frame: (UInt16, Data)) throws -> PortalReply {
        let (type, payload) = frame
        switch type {
        case Portal.error:
            throw Failure.guestError(String(decoding: payload, as: UTF8.self))
        case Portal.result:
            guard payload.count >= 12 else { throw Failure.malformed }
            let status = payload.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 0, as: Int32.self) }
            let outLength = Int(payload.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 4, as: UInt32.self) })
            let errorLength = Int(payload.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 8, as: UInt32.self) })
            guard payload.count >= 12 + outLength + errorLength else { throw Failure.malformed }
            let out = payload.subdata(in: 12..<(12 + outLength))
            let err = payload.subdata(in: (12 + outLength)..<(12 + outLength + errorLength))
            return PortalReply(type: type, status: status,
                               out: String(decoding: out, as: UTF8.self),
                               error: String(decoding: err, as: UTF8.self), text: "")
        default:
            return PortalReply(type: type, status: 0, out: "", error: "",
                               text: String(decoding: payload, as: UTF8.self))
        }
    }
}
