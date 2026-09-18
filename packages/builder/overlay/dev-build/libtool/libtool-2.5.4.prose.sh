# libtool 2.5.4: the recipe re-bootstraps the release with the system's
# autotools. Here that clones gnulib over the network, runs git submodule
# commands outside a repository and fails in Apple's m4. The release
# tarball's own configure is complete, so configure it as shipped.
# Same as the recipe's BUILD otherwise.
BUILD()
{
	touch README-release
	SED='sed' NM='nm' LD=ld runConfigure ./configure \
		--with-gnu-ld \
		--disable-static
	make $jobArgs MAKEINFO=true HELP2MAN=true
	# the recipe's fixup: no absolute compiler paths in the installed libtool
	sed -i -e 's@^predep_objects=".*"$@predep_objects=""'@ \
		-e 's@^postdep_objects=".*"$@postdep_objects=""'@ \
		-e 's@^postdeps=".*"$@postdeps=""'@ \
		-e 's@^compiler_lib_search_path=".*"$@compiler_lib_search_path=""'@ \
		-e 's@^compiler_lib_search_dirs=".*"$@compiler_lib_search_dirs=""'@ \
		libtool
}
