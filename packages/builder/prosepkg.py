#!/usr/bin/env python3
"""prosepkg — Prose's package builder.

Cross-builds haikuports recipes into Haiku packages (.hpkg) for arm64 on
this macOS host. Recipes run unmodified where possible; cross-specific
changes live in packages/builder/overlay. See packages/README.md.

Everything the builder writes lives below ROOT (the case-sensitive build
volume). The Haiku tree is an input only: `bootstrap` copies what it needs
(toolchain, host tools, system packages) once, and builds never touch the
tree again. The exception is `local-packages`, which the image build scripts
run: it copies the packages a Haiku tree's own list names into that tree's
generated/download/ and turns its downloads off.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

# -- configuration --------------------------------------------------------------

ROOT = Path(os.environ.get('PROSEPKG_ROOT', '/Volumes/HaikuSrc/prose-packages'))
# the Haiku tree: toolchain and host tools, and (SYSTEM_TREE, the same tree
# unless overridden) the system packages of the Prose build whose image we
# target -- /Volumes/HaikuSrc/haiku on the branch "prose", built by
# scripts/build-image.sh
HAIKU_TREE = Path(os.environ.get('PROSEPKG_HAIKU_TREE', '/Volumes/HaikuSrc/haiku'))
SYSTEM_TREE = Path(os.environ.get('PROSEPKG_SYSTEM_TREE', str(HAIKU_TREE)))
BUILDER_DIR = Path(__file__).resolve().parent
OVERLAY_DIR = BUILDER_DIR / 'overlay'
RUNTIME_SH = BUILDER_DIR / 'recipe-runtime.sh'

ARCH = 'arm64'
TRIPLE = 'aarch64-unknown-haiku'
# Target CPU: Apple M1 and later (Prose runs in VMs on Apple silicon). gcc 13
# has no apple-m* names; this is the M1 feature floor: ARMv8.4-A (LSE, RDM,
# RCPC, JSCVT, FCMA, DotProd, FlagM) + FP16/FHM + AES/SHA1/SHA2/SHA3/SHA512.
# Not "+crypto": for v8.4 that adds SM3/SM4, which Apple lacks. BF16/I8MM
# (M2+) and SVE/SME stay off. Wrappers put these first, so a package's own
# -march still wins for the files it compiles specially.
TARGET_CPU_FLAGS = '-march=armv8.4-a+fp16+fp16fml+aes+sha2+sha3 -mtune=cortex-x1'
BUILD_TRIPLE = 'aarch64-apple-darwin'
PACKAGER = 'Prose Package Builder <packages@prose.local>'
VENDOR = 'Prose'
JOBS = os.cpu_count() or 8

HAIKUPORTS = ROOT / 'haikuports'
TOOLCHAIN = ROOT / 'toolchain' / 'cross-tools-arm64'
HOSTTOOLS = ROOT / 'hosttools'
BASE = ROOT / 'base'
BASE_SYSROOT = BASE / 'sysroot'
ENV_DIR = ROOT / 'env'
DOWNLOADS = ROOT / 'downloads'
WORK = ROOT / 'work'
REPO = ROOT / 'repo'
CACHE = ROOT / 'cache'
LOGS = ROOT / 'logs'
RESULTS = ROOT / 'results.json'

BASH = '/opt/homebrew/bin/bash'
HOST_PATH = [
	'/opt/homebrew/opt/coreutils/libexec/gnubin',
	'/opt/homebrew/opt/gnu-sed/libexec/gnubin',
	# keg-only: Apple's bison is 2.3, which cannot read a %destructor with a
	# type tag (libnslog); the Haiku build puts these first too
	'/opt/homebrew/opt/bison/bin',
	'/opt/homebrew/opt/gettext/bin',
	'/opt/homebrew/bin',
	'/usr/bin', '/bin', '/usr/sbin', '/sbin',
]

# host tools the Haiku build produced for itself (tools/<dir>/<name> or tools/<name>)
HOST_TOOL_NAMES = [
	'rc', 'xres', 'mimeset', 'settype', 'setversion', 'addattr', 'copyattr',
	'resattr', 'collectcatkeys', 'linkcatkeys', 'package', 'package_repo',
	'bfs_shell',
]

# the system packages of the target image: what a Prose system provides
BASE_PACKAGES_BUILT = ['haiku.hpkg', 'haiku_devel.hpkg']
# commands the build environment provides (cross toolchain, host make and
# pkg-config): a recipe naming them in BUILD_REQUIRES (libprefs: cmd:gcc)
# must not make prosepkg build the gcc port
TOOLCHAIN_COMMANDS = {'cmd:' + c for c in ('gcc', 'g++', 'cc', 'c++', 'cpp', 'ld', 'as',
	'ar', 'nm', 'ranlib', 'strip', 'objcopy', 'objdump', 'readelf', 'make', 'pkg_config',
	'pkgconf')}

BINUTILS = ['addr2line', 'ar', 'as', 'c++filt', 'elfedit', 'ld', 'nm', 'objcopy',
	'objdump', 'ranlib', 'readelf', 'size', 'strings', 'strip']

RELATIVE_CONFIGURE_DIRS = [
	('dataDir', 'data'), ('dataRootDir', 'data'), ('binDir', 'bin'),
	('sbinDir', 'bin'), ('libDir', 'lib'), ('includeDir', 'develop/headers'),
	('oldIncludeDir', 'develop/headers'), ('docDir', 'documentation/packages/{name}'),
	('infoDir', 'documentation/info'), ('manDir', 'documentation/man'),
	('libExecDir', 'lib'), ('sharedStateDir', 'var'), ('localStateDir', 'var'),
]
RELATIVE_OTHER_DIRS = [
	('addOnsDir', 'add-ons'), ('appsDir', 'apps'), ('debugInfoDir', 'develop/debug'),
	('developDir', 'develop'), ('developDocDir', 'develop/documentation/{name}'),
	('developLibDir', 'develop/lib'), ('documentationDir', 'documentation'),
	('fontsDir', 'data/fonts'), ('postInstallDir', 'boot/post-install'),
	('preUninstallDir', 'boot/pre-uninstall'), ('preferencesDir', 'preferences'),
	('settingsDir', 'settings'),
]

# -- small utilities --------------------------------------------------------------


class BuildError(Exception):
	pass


def say(*parts):
	print('prosepkg:', *parts, flush=True)


def run(cmd, cwd=None, env=None, log=None, check=True, capture=False):
	"""Run a command; stream into the log file if one is given."""
	if log is not None:
		log.write('$ ' + ' '.join(str(c) for c in cmd) + '\n')
		log.flush()
		proc = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env,
			stdout=subprocess.PIPE if capture else log,
			stderr=subprocess.STDOUT if not capture else log, text=True)
	else:
		proc = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env,
			capture_output=capture, text=True)
	if check and proc.returncode != 0:
		raise BuildError('command failed (%d): %s' % (proc.returncode,
			' '.join(str(c) for c in cmd)))
	return proc


def write_file(path, text, mode=0o644):
	"""Replace a file by rename: never writes through an existing symlink."""
	path = Path(path)
	path.parent.mkdir(parents=True, exist_ok=True)
	tmp = path.with_name('.' + path.name + '.new')
	tmp.write_text(text)
	os.chmod(tmp, mode)
	os.replace(tmp, path)


def sha256(path):
	h = hashlib.sha256()
	with open(path, 'rb') as f:
		for chunk in iter(lambda: f.read(1 << 20), b''):
			h.update(chunk)
	return h.hexdigest()


def rmtree(path):
	path = Path(path)
	if path.is_symlink() or path.is_file():
		path.unlink()
	elif path.exists():
		# make everything writable first (read-only trees from extraction)
		subprocess.run(['chmod', '-R', 'u+w', str(path)], check=False)
		shutil.rmtree(path)


def clone_tree(src, dst):
	"""APFS clone (copy-on-write) of a directory tree."""
	rmtree(dst)
	dst.parent.mkdir(parents=True, exist_ok=True)
	run(['/bin/cp', '-c', '-R', src, dst])


def entries(value):
	"""Split a PROVIDES/REQUIRES style value into entries (comments dropped)."""
	result = []
	for line in (value or '').splitlines():
		line = line.split('#', 1)[0].strip()
		if line:
			result.append(line)
	return result


def entry_name(entry):
	"""'lib:libFoo >= 1.2' -> 'lib:libfoo' (Haiku compares resolvable names
	case-insensitively; the package tool stores them lowercased)"""
	return re.split(r'\s|[<>=!]', entry.strip(), maxsplit=1)[0].lower()


def natural_key(version):
	return [int(p) if p.isdigit() else p for p in re.split(r'(\d+)', version)]


def load_json(path, default):
	try:
		return json.loads(Path(path).read_text())
	except (OSError, ValueError):
		return default


def save_json(path, data):
	write_file(path, json.dumps(data, indent=1, sort_keys=True) + '\n')


def host_tool(name):
	return HOSTTOOLS / 'bin' / name


# -- bootstrap ----------------------------------------------------------------------


def bootstrap(args):
	for d in (ROOT, DOWNLOADS, WORK, REPO, CACHE, LOGS):
		d.mkdir(parents=True, exist_ok=True)
	refresh = set((args.refresh or '').split(',')) if args.refresh else set()
	redo = lambda step: step in refresh or 'all' in refresh
	bootstrap_toolchain(redo('toolchain'))
	bootstrap_hosttools(redo('hosttools'))
	bootstrap_hostsdk(redo('hostsdk'))
	bootstrap_base(redo('base'))
	bootstrap_env()
	check_toolchain()
	say('bootstrap complete:', ROOT)


def bootstrap_toolchain(refresh):
	src = HAIKU_TREE / 'generated' / 'cross-tools-arm64'
	if TOOLCHAIN.exists() and not refresh:
		return
	gcc = src / 'bin' / (TRIPLE + '-gcc')
	with open(gcc, 'rb') as f:
		if f.read(4) != b'\xcf\xfa\xed\xfe':
			raise BuildError('%s is not a Mach-O executable: the toolchain is '
				'damaged (see scripts/repair-cross-gcc.sh)' % gcc)
	say('copying toolchain from', src)
	rmtree(TOOLCHAIN)
	TOOLCHAIN.parent.mkdir(parents=True, exist_ok=True)
	run(['ditto', src, TOOLCHAIN])
	rmtree(TOOLCHAIN / 'sysroot')
	# libtool archives of the toolchain's own target libs point into the
	# source tree and at libstdc++.a, which build_cross_tools_gcc4 renamed so
	# that nothing links it by accident; libtool would do exactly that. On
	# Haiku, C++ libraries link libstdc++.so from gcc_syslibs.
	for la in list(TOOLCHAIN.glob('*/lib/*.la')) + list(TOOLCHAIN.glob('lib/*.la')):
		la.unlink()
	# read-only: nothing may ever write into the compiler again
	run(['chmod', '-R', 'a-w', TOOLCHAIN])


def bootstrap_hosttools(refresh):
	if (HOSTTOOLS / 'bin' / 'package').exists() and not refresh:
		return
	tools = HAIKU_TREE / 'generated' / 'objects' / 'darwin' / 'arm64' / 'release' / 'tools'
	libdir = HAIKU_TREE / 'generated' / 'objects' / 'darwin' / 'lib'
	say('copying host tools from', tools)
	rmtree(HOSTTOOLS)
	(HOSTTOOLS / 'bin').mkdir(parents=True)
	(HOSTTOOLS / 'lib').mkdir(parents=True)
	copied = []
	for name in HOST_TOOL_NAMES:
		for candidate in (tools / name / name, tools / name, tools / 'locale' / name):
			if candidate.is_file():
				shutil.copy2(candidate, HOSTTOOLS / 'bin' / name)
				copied.append(HOSTTOOLS / 'bin' / name)
				break
		else:
			raise BuildError('host tool %s not found under %s' % (name, tools))
	for lib in sorted(libdir.glob('*_build.so')):
		shutil.copy2(lib, HOSTTOOLS / 'lib' / lib.name)
		copied.append(HOSTTOOLS / 'lib' / lib.name)
	# jam (Haiku's), used by some recipes
	jam = shutil.which('jam')
	if jam:
		shutil.copy2(jam, HOSTTOOLS / 'bin' / 'jam')
	# make the copies independent of the Haiku tree: relative dylib paths
	prefix = str(libdir) + '/'
	for f in copied:
		out = run(['otool', '-L', f], capture=True).stdout.splitlines()[1:]
		changes = []
		for line in out:
			dep = line.strip().split(' ')[0]
			if dep.startswith(prefix):
				name = dep[len(prefix):]
				if f.parent.name == 'lib':
					changes += ['-change', dep, '@loader_path/' + name]
				else:
					changes += ['-change', dep, '@executable_path/../lib/' + name]
		if f.parent.name == 'lib':
			changes += ['-id', '@loader_path/' + f.name]
		if changes:
			run(['install_name_tool'] + changes + [f], capture=True)
			run(['codesign', '--force', '--sign', '-', f], capture=True)
	run([HOSTTOOLS / 'bin' / 'package', 'list', '-i',
		HAIKU_TREE / 'generated/objects/haiku/arm64/packaging/packages/makefile_engine.hpkg'],
		capture=True)


HOST_BE_CXX = r'''#!/bin/sh
# Compile and link a Be API program for this Mac, the way the Haiku build
# compiles its own host tools (rc, xres, mimeset ...): with the build-host
# headers and libbe_build/libroot_build, which cover the storage, support
# and app kits' file, resource, message and string classes -- enough for the
# tools a port runs during its build (Pe's rez).
#
# Usage: host-be-c++ -o <program> <sources...> [more compiler flags]
SDK=%(sdk)s
LIB=%(lib)s
out=""
prev=""
for a in "$@"; do [ "$prev" = -o ] && out="$a"; prev="$a"; done
/usr/bin/clang++ -O2 -std=gnu++17 -Wno-multichar -Wno-deprecated-declarations \
	-include "$SDK/headers/build/BeOSBuildCompatibility.h" \
	-DARCH_arm64 -D_NO_INLINE_ASM -D__NO_INLINE__ -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
	-D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -DHAIKU_HOST_USE_XATTR \
	-DHAIKU_HOST_PLATFORM_DARWIN -DHAIKU_HOST_PLATFORM_64_BIT -DHAIKU_PACKAGING_ARCH='"arm64"' \
	-iquote "$SDK/config_headers" \
	-I "$SDK/headers/build/host/darwin" -I "$SDK/headers/build" -I "$SDK/headers/build/os" \
	-I "$SDK/headers/build/os/add-ons/registrar" -I "$SDK/headers/build/os/app" \
	-I "$SDK/headers/build/os/drivers" -I "$SDK/headers/build/os/kernel" \
	-I "$SDK/headers/build/os/interface" -I "$SDK/headers/build/os/locale" \
	-I "$SDK/headers/build/os/storage" -I "$SDK/headers/build/os/support" \
	-I "$SDK/headers/build/private" -I "$SDK/headers/build/private/kernel" \
	-I "$SDK/headers/build/private/libroot" -I "$SDK/headers/build/private/system" \
	-I "$SDK/headers/private/system" \
	"$@" \
	"$LIB/libroot_build_function_remapper.a" "$LIB/libroot_build.so" "$LIB/libbe_build.so" -lz \
	-Wl,-headerpad_max_install_names \
	|| exit 1
if [ -n "$out" ]; then
	# the libraries' install names are relative to the loading binary
	# (@loader_path, for the tools next to them); this program lives elsewhere
	for l in libroot_build.so libbe_build.so; do
		install_name_tool -change "@loader_path/$l" "$LIB/$l" "$out"
	done
	codesign --force --sign - "$out" 2>/dev/null
fi
'''


def bootstrap_hostsdk(refresh):
	"""The build-host Be API, for tools a port compiles and runs during its
	own build: the Haiku tree's headers/build, which mostly forward to the
	real headers (headers/os, headers/private, headers/config, as <../os/...>),
	the config headers, libroot_build's function remapper archive (the .so
	files came with the host tools), and host-be-c++."""
	sdk = HOSTTOOLS / 'sdk'
	if (sdk / 'headers' / 'build').is_dir() and not refresh:
		return
	say('copying the build-host Be API headers from', HAIKU_TREE)
	rmtree(sdk)
	for rel in ('headers/build', 'headers/os', 'headers/private', 'headers/config',
			'build/config_headers'):
		dst = sdk / ('config_headers' if rel == 'build/config_headers' else rel)
		dst.parent.mkdir(parents=True, exist_ok=True)
		shutil.copytree(HAIKU_TREE / rel, dst)
	# the umbrella headers (StorageKit.h, SupportKit.h ...) over what is there
	for kit, directory in (('Storage', 'storage'), ('Support', 'support'), ('App', 'app')):
		headers = sorted(h.name for h in (sdk / 'headers' / 'build' / 'os' / directory).glob('*.h'))
		write_file(sdk / 'headers' / 'build' / 'os' / (kit + 'Kit.h'),
			'// the build-host subset of the %s Kit (prosepkg host SDK)\n' % kit
			+ ''.join('#include <%s/%s>\n' % (directory, h) for h in headers))
	remapper = HAIKU_TREE / 'generated' / 'objects' / 'darwin' / 'arm64' / 'release' / 'build' \
		/ 'libroot' / 'libroot_build_function_remapper.a'
	shutil.copy2(remapper, HOSTTOOLS / 'lib' / remapper.name)
	write_file(HOSTTOOLS / 'bin' / 'host-be-c++',
		HOST_BE_CXX % {'sdk': sh_quote(sdk), 'lib': sh_quote(HOSTTOOLS / 'lib')}, 0o755)


def base_package_sources():
	"""haiku + haiku_devel of the Prose build, makefile_engine, and the
	bootstrap system packages its image is made of. Only *_bootstrap-*
	packages: the OS build's download dir may also hold packages we built."""
	sources = []
	for tree in (SYSTEM_TREE, HAIKU_TREE):
		built = tree / 'generated' / 'objects' / 'haiku' / ARCH / 'packaging' / 'packages'
		if (built / 'haiku.hpkg').exists():
			sources = [built / name for name in BASE_PACKAGES_BUILT]
			break
	for tree in (SYSTEM_TREE, HAIKU_TREE):
		engine = tree / 'generated' / 'objects' / 'haiku' / ARCH / 'packaging' / 'packages' \
			/ 'makefile_engine.hpkg'
		if engine.exists():
			sources.append(engine)
			break
	download = SYSTEM_TREE / 'generated' / 'download'
	sources += sorted(p for p in download.glob('*_bootstrap-*.hpkg')
		if p.name.endswith(('-%s.hpkg' % ARCH, '-any.hpkg')))
	return sources


