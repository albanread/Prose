import Foundation
func log(_ s: String) { print(s) }

let data = try! Data(contentsOf: URL(fileURLWithPath: "tunes.json"))
let tunes = try! JSONSerialization.jsonObject(with: data) as! [String: String]

/// The reference player, transcribed from the Mojo's chipplay.render_scheduled:
/// one chip, three voices, first-free allocation, and a silent tick.
func referenceRender(_ source: String, frames: Int) -> [Float] {
    let chip = Chip()
    let steps = schedule(parseABC(source))
    var note = [-1, -1, -1]
    var age = [0, 0, 0]

    func noteOn(_ midi: Int) {
        var chosen = -1
        for v in 0..<3 where note[v] < 0 { chosen = v; break }
        if chosen < 0 { for v in 0..<3 where chip.isIdle(v) { chosen = v; break } }
        if chosen < 0 {
            var oldest = age[0]; chosen = 0
            for v in 1..<3 where age[v] < oldest { oldest = age[v]; chosen = v }
        }
        note[chosen] = midi
        chip.setFrequencyHz(chosen, 440.0 * exp2((Double(midi) - 69.0) / 12.0))
        chip.gateOn(chosen)
    }
    func noteOff(_ midi: Int) {
        for v in 0..<3 where note[v] == midi { note[v] = -1; chip.gateOff(v); return }
    }
    var adsr = [(0, 9, 0, 9), (0, 9, 0, 9), (0, 9, 0, 9)]
    func applyChip(_ v: Int, _ param: Int, _ value: Int) {
        switch param {
        case CP.cutoff: chip.setFilter(cutoff: value, resonance: chip.filterResonance, mode: chip.filterMode)
        case CP.res: chip.setFilter(cutoff: chip.filterCutoff, resonance: value, mode: chip.filterMode)
        case CP.fmode: chip.setFilter(cutoff: chip.filterCutoff, resonance: chip.filterResonance, mode: value)
        case CP.vol: chip.setVolume(value)
        default:
            guard v >= 0 && v <= 2 else { return }
            switch param {
            case CP.wave: chip.setWave(v, value)
            case CP.pw: chip.setPulseWidth(v, value)
            case CP.filt: chip.routeFilter(v, value != 0)
            case CP.a: adsr[v].0 = value; chip.setADSR(v, adsr[v].0, adsr[v].1, adsr[v].2, adsr[v].3)
            case CP.d: adsr[v].1 = value; chip.setADSR(v, adsr[v].0, adsr[v].1, adsr[v].2, adsr[v].3)
            case CP.s: adsr[v].2 = value; chip.setADSR(v, adsr[v].0, adsr[v].1, adsr[v].2, adsr[v].3)
            case CP.r: adsr[v].3 = value; chip.setADSR(v, adsr[v].0, adsr[v].1, adsr[v].2, adsr[v].3)
            default: break
            }
        }
    }

    var out = [Float](repeating: 0, count: frames)
    let buf = UnsafeMutablePointer<Float>.allocate(capacity: frames)
    defer { buf.deallocate() }
    var filled = 0, cursor = 0, now = 0
    while filled < frames {
        while cursor < steps.count && steps[cursor].sample <= now {
            let s = steps[cursor]
            switch s.kind {
            case .chip: applyChip(s.voice - 1, s.param, s.value)
            case .noteOn: noteOn(s.midi); age[note.firstIndex(of: s.midi) ?? 0] = now
            case .noteOff: noteOff(s.midi)
            }
            cursor += 1
        }
        var span = frames - filled
        if cursor < steps.count { span = min(span, max(1, steps[cursor].sample - now)) }
        chip.render(into: buf + filled, frames: span) { _ in }
        filled += span
        now += span
    }
    for i in 0..<frames { out[i] = buf[i] }
    return out
}

let seconds = 4
let frames = kChipSampleRate * seconds
var anyDiff = false
print("tune             first divergence      max |difference|")
for name in tunes.keys.sorted() where name.hasPrefix("M_") {
    let reference = referenceRender(tunes[name]!, frames: frames)

    let trio = Trio()
    // Hold the engine to the reference's own allocation rule, so this
    // measures the engine and not the improvement Prose makes on top of it.
    trio.referenceAllocation = true
    _ = trio.play(tunes[name]!, track: 0, loop: false)
    let buf = UnsafeMutablePointer<Float>.allocate(capacity: frames * 2)
    defer { buf.deallocate() }
    trio.render(into: buf, frames: frames)

    // Chip 0's pan gain, exactly as the trio computes it: pan 0 maps to
    // (0 + 128) / 255, which is not quite a half.
    let gain = Float(1.0 / cos((128.0 / 255.0) * Double.pi / 2))
    var first = -1
    var worst: Float = 0
    for i in 0..<frames {
        let mine = buf[i * 2] * gain
        let d = abs(mine - reference[i])
        if d > 0.0005 && first < 0 { first = i }
        worst = max(worst, d)
    }
    let where_ = first < 0 ? "none" : String(format: "%.4f s", Double(first) / Double(kChipSampleRate))
    if first >= 0 { anyDiff = true }
    print(name + String(repeating: " ", count: max(0, 17 - name.count))
          + where_ + String(repeating: " ", count: max(0, 22 - where_.count))
          + String(format: "%.5f", worst))
}
print("")
print(anyDiff ? "the trio does NOT match the reference player"
              : "the trio matches the reference player exactly")
