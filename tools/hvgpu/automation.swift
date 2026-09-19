// automation.swift: the automation core — one place that performs an operation
// and says what happened. docs/automation.md.
//
// Everything that drives Prose from outside goes through `Automation.perform`:
// the AppleScript suite and the prose(1) client (both still to come) and, today,
// --script. Adding a capability here adds it to all of them at once.
//
// Two rules the surfaces rely on. Every operation returns an `AutomationResult`,
// including the ones that read like verbs, so a caller never infers what
// happened from a screenshot. And waiting is an operation, not a sleep: a caller
// says what it is waiting for and how long it will wait, and is told which of
// those two things happened.
//
// It is off until the owner turns it on (Machine ▸ Allow Automation and Testing,
// or --automation for a test run). The guest-side half — running a command in
// the guest, and its own scripting — is a later patch; the operations here need
// nothing from the guest at all.
import AppKit
import Foundation

/// What went wrong, in a form a caller can branch on rather than parse.
enum AutomationError: String {
    case notPermitted = "not permitted"
    case unknownCommand = "unknown command"
    case badArgument = "bad argument"
    case wrongState = "wrong state"
    case noWindow = "no window"
    case noPicture = "no picture"
    case guestNotAnswering = "guest not answering"
    case timedOut = "timed out"
    case failed = "failed"
}

/// The outcome of one operation: readable for a person, structured for a program.
struct AutomationResult {
    let ok: Bool
    let error: AutomationError?
    let detail: String
    let values: [String: Any]

    static func done(_ values: [String: Any] = [:], _ detail: String = "") -> AutomationResult {
        AutomationResult(ok: true, error: nil, detail: detail, values: values)
    }

    static func failed(_ error: AutomationError, _ detail: String = "") -> AutomationResult {
        AutomationResult(ok: false, error: error, detail: detail, values: [:])
    }

    /// One line for a log or a terminal.
    var line: String {
        if !ok { return "error: \(error?.rawValue ?? "failed")\(detail.isEmpty ? "" : " — \(detail)")" }
        let pairs = values.keys.sorted().map { "\($0)=\(describe(values[$0]!))" }.joined(separator: " ")
        return (["ok", detail.isEmpty ? nil : detail, pairs.isEmpty ? nil : pairs]
            .compactMap { $0 }).joined(separator: " ")
    }

    var json: String {
        var object: [String: Any] = ["ok": ok]
        if let error { object["error"] = error.rawValue }
        if !detail.isEmpty { object["detail"] = detail }
        for (k, v) in values { object[k] = v }
        guard let data = try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys]),
              let text = String(data: data, encoding: .utf8) else { return "{\"ok\":false}" }
        return text
    }

    private func describe(_ value: Any) -> String {
        if let list = value as? [Int] { return list.map(String.init).joined(separator: "x") }
        return String(describing: value)
    }
}

// MARK: - keys

/// US layout: a character as an evdev key code, and whether shift is held.
/// Enough to type a path or a command; a caller wanting more should use `key`.
private let typeTable: [Character: (UInt16, Bool)] = {
    var table: [Character: (UInt16, Bool)] = [:]
    let rows: [(String, [UInt16])] = [
        ("abcdefghijklmnopqrstuvwxyz",
         [30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44]),
        ("1234567890", [2, 3, 4, 5, 6, 7, 8, 9, 10, 11]),
        ("-=[];'`\\,./ ", [12, 13, 26, 27, 39, 40, 41, 43, 51, 52, 53, 57]),
    ]
    for (characters, codes) in rows {
        for (character, code) in zip(characters, codes) { table[character] = (code, false) }
    }
    for (character, code) in zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        [30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44] as [UInt16]) {
        table[character] = (code, true)
    }
    for (character, code) in zip("!@#$%^&*()", [2, 3, 4, 5, 6, 7, 8, 9, 10, 11] as [UInt16]) {
        table[character] = (code, true)
    }
    for (character, code) in zip("_+{}:\"~|<>?", [12, 13, 26, 27, 39, 40, 41, 43, 51, 52, 53] as [UInt16]) {
        table[character] = (code, true)
    }
    table["\n"] = (28, false)
    table["\t"] = (15, false)
    return table
}()

