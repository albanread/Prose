// vzprobe: boot a disk in a Virtualization.framework generic-platform VM that
// carries a broad set of devices, so guest firmware (e.g. the EDK2 UEFI Shell)
// can report the platform VZ presents to a non-macOS arm64 guest. Headless.
//
// usage: vzprobe <boot.img> <efi-vars> <serial.log> [options]
//   --nvme <img>        also attach <img> as an NVMe controller (macOS 14+)
//   --nested            enable nested virtualization (macOS 15+, M3+)
//   --pl011             use the private PL011 UART instead of virtio-console
//   --linear-fb WxH     add the private linear framebuffer device
//   --no-virtio-gpu     omit the virtio-gpu device
//   --list-private      list the private _VZ*Configuration classes
//   --check-only        validate the configuration and exit
//   --timeout <sec>     give up after <sec> seconds (default 120)
// Exit status: 0 guest powered off, 1 error/start failure, 2 timeout.
import Foundation
import ObjectiveC
import Virtualization

setvbuf(stdout, nil, _IONBF, 0)

let args = CommandLine.arguments
guard args.count >= 4 else {
    print("usage: vzprobe <boot.img> <efi-vars> <serial.log> [--nvme img] [--nested] [--pl011] "
        + "[--linear-fb WxH] [--no-virtio-gpu] [--list-private] [--check-only] [--timeout sec]")
    exit(64)
}
func option(_ name: String) -> String? {
    // The last occurrence wins, so wrappers can put defaults first.
    guard let i = args.lastIndex(of: name), i + 1 < args.count else { return nil }
    return args[i + 1]
}
let bootURL = URL(fileURLWithPath: args[1])
let varsURL = URL(fileURLWithPath: args[2])
let serialPath = args[3]
let timeout = Double(option("--timeout") ?? "120") ?? 120

// --- Host capability report -------------------------------------------------
print("VZ supported on host: \(VZVirtualMachine.isSupported)")
print("max vCPUs: \(VZVirtualMachineConfiguration.maximumAllowedCPUCount), "
    + "max memory: \(VZVirtualMachineConfiguration.maximumAllowedMemorySize >> 30) GiB")
if #available(macOS 15.0, *) {
    print("nested virtualization supported: \(VZGenericPlatformConfiguration.isNestedVirtualizationSupported)")
}
// macOS 27 custom-virtio API, looked up at runtime (works with an older SDK).
let customVirtioClasses = ["VZCustomVirtioDeviceConfiguration", "VZCustomVirtioDevice",
                           "VZVirtioSharedMemoryRegionConfiguration", "VZGuestMemoryMapping"]
let presentClasses = customVirtioClasses.filter { NSClassFromString($0) != nil }
print("macOS 27 custom virtio API: \(presentClasses.count == customVirtioClasses.count ? "present" : "absent")"
    + " (\(presentClasses.count)/\(customVirtioClasses.count) classes)")
if args.contains("--list-private") {
    var count: UInt32 = 0
    let image = "/System/Library/Frameworks/Virtualization.framework/Versions/A/Virtualization"
    if let names = objc_copyClassNamesForImage(image, &count) {
        let priv = (0..<Int(count)).map { String(cString: names[$0]) }
            .filter { $0.hasPrefix("_VZ") && $0.hasSuffix("Configuration") }
            .sorted()
        print("private _VZ*Configuration classes (\(priv.count)):")
        for n in priv { print("  \(n)") }
        free(names)
    }
}

// Instantiate a (private) Objective-C class via alloc/init.
func instantiate(_ className: String) -> NSObject? {
    guard let cls = NSClassFromString(className),
          let allocated = (cls as AnyObject).perform(NSSelectorFromString("alloc")) else { return nil }
    return allocated.takeUnretainedValue().perform(NSSelectorFromString("init"))?.takeRetainedValue() as? NSObject
}

