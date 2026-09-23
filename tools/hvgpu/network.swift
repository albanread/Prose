// network.swift: Machine ▸ Network — what address the machine has, and how to
// give it one when the Mac will not.
//
// The Mac's NAT hands out addresses through bootpd, whose lease database is a
// file that fills up: after enough virtual machines have come and gone every
// entry in it has expired, and rather than reuse one the server stops
// answering. The guest asks, the bridge carries the question, and nothing
// comes back. Waiting does not fix it and there is no service to restart --
// bootpd is socket-activated and disabled in its own plist, brought up by
// Virtualization.framework on demand.
//
// So the machine should not have to depend on it. The host knows the bridge's
// own address and netmask, and it can read which addresses the lease file has
// spoken for; picking a free one and telling the guest to use it is three
// commands through the portal. That is what "Set a Static Address" does, and
// it is the difference between a machine that is off the network for an hour
// and one that is on it in a second.
//
// The owner choosing from their own machine's menu needs no automation
// permission; that switch is for other applications on this Mac.
import AppKit
import Foundation

/// What the guest says about one of its interfaces.
struct GuestInterface {
    var name: String            // /dev/net/virtio/0
    var address: String?        // nil when it has none
    var mask: String?
}

/// The Mac's side of the NAT: the bridge the machine is plugged into.
struct HostBridge {
    var interface: String       // bridge100
    var gateway: String         // 192.168.64.1
    var mask: String            // 255.255.255.0
    var prefix: String          // 192.168.64.
}

extension Controller {

    // MARK: what the Mac knows

