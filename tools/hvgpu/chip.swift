// chip.swift: a 6581-flavoured synthesiser -- three voices, ADSR, a resonant
// filter -- ported from the Mojo gamepane's api/audio.mojo.
//
// Not an emulator. What makes it sound like a chip rather than a generic
// synth is kept exactly:
//
//   * 24-bit phase accumulators, so the pitch quantises the same way and the
//     waveforms have the same hard edges
//   * the actual 23-bit LFSR with the actual output taps, which is why the
//     noise rasps instead of hissing
//   * the envelope's period-stretching table rather than an exponential,
//     which is the difference between a C64 snare and a beep
//   * combined waveforms AND together, as they do on the real chip
//   * the pulse width is a register you are expected to modulate every frame
//
// The chip is integer hardware, so this is integer arithmetic. The only
// floating point is the filter and the final sample.
//
// Not ported: WAVE_PCM. The Mojo design leaves it an explicit go/no-go, it
// exists for imported tracker samples, and nothing here has any.
import Foundation

let kChipClockPAL = 985248
let kChipSampleRate = 48000
/// A frame is one turn of the player routine: 50 Hz, the vertical blank the
/// interrupt hung off. Every C64 tune is written in these units.
let kChipFrameSamples = kChipSampleRate / 50
private let kFilterCeiling = 65536.0

struct ChipWave {
    static let tri = 1, saw = 2, pulse = 4, noise = 8
}

struct ChipFilter {
    static let lowpass = 1, bandpass = 2, highpass = 4
}

private enum Env {
    static let idle = 0, attack = 1, decay = 2, sustain = 3, release = 4
}

/// The chip's attack times in milliseconds, 0..15. Decay and release run
/// three times slower for the same index, which is why a C64 bass can have a
/// snap on the front and still ring for half a second.
private let kAttackMS = [2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800,
                         1000, 3000, 5000, 8000]

/// Envelope steps per sample, 16.16 fixed point, for a full 0..255 sweep.
/// Clamped to at least one so the shortest attack still moves; a zero would
/// hang the envelope in its attack and the voice would never sound.
private func rateIncrement(_ ms: Int) -> Int {
    let samples = (ms * kChipSampleRate) / 1000
    if samples < 1 { return 255 << 16 }
    return max(1, (255 << 16) / samples)
}

private struct Voice {
    var acc = 0                 // phase accumulator, 24 bits with 8 fractional
    var step = 0                // per-sample increment, same fixed point
    var pw = 2048               // 12-bit pulse width
    var wave = ChipWave.pulse
    var gate = 0
    var aInc = 0, dInc = 0, rInc = 0
    var sus = 0                 // sustain level, 0..255
    var env = 0                 // envelope level, 16.16 fixed point
    var phase = Env.idle
    var lfsr = 0x7FFFF8         // never zero: all-zeroes is a fixed point of
                                // the shift and the noise would be silence
    var ring = false            // ring-modulate voice n by voice n-1
    var sync = false            // hard-sync voice n to voice n-1
    var filtered = false
    var prev = 0
}

final class Chip {
    private var voices = [Voice](repeating: Voice(), count: 3)
    private var tick = kChipFrameSamples
    private var cutoff = 1024
    private var resonance = 0
    private var mode = ChipFilter.lowpass
    private var volume = 15
    private(set) var frame = 0
    private var dirty = true

    private var low = 0.0, band = 0.0, coefF = 0.0, coefQ = 0.0

    /// Where this chip sits in the stereo picture, -128..127.
    var pan = 0

    init() {
        for v in 0..<3 { setADSR(v, 0, 9, 0, 9) }
    }

    // MARK: registers -- the interface a player routine pokes. These take the
    // same numbers a C64 player would write, so a tune ported from real chip
    // data keeps its values.

    /// The 16-bit frequency register, meaning freq * CLOCK / 2^24 Hz. The
    /// accumulator steps once per output sample rather than once per chip
    /// cycle, so eight fractional bits of headroom keep the pitch exact
    /// instead of a few cents flat in the high octaves.
    func setFrequencyRegister(_ v: Int, _ freq: Int) {
        voices[v].step = (freq * kChipClockPAL * 256) / kChipSampleRate
    }

    func setFrequencyHz(_ v: Int, _ hz: Double) {
        setFrequencyRegister(v, Int(hz * 16777216.0 / Double(kChipClockPAL)))
    }

    func setPulseWidth(_ v: Int, _ pw: Int) { voices[v].pw = pw & 0xFFF }
    func setWave(_ v: Int, _ wave: Int) { voices[v].wave = wave & 15 }
    func setRing(_ v: Int, _ on: Bool) { voices[v].ring = on }
    func setSync(_ v: Int, _ on: Bool) { voices[v].sync = on }
    func routeFilter(_ v: Int, _ on: Bool) { voices[v].filtered = on }
    func setVolume(_ vol: Int) { volume = max(0, min(15, vol)) }
    func wave(_ v: Int) -> Int { voices[v].wave }
    func pulseWidth(_ v: Int) -> Int { voices[v].pw }

