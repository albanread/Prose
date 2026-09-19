# NetSurf runs nsgenbind, its JavaScript binding generator, while building.
# That is a Haiku binary from the nsgenbind port; here it is built for the
# Mac from the same source, with the NetSurf build system's makefiles (both
# fetched as prosepkg fetches any port), and put on the build's PATH.
PROSE_HOST_TOOLS="netsurf_buildsystem nsgenbind"
HOST_BUILD()
{
	make -C "$hostSourceDir_nsgenbind" NSSHARED="$hostSourceDir_netsurf_buildsystem" \
		PREFIX="$hostPrefix" install
	"$hostPrefix/bin/nsgenbind" --help 2>&1 | head -1
}

# The Makefile sets BUILD_CC := cc for the tools it runs while building
# (its xxd); "cc" here is the cross compiler, so the Mac's is named on the
# command line. Same as the recipe's BUILD and INSTALL otherwise.
BUILD()
{
	make TARGET=beos PREFIX=$prefix/ DESTDIR=$appsDir NETSURF_BEOS_BIN="/" \
		BUILD=release BUILD_CC=/usr/bin/clang $jobArgs
}

INSTALL()
{
	make TARGET=beos PREFIX=$prefix/ DESTDIR=$appsDir NETSURF_BEOS_BIN="/" \
		BUILD=release BUILD_CC=/usr/bin/clang install

	# Resources not needed since 3.6
	rm -rf $appsDir/boot

	addAppDeskbarSymlink $appsDir/NetSurf
}
