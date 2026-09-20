// settings.swift: what the machine is made of, and the window for changing it.
//
// Everything here was a command-line flag, which is fine for a script and no use
// at all to somebody who opened an application. The values live in the app's own
// defaults; a flag still wins when one is given, so the test scripts keep working
// and an installed copy has something to remember.
//
// None of it can change while the machine is running: Virtualization.framework
// fixes a configuration when the machine is created. makeConfiguration() reads
// these each time a machine is made, and a machine is made afresh whenever a
// stopped one is started, so the answer is always "next time it starts".

import AppKit
import Virtualization

enum Settings {
    static let cpuKey = "prose.cpuCount"
    static let memoryKey = "prose.memoryGiB"
    static let networkKey = "prose.networking"
    static let soundKey = "prose.sound"
    static let shareEnabledKey = "prose.shareEnabled"
    static let shareFolderKey = "prose.shareFolder"
    static let shareReadOnlyKey = "prose.shareReadOnly"

    /// VZ's own floor and ceiling. A configuration outside these is refused, so
    /// every value is clamped to them however it arrived.
    static var cpuLimits: ClosedRange<Int> {
        VZVirtualMachineConfiguration.minimumAllowedCPUCount
            ... VZVirtualMachineConfiguration.maximumAllowedCPUCount
    }
    static var memoryLimitsGiB: ClosedRange<Int> {
        let low = max(1, Int(VZVirtualMachineConfiguration.minimumAllowedMemorySize >> 30))
        return low ... max(low, Int(VZVirtualMachineConfiguration.maximumAllowedMemorySize >> 30))
    }

    /// What the window offers, which is deliberately less. On this Mac VZ allows
    /// 64 processors and every gigabyte of memory the machine has; handing either
    /// to a guest leaves macOS nothing to run on. The window stops at the host's
    /// core count and keeps 8 GB back for the host. A flag may still ask for more.
    static var cpuChoices: ClosedRange<Int> {
        let host = ProcessInfo.processInfo.processorCount
        return cpuLimits.lowerBound ... min(cpuLimits.upperBound, max(cpuLimits.lowerBound, host))
    }
    static var memoryChoicesGiB: ClosedRange<Int> {
        let host = Int(ProcessInfo.processInfo.physicalMemory >> 30)
        let low = memoryLimitsGiB.lowerBound
        return low ... min(memoryLimitsGiB.upperBound, max(low, host - 8))
    }

    private static func stored(_ key: String, _ fallback: Int) -> Int {
        UserDefaults.standard.object(forKey: key) as? Int ?? fallback
    }
    private static func stored(_ key: String, _ fallback: Bool) -> Bool {
        UserDefaults.standard.object(forKey: key) as? Bool ?? fallback
    }

    /// A flag beats a setting: scripts say what they mean and should not be
    /// steered by whatever the window was last left at.
    static var cpuCount: Int {
        let wanted = Int(option("--cpus") ?? "") ?? stored(cpuKey, min(4, cpuChoices.upperBound))
        return min(max(wanted, cpuLimits.lowerBound), cpuLimits.upperBound)
    }
    static var memoryGiB: Int {
        let wanted = Int(option("--memory") ?? "") ?? stored(memoryKey, 4)
        return min(max(wanted, memoryLimitsGiB.lowerBound), memoryLimitsGiB.upperBound)
    }
    static var networking: Bool { !args.contains("--no-net") && stored(networkKey, true) }
    static var sound: Bool { !args.contains("--no-sound") && stored(soundKey, true) }

    /// The folder the guest mounts as HostFS. Where it has always been until
    /// somebody chooses otherwise.
    static var defaultShareFolder: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("HostFS", isDirectory: true).standardizedFileURL
    }
    static var shareEnabled: Bool { stored(shareEnabledKey, true) }
    static var shareReadOnly: Bool { stored(shareReadOnlyKey, false) }
    static var shareFolder: URL {
        guard let path = UserDefaults.standard.string(forKey: shareFolderKey), !path.isEmpty
        else { return defaultShareFolder }
        return URL(fileURLWithPath: path).standardizedFileURL
    }

    static func set(_ key: String, _ value: Int) { UserDefaults.standard.set(value, forKey: key) }
    static func set(_ key: String, _ value: Bool) { UserDefaults.standard.set(value, forKey: key) }
    static func set(_ key: String, _ value: String) { UserDefaults.standard.set(value, forKey: key) }

    /// A flag in force means the window cannot honestly offer that choice.
    static func overriddenByFlag(_ key: String) -> String? {
        switch key {
        case cpuKey: return option("--cpus") != nil ? "--cpus" : nil
        case memoryKey: return option("--memory") != nil ? "--memory" : nil
        case networkKey: return args.contains("--no-net") ? "--no-net" : nil
        case soundKey: return args.contains("--no-sound") ? "--no-sound" : nil
        case shareEnabledKey, shareFolderKey, shareReadOnlyKey:
            return args.contains("--share") || args.contains("--share-ro") ? "--share" : nil
        default: return nil
        }
    }
}

