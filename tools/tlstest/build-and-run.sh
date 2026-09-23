#!/bin/sh
# On Prose: build the TLS test with Prose's own clang, and run it.
set -e
cd "$(dirname "$0")"
clang++ -O2 -shared -fPIC -o libtlsshared.so tlsshared.cpp
clang++ -O2 -shared -fPIC -o libtlsdl.so tlsdl.cpp
clang++ -O2 -o tlstest tlstest.cpp -L. -ltlsshared -Wl,-rpath,'$ORIGIN'
./tlstest
