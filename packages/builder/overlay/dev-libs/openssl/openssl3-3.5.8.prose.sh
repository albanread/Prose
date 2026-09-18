# OpenSSL 3.5.8 on Haiku arm64. OpenSSL knows no haiku-aarch64 target, and
# ./config guessed the platform with Perl's uname, i.e. the build host
# (Darwin). openssl3-3.5.8-haiku-arm64.patch adds the target and takes the
# CPU capabilities from the compile-time features instead of SIGILL probing,
# so assembly is on here: NEON and the ARMv8 AES/PMULL/SHA1/SHA2/SHA512/SHA3
# instructions (the recipe's no-asm is for architectures without a Haiku asm
# target). Same as the recipe's BUILD otherwise.
BUILD()
{
	./Configure haiku-aarch64 --prefix=$prefix --libdir=$relativeLibDir \
		--openssldir=$dataRootDir/ssl \
		enable-zlib enable-zstd shared -g
	make $jobArgs
}