/// Keys by name, for `key name=...`.
private let namedKeys: [String: UInt16] = [
    "escape": 1, "esc": 1, "backspace": 14, "tab": 15, "return": 28, "enter": 28,
    "space": 57, "capslock": 58, "delete": 111, "insert": 110,
    "home": 102, "end": 107, "pageup": 104, "pagedown": 109,
    "up": 103, "down": 108, "left": 105, "right": 106,
    "f1": 59, "f2": 60, "f3": 61, "f4": 62, "f5": 63, "f6": 64,
    "f7": 65, "f8": 66, "f9": 67, "f10": 68, "f11": 87, "f12": 88,
]

/// Modifiers as Haiku sees them through our keymap: ⌘ is KEY_LEFTALT, ⌥ is KEY_LEFTMETA.
private let modifierCodes: [String: UInt16] = [
    "shift": 42, "control": 29, "ctrl": 29, "command": 56, "cmd": 56, "option": 125, "alt": 125,
]

// MARK: - the core

final class Automation {
    static let defaultsKey = "prose.automationEnabled"

    /// Off until the owner of the machine says otherwise. The guest-side channel
    /// is not added to the VM's configuration while this is false, so turning it
    /// off removes the path rather than refusing to use it.
    static var enabled: Bool {
        get { args.contains("--automation") || UserDefaults.standard.bool(forKey: defaultsKey) }
        set { UserDefaults.standard.set(newValue, forKey: defaultsKey) }
    }

    /// What the core answers to. A surface asks rather than guessing.
    static let commands: Set<String> = [
        "state", "start", "shut-down", "shutdown", "restart", "force-stop", "run", "info", "ping",
        "pause", "resume", "type", "key", "click", "move", "capture",
        "display-size", "presenter", "full-screen", "wait", "stats",
    ]

    /// A --script step as a command line for the core, or nil when it is one of
    /// the window-level steps the core does not own (toolbar, snapshot, quit).
    static func command(for step: String) -> String? {
        if commands.contains(step.split(separator: " ").first.map(String.init) ?? step) { return step }
        let parts = step.split(separator: "=", maxSplits: 1).map(String.init)
        switch parts[0] {
        case "fullscreen": return "full-screen"
        case "size":
            let wh = parts.count > 1 ? parts[1].split(separator: "x").map(String.init) : []
            return wh.count == 2 ? "display-size width=\(wh[0]) height=\(wh[1])" : nil
        default: return nil
        }
    }

    unowned let controller: Controller
    private var waiters: [Waiter] = []

    // The portal answers over a socket, which must not be touched from the main
    // thread: a waiter asks the cached answer, a background poll keeps it fresh.
    private let portalQueue = DispatchQueue(label: "hvgpu.portal")
    private let portalLock = NSLock()
    private var portalInfo: [String: String] = [:]
    private var portalAnswered = false
    private var portalAddress: String?
    private var polling = false

    init(controller: Controller) { self.controller = controller }

    /// "command key=value key=value" — the form --script and prose(1) both use.
    func perform(_ line: String, completion: @escaping (AutomationResult) -> Void) {
        let (command, args) = Automation.parse(line)
        guard !command.isEmpty else { return completion(.failed(.unknownCommand, "empty")) }
        perform(command, args, completion: completion)
    }

    /// `command key=value key="two words" key=the rest of the line`.
    ///
    /// A quoted value keeps its spaces. An unquoted one runs to the next
    /// `word=`, so `type text=hello world` types both words: text is the common
    /// case and having to quote it every time would be a trap.
    static func parse(_ line: String) -> (String, [String: String]) {
        var fields: [String] = []
        var current = "", quote: Character? = nil
        for character in line {
            if let q = quote {
                if character == q { quote = nil } else { current.append(character) }
            } else if character == "\"" || character == "'" {
                quote = character
            } else if character == " " {
                if !current.isEmpty { fields.append(current); current = "" }
            } else {
                current.append(character)
            }
        }
        if !current.isEmpty { fields.append(current) }
        guard !fields.isEmpty else { return ("", [:]) }
        let command = fields.removeFirst()
        var args: [String: String] = [:]
        var key: String? = nil
        for field in fields {
            if let index = field.firstIndex(of: "="), !field.hasPrefix("=") {
                key = String(field[field.startIndex..<index])
                args[key!] = String(field[field.index(after: index)...])
            } else if let key {
                args[key] = (args[key].map { $0 + " " } ?? "") + field      // the rest of the line
            } else {
                args[field] = "yes"
            }
        }
        return (command, args)
    }

