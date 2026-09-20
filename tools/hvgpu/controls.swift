// controls.swift: the VM controls behind the menus, toolbar, Dock menu and overlay,
// and the machine's life cycle. A windowed VM outlives its guest: when Prose powers
// off, the window stays with a Start button, as in any virtual machine app.
// Headless, timed and scripted test runs exit then, as they always have.
import AppKit
import ImageIO
import Virtualization

/// Quit when the guest powers off? Yes for headless, --seconds and --input-test runs
/// (tests and scripts rely on it); a windowed VM stays open. --exit-on-stop / --stay-on-stop override.
let exitOnStop = args.contains("--exit-on-stop")
    || (!args.contains("--stay-on-stop") && (headless || runSeconds != nil || args.contains("--input-test")))

/// The guest's MAC address. --mac auto (the default) derives a locally administered address
/// from the disk image's path, so a VM keeps its address, and bootpd its IP lease, across runs;
/// --mac random is VZ's own choice (a new one every run); or --mac aa:bb:cc:dd:ee:ff.
func makeMACAddress() -> VZMACAddress {
    let choice = option("--mac") ?? "auto"
    if choice == "random" { return .randomLocallyAdministered() }
    if choice != "auto", let mac = VZMACAddress(string: choice) { return mac }
    var hash: UInt64 = 0xcbf2_9ce4_8422_2325                   // FNV-1a
    for byte in diskURL.standardizedFileURL.path.utf8 { hash = (hash ^ UInt64(byte)) &* 0x100_0000_01b3 }
    var octets = (0..<6).map { UInt8(truncatingIfNeeded: hash >> (8 * UInt64($0))) }
    octets[0] = (octets[0] & 0xfc) | 0x02                       // unicast, locally administered
    return VZMACAddress(string: octets.map { String(format: "%02x", $0) }.joined(separator: ":"))!
}

extension Controller: NSMenuItemValidation, NSToolbarItemValidation {

    // MARK: life cycle

    func makeVM() throws {
        let config = try makeConfiguration()
        vm = VZVirtualMachine(configuration: config)
        vm.delegate = self
        stateObservation = vm.observe(\.state, options: [.new]) { [weak self] vm, _ in
            log("state \(vm.state.rawValue)")
            DispatchQueue.main.async { self?.stateChanged() }
        }
        presenter.vmView?.virtualMachine = vm
    }

    /// Start the machine: at launch, from Start, and for Restart. A stopped VZ machine
    /// that cannot start again is replaced by a new one, and so is one whose processor
    /// memory, networking or sound setting has been changed since it was built — a
    /// configuration is fixed when the machine is created, so a new setting needs a
    /// new machine.
    func bootVM() {
        let settingsChanged = cpus != Settings.cpuCount || memoryGiB != UInt64(Settings.memoryGiB)
            || networkingOn != Settings.networking || soundOn != Settings.sound
            || (appliedShares.map { $0 != hostShares() } ?? false)
        if vm == nil || !vm.canStart || settingsChanged || needNewMachine {
            needNewMachine = false
            do {
                try makeVM()
            } catch {
                log("configuration invalid: \(error.localizedDescription)")
                if exitOnStop || vm == nil { exit(1) }
                failure = error.localizedDescription
                stateChanged()
                return
            }
        }
        failure = nil
        presenter.poweredOff = false
        presenter.everHadPicture = false    // a new boot draws nothing for a while
        guestResetHandled = false           // and a new boot may ask for its own reset
        monitor?.reset()
        chrome?.forgetGuestAddress()
        log("starting \(diskURL.path) cpus=\(cpus) memory=\(memoryGiB)GiB scanout=\(width)x\(height) mac=\(macAddress.string)")
        vm.start { [self] result in
            switch result {
            case .success:
                log("started")
                runningSince = Date()
                displaySource.vmDidStart()
            case .failure(let error):
                log("start failed: \(error.localizedDescription)")
                if exitOnStop || stopping { exit(1) }
                failure = error.localizedDescription
                presenter.poweredOff = true
            }
            stateChanged()
        }
        stateChanged()
    }

