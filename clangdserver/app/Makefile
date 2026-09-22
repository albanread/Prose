# clangd_server -- the BeOS shape around clangd, cross-compiled for Haiku
# arm64 on this Mac. Toolchain and libraries as in prosewriter/app: the Prose
# build tree used read-only, its own sysroot from the build's packages.

HAIKU ?= /Volumes/HaikuSrc/haiku
SYSROOT ?= ../../prosewriter/app/sysroot
SYS    := $(SYSROOT)/boot/system
XGXX   := $(HAIKU)/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++
RC     := $(HAIKU)/generated/objects/darwin/arm64/release/tools/rc/rc
XRES   := $(HAIKU)/generated/objects/darwin/arm64/release/tools/xres

# kit directories on the include path, so Be-style flat #includes resolve
KITDIRS := $(shell find $(SYS)/develop/headers -type d -maxdepth 2)
INCS   := --sysroot $(SYSROOT) $(KITDIRS:%=-I%)
LIBS   := -L$(SYS)/develop/lib

CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpointer-arith -Wno-unused-parameter \
	-std=c++17
LDLIBS  := -lbe -lroot -lstdc++ -lgcc_s

SRCS := src/json.cpp src/lsp.cpp src/main.cpp
OBJS := $(SRCS:.cpp=.o)
RSRC := clangd_server.rsrc
APP  := clangd_server

CLIENT_RSRC := clangd_client.rsrc
CLIENT := clangd_client

all: $(APP) $(CLIENT)

$(APP): $(OBJS) $(RSRC)
	$(XGXX) $(CXXFLAGS) $(INCS) $(LIBS) -o $@ $(OBJS) $(LDLIBS)
	$(XRES) -o $@ $(RSRC)

$(CLIENT): src/json.o src/client.o $(CLIENT_RSRC)
	$(XGXX) $(CXXFLAGS) $(INCS) $(LIBS) -o $@ src/json.o src/client.o $(LDLIBS)
	$(XRES) -o $@ $(CLIENT_RSRC)

%.o: %.cpp
	$(XGXX) $(CXXFLAGS) $(INCS) -MMD -MP -c -o $@ $<

$(RSRC): clangd_server.rdef
	$(RC) -o $@ $<

$(CLIENT_RSRC): clangd_client.rdef
	$(RC) -o $@ $<

clean:
	rm -f src/*.o src/*.d $(RSRC) $(CLIENT_RSRC) $(APP) $(CLIENT)

.PHONY: all clean

-include $(wildcard src/*.d)