    func perform(_ command: String, _ args: [String: String],
                 completion: @escaping (AutomationResult) -> Void) {
        guard Automation.enabled else {
            return completion(.failed(.notPermitted,
                "automation is off: Machine ▸ Allow Automation and Testing"))
        }
        // AppKit and the VM are main-thread things; a surface may call from anywhere
        guard Thread.isMainThread else {
            return DispatchQueue.main.async { self.perform(command, args, completion: completion) }
        }
        if let result = run(command, args, completion) { completion(result) }
    }

    // MARK: operations

    /// nil means the operation has not finished: it will call `completion` itself.
    private func run(_ command: String, _ args: [String: String],
                     _ completion: @escaping (AutomationResult) -> Void) -> AutomationResult? {
        let c = controller
        switch command {

        case "state":
            return .done(stateValues())

        case "start":
            guard c.vm == nil || c.vm.state == .stopped || c.vm.state == .error else {
                return .failed(.wrongState, "already \(stateName)")
            }
            c.startVM(nil)
            return .done(["state": stateName])

        case "shut-down", "shutdown":
            guard running else { return .failed(.wrongState, stateName) }
            c.shutDown(nil)
            return .done()

        case "restart":
            guard running else { return .failed(.wrongState, stateName) }
            c.restart(nil)
            return .done()

        case "force-stop":
            c.forceStop(nil)
            return .done()

        case "pause":
            guard c.vm?.state == .running else { return .failed(.wrongState, stateName) }
            c.togglePause(nil)
            return .done()

        case "resume":
            guard c.vm?.state == .paused else { return .failed(.wrongState, stateName) }
            c.togglePause(nil)
            return .done()

        case "type":
            guard let text = args["text"], !text.isEmpty else {
                return .failed(.badArgument, "type text=…")
            }
            return type(text)

        case "key":
            return key(args)

        case "click", "move":
            guard let x = args["x"].flatMap(Double.init), let y = args["y"].flatMap(Double.init) else {
                return .failed(.badArgument, "\(command) x=… y=… (guest pixels)")
            }
            return pointer(x: x, y: y, click: command == "click", button: args["button"] ?? "left")

        case "capture":
            guard let path = args["path"] else { return .failed(.badArgument, "capture path=…") }
            return capture(to: path)

        case "display-size":
            if let w = args["width"].flatMap(Double.init), let h = args["height"].flatMap(Double.init) {
                guard displayMode == "s2" else { return .failed(.failed, "the S1 display has a fixed size") }
                let before = sizeValue()
                c.resizeDisplay(to: NSSize(width: w, height: h))
                // The guest follows the window, but not at once: its agent waits
                // for the drag to settle before switching mode. Report the size
                // actually applied, which is often not the one asked for -- macOS
                // constrains a window to the screen it is on.
                let deadline = Date().addingTimeInterval(Double(args["timeout"] ?? "") ?? 5)
                watch(until: deadline, met: { [weak self] in
                    let now = self?.sizeValue() ?? []
                    return !now.isEmpty && now != before
                }, describe: "the guest to follow") { [weak self] result in
                    completion(.done(["asked": [Int(w), Int(h)], "size": self?.sizeValue() ?? [],
                                      "followed": result.ok]))
                }
                return nil
            }
            return .done(["size": sizeValue()])

        case "presenter":
            if let name = args["mode"] {
                guard let mode = PresenterMode(rawValue: name) else {
                    return .failed(.badArgument, "presenter mode=native|crisp|smooth")
                }
                PresenterMode.current = mode
                UserDefaults.standard.set(mode.rawValue, forKey: PresenterMode.defaultsKey)
                c.chrome?.content.reannounceDisplaySize()
            }
            return .done(["mode": PresenterMode.current.rawValue])

        case "full-screen":
            guard let window = c.presenter.window else { return .failed(.noWindow, "headless") }
            let want = args["on"].map { $0 != "no" && $0 != "off" } ?? true
            if want != window.styleMask.contains(.fullScreen) { c.toggleFullScreen(nil) }
            return .done(["full-screen": want])

        case "run":
            guard let command = args["command"] ?? args["text"], !command.isEmpty else {
                return .failed(.badArgument, "run command=…")
            }
            portal(completion) { client in try client.send(Portal.run, payload: Data(command.utf8)) } handle: { reply in
                // the command's own exit status, kept apart from "the portal
                // could not be reached", which is an error and not a status
                .done(["status": Int(reply.status), "stdout": reply.out, "stderr": reply.error],
                      reply.status == 0 ? "" : "exit \(reply.status)")
            }
            return nil

        case "info":
            portal(completion) { client in try client.send(Portal.info) } handle: { reply in
                var values: [String: Any] = [:]
                for line in reply.text.split(separator: "\n") {
                    let pair = line.split(separator: "=", maxSplits: 1)
                    if pair.count == 2 { values[String(pair[0])] = String(pair[1]) }
                }
                return .done(values)
            }
            return nil

        case "ping":
            portal(completion) { client in try client.send(Portal.ping) } handle: { _ in .done() }
            return nil

        case "wait":
            return wait(args, completion)

        case "stats":
            return .done([:], c.statsLine())

        default:
            return .failed(.unknownCommand, command)
        }
    }