def package_info(hpkg):
	"""Parse `package list -i` into a dict of lists."""
	out = run([host_tool('package'), 'list', '-i', hpkg], capture=True).stdout
	info = {}
	for line in out.splitlines():
		m = re.match(r'\s*([a-z][a-z -]*):\s*(.*)$', line)
		if m:
			info.setdefault(m.group(1).strip(), []).append(m.group(2).strip())
	return info


def bootstrap_base(refresh):
	if (BASE / 'provides.json').exists() and not refresh:
		return
	say('setting up the base system packages and sysroot')
	rmtree(BASE)
	(BASE / 'packages').mkdir(parents=True)
	manifest = {}
	provides = {}
	system = BASE_SYSROOT / 'boot' / 'system'
	system.mkdir(parents=True)
	for src in base_package_sources():
		dst = BASE / 'packages' / src.name
		shutil.copy2(src, dst)
		manifest[src.name] = {'source': str(src), 'sha256': sha256(dst)}
		info = package_info(dst)
		name = info['name'][0]
		for p in info.get('provides', []) + [name]:
			provides.setdefault(entry_name(p), []).append(name)
		run([host_tool('package'), 'extract', '-C', system, dst], capture=True)
		(system / '.PackageInfo').unlink(missing_ok=True)
	link_package_prefixes(BASE_SYSROOT)
	# the system MIME database (attributes live in user.haiku.* xattrs)
	for tree in (SYSTEM_TREE, HAIKU_TREE):
		mimedb = tree / 'generated' / 'objects' / 'common' / 'data' / 'mime_db' / 'mime_db'
		if mimedb.is_dir():
			break
	run(['ditto', mimedb, BASE / 'mime_db'])
	save_json(BASE / 'manifest.json', manifest)
	save_json(BASE / 'provides.json', provides)


_env_written = set()


def link_package_prefixes(sysroot):
	"""Packages built by haikuporter (the bootstrap system packages) record
	their prefix as /packages/<package>/.self -- packagefs's package links,
	e.g. in .pc files. Recreate the links they use: .self -> /boot/system,
	.settings -> /boot/system/settings (relative, inside the sysroot)."""
	pattern = re.compile(rb'/packages/([^/\s"\']+)/\.(self|settings)\b')
	found = set()
	for path in (sysroot / 'boot' / 'system').rglob('*'):
		if path.is_file() and not path.is_symlink() and path.stat().st_size < 4 << 20:
			data = path.read_bytes()
			if b'/packages/' in data and b'\0' not in data[:8192]:
				found.update(pattern.findall(data))
	for name, kind in sorted(found):
		link = sysroot / 'packages' / name.decode() / ('.' + kind.decode())
		link.parent.mkdir(parents=True, exist_ok=True)
		if not link.is_symlink():
			os.symlink('../../boot/system' + ('/settings' if kind == b'settings' else ''), link)
	return found


def wrapper(path, body):
	write_file(path, '#!/bin/bash\n# generated by prosepkg bootstrap; do not edit\n' + body, 0o755)
	_env_written.add(Path(path))


COMPILER_WRAPPER = r'''
# Cross compiler for the current build's sysroot. Haiku-absolute paths in
# arguments (/boot/..., /system/..., /packages/...) are mapped into the
# sysroot, like a chroot would see them.
: "${PROSE_SYSROOT:?PROSE_SYSROOT is not set (run builds through prosepkg)}"
args=()
for a in "$@"; do
	case "$a" in
	/boot/*|/packages/*) a="$PROSE_SYSROOT$a" ;;
	/system/*) a="$PROSE_SYSROOT/boot$a" ;;
	-I/*|-L/*|-B/*|-isystem/*|-iquote/*|-idirafter/*|-include/*|-imacros/*)
		# joined forms, as the makefile-engine writes them (-isystem/system/...)
		opt=${a%%%%/*}; path=/${a#*/}
		case "$path" in
		/boot/*|/packages/*) a="$opt$PROSE_SYSROOT$path" ;;
		/system/*) a="$opt$PROSE_SYSROOT/boot$path" ;;
		esac ;;
	esac
	args+=("$a")
done
# The cross gcc was configured --disable-shared: its driver links only the
# static libgcc, without the unwinder. A native Haiku gcc links libgcc_s, so
# C++ code linked through the C driver (the makefile-engine links with $(CC))
# finds _Unwind_Resume there. As-needed: C programs don't get the dependency.
link=1
for a in "$@"; do
	case "$a" in
	-c|-S|-E|-M|-MM|-r|-nostdlib|-nodefaultlibs|-static|-v|--version|-print-*|-dump*|-\#\#\#)
		link=0 ;;
	esac
done
[ $# -gt 0 ] && [ $link = 1 ] && args+=(-Wl,--as-needed -lgcc_s -Wl,--no-as-needed)
exec %(real)s --sysroot="$PROSE_SYSROOT" %(cpu)s "${args[@]}"
'''


MAKE_WRAPPER = r"""#!/opt/homebrew/bin/python3
# generated by prosepkg bootstrap; do not edit
# make for cross builds:
#  - INSTALL exports DESTDIR. It also goes on the command line, which beats
#    Makefiles that assign "DESTDIR =". A command-line value that already
#    points into the staging tree (make install PREFIX=$prefix, MANDIR=$manDir)
#    becomes the runtime path again when the Makefile roots that variable
#    under DESTDIR ($(DESTDIR)$(PREFIX)), so both re-root it once; a value
#    the Makefile uses as given stays staged; without DESTDIR in the
#    Makefile at all, DESTDIR is dropped
#  - `include /boot/system/...` (and /system/...) names paths inside the
#    chroot a Haiku build runs in; a makefile with them runs as a copy whose
#    includes point into the build sysroot; BUILDHOME=/system/develop (the
#    makefile-engine's build-time develop dir) likewise
#  - the real GNU make runs as "make", so $(MAKE) finds this wrapper again
import os, re, sys

REAL = '@REAL_MAKE@'  # the real GNU make, not the /usr/bin/make shim (which
                      # re-execs it by full path, so $(MAKE) would bypass us)
args = sys.argv[1:]
env = dict(os.environ)
sysroot = env.get('PROSE_SYSROOT')
root = env.get('PROSE_STAGING_ROOT')

# which makefiles this make reads: -C dirs, -f files, or the default one
directory = '.'
files = []
i = 0
while i < len(args):
	a = args[i]
	if a in ('-C', '--directory') and i + 1 < len(args):
		directory = os.path.join(directory, args[i + 1]); i += 2; continue
	if a.startswith('--directory='):
		directory = os.path.join(directory, a.split('=', 1)[1])
	elif a.startswith('-C') and len(a) > 2:
		directory = os.path.join(directory, a[2:])
	elif a in ('-f', '--file', '--makefile') and i + 1 < len(args):
		files.append((i + 1, args[i + 1], args[i + 1])); i += 2; continue
	elif a.startswith(('--file=', '--makefile=')):
		files.append((i, a, a.split('=', 1)[1]))
	elif a.startswith('-f') and len(a) > 2:
		files.append((i, a, a[2:]))
	i += 1
if not files:
	for name in ('GNUmakefile', 'makefile', 'Makefile'):
		if os.path.exists(os.path.join(directory, name)):
			files.append((None, None, name))
			break

def read(name):
	try:
		with open(os.path.join(directory, name)) as f:
			return f.read()
	except (OSError, UnicodeDecodeError):
		return ''

texts = {name: read(name) for _, _, name in files}

# a chroot path in a command-line variable names something in the sysroot
# here (BUILDHOME=/system/develop, the netsurf libraries' NSSHARED=/system/
# data/netsurf-buildsystem that their Makefiles include from): map it when
# the sysroot has it. A destination that does not exist there stays as it
# is, and the recipes give destinations as the staged $prefix anyway.
for i, a in enumerate(args):
	if not sysroot or a.startswith('-') or '=' not in a:
		continue
	name, value = a.split('=', 1)
	if value.startswith('/boot/'):
		mapped = sysroot + value
	elif value.startswith('/system/'):
		mapped = sysroot + '/boot' + value
	else:
		continue
	if os.path.exists(mapped):
		args[i] = name + '=' + mapped

# DESTDIR during INSTALL
if root and env.get('DESTDIR'):
	assigned = [(i, a) for i, a in enumerate(args) if not a.startswith('-') and '=' in a]
	staged = [(i, a) for i, a in assigned if a.split('=', 1)[1].startswith(root)]
	uses_destdir = any('DESTDIR' in text for text in texts.values())
	if staged and not uses_destdir:
		del env['DESTDIR']
	else:
		for i, a in staged:
			name, value = a.split('=', 1)
			# only a variable the Makefile itself roots under DESTDIR
			# ($(DESTDIR)$(PREFIX)) goes back to the runtime path; one it uses
			# as given stays staged (BASE=... in netsurf's buildsystem, whose
			# default is $(DESTDIR)$(PREFIX)/...): the install would otherwise
			# land in the Mac's /boot. A doubled staging prefix, when a derived
			# variable gets DESTDIR, is folded back after the phase.
			applied = re.compile(r'\$[({]DESTDIR[)}]/?\$[({]%s[)}]' % re.escape(name))
			if any(applied.search(text) for text in texts.values()):
				args[i] = name + '=' + value[len(root):]
		if not any(a.startswith('DESTDIR=') for _, a in assigned):
			args.append('DESTDIR=' + env['DESTDIR'])

# recipes run in /bin/sh, on macOS bash 3.2 in POSIX mode: "echo -n" prints
# the -n (NetSurf's link.d came out unreadable). Haiku's /bin/sh is bash 5;
# the same one runs them here
if not any(a.startswith('SHELL=') for a in args):
	args.append('SHELL=@BASH@')

# absolute Haiku includes
INCLUDE = re.compile(r'^(\s*-?include\s+)/(boot/|system/)', re.M)
for index, arg, name in files:
	text = texts.get(name, '')
	if not sysroot or not INCLUDE.search(text):
		continue
	text = INCLUDE.sub(lambda m: m.group(1) + sysroot + '/boot/'
		+ ('system/' if m.group(2) == 'system/' else ''), text)
	copy = os.path.join(os.path.dirname(name), '.prose.' + os.path.basename(name))
	with open(os.path.join(directory, copy), 'w') as f:
		f.write(text)
	if index is None:
		args = ['-f', copy] + args
	elif arg == name:
		args[index] = copy
	else:
		args[index] = '-f' + copy

os.execve(REAL, ['make'] + args, env)
"""