    /// VZ's power button: Prose shuts down cleanly (needs app_server running).
    @discardableResult
    func requestGuestStop() -> Bool {
        guard vm.canRequestStop else { return false }
        do {
            try vm.requestStop()
            log("requested guest power-off")
            return true
        } catch {
            log("requestStop failed: \(error.localizedDescription)")
            return false
        }
    }

    /// The machine is off (guest power-off, force stop or error); the window stays.
    func vmStopped() {
        shutdownRequested = nil
        restartPending = false
        runningSince = nil
        displaySource.vmDidStop()
        presenter.poweredOff = true
        monitor?.reset()
        stateChanged()
    }

    func stateChanged() {
        inputRouter?.enabled = vm?.state == .running
        chrome?.update()
    }

    /// Status bar text and light colour.
    var stateDescription: (String, NSColor) {
        switch vm?.state {
        case .starting?: return ("Starting…", .systemOrange)
        case .running?:
            if shutdownRequested != nil { return (restartPending ? "Restarting…" : "Shutting down…", .systemOrange) }
            return ("Running", .systemGreen)
        case .pausing?: return ("Pausing…", .systemYellow)
        case .paused?: return ("Paused", .systemYellow)
        case .resuming?: return ("Resuming…", .systemYellow)
        case .stopping?: return ("Stopping…", .systemOrange)
        case .error?: return ("Stopped with an error", .systemRed)
        case .stopped?, nil: return (failure == nil ? "Shut down" : "Failed to start", failure == nil ? .tertiaryLabelColor : .systemRed)
        default: return ("", .tertiaryLabelColor)
        }
    }

    var overlayKind: StateOverlay.Kind {
        if let failure { return .failed(failure) }
        switch vm?.state {
        case .paused?: return .paused
        case .stopped?: return .stopped
        case .error?: return .failed("VZ reported an internal error.")
        default: return .none
        }
    }

    /// The guest's picture size, nil while there is none.
    var displaySize: (width: Int, height: Int)? {
        guard !presenter.poweredOff, let source = presenterGPU, let surface = source.surface,
              source.seq.withLock({ $0 }) > 0 else { return nil }
        return presentLock.withLock { surface.width > 0 && surface.height > 0 ? (surface.width, surface.height) : nil }
    }

    // MARK: actions

    @objc func startVM(_ sender: Any?) {
        guard vm == nil || vm.state == .stopped || vm.state == .error else { return }
        bootVM()
    }

    @objc func shutDown(_ sender: Any?) {
        guard shutdownRequested == nil else { return }
        switch vm.state {
        case .running:
            if requestGuestStop() { shutdownRequested = Date() }
            stateChanged()
        case .paused:
            vm.resume { [self] _ in shutDown(sender) }
        default:
            break
        }
    }

    /// The toolbar's power button: Start, Shut Down, or — once a shutdown is under way
    /// and Prose may not be answering — Force Stop.
    @objc func powerButton(_ sender: Any?) {
        switch vm.state {
        case .stopped, .error: bootVM()
        case .running, .paused: shutdownRequested == nil ? shutDown(sender) : forceStop(sender)
        default: break
        }
    }

    @objc func forceStop(_ sender: Any?) {
        confirm("Turn off the virtual machine?", button: "Force Stop") { [self] in hardStop(thenStart: false) }
    }

    @objc func forceRestart(_ sender: Any?) {
        confirm("Restart the virtual machine now?", button: "Force Restart") { [self] in hardStop(thenStart: true) }
    }

    func hardStop(thenStart: Bool) {
        guard vm.canStop else { return }
        vm.stop { [self] error in
            log("force stopped" + (error.map { ": \($0.localizedDescription)" } ?? ""))
            if exitOnStop && !thenStart { exit(0) }
            vmStopped()
            if thenStart { bootVM() }
        }
    }

