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

```sh
scripts/prosepkg install <image> libwebp flac     # + their requirements
scripts/prosepkg install <image> @codecs          # a set: packages/sets/codecs
PW_PACKAGES=@codecs private_workspace/run-vz.sh try   # boot with them (VZ)
PW_PACKAGES=@codecs private_workspace/run-qemu.sh     # (QEMU, boots a copy)
packages/test-codecs.sh                           # boot test, see below
```

`install` copies the packages into the image's `system/packages`, and
packagefs activates them at the next boot. Requirements are resolved against
what the image already has. System packages the image lacks come from the
base set (the minimum image has no `grep`, for example). Older versions of
the same packages are removed, and the result is checked by remounting. The
run scripts install into their fresh copy, never into the build output.

**Codecs (verified on arm64):** libpng16, libjpeg_turbo, libwebp, tiff,
giflib, openjpeg, lcms, libogg, libvorbis, flac, opus, speex, speexdsp,
mpg123, lame, wavpack, libtheora, libvpx, dav1d (+ libiconv, libltdl).
`test-codecs.sh` installs `codec_check`, our own recipe in
`builder/overlay/prose-tests`. It links all of them, prints each version and
round-trips data through each codec (PNG/WebP/TIFF/GIF/FLAC lossless,
JPEG, Opus, LAME→mpg123, VP8). It boots a clone of the image headless under
QEMU and prints what the guest wrote. Boot to power-off takes about 7 s.
Current result: 19/19 ok.

## Rules (the incident, turned into design)

1. The builder writes only below `/Volumes/HaikuSrc/prose-packages/`.
   `bootstrap` copies what it needs from the Haiku tree. After that, builds
   read nothing from the tree, and nothing is ever written into it.
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
packages/builder/overlay/           our recipes / per-recipe *.prose.sh snippets
packages/sets/                      named package sets for `install @<set>`
packages/test-codecs.sh             boot test of the codec packages

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
Programs that probe CPU features by catching SIGILL (OpenSSL on unknown
OSes) will crash or hang until that is fixed.

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
  assign `DESTDIR =`.
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
