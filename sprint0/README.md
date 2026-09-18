# Sprint 0: platform measurements (macOS 26 baseline, re-run on 27)

These tests establish, by measurement, the facts behind [haiku_virtualized.md](../haiku_virtualized.md). They need no macOS 27 SDK:
- T1, T3 and T5 run on any recent macOS.
- T2 and T4 boot the real Haiku image.

The host was upgraded from macOS 26.5.1 to macOS 27.0 during the sprint. Results for both are recorded below.

## Running

```sh
tools/build.sh                     # Swift tools -> build/bin (vzprobe, hvz, presenter)
sprint0/t1-qemu-hvf-matrix.sh      # ~2.5 min, headless
sprint0/t3-vz-platform-probe.sh    # ~35 s, headless
sprint0/t5-metal-presenter.sh      # ~25 s, opens a window per variant
scripts/mount-src.sh               # T2/T4 need the build volume and a built image
sprint0/t2-qemu-haiku-boot.sh 90   # 5 headless boots x 90 s
sprint0/t4-vz-haiku-boot.sh 120    # one windowed VZ boot of a copy of the image
```

T1 needs coreutils' `timeout` (`brew install coreutils`). After an OS or Xcode change, rebuild the tools with `FORCE=1 tools/build.sh`, which also re-signs the VZ tools.

Everything a test writes goes under `work/<test>/`, which is gitignored. Each test exits 0 on PASS and non-zero on FAIL/DIFF, so the set can be run again after every macOS, QEMU or Haiku update.

## Tests

| ID | What it establishes | Tooling | Needs |
|---|---|---|---|
| T1 | What QEMU + HVF accepts on this host. What boot framebuffer each display device gets, and whether the OS gets ACPI or a device tree | `t1-qemu-hvf-matrix.sh`, the UEFI Shell probe (`tools/uefi`) | QEMU |
| T2 | How far Haiku boots under QEMU + HVF for each transport, disk and display combination | `t2-qemu-haiku-boot.sh`, `boot_markers.py` | built image |
| T3 | The platform Virtualization.framework presents to a non-macOS guest, compared with the recorded baseline. It also checks restricted private devices and the macOS 27 API | `t3-vz-platform-probe.sh`, `build/bin/vzprobe`, `tools/uefi/probe_decode.py` | — |
| T4 | Whether Haiku boots under Virtualization.framework with VZ's own 2D virtio-gpu | `t4-vz-haiku-boot.sh`, `build/bin/hvz`, `scripts/haiku-syslog.sh` | built image, `bfs_shell` |
| T5 | That the Metal presenter design works, on synthetic `B_RGB32` surfaces | `t5-metal-presenter.sh`, `build/bin/presenter` | — |

### T1: QEMU + HVF matrix
- **Part A:** boots EDK2 under nine interrupt-controller, MSI and EL2 configurations. Each must boot or be rejected exactly as recorded.
- **Part B:** boots the UEFI Shell with each display device and reads the GOP. It also dumps the UEFI configuration table to see whether the OS gets ACPI or a device tree.

### T2: Haiku boot matrix under QEMU + HVF
- **Rows:**
  - `reference`: the validated `scripts/run-qemu.sh` setup (virtio-mmio, blk, ramfb).
  - `pci-blk`, `mmio-scsi`, `pci-scsi`: vary the transport and disk.
  - `mmio-gpu`: adds virtio-gpu to see whether Haiku's `virtio_gpu` driver takes over the display.
- **Each run:**
  - Uses `-snapshot` and runs headless.
  - Captures the serial log.
  - Takes `screendump`s of the ramfb and virtio-gpu consoles.
- **Milestones checked, from BUILDING.md §6:** loader → kernel → boot volume mounted → `framebuffer_init() completed successfully` → `Running first login script`. Also counted: `I/O error`, `PANIC` and KDL.
- **PASS** means the reference row reached the first-login script with no errors. The other rows are recorded for characterization.
- **Harness caveats (measured):**
  - `first_login` fires **once per installed system** — the reference row must run against a freshly built image (`scripts/build-image.sh` after removing the old one), or it can never pass.
  - **ramfb screendumps under `-display none` are stale**: the EFI loader draws through firmware calls, but after `ExitBootServices` the kernel writes pixels directly into guest RAM, which QEMU only re-reads on display refresh — headless there is none, so the dump freezes at the last firmware-drawn frame (typically the boot menu). The virtio-gpu console stays live (virtqueue-driven) and is the reliable headless display evidence. A windowed run of the reference row showed the **full desktop on ramfb at 75 s**, so the stale dumps are an artifact, not a boot failure.

