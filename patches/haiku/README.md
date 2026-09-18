# Haiku fork patches

These apply on top of Haiku **hrev60122**. We maintain our own fork; see `haiku_virtualized.md` §1.5 for why none of this goes upstream.

| Patch | What it does | Why |
|---|---|---|
| 0001 | The arm64 kernel keeps a RAM copy of all its debug output, starting with the tag `HAIKU-RAMLOG-V1` | Apple's Virtualization.framework gives the guest no UART. `tools/hvgpu`'s RAM console reads this buffer from guest memory, including panics and KDL output |
| 0002 | `virtio_gpu` refuses GPUs that don't offer `VIRTIO_GPU_F_EDID` | Under VZ, app_server would otherwise open VZ's own virtio-gpu first and hang. Our custom GPU offers EDID |
| 0003 | `virtio_pci`, modern-interface fixes: (a) the driver's accepted features are written to `driver_feature(_select)`, not the read-only `device_feature(_select)`; (b) transitional devices (IDs 0x1000–0x103f) that expose the modern capabilities use the modern interface; (c) capabilities are read by `cap_len`, not `length`; (d) notify offsets start as "unset" and notifies are range- and magic-checked; (e) 64-bit queue addresses are written as two 32-bit halves; (f) interrupts that find ISR 0 are counted | (a) Haiku never told a modern device which features it accepted. QEMU shrugs; VZ's block and GPU devices refuse to operate without `VERSION_1`, which is why virtio-blk failed under VZ. (b) The legacy interface uses I/O ports, and on arm64 the PCI layer reports host address 0 for every I/O BAR (`pci_ram_address()` only knows memory ranges), so all legacy devices aliased whichever sat at I/O address 0: initializing virtio-net reset the disk. That was QEMU's `pci-blk`/`pci-scsi` failure. (c) VZ's notify regions are tiny, so `notify_off_multiplier` was never read and `virtio_net` notified random kernel addresses. (d)–(f) are defences and diagnostics, plus the spec's §4.1.3.1 access rule |
| 0004 | The EFI loader numbers CPUs in MADT order, with the CPU it runs on as 0 | VZ's MADT reports GICC "CPU interface number" 0 for every CPU, which is valid for GICv3. Using it as the id made every CPU look like the boot CPU, so none were started and the kernel waited for them forever |
| 0005 | The loader's PSCI calls follow SMCCC: x0–x3 are in/out, x4–x17 are clobbered, and the status is returned | The asm declared inputs only, which is wrong for any PSCI implementation that writes more than x0. It also logs each `CPU_ON` result |
| 0006 | The loader logs "Entering the kernel on the boot CPU" | Shows in the RAM console where the loader hands over |
| 0007 | `virtio_block` logs every request that fails or times out (op, sector, length, vectors, wait result, elapsed, status byte) | Distinguishes "device never answered" (timeout, status 255) from "device rejected" (status 1/2) without a debugger |
| 0008 | The virtio bus manager logs the device's offered and the driver's accepted feature bits, in hex, for every device | The negotiation is where both VZ failures hid |

Apply to a clean tree, then rebuild the image:

```sh
cd /Volumes/HaikuSrc/haiku
git apply /Volumes/xb/HaikuArmQemu/patches/haiku/*.patch
/Volumes/xb/HaikuArmQemu/scripts/build-image.sh
```

To refresh the patches after editing the tree:

```sh
git diff -- <file> > /Volumes/xb/HaikuArmQemu/patches/haiku/<NNNN-name>.patch
```

Each patch covers one file.
