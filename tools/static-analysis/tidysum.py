#!/usr/bin/env python3
import re, sys, collections, json
HIGH = ["clang-analyzer-core.NullDereference", "clang-analyzer-unix.Malloc", "clang-analyzer-cplusplus.NewDelete]",
    "clang-analyzer-cplusplus.NewDeleteLeaks", "clang-analyzer-core.CallAndMessage", "clang-analyzer-core.uninitialized",
    "clang-analyzer-core.UndefinedBinaryOperatorResult", "clang-analyzer-security.ArrayBound", "clang-analyzer-core.DivideZero",
    "clang-analyzer-unix.MallocSizeof", "clang-analyzer-core.NullPointerArithm", "clang-analyzer-unix.cstring.NullArg",
    "clang-analyzer-unix.StdCLibraryFunctions", "clang-analyzer-core.VLASize", "clang-analyzer-cplusplus.PlacementNew",
    "clang-analyzer-unix.Stream", "clang-analyzer-security.PointerSub", "bugprone-suspicious-semicolon",
    "bugprone-not-null-terminated-result", "bugprone-sizeof-expression", "bugprone-too-small-loop-variable",
    "misc-redundant-expression", "bugprone-copy-constructor-init", "bugprone-undefined-memory-manipulation",
    "bugprone-use-after-move", "bugprone-infinite-loop", "bugprone-suspicious-memset-usage",
    "bugprone-misplaced-widening-cast", "bugprone-string-constructor", "bugprone-unused-raii",
    "bugprone-undelegated-constructor", "bugprone-posix-return", "bugprone-suspicious-missing-comma",
    "bugprone-redundant-branch-condition", "bugprone-signal-handler", "clang-analyzer-core.StackAddressEscape",
    "clang-analyzer-core.BitwiseShift", "clang-analyzer-cplusplus.InnerPointer", "clang-analyzer-unix.MismatchedDeallocator"]
THIRD = ("src/libs/", "/contrib/", "src/system/libroot/posix/glibc/", "src/system/libroot/posix/musl/",
    "src/system/libroot/posix/malloc/openbsd/malloc.c", "src/system/libnetwork/netresolv/", "src/libs/agg/",
    "src/add-ons/kernel/network/", "headers/libs/")
seen = {}
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"^(/Volumes/HaikuSrc/haiku/)?([^:]+):(\d+):(\d+): warning: (.*) \[([^\]]+)\]$", line.strip())
    if not m:
        continue
    path, lno, check, msg = m.group(2), int(m.group(3)), m.group(6), m.group(5)
    key = (path, lno, check)
    if key not in seen:
        seen[key] = msg
rows = []
for (path, lno, check), msg in seen.items():
    sev = "high" if any(check.startswith(h.rstrip("]")) and (not h.endswith("]") or check == h[:-1]) for h in HIGH) else "low"
    origin = "third-party" if any(t in path for t in THIRD) else "haiku"
    rows.append({"file": path, "line": lno, "check": check, "message": msg, "severity": sev, "origin": origin})
json.dump(rows, open(sys.argv[2], "w"), indent=1)
c = collections.Counter((r["severity"], r["origin"]) for r in rows)
print("unique findings:", len(rows), dict(c))
hc = collections.Counter(r["check"] for r in rows if r["severity"] == "high" and r["origin"] == "haiku")
for k, v in hc.most_common(): print(f"{v:5d}  {k}")
