# make builds itself twice: build.sh compiles a first "make" with the shell,
# and the recipe then runs that make to build itself again. Here the first one
# is an arm64 Haiku binary and the machine doing the building is a Mac, so it
# cannot be run. build.sh alone produces a complete make, so the second pass
# is dropped.
BUILD()
{
	runConfigure ./configure \
		--disable-rpath --with-gnu-ld --disable-dependency-tracking
	./build.sh
	test -x ./make || { echo "make did not build" >&2; exit 1; }
}

# and the same for installing: the build host's own make does it
INSTALL()
{
	make install
}