def bootstrap_env():
	say('generating the build environment in', ENV_DIR)
	# no rmtree: builds may be running; every file is replaced by rename and
	# leftovers are removed at the end
	bindir = ENV_DIR / 'bin'
	bindir.mkdir(parents=True, exist_ok=True)
	before = {p for p in ENV_DIR.rglob('*') if p.is_file() or p.is_symlink()}
	global _env_written
	_env_written = set()
	real = TOOLCHAIN / 'bin'
	for tool in ('gcc', 'g++', 'c++', 'cpp'):
		body = COMPILER_WRAPPER % {'real': real / (TRIPLE + '-' + tool), 'cpu': TARGET_CPU_FLAGS}
		wrapper(bindir / (TRIPLE + '-' + tool), body)
		wrapper(bindir / tool, body)
	cc = COMPILER_WRAPPER % {'real': real / (TRIPLE + '-gcc'), 'cpu': TARGET_CPU_FLAGS}
	wrapper(bindir / (TRIPLE + '-cc'), cc)
	wrapper(bindir / 'cc', cc)
	for tool in BINUTILS:
		extra = ' --sysroot="$PROSE_SYSROOT"' if tool == 'ld' else ''
		body = 'exec %s%s "$@"\n' % (real / (TRIPLE + '-' + tool), extra)
		wrapper(bindir / (TRIPLE + '-' + tool), body)
		wrapper(bindir / tool, body)
	pkgconfig = r'''
: "${PROSE_SYSROOT:?}"
S="$PROSE_SYSROOT/boot/system"
export PKG_CONFIG_SYSROOT_DIR="$PROSE_SYSROOT"
export PKG_CONFIG_LIBDIR="$S/develop/lib/pkgconfig:$S/data/pkgconfig:$S/lib/pkgconfig"
unset PKG_CONFIG_PATH
exec /opt/homebrew/bin/pkgconf "$@"
'''
	wrapper(bindir / 'pkg-config', pkgconfig)
	wrapper(bindir / (TRIPLE + '-pkg-config'), pkgconfig)
	wrapper(bindir / 'mimeset', r'''
# host mimeset with the build's MIME databases (writable one first)
for a in "$@"; do
	[ "$a" = --mimedb ] && exec %(tool)s "$@"
done
exec %(tool)s --mimedb "${PROSE_MIMEDB_WORK:?}" --mimedb "${PROSE_MIMEDB_SYSTEM:?}" "$@"
''' % {'tool': host_tool('mimeset')})
	# Haiku utilities that makefiles call while being parsed
	wrapper(bindir / 'finddir', r'''
# finddir for the build sysroot (what a chroot would answer)
: "${PROSE_SYSROOT:?}"
S="$PROSE_SYSROOT/boot/system"; H="$PROSE_SYSROOT/boot/home/config"
while [ $# -gt 0 ]; do
	case "$1" in -v|-c) shift 2 ;; -e|-p) shift ;; *) break ;; esac
done
case "$1" in
B_SYSTEM_DIRECTORY|B_BEOS_SYSTEM_DIRECTORY) echo "$S" ;;
B_SYSTEM_ADDONS_DIRECTORY|B_BEOS_ADDONS_DIRECTORY) echo "$S/add-ons" ;;
B_SYSTEM_APPS_DIRECTORY|B_APPS_DIRECTORY|B_BEOS_APPS_DIRECTORY) echo "$S/apps" ;;
B_SYSTEM_BIN_DIRECTORY|B_BEOS_BIN_DIRECTORY) echo "$S/bin" ;;
B_SYSTEM_DATA_DIRECTORY|B_BEOS_DATA_DIRECTORY) echo "$S/data" ;;
B_SYSTEM_DEVELOP_DIRECTORY|B_DEVELOP_DIRECTORY) echo "$S/develop" ;;
B_SYSTEM_DOCUMENTATION_DIRECTORY) echo "$S/documentation" ;;
B_SYSTEM_HEADERS_DIRECTORY) echo "$S/develop/headers" ;;
B_SYSTEM_LIB_DIRECTORY|B_BEOS_LIB_DIRECTORY) echo "$S/lib" ;;
B_SYSTEM_DEVELOP_LIB_DIRECTORY) echo "$S/develop/lib" ;;
B_SYSTEM_PREFERENCES_DIRECTORY|B_PREFERENCES_DIRECTORY) echo "$S/preferences" ;;
B_SYSTEM_SETTINGS_DIRECTORY|B_COMMON_SETTINGS_DIRECTORY) echo "$S/settings" ;;
B_SYSTEM_ETC_DIRECTORY|B_COMMON_ETC_DIRECTORY) echo "$S/settings/etc" ;;
B_SYSTEM_NONPACKAGED_DIRECTORY) echo "$S/non-packaged" ;;
B_SYSTEM_NONPACKAGED_*) d="${1#B_SYSTEM_NONPACKAGED_}"; d="${d%_DIRECTORY}"
	case "$d" in ADDONS) d=add-ons ;; DEVELOP) d=develop ;; *) d=$(echo "$d" | tr A-Z a-z) ;; esac
	echo "$S/non-packaged/$d" ;;
B_USER_DIRECTORY) echo "$PROSE_SYSROOT/boot/home" ;;
B_USER_CONFIG_DIRECTORY) echo "$H" ;;
B_USER_NONPACKAGED_*) d="${1#B_USER_NONPACKAGED_}"; d="${d%_DIRECTORY}"
	case "$d" in ADDONS) d=add-ons ;; DEVELOP) d=develop ;; *) d=$(echo "$d" | tr A-Z a-z) ;; esac
	echo "$H/non-packaged/$d" ;;
B_USER_*) d="${1#B_USER_}"; d="${d%_DIRECTORY}"
	case "$d" in ADDONS) d=add-ons ;; *) d=$(echo "$d" | tr A-Z a-z) ;; esac
	echo "$H/$d" ;;
*) echo "finddir: unsupported constant $1" >&2; exit 1 ;;
esac
''')
	wrapper(bindir / 'findpaths', r'''
# findpaths for the build sysroot: the system installation location only.
# findpaths [-e] [-a arch] [-r dependency] [-c separator] <kind> [<subpath>]
: "${PROSE_SYSROOT:?}"
S="$PROSE_SYSROOT/boot/system"
existing=0
while [ $# -gt 0 ]; do
	case "$1" in -a|-r|-c) shift 2 ;; -e) existing=1; shift ;; -p|-l) shift ;; *) break ;; esac
done
d="${1#B_FIND_PATH_}"; d="${d%_DIRECTORY}"
case "$d" in
ADD_ONS) d=add-ons ;; APPS) d=apps ;; BIN) d=bin ;; BOOT) d=boot ;;
CACHE) d=cache ;; DATA) d=data ;; DEVELOP) d=develop ;;
DEVELOP_LIB) d=develop/lib ;; DOCUMENTATION) d=documentation ;;
ETC) d=settings/etc ;; FONTS) d=data/fonts ;; HEADERS) d=develop/headers ;;
LIB) d=lib ;; LOG) d=var/log ;; MEDIA_NODES) d=add-ons/media ;;
PACKAGES) d=packages ;; PREFERENCES) d=preferences ;; SERVERS) d=servers ;;
SETTINGS) d=settings ;; SOUNDS) d=data/sounds ;; SPOOL) d=var/spool ;;
TRANSLATORS) d=add-ons/Translators ;; VAR) d=var ;; IMAGE_PATH) d="" ;;
*) echo "findpaths: unsupported constant $1" >&2; exit 1 ;;
esac
path="$S${d:+/$d}${2:+/$2}"
[ $existing = 1 ] && [ ! -e "$path" ] && exit 0
echo "$path"
''')
	wrapper(bindir / 'linkcatkeys', r'''
# The host build of linkcatkeys cannot write into a binary (-tr goes through
# entry_ref APIs libbe_build lacks). For -tr, write a catalog file and embed
# it with xres the way DefaultCatalog::WriteToResource does: type 'CADA',
# id CatKey::HashFun(language) as int32, name = language.
REAL=%(real)s
XRES=%(xres)s
target="" lang="" tr=0 args=()
while [ $# -gt 0 ]; do
	case "$1" in
	-tr) tr=1 ;;
	-o) target="$2"; shift ;;
	-l) lang="$2"; args+=(-l "$2"); shift ;;
	*) args+=("$1") ;;
	esac
	shift
done
[ $tr = 1 ] || exec "$REAL" ${target:+-o "$target"} "${args[@]}"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/linkcatkeys.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
"$REAL" -o "$tmp/catalog" "${args[@]}" || exit 1
h=0
for ((i = 0; i < ${#lang}; i++)); do
	printf -v c '%%d' "'${lang:i:1}"
	(( c > 127 )) && (( c -= 256 ))
	h=$(( (5 * h + c) & 0xffffffff ))
done
h=$(( (5 * h + 1) & 0xffffffff ))
(( h >= 0x80000000 )) && (( h -= 0x100000000 ))
# xres -o <executable> drops existing resources: carry them over
"$XRES" -o "$tmp/old.rsrc" "$target"
old=()
[ -f "$tmp/old.rsrc" ] && old=("$tmp/old.rsrc")
exec "$XRES" -o "$target" "${old[@]}" -a "CADA:$h:$lang" "$tmp/catalog"
''' % {'real': host_tool('linkcatkeys'), 'xres': host_tool('xres')})
	wrapper(bindir / 'mkdepend', r'''
# makefile-engine dependency files: package builds are one-shot, an empty
# dependency file is all make needs
while [ $# -gt 0 ]; do
	[ "$1" = -f ] && { : > "$2"; exit 0; }
	shift
done
''')
	wrapper(bindir / 'uname', r'''
# uname as Haiku arm64 answers it (what a build in a Haiku chroot sees);
# makefiles branch on it (e.g. giflib builds a .dylib for "Darwin")
out=()
add() { out+=("$1"); }
[ $# -eq 0 ] && set -- -s
for a in "$@"; do
	case "$a" in
	-a|--all) set -- -snrvmo; add Haiku; add prose; add R1~beta6+development
		add "hrev60122"; add arm64; add Haiku; break ;;
	--kernel-name) add Haiku ;;
	--nodename) add prose ;;
	--kernel-release) add R1~beta6+development ;;
	--kernel-version) add hrev60122 ;;
	--machine) add arm64 ;;
	--processor|--hardware-platform) add unknown ;;
	--operating-system) add Haiku ;;
	-*) flags=${a#-}
		while [ -n "$flags" ]; do
			case "${flags:0:1}" in
			s) add Haiku ;; n) add prose ;; r) add R1~beta6+development ;;
			v) add hrev60122 ;; m) add arm64 ;; p|i) add unknown ;; o) add Haiku ;;
			*) echo "uname: invalid option -- '${flags:0:1}'" >&2; exit 1 ;;
			esac
			flags=${flags:1}
		done ;;
	*) echo "uname: extra operand '$a'" >&2; exit 1 ;;
	esac
done
echo "${out[*]}"
''')
	# Homebrew installs GNU libtoolize as glibtoolize (Apple owns "libtool")
	wrapper(bindir / 'libtoolize', 'exec /opt/homebrew/bin/glibtoolize "$@"\n')
	real_make = run(['xcrun', '-f', 'make'], capture=True, check=False).stdout.strip() \
		or '/usr/bin/make'
	write_file(bindir / 'make', MAKE_WRAPPER.replace('@REAL_MAKE@', real_make).replace('@BASH@', BASH), 0o755)
	_env_written.add(bindir / 'make')
	wrapper(bindir / 'jam', r'''
# jam as a Haiku chroot runs it: its built-in OS variable names the build
# host, and Jamrules branch on it (Pe links BeOS R5's -lnet -lstdc++.r4
# when OS isn't HAIKU)
for a in "$@"; do
	case "$a" in -sOS=*) exec %s "$@" ;; esac
done
exec %s -sOS=HAIKU "$@"
''' % (host_tool('jam'), host_tool('jam')))
	wrapper(bindir / 'getarch', 'echo %s\n' % ARCH)
	wrapper(bindir / 'setarch', r'''
# single-architecture target: setarch <arch> [command...] just runs it
shift
[ $# -gt 0 ] && exec "$@"
''')
	# cmake toolchain file and autoconf site defaults
	tc = ENV_DIR / 'cmake-toolchain.cmake'
	write_file(tc, '''# generated by prosepkg bootstrap
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT "$ENV{PROSE_SYSROOT}")
set(CMAKE_C_COMPILER "%(bin)s/%(t)s-gcc")
set(CMAKE_CXX_COMPILER "%(bin)s/%(t)s-g++")
set(CMAKE_AR "%(bin)s/%(t)s-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "%(bin)s/%(t)s-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "%(bin)s/%(t)s-strip" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "%(bin)s/%(t)s-objcopy" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH "$ENV{PROSE_SYSROOT}/boot/system" "$ENV{PROSE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
''' % {'bin': bindir, 't': TRIPLE})
	_env_written.add(tc)
	_env_written.add(ENV_DIR / 'config.site')
	write_file(ENV_DIR / 'config.site', '''# generated by prosepkg bootstrap
# Haiku answers for configure tests that cannot run target code here.
ac_cv_func_malloc_0_nonnull=${ac_cv_func_malloc_0_nonnull=yes}
ac_cv_func_realloc_0_nonnull=${ac_cv_func_realloc_0_nonnull=yes}
# gnulib's strcasecmp check runs a test program (diffutils, grep ...)
gl_cv_func_strcasecmp_works=${gl_cv_func_strcasecmp_works=yes}
''')
	for stale in before - _env_written:
		stale.unlink()