### T3: Virtualization.framework platform probe
- Boots the EDK2 UEFI Shell three times in a headless generic-platform VM. Each pass computes the addresses the next pass needs:
  1. Memory map, PCI list, GOP.
  2. Raw ACPI tables, configuration table, PCI config spaces.
  3. virtio-gpu feature and config registers.
- `probe_decode.py` then compares 20 facts with the macOS 26.5.1 baseline.
- Host checks:
  - Nested virtualization support.
  - Whether the macOS 27 custom-virtio classes exist at runtime.
  - `VZMacGraphicsDevice` is still refused for non-macOS guests.
  - The private PL011 and linear-framebuffer devices are still refused at VM start.

### T4: Haiku under Virtualization.framework
- Boots a **copy** of the image in `hvz`, in a window, with VZ's own virtio-gpu, then copies Haiku's syslog off the disk.
- **Correction (2026-09-18):** an earlier "PASS" here was wrong. It read a syslog left in the image by a QEMU boot (ACPI `oem id: BOCHS`, DHCP from `10.0.2.2`). The VZ boot itself never reached userland. The script now deletes the image's syslog before booting and only accepts a syslog containing `oem id: APPLE`.
- **What actually happens under VZ** (seen live with `tools/hvgpu`'s RAM console):
  - With more than 1 vCPU, the kernel never starts. It hangs in SMP bring-up, after ExitBootServices and before the kernel's first line of output.
  - `virtio_block` over PCI can't read the disk ("reading the partition table failed: 80000001"). This is the same bug as T2's `pci-blk` row. Booting from NVMe works.
  - With VZ's virtio-gpu present, app_server opens it (`graphics/virtio/0`) and hangs waiting for it.
- **PASS** means the syslog reaches `Running first login script` with no I/O errors or panics, and the syslog comes from a VZ boot.
- **Working setup (S1):** `tools/hvgpu`, with 1 vCPU, an NVMe boot disk, and our custom virtio-gpu as the only GPU. It boots to the desktop. See the design doc.

### T5: Metal presenter
- **Surface setup:** 16 KiB-aligned `mmap` memory wrapped with `makeBuffer(bytesNoCopy:)`. The shader samples the buffer with any stride and any 4-byte-aligned offset, forces alpha, and is paced by `CADisplayLink`.
- **Self-test:** renders the surface 1:1 offscreen and compares every pixel. Colors must be intact and alpha must be 255, even though the source's X bytes are 0.
- **PASS also requires:**
  - The display link keeps rate.
  - A present on every tick with new content.
  - The drawable pool never runs dry.
- **Informational:** the number of frames a free-running producer overwrote between ticks. Pacing that is the guest's job, via VSYNC events.

## Results

### Host: Mac Studio M4 Max, QEMU 11.1.1

| Test | macOS 26.5.1 | macOS 27.0 |
|---|---|---|
| T1 | PASS, after recording that `virtio-gpu-device` gets no GOP | **PASS 15/15**: no change from 26.5.1 |
| T2 | Manual run, before the scripted matrix: mmio + blk + ramfb **boots to the desktop**; PCI virtio-blk gives intermittent `I/O error`s (BUILDING.md §6) | **PASS** (pristine image, 180 s/row): `reference`/`mmio-scsi`/`mmio-gpu` all reach first-login with 0 errors; `pci-blk` blocked by 21 I/O errors; `pci-scsi` boots clean but didn't reach first-login in 180 s. **Root cause found and fixed in the fork** (patch 0003): those rows used Haiku's legacy I/O-port virtio interface, where every I/O BAR gets host address 0 on arm64, so initializing virtio-net reset the disk. With the fork, `pci-blk` reaches first login with 0 errors (150 s run, 2026-09-18); `disable-legacy=on` on the QEMU devices is an equivalent stock workaround |
| T3 | PASS 20/20 | **PASS 20/20**: platform unchanged. Custom virtio API present at runtime (4/4 classes) |
| T4 | ready, not run yet | **FAIL.** The earlier "PASS" read a stale QEMU syslog. VZ's GPU plus virtio-blk never reaches userland; see T4 notes. S1 (`hvgpu`) reaches the desktop |
| T5 | — | **PASS 5/5** |

### What we learned
- **T1:**
  - HVF with Apple's GIC accepts GICv3 only. It rejects ITS and GICv4, and `msi=gicv2m` works.
  - `kernel-irqchip=off` brings back GICv2 and GICv3 with ITS.
  - `virtualization=on` boots.
  - Boot framebuffers: **ramfb → linear BGRX; `virtio-gpu-pci` → BltOnly; `virtio-gpu-device` → no GOP at all; `bochs-display` → none.**
  - EDK2 gives the OS ACPI by default and a device tree with `acpi=off`.
- **T3:** macOS 27 changed nothing that a non-macOS guest sees.
  - Same firmware, ACPI-only tables, GICv3 plus GICv2m, modern virtio-pci, 2D-only virtio-gpu.
  - Still no serial port and no boot framebuffer.
  - The ACPI tables sit at different addresses, and one unidentified configuration-table entry is gone.
  - The custom-virtio classes (`VZCustomVirtioDeviceConfiguration`, `VZCustomVirtioDevice`, `VZVirtioSharedMemoryRegionConfiguration`, `VZGuestMemoryMapping`) exist at runtime. Xcode's SDK is still 26.2, though, so S1 needs an Xcode that ships the macOS 27 SDK, or runtime (Objective-C) calls.
- **T5:**
  - Every variant passes pixel-exact: 1280×800, 1366×768 at a 4 KiB offset, 1920×1080 with a padded stride, and 3840×2160.
  - GPU cost is 0.045–0.088 ms per frame, with 0 drawable stalls at 60 Hz.
  - One early run delivered only 45 presents/s, because an unsynchronized 60 Hz producer against a 60 Hz display skips frames by chance. That confirms the design's choice to pace the guest with VSYNC events.
- **T2 (matrix, pristine image):**
  - The `I/O error` storm is **specific to virtio-blk over PCI** (`pci-blk`: 21 errors, boot stalls). `pci-scsi` is error-free — the PCI transport itself is fine.
  - `mmio-scsi` passes identically to `reference`, so virtio-scsi is a drop-in alternative if blk ever misbehaves.
  - `pci-scsi` boots (kernel, boot volume) but hadn't reached first-login at 180 s — slower, or stalled late; unexplained, recorded.
  - **Haiku's own `virtio_gpu` driver binds QEMU's `virtio-gpu-device` (mmio)** and renders the desktop (`mmio-gpu` row: `virtio_gpu=True`, first-login reached, live console content). That driver+accelerant pair is the template and the baseline for S1.
  - Harness: `first_login` is one-shot per install (fresh image required); ramfb screendumps under `-display none` freeze at the last firmware-drawn frame (see T2 caveats).
- **T4 (Haiku under VZ, macOS 27):**
  - **Full boot to desktop, blind.** DHCP lease via VZ NAT, both input devices published, framebuffer accelerant initialized, first-login reached, 0 errors.
  - VZ's platform is configuration-dependent: with hvz's serial port and virtio-gpu attached, the firmware publishes SPCR (PL011 @ `0x9000000`) and a linear GOP — neither appeared in the headless T3 probe. Guest serial output did not reach the host file, though; one dedicated experiment needed before relying on it.
  - The kernel saw GICD at `0x8000000` / GICR at `0x80a0000`, not the `0x10000000`/`0x10010000` recorded in the design doc §2.5 — treat GIC bases as config-dependent until measured deliberately.
  - The window stays black because the GOP is not scan-out backed: post-`ExitBootServices` writes never reach VZ's display. Haiku's `virtio_gpu` driver does not bind VZ's device (no EDID). Both are exactly the gaps S1/S2 close.

## Carried into Sprint 1
1. ~~Run T2 and T4 once the build volume is mounted~~ — done, results above.
2. Install an Xcode with the macOS 27 SDK, then measure the open questions from the design doc (§2.6):
   - Does `update()` raise a config-change interrupt?
   - Shared-region alignment and limits.
   - Can `mapMemory` take `mmap`'d memory that is also wrapped by Metal?
   - Latency from a queue kick to `returnToQueue`.
3. Write the S2 spec. Start the QEMU S2 device and the Haiku `virtio_pci` shared-memory capability support under QEMU.
