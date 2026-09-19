// applescript.swift: the scripting suite declared in Prose.sdef.
//
// One command class serves every verb: the dictionary names the vocabulary and
// each verb is one call into the automation core, so the two cannot drift
// apart. Adding a verb is an sdef entry and a line in `coreCommand`.
//
// The permission is macOS's own. An application sending these events is asked
// about once, by name, in Privacy & Security > Automation, and Prose refuses
// everything until its owner allows automation in the Machine menu.
import AppKit
import Foundation

// MARK: - properties of the application object

extension NSApplication {
    private var prose: Controller? { NSApp.delegate as? Controller }

    @objc var proseState: String {
        guard let vm = prose?.vm else { return "off" }
        switch vm.state {
        case .running: return "running"
        case .paused: return "paused"
        case .starting: return "starting"
        case .stopping: return "stopping"
        case .error: return "error"
        default: return "off"
        }
    }

    @objc var proseDisplaySize: [Int] {
        guard let size = prose?.displaySize else { return [] }
        return [size.width, size.height]
    }

    @objc var prosePresenterMode: String {
        get { PresenterMode.current.rawValue }
        set {
            guard let mode = PresenterMode(rawValue: newValue) else { return }
            PresenterMode.current = mode
            UserDefaults.standard.set(mode.rawValue, forKey: PresenterMode.defaultsKey)
            prose?.chrome?.content.reannounceDisplaySize()
        }
    }

    @objc var proseGuestAddress: String { prose?.monitor?.guestIP() ?? "" }
    @objc var proseAutomationAllowed: Bool { Automation.enabled }
}

// MARK: - the verbs

/// Every command in the suite. The core does the work and says what happened;
/// this turns that into an AppleScript result or an AppleScript error.
@objc(ProseCommand)
final class ProseCommand: NSScriptCommand {

    /// The sdef's verb as the core's command name.
    private static let coreCommand: [String: String] = [
        "start": "start", "shut down": "shut-down", "restart": "restart",
        "force stop": "force-stop", "pause": "pause", "resume": "resume",
        "type": "type", "press": "key", "click": "click", "capture": "capture",
        "execute": "run", "guest info": "info", "ping": "ping", "wait for": "wait",
    ]

    /// Nothing in this program refers to this class -- the dictionary names it in
    /// a string -- so without this the optimiser has no reason to keep it, and
    /// Cocoa's lookup by name quietly falls back to NSScriptCommand, whose
    /// default implementation returns nothing. An empty result with no error is
    /// what that looks like from a script.
    static func register() { _ = ProseCommand.self }

    override func performDefaultImplementation() -> Any? {
        log("applescript: \(commandDescription.commandName)")
        let name = commandDescription.commandName
        guard let command = ProseCommand.coreCommand[name] else {
            return fail("Prose does not know how to \(name)")
        }
        guard let controller = NSApp.delegate as? Controller else { return fail("Prose is not ready") }

        var args = arguments(for: command)
        if let direct = directParameter { addDirect(direct, to: &args, for: command) }

        // AppleScript waits: the script is suspended and resumed with the answer,
        // so `execute` and `wait for` can take as long as they need without
        // blocking the machine's own window.
        suspendExecution()
        controller.automation.perform(command, args) { [weak self] result in
            guard let self else { return }
            guard result.ok else {
                self.scriptErrorNumber = errAEEventFailed
                self.scriptErrorString = result.error.map {
                    result.detail.isEmpty ? $0.rawValue : "\($0.rawValue): \(result.detail)"
                } ?? result.detail
                self.resumeExecution(withResult: nil)
                return
            }
            self.resumeExecution(withResult: self.value(of: result, for: command))
        }
        return nil
    }

    /// `execute` answers a record so a caller can see the exit status apart from
    /// the output; `guest info` answers what the guest said; the rest answer a line.
    private func value(of result: AutomationResult, for command: String) -> Any {
        switch command {
        case "run":
            return ["status": result.values["status"] ?? 0,
                    "stdout": result.values["stdout"] ?? "",
                    "stderr": result.values["stderr"] ?? ""]
        case "info":
            return result.values
        default:
            return result.line
        }
    }

    private func arguments(for command: String) -> [String: String] {
        var args: [String: String] = [:]
        for (key, value) in evaluatedArguments ?? [:] {
            if let url = value as? URL {
                args[key] = url.path
            } else if let list = value as? [Any], list.count == 2 {
                args["x"] = "\(list[0])"
                args["y"] = "\(list[1])"
            } else {
                args[key] = "\(value)"
            }
        }
        return args
    }

    private func addDirect(_ direct: Any, to args: inout [String: String], for command: String) {
        if let list = direct as? [Any], list.count == 2 {
            args["x"] = "\(list[0])"
            args["y"] = "\(list[1])"
            return
        }
        let text = "\(direct)"
        switch command {
        case "type": args["text"] = text
        case "key": args["name"] = text
        case "run": args["command"] = text
        case "wait": args["for"] = text
        default: args["value"] = text
        }
    }

    private func fail(_ message: String) -> Any? {
        scriptErrorNumber = errAEEventNotHandled
        scriptErrorString = message
        return nil
    }
}
