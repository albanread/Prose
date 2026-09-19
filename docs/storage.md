# The disk image, NVMe, and how the guest boots

One file on the Mac is the guest's whole disk. This is what is in it, how it is
attached, and how the firmware and kernel find their way from one end to the
other.

## The image

`haiku-mmc.image`, 686 MiB, built by the `@prose-mmc` jam target
(`build/jam/images/MMCImage`). It is an *MMC/SD card* image: an MBR partition
table and two partitions, which is the layout Haiku's arm64 build produces and
which UEFI is happy to boot.

Read straight out of the built image:

| | type | start | size |
|---|---|---|---|
| Partition 0 | `0xEF` EFI System Partition (FAT) | LBA 8192 — 4 MiB | 32 MiB |
| Partition 1 | `0xEB` BFS — the operating system | LBA 73728 — 36 MiB | 650 MiB |

The 4 MiB gap at the front is `HAIKU_BOOT_SDIMAGE_BEGIN`, which
`build/jam/ArchitectureRules` sets to 4096 KiB for arm64 — generous alignment
inherited from real SD cards. The 650 MiB is `HAIKU_IMAGE_SIZE` from the
`prose-*` build profile.

**Partition 0** holds `EFI/BOOT/BOOTAA64.EFI` — Haiku's EFI loader, named for
the architecture by `MMCImage` — plus `uEnv.txt`, an `fdt` directory and the
UEFI signing keys. Its FAT label comes from `HAIKU_MMC_LABEL`.

**Partition 1** is BFS: the system, and `system/packages` full of `.hpkg` files
that `packagefs` mounts read-only over `/boot/system` at boot. Its volume name
is `HAIKU_IMAGE_LABEL`, which is what Tracker shows on the desktop. In this
build both are `Prose`; the BFS superblock, 512 bytes into partition 1, reads:

```
volume name: 'Prose'   magic1: b'1SFB'
```

## How the Mac attaches it

`tools/hvgpu` opens the file once:

```swift
VZDiskImageStorageDeviceAttachment(url: diskURL, readOnly: false,
                                   cachingMode: .automatic,
                                   synchronizationMode: .full)
```

`.full` means a guest flush reaches the physical disk, so a guest that shuts
down cleanly — or panics — leaves a consistent image behind.

That attachment is then given to one of three controllers (`--disk`):

| `--disk` | device | notes |
|---|---|---|
| `nvme` (default) | `VZNVMExpressControllerDeviceConfiguration` | Stock Haiku drives it. No fork patch involved |
| `virtio` | `VZVirtioBlockDeviceConfiguration` | Needs fork patch 0003; see below |
| `usb` | `VZUSBMassStorageDeviceConfiguration` | Slow, but a useful third opinion when the other two disagree |

### Why NVMe is the default

Not preference — necessity, at first. Virtualization.framework exposes virtio
devices only over PCI, and Haiku's `virtio_pci` did not drive a modern device
correctly: it wrote the driver's accepted features to the read-only
`device_feature` register, so VZ's block device never saw a `VERSION_1`
acknowledgement and refused to operate. The symptom was `reading the partition
table failed` and no boot at all.

NVMe had no such problem. Haiku's `nvme_disk` driver
(`src/add-ons/kernel/drivers/disk/nvme/`, with a vendored `libnvme`) is a plain
PCI driver with none of virtio's negotiation, so it worked on the first
attempt and the whole bring-up proceeded on it.

Patch 0003 later fixed `virtio_pci`, and `--disk virtio` works now. NVMe stays
the default for a reason worth keeping: **it is the one storage path that needs
no patch of ours at all**. Nothing in `patches/haiku` touches NVMe. An
unmodified Haiku arm64 build will boot from this image on NVMe, which makes it
the control case whenever something else looks broken.

## The NVMe driver

`nvme_disk.cpp` plus `libnvme`, derived from Intel's SPDK. It registers as

```
drivers/disk/nvme_disk/driver_v1
drivers/disk/nvme_disk/device_v1
```

and publishes `disk/nvme/<n>/raw`, on which the disk device manager lays the
partitions it finds. The boot log line

```
Mounted boot partition: /dev/disk/nvme/0/1
```

is partition 1 — the BFS one — of the first NVMe namespace.

## The path from power-on to desktop

1. **`VZEFIBootLoader`** starts, with its variable store in a file beside the
   image (`--efivars`; `run-vz.sh` deletes it whenever it makes a fresh copy, so
   firmware state never outlives its disk).
2. The firmware enumerates the NVMe controller, reads the MBR, finds the EFI
   System Partition and runs `EFI/BOOT/BOOTAA64.EFI`.
3. **Haiku's EFI loader** reads the real time from UEFI `GetTime()` before
   `ExitBootServices` (patch 0010 — arm64 Haiku has no RTC, and VZ offers none,
   so without this the clock starts at 1970), numbers the CPUs in MADT order
   (0004, because VZ reports GICC interface number 0 for every CPU), and makes
   its PSCI calls following SMCCC (0005).
4. The loader reads the kernel and boot modules **from the BFS partition** —
   the ESP holds only the loader itself — and enters the kernel (0006 logs it).
5. The kernel brings up the device manager, which probes PCI. `nvme_disk`
   attaches, publishes its device, the partition scan finds BFS, and `/boot` is
   mounted.
6. `packagefs` mounts every `.hpkg` in `system/packages`, and the launch daemon
   starts `app_server`, `registrar`, `prose_display_agent` and the rest.

There is no serial port on a VZ guest, so everything above is read back from the
kernel's RAM log (patch 0001) rather than a console.

## Working with the image from macOS

The BFS partition can be read and written while the VM is *not* running, with
Haiku's own `bfs_shell` built as a host tool. Every script that does this finds
the partition the same way — parse the MBR, take the entry whose type is
`0xEB` — rather than assuming the 36 MiB offset:

```python
for i in range(4):
    entry = mbr[446 + 16 * i:462 + 16 * i]
    if entry[4] == 0xEB:
        lba, count = struct.unpack("<II", entry[8:16])
```

| script | what it does |
|---|---|
| `private_workspace/extract.sh <run> <path>` | copies a file out of a run's image |
| `private_workspace/syslog.sh <run>` | copies the guest's syslog out |
| `private_workspace/skip-first-boot-prompt.sh` | seeds the locale settings so a regular image does not stop in FirstBootPrompt |
| `scripts/prosepkg install <image> <packages>` | installs built packages into an image |

`PW_INJECT_SCRIPT=<file> run-vz.sh <run>` installs a script as the guest's
`UserBootscript`, which is how the probes in `private_workspace/` run without
anyone typing in the guest. It must be plain `sh`.

## Fresh copies, and `--keep`

`run-vz.sh <run>` copies the built image to `private_workspace/work/<run>/` and
boots the copy, so every test starts from an identical first boot and the built
image is never written to. `--keep` reuses the copy instead, which is what you
want when the VM is a machine you are actually using.

The distinction has a visible consequence: Tracker writes the desktop background
setting during first boot, and renders it from the *next* boot. A run that
always starts from a fresh copy never shows the wallpaper.
