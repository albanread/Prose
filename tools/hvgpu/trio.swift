// trio.swift: three chips, nine voices, and the schedule that drives them.
//
// Ported from the Mojo gamepane's abc/trioplay.mojo. The chips themselves do
// not change; what is new is the frame around them:
//
//   * ONE CURSOR per track. ABC voice V:n lands on chip (n-1)/3, and the note
//     allocation stays dynamic within that chip's three voices -- so every
//     existing three-voice tune plays on a trio unchanged.
//   * SPAN RENDERING, three ways. Between events all three chips render the
//     span and the mixer folds them to stereo. The chips stay independent:
//     three filters, three master volumes, which is what makes a trio richer
//     than one nine-voice chip -- a swept lowpass on the lead never dulls the
//     drums.
//   * STEREO. Each chip has a pan through a constant-power table. Defaults:
//     chip 0 centre, chip 1 left, chip 2 right, the classic twin-SID rig.
//
// And the performance layer, which is where a chip tune actually lives: a
// tune is not notes, it is register writes at frame rate. Arpeggio, vibrato,
// slide, pulse-width sweep, filter sweep and tremolo all run in the 50 Hz
// tick, integer state, nothing allocated.
import Foundation
import AVFoundation

let kChipsPerTrio = 3
let kVoicesPerTrio = kChipsPerTrio * 3
let kTrioTracks = 4

/// Equal temperament from A4 = 440, for FRACTIONAL notes -- vibrato and
/// slides live between the semitones.
private func hz(_ midi: Double) -> Double {
    440.0 * exp2((midi - 69.0) / 12.0)
}

/// Integer sine, 64 steps a cycle, -127..127: Bhaskara's parabola on a
/// quarter wave. No table, and exactly reproducible.
private func isin64(_ phase: Int) -> Int {
    let x = phase & 63
    let h = x & 31
    let v = 127 * h * (32 - h) / 256
    return x < 32 ? v : -v
}

/// One voice's performance macros. They are per voice and SURVIVE NOTES --
/// they are how a voice plays, not how one note sounds.
private struct Macros {
    var arp = 0                 // count<<32 | nibbles, first digit lowest
    var arpPos = 0
    var vib = 0                 // depth<<8 | rate
    var vibPhase = 0
    var slide = 0
    var slideCurrent = 0.0
    var pwm = 0
    var pwmPhase = 0
    var pwmBase = 2048
    var trem = 0
    var tremPhase = 0
    var note = -1               // the MIDI note this voice holds, -1 for none
    var age = 0
    var sustain = 0             // the ADSR sustain as written, for tremolo
}

/// One schedule being walked. Several may play at once -- a motif and a shot
/// are different tunes -- and they share the nine voices, which is the tune
/// author's business: the grammar says `v=` explicitly.
private struct Track {
    var steps: [Step] = []
    var cursor = 0
    var position = 0
    var loop = false
    var playing = false
}

final class Trio {
    private var chips: [Chip] = (0..<kChipsPerTrio).map { _ in Chip() }
    private var macros = [Macros](repeating: Macros(), count: kVoicesPerTrio)
    private var sweep = [Int](repeating: 0, count: kChipsPerTrio)
    private var tracks = [Track](repeating: Track(), count: kTrioTracks)
    private var scratch: UnsafeMutablePointer<Float>
    private let scratchFrames = 4096

    /// Constant-power pan gains per chip, computed when the pan changes.
    private var gainL = [Double](repeating: 0.7071, count: kChipsPerTrio)
    private var gainR = [Double](repeating: 0.7071, count: kChipsPerTrio)

    // One stereo delay line for the trio. Dry chips sound like 1982; the same
    // chips into a quarter-note echo sound like a demo.
    private var echoL: [Float]
    private var echoR: [Float]
    private var echoAt = 0
    private var echoSamples = kChipSampleRate / 4
    private var echoFeedback = 0
    private var echoSend = [Int](repeating: 0, count: kChipsPerTrio)

    private let lock = NSLock()
    var masterVolume = 1.0

    init() {
        scratch = UnsafeMutablePointer<Float>.allocate(capacity: scratchFrames)
        scratch.initialize(repeating: 0, count: scratchFrames)
        echoL = [Float](repeating: 0, count: kChipSampleRate)
        echoR = [Float](repeating: 0, count: kChipSampleRate)
        setPan(0, 0)
        setPan(1, -96)
        setPan(2, 96)
    }

    deinit { scratch.deallocate() }

