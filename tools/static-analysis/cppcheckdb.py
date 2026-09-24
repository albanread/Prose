#!/usr/bin/env python3
"""A copy of the compile database for cppcheck: without the gcc, libstdc++ and
clang header directories, which cppcheck cannot parse (it models the standard
library with its own --library configurations instead)."""
import json, sys
cdb = json.load(open(sys.argv[1]))
out = []
for e in cdb:
    args = e["arguments"]
    new = []
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-I", "-isystem", "-iquote") and i + 1 < len(args) \
                and ("gcc_syslibs_devel" in args[i + 1] or "/lib/clang/" in args[i + 1]):
            i += 2
            continue
        if a.startswith("-I") and ("gcc_syslibs_devel" in a or "/lib/clang/" in a):
            i += 1
            continue
        new.append(a)
        i += 1
    out.append(dict(e, arguments=new))
json.dump(out, open(sys.argv[2], "w"), indent=1)
