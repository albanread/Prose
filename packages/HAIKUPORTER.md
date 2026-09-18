# haikuporter on macOS — the Prose cross-build investigation

How the Haiku community mass-builds software, and what it takes to run their
machinery on this Mac for arm64. Sources cloned under
`/Volumes/HaikuSrc/prose-packages/research/` (haikuporter + haikuports).

## How the community build servers work

* **haikuporter** (Python) reads **recipes** (haikuports repo, ~6000 ports).
  Each recipe declares PROVIDES/REQUIRES/BUILD_REQUIRES, and BUILD/INSTALL/
  TEST scriptlets using helper functions (`runConfigure`, `packageEntries`,
  `prepareInstalledDevelLib`, ...).
* Dependency resolution builds a graph; a port's build environment is a
  **chroot** on Haiku, or a **non-chroot staged sysroot** on Unix hosts:
  per-port `<work>/boot/cross-sysroot/arm64` gets `haiku_devel` + the port's
  required packages extracted, the cross compiler is staged with
  include/lib symlinks into it, and `B_PACKAGE_LINKS_DIRECTORY` (`/packages`)
  is where recipes "install" — a root-level symlink on their Linux
  containers (they run as root in docker; the buildmaster image is
  `ghcr.io/haikuports/haikuporter/buildmaster`).
* On **Haiku** hosts everything runs chrooted (binaries executable natively).
* On **Linux** they run non-chrooted cross builds (or full docker clusters).

## Status on this Mac (macOS 27, arm64)

Working:
- haikuporter runs on macOS via `BuildPlatformUnix`; config at
  `/tmp/haikuports.conf` (regenerate: see below); the `.cross` marker file
  in the tree enables cross mode.
- Repository population, dependency resolution across the whole tree,
  source download/unpack/patch, recipe scriptlets (needs **bash 5**: brew),
  packaging into hpkgs, and package-into-repository flow all work.
- `--all-dependencies` planned and walked the chain for bepdf
  (zlib -> libpng16 -> libiconv -> bzip2 -> freetype -> ... -> bepdf).

Patched for macOS (all in research/haikuporter, uncommitted — keep as a
patch series):
1. `BuildPlatform.py`: `B_PACKAGE_LINKS_DIRECTORY` → writable volume path
   (macOS root is read-only); per-build symlink of that path into the
   staged sysroot (mirrors the Linux container's `/packages`).
2. `BuildPlatform.py`: `getInstallDestDir()` returns `None` (chroot-equivalent
   semantics; the sysroot prefix double-concatenates paths otherwise).
3. `BuildPlatform.py`: non-chroot cleanup removes only the staged symlinks,
   not the whole gcc sysroot (`shutil.rmtree(sysroot)` was destroying
   shared state between recipe phases).
4. `ShellScriptlets.py`: `runConfigure` passes `--host/--build` triples for
   cross repos (configure must not try to execute target test binaries).
5. Cross gcc staged with plain names (`env/plainbin`: gcc, g++, cc, ar, ...)
   and `cross-tools-arm64/aarch64-unknown-haiku/{include,lib}` symlinked to
   the staged develop tree, so the compiler resolves target headers.

Known remaining bug (characterized, not yet fixed):
- The BUILD-phase wrapper executes before `setupNonChrootBuildEnvironment()`
  in some code paths, so the staged sysroot/include links don't exist when
  `configure`/`make` run (symptom: `errno.h: No such file`, configure
  disables everything). The setup call needs to move earlier (or the wrapper
  needs to ensure the environment itself). Everything else works — when the
  links exist (verified manually), the exact same compile succeeds.

## Regenerate the environment

```sh
brew install bash autoconf automake libtool   # host tools
scripts/pkg-env.sh                             # wrappers + stubs (standalone builds)
# haikuports.conf: see /tmp/haikuports.conf in shell history; keys:
#   TREE_PATH, TARGET_ARCHITECTURE=arm64, CROSS_TOOLS=cross-tools-arm64,
#   CROSS_DEVEL_PACKAGE=haiku_devel.hpkg, PACKAGE_COMMAND=<package tool>,
#   MIMESET_COMMAND=<stub>, SYSTEM_MIME_DB, PACKAGES_PATH=<packages dir>,
#   LICENSES_DIRECTORY=<sysroot>/data/licenses, ALLOW_UNTESTED=yes
touch <TREE_PATH>/.cross
PATH=/opt/homebrew/bin:$PATH python3 haikuporter.py --config <conf> <port>
```