def check_toolchain():
	"""Compile and link a Be API program against the base sysroot."""
	tmp = CACHE / 'toolchain-check'
	rmtree(tmp)
	tmp.mkdir(parents=True)
	(tmp / 'hello.cpp').write_text(
		'#include <Application.h>\n#include <String.h>\n'
		'int main() { BApplication app("application/x-vnd.prose-check");'
		' BString s("ok"); return s.Length() == 2 ? 0 : 1; }\n')
	env = build_env(BASE_SYSROOT, tmp)
	run(['g++', '-O2', 'hello.cpp', '-o', 'hello', '-lbe'], cwd=tmp, env=env)
	out = run([TOOLCHAIN / 'bin' / (TRIPLE + '-readelf'), '-d', tmp / 'hello'],
		capture=True).stdout
	if 'libbe.so' not in out:
		raise BuildError('toolchain check: hello does not link libbe.so')
	say('toolchain check passed (Be API program links for %s)' % ARCH)


# -- recipes ------------------------------------------------------------------------


def all_recipe_files():
	"""{port name: [(version, path, category)]}; overlay recipes win."""
	found = {}
	for tree in (OVERLAY_DIR, HAIKUPORTS):
		if not tree.is_dir():
			continue
		for recipe in tree.glob('*/*/*.recipe'):
			m = re.match(r'^([^-]+)-(.+)\.recipe$', recipe.name)
			if not m:
				continue
			name, version = m.group(1), m.group(2)
			bucket = found.setdefault(name, [])
			if any(v == version for v, _, _ in bucket):
				continue
			bucket.append((version, recipe, recipe.parent.parent.name))
	return found


def dir_variables(name, prefix):
	values = {'prefix': prefix, 'sysconfDir': prefix + '/settings'}
	for var, rel in RELATIVE_CONFIGURE_DIRS + RELATIVE_OTHER_DIRS:
		values[var] = prefix + '/' + rel.format(name=name)
	return values


def shell_variables(port_name, version, revision, recipe, phase_prefix, extra=None):
	"""The variables haikuporter predefines for recipes, prosepkg-style."""
	runtime = dir_variables(port_name, '/boot/system')
	phase = dir_variables(port_name, phase_prefix)
	v = {
		'portName': port_name, 'portBaseName': port_name,
		'portVersion': version, 'portVersionedName': '%s-%s' % (port_name, version),
		'portRevision': revision, 'portFullVersion': '%s-%s' % (version, revision),
		'portRevisionedName': '%s-%s-%s' % (port_name, version, revision),
		'portDir': str(recipe.parent), 'portBaseDir': str(recipe.parent),
		'haikuVersion': 'r1~alpha1',
		'buildArchitecture': ARCH, 'targetArchitecture': ARCH,
		'effectiveTargetArchitecture': ARCH,
		'effectiveTargetMachineTriple': TRIPLE,
		'effectiveTargetMachineTripleAsName': TRIPLE.replace('-', '_'),
		'targetMachineTriple': TRIPLE, 'targetMachineTripleAsName': TRIPLE.replace('-', '_'),
		'buildMachineTriple': BUILD_TRIPLE, 'proseBuildTriple': BUILD_TRIPLE,
		'isCrossRepository': 'false',
		'secondaryArchSuffix': '', 'secondaryArchSubDir': '',
		'jobs': str(JOBS), 'jobArgs': '-j%d' % JOBS,
		'packagerName': 'Prose Package Builder', 'packagerEmail': 'packages@prose.local',
		'SOURCE_DIR': '%s-%s' % (port_name, version),
		'installDestDir': '',
		'configureDirVariables': ' '.join(['prefix', 'sysconfDir']
			+ [k for k, _ in RELATIVE_CONFIGURE_DIRS]),
		'configureDirArgs': ' '.join('--%s=%s' % (k.lower(), runtime[k])
			for k in ['prefix', 'sysconfDir'] + [k for k, _ in RELATIVE_CONFIGURE_DIRS]),
		'cmakeDirArgs': ' '.join(['-DCMAKE_INSTALL_PREFIX=' + runtime['prefix'],
			'-DCMAKE_INSTALL_SYSCONFDIR=' + runtime['sysconfDir']]
			+ ['-DCMAKE_INSTALL_%s=%s' % (k.upper(), rel.format(name=port_name))
				for k, rel in RELATIVE_CONFIGURE_DIRS]),
	}
	for var, rel in RELATIVE_CONFIGURE_DIRS + RELATIVE_OTHER_DIRS:
		v['relative' + var[0].upper() + var[1:]] = rel.format(name=port_name)
	v.update(phase)
	for k, val in runtime.items():
		v['prose_runtime_' + k] = val
	if extra:
		v.update(extra)
	return v


def shell_setters(variables):
	return ''.join("%s='%s'\n" % (k, str(v).replace("'", "'\\''"))
		for k, v in sorted(variables.items()))


def overlay_snippet(recipe):
	"""packages/builder/overlay/<category>/<port>/<recipe-stem>.prose.sh"""
	category, port = recipe.parent.parent.name, recipe.parent.name
	snippet = OVERLAY_DIR / category / port / (recipe.stem + '.prose.sh')
	return snippet if snippet.is_file() else None


def evaluate_recipe(recipe, name, version):
	"""Source a recipe in parse mode and return its keys."""
	variables = shell_variables(name, version, '$REVISION', recipe, '/boot/system')
	script = shell_setters(variables) + 'declare -a PROSE_DEBUG_INFO_PATHS=()\n'
	script += '. %s\n' % sh_quote(RUNTIME_SH)
	script += '. %s >/dev/null\n' % sh_quote(recipe)
	snippet = overlay_snippet(recipe)
	if snippet:
		script += '. %s >/dev/null\n' % sh_quote(snippet)
	script += 'proseDumpRecipe\n'
	proc = subprocess.run([BASH, '-c', script], capture_output=True,
		env={'PATH': ':'.join(HOST_PATH), 'HOME': os.environ.get('HOME', '/tmp')})
	if proc.returncode != 0:
		raise BuildError('recipe %s does not evaluate: %s' % (recipe,
			proc.stderr.decode(errors='replace').strip()[-400:]))
	parts = proc.stdout.decode(errors='replace').split('\0')
	keys = {}
	for i in range(0, len(parts) - 1, 2):
		keys[parts[i]] = parts[i + 1]
	# the revision is known now: resolve the placeholders
	revision = keys.get('REVISION', '1')
	for k, v in keys.items():
		keys[k] = v.replace('$REVISION', revision)
	return keys


def sh_quote(s):
	return "'" + str(s).replace("'", "'\\''") + "'"


def arch_status(keys, suffix=''):
	"""'ok', 'untested' or 'broken' for arm64 per ARCHITECTURES."""
	archs = (keys.get('ARCHITECTURES' + suffix) or keys.get('ARCHITECTURES') or '').split()
	if 'any' in archs:
		return 'any'
	if '!' + ARCH in archs:
		return 'broken'
	if ARCH in archs or 'all' in archs:
		return 'ok'
	if '?' + ARCH in archs or '?all' in archs:
		return 'untested'
	if '!all' in archs:
		return 'broken'
	listed = [a.lstrip('?') for a in archs if not a.startswith('!')]
	if listed and all(a == 'x86_gcc2' for a in listed):
		return 'broken'  # gcc2-only (BeOS-era) code
	return 'untested'


class Port:
	def __init__(self, name, version, recipe, category):
		self.name = name
		self.version = version
		self.recipe = recipe
		self.category = category
		self.keys = evaluate_recipe(recipe, name, version)
		self.revision = self.keys.get('REVISION', '1')

	@property
	def full_version(self):
		return '%s-%s' % (self.version, self.revision)

	def packages(self):
		"""[(suffix, package name)] — main package first, no debuginfo."""
		result = [('', self.keys.get('PACKAGE_NAME') or self.name)]
		for key in sorted(self.keys):
			m = re.match(r'^PROVIDES_(\w+)$', key)
			if m and m.group(1) != 'debuginfo':
				suffix = m.group(1)
				result.append((suffix, self.keys.get('PACKAGE_NAME_' + suffix)
					or '%s_%s' % (self.name, suffix)))
		return result

	def key(self, name, suffix=''):
		if suffix:
			return self.keys.get('%s_%s' % (name, suffix))
		return self.keys.get(name)

	def hpkg_name(self, package, suffix=''):
		arch = 'any' if arch_status(self.keys, '_' + suffix if suffix else '') == 'any' else ARCH
		version = self.key('PACKAGE_VERSION', suffix) or self.version
		return '%s-%s-%s-%s.hpkg' % (package, version, self.revision, arch)


# -- the provides index ------------------------------------------------------------


def recipe_index(refresh=False):
	"""{provided name: [(port, version, package suffix)]} over all recipes,
	cached per recipe mtime."""
	cache_file = CACHE / 'recipes.json'
	cache = {} if refresh else load_json(cache_file, {})
	recipes = all_recipe_files()
	jobs = []
	for name, versions in recipes.items():
		for version, path, category in versions:
			stamp = '%s:%d' % (path, path.stat().st_mtime_ns)
			snippet = overlay_snippet(path)
			if snippet:
				stamp += ':%d' % snippet.stat().st_mtime_ns
			if cache.get(str(path), {}).get('stamp') != stamp:
				jobs.append((name, version, path, stamp))

	def evaluate(job):
		name, version, path, stamp = job
		try:
			keys = evaluate_recipe(path, name, version)
			return str(path), {'stamp': stamp, 'keys': {k: v for k, v in keys.items()
				if k.startswith(('PROVIDES', 'ARCHITECTURES', 'PACKAGE_NAME', 'REVISION'))}}
		except BuildError as e:
			return str(path), {'stamp': stamp, 'error': str(e)}

	if jobs:
		say('evaluating %d recipes ...' % len(jobs))
		with ThreadPoolExecutor(JOBS) as pool:
			for path, entry in pool.map(evaluate, jobs):
				cache[path] = entry
		live = {str(p) for versions in recipes.values() for _, p, _ in versions}
		cache = {k: v for k, v in cache.items() if k in live}
		save_json(cache_file, cache)

	index = {}
	for name, versions in recipes.items():
		for version, path, category in versions:
			keys = cache.get(str(path), {}).get('keys')
			if not keys:
				continue
			for key, value in keys.items():
				m = re.match(r'^PROVIDES(?:_(\w+))?$', key)
				if not m or m.group(1) == 'debuginfo':
					continue
				for e in entries(value):
					index.setdefault(entry_name(e), []).append(
						(name, version, m.group(1) or ''))
	return recipes, index


def choose_provider(candidates, keys_of, built):
	"""Pick the port to build for a requirement: one already built, else one
	whose ARCHITECTURES supports arm64 over an untested one, then the newest
	version. Cross/bootstrap variants never qualify."""
	best = {}
	for name, version, suffix in candidates:
		if '_cross_' in name or name.endswith('_bootstrap'):
			continue
		cur = best.get(name)
		if cur is None or natural_key(version) > natural_key(cur[0]):
			best[name] = (version, suffix)
	if not best:
		return None
	order = {'ok': 0, 'any': 0, 'untested': 1, 'broken': 2}

	def rank(name):
		version, suffix = best[name]
		return (name not in built, order[arch_status(keys_of(name, version))], name)
	name = min(best, key=rank)
	return name, best[name][0], best[name][1]


# -- building -----------------------------------------------------------------------


