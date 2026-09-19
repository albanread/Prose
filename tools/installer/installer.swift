// installer.swift: the Prose installer — the app that puts Prose on a Mac, updates
// it, and takes it off again.
//
// An installation is not one thing. It is the application, which is ours and
// always safe to replace; the machine, which is the owner's and holds whatever
// they have put in it; their settings; and the folder they share into it, which
// is none of our business. Drag-and-drop can only express "replace the app",
// and the one thing it silently cannot do is update the machine — which is how
// a newer Prose came to boot an older machine and sit there saying its portal
// would not answer.
//
// So each part is offered separately, and the destructive one is off by default
// when there is something to destroy.

import AppKit
import CryptoKit

// MARK: - where things live

enum Paths {
    static let app = URL(fileURLWithPath: "/Applications/Prose.app")
    static let domain = "org.prose.vm"

    static var support: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Prose", isDirectory: true)
    }
    static var machines: URL { support.appendingPathComponent("Machines", isDirectory: true) }
    static var machine: URL { machines.appendingPathComponent("Prose.image") }
    static var templateStamp: URL { machines.appendingPathComponent(".template") }
    static var logs: URL {
        FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Logs/Prose", isDirectory: true)
    }
    static var share: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("HostFS", isDirectory: true)
    }
    static var preferences: URL {
        FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Preferences/\(domain).plist")
    }
}

func exists(_ url: URL) -> Bool { FileManager.default.fileExists(atPath: url.path) }

func size(of url: URL) -> Int64 {
    guard let values = try? url.resourceValues(forKeys: [.fileSizeKey, .isDirectoryKey]) else { return 0 }
    if values.isDirectory == true {
        var total: Int64 = 0
        if let walk = FileManager.default.enumerator(at: url, includingPropertiesForKeys: [.fileSizeKey]) {
            for case let item as URL in walk {
                total += Int64((try? item.resourceValues(forKeys: [.fileSizeKey]))?.fileSize ?? 0)
            }
        }
        return total
    }
    return Int64(values.fileSize ?? 0)
}

func readable(_ bytes: Int64) -> String {
    let formatter = ByteCountFormatter()
    formatter.countStyle = .file
    return formatter.string(fromByteCount: bytes)
}

func version(of bundle: URL) -> String? {
    guard let plist = NSDictionary(contentsOf: bundle.appendingPathComponent("Contents/Info.plist")),
          let short = plist["CFBundleShortVersionString"] as? String else { return nil }
    let build = plist["CFBundleVersion"] as? String ?? "?"
    return "\(short) (\(build))"
}

/// What a machine was copied from: the image's size and a hash of the first
/// 4 MiB of its system partition, which differs between builds where the front
/// of the file does not. The same identity Prose itself records, so the two
/// agree about whether a machine is current.
func templateIdentity(_ url: URL) -> String? {
    guard let bytes = try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? Int,
          let handle = try? FileHandle(forReadingFrom: url) else { return nil }
    defer { try? handle.close() }
    guard let mbr = try? handle.read(upToCount: 512), mbr.count == 512 else { return nil }
    var start: UInt64 = 36 << 20
    for i in 0..<4 {
        let entry = mbr.subdata(in: (446 + 16 * i)..<(462 + 16 * i))
        if entry[4] == 0xEB {
            start = UInt64(entry.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 8, as: UInt32.self) }) * 512
            break
        }
    }
    try? handle.seek(toOffset: start)
    guard let sample = try? handle.read(upToCount: 4 << 20) else { return nil }
    return "\(bytes)-" + SHA256.hash(data: sample).map { String(format: "%02x", $0) }.joined().prefix(16)
}

// MARK: - what is here now

struct State {
    let source: URL?                    // the Prose.app beside this installer
    let sourceVersion: String?
    let sourceMachine: URL?
    let installed: Bool
    let installedVersion: String?
    let machineExists: Bool
    let machineSize: Int64
    let machineUsed: Date?
    let machineIsCurrent: Bool          // made from the machine this installer carries
    let settingsExist: Bool
    let shareExists: Bool
    let shareItems: Int

