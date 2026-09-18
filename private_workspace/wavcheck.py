#!/usr/bin/env python3
"""wavcheck.py out.wav: level and dominant frequency of the loudest second, to
confirm a guest played a tone into QEMU's wav backend. Tolerates the unfinished
header QEMU leaves when it is killed (sizes 0): the fmt fields are still valid."""
import struct, sys

data = open(sys.argv[1], "rb").read()
if data[:4] != b"RIFF" or data[8:12] != b"WAVE" or data[12:16] != b"fmt ":
    sys.exit("not a RIFF/WAVE file")
fmtSize = struct.unpack("<I", data[16:20])[0]
audioFormat, channels, rate, byteRate, blockAlign, bits = struct.unpack("<HHIIHH", data[20:36])
pos = 20 + fmtSize
while data[pos:pos + 4] != b"data":
    size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
    pos += 8 + size
pcm = data[pos + 8:]
width = bits // 8
frames = len(pcm) // (channels * width)
fmt = {1: "b", 2: "h", 4: "i"}[width]
samples = struct.unpack("<%d%s" % (frames * channels, fmt), pcm[:frames * channels * width])
left = samples[::channels]
full = (1 << (bits - 1)) - 1
print("%s: %d Hz, %d ch, %d-bit, %.2f s of audio" % (sys.argv[1], rate, channels, bits, frames / rate))
best, bestRms = 0, 0.0
step = max(1, rate // 2)
for start in range(0, max(1, len(left) - rate + 1), step):
    chunk = left[start:start + rate]
    rms = (sum(x * x for x in chunk) / len(chunk)) ** 0.5 / full
    if rms > bestRms:
        best, bestRms = start, rms
chunk = left[best:best + rate]
crossings = sum(1 for a, b in zip(chunk, chunk[1:]) if (a < 0) != (b < 0))
quiet = sum(1 for x in left if abs(x) < full // 200) / max(1, len(left))
print("loudest second at %.1f s: RMS %.3f of full scale, ~%.0f Hz; %.0f%% of all samples near silence"
    % (best / rate, bestRms, crossings / 2 * rate / len(chunk), quiet * 100))
