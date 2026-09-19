// menus.swift: the Prose app's menu bar.
//
// The guest owns the keyboard: ⌘ is Prose's own Command key, so ⌘Q, ⌘W and the
// rest go to Prose, as they would on a Prose machine. The host's shortcuts are
// ⌃⌘ chords instead (MetalView offers those to this menu first). ⌃⌘Q locks the
// Mac's screen, so Quit has none: closing the window shuts Prose down anyway.
import AppKit

private let host: NSEvent.ModifierFlags = [.control, .command]

/// Screen modes offered by View ▸ Screen Mode: the guest's resolution, in its own
/// pixels. The S2 display follows the window, so choosing one resizes the window
/// to suit, through whatever View ▸ Scale is set to. The ceiling is the device's
/// 3840×2160 (prds.swift).
let displaySizePresets: [(width: Int, height: Int)] = [
    (1024, 768), (1280, 800), (1280, 1024), (1440, 900), (1680, 1050),
    (1920, 1080), (1920, 1200),
    (2048, 1280), (2560, 1440), (2560, 1600), (2880, 1800),
    (3024, 1890), (3456, 2160), (3840, 2160),
]

func makeMainMenu(_ controller: Controller) -> NSMenu {
    let main = NSMenu()

    func menu(_ title: String) -> NSMenu {
        let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        let submenu = NSMenu(title: title)
        item.submenu = submenu
        main.addItem(item)
        return submenu
    }

    @discardableResult
    func add(_ menu: NSMenu, _ title: String, _ action: Selector?, _ key: String = "",
             _ modifiers: NSEvent.ModifierFlags = host, target: AnyObject? = controller) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        // without a key the mask only matters to alternates: ⌥ alone reveals them
        item.keyEquivalentModifierMask = key.isEmpty ? [] : modifiers
        item.target = target
        menu.addItem(item)
        return item
    }

    /// The ⌥ variant of the item above it (same key, ⌥ added).
    func alternate(_ menu: NSMenu, _ title: String, _ action: Selector, _ key: String = "") {
        let above = menu.items.last!
        let item = add(menu, title, action, key, above.keyEquivalentModifierMask.union(.option))
        if key.isEmpty { item.keyEquivalentModifierMask = .option }
        item.isAlternate = true
    }

    // Prose
    let app = menu("Prose")
    add(app, "About Prose", #selector(Controller.showAbout(_:)))
    app.addItem(.separator())
    add(app, "Settings…", #selector(Controller.showSettings(_:)), ",", [.command])
    app.addItem(.separator())
    let services = NSMenuItem(title: "Services", action: nil, keyEquivalent: "")
    services.submenu = NSMenu(title: "Services")
    NSApp.servicesMenu = services.submenu
    app.addItem(services)
    app.addItem(.separator())
    add(app, "Hide Prose", #selector(NSApplication.hide(_:)), "h", target: nil)
    add(app, "Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.control, .option, .command], target: nil)
    add(app, "Show All", #selector(NSApplication.unhideAllApplications(_:)), target: nil)
    app.addItem(.separator())
    add(app, "Quit Prose", #selector(NSApplication.terminate(_:)), target: nil)

    // Machine
    let machine = menu("Machine")
    add(machine, "Start", #selector(Controller.startVM(_:)))
    add(machine, "Shut Down", #selector(Controller.shutDown(_:)))
    alternate(machine, "Force Stop", #selector(Controller.forceStop(_:)))
    add(machine, "Restart", #selector(Controller.restart(_:)), "r")
    alternate(machine, "Force Restart", #selector(Controller.forceRestart(_:)), "r")
    add(machine, "Pause", #selector(Controller.togglePause(_:)), "p")
    machine.addItem(.separator())
    let keys = NSMenuItem(title: "Send Keys", action: nil, keyEquivalent: "")
    keys.submenu = controller.makeSendKeysMenu()
    machine.addItem(keys)
    machine.addItem(.separator())
    add(machine, "Take Screenshot", #selector(Controller.takeScreenshot(_:)), "s")
    add(machine, "Open Guest Log", #selector(Controller.openGuestLog(_:)), "l")
    machine.addItem(.separator())
    let automation = add(machine, "Allow Automation and Testing",
                         #selector(Controller.toggleAutomation(_:)))
    automation.state = Automation.enabled ? .on : .off
    automation.toolTip = "Let other applications on this Mac start and stop this machine, "
        + "send it keyboard and pointer input, capture its screen, and run commands inside it. "
        + "macOS asks before each application may do so."

    // View
    let view = menu("View")
    add(view, "Show Toolbar", #selector(NSWindow.toggleToolbarShown(_:)), "t", target: nil)
    add(view, "Customize Toolbar…", #selector(NSWindow.runToolbarCustomizationPalette(_:)), target: nil)
    add(view, "Show Status Bar", #selector(Controller.toggleStatusBar(_:)), "/")
    view.addItem(.separator())
    add(view, "Actual Size", #selector(Controller.actualSize(_:)), "0")

    // what the guest thinks its screen is
    let sizes = NSMenuItem(title: "Screen Mode", action: nil, keyEquivalent: "")
    sizes.submenu = NSMenu(title: "Screen Mode")
    for (i, size) in displaySizePresets.enumerated() {
        let item = add(sizes.submenu!, "\(size.width) × \(size.height)",
                       #selector(Controller.setDisplaySize(_:)))
        item.tag = i
        item.toolTip = "Set the machine's screen to \(size.width) × \(size.height) pixels."
    }
    view.addItem(sizes)

    // ... and how big one of its pixels is here
    let scales = NSMenuItem(title: "Scale", action: nil, keyEquivalent: "")
    scales.submenu = NSMenu(title: "Scale")
    for (i, scale) in PresenterScale.allCases.enumerated() {
        let item = add(scales.submenu!, scale.title, #selector(Controller.setPresenterScale(_:)))
        item.tag = i
        item.toolTip = scale.detail
        item.state = scale == PresenterScale.current ? .on : .off
    }
    view.addItem(scales)
    let presenter = NSMenuItem(title: "Presenter", action: nil, keyEquivalent: "")
    presenter.submenu = NSMenu(title: "Presenter")
    for (i, mode) in PresenterMode.allCases.enumerated() {
        let item = add(presenter.submenu!, mode.title, #selector(Controller.setPresenterMode(_:)))
        item.tag = i
        item.toolTip = mode.detail
        item.state = mode == PresenterMode.current ? .on : .off
    }
    view.addItem(presenter)
    view.addItem(.separator())
    // the Mac side of HostFS: what the machine sees as its HostFS volume
    add(view, "Host Files in Finder", #selector(Controller.revealHostFiles(_:)))
    view.addItem(.separator())
    add(view, "Enter Full Screen", #selector(Controller.toggleFullScreen(_:)), "f")

    // Window
    let window = menu("Window")
    add(window, "Minimize", #selector(NSWindow.performMiniaturize(_:)), "m", target: nil)
    add(window, "Zoom", #selector(NSWindow.performZoom(_:)), target: nil)
    window.addItem(.separator())
    add(window, "Bring All to Front", #selector(NSApplication.arrangeInFront(_:)), target: nil)
    NSApp.windowsMenu = window

    // Help
    let help = menu("Help")
    add(help, "Keyboard Shortcuts", #selector(Controller.showKeyboardHelp(_:)), "?")
    NSApp.helpMenu = help

    return main
}

extension Controller {
    /// Keys a Mac keyboard can't type, for the Machine menu and the toolbar.
    func makeSendKeysMenu() -> NSMenu {
        let menu = NSMenu(title: "Send Keys")
        for (title, action) in [("Control-Alt-Delete", #selector(Controller.sendControlAltDelete(_:))),
                                ("Print Screen", #selector(Controller.sendPrintScreen(_:)))] {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
            item.target = self
            menu.addItem(item)
        }
        return menu
    }

    func applicationDockMenu(_ sender: NSApplication) -> NSMenu? {
        let menu = NSMenu()
        for (title, action) in [("Pause", #selector(Controller.togglePause(_:))),
                                ("Restart", #selector(Controller.restart(_:))),
                                ("Shut Down", #selector(Controller.shutDown(_:))),
                                ("Start", #selector(Controller.startVM(_:))),
                                ("Take Screenshot", #selector(Controller.takeScreenshot(_:)))] {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
            item.target = self
            menu.addItem(item)
        }
        return menu
    }
}