    static func read() -> State {
        // Prose.app travels inside this installer, so the disk image holds one
        // thing and nobody has to guess whether to drag it or open it. Beside
        // the installer works too, for a build directory.
        let inside = Bundle.main.bundleURL.appendingPathComponent("Contents/Resources/Prose.app", isDirectory: true)
        let beside = Bundle.main.bundleURL.deletingLastPathComponent()
            .appendingPathComponent("Prose.app", isDirectory: true)
        let source: URL? = exists(inside) ? inside : (exists(beside) ? beside : nil)
        let sourceMachine = source?.appendingPathComponent("Contents/Resources/prose.image")

        var current = false
        if let sourceMachine, exists(sourceMachine), exists(Paths.machine) {
            let stamped = try? String(contentsOf: Paths.templateStamp, encoding: .utf8)
            current = stamped != nil && stamped == templateIdentity(sourceMachine)
        }
        let used = (try? Paths.machine.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate
        let shareItems = (try? FileManager.default.contentsOfDirectory(atPath: Paths.share.path).count) ?? 0

        return State(source: source,
                     sourceVersion: source.flatMap(version),
                     sourceMachine: sourceMachine.flatMap { exists($0) ? $0 : nil },
                     installed: exists(Paths.app),
                     installedVersion: exists(Paths.app) ? version(of: Paths.app) : nil,
                     machineExists: exists(Paths.machine),
                     machineSize: exists(Paths.machine) ? size(of: Paths.machine) : 0,
                     machineUsed: used,
                     machineIsCurrent: current,
                     settingsExist: exists(Paths.preferences),
                     shareExists: exists(Paths.share),
                     shareItems: shareItems)
    }
}

// MARK: - a row: a checkbox, a title, a size, and what it means

final class Choice: NSView {
    let box = NSButton(checkboxWithTitle: "", target: nil, action: nil)
    private let title = NSTextField(labelWithString: "")
    private let detail = NSTextField(wrappingLabelWithString: "")
    private let note = NSTextField(labelWithString: "")

    var on: Bool { box.state == .on }

    init(_ name: String, note trailing: String = "", detail text: String, on: Bool,
         enabled: Bool = true, warning: Bool = false) {
        super.init(frame: .zero)
        box.state = on ? .on : .off
        box.isEnabled = enabled
        title.stringValue = name
        title.font = .systemFont(ofSize: 13, weight: .medium)
        note.stringValue = trailing
        note.font = .systemFont(ofSize: 11)
        note.textColor = .secondaryLabelColor
        detail.stringValue = text
        detail.font = .systemFont(ofSize: 11)
        detail.textColor = warning ? .systemOrange : .secondaryLabelColor
        if !enabled { title.textColor = .disabledControlTextColor }

        // the title keeps its size and the gap before the note takes the slack
        title.setContentHuggingPriority(.required, for: .horizontal)
        note.setContentHuggingPriority(.required, for: .horizontal)
        detail.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let heading = NSStackView(views: [title, spacer, note])
        heading.orientation = .horizontal
        heading.spacing = 8
        heading.alignment = .firstBaseline

        let column = NSStackView(views: [heading, detail])
        column.orientation = .vertical
        column.alignment = .leading
        column.spacing = 2
        heading.widthAnchor.constraint(equalTo: column.widthAnchor).isActive = true

        let row = NSStackView(views: [box, column])
        row.orientation = .horizontal
        row.alignment = .top
        row.spacing = 6
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        NSLayoutConstraint.activate([
            row.leadingAnchor.constraint(equalTo: leadingAnchor),
            row.trailingAnchor.constraint(equalTo: trailingAnchor),
            row.topAnchor.constraint(equalTo: topAnchor),
            row.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }

    /// The wrapping detail only knows how tall it is once it knows how wide.
    func fit(to width: CGFloat) { detail.preferredMaxLayoutWidth = width - 26 }

    required init?(coder: NSCoder) { fatalError("not used") }
}

// MARK: - the window

final class InstallerWindow: NSObject, NSWindowDelegate {
    private let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 300),
                                  styleMask: [.titled, .closable, .miniaturizable],
                                  backing: .buffered, defer: false)
    private var state = State.read()
    private var rows: [String: Choice] = [:]
    private let stack = NSStackView()
    private let heading = NSTextField(labelWithString: "")
    private let subheading = NSTextField(wrappingLabelWithString: "")
    private let footnote = NSTextField(wrappingLabelWithString: "")
    private let progress = NSProgressIndicator()
    private let status = NSTextField(labelWithString: "")
    private let goButton = NSButton(title: "Install", target: nil, action: nil)
    private let removeButton = NSButton(title: "Uninstall…", target: nil, action: nil)
    private var mode: Mode = .install

