// hvz: run a Haiku arm64 disk image in an Apple Virtualization.framework VM,
// shown in a window (VZVirtualMachineView). Sprint 0 uses VZ's built-in 2D
// virtio-gpu; later sprints add the custom display device (macOS 27).
//
// usage: hvz <disk.img> [options]
//   --efivars <path>   EFI variable store, created if missing (default <disk>.efivars)
//   --cpus <n>         vCPUs (default 4)
//   --memory <GiB>     RAM (default 4)
//   --size WxH         virtio-gpu scanout and window size (default 1280x800)
//   --seconds <n>      request a stop after n seconds (default: run until closed)
//   --grace <n>        seconds to wait for a guest power-off before forcing (default 8)
//   --serial <path>    virtio-console output file (Haiku has no driver for it yet)
//   --nested           enable nested virtualization (M3+ / macOS 15+)
//   --no-net           omit the network device
//   --headless         no window
// Prints "HVZ <elapsed-seconds> <event>" lines for scripts.
// Exit status: 0 stopped normally, 1 error.
import AppKit
import Virtualization

setvbuf(stdout, nil, _IONBF, 0)

let args = CommandLine.arguments
func option(_ name: String) -> String? {
    guard let i = args.firstIndex(of: name), i + 1 < args.count else { return nil }
    return args[i + 1]
}
guard args.count >= 2, !args[1].hasPrefix("--") else {
    print("usage: hvz <disk.img> [--efivars path] [--cpus n] [--memory GiB] [--size WxH] "
        + "[--seconds n] [--grace n] [--serial path] [--nested] [--no-net] [--headless]")
    exit(64)
}

let diskURL = URL(fileURLWithPath: args[1])
let varsURL = URL(fileURLWithPath: option("--efivars") ?? args[1] + ".efivars")
let cpus = Int(option("--cpus") ?? "4") ?? 4
let memoryGiB = UInt64(option("--memory") ?? "4") ?? 4
let size = (option("--size") ?? "1280x800").split(separator: "x").compactMap { Int($0) }
let (width, height) = (size.count == 2 ? size[0] : 1280, size.count == 2 ? size[1] : 800)
let runSeconds = option("--seconds").flatMap(Double.init)
let grace = Double(option("--grace") ?? "8") ?? 8
let headless = args.contains("--headless")

let startTime = Date()
func log(_ event: String) {
    print(String(format: "HVZ %7.2f ", Date().timeIntervalSince(startTime)) + event)
}

func stateName(_ s: VZVirtualMachine.State) -> String {
    switch s {
    case .stopped: return "stopped"
    case .running: return "running"
    case .paused: return "paused"
    case .error: return "error"
    case .starting: return "starting"
    case .pausing: return "pausing"
    case .resuming: return "resuming"
    case .stopping: return "stopping"
    case .saving: return "saving"
    case .restoring: return "restoring"
    @unknown default: return "state-\(s.rawValue)"
    }
}

func makeConfiguration() throws -> VZVirtualMachineConfiguration {
    let config = VZVirtualMachineConfiguration()
    config.cpuCount = cpus
    config.memorySize = memoryGiB << 30

    let platform = VZGenericPlatformConfiguration()
    if args.contains("--nested"), #available(macOS 15.0, *) {
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
    config.storageDevices = [VZVirtioBlockDeviceConfiguration(attachment: disk)]

    if !args.contains("--no-net") {
        let net = VZVirtioNetworkDeviceConfiguration()
        net.attachment = VZNATNetworkDeviceAttachment()
        config.networkDevices = [net]
    }

    let gpu = VZVirtioGraphicsDeviceConfiguration()
    gpu.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels: width, heightInPixels: height)]
    config.graphicsDevices = [gpu]

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

final class Controller: NSObject, NSApplicationDelegate, NSWindowDelegate, VZVirtualMachineDelegate {
    var vm: VZVirtualMachine!
    var window: NSWindow?
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
            log("state \(stateName(vm.state))")
        }

        if !headless {
            let view = VZVirtualMachineView()
            view.virtualMachine = vm
            view.capturesSystemKeys = true
            let window = NSWindow(
                contentRect: NSRect(x: 0, y: 0, width: width, height: height),
                styleMask: [.titled, .closable, .miniaturizable, .resizable],
                backing: .buffered, defer: false)
            window.title = "Haiku (VZ) — \(diskURL.lastPathComponent)"
            window.contentView = view
            window.delegate = self
            window.center()
            window.makeKeyAndOrderFront(nil)
            window.makeFirstResponder(view)
            self.window = window
            NSApp.activate(ignoringOtherApps: true)
        }

        log("starting \(diskURL.path) cpus=\(cpus) memory=\(memoryGiB)GiB scanout=\(width)x\(height)")
        vm.start { result in
            switch result {
            case .success:
                log("started")
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
appMenu.addItem(withTitle: "Quit hvz", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
appItem.submenu = appMenu
app.mainMenu = mainMenu
let controller = Controller()
app.delegate = controller
app.run()
