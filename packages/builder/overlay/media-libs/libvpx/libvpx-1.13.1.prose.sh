# libvpx 1.13.1: configure knows no Haiku target and fell back to
# generic-gnu, i.e. plain C, no NEON at all. arm64-linux-gcc is the target
# it gives every BSD (no Linux-only code behind it). Runtime CPU detection
# exists only for Linux, Android and Darwin, so it is turned off and the
# NEON paths are built in (always present on arm64). --as=yasm is x86-only.
# Same as the recipe's BUILD otherwise.
BUILD()
{
	export CONFIG_SPATIAL_SVC=yes
	./configure \
		--target=arm64-linux-gcc \
		--disable-runtime-cpu-detect \
		--prefix="$prefix" \
		--libdir="$libDir" \
		--enable-pic \
		--enable-shared \
		--disable-static \
		--enable-vp8 \
		--enable-vp9 \
		--enable-postproc
	make $jobArgs
}
