# Haiku Virtualized: Design and Recommendations

*Status: 2026-09-18. Host of record: Mac Studio (M4 Max, 36 GB), macOS 26.5.1, QEMU 11.1.1 (Homebrew). Haiku tree: `/Volumes/HaikuSrc/haiku` at `55d56e03a0` (2026-09-17).*

This document records what we measured and researched, the architecture we chose, and the order we intend to build it in. It implements the goals in [manifest.md](manifest.md). Tests that can run before macOS 27 are in [sprint0/README.md](sprint0/README.md).

Tags: **[measured]** means we ran it on the host of record. **[source]** means we read it in the Haiku or QEMU source tree. **[docs]** means it comes from vendor documentation but hasn't been tested yet. **[reported]** means it is secondhand.

---

## 0. Status: S1 works on macOS 27 (2026-09-18)

Haiku arm64 boots to the desktop under Apple's Virtualization.framework, with keyboard and mouse. It displays through **our** virtio-gpu, a macOS 27 `VZCustomVirtioDevice` in `tools/hvgpu`, presented with Metal. Haiku's stock `virtio_gpu` driver and accelerant drive it.
- **Run:** `build/bin/hvgpu <image-copy> --cpus 8 --disk nvme` (VZ writes to the disk, so boot a copy). SMP works; 2, 4 and 8 vCPUs were tested.
- **RAM console:** VZ gives the guest no UART, so `hvgpu` reads Haiku's logs straight out of guest RAM through `VZGuestMemoryMapping` and prints them as `RAM|` lines.
  - It finds the loader's log buffer.
  - It finds the kernel RAM log (patch 0001), which holds all kernel debug output, including panics and KDL.
- **Our Haiku fork** (`patches/haiku/`, against hrev60122):
  1. `0001`: the kernel keeps a RAM copy of its debug output, tagged `HAIKU-RAMLOG-V1`. It must be `volatile`/`used`, or GCC drops it.
  2. `0002`: `virtio_gpu` skips GPUs without EDID. VZ's own virtio-gpu has none, and app_server hangs on it. Keeping VZ's GPU attached is still necessary, because VZ's absolute pointer maps onto it.
  3. `0003`: `virtio_pci` modern-interface fixes:
     - The driver's accepted features are written to `driver_feature`, not the read-only `device_feature`. Haiku never told a modern device what it accepted. This was the virtio-blk failure under VZ: VZ's devices refuse to operate without `VERSION_1`.
     - Transitional devices with modern capabilities use the modern interface, as Linux does. The legacy path uses I/O ports, and on arm64 Haiku's PCI layer gives every I/O BAR host address 0, so all legacy devices aliased one another: initializing virtio-net reset the disk. That was QEMU's `pci-blk`/`pci-scsi` failure.
     - Capabilities are read by `cap_len`. Reading by `length` left `notify_off_multiplier` as garbage on VZ, which caused the `virtio_net` panics.
     - Notify offsets start as "unset", and notifies are range- and magic-checked.
     - 64-bit common-config fields are written as two 32-bit halves, per the spec.
     - Interrupts that find ISR 0 are counted.
  7. `0007`, `0008`: diagnostics. Failed block requests and every feature negotiation are logged.
  4. `0004`: the loader numbers CPUs in MADT order, with the boot CPU as 0. VZ reports GICC interface number 0 for every CPU.
  5. `0005`: the loader's PSCI calls follow SMCCC (x0–x3 in/out, x4–x17 clobbered) and log each `CPU_ON` result.
  6. `0006`: the loader logs its handoff to the kernel.
- **`hvgpu` fixes on the way:**
  - Correct virtio-gpu command IDs, little-endian headers, and the echoed fence.
  - EDID is feature bit 1.
  - The transfer offset follows the spec.
  - The Metal overlay sits above VZ's view, and the display link is driven by a visible view.
- **SMP fixed** (patches 0003–0005). The multi-vCPU hang had two causes:
  - VZ's MADT gives every CPU GICC interface number 0, so the loader never started the other CPUs, and the kernel's rendezvous waited for them forever.
  - Once they did start, a `virtio_pci` bug (capabilities read by `length`, not `cap_len`) left `notify_off_multiplier` as garbage on VZ's small notify regions. `virtio_net` then panicked writing to random kernel addresses.
- **virtio over PCI fixed** (patch 0003): virtio-blk boots under VZ, and QEMU's default `virtio-*-pci` devices boot on arm64 with no I/O errors. Both failures traced to `virtio_pci`, not to descriptor chains: Haiku's chains are spec-correct and VZ's custom-device framework consumes them unchanged.
- **Sprint 1 items landed (patches 0009–0013, branch `vz-fork` in the private worktree):**
  - Clean shutdown and reboot: PSCI `SYSTEM_OFF`/`SYSTEM_RESET` (0009). A VZ stop request or QEMU `system_powerdown` now shuts Haiku down in about a second: the power button arrives as an ACPI GPIO event on VZ's PL061 (0011) or through QEMU's Generic Event Device (0013), reaches `acpi_button` (now built for arm64) and `power_daemon`.
  - Wall clock: UEFI `GetTime()` captured by the loader, RTC emulated from it (0010).
  - Per-interrupt trigger configuration in the GICv3 driver (0012); QEMU's GED line is edge-triggered and pulsed.
  - VZ quirk worth remembering: its PL061 model terminates the VM ("stopped unexpectedly") on register writes it does not expect, e.g. `AFSEL` or `IC` bits for unconfigured pins. Write only what Linux's `pl061` driver writes.
