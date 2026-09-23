// abc.swift: ABC notation in, sample-stamped steps out.
//
// Ported from the Mojo gamepane's abc/ module. Two decisions from it are
// kept because everything else depends on them:
//
// **Time is an integer.** Durations count in ticks at 480 to the quarter --
// 1920 to the whole -- never in seconds. That divides exactly by everything
// ABC can ask for, so a tune that should land on the bar line does, however
// many dots and halvings came before it. Floating-point timestamps drift by
// a fraction of a tick a note, which is inaudible until a few hundred notes
// have gone by and the voices are visibly apart.
//
// **A key signature is arithmetic, not a table.** The alteration a key
// applies to a letter follows from the circle of fifths.
//
// The grammar read here is the subset the tunes use: the X M L Q K V header,
// notes with accidentals, octaves and durations, rests, bars, chords, ties,
// tuplets, broken rhythm, and the inline fields [I:chip ...] [V:n] [K:] [L:]
// [M:] [Q:]. Not read: grace notes, decorations, lyrics, chord symbols and
// repeat expansion. Unknown inline keys are IGNORED rather than refused,
// which is the whole extension mechanism -- an old build plays a new tune's
// notes.
//
// Anything here that silently changes a duration is the dangerous part, so
// the two that do -- tuplets and broken rhythm -- are implemented rather than
// skipped, and `Tune.barWarnings` counts every bar line the durations did not
// land on. A parser that quietly drops `(3` plays the right notes at the
// wrong times, which is the one failure nobody hears until the voices drift
// apart.
import Foundation

let kTicksPerQuarter = 480
let kTicksPerWhole = kTicksPerQuarter * 4

/// Which register a chip event carries. Per-voice ids are below 20, global
/// ones above, so the player can tell them apart with one comparison.
enum CP {
    static let wave = 1, pw = 2, a = 3, d = 4, s = 5, r = 6, filt = 7
    static let cutoff = 20, res = 21, fmode = 22, vol = 23
    static let pan = 30, echo = 31, etime = 32, efb = 33
    // The 50 Hz performance macros: where a chip tune actually lives.
    static let arp = 40, vib = 41, slide = 42, pwm = 43, sweep = 44, trem = 45
}

enum StepKind { case noteOn, noteOff, chip }

/// One thing to do, at one sample.
struct Step {
    var sample = 0
    var kind = StepKind.noteOn
    var voice = 0           // 1-based, as the ABC is
    var midi = 0
    var velocity = 0
    var param = 0           // chip steps: which register
    var value = 0
}

private struct ABCVoice {
    var tick = 0
    var unitNum = 1, unitDen = 8
    var meterNum = 4, meterDen = 4
    var keySharps = 0
    var velocity = 100
    var accidentals = [Int](repeating: 99, count: 7)    // 99: none this bar
}

/// What a music line carries from one note to the next.
private struct MusicCtx {
    var tupletLeft = 0
    var tupletNum = 1, tupletDen = 1
    var broken = 0                  // >0 after `>`, <0 after `<`, magnitude the count
    var lastNote = -1               // index into Tune.notes, for a `>` to reach back to
    var pendingTie = false
}

/// How many notes' worth of time a `(p` tuplet occupies. The defaults are
/// ABC's own and they depend on the meter: in compound time a (3 is still
/// three in the time of two, but (5, (7 and (9 change.
private func tupletDefaultDen(_ p: Int, compound: Bool) -> Int {
    switch p {
    case 2: return 3
    case 3: return 2
    case 4: return 3
    case 6: return 2
    case 8: return 3
    case 5, 7, 9: return compound ? 3 : 2
    default: return 2
    }
}

private let kNoAccidental = 99
private let kSemitone = [0, 2, 4, 5, 7, 9, 11]          // C D E F G A B
private let kSharpOrder = [3, 0, 4, 1, 5, 2, 6]         // F C G D A E B

/// What a key signature does to one letter: +1, 0 or -1. Derived rather than
/// stored -- the whole of Western key signatures is two orderings of the same
/// seven letters.
private func keyAlter(_ sharps: Int, _ letter: Int) -> Int {
    if sharps > 0 {
        for k in 0..<min(sharps, 7) where kSharpOrder[k] == letter { return 1 }
    } else if sharps < 0 {
        for k in 0..<min(-sharps, 7) where kSharpOrder[6 - k] == letter { return -1 }
    }
    return 0
}

