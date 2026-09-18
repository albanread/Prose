# Building the Haiku ARM64 image (on Apple Silicon macOS)

How we go from zero to a bootable `haiku-mmc.image` that runs under QEMU/HVF
at native speed. Canonical upstream reference: `ReadMe.Compiling.md` in the
Haiku source tree and <https://www.haiku-os.org/guides/building/>.

## Status on this machine

| Step | Status |
|---|---|
| Case-sensitive build volume | done (`/Volumes/HaikuSrc`) |
| haiku + buildtools cloned (+ Gerrit tags) | done (2026-09-18, master @ hrev60122) |
| brew prerequisites + jam | done |
| configure + arm64 cross-tools (GCC 13.3) | done |
| `jam @minimum-mmc` (source-built image) | done — `haiku/haiku-mmc.image`, 336 MB, ESP + BFS verified |
| `jam haiku-mmc.image` (full desktop) | blocked: arm64 HaikuPorts packages unavailable locally; needs bootstrap (see §5) |
| QEMU/HVF boot | draft command in `scripts/run-qemu.sh`, not yet validated |

## 1. Why the odd layout: case sensitivity

Haiku's source tree contains files that differ only by case, so it **cannot
check out or build on a case-insensitive filesystem** — and every stock macOS
volume (including `/Volumes/xb`) is case-insensitive.

Solution: a grow-on-demand APFS **case-sensitive** sparse bundle stored inside
this repo, mounted at `/Volumes/HaikuSrc`. The repo's `src` symlink points at
the mount (both are gitignored).

```
/Volumes/xb/HaikuArmQemu/           this repo (docs + scripts, git)
├── manifest.md
├── BUILDING.md                     you are here
├── scripts/                        mount / build / run helpers
├── HaikuBuild.sparsebundle         120 GB grow-on-demand disk image (gitignored)
└── src -> /Volumes/HaikuSrc        mountpoint symlink (gitignored)

/Volumes/HaikuSrc/                  case-sensitive build volume
├── haiku/                          Haiku sources
│   └── generated/                  build output; image lands here
└── buildtools/                     cross-compiler sources (gcc, binutils, jam…)
```

The volume does not survive a reboot mounted. After each reboot run:

```sh
scripts/mount-src.sh        # hdiutil attach + disable Spotlight on it
```

Creating it from scratch (only needed once; already done):

```sh
hdiutil create -size 120g -type SPARSEBUNDLE -fs "Case-sensitive APFS" \
    -volname HaikuSrc /Volumes/xb/HaikuArmQemu/HaikuBuild.sparsebundle
hdiutil attach /Volumes/xb/HaikuArmQemu/HaikuBuild.sparsebundle   # -> /Volumes/HaikuSrc
```

## 2. One-time host setup

* **Xcode Command Line Tools** — provides clang, make, bison, flex (Apple's
  clang 17 builds the cross-toolchain fine).
* **Homebrew packages** (per `ReadMe.Compiling.md` + what configure needed):

  ```sh
  brew install texinfo expat gawk gettext libiconv gsed cdrtools nasm wget \
               mpfr gmp libmpc bison u-boot-tools mtools zstd
  ```

  * `mpfr gmp libmpc` — GMP/MPFR/MPC libs needed to build the cross-GCC.
  * `u-boot-tools` — provides `mkimage`, required for ARM images.
  * `bison`, `gettext` are keg-only; the build script puts their keg paths on
    `PATH` ahead of the ancient system ones.
* **jam** — Haiku's build tool, built from buildtools and installed to
  `~/bin` (no sudo):

  ```sh
  cd /Volumes/HaikuSrc/buildtools/jam
  make -j$(sysctl -n hw.ncpu)
  ./jam0 -sBINDIR="$HOME/bin" install
  ```

## 3. Getting the sources

Already cloned from the GitHub mirrors:

```sh
git clone https://github.com/haiku/haiku       /Volumes/HaikuSrc/haiku
git clone https://github.com/haiku/buildtools  /Volumes/HaikuSrc/buildtools
```

**Important:** the GitHub mirror carries no `hrev*` tags, and the build fails
late ("you are using a Haiku clone without tags") without them. Fetch the tags
from Haiku's Gerrit (done here; also adds the `gerrit` remote for later
contributions):

```sh
cd /Volumes/HaikuSrc/haiku
git remote add gerrit https://review.haiku-os.org/haiku.git
git fetch gerrit --tags      # ~58k tags; HEAD then describes as hrev<NNNNN>
```

## 4. Configure + build the arm64 cross-toolchain

One command; it configures the build and compiles binutils + GCC for the
`arm64` target into `haiku/generated/cross-tools`. Takes roughly 15–40 min on
an M-series with `-j14`. **No bootstrap build is needed** — arm64 release
packages exist, and the image build downloads them automatically.