    /// The guest asked for a reset (RAMConsole saw PSCI SYSTEM_RESET). Its CPUs
    /// are already stopping and nothing more will be drawn, so there is no
    /// graceful shutdown left to ask for: stop the machine and start it again.
    /// Guarded because the line is read once per boot but the log is re-read.
    func restartAfterGuestReset() {
        guard !guestResetHandled, !stopping else { return }
        guestResetHandled = true
        restartPending = true
        // A machine stopped in the middle of its own reset is not a machine to
        // start again: VZ will say it can, and then the devices that were torn
        // down half-way stay that way -- the shared memory pool unmapped, the
        // RAM console unable to find guest memory, nothing ever drawn. Build a
        // new one instead. A configuration costs nothing to make.
        needNewMachine = true
        stateChanged()
        hardStop(thenStart: true)
    }

    @objc func restart(_ sender: Any?) {
        guard shutdownRequested == nil else { return }
        switch vm.state {
        case .running:
            if requestGuestStop() {
                shutdownRequested = Date()
                restartPending = true
            }
            stateChanged()
        case .paused:
            vm.resume { [self] _ in restart(sender) }
        default:
            break
        }
    }

    @objc func togglePause(_ sender: Any?) {
        switch vm.state {
        case .running:
            inputRouter?.releaseAll()
            vm.pause { result in
                if case .failure(let error) = result { log("pause failed: \(error.localizedDescription)") }
            }
        case .paused:
            vm.resume { result in
                if case .failure(let error) = result { log("resume failed: \(error.localizedDescription)") }
            }
        default:
            break
        }
    }

    /// The overlay's button: Resume or Start.
    func overlayButton() {
        switch vm?.state {
        case .paused?: togglePause(nil)
        default: startVM(nil)
        }
    }