/// The sharp count for a K: field: positive sharps, negative flats. The mode
/// suffixes are not decoration -- a tune marked K:Ador is in G major's
/// signature, and reading it as A major puts three accidentals in the wrong
/// place for the whole tune.
private func keySharpsFor(_ name: String) -> Int {
    let chars = Array(name)
    guard let first = chars.first else { return 0 }
    let tonic = String(first).uppercased()
    var fifths: Int
    switch tonic {
    case "F": fifths = -1
    case "C": fifths = 0
    case "G": fifths = 1
    case "D": fifths = 2
    case "A": fifths = 3
    case "E": fifths = 4
    case "B": fifths = 5
    default: return 0
    }
    var at = 1
    if chars.count > 1 && chars[1] == "#" { fifths += 7; at = 2 }
    else if chars.count > 1 && chars[1] == "b" { fifths -= 7; at = 2 }

    let rest = String(chars[at...]).filter { !$0.isWhitespace }.lowercased()
    if rest.hasPrefix("maj") || rest.hasPrefix("ion") { }
    else if rest.hasPrefix("mix") { fifths -= 1 }
    else if rest.hasPrefix("m") { fifths -= 3 }
    else if rest.hasPrefix("dor") { fifths -= 2 }
    else if rest.hasPrefix("phr") { fifths -= 4 }
    else if rest.hasPrefix("lyd") { fifths += 1 }
    else if rest.hasPrefix("loc") { fifths -= 5 }
    else if rest.hasPrefix("aeo") { fifths -= 3 }
    return fifths
}

/// `chip v=2 wave=pulse pw=900 d=4 cutoff=1800` into (voice, param, value)
/// triples. Unknown keys are ignored: a tune carrying a setting this build
/// does not have should still play the notes.
private func chipSettings(_ text: String, currentVoice: Int) -> [(Int, Int, Int)] {
    var out: [(Int, Int, Int)] = []
    var voice = currentVoice
    for field in text.split(whereSeparator: { $0 == " " || $0 == "," }) {
        guard let equals = field.firstIndex(of: "=") else { continue }   // `chip`
        let key = String(field[field.startIndex..<equals])
        let value = String(field[field.index(after: equals)...])
        if key.isEmpty || value.isEmpty { continue }
        if key == "v" {
            if let n = Int(value) { voice = n }
            continue
        }

        var param = -1, number = -1
        switch key {
        case "wave":
            param = CP.wave
            var w = 0
            for name in value.split(separator: "+") {     // + joins waveforms
                switch name {
                case "tri": w |= ChipWave.tri
                case "saw": w |= ChipWave.saw
                case "pulse": w |= ChipWave.pulse
                case "noise": w |= ChipWave.noise
                default: break
                }
            }
            number = w
        case "mode":
            param = CP.fmode
            switch value {
            case "bp": number = ChipFilter.bandpass
            case "hp": number = ChipFilter.highpass
            default: number = ChipFilter.lowpass
            }
        case "filt":
            param = CP.filt
            number = (value == "on" || value == "1") ? 1 : 0
        case "arp":
            // Hex digits, first heard first: arp=047 is root, +4, +7. Packed
            // as count<<32 | nibbles (first digit lowest), so one integer
            // carries the table and 0 means off.
            param = CP.arp
            var packed = 0, count = 0
            for c in value {
                guard let digit = c.hexDigitValue, count < 8 else { continue }
                packed |= digit << (4 * count)
                count += 1
            }
            number = count > 0 ? (count << 32) | packed : 0
        case "vib", "pwm", "trem":
            // A pair, depth/rate: vib=8/3. Packed depth<<8 | rate; a rate of
            // zero is off, whatever the depth says.
            param = key == "vib" ? CP.vib : (key == "pwm" ? CP.pwm : CP.trem)
            let parts = value.split(separator: "/", maxSplits: 1)
            let depth = min(255, Int(parts.first ?? "0") ?? 0)
            let rate = parts.count > 1 ? min(255, Int(parts[1]) ?? 0) : 0
            number = (depth << 8) | rate
        case "pan", "sweep":
            // Signed, riding a bias so `number >= 0` stays the one gate.
            guard let n = Int(value) else { continue }
            param = key == "pan" ? CP.pan : CP.sweep
            number = key == "pan" ? max(-128, min(127, n)) + 128 : n + 1024
        default:
            let simple: [String: Int] = [
                "pw": CP.pw, "a": CP.a, "d": CP.d, "s": CP.s, "r": CP.r,
                "cutoff": CP.cutoff, "res": CP.res, "vol": CP.vol,
                "slide": CP.slide, "echo": CP.echo, "etime": CP.etime,
                "efb": CP.efb
            ]
            if let p = simple[key], let n = Int(value) { param = p; number = n }
        }
        if param >= 0 && number >= 0 { out.append((voice, param, number)) }
    }
    return out
}


