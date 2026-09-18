// hvgpu: our own virtio-input devices (virtio device ID 18), a keyboard and an
// absolute tablet, fed from the events our own window receives. With them VZ's
// graphics device, USB keyboard and pointing device are not needed at all: the
// window is entirely ours and pointer coordinates are mapped from the geometry
// of the image we present, so they are exact at any size.
//
// Haiku's stock virtio_input driver reads the device configuration with the
// select/subsel protocol, which needs configuration writes; VZ never delivers
// those to a custom device. Our fork's driver (patch 0019) instead reads a static
// table of answers placed after the standard 136-byte configuration, marked by
// "PRIN1" in the reserved bytes. Everything else is standard virtio-input, so
// the same guest works unchanged on QEMU's virtio-keyboard/tablet.
import AppKit
import Foundation
import Virtualization

enum EV {
    static let syn: UInt16 = 0, key: UInt16 = 1, rel: UInt16 = 2, abs: UInt16 = 3, rep: UInt16 = 4
    static let absX: UInt16 = 0, absY: UInt16 = 1
    static let relHWheel: UInt16 = 6, relWheel: UInt16 = 8
    static let btnLeft: UInt16 = 0x110, btnRight: UInt16 = 0x111, btnMiddle: UInt16 = 0x112
    static let absMax = 32767          // Haiku's add-on divides by 32768
    // virtio-input config selectors
    static let cfgIdName: UInt8 = 0x01, cfgIdDevids: UInt8 = 0x03, cfgEvBits: UInt8 = 0x11, cfgAbsInfo: UInt8 = 0x12
}

typealias InputEvent = (type: UInt16, code: UInt16, value: Int32)

/// macOS virtual key codes (ANSI layout, positional) -> Linux evdev key codes.
/// ⌘ becomes KEY_LEFTALT/KEY_RIGHTALT (Haiku's Command) and ⌥ KEY_LEFTMETA/RIGHTMETA
/// (Haiku's Option), which is what a Mac user expects from Haiku's shortcuts.
let macToEvdev: [UInt16: UInt16] = [
    0x00: 30, 0x01: 31, 0x02: 32, 0x03: 33, 0x04: 35, 0x05: 34, 0x06: 44, 0x07: 45,   // A S D F H G Z X
    0x08: 46, 0x09: 47, 0x0A: 86, 0x0B: 48, 0x0C: 16, 0x0D: 17, 0x0E: 18, 0x0F: 19,   // C V § B Q W E R
    0x10: 21, 0x11: 20, 0x12: 2, 0x13: 3, 0x14: 4, 0x15: 5, 0x16: 7, 0x17: 6,          // Y T 1 2 3 4 6 5
    0x18: 13, 0x19: 10, 0x1A: 8, 0x1B: 12, 0x1C: 9, 0x1D: 11, 0x1E: 27, 0x1F: 24,      // = 9 7 - 8 0 ] O
    0x20: 22, 0x21: 26, 0x22: 23, 0x23: 25, 0x24: 28, 0x25: 38, 0x26: 36, 0x27: 40,    // U [ I P Return L J '
    0x28: 37, 0x29: 39, 0x2A: 43, 0x2B: 51, 0x2C: 53, 0x2D: 49, 0x2E: 50, 0x2F: 52,    // K ; \ , / N M .
    0x30: 15, 0x31: 57, 0x32: 41, 0x33: 14, 0x35: 1,                                   // Tab Space ` Backspace Esc
    0x36: 100, 0x37: 56, 0x38: 42, 0x39: 58, 0x3A: 125, 0x3B: 29, 0x3C: 54, 0x3D: 126, 0x3E: 97, // R⌘ ⌘ ⇧ Caps ⌥ ⌃ R⇧ R⌥ R⌃
    0x40: 187, 0x41: 83, 0x43: 55, 0x45: 78, 0x47: 69, 0x4B: 98, 0x4C: 96, 0x4E: 74,   // F17 KP. KP* KP+ Clear KP/ KPEnter KP-
    0x4F: 188, 0x50: 189, 0x51: 117, 0x52: 82, 0x53: 79, 0x54: 80, 0x55: 81, 0x56: 75, // F18 F19 KP= KP0-4
    0x57: 76, 0x58: 77, 0x59: 71, 0x5A: 190, 0x5B: 72, 0x5C: 73,                       // KP5 KP6 KP7 F20 KP8 KP9
    0x60: 63, 0x61: 64, 0x62: 65, 0x63: 61, 0x64: 66, 0x65: 67, 0x67: 87, 0x69: 183,   // F5 F6 F7 F3 F8 F9 F11 F13
    0x6A: 186, 0x6B: 184, 0x6D: 68, 0x6E: 127, 0x6F: 88, 0x71: 185, 0x72: 110,         // F16 F14 F10 Menu F12 F15 Insert
    0x73: 102, 0x74: 104, 0x75: 111, 0x76: 62, 0x77: 107, 0x78: 60, 0x79: 109, 0x7A: 59, // Home PgUp Del F4 End F2 PgDn F1
    0x7B: 105, 0x7C: 106, 0x7D: 108, 0x7E: 103,                                        // ← → ↓ ↑
]

