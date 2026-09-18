# Sprint 0 — platform measurements (macOS 26 baseline, re-run on 27)

Tests that need no macOS 27 APIs. Full context: [../haiku_virtualized.md](../haiku_virtualized.md).
Artifacts land in `work/<test>/`; conclusions in the design doc (§2).

## Tests

| ID | What | How | Status |
|---|---|---|---|
| T1 | QEMU+HVF acceptance matrix: GIC/MSI/EL2 options (part A) and boot framebuffer + ACPI-vs-DT per display device (part B) | `./t1-qemu-hvf-matrix.sh` → `work/t1/report.txt` | script ready; run after `brew install coreutils` (`timeout`/`gtimeout` required) |
| T2 | Haiku boot matrix under QEMU (virtio mmio vs PCI, blk vs SCSI; register-access hazards) | `../scripts/run-qemu-hvf.sh --transport/--disk` | mmio+blk+ramfb **boots to desktop** [measured 2026-09-18]; PCI virtio-blk shows intermittent I/O errors — matrix run pending |
| T3 | VZ platform probe: what Virtualization.framework presents to a generic arm64 guest (ACPI, GIC, PCI, virtio features, private devices), via 3-pass UEFI Shell dump | `./t3-vz-platform-probe.sh` → `work/t3/report.txt` | **done on macOS 26.5.1** (baseline recorded); **re-run on every macOS update, first thing on 27** |
| T4 | Boot Haiku itself under VZ (VZ's own virtio-gpu; does the accelerant light up without EDID? does it boot at all, ACPI-only and blind?) | `../build/bin/hvz <image-copy>` (see `tools/hvz/hvz.swift`) | pending — run on a **copy** of `haiku-mmc.image` (hvz writes to the disk) |
| T5 | Metal presenter proof on synthetic buffers: bytesNoCopy + fragment-shader sampling of odd strides, opaque BGRA layer, CADisplayLink pacing | `../build/bin/presenter` (see `tools/presenter/presenter.swift`) | pending |

## Rebuild after OS changes

```sh
FORCE=1 tools/build.sh     # rebuilds vzprobe, hvz, presenter; ad-hoc signs VZ tools
```
