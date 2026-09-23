// miditune.swift: the other half of the rule -- MIDI to MIDI, ABC to chip.
//
// A tune carrying `%%MIDI program 80` was written for a synthesiser, not for
// this chip: playing it on a default pulse wave is not a port, it is a
// mistake with the right notes in it. The Mac already has a General MIDI
// synth running for the guest's own MIDI port, so such a tune goes there,
// and only a tune carrying `[I:chip ...]` reaches the trio.
//
// The notation says which machine it is for, so nothing above has to choose.
import Foundation

/// One ABC tune played on the host's General MIDI synth.
///
/// Timed from a repeating timer rather than from the audio clock: a synth on
/// the other side of CoreMIDI has its own latency anyway, and sending from a
/// render thread would put allocation on it. Two milliseconds of jitter on a
/// fanfare is inaudible; the chip, which needs the sample, gets the sample.
final class MIDITune {
    private var timer: DispatchSourceTimer?
    private let queue = DispatchQueue(label: "hvgpu.miditune", qos: .userInteractive)
    private var steps: [Step] = []
    private var cursor = 0
    private var started = Date.distantPast
    private var loop = false
    private var sounding: Set<Int> = []         // channel << 8 | note
    private weak var synth: HostSynth?
    private var channelOf: [Int: Int] = [:]     // ABC voice -> MIDI channel
    private(set) var playing = false

    func play(_ tune: Tune, on synth: HostSynth, loop: Bool) {
        stop()
        guard !tune.notes.isEmpty else { return }
        self.synth = synth
        self.loop = loop
        steps = schedule(tune)
        cursor = 0

        // One channel a voice, and the program it asked for on each. Channel 9
        // is the drum channel and is left alone unless a tune asks for it.
        channelOf = [:]
        var next = 0
        for voice in Set(tune.notes.map { $0.voice }).sorted() {
            let channel = tune.midiChannels[voice].map { max(0, min(15, $0 - 1)) }
                ?? { let c = next == 9 ? 10 : next; next = c + 1; return min(15, c) }()
            channelOf[voice] = channel
            if let program = tune.midiPrograms[voice] ?? tune.midiPrograms[1] {
                synth.play([0xC0 | UInt8(channel), UInt8(max(0, min(127, program)))])
            }
        }

        started = Date()
        playing = true
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now(), repeating: .milliseconds(2), leeway: .milliseconds(1))
        t.setEventHandler { [weak self] in self?.tick() }
        timer = t
        t.resume()
    }

    func stop() {
        timer?.cancel()
        timer = nil
        allOff()
        playing = false
        steps = []
        cursor = 0
    }

    private func allOff() {
        guard let synth else { return }
        for key in sounding {
            synth.play([0x80 | UInt8(key >> 8), UInt8(key & 0xFF), 0])
        }
        sounding.removeAll()
    }

    private func tick() {
        guard playing, let synth else { return }
        let elapsed = Date().timeIntervalSince(started)
        let sample = Int(elapsed * Double(kChipSampleRate))
        while cursor < steps.count && steps[cursor].sample <= sample {
            let step = steps[cursor]
            cursor += 1
            let channel = channelOf[step.voice] ?? 0
            switch step.kind {
            case .noteOn:
                let velocity = UInt8(max(1, min(127, step.velocity)))
                synth.play([0x90 | UInt8(channel), UInt8(step.midi & 0x7F), velocity])
                sounding.insert(channel << 8 | (step.midi & 0x7F))
            case .noteOff:
                synth.play([0x80 | UInt8(channel), UInt8(step.midi & 0x7F), 0])
                sounding.remove(channel << 8 | (step.midi & 0x7F))
            case .chip:
                break                       // a chip register means nothing here
            }
        }
        if cursor >= steps.count {
            if loop {
                allOff()
                cursor = 0
                started = Date()
            } else {
                timer?.cancel()
                timer = nil
                allOff()
                playing = false
            }
        }
    }
}
