# A day on PROSE — 18 September 2026

One long session. Haiku on Apple Silicon went from "boots to a desktop" to
something you can sit in front of: a tear-free display that follows
the window, our own keyboard and mouse, networking, sound, MIDI played by the
Mac, image decoding, and a look of its own.

Two tracks ran in parallel — this one (host integration and the fork) and a
colleague's package-building track. Both commit to `vz-s1`.

---

## What works now that didn't this morning

### Display — the Prose Display device (S2)

The whole pipe was built and then rebuilt three times as we learned what
actually matters.

- **The guest draws into the Mac's memory.** app_server draws into the device's shared
  surface pool — both its back buffer and the front buffer live there
  (patch 0018), so the host's GPU can reach everything the guest draws.
- **Tear-free by construction** (patch 0017). Only app_server knows when a
  frame is ready, so its back-to-front copy is the ready signal: the commit is
  *synchronous*, the host copies exactly the committed rectangles into its own
  display buffer, and the presenter renders from that under the same lock,
  waiting for the GPU before releasing it. An earlier design sampled the pool
  directly from the shader — it tears, and is gone.
- **Live resize** (patches 0016, 0020). Drag the window; the guest switches
  mode once the corner stops, ~160 ms later. A `MODE_HINT` can't be lost, and a
  drag no longer produces a switch every 170 ms trailing the mouse.
- **Analogue TV static** while the guest has no picture, instead of black: a
  procedural shader with horizontally-smeared grain, scanlines, a drifting hum
  bar, rare vertical-hold slips and corner falloff. The guest's colour-bar test
  card is off by default now.

Measured: ~70 commits/s on an idle desktop (almost all of it the Deskbar CPU
meter redrawing eight 2×1-pixel bars), 50 µs commit round trip, 1 µs host copy
for a typical rect.

### Input — our own devices

`tools/hvgpu/input.swift` provides a virtio keyboard and absolute tablet
(patch 0019). VZ's graphics device, USB keyboard, pointing device and
`VZVirtualMachineView` are all gone from the VM: the window is entirely ours
and pointer coordinates map through the presenter's own geometry, so they stay
exact at any size. 25 minutes of real use: 2552 key events, 20118 tablet
events, none dropped.

### Networking (patch 0021)

Never worked under VZ. Two causes, both real bugs:

- `virtio_net` used the 10-byte legacy header on a `VIRTIO_F_VERSION_1` device,
  where it is 12 bytes — every frame misframed in both directions. Latent on
  QEMU too since we started driving transitional devices as modern.
- Apple's virtio-net refuses `FEATURES_OK` for any feature set without
  `VIRTIO_NET_F_CSUM`, and the driver ignored the negotiation error and carried
  on with no features at all: no MAC, device stuck in FAILED.

Now: DHCP lease in the first second, gateway 0.2 ms, 1.1.1.1 ~29 ms, DNS,
1400-byte pings. Same driver verified on QEMU.

### Sound (patch 0022) and MIDI (patch 0023)

A real `hmulti_audio` driver for virtio-snd — the stub was 134 lines. The
mixer's buffers *are* the txq buffers, and a completion is the "buffer played"
event, so the Mac's audio clock paces Haiku's mixer. Verified by recording a
440 Hz tone out of QEMU and by playing it through the Mac under VZ.

Then MIDI: the Prose MIDI device appears as `/dev/midi/prose/0`, `midi_server`
turns it into a port, and what the guest plays goes to Core Audio's General
MIDI synth and to a CoreMIDI virtual source. A scale played through the Midi
Kit came out of the Mac's speakers.

### Build — no remote package resolution

The build used to fetch third-party packages from `eu.hpkg.haiku-os.org`, with
the package list checksummed so a locally built package could not be added at
all. With `HAIKU_NO_DOWNLOADS=1` the build synthesises the repository from
whatever is in `generated/download/`. `private_workspace/addpkg.sh` imports a
package from the colleague's tree in one command.

That unlocked the image formats: **15 translators** now ship — BMP, GIF, HVIF,
ICO, PCX, PPM, PSD, RTF, SGI, STXT, TGA (which needed nothing but listing and
were simply absent), plus JPEG, PNG, TIFF and WebP from the colleague's
libjpeg-turbo, libpng16, tiff, libwebp and giflib.

### Look

- New wallpapers rendered by `tools/artwork/artwork.swift`: ink or paper, one
  broad-nib pen stroke through the lower third, grain, vignette, quiet corners.
- `tools/artwork/hvif.py` — a minimal HVIF writer, because Haiku vector icons
  can otherwise only be authored in Icon-O-Matic, which needs a running Haiku.
- About this system says what this is: an unofficial experimental port, not
  affiliated with or supported by the upstream project, and that everything
  which makes it an operating system is their developers' work. It uses none of
  their marks and claims nothing but gratitude.

---

## Things learned that cost time

Worth writing down because each one wasted an hour or more.

- **Custom virtio device IDs must be ≤ 63.** PCI device ID is `0x1040 + ID`;
  64 lands outside the modern range `virtio_pci` accepts, and the device is
  simply invisible. The display took 63; MIDI is 62.