    /// Attack, decay, sustain, release as the chip's 4-bit register values.
    func setADSR(_ v: Int, _ a: Int, _ d: Int, _ s: Int, _ r: Int) {
        voices[v].aInc = rateIncrement(kAttackMS[a & 15])
        voices[v].dInc = rateIncrement(kAttackMS[d & 15] * 3)
        voices[v].sus = (s & 15) * 17               // 4 bits scaled to 0..255
        voices[v].rInc = rateIncrement(kAttackMS[r & 15] * 3)
    }

    /// Tremolo dips the sustain target while a note holds, which is the only
    /// per-voice volume this chip has.
    func setSustain(_ v: Int, _ level: Int) { voices[v].sus = max(0, min(255, level)) }

    func gateOn(_ v: Int) { voices[v].gate = 1; voices[v].phase = Env.attack }
    func gateOff(_ v: Int) { voices[v].gate = 0; voices[v].phase = Env.release }
    func isGated(_ v: Int) -> Bool { voices[v].gate != 0 }
    func isIdle(_ v: Int) -> Bool { voices[v].phase == Env.idle }

    /// Clamped, not masked. `cutoff & 0x7FF` wraps 2048 back to 0, so a sweep
    /// running off the top of the range would slam the filter shut instead of
    /// leaving it open -- an effect that opens up ending in a thud.
    func setFilter(cutoff c: Int, resonance r: Int, mode m: Int) {
        cutoff = max(0, min(0x7FF, c))
        resonance = r & 15
        mode = m & 7
        dirty = true
    }

    var filterCutoff: Int { cutoff }
    var filterResonance: Int { resonance }
    var filterMode: Int { mode }

    // MARK: the oscillator

    /// The 12-bit output of one voice's waveform selector. Selecting more
    /// than one ANDs them together on the real chip -- an accident of how the
    /// outputs are wired, not a design, and the source of most of the timbres
    /// people remember.
    private func waveform(_ v: Int, _ acc24: Int, _ ringSourceMSB: Int) -> Int {
        let w = voices[v].wave
        if w == 0 { return 0 }
        var out = 0xFFF

        if w & ChipWave.tri != 0 {
            // The triangle folds the top bit into the rest, and ring
            // modulation replaces that bit with the previous voice's -- which
            // is the whole of ring modulation on this chip. One XOR, and it
            // is why bells and gongs sound the way they do.
            var folded = acc24
            if (acc24 ^ ringSourceMSB) & 0x800000 != 0 { folded = ~acc24 & 0xFFFFFF }
            out &= (folded >> 11) & 0xFFF
        }
        if w & ChipWave.saw != 0 {
            out &= (acc24 >> 12) & 0xFFF
        }
        if w & ChipWave.pulse != 0 {
            out &= (acc24 >> 12) >= voices[v].pw ? 0xFFF : 0
        }
        if w & ChipWave.noise != 0 {
            // Eight taps, scattered: bits 22, 20, 16, 13, 11, 7, 4 and 2
            // become the output's bits 11 down to 4. The low four bits are
            // always zero, which is part of why the noise sounds coarse.
            let l = voices[v].lfsr
            out &= ((l >> 11) & 0x800) | ((l >> 10) & 0x400) | ((l >> 7) & 0x200)
                | ((l >> 5) & 0x100) | ((l >> 4) & 0x080) | ((l >> 1) & 0x040)
                | ((l << 1) & 0x020) | ((l << 2) & 0x010)
        }
        return out
    }

    /// One sample of the envelope, 0..255. The decay and release are not
    /// exponential curves: the chip counts down at a rate divided further as
    /// the level falls -- once below 93, then 54, 26, 14 and 6 -- so the tail
    /// flattens in five visible steps. Replacing that with a smooth
    /// exponential is the single change that makes this sound like a
    /// synthesiser instead of a games machine.
    private func advanceEnvelope(_ v: Int) -> Int {
        let phase = voices[v].phase
        if phase == Env.idle { return 0 }
        var env = voices[v].env

        if phase == Env.attack {
            env += voices[v].aInc                   // the attack is linear
            if env >= 255 << 16 {
                env = 255 << 16
                voices[v].phase = Env.decay
            }
        } else if phase == Env.decay || phase == Env.release {
            let level = env >> 16
            var divisor = 1
            if level <= 6 { divisor = 30 }
            else if level <= 14 { divisor = 16 }
            else if level <= 26 { divisor = 8 }
            else if level <= 54 { divisor = 4 }
            else if level <= 93 { divisor = 2 }
            env -= (phase == Env.decay ? voices[v].dInc : voices[v].rInc) / divisor
            if phase == Env.decay {
                let floorLevel = voices[v].sus << 16
                if env <= floorLevel {
                    env = floorLevel
                    voices[v].phase = Env.sustain
                }
            } else if env <= 0 {
                env = 0
                voices[v].phase = Env.idle
            }
        }
        voices[v].env = env
        return env >> 16
    }