    private func confirm(_ message: String, button: String, _ action: @escaping () -> Void) {
        guard let window = presenter.window else { return action() }
        let alert = NSAlert()
        alert.messageText = message
        alert.informativeText = "Prose won't get to shut down: anything it hasn't saved is lost."
        alert.alertStyle = .warning
        alert.addButton(withTitle: button).hasDestructiveAction = true
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            if response == .alertFirstButtonReturn { action() }
        }
    }

    @objc func sendControlAltDelete(_ sender: Any?) {
        inputRouter?.chord([29, 56, 111])       // KEY_LEFTCTRL, KEY_LEFTALT, KEY_DELETE
    }

    @objc func sendPrintScreen(_ sender: Any?) {
        inputRouter?.tap(evdev: 99)             // KEY_SYSRQ, Prose's Print Screen
    }

    // MARK: screenshots

    @objc func takeScreenshot(_ sender: Any?) {
        guard let surface = presenterGPU?.surface, displaySize != nil else { return }
        let stamp = DateFormatter()
        stamp.dateFormat = "yyyy-MM-dd 'at' HH.mm.ss"
        let url = screenshotFolder().appendingPathComponent("Prose \(stamp.string(from: Date())).png")
        guard let image = surfaceImage(surface), writePNG(image, to: url) else {
            chrome?.content.statusBar.show(message: "Couldn't save the screenshot")
            return
        }
        log("screenshot: \(url.path)")
        chrome?.content.flash()
        chrome?.content.statusBar.show(message: "Saved “\(url.lastPathComponent)” to \(url.deletingLastPathComponent().lastPathComponent)")
    }

    /// Where the Mac's own screenshots go (Screenshot ▸ Options ▸ Save to), else the Desktop.
    private func screenshotFolder() -> URL {
        if let path = UserDefaults(suiteName: "com.apple.screencapture")?.string(forKey: "location") {
            let url = URL(fileURLWithPath: (path as NSString).expandingTildeInPath, isDirectory: true)
            if FileManager.default.fileExists(atPath: url.path) { return url }
        }
        return FileManager.default.urls(for: .desktopDirectory, in: .userDomainMask)[0]
    }

    /// The guest's picture as it is presented (copied under the presentation lock).
    func surfaceImage(_ surface: FrameSurface) -> CGImage? {
        presentLock.withLock {
            guard surface.width > 0, surface.height > 0 else { return nil }
            let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)
            return CGContext(data: surface.base + surface.offset, width: surface.width, height: surface.height,
                             bitsPerComponent: 8, bytesPerRow: surface.stride, space: CGColorSpaceCreateDeviceRGB(),
                             bitmapInfo: info.rawValue)?.makeImage()
        }
    }

    func writePNG(_ image: CGImage, to url: URL) -> Bool {
        guard let dest = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else { return false }
        CGImageDestinationAddImage(dest, image, nil)
        return CGImageDestinationFinalize(dest)
    }

    // MARK: view

    @objc func toggleStatusBar(_ sender: Any?) {
        guard let chrome else { return }
        let show = !chrome.content.statusBarShown
        UserDefaults.standard.set(show, forKey: WindowChrome.statusBarDefault)
        chrome.content.setStatusBar(shown: show, growing: chrome.window)
    }

    /// Machine ▸ Allow Automation and Testing. The guest-side channel is added to
    /// the VM's configuration only while this is on, so turning it off takes the
    /// path away at the next start rather than refusing to use it.
    @objc func toggleAutomation(_ sender: Any?) {
        let allow = !Automation.enabled
        Automation.enabled = allow
        (sender as? NSMenuItem)?.state = allow ? .on : .off
        chrome?.content.statusBar.show(message: allow
            ? "Automation and testing allowed" : "Automation and testing off")
        log("automation: \(allow ? "allowed" : "off")")
    }

    /// View ▸ Host Files in Finder: the folders shared into the machine, opened
    /// on the Mac. What the guest mounts as HostFS is one of these, or all of
    /// them as subfolders when there are several.
    @objc func revealHostFiles(_ sender: Any?) {
        // What the guest has mounted, not what has been chosen since.
        let shares = appliedShares ?? hostShares()
        guard !shares.isEmpty else { return }         // the item is disabled; belt and braces
        for share in shares {
            // the folder may not exist yet on a machine that has not started
            try? FileManager.default.createDirectory(at: share.url, withIntermediateDirectories: true)
            NSWorkspace.shared.open(share.url)
        }
    }

    @objc func showSettings(_ sender: Any?) { settingsWindow.show() }

    @objc func toggleFullScreen(_ sender: Any?) {
        presenter.window?.toggleFullScreen(sender)
    }

    private var windowed: Bool {
        guard let window = presenter.window else { return false }
        return !window.styleMask.contains(.fullScreen)
    }

    /// View ▸ Presenter: what happens to a guest pixel that covers more than one
    /// of the Mac's. Only the filtering; the size is View ▸ Scale.
    @objc func setPresenterMode(_ sender: NSMenuItem) {
        guard PresenterMode.allCases.indices.contains(sender.tag) else { return }
        let mode = PresenterMode.allCases[sender.tag]
        for item in sender.menu?.items ?? [] { item.state = item === sender ? .on : .off }
        guard mode != PresenterMode.current else { return }
        PresenterMode.current = mode
        UserDefaults.standard.set(mode.rawValue, forKey: PresenterMode.defaultsKey)
        chrome?.content.statusBar.show(message: "Presenter: \(mode.title)")
        log("presenter: \(mode.rawValue)")
    }

    /// View ▸ Scale: magnification here, with the guest's screen mode left alone.
    /// The window grows or shrinks around it; the guest is told nothing has
    /// changed, because from where it sits nothing has.
    @objc func setPresenterScale(_ sender: NSMenuItem) {
        guard PresenterScale.allCases.indices.contains(sender.tag) else { return }
        let scale = PresenterScale.allCases[sender.tag]
        for item in sender.menu?.items ?? [] { item.state = item === sender ? .on : .off }
        guard scale != PresenterScale.current, let size = displaySize else { return }
        PresenterScale.current = scale
        UserDefaults.standard.set(scale.rawValue, forKey: PresenterScale.defaultsKey)
        // keep the mode, change the window: resizeDisplay takes guest pixels and
        // converts them through the scale that is now in force
        resizeDisplay(to: NSSize(width: size.width, height: size.height))
        chrome?.content.statusBar.show(message: "Scale: \(scale.title)")
        log("scale: \(scale.title)")
    }

    /// The window back at the size it started: --size points, whatever that is
    /// in guest pixels under the current presenter mode.
    @objc func actualSize(_ sender: Any?) {
        let size = initialGuestSize()
        resizeDisplay(to: NSSize(width: size.width, height: size.height))
    }

    /// S2: the guest's screen follows the window, so a display size is a window size.
    @objc func setDisplaySize(_ sender: NSMenuItem) {
        guard displaySizePresets.indices.contains(sender.tag) else { return }
        let size = displaySizePresets[sender.tag]
        resizeDisplay(to: NSSize(width: size.width, height: size.height))
    }

    /// \a size is in guest pixels: in Native mode a point holds more than one of
    /// them, so the window is that much smaller.
    func resizeDisplay(to size: NSSize) {
        guard let chrome, windowed else { return }
        let bar = chrome.content.statusBarShown ? StatusBar.height : 0
        let window = chrome.window
        let perPoint = PresenterScale.current.guestPixelsPerPoint(window.backingScaleFactor)
        let points = NSSize(width: size.width / perPoint, height: size.height / perPoint)
        let target = window.frameRect(forContentRect: NSRect(x: 0, y: 0, width: points.width, height: points.height + bar))
        var frame = window.frame
        frame.origin.y += frame.height - target.height          // keep the top left corner
        frame.size = target.size
        window.setFrame(frame, display: true)
    }

    /// Would a guest screen of this size fit on the Mac's screen at `scale`?
    ///
    /// A screen mode is in the guest's pixels and a window is in points, and the
    /// two are only the same number when the scale happens to match the display.
    /// Comparing them directly disabled every mode above 1280x800 -- and a
    /// disabled item swallows the click, so the menu simply did nothing.
    private func fits(_ size: (width: Int, height: Int), at scale: PresenterScale? = nil) -> Bool {
        guard let window = presenter.window, let screen = window.screen ?? NSScreen.main else { return false }
        let perPoint = (scale ?? PresenterScale.current).guestPixelsPerPoint(window.backingScaleFactor)
        let bar = chrome?.content.statusBarShown == true ? StatusBar.height : 0
        let frame = window.frameRect(forContentRect:
            NSRect(x: 0, y: 0, width: CGFloat(size.width) / perPoint,
                   height: CGFloat(size.height) / perPoint + bar))
        return frame.width <= screen.visibleFrame.width && frame.height <= screen.visibleFrame.height
    }

    /// Why a screen mode is not on offer, in the item's tooltip. A greyed menu
    /// item that swallows the click and says nothing is the worst of both.
    private func whyNot(_ size: (width: Int, height: Int)) -> String {
        guard displayMode == "s2" else { return "The S1 display has a fixed size." }
        guard windowed else { return "Leave full screen to choose a screen mode." }
        let smaller = PresenterScale.allCases.first { fits(size, at: $0) }
        if let smaller {
            return "\(size.width) × \(size.height) needs Scale \(smaller.title) to fit this screen."
        }
        guard let window = presenter.window, let screen = window.screen ?? NSScreen.main else { return "" }
        let perPoint = PresenterScale.current.guestPixelsPerPoint(window.backingScaleFactor)
        return "\(size.width) × \(size.height) would need a window "
            + "\(Int(CGFloat(size.width) / perPoint)) × \(Int(CGFloat(size.height) / perPoint)) points "
            + "and this screen has \(Int(screen.visibleFrame.width)) × \(Int(screen.visibleFrame.height)). "
            + "Hiding the status bar or the toolbar buys a little."
    }

    // MARK: window delegate additions

    /// Moved to a screen of another scale: in Native mode that changes how many
    /// guest pixels the same window holds.
    func windowDidChangeBackingProperties(_ notification: Notification) {
        chrome?.content.reannounceDisplaySize()
    }

    func windowDidResignKey(_ notification: Notification) {
        // a key held when the window lost the keyboard (⌘Tab) would stay down in the guest
        inputRouter?.releaseAll()
    }

    // Full screen hides the status bar. The guest hears of the new size once the animation
    // is over, not of every size on the way.
    func windowWillEnterFullScreen(_ notification: Notification) {
        guard let chrome else { return }
        chrome.content.holdDisplaySize(true)
        chrome.statusBarBeforeFullScreen = chrome.content.statusBarShown
        chrome.content.setStatusBar(shown: false, growing: nil)
    }

    func windowDidEnterFullScreen(_ notification: Notification) {
        chrome?.content.holdDisplaySize(false)
    }

    func windowDidFailToEnterFullScreen(_ window: NSWindow) {
        guard let chrome else { return }
        chrome.content.setStatusBar(shown: chrome.statusBarBeforeFullScreen, growing: nil)
        chrome.content.holdDisplaySize(false)
    }

    func windowWillExitFullScreen(_ notification: Notification) {
        guard let chrome else { return }
        chrome.content.holdDisplaySize(true)
        chrome.content.setStatusBar(shown: chrome.statusBarBeforeFullScreen, growing: nil)
    }

    func windowDidExitFullScreen(_ notification: Notification) {
        chrome?.content.holdDisplaySize(false)
    }

    func windowDidFailToExitFullScreen(_ window: NSWindow) {
        chrome?.content.holdDisplaySize(false)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    // MARK: help

    @objc func showAbout(_ sender: Any?) {
        let info = Bundle.main.infoDictionary
        let centred = NSMutableParagraphStyle()
        centred.alignment = .center
        let credits = [
            "An operating system for people who write,",
            "running on Apple Virtualization.",
            "",
            "\(cpus) virtual CPUs · \(memoryGiB) GB memory · "
                + (displayMode == "s2" ? "Prose Display" : "virtio-gpu display"),
            diskURL.path,
        ].joined(separator: "\n")
        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationName: "Prose",
            .applicationIcon: NSApp.applicationIconImage as Any,
            .applicationVersion: info?["CFBundleShortVersionString"] as? String ?? "0.1",
            .version: "hvgpu " + (info?["CFBundleVersion"] as? String ?? "dev"),
            .credits: NSAttributedString(string: credits, attributes: [
                .font: NSFont.systemFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor,
                .paragraphStyle: centred]),
        ])
        NSApp.activate()
    }

    @objc func showKeyboardHelp(_ sender: Any?) {
        let alert = NSAlert()
        alert.messageText = "Prose has the keyboard"
        alert.informativeText = """
            While its window is active, every key goes to Prose. ⌘ is Prose's Command key and ⌥ its \
            Option key, so Prose's shortcuts are the ones you know from the Mac.

            The window's own shortcuts add Control: ⌃⌘F full screen, ⌃⌘P pause, ⌃⌘R restart, \
            ⌃⌘S screenshot, ⌃⌘T toolbar, ⌃⌘/ status bar, ⌃⌘0 actual size, ⌃⌘H hide, ⌃⌘M minimize.

            Keys a Mac keyboard doesn't have are in Machine ▸ Send Keys.
            """
        alert.icon = NSApp.applicationIconImage
        if let window = presenter.window { alert.beginSheetModal(for: window) } else { alert.runModal() }
    }

    var guestLogURL: URL { URL(fileURLWithPath: option("--ramconsole-log") ?? "ramconsole.log") }

    @objc func openGuestLog(_ sender: Any?) {
        NSWorkspace.shared.open(guestLogURL)
    }

    // MARK: validation

    func validateMenuItem(_ item: NSMenuItem) -> Bool {
        let state = vm?.state
        let live = state == .running || state == .paused
        switch item.action {
        case #selector(startVM(_:)): return state == .stopped || state == .error
        case #selector(shutDown(_:)), #selector(restart(_:)): return live && shutdownRequested == nil
        case #selector(forceStop(_:)), #selector(forceRestart(_:)): return vm?.canStop ?? false
        case #selector(togglePause(_:)):
            item.title = state == .paused ? "Resume" : "Pause"
            return live
        case #selector(takeScreenshot(_:)): return displaySize != nil
        case #selector(sendControlAltDelete(_:)), #selector(sendPrintScreen(_:)):
            return state == .running && inputRouter != nil
        case #selector(openGuestLog(_:)):
            return !args.contains("--no-ramconsole") && FileManager.default.fileExists(atPath: guestLogURL.path)
        case #selector(toggleStatusBar(_:)):
            item.title = chrome?.content.statusBarShown == true ? "Hide Status Bar" : "Show Status Bar"
            return chrome != nil
        case #selector(toggleFullScreen(_:)):
            item.title = windowed || presenter.window == nil ? "Enter Full Screen" : "Exit Full Screen"
            return presenter.window != nil
        case #selector(revealHostFiles(_:)):
            let shares = appliedShares ?? hostShares()
            item.toolTip = shares.isEmpty
                ? "No folder is shared with this machine. Choose one in Settings, or pass --share."
                : "Open " + shares.map {
                    $0.url.path.replacingOccurrences(of: NSHomeDirectory(), with: "~")
                  }.joined(separator: ", ") + " in the Finder."
            return !shares.isEmpty
        case #selector(actualSize(_:)):
            return displayMode != "s2" && windowed
        case #selector(setDisplaySize(_:)):
            let size = displaySizePresets[item.tag]
            item.state = displaySize.map { $0.width == size.width && $0.height == size.height } == true ? .on : .off
            let available = displayMode == "s2" && windowed && fits(size)
            item.toolTip = available
                ? "Set the machine's screen to \(size.width) × \(size.height) pixels."
                : whyNot(size)
            return available
        case #selector(setPresenterScale(_:)):
            guard PresenterScale.allCases.indices.contains(item.tag) else { return false }
            let scale = PresenterScale.allCases[item.tag]
            item.state = scale == PresenterScale.current ? .on : .off
            // the guest follows the window, so a scale its mode cannot be shown
            // at would shrink the mode rather than the window
            guard let size = displaySize else { return scale == PresenterScale.current }
            let available = windowed && (scale == PresenterScale.current || fits(size, at: scale))
            item.toolTip = available ? scale.detail
                : "\(size.width) × \(size.height) shown \(scale.title) would not fit this screen. "
                  + "Choose a smaller screen mode first."
            return available
        default:
            return true
        }
    }

    func validateToolbarItem(_ item: NSToolbarItem) -> Bool {
        let state = vm?.state
        let live = state == .running || state == .paused
        func show(_ label: String, _ symbol: String, _ tip: String) {
            guard item.label != label else { return }
            item.label = label
            item.toolTip = tip
            item.image = NSImage(systemSymbolName: symbol, accessibilityDescription: label)
        }
        switch item.itemIdentifier {
        case .power:
            if state == .stopped || state == .error {
                show("Start", "play.fill", "Start Prose")
                return true
            }
            if shutdownRequested != nil {
                show("Force Stop", "stop.fill", "Prose hasn't shut down yet: turn the virtual machine off")
                return true
            }
            show("Shut Down", "power", "Shut down Prose, as if you pressed its power button")
            return live
        case .pause:
            if state == .paused {
                show("Resume", "play.fill", "Resume the virtual machine")
            } else {
                show("Pause", "pause.fill", "Pause the virtual machine")
            }
            return live
        case .restart: return live && shutdownRequested == nil
        case .forceStop: return vm?.canStop ?? false
        case .screenshot: return displaySize != nil
        case .actualSize: return displayMode != "s2" && windowed
        default: return true
        }
    }

    // MARK: --script: drive the controls unattended (tests)

    /// "T:step,T:step..." with T in seconds from launch. Steps: pause resume shutdown start
    /// restart force-stop force-restart screenshot statusbar toolbar fullscreen keys-cad
    /// keys-print stats quit size=WxH snapshot=PATH (the window, chrome included, as PNG).
    func runScript(_ script: String) {
        for step in script.split(separator: ",") {
            let parts = step.split(separator: ":", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
            guard parts.count == 2, let at = Double(parts[0]) else {
                log("script: can't read step \"\(step)\"")
                continue
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + at) { [self] in scriptStep(parts[1]) }
        }
    }

    private func scriptStep(_ step: String) {
        log("script: \(step) (state \(vm.state.rawValue))")
        // Anything the core knows is a script step: one implementation, and the
        // step now says what happened instead of disappearing (docs/automation.md).
        if let command = Automation.command(for: step) {
            automation.perform(command) { result in log("script: \(step) -> \(result.line)") }
            return
        }
        let parts = step.split(separator: "=", maxSplits: 1).map(String.init)
        let value = parts.count > 1 ? parts[1] : ""
        switch parts[0] {
        case "pause": if vm.state == .running { togglePause(nil) }
        case "resume": if vm.state == .paused { togglePause(nil) }
        case "shutdown": shutDown(nil)
        case "start": startVM(nil)
        case "restart": restart(nil)
        case "force-stop": hardStop(thenStart: false)
        case "force-restart": hardStop(thenStart: true)
        case "screenshot": takeScreenshot(nil)
        case "statusbar": toggleStatusBar(nil)
        case "toolbar": presenter.window?.toggleToolbarShown(nil)
        case "fullscreen": toggleFullScreen(nil)
        case "keys-cad": sendControlAltDelete(nil)
        case "keys-print": sendPrintScreen(nil)
        case "stats": log(statsLine())
        case "quit": NSApp.terminate(nil)
        case "size":
            let wh = value.split(separator: "x").compactMap { Double($0) }
            if wh.count == 2 { resizeDisplay(to: NSSize(width: wh[0], height: wh[1])) }
        case "snapshot": snapshotWindow(to: value)
        default: log("script: unknown step \"\(step)\"")
        }
    }

    /// One line with what the status bar knows (headless runs have no status bar to look at).
    func statsLine() -> String {
        guard let monitor else { return "monitor: off" }
        monitor.locate()
        guard let c = monitor.sample() else { return "monitor: VM process not found" }
        let audio = monitor.audio()
        return "monitor: pid \(monitor.pid) \(monitor.interface ?? "no interface"), "
            + "cpu \(c.cpuNanos / 1_000_000) ms, disk read \(formatBytes(c.diskRead)) written \(formatBytes(c.diskWritten)), "
            + "net received \(formatBytes(c.netReceived)) sent \(formatBytes(c.netSent)), "
            + "ip \(monitor.guestIP() ?? "none"), audio out \(audio.output) in \(audio.input), "
            + "midi \(midi.activity.withLock { $0 }), display \(displaySize.map { "\($0.width)x\($0.height)" } ?? "none")"
    }

    /// The whole window as the user sees it, for unattended checks: the layer tree renders
    /// the chrome; the guest's picture is painted into the display area (a Metal layer
    /// doesn't render outside the compositor).
    func snapshotWindow(to path: String) {
        guard let window = presenter.window, let frameView = window.contentView?.superview,
              let root = frameView.layer else { return }
        let scale = window.backingScaleFactor
        let size = frameView.bounds.size
        guard let ctx = CGContext(data: nil, width: Int(size.width * scale), height: Int(size.height * scale),
                                  bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue) else { return }
        ctx.scaleBy(x: scale, y: scale)
        if root.isGeometryFlipped || frameView.isFlipped {
            ctx.translateBy(x: 0, y: size.height)
            ctx.scaleBy(x: 1, y: -1)
        }
        root.render(in: ctx)
        if let surface = presenterGPU?.surface, displaySize != nil, let image = surfaceImage(surface) {
            let r = presenter.view.convert(presenter.imageRect(), to: nil)      // window coordinates, y up
            ctx.saveGState()
            if root.isGeometryFlipped || frameView.isFlipped {                  // undo the flip for drawing
                ctx.translateBy(x: 0, y: size.height)
                ctx.scaleBy(x: 1, y: -1)
            }
            ctx.draw(image, in: r)
            // the overlay (paused, shut down) lies over the picture
            if let overlay = presenter.content?.overlay, !overlay.isHidden, let layer = overlay.layer {
                let o = overlay.convert(overlay.bounds, to: nil)
                ctx.translateBy(x: o.minX, y: o.minY)
                layer.render(in: ctx)
            }
            ctx.restoreGState()
        }
        let buttons = window.toolbar.map { toolbar in
            toolbar.isVisible ? toolbar.items.map { $0.label + ($0.isEnabled ? "" : " (off)") }.joined(separator: ", ") : "hidden"
        } ?? "none"
        if let image = ctx.makeImage(), writePNG(image, to: URL(fileURLWithPath: path)) {
            log("snapshot: \(path); display \(Int(presenter.view.bounds.width))x\(Int(presenter.view.bounds.height)); toolbar: \(buttons)")
        }
    }
}