/// A tune, read. Events carry absolute ticks; the schedule turns those into
/// samples once, at the end, so nothing accumulates.
struct Tune {
    struct Note { var tick = 0, duration = 0, voice = 0, midi = 0, velocity = 0 }
    struct ChipSet { var tick = 0, voice = 0, param = 0, value = 0 }

    var notes: [Note] = []
    var chips: [ChipSet] = []
    var bpm = 120, tempoNum = 1, tempoDen = 4
    /// Bar lines the accumulated durations did not land on. ABC does not
    /// require a bar to be full -- several of these tunes write half bars on
    /// purpose -- so this is a report and not a refusal. A tune that suddenly
    /// grows one has had a duration read wrongly.
    var barWarnings: [String] = []

    var ticksPerBeat: Int { max(1, (kTicksPerWhole * tempoNum) / tempoDen) }
}


/// Read one ABC tune. Never throws: a line it cannot make sense of is skipped,
/// because a player that refuses a tune is worse than one that plays most of it.
func parseABC(_ source: String) -> Tune {
    var tune = Tune()
    var voices: [Int: ABCVoice] = [:]
    var current = 1
    var unitSetInHeader = false

    func voice(_ n: Int) -> ABCVoice {
        if let v = voices[n] { return v }
        var v = ABCVoice()
        if let one = voices[1] { v.unitNum = one.unitNum; v.unitDen = one.unitDen
                                 v.keySharps = one.keySharps }
        voices[n] = v
        return v
    }

    /// An inline or header field. Returns false when it was not one we know.
    func applyField(_ letter: Character, _ value: String) {
        switch letter {
        case "V":
            let digits = value.prefix { $0.isNumber }
            current = Int(digits) ?? 1
            var v = voice(current)
            v.accidentals = [Int](repeating: kNoAccidental, count: 7)
            voices[current] = v
        case "K":
            var v = voice(current)
            v.keySharps = keySharpsFor(value)
            v.accidentals = [Int](repeating: kNoAccidental, count: 7)
            voices[current] = v
        case "L":
            let parts = value.split(separator: "/")
            if parts.count == 2, let n = Int(parts[0]), let d = Int(parts[1]), d > 0 {
                var v = voice(current)
                v.unitNum = n
                v.unitDen = d
                voices[current] = v
                unitSetInHeader = true
            }
        case "M":
            let parts = value.split(separator: "/")
            if parts.count == 2, let n = Int(parts[0]), let d = Int(parts[1]), d > 0 {
                var v = voice(current)
                v.meterNum = n
                v.meterDen = d
                if !unitSetInHeader {
                    // No L: yet, so the meter picks the default unit: 1/16
                    // under 0.75, 1/8 at or above it. ABC's own rule.
                    v.unitNum = 1
                    v.unitDen = Double(n) / Double(d) < 0.75 ? 16 : 8
                }
                voices[current] = v
            }
        case "Q":
            // Q:1/4=120, or a bare number meaning quarters.
            if let equals = value.firstIndex(of: "=") {
                let beat = value[value.startIndex..<equals].split(separator: "/")
                if beat.count == 2, let n = Int(beat[0]), let d = Int(beat[1]), d > 0 {
                    tune.tempoNum = n
                    tune.tempoDen = d
                }
                tune.bpm = Int(value[value.index(after: equals)...].trimmingCharacters(
                    in: .whitespaces)) ?? tune.bpm
            } else if let n = Int(value.trimmingCharacters(in: .whitespaces)) {
                tune.bpm = n
            }
        default: break
        }
    }

    for rawLine in source.split(separator: "\n", omittingEmptySubsequences: false) {
        let line = String(rawLine).trimmingCharacters(in: .whitespaces)
        if line.isEmpty || line.hasPrefix("%") { continue }

        // A header line: one letter, a colon, the value.
        let chars = Array(line)
        if chars.count > 1 && chars[1] == ":" && chars[0].isLetter {
            let letter = chars[0]
            let value = String(chars[2...]).trimmingCharacters(in: .whitespaces)
            if letter == "X" || letter == "T" || letter == "w" || letter == "%" { continue }
            if letter == "I" {
                for (v, p, n) in chipSettings(value, currentVoice: current) {
                    tune.chips.append(Tune.ChipSet(tick: voice(v).tick, voice: v,
                                                   param: p, value: n))
                }
                continue
            }
            applyField(letter, value)
            continue
        }

        // A music line.
        var ctx = MusicCtx()
        var i = 0
        while i < chars.count {
            let c = chars[i]

            if c == " " || c == "\t" { i += 1; continue }
            if c == "%" { break }                       // a comment to end of line
            if c == "|" {
                // The bar line: accidentals stop holding, and the durations
                // since the last one either add up or they do not.
                let v = voice(current)
                let bar = (kTicksPerWhole * v.meterNum) / max(1, v.meterDen)
                if bar > 0 && v.tick % bar != 0 {
                    tune.barWarnings.append("voice \(current): bar line at tick "
                        + "\(v.tick), which is \(v.tick % bar) past a \(bar)-tick bar")
                }
                voices[current]!.accidentals = [Int](repeating: kNoAccidental, count: 7)
                i += 1
                continue
            }
            if c == ":" { i += 1; continue }            // a repeat mark, not expanded
            if c == "-" { ctx.pendingTie = true; i += 1; continue }

            if c == "(" {
                // A slur, or a tuplet if a digit follows: (p, (p:q or (p:q:r.
                if i + 1 < chars.count && chars[i + 1].isNumber {
                    i += 1
                    let p = readInt(chars, &i) ?? 3
                    let v = voice(current)
                    let compound = v.meterDen == 8 && v.meterNum % 3 == 0
                    var q = tupletDefaultDen(p, compound: compound)
                    var r = p
                    if i < chars.count && chars[i] == ":" {
                        i += 1
                        if let qq = readInt(chars, &i) { q = qq }
                        if i < chars.count && chars[i] == ":" {
                            i += 1
                            if let rr = readInt(chars, &i) { r = rr }
                        }
                    }
                    ctx.tupletLeft = r
                    ctx.tupletNum = q
                    ctx.tupletDen = p
                    continue
                }
                i += 1
                continue
            }
            if c == ")" { i += 1; continue }

            if c == ">" || c == "<" {
                // Broken rhythm. The mark lengthens the note before it and
                // shortens the one after, or the other way round for `<`.
                let mark = c
                var count = 0
                while i < chars.count && chars[i] == mark { count += 1; i += 1 }
                if ctx.lastNote >= 0 {
                    var half = 2
                    for _ in 1..<max(count, 1) { half *= 2 }
                    let at = ctx.lastNote
                    let was = tune.notes[at].duration
                    let now = mark == ">" ? (was * (2 * half - 1)) / half : was / half
                    tune.notes[at].duration = max(1, now)
                    let owner = tune.notes[at].voice
                    voices[owner]!.tick += tune.notes[at].duration - was
                }
                ctx.broken = mark == ">" ? count : -count
                continue
            }

            // An inline field or a chord.
            if c == "[" {
                if i + 2 < chars.count && chars[i + 2] == ":" {
                    let letter = chars[i + 1]
                    var close = i + 3
                    while close < chars.count && chars[close] != "]" { close += 1 }
                    let value = String(chars[(i + 3)..<min(close, chars.count)])
                        .trimmingCharacters(in: .whitespaces)
                    if letter == "I" {
                        for (v, p, n) in chipSettings(value, currentVoice: current) {
                            tune.chips.append(Tune.ChipSet(tick: voice(v).tick,
                                voice: v, param: p, value: n))
                        }
                    } else {
                        applyField(letter, value)
                    }
                    i = min(close + 1, chars.count)
                    continue
                }
                if i + 1 < chars.count && chars[i + 1].isNumber {
                    i += 2                              // [1 or [2: an ending
                    continue
                }
                // A chord: every note inside sounds at the same tick, and the
                // chord's own length -- written after the bracket -- governs.
                let startTick = voice(current).tick
                var members: [Int] = []
                var memberTicks = 0
                i += 1
                while i < chars.count && chars[i] != "]" {
                    if chars[i] == " " { i += 1; continue }
                    guard let note = readNote(chars, &i, &voices, current) else {
                        i += 1
                        continue
                    }
                    let written = readDuration(chars, &i, unitTicks(voice(current)))
                    if members.isEmpty { memberTicks = written ?? unitTicks(voice(current)) }
                    members.append(note)
                }
                if i < chars.count { i += 1 }           // the ]
                let outer = readDuration(chars, &i, unitTicks(voice(current)))
                let length = adjust(outer ?? memberTicks, &ctx)
                for m in members {
                    tune.notes.append(Tune.Note(tick: startTick, duration: length,
                        voice: current, midi: m, velocity: voice(current).velocity))
                }
                if !members.isEmpty { ctx.lastNote = tune.notes.count - 1 }
                voices[current]!.tick = startTick + length
                continue
            }

            if c == "z" || c == "x" || c == "Z" {
                i += 1
                let v = voice(current)
                // z is one unit; Z is a whole bar, and its number counts bars.
                let base = c == "Z" ? (kTicksPerWhole * v.meterNum) / max(1, v.meterDen)
                                    : unitTicks(v)
                let written = readDuration(chars, &i, base) ?? base
                voices[current]!.tick += adjust(written, &ctx)
                continue
            }

            if let note = readNote(chars, &i, &voices, current) {
                let written = readDuration(chars, &i, unitTicks(voice(current)))
                let d = adjust(written ?? unitTicks(voice(current)), &ctx)
                let tick = voice(current).tick
                if ctx.pendingTie, let last = tune.notes.indices.last(where: {
                        tune.notes[$0].voice == current && tune.notes[$0].midi == note
                            && tune.notes[$0].tick + tune.notes[$0].duration == tick }) {
                    // A tie means one sound, not two: extend the first and
                    // never strike the second.
                    tune.notes[last].duration += d
                    ctx.lastNote = last
                } else {
                    tune.notes.append(Tune.Note(tick: tick, duration: d,
                        voice: current, midi: note,
                        velocity: voice(current).velocity))
                    ctx.lastNote = tune.notes.count - 1
                }
                ctx.pendingTie = false
                voices[current]!.tick = tick + d
                continue
            }
            i += 1                                      // anything else: skip it
        }
    }
    return tune
}


