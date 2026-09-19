# Friday evening — how to build all of this from scratch

A runbook. Someone with a clean Apple Silicon Mac should be able to follow it
top to bottom and end up with Haiku running on Virtualization.framework with
our display, input, network, sound and MIDI.

It assumes nothing except the hardware. Everything that bit us is called out
where it bites, not at the end.

---

## 0. The machine, and why macOS 27 matters

Built and tested on a Mac Studio M4 Max, 36 GB, **macOS 27** with **Xcode 27**
(for the macOS 27 SDK). Also runs on QEMU 11.x with HVF.

**The OS upgrade is not optional for the good path.** The display, input and
MIDI devices are `VZCustomVirtioDevice` — Apple's API for implementing your own
virtio device inside a VM — and that class only exists in the macOS 27 SDK.
On macOS 26 you can still build and run the QEMU side and Haiku itself, but
none of the host integration.

What the upgrade gives us:

| API | Used for |
|---|---|
| `VZCustomVirtioDeviceConfiguration` / `VZCustomVirtioDevice` | our display (ID 63), keyboard and tablet (ID 18), MIDI (ID 62) |
| `VZVirtioSharedMemoryRegionConfiguration` + `mapMemory` | the surface pool the guest draws into |
| `VZVirtioSoundDeviceConfiguration` | sound to the Mac's output |

After upgrading, check the SDK is really there before anything else:

```bash
ls "$(xcrun --show-sdk-path)/System/Library/Frameworks/Virtualization.framework/Headers" | grep Custom
# VZCustomVirtioDevice.h, VZCustomVirtioDeviceConfiguration.h, ...
```

If that is empty, `xcode-select` is pointing at the old toolchain.

---

## 1. A case-sensitive volume (Haiku will not build without one)

Haiku's tree contains files differing only by case, and every stock macOS
volume is case-insensitive. Make a sparse bundle once:

```bash
hdiutil create -size 120g -type SPARSEBUNDLE -fs "Case-sensitive APFS" \
    -volname HaikuSrc /Volumes/xb/HaikuArmQemu/HaikuBuild.sparsebundle
hdiutil attach /Volumes/xb/HaikuArmQemu/HaikuBuild.sparsebundle   # -> /Volumes/HaikuSrc
```

It does **not** survive a reboot mounted. After every reboot:

```bash
scripts/mount-src.sh
```

---

## 2. Host prerequisites

```bash
brew install texinfo expat gawk gettext libiconv gsed cdrtools nasm wget \
             mpfr gmp libmpc bison u-boot-tools mtools zstd qemu
```

`mpfr gmp libmpc` are needed to build the cross-GCC, `u-boot-tools` provides
`mkimage`. `bison` and `gettext` are keg-only and **must precede** the ancient
system copies on `PATH`, or the build fails in confusing ways:

```bash
export PATH="$HOME/bin:/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/gettext/bin:$PATH"
```

Then Haiku's build tool, into `~/bin` (no sudo):

```bash
cd /Volumes/HaikuSrc/buildtools/jam
make -j$(sysctl -n hw.ncpu)
./jam0 -sBINDIR="$HOME/bin" install
```

---

## 3. Sources — and the tags, which are not optional

```bash
git clone https://github.com/haiku/haiku      /Volumes/HaikuSrc/haiku
git clone https://github.com/haiku/buildtools /Volumes/HaikuSrc/buildtools
```

The GitHub mirror has **no `hrev*` tags**, and the build fails late with "you
are using a Haiku clone without tags". Fetch them from Gerrit:

```bash
cd /Volumes/HaikuSrc/haiku
git remote add gerrit https://review.haiku-os.org/haiku.git
git fetch gerrit --tags        # ~58k tags
```

---

## 4. Configure and build the cross-toolchain

```bash
cd /Volumes/HaikuSrc/haiku
./configure -j14 --cross-tools-source ../buildtools --build-cross-tools arm64
```

15–40 minutes. Note the flag form: the older `--build-cross-tools arm64
../buildtools` is rejected by current configure.

---

## 5. The fork: a separate worktree

Do **not** develop in the main tree — the package work lives there. Use a git
worktree so both can coexist:

```bash
cd /Volumes/HaikuSrc/haiku
git worktree add /Volumes/HaikuSrc/private_workspace/haiku -b vz-fork
cd /Volumes/HaikuSrc/private_workspace/haiku
./configure -j14 --cross-tools-prefix \
    /Volumes/HaikuSrc/haiku/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-
```

The worktree reuses the main tree's cross-tools read-only. A `git clean` or
reconfigure in the main tree will break this workspace's build.

