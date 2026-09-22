// hvgpu: the Prose MIDI device — the guest's MIDI port, played by the host.
//
// A custom virtio device (ID 62) with two queues: 0 carries MIDI bytes from the
// guest (driver-readable buffers), 1 carries MIDI bytes to the guest (the driver
// posts writable buffers). Haiku's fork driver publishes it as /dev/midi/prose/0,
// midi_server turns that into a producer/consumer pair, and every MIDI app in the
// guest can play through the Mac: the bytes go to Core Audio's General MIDI (DLS)
// synth, and to a CoreMIDI virtual source so Mac apps can listen too. A CoreMIDI
// virtual destination feeds Mac keyboards and sequencers into the guest.
import AVFoundation
import CoreMIDI
import Foundation
import Virtualization
import os

// MARK: - MIDI byte stream parser (running status, system common, sysex, realtime)

struct MIDIParser {
    private var status: UInt8 = 0
    private var data: [UInt8] = []
    private var needed = 0
    private var sysex: [UInt8]? = nil

    static func dataLength(_ status: UInt8) -> Int {
        switch status & 0xF0 {
        case 0x80, 0x90, 0xA0, 0xB0, 0xE0: return 2
        case 0xC0, 0xD0: return 1
        default:
            switch status {
            case 0xF1, 0xF3: return 1
            case 0xF2: return 2
            default: return 0
            }
        }
    }

    /// Feed bytes; returns complete messages (channel/system common as 1–3 bytes,
    /// realtime as 1 byte, sysex as the whole F0…F7 block).
    mutating func feed(_ bytes: [UInt8]) -> [[UInt8]] {
        var out: [[UInt8]] = []
        for b in bytes {
            if b >= 0xF8 {                          // realtime: passes through anything
                out.append([b])
                continue
            }
            if sysex != nil {
                sysex!.append(b)
                if b == 0xF7 { out.append(sysex!); sysex = nil }
                else if b >= 0x80 { sysex = nil }   // a status byte ends a broken sysex
                continue
            }
            if b == 0xF0 { sysex = [b]; continue }
            if b >= 0x80 {
                status = b
                data.removeAll()
                needed = MIDIParser.dataLength(b)
                if needed == 0 { out.append([b]); status = b < 0xF0 ? b : 0 }
                continue
            }
            guard status != 0 else { continue }     // data without status: drop
            data.append(b)
            if data.count == needed {
                out.append([status] + data)
                data.removeAll()                    // running status stays for channel messages
                if status >= 0xF0 { status = 0 }
            }
        }
        return out
    }
}

// MARK: - Host synth (Core Audio DLS General MIDI)

final class HostSynth {
    private let engine = AVAudioEngine()
    private let synth: AVAudioUnitMIDIInstrument
    private(set) var ok = false

    init() {
        var desc = AudioComponentDescription()
        desc.componentType = kAudioUnitType_MusicDevice
        desc.componentSubType = kAudioUnitSubType_DLSSynth
        desc.componentManufacturer = kAudioUnitManufacturer_Apple
        synth = AVAudioUnitMIDIInstrument(audioComponentDescription: desc)
        engine.attach(synth)
        engine.connect(synth, to: engine.mainMixerNode, format: nil)
        do {
            try engine.start()
            ok = true
            log("midi: host General MIDI synth running")
        } catch {
            log("midi: synth failed to start: \(error.localizedDescription)")
        }
    }

    func play(_ message: [UInt8]) {
        guard ok, let status = message.first else { return }
        // an engine stops itself on a configuration change or a render error,
        // and a synth whose engine stopped is simply never heard again: say
        // so, and start it once more (the notes after that are played)
        if !engine.isRunning {
            log("midi: the synth's engine had stopped; starting it again")
            do {
                try engine.start()
            } catch {
                log("midi: engine restart failed: \(error.localizedDescription)")
                ok = false
            }
        }
        if status == 0xF0 {
            synth.sendMIDISysExEvent(Data(message))
        } else if status < 0xF0 {
            synth.sendMIDIEvent(status, data1: message.count > 1 ? message[1] : 0,
                                data2: message.count > 2 ? message[2] : 0)
        }
        // system common / realtime: nothing for a synth to do
    }
}

// MARK: - The device

final class ProseMIDIDevice: NSObject, VZCustomVirtioDeviceConfigurationDelegate, VZCustomVirtioDeviceDelegate {
    // Every MIDI byte from the guest is delivered on this queue and handed
    // straight to the synth. At the default service class it queues behind
    // whatever else the machine is doing, and a note that arrives late is a
    // note played late: the timing is the whole point of MIDI.
    static let queue = DispatchQueue(label: "hvgpu.midi", qos: .userInteractive)
    static let deviceID: UInt16 = 62      // 63 is the display; 64+ would map past virtio-pci's modern ID range
    static let endpointName = "Prose"     // the CoreMIDI source and destination, as Mac apps list them
    /// Messages from the guest, for the status bar's MIDI light (read on the main thread).
    let activity = OSAllocatedUnfairLock(initialState: 0)

    private(set) var device: VZCustomVirtioDevice?
    private var rxElements: [VZVirtioQueueElement] = []      // guest's buffers for host -> guest bytes
    private var pendingToGuest: [UInt8] = []
    private var parser = MIDIParser()
    private var synth: HostSynth?
    private var client = MIDIClientRef()
    private var source = MIDIEndpointRef()
    private var destination = MIDIEndpointRef()
    private let verbose = args.contains("--midi-log")
    private(set) var fromGuest = 0, toGuest = 0, messages = 0

