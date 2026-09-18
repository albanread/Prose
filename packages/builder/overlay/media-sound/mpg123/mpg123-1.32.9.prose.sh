# mpg123 1.32.9: configure picks its NEON64 decoders only for Linux and
# Darwin hosts and chose generic_fpu for Haiku; arm64 always has NEON.
# Same as the recipe's BUILD otherwise.
BUILD()
{
	runConfigure ./configure --with-cpu=aarch64
	make $jobArgs
}