class Builder:
	def __init__(self, args):
		self.args = args
		self.recipes, self.index = recipe_index()
		self.base_provides = load_json(BASE / 'provides.json', {})
		self.results = load_json(RESULTS, {})
		self.building = []
		self.failed = {}  # port -> error, within this run
		self.done = set()  # ports built in this run

	# -- lookup

	def find_port(self, spec):
		"""'name' or 'name-version' -> Port"""
		name, _, version = spec.partition('-')
		versions = self.recipes.get(name)
		if not versions:
			raise BuildError('no recipe for %s' % spec)
		if version:
			matches = [v for v in versions if v[0] == version]
			if not matches:
				raise BuildError('no recipe %s-%s' % (name, version))
			version, path, category = matches[0]
		else:
			ok = [v for v in versions if arch_status(self._cached_keys(v[1])) != 'broken']
			version, path, category = max(ok or versions, key=lambda v: natural_key(v[0]))
		return Port(name, version, path, category)

	def _record(self, name, result):
		"""Save a port's result. The file is re-read first: another prosepkg
		may be building at the same time, and its entries must survive."""
		self.results = load_json(RESULTS, {})
		self.results[name] = result
		save_json(RESULTS, self.results)

	def _cached_keys(self, path):
		if not hasattr(self, '_recipe_cache'):
			self._recipe_cache = load_json(CACHE / 'recipes.json', {})
		return self._recipe_cache.get(str(path), {}).get('keys', {})

	def provider_of(self, entry):
		"""(port name, version, suffix) providing an entry, or None if the
		base system provides it."""
		name = entry_name(entry)
		if name in self.base_provides or name in TOOLCHAIN_COMMANDS:
			return None
		candidates = self.index.get(name)
		if not candidates:
			raise BuildError('nothing provides %s' % name)
		built = {n for n, r in self.results.items() if r.get('status') == 'built'}
		return choose_provider(candidates, self._keys_of, built) or candidates[0]

	def _keys_of(self, name, version):
		for v, path, _ in self.recipes.get(name, []):
			if v == version:
				return self._cached_keys(path)
		return {}

	# -- the build

	def build(self, spec, force=False):
		port = self.find_port(spec)
		status = self.results.get(port.name, {})
		built = all((REPO / port.hpkg_name(pkg, sfx)).exists() for sfx, pkg in port.packages())
		if getattr(self.args, 'force_all', False) and port.name not in self.done:
			force = True
		if (built and status.get('version') == port.full_version and not force) \
				or port.name in self.done:
			return port
		if port.name in self.failed:
			raise BuildError('%s failed earlier in this run' % port.name)
		if port.name in self.building:
			raise BuildError('dependency cycle: %s' % ' -> '.join(self.building + [port.name]))
		self.building.append(port.name)
		try:
			self._build(port)
			self.done.add(port.name)
		except BuildError as e:
			self.failed[port.name] = str(e)
			raise
		finally:
			self.building.pop()
		return port

	def _build(self, port):
		say('building %s-%s (%s)' % (port.name, port.full_version, port.recipe))
		archs = arch_status(port.keys)
		if archs == 'broken' and not self.args.force_arch:
			raise BuildError('%s is marked broken for %s (ARCHITECTURES)' % (port.name, ARCH))
		work = WORK / port.name
		rmtree(work)
		work.mkdir(parents=True)
		LOGS.mkdir(exist_ok=True)
		log_path = LOGS / ('%s.log' % port.name)
		started = time.time()
		result = {'version': port.full_version, 'recipe': str(port.recipe),
			'arch_status': archs, 'log': str(log_path)}
		with open(log_path, 'w') as log:
			try:
				self._prepare_sysroot(port, work, log)
				sources = self._fetch_and_unpack(port, work, log)
				self._build_host_tools(port, work, sources, log)
				self._run_phase(port, work, sources, 'PATCH', log)
				self._run_phase(port, work, sources, 'BUILD', log)
				self._run_phase(port, work, sources, 'INSTALL', log)
				hpkgs = self._package(port, work, log)
			except BuildError as e:
				log.write('\nFAILED: %s\n' % e)
				result.update(status='failed', error=str(e),
					seconds=round(time.time() - started))
				self._record(port.name, result)
				raise BuildError('%s failed: %s (log: %s)' % (port.name, e, log_path))
		result.update(status='built', packages=hpkgs, seconds=round(time.time() - started))
		self._record(port.name, result)
		if not self.args.keep_work:
			rmtree(work)
		say('built %s: %s' % (port.name, ', '.join(hpkgs)))

	def _prepare_sysroot(self, port, work, log):
		"""Clone the base sysroot, then activate the build requirements."""
		sysroot = work / 'sysroot'
		clone_tree(BASE_SYSROOT, sysroot)
		prerequires = entries(port.keys.get('BUILD_PREREQUIRES'))
		missing = [e for e in prerequires if entry_name(e).startswith('cmd:')
			and not self._host_command(entry_name(e)[4:])]
		if missing:
			log.write('note: host commands not found: %s\n' % ', '.join(missing))
		todo = list(entries(port.keys.get('BUILD_REQUIRES')))
		# prerequisites are build-time tools, never linked against: a Perl
		# module (xml_parser for libdom) or a generator the host has anyway.
		# One that cannot be built for the target is noted, not fatal.
		tools = [e for e in prerequires if not entry_name(e).startswith('cmd:')]
		todo += tools
		tool_names = {entry_name(e) for e in tools}
		activated = set()
		while todo:
			entry = todo.pop(0)
			provider = self.provider_of(entry)
			if provider is None:
				continue
			pname, pversion, psuffix = provider
			try:
				dep = self.build('%s-%s' % (pname, pversion))
			except BuildError as e:
				if entry_name(entry) not in tool_names:
					raise
				log.write('note: prerequisite %s not built for the target (%s); '
					'the host may provide it\n' % (entry, str(e)[:120]))
				continue
			package = dict(dep.packages()).get(psuffix)
			if package is None:
				raise BuildError('%s: %s provides %s via an unbuilt package'
					% (port.name, pname, entry))
			hpkg = REPO / dep.hpkg_name(package, psuffix)
			if hpkg.name in activated:
				continue
			activated.add(hpkg.name)
			log.write('activating %s for %s\n' % (hpkg.name, entry))
			run([host_tool('package'), 'extract', '-C', sysroot / 'boot' / 'system', hpkg],
				log=log)
			(sysroot / 'boot' / 'system' / '.PackageInfo').unlink(missing_ok=True)
			# runtime requirements of what we just activated
			todo += entries(dep.key('REQUIRES', psuffix))
		self._link_package_dir(port, sysroot)

	def _link_package_dir(self, port, sysroot):
		"""/packages/<name>-<version>-<revision>/ in the sysroot, as packagefs
		makes it on Haiku for an activated package: .self and .settings, and a
		link per requirement named as packagefs names them (ca_root_certificates,
		lib~libz, cmd~perl, devel~libcurl). Every activated package is merged
		into the sysroot's boot/system, so all of them point there."""
		links = sysroot / 'packages' / ('%s-%s-%s' % (port.name, port.version, port.revision))
		rmtree(links)
		links.mkdir(parents=True)
		os.symlink('../../boot/system', links / '.self')
		os.symlink('../../boot/system/settings', links / '.settings')
		for key in ('BUILD_REQUIRES', 'REQUIRES', 'BUILD_PREREQUIRES'):
			for e in entries(port.keys.get(key)):
				name = entry_name(e).replace(':', '~')
				if name and not (links / name).exists():
					os.symlink('../../boot/system', links / name)

	def _host_command(self, name):
		path = ':'.join([str(ENV_DIR / 'bin'), str(HOSTTOOLS / 'bin')] + HOST_PATH)
		return shutil.which(name.replace('_', '-'), path=path) or shutil.which(name, path=path)

	def _build_host_tools(self, port, work, sources, log):
		"""Tools a port runs while building, built for this Mac: an overlay
		names ports in PROSE_HOST_TOOLS (NetSurf: nsgenbind, its JavaScript
		binding generator, and the NetSurf build system it needs). Their
		sources are fetched like any port's, then the overlay's HOST_BUILD()
		runs in a host environment -- cc is the Mac's compiler, no cross
		wrappers -- with $hostSourceDir_<name> for each and $hostPrefix to
		install into; $hostPrefix/bin is on the PATH of the phases after."""
		names = port.keys.get('PROSE_HOST_TOOLS', '').split()
		if not names:
			return
		if not port.keys.get('PHASE_HOST_BUILD'):
			raise BuildError('%s names PROSE_HOST_TOOLS but has no HOST_BUILD()' % port.name)
		extra = {}
		for name in names:
			host_port = self.find_port(name)
			base = work / 'host' / name
			base.mkdir(parents=True, exist_ok=True)
			log.write('host tool %s: sources of %s-%s\n' % (name, host_port.name, host_port.version))
			host_sources = self._fetch_and_unpack(host_port, base, log)
			extra['hostSourceDir_' + re.sub(r'\W', '_', name)] = str(host_sources['1'])
		self._run_phase(port, work, sources, 'HOST_BUILD', log, host_extra=extra)

	def _fetch_and_unpack(self, port, work, log):
		"""Download, verify, unpack and patch every source; {index: dir}."""
		sources = {}
		indices = sorted({m.group(1) or '1' for k in port.keys
			for m in [re.match(r'^SOURCE_URI(?:_(\d+))?$', k)] if m}, key=int)
		for index in indices:
			sfx = '' if index == '1' else '_' + index
			uris = port.keys.get('SOURCE_URI' + sfx, '').split()
			if not uris:
				continue
			checksum = port.keys.get('CHECKSUM_SHA256' + sfx, '').strip()
			filename = port.keys.get('SOURCE_FILENAME' + sfx, '').strip()
			# SOURCE_DIR defaults to <name>-<version> (set before parsing);
			# additional sources unpack at their top level unless told otherwise
			source_dir = port.keys.get('SOURCE_DIR' + sfx, '')
			base = work / ('sources' if index == '1' else 'sources-' + index)
			base.mkdir()
			self._unpack(uris, checksum, filename, source_dir, base, log, port.recipe.parent)
			# the whole relative path, not just its first component: haikuporter's
			# sourceSubDir may be nested (timgmsoundfont: common/data/synth).
			# Unpacking still filters on the top-level member above.
			sdir = base / source_dir if source_dir else base
			if not sdir.is_dir():
				raise BuildError('source dir %s missing after unpacking (SOURCE_DIR?)'
					% source_dir)
			sources[index] = sdir
			patches = port.keys.get('PATCHES' + sfx, '').split()
			if patches:
				self._apply_patches(port, sdir, patches, log)
			if index == '1':
				self._apply_overlay_patches(port, sdir, log)
		return sources

	def _apply_overlay_patches(self, port, sdir, log):
		"""overlay/<category>/<port>/<recipe-stem>*.patch, after the recipe's
		PATCHES: our own source fixes, kept as reviewable diffs."""
		directory = OVERLAY_DIR / port.recipe.parent.parent.name / port.recipe.parent.name
		for patch in sorted(directory.glob(port.recipe.stem + '*.patch')):
			log.write('applying overlay patch %s\n' % patch.name)
			run(['git', 'apply', '--verbose', '-p1', patch], cwd=sdir, log=log)

	def _unpack(self, uris, checksum, filename, source_dir, base, log, recipe_dir):
		uri = uris[0]
		if uri.startswith('git+') or uri.startswith('git://'):
			url, _, rev = uri[4 if uri.startswith('git+') else 0:].partition('#')
			target = base / (source_dir.split('/')[0] or 'git')
			run(['git', 'clone', '--quiet', url, target], log=log)
			if rev:
				run(['git', '-C', target, 'checkout', '--quiet', rev], log=log)
			return
		noarchive = '#noarchive' in uri
		urls = [u.split('#')[0] for u in uris]
		name = filename or urls[0].rstrip('/').rsplit('/', 1)[-1]
		cached = DOWNLOADS / name
		# A file:// source is a file in the recipe's own directory, so there is
		# nothing to verify it against: haikuporter's SourceFetcherForLocalFile
		# sets sourceShouldBeValidated = False and never checksums one (several
		# haiku-data recipes carry a CHECKSUM_SHA256 that does not match their
		# own file). Copy it afresh every time, as haikuporter symlinks it.
		local_source = urls[0].startswith('file://')
		if local_source or not cached.exists() or (checksum and sha256(cached) != checksum):
			# curl, not urllib: urllib's macOS proxy lookup loads Network.framework
			# into this process, whose atfork handler then crashes forked
			# children (SIGSEGV before exec) on macOS 27
			tmp = cached.with_name(cached.name + '.part')
			for url in urls:
				if url.startswith('file://'):
					# as haikuporter: a file:// URI names a file kept beside the
					# recipe (haiku-data ports ship their data that way)
					local = recipe_dir / url[len('file://'):]
					log.write('copying %s\n' % local)
					if not local.is_file():
						log.write('  not there\n')
						continue
					shutil.copy2(local, tmp)
					os.replace(tmp, cached)
					break
				log.write('downloading %s\n' % url)
				log.flush()
				proc = subprocess.run(['curl', '-fsSL', '--retry', '3', '--connect-timeout', '30',
					'-A', 'prosepkg', '-o', str(tmp), url], stdout=log, stderr=log)
				if proc.returncode == 0:
					os.replace(tmp, cached)
					break
				log.write('  failed (curl exit %d)\n' % proc.returncode)
			else:
				tmp.unlink(missing_ok=True)
				raise BuildError('cannot download %s' % name)
		if checksum and not local_source and sha256(cached) != checksum:
			raise BuildError('checksum mismatch for %s' % name)
		if not checksum and not local_source:
			log.write('warning: no CHECKSUM_SHA256 for %s\n' % name)
		if noarchive:
			# as haikuporter: the file goes into the source dir itself
			target = base / source_dir.split('/')[0] if source_dir else base
			target.mkdir(parents=True, exist_ok=True)
			shutil.copy2(cached, target / name)
			return
		sub = source_dir.split('/')[0] if source_dir else None
		if tarfile.is_tarfile(cached):
			with tarfile.open(cached) as t:
				members = [m for m in t.getmembers()
					if not sub or m.name == sub or m.name.startswith(sub + '/')
					or m.name.startswith('./' + sub + '/')]
				t.extractall(base, members=members, filter='tar')
		elif zipfile.is_zipfile(cached):
			with zipfile.ZipFile(cached) as z:
				z.extractall(base)
				for info in z.infolist():  # zipfile drops the x bits
					mode = info.external_attr >> 16
					if mode:
						os.chmod(base / info.filename, mode & 0o777)
		else:
			raise BuildError('unknown archive format: %s' % name)

	def _apply_patches(self, port, sdir, patches, log):
		env = dict(os.environ, GIT_COMMITTER_NAME='prosepkg',
			GIT_COMMITTER_EMAIL='packages@prose.local', GIT_AUTHOR_NAME='prosepkg',
			GIT_AUTHOR_EMAIL='packages@prose.local')
		implicit = not (sdir / '.git').exists()
		if implicit:
			run(['git', 'init', '-q'], cwd=sdir, log=log)
			run(['git', 'add', '-A', '-f', '.'], cwd=sdir, log=log)
			run(['git', 'commit', '-q', '--no-verify', '-m', 'import'], cwd=sdir, env=env, log=log)
		for patch in patches:
			path = port.recipe.parent / 'patches' / patch
			if not path.exists():
				raise BuildError('patch %s not found' % path)
			if patch.endswith('.patchset'):
				run(['git', 'am', '--ignore-whitespace', '-3', '--keep-cr', path],
					cwd=sdir, env=env, log=log)
			else:
				run(['git', 'apply', '--ignore-whitespace', '-p1', '--index', path],
					cwd=sdir, log=log)
				run(['git', 'commit', '-q', '--no-verify', '-m', 'patch ' + patch],
					cwd=sdir, env=env, log=log)
		if implicit:
			# the repository only served to apply the patches; builds that find
			# one version themselves from it (flac: "git-709e212 <date>")
			rmtree(sdir / '.git')

	def _run_phase(self, port, work, sources, phase, log, host_extra=None):
		if not port.keys.get('PHASE_' + phase):
			return
		install = phase == 'INSTALL'
		host = host_extra is not None
		destdir = work / 'destdir'
		prefix = str(destdir) + '/boot/system' if install else '/boot/system'
		extra = {'proseInInstall': '1' if install else '0',
			'proseSubpackagesDir': str(work / 'sub'),
			# packagefs's links directory for the port, as on Haiku: recipes bake
			# it into binaries (curl's CA bundle) and scripts; the sysroot has the
			# same directory, so compile-time lookups through the wrappers work
			'portPackageLinksDir': '/packages/%s-%s-%s' % (port.name, port.version, port.revision),
			'workDir': str(work), 'hostPrefix': str(work / 'host' / 'prefix')}
		if host:
			extra.update(host_extra)
		for index, sdir in sources.items():
			extra['sourceDir' if index == '1' else 'sourceDir' + index] = str(sdir)
		variables = shell_variables(port.name, port.version, port.revision, port.recipe,
			prefix, extra)
		script = '#!%s\nset -e\n' % BASH
		script += shell_setters(variables)
		script += 'declare -a PROSE_DEBUG_INFO_PATHS=()\n'
		script += '. %s\n' % sh_quote(RUNTIME_SH)
		script += 'PATCH() { true; }\nBUILD() { true; }\nINSTALL() { true; }\nTEST() { true; }\n'
		cwd = work / 'host' if host else sources.get('1', work)
		script += 'cd %s\n' % sh_quote(cwd)
		script += '. %s >/dev/null\n' % sh_quote(port.recipe)
		snippet = overlay_snippet(port.recipe)
		if snippet:
			script += '. %s >/dev/null\n' % sh_quote(snippet)
		script += '%s\n' % phase
		if install:
			script += 'proseStripDebugInfos\n'
		path = work / ('phase-%s.sh' % phase)
		write_file(path, script, 0o755)
		if host:
			# the Mac's own tools and compiler: no cross wrappers on the PATH
			(work / 'tmp').mkdir(exist_ok=True)
			env = {'PATH': ':'.join([str(HOSTTOOLS / 'bin')] + HOST_PATH),
				'HOME': os.environ.get('HOME', '/tmp'), 'LANG': 'en_US.UTF-8',
				'TMPDIR': str(work / 'tmp')}
		else:
			env = build_env(work / 'sysroot', work)
		if install:
			env['DESTDIR'] = str(destdir)
			env['PROSE_STAGING_ROOT'] = str(destdir)
		log.write('\n==== %s ====\n' % phase)
		log.flush()
		proc = subprocess.run([BASH, path], cwd=cwd, env=env,
			stdout=log, stderr=subprocess.STDOUT)
		if proc.returncode != 0:
			raise BuildError('%s phase failed (exit %d)' % (phase, proc.returncode))
		if install:
			self._normalize_destdir(destdir, log)
			roots = [destdir] + sorted((work / 'sub').glob('*'))
			self._scrub_host_paths(roots, log)

	def _normalize_destdir(self, destdir, log):
		"""`make install PREFIX=$prefix` with DESTDIR also exported lands in
		<destdir>/<destdir>/...: fold that back."""
		doubled = destdir / str(destdir).lstrip('/')
		if doubled.exists():
			log.write('note: folding doubled DESTDIR prefix back\n')
			run(['/bin/cp', '-R', str(doubled) + '/.', destdir], log=log)
			rmtree(destdir / str(destdir).lstrip('/').split('/')[0])

	# host paths that mean a Haiku path in installed text files
	HOST_TOOL_PATHS = [
		('/opt/homebrew/opt/coreutils/libexec/gnubin/', '/bin/'),
		('/opt/homebrew/opt/gnu-sed/libexec/gnubin/', '/bin/'),
		('/opt/homebrew/bin/', '/bin/'),
	]

	def _scrub_host_paths(self, roots, log):
		"""Recipes that write files in INSTALL (.pc files from a heredoc over
		$prefix, ...) see the staging paths, and configure's flags carry the
		sysroot: map every staging root and the sysroot back to /boot/system,
		and host tool paths to Haiku's. Text files and symlink targets only;
		binaries that embed a staging path are reported."""
		# every staging root in every tree: packageEntries moves files that
		# INSTALL wrote into destdir (with destdir paths) to sub/<suffix>
		roots = [r for r in roots if r.is_dir()]
		pairs = []
		for root in roots:
			pairs += [((str(root) + '/boot/system').encode(), b'/boot/system'),
				(str(root).encode() + b'/', b'/')]
		pairs += [(a.encode(), b.encode()) for a, b in self.HOST_TOOL_PATHS]
		# the build sysroot too: configure's -L/-I flags end up in *.pc files
		# and *-config scripts (libcurl.pc, curl-config, fluidlite.pc)
		sysroot = str(roots[0].parent / 'sysroot') if roots else None
		if sysroot and os.path.isdir(sysroot):
			pairs += [((sysroot + '/boot/system').encode(), b'/boot/system'),
				(sysroot.encode() + b'/', b'/')]
		prefixes = tuple(str(root) + '/' for root in roots)
		for root in roots:
			for path in sorted(root.rglob('*')):
				if path.is_symlink():
					target = os.readlink(path)
					if target.startswith(prefixes):
						for prefix in prefixes:
							if target.startswith(prefix):
								fixed = target[len(prefix) - 1:]
								break
						path.unlink()
						os.symlink(fixed, path)
						log.write('scrubbed symlink %s -> %s\n' % (path, fixed))
					continue
				if not path.is_file():
					continue
				data = path.read_bytes()
				if b'\0' in data[:8192]:
					if any(p.encode() in data for p in prefixes):
						log.write('warning: binary embeds a staging path: %s\n' % path)
					continue
				fixed = data
				for a, b in pairs:
					fixed = fixed.replace(a, b)
				if fixed != data:
					mode = path.stat().st_mode
					path.write_bytes(fixed)
					os.chmod(path, mode)
					log.write('scrubbed host paths in %s\n' % path)

	def _package(self, port, work, log):
		hpkgs = []
		REPO.mkdir(exist_ok=True)
		for suffix, package in port.packages():
			root = (work / 'destdir' if not suffix else work / 'sub' / suffix) / 'boot' / 'system'
			root.mkdir(parents=True, exist_ok=True)
			if not any(root.iterdir()):
				log.write('warning: package %s is empty\n' % package)
			licenses = port.recipe.parent / 'licenses'
			if licenses.is_dir():
				shutil.copytree(licenses, root / 'data' / 'licenses', dirs_exist_ok=True)
			write_file(root / '.PackageInfo', self._package_info(port, suffix, package))
			# attributes (types, app signatures, icons) from the resources
			mimedb = root / 'data' / 'mime_db'
			mimedb.mkdir(parents=True, exist_ok=True)
			run([host_tool('mimeset'), '--all', '--mimedb', 'data/mime_db',
				'--mimedb', BASE / 'mime_db', '.'], cwd=root, log=log)
			if not any(mimedb.iterdir()):
				mimedb.rmdir()
				if not any((root / 'data').iterdir()):
					(root / 'data').rmdir()
			else:
				# mimeset records the app's build-host path as its "preferred
				# path"; the registrar finds apps by signature without it
				for entry in mimedb.rglob('*'):
					subprocess.run(['/usr/bin/xattr', '-d', 'user.haiku.META:PPATH',
						str(entry)], capture_output=True)
			out = REPO / port.hpkg_name(package, suffix)
			out.unlink(missing_ok=True)
			for older in REPO.glob(package + '-*.hpkg'):
				if older.name.split('-')[0] == package:
					log.write('replacing %s\n' % older.name)
					older.unlink()
			run([host_tool('package'), 'create', out], cwd=root, log=log)
			hpkgs.append(out.name)
		return hpkgs

	def _package_info(self, port, suffix, package):
		k = lambda name: port.key(name, suffix) if suffix else port.key(name)
		summary = k('SUMMARY') or port.key('SUMMARY') or package
		description = k('DESCRIPTION') or port.key('DESCRIPTION') or summary
		version = (k('PACKAGE_VERSION') or port.version) + '-' + port.revision
		arch = 'any' if arch_status(port.keys, '_' + suffix if suffix else '') == 'any' else ARCH
		esc = lambda s: s.replace('\\', '\\\\').replace('"', '\\"')
		out = ['name\t\t\t%s' % package, 'version\t\t\t%s' % version,
			'architecture\t\t%s' % arch,
			'summary\t\t\t"%s"' % esc(summary.strip()),
			'description\t\t"%s"' % esc(description.strip()),
			'packager\t\t"%s"' % PACKAGER, 'vendor\t\t\t"%s"' % VENDOR]

		def block(keyword, items, quote=False):
			if items:
				out.append(keyword + ' {')
				for item in items:
					out.append('\t"%s"' % esc(item) if quote else '\t' + item)
				out.append('}')

		block('licenses', entries(port.key('LICENSE')), quote=True)
		block('copyrights', entries(port.key('COPYRIGHT')), quote=True)
		block('provides', entries(k('PROVIDES')))
		block('requires', entries(k('REQUIRES')))
		for key, keyword in (('SUPPLEMENTS', 'supplements'), ('CONFLICTS', 'conflicts'),
				('FRESHENS', 'freshens'), ('REPLACES', 'replaces')):
			block(keyword, entries(k(key)))
		block('urls', entries(port.key('HOMEPAGE')), quote=True)
		for key, keyword in (('GLOBAL_WRITABLE_FILES', 'global-writable-files'),
				('USER_SETTINGS_FILES', 'user-settings-files'),
				('POST_INSTALL_SCRIPTS', 'post-install-scripts'),
				('PRE_UNINSTALL_SCRIPTS', 'pre-uninstall-scripts')):
			block(keyword, [quote_paths(e) for e in entries(k(key))])
		block('users', entries(k('PACKAGE_USERS')))
		block('groups', entries(k('PACKAGE_GROUPS')))
		urls = [u.split('#')[0] for i in sorted(port.keys) if re.match(r'^SOURCE_URI(_\d+)?$', i)
			for u in port.keys[i].split()[:1] if not u.startswith('file://')]
		block('source-urls', urls, quote=True)
		return '\n'.join(out) + '\n'


