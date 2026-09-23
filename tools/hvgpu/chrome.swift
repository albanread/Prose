// chrome.swift: the Prose window around the guest display — a unified toolbar with
// the VM controls, a status bar with activity lights, and an overlay that says
// when the machine is paused or off. All of it is optional (View menu), and none
// of it changes the size of the guest's display: showing or hiding a bar resizes
// the window instead.
import AppKit
import QuartzCore
import Virtualization

// MARK: - The window

/// On macOS 27, showing or hiding a unified toolbar moves the window's frame by the whole
/// title bar height (52 points) while the title bar itself changes by 20 (measured): the
/// content area, and with it the guest's screen, would grow or shrink by 32 points. This
/// window keeps its content size instead, top edge fixed, and tells the guest nothing.
final class ProseWindow: NSWindow {
    override func toggleToolbarShown(_ sender: Any?) {
        keepingContentSize { super.toggleToolbarShown(sender) }
    }

    func keepingContentSize(_ change: () -> Void) {
        let content = contentView as? VMContentView
        let size = contentView?.frame.size ?? .zero
        let top = frame.maxY
        content?.holdDisplaySize(true)
        change()
        if !styleMask.contains(.fullScreen), contentView?.frame.size != size {
            var restored = frameRect(forContentRect: NSRect(origin: .zero, size: size))
            restored.origin = NSPoint(x: frame.minX, y: top - restored.height)
            setFrame(restored, display: true)
        }
        content?.holdDisplaySize(false)
    }
}

// MARK: - Content view: guest display above the status bar

final class VMContentView: NSView {
    let displayViews: [NSView]          // the Metal view (and VZ's own view with --input vz)
    let statusBar = StatusBar()
    let overlay = StateOverlay()
    /// Called whenever the display area changes size (window resize, bars, full screen).
    var onDisplayResize: ((NSSize) -> Void)?
    private var lastDisplaySize = NSSize.zero
    private var holds = 0

    /// While held, layout goes on but the guest isn't told: the size in between is not
    /// one it should switch to. The last release tells it, if the size really changed.
    func holdDisplaySize(_ hold: Bool) {
        holds = max(0, holds + (hold ? 1 : -1))
        if holds == 0 { layoutChildren() }
    }

    private(set) var statusBarShown: Bool

    /// Show or hide the status bar. With a window, the window grows or shrinks by the bar
    /// (top edge fixed) so the guest's display keeps its size; without one (full screen)
    /// the display gives up or takes the room.
    func setStatusBar(shown: Bool, growing window: NSWindow?) {
        guard shown != statusBarShown else { return }
        statusBarShown = shown
        statusBar.isHidden = !shown
        if let window, !window.styleMask.contains(.fullScreen) {
            var frame = window.frame
            let delta = shown ? StatusBar.height : -StatusBar.height
            frame.size.height += delta
            frame.origin.y -= delta
            window.setFrame(frame, display: true)       // lays out with the new frame
        }
        layoutChildren()
    }

    init(displaySize: NSSize, displayViews: [NSView], statusBarShown: Bool) {
        self.displayViews = displayViews
        self.statusBarShown = statusBarShown
        let bar = statusBarShown ? StatusBar.height : 0
        super.init(frame: NSRect(x: 0, y: 0, width: displaySize.width, height: displaySize.height + bar))
        wantsLayer = true
        for view in displayViews {
            view.autoresizingMask = []
            addSubview(view)
        }
        addSubview(overlay)
        addSubview(statusBar)
        statusBar.isHidden = !statusBarShown
        layoutChildren()
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    var displayFrame: NSRect {
        let bar = statusBarShown ? StatusBar.height : 0
        return NSRect(x: 0, y: bar, width: bounds.width, height: max(0, bounds.height - bar))
    }

    func layoutChildren() {
        let frame = displayFrame
        for view in displayViews { view.frame = frame }
        overlay.frame = frame
        statusBar.frame = NSRect(x: 0, y: 0, width: bounds.width, height: StatusBar.height)
        if holds == 0, frame.size != lastDisplaySize {
            lastDisplaySize = frame.size
            onDisplayResize?(frame.size)
        }
    }

    override func resizeSubviews(withOldSize oldSize: NSSize) {
        layoutChildren()
    }

    /// Say the display size again although it has not changed: what it means to
    /// the guest has (the presenter mode, or a move to a screen of another scale).
    func reannounceDisplaySize() {
        lastDisplaySize = .zero
        layoutChildren()
    }

    /// A camera flash over the display (screenshots).
    func flash() {
        let white = NSView(frame: displayFrame)
        white.wantsLayer = true
        white.layer?.backgroundColor = NSColor.white.cgColor
        white.alphaValue = 0.75
        addSubview(white, positioned: .below, relativeTo: statusBar)
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.4
            white.animator().alphaValue = 0
        }, completionHandler: { white.removeFromSuperview() })
    }
}

