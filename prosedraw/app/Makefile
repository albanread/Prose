# ProseDraw — cross-compiled for Haiku arm64 on this Mac.
#
# Shares the ProseWriter sysroot (a symlink farm into the Prose build's
# extracted packages, used strictly read-only) and cross-compiler.
HAIKU ?= /Volumes/HaikuSrc/haiku
SYSROOT ?= /Volumes/xb/HaikuArmQemu/prosewriter/app/sysroot
SYS    := $(SYSROOT)/boot/system
XGXX   := $(HAIKU)/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++

KITDIRS := $(shell find $(SYS)/develop/headers -type d -maxdepth 2)
INCS   := --sysroot $(SYSROOT) $(KITDIRS:%=-I%)
LIBS   := -L$(SYS)/develop/lib

CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpointer-arith -Wno-unused-parameter \
	-Wno-missing-field-initializers -Wno-multichar -std=c++17
LDFLAGS  ?=
LDLIBS   := -lbe -lroot -ltracker -ltranslation -lstdc++ -lgcc_s

BINS := ProseDraw

all: $(BINS)

PDSRC := $(wildcard src/*.cpp)
PDOBJ := $(PDSRC:src/%.cpp=src/%.o)

ProseDraw: $(PDOBJ)
	$(XGXX) $(CXXFLAGS) $(INCS) $(LIBS) -o $@ $(PDOBJ) $(LDFLAGS) $(LDLIBS)

src/%.o: src/%.cpp $(wildcard src/*.h)
	$(XGXX) $(CXXFLAGS) $(INCS) -c -o $@ $<

clean:
	rm -f $(BINS) src/*.o

.PHONY: all clean
