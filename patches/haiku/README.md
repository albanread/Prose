# Haiku fork patches

These apply on top of Haiku **hrev60122**. We maintain our own fork; see `haiku_virtualized.md` §1.5 for why none of this goes upstream.

| Patch | What it does | Why |
|---|---|---|
| 0001 | The arm64 kernel keeps a RAM copy of all its debug output, starting with the tag `HAIKU-RAMLOG-V1` | Apple's Virtualization.framework gives the guest no UART. `tools/hvgpu`'s RAM console reads this buffer from guest memory, including panics and KDL output |
| 0002 | `virtio_gpu` refuses GPUs that don't offer `VIRTIO_GPU_F_EDID` | Under VZ, app_server would otherwise open VZ's own virtio-gpu first and hang. Our custom GPU offers EDID |
| 0003 | `virtio_pci` (modern path): notify offsets start as "unset", notifying a queue that was never set up is refused, and 64-bit queue addresses are written as two 32-bit halves | Uninitialized notify offsets let a stray notify write to a random kernel address, which panicked in `virtio_net` under VZ. The virtio spec (§4.1.3.1) requires 32-bit accesses |

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