// MARK: - Activity lights

/// A small round light, like a drive LED: flash() lights it and lets it fade.
final class LEDView: NSView {
    var color: NSColor {
        didSet { needsDisplay = true }
    }
    let diameter: CGFloat
    private var lit = false

    init(color: NSColor, diameter: CGFloat = 7) {
        self.color = color
        self.diameter = diameter
        super.init(frame: NSRect(x: 0, y: 0, width: diameter, height: diameter))
        wantsLayer = true
        setContentHuggingPriority(.required, for: .horizontal)
        setContentHuggingPriority(.required, for: .vertical)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override var intrinsicContentSize: NSSize { NSSize(width: diameter, height: diameter) }
    override var wantsUpdateLayer: Bool { true }

    private func cg(_ c: NSColor) -> CGColor {
        var result = c.cgColor
        effectiveAppearance.performAsCurrentDrawingAppearance { result = c.cgColor }
        return result
    }

    override func updateLayer() {
        guard let layer else { return }
        layer.cornerRadius = diameter / 2
        layer.backgroundColor = cg(lit ? color : .quaternaryLabelColor)
        layer.borderWidth = 0.5
        layer.borderColor = cg(NSColor.black.withAlphaComponent(0.12))
    }

    /// Light up and fade out (activity).
    func flash() {
        guard let layer, !lit else { return }
        let fade = CABasicAnimation(keyPath: "backgroundColor")
        fade.fromValue = cg(color)
        fade.toValue = cg(.quaternaryLabelColor)
        fade.duration = 0.35
        fade.timingFunction = CAMediaTimingFunction(name: .easeIn)
        layer.add(fade, forKey: "flash")
    }

    /// Stay lit or dark (state, such as sound playing).
    func setLit(_ on: Bool) {
        guard on != lit else { return }
        lit = on
        needsDisplay = true
    }
}

// MARK: - Status bar

/// One indicator: a symbol, an optional value, and activity lights.
final class StatusItem: NSStackView {
    let icon = NSImageView()
    let label = StatusBar.label(monospaced: true)
    let lights: [LEDView]

    init(symbol: String, name: String, lights colors: [NSColor] = [], showsLabel: Bool = true) {
        lights = colors.map { LEDView(color: $0) }
        super.init(frame: .zero)
        orientation = .horizontal
        alignment = .centerY
        spacing = 4
        icon.image = NSImage(systemSymbolName: symbol, accessibilityDescription: name)
        icon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 11, weight: .regular)
        icon.contentTintColor = .secondaryLabelColor
        addArrangedSubview(icon)
        if showsLabel { addArrangedSubview(label) }
        if !lights.isEmpty {
            let group = NSStackView(views: lights)
            group.spacing = 2
            addArrangedSubview(group)
        }
        setAccessibilityElement(true)
        setAccessibilityLabel(name)
    }

    required init?(coder: NSCoder) { fatalError("not used") }
}

final class StatusBar: NSView {
    static let height: CGFloat = 24

    let stateLight = LEDView(color: .systemGreen, diameter: 8)
    let stateLabel = StatusBar.label()
    let uptimeLabel = StatusBar.label(monospaced: true)
    let display = StatusItem(symbol: "display", name: "Display")
    let message = StatusBar.label()
    let cpu = StatusItem(symbol: "cpu", name: "Processor")
    let disk = StatusItem(symbol: "internaldrive", name: "Disk", lights: [.systemGreen, .systemOrange], showsLabel: false)
    let network = StatusItem(symbol: "network", name: "Network", lights: [.systemGreen, .systemOrange])
    let sound = StatusItem(symbol: "speaker.wave.2", name: "Sound", lights: [.systemGreen], showsLabel: false)
    let microphone = StatusItem(symbol: "mic.fill", name: "Microphone", showsLabel: false)
    let midi = StatusItem(symbol: "pianokeys", name: "MIDI", lights: [.systemBlue], showsLabel: false)
    let chip = StatusItem(symbol: "waveform", name: "Chip", lights: [.systemGreen], showsLabel: false)
    private var messageExpiry: DispatchWorkItem?

