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
* Packaging follows haikuporter: licenses into `data/licenses`,
  `.PackageInfo` from the recipe, `mimeset --all` with a writable package
  MIME DB, then `package create`.

Not done (yet): `_debuginfo` packages (their files are stripped instead),
`TEST()`, source packages, and version constraints in dependency resolution
(resolution is by name).

## Other files here

* [CATALOG.md](CATALOG.md) — index of the 385 HaikuArchives repositories
  (`make_catalog.py`, raw GitHub data in `data/`); clones under `src/`.
* [HAIKUPORTER.md](HAIKUPORTER.md) — how the community builds packages, and
  the incident.
