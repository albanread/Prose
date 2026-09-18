// monitor.swift: what the status bar shows about the running VM, measured from outside.
//
// VZ exposes no device statistics, but every VM runs in its own XPC process
// (com.apple.Virtualization.VirtualMachine) under our user, and that is enough:
//  - the process that holds our disk image open (same device and inode) is our VM;
//  - its rusage counts the disk bytes it reads and writes, and its CPU time
//    includes the guest's vCPU threads;
//  - its NAT port is a vmenetN interface on the NAT bridge (bridge100), whose
//    counters are the guest's network traffic. Which one is ours: the bridge's
//    address table says which member learned the guest's MAC address (the
//    firmware's DHCP teaches it within a second or two of starting). Interface
//    numbers are recycled, so nothing else about a vmenet ties it to a VM;
//  - bootpd's lease file maps the guest's MAC address to the IP it handed out;
//  - Core Audio says whether that process is playing or recording.
import CoreAudio
import Darwin
import Foundation

struct VMCounters {
    var cpuNanos: UInt64 = 0
    var diskRead: UInt64 = 0, diskWritten: UInt64 = 0
    var netReceived: UInt64 = 0, netSent: UInt64 = 0      // bytes, as the guest sees it
    var packetsReceived: UInt64 = 0, packetsSent: UInt64 = 0
    var footprint: UInt64 = 0
}

final class VMMonitor {
    static let serviceName = "com.apple.Virtualization.VirtualMachine"
    static let leaseFile = "/var/db/dhcpd_leases"

    let diskImage: URL
    let guestMAC: String?                   // lowercase, two digits per octet
    private(set) var pid: pid_t = 0
    private(set) var interface: String?     // vmenetN
    private var interfaceIndex: UInt32 = 0
    private var audioObject = AudioObjectID(kAudioObjectUnknown)
    private let ticksToNanos: Double

    init(diskImage: URL, guestMAC: String?) {
        self.diskImage = diskImage
        self.guestMAC = guestMAC.map(VMMonitor.normalizedMAC)
        var timebase = mach_timebase_info_data_t()
        mach_timebase_info(&timebase)
        ticksToNanos = Double(timebase.numer) / Double(timebase.denom)
    }

    /// Each start may be a new VM process: forget the old one.
    func reset() {
        pid = 0
        interface = nil
        interfaceIndex = 0
        audioObject = AudioObjectID(kAudioObjectUnknown)
    }

    // MARK: finding the VM's process

    /// Look for the VZ process holding our disk image, then for its NAT port (known once
    /// the guest has sent a frame). True once the process is known; call again for the port.
    @discardableResult
    func locate() -> Bool {
        if pid == 0 {
            var st = stat()
            guard stat(diskImage.path, &st) == 0 else { return false }
            let disk = (UInt32(bitPattern: st.st_dev), st.st_ino)
            pid = VMMonitor.allPIDs().first { VMMonitor.isVMService($0) && VMMonitor.openFiles($0).contains { $0 == disk } } ?? 0
        }
        if pid != 0, interface == nil, let mac = guestMAC, let port = VMMonitor.bridgePort(for: mac) {
            let index = if_nametoindex(port)
            if index != 0 {
                interface = port
                interfaceIndex = index
            }
        }
        return pid != 0
    }

    /// The files a process holds open, as (device, inode).
    private static func openFiles(_ pid: pid_t) -> [(UInt32, UInt64)] {
        fileDescriptors(pid).compactMap { fd in
            guard fd.proc_fdtype == UInt32(PROX_FDTYPE_VNODE) else { return nil }
            var info = vnode_fdinfowithpath()
            let size = Int32(MemoryLayout<vnode_fdinfowithpath>.size)
            guard proc_pidfdinfo(pid, fd.proc_fd, PROC_PIDFDVNODEPATHINFO, &info, size) == size else { return nil }
            return (info.pvip.vip_vi.vi_stat.vst_dev, info.pvip.vip_vi.vi_stat.vst_ino)
        }
    }

