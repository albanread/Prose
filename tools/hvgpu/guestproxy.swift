// guestproxy.swift: the guest's way out when something on the Mac owns the route.
//
// A VPN on the Mac takes the guest off the internet. vmnet still NATs, so the
// guest reaches the gateway and the local network and nothing beyond: what is
// forwarded into the tunnel does not come back. Reconfiguring the VPN is one
// answer and it is not ours to give.
//
// This is the other. The proxy listens on the machine's own bridge address and
// opens ordinary sockets from this process, so its traffic takes whatever route
// the Mac has -- tunnel included -- while the guest only ever talks to the
// gateway it can already reach. It speaks enough HTTP to be a proxy and nothing
// more: CONNECT for https, absolute-URI requests for http, bytes after that.
//
// It binds to the bridge address alone, which exists only while a machine is
// running and is reachable only from that machine, so nothing outside can use
// it. NetSurf and anything else built on libcurl can point at it; Haiku's own
// BUrlRequest has no proxy support, so pkgman cannot. See
// tools/guestproxy/README.md.

import Foundation
import Network

final class GuestProxy {
    private var listener: NWListener?
    private let queue = DispatchQueue(label: "org.prose.guestproxy", attributes: .concurrent)
    private(set) var host: String?
    private(set) var port: UInt16 = 0
    private var served = 0

    var isRunning: Bool { listener != nil }

    /// What the menu and the settings window say about it.
    var summary: String {
        guard let host, isRunning else { return "off" }
        return "\(host):\(port)"
    }

    // MARK: starting and stopping

    @discardableResult
    func start(on address: String, port wanted: UInt16) -> Bool {
        if isRunning, host == address, port == wanted { return true }
        stop()
        let parameters = NWParameters.tcp
        parameters.requiredLocalEndpoint = .hostPort(host: .init(address),
                                                     port: .init(integerLiteral: wanted))
        parameters.allowLocalEndpointReuse = true
        guard let listener = try? NWListener(using: parameters) else {
            log("guest proxy: cannot listen on \(address):\(wanted)")
            return false
        }
        listener.newConnectionHandler = { [weak self] in self?.accept($0) }
        listener.stateUpdateHandler = { [weak self] state in
            if case .failed(let error) = state {
                log("guest proxy: \(error)")
                self?.stop()
            }
        }
        listener.start(queue: queue)
        self.listener = listener
        self.host = address
        self.port = wanted
        log("guest proxy: listening on \(address):\(wanted)")
        return true
    }

    func stop() {
        guard let listener else { return }
        listener.cancel()
        self.listener = nil
        if let host { log("guest proxy: stopped on \(host):\(port) after \(served) requests") }
        host = nil
        served = 0
    }

    // MARK: one client

    private func accept(_ client: NWConnection) {
        client.start(queue: queue)
        readHead(client, Data())
    }

