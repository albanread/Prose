#!/usr/bin/python3
"""The loud stretches of a WAV, one per line: '  start-end (n.ns)'.
Used by sound-regression.sh on what --record-sound wrote."""
import math
import struct
import sys
import wave

w = wave.open(sys.argv[1])
rate = w.getframerate()
n = w.getnframes()
samples = struct.unpack(f"<{n * 2}h", w.readframes(n))
win = rate // 10          # 100 ms windows
start = prev = None
for i in range(0, len(samples) - win, win):
    seg = samples[i:i + win:2]
    rms = math.sqrt(sum(x * x for x in seg) / len(seg))
    t = i // 2 / rate
    if rms > 300:
        if start is None:
            start = t
        prev = t
    elif start is not None:
        if prev - start > 0.05:
            print(f"  {start:6.1f}-{prev:6.1f} ({prev - start:4.1f}s)")
        start = None
if start is not None:
    print(f"  {start:6.1f}-{prev:6.1f} ({prev - start:4.1f}s)")
