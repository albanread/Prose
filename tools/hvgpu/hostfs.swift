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
// With no --share at all, an installed copy shares the folder chosen in
// Settings, which starts as ~/Documents/HostFS.
//
// One share: the volume is that directory. Several: the volume holds one folder per
// share, named after the directory (VZMultipleDirectoryShare).
import Foundation
import Virtualization

struct HostShare: Equatable {
    let url: URL
    let readOnly: Bool
}

/// What the machine in memory was actually given. Settings can be changed under a
/// running guest; the menu that opens the shared folder in the Finder should open
/// the folder the guest has mounted, not the one chosen since.
var appliedShares: [HostShare]?

/// What the machine should be given: the command line if it said anything, else
/// the window's choice. Pure — the caller creates the directory and logs.
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
    if !shares.isEmpty { return shares }

    // An installed copy shares a folder without being asked to: a machine with no
    // way to exchange a file with the Mac it runs on is much less useful, and there
    // is no command line to put a --share on. A script that passes a disk and no
    // --share still gets none, so what a test sees does not depend on what somebody
    // last chose in a window.
    guard usingInstalledMachine, Settings.shareEnabled else { return [] }
    return [HostShare(url: Settings.shareFolder, readOnly: Settings.shareReadOnly)]
}

func makeHostFSDevice() throws -> VZVirtioFileSystemDeviceConfiguration? {
    let shares = hostShares()
    appliedShares = shares
    guard !shares.isEmpty else { return nil }
    let tag = option("--share-tag") ?? "HostFS"
    try VZVirtioFileSystemDeviceConfiguration.validateTag(tag)
    for share in shares {
        // A folder chosen in the window, or the default one, may not exist yet.
        try? FileManager.default.createDirectory(at: share.url, withIntermediateDirectories: true)
        log("hostfs: sharing \(share.url.path)\(share.readOnly ? " read-only" : "")")
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: share.url.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            throw NSError(domain: "hvgpu", code: 2, userInfo: [
                NSLocalizedDescriptionKey: "shared folder is not a directory: \(share.url.path)"])
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
