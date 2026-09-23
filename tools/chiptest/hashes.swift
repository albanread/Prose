// The Mojo gamepane's own regression fixtures, reused as ours: twelve effects
// are defined there as chip recipes plus a 50 Hz routine, and their rendered
// output is hashed FNV-1a over the 16-bit quantisation and the numbers
// committed. Four of them are here.
//
// If this chip is a faithful port of that one it reproduces those numbers bit
// for bit -- which is a far stronger claim than "sounds about right", and it
// is what let the search for a wrong note skip the oscillator, the envelope
// and the filter entirely and go straight to the player.
//
// Regenerate deliberately, never to make a red test green.
import Foundation

func hashOf(_ samples: [Float]) -> UInt64 {
    var h: UInt64 = 0xCBF29CE484222325
    for s in samples {
        let v = Double(min(1.0, max(-1.0, s)))
        var q = Int(v * 32767.0)
        if q < 0 { q += 65536 }
        h = (h ^ UInt64(q & 255)) &* 0x100000001B3
        h = (h ^ UInt64((q >> 8) & 255)) &* 0x100000001B3
    }
    return h
}

struct Effect {
    let name: String
    let frames: Int
    let start: (Chip) -> Void
    let frame: (Chip, Int) -> Void
    let expect: UInt64
}

let effects: [Effect] = [
    Effect(name: "zap", frames: 12, start: { c in
        c.setWave(0, ChipWave.saw)
        c.setADSR(0, 0, 5, 4, 4)
        c.setFrequencyHz(0, 1760.0)
    }, frame: { c, f in c.setFrequencyHz(0, 1760.0 - Double(f) * 120.0) },
       expect: 0x56a630415fb74441),

    Effect(name: "shoot", frames: 8, start: { c in
        c.setWave(0, ChipWave.saw | ChipWave.noise)
        c.setADSR(0, 0, 4, 0, 3)
        c.setFrequencyHz(0, 1200.0)
    }, frame: { c, f in c.setFrequencyHz(0, 1200.0 - Double(f) * 110.0) },
       expect: 0x5b6edb0d23533bd5),

    Effect(name: "explode", frames: 30, start: { c in
        c.setWave(0, ChipWave.noise)
        c.setADSR(0, 0, 12, 6, 10)
        c.setFrequencyHz(0, 900.0)
        c.routeFilter(0, true)
        c.setFilter(cutoff: 2047, resonance: 4, mode: ChipFilter.lowpass)
    }, frame: { c, f in
        c.setFrequencyHz(0, 900.0 - Double(f) * 25.0)
        c.setFilter(cutoff: max(120, 2047 - f * 60), resonance: 4, mode: ChipFilter.lowpass)
    }, expect: UInt64(bitPattern: Int64(-0x1528fa4ba63d0303))),

    Effect(name: "click", frames: 3, start: { c in
        c.setWave(0, ChipWave.noise)
        c.setADSR(0, 0, 2, 0, 2)
        c.setFrequencyHz(0, 3000.0)
    }, frame: { _, _ in }, expect: 0x1bc997386f0b5ba4),
]

var failures = 0
for e in effects {
    let chip = Chip()
    chip.setVolume(15)
    chip.routeFilter(0, false)
    e.start(chip)
    chip.gateOn(0)
    let buffer = UnsafeMutablePointer<Float>.allocate(capacity: kChipFrameSamples)
    defer { buffer.deallocate() }
    var out: [Float] = []
    for f in 0..<e.frames {
        e.frame(chip, f)
        chip.render(into: buffer, frames: kChipFrameSamples) { _ in }
        out.append(contentsOf: UnsafeBufferPointer(start: buffer, count: kChipFrameSamples))
    }
    let h = hashOf(out)
    if h == e.expect {
        print("  ok   \(e.name) hashes to its committed value")
    } else {
        print("  FAIL \(e.name) hashes 0x\(String(h, radix: 16)), expected 0x\(String(e.expect, radix: 16))")
        failures += 1
    }
}
print(failures == 0 ? "\nbit-identical to the reference chip"
                    : "\n\(failures) of \(effects.count) differ")
exit(failures == 0 ? 0 : 1)
