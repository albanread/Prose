# Prose packages — Haiku-native software collection

Native Be/Haiku API C++ software only — no GTK/Qt/wx ports. Codecs and
POSIX/shell software allowed. Sources: the
[HaikuArchives](https://github.com/haikuarchives) collection;
[CATALOG.md](CATALOG.md) indexes it (regenerate: `python3 make_catalog.py`,
raw GitHub API data in `data/`).

## Layout

```
packages/
├── CATALOG.md          index of all 385 repos, batch-1 marked
├── src -> /Volumes/HaikuSrc/prose-packages/src       cloned repos (case-sensitive!)
├── sysroot-arm64 -> /Volumes/HaikuSrc/prose-packages/sysroot-arm64
├── out/<name>/         build trees + binaries
├── incflags.txt        the -isystem set for the cross compiler
└── bin/                stubs for guest-only tools (xres, mimeset, ...)
```

The sysroot and sources live on the case-sensitive build volume because
Haiku headers need case (`be/support/String.h` vs `<string.h>`).
The sysroot = `haiku_devel` + `gcc_syslibs_devel` + `haiku` (runtime) +
`gcc_syslibs` (runtime) hpkgs from our own build, extracted into one tree.

## Pipeline

```sh
scripts/pkg-build.sh Tipster        # cross-build for arm64 (makefile-engine apps)
scripts/pkg-install.sh Tipster      # bfs_shell-inject into an image copy +
                                    #   launch line in UserBootscript
scripts/run-qemu.sh /Volumes/xb/HaikuArmQemu/work/pkgtest/disk.img   # boot + eyeball
```

`pkg-build.sh` v0 handles the "Haiku Generic Makefile" layout: redirects the
engine include to our copy, stubs `xres`/`mimeset` (resources/icons not yet
merged — apps run with default icons), links `-lbe -lstdc++ -lgcc_s -lroot
-llocalestub`. Apps install to `system/non-packaged/apps/`, which packagefs
mirrors into `/boot/system/apps` at boot.

## Status

* Toolchain: proven — hello-Be-app + **Tipster** build as arm64 Haiku ELF
  from this Mac.
* Batch 1: 49/49 repos cloned (see CATALOG.md).
* To do: resources (host `rc`/`xres` from the Haiku tree), per-app fixes
  (jam-based projects, extra deps like libcolumnlistview/liblayout),
  screendump-verified run of each app, repo of builds for the future Prose
  installer.