def quote_paths(item):
	parts = re.findall(r'"[^"]*"|\S+', item)
	return ' '.join('"%s"' % p if not p.startswith('"') and '/' in p else p for p in parts)


def build_env(sysroot, work):
	"""A clean environment: the build must not see the host's CC, CFLAGS, ..."""
	tmp = Path(work) / 'tmp'
	tmp.mkdir(parents=True, exist_ok=True)
	mimedb = Path(work) / 'mime_db'
	mimedb.mkdir(parents=True, exist_ok=True)
	env = {
		# "." last: Haiku's default PATH has it, and recipes run "configure"
		# a port's own host tools (PROSE_HOST_TOOLS) first, then the wrappers
		'PATH': ':'.join(([str(Path(work) / 'host' / 'prefix' / 'bin')]
				if (Path(work) / 'host' / 'prefix' / 'bin').is_dir() else [])
			+ [str(ENV_DIR / 'bin'), str(HOSTTOOLS / 'bin')] + HOST_PATH + ['.']),
		'HOME': os.environ.get('HOME', '/tmp'),
		'USER': os.environ.get('USER', 'prose'),
		'TMPDIR': str(tmp),
		'LANG': 'en_US.UTF-8',
		'PROSE_SYSROOT': str(sysroot),
		'PROSE_MIMEDB_WORK': str(mimedb),
		'PROSE_MIMEDB_SYSTEM': str(BASE / 'mime_db'),
		'PROSE_CMAKE_TOOLCHAIN': str(ENV_DIR / 'cmake-toolchain.cmake'),
		'PROSE_MESON_CROSS': str(Path(work) / 'meson-cross.ini'),
		'CONFIG_SITE': str(ENV_DIR / 'config.site'),
		# the host's aclocal looks only in its own dirs; macro packages a recipe
		# requires (autoconf_archive) are in the sysroot, where Haiku puts them
		'ACLOCAL_PATH': str(sysroot / 'boot' / 'system' / 'data' / 'aclocal'),
		'CC_FOR_BUILD': '/usr/bin/clang', 'CXX_FOR_BUILD': '/usr/bin/clang++',
		'BUILD_CC': '/usr/bin/clang', 'HOSTCC': '/usr/bin/clang',
	}
	bindir = ENV_DIR / 'bin'
	write_file(Path(work) / 'meson-cross.ini', '''[binaries]
c = '%(b)s/%(t)s-gcc'
cpp = '%(b)s/%(t)s-g++'
ar = '%(b)s/%(t)s-ar'
strip = '%(b)s/%(t)s-strip'
pkg-config = '%(b)s/pkg-config'

[properties]
sys_root = '%(s)s'

[host_machine]
system = 'haiku'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
''' % {'b': bindir, 't': TRIPLE, 's': sysroot})
	return env


# -- commands -----------------------------------------------------------------------


def cmd_build(args):
	builder = Builder(args)
	failed = []
	for spec in args.ports:
		try:
			builder.build(spec, force=args.force)
		except BuildError as e:
			say('ERROR:', e)
			failed.append(spec)
			if not args.keep_going:
				break
	write_report(builder.results)
	if failed:
		say('failed:', ' '.join(failed))
		sys.exit(1)


def cmd_info(args):
	builder = Builder(args)
	for spec in args.ports:
		port = builder.find_port(spec)
		print('%s-%s  %s  [%s]' % (port.name, port.full_version, port.recipe,
			arch_status(port.keys)))
		for suffix, package in port.packages():
			print('  package', package, '->', port.hpkg_name(package, suffix))
		for e in entries(port.keys.get('BUILD_REQUIRES')):
			try:
				p = builder.provider_of(e)
			except BuildError as err:
				p = str(err)
			print('  build requires %-40s %s' % (e, 'base' if p is None else p))


def repo_packages():
	"""{package name: (hpkg path, package info)} for the built packages."""
	result = {}
	for hpkg in sorted(REPO.glob('*.hpkg')):
		info = package_info(hpkg)
		result[info['name'][0]] = (hpkg, info)
	return result


