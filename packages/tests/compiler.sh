#!/bin/sh
# A compiler on the machine (patch 0077): clang and lld must be installed and
# must compile and link a program that then runs -- a C one, a C++ one, and a
# Be API one against the system's own headers and libraries.
# Runs on the target from boot-test.sh; the last line is PASS or FAIL.
fail=0
say() { echo "$*"; echo "compiler probe: $*" > /dev/dprintf 2>/dev/null; }

for f in /boot/system/bin/clang /boot/system/bin/clang++ /boot/system/bin/ld.lld \
		/boot/system/develop/headers/os/Be.h /boot/system/develop/lib/libbe.so \
		/boot/system/develop/lib/crti.o; do
	if [ -e "$f" ]; then say "present: $f"; else say "MISSING: $f"; fail=1; fi
done

say "$(clang --version | head -1)"

dir=/boot/home/compiler-probe
rm -rf $dir; mkdir -p $dir; cd $dir

# C
printf '#include <stdio.h>\nint main(void) { printf("c ok\\n"); return 0; }\n' > hello.c
if clang -O2 -Wall -o hello_c hello.c > c.log 2>&1; then
	out=$(./hello_c)
	if [ "$out" = "c ok" ]; then say "C: compiled, linked and ran"; else say "C: ran but said '$out'"; fail=1; fi
else
	say "C: FAILED to build: $(head -c 300 c.log | tr '\n' ' ')"; fail=1
fi

# C++
printf '#include <string>\n#include <cstdio>\nint main() { std::string s = "c++ ok"; printf("%%s\\n", s.c_str()); return 0; }\n' > hello.cpp
if clang++ -O2 -Wall -o hello_cpp hello.cpp > cpp.log 2>&1; then
	out=$(./hello_cpp)
	if [ "$out" = "c++ ok" ]; then say "C++: compiled, linked and ran (libstdc++)"; else say "C++: ran but said '$out'"; fail=1; fi
else
	say "C++: FAILED to build: $(head -c 300 cpp.log | tr '\n' ' ')"; fail=1
fi

# the Be API, against the system's own headers and libraries
printf '#include <Application.h>\n#include <cstdio>\nint main() { BApplication app("application/x-vnd.prose-probe"); printf("be ok\\n"); return 0; }\n' > hello_be.cpp
if clang++ -O2 -o hello_be hello_be.cpp -lbe > be.log 2>&1; then
	out=$(./hello_be)
	if [ "$out" = "be ok" ]; then say "Be API: compiled, linked and ran"; else say "Be API: ran but said '$out'"; fail=1; fi
else
	say "Be API: FAILED to build: $(head -c 400 be.log | tr '\n' ' ')"; fail=1
fi

# the linker that was used
if [ -x /boot/system/bin/ld.lld ]; then
	say "$(ld.lld --version 2>&1 | head -1)"
fi

[ $fail = 0 ] && say PASS || say FAIL