- **Remaining:**
  - app_server hangs on VZ's own virtio-gpu (handled by patch 0002). Retested with the feature fix: Haiku's driver negotiates `VERSION_1`, then its first command never completes. Cause unknown; low priority, since our device is the display.
  - Haiku's legacy virtio-pci path (I/O ports) is still broken on arm64. It's now unused, because transitional devices take the modern path.
- **Retracted:** Sprint 0's first T4 "PASS" read a syslog a QEMU boot had left in the image. T4 now deletes the image's syslog first and requires `oem id: APPLE`.

---

## 1. Decisions

1. **Development baseline: QEMU + HVF.**
   - It boots Haiku arm64 today.
   - It has a GDB stub and a PL011 early serial console.
   - It supports `-snapshot` and gives full control over the device model.
   - The same `virt` machine runs under WHPX on Windows on ARM.
2. **Daily driver on the Mac, after the macOS 27 upgrade: our own Virtualization.framework (VZ) app with a custom virtio display device.**
   - macOS 27 adds `VZCustomVirtioDevice`, guest-memory mapping and shared-memory regions. That makes a zero-copy framebuffer possible, where Haiku draws straight into host Metal memory.
   - A Metal presenter shows it, paced by `CADisplayLink`.
3. **Display plan: build in phases.** Each phase builds on the previous one.
   - **S1:** impersonate virtio-gpu. It needs no Haiku changes, and it can present without copying by aliasing Haiku's scanout pages.
   - **S2:** a purpose-built shared-surface device, plus a small Haiku driver and accelerant.
   - **S3:** vsync pacing, live resize and 2D command offload.
4. **The S2 device also gets a QEMU implementation.**
   - Haiku-side S2 work can start under QEMU (with GDB) before macOS 27 arrives.
   - The Windows track (WHPX plus a Direct3D presenter) reuses the same guest driver.
5. **We maintain our own forks of Haiku and QEMU and don't target upstream.**
   - Haiku's `AGENTS.md` (July 2026) doesn't accept AI-assisted material.
   - QEMU's `docs/devel/code-provenance.rst` declines contributions "believed to include or derive from AI generated content".
   - Patched sources and scripts live in this repo.

---

## 2. Facts that constrain the design