    // MARK: input

    private func type(_ text: String) -> AutomationResult {
        let router = controller.router
        guard router.enabled else {
            return .failed(.failed, "no input device (--input vz)")
        }
        var sent = 0, skipped: [Character] = []
        for character in text {
            guard let (code, shift) = typeTable[character] else {
                skipped.append(character)
                continue
            }
            if shift { router.chord([42, code]) } else { router.tap(evdev: code) }
            sent += 1
        }
        if sent == 0 && !skipped.isEmpty {
            return .failed(.badArgument, "nothing in “\(text)” can be typed on a US layout")
        }
        var values: [String: Any] = ["typed": sent]
        if !skipped.isEmpty { values["skipped"] = String(skipped) }
        return .done(values)
    }

    private func key(_ args: [String: String]) -> AutomationResult {
        let router = controller.router
        guard router.enabled else {
            return .failed(.failed, "no input device (--input vz)")
        }
        var code: UInt16
        if let name = args["name"]?.lowercased() {
            guard let found = namedKeys[name] ?? typeTable[Character(name)]?.0 else {
                return .failed(.badArgument, "no key called “\(name)”")
            }
            code = found
        } else if let raw = args["code"].flatMap(UInt16.init) {
            code = raw
        } else {
            return .failed(.badArgument, "key name=… or key code=…")
        }
        let modifiers = (args["modifiers"] ?? "").split(whereSeparator: { $0 == "+" || $0 == "," })
            .map { modifierCodes[$0.lowercased()] }
        if modifiers.contains(where: { $0 == nil }) {
            return .failed(.badArgument, "modifiers=shift+control+command+option")
        }
        let codes = modifiers.compactMap { $0 } + [code]
        if codes.count == 1 { router.tap(evdev: code) } else { router.chord(codes) }
        return .done(["code": Int(code), "modifiers": codes.count - 1])
    }

    private func pointer(x: Double, y: Double, click: Bool, button: String) -> AutomationResult {
        let router = controller.router
        guard router.enabled else {
            return .failed(.failed, "no input device (--input vz)")
        }
        guard let size = controller.displaySize else { return .failed(.noPicture) }
        guard x >= 0, y >= 0, x < Double(size.width), y < Double(size.height) else {
            return .failed(.badArgument, "outside the guest's \(size.width)x\(size.height) screen")
        }
        // the tablet is absolute over 0...EV.absMax, whatever the guest's mode
        router.move(to: Int32(x / Double(size.width) * Double(EV.absMax)),
                    Int32(y / Double(size.height) * Double(EV.absMax)))
        if click {
            let code: UInt16 = button == "right" ? EV.btnRight : button == "middle" ? EV.btnMiddle : EV.btnLeft
            router.click(code)
        }
        return .done(["x": Int(x), "y": Int(y)])
    }

    private func capture(to path: String) -> AutomationResult {
        guard let surface = presenterGPU?.surface, controller.displaySize != nil else {
            return .failed(.noPicture, "the guest has not drawn anything yet")
        }
        let url = URL(fileURLWithPath: (path as NSString).expandingTildeInPath)
        guard let image = controller.surfaceImage(surface), controller.writePNG(image, to: url) else {
            return .failed(.failed, "could not write \(url.path)")
        }
        return .done(["path": url.path, "size": sizeValue()])
    }

    // MARK: the portal