    var configuration: VZCustomVirtioDeviceConfiguration {
        let cfg = VZCustomVirtioDeviceConfiguration()
        cfg.deviceID = ProseMIDIDevice.deviceID
        cfg.pciClassID = 0x04                     // multimedia: the guest probes drivers/midi for it
        cfg.pciSubclassID = 0x01
        cfg.virtioQueueCount = 2
        cfg.deviceSpecificConfiguration = VZVirtioDeviceSpecificConfiguration(
            configurationData: Data(Array("PRMD".utf8) + le32(1) + [UInt8](repeating: 0, count: 8)))
        cfg.provider = VZCustomVirtioDeviceDelegateProvider(deviceQueue: ProseMIDIDevice.queue, delegate: self)
        return cfg
    }

    // MARK: host side setup

    private func setupHost() {
        guard client == 0 else { return }   // a restarted machine keeps the synth and endpoints
        if !args.contains("--no-synth") { synth = HostSynth() }
        // CoreMIDI: a virtual source (what the guest plays) and a virtual destination (into the guest)
        let name = ProseMIDIDevice.endpointName
        var status = MIDIClientCreateWithBlock("hvgpu" as CFString, &client) { _ in }
        guard status == noErr else { log("midi: MIDIClientCreate failed: \(status)"); return }
        status = MIDISourceCreateWithProtocol(client, name as CFString, ._1_0, &source)
        if status != noErr { log("midi: virtual source failed: \(status)") }
        status = MIDIDestinationCreateWithProtocol(client, name as CFString, ._1_0, &destination) { [weak self] list, _ in
            // UMP MIDI 1.0 channel voice / system messages -> bytes for the guest
            var bytes: [UInt8] = []
            for packet in list.unsafeSequence() {
                let count = Int(packet.pointee.wordCount)
                let words: [UInt32] = withUnsafePointer(to: packet.pointee.words) { p in
                    p.withMemoryRebound(to: UInt32.self, capacity: 64) {
                        Array(UnsafeBufferPointer(start: $0, count: min(count, 64)))
                    }
                }
                for w in words {
                    let type = (w >> 28) & 0xF
                    if type == 2 || type == 1 {           // MIDI 1.0 channel voice / system common+realtime
                        let status = UInt8((w >> 16) & 0xFF)
                        let n = MIDIParser.dataLength(status)
                        bytes.append(status)
                        if n >= 1 { bytes.append(UInt8((w >> 8) & 0x7F)) }
                        if n >= 2 { bytes.append(UInt8(w & 0x7F)) }
                    }
                }
            }
            if !bytes.isEmpty { self?.send(toGuest: bytes) }
        }
        if status != noErr { log("midi: virtual destination failed: \(status)") }
        log("midi: CoreMIDI endpoints \"\(name)\" created")
    }

    /// The guest's MIDI output, to the synth and to CoreMIDI.
    private func handle(fromGuest bytes: [UInt8]) {
        fromGuest += bytes.count
        for message in parser.feed(bytes) {
            messages += 1
            activity.withLock { $0 += 1 }
            if verbose { log("midi: " + message.map { String(format: "%02X", $0) }.joined(separator: " ")) }
            synth?.play(message)
            if source != 0, let status = message.first, status != 0xF0, message.count <= 3 {
                var word = UInt32(status) << 16
                if message.count > 1 { word |= UInt32(message[1]) << 8 }
                if message.count > 2 { word |= UInt32(message[2]) }
                word |= (status >= 0xF0 ? 1 : 2) << 28
                var list = MIDIEventList()
                let packet = MIDIEventListInit(&list, ._1_0)
                _ = MIDIEventListAdd(&list, MemoryLayout<MIDIEventList>.size, packet, 0, 1, [word])
                MIDIReceivedEventList(source, &list)
            }
        }
    }

    func send(toGuest bytes: [UInt8]) {
        ProseMIDIDevice.queue.async { [self] in
            pendingToGuest.append(contentsOf: bytes)
            if pendingToGuest.count > 65536 { pendingToGuest.removeFirst(pendingToGuest.count - 65536) }
            flushToGuest()
        }
    }

    private func flushToGuest() {
        while !pendingToGuest.isEmpty, !rxElements.isEmpty {
            let element = rxElements.removeFirst()
            let n = min(pendingToGuest.count, element.writeBuffersByteCount)
            let chunk = Array(pendingToGuest.prefix(n))
            pendingToGuest.removeFirst(n)
            _ = try? element.write(Data(chunk))
            element.returnToQueue()
            toGuest += n
        }
    }

    // MARK: delegate

    func customVirtioConfiguration(_ configuration: VZCustomVirtioDeviceConfiguration,
                                   didCreateDevice device: VZCustomVirtioDevice) {
        self.device = device
        device.delegate = self
        log("midi: device created")
        DispatchQueue.main.async { [self] in setupHost() }
    }

    func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
        log("midi: DRIVER_OK")
    }

    func customVirtioDeviceWillReset(_ device: VZCustomVirtioDevice) {
        rxElements.removeAll()
        pendingToGuest.removeAll()
        parser = MIDIParser()
    }

    func customVirtioDeviceWillStop(_ device: VZCustomVirtioDevice) {
        rxElements.removeAll()
    }

    func customVirtioDevice(_ device: VZCustomVirtioDevice, didReceiveNotificationFor queue: VZVirtioQueue) {
        if queue.queueIndex == 0 {
            while let element = queue.nextElement() {
                let length = element.readBuffersByteCount
                var bytes = [UInt8](repeating: 0, count: length)
                if length > 0, (try? element.readBytes(intoBuffer: &bytes, exactLength: length)) != nil {
                    handle(fromGuest: bytes)
                }
                element.returnToQueue()
            }
        } else {
            while let element = queue.nextElement() { rxElements.append(element) }
            flushToGuest()
        }
    }

    var stats: String { "midi: \(fromGuest) bytes / \(messages) messages from the guest, \(toGuest) bytes to it" }
}
