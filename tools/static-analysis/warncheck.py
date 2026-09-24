#!/usr/bin/env python3
"""clang's bug-finding warnings over the compile database, syntax only."""
import json, subprocess, sys, re, collections
from concurrent.futures import ThreadPoolExecutor
WARN = ["-Wdangling", "-Wdangling-gsl", "-Wreturn-stack-address",
    "-Wuninitialized", "-Wsometimes-uninitialized", "-Wconditional-uninitialized",
    "-Wsizeof-pointer-memaccess", "-Wsizeof-array-argument", "-Wmemset-transposed-args",
    "-Wself-assign", "-Wself-move", "-Wtautological-overlap-compare",
    "-Wtautological-pointer-compare", "-Wtautological-undefined-compare",
    "-Wstring-conversion", "-Wloop-analysis", "-Wmisleading-indentation",
    "-Wnull-dereference", "-Wformat", "-Wformat-security", "-Wreturn-type",
    "-Wunsequenced", "-Warray-bounds", "-Warray-bounds-pointer-arithmetic",
    "-Wbool-operation", "-Wstring-compare", "-Wpointer-bool-conversion",
    "-Wundefined-bool-conversion", "-Wmismatched-new-delete", "-Wdelete-incomplete",
    "-Wdelete-non-abstract-non-virtual-dtor", "-Wdynamic-class-memaccess",
    "-Wnontrivial-memcall", "-Wsuspicious-memaccess", "-Wfree-nonheap-object",
    "-Wshift-overflow", "-Wshift-count-overflow", "-Wparentheses",
    "-Winfinite-recursion", "-Wimplicit-fallthrough"]
cdb = json.load(open(sys.argv[1]))
def run(e):
    cmd = [a for a in e["arguments"] if a not in ("-c", "-w")] + ["-fsyntax-only"] + WARN
    r = subprocess.run(cmd, cwd=e["directory"], capture_output=True, text=True)
    return [l for l in r.stderr.splitlines() if ": warning:" in l]
seen = set()
by_flag = collections.Counter()
lines = []
with ThreadPoolExecutor(16) as pool:
    for warnings in pool.map(run, cdb):
        for l in warnings:
            if l in seen:
                continue
            seen.add(l)
            m = re.search(r"\[(-W[^\],]+)", l)
            by_flag[m.group(1) if m else "?"] += 1
            lines.append(l)
open(sys.argv[2], "w").write("\n".join(sorted(lines)) + "\n")
for flag, n in by_flag.most_common():
    print(f"{n:6d}  {flag}")
