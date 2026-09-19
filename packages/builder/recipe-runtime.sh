# recipe-runtime.sh — the shell environment in which prosepkg runs
# haikuports recipes, cross-building for Haiku arm64 on a macOS host.
#
# Sourced (bash 5) before the recipe. Function names and behaviour follow
# haikuporter's ShellScriptlets.py (Copyright 2013 Oliver Tappe, MIT
# License), adapted for building outside a Haiku chroot:
#
#  - PATCH/BUILD see the runtime directory variables ($prefix=/boot/system,
#    $appsDir=/boot/system/apps, ...). The compiler wrappers map /boot/...
#    arguments into the build sysroot.
#  - INSTALL sees the same variables re-rooted into the staging tree
#    ($prefix=<work>/destdir/boot/system), with DESTDIR=<work>/destdir
#    exported, so both `cp foo $appsDir` and a plain `make install` land in
#    the package. $prose_runtime_<var> always holds the runtime value.
#  - runConfigure always cross-configures (--build/--host).

# -- helpers from haikuporter's commonRecipeScriptHead -------------------------

getPackagePrefix()
{
	# Usage: getPackagePrefix <packageSuffix>
	local packageSuffix="$1"
	if [ "$proseInInstall" = 1 ]; then
		echo "$proseSubpackagesDir/$packageSuffix/boot/system"
	else
		echo "$prose_runtime_prefix"
	fi
}

defineDebugInfoPackage()
{
	# Usage: defineDebugInfoPackage [ --directory <toDirectory> ]
	#	<basePackageName> <path> ...
	# prosepkg ships no _debuginfo packages: the listed files get their debug
	# info stripped at the end of INSTALL (resources preserved).
	if [ $# -lt 2 -o "$1" = "--directory" -a $# -lt 4 ]; then
		echo >&2 "Usage: defineDebugInfoPackage [ --directory <toDirectory> ]" \
			"<basePackageName> <path> ..."
		exit 1
	fi
	if [ "$1" = "--directory" ]; then
		shift 2
	fi
	shift 1
	DEBUG_INFO_PACKAGES="$DEBUG_INFO_PACKAGES debuginfo"
	PROSE_DEBUG_INFO_PATHS+=("$@")
}

# -- helpers from haikuporter's recipeActionScript -----------------------------

getTargetArchitectureCommand()
{
	# Usage: getTargetArchitectureCommand <command>
	if [ $# -lt 1 ]; then
		echo >&2 "Usage: getTargetArchitectureCommand <command>"
		exit 1
	fi
	echo "${effectiveTargetMachineTriple}-$1"
}

runConfigure()
{
	# Usage: runConfigure [ --omit-dirs <dirsToOmit> ] [ --omit-buildspec ]
	#	<configure> <argsToConfigure> ...
	local varsToOmit=""
	local omitBuildSpec=false
	while [ $# -ge 1 ]; do
		case $1 in
		--omit-dirs)
			shift 1
			varsToOmit="$1"
			shift 1
			;;
		--omit-buildspec)
			omitBuildSpec=true
			shift 1
			;;
		*)
			break
			;;
		esac
	done
	if [ $# -lt 1 ]; then
		echo >&2 "Usage: runConfigure [ --omit-dirs <dirsToOmit> ]" \
			"[ --omit-buildspec ] <configure> <argsToConfigure> ..."
		exit 1
	fi

	local configure=$1
	shift 1

	# directory arguments always carry the runtime paths
	local dirArgs=""
	local dir runtimeVar
	for dir in $configureDirVariables; do
		if ! [[ "$varsToOmit" =~ (^|\ )${dir}($|\ ) ]]; then
			runtimeVar=prose_runtime_$dir
			dirArgs="$dirArgs --${dir,,}=${!runtimeVar}"
		fi
	done

	# Always a cross build: configure must not try to run target binaries.
	# --omit-buildspec marks configure scripts that are not autoconf's.
	local buildSpec=""
	if [ $omitBuildSpec != true ]; then
		buildSpec="--build=$proseBuildTriple --host=$effectiveTargetMachineTriple"
	fi

	# tools under their plain names, as in a native build: configure records
	# them in installed scripts (libtool, *-config), which must work on Haiku
	# (here the plain names are the cross wrappers)
	CC="${CC:-gcc}" CXX="${CXX:-g++}" AR="${AR:-ar}" RANLIB="${RANLIB:-ranlib}" \
		STRIP="${STRIP:-strip}" NM="${NM:-nm}" OBJDUMP="${OBJDUMP:-objdump}" \
		$configure $dirArgs $buildSpec "$@"
}

cmake()
{
	# The toolchain file makes every configure run a cross configure;
	# build/install/command modes pass through.
	local CMAKE
	CMAKE=$(type -Pp cmake)
	case "$1" in
	--build|--install|-E|-P|--open|--version|--help)
		"$CMAKE" "$@"
		;;
	*)
		"$CMAKE" -DCMAKE_TOOLCHAIN_FILE="$PROSE_CMAKE_TOOLCHAIN" "$@"
		;;
	esac
}

