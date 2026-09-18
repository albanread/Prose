# Haiku fork patches

These apply on top of Haiku **hrev60122**. We maintain our own fork; see `haiku_virtualized.md` §1.5 for why none of this goes upstream.

| Patch | What it does | Why |
|---|---|---|
| 0001 | The arm64 kernel keeps a RAM copy of all its debug output, starting with the tag `HAIKU-RAMLOG-V1` | Apple's Virtualization.framework gives the guest no UART. `tools/hvgpu`'s RAM console reads this buffer from guest memory, including panics and KDL output |
| 0002 | `virtio_gpu` refuses GPUs that don't offer `VIRTIO_GPU_F_EDID` | Under VZ, app_server would otherwise open VZ's own virtio-gpu first and hang. Our custom GPU offers EDID |
| 0003 | `virtio_pci` (modern path), four fixes: capabilities are read by `cap_len`, not `length`; notify offsets start as "unset"; the notify address is range- and magic-checked; 64-bit queue addresses are written as two 32-bit halves | Reading by `length` (the BAR region size) meant VZ's small notify regions never had `notify_off_multiplier` read, so it was garbage. `virtio_net` then notified random kernel addresses and panicked. The rest are defences, plus the spec's §4.1.3.1 32-bit access rule |
| 0004 | The EFI loader numbers CPUs in MADT order, with the CPU it runs on as 0 | VZ's MADT reports GICC "CPU interface number" 0 for every CPU, which is valid for GICv3. Using it as the id made every CPU look like the boot CPU, so none were started and the kernel waited for them forever |
| 0005 | The loader's PSCI calls follow SMCCC: x0–x3 are in/out, x4–x17 are clobbered, and the status is returned | The asm declared inputs only, which is wrong for any PSCI implementation that writes more than x0. It also logs each `CPU_ON` result |
| 0006 | The loader logs "Entering the kernel on the boot CPU" | Shows in the RAM console where the loader hands over |

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
