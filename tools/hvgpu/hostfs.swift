// hvgpu: HostFS — macOS directories as a disk in the guest.
//
// The device is VZ's own virtio-fs (PCI 1AF4:105A, class mass storage/other) with
// VZ's own FUSE server behind it, so the host side is only configuration. In the
// guest, the Haiku fork's virtio_fs driver publishes the device as a small
// read-only pseudo-disk (/dev/disk/virtual/virtio_fs/N/raw) that the hostfs file
// system recognises. mount_server then mounts it at boot like any disk, at
// /<tag>, and it shows up on the Desktop, in Tracker's Mount menu and in DriveSetup.
//
//   --share PATH       share PATH read-write (repeatable)
//   --share-ro PATH    share PATH read-only (repeatable)
//   --share-tag TAG    virtio-fs tag, which is also the Haiku volume name (default HostFS)
//
// One share: the volume is that directory. Several: the volume holds one folder per
// share, named after the directory (VZMultipleDirectoryShare).
import Foundation
import Virtualization

struct HostShare {
    let url: URL
    let readOnly: Bool
}

/// Every --share / --share-ro on the command line, in order.
func hostShares() -> [HostShare] {
    var shares: [HostShare] = []
    var i = 1
    while i < args.count {
        if args[i] == "--share" || args[i] == "--share-ro", i + 1 < args.count {
            let path = (args[i + 1] as NSString).expandingTildeInPath
            shares.append(HostShare(url: URL(fileURLWithPath: path).standardizedFileURL,
                                    readOnly: args[i] == "--share-ro"))
            i += 2
        } else {
            i += 1
        }
    }
    // An installed copy shares a folder without being asked to: a machine with no
    // way to exchange a file with the Mac it runs on is much less useful, and there
    // is no command line to put a --share on. A script that passes none gets none.
    if shares.isEmpty, usingInstalledMachine {
        let folder = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("HostFS", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        if FileManager.default.fileExists(atPath: folder.path) {
            shares.append(HostShare(url: folder.standardizedFileURL, readOnly: false))
            log("hostfs: sharing \(folder.path)")
        }
    }
    return shares
}

func makeHostFSDevice() throws -> VZVirtioFileSystemDeviceConfiguration? {
    let shares = hostShares()
    guard !shares.isEmpty else { return nil }
    let tag = option("--share-tag") ?? "HostFS"
    try VZVirtioFileSystemDeviceConfiguration.validateTag(tag)
    for share in shares {
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: share.url.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            throw NSError(domain: "hvgpu", code: 2, userInfo: [
                NSLocalizedDescriptionKey: "--share: not a directory: \(share.url.path)"])
        }
    }

    let device = VZVirtioFileSystemDeviceConfiguration(tag: tag)
    if shares.count == 1 {
        device.share = VZSingleDirectoryShare(
            directory: VZSharedDirectory(url: shares[0].url, readOnly: shares[0].readOnly))
    } else {
        var directories: [String: VZSharedDirectory] = [:]
        for share in shares {
            var name = share.url.lastPathComponent
            if name.isEmpty || name == "/" { name = "root" }
            var unique = name
            var n = 2
            while directories[unique] != nil {
                unique = "\(name)-\(n)"
                n += 1
            }
            directories[unique] = VZSharedDirectory(url: share.url, readOnly: share.readOnly)
        }
        device.share = VZMultipleDirectoryShare(directories: directories)
    }
    for share in shares {
        log("HostFS: \(share.url.path) \(share.readOnly ? "read-only" : "read-write"), tag \(tag)")
    }
    return device
}
