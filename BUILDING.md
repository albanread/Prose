# Building the Haiku ARM64 image (on Apple Silicon macOS)

How we go from zero to a bootable `haiku-mmc.image` that runs under QEMU/HVF
at native speed. Canonical upstream reference: `ReadMe.Compiling.md` in the
Haiku source tree and <https://www.haiku-os.org/guides/building/>.

## Status on this machine

| Step | Status |
|---|---|
| Case-sensitive build volume | done (`/Volumes/HaikuSrc`) |
| haiku + buildtools cloned | done (2026-09-18, master) |
| brew prerequisites + jam | done |
| configure + arm64 cross-tools | done |
| `jam haiku-mmc.image` | documented below |
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

Canonical development happens on Haiku's Gerrit (`review.haiku-os.org`); the
GitHub repos are mirrors. To later push branches for review:

```sh
cd /Volumes/HaikuSrc/haiku
git remote add gerrit https://review.haiku-os.org/haiku.git
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
scripts/build-image.sh
# which is: cd /Volumes/HaikuSrc/haiku && jam -q -j14 haiku-mmc.image
```

* Output: `/Volumes/HaikuSrc/haiku/generated/haiku-mmc.image` (raw disk image
  with an EFI system partition containing the Haiku UEFI loader — for arm64 no
  u-boot stage is wrapped in).
* The **first** run downloads a few hundred MB of prebuilt packages from
  Haiku's repositories — network required.
* Expect 10–30 min on 14 cores after packages are cached.

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
