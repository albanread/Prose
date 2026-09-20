// themes.swift: View ▸ Theme — the machine's themes, offered here and applied
// through the portal.
//
// A theme is a window decorator, the system's colours and the wallpaper, and
// the machine keeps it: prosetheme(1) in the guest lists and applies them (see
// docs/decorators.md), and the menu only ever shows what the guest answered.
// The owner choosing from their own machine's menu needs no automation
// permission; that switch is for other applications on this Mac.
import AppKit
import Foundation

enum GuestRun {
    case ok(String)             // stdout
    case failed(String)         // stderr, "exit N", or why the guest could not be asked
}

extension Controller {

    /// The submenu View ▸ Theme shows, from what the guest last answered.
    func rebuildThemeMenu() {
        themeMenu.removeAllItems()
        if themes.isEmpty {
            let answering = portal.alive.withLock { $0 }
            let item = NSMenuItem(title: answering ? "No Themes" : "Machine Not Running",
                                  action: nil, keyEquivalent: "")
            item.isEnabled = false
            themeMenu.addItem(item)
            return
        }
        for name in themes {
            let item = NSMenuItem(title: name, action: #selector(chooseTheme(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = name
            item.state = name == currentTheme ? .on : .off
            themeMenu.addItem(item)
        }
    }

    /// Ask the guest which themes it has and which is current.
    func refreshThemes(_ completion: ((GuestRun) -> Void)? = nil) {
        guestRun("prosetheme --list", timeout: 10) { [weak self] result in
            guard let self else { return }
            if case .ok(let out) = result {
                var names: [String] = []
                var current: String?
                for raw in out.split(separator: "\n") {
                    var line = String(raw)
                    let isCurrent = line.hasPrefix("* ")
                    if isCurrent { line.removeFirst(2) }
                    line = line.trimmingCharacters(in: .whitespaces)
                    guard !line.isEmpty else { continue }
                    names.append(line)
                    if isCurrent { current = line }
                }
                themes = names
                currentTheme = current
                rebuildThemeMenu()
            }
            completion?(result)
        }
    }

    /// Apply a theme by name; the check mark moves when the guest says it did.
    func applyTheme(_ name: String, _ completion: ((GuestRun) -> Void)? = nil) {
        guestRun("prosetheme \"\(name)\"", timeout: 30) { [weak self] result in
            guard let self else { return }
            switch result {
            case .ok(let out):
                currentTheme = name
                rebuildThemeMenu()
                log("theme: \(out.trimmingCharacters(in: .whitespacesAndNewlines))")
            case .failed(let why):
                log("theme: \(name): \(why)")
            }
            completion?(result)
        }
    }

    @objc func chooseTheme(_ sender: Any?) {
        guard let name = (sender as? NSMenuItem)?.representedObject as? String else { return }
        applyTheme(name)
    }

    /// The machine is off or restarting: the menu says so until it answers again.
    func forgetThemes() {
        themes = []
        currentTheme = nil
        rebuildThemeMenu()
    }

    /// A command in the guest through the portal, answered on the main thread.
    func guestRun(_ command: String, timeout: TimeInterval, _ completion: @escaping (GuestRun) -> Void) {
        portal.send(Portal.run, payload: Data(command.utf8), timeout: timeout) { outcome in
            let result: GuestRun
            switch outcome {
            case .success(let reply) where reply.status == 0:
                result = .ok(reply.out)
            case .success(let reply):
                let error = reply.error.trimmingCharacters(in: .whitespacesAndNewlines)
                result = .failed(error.isEmpty ? "exit \(reply.status)" : error)
            case .failure(let failure):
                result = .failed("\(failure)")
            }
            DispatchQueue.main.async { completion(result) }
        }
    }
}