meson()
{
	local MESON
	MESON=$(type -Pp meson)
	case "$1" in
	setup)
		shift 1
		"$MESON" setup --cross-file "$PROSE_MESON_CROSS" \
			--wrap-mode=nodownload "$@"
		;;
	compile|install|test|configure|introspect|dist|subprojects|wrap|rewrite|devenv|env2mfile|init|--version|--help)
		"$MESON" "$@"
		;;
	*)
		# legacy form: meson [options] <builddir> is "setup"
		"$MESON" setup --cross-file "$PROSE_MESON_CROSS" \
			--wrap-mode=nodownload "$@"
		;;
	esac
}

fixDevelopLibDirReferences()
{
	# Usage: fixDevelopLibDirReferences <file> ...
	# Replaces the runtime $libDir in the given files with $developLibDir.
	local file
	for file in $*; do
		sed -i "s,$prose_runtime_libDir,$prose_runtime_developLibDir,g" $file
	done
}

prepareInstalledDevelLib()
{
	if [ $# -lt 1 ]; then
		echo >&2 "Usage: prepareInstalledDevelLib <libBaseName>" \
			"[ <soPattern> [ <pattern> ] ]"
		exit 1
	fi

	mkdir -p $installDestDir$developLibDir

	local libBaseName=$1
	local soPattern=$2
	local pattern=$3

	# find the shared library file and get its soname
	local sharedLib=""
	local sonameLib=""
	local soname=""
	local readelf
	readelf=$(getTargetArchitectureCommand readelf)
	local lib sonameLine

	for lib in $installDestDir$libDir/${libBaseName}${soPattern:-.so*}; do
		if [ -f $lib -a ! -h $lib ]; then
			sharedLib=$lib
			set +e
			sonameLine=$($readelf --dynamic $lib | grep SONAME)
			set -e
			if [ -n "$sonameLine" ]; then
				soname=$(echo "$sonameLine" | sed 's,.*\[\(.*\)\].*,\1,')
				if [ "$soname" != "$sonameLine" ]; then
					sonameLib=$installDestDir$libDir/$soname
				else
					soname=""
				fi
			fi
			break
		fi
	done

	# Make sure there is not a static library in addition to a shared library.
	if [ -f "$installDestDir$libDir/$libBaseName.so" ] \
			&& [ -f "$installDestDir$libDir/$libBaseName.a" \
				-o -f "$installDestDir$developLibDir/$libBaseName.a" ]; then
		echo "prepareInstalledDevelLib error:" \
			"there is both a shared and a static library for $libBaseName!"
		exit 1
	fi

	# The shared library file and the soname symlink stay, the development
	# directory gets symlinks to them; everything else moves there.
	for lib in $installDestDir$libDir/${libBaseName}${pattern:-.*}; do
		if [ "$lib" = "$sharedLib" ]; then
			symlinkRelative -s $installDestDir$libDir/$(basename $lib) \
				$installDestDir$developLibDir/
		elif [ "$lib" = "$sonameLib" ]; then
			ln -s $(basename $sharedLib) $installDestDir$developLibDir/$soname
		else
			if [[ "$lib" = *.la ]]; then
				fixDevelopLibDirReferences $lib
			fi
			mv $lib $installDestDir$developLibDir/
		fi
	done
}

prepareInstalledDevelLibs()
{
	while [ $# -ge 1 ]; do
		prepareInstalledDevelLib $1
		shift 1
	done
}

fixPkgconfig()
{
	local sourcePkgconfigDir=$installDestDir$libDir/pkgconfig
	local targetPkgconfigDir=$installDestDir$developLibDir/pkgconfig
	local file name

	if [ ! -d $sourcePkgconfigDir ]; then
		return
	fi

	mkdir -p $targetPkgconfigDir

	for file in $sourcePkgconfigDir/*; do
		name=$(basename $file)
		if [ "$1" == "strict" ]; then
			sed -e 's,^libdir=${prefix}/'${relativeLibDir}',libdir=${prefix}/'${relativeDevelopLibDir}',' \
				$file > $targetPkgconfigDir/$name
		else
			sed -e 's,^libdir=\(.*\),libdir=${prefix}/'${relativeDevelopLibDir}',' \
				-e 's,^includedir=\(.*\),includedir=${prefix}/'${relativeIncludeDir}',' \
				$file > $targetPkgconfigDir/$name
		fi
	done

	rm -r $sourcePkgconfigDir
}

fixCMake()
{
	local sourceCMakeDir=$installDestDir$libDir/cmake
	local targetCMakeDir=$installDestDir$developLibDir/cmake
	local path name

	if [ ! -d $sourceCMakeDir ]; then
		return
	fi

	mkdir -p $targetCMakeDir

	for path in $(find $sourceCMakeDir); do
		name=$(realpath --relative-to="$sourceCMakeDir" $path)
		if [ -d $path ]; then
			mkdir -p $targetCMakeDir/$name
			continue
		fi
		sed -e "s,${prose_runtime_libDir},${prose_runtime_developLibDir}," \
			$path > $targetCMakeDir/$name
	done

	rm -r $sourceCMakeDir
}

fixLibtoolArchives()
{
	# haikuporter rewrites /packages/... dependency paths in .la files; our
	# .la files only reference /boot/system, which needs no fixing.
	true
}

addResourcesToBinaries()
{
	# Usage: addResourcesToBinaries <rdefPath> <path> ...
	if [ $# -lt 2 ]; then
		echo >&2 "Usage: addResourcesToBinaries <rdefPath> <path> ..."
		exit 1
	fi

	local rdefPath="$1"
	shift 1

	# rc writes to <name>.rsrc when the output name has no .rsrc extension
	# (silently: curl's icon was lost that way), so the temp name gets one
	local rsrcPath
	rsrcPath=$(mktemp "${TMPDIR:-/tmp}/prose_resources.XXXXXX").rsrc
	rm -f "$rsrcPath" "${rsrcPath%.rsrc}"
	rc -o "$rsrcPath" "$rdefPath"

	while [ $# -gt 0 ]; do
		xres -o "$1" "$rsrcPath"
		shift 1
	done
	rm -f "$rsrcPath"
}

symlinkRelative()
{
	local flags
	while [ $# -ge 1 ] && [[ "$1" = -* ]]; do
		flags="$flags $1"
		shift 1
	done

	if [ $# -lt 2 ]; then
		echo "Usage: symlinkRelative <flags> <from> ... <to>" >&2
		exit 1
	fi

	declare -a fromPaths
	while [ $# -gt 1 ]; do
		fromPaths[${#fromPaths[@]}]="$1"
		shift 1
	done
	local toPath="$1"

	# make sure target path is absolute
	if [[ "$toPath" != /* ]]; then
		toPath="$(pwd)/$toPath"
	fi

	# get target path prefixes
	declare -a toPathPrefixes
	declare -a toPathUpPrefixes
	local path="$toPath"
	if [ -d "$path" ]; then
		path="$path/_"
	fi
	local upPrefix=
	while [ "$path" != / ]; do
		path="$(dirname "$path")"
		toPathPrefixes=("$path" "${toPathPrefixes[@]}")
		toPathUpPrefixes=("$upPrefix" "${toPathUpPrefixes[@]}")
		upPrefix=${upPrefix}../
	done

	# process the from paths
	declare -a processedFromPaths
	local fromPath
	for fromPath in "${fromPaths[@]}"; do
		if [[ "$fromPath" != /* ]]; then
			fromPath="$(pwd)/$fromPath"
		fi

		declare -a fromPathPrefixes=()
		local path="$fromPath"
		while [ "$path" != / ]; do
			path="$(dirname "$path")"
			fromPathPrefixes=("$path" "${fromPathPrefixes[@]}")
		done

		# get the longest common prefix
		local commonPrefix
		local i
		for (( i=0 ; i<${#fromPathPrefixes[@]} ; i++ )) ; do
			if [ "${fromPathPrefixes[$i]}" != "${toPathPrefixes[$i]}" ]; then
				break
			fi
			commonPrefix="${fromPathPrefixes[$i]}"
			upPrefix="${toPathUpPrefixes[$i]}"
		done
		local prefixLength=${#commonPrefix}
		if [ $prefixLength -gt 1 ]; then
			prefixLength=$(($prefixLength + 1))
		fi
		local fromSuffix="${fromPath:$prefixLength}"

		processedFromPaths[${#processedFromPaths[@]}]="$upPrefix$fromSuffix"
	done

	ln $flags "${processedFromPaths[@]}" "$toPath"
}

proseAddDeskbarSymlink()
{
	# Usage: proseAddDeskbarSymlink <menuDir> <appPath> [ <entryName> ]
	local menuDir="$1"
	local appPath="$2"
	local entryName="${3:-$(basename "$appPath")}"
	local targetDir="$dataDir/deskbar/menu/$menuDir"
	mkdir -p "$targetDir"
	symlinkRelative -s "$appPath" "$targetDir/$entryName"
}

addAppDeskbarSymlink()
{
	# Usage: addAppDeskbarSymlink <appPath> [ <entryName> ]
	if [ $# -lt 1 ]; then
		echo >&2 "Usage: addAppDeskbarSymlink <appPath> [ <entryName> ]"
		exit 1
	fi
	proseAddDeskbarSymlink Applications "$@"
}

addAppletDeskbarSymlink()
{
	# Usage: addAppletDeskbarSymlink <appPath> [ <entryName> ]
	if [ $# -lt 1 ]; then
		echo >&2 "Usage: addAppletDeskbarSymlink <appPath> [ <entryName> ]"
		exit 1
	fi
	proseAddDeskbarSymlink "Desktop applets" "$@"
}

addPreferencesDeskbarSymlink()
{
	# Usage: addPreferencesDeskbarSymlink <appPath> [ <entryName> ]
	if [ $# -lt 1 ]; then
		echo >&2 "Usage: addPreferencesDeskbarSymlink <appPath> [ <entryName> ]"
		exit 1
	fi
	proseAddDeskbarSymlink Preferences "$@"
}

packageEntries()
{
	# Usage: packageEntries <packageSuffix> <entry> ...
	# Moves the given entries (absolute, or relative to $prefix) into the
	# staging tree of the subpackage <packageSuffix> (e.g. "devel").
	if [ $# -lt 2 ]; then
		echo >&2 "Usage: packageEntries <packageSuffix> <entry> ..."
		exit 1
	fi

	local packageSuffix="$1"
	shift 1

	local packagePrefix
	packagePrefix=$(getPackagePrefix $packageSuffix)

	local file targetDir
	for file; do
		if [[ "$file" = /* ]]; then
			if [[ "$file" =~ "$installDestDir$prefix"/(.*) ]]; then
				file=${BASH_REMATCH[1]}
			else
				echo >&2 "packageEntries: error: absolute entry \"$file\""
				echo >&2 "isn't in \"$installDestDir$prefix\"."
				exit 1
			fi
		fi
		targetDir=$(dirname "$packagePrefix/$file")
		mkdir -p "$targetDir"
		mv "$installDestDir$prefix/$file" "$targetDir"
	done
}

proseStripDebugInfos()
{
	# the tail of haikuporter's extractDebugInfo, without the debuginfo file:
	# strip debug info, keeping the Haiku resources appended to the ELF file
	local strip path tmpfile
	strip=$(getTargetArchitectureCommand strip)
	for path in "${PROSE_DEBUG_INFO_PATHS[@]}"; do
		[ -f "$path" ] || continue
		tmpfile=$(mktemp "${TMPDIR:-/tmp}/prose_strip.XXXXXX")
		xres -o "$tmpfile" "$path"
		$strip --strip-debug "$path"
		xres -o "$path" "$tmpfile"
		rm -f "$tmpfile"
	done
}

# -- parse mode ------------------------------------------------------------------

proseRecipeKeys='SUMMARY|DESCRIPTION|HOMEPAGE|COPYRIGHT|LICENSE|REVISION|SOURCE_URI|SOURCE_FILENAME|SOURCE_DIR|CHECKSUM_SHA256|PATCHES|ADDITIONAL_FILES|ARCHITECTURES|SECONDARY_ARCHITECTURES|PROVIDES|REQUIRES|BUILD_REQUIRES|BUILD_PREREQUIRES|TEST_REQUIRES|SUPPLEMENTS|CONFLICTS|FRESHENS|REPLACES|GLOBAL_WRITABLE_FILES|USER_SETTINGS_FILES|POST_INSTALL_SCRIPTS|PRE_UNINSTALL_SCRIPTS|PACKAGE_USERS|PACKAGE_GROUPS|PACKAGE_NAME|PACKAGE_VERSION|DISABLE_SOURCE_PACKAGE|BUILD_PACKAGE_ACTIVATION_PHASE|MESSAGE|DEBUG_INFO_PACKAGES'

proseDumpRecipe()
{
	# NUL-separated key/value pairs of every recipe key that is set
	local key
	for key in $(compgen -v); do
		if [[ $key =~ ^($proseRecipeKeys)(_[0-9a-zA-Z_]+)?$ ]]; then
			printf '%s\0%s\0' "$key" "${!key}"
		fi
	done
	for key in PATCH BUILD INSTALL TEST; do
		if declare -F $key >/dev/null; then
			printf '%s\0%s\0' "PHASE_$key" 1
		fi
	done
}
