// chipdevice.swift: the Prose Chip (PRCH), a custom virtio device the guest
// sends notation to.
//
// The guest sends ABC source and nothing else -- no samples, no note events,
// no timing. The host parses it, schedules it against its own sample clock
// and makes the sound. That is the same trade the display device makes with
// a shader, and for audio it is the stronger one: a motif is four hundred
// bytes where a second of stereo is four hundred kilobytes, and the clock
// that matters is CoreAudio's, not a guest timer woken late by whatever the
// scheduler is busy with.
//
// Where a tune goes is the notation's own business. `[I:chip ...]` is for the
// trio; `%%MIDI program n` was written for a synthesiser and goes to the Mac's
// General MIDI one, the same synth the guest's own MIDI port already plays.
import Foundation
import Virtualization
import os

enum PRCH {
    // 60, not 64: the modern PCI device ID is 0x1040 + this, and the range
    // ends at 0x107F -- so 64 would be outside it. 61 is the portal, 62 MIDI,
    // 63 the display.
    static let deviceID: UInt16 = 60
    static let magic: UInt32 = 0x48435250          // "PRCH" little-endian
    static let version: UInt16 = 1
    static let configSize = 32
    static let maxText = 16384
    static let tracks = kTrioTracks

    static let cmdGetInfo: UInt32 = 0x0100
    static let cmdPlay: UInt32 = 0x0101
    static let cmdStop: UInt32 = 0x0102
    static let cmdStopAll: UInt32 = 0x0103
    static let cmdVolume: UInt32 = 0x0104
    static let cmdState: UInt32 = 0x0105

    static let respOK: UInt32 = 0x1000
    static let errInvalid: UInt32 = 0x1100
    static let errUnsupported: UInt32 = 0x1101
    static let errState: UInt32 = 0x1102

    static let flagLoop: UInt32 = 1
}