```sh
export PATH="$HOME/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/gettext/bin:$PATH"
cd /Volumes/HaikuSrc/haiku
./configure -j14 --cross-tools-source ../buildtools --build-cross-tools arm64
```

Notes:

* The trailing-argument form `--build-cross-tools arm64 ../buildtools` that
  older guides show is **rejected by current configure** ("Invalid argument")
  — use `--cross-tools-source`.
* Re-running the same `configure` command after `git pull` is safe and quick
  once cross-tools exist.

## 5. Build the image

```sh
scripts/build-image.sh              # default: @minimum-mmc
# which is: cd /Volumes/HaikuSrc/haiku && jam -q -j14 @minimum-mmc
```

* Output: `/Volumes/HaikuSrc/haiku/haiku-mmc.image` — **in the haiku source
  root, not `generated/`**: the MMC image target isn't `MakeLocate`d, so jam
  writes it to its working directory. It also keeps the default file name
  even under the `@minimum-mmc` profile.
* Structure (verified): MBR with a 32 MiB EFI System Partition containing
  `EFI/BOOT/BOOTAA64.EFI` (edk2 auto-boots this) + a 300 MiB BFS partition.
* The **minimum** profile builds everything from source and needs no binary
  HaikuPorts packages — this is the supported local target for arm64 today,
  and the fast dev loop for kernel/driver work (`jam` is incremental).

### Why not the full desktop image (`haiku-mmc.image`)?

Current master only ships a full in-tree HaikuPorts package manifest for
x86_64 (`build/jam/repositories/HaikuPorts/x86_64` lists 400+ packages; the
`arm64` file lists only bootstrap tools). A regular arm64 image therefore
fails:

* dozens of `AddHaikuImagePackages: package ... not available!` warnings, and
* a fatal `don't know how to make libmidi.so` — the MIDI kit needs the
  `fluidlite` build feature, which is provided by HaikuPorts packages that
  are unavailable for arm64 local builds.

The full arm64 package set is produced by Haiku's buildmaster via a
**bootstrap build** (`--bootstrap` with haikuporter/haikuports trees, then
`jam -q @bootstrap-raw`) — hours of compilation, from-source everything.
Getting a full desktop image locally is queued in the roadmap; until then,
`@minimum-mmc` is our baseline.

## 6. Run it under QEMU on Apple Silicon (HVF = native speed)

```sh
brew install qemu          # ships UEFI firmware for aarch64
scripts/run-qemu.sh
```

Which expands to:

```sh
qemu-system-aarch64 \
    -machine virt -cpu host -accel hvf \
    -smp 8 -m 4G \
    -bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
    -drive if=none,file=.../haiku-mmc.image,format=raw,id=hd0 \
    -device virtio-blk-pci,drive=hd0 \
    -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
    -device virtio-gpu-pci \
    -device virtio-keyboard-pci -device virtio-tablet-pci \
    -serial stdio
```

`-accel hvf` runs the guest CPUs natively via the Hypervisor framework — no
emulation. Device choices to validate on first boot (swap-ins if something
misbehaves):

* display: `virtio-gpu-pci` (Haiku has a virtio-gpu driver + accelerant) —
  fallback `-device ramfb` (firmware framebuffer);
* disk: `virtio-blk-pci` — fallback `-device nvme,drive=hd0`;
* input: `virtio-keyboard-pci`/`virtio-tablet-pci` — fallback `-device usb-kbd -device usb-tablet`.

## 7. Day-to-day update cycle

```sh
scripts/mount-src.sh
cd /Volumes/HaikuSrc/haiku      && git pull
cd /Volumes/HaikuSrc/buildtools && git pull
cd /Volumes/HaikuSrc/haiku
./configure -j14 --cross-tools-source ../buildtools --build-cross-tools arm64  # re-run, quick
scripts/build-image.sh
```

Rebuilding after touching kernel/driver sources only needs `jam` — it is
incremental. To force-rebuild one component: `jam -qa <Target>`.

## 8. Troubleshooting

* **"case-sensitive" or weird "duplicate file" checkout errors** — you are
  building on `/Volumes/xb` instead of `/Volumes/HaikuSrc`. Run
  `scripts/mount-src.sh` and build from `/Volumes/HaikuSrc/haiku`.
* **`makeinfo: command not found` building cross-GCC** — `brew install texinfo`.
* **`mkimage not found`** — `brew install u-boot-tools`.
* **bison "too old" / linker complaints about gettext symbols** — ensure the
  keg-only paths are on `PATH` as in §4.
* **jam: too many open files** — `ulimit -n 1024` (build-image.sh does this).
* **configure "Invalid argument: ../buildtools"** — old syntax; see §4.
* **"you are using a Haiku clone without tags"** — GitHub mirror has no
  `hrev*` tags; `git fetch gerrit --tags` (see §3), then re-run jam.
* **Kernel/boot bring-up debugging** — a minimal image boots much faster:
  `jam -q @minimum-mmc` (produces `haiku-minimal-mmc.image`).