    // The bridge's address table (if_bridgevar.h, not in the SDK): SIOCGDRVSPEC with
    // BRDGRTS fills an ifbaconf, pack(4) { u32 len; void *buf }, with ifbareq records,
    // pack(4) { char ifsname[16]; u64 expire; u8 flags; u8 dst[6]; u16 vlan } = 36 bytes.
    private static let SIOCGDRVSPEC: UInt = 0xC000_0000 | (UInt(MemoryLayout<ifdrv>.size & 0x1fff) << 16) | (0x69 << 8) | 123
    private static let BRDGRTS: UInt = 7
    private static let addressRecordSize = 36

    /// The bridge member (vmenetN) that learned this MAC address, if any.
    static func bridgePort(for mac: String) -> String? {
        guard let names = if_nameindex() else { return nil }
        defer { if_freenameindex(names) }
        var bridges: [String] = []
        var entry = names
        while entry.pointee.if_index != 0 {
            let name = String(cString: entry.pointee.if_name)
            if name.hasPrefix("bridge") { bridges.append(name) }
            entry += 1
        }
        let s = socket(AF_INET, SOCK_DGRAM, 0)
        guard s >= 0 else { return nil }
        defer { close(s) }
        let capacity = addressRecordSize * 1024
        let records = UnsafeMutableRawPointer.allocate(byteCount: capacity, alignment: 8)
        defer { records.deallocate() }
        let conf = UnsafeMutableRawPointer.allocate(byteCount: 16, alignment: 8)
        defer { conf.deallocate() }
        for bridge in bridges {
            conf.storeBytes(of: UInt32(capacity), toByteOffset: 0, as: UInt32.self)
            conf.storeBytes(of: UInt(bitPattern: records), toByteOffset: 4, as: UInt.self)
            var request = ifdrv()
            let nameBytes = Array(bridge.utf8.prefix(Int(IFNAMSIZ) - 1)) + [0]
            withUnsafeMutableBytes(of: &request.ifd_name) { $0.copyBytes(from: nameBytes) }
            request.ifd_cmd = BRDGRTS
            request.ifd_len = 12
            request.ifd_data = conf
            guard ioctl(s, SIOCGDRVSPEC, &request) == 0 else { continue }
            let length = min(Int(conf.load(fromByteOffset: 0, as: UInt32.self)), capacity)
            var offset = 0
            while offset + addressRecordSize <= length {
                let dst = (0..<6).map { String(format: "%02x", records.load(fromByteOffset: offset + 25 + $0, as: UInt8.self)) }
                if dst.joined(separator: ":") == mac {
                    return String(cString: (records + offset).assumingMemoryBound(to: CChar.self))
                }
                offset += addressRecordSize
            }
        }
        return nil
    }

    private static func allPIDs() -> [pid_t] {
        let count = proc_listallpids(nil, 0)
        guard count > 0 else { return [] }
        var pids = [pid_t](repeating: 0, count: Int(count) + 64)
        let got = proc_listallpids(&pids, Int32(pids.count * MemoryLayout<pid_t>.size))
        return Array(pids.prefix(Int(max(got, 0))))
    }

    private static func isVMService(_ pid: pid_t) -> Bool {
        // proc_name stops at 32 characters: check the prefix, then the full path
        var name = [CChar](repeating: 0, count: 64)
        guard proc_name(pid, &name, UInt32(name.count)) > 0,
              String(cString: name).hasPrefix("com.apple.Virtualization.Virtu") else { return false }
        var path = [CChar](repeating: 0, count: Int(MAXPATHLEN) * 4)
        guard proc_pidpath(pid, &path, UInt32(path.count)) > 0 else { return false }
        return String(cString: path).hasSuffix("/" + serviceName)
    }