    static func label(monospaced: Bool = false) -> NSTextField {
        let label = NSTextField(labelWithString: "")
        label.font = monospaced ? .monospacedDigitSystemFont(ofSize: 11, weight: .regular) : .systemFont(ofSize: 11)
        label.textColor = .secondaryLabelColor
        label.lineBreakMode = .byTruncatingTail
        return label
    }

    init() {
        super.init(frame: NSRect(x: 0, y: 0, width: 600, height: StatusBar.height))
        let state = NSStackView(views: [stateLight, stateLabel, uptimeLabel])
        state.spacing = 5
        let left = NSStackView(views: [state, display, message])
        left.spacing = 16
        message.setContentCompressionResistancePriority(.defaultLow - 1, for: .horizontal)
        microphone.icon.contentTintColor = .systemOrange
        microphone.isHidden = true
        let right = NSStackView(views: [cpu, disk, network, sound, microphone, midi, chip])
        right.spacing = 14
        for stack in [left, right] {
            stack.orientation = .horizontal
            stack.alignment = .centerY
            stack.translatesAutoresizingMaskIntoConstraints = false
            addSubview(stack)
        }
        NSLayoutConstraint.activate([
            left.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            left.centerYAnchor.constraint(equalTo: centerYAnchor),
            right.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            right.centerYAnchor.constraint(equalTo: centerYAnchor),
            left.trailingAnchor.constraint(lessThanOrEqualTo: right.leadingAnchor, constant: -12),
        ])
        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        setAccessibilityLabel("Status bar")
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.windowBackgroundColor.setFill()
        bounds.fill()
        NSColor.separatorColor.setFill()
        NSRect(x: 0, y: bounds.height - 1, width: bounds.width, height: 1).fill()
    }

    /// A passing remark ("Screenshot saved…") for a few seconds.
    func show(message text: String) {
        message.stringValue = text
        messageExpiry?.cancel()
        let expiry = DispatchWorkItem { [weak self] in self?.message.stringValue = "" }
        messageExpiry = expiry
        DispatchQueue.main.asyncAfter(deadline: .now() + 5, execute: expiry)
    }
}

// MARK: - Overlay: paused, shut down, failed

final class StateOverlay: NSView {
    enum Kind: Equatable { case none, paused, stopped, failed(String) }

    var kind: Kind = .none {
        didSet { if kind != oldValue { apply() } }
    }
    var action: (() -> Void)?
    private let icon = NSImageView()
    private let title = NSTextField(labelWithString: "")
    private let detail = NSTextField(wrappingLabelWithString: "")
    private let button = NSButton(title: "", target: nil, action: nil)

    init() {
        super.init(frame: .zero)
        wantsLayer = true
        appearance = NSAppearance(named: .darkAqua)
        icon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 48, weight: .light)
        icon.contentTintColor = NSColor.white.withAlphaComponent(0.85)
        title.font = .systemFont(ofSize: 20, weight: .semibold)
        title.textColor = .white
        detail.font = .systemFont(ofSize: 12)
        detail.textColor = NSColor.white.withAlphaComponent(0.7)
        detail.alignment = .center
        detail.preferredMaxLayoutWidth = 420
        button.bezelStyle = .push
        button.controlSize = .large
        button.target = self
        button.action = #selector(pressed)
        let stack = NSStackView(views: [icon, title, detail, button])
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 10
        stack.setCustomSpacing(20, after: detail)
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(equalTo: centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: centerYAnchor),
            stack.widthAnchor.constraint(lessThanOrEqualTo: widthAnchor, constant: -40),
        ])
        isHidden = true
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    private func apply() {
        func show(_ symbol: String, _ heading: String, _ text: String, _ label: String, dim: CGFloat) {
            icon.image = NSImage(systemSymbolName: symbol, accessibilityDescription: heading)
            title.stringValue = heading
            detail.stringValue = text
            detail.isHidden = text.isEmpty
            button.title = label
            layer?.backgroundColor = NSColor.black.withAlphaComponent(dim).cgColor
            isHidden = false
        }
        switch kind {
        case .none: isHidden = true
        case .paused: show("pause.circle", "Paused", "", "Resume", dim: 0.55)
        case .stopped: show("power", "Prose is shut down", "", "Start", dim: 0.9)
        case .failed(let why): show("exclamationmark.triangle", "The virtual machine stopped", why, "Start", dim: 0.9)
        }
    }

    @objc private func pressed() { action?() }

    // Everything under the overlay is out of reach while it shows.
    override func hitTest(_ point: NSPoint) -> NSView? { isHidden ? nil : super.hitTest(point) }
    override func mouseDown(with event: NSEvent) {}
}

