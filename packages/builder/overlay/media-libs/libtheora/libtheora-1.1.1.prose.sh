# libtheora 1.1.1: examples/ builds encoder_example, which includes
# vorbis/codec.h, even when configure finds no libvorbis (the recipe only
# requires libogg). The package ships no examples, so don't build them.
# Same as the recipe's BUILD otherwise.
BUILD()
{
	libtoolize --force --copy --install
	aclocal -I m4
	autoconf
	automake --add-missing
	runConfigure ./configure \
		--docdir $developDocDir \
		--disable-static \
		--disable-asm \
		--disable-examples
	make $jobArgs
}