    /// Run a portal request off the main thread and answer on it.
    private func portal(_ completion: @escaping (AutomationResult) -> Void,
                        _ request: @escaping (PortalClient) throws -> PortalReply,
                        handle: @escaping (PortalReply) -> AutomationResult) {
        guard let address = controller.monitor?.guestIP() else {
            return completion(.failed(.guestNotAnswering, "the guest has no address yet"))
        }
        portalQueue.async {
            let client = PortalClient(address: address)
            let result: AutomationResult
            do {
                result = handle(try request(client))
            } catch PortalClient.Failure.guestError(let message) {
                result = .failed(.failed, message)
            } catch {
                result = .failed(.guestNotAnswering, "\(address): \(error)")
            }
            DispatchQueue.main.async { completion(result) }
        }
    }

    /// Keep a cached answer from the guest, so a waiter's question is cheap.
    /// Started by the first waiter that needs it and stopped when none do.
    private func pollPortal() {
        guard !polling else { return }
        polling = true
        portalQueue.async { [weak self] in
            while true {
                guard let self else { return }
                self.portalLock.lock()
                let address = self.portalAddress
                self.portalLock.unlock()
                if let address {
                    let client = PortalClient(address: address, timeout: 2)
                    if let reply = try? client.send(Portal.info) {
                        var info: [String: String] = [:]
                        for line in reply.text.split(separator: "\n") {
                            let pair = line.split(separator: "=", maxSplits: 1)
                            if pair.count == 2 { info[String(pair[0])] = String(pair[1]) }
                        }
                        self.portalLock.lock()
                        self.portalInfo = info
                        self.portalAnswered = true
                        self.portalLock.unlock()
                    }
                }
                Thread.sleep(forTimeInterval: 1)
                var wanted = false
                DispatchQueue.main.sync { wanted = !(self.waiters.isEmpty) }
                if !wanted {
                    DispatchQueue.main.async { self.polling = false }
                    return
                }
            }
        }
    }

    private func portalSays(_ key: String, is value: String) -> Bool {
        portalLock.lock()
        defer { portalLock.unlock() }
        return portalInfo[key] == value
    }

    private var portalIsAnswering: Bool {
        portalLock.lock()
        defer { portalLock.unlock() }
        return portalAnswered
    }

    // MARK: waiting

    /// A condition being watched, with the deadline it must be met by.
    private final class Waiter {
        let met: () -> Bool
        let deadline: Date
        let describe: String
        let started = Date()
        let completion: (AutomationResult) -> Void
        init(describe: String, deadline: Date, met: @escaping () -> Bool,
             completion: @escaping (AutomationResult) -> Void) {
            self.describe = describe; self.deadline = deadline
            self.met = met; self.completion = completion
        }
    }

    private func wait(_ args: [String: String],
                      _ completion: @escaping (AutomationResult) -> Void) -> AutomationResult? {
        let what = args["for"] ?? "booted"
        let timeout = Double(args["timeout"] ?? "") ?? 120
        let settle = Double(args["seconds"] ?? "") ?? 2
        let deadline = Date().addingTimeInterval(timeout)
        var met: () -> Bool

        switch what {
        case "state":
            guard let want = args["value"] else { return .failed(.badArgument, "wait for=state value=…") }
            met = { [weak self] in self?.stateName == want }
        case "picture":
            met = { [weak self] in self?.controller.displaySize != nil }
        case "address":
            met = { [weak self] in self?.controller.monitor?.guestIP() != nil }
        case "idle":
            let idle = IdleWatch(seconds: settle, bytes: Int(args["bytes"] ?? "") ?? 256 << 10)
            met = { idle.settled() }
        case "guest":
            pollPortal()
            met = { [weak self] in self?.portalIsAnswering ?? false }

        case "desktop":
            // The guest is asked rather than guessed at: Tracker and the Deskbar
            // are either running or they are not.
            pollPortal()
            met = { [weak self] in self?.portalSays("desktop", is: "yes") ?? false }

        case "drawn":
            // As close to "ready" as the host can see without the guest-side
            // agent, and the settling is what does the work. The other two
            // conditions are necessary but nowhere near sufficient:
            //
            //   - a picture arrives at ~2.4 s, when the kernel driver sets the
            //     mode and the boot splash draws, twenty-odd seconds before
            //     there is a desktop;
            //   - the address is read from the host's DHCP lease file, which
            //     persists per MAC, so on a --keep run it is known before the
            //     guest has asked for it.
            //
            // Drawing tells the truth: ~51 MB committed in the first ten
            // seconds of a boot, then a few KiB per ten seconds once the
            // desktop is up.
            // 4 MiB is "a desktop was painted" -- one full frame at 2560x1600 is
            // about sixteen -- and then it has to go quiet for `seconds`.
            let settled = IdleWatch(seconds: Double(args["seconds"] ?? "") ?? 3,
                                    bytes: Int(args["bytes"] ?? "") ?? 256 << 10,
                                    after: Int(args["after"] ?? "") ?? 4 << 20)
            met = { [weak self] in
                guard let self, self.controller.displaySize != nil,
                      self.controller.monitor?.guestIP() != nil else { return false }
                return settled.settled()
            }
        case "booted":
            // What a caller means by "booted" is that there is a desktop, and
            // the guest is the only thing that knows. `drawn` keeps the old
            // host-side guess for a guest with no portal.
            pollPortal()
            met = { [weak self] in self?.portalSays("desktop", is: "yes") ?? false }

        default:
            return .failed(.badArgument,
                "wait for=booted|desktop|guest|picture|address|idle|drawn|state")
        }

        if met() { return .done(["waited": 0.0, "for": what], "already \(what)") }
        watch(until: deadline, met: met, describe: what, completion: completion)
        return nil          // answered when the condition is met, or when time runs out
    }