    private func setPan(_ chip: Int, _ pan: Int) {
        let p = (Double(max(-128, min(127, pan))) + 128.0) / 255.0   // 0..1
        gainL[chip] = cos(p * Double.pi / 2)
        gainR[chip] = sin(p * Double.pi / 2)
    }

    // MARK: what the outside asks for

    /// Play a tune on a track, replacing whatever that track held.
    func play(_ source: String, track: Int, loop: Bool) -> (notes: Int, warnings: [String]) {
        let tune = parseABC(source)
        let steps = schedule(tune)
        lock.lock()
        defer { lock.unlock() }
        guard track >= 0 && track < kTrioTracks else { return (0, ["no such track"]) }
        silence(track: track)
        tracks[track].steps = steps
        tracks[track].cursor = 0
        tracks[track].position = 0
        tracks[track].loop = loop
        tracks[track].playing = !steps.isEmpty
        return (tune.notes.count, tune.barWarnings)
    }

    func stop(track: Int) {
        lock.lock()
        defer { lock.unlock() }
        guard track >= 0 && track < kTrioTracks else { return }
        silence(track: track)
        tracks[track].playing = false
        tracks[track].steps = []
    }

    func stopAll() {
        for t in 0..<kTrioTracks { stop(track: t) }
    }

    func isPlaying(track: Int) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return track >= 0 && track < kTrioTracks && tracks[track].playing
    }

    /// Release every voice this track's tune is holding. A track that stops
    /// must not leave a note sounding for ever.
    private func silence(track: Int) {
        guard tracks[track].playing else { return }
        for g in 0..<kVoicesPerTrio where macros[g].note >= 0 {
            macros[g].note = -1
            chips[g / 3].gateOff(g % 3)
        }
    }

    // MARK: the schedule, applied

    private func applyNoteOn(_ abcVoice: Int, _ midi: Int) {
        // V:n lands on chip (n-1)/3, and inside it the voice the tune most
        // likely means -- (n-1)%3 -- before any other free one. Seeding the
        // allocator by the ABC voice is what makes `[I:chip v=k]` reliably
        // address the voice that V:k+1's notes actually land on.
        let chip = max(0, min(kChipsPerTrio - 1, (abcVoice - 1) / 3))
        let preferred = max(0, (abcVoice - 1) % 3)
        var chosen = -1
        let base = chip * 3
        if macros[base + preferred].note < 0 { chosen = preferred }
        if chosen < 0 {
            for v in 0..<3 where macros[base + v].note < 0 { chosen = v; break }
        }
        if chosen < 0 {
            for v in 0..<3 where chips[chip].isIdle(v) { chosen = v; break }
        }
        if chosen < 0 {
            // All busy: take the oldest sounding note, the one furthest
            // through its decay and the least missed.
            var oldest = macros[base].age
            chosen = 0
            for v in 1..<3 where macros[base + v].age < oldest {
                oldest = macros[base + v].age
                chosen = v
            }
        }
        let g = base + chosen
        macros[g].note = midi
        macros[g].age = tracks[0].position
        macros[g].slideCurrent = 0.0
        chips[chip].setFrequencyHz(chosen, hz(Double(midi)))
        chips[chip].gateOn(chosen)
    }

    private func applyNoteOff(_ abcVoice: Int, _ midi: Int) {
        let chip = max(0, min(kChipsPerTrio - 1, (abcVoice - 1) / 3))
        for v in 0..<3 where macros[chip * 3 + v].note == midi {
            macros[chip * 3 + v].note = -1
            chips[chip].gateOff(v)
            return
        }
    }

    /// A register write. `v=` in the grammar is a GLOBAL voice, 0..8, which
    /// is what the tunes already written expect; the chip-level registers are
    /// addressed through whichever voice the tune names.
    private func applyChip(_ voice: Int, _ param: Int, _ value: Int) {
        let g = max(0, min(kVoicesPerTrio - 1, voice))
        let chip = g / 3, v = g % 3
        switch param {
        case CP.wave: chips[chip].setWave(v, value)
        case CP.pw:
            chips[chip].setPulseWidth(v, value)
            macros[g].pwmBase = value
        case CP.a, CP.d, CP.s, CP.r:
            // ADSR arrives a register at a time, so the chip is re-armed from
            // what has been said so far.
            var adsr = adsrOf(g)
            switch param {
            case CP.a: adsr.0 = value
            case CP.d: adsr.1 = value
            case CP.s: adsr.2 = value
            default: adsr.3 = value
            }
            setADSR(g, adsr)
        case CP.filt: chips[chip].routeFilter(v, value != 0)
        case CP.cutoff:
            chips[chip].setFilter(cutoff: value, resonance: chips[chip].filterResonance,
                                  mode: chips[chip].filterMode)
        case CP.res:
            chips[chip].setFilter(cutoff: chips[chip].filterCutoff, resonance: value,
                                  mode: chips[chip].filterMode)
        case CP.fmode:
            chips[chip].setFilter(cutoff: chips[chip].filterCutoff,
                                  resonance: chips[chip].filterResonance, mode: value)
        case CP.vol: chips[chip].setVolume(value)
        case CP.pan: setPan(chip, value - 128)
        case CP.echo: echoSend[chip] = max(0, min(15, value))
        case CP.etime:
            echoSamples = max(1, min(kChipSampleRate - 1, value * kChipFrameSamples))
        case CP.efb: echoFeedback = max(0, min(15, value))
        case CP.arp: macros[g].arp = value; macros[g].arpPos = 0
        case CP.vib: macros[g].vib = value; macros[g].vibPhase = 0
        case CP.slide: macros[g].slide = value
        case CP.pwm:
            macros[g].pwm = value
            macros[g].pwmPhase = 0
            macros[g].pwmBase = chips[chip].pulseWidth(v)
        case CP.trem: macros[g].trem = value; macros[g].tremPhase = 0
        case CP.sweep: sweep[chip] = value - 1024
        default: break                  // an unknown register: play the notes
        }
    }

    private var adsrStore = [(Int, Int, Int, Int)](repeating: (0, 9, 0, 9),
                                                   count: kVoicesPerTrio)
    private func adsrOf(_ g: Int) -> (Int, Int, Int, Int) { adsrStore[g] }
    private func setADSR(_ g: Int, _ v: (Int, Int, Int, Int)) {
        adsrStore[g] = v
        macros[g].sustain = v.2
        chips[g / 3].setADSR(g % 3, v.0, v.1, v.2, v.3)
    }

    /// One chip's macros, one 50 Hz frame.
    private func macroTick(_ chip: Int) {
        for v in 0..<3 {
            let g = chip * 3 + v
            var m = macros[g]

            // Pitch: arp picks the note, slide approaches it, vibrato wobbles
            // the approach -- in that order, and only while a note is held.
            if m.note >= 0 && (m.arp != 0 || (m.vib & 255) != 0 || m.slide != 0) {
                var target = Double(m.note)
                if m.arp != 0 {
                    let count = m.arp >> 32
                    if count > 0 {
                        target += Double((m.arp >> (4 * (m.arpPos % count))) & 15)
                        m.arpPos += 1
                    }
                }
                var current = m.slideCurrent
                if m.slide == 0 || current == 0.0 {
                    current = target
                } else {
                    let step = Double(m.slide) / 16.0
                    if current < target { current = min(target, current + step) }
                    else if current > target { current = max(target, current - step) }
                }
                m.slideCurrent = current
                var effective = current
                if m.vib & 255 != 0 {
                    m.vibPhase += m.vib & 255
                    effective += Double((m.vib >> 8) & 255) * Double(isin64(m.vibPhase))
                        / (127.0 * 16.0)
                }
                chips[chip].setFrequencyHz(v, hz(effective))
            }

            // The pulse width breathes even through the release.
            if m.pwm & 255 != 0 {
                m.pwmPhase += m.pwm & 255
                // A triangle, the classic shape: depth is in eighths of the
                // 12-bit range, so pwm=64/2 swings the width by +-512.
                let tp = m.pwmPhase & 127
                var tri = tp - 64
                if tri < 0 { tri = -tri }
                tri -= 32
                let pw = m.pwmBase + ((m.pwm >> 8) & 255) * tri / 4
                chips[chip].setPulseWidth(v, max(16, min(4080, pw)))
            }

            // Tremolo dips the sustain target, and only that: notes in attack
            // or decay pass unwobbled. The SID had no per-voice volume
            // either, and this is the honest equivalent.
            if m.trem & 255 != 0 {
                m.tremPhase += m.trem & 255
                let full = m.sustain * 17
                let dip = ((m.trem >> 8) & 255) * (127 + isin64(m.tremPhase)) / 254
                chips[chip].setSustain(v, max(0, min(255, full - dip)))
            }
            macros[g] = m
        }

        if sweep[chip] != 0 {
            let c = chips[chip].filterCutoff + sweep[chip]
            chips[chip].setFilter(cutoff: max(0, min(2047, c)),
                resonance: chips[chip].filterResonance, mode: chips[chip].filterMode)
        }
    }

    // MARK: rendering

    /// Fill `frames` interleaved stereo frames. Everything due at this sample
    /// applies before another sample is rendered, then all three chips render
    /// to the next event and the mixer folds them left and right.
    func render(into dest: UnsafeMutablePointer<Float>, frames: Int) {
        for i in 0..<(frames * 2) { dest[i] = 0 }
        lock.lock()
        defer { lock.unlock() }

        var done = 0
        while done < frames {
            // Everything due now.
            for t in 0..<kTrioTracks where tracks[t].playing {
                while tracks[t].cursor < tracks[t].steps.count
                        && tracks[t].steps[tracks[t].cursor].sample <= tracks[t].position {
                    let step = tracks[t].steps[tracks[t].cursor]
                    switch step.kind {
                    case .noteOn: applyNoteOn(step.voice, step.midi)
                    case .noteOff: applyNoteOff(step.voice, step.midi)
                    case .chip: applyChip(step.voice, step.param, step.value)
                    }
                    tracks[t].cursor += 1
                }
                if tracks[t].cursor >= tracks[t].steps.count {
                    if tracks[t].loop {
                        tracks[t].cursor = 0
                        tracks[t].position = 0
                    } else {
                        tracks[t].playing = false
                    }
                }
            }

            // How far to the next thing that happens.
            var span = min(frames - done, scratchFrames)
            for t in 0..<kTrioTracks where tracks[t].playing {
                if tracks[t].cursor < tracks[t].steps.count {
                    span = min(span, max(1, tracks[t].steps[tracks[t].cursor].sample
                        - tracks[t].position))
                }
            }
            span = max(1, span)

            for chip in 0..<kChipsPerTrio {
                chips[chip].render(into: scratch, frames: span) { [self] _ in
                    macroTick(chip)
                }
                let l = gainL[chip], r = gainR[chip]
                let send = Double(echoSend[chip]) / 15.0
                for i in 0..<span {
                    let s = Double(scratch[i])
                    dest[(done + i) * 2] += Float(s * l)
                    dest[(done + i) * 2 + 1] += Float(s * r)
                    if send > 0 {
                        let at = (echoAt + i) % echoL.count
                        echoL[at] += Float(s * l * send)
                        echoR[at] += Float(s * r * send)
                    }
                }
            }

            // The delay line, read where it was written a quarter note ago.
            let feedback = Float(echoFeedback) / 16.0
            for i in 0..<span {
                let write = (echoAt + i) % echoL.count
                let read = (write + echoL.count - echoSamples) % echoL.count
                let l = echoL[read], r = echoR[read]
                dest[(done + i) * 2] += l
                dest[(done + i) * 2 + 1] += r
                echoL[write] += l * feedback
                echoR[write] += r * feedback
                echoL[read] = 0
                echoR[read] = 0
            }
            echoAt = (echoAt + span) % echoL.count

            for t in 0..<kTrioTracks where tracks[t].playing {
                tracks[t].position += span
            }
            done += span
        }

        if masterVolume != 1.0 {
            for i in 0..<(frames * 2) { dest[i] *= Float(masterVolume) }
        }
    }
}