/// Modifier keys as they arrive in flagsChanged: key code -> (evdev code, device flag bit).
let modifierKeys: [UInt16: (code: UInt16, mask: UInt)] = [
    0x38: (42, 0x0002), 0x3C: (54, 0x0004),      // shift (NX_DEVICELSHIFTKEYMASK / R)
    0x3B: (29, 0x0001), 0x3E: (97, 0x2000),      // control
    0x3A: (125, 0x0020), 0x3D: (126, 0x0040),    // option -> meta
    0x37: (56, 0x0008), 0x36: (100, 0x0010),     // command -> alt
]

// MARK: - The device

final class VirtioInputDevice: NSObject, VZCustomVirtioDeviceConfigurationDelegate, VZCustomVirtioDeviceDelegate {
    enum Kind: String { case keyboard, tablet }
    static let queue = DispatchQueue(label: "hvgpu.input")

    let kind: Kind
    private(set) var device: VZCustomVirtioDevice?
    private var elements: [VZVirtioQueueElement] = []      // event buffers the driver has posted
    private var pending: [InputEvent] = []
    private var driverOK = false
    private(set) var sent = 0, dropped = 0

    init(kind: Kind) {
        self.kind = kind
    }

    var configuration: VZCustomVirtioDeviceConfiguration {
        let cfg = VZCustomVirtioDeviceConfiguration()
        cfg.deviceID = 18                       // VIRTIO_ID_INPUT
        cfg.pciClassID = 0x09                   // input device controller
        cfg.pciSubclassID = 0x80
        cfg.virtioQueueCount = 2                // eventq, statusq
        cfg.deviceSpecificConfiguration = VZVirtioDeviceSpecificConfiguration(configurationData: staticConfig())
        cfg.provider = VZCustomVirtioDeviceDelegateProvider(deviceQueue: VirtioInputDevice.queue, delegate: self)
        return cfg
    }

    // MARK: configuration (static answer table, see the header comment)

    private func bitmap(_ codes: [Int]) -> [UInt8] {
        guard let top = codes.max() else { return [] }
        var b = [UInt8](repeating: 0, count: top / 8 + 1)
        for c in codes { b[c / 8] |= 1 << UInt8(c % 8) }
        return b
    }

    private func entry(_ select: UInt8, _ subsel: UInt16, _ data: [UInt8]) -> [UInt8] {
        precondition(data.count <= 128 && subsel < 256)
        return [select, UInt8(subsel), UInt8(data.count), 0] + data + [UInt8](repeating: 0, count: 128 - data.count)
    }

