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
  The community buildmaster (the docker image above) only *orchestrates*:
  its builders are Haiku machines reached over SSH, building natively.
* The **non-chroot** mode on Linux/macOS exists for Haiku's own bootstrap
  (`configure --bootstrap`: haikuports.cross plus a few cross-aware
  recipes). It *owns* the `CROSS_TOOLS` directory it is given: every build
  `rmtree`s and re-stages `<cross-tools>/sysroot/boot/system/develop` and
  symlinks headers/libs into it. The bootstrap gives it a toolchain built for
  that purpose.

## Incident 2026-09-18 — why this path was abandoned

The first attempt (another agent, not Claude) pointed haikuporter's
`CROSS_TOOLS` at the **main tree's** toolchain
(`/Volumes/HaikuSrc/haiku/generated/cross-tools-arm64`) and hacked
haikuporter in place (hard-coded `/Volumes/...` paths, trace code compiling
`/tmp/et.c` in every phase, recipes deleted from the haikuports clone so the
resolver would pick bootstrap packages). Damage, all repaired the same day:

| what | effect | repair |
|---|---|---|
| haikuporter staging | main toolchain's `sysroot/` wiped and re-staged per build | removed, as `build_cross_tools_gcc4` leaves it |
| `env/plainbin/{gcc,g++,cc,c++}` = symlinks into the toolchain, then `cat > plainbin/g++` | `aarch64-unknown-haiku-{gcc,gcc-13.3.0,g++,c++}` replaced by a 490-byte script that exec'd **itself**; every jam build hung (the private-workspace build sat 12 min in the loop) | drivers rebuilt from buildtools with the toolchain's own `configargs.h` line, installed with gcc's hard-link pairs — `scripts/repair-cross-gcc.sh` |
| extra `aarch64-unknown-haiku-cc` script | not part of gcc's install | deleted |
| `configure --build-cross-tools` re-run during the breakage | `BuildConfig` rewritten with an empty `HAIKU_GCC_LIB_DIR_arm64` (its gcc query returned nothing) | line restored; file byte-identical in size to before |
| `PACKAGES_PATH` = main tree's `packaging/packages` | bzip2, libpng16 and copies of zlib/freetype bootstrap hpkgs mixed into the Haiku build's own packages | moved out |
| `pkg-sysroot.sh` | `headers/compatibility/oldstl/` created in the main **source** tree (incl. a `string.h` that includes `<string>`) | moved out |

Everything moved out is kept in
`/Volumes/HaikuSrc/prose-packages/quarantine-2026-09-18/` (the plainbin
files, the stray hpkgs, `oldstl/`, the haikuporter hack diff, the haikuports
recipe edits, the `haikuports.conf`). The research clones are reset to
upstream.

Lessons, binding for the package builder:
1. The builder writes only below `/Volumes/HaikuSrc/prose-packages/`. The
   Haiku tree, its toolchain and its image are inputs, never outputs.
2. No symlinks to tools. Generated wrappers are real files, replaced by
   rename, never written through.
3. A builder that must own a toolchain gets its own copy.
4. Recipe fixes live in our own overlay, not in edited upstream clones.

Conclusion: haikuporter stays a reference. Prose builds packages with its own
builder (see README.md), which reads haikuports recipes but runs them in a
cross environment designed for this host.