/// Apply a running tuplet and a pending broken-rhythm mark. Everything that
/// produces a duration goes through here, which is the only way a tuplet can
/// be right: it is not a property of a note, it is a property of the next r
/// notes whatever they turn out to be.
private func adjust(_ ticksIn: Int, _ ctx: inout MusicCtx) -> Int {
    var ticks = ticksIn
    if ctx.tupletLeft > 0 {
        ticks = (ticks * ctx.tupletNum) / ctx.tupletDen
        ctx.tupletLeft -= 1
    }
    if ctx.broken != 0 {
        // The note after `>` is shortened; after `<` it is lengthened. The
        // note before was adjusted when the mark was read.
        var half = 2
        for _ in 1..<max(abs(ctx.broken), 1) { half *= 2 }
        ticks = ctx.broken > 0 ? ticks / half : (ticks * (2 * half - 1)) / half
        ctx.broken = 0
    }
    return max(1, ticks)
}


private func readInt(_ chars: [Character], _ i: inout Int) -> Int? {
    var n = 0
    var any = false
    while i < chars.count, chars[i].isNumber, let d = chars[i].wholeNumberValue {
        n = n * 10 + d
        i += 1
        any = true
    }
    return any ? n : nil
}


private func unitTicks(_ v: ABCVoice) -> Int {
    max(1, (kTicksPerWhole * v.unitNum) / v.unitDen)
}


