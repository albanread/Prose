# HaikuArmQemu — Manifest of Intentions and Goals

## Primary Goal

Run the Haiku operating system (ARM64 port) inside QEMU on Apple Silicon and
Snapdragon (Windows on ARM) hosts at high speed — native virtualization
(KVM/HVF/WHVP), no instruction emulation — so we can enjoy and further develop
Haiku without the booting and hardware-compatibility issues of physical PCs.

## Secondary Goals

- **Video accelerants on the host GPU**: expose host GPU acceleration
  (Metal on macOS, DirectX on Windows on ARM) to the Haiku guest, so the
  guest's video accelerants can drive real host GPU pipelines rather than
  emulated framebuffer-only display.

## Guiding Principles

1. **Native speed first**: use QEMU's hypervisor backends — HVF on Apple
   Silicon, WHVP on Snapdragon/Windows — so guest code runs at near-native
   speed. Emulation (TCG) is only a debugging fallback, never the target.
2. **Developer-friendly workflow**: reproducible build-and-run scripts for the
   Haiku ARM64 images, easy serial/console access, snapshot support, and fast
   edit–build–boot cycles for kernel and driver work.
3. **Incremental hardware enablement**: start from a working baseline
   (virtio devices: block, net, input, framebuffer via virtio-gpu), then layer
   on acceleration features one at a time.
4. **Everything versioned here**: build scripts, QEMU wrappers, patched
   sources, and documentation of what works on which host.

## Roadmap Sketch

1. Baseline: boot Haiku ARM64 image under QEMU with HVF on Apple Silicon.
2. Same baseline on Snapdragon/Windows via WHVP.
3. Stabilize virtio device support (disk, network, input, gpu).
4. Investigate virtio-gpu + host GPU passthrough/paravirtualization paths:
   - macOS: Metal-backed rendering (e.g. via GPU paravirtualization or a
     host-side rendering service).
   - Windows on ARM: DirectX-backed rendering.
5. Develop and iterate on Haiku video accelerants against those paths.

## Non-Goals

- Running Haiku x86_64 under emulation.
- Supporting hosts other than Apple Silicon macOS and Snapdragon Windows-on-ARM
  (others may work, but are not priorities).