    private func staticConfig() -> Data {
        var d: [UInt8] = [0, 0, 0] + Array("PRIN1".utf8) + [UInt8](repeating: 0, count: 128)   // select, subsel, size, reserved, union
        precondition(d.count == 136)
        let name = kind == .keyboard ? "Prose Keyboard" : "Prose Tablet"
        d += entry(EV.cfgIdName, 0, Array(name.utf8))
        d += entry(EV.cfgIdDevids, 0, [6, 0, 0xf4, 0x1a, kind == .keyboard ? 1 : 2, 0, 1, 0])   // BUS_VIRTUAL, vendor 1af4
        switch kind {
        case .keyboard:
            d += entry(EV.cfgEvBits, EV.key, bitmap(macToEvdev.values.map(Int.init)))
            d += entry(EV.cfgEvBits, EV.rep, [0x03])
        case .tablet:
            d += entry(EV.cfgEvBits, EV.key, bitmap([Int(EV.btnLeft), Int(EV.btnRight), Int(EV.btnMiddle)]))
            d += entry(EV.cfgEvBits, EV.rel, bitmap([Int(EV.relWheel), Int(EV.relHWheel)]))
            d += entry(EV.cfgEvBits, EV.abs, bitmap([Int(EV.absX), Int(EV.absY)]))
            let absInfo = le32(0) + le32(UInt32(EV.absMax)) + le32(0) + le32(0) + le32(0)   // min max fuzz flat res
            d += entry(EV.cfgAbsInfo, EV.absX, absInfo)
            d += entry(EV.cfgAbsInfo, EV.absY, absInfo)
        }
        d += entry(0xff, 0, [])                 // terminator
        return Data(d)
    }

    // MARK: delegate

    func customVirtioConfiguration(_ configuration: VZCustomVirtioDeviceConfiguration,
                                   didCreateDevice device: VZCustomVirtioDevice) {
        self.device = device
        device.delegate = self
        log("input: \(kind.rawValue) device created")
    }

    func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
        driverOK = true
        log("input: \(kind.rawValue): DRIVER_OK")
    }

    func customVirtioDeviceWillReset(_ device: VZCustomVirtioDevice) {
        driverOK = false
        elements.removeAll()
        pending.removeAll()
    }

    func customVirtioDeviceWillStop(_ device: VZCustomVirtioDevice) {
        elements.removeAll()
    }

    func customVirtioDevice(_ device: VZCustomVirtioDevice, didReceiveNotificationFor queue: VZVirtioQueue) {
        if queue.queueIndex == 0 {
            while let element = queue.nextElement() { elements.append(element) }
            flush()
        } else {
            // statusq: LED changes and the like; acknowledged, not acted on
            while let element = queue.nextElement() { element.returnToQueue() }
        }
    }

    // MARK: events

    /// Queue events for the guest (any thread). One virtio-input event per buffer.
    func send(_ events: [InputEvent]) {
        VirtioInputDevice.queue.async { [self] in
            guard driverOK else { return }
            if pending.count + events.count > 4096 {
                dropped += events.count
                return
            }
            pending.append(contentsOf: events)
            flush()
        }
    }

    private func flush() {
        while !pending.isEmpty, !elements.isEmpty {
            let element = elements.removeFirst()
            let ev = pending.removeFirst()
            let bytes = le16(ev.type) + le16(ev.code) + le32(UInt32(bitPattern: ev.value))
            _ = try? element.write(Data(bytes))
            element.returnToQueue()
            sent += 1
        }
    }
}

// MARK: - Window events -> device events

final class InputRouter {
    let keyboard = VirtioInputDevice(kind: .keyboard)
    let tablet = VirtioInputDevice(kind: .tablet)
    unowned let presenter: Presenter
    private var cursorHidden = false
    private var wheelX = 0.0, wheelY = 0.0
    private var lastX: Int32 = -1, lastY: Int32 = -1

    init(presenter: Presenter) {
        self.presenter = presenter
    }

