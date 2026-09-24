#!/usr/bin/env python3
"""GCC's -fanalyzer over the C files of the jam dump, with the real cross
compiler and flags; object output to /dev/null."""
import shlex, subprocess, sys, re, collections
from concurrent.futures import ThreadPoolExecutor
SKIP = ("/glibc/", "/musl/", "/contrib/", "src/libs/", "/netresolv/", "/openbsd/malloc.c")
cmds = {}
for line in open(sys.argv[1], errors="replace"):
    if "aarch64-unknown-haiku-gcc" not in line or " -c " not in line:
        continue
    try:
        args = shlex.split(line.strip())
    except ValueError:
        continue
    src = next((a for a in args if a.endswith(".c") and not a.startswith("-")), None)
    if src is None or any(s in src for s in SKIP):
        continue
    if not src.startswith(("src/system/kernel", "src/system/libroot", "src/system/runtime_loader", "src/add-ons/kernel")):
        continue
    out = []
    i = 0
    while i < len(args):
        if args[i] == "-o":
            out += ["-o", "/dev/null"]
            i += 2
            continue
        if args[i] in ("-Werror",):
            i += 1
            continue
        out.append(args[i])
        i += 1
    cmds[src] = out + ["-fanalyzer", "-Wno-analyzer-too-complex", "-fdiagnostics-plain-output", "-fno-diagnostics-show-caret"]
def run(item):
    src, cmd = item
    try:
        r = subprocess.run(cmd, cwd="/Volumes/HaikuSrc/haiku", capture_output=True, text=True, timeout=600)
        return src, r.stderr
    except subprocess.TimeoutExpired:
        return src, "TIMEOUT"
found = []
with ThreadPoolExecutor(16) as pool:
    for src, err in pool.map(run, cmds.items()):
        for l in err.splitlines():
            if "[-Wanalyzer-" in l and ": warning:" in l:
                found.append(l)
print(len(cmds), "C files analysed")
open(sys.argv[2], "w").write("\n".join(sorted(set(found))) + "\n")
c = collections.Counter(re.search(r"\[(-Wanalyzer-[^\]]+)\]", l).group(1) for l in set(found))
for k, v in c.most_common(): print(f"{v:5d}  {k}")
