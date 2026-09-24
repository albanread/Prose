#!/usr/bin/env python3
import json, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
cdb = json.load(open(sys.argv[1] + "/compile_commands.json"))
files = [e["file"] for e in cdb if e["file"].endswith((".cpp", ".cc"))]
if len(sys.argv) > 3:
    files = [f for f in files if any(f.startswith(p) for p in sys.argv[3].split(","))]
chunks = [files[i::16] for i in range(16)]
def run(chunk):
    if not chunk:
        return ""
    r = subprocess.run(["/opt/homebrew/opt/llvm/bin/clang-query", "-p", sys.argv[1],
        "-f", sys.argv[2]] + chunk, cwd="/Volumes/HaikuSrc/haiku", capture_output=True, text=True)
    return r.stdout + r.stderr
out = []
with ThreadPoolExecutor(16) as pool:
    for text in pool.map(run, chunks):
        out.append(text)
print("\n".join(out))