    /// A proxy request is its head; everything after the blank line belongs to
    /// whoever we hand the connection to, so it is kept and passed on.
    private func readHead(_ client: NWConnection, _ sofar: Data) {
        client.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) {
            [weak self] data, _, done, error in
            guard let self else { return client.cancel() }
            guard error == nil, let data, !data.isEmpty else { return client.cancel() }
            var buffer = sofar
            buffer.append(data)
            guard let end = buffer.range(of: Data("\r\n\r\n".utf8)) else {
                // A head this long is not a head.
                if done || buffer.count > 64 * 1024 { return client.cancel() }
                return self.readHead(client, buffer)
            }
            let head = String(decoding: buffer[..<end.lowerBound], as: UTF8.self)
            self.dispatch(client, head, Data(buffer[end.upperBound...]))
        }
    }

    private func dispatch(_ client: NWConnection, _ head: String, _ rest: Data) {
        let lines = head.components(separatedBy: "\r\n")
        let request = lines.first ?? ""
        let parts = request.split(separator: " ", maxSplits: 2).map(String.init)
        guard parts.count == 3 else { return refuse(client, "400 Bad Request") }
        let method = parts[0], target = parts[1]
        served += 1

        if method == "CONNECT" {
            guard let colon = target.lastIndex(of: ":"),
                  let port = UInt16(target[target.index(after: colon)...])
            else { return refuse(client, "400 Bad Request") }
            let peer = String(target[..<colon])
            open(peer, port) { [weak self] upstream in
                guard let self, let upstream else { return self?.refuse(client, "502 Bad Gateway") ?? () }
                client.send(content: Data("HTTP/1.1 200 Connection established\r\n\r\n".utf8),
                            completion: .contentProcessed { _ in self.pump(client, upstream) })
            }
            return
        }

        // An absolute-URI request: the proxy form of an ordinary GET. Only the
        // request line changes, and the hop-by-hop headers go.
        guard target.hasPrefix("http://") else { return refuse(client, "400 Bad Request") }
        let authority = target.dropFirst("http://".count)
        let slash = authority.firstIndex(of: "/")
        let hostport = String(slash.map { authority[..<$0] } ?? authority)
        let path = slash.map { String(authority[$0...]) } ?? "/"
        let colon = hostport.lastIndex(of: ":")
        let peer = colon.map { String(hostport[..<$0]) } ?? hostport
        let port = colon.flatMap { UInt16(hostport[hostport.index(after: $0)...]) } ?? 80

        open(peer, port) { [weak self] upstream in
            guard let self, let upstream else { return self?.refuse(client, "502 Bad Gateway") ?? () }
            var out = ["\(method) \(path) \(parts[2])"]
            for line in lines.dropFirst() {
                let name = line.prefix(while: { $0 != ":" }).lowercased()
                if name == "proxy-connection" || name == "connection" { continue }
                out.append(line)
            }
            out += ["Connection: close", "", ""]
            var head = Data(out.joined(separator: "\r\n").utf8)
            head.append(rest)
            upstream.send(content: head, completion: .contentProcessed { _ in
                self.pump(client, upstream)
            })
        }
    }

    private func refuse(_ client: NWConnection, _ status: String) {
        client.send(content: Data("HTTP/1.1 \(status)\r\nConnection: close\r\n\r\n".utf8),
                    completion: .contentProcessed { _ in client.cancel() })
    }

    // MARK: the other end

    private func open(_ peer: String, _ port: UInt16, _ ready: @escaping (NWConnection?) -> Void) {
        let upstream = NWConnection(host: .init(peer), port: .init(integerLiteral: port), using: .tcp)
        var answered = false
        upstream.stateUpdateHandler = { state in
            switch state {
            case .ready:
                guard !answered else { return }
                answered = true
                ready(upstream)
            case .failed, .cancelled:
                guard !answered else { return }
                answered = true
                upstream.cancel()
                ready(nil)
            default:
                break
            }
        }
        upstream.start(queue: queue)
    }

    /// Bytes both ways until either end is finished with them.
    private func pump(_ a: NWConnection, _ b: NWConnection) {
        relay(a, to: b)
        relay(b, to: a)
    }

    private func relay(_ from: NWConnection, to: NWConnection) {
        from.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) {
            [weak self] data, _, done, error in
            if let data, !data.isEmpty {
                to.send(content: data, completion: .contentProcessed { _ in })
            }
            guard error == nil, !done else {
                from.cancel()
                to.cancel()
                return
            }
            self?.relay(from, to: to)
        }
    }
}

// MARK: - keeping the guest's own settings in step

extension Controller {
    /// NetSurf reads a proxy out of its Choices file, and the image ships one
    /// pointing at the usual bridge address. That is a guess: the bridge can be
    /// somewhere else (Shared_Net_Address moves it), the port is a setting, and
    /// the proxy can be switched off entirely -- and a Choices file naming a
    /// proxy that is not there is worse than none, because then nothing loads.
    ///
    /// So the side that knows says so, once the guest is answering. The file is
    /// rewritten whole rather than edited: it is four lines, and a half-applied
    /// edit is the failure this exists to prevent.
    func syncGuestProxySettings() {
        guard portal.alive.withLock({ $0 }) else { return }
        // The guest says hello before the bridge is up, so the proxy is enabled
        // and not yet listening for a moment. Writing "direct" into that gap
        // and "through" a moment later is two round trips to reach the answer
        // we already know, so wait for it.
        if Settings.guestProxy && !guestProxy.isRunning { return }
        let choices = "/boot/home/config/settings/NetSurf/Choices"
        let wanted: String
        if guestProxy.isRunning, let host = guestProxy.host {
            wanted = "http_proxy:1\nhttp_proxy_host:\(host)\nhttp_proxy_port:\(guestProxy.port)\nhttp_proxy_auth:0"
        } else {
            // Off, not absent: leave the address behind so switching it back on
            // is one line, and so the file still says what it was for.
            wanted = "http_proxy:0\nhttp_proxy_host:\(guestProxy.host ?? "192.168.64.1")\nhttp_proxy_port:\(Settings.guestProxyPort)\nhttp_proxy_auth:0"
        }
        guard wanted != lastGuestProxyChoices else { return }
        // Claimed before it is sent, not after: this is called from several
        // places at once during a start, and a flag set in the completion
        // handler lets every one of them through.
        lastGuestProxyChoices = wanted
        // Written to a temporary file and moved, so NetSurf never reads a half
        // one, and only if it would differ -- this runs on every state change.
        let script = "mkdir -p /boot/home/config/settings/NetSurf"
            + " && printf '%s\\n' '\(wanted.replacingOccurrences(of: "\n", with: "' '"))'"
            + " > /boot/home/config/settings/NetSurf/Choices.tmp"
            + " && mv /boot/home/config/settings/NetSurf/Choices.tmp \(choices)"
        guestRun(script, timeout: 20) { [weak self] result in
            guard let self else { return }
            if case .failed(let why) = result {
                log("guest proxy: could not set NetSurf's proxy in the guest: \(why)")
                lastGuestProxyChoices = nil        // so the next change tries again
                return
            }
            log("guest proxy: NetSurf in the guest now "
                + (guestProxy.isRunning ? "goes through \(guestProxy.summary)" : "goes out directly"))
        }
    }
}