def install_closure(specs, image_names):
	"""Package files to install for the given package or port names: them,
	plus everything they require that the image does not provide yet -- from
	the built packages, or system packages the image lacks (e.g. grep)."""
	packages = repo_packages()
	provides = {}
	for name, (hpkg, info) in packages.items():
		for p in info.get('provides', []) + [name]:
			provides.setdefault(entry_name(p), name)
	system = {}
	for hpkg in sorted((BASE / 'packages').glob('*.hpkg')):
		info = package_info(hpkg)
		system[info['name'][0]] = (hpkg, info)
	# what the image provides: its packages, system or built by us (an image
	# built with our codecs already has lib:libpng16, for example)
	in_image = set(image_names)
	for name, (hpkg, info) in list(system.items()) + list(packages.items()):
		if name in image_names:
			in_image.update(entry_name(p) for p in info.get('provides', []))
	system_provides = {}
	for name, (hpkg, info) in system.items():
		for p in info.get('provides', []) + [name]:
			system_provides.setdefault(entry_name(p), name)
	results = load_json(RESULTS, {})
	todo = []
	files = []
	expanded = []
	for spec in specs:
		if spec.startswith('@'):
			# a package set: packages/sets/<name>, one port or package per line
			expanded += entries((BUILDER_DIR.parent / 'sets' / spec[1:]).read_text())
		else:
			expanded.append(spec)
	for spec in expanded:
		if spec.endswith('.hpkg') and os.path.isfile(spec):
			# a package file (haiku_devel of the image's own build, say):
			# installed as it is, its requirements left to the image
			files.append(Path(spec).resolve())
		elif spec in packages or spec in system:
			# a built package, or a system package the image may lack (grep)
			todo.append(spec)
		elif results.get(spec, {}).get('status') == 'built':
			# a port: its main package
			main = results[spec]['packages'][0]
			todo.append(package_info(REPO / main)['name'][0])
		else:
			raise BuildError('%s is neither a built package nor a built port' % spec)
	closure = []
	while todo:
		name = todo.pop(0)
		if name in closure:
			continue
		closure.append(name)
		info = packages[name][1] if name in packages else system[name][1]
		for req in info.get('requires', []):
			n = entry_name(req)
			if n in in_image:
				continue
			if n in provides:
				todo.append(provides[n])
			elif n in system_provides:
				todo.append(system_provides[n])
			else:
				raise BuildError('%s requires %s, which nothing built provides' % (name, n))
	return files + [packages[n][0] if n in packages else system[n][0] for n in closure]


def hpkg_stem(file_name):
	"""The package name of an .hpkg file name: name-version-revision-arch.hpkg,
	or name.hpkg as the Haiku build names its own."""
	return file_name.split('-')[0].removesuffix('.hpkg')


def bfs_partition(image):
	"""Byte range of the BFS partition (MBR type 0xEB) of a Haiku image."""
	with open(image, 'rb') as f:
		mbr = f.read(512)
	for i in range(4):
		entry = mbr[446 + 16 * i:462 + 16 * i]
		if entry[4] == 0xEB:
			lba = int.from_bytes(entry[8:12], 'little')
			count = int.from_bytes(entry[12:16], 'little')
			return lba * 512, (lba + count) * 512
	raise BuildError('%s has no BFS partition' % image)


def bfs_shell(image, commands, check=True):
	start, end = bfs_partition(image)
	proc = subprocess.run([host_tool('bfs_shell'), '--start-offset', str(start),
		'--end-offset', str(end), str(image)], input='\n'.join(commands + ['quit']) + '\n',
		capture_output=True, text=True)
	if check and proc.returncode != 0:
		raise BuildError('bfs_shell failed on %s: %s' % (image, proc.stdout[-400:] + proc.stderr[-400:]))
	return proc.stdout


def bfs_listing(image, directory):
	"""{file name: size} of a directory in the image."""
	out = bfs_shell(image, ['ls ' + directory])
	files = {}
	for line in out.splitlines():
		parts = line.replace('fssh:/> ', '').split()
		# -rw-r--r--  0  0    34433 2026-09-18 19:55:46 name
		if len(parts) >= 7 and parts[0].startswith('-'):
			files[' '.join(parts[6:])] = int(parts[3])
	return files


def cmd_install(args):
	"""Copy packages (and their requirements) into a Haiku image's
	system/packages; packagefs activates them at the next boot. Other
	versions of the same packages are removed first."""
	image = Path(args.image)
	packages_dir = '/myfs/system/packages'
	activation = packages_dir + '/administrative/activated-packages'
	existing = bfs_listing(image, packages_dir)
	hpkgs = install_closure(args.packages,
		{hpkg_stem(f) for f in existing if f.endswith('.hpkg')})
	ours = {hpkg_stem(h.name) for h in hpkgs}
	stale = sorted(f for f in existing if f.endswith('.hpkg') and hpkg_stem(f) in ours)
	if stale:
		# never cp over an existing file: fs_shell leaks a reference then
		# and cannot unmount cleanly
		bfs_shell(image, ['rm %s/%s' % (packages_dir, f) for f in stale] + ['sync'])
	has_activation = 'activated-packages' in bfs_listing(image, packages_dir + '/administrative') \
		if 'administrative' in bfs_shell(image, ['ls ' + packages_dir]) else False
	commands = ['cp :%s %s/%s' % (h, packages_dir, h.name) for h in hpkgs]
	if has_activation:
		# only listed packages activate: replace stale names, append ours
		current = bfs_shell(image, ['cat ' + activation])
		names = [l.strip() for l in current.splitlines()
			if l.strip().endswith('.hpkg') and l.strip() not in stale]
		names += [h.name for h in hpkgs if h.name not in names]
		tmp = CACHE / 'activated-packages'
		write_file(tmp, '\n'.join(names) + '\n')
		bfs_shell(image, ['rm ' + activation, 'sync'])
		commands.append('cp :%s %s' % (tmp, activation))
	bfs_shell(image, commands + ['sync'])
	installed = bfs_listing(image, packages_dir)
	wrong = [h.name for h in hpkgs if installed.get(h.name) != h.stat().st_size]
	if wrong:
		raise BuildError('not in the image intact after copying: %s' % ', '.join(wrong))
	say('installed into %s (%s)%s:' % (image, 'verified', ', activation file updated'
		if has_activation else ''))
	for h in hpkgs:
		print('  %s%s' % (h.name, '  (replaced an older version)' if hpkg_stem(h.name) in
			{hpkg_stem(s) for s in stale} and h.name not in stale else ''))


def cmd_audit(args):
	"""Look inside built packages for things that cannot work on Haiku:
	build-host paths in text files or symlinks, cross tool names in
	installed scripts. Binaries are skipped (debug info names source files)."""
	import tempfile
	bad = 0
	hpkgs = [REPO / h for h in args.packages] if args.packages else sorted(REPO.glob('*.hpkg'))
	needles = [b'/Volumes/', b'/opt/homebrew', b'/private/tmp', (TRIPLE + '-').encode()]
	for hpkg in hpkgs:
		with tempfile.TemporaryDirectory(dir=CACHE) as tmp:
			run([host_tool('package'), 'extract', '-C', tmp, hpkg], capture=True)
			problems = []
			for path in sorted(Path(tmp).rglob('*')):
				rel = path.relative_to(tmp)
				if path.is_symlink():
					if os.readlink(path).startswith(('/Volumes/', '/private/', '/opt/')):
						problems.append('%s -> %s' % (rel, os.readlink(path)))
				elif path.is_file() and str(rel) != '.PackageInfo':
					data = path.read_bytes()
					if b'\0' in data[:8192]:
						continue
					for needle in needles:
						if needle in data:
							i = data.index(needle)
							line = data[max(0, i - 40):i + 60]
							problems.append('%s: ...%s...' % (rel,
								line.decode(errors='replace').replace('\n', ' ').strip()))
							break
			if problems:
				bad += 1
				print(hpkg.name)
				for p in problems:
					print('   ' + p)
	say('%d of %d packages have problems' % (bad, len(hpkgs)))
	if bad:
		sys.exit(1)


# -- the fork's own packages in a Haiku tree ------------------------------------------


def repository_groups(text):
	"""The ':'-separated groups of a build/jam/repositories/HaikuPorts/<arch>
	file: name, architecture, URL, "any" packages, architecture packages,
	source packages, debug info packages."""
	tokens = []
	for line in text.splitlines():
		tokens += line.split('#', 1)[0].split()
	groups, current = [], []
	for token in tokens:
		if token in (':', ';'):
			groups.append(current)
			current = []
			if token == ';':
				break
		else:
			current.append(token)
	return groups


def listed_packages(tree, arch, ref=None):
	"""{package file name: entry} for what a Haiku tree's HaikuPorts list
	names: the file in the working tree, or the one at ref."""
	path = 'build/jam/repositories/HaikuPorts/' + arch
	if ref:
		text = run(['git', 'show', '%s:%s' % (ref, path)], cwd=tree, capture=True).stdout
	else:
		text = (tree / path).read_text()
	groups = repository_groups(text)
	if len(groups) < 5:
		raise BuildError('%s: not a package list this can read' % (tree / path))
	files = {'%s-any.hpkg' % e: e for e in groups[3]}
	files.update({'%s-%s.hpkg' % (e, arch): e for e in groups[4]})
	return files


def upstream_base(tree):
	"""The upstream commit a Haiku tree is based on, whether the fork's
	patches are applied on top or committed."""
	for ref in ('origin/master', 'gerrit/master'):
		proc = run(['git', 'merge-base', 'HEAD', ref], cwd=tree, capture=True, check=False)
		if proc.returncode == 0 and proc.stdout.strip():
			return proc.stdout.strip()
	raise BuildError('%s: no origin/master or gerrit/master to compare its package list with'
		% tree)


LISTING_ENTRY = re.compile(r'(.+?)\s+(\d+)\s+\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\s+(\S+)\s*(.*)$')


def package_listing(hpkg):
	"""`package list -a`, parsed: ({attribute: [values]}, {entry path: [size,
	mode, link target, attributes...]}). Keyed by path and without
	timestamps, so a package extracted and created again compares equal."""
	out = run([host_tool('package'), 'list', '-a', hpkg], capture=True).stdout
	attributes, contents, path, current, key = {}, {}, [], None, None
	for line in out.splitlines():
		text = line.strip()
		m = LISTING_ENTRY.match(text)
		if current is None and not (m and not line.startswith('\t')):
			# the package attributes, before the first entry: one per line,
			# indented by a tab; a value with newlines (a description) goes on
			# in lines without the tab
			if line.startswith('\t'):
				key, _, value = text.partition(':')
				attributes.setdefault(key, []).append(value.strip())
			elif key is not None:
				attributes[key][-1] += '\n' + line
		elif text.startswith('<'):
			# an attribute of the entry above (kept sorted: the order they
			# are stored in says nothing)
			contents[current].append(' '.join(text.split()))
			contents[current][3:] = sorted(contents[current][3:])
		elif m:
			depth = (len(line) - len(line.lstrip(' '))) // 2
			path = path[:depth] + [m.group(1)]
			current = '/'.join(path)
			size = '-' if m.group(3).startswith('d') else m.group(2)
			contents[current] = [size, m.group(3), m.group(4)]
		else:
			raise BuildError('%s: unexpected line in its listing: %s' % (hpkg, text))
	return attributes, contents


def same_package_but_vendor(a, b):
	(attributes_a, contents_a), (attributes_b, contents_b) = a, b
	strip = lambda d, key: {k: v for k, v in d.items() if k != key}
	return (strip(attributes_a, 'vendor') == strip(attributes_b, 'vendor')
		and strip(contents_a, '.PackageInfo') == strip(contents_b, '.PackageInfo'))


DESKBAR_APPLICATIONS = 'data/deskbar/menu/Applications'


def jam_tokens(text):
	"""The tokens of a Jamfile: words between white space, a "quoted" one as
	one word, # comments left out."""
	return [m.group(1) if m.group(1) is not None else m.group(3)
		for m in re.finditer(r'"([^"]*)"|(#[^\n]*)|(\S+)', text) if m.group(2) is None]


def deskbar_categories(tree):
	"""{Applications menu entry: folder} from a Haiku tree's
	build/jam/DeskbarCategories (the Prose fork's): the folder of the
	Deskbar's Applications menu each application's entry goes into. The
	tree places its own applications; the ports' entries are moved when
	local-packages copies their packages in. Empty for a tree without it."""
	path = Path(tree) / 'build' / 'jam' / 'DeskbarCategories'
	if not path.exists():
		return {}
	tokens = jam_tokens(path.read_text())
	lists = {}
	for i, token in enumerate(tokens):
		if token.startswith('DESKBAR_CATEGOR') and tokens[i + 1:i + 2] == ['=']:
			lists[token] = tokens[i + 2:tokens.index(';', i)]
	folders = lists.get('DESKBAR_CATEGORIES', [])
	categories = {}
	for name, entries in lists.items():
		if name == 'DESKBAR_CATEGORIES':
			continue
		folder = name[len('DESKBAR_CATEGORY_'):]
		if folder not in folders:
			raise BuildError('%s: %s, but %s is not in DESKBAR_CATEGORIES' % (path, name, folder))
		for entry in entries:
			if entry in categories:
				raise BuildError('%s: %s is in %s and in %s' % (path, entry, categories[entry], folder))
			categories[entry] = folder
	return categories


def blocked_apps(tree):
	"""The applications a Haiku tree's build/jam/ProseBlocklist leaves out
	(PROSE_BLOCKED_APPS): Haiku's own, which the tree's build handles, and
	ones inside a port's package, which local-packages takes out of the
	package (its program in apps/ and its Deskbar entries); the rest of the
	package stays."""
	path = Path(tree) / 'build' / 'jam' / 'ProseBlocklist'
	if not path.exists():
		return set()
	tokens = jam_tokens(path.read_text())
	if 'PROSE_BLOCKED_APPS' not in tokens:
		return set()
	start = tokens.index('PROSE_BLOCKED_APPS') + 2
	return set(tokens[start:tokens.index(';', start)])


def remove_blocked_apps(root, blocked):
	"""Take the blocked applications out of an extracted package (root):
	apps/<name>, and Deskbar menu entries so named. What was removed."""
	removed = []
	apps = Path(root) / 'apps'
	for name in sorted(blocked):
		target = apps / name
		if target.is_symlink() or target.exists():
			rmtree(target)
			removed.append('apps/' + name)
	menu = Path(root) / 'data' / 'deskbar' / 'menu'
	if menu.is_dir():
		for entry in sorted(menu.rglob('*')):
			if entry.name in blocked and (entry.is_symlink() or entry.is_file()):
				entry.unlink()
				removed.append(str(entry.relative_to(root)))
	return removed