// MARK: - Toolbar

extension NSToolbarItem.Identifier {
    static let power = NSToolbarItem.Identifier("prose.power")
    static let pause = NSToolbarItem.Identifier("prose.pause")
    static let restart = NSToolbarItem.Identifier("prose.restart")
    static let forceStop = NSToolbarItem.Identifier("prose.force-stop")
    static let sendKeys = NSToolbarItem.Identifier("prose.send-keys")
    static let screenshot = NSToolbarItem.Identifier("prose.screenshot")
    static let fullScreen = NSToolbarItem.Identifier("prose.full-screen")
    static let statusBar = NSToolbarItem.Identifier("prose.status-bar")
    static let actualSize = NSToolbarItem.Identifier("prose.actual-size")
}

final class ToolbarController: NSObject, NSToolbarDelegate {
    unowned let controller: Controller

    init(controller: Controller) {
        self.controller = controller
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.power, .pause, .restart, .flexibleSpace, .sendKeys, .screenshot, .fullScreen]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.power, .pause, .restart, .forceStop, .sendKeys, .screenshot, .fullScreen, .statusBar, .actualSize,
         .space, .flexibleSpace]
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier id: NSToolbarItem.Identifier,
                 willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        switch id {
        case .power:
            return item(id, "Shut Down", palette: "Start / Shut Down", symbol: "power",
                        tip: "Shut down Prose, as if you pressed its power button", #selector(Controller.powerButton(_:)))
        case .pause:
            return item(id, "Pause", palette: "Pause / Resume", symbol: "pause.fill",
                        tip: "Pause the virtual machine", #selector(Controller.togglePause(_:)))
        case .restart:
            return item(id, "Restart", palette: "Restart", symbol: "arrow.clockwise",
                        tip: "Shut Prose down, then start it again", #selector(Controller.restart(_:)))
        case .forceStop:
            return item(id, "Force Stop", palette: "Force Stop", symbol: "stop.fill",
                        tip: "Turn the virtual machine off without shutting Prose down", #selector(Controller.forceStop(_:)))
        case .screenshot:
            return item(id, "Screenshot", palette: "Take Screenshot", symbol: "camera",
                        tip: "Save a picture of Prose's screen", #selector(Controller.takeScreenshot(_:)))
        case .fullScreen:
            return item(id, "Full Screen", palette: "Full Screen", symbol: "arrow.up.left.and.arrow.down.right",
                        tip: "Show Prose on the whole screen", #selector(Controller.toggleFullScreen(_:)))
        case .statusBar:
            return item(id, "Status Bar", palette: "Show / Hide Status Bar", symbol: "rectangle.bottomthird.inset.filled",
                        tip: "Show or hide the status bar", #selector(Controller.toggleStatusBar(_:)))
        case .actualSize:
            return item(id, "Actual Size", palette: "Actual Size", symbol: "1.magnifyingglass",
                        tip: "One guest pixel per point", #selector(Controller.actualSize(_:)))
        case .sendKeys:
            let item = NSMenuToolbarItem(itemIdentifier: id)
            item.label = "Send Keys"
            item.paletteLabel = "Send Keys"
            item.toolTip = "Send keys a Mac keyboard doesn't have"
            item.image = NSImage(systemSymbolName: "keyboard", accessibilityDescription: "Send Keys")
            item.menu = controller.makeSendKeysMenu()
            item.showsIndicator = true
            return item
        default:
            return nil
        }
    }

    private func item(_ id: NSToolbarItem.Identifier, _ label: String, palette: String, symbol: String,
                      tip: String, _ action: Selector) -> NSToolbarItem {
        let item = NSToolbarItem(itemIdentifier: id)
        item.label = label
        item.paletteLabel = palette
        item.toolTip = tip
        item.image = NSImage(systemSymbolName: symbol, accessibilityDescription: label)
        item.isBordered = true
        item.target = controller
        item.action = action
        return item
    }
}

// MARK: - The app icon: the Prose mark on a sheet of ruled paper

enum ProseIcon {
    static let paper = NSColor(srgbRed: 0xF5 / 255, green: 0xEF / 255, blue: 0xE3 / 255, alpha: 1)
    static let ink = NSColor(srgbRed: 0x1B / 255, green: 0x19 / 255, blue: 0x17 / 255, alpha: 1)
    static let blue = NSColor(srgbRed: 0x2F / 255, green: 0x5A / 255, blue: 0xA8 / 255, alpha: 1)
    static let vermilion = NSColor(srgbRed: 0xD6 / 255, green: 0x48 / 255, blue: 0x2B / 255, alpha: 1)

    static func image(size: CGFloat = 512) -> NSImage {
        NSImage(size: NSSize(width: size, height: size), flipped: false) { rect in
            let s = rect.width / 1024          // Apple's icon grid is 1024 with an 824 body
            let body = NSRect(x: 100 * s, y: 100 * s, width: 824 * s, height: 824 * s)
            let sheet = NSBezierPath(roundedRect: body, xRadius: 185 * s, yRadius: 185 * s)

            NSGraphicsContext.saveGraphicsState()
            let shadow = NSShadow()
            shadow.shadowOffset = NSSize(width: 0, height: -12 * s)
            shadow.shadowBlurRadius = 28 * s
            shadow.shadowColor = NSColor.black.withAlphaComponent(0.28)
            shadow.set()
            paper.setFill()
            sheet.fill()
            NSGraphicsContext.restoreGraphicsState()

            NSGraphicsContext.saveGraphicsState()
            sheet.addClip()
            NSGradient(starting: .white.withAlphaComponent(0.55), ending: .clear)?.draw(in: body, angle: -90)
            // ruled lines and the proofreader's margin
            blue.withAlphaComponent(0.22).setFill()
            var y = body.minY + 118 * s
            while y < body.maxY - 60 * s {
                NSRect(x: body.minX, y: y, width: body.width, height: 5 * s).fill()
                y += 88 * s
            }
            vermilion.withAlphaComponent(0.8).setFill()
            NSRect(x: body.minX + 190 * s, y: body.minY, width: 6 * s, height: body.height).fill()
            NSGraphicsContext.restoreGraphicsState()

            // The pilcrow (brand/README.md): a left half-annulus bowl, centre (146,107),
            // radii 57 and 27, fused to a stem x 118–146, y 50–220, in a 240-unit box, y down.
            let k = 3.1 * s
            let origin = NSPoint(x: 512 * s - 117.5 * k, y: 512 * s - 105 * k)
            func p(_ x: CGFloat, _ y: CGFloat) -> NSPoint { NSPoint(x: origin.x + x * k, y: origin.y + (240 - y) * k) }
            let mark = NSBezierPath()
            let centre = p(146, 107)
            mark.appendArc(withCenter: centre, radius: 57 * k, startAngle: 90, endAngle: 270, clockwise: false)
            mark.appendArc(withCenter: centre, radius: 27 * k, startAngle: 270, endAngle: 90, clockwise: true)
            mark.close()
            let stemOrigin = p(118, 220)
            mark.append(NSBezierPath(roundedRect: NSRect(x: stemOrigin.x, y: stemOrigin.y, width: 28 * k, height: 170 * k),
                                     xRadius: 14 * k, yRadius: 14 * k))
            ink.setFill()
            mark.fill()
            return true
        }
    }
}

// MARK: - Glue: keeps the toolbar, status bar and overlay in step with the VM

final class WindowChrome: NSObject {
    static let statusBarDefault = "ShowStatusBar"
    unowned let controller: Controller
    let window: NSWindow
    let content: VMContentView
    let toolbarController: ToolbarController
    private var timer: Timer?
    private var ticks = 0
    private var last: VMCounters?
    private var lastCPU: (nanos: UInt64, time: UInt64)?
    private var lastMIDI = 0
    private var lastChip = 0
    private var locateAttempts = 0
    private var guestIP: String?
    private var ipCheckedAt = 0
    var statusBarBeforeFullScreen = true
    private var dropReceiver: DropReceiver?

    /// Show the status bar? The user's last choice; --no-statusbar for this run only.
    static var statusBarInitiallyShown: Bool {
        if args.contains("--no-statusbar") { return false }
        UserDefaults.standard.register(defaults: [statusBarDefault: true])
        return UserDefaults.standard.bool(forKey: statusBarDefault)
    }

    init(controller: Controller, window: NSWindow, content: VMContentView) {
        self.controller = controller
        self.window = window
        self.content = content
        toolbarController = ToolbarController(controller: controller)
        super.init()

        let toolbar = NSToolbar(identifier: "ProseVMToolbar")
        toolbar.delegate = toolbarController
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = true
        // --no-toolbar hides it for this run without remembering that as the user's choice
        toolbar.autosavesConfiguration = !args.contains("--no-toolbar")
        window.toolbarStyle = .unified
        let install = {
            window.toolbar = toolbar
            if args.contains("--no-toolbar") { toolbar.isVisible = false }
        }
        // the display keeps the size it was made with (see ProseWindow)
        if let window = window as? ProseWindow { window.keepingContentSize(install) } else { install() }

        content.overlay.action = { [unowned controller] in controller.overlayButton() }
        let bar = content.statusBar
        bar.sound.isHidden = args.contains("--no-sound")
        bar.midi.isHidden = args.contains("--no-midi")
        bar.chip.isHidden = args.contains("--no-chip")
        bar.network.isHidden = args.contains("--no-net")
        bar.disk.toolTip = "Disk: \(diskURL.lastPathComponent)"

        // files dropped on the window go into the shared folder (drop.swift)
        let drop = DropReceiver { [unowned controller] line in
            controller.chrome?.content.statusBar.show(message: line)
            log(line)
        }
        drop.flash = { [unowned self] in content.flash() }
        content.registerForDraggedTypes([.fileURL])
        dropReceiver = drop

        let t = Timer(timeInterval: 0.1, target: self, selector: #selector(tick), userInfo: nil, repeats: true)
        RunLoop.main.add(t, forMode: .common)       // keeps running while menus are open or the window resizes
        timer = t
        update()
    }

    /// The VM changed state: labels, lights, overlay, toolbar.
    func update() {
        let bar = content.statusBar
        let (text, color) = controller.stateDescription
        bar.stateLabel.stringValue = text
        bar.stateLight.color = color
        bar.stateLight.setLit(true)
        content.overlay.kind = controller.overlayKind
        window.toolbar?.validateVisibleItems()
        updateUptime()
        if controller.vm?.state != .running {
            for light in [bar.disk.lights, bar.network.lights, bar.midi.lights,
                          bar.chip.lights].joined() { light.setLit(false) }
            bar.sound.lights.first?.setLit(false)
            bar.microphone.isHidden = true
            bar.cpu.label.stringValue = "–"
            bar.network.label.stringValue = "–"
            lastCPU = nil
            last = nil
        }
    }

    private func updateUptime() {
        let label = content.statusBar.uptimeLabel
        guard let since = controller.runningSince else {
            label.stringValue = ""
            return
        }
        let t = Int(Date().timeIntervalSince(since))
        label.stringValue = String(format: "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60)
    }

    @objc private func tick() {
        ticks += 1
        let bar = content.statusBar
        let running = controller.vm?.state == .running

        // MIDI is our own device: its counter lights the MIDI light directly
        let midiCount = controller.midi.activity.withLock { $0 }
        if midiCount != lastMIDI {
            lastMIDI = midiCount
            bar.midi.lights.first?.flash()
        }

        // The chip is ours too: its light is lit while anything is sounding and
        // flashes as each tune starts, so a tune that never arrives and a tune
        // that arrives and says nothing look different.
        let chipActivity = controller.chip.activity.withLock { $0 }
        if chipActivity.started != lastChip {
            lastChip = chipActivity.started
            bar.chip.lights.first?.flash()
        }
        bar.chip.lights.first?.setLit(controller.chip.sounding)

        if running, let monitor = controller.monitor {
            // the process at once, its network port a moment later; then look less often
            let wanted = monitor.pid == 0 || (monitor.interface == nil && !args.contains("--no-net"))
            if wanted, ticks % (locateAttempts < 60 ? 10 : 100) == 0 {
                locateAttempts += 1
                let known = (monitor.pid, monitor.interface)
                monitor.locate()
                if monitor.pid != known.0 { log("monitor: VM process \(monitor.pid)") }
                if let port = monitor.interface, known.1 == nil { log("monitor: network port \(port)") }
            }
            if let now = monitor.sample() {
                if let last {
                    if now.diskRead > last.diskRead { bar.disk.lights[0].flash() }
                    if now.diskWritten > last.diskWritten { bar.disk.lights[1].flash() }
                    if now.packetsReceived > last.packetsReceived { bar.network.lights[0].flash() }
                    if now.packetsSent > last.packetsSent { bar.network.lights[1].flash() }
                }
                last = now
            }
        }

        guard ticks % 10 == 0 else { return }      // once a second: numbers and tooltips
        updateUptime()
        updateDisplay()
        window.toolbar?.validateVisibleItems()     // a picture arriving enables Screenshot, and so on
        guard running, let monitor = controller.monitor, let now = last else { return }

        let time = clock_gettime_nsec_np(CLOCK_UPTIME_RAW)
        if let prev = lastCPU, time > prev.time {
            let cores = Double(now.cpuNanos &- prev.nanos) / Double(time - prev.time)
            let percent = Int((cores / Double(cpus) * 100).rounded())
            bar.cpu.label.stringValue = "\(min(percent, 100))%"
            bar.cpu.toolTip = String(format: "Processor: %d%% of %d virtual CPUs (%.2f host cores)\nMemory: %d GB, %@ in use on the Mac",
                                     percent, cpus, cores, Int(memoryGiB), formatBytes(now.footprint))
        }
        lastCPU = (now.cpuNanos, time)

        bar.disk.toolTip = "Disk: \(diskURL.lastPathComponent) (\((option("--disk") ?? "nvme").uppercased()))\n"
            + "Read \(formatBytes(now.diskRead)) · written \(formatBytes(now.diskWritten))\n"
            + "Lights: green reading, orange writing"

        // the guest's address: look until bootpd has leased one, then now and then
        if guestIP == nil ? ticks - ipCheckedAt >= 20 : ticks - ipCheckedAt >= 300 {
            ipCheckedAt = ticks
            let ip = monitor.guestIP()
            if ip != guestIP, let ip { log("monitor: guest IP \(ip)") }
            guestIP = ip ?? guestIP
        }
        bar.network.label.stringValue = guestIP ?? "no address"
        bar.network.toolTip = "Network: NAT" + (monitor.interface.map { " on \($0)" } ?? "") + "\n"
            + "IP address: \(guestIP ?? "not yet leased")\n"
            + "MAC address: \(controller.macAddress.string)\n"
            + "Received \(formatBytes(now.netReceived)) · sent \(formatBytes(now.netSent))\n"
            + "Lights: green receiving, orange sending"

        let audio = monitor.audio()
        bar.sound.lights.first?.setLit(audio.output)
        bar.sound.toolTip = audio.output ? "Sound: Prose is playing" : "Sound: quiet"
        bar.microphone.isHidden = !audio.input
        bar.microphone.toolTip = "Prose is recording from the Mac's sound input"
        bar.midi.toolTip = "MIDI: \(midiCount) messages from Prose, played on the Mac's synthesizer "
            + "(CoreMIDI source “\(ProseMIDIDevice.endpointName)”)"
        bar.chip.toolTip = "Chip: \(chipActivity.started) tunes sent as notation — "
            + "\(chipActivity.chip) on the three-chip synthesiser, \(chipActivity.synth) "
            + "on the Mac's General MIDI one"
    }

    private func updateDisplay() {
        let item = content.statusBar.display
        let size = controller.displaySize
        // The screen mode is the guest's; the scale is what happens to it here.
        // Showing the mode alone reads as a claim about what is on the screen,
        // so the scale goes beside it whenever it is not 1.
        let scale = PresenterScale.current
        item.label.stringValue = size.map {
            scale == .x1 ? "\($0.width) × \($0.height)" : "\($0.width) × \($0.height)  \(scale.title)"
        } ?? "No signal"
        if let size, displayMode == "s2" {
            let across = size.width * scale.rawValue, down = size.height * scale.rawValue
            item.toolTip = scale == .x1
                ? "Prose's screen is \(size.width) × \(size.height), one guest pixel per screen pixel "
                  + "(View ▸ Screen Mode, View ▸ Scale)"
                : "Prose's screen is \(size.width) × \(size.height), shown \(scale.title) — "
                  + "\(across) × \(down) of this Mac's pixels (View ▸ Screen Mode, View ▸ Scale)"
        } else if size != nil {
            item.toolTip = "virtio-gpu (S1): the guest's screen is \(width) × \(height); the window scales it"
        }
    }

    func forgetGuestAddress() {
        guestIP = nil
        locateAttempts = 0
    }
}
