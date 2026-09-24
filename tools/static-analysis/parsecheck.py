#!/usr/bin/env python3
import json, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
cdb = json.load(open(sys.argv[1]))
def run(e):
    cmd = [a for a in e["arguments"] if a != "-c"] + ["-fsyntax-only"]
    r = subprocess.run(cmd, cwd=e["directory"], capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() if ": error:" in l or "fatal error" in l]
    return e["file"], r.returncode, errs
bad = []
with ThreadPoolExecutor(16) as pool:
    for f, rc, errs in pool.map(run, cdb):
        if rc != 0:
            bad.append((f, errs))
print(len(cdb) - len(bad), "of", len(cdb), "parse")
import collections
c = collections.Counter()
for f, errs in bad:
    for l in errs[:1]:
        c[l.split("error:")[-1].strip()[:90]] += 1
for msg, n in c.most_common(25):
    print(f"{n:5d}  {msg}")
json.dump(bad, open(sys.argv[2], "w"), indent=1)
