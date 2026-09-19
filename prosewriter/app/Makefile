# ProseWriter + proseagent — cross-compiled for Haiku arm64 on this Mac.
#
# Toolchain and libraries come from the Prose build tree (used read-only):
#   cross g++      generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++
#   headers+libs   our own sysroot (mksysroot.sh) assembled from the build's
#                  extracted packages
# Nothing here runs jam or writes to that tree.

HAIKU ?= /Volumes/HaikuSrc/haiku
SYSROOT ?= $(CURDIR)/sysroot
SYS    := $(SYSROOT)/boot/system
XGXX   := $(HAIKU)/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++

# The cross-gcc finds headers, startfiles and libs under the sysroot;
# kit directories go on the include path so that Be-style flat
# #include <Application.h> resolves.
KITDIRS := $(shell find $(SYS)/develop/headers -type d -maxdepth 2)
INCS   := --sysroot $(SYSROOT) $(KITDIRS:%=-I%)
LIBS   := -L$(SYS)/develop/lib

CXXFLAGS ?= -O2 -g -Wall -Wno-multichar -std=c++17
LDFLAGS  ?=
LDLIBS   := -lbe -lroot -ltracker -lstdc++ -lgcc_s

BINS := ProseWriter proseagent

all: $(BINS)

%.o: %.cpp
	$(XGXX) $(CXXFLAGS) $(INCS) -c -o $@ $<

ProseWriter: $(wildcard src/*.cpp) $(wildcard src/*.h)
	$(XGXX) $(CXXFLAGS) $(INCS) $(LIBS) -o $@ $(filter %.cpp,$^) $(LDFLAGS) $(LDLIBS)

proseagent: proseagent.cpp
	$(XGXX) $(CXXFLAGS) $(INCS) $(LIBS) -o $@ $< $(LDFLAGS) $(LDLIBS)

hello: hello.cpp
	$(XGXX) $(CXXFLAGS) $(INCS) $(LIBS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(BINS) hello *.o

.PHONY: all clean
