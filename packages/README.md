# Prose packages — our own Haiku package builder for arm64

`prosepkg` builds Haiku packages (`.hpkg`) for arm64 on this Mac. Its input
is the community's [haikuports](https://github.com/haikuports/haikuports)
recipes, run unmodified where possible. Its output is real packages:
`.PackageInfo`, resources, MIME attributes, and devel subpackages, laid out
exactly as haikuporter lays them out on Haiku.

Why our own builder and not haikuporter: its non-chroot mode only supports
Haiku's bootstrap recipes, and it takes ownership of the cross-tools
directory it is given. See [HAIKUPORTER.md](HAIKUPORTER.md) for the
2026-09-18 incident that came from pointing it at the main tree.

```sh
scripts/prosepkg bootstrap            # once: private toolchain, host tools, base sysroot
scripts/prosepkg build libpng16       # build a port (and whatever it needs to build)
scripts/prosepkg build -k tipster pe  # several; -k keeps going after failures
scripts/prosepkg info bepdf           # recipe, packages, how build requirements resolve
```

Results: [RESULTS.md](RESULTS.md) (regenerated on every build), packages in
`/Volumes/HaikuSrc/prose-packages/repo/`, logs in `.../logs/<port>.log`.

## Putting packages on our image

The fork's image is built with the codec set in it (patch 0040). The Haiku build
takes those packages from its `generated/download/`, and
`scripts/local-packages.sh` puts them there: it copies every package the tree
lists beyond upstream's, then turns downloads off. `scripts/build-image.sh`
and `private_workspace/build.sh` run it, so a codec that prosepkg rebuilds is
in the next image (see `private_workspace/README.md`, "Local packages").
`install` is for everything else, and for trying packages without rebuilding
the image:

```sh
scripts/prosepkg install <image> libwebp flac     # + their requirements
scripts/prosepkg install <image> @codecs          # a set: packages/sets/codecs
PW_PACKAGES=@codecs private_workspace/run-vz.sh try   # boot with them (VZ)
PW_PACKAGES=@codecs private_workspace/run-qemu.sh     # (QEMU, boots a copy)
packages/boot-test.sh packages/tests/codecs.sh codec_check   # boot test, see below
```

`install` copies the packages into the image's `system/packages`, and
packagefs activates them at the next boot. Requirements are resolved against
what the image already has, whether system packages or ours. System packages the image lacks come from the
base set (the minimum image has no `grep`, for example). Older versions of
the same packages are removed, and the result is checked by remounting. The
run scripts install into their fresh copy, never into the build output.

**Codecs (verified on arm64):** libpng16, libjpeg_turbo, libwebp, tiff,
giflib, openjpeg, lcms, libogg, libvorbis, flac, opus, speex, speexdsp,
mpg123, lame, wavpack, libtheora, libvpx, dav1d (+ libiconv, libltdl).
`codec_check` is our own recipe in `builder/overlay/prose-tests`. It links
all of them, prints each version and round-trips data through each codec
(PNG/WebP/TIFF/GIF/FLAC lossless, JPEG, Opus, LAME→mpg123, VP8). Current
result: 19/19 ok.