    /// Less than `bytes` drawn in `seconds`: the screen has settled.
    ///
    /// Measured in bytes copied, not in commits. An idle Haiku desktop commits
    /// about seventy times a second -- the Deskbar's CPU meter is eight 2x1
    /// rectangles -- so "no commits" never arrives and a caller waits for ever.
    /// The area of those commits is a few hundred bytes.
    private final class IdleWatch {
        private let seconds: Double, bytes: Int, after: Int
        private var mark = -1               // nothing looked at yet
        private var start = 0
        private var since = Date()
        /// `after` bytes must be drawn before quiet counts as settled, so that
        /// "nothing has happened yet" is not mistaken for "it has finished".
        init(seconds: Double, bytes: Int, after: Int = 0) {
            self.seconds = seconds; self.bytes = bytes; self.after = after
        }
        func settled() -> Bool {
            let drawn = presenterGPU?.committedBytes ?? 0
            if mark < 0 {                   // the clock starts at the first look,
                mark = drawn                // not when this object was made: the
                start = drawn               // caller may have been waiting on
                since = Date()              // something else until now
                return false
            }
            if drawn - mark > bytes { mark = drawn; since = Date(); return false }
            guard drawn - start >= after else { return false }
            return Date().timeIntervalSince(since) >= seconds
        }
    }

    /// Call `completion` when `met` becomes true, or when the deadline passes.
    private func watch(until deadline: Date, met: @escaping () -> Bool, describe: String,
                       completion: @escaping (AutomationResult) -> Void) {
        waiters.append(Waiter(describe: describe, deadline: deadline, met: met, completion: completion))
        if waiters.count == 1 { tick() }
    }

    private func tick() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
            guard let self, !self.waiters.isEmpty else { return }
            self.portalLock.lock()
            self.portalAddress = self.controller.monitor?.guestIP()
            self.portalLock.unlock()
            let now = Date()
            var remaining: [Waiter] = []
            for waiter in self.waiters {
                if waiter.met() {
                    waiter.completion(.done(["waited": round(now.timeIntervalSince(waiter.started) * 10) / 10,
                                             "for": waiter.describe]))
                } else if now >= waiter.deadline {
                    waiter.completion(.failed(.timedOut, "waiting for \(waiter.describe)"))
                } else {
                    remaining.append(waiter)
                }
            }
            self.waiters = remaining
            if !self.waiters.isEmpty { self.tick() }
        }
    }

    // MARK: state

    private var running: Bool { controller.vm?.state == .running || controller.vm?.state == .paused }

    private var stateName: String {
        switch controller.vm?.state {
        case .some(.running): return "running"
        case .some(.paused): return "paused"
        case .some(.starting): return "starting"
        case .some(.stopping): return "stopping"
        case .some(.stopped), .none: return "off"
        case .some(.error): return "error"
        default: return "unknown"
        }
    }

    private func sizeValue() -> [Int] {
        guard let size = controller.displaySize else { return [] }
        return [size.width, size.height]
    }

    private func stateValues() -> [String: Any] {
        var values: [String: Any] = [
            "state": stateName,
            "presenter": PresenterMode.current.rawValue,
            "picture": controller.displaySize != nil,
            "display": displayMode,
        ]
        let size = sizeValue()
        if !size.isEmpty { values["size"] = size }
        if let ip = controller.monitor?.guestIP() { values["address"] = ip }
        return values
    }
}
