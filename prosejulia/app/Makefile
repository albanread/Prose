# ProseJulia — cross-compiled for Haiku arm64 on this Mac.
#
# Shares the ProseWriter sysroot (a symlink farm into the Prose build's
# extracted packages, used strictly read-only) and cross-compiler.
HAIKU ?= /Volumes/HaikuSrc/haiku
SYSROOT ?= /Volumes/xb/HaikuArmQemu/prosewriter/app/sysroot
SYS    := $(SYSROOT)/boot/system
XGXX   := $(HAIKU)/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++
# zlib headers + lib come with ProseWriter's kit (Flate streams)
ZLIBDIR ?= /Volumes/xb/HaikuArmQemu/prosewriter/app/zlib

KITDIRS := $(shell find $(SYS)/develop/headers -type d -maxdepth 2)
INCS   := --sysroot $(SYSROOT) $(KITDIRS:%=-I%) -I$(ZLIBDIR)
LIBS   := -L$(SYS)/develop/lib

CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpointer-arith -Wno-unused-parameter \
	-Wno-missing-field-initializers -Wno-multichar -std=c++17
LDFLAGS  ?=
LDLIBS   := -lbe -lroot -ltracker -ltranslation -lstdc++ -lgcc_s -lz

BINS := ProseJulia

all: $(BINS)

PPSRC := $(wildcard src/*.cpp)
PPOBJ := $(PPSRC:src/%.cpp=src/%.o)

ProseJulia: $(PPOBJ)
	$(XGXX) $(CXXFLAGS) $(INCS) $(LIBS) -o $@ $(PPOBJ) $(LDFLAGS) $(LDLIBS)

src/%.o: src/%.cpp $(wildcard src/*.h)
	$(XGXX) $(CXXFLAGS) $(INCS) -c -o $@ $<

clean:
	rm -f $(BINS) src/*.o

.PHONY: all clean