**Boot tests:** `boot-test.sh <probe> <packages...>` installs the packages
into a clone of the image and runs the probe (`packages/tests/*.sh`) from a
UserBootscript. It boots headless under QEMU, prints what the guest wrote,
and exits 0 if the last line is `PASS`. Boot to power-off takes about 7 s.
The probes are `tests/codecs.sh` (codec_check), `tests/openssl.sh`,
`tests/minimum.sh` (OpenSSL with its certificates, bc, wget: patch 0041) and
`tests/apps.sh` (the Prose profile's applications and demos: patch 0042),
`tests/netsurf.sh`, `tests/network.sh` (HTTPS from the target) and
`tests/midiplayer.sh` (patch 0048: plays a demo tune, quits cleanly).
The minimum image has no grep, sed or awk, so probes use bash and coreutils.

## Rules (the incident, turned into design)

1. Builds write only below `/Volumes/HaikuSrc/prose-packages/`.
   `bootstrap` copies what it needs from the Haiku tree. After that, builds
   read nothing from the tree and never write into it. The one command that
   writes into a Haiku tree is `local-packages`, run by the image build
   scripts. It writes only the package files the tree's own list names, into
   `generated/download/`, and the `HAIKU_NO_DOWNLOADS` line of
   `generated/build/BuildConfig`.
2. The toolchain is a private, read-only copy (`chmod a-w`).
3. Generated wrappers are real files, replaced by rename. There are no
   symlinks to tools.
4. Each build is hermetic: an APFS clone of the base sysroot plus exactly the
   declared build requirements, and a clean environment (no host `CC`,
   `CFLAGS`, ...).
5. Cross-specific recipe changes live in `builder/overlay/`, never in the
   haikuports clone.

## Layout

```
packages/builder/prosepkg.py        the builder
packages/builder/recipe-runtime.sh  shell environment recipes run in
packages/builder/overlay/           our recipes, per-recipe <recipe>.prose.sh snippets
                                    and <recipe>*.patch source patches
packages/sets/                      named package sets for `install @<set>`
packages/boot-test.sh, tests/       boot tests on the target (probes)

/Volumes/HaikuSrc/prose-packages/   (case-sensitive volume)
  toolchain/cross-tools-arm64/  gcc 13.3 + binutils, copied, read-only
  hosttools/                    rc xres mimeset settype linkcatkeys package ... (+ their dylibs)
  base/packages/                the system packages of a Prose image (haiku,
                                haiku_devel, makefile_engine, gcc_syslibs, zlib,
                                freetype, icu74, ncurses6, expat, ...)
  base/sysroot/boot/system/     all of them extracted; per-build sysroots clone it
  base/mime_db/                 system MIME database (attributes as xattrs)
  env/bin/                      compiler/binutils/pkg-config/finddir/... wrappers
  haikuports/                   recipe tree (git clone, unmodified)
  work/<port>/                  sources, sysroot, destdir while building
  repo/                         built .hpkg files
```

## Target CPU: Apple silicon, NEON and SIMD on

Prose runs in VMs on Apple silicon, so everything is compiled for the
**Apple M1 feature floor**:

    -march=armv8.4-a+fp16+fp16fml+aes+sha2+sha3 -mtune=cortex-x1

That is LSE atomics, RDM, RCPC, JSCVT, FCMA, DotProd and FlagM, plus
FP16/FHM and AES/SHA1/SHA2/SHA3/SHA512. gcc 13 has no `apple-m*` names, and
`+crypto` is avoided because for v8.4 it adds SM3/SM4, which Apple lacks.
BF16/I8MM (M2+) and SVE/SME stay off, so packages run on any M-series host
(`TARGET_CPU_FLAGS` in prosepkg.py). The wrappers put these flags first,
so a package's own `-march` for a specially compiled file still wins.
Compile-time features also switch on code paths that libraries would
otherwise pick by runtime detection that knows no Haiku (dav1d's DotProd
assembly, for example).

SIMD per codec, checked in the build logs:

- NEON already on by default: libjpeg-turbo (full NEON intrinsics),
  libpng, libwebp, FLAC, Opus and SpeexDSP.
- Overlays needed: **libvpx** configured as `generic-gnu` (no SIMD at all)
  because it knows no Haiku target, so it now builds as `arm64-linux-gcc`
  (the target libvpx uses for BSDs) with NEON built in. **mpg123** chose
  `generic_fpu`, so it now uses `--with-cpu=aarch64` (NEON64).
- No ARM SIMD upstream: LAME, Vorbis, Theora, OpenJPEG and lcms2.

`codec_check` executes one instruction per floor feature in a child
process on the target. All of them work under QEMU+HVF on an M4, and so do
the M2+ extras (I8MM, BF16).

Known Haiku arm64 kernel bug (our fork): `do_sync_handler` has no case for
undefined instructions (`EXCP_UNKNOWN`). The thread gets a garbage
exception type and signal ("Alignment exception") instead of SIGILL.
Programs that probe CPU features by catching SIGILL will crash or hang until
that is fixed.

**OpenSSL** (3.5.8) is one such program: on OSes without getauxval/sysctl it
executes 11 probe instructions when libcrypto loads, and 5 of them do not
exist on Apple silicon. The recipe also builds it `no-asm` everywhere but
x86_64, and OpenSSL knows no `haiku-aarch64` target. Our overlay patch
(`overlay/dev-libs/openssl/`) does three things:
- adds that target;
- takes the CPU capabilities from the compile-time `__ARM_FEATURE_*`, the
  way OpenSSL's own Apple branch presets them, so there is no probing at
  all (`OPENSSL_armcap` still overrides);
- builds with assembly.

`tests/openssl.sh` checks known-answer digests (SHA-256/512, SHA3-256) and
an AES round trip, then compares throughput in the guest on an M4 against
`OPENSSL_armcap=0` (plain C):

| | ARMv8 crypto | plain C |
|---|---|---|
| AES-128-GCM | 10.8 GB/s | 0.26 GB/s |
| SHA-256 | 3.3 GB/s | 0.61 GB/s |
| SHA-512 | 1.8 GB/s | 0.96 GB/s |

(`openssl speed` needs `-elapsed` on Prose: CPU-time accounting via
`times()` reports far too little, which gave TB/s figures.)

## Emulating the chroot

A Haiku build runs inside a chroot; here the build env answers the way that
chroot would:

- **compiler wrappers:** `--sysroot`, and `/boot`, `/system` and
  `/packages` paths in arguments, including joined `-isystem/...` forms
- **`uname`** reports Haiku arm64; **`jam`** runs with `-sOS=HAIKU`
- **`finddir`/`findpaths`** answer with sysroot paths, including subpaths
- **`/packages/<pkg>/.self`** links exist in the sysroot, for the prefixes
  recorded by haikuporter-built system packages
- **`make`:** a Makefile that does `include /boot/system/...` runs as a
  translated copy. `$(MAKE)` comes back through the wrapper. In INSTALL,
  `DESTDIR` is also passed on the command line, which beats Makefiles that
  assign `DESTDIR =`. A staged path on the command line (`make install
  PREFIX=$prefix`, `MANDIR=$manDir`) turns back into the runtime path when
  the Makefile uses `DESTDIR`; if it doesn't, `DESTDIR` is dropped.
- **PATH** ends in `.`, as Haiku's does

What cannot be emulated goes into an overlay: scripts with `#!/bin/sh`
bashisms (macOS `/bin/sh` is bash 3.2), and `test -f /boot/...` in build
scripts.

## How a recipe runs

As in haikuporter: the recipe is sourced by bash 5 with the usual variables
(`$portName`, `$sourceDir`, `$jobArgs`, the directory variables, ...) and
helper functions (`runConfigure`, `packageEntries`,
`prepareInstalledDevelLibs`, `fixPkgconfig`, `addAppDeskbarSymlink`,
`defineDebugInfoPackage`, ...). Then PATCH, BUILD and INSTALL run in
`$sourceDir`. The difference is that there is no chroot:

* **BUILD** sees runtime paths (`$prefix` = `/boot/system`). The compiler
  wrappers pass `--sysroot` and map `/boot/...` and `/system/...` arguments
  into the build sysroot, so `-I/boot/system/develop/headers/...` works as
  on Haiku. `finddir`/`findpaths` answer with sysroot paths.
* **INSTALL** sees the same variables re-rooted into
  `work/<port>/destdir/boot/system`, with `DESTDIR` exported. Both
  `cp foo $appsDir` and a plain `make install` land in the package.
  `packageEntries` moves files into `work/<port>/sub/<suffix>/...`.
* `runConfigure` always passes `--build=aarch64-apple-darwin
  --host=aarch64-unknown-haiku`. `cmake` gets a toolchain file and `meson`
  a cross file.
* Host Haiku tools (`rc`, `xres`, `mimeset`, `linkcatkeys`, ...) are the
  ones the Haiku build compiled for macOS. `linkcatkeys -tr` (embedding a
  catalog into a binary) is emulated with `xres`, because the host build
  cannot do it itself.
* **Tools a port runs while building** are Haiku binaries in a native
  build and cannot run here. Two ways out, both in overlays:
  `host-be-c++` (the bootstrap's `hostsdk` step: the build-host Be API
  headers and `libbe_build`, as Haiku compiles `rc` and `xres`) builds a
  port's own tool for the Mac -- Pe's resource compiler `rez`; and
  `PROSE_HOST_TOOLS="<ports>"` with a `HOST_BUILD()` function fetches those
  ports' sources and builds them for the Mac before the real build, with
  `$hostPrefix/bin` on the PATH after -- NetSurf's `nsgenbind`, with the
  NetSurf build system. A Makefile that hard-codes its build compiler
  (`BUILD_CC := cc`) gets `BUILD_CC=/usr/bin/clang` on the command line.
* `make` recipes run in bash 5, as on Haiku (`SHELL=`): macOS's `/bin/sh`
  is bash 3.2 in POSIX mode, whose `echo -n` prints the `-n`.
* A `BUILD_PREREQUIRES` entry that is a port (a Perl module, say) is
  built for the target if it can be; if not, that is a note in the log,
  not a failure: prerequisites are build-time tools, and the Mac often has
  them (`XML::Parser`, `HTML::Parser`).
* After INSTALL, text files and symlinks are scrubbed. Recipes that write
  files during INSTALL (e.g. a `.pc` file from a heredoc over `$prefix`)
  saw staging paths, so every staging root is mapped back to `/boot/system`,
  and Homebrew tool paths to `/bin`. `runConfigure` passes the plain tool
  names (`CC=gcc`, `AR=ar`, ...) for the same reason: installed scripts such
  as `libtool` must name tools that exist on Haiku.
* Packaging follows haikuporter: licenses into `data/licenses`,
  `.PackageInfo` from the recipe, `mimeset --all` with a writable package
  MIME DB, then `package create`.
* `prosepkg audit` extracts built packages and flags build-host paths or
  cross tool names in text files and symlinks.

Not done (yet): `_debuginfo` packages (their files are stripped instead),
`TEST()`, source packages, and version constraints in dependency resolution
(resolution is by name).

## Other files here

* [CATALOG.md](CATALOG.md) — index of the 385 HaikuArchives repositories
  (`make_catalog.py`, raw GitHub data in `data/`); clones under `src/`.
* [HAIKUPORTER.md](HAIKUPORTER.md) — how the community builds packages, and
  the incident.