Apply the fork patches (0009 onward are `git format-patch` output):

```bash
cd /Volumes/HaikuSrc/private_workspace/haiku
git am /Volumes/xb/HaikuArmQemu/patches/haiku/00*.patch
```

`patches/haiku/README.md` says what each one is and why. In short: 0001–0008
are bring-up and diagnostics, 0009–0013 shutdown/clock/power-button/interrupts,
0014–0020 the display, 0021 networking, 0022 sound, 0023 MIDI.

---

## 6. Local packages — turn off remote resolution first

Do this **before** the first image build if you want image formats, sound
translations or anything else from a package.

The build fetches third-party packages from `eu.hpkg.haiku-os.org`. The
package list in `build/jam/repositories/HaikuPorts/arm64` is checksummed, and
that checksum picks a server-side snapshot — so adding a locally built package
makes the build fetch a snapshot that does not exist (404). The supported way
out is to stop fetching entirely:

```bash
# in the worktree, after configure (configure resets this - check it again after any reconfigure)
sed -i '' 's/^HAIKU_NO_DOWNLOADS.*$/HAIKU_NO_DOWNLOADS\t\t\t?= "1" ;/' generated/build/BuildConfig
```

The build then synthesises the repository from the `.hpkg` files in
`generated/download/` and offers only packages whose files are actually there.
Everything already downloaded stays available.

To add one of your own packages:

```bash
private_workspace/addpkg.sh libjpeg_turbo tiff libwebp giflib
```