    /// Turn the cutoff and resonance registers into filter coefficients. The
    /// 6581's curve is notoriously non-linear and differs chip to chip, so
    /// there is no correct mapping to reproduce: this is a plain linear sweep.
    private func recomputeFilter() {
        let hz = 200.0 + Double(cutoff) * 5.8
        var f = 2.0 * sin(Double.pi * hz / Double(kChipSampleRate))
        // Resonance 0..15 maps to damping 1.4 down to 0.1.
        let q = 1.4 - Double(resonance) * 0.086
        // The two are NOT independent: a Chamberlin state-variable filter is
        // stable only while f + q < 2, so the usable cutoff depends on the
        // resonance chosen with it.
        let limit = min(1.4, 0.95 * (2.0 - q))
        if f > limit { f = limit }
        coefF = f
        coefQ = q
        dirty = false
    }

    /// Fill `frames` samples into `dry` and `wet` accumulating buffers,
    /// running `tickRoutine` every 50 Hz frame -- on the beat, exactly as a
    /// raster interrupt would have.
    func render(into dest: UnsafeMutablePointer<Float>, frames: Int,
                tick tickRoutine: (Chip) -> Void) {
        if dirty { recomputeFilter() }

        for i in 0..<frames {
            // The 50 Hz frame boundary. A real machine got here by interrupt;
            // the arithmetic is the same either way.
            var countdown = tick - 1
            if countdown <= 0 {
                countdown = kChipFrameSamples
                frame += 1
                tickRoutine(self)
                if dirty { recomputeFilter() }
            }
            tick = countdown

            var dry = 0.0, wet = 0.0

            // Advance all three FIRST, then apply sync. Sync is a ring --
            // voice 1 syncs to 0, 2 to 1, and 0 to 2 -- so there is no order
            // in which every source is already up to date. On a real chip all
            // three oscillators advance together, so the wrap each voice
            // reacts to is detected from its OWN prev-to-raw step.
            var prev = [voices[0].acc, voices[1].acc, voices[2].acc]
            var raw = [0, 0, 0]
            var wrapped = [false, false, false]
            for v in 0..<3 {
                raw[v] = (prev[v] &+ voices[v].step) & 0xFFFFFFFF
                wrapped[v] = ((raw[v] >> 8) & 0xFFFFFF) < ((prev[v] >> 8) & 0xFFFFFF)
            }

            for v in 0..<3 {
                var acc = raw[v]
                let sourceWrapped = wrapped[(v + 2) % 3]
                // Hard sync: when the previous voice's accumulator wraps this
                // one is slammed back to zero. Two oscillators at unrelated
                // pitches, one resetting the other, is the chip lead sound.
                if voices[v].sync && sourceWrapped { acc = 0 }

                let acc24 = (acc >> 8) & 0xFFFFFF
                let was24 = (prev[v] >> 8) & 0xFFFFFF

                // The noise register shifts once per rising edge of
                // accumulator bit 19 -- so noise pitch follows the frequency
                // register, and a rising noise sweep is a rising frequency,
                // not a filter.
                if was24 & 0x80000 == 0 && acc24 & 0x80000 != 0 {
                    let l = voices[v].lfsr
                    let feedback = ((l >> 22) ^ (l >> 17)) & 1
                    voices[v].lfsr = ((l << 1) | feedback) & 0x7FFFFF
                }

                voices[v].prev = prev[v]
                voices[v].acc = acc

                // Ring modulation reads the same generation sync did: the raw
                // accumulators from this sample, before any was reset.
                var ringMSB = 0
                if voices[v].ring { ringMSB = (raw[(v + 2) % 3] >> 8) & 0xFFFFFF }

                let w = waveform(v, acc24, ringMSB)
                let env = advanceEnvelope(v)
                // Centre the waveform before the envelope scales it, or every
                // note-on would put a step of DC through the filter.
                let sample = Double((w - 2048) * env) / 255.0
                if voices[v].filtered { wet += sample } else { dry += sample }
            }

            // A two-pole state-variable filter. The 6581's is analogue and its
            // curve famously varies between chips; this is the honest digital
            // equivalent rather than a model of any particular one.
            low += coefF * band
            var high = wet - low - coefQ * band
            band += coefF * high

            // A state-variable filter is only conditionally stable, and
            // nothing stops a tune asking for a high cutoff and a high
            // resonance together. Left alone the state diverges and becomes
            // NaN -- which is sticky, so the synth would go silent for good.
            // A real filter saturates, so this one does.
            if low.isNaN || band.isNaN { low = 0; band = 0; high = 0 }
            low = min(kFilterCeiling, max(-kFilterCeiling, low))
            band = min(kFilterCeiling, max(-kFilterCeiling, band))

            var filteredOut = 0.0
            if mode & ChipFilter.lowpass != 0 { filteredOut += low }
            if mode & ChipFilter.bandpass != 0 { filteredOut += band }
            if mode & ChipFilter.highpass != 0 { filteredOut += high }

            // Three voices at full envelope reach 3 * 2048; the divisor leaves
            // headroom for the filter's resonant peak, which can exceed it.
            let mixed = (dry + filteredOut) * Double(volume) / 15.0
            dest[i] = Float(min(1.0, max(-1.0, mixed / 8192.0)))
        }
    }
}
