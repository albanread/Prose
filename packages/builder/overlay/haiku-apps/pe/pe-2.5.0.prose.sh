# Pe 2.5.0: build/BuildSettings GLOBs /boot/system/develop/headers for
# pcre.h. Those paths exist in a Haiku chroot, here they are in the build
# sysroot.
PATCH()
{
	sed -i "s|/boot/system/develop/headers|$PROSE_SYSROOT/boot/system/develop/headers|" \
		build/BuildSettings
}

# Pe compiles its own resource compiler, rez, and runs it on every resource
# file -- a Haiku binary here. So rez is built for the Mac as well, with the
# build-host Be API Haiku's own tools use (host-be-c++), and put in the
# target rez's place after that one is built: the resource files are then
# made by the Mac rez, and everything else builds as the recipe says.
BUILD()
{
	jam -q -sOSPLAT=X86 -sDISTRO_DIR=distro rez
	rm -rf host-rez
	mkdir host-rez
	(
		cd host-rez
		bison --defines=rez_parser.cpp.h -o rez_parser.cpp ../rez/Sources/rez_parser.y
		flex -i -o rez_scanner.cpp ../rez/Sources/rez_scanner.l
		host-be-c++ -I ../rez/Sources -I . -o rez rez_parser.cpp rez_scanner.cpp \
			../rez/Sources/RState.cpp ../rez/Sources/SymbolTable.cpp \
			../rez/Sources/rez.cpp ../rez/Sources/REval.cpp ../rez/Sources/RElem.cpp
	)
	cp host-rez/rez rez/rez
	jam -q -sOSPLAT=X86 -sDISTRO_DIR=distro
}