That finds each package and its `_devel`, **rewrites the vendor** to
`Haiku Project` (`package_repo` rejects any other vendor outright — this will
waste your afternoon if you don't know), writes them to `generated/download/`,
and prints the lines to paste into
`build/jam/repositories/HaikuPorts/arm64`.

A package only becomes *available to the build* that way. To get it **into the
image** as well, add it to `build/jam/UserBuildConfig`:

```jam
AddHaikuImageSystemPackages libpng16 libjpeg_turbo tiff libwebp giflib ;
```

---

## 7. Build the image

```bash
private_workspace/build.sh        # jam -q -j<ncpu> @minimum-mmc in the worktree
```

Output is `/Volumes/HaikuSrc/private_workspace/haiku/haiku-mmc.image` — in the
source root, **not** `generated/`: the MMC image target isn't `MakeLocate`d.
336 MB: MBR, a 32 MiB ESP with `EFI/BOOT/BOOTAA64.EFI`, and a BFS partition.

Use the **minimum** profile. The regular profile wants 400+ HaikuPorts
packages that only exist for x86_64, and dies on `don't know how to make
libmidi.so` (the MIDI kit needs `fluidlite`).

---

## 8. Build the host tools

```bash
/Volumes/xb/HaikuArmQemu/tools/build.sh      # FORCE=1 to rebuild everything
```

Produces `build/bin/hvgpu` (and `hvz`, `vzprobe`, `presenter`). Tools that use
Virtualization.framework are **ad-hoc signed with the
`com.apple.security.virtualization` entitlement** — without it the VM refuses
to start. The deployment target is pinned (`-target arm64-apple-macos27.0` for
hvgpu) because the host compiler otherwise targets the running OS.

---

## 9. Run it

```bash
private_workspace/run-vz.sh desktop --keep
```

- The Prose Display (S2) is the default. `--display s1` selects the older
  virtio-gpu impersonation instead — a fixed scanout, no live resize; keep it
  for bisecting display problems, not for use.
- `--keep` — **reuse this run's disk**, so preferences and files survive.
  Without it every run starts from a pristine copy, which is what the tests
  want but means nothing you do in the guest persists. It also means Tracker's
  default desktop background, which is written at first boot and only renders
  from the *next* boot, never appears.
- Input is ours by default (`--input own`); `--input vz` restores VZ's keyboard,
  pointing device and view.
- Closing the window presses the guest's power button: clean shutdown in ~1 s.

QEMU, for comparison and for the non-Apple path:

```bash
private_workspace/run-qemu.sh                     # HVF, modern virtio-pci, ramfb
private_workspace/run-qemu.sh --sound wav --copy --seconds 100
```

---

## 10. Verify each piece

Each of these runs unattended and tells you whether a subsystem is actually
working, rather than merely present.

```bash
# display: mode set, commits, vsync, and a PNG of what is on screen
private_workspace/run-vz.sh s2 --screenshot private_workspace/work/s2/shot.png

# live resize: 40 hints should produce exactly one mode switch
private_workspace/run-vz.sh s2 --resize-drag 30 1000x700

# input: clicks the Deskbar leaf, presses Escape, parks the pointer
private_workspace/run-vz.sh in --input-test

# networking: DHCP, gateway, internet, DNS, a 1400-byte ping
PW_INJECT_SCRIPT=private_workspace/netprobe.sh private_workspace/run-vz.sh net --headless --seconds 80
private_workspace/extract.sh net /home/netprobe.txt

# sound: a 440 Hz tone through the Mac
PW_INJECT_SCRIPT=private_workspace/soundprobe.sh private_workspace/run-vz.sh snd --seconds 70

# sound, measured: record it out of QEMU and check level and pitch
PW_INJECT_SCRIPT=private_workspace/soundprobe.sh private_workspace/run-qemu.sh --headless --copy --sound wav --seconds 100
python3 private_workspace/wavcheck.py private_workspace/work/qemu/out.wav

# MIDI: a scale through the Midi Kit and then straight into the port
PW_INJECT_SCRIPT=private_workspace/midiprobe.sh private_workspace/run-vz.sh midi --seconds 75 --midi-log
```

**One caveat that cost me an hour and a wrong conclusion:** a headless
`--screenshot` dumps the *surface pool*. With no window, no display link and no
input, the desktop never repaints, so you photograph a stale state and think a
feature is broken. **Check anything visual in a windowed run.**

---

## 11. When it breaks

- **There is no serial port under Virtualization.framework.** `hvgpu` scans
  guest RAM once a second for the loader log and the kernel's RAM log
  (`HAIKU-RAMLOG-V1`, fork patch 0001) and writes it to
  `work/<name>/ramconsole.log`. For crash hunting put `snooze(1300000)` between
  suspect steps so the last lines get flushed.
- **Userland logs are not in there.** `private_workspace/syslog.sh <name>`
  copies Haiku's syslog out of the run's image; `extract.sh <name> <path>` pulls
  any file out.
- **Run a script inside the guest at boot:** `PW_INJECT_SCRIPT=<file>` installs
  it as the guest's `UserBootscript`. Plain `sh` only — the minimum image has
  no `grep` or `sed`.
- QEMU prints `virtio_error` to its own stderr, and `run-qemu.sh` exposes a
  monitor socket (`system_powerdown`).

---

## 12. Gotchas, collected

Things that look like your bug and are not.

- **Custom virtio device IDs must be ≤ 63.** The PCI device ID is
  `0x1040 + ID`; 64 lands outside the modern range `virtio_pci` accepts and the
  device is simply invisible.
- **A virtio child's driver is searched once**, at node registration, in the
  context of whichever `/dev/<class>` scan triggered it. Our MIDI driver
  therefore lives under `drivers/audio/hmulti` even though it publishes
  `/dev/midi/prose/0`.
- **`CADisplayLink` stops when the Mac's display sleeps.** "0 vsyncs" is
  usually a sleeping display. `caffeinate -u -t 120` while testing.
- **`VZVirtioSharedMemoryRegion.mapMemory` must be called on the device queue,
  and only once the VM is running** — before `start` completes it fails with
  "The virtual machine is not live", and off-queue it trips
  `dispatch_assert_queue`.
- **`NSWindow.delegate` must be set explicitly** or `windowDidResize` and
  `windowShouldClose` never fire.
- **arm64 Haiku cannot map an area kernel-RW/user-RO** — shared-info areas are
  kernel-only plus `B_CLONEABLE_AREA`, and the driver clones into the caller
  with `vm_clone_area(..., kernel = true)`, which preserves the memory type.
- **User space may not acquire kernel-owned semaphores.** Hand the semaphore to
  the opening team at `open()` (the intel_extreme/nvidia pattern); it dies with
  that team, so recreate it on the next open.
- **VZ's PL061 model terminates the VM** on register writes it does not expect
  (`AFSEL`, or `IC` bits of unconfigured pins). Write only the registers Linux's
  pl061 driver touches, and only the event pins' bits.
- **The JPEG translator's catalog pass fails** on a missing `jpeglib.h` — that
  pass does not get build-feature headers. The translator itself builds and
  links; only its localisations are missing.

---

## 13. What you end up with

A Haiku desktop on Apple Silicon with:

- a zero-copy, tear-free display that follows the host window and shows
  analogue TV static before the guest paints;
- keyboard and mouse from our own virtio devices, with the window entirely ours;
- networking, sound through the Mac, and MIDI played by Core Audio's synth;
- 14 image translators;
- and an About box that says plainly what it is and credits the people whose
  operating system this is.

`saturday_all_day.md` covers what was built and what remains. This file is
how to get there.