- **A virtio child's driver is searched exactly once**, when its node is
  registered, in the context of whichever `/dev/<class>` scan triggered it. A
  multimedia-class device is only ever probed during the `/dev/audio` scan, so
  the MIDI driver lives under `drivers/audio/hmulti` even though it publishes
  `/dev/midi/prose/0`.
- **`/dev/<class>` directories were never published after boot** — an upstream
  `// TODO`. Without it a device-manager audio driver is unreachable on a system
  with no legacy audio driver, which is why there was no `/dev/audio` at all.
- **The image profile asked for translators that were never packaged.** Every
  profile set `SYSTEM_ADD_ONS_TRANSLATORS`; `build/jam/packages/Haiku` had no
  line adding it. No translator ever reached the image.
- **The multi_audio node drops a mixer buffer** that arrives while the previous
  one is unconsumed, and runs only one period ahead. Hosts complete in bursts
  (QEMU's 10 ms timer), so the driver must hand buffers out on a steady
  schedule anchored to the device's completions.
- **User space may not acquire kernel-owned semaphores.** Graphics drivers hand
  the vblank semaphore to the opening team at `open()`; it dies with that team,
  so it must be recreated. A wait that must stay kernel-owned goes inside an
  ioctl.
- **Locally built packages must carry the vendor the repository expects**
  (`Haiku Project`) — `package_repo` rejects anything else outright.
- **`CADisplayLink` stops when the host display sleeps.** "0 vsyncs" is usually
  the Mac being asleep, not a bug. The guest never waits on vsync without a
  timeout.
- **The accelerant's pool-bandwidth test overwrites the end of the pool.** At
  init it memsets the last 4 MiB with `0x5a`, which is exactly where the cursor
  block sits (`poolSize - 128 KiB`), so a host cursor overlay reads a block of
  `0x5a`. The overlay was reverted on time grounds; this is the two-line fix if
  it comes back.
- **A headless `--screenshot` run captures a desktop that never repainted.** It
  dumps the surface pool, and with no window, no display link and no input
  app_server never draws the background into the front buffer, so the capture
  shows the pre-wallpaper state however long the run is. Check the desktop in a
  windowed run.
- **`run-vz.sh` copied a fresh image every run**, which is right for tests and
  wrong for use: Tracker writes the desktop background at first boot and renders
  it from the *next* one, so it never appeared. `--keep` reuses the disk.
- **The inherited artwork's wiring had three faults**: logos of 1920×626
  labelled "tiny", which would have blown out the About window about 12×;
  deletion of the file Tracker hardcodes as the default background; and
  wallpapers shipped that nothing referenced.

---

## Open, in the order I'd take them

1. **The Deskbar mark doesn't show.** The HVIF is structurally valid — it
   round-trips cleanly through Haiku's own parse order — but the stroke spans
   only y 21–45 of the 64-unit grid, and the Deskbar squashes that grid into 22
   pixels, so it draws about 8 pixels tall and vanishes. Widen the swash. Small.
2. **NEON in the blit path.** NEON is *already on* — it is mandatory on AArch64
   and our userland binaries contain it (`libroot.so` 64 instructions,
   `libbe.so` 61). The `-fno-tree-vectorize` in the tree is kernel-only and
   deliberate (Haiku #18593: GCC 13 autovectorisation breaks in virtual
   machines — which is us). What is left is the ARMv8.0 baseline: either raise
   it (`-mcpu=apple-m1`), which forfeits the Snapdragon/WHPX goal, or hand-
   optimise the hot paths with runtime dispatch. Measure first — commits are
   memory-bandwidth-bound row copies.
3. **ffmpeg.** The colleague has vorbis, opus, flac, speex, mpg123, lame,
   theora, vpx, dav1d and wavpack built, but Haiku decodes media through its
   ffmpeg plugin — those libraries do nothing for the media kit on their own.
   ffmpeg is the package that would actually give playback.
4. **Audio capture** (the rxq — same shape as playback, VZ's input stream is
   already attached), the cursor overlay retry, and the 19 ms commit-latency
   outliers.
5. Later: the QEMU `prose-display` and virtio-input devices for the Windows
   track.

---

## Fork patches

`patches/haiku/0001`–`0023`, with `README.md` alongside. 0009 onward are
`git format-patch` output from the `vz-fork` worktree. Today added:

| | |
|---|---|
| 0014 | virtio shared memory + `prose_display` driver |
| 0015 | accelerant + the `B_COMMIT_RECTANGLES` app_server hook |
| 0016 | live resize agent |
| 0017 | synchronous, tear-free commits |
| 0018 | app_server's back buffer in the pool |
| 0019 | `virtio_input` static configuration table |
| 0020 | resize settle |
| 0021 | `virtio_net` modern header, CSUM, MTU |
| 0022 | `virtio_sound` driver + media stack |
| 0023 | Prose MIDI port |

Plus, uncaptured as numbered patches but committed in the worktree: the
branding, wallpaper, translators, local-package build and trademark removal.