    private static func fileDescriptors(_ pid: pid_t) -> [proc_fdinfo] {
        let size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nil, 0)
        guard size > 0 else { return [] }
        var fds = [proc_fdinfo](repeating: proc_fdinfo(), count: Int(size) / MemoryLayout<proc_fdinfo>.stride + 16)
        let got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, &fds, Int32(fds.count * MemoryLayout<proc_fdinfo>.stride))
        return Array(fds.prefix(Int(max(got, 0)) / MemoryLayout<proc_fdinfo>.stride))
    }

    // MARK: sampling

    /// Cumulative counters, or nil while the VM's process is unknown or gone.
    func sample() -> VMCounters? {
        guard pid != 0 else { return nil }
        var usage = rusage_info_v6()
        let status = withUnsafeMutablePointer(to: &usage) {
            $0.withMemoryRebound(to: rusage_info_t?.self, capacity: 1) { proc_pid_rusage(pid, RUSAGE_INFO_V6, $0) }
        }
        guard status == 0 else {
            reset()                 // the process ended
            return nil
        }
        var c = VMCounters()
        c.cpuNanos = UInt64(Double(usage.ri_user_time + usage.ri_system_time) * ticksToNanos)
        c.diskRead = usage.ri_diskio_bytesread
        c.diskWritten = usage.ri_diskio_byteswritten
        c.footprint = usage.ri_phys_footprint
        if interfaceIndex != 0 {
            if let port = VMMonitor.interfaceData(index: interfaceIndex) {
                // the host's port on the bridge: what it receives, the guest sent
                c.netReceived = port.ifi_obytes
                c.netSent = port.ifi_ibytes
                c.packetsReceived = port.ifi_opackets
                c.packetsSent = port.ifi_ipackets
            } else {
                interface = nil         // gone: look it up again
                interfaceIndex = 0
            }
        }
        return c
    }

    /// One interface's 64-bit counters (net.link.generic.ifdata.<index>.general, as netstat
    /// reads them: exact, where getifaddrs and NET_RT_IFLIST2 round bytes down to KiB).
    private static func interfaceData(index: UInt32) -> if_data64? {
        var mib: [Int32] = [CTL_NET, PF_LINK, NETLINK_GENERIC, IFMIB_IFDATA, Int32(index), IFDATA_GENERAL]
        var data = ifmibdata()
        var length = MemoryLayout<ifmibdata>.size
        guard sysctl(&mib, UInt32(mib.count), &data, &length, nil, 0) == 0 else { return nil }
        return data.ifmd_data
    }

    /// Whether the VM's process is running audio output (the guest plays) and input (it records).
    func audio() -> (output: Bool, input: Bool) {
        guard pid != 0 else { return (false, false) }
        if audioObject == kAudioObjectUnknown {
            var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyTranslatePIDToProcessObject,
                                                     mScope: kAudioObjectPropertyScopeGlobal,
                                                     mElement: kAudioObjectPropertyElementMain)
            var qualifier = pid
            var object = AudioObjectID(kAudioObjectUnknown)
            var size = UInt32(MemoryLayout<AudioObjectID>.size)
            guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address,
                                             UInt32(MemoryLayout<pid_t>.size), &qualifier, &size, &object) == noErr,
                  object != kAudioObjectUnknown else { return (false, false) }
            audioObject = object
        }
        func flag(_ selector: AudioObjectPropertySelector) -> Bool {
            var address = AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
                                                     mElement: kAudioObjectPropertyElementMain)
            var value: UInt32 = 0
            var size = UInt32(MemoryLayout<UInt32>.size)
            return AudioObjectGetPropertyData(audioObject, &address, 0, nil, &size, &value) == noErr && value != 0
        }
        return (flag(kAudioProcessPropertyIsRunningOutput), flag(kAudioProcessPropertyIsRunningInput))
    }

    // MARK: the guest's address

    /// The IP bootpd leased to our MAC address (the file lists the newest lease first).
    func guestIP() -> String? {
        guard let mac = guestMAC,
              let text = try? String(contentsOfFile: VMMonitor.leaseFile, encoding: .utf8) else { return nil }
        var ip: String?, hw: String?
        for line in text.split(separator: "\n") {
            let entry = line.trimmingCharacters(in: .whitespaces)
            if entry == "{" {
                ip = nil
                hw = nil
            } else if entry.hasPrefix("ip_address=") {
                ip = String(entry.dropFirst("ip_address=".count))
            } else if entry.hasPrefix("hw_address=") {
                // "1,26:a0:4f:35:97:d2" (hardware type, then octets without leading zeros)
                hw = entry.split(separator: ",", maxSplits: 1).last.map { VMMonitor.normalizedMAC(String($0)) }
            } else if entry == "}", hw == mac, let ip {
                return ip
            }
        }
        return nil
    }

    static func normalizedMAC(_ mac: String) -> String {
        mac.split(separator: ":").map { String(format: "%02x", UInt8($0, radix: 16) ?? 0) }.joined(separator: ":")
    }
}

/// Byte counts the way Finder writes them.
func formatBytes(_ bytes: UInt64) -> String {
    ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file)
}