/// One note's pitch, or nil. Accidentals are applied and recorded: an explicit
/// one governs the rest of the bar for that letter, and in its absence the key
/// signature governs.
private func readNote(_ chars: [Character], _ i: inout Int,
                      _ voices: inout [Int: ABCVoice], _ current: Int) -> Int? {
    var j = i
    var alter = kNoAccidental
    // ^ and _ may be doubled; = cancels.
    while j < chars.count {
        switch chars[j] {
        case "^": alter = alter == kNoAccidental ? 1 : alter + 1; j += 1
        case "_": alter = alter == kNoAccidental ? -1 : alter - 1; j += 1
        case "=": alter = 0; j += 1
        default:
            guard let letter = "CDEFGAB".firstIndex(of: Character(
                    String(chars[j]).uppercased())) else { return nil }
            let index = "CDEFGAB".distance(from: "CDEFGAB".startIndex, to: letter)
            var octave = chars[j].isLowercase ? 6 : 5   // ABC's C is middle C
            j += 1
            while j < chars.count {
                if chars[j] == "'" { octave += 1; j += 1 }
                else if chars[j] == "," { octave -= 1; j += 1 }
                else { break }
            }
            if alter != kNoAccidental { voices[current]!.accidentals[index] = alter }
            var applied = keyAlter(voices[current]!.keySharps, index)
            let held = voices[current]!.accidentals[index]
            if held != kNoAccidental { applied = held }
            i = j
            return octave * 12 + kSemitone[index] + applied
        }
    }
    return nil
}