    /// The tablet position for a point in the view (points, AppKit's y up), mapped
    /// through the rectangle the guest image occupies, clamped to it.
    func guestPosition(_ point: NSPoint) -> (Int32, Int32) {
        let r = presenter.imageRect()
        guard r.width > 0, r.height > 0 else { return (0, 0) }
        let u = min(max((point.x - r.minX) / r.width, 0), 1)
        let v = min(max(1 - (point.y - r.minY) / r.height, 0), 1)
        return (Int32((u * Double(EV.absMax)).rounded()), Int32((v * Double(EV.absMax)).rounded()))
    }

    func move(to x: Int32, _ y: Int32) {
        guard x != lastX || y != lastY else { return }
        lastX = x
        lastY = y
        tablet.send([(EV.abs, EV.absX, x), (EV.abs, EV.absY, y), (EV.syn, 0, 0)])
    }

    func pointer(_ event: NSEvent, in view: NSView) {
        let (x, y) = guestPosition(view.convert(event.locationInWindow, from: nil))
        move(to: x, y)
    }

    func button(_ event: NSEvent, in view: NSView) {
        let (x, y) = guestPosition(view.convert(event.locationInWindow, from: nil))
        let code: UInt16
        switch event.buttonNumber {
        case 0: code = EV.btnLeft
        case 1: code = EV.btnRight
        default: code = EV.btnMiddle
        }
        let pressed: Int32 = [.leftMouseDown, .rightMouseDown, .otherMouseDown].contains(event.type) ? 1 : 0
        lastX = x
        lastY = y
        tablet.send([(EV.abs, EV.absX, x), (EV.abs, EV.absY, y), (EV.key, code, pressed), (EV.syn, 0, 0)])
    }

    func click(_ code: UInt16 = EV.btnLeft) {
        tablet.send([(EV.key, code, 1), (EV.syn, 0, 0), (EV.key, code, 0), (EV.syn, 0, 0)])
    }

    func scroll(_ event: NSEvent) {
        // lines; trackpads report pixels, which ~10 per line turns into something usable
        let scale = event.hasPreciseScrollingDeltas ? 0.1 : 1.0
        wheelY += event.scrollingDeltaY * scale
        wheelX += event.scrollingDeltaX * scale
        var events: [InputEvent] = []
        let stepsY = Int32(wheelY.rounded(.towardZero)), stepsX = Int32(wheelX.rounded(.towardZero))
        if stepsY != 0 { wheelY -= Double(stepsY); events.append((EV.rel, EV.relWheel, stepsY)) }
        if stepsX != 0 { wheelX -= Double(stepsX); events.append((EV.rel, EV.relHWheel, -stepsX)) }
        if !events.isEmpty { tablet.send(events + [(EV.syn, 0, 0)]) }
    }

    func key(_ event: NSEvent, pressed: Bool) {
        if pressed && event.isARepeat { return }        // the guest repeats keys itself
        guard let code = macToEvdev[event.keyCode] else { return }
        keyboard.send([(EV.key, code, pressed ? 1 : 0), (EV.syn, 0, 0)])
    }

    func tap(evdev code: UInt16) {
        keyboard.send([(EV.key, code, 1), (EV.syn, 0, 0), (EV.key, code, 0), (EV.syn, 0, 0)])
    }

    func flags(_ event: NSEvent) {
        if event.keyCode == 0x39 {                       // caps lock toggles: press and release
            tap(evdev: 58)
            return
        }
        guard let m = modifierKeys[event.keyCode] else { return }
        let pressed = event.modifierFlags.rawValue & m.mask != 0
        keyboard.send([(EV.key, m.code, pressed ? 1 : 0), (EV.syn, 0, 0)])
    }

    func entered() {
        if !cursorHidden { NSCursor.hide(); cursorHidden = true }
    }

    func exited() {
        if cursorHidden { NSCursor.unhide(); cursorHidden = false }
    }

    var stats: String {
        "input: keyboard \(keyboard.sent) events sent, \(keyboard.dropped) dropped; tablet \(tablet.sent) sent, \(tablet.dropped) dropped"
    }
}

func le16(_ v: UInt16) -> [UInt8] { [UInt8(v & 0xff), UInt8(v >> 8)] }