final class ProseChipDevice: NSObject, VZCustomVirtioDeviceConfigurationDelegate,
    VZCustomVirtioDeviceDelegate {
    static let queue = DispatchQueue(label: "hvgpu.chip")

    /// Test only: `--chip-late-reply <seconds>` holds back the answer to the first
    /// command that long -- longer than the guest waits -- and then gives it.
    /// That is how the guest driver's handling of a command that timed out is
    /// proved (Haiku patch 0127): the answer does come, late, after the guest has
    /// given up on it. Unset, every answer goes at once.
    private var lateReply = option("--chip-late-reply").flatMap(Double.init)

    private let audio = ChipAudio()
    private var midiTunes: [MIDITune] = (0..<PRCH.tracks).map { _ in MIDITune() }
    private var device: VZCustomVirtioDevice?
    /// Where to find the General MIDI synth. The guest's MIDI port already
    /// runs one, so this borrows it rather than starting a second; with the
    /// MIDI port left out, one is made the first time a tune needs it.
    var borrowSynth: (() -> HostSynth?)?
    private var ownSynth: HostSynth?

    private func generalMIDI() -> HostSynth? {
        if let shared = borrowSynth?(), shared.ok { return shared }
        if ownSynth == nil { ownSynth = HostSynth() }
        return ownSynth?.ok == true ? ownSynth : nil
    }
    private var plays = 0, midiPlays = 0, refusals = 0
    /// What the status bar watches: tunes started, and whether anything is
    /// sounding now. Written on the device queue, read on the main thread.
    let activity = OSAllocatedUnfairLock(initialState: (started: 0, chip: 0, synth: 0))

    var configuration: VZCustomVirtioDeviceConfiguration {
        let cfg = VZCustomVirtioDeviceConfiguration()
        cfg.deviceID = PRCH.deviceID
        cfg.pciClassID = 0x04                  // multimedia
        cfg.pciSubclassID = 0x01               // audio
        // Two, though only the first is used: the portal and the MIDI port
        // both declare two, and a one-queue custom device was not enumerated
        // by the guest at all.
        cfg.virtioQueueCount = 2
        cfg.deviceSpecificConfiguration = VZVirtioDeviceSpecificConfiguration(configurationData: configData())
        cfg.provider = VZCustomVirtioDeviceDelegateProvider(
            deviceQueue: ProseChipDevice.queue, delegate: self)
        return cfg
    }

    private func configData() -> Data {
        var d = Data()
        d.append(contentsOf: le32(PRCH.magic))
        d.append(contentsOf: [UInt8(PRCH.version & 0xff), UInt8(PRCH.version >> 8), 0, 0])
        d.append(contentsOf: le32(UInt32(kChipsPerTrio)))
        d.append(contentsOf: le32(UInt32(kVoicesPerTrio)))
        d.append(contentsOf: le32(UInt32(PRCH.tracks)))
        d.append(contentsOf: le32(UInt32(kChipSampleRate)))
        d.append(contentsOf: le32(UInt32(PRCH.maxText)))
        d.append(contentsOf: [UInt8](repeating: 0, count: PRCH.configSize - d.count))
        return d
    }

    func start() { audio.start() }
    func stop() {
        for t in midiTunes { t.stop() }
        audio.stop()
    }

    /// True while any track is still sounding, on either machine.
    var sounding: Bool {
        for t in 0..<PRCH.tracks where audio.trio.isPlaying(track: t) || midiTunes[t].playing {
            return true
        }
        return false
    }

    var stats: String {
        "chip: \(plays) tunes on the trio, \(midiPlays) on the synth, \(refusals) refused"
    }

    // MARK: the control queue

    func customVirtioConfiguration(_ configuration: VZCustomVirtioDeviceConfiguration,
                                   didCreateDevice device: VZCustomVirtioDevice) {
        device.delegate = self
        self.device = device
        log("chip: device created, \(kChipsPerTrio) chips, \(kVoicesPerTrio) voices, "
            + "\(PRCH.tracks) tracks")
    }

    func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
        log("chip: DRIVER_OK")
        start()
    }

    func customVirtioDeviceWillReset(_ device: VZCustomVirtioDevice) { stop() }
    func customVirtioDeviceWillStop(_ device: VZCustomVirtioDevice) { stop() }

    func customVirtioDevice(_ device: VZCustomVirtioDevice,
                            didReceiveNotificationFor queue: VZVirtioQueue) {
        while let element = queue.nextElement() { handle(element) }
    }

    private func handle(_ element: VZVirtioQueueElement) {
        let length = element.readBuffersByteCount
        var bytes = [UInt8](repeating: 0, count: length)
        guard length >= 16,
              (try? element.readBytes(intoBuffer: &bytes, exactLength: length)) != nil else {
            element.returnToQueue()
            return
        }
        let req = Data(bytes)
        let type = leU32(req, 0), seqNo = leU64(req, 8)
        var status = PRCH.respOK
        var value: UInt32 = 0
        var payload: [UInt8] = []

        switch type {
        case PRCH.cmdGetInfo:
            payload = [UInt8](configData())
        case PRCH.cmdPlay:
            (status, value) = play(req)
        case PRCH.cmdStop:
            let track = Int(leU32(req, 16))
            if track < 0 || track >= PRCH.tracks { status = PRCH.errInvalid }
            else {
                audio.trio.stop(track: track)
                midiTunes[track].stop()
            }
        case PRCH.cmdStopAll:
            audio.trio.stopAll()
            for t in midiTunes { t.stop() }
        case PRCH.cmdVolume:
            audio.trio.masterVolume = min(1.0, Double(leU32(req, 16)) / 255.0)
        case PRCH.cmdState:
            for t in 0..<PRCH.tracks
                where audio.trio.isPlaying(track: t) || midiTunes[t].playing {
                value |= 1 << UInt32(t)
            }
        default:
            log("chip: unsupported command 0x\(String(type, radix: 16))")
            status = PRCH.errUnsupported
        }

        let response = le32(status) + le32(0) + le64(seqNo) + le32(value) + le32(0) + payload
        let answer = element.writeBuffersByteCount < response.count
            ? Data(le32(PRCH.errInvalid) + le32(0) + le64(seqNo) + le32(0) + le32(0))
            : Data(response)
        if let delay = lateReply {
            lateReply = nil
            log("chip: answering command \(seqNo) \(delay) s late (--chip-late-reply)")
            // Written late as well as returned late: a slow host has not got an
            // answer to write yet, and the guest must not find one early.
            ProseChipDevice.queue.asyncAfter(deadline: .now() + delay) {
                _ = try? element.write(answer)
                element.returnToQueue()
                log("chip: answered command \(seqNo)")
            }
            return
        }
        _ = try? element.write(answer)
        element.returnToQueue()
    }

    /// The whole of the dispatch: read the notation, and let it say where it
    /// belongs. A tune that names neither a chip nor a program is a bare tune,
    /// and the chip is what this device is for.
    private func play(_ req: Data) -> (UInt32, UInt32) {
        guard req.count >= 32 else { return (PRCH.errInvalid, 0) }
        let track = Int(leU32(req, 16))
        let length = Int(leU32(req, 20))
        let flags = leU32(req, 24)
        guard track >= 0, track < PRCH.tracks, length > 0, length <= PRCH.maxText,
              req.count >= 32 + length else {
            refusals += 1
            return (PRCH.errInvalid, 0)
        }
        let source = String(decoding: req[(req.startIndex + 32)..<(req.startIndex + 32 + length)],
                            as: UTF8.self)
        let loop = flags & PRCH.flagLoop != 0
        let tune = parseABC(source)
        guard !tune.notes.isEmpty else {
            refusals += 1
            return (PRCH.errInvalid, 0)
        }

        // Whichever machine it is for, the other one lets go of this track.
        audio.trio.stop(track: track)
        midiTunes[track].stop()

        if !tune.isChipTune && !tune.midiPrograms.isEmpty, let synth = generalMIDI() {
            midiTunes[track].play(tune, on: synth, loop: loop)
            midiPlays += 1
            activity.withLock { $0.started += 1; $0.synth += 1 }
            log("chip: track \(track): \(tune.notes.count) notes to the synth, "
                + "program \(tune.midiPrograms.values.first ?? 0)")
        } else {
            let result = audio.trio.play(source, track: track, loop: loop)
            plays += 1
            activity.withLock { $0.started += 1; $0.chip += 1 }
            log("chip: track \(track): \(result.notes) notes on the trio"
                + (result.warnings.isEmpty ? ""
                   : ", \(result.warnings.count) bar line(s) the durations did not land on"))
        }
        return (PRCH.respOK, UInt32(tune.notes.count))
    }
}
