# TLS test

The regression test for Haiku patch 0132: thread-local storage through TLS
descriptors (`R_AARCH64_TLSDESC`), which is all clang generates for a
`thread_local` on arm64. Before 0132, Haiku's runtime_loader refused every
such program — including any C++ program using `std::call_once`, whose
libstdc++ implementation passes its callable in two thread-locals.

On Prose, with Prose's own clang:

	sh build-and-run.sh

Six checks, each printing PASS or FAIL, then `SELFTEST PASS 6/6`: a
thread-local in the program, one in a shared library used from the program,
one in a library loaded with `dlopen()`, `std::call_once`, eight threads each
with its own copies of all three, and the main thread's values untouched by
them. Passed 6/6 on 2026-09-23 on the image with 0132.
