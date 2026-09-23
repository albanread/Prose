from std.time import perf_counter_ns, sleep
from std.os import getenv
from std.pathlib import Path


def main() raises:
    var t0 = perf_counter_ns()
    var d = Dict[String, Int]()
    d["answer"] = 42
    print("dict:", d["answer"])
    print("HOME:", getenv("HOME"))
    with open("/tmp/mojo-probe.txt", "w") as f:
        f.write("written by Mojo on Haiku\n")
    print("file:", Path("/tmp/mojo-probe.txt").read_text(), end="")
    sleep(0.05)
    var elapsed = perf_counter_ns() - t0
    print("elapsed ns > 50ms:", elapsed > 50_000_000, elapsed)
