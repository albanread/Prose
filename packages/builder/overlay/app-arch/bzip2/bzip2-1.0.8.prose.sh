# bzip2 1.0.8: the Makefile's default target also runs the test suite,
# i.e. executes the freshly built arm64 binary on the build host. Build the
# same targets without the tests. Same as the recipe's BUILD otherwise.
BUILD()
{
	make $jobArgs bzip2 bzip2recover libbz2.a
	make $jobArgs -f Makefile-libbz2_so
		# shared libary not built by default
}