// --- VM configuration -------------------------------------------------------
func makeConfig() throws -> VZVirtualMachineConfiguration {
    let config = VZVirtualMachineConfiguration()
    config.cpuCount = 2
    config.memorySize = 2 << 30

    let platform = VZGenericPlatformConfiguration()
    if args.contains("--nested"), #available(macOS 15.0, *) {
        platform.isNestedVirtualizationEnabled = true
    }
    config.platform = platform

    let loader = VZEFIBootLoader()
    if FileManager.default.fileExists(atPath: varsURL.path) {
        loader.variableStore = VZEFIVariableStore(url: varsURL)
    } else {
        loader.variableStore = try VZEFIVariableStore(creatingVariableStoreAt: varsURL)
    }
    config.bootLoader = loader

    let boot = try VZDiskImageStorageDeviceAttachment(
        url: bootURL, readOnly: false, cachingMode: .automatic, synchronizationMode: .full)
    var storage: [VZStorageDeviceConfiguration] = [VZVirtioBlockDeviceConfiguration(attachment: boot)]
    if let nvme = option("--nvme"), #available(macOS 14.0, *) {
        let att = try VZDiskImageStorageDeviceAttachment(url: URL(fileURLWithPath: nvme), readOnly: false)
        storage.append(VZNVMExpressControllerDeviceConfiguration(attachment: att))
    }
    config.storageDevices = storage

    FileManager.default.createFile(atPath: serialPath, contents: nil)
    let serial: VZSerialPortConfiguration
    if args.contains("--pl011"),
       let pl011 = instantiate("_VZPL011SerialPortConfiguration") as? VZSerialPortConfiguration {
        serial = pl011
    } else {
        serial = VZVirtioConsoleDeviceSerialPortConfiguration()
    }
    serial.attachment = VZFileHandleSerialPortAttachment(
        fileHandleForReading: nil, fileHandleForWriting: FileHandle(forWritingAtPath: serialPath)!)
    config.serialPorts = [serial]

    let net = VZVirtioNetworkDeviceConfiguration()
    net.attachment = VZNATNetworkDeviceAttachment()
    config.networkDevices = [net]

    var graphics: [VZGraphicsDeviceConfiguration] = []
    if !args.contains("--no-virtio-gpu") {
        let gfx = VZVirtioGraphicsDeviceConfiguration()
        gfx.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels: 1280, heightInPixels: 800)]
        graphics.append(gfx)
    }
    if let size = option("--linear-fb"),
       let fb = instantiate("_VZLinearFramebufferGraphicsDeviceConfiguration") as? VZGraphicsDeviceConfiguration {
        let wh = size.split(separator: "x").compactMap { Double($0) }
        fb.setValue(NSValue(size: NSSize(width: wh[0], height: wh[1])), forKey: "backingStoreSize")
        graphics.append(fb)
    }
    config.graphicsDevices = graphics

    config.keyboards = [VZUSBKeyboardConfiguration()]
    config.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
    config.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
    config.memoryBalloonDevices = [VZVirtioTraditionalMemoryBalloonDeviceConfiguration()]
    config.socketDevices = [VZVirtioSocketDeviceConfiguration()]

    let share = VZVirtioFileSystemDeviceConfiguration(tag: "hostshare")
    share.share = VZSingleDirectoryShare(
        directory: VZSharedDirectory(url: URL(fileURLWithPath: NSTemporaryDirectory()), readOnly: true))
    config.directorySharingDevices = [share]

    let sound = VZVirtioSoundDeviceConfiguration()
    let out = VZVirtioSoundDeviceOutputStreamConfiguration()
    out.sink = VZHostAudioOutputStreamSink()
    sound.streams = [out]
    config.audioDevices = [sound]
    return config
}

// Can the Metal-backed paravirtualized GPU be given to a non-macOS guest?
do {
    let c = try makeConfig()
    let mac = VZMacGraphicsDeviceConfiguration()
    mac.displays = [VZMacGraphicsDisplayConfiguration(widthInPixels: 1280, heightInPixels: 800, pixelsPerInch: 80)]
    c.graphicsDevices = [mac]
    try c.validate()
    print("VZMacGraphicsDevice on generic platform: ACCEPTED")
} catch {
    print("VZMacGraphicsDevice on generic platform: rejected (\(error.localizedDescription))")
}

let config: VZVirtualMachineConfiguration
do {
    config = try makeConfig()
    try config.validate()
    print("configuration valid")
} catch {
    print("configuration invalid: \(error)")
    exit(1)
}
if args.contains("--check-only") { exit(0) }

// --- Run ----------------------------------------------------------------------
final class Delegate: NSObject, VZVirtualMachineDelegate {
    func guestDidStop(_ virtualMachine: VZVirtualMachine) {
        print("guest stopped (powered off)")
        exit(0)
    }
    func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: Error) {
        print("VM stopped with error: \(error)")
        exit(1)
    }
}

let vm = VZVirtualMachine(configuration: config)
let delegate = Delegate()
vm.delegate = delegate
vm.start { result in
    switch result {
    case .success: print("VM started")
    case .failure(let error):
        print("start failed: \(error)")
        exit(1)
    }
}
DispatchQueue.main.asyncAfter(deadline: .now() + timeout) {
    print("timeout after \(Int(timeout)) s; state=\(vm.state.rawValue)")
    exit(2)
}
dispatchMain()
