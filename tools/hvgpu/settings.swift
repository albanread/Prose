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

    static func set(_ key: String, _ value: Int) { UserDefaults.standard.set(value, forKey: key) }
    static func set(_ key: String, _ value: Bool) { UserDefaults.standard.set(value, forKey: key) }

    /// A flag in force means the window cannot honestly offer that choice.
    static func overriddenByFlag(_ key: String) -> String? {
        switch key {
        case cpuKey: return option("--cpus") != nil ? "--cpus" : nil
        case memoryKey: return option("--memory") != nil ? "--memory" : nil
        case networkKey: return args.contains("--no-net") ? "--no-net" : nil
        case soundKey: return args.contains("--no-sound") ? "--no-sound" : nil
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

    private func build() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 420, height: 240),
                         styleMask: [.titled, .closable], backing: .buffered, defer: false)
        w.title = "Prose Settings"
        w.delegate = self
        window = w

        note.font = .systemFont(ofSize: 11)
        note.textColor = .secondaryLabelColor
        note.preferredMaxLayoutWidth = 372

        for box in [network, sound] {
            box.target = self
            box.action = #selector(changed(_:))
        }
        let toggles = NSStackView(views: [network, sound])
        toggles.orientation = .horizontal
        toggles.spacing = 20

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

        let column = NSStackView(views: [
            row("Processors", cpuField, cpuStepper, Settings.cpuChoices, ""),
            row("Memory", memoryField, memoryStepper, Settings.memoryChoicesGiB, " GB"),
            toggles, note, buttons,
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
            column.widthAnchor.constraint(equalToConstant: 372),
        ])
        buttons.widthAnchor.constraint(equalTo: column.widthAnchor).isActive = true
        note.widthAnchor.constraint(equalTo: column.widthAnchor).isActive = true
        w.contentView = content
    }

    private func refresh() {
        cpuStepper.integerValue = Settings.cpuCount
        memoryStepper.integerValue = Settings.memoryGiB
        cpuField.stringValue = "\(Settings.cpuCount)"
        memoryField.stringValue = "\(Settings.memoryGiB) GB"
        network.state = Settings.networking ? .on : .off
        sound.state = Settings.sound ? .on : .off

        // A flag in force makes the setting a lie; say so rather than show a
        // control that changes nothing.
        var flagged: [String] = []
        for (key, control) in [(Settings.cpuKey, cpuStepper), (Settings.memoryKey, memoryStepper)] {
            let flag = Settings.overriddenByFlag(key)
            control.isEnabled = flag == nil
            if let flag { flagged.append(flag) }
        }
        for (key, box) in [(Settings.networkKey, network), (Settings.soundKey, sound)] {
            let flag = Settings.overriddenByFlag(key)
            box.isEnabled = flag == nil
            if let flag { flagged.append(flag) }
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
        refresh()
    }

    @objc private func restartNow() {
        guard controller.vm != nil else { return }
        close()
        controller.restart(nil)
    }

    @objc private func close() { window?.close() }
}
