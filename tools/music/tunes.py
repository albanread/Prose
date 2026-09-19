#!/usr/bin/env python3
"""Write the demo tunes that ship in data/music as standard MIDI files.

    python3 tools/music/tunes.py /Volumes/HaikuSrc/haiku/data/music

MidiPlayer opens to an empty file picker otherwise: nothing in the image is
a .mid. These are here rather than downloaded so their provenance is plain -
every piece is long out of copyright (Pachelbel d. 1706, Beethoven d. 1827) -
and so they can be regenerated.

They are short arrangements written by ear, not urtext transcriptions: enough
to recognise the tune and to hear the General MIDI soundfont (and the Prose
MIDI port) doing their job.

Format 1 SMF, following the specification: a tempo track, then one track per
voice. Delta times are variable-length quantities; a note is a 0x9n on and a
0x8n off, and note-offs are emitted before note-ons at the same tick so a
repeated note is not cut short by its own predecessor.
"""
import struct
import sys
import os

DIVISION = 480                      # ticks per quarter note

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def pitch(name):
    """"C4" -> 60 (middle C), "D#5" -> 75, "Bb3" -> 58."""
    step = NOTE[name[0].upper()]
    i = 1
    while i < len(name) and name[i] in '#b':
        step += 1 if name[i] == '#' else -1
        i += 1
    return 12 * (int(name[i:]) + 1) + step


def vlq(value):
    """A variable-length quantity, as delta times are encoded."""
    out = bytes([value & 0x7F])
    value >>= 7
    while value:
        out = bytes([(value & 0x7F) | 0x80]) + out
        value >>= 7
    return out


def sequence(items, start=0.0):
    """[(note, beats), ...] -> [(start, beats, pitch)].

    note is a name, a list of names (a chord) or None (a rest); beats are
    quarter notes, so 0.25 is a semiquaver.
    """
    notes, t = [], start
    for what, beats in items:
        if what is not None:
            for name in ([what] if isinstance(what, str) else what):
                notes.append((t, beats, pitch(name)))
        t += beats
    return notes


def track(name, notes, program=0, channel=0, velocity=76):
    """One MTrk: a name, an instrument and the notes."""
    events = []                      # (tick, order, bytes)
    events.append((0, 0, b'\xFF\x03' + vlq(len(name)) + name.encode()))
    events.append((0, 0, bytes([0xC0 | channel, program])))
    for start, beats, key in notes:
        on = int(round(start * DIVISION))
        off = int(round((start + beats) * DIVISION))
        # order 0 sorts note-offs ahead of note-ons at the same tick
        events.append((on, 1, bytes([0x90 | channel, key, velocity])))
        events.append((off, 0, bytes([0x80 | channel, key, 0])))
    events.sort(key=lambda e: (e[0], e[1]))

    data, last = b'', 0
    for tick, _, payload in events:
        data += vlq(tick - last) + payload
        last = tick
    data += vlq(0) + b'\xFF\x2F\x00'
    return b'MTrk' + struct.pack('>I', len(data)) + data


