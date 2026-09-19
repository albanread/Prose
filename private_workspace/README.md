# private_workspace: build, run and test scripts

Historically Claude's separate checkout of the Haiku fork, so the main tree was
left alone. Since 2026-09-19 there is one Haiku tree and these scripts run
against it; the separate checkout is gone.

| What | Where |
|---|---|
| Haiku source | `/Volumes/HaikuSrc/haiku`, branch `prose` (hrev60122 + `patches/haiku/0001`–`0040` as one commit each, applied by `scripts/apply-patches.sh`) |
| Build output | its `generated/` |
| Image | `/Volumes/HaikuSrc/haiku/haiku-mmc.image` (`PW_IMAGE` overrides) |
| Run scratch (copies of the image, logs) | `private_workspace/work/` (gitignored) |

## Commands

```sh
source private_workspace/env.sh
private_workspace/build.sh               # = scripts/build-image.sh (patches applied, local packages, jam)
private_workspace/run-vz.sh smoke        # hvgpu, windowed, 8 vCPUs, NVMe, fresh copy in work/smoke
private_workspace/run-vz.sh t1 --headless --seconds 100 --disk virtio
private_workspace/run-vz.sh s2 --display s2 --screenshot private_workspace/work/s2/shot.png   # S2 Prose Display instead of the S1 virtio-gpu
private_workspace/run-vz.sh s2 --display s2 --resize-after 45 1600x1000   # live-resize test: the guest desktop follows the window
private_workspace/run-vz.sh in --display s2 --input-test   # our own keyboard/tablet (default); --input vz restores VZ's devices + view
PW_INJECT_SCRIPT=private_workspace/netprobe.sh private_workspace/run-vz.sh net --headless --seconds 80   # guest-side network probe
private_workspace/extract.sh net /home/netprobe.txt   # ... and its result (any file from a run's image copy)
PW_INJECT_SCRIPT=private_workspace/soundprobe.sh private_workspace/run-vz.sh snd --seconds 70   # plays a 440 Hz tone through the Mac at ~30 s
PW_INJECT_SCRIPT=private_workspace/midiprobe.sh private_workspace/run-vz.sh midi --seconds 75 --midi-log   # a C-major scale via the Midi Kit, then direct, through the Mac's GM synth
PW_INJECT_SCRIPT=private_workspace/soundprobe.sh private_workspace/run-qemu.sh --headless --copy --sound wav --seconds 100 && python3 private_workspace/wavcheck.py private_workspace/work/qemu/out.wav
PW_SHARE=~/Projects private_workspace/run-vz.sh work   # HostFS: that Mac folder as the disk /HostFS (default ~/Documents/HostFS; PW_SHARE= for none)
private_workspace/run-vz.sh work --share ~/a --share-ro ~/b   # several folders: /HostFS/a, /HostFS/b
private_workspace/hostfs-test.sh         # HostFS end to end: scratch share + hostfsprobe.sh, both views compared
private_workspace/trap-test.sh [qemu|vz] # user-mode exceptions -> signals (patch 0029): trap_check.c run at boot, plus what debug_server logged
private_workspace/run-qemu.sh            # QEMU + HVF, modern virtio-pci, ramfb, -snapshot
private_workspace/syslog.sh smoke        # copy Haiku's syslog out of work/smoke/haiku.img
```

`run-vz.sh` always boots a **fresh copy** of the image, so every run starts from first boot and `first login` is a reliable marker.

## Rules
- Change Haiku as commits on `prose`; export each with `scripts/export-patch.sh` as the next `patches/haiku/NNNN-*.patch`, so main carries it. The build refuses a tree with uncommitted changes.
- The image is only ever built by `scripts/build-image.sh` (or `build.sh`, which runs it).

## Local packages (no remote resolution)

The Haiku build normally downloads third-party packages from
`eu.hpkg.haiku-os.org`; the list in `build/jam/repositories/HaikuPorts/arm64` is
checksummed and that checksum picks a server-side snapshot, so you cannot add a
locally built package to it (the build then fetches a snapshot that does not
exist and fails with a 404).

The `prose` build uses local packages instead:

1. `generated/build/BuildConfig` has `HAIKU_NO_DOWNLOADS ?= "1" ;` (not tracked -
   re-running `configure` resets it, so check it after). The build then
   synthesises the repository from the `.hpkg` files in `generated/download/`
   and only offers packages whose files are actually there.
2. Put the package in `generated/download/` and list it in
   `build/jam/repositories/HaikuPorts/arm64`.
3. Packages must carry the vendor the repository expects (`Haiku Project`) -
   `package_repo` rejects anything else. prose-packages builds say `Prose`, so
   rewrite it first:

```bash
P=/Volumes/HaikuSrc/haiku/generated/objects/darwin/arm64/release/tools/package/package
export DYLD_LIBRARY_PATH=/Volumes/HaikuSrc/haiku/generated/objects/darwin/lib
mkdir -p /tmp/fix && cd /tmp/fix && $P extract <pkg>.hpkg
sed -i '' 's/^vendor.*$/vendor\t\t"Haiku Project"/' .PackageInfo
$P create -q /Volumes/HaikuSrc/haiku/generated/download/<pkg>.hpkg
```

4. To install a package into the image (not just make it available to the
   build), add it to `build/jam/UserBuildConfig` with
   `AddHaikuImageSystemPackages <name> ;`.

`scripts/local-packages.sh <tree>` does steps 1 and 3 and the copy of step 2 for
every package the tree lists beyond upstream's list. `build.sh` and
`scripts/build-image.sh` run it before jam. A package prosepkg has rebuilt is
copied again, and one that is unchanged is left alone. The image carries the
whole codec set this way (patch 0040). To add a package:
`private_workspace/addpkg.sh <name>` prints its lines for the list; then list it
in `UserBuildConfig` and build:

```bash
private_workspace/build.sh
packages/boot-test.sh packages/tests/codecs.sh codec_check   # 19/19 on the image's own packages
```