def unblocked_listing(listing, blocked):
	"""A package_listing() as remove_blocked_apps() leaves the package."""
	attributes, contents = listing
	if not blocked:
		return listing
	keep = {}
	for path, value in contents.items():
		parts = path.split('/')
		if len(parts) >= 2 and parts[0] == 'apps' and parts[1] in blocked:
			continue
		if path.startswith('data/deskbar/menu/') and parts[-1] in blocked \
				and not value[1].startswith('d'):
			continue
		keep[path] = value
	return attributes, keep


def deskbar_category_icons(tree, categories):
	"""{folder: HVIF icon} from a Haiku tree's
	src/data/directory_attrs/deskbar-applications-<folder>.rdef, the icons
	the haiku package gives its category folders; folders without one are
	left out."""
	icons = {}
	for folder in sorted(set(categories.values())):
		path = Path(tree) / 'src' / 'data' / 'directory_attrs' / (
			'deskbar-applications-%s.rdef' % folder.lower())
		if not path.exists():
			continue
		m = re.search(r'resource\s*\([^)]*"BEOS:ICON"\s*\)\s*#\'VICN\'\s*array\s*\{(.*?)\}',
			path.read_text(), re.S)
		if not m:
			raise BuildError('%s: no BEOS:ICON (VICN) resource' % path)
		icons[folder] = bytes.fromhex(''.join(re.findall(r'\$"([0-9A-Fa-f]*)"', m.group(1))))
	return icons


def copy_haiku_attributes(src, dst):
	"""Copy the Haiku attributes of src to dst -- the user.haiku.* extended
	attributes the host's package tool keeps them in -- of symlinks
	themselves."""
	names = run(['/usr/bin/xattr', '-s', src], capture=True).stdout.split('\n')
	for name in names:
		if name.startswith('user.haiku.'):
			value = run(['/usr/bin/xattr', '-s', '-px', name, src], capture=True).stdout
			run(['/usr/bin/xattr', '-s', '-wx', name, ''.join(value.split()), dst], capture=True)


def categorize_deskbar_entries(root, categories, icons=None):
	"""Move the Deskbar Applications entries of an extracted package (root)
	into their folders (deskbar_categories()): a folder gets the mode and
	attributes of Applications and its icon (deskbar_category_icons()), and
	a relative symlink one more '../'. The number of entries moved."""
	apps = Path(root) / DESKBAR_APPLICATIONS
	if not categories or not apps.is_dir():
		return 0
	moved = 0
	for entry in sorted(apps.iterdir()):
		folder = categories.get(entry.name)
		if folder is None or not entry.is_symlink():
			continue
		target = apps / folder
		if not target.exists():
			target.mkdir()
			os.chmod(target, os.stat(apps).st_mode & 0o7777)
			copy_haiku_attributes(apps, target)
			if icons and folder in icons:
				# the attribute's type code ('VICN') first, little-endian
				run(['/usr/bin/xattr', '-s', '-wx', 'user.haiku.BEOS:ICON',
					(b'NCIV' + icons[folder]).hex(), target], capture=True)
		link = os.readlink(entry)
		new = target / entry.name
		os.symlink(link if link.startswith('/') else '../' + link, new)
		os.lchmod(new, os.lstat(entry).st_mode & 0o7777)
		copy_haiku_attributes(entry, new)
		entry.unlink()
		moved += 1
	return moved


def categorized_listing(listing, categories, icons=None):
	"""A package_listing() as categorize_deskbar_entries() leaves the
	package."""
	attributes, contents = listing
	prefix = DESKBAR_APPLICATIONS + '/'
	moved = {}
	for path, value in contents.items():
		name = path[len(prefix):] if path.startswith(prefix) else None
		folder = categories.get(name) if name and '/' not in name else None
		if folder is None or not value[1].startswith('l'):
			moved[path] = value
			continue
		if prefix + folder not in moved:
			entry = list(contents[DESKBAR_APPLICATIONS])
			if icons and folder in icons:
				entry[3:] = sorted(entry[3:] + ["<BEOS:ICON %d 'VICN'>" % len(icons[folder])])
			moved[prefix + folder] = entry
		link = value[2]
		if link.startswith('-> ') and not link.startswith('-> /'):
			link = '-> ../' + link[3:]
		moved[prefix + folder + '/' + name] = value[:2] + [link] + value[3:]
	return attributes, moved


def copy_with_vendor(src, dst, vendor, categories=None, icons=None, blocked=None):
	"""Write the package src to dst with another vendor, and its Deskbar
	Applications entries in their folders (categorize_deskbar_entries()). A
	Haiku build's repository accepts only its own vendor ("Haiku Project");
	ours say VENDOR."""
	tmp = CACHE / ('vendor-' + src.name)
	rmtree(tmp)
	tmp.mkdir(parents=True)
	new = dst.with_name('.' + dst.name + '.new')
	try:
		run([host_tool('package'), 'extract', '-C', tmp, src], capture=True)
		info = tmp / '.PackageInfo'
		text, count = re.subn(r'^vendor\s.*$', 'vendor\t\t"%s"' % vendor,
			info.read_text(), flags=re.M)
		if count != 1:
			raise BuildError('%s: %d vendor lines in its .PackageInfo' % (src.name, count))
		info.write_text(text)
		if blocked:
			removed = remove_blocked_apps(tmp, blocked)
			if removed:
				say('%s: left out (ProseBlocklist): %s' % (src.name, ', '.join(removed)))
		categorize_deskbar_entries(tmp, categories, icons)
		if new.exists():
			new.unlink()
		run([host_tool('package'), 'create', '-q', new], cwd=tmp, capture=True)
		# replaced by rename: the build never sees half a package
		os.replace(new, dst)
	finally:
		rmtree(tmp)
		if new.exists():
			new.unlink()


def cmd_local_packages(args):
	"""Make a Haiku tree build with the packages its fork lists beyond
	upstream's HaikuPorts list, which prosepkg builds and the package server
	does not have: copy them into the tree's generated/download/ with the
	repository's vendor, and turn downloads off. A tree whose list is
	upstream's is left alone."""
	tree = Path(args.tree).resolve()
	config_file = tree / 'generated' / 'build' / 'BuildConfig'
	config = config_file.read_text() if config_file.exists() else ''
	m = re.search(r'^HAIKU_PACKAGING_ARCHS\s*\?=\s*"?(\w+)', config, re.M)
	arch = m.group(1) if m else ARCH
	upstream = listed_packages(tree, arch, upstream_base(tree))
	ours = sorted(leaf for leaf in listed_packages(tree, arch) if leaf not in upstream)
	if not ours:
		say('%s: the package list is upstream\'s, nothing to do' % tree)
		return
	if not config:
		raise BuildError('%s is not configured (no generated/build/BuildConfig)' % tree)
	if arch != ARCH:
		raise BuildError('%s builds %s; prosepkg builds %s packages' % (tree, arch, ARCH))
	if not host_tool('package').exists() or not REPO.is_dir():
		raise BuildError('%s lists %d packages beyond upstream\'s, which come from prosepkg; '
			'%s has none (scripts/prosepkg bootstrap, then build them)' % (tree, len(ours), ROOT))
	m = re.search(r'^vendor\s+"([^"]*)"', (tree / 'src' / 'data' / 'repository_infos'
		/ 'haikuports').read_text(), re.M)
	if not m:
		raise BuildError('%s: no vendor in src/data/repository_infos/haikuports' % tree)
	vendor = m.group(1)

	categories = deskbar_categories(tree)
	icons = deskbar_category_icons(tree, categories)
	blocked = blocked_apps(tree)
	download = tree / 'generated' / 'download'
	missing, todo, foreign = [], [], []
	for leaf in ours:
		src, dst = REPO / leaf, download / leaf
		if not src.exists():
			missing.append(leaf)
		elif not dst.exists():
			todo.append(('add', leaf))
		else:
			listing = package_listing(dst)
			if listing[0].get('packager') != [PACKAGER]:
				foreign.append(leaf)
			elif listing[0].get('vendor') != [vendor] \
					or not same_package_but_vendor(listing, categorized_listing(
						unblocked_listing(package_listing(src), blocked), categories, icons)):
				todo.append(('update', leaf))
	if missing:
		results = load_json(RESULTS, {})
		lines = []
		for leaf in missing:
			name = leaf.split('-')[0]
			built = sorted(p.name for p in REPO.glob(name + '-*.hpkg'))
			port = next((p for p, r in sorted(results.items())
				if any(f.split('-')[0] == name for f in r.get('packages', []))), None)
			lines.append('  %-44s %s' % (leaf, 'built: ' + ', '.join(built) if built
				else 'port %s: %s' % (port, results[port].get('status')) if port
				else 'never built'))
		raise BuildError('%s lists packages prosepkg has not built:\n%s\n'
			'build them (scripts/prosepkg build <port>), or list the versions that are built'
			% (tree, '\n'.join(lines)))

	downloads_off = re.search(r'^HAIKU_NO_DOWNLOADS\s*\?=\s*"1"\s*;', config, re.M)
	say('%s: %d packages beyond upstream\'s list, %d up to date in generated/download%s' % (
		tree, len(ours), len(ours) - len(todo) - len(foreign),
		'' if downloads_off else '; downloads are on'))
	for action, leaf in todo:
		print('  %-6s %s' % (action, leaf))
	for leaf in foreign:
		print('  kept   %s (not built by prosepkg)' % leaf)
	new_config = None
	if not downloads_off:
		new_config, n = re.subn(r'^HAIKU_NO_DOWNLOADS\s*\?=.*$',
			'HAIKU_NO_DOWNLOADS\t\t\t?= "1" ;', config, flags=re.M)
		if not n:
			new_config = config.rstrip('\n') + '\nHAIKU_NO_DOWNLOADS\t\t\t?= "1" ;\n'
		print('  set    HAIKU_NO_DOWNLOADS ?= "1" in generated/build/BuildConfig')
	if args.dry_run:
		if todo or new_config is not None:
			say('dry run: nothing written')
		return
	download.mkdir(parents=True, exist_ok=True)
	for action, leaf in todo:
		copy_with_vendor(REPO / leaf, download / leaf, vendor, categories, icons, blocked)
	if new_config is not None:
		write_file(config_file, new_config)
	if todo or new_config is not None:
		say('%s: done' % tree)


def cmd_index(args):
	recipes, index = recipe_index(refresh=args.refresh)
	print('%d ports, %d provided names' % (len(recipes), len(index)))


def haikuports_commit():
	"""The recipe tree's commit: the versions the packages have (and the
	Haiku tree's package list names) come from the recipes at this commit."""
	proc = run(['git', 'rev-parse', '--short', 'HEAD'], cwd=HAIKUPORTS, capture=True, check=False)
	return proc.stdout.strip() if proc.returncode == 0 else 'unknown'


def write_report(results):
	lines = ['# prosepkg build results', '',
		'Generated by `prosepkg build`; one row per port, newest attempt.',
		'Recipes: haikuports at commit %s (`/Volumes/HaikuSrc/prose-packages/haikuports`); '
		'the package versions here, and the ones patches/haiku 0033, 0037 and 0040 '
		'list, are the recipes\' at that commit.' % haikuports_commit(), '',
		'| port | version | result | packages / error |', '|---|---|---|---|']
	for name in sorted(results):
		r = results[name]
		what = ', '.join(r.get('packages', [])) if r.get('status') == 'built' \
			else (r.get('error') or '')[:160].replace('|', '/')
		lines.append('| %s | %s | %s | %s |' % (name, r.get('version', ''),
			r.get('status', ''), what))
	write_file(BUILDER_DIR.parent / 'RESULTS.md', '\n'.join(lines) + '\n')


def main():
	p = argparse.ArgumentParser(prog='prosepkg', description=__doc__.split('\n')[0])
	sub = p.add_subparsers(dest='command', required=True)
	b = sub.add_parser('bootstrap', help='copy toolchain, host tools and base packages; generate the environment')
	b.add_argument('--refresh', nargs='?', const='all', metavar='STEPS',
		help='redo steps: all (default) or a comma list of toolchain,hosttools,hostsdk,base')
	b.set_defaults(func=bootstrap)
	b = sub.add_parser('build', help='build ports (and their build requirements)')
	b.add_argument('ports', nargs='+')
	b.add_argument('--force', action='store_true', help='rebuild the named ports even if packaged')
	b.add_argument('--force-all', action='store_true',
		help='rebuild every port the run touches (dependencies too), each once')
	b.add_argument('--force-arch', action='store_true', help='ignore ARCHITECTURES')
	b.add_argument('--keep-going', '-k', action='store_true')
	b.add_argument('--keep-work', action='store_true', help='keep the work dir of successful builds')
	b.set_defaults(func=cmd_build)
	b = sub.add_parser('info', help='show how a port resolves')
	b.add_argument('ports', nargs='+')
	b.add_argument('--force-arch', action='store_true')
	b.add_argument('--keep-work', action='store_true')
	b.set_defaults(func=cmd_info)
	b = sub.add_parser('install', help='install built packages (+ requirements) into a Haiku image')
	b.add_argument('image')
	b.add_argument('packages', nargs='+', help='package or port names, @set (packages/sets/<set>), '
		'or .hpkg files (installed as they are)')
	b.set_defaults(func=cmd_install)
	b = sub.add_parser('local-packages', help='put the packages a Haiku tree lists beyond '
		'upstream into its generated/download/, downloads off')
	b.add_argument('tree', nargs='?', default=str(HAIKU_TREE))
	b.add_argument('--dry-run', '-n', action='store_true', help='say what would change')
	b.set_defaults(func=cmd_local_packages)
	b = sub.add_parser('audit', help='check built packages for build-host paths and cross tool names')
	b.add_argument('packages', nargs='*', help='hpkg file names (default: the whole repo)')
	b.set_defaults(func=cmd_audit)
	b = sub.add_parser('index', help='(re)evaluate all recipes')
	b.add_argument('--refresh', action='store_true')
	b.set_defaults(func=cmd_index)
	args = p.parse_args()
	try:
		args.func(args)
	except BuildError as e:
		say('ERROR:', e)
		sys.exit(1)


if __name__ == '__main__':
	main()