// MARK: - the window

final class SettingsWindow: NSObject, NSWindowDelegate {
    private var window: NSWindow?
    private unowned let controller: Controller

    private let cpuField = NSTextField(labelWithString: "")
    private let cpuStepper = NSStepper()
    private let memoryField = NSTextField(labelWithString: "")
    private let memoryStepper = NSStepper()
    private let network = NSButton(checkboxWithTitle: "Networking", target: nil, action: nil)
    private let sound = NSButton(checkboxWithTitle: "Sound", target: nil, action: nil)
    private let shareBox = NSButton(checkboxWithTitle: "Share a folder with the guest",
                                    target: nil, action: nil)
    private let readOnlyBox = NSButton(checkboxWithTitle: "Read only", target: nil, action: nil)
    private let folderPath = NSTextField(labelWithString: "")
    private let choose = NSButton(title: "Choose…", target: nil, action: nil)
    private let note = NSTextField(wrappingLabelWithString: "")
    private let restart = NSButton(title: "Restart Now", target: nil, action: nil)

    init(controller: Controller) {
        self.controller = controller
        super.init()
    }

    func show() {
        if window == nil { build() }
        window?.center()
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        refresh()
    }

    private func row(_ label: String, _ value: NSTextField, _ stepper: NSStepper,
                     _ range: ClosedRange<Int>, _ suffix: String) -> NSView {
        let name = NSTextField(labelWithString: label)
        name.alignment = .right
        name.translatesAutoresizingMaskIntoConstraints = false
        name.widthAnchor.constraint(equalToConstant: 92).isActive = true
        value.font = .monospacedDigitSystemFont(ofSize: 13, weight: .regular)
        value.alignment = .right
        value.translatesAutoresizingMaskIntoConstraints = false
        value.widthAnchor.constraint(equalToConstant: 64).isActive = true
        stepper.minValue = Double(range.lowerBound)
        stepper.maxValue = Double(range.upperBound)
        stepper.increment = 1
        stepper.valueWraps = false
        stepper.target = self
        stepper.action = #selector(changed(_:))
        let limit = NSTextField(labelWithString: "of \(range.upperBound)\(suffix) available")
        limit.font = .systemFont(ofSize: 11)
        limit.textColor = .secondaryLabelColor
        let stack = NSStackView(views: [name, value, stepper, limit])
        stack.orientation = .horizontal
        stack.spacing = 8
        stack.alignment = .centerY
        return stack
    }

    /// The shared folder, on its own line because a path needs the width.
    private func folderRow() -> NSView {
        let name = NSTextField(labelWithString: "Folder")
        name.alignment = .right
        name.translatesAutoresizingMaskIntoConstraints = false
        name.widthAnchor.constraint(equalToConstant: 92).isActive = true
        folderPath.lineBreakMode = .byTruncatingMiddle
        folderPath.setContentCompressionResistancePriority(.init(1), for: .horizontal)
        choose.bezelStyle = .rounded
        choose.target = self
        choose.action = #selector(chooseFolder)
        let stack = NSStackView(views: [name, folderPath, choose])
        stack.orientation = .horizontal
        stack.spacing = 8
        stack.alignment = .centerY
        return stack
    }