    /// The vmnet bridge a NAT machine is on. There is normally exactly one,
    /// and it exists only while some virtual machine is running.
    func hostBridge() -> HostBridge? {
        var addresses: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&addresses) == 0, let first = addresses else { return nil }
        defer { freeifaddrs(addresses) }
        var found: HostBridge?
        for pointer in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let name = String(cString: pointer.pointee.ifa_name)
            guard name.hasPrefix("bridge"), let sa = pointer.pointee.ifa_addr,
                  sa.pointee.sa_family == UInt8(AF_INET) else { continue }
            func text(_ sa: UnsafeMutablePointer<sockaddr>?) -> String? {
                guard let sa else { return nil }
                var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
                guard getnameinfo(sa, socklen_t(sa.pointee.sa_len), &host,
                                  socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0
                else { return nil }
                return String(cString: host)
            }
            guard let gateway = text(sa), let mask = text(pointer.pointee.ifa_netmask)
            else { continue }
            // vmnet numbers its bridges from 100; bridge0 is the Mac's own, and
            // matching on the address instead would miss a shared network moved
            // off 192.168.64 by Shared_Net_Address.
            guard let number = Int(name.dropFirst("bridge".count)), number >= 100
            else { continue }
            let prefix = gateway.split(separator: ".").dropLast().joined(separator: ".") + "."
            found = HostBridge(interface: name, gateway: gateway, mask: mask, prefix: prefix)
            break
        }
        return found
    }

    /// How full the Mac's lease database is, and how much of it is dead.
    ///
    /// bootpd hands addresses out from the bottom of the range and will not
    /// reuse an expired entry, so after enough machines have come and gone it
    /// simply stops answering. That is not a failure anyone is told about --
    /// the guest asks and nothing comes back -- so the menu says it instead.
    func leasePool() -> (total: Int, expired: Int) {
        guard let text = try? String(contentsOfFile: VMMonitor.leaseFile, encoding: .utf8) else {
            return (0, 0)
        }
        let now = Date().timeIntervalSince1970
        var total = 0, expired = 0
        for line in text.split(separator: "\n") {
            let entry = line.trimmingCharacters(in: .whitespaces)
            guard entry.hasPrefix("lease=0x") else { continue }
            total += 1
            if TimeInterval(UInt64(entry.dropFirst("lease=0x".count), radix: 16) ?? 0) < now {
                expired += 1
            }
        }
        return (total, expired)
    }

    /// An address on the bridge's subnet that the lease file has not spoken
    /// for. Counts down from .250, because bootpd hands out from the bottom:
    /// starting at the other end is the least likely to collide with a lease
    /// it gives someone else later.
    func freeAddress(on bridge: HostBridge) -> String? {
        var taken = Set<String>()
        if let text = try? String(contentsOfFile: VMMonitor.leaseFile, encoding: .utf8) {
            for line in text.split(separator: "\n") {
                let entry = line.trimmingCharacters(in: .whitespaces)
                if entry.hasPrefix("ip_address=") {
                    taken.insert(String(entry.dropFirst("ip_address=".count)))
                }
            }
        }
        taken.insert(bridge.gateway)
        for last in stride(from: 250, through: 200, by: -1) {
            let candidate = bridge.prefix + String(last)
            if !taken.contains(candidate) { return candidate }
        }
        return nil
    }

    // MARK: what the guest says

    /// Read the guest's interfaces. `ifconfig` starts an interface at the left
    /// margin and indents what it then says about it, so the shape parses
    /// without asking the guest for anything cleverer.
    func refreshNetwork(_ completion: (([GuestInterface]) -> Void)? = nil) {
        guestRun("ifconfig", timeout: 10) { [weak self] result in
            guard let self else { return }
            var found: [GuestInterface] = []
            if case .ok(let out) = result {
                var current: GuestInterface?
                for raw in out.split(separator: "\n", omittingEmptySubsequences: false) {
                    let line = String(raw)
                    if !line.hasPrefix("\t") && !line.hasPrefix(" ") {
                        if let c = current, c.name.hasPrefix("/dev/") { found.append(c) }
                        let name = line.split(separator: "\t").first.map(String.init)
                            ?? line.trimmingCharacters(in: .whitespaces)
                        current = name.isEmpty ? nil : GuestInterface(name: name)
                    } else if line.contains("inet addr:") {
                        let fields = line.split(separator: ",")
                        for field in fields {
                            let t = field.trimmingCharacters(in: .whitespaces)
                            if t.hasPrefix("inet addr:") {
                                let value = t.dropFirst("inet addr:".count)
                                    .trimmingCharacters(in: .whitespaces)
                                if value != "--" { current?.address = value }
                            } else if t.hasPrefix("Mask:") {
                                let value = t.dropFirst("Mask:".count)
                                    .trimmingCharacters(in: .whitespaces)
                                if value != "--" { current?.mask = value }
                            }
                        }
                    }
                }
                if let c = current, c.name.hasPrefix("/dev/") { found.append(c) }
            }
            guestInterfaces = found
            rebuildNetworkMenu()
            completion?(found)
        }
    }

    // MARK: the menu

    func rebuildNetworkMenu() {
        networkMenu.removeAllItems()
        let answering = portal.alive.withLock { $0 }
        guard answering else {
            let item = NSMenuItem(title: "Machine Not Running", action: nil, keyEquivalent: "")
            item.isEnabled = false
            networkMenu.addItem(item)
            return
        }

        let wired = guestInterfaces.first
        let status: String
        if let wired {
            status = wired.address.map { "\(wired.name) — \($0)" } ?? "\(wired.name) — no address"
        } else {
            status = "No network interface"
        }
        let line = NSMenuItem(title: status, action: nil, keyEquivalent: "")
        line.isEnabled = false
        networkMenu.addItem(line)

        // When there is no address, say why rather than leaving the owner to
        // find out the hard way. A pool with nothing live left in it is the
        // usual reason and the one nothing else reports.
        if wired?.address == nil {
            let pool = leasePool()
            if pool.total > 0 && pool.expired == pool.total {
                let why = NSMenuItem(
                    title: "The Mac's DHCP pool is full — \(pool.total) leases, all expired",
                    action: nil, keyEquivalent: "")
                why.isEnabled = false
                networkMenu.addItem(why)
                let copy = NSMenuItem(title: "Copy the Command to Clear It",
                                      action: #selector(copyLeaseFix(_:)), keyEquivalent: "")
                copy.target = self
                copy.toolTip = "bootpd will not reuse an expired lease and there is no "
                    + "service to restart: it is socket-activated and disabled in its own "
                    + "plist. Removing its database is the lever. It needs your password."
                networkMenu.addItem(copy)
            }
        }
        networkMenu.addItem(.separator())

        let renew = NSMenuItem(title: "Renew from DHCP",
                               action: #selector(renewDHCP(_:)), keyEquivalent: "")
        renew.target = self
        renew.isEnabled = wired != nil
        renew.toolTip = "Ask the Mac's DHCP server again. It may not answer: its lease "
            + "file fills up, and then it stops rather than reusing an expired address."
        networkMenu.addItem(renew)

        let assign = NSMenuItem(title: "Set a Static Address",
                                action: #selector(assignStaticAddress(_:)), keyEquivalent: "")
        assign.target = self
        assign.isEnabled = wired != nil && hostBridge() != nil
        assign.toolTip = "Pick an address the Mac has not given away and set it here, with "
            + "the bridge as the gateway and the name server. This always reaches the Mac; "
            + "whether it reaches beyond depends on the Mac's NAT, which forwards only for "
            + "machines it believes in."
        networkMenu.addItem(assign)

        networkMenu.addItem(.separator())
        let refresh = NSMenuItem(title: "Refresh",
                                 action: #selector(refreshNetworkMenu(_:)), keyEquivalent: "")
        refresh.target = self
        networkMenu.addItem(refresh)
    }

    @objc func refreshNetworkMenu(_ sender: Any?) {
        refreshNetwork()
    }

    @objc func copyLeaseFix(_ sender: Any?) {
        let command = "sudo rm /var/db/dhcpd_leases"
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(command, forType: .string)
        chrome?.content.statusBar.show(message: "Copied: \(command)")
    }

    @objc func renewDHCP(_ sender: Any?) {
        guard let wired = guestInterfaces.first else { return }
        chrome?.content.statusBar.show(message: "Asking for an address…")
        guestRun("ifconfig \(wired.name) auto-config", timeout: 30) { [weak self] _ in
            guard let self else { return }
            // Give the exchange a moment before believing the answer.
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
                self?.refreshNetwork { interfaces in
                    let address = interfaces.first?.address
                    self?.chrome?.content.statusBar.show(message: address.map {
                        "Address \($0)" } ?? "No answer from DHCP")
                }
            }
        }
    }

    /// Set an address the Mac has not given away, with the bridge as gateway
    /// and resolver. Three commands, and the machine is on the network.
    /// A safety net for a machine DHCP never answers, and nothing more.
    ///
    /// **DHCP here takes a minute or two**, and longer is not failure, so the
    /// wait is three. An earlier version waited 25 seconds, which is not a
    /// fallback but a race: it overwrote an address that was on its way and
    /// made a working network look like a broken one. `--dhcp-wait 0` turns it
    /// off entirely.
    ///
    /// What it is actually for: a VPN connected on the Mac blocks the guest's
    /// DHCP broadcasts before they reach bootpd, so the machine will wait for
    /// an answer that is never coming (docs/networking.md has the capture).
    /// Three minutes in, an address of our own beats no address at all.
    ///
    /// Only ever when there is none: an address that came from DHCP is left
    /// alone, and so is a machine whose networking is switched off.
    func ensureGuestAddress(after delay: TimeInterval? = nil) {
        guard Settings.networking else { return }
        // How long DHCP gets before we decide it is not coming. Tunable because
        // the right number is a measurement, not a guess: --dhcp-wait 0 leaves
        // the machine to DHCP however long it takes.
        let delay = delay ?? (Double(option("--dhcp-wait") ?? "") ?? 180)
        guard delay > 0 else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self, vm?.state == .running else { return }
            refreshNetwork { [weak self] interfaces in
                guard let self, let wired = interfaces.first else { return }
                if wired.address != nil { return }          // DHCP answered after all
                log("network: \(wired.name) has no address \(Int(delay)) s in — "
                    + "the Mac's DHCP is not answering, so taking one")
                setStaticAddress()
            }
        }
    }

    /// What the guest has is learned when the Network menu is built, so a click
    /// that arrives before that -- a script, or a menu driven from outside --
    /// would find nothing and say so in the words of a different failure.
    @objc func assignStaticAddress(_ sender: Any?) {
        guard !guestInterfaces.isEmpty else {
            chrome?.content.statusBar.show(message: "Asking the guest what it has…")
            refreshNetwork { [weak self] _ in self?.setStaticAddress() }
            return
        }
        setStaticAddress()
    }

    /// Each way this can fail says which one it was: they need different fixes.
    func setStaticAddress() {
        guard let wired = guestInterfaces.first else {
            chrome?.content.statusBar.show(message: "The guest has no network interface")
            return
        }
        guard let bridge = hostBridge() else {
            chrome?.content.statusBar.show(message: "No bridge to be on — is networking switched on?")
            return
        }
        guard let address = freeAddress(on: bridge) else {
            chrome?.content.statusBar.show(message: "No free address on \(bridge.interface)")
            return
        }
        chrome?.content.statusBar.show(message: "Setting \(address)…")
        // The resolver reads /etc/resolv.conf, and the NAT's gateway answers
        // DNS as well as routing, so it is both.
        let script = "ifconfig \(wired.name) \(address) \(bridge.mask)"
            + " && route add \(wired.name) default gw \(bridge.gateway)"
            + " ; echo nameserver \(bridge.gateway) > /etc/resolv.conf ; echo done"
        guestRun(script, timeout: 30) { [weak self] result in
            guard let self else { return }
            if case .failed(let why) = result {
                chrome?.content.statusBar.show(message: "Could not set an address: \(why)")
                return
            }
            refreshNetwork { [weak self] interfaces in
                let got = interfaces.first?.address
                self?.chrome?.content.statusBar.show(message: got.map {
                    "Address \($0), gateway \(bridge.gateway)" } ?? "The address did not take")
            }
        }
    }
}
