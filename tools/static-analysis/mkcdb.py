#!/usr/bin/env python3
"""Turn jam's -dx command dump into a clang compile_commands.json for the
Haiku arm64 target: the cross gcc becomes Homebrew clang with
--target=aarch64-unknown-haiku, gcc's own builtin headers give way to clang's,
and warning and gcc-only flags are dropped."""
import json, shlex, sys

TREE = "/Volumes/HaikuSrc/haiku"
CLANG = "/opt/homebrew/opt/llvm/bin/clang"
RESOURCE = "/opt/homebrew/Cellar/llvm/22.1.2/lib/clang/22/include"
DROP = {"-finline", "-fno-tree-vectorize", "-Werror", "-fno-semantic-interposition"}

entries = {}
for line in open(sys.argv[1], errors="replace"):
    if "aarch64-unknown-haiku-gcc" not in line or " -c " not in line:
        continue
    try:
        args = shlex.split(line.strip())
    except ValueError:
        continue
    if not args or not args[0].endswith("aarch64-unknown-haiku-gcc"):
        continue
    source = None
    out = []
    skip = False
    i = 1
    while i < len(args):
        a = args[i]
        if a == "-o":
            i += 2
            continue
        if a == "-c":
            i += 1
            continue
        if a.endswith((".c", ".cpp", ".cc", ".S", ".s")) and not a.startswith("-"):
            source = a
            i += 1
            continue
        if a in DROP or (a.startswith("-W") and not a.startswith("-Wa,")):
            i += 1
            continue
        if a in ("-I", "-iquote", "-isystem", "-include", "-imacros"):
            value = args[i + 1]
            if "/headers/gcc/include" in value:
                i += 2
                continue
            out += [a, value]
            i += 2
            continue
        if a.startswith("-I") and "/headers/gcc/include" in a:
            i += 1
            continue
        out.append(a)
        i += 1
    if source is None or source.endswith((".S", ".s")):
        continue
    cxx = source.endswith((".cpp", ".cc"))
    command = [CLANG + ("++" if cxx else ""), "--target=aarch64-unknown-haiku",
        "-isystem", RESOURCE, "-Wno-unknown-warning-option", "-w"] + out + ["-c", source]
    # one entry per source; the kernel and libroot build some twice
    entries[source] = {"directory": TREE, "file": source, "arguments": command}

json.dump(list(entries.values()), open(sys.argv[2], "w"), indent=1)
print(len(entries), "entries")