    private func build() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 468, height: 300),
                         styleMask: [.titled, .closable], backing: .buffered, defer: false)
        w.title = "Prose Settings"
        // A window made this way is released when it closes, which leaves the
        // property above pointing at freed memory and crashes the second time
        // Settings is opened. The property is the owner; closing only hides it.
        w.isReleasedWhenClosed = false
        w.delegate = self
        window = w

        note.font = .systemFont(ofSize: 11)
        note.textColor = .secondaryLabelColor
        note.preferredMaxLayoutWidth = 420

        for box in [network, sound, shareBox, readOnlyBox] {
            box.target = self
            box.action = #selector(changed(_:))
        }
        let toggles = NSStackView(views: [network, sound])
        toggles.orientation = .horizontal
        toggles.spacing = 20
        let shareToggles = NSStackView(views: [shareBox, readOnlyBox])
        shareToggles.orientation = .horizontal
        shareToggles.spacing = 20

        let rule = NSBox()
        rule.boxType = .separator

        restart.bezelStyle = .rounded
        restart.target = self
        restart.action = #selector(restartNow)
        let done = NSButton(title: "Done", target: self, action: #selector(close))
        done.bezelStyle = .rounded
        done.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let buttons = NSStackView(views: [spacer, restart, done])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let folder = folderRow()
        let column = NSStackView(views: [
            row("Processors", cpuField, cpuStepper, Settings.cpuChoices, ""),
            row("Memory", memoryField, memoryStepper, Settings.memoryChoicesGiB, " GB"),
            toggles, rule, shareToggles, folder, note, buttons,
        ])
        column.orientation = .vertical
        column.alignment = .leading
        column.spacing = 16
        column.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(column)
        NSLayoutConstraint.activate([
            column.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 24),
            column.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -24),
            column.topAnchor.constraint(equalTo: content.topAnchor, constant: 22),
            column.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -18),
            column.widthAnchor.constraint(equalToConstant: 420),
        ])
        for filling: NSView in [buttons, shareToggles, folder, rule, note] {
            filling.widthAnchor.constraint(equalTo: column.widthAnchor).isActive = true
        }
        w.contentView = content
    }

    private func refresh() {
        cpuStepper.integerValue = Settings.cpuCount
        memoryStepper.integerValue = Settings.memoryGiB
        cpuField.stringValue = "\(Settings.cpuCount)"
        memoryField.stringValue = "\(Settings.memoryGiB) GB"
        network.state = Settings.networking ? .on : .off
        sound.state = Settings.sound ? .on : .off
        shareBox.state = Settings.shareEnabled ? .on : .off
        readOnlyBox.state = Settings.shareReadOnly ? .on : .off
        folderPath.stringValue = Settings.shareFolder.path
            .replacingOccurrences(of: NSHomeDirectory(), with: "~")

        // A flag in force makes the setting a lie; say so rather than show a
        // control that changes nothing.
        var flagged: [String] = []
        for (key, control) in [(Settings.cpuKey, cpuStepper), (Settings.memoryKey, memoryStepper)] {
            let flag = Settings.overriddenByFlag(key)
            control.isEnabled = flag == nil
            if let flag { flagged.append(flag) }
        }
        for (key, box) in [(Settings.networkKey, network), (Settings.soundKey, sound),
                           (Settings.shareEnabledKey, shareBox)] {
            let flag = Settings.overriddenByFlag(key)
            box.isEnabled = flag == nil
            if let flag { flagged.append(flag) }
        }
        // The folder only has an effect where the default share does: a run given a
        // disk of its own is a script, and gets exactly the shares it asked for.
        let shareApplies = usingInstalledMachine && Settings.overriddenByFlag(Settings.shareEnabledKey) == nil
        shareBox.isEnabled = shareApplies
        let folderUsable = shareApplies && Settings.shareEnabled
        readOnlyBox.isEnabled = folderUsable
        choose.isEnabled = folderUsable
        folderPath.textColor = folderUsable ? .labelColor : .disabledControlTextColor
        if !usingInstalledMachine {
            folderPath.toolTip = "This run was given a disk on the command line, so it shares "
                + "only what --share asked for."
        }

        let running = controller.vm?.state == .running || controller.vm?.state == .paused
        restart.isEnabled = running
        if !flagged.isEmpty {
            note.stringValue = "Started with \(flagged.joined(separator: ", ")), so those are fixed "
                + "for this run. What you choose here applies when Prose is next opened without them."
        } else if running {
            note.stringValue = "Running on \(cpus) processors and \(memoryGiB) GB. "
                + "Changes take effect the next time the machine starts."
        } else {
            note.stringValue = "Changes take effect the next time the machine starts."
        }
    }

    @objc private func changed(_ sender: NSControl) {
        if sender === cpuStepper { Settings.set(Settings.cpuKey, cpuStepper.integerValue) }
        if sender === memoryStepper { Settings.set(Settings.memoryKey, memoryStepper.integerValue) }
        if sender === network { Settings.set(Settings.networkKey, network.state == .on) }
        if sender === sound { Settings.set(Settings.soundKey, sound.state == .on) }
        if sender === shareBox { Settings.set(Settings.shareEnabledKey, shareBox.state == .on) }
        if sender === readOnlyBox { Settings.set(Settings.shareReadOnlyKey, readOnlyBox.state == .on) }
        refresh()
    }

    @objc private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = Settings.shareFolder
        panel.prompt = "Share"
        panel.message = "Choose the folder the guest mounts as HostFS."
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Settings.set(Settings.shareFolderKey, url.standardizedFileURL.path)
        refresh()
    }

    @objc private func restartNow() {
        guard controller.vm != nil else { return }
        close()
        controller.restart(nil)
    }

    @objc private func close() { window?.close() }
}