/// The length that follows a note, rest or chord. ABC writes a multiplier, a
/// divisor after `/`, or both: A2, A/2, A/, A//, A3/2. A bare `/` halves, and
/// each extra `/` halves again.
private func readDuration(_ chars: [Character], _ i: inout Int, _ unit: Int) -> Int? {
    let mul = readInt(chars, &i)
    var div = 1
    var sawSlash = false
    while i < chars.count && chars[i] == "/" {
        i += 1
        sawSlash = true
        div *= readInt(chars, &i).map { max(1, $0) } ?? 2
    }
    if mul == nil && !sawSlash { return nil }
    return max(1, (unit * (mul ?? 1)) / div)
}


/// Exact, and rounded once at the end rather than accumulated. The
/// alternative -- working in seconds and sleeping until each event is due --
/// asks the operating system to wake a thread at a moment, which it does late
/// by whatever the scheduler is busy with. At 120bpm a semiquaver is 125 ms,
/// so a 5 ms error is 4% of a note: audible as looseness, and worse, it varies.
func tickToSample(_ tick: Int, bpm: Int, perBeat: Int, sampleRate: Int) -> Int {
    let denom = bpm * perBeat
    if denom <= 0 { return 0 }
    return (tick * sampleRate * 60 + denom / 2) / denom
}


/// A tune into steps, sorted by sample, note-offs before note-ons at the same
/// instant so a repeated pitch re-strikes rather than being cut by its own
/// predecessor's release.
func schedule(_ tune: Tune, sampleRate: Int = kChipSampleRate) -> [Step] {
    var steps: [Step] = []
    let perBeat = tune.ticksPerBeat

    for c in tune.chips {
        steps.append(Step(sample: tickToSample(c.tick, bpm: tune.bpm,
            perBeat: perBeat, sampleRate: sampleRate), kind: .chip,
            voice: c.voice, midi: 0, velocity: 0, param: c.param, value: c.value))
    }
    for n in tune.notes {
        steps.append(Step(sample: tickToSample(n.tick, bpm: tune.bpm,
            perBeat: perBeat, sampleRate: sampleRate), kind: .noteOn,
            voice: n.voice, midi: n.midi, velocity: n.velocity))
        steps.append(Step(sample: tickToSample(n.tick + n.duration, bpm: tune.bpm,
            perBeat: perBeat, sampleRate: sampleRate), kind: .noteOff,
            voice: n.voice, midi: n.midi, velocity: 0))
    }

    steps.sort {
        if $0.sample != $1.sample { return $0.sample < $1.sample }
        return order($0.kind) < order($1.kind)
    }
    return steps
}

private func order(_ kind: StepKind) -> Int {
    switch kind {
    case .chip: return 0        // registers first: a note wants them set
    case .noteOff: return 1
    case .noteOn: return 2
    }
}