### 2.1 Haiku arm64, as of hrev60122
- **Boots to the desktop under QEMU `virt`**, with SMP up to 8 vCPUs. R1/beta6 (2026-08-27) says so [reported]. The official [arm64 guide](https://www.haiku-os.org/guides/building/compiling-arm64/) has an HVF command line, tested on macOS 15.7.7 with QEMU 11.1.0 [reported].
- **HVF works only since the GICv3 driver** (2026-09-02, `11186a8450`) [source]. QEMU under HVF defaults to Apple's in-kernel GIC, which is GICv3-only.
- **Timer: the virtual timer** (`CNTV_*`) on PPI 27 (`arch_timer.cpp`) [source]. This is the timer HVF, WHPX and VZ all deliver.
- **EFI loader:**
  - Handles entry at EL1 or EL2.
  - Handles both device tree and ACPI: MADT for the GIC, FADT for PSCI, SPCR/DBG2 for serial (`boot/platform/efi/arch/arm64/arch_acpi.cpp`) [source].
- **PCI:**
  - The ECAM host bridge is found via device tree or ACPI, with ACPI `_PRT` INTx routing (`ECAMPCIControllerACPI.cpp`) [source].
  - **No MSI:** the GICv3 driver still has `// TODO lpi init (needed for MSI(-X))`, and there is no GICv2m support [source]. Every device therefore uses **INTx**.
- **virtio:**
  - Transports: `virtio_pci` (legacy and modern) and `virtio_mmio` (legacy only) [source].
  - Drivers: block, SCSI, net, rng, balloon, input and gpu.
  - `virtio_pci` parses capability types 1–5 only. **It has no shared-memory capability (type 8)** [source].
- **virtio_gpu:**
  - 2D only; negotiates `VIRTIO_GPU_F_EDID` only.
  - Format `B8G8R8X8_UNORM`.
  - One `B_CONTIGUOUS` backing area.
  - A kernel thread transfers and flushes the full frame every 20 ms.
  - Ignores config events (`// TODO read config`).
  - Its accelerant returns `-1` for the retrace semaphore [source].
- **No RTC** (`arch_rtc_get_hw_time()` returns 0). No arm64 HaikuPorts repository, so no Mesa or apps beyond the bootstrap packages [reported].

### 2.2 Haiku's graphics model, which shapes everything below
All of this is from `src/servers/app/drawing/interface/local/AccelerantHWInterface.cpp` and the files named [source]:
- **app_server renders in software** into a malloc'd **back buffer**. `_CopyBackToFront(region)` copies dirty regions into the front buffer, which is the framebuffer. **No accelerant hook runs after that copy**, so a driver can't tell when a frame is complete. That's why virtio_gpu polls.
- **app_server never paces itself to vsync.** The only `WaitForRetrace` call is commented out (`ServerWindow.cpp:2709`). It merely passes the accelerant's retrace semaphore on to applications (`ServerApp.cpp:3262`, used by `BScreen::WaitForRetrace`).
- **No page flipping.** The local HW interface never uses `MOVE_DISPLAY`.
- **Pixel format:** the framebuffer is `B_RGB32` = bytes **B, G, R, X** (`GraphicsDefs.h:176`), and **the X byte is undefined**.
  - It maps onto `MTLPixelFormatBGRA8Unorm` without swizzling the color channels.
  - The host **must force alpha = 1**, using an opaque layer and a swizzle or the shader.
- **Memory types on arm64** (`VMSAv8TranslationMap.cpp:547`): `B_WRITE_BACK_MEMORY` → Normal WB; `B_WRITE_COMBINING_MEMORY` → Normal-NC; `B_UNCACHED_MEMORY` → Device-nGnRnE.
  - The EFI `framebuffer` driver passes no memory type, so don't copy its mapping call.

### 2.3 QEMU 11.1.1 + HVF on the M4 Max [measured]
- **Accelerators:** `hvf`, `tcg`.
- **Displays:** `cocoa`, `dbus`, `curses`, `none`.
- **Devices:** no `virtio-gpu-gl` and no `rutabaga`, because Homebrew doesn't build virglrenderer. `apple-gfx-pci` is present, but only macOS guests have a driver for it.
- **Default `-M virt -accel hvf`:** Apple's GIC, which accepts **GICv3 only**. `its=on` is rejected, **`msi=gicv2m` works**, and `gic-version=4` needs `virtualization=on`.
- **`-accel hvf,kernel-irqchip=off`:** QEMU's own GIC, which allows GICv2 and GICv3 with ITS. It's slower but useful for debugging.
- **`virtualization=on`** (nested EL2) is accepted. Per the research it is non-VHE only on macOS 26 [reported].
- **Framebuffer from the EDK2 firmware:**
  - `ramfb` gives a **linear BGRX8888 framebuffer**, 800×600 by default.
  - `virtio-gpu-*` gives **BltOnly** (no framebuffer). Haiku's loader skips that, so the screen stays blank until Haiku's own driver loads.
  - `bochs-display` gets no display at all on arm64.
- **ACPI vs device tree:** by default EDK2 gives the OS ACPI. `acpi=off` makes it give a device tree instead.
- **MMIO hazard** [reported]:
  - If a guest touches **device registers** with an instruction that gives no syndrome information (LDP/STP, writeback forms, SIMD), QEMU **aborts the VM** (`assert(isv)`). This applies under both HVF and WHPX.
  - Haiku drivers must use single LDR/STR on device registers.
  - RAM-backed shared memory never traps, so pixel traffic is unaffected.

### 2.4 WHPX (Windows 11 on ARM) [reported]
- AArch64 WHPX is upstream since QEMU 11.0 and needs Windows 11 24H2 build ≥ 26100.3915.
- GICv3 only; MSIs go through GICv2m, and there's no ITS.
- No nested virtualization, no SVE/SME, no migration.
- A guest-reset hang is fixed only in master (2026-09-14).
- MSYS2's `clangarm64` QEMU package links virglrenderer, ANGLE and SDL2.
- Nobody has reported running Haiku on WHPX yet.

### 2.5 What VZ presents to a non-macOS guest (macOS 26.5.1) [measured]
We measured this with `tools/vzprobe` by booting the EDK2 UEFI Shell in a generic-platform VM (`sprint0` test T3).

| Area | What VZ provides |
|---|---|
| Firmware | EDK2-based UEFI (`VZEFIBootLoader`). It boots `\EFI\BOOT\BOOTAA64.EFI` from MBR or GPT FAT. Custom firmware is impossible. |
| Hardware description | **ACPI only; no device tree.** Hardware-reduced ACPI (FADT 6.3), OEM `APPLE`/`Apple Vz`. |
| CPU interface | **PSCI via HVC.** |
| Interrupts | **GICv3**: GICD `0x10000000`, GICR `0x10010000`. **GICv2m frame** at `0x1FFF0000` with 128 SPIs starting at SPI 128. **No ITS.** |
| Timers (GTDT) | Virtual timer PPI 11 (GSIV 27), physical PPI 14 (GSIV 30), EL2 physical PPI 10 (GSIV 26). |
| Serial | **None.** No SPCR or DBG2 table. The only console is virtio-console, which Haiku has no driver for. |
| PCI | ECAM at `0x40000000`, a single root bus. Every device offers **MSI-X and INTx pin A**. |
| virtio | **Modern-only** PCI devices: net, console, blk, fs, vsock, gpu, sound, rng, balloon. NVMe and XHCI are Apple PCI devices. |
| Display | virtio-gpu features: `VERSION_1`, `RING_PACKED`, `INDIRECT_DESC`, `EVENT_IDX`. **No** `VIRGL`/`EDID`/`BLOB`/`CONTEXT_INIT`, 0 capsets, no shared-memory BAR. The firmware display is **BltOnly with no framebuffer**. |
| RAM | Starts at `0x70000000`. |
| Metal GPU | `VZMacGraphicsDevice` is rejected: "Virtual machines not using the macOS platform cannot use Mac graphics devices." |
| Private devices | `_VZLinearFramebufferGraphicsDeviceConfiguration`, `_VZPL011SerialPortConfiguration` and `_VZGDBDebugStubConfiguration` exist. VM start fails: "Restricted devices require the com.apple.private.virtualization entitlement", which in practice means SIP/AMFI off. **Not usable.** |

Haiku already supports all of this except the **serial port** and the **boot framebuffer**. Two consequences: early boot under VZ is blind, and nothing is displayed until a Haiku display driver loads. VZ's own virtio-gpu also offers no EDID, which Haiku's accelerant uses to build its mode list (to be checked in T4).

### 2.6 The macOS 27 custom virtio API [docs]
- **`VZCustomVirtioDeviceConfiguration`:**
  - `deviceID` (UInt16), `pciClassID`/`pciSubclassID`, `virtioQueueCount`.
  - `mandatoryFeatures`/`optionalFeatures`, `deviceSpecificConfiguration` (opaque `Data`), `sharedMemoryRegions` (`regionID` UInt8 + `size` UInt64), `supportsSaveRestore`.
  - `maximumAllowedSharedMemoryRegionCount` limits how many regions a device may have.
- **`VZCustomVirtioDevice`:**
  - `queue(at:)`, `guestMemoryMapping(atPhysicalAddress:length:)`, `sharedMemoryRegions`, `update(_:)` for the config, `requestReset()`, and `deviceQueue`, a serial dispatch queue.
- **`VZGuestMemoryMapping.mutableBytes` is a raw host pointer into guest RAM.** It is invalidated on reboot or shutdown.
- **`VZVirtioSharedMemoryRegion.mapMemory(ptr, atOffset:, size:, completionHandler:)`** maps host memory into a region, **asynchronously**.
- **Queues:**
  - The delegate receives `didReceiveNotificationFor:`, then drains with `nextElement()`.
  - Each element offers `readBytes…`/`peekIntoReadBuffers…`/`write…` (copying, and meant to be used once) and ends with `returnToQueue()`.
- **Delegate callbacks:** `DidAcceptDriverOk`, `WillPause`/`WillResume`/`WillReset`/`WillStop`, and save/restore.
  - **There is no callback for guest writes to config space.**
- **Undocumented, to be tested on 27:**
  - Whether `update(_:)` raises a config-change interrupt.
  - Shared-region alignment and size limits (16 KiB expected).
  - Whether `mapMemory` accepts Metal-allocated memory. The VM runs in a separate XPC service process, so the memory probably has to be shareable.
  - Whether `returnToQueue()` always interrupts the guest.

---

## 3. Architecture

```
 Haiku guest                                   Host app (VZ on macOS 27; QEMU device for dev/Windows)
 ───────────                                   ───────────────────────────────────────────────────────
 app_server ──(back→front copy)──► front surface ═══ shared memory ═══► MTLBuffer (bytesNoCopy)
     │  commit hook (S2/S3)                         (S1: aliased guest pages;       │
     ▼                                               S2: shared-memory region)      ▼
 accelerant ──ioctl──► kernel driver ──virtqueues (INTx)──► device delegate ──► Metal presenter
                          ▲                                    │                (CAMetalLayer,
                          └────── events: vsync, mode ◄────────┘                 CADisplayLink)
```

### 3.1 The Metal presenter, shared by every phase
- **Source:** a BGRX surface described by base pointer, byte offset, stride, width and height. The fragment shader **samples the buffer directly** instead of through a linear texture.
  - That removes the 16 KiB page and linear-texture alignment constraints, so odd strides (1366×4) and 4 KiB-aligned guest addresses work.
- **Memory:** 16 KiB-aligned host memory from `mmap`, wrapped with `makeBuffer(bytesNoCopy:)`. Don't allocate through Metal and then share.
- **Output:** a `CAMetalLayer` with `isOpaque = true`, pixel format `.bgra8Unorm`, **alpha forced to 1**, and aspect-correct scaling.
- **Pacing:** `CADisplayLink` on the view. Each tick presents if something changed, and in S2 it also generates the vsync event.
- **Coherency:** host and guest share one coherent memory system. As long as the guest maps the surface **write-back cacheable**, no cache maintenance is needed, only ordering.
  - The virtio publish path is the release point: Haiku's virtio bus issues a barrier before publishing and notifying.
  - The host reads the surface only after it dequeues the corresponding message.

### 3.2 Phase S1: impersonate virtio-gpu (no Haiku changes)
- **Device:** `deviceID = 16` (GPU), PCI class display, 2 queues (control and cursor).
- **Features:** offer `VERSION_1` plus **`VIRTIO_GPU_F_EDID`**. Haiku's accelerant builds its mode list from EDID; VZ's own virtio-gpu doesn't offer it. Don't offer packed rings, which Haiku doesn't support.
- **Commands to implement:** `GET_DISPLAY_INFO`, `GET_EDID` (generated from the window size), `RESOURCE_CREATE_2D` (`B8G8R8X8`), `RESOURCE_ATTACH_BACKING`, `SET_SCANOUT`, `TRANSFER_TO_HOST_2D`, `RESOURCE_FLUSH`, `RESOURCE_DETACH_BACKING`, `RESOURCE_UNREF`. The cursor queue can be ignored because Haiku draws a software cursor.
- **Zero-copy by aliasing:**
  - Haiku's backing store is one `B_CONTIGUOUS` area.
  - On `ATTACH_BACKING`, create `guestMemoryMapping(at: pa, length:)`, round its `mutableBytes` down to a 16 KiB boundary, and wrap it with `makeBuffer(bytesNoCopy:)`, keeping the remainder as the offset.
  - `TRANSFER_TO_HOST_2D` then only records damage, and `RESOURCE_FLUSH` marks the frame dirty.
  - If the backing isn't one mapping-friendly range, fall back to a host copy.
- **Lifetime:** drop the aliases in `WillReset`/`WillStop`, because mappings don't survive a reboot.
- **Known limits of the stock driver:**
  - It flushes the full frame every 20 ms, with no damage information.
  - It ignores config events, so there's no live resize.
  - Its retrace semaphore is `-1`, so there's no vsync.
  - S1's purpose is to derisk the VZ plumbing (attach, queues, interrupts, presentation), not to be the end state.

### 3.3 Phase S2: the shared-surface device (zero copy)
The formal contract belongs in a separate spec. It must satisfy these constraints:
- **Identity:** use a virtio device ID that is **not** 16, so Haiku's `virtio_gpu` never binds. PCI class display. **`VERSION_1` mandatory.** No packed rings. `EVENT_IDX` optional.
- **Config space** is **read-only for the driver**: VZ has no hook for guest writes, and `update()` must keep the size unchanged. Use a **fixed-size struct with reserved fields**:
  - magic and version, maximum and preferred width/height, refresh rate (mHz), stride alignment, supported formats (B8G8R8X8 only), shared region ID and size, flags.
  - Honor `config_generation`.
- **Shared memory:** one region, sized up front for the **largest supported mode**, rounded up to 16 KiB. It can't grow at runtime, and `mapMemory` is asynchronous.
  - Haiku maps it with **`B_WRITE_BACK_MEMORY`**.
  - Haiku's `virtio_pci` needs support for capability type 8 plus a bus-manager call that returns each region's address and size.
- **Queues:**
  - **controlq** (driver → device, with replies): `SET_MODE {w, h, stride, format, offset}` and `COMMIT {dirty rects}`.
  - **eventq** (the driver keeps buffers posted, as virtio-input does; the device completes them): `VSYNC {seq, timestamp}`, `MODE_HINT {w, h}` (window resized), `REDRAW {}` (after a restore).
  - Mode changes are **also** delivered as events, because the docs don't say whether `update()` raises a config interrupt.
- **Commit semantics (recommended):**
  - On `COMMIT`, the host GPU-copies the dirty rects from the shared surface into its private present texture.
  - It completes the element **only after that copy finishes**. Completion means "the surface may be written again".
  - This is tear-free without double-buffering the shared region. Flipping only pays off if app_server stops using its RAM back buffer.
- **Interrupts:** INTx, level-triggered, through ACPI `_PRT`. At 60–120 events/s that's fine without MSI.
- **Save/restore:** the region's contents are not guest RAM. Save the surface in `customVirtioDeviceSaveState`, or send `REDRAW` after restore.

**Haiku side (our fork):**
- `virtio_pci` shared-memory capability plus the bus-manager API.
- A new kernel driver, modelled on `virtio_gpu`: probe, map the region WB, run the queues, retrace semaphore.
- A new accelerant, modelled on the virtio and `framebuffer` accelerants: modes from config or events, `B_GET_FRAME_BUFFER_CONFIG` pointing into the region, a real `B_ACCELERANT_RETRACE_SEMAPHORE`.
- **An app_server patch:** call a new accelerant hook with the dirty region after `_CopyBackToFront`. Without it there is no commit point.

### 3.4 Phase S3: pacing, resize and 2D offload
- **Vsync pacing:** an app_server patch that waits on the retrace semaphore (bounded) before copying to the front buffer. The eventq supplies `VSYNC` at the real display cadence.
- **Live resize:** host window resize → `MODE_HINT` → the accelerant re-lists modes → app_server switches mode.
- **2D offload:** the same controlq carries fill, blit and composite commands, executed with Metal blit and compute encoders. This is "accelerants on host Metal" in the manifest's sense, and it needs no redesign.

### 3.5 The same device on QEMU (dev baseline and Windows)
- **QEMU fork:** implement the S2 device as a virtio device with a shared-memory capability. QEMU already provides `virtio_pci_add_shm_cap()`, used by virtio-gpu hostmem and vhost-user-fs DAX.
  - Back it with host memory, and show it through a QEMU display surface first, then a Metal view.
  - Same guest driver and same contract, with a GDB stub and `-snapshot` available.
- **Windows:** the same device under WHPX, presented through Direct3D 11/12.

---

## 4. Recommendations
1. **Keep QEMU + HVF as the development and debugging platform.** Start every Haiku-side change there, with serial, GDB and `-snapshot`.
2. **Run Sprint 0 now, on macOS 26:**
   - Measure the platform (VZ probe, HVF matrix).
   - Boot Haiku under VZ with VZ's own virtio-gpu.
   - Characterize the QEMU boot matrix (virtio-mmio vs PCI, blk vs SCSI).
   - Prove the Metal presenter on synthetic buffers.
3. **Write the S2 spec next** (the contract in §3.3), so host and guest work can proceed in parallel. Build the **QEMU implementation of S2 first**, because it doesn't need macOS 27.
4. **After upgrading to macOS 27**, in this order:
   - Re-run T3. The VZ firmware or platform may have changed.
   - Build the VZ runner.
   - Build S1.
   - Build S2 on VZ.
5. **In Haiku drivers, access device registers with plain 32/64-bit loads and stores only**, never LDP/STP, writeback forms or SIMD. Otherwise HVF and WHPX abort the VM.
6. **Map every host-shared surface `B_WRITE_BACK_MEMORY`.**
7. **Treat `B_RGB32` as BGRX with undefined alpha** everywhere in the host path.
8. **Avoid private VZ APIs.** They need SIP off.
9. **Keep patches in this repo** (as patch series against Haiku and QEMU revisions), and track upstream for fixes.

## 5. Risks and open questions
| Risk | How we'll resolve it |
|---|---|
| Haiku doesn't boot under VZ (ACPI-only, no serial) | Sprint 0 T4, which extracts the syslog with `bfs_shell` |
| Haiku's accelerant can't list modes without EDID on VZ's virtio-gpu | T4. S1 offers EDID in any case |
| macOS 27 API unknowns (config interrupt, region alignment and size, sharing memory with the XPC service, interrupt behavior) | First tasks in Sprint 1 on macOS 27 |
| Latency from the delegate being called on a dispatch queue under load | Sprint 1 measurement: kick → delegate → `returnToQueue` → guest IRQ |
| Guest register accesses that trap without a syndrome (`assert(isv)`) | Review driver MMIO accessors; run the T2 boot matrix |
| Nothing on screen until a driver loads under VZ | Accept; use QEMU with `ramfb` for early-boot work |
| No MSI (INTx only) | Fine at display event rates. Add GICv2m support later if needed |
| Upstream drift, since we're a fork | Rebase regularly. Keep the patch series small and topic-scoped |

## 6. Roadmap
- **Sprint 0 (macOS 26, now):** tests T1–T5 in [sprint0/README.md](sprint0/README.md).
- **Sprint 1:**
  - S2 spec.
  - QEMU S2 device.
  - Haiku `virtio_pci` shared-memory support, S2 driver and accelerant, and the app_server commit hook, all developed under QEMU.
  - After the upgrade: VZ runner, S1, and the macOS 27 API measurements.
- **Sprint 2:** S2 on VZ (zero copy) with the Metal presenter; eventq vsync; `MODE_HINT` resize.
- **Sprint 3:** app_server vsync pacing; 2D offload over controlq; Windows track (WHPX with a Direct3D presenter).

## 7. References
- Haiku arm64 build guide: https://www.haiku-os.org/guides/building/compiling-arm64/
- Haiku GICv3 driver: https://github.com/haiku/haiku/commit/11186a845006f6d5464692a031953f1e0ec7ffb0
- Haiku contribution policy: https://github.com/haiku/haiku/blob/master/AGENTS.md
- QEMU code provenance policy: https://www.qemu.org/docs/master/devel/code-provenance.html
- QEMU 11.1 release: https://www.qemu.org/2026/08/11/qemu-11-1-0/
- QEMU WHPX: https://www.qemu.org/docs/master/system/whpx.html
- Apple `VZCustomVirtioDevice`: https://developer.apple.com/documentation/virtualization/vzcustomvirtiodevice
- Apple `VZGuestMemoryMapping`: https://developer.apple.com/documentation/virtualization/vzguestmemorymapping
- Apple `VZVirtioSharedMemoryRegion`: https://developer.apple.com/documentation/virtualization/vzvirtiosharedmemoryregion
- Virtio 1.3 specification: https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.html
- UTM graphics (virgl/Venus on macOS reference): https://github.com/utmapp/UTM/blob/main/Documentation/Graphics.md

---

## 8. Plan (2026-09-18): from "it boots" to a first-class Haiku on Apple Silicon

Scope decision: we fork Haiku (PROSE) and change whatever the experience needs. Nothing here is constrained by upstream acceptability.

### 8.1 What the virtio investigation established

- **Haiku's virtqueue code is correct.** `VirtioQueue.cpp` builds spec-conformant split-ring chains, direct and indirect: device-readable descriptors first, `VRING_DESC_F_WRITE` on the rest, `NEXT` on all but the last, an indirect table sized `count × 16` with a proper free list, `avail->idx` published behind a write barrier, and the used ring drained with the free list restored. VZ's custom-device framework consumed thousands of these chains unchanged (our GPU), as did VZ's built-in net and, once negotiation was fixed, its block device.
- **Feature negotiation is narrower than it looks.** A driver only gets transport features it asks for (`fFeatures &= supported` runs first). No Haiku driver asks for `EVENT_IDX`, so its unimplemented `used_event` never matters. `virtio_block` asks for `INDIRECT_DESC` and uses it; others don't.
- **Every failure was in the PCI transport** (`busses/virtio/virtio_pci`), and QEMU's leniency hid all of them: the driver-feature write to the read-only register, the `cap_len`/`length` mix-up, uninitialized notify offsets, and the legacy I/O-port path whose BARs all translate to host address 0 on arm64. Patch 0003 fixes them; the QEMU `pci-*` rows and VZ's virtio-blk both pass now.
- **Two things remain unexplained**, both low priority: VZ's own virtio-gpu still hangs Haiku's driver on its first command despite correct negotiation, and the legacy I/O BAR translation (`pci_ram_address()` ignores I/O ranges) is unfixed because nothing uses it any more.

### 8.2 Code reviewed, and what each part needs

| Area | Files | Verdict | Work |
|---|---|---|---|
| Virtqueues | `bus_managers/virtio/VirtioQueue.cpp`, `virtio_ring.h` | Correct | Optional: implement `used_event`/`avail_event` and accept `EVENT_IDX` (fewer interrupts and notifies under load) |
| Negotiation | `bus_managers/virtio/VirtioDevice.cpp` | Correct once the transport writes the right register | Drivers ignore `negotiate_features()` failures (`virtio_gpu`, `virtio_block`, `virtio_net`, …); make them fail init instead of hanging |
| PCI transport | `busses/virtio/virtio_pci/virtio_pci.cpp` | Fixed (0003) | MSI-X via GICv2m (VZ offers MSI-X on every device; Haiku has no arm64 MSI at all, so everything is INTx); fix I/O BAR translation in `bus_managers/pci/pci.cpp` for completeness |
| Block driver | `drivers/disk/virtual/virtio_block/virtio_block.cpp` | Works; one request in flight, one shared header buffer | Pipeline requests (per-request headers, many in flight) or standardize on NVMe; measure both on VZ first |
| GPU driver | `drivers/graphics/virtio/virtio_gpu.cpp` | Works on our device; 20 ms full-frame copies; ignores config events | Superseded by S2 (below); keep for S1 fallback |
| Interrupts | `kernel/arch/arm64/arch_int_gicv3.cpp`, `kernel/interrupts.cpp` | Level-triggered SPIs, EOI after dispatch; correct | GICv2m MSI frame support (prerequisite for MSI-X) |
| Loader SMP/ACPI | `boot/platform/efi/arch/arm64/arch_smp.cpp`, `arch_acpi.cpp` | Fixed (0004, 0005) | None |
| Power | `kernel/arch/arm64/arch_cpu.cpp` (`arch_cpu_shutdown` returns `B_ERROR`) | Missing | PSCI `SYSTEM_OFF`/`SYSTEM_RESET`, and handle VZ's ACPI power button (`PNP0C0C`) so `requestStop` shuts Haiku down cleanly |
| Clock | `arch_rtc_get_hw_time()` returns 0; VZ exposes no RTC | Missing | Host wall time via a config field of our custom device, read once at boot; NTP later |
| Console | none under VZ; RAM log (0001) | Adequate for reading | Interactive KDL over a virtio-console (Haiku change 10182 exists) or over our custom device's control queue |

### 8.3 Sequencing

**Sprint 1 (now):**
1. ✅ Turn the patch series into a proper branch in the Haiku tree: branch `vz-fork` in the private worktree, one commit per patch, `patches/haiku/` as the exported form. (The PROSE branding lives on the colleague's tree; merge later.)
2. ✅ Clean shutdown and reboot: PSCI plus the ACPI power-button event (PL061 GPIO events on VZ, GED on QEMU), with per-IRQ trigger configuration in the GIC driver.
3. ✅ Wall clock at boot: UEFI `GetTime()` via the loader turned out simpler than a device config field.
4. ✅ Draft the **S2 spec**: [docs/s2-display-device.md](docs/s2-display-device.md) (config layout, shared-region geometry, queue protocol, sync rules, event queue). Draft 1 is out for review; §12 lists what must be measured on the host before version 1 is frozen.
5. Time-boxed: a virtqueue dump in `hvgpu` (Haiku prints ring addresses; the host reads them from guest RAM) to close the VZ-GPU question. Drop it if it takes more than a day.

**Sprint 2: S2, the zero-copy display.**
- ✅ **Milestone A (2026-09-18): the whole pipe, no accelerant yet.** Host: the S2 device in `hvgpu` (`--display s2`; `tools/hvgpu/prds.swift`: shared region, control and event queues, `CADisplayLink` vsync events, `--screenshot`). Guest (patch 0014): `virtio_pci` shared-memory capability plus `get_shared_memory()` in the bus manager, and the `prose_display` driver, which maps the pool, sets the mode, posts event buffers and — for now — draws a test pattern from a kernel thread. Verified on VZ: mode 1280x800, ~10 commits/s, 60 Hz vsync events with host and guest counts identical. Findings recorded in the spec §10.1/§12: `mapMemory` only after the VM starts and on the device queue; one region per device; the region is a 64-bit BAR above RAM; **no vsync while the host display sleeps**.
- ✅ **Milestone B (2026-09-18): the Haiku desktop on S2, zero copy.** Patch 0015: `prose_display.accelerant` (app_server draws straight into the cloned surface pool), a new optional accelerant hook `B_COMMIT_RECTANGLES` that `AccelerantHWInterface` calls after every back-to-front copy and cursor draw, and a commit thread that sends the collected rectangles once per vsync (retrace semaphore handed to app_server's team; 20 ms timer when there is no vsync). Desktop, windows and cursor verified through `run-vz.sh <name> --display s2 --screenshot`; boot drawing coalesces to ≤ 1 commit per vsync, an idle desktop commits ~4×/s.
- ✅ **Milestone C (2026-09-18): live resize, and the tear-free three-buffer pipe.** `dispbuf ← fb ← bb`: app_server's back-to-front copy is the ready signal; the commit is synchronous and the host copies exactly those rects into its display buffer under the presentation lock, which the presenter also holds while the GPU reads (spec §10.1). Sampling the pool directly with the shader was tried and dropped: it tears. The pool stays mapped for Metal so the next step — the back buffer in the pool and a blit kernel bb → dispbuf — needs no new plumbing; pool reads from the guest run at RAM speed. Measured: 50 µs commit round trip, 1 µs host copy per typical commit, ~70 commits/s idle (the Deskbar CPU meter). Patch 0016: `PRDS_WAIT_MODE_HINT` ioctl, `prose_display_agent` (a launch_daemon service that follows the host window with `BScreen::SetMode()`), `REDRAW` → full commit. Measured: host window resize → guest desktop at the new size in ~160 ms; VZ raises the config-change interrupt on `update(_:)` (spec §12.2 closed). `hvgpu --resize-after N WxH` scripts the test.
- Remaining for S2 (spec §12.5–6): the 19 ms commit-latency outliers; the back buffer in the pool + GPU blit bb → dispbuf; cursor as a host overlay; `hvgpu` defaulting to `--display s2`; a Haiku-side pixel test.

**Sprint 3: polish and reach.**
- Our own virtio-input keyboard and tablet devices, driven from `hvgpu`'s window, so VZ's GPU no longer has to stay attached and the window is entirely ours.
- MSI-X through GICv2m; `EVENT_IDX`; block pipelining or NVMe as the settled default.
- Console: virtio-console debug channel for interactive KDL.
- 2D offload over the S2 control queue (fill, blit, composite on Metal).
- The QEMU implementation of the S2 device, for the Windows/WHPX track.

**Parallel track: packages.** There is no arm64 HaikuPorts repository, so the desktop has no applications. A bootstrap build (`haikuporter` + `haikuports`, `jam @bootstrap-raw`) on this Mac produces our own package repository. Hours of compute, little attention; start it early and let it run.

### 8.4 Verification that stays in place

- `sprint0/t2` (QEMU rows, now all expected to pass), `sprint0/t3` (VZ platform baseline), `sprint0/t4` (VZ boot with syslog provenance check), `sprint0/t5` (presenter).
- New for Sprint 1: a headless `hvgpu` boot check (8 vCPUs, virtio-blk and NVMe, RAM-console markers: no panics, no `ISR 0`, first login, accelerant) — the same command used throughout this investigation, scripted.