def tempo_track(bpm, beats_per_bar=4, beat=4, title=''):
    data = b''
    if title:
        data += vlq(0) + b'\xFF\x03' + vlq(len(title)) + title.encode()
    data += vlq(0) + b'\xFF\x51\x03' + struct.pack('>I', int(60_000_000 / bpm))[1:]
    denominator = {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[beat]
    data += vlq(0) + b'\xFF\x58\x04' + bytes([beats_per_bar, denominator, 24, 8])
    data += vlq(0) + b'\xFF\x2F\x00'
    return b'MTrk' + struct.pack('>I', len(data)) + data


def write(path, tempo, tracks, beats_per_bar=4, beat=4, title=''):
    chunks = [tempo_track(tempo, beats_per_bar, beat, title)] + tracks
    head = b'MThd' + struct.pack('>IHHH', 6, 1, len(chunks), DIVISION)
    with open(path, 'wb') as f:
        f.write(head + b''.join(chunks))
    print('wrote %s (%d bytes, %d tracks)'
          % (os.path.basename(path), len(head) + sum(len(c) for c in chunks), len(chunks)))


# ---------------------------------------------------------------------------
# The tunes. Beats are quarter notes: 0.25 is a semiquaver, 1.5 a dotted crotchet.
# ---------------------------------------------------------------------------

def fur_elise():
	"""Beethoven, Bagatelle in A minor WoO 59 (1810): the A section, twice.

	3/8, so a bar is 1.5 beats. The right hand alternates a semiquaver figure
	with a held note; under each held note the left hand rolls the chord.
	"""
	figure = [('E5', .25), ('D#5', .25), ('E5', .25), ('B4', .25), ('D5', .25), ('C5', .25)]
	right, left = [], []
	for _ in range(2):
		right += [('E5', .25), ('D#5', .25)]                        # the pickup
		right += figure
		right += [('A4', .5), (None, .25), ('C4', .25), ('E4', .25), ('A4', .25)]
		right += [('B4', .5), (None, .25), ('E4', .25), ('G#4', .25), ('B4', .25)]
		right += [('C5', .5), (None, .25), ('E4', .25), ('E5', .25), ('D#5', .25)]
		right += figure
		right += [('A4', .5), (None, .25), ('C4', .25), ('E4', .25), ('A4', .25)]
		right += [('B4', .5), (None, .25), ('E4', .25), ('C5', .25), ('B4', .25)]
		right += [('A4', 1.5)]
		# the left hand rests through the pickup and the figure, then rolls
		a_minor = [('A2', .25), ('E3', .25), ('A3', .25), (None, .75)]
		e_major = [('E2', .25), ('E3', .25), ('G#3', .25), (None, .75)]
		left += [(None, .5 + 1.5)]
		left += a_minor + e_major + a_minor
		left += [(None, 1.5)]
		left += a_minor + e_major
		left += [('A2', .25), ('E3', .25), ('A3', .25), (None, .75)]
	return [track('Right hand', sequence(right), program=0, velocity=80),
		track('Left hand', sequence(left), program=0, channel=1, velocity=62)]


def canon_in_d():
	"""Pachelbel, Canon in D (before 1706): the ground bass and the opening
	theme in three voices, each entering two bars after the last."""
	ground = ['D3', 'A2', 'B2', 'F#2', 'G2', 'D2', 'G2', 'A2']
	theme = ['F#5', 'E5', 'D5', 'C#5', 'B4', 'A4', 'B4', 'C#5']
	cycle = 16.0                                     # eight minims
	bars = 10                                        # of the ground

	bass = sequence([(n, 2.0) for n in ground] * bars)
	voices = []
	for i in range(3):
		entry = cycle + i * 8.0                      # the bass alone for one cycle
		repeats = int((bars * cycle - entry) // cycle)
		notes = sequence([(n, 2.0) for n in theme] * repeats, start=entry)
		voices.append(track('Violin %d' % (i + 1), notes,
			program=48, channel=i, velocity=78 - 6 * i))
	return voices + [track('Ground bass', bass, program=42, channel=3, velocity=70)]


def ode_to_joy():
	"""Beethoven, Symphony No. 9 (1824): the theme of the final movement."""
	phrase = [('E4', 1), ('E4', 1), ('F4', 1), ('G4', 1),
		('G4', 1), ('F4', 1), ('E4', 1), ('D4', 1),
		('C4', 1), ('C4', 1), ('D4', 1), ('E4', 1)]
	melody = phrase + [('E4', 1.5), ('D4', .5), ('D4', 2)] \
		+ phrase + [('D4', 1.5), ('C4', .5), ('C4', 2)]
	bass = [('C3', 2), ('C3', 2), ('C3', 2), ('G2', 2), ('C3', 2), ('G2', 2), ('G2', 2), ('G2', 2),
		('C3', 2), ('C3', 2), ('C3', 2), ('G2', 2), ('C3', 2), ('G2', 2), ('G2', 2), ('C3', 2)]
	return [track('Melody', sequence(melody), program=48, velocity=82),
		track('Bass', sequence(bass), program=48, channel=1, velocity=62)]


TUNES = [
	('Beethoven - Fur Elise.mid', 80, fur_elise, 3, 8, 'Fur Elise'),
	('Pachelbel - Canon in D.mid', 60, canon_in_d, 4, 4, 'Canon in D'),
	('Beethoven - Ode to Joy.mid', 100, ode_to_joy, 4, 4, 'Ode to Joy'),
]


if __name__ == '__main__':
	out = sys.argv[1] if len(sys.argv) > 1 else '.'
	os.makedirs(out, exist_ok=True)
	for name, tempo, build, per_bar, beat, title in TUNES:
		write(os.path.join(out, name), tempo, build(), per_bar, beat, title)