/// The trio, wired to the Mac's output. One source node, stereo, 48 kHz --
/// the chip's own rate, so nothing resamples what an integer oscillator
/// carefully computed.
final class ChipAudio {
    let trio = Trio()
    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode?
    private var running = false

    func start() {
        guard !running else { return }
        let format = AVAudioFormat(standardFormatWithSampleRate: Double(kChipSampleRate),
                                   channels: 2)!
        let node = AVAudioSourceNode(format: format) { [trio] _, _, frameCount, audioBufferList in
            let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
            let frames = Int(frameCount)
            guard buffers.count >= 2,
                  let left = buffers[0].mData?.assumingMemoryBound(to: Float.self),
                  let right = buffers[1].mData?.assumingMemoryBound(to: Float.self)
            else { return noErr }
            // AVAudioSourceNode hands out deinterleaved buffers; the trio
            // renders interleaved, so it lands in scratch and is split here.
            let interleaved = UnsafeMutablePointer<Float>.allocate(capacity: frames * 2)
            defer { interleaved.deallocate() }
            trio.render(into: interleaved, frames: frames)
            for i in 0..<frames {
                left[i] = interleaved[i * 2]
                right[i] = interleaved[i * 2 + 1]
            }
            return noErr
        }
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        source = node
        do {
            try engine.start()
            running = true
            log("chip: trio running, 3 chips, 9 voices, \(kChipSampleRate) Hz stereo")
        } catch {
            log("chip: could not start the audio engine: \(error.localizedDescription)")
        }
    }

    func stop() {
        guard running else { return }
        trio.stopAll()
        engine.stop()
        running = false
    }
}
