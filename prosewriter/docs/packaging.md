# Packaging ProseWriter as a prose package

Notes for when ProseWriter is finished and becomes a package (per the
repo's own flow in `packages/README.md`). Nothing here is executed yet —
this is the hand-off plan.

## The moving parts

1. **prosepkg** builds `.hpkg` from haikuports-style recipes:
   `scripts/prosepkg build <port>` (it owns a private toolchain; it must
   never point at the main Haiku tree).
2. **Onto an image without a rebuild**: `scripts/prosepkg install <image>
   <package>` copies into `system/packages`; packagefs activates it at
   next boot. That is the quickest way to try ProseWriter on a real Prose
   image (including `private_workspace/run-qemu.sh` copies).
3. **Into the built image**: add the package to the tree's package list
   and `scripts/local-packages.sh` picks it up from
   `generated/download/` (patch 0033 flow), or list it in the
   `prose-*` build profile.
4. **Deskbar placement**: the Applications-menu folder comes from
   `build/jam/DeskbarCategories` (patches 0053/0055) — add ProseWriter's
   menu name there, or let `prosepkg install` leave it at top level.

## Recipe draft

A recipe in the builder's overlay (`builder/overlay/prose-tests` is the
existing example of a recipe of ours), roughly:

```sh
SUMMARY="A word processor for Prose"
DESCRIPTION="ProseWriter — a native word processor: styled text, pages, \
headers and footers, find/replace, RTF."
ARCHITECTURES="arm64"
HOMEPAGE="local"
COPYRIGHT="2026 Prose project contributors"
LICENSE="MIT"
SOURCE_URI="local: prosewriter-$portVersion.tar.xz"

BUILD()
{
	cd prosewriter-$portVersion/app
	make -j2 ProseWriter	# cross gcc + sysroot; see app/Makefile
}

INSTALL()
{
	mkdir -p $appsDir/ProseWriter
	cp app/ProseWriter $appsDir/ProseWriter/ProseWriter
	addResourcesToBinaries … rdef …		# app icon, signature, version
	addAppDeskbarSymlink $appsDir/ProseWriter/ProseWriter
}
```

Open items before this is real:

- An **rdef** with the app signature (`application/x-vnd.prose.ProseWriter`),
  an HVIF icon, version info, and the document MIME
  (`application/x-vnd.prose.ProseWriter-doc` with the `.prose` extension).
  The repo's `tools/artwork/hvif.py` can generate the icon; the host
  `rc`/`resattr` equivalents live in the prosepkg bootstrap's host tools.
- The build inside prosepkg needs the same sysroot treatment as
  `app/mksysroot.sh` — against prosepkg's own base sysroot instead of the
  main tree's extracted packages (keeps the non-interference rule).
- RTF import association (`text/rtf`) as a secondary handler, so
  "Open with… ProseWriter" works from Tracker.

## Until then

The dev loop stays: `make -C prosewriter/app ProseWriter` on the host,
`prosewriter/vm/guest.sh put … /boot/home/apps/ProseWriter` into the dev
image. `--selftest` (now 41 cases) is the gate; the flags `--set-header`,
`--set-footer`, `--paper`, `--landscape`, `--seed`, `--print` exist for
the harness and double as a demo mode.