    enum Mode { case install, uninstall, done }

    func show() {
        build()
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    // MARK: layout

    private func build() {
        window.title = "Install Prose"
        window.delegate = self
        let content = NSView()

        let icon = NSImageView()
        // Prose's own icon, from this bundle: the same drawing the application
        // uses, and present whether or not a copy of Prose is beside us
        if let icns = Bundle.main.url(forResource: "Prose", withExtension: "icns"),
           let drawn = NSImage(contentsOf: icns) {
            icon.image = drawn
        } else {
            icon.image = NSImage(named: NSImage.applicationIconName)
        }
        icon.imageScaling = .scaleProportionallyUpOrDown
        icon.translatesAutoresizingMaskIntoConstraints = false
        icon.widthAnchor.constraint(equalToConstant: 64).isActive = true
        icon.heightAnchor.constraint(equalToConstant: 64).isActive = true

        heading.font = .systemFont(ofSize: 16, weight: .semibold)
        subheading.font = .systemFont(ofSize: 11)
        subheading.textColor = .secondaryLabelColor
        subheading.preferredMaxLayoutWidth = 396
        footnote.font = .systemFont(ofSize: 11)
        footnote.textColor = .secondaryLabelColor
        footnote.preferredMaxLayoutWidth = 464

        let titles = NSStackView(views: [heading, subheading])
        titles.orientation = .vertical
        titles.alignment = .leading
        titles.spacing = 3
        let top = NSStackView(views: [icon, titles])
        top.orientation = .horizontal
        top.alignment = .top
        top.spacing = 16

        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 14

        progress.style = .bar
        progress.isIndeterminate = false
        progress.minValue = 0
        progress.maxValue = 1
        progress.isHidden = true
        status.font = .systemFont(ofSize: 11)
        status.textColor = .secondaryLabelColor
        status.isHidden = true

        goButton.bezelStyle = .rounded
        goButton.keyEquivalent = "\r"
        goButton.target = self
        goButton.action = #selector(go)
        removeButton.bezelStyle = .rounded
        removeButton.target = self
        removeButton.action = #selector(toggleUninstall)
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let buttons = NSStackView(views: [removeButton, spacer, goButton])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        // one column: hidden arranged subviews collapse, so nothing reserves
        // room for a progress bar that is not running
        let column = NSStackView(views: [top, stack, footnote, progress, status, buttons])
        column.orientation = .vertical
        column.alignment = .leading
        column.spacing = 18
        column.setCustomSpacing(6, after: progress)
        column.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(column)
        NSLayoutConstraint.activate([
            column.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 24),
            column.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -24),
            column.topAnchor.constraint(equalTo: content.topAnchor, constant: 24),
            column.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -20),
            column.widthAnchor.constraint(equalToConstant: 472),
        ])
        for v in [stack, footnote, progress, buttons] {
            v.widthAnchor.constraint(equalTo: column.widthAnchor).isActive = true
        }
        window.contentView = content
        populate()
    }

    /// The choices, and their defaults. The one rule: a default never destroys
    /// something the owner made.
    private func populate() {
        state = State.read()
        stack.arrangedSubviews.forEach { $0.removeFromSuperview() }
        rows.removeAll()

        func add(_ key: String, _ row: Choice) {
            rows[key] = row
            row.translatesAutoresizingMaskIntoConstraints = false
            row.fit(to: 472)
            stack.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        }

        switch mode {
        case .install:
            guard let source = state.source, let sourceVersion = state.sourceVersion else {
                heading.stringValue = "This installer has no copy of Prose in it"
                subheading.stringValue = "Open the disk image you downloaded and run the installer "
                    + "from inside it."
                footnote.stringValue = ""
                goButton.isEnabled = false
                removeButton.isHidden = !state.installed
                return
            }
            _ = source
            heading.stringValue = state.installed ? "Update Prose" : "Install Prose"
            subheading.stringValue = state.installed
                ? "Choose what to replace. Anything you leave unticked is kept as it is."
                : "Haiku on Apple Silicon, in a window, with the host GPU behind its display."

            add("app", Choice("Application", note: sourceVersion,
                detail: state.installed
                    ? "Replaces \(state.installedVersion ?? "the copy") in Applications."
                    : "Installs Prose in Applications.",
                on: true))

            if let machineSource = state.sourceMachine {
                let bundled = readable(size(of: machineSource))
                if state.machineExists {
                    let when = state.machineUsed.map {
                        DateFormatter.localizedString(from: $0, dateStyle: .medium, timeStyle: .short)
                    } ?? "unknown"
                    // Half of Prose lives in the machine -- the portal Prose talks to
                    // it through, its drivers, its applications. Keeping an old
                    // machine keeps that half old, which is worth saying plainly,
                    // because the symptom is Prose reporting that its own portal
                    // will not answer.
                    add("machine", Choice("File system", note: bundled,
                        detail: state.machineIsCurrent
                            ? "Yours is already this version. Replacing it loses everything in it — "
                              + "files, settings, anything you installed. Last used \(when)."
                            : "Yours was made by an earlier version of Prose and has that version's "
                              + "drivers and portal, so automation may not work until it is replaced. "
                              + "Replacing it loses everything in it — files, settings, anything you "
                              + "installed. Last used \(when).",
                        on: false, warning: !state.machineIsCurrent))
                } else {
                    add("machine", Choice("File system", note: bundled,
                        detail: "The Haiku system Prose runs, with its drivers and the portal Prose "
                              + "talks to it through. Goes in Application Support.",
                        on: true))
                }
            }

            if state.settingsExist {
                add("settings", Choice("Reset settings",
                    detail: "Presenter mode, whether automation is allowed, window and toolbar.",
                    on: false))
            }

            footnote.stringValue = state.shareExists
                ? "Your shared folder, \(Paths.share.path.replacingOccurrences(of: NSHomeDirectory(), with: "~")) "
                  + "(\(state.shareItems) item\(state.shareItems == 1 ? "" : "s")), is never touched."
                : "A shared folder is created at ~/Documents/HostFS the first time Prose runs."
            goButton.title = state.installed ? "Update" : "Install"
            goButton.isEnabled = true
            removeButton.isHidden = !state.installed
            removeButton.title = "Uninstall…"

        case .uninstall:
            heading.stringValue = "Remove Prose"
            subheading.stringValue = "Choose what to remove. This cannot be undone."
            add("app", Choice("Application", note: state.installedVersion ?? "",
                detail: "Removes Prose from Applications.", on: state.installed,
                enabled: state.installed))
            if state.machineExists {
                add("machine", Choice("File system", note: readable(state.machineSize),
                    detail: "Everything in it — files, settings, anything you installed — goes with it.",
                    on: false, warning: true))
            }
            if state.settingsExist {
                add("settings", Choice("Settings", detail: "Presenter mode, automation, window and toolbar.",
                    on: true))
            }
            if state.shareExists {
                add("share", Choice("Shared folder", note: "\(state.shareItems) item\(state.shareItems == 1 ? "" : "s")",
                    detail: "\(Paths.share.path.replacingOccurrences(of: NSHomeDirectory(), with: "~")) — "
                          + "your own files. Left alone unless you say otherwise.",
                    on: false, warning: state.shareItems > 0))
            }
            footnote.stringValue = ""
            footnote.isHidden = true
            goButton.title = "Remove"
            goButton.isEnabled = true
            removeButton.isHidden = false
            removeButton.title = "Back"

        case .done:
            break
        }
        footnote.isHidden = footnote.stringValue.isEmpty
        resize()
    }

    private func resize() {
        window.layoutIfNeeded()
        let fitting = window.contentView!.fittingSize
        window.setContentSize(NSSize(width: 520, height: fitting.height))
    }

    // MARK: doing it

    @objc private func toggleUninstall() {
        mode = (mode == .uninstall) ? .install : .uninstall
        populate()
    }

    @objc private func go() {
        let picked = rows.filter { $0.value.on }.map(\.key)
        guard !picked.isEmpty else { return }

        if mode == .uninstall || picked.contains("machine"), state.machineExists,
           picked.contains("machine") {
            let alert = NSAlert()
            alert.messageText = "Remove the file system and everything in it?"
            alert.informativeText = "Files you made in Prose, its settings, and anything you installed "
                + "are lost. \(readable(state.machineSize)) is freed."
            alert.addButton(withTitle: mode == .uninstall ? "Remove" : "Replace")
            alert.addButton(withTitle: "Cancel")
            alert.alertStyle = .critical
            guard alert.runModal() == .alertFirstButtonReturn else { return }
        }

        goButton.isEnabled = false
        removeButton.isEnabled = false
        rows.values.forEach { $0.box.isEnabled = false }
        progress.isHidden = false
        status.isHidden = false
        progress.doubleValue = 0
        resize()

        let work = mode
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            let failure = work == .uninstall ? remove(picked) : install(picked)
            DispatchQueue.main.async { self.finished(failure) }
        }
    }

    private func step(_ text: String, _ fraction: Double) {
        DispatchQueue.main.async { [self] in
            status.stringValue = text
            progress.doubleValue = fraction
        }
    }

    /// Prose must not be running while its application or machine is replaced.
    private func quitProse() {
        let running = NSRunningApplication.runningApplications(withBundleIdentifier: Paths.domain)
        guard !running.isEmpty else { return }
        step("Quitting Prose…", 0.02)
        running.forEach { $0.terminate() }
        for _ in 0..<100 {
            if NSRunningApplication.runningApplications(withBundleIdentifier: Paths.domain).isEmpty { return }
            Thread.sleep(forTimeInterval: 0.1)
        }
        NSRunningApplication.runningApplications(withBundleIdentifier: Paths.domain).forEach { $0.forceTerminate() }
        Thread.sleep(forTimeInterval: 0.5)
    }

    /// A gigabyte is long enough that a spinner is not honest about it.
    private func copy(_ from: URL, to: URL, _ label: String, _ base: Double, _ span: Double) throws {
        let total = Double(max(size(of: from), 1))
        try? FileManager.default.removeItem(at: to)
        try FileManager.default.createDirectory(at: to.deletingLastPathComponent(),
                                                withIntermediateDirectories: true)
        guard let input = try? FileHandle(forReadingFrom: from) else {
            throw CocoaError(.fileReadNoSuchFile)
        }
        defer { try? input.close() }
        FileManager.default.createFile(atPath: to.path, contents: nil)
        guard let output = try? FileHandle(forWritingTo: to) else { throw CocoaError(.fileWriteNoPermission) }
        defer { try? output.close() }
        var written = 0.0
        while true {
            guard let chunk = try input.read(upToCount: 8 << 20), !chunk.isEmpty else { break }
            try output.write(contentsOf: chunk)
            written += Double(chunk.count)
            step(label, base + span * (written / total))
        }
    }

    private func install(_ picked: [String]) -> String? {
        do {
            quitProse()
            if picked.contains("app"), let source = state.source {
                step("Installing the application…", 0.05)
                try? FileManager.default.removeItem(at: Paths.app)
                try FileManager.default.copyItem(at: source, to: Paths.app)
            }
            if picked.contains("machine"), let machineSource = state.sourceMachine {
                // the EFI variables belong to the machine that is going
                try? FileManager.default.removeItem(at: Paths.machines.appendingPathComponent("Prose.image.efivars"))
                try FileManager.default.createDirectory(at: Paths.machines, withIntermediateDirectories: true)
                try copy(machineSource, to: Paths.machine, "Copying the machine…", 0.1, 0.85)
                if let identity = templateIdentity(machineSource) {
                    try? identity.write(to: Paths.templateStamp, atomically: true, encoding: .utf8)
                }
                FileManager.default.createFile(atPath: Paths.machines.appendingPathComponent(".firstrun").path,
                                               contents: nil)
            }
            if picked.contains("settings") {
                step("Resetting settings…", 0.97)
                forgetSettings()
            }
            step("Done", 1)
            return nil
        } catch {
            return (error as NSError).localizedDescription
        }
    }

    private func remove(_ picked: [String]) -> String? {
        quitProse()
        var failed: [String] = []
        func drop(_ url: URL, _ name: String) {
            do { try FileManager.default.removeItem(at: url) } catch { failed.append(name) }
        }
        if picked.contains("app") { step("Removing the application…", 0.2); drop(Paths.app, "the application") }
        if picked.contains("machine") {
            step("Removing the machine…", 0.5)
            drop(Paths.support, "the machine")
        } else if picked.contains("app") {
            // the logs are ours, and mean nothing without the application
            try? FileManager.default.removeItem(at: Paths.logs)
        }
        if picked.contains("settings") { step("Removing settings…", 0.8); forgetSettings() }
        if picked.contains("share") { step("Removing the shared folder…", 0.9); drop(Paths.share, "the shared folder") }
        step("Done", 1)
        return failed.isEmpty ? nil : "Could not remove " + failed.joined(separator: ", ") + "."
    }

    /// `defaults delete` is not enough on its own: cfprefsd holds the domain and
    /// writes it back, so the file goes and the daemon is told to forget.
    private func forgetSettings() {
        try? FileManager.default.removeItem(at: Paths.preferences)
        let defaults = Process()
        defaults.executableURL = URL(fileURLWithPath: "/usr/bin/defaults")
        defaults.arguments = ["delete", Paths.domain]
        try? defaults.run()
        defaults.waitUntilExit()
        let kill = Process()
        kill.executableURL = URL(fileURLWithPath: "/usr/bin/killall")
        kill.arguments = ["cfprefsd"]
        try? kill.run()
        kill.waitUntilExit()
    }

    private func finished(_ failure: String?) {
        progress.isHidden = true
        status.isHidden = true
        removeButton.isEnabled = true
        let wasUninstall = (mode == .uninstall)
        state = State.read()

        if let failure {
            status.stringValue = ""
            let alert = NSAlert()
            alert.messageText = wasUninstall ? "Prose was not fully removed" : "Prose was not installed"
            alert.informativeText = failure
            alert.alertStyle = .warning
            alert.runModal()
            mode = wasUninstall ? .uninstall : .install
            goButton.isEnabled = true
            populate()
            return
        }

        if wasUninstall && !state.installed {
            heading.stringValue = "Prose has been removed"
            subheading.stringValue = state.machineExists
                ? "Your machine is still in Application Support."
                : ""
            stack.arrangedSubviews.forEach { $0.removeFromSuperview() }
            rows.removeAll()
            footnote.stringValue = ""
            status.stringValue = ""
            removeButton.isHidden = true
            goButton.title = "Quit"
            goButton.action = #selector(NSApplication.terminate(_:))
            goButton.target = NSApp
            goButton.isEnabled = true
            resize()
            return
        }

        heading.stringValue = "Prose is installed"
        subheading.stringValue = "It is in your Applications folder."
        stack.arrangedSubviews.forEach { $0.removeFromSuperview() }
        rows.removeAll()
        footnote.stringValue = "The first run takes a moment: Prose sets up its folders and starts the machine."
        status.stringValue = ""
        removeButton.isHidden = true
        goButton.title = "Open Prose"
        goButton.action = #selector(openProse)
        goButton.target = self
        goButton.isEnabled = true
        resize()
    }

    @objc private func openProse() {
        NSWorkspace.shared.openApplication(at: Paths.app, configuration: NSWorkspace.OpenConfiguration())
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { NSApp.terminate(nil) }
    }

    func windowWillClose(_ notification: Notification) { NSApp.terminate(nil) }
}

// MARK: -

final class Delegate: NSObject, NSApplicationDelegate {
    let window = InstallerWindow()
    func applicationDidFinishLaunching(_ notification: Notification) { window.show() }
    func applicationShouldTerminateAfterLastWindowClosed(_ app: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = Delegate()
app.delegate = delegate
app.run()
