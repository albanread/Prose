import Foundation

var failures = 0
func check(_ name: String, _ got: Any, _ want: Any) {
    let g = "\(got)", w = "\(want)"
    if g == w { print("  ok   \(name)") }
    else { print("  FAIL \(name)\n         got  \(g)\n         want \(w)"); failures += 1 }
}

func head(_ body: String, meter: String = "4/4", unit: String = "1/8",
          key: String = "C", q: String = "1/4=120") -> String {
    "X:1\nM:\(meter)\nL:\(unit)\nQ:\(q)\nK:\(key)\n\(body)\n"
}

/// (tick, duration, midi) per note, in source order.
func notes(_ abc: String) -> [[Int]] {
    parseABC(abc).notes.map { [$0.tick, $0.duration, $0.midi] }
}

print("durations")
// L:1/8 at 480 ticks a quarter: one unit is 240.
check("plain units",   notes(head("C2 _E2 G2 _E2 | C4")).map { [$0[0], $0[1]] },
      [[0,480],[480,480],[960,480],[1440,480],[1920,960]])
check("halved /2",     notes(head("c/2d/2c/2d/2")).map { $0[1] }, [120,120,120,120])
check("bare slash",    notes(head("a/b//c")).map { $0[1] }, [120,60,240])
check("dotted 3/2",    notes(head("a3/2b/2")).map { $0[1] }, [360,120])

print("tuplets")
// (3 is three in the time of two: 240 * 2/3 = 160 each, and only three of them.
check("(3 triplet",    notes(head("(3abc d")).map { $0[1] }, [160,160,160,240])
check("(2 duplet",     notes(head("(2ab c")).map { $0[1] }, [360,360,240])
check("(3 explicit",   notes(head("(3:2:2ab c")).map { $0[1] }, [160,160,240])

print("broken rhythm")
check("a>b",           notes(head("a>b")).map { $0[1] }, [360,120])
check("a<b",           notes(head("a<b")).map { $0[1] }, [120,360])
check("a>b ticks",     notes(head("a>b c")).map { $0[0] }, [0,360,480])
check("a>>b",          notes(head("a>>b")).map { $0[1] }, [420,60])

print("ties and chords")
check("tie joins",     notes(head("C-C D")), [[0,480,60],[480,240,62]])
check("chord",         notes(head("[CEG]2 D")),
      [[0,480,60],[0,480,64],[0,480,67],[480,240,62]])

print("pitch")
check("octaves",       notes(head("C,, C, C c c'")).map { $0[2] }, [36,48,60,72,84])
check("Cm key",        notes(head("C D E F G A B", key: "Cm")).map { $0[2] },
      [60,62,63,65,67,68,70])          // three flats: B E A
check("accidental holds", notes(head("^F F | F")).map { $0[2] }, [66,66,65])
check("natural cancels",  notes(head("E =E", key: "Cm")).map { $0[2] }, [63,64])
check("Am key",        notes(head("A B c", key: "Am")).map { $0[2] }, [69,71,72])
check("Ador is G",     notes(head("F c", key: "Ador")).map { $0[2] }, [66,72])

print("tempo")
// Q:1/4=210 -> a quarter is 60/210 s; a 240-tick eighth is 48000*60/(210*480)
// samples = 28571.4 -> the schedule rounds once, at the end.
let t = parseABC(head("C2 C2", q: "1/4=210"))
check("ticks per beat", t.ticksPerBeat, 480)
check("bpm", t.bpm, 210)
let steps = schedule(t)
check("first note at 0", steps.first(where: { $0.kind == .noteOn })!.sample, 0)
check("second at 3428", steps.filter { $0.kind == .noteOn }[1].sample,
      (480 * 48000 * 60 + 210 * 480 / 2) / (210 * 480))

print("chip settings")
let c = parseABC(head("[I:chip v=1 wave=pulse pw=500 a=0 d=4 s=6 r=3 vol=12]\nC"))
check("chip events", c.chips.count, 7)
check("wave is pulse", c.chips.first(where: { $0.param == CP.wave })!.value, ChipWave.pulse)
check("pw", c.chips.first(where: { $0.param == CP.pw })!.value, 500)
let arp = parseABC(head("[I:chip v=1 arp=047]\nC"))
check("arp packed", arp.chips.first(where: { $0.param == CP.arp })!.value,
      (3 << 32) | (0) | (4 << 4) | (7 << 8))
let vib = parseABC(head("[I:chip v=1 vib=8/3]\nC"))
check("vib packed", vib.chips.first(where: { $0.param == CP.vib })!.value, (8 << 8) | 3)

print("")
print(failures == 0 ? "ALL PASS" : "\(failures) FAILED")
exit(failures == 0 ? 0 : 1)
