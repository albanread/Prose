# S2: the shared-surface display device (Prose Display, `PRDS`)

*Draft 1, 2026-09-18. The contract between the Haiku guest driver and the host device for phase S2 of [haiku_virtualized.md](../haiku_virtualized.md) §3.3. Everything tagged [measure] is a host property still to be confirmed on macOS 27 before the numbers are frozen.*

## 1. Purpose and scope

Haiku's app_server renders in software into a back buffer and copies dirty regions into the front buffer. With this device the **front buffer is host memory**: a shared memory region the guest maps write-back cacheable and the host wraps in a Metal (or Direct3D) buffer. The guest tells the host which rectangles changed; the host presents them at the display's refresh and tells the guest when frames were shown. No pixel is copied by the guest, and none by the host beyond what the GPU does to compose the window.

In scope for version 1:
- One scanout, one surface, `B8G8R8X8` pixels.
- Mode setting from a host-advertised mode list, live resize hints from the host.
- Commit with dirty rectangles; vsync events for pacing.
- Clean behaviour across device reset and host-side loss of the frame.

Reserved for later versions (fields and type ranges are set aside): hardware cursor, multiple surfaces (flipping), 2D command offload (fill, blit, composite), multiple scanouts, save/restore.

Non-goals: 3D, any compressed or planar format, audio.

## 2. Transport and identification

| Property | Value | Notes |
|---|---|---|
| Virtio version | 1.x, split virtqueues, little-endian | `VIRTIO_F_VERSION_1` mandatory. Packed rings are not used. `RING_INDIRECT_DESC`/`EVENT_IDX` may be offered; the guest driver does not ask for them |
| Virtio device ID | **63 (0x3F)** | Unassigned by the spec (allocations reach 46 in virtio 1.3). Must stay ≤ 63: Haiku's `virtio_pci` only binds modern PCI IDs `0x1040`–`0x107F`, i.e. `0x1040 + ID` |
| PCI class / subclass | `0x03` / `0x80` | Display controller, other. Same as VZ's own virtio-gpu, so firmware ignores it |
| Queues | 2: `controlq` (0), `eventq` (1) | Sizes ≥ 16 |
| Interrupts | INTx (or MSI-X if the guest ever gains it) | Haiku arm64 has no MSI today |
| Shared memory | region **ID 0**, the surface pool | Virtio PCI shared-memory capability (`cfg_type` 8, `virtio_pci_cap64`, `id` = 0) |
| Config space | 64 bytes, read-only for the driver | See §4 |

Device-specific feature bits (device bits 0–23):

| Bit | Name | Meaning |
|---|---|---|
| 0 | `PRDS_F_VSYNC` | The device can send `VSYNC` events |
| 1 | `PRDS_F_RESIZE` | The device can send `MODE_HINT` events (host window resizes) |
| 2 | `PRDS_F_BLIT` | 2D command family (§6.3). Not in version 1; a device must not offer it yet |
| 3–23 | reserved | Must not be offered |

A driver that accepts neither 0 nor 1 gets a static display with no pacing; that still works.

## 3. The surface pool (shared memory region 0)

- The region is host memory mapped into guest physical space at a fixed guest address for the life of the VM. Its size is fixed at VM creation. The device reports it in config (`shm_size`) and the transport reports it in the capability; the capability is authoritative.
- **Alignment.** The region base and size are multiples of 16 KiB (the macOS page size, which is also what Metal's `bytesNoCopy` needs) [measure: VZ's own region alignment]. Surface offsets inside the region must be multiples of 16 KiB.
- **Contents.** Undefined until the guest writes them, and undefined again after device reset. The host never writes the region in version 1 (reserved: `PRDS_F_BLIT` results).
- **Sizing rule.** `shm_size ≥ max_surfaces × align16k(max_height × align(max_width × 4, stride_align))`. Version 1 devices set `max_surfaces = 1`; a host that wants headroom for flipping later may already provide 2.
- **Guest mapping.** Write-back cacheable, inner shareable (`B_WRITE_BACK_MEMORY` on Haiku arm64, i.e. Normal WB). Never write-combining or device memory: those are Normal-NC / Device-nGnRnE on arm64 and would trap or crawl. Both host and guest run on the same coherent memory system on Apple Silicon and on x86, so **no cache maintenance is ever required, only ordering** (§7).
- **Pixel format** `PRDS_FORMAT_B8G8R8X8` (value 1): 32-bit pixels, bytes in memory order **B, G, R, X**; the X byte is undefined and must be ignored (Haiku's `B_RGB32` leaves it stale). The host presents with alpha forced to 1. Little-endian `uint32` view: `0x??RRGGBB`.

## 4. Configuration space

Fixed 64 bytes, all little-endian, read-only for the driver. Changes are announced through the virtio config-change interrupt **and** as `MODE_HINT` events; the driver must not rely on the interrupt alone (VZ's `update()` may not raise one [measure]).

```c
struct prds_config {
	uint32_t magic;          // 'PRDS' = 0x53445250
	uint16_t version;        // 1
	uint16_t flags;          // bit 0: host window can be resized by the user
	uint32_t max_width;      // largest mode the pool can hold
	uint32_t max_height;
	uint32_t pref_width;     // the host window's current size: the preferred mode
	uint32_t pref_height;
	uint32_t refresh_mhz;    // display refresh in millihertz, e.g. 60000; 0 = unknown
	uint32_t stride_align;   // bytes; mode strides must be multiples of this (host sets 64)
	uint32_t formats;        // bitmask of PRDS_FORMAT_*; bit 0 (B8G8R8X8) is always set
	uint32_t max_surfaces;   // 1 in version 1
	uint8_t  shm_region_id;  // 0
	uint8_t  reserved0[3];
	uint64_t shm_size;       // bytes, informational (the capability is authoritative)
	uint8_t  reserved1[12];  // must read as zero
};                           // 64 bytes
```

Rules: unknown `magic` or `version` → the driver refuses the device. `reserved*` must be zero. `pref_*` change when the host window is resized (with `PRDS_F_RESIZE`); everything else is constant for the life of the VM.

## 5. Message framing

Every request on `controlq` is one descriptor chain: a device-readable part (the request) followed by a device-writable part (the response, at least 16 bytes). Every `eventq` element is one device-writable buffer of at least 32 bytes that the driver keeps posted; the device fills it with one event and completes it.

```c
struct prds_hdr {                  // starts every request, response and event
	uint32_t type;                 // PRDS_CMD_*, PRDS_RESP_*, PRDS_EVT_*
	uint32_t flags;                // reserved, 0
	uint64_t seq;                  // requests: driver-chosen, echoed in the response;
	                               // events: device-chosen, increasing per event kind
};

struct prds_rect { uint32_t x, y, width, height; };

struct prds_resp {                 // minimal response; commands may append payload
	struct prds_hdr hdr;           // type = PRDS_RESP_*
};
```

Type numbers:

| Range | Kind |
|---|---|
| `0x0100`–`0x01FF` | commands, version 1 |
| `0x0200`–`0x02FF` | 2D command family (`PRDS_F_BLIT`, later) |
| `0x1000` | `PRDS_RESP_OK` |
| `0x1100`–`0x11FF` | errors: `0x1100 ERR_INVALID` (bad parameter), `0x1101 ERR_UNSUPPORTED` (unknown type or feature not negotiated), `0x1102 ERR_STATE` (no mode set, or pool not ready), `0x1103 ERR_BOUNDS` (rect or offset outside the surface/pool) |
| `0x2000`–`0x20FF` | events |

A response's `hdr.seq` equals the request's. Unknown command types get `ERR_UNSUPPORTED`; the device never drops a request without completing it. A response buffer shorter than the response the command produces is `ERR_INVALID`.

## 6. Commands (`controlq`)

### 6.1 `PRDS_CMD_SET_MODE` (0x0100)

```c
struct prds_set_mode {
	struct prds_hdr hdr;
	uint32_t surface_id;     // 0 in version 1
	uint32_t width, height;  // 1 ≤ width ≤ max_width, 1 ≤ height ≤ max_height
	uint32_t stride;         // bytes; ≥ width × 4, multiple of stride_align
	uint32_t format;         // PRDS_FORMAT_B8G8R8X8
	uint64_t offset;         // of the surface in the pool; multiple of 16 KiB;
	                         // offset + stride × height ≤ shm_size
};
```

Defines the surface the scanout presents from. Any previous mode is replaced. Until the first `COMMIT` after a `SET_MODE`, the host shows black (not stale memory). Response: `OK`, or `ERR_INVALID` / `ERR_BOUNDS` with the mode unchanged.

`width = height = 0` disables the scanout (host shows black; commits are `ERR_STATE`).

### 6.2 `PRDS_CMD_COMMIT` (0x0101)

```c
struct prds_commit {
	struct prds_hdr hdr;
	uint32_t surface_id;     // 0
	uint32_t rect_count;     // 0 = the whole surface; ≤ 64 per request
	struct prds_rect rects[];
};
```

"These rectangles of the surface are complete; show them." The host reads the listed pixels from the pool, folds them into its presentation image and presents at the next refresh. **Completion of the request means the host has finished reading those rectangles**; until then the guest must not write to them (it may write elsewhere in the surface). Rects are clipped to the surface; an empty result is not an error. Rects may overlap. The guest may have several commits in flight; the host applies them in order.

Response: `OK`; `ERR_STATE` if no mode is set.

Why no flip: app_server has no commit point today and always draws into its own RAM back buffer; the host folding dirty rects into a private image on commit is tear-free without double-buffering the pool, and the host controls when the frame is shown. A `surface_id` field is reserved so flipping can be added when app_server can render directly into the pool.

### 6.3 `PRDS_CMD_ENABLE_EVENTS` (0x0102)

```c
struct prds_enable_events {
	struct prds_hdr hdr;
	uint32_t mask;           // bit 0 VSYNC, bit 1 MODE_HINT, bit 2 REDRAW (always on)
	uint32_t reserved;
};
```

Selects which events the device sends. Default after reset: `REDRAW` only. `VSYNC` requires `PRDS_F_VSYNC`, `MODE_HINT` requires `PRDS_F_RESIZE`; requesting one that was not negotiated is `ERR_UNSUPPORTED`. The driver should enable `VSYNC` only while app_server wants pacing (for example while a retrace semaphore has waiters), since it costs an interrupt per refresh.

### 6.4 `PRDS_CMD_GET_INFO` (0x0103)

Request: header only. Response payload: `struct prds_config` as currently seen by the device. Lets the driver re-read the configuration when it cannot trust the config-change interrupt.

### 6.5 Reserved

`0x0110 SET_CURSOR` (hardware cursor), `0x0111 MOVE_CURSOR`, and the `0x02xx` family (`FILL_RECT`, `COPY_RECT`, `COMPOSITE`) are reserved. A version-1 device answers them with `ERR_UNSUPPORTED`.

## 7. Events (`eventq`) and the retrace semaphore

The driver posts at least 8 buffers of 32 bytes. If no buffer is posted when an event occurs, the event is dropped (events are advisory; the guest must not depend on receiving every one). Each event is 32 bytes:

```c
struct prds_evt_vsync {          // PRDS_EVT_VSYNC 0x2000
	struct prds_hdr hdr;         // seq = frame counter
	uint64_t timestamp_ns;       // host monotonic clock at the refresh
	uint64_t reserved;
};
struct prds_evt_mode_hint {      // PRDS_EVT_MODE_HINT 0x2001
	struct prds_hdr hdr;
	uint32_t width, height;      // the host window's new size; also in config pref_*
	uint64_t reserved;
};
struct prds_evt_redraw {         // PRDS_EVT_REDRAW 0x2002
	struct prds_hdr hdr;         // the host lost its presentation image
	uint64_t reserved[2];        // (restore, GPU reset): commit the whole surface
};
```

The driver releases Haiku's retrace semaphore on every `VSYNC`. `MODE_HINT` is a suggestion: the driver reports a new preferred mode; app_server decides whether to switch (and does so with `SET_MODE`, which changes the surface geometry, then repaints).

## 8. Ordering and memory model

1. The guest writes pixels to the surface (cacheable stores, any width).
2. The guest builds the `COMMIT` request in its own memory, then publishes the descriptor: Haiku's virtio bus manager issues a full write barrier (`dmb`) before updating `avail->idx`, then notifies. That barrier is the **release** point: it orders the pixel writes before the descriptor becomes visible.
3. The host observes the new `avail->idx` (the VZ framework does this on the guest's notification) and then reads the pixels. Reading the index before the data is the **acquire** side; the framework's element handling provides it.
4. The host completes the element (used ring update, another release) only after it has finished reading the rectangles.
5. The guest's dequeue of the used entry (acquire) is its permission to write those rectangles again.

Because both sides run on one coherent memory system, no cache flush or invalidate is ever needed. The same holds on x86 hosts (QEMU/Windows). A future non-coherent transport would need an explicit flush step here; the protocol reserves `hdr.flags` for it.

## 9. Lifecycle

| Situation | Behaviour |
|---|---|
| Driver load | Negotiate features; read config; verify `magic`/`version`; map the pool WB; post event buffers; `ENABLE_EVENTS`; `SET_MODE` (preferred mode); full `COMMIT` |
| Device reset (virtio status 0) | Mode cleared, events disabled, no presentation (black). Pool contents undefined. The driver repeats the load sequence |
| Guest reboot | As device reset. The host's guest-RAM mappings are invalid across reboot; the pool mapping is not guest RAM and survives |
| Host window resize | With `PRDS_F_RESIZE`: config `pref_*` updated, `MODE_HINT` sent. Without: the host scales the current mode into the window |
| Host lost its image | `REDRAW` event; the guest commits everything |
| Host cannot map the pool yet (VZ `mapMemory` is asynchronous) | `SET_MODE`/`COMMIT` answer `ERR_STATE`; the driver retries after a short delay. In practice the mapping completes long before the guest driver loads |
| Save/restore | Not supported in version 1; the device reports `supportsSaveRestore = false` on VZ |

## 10. Implementation notes

### 10.1 Host: `hvgpu` on Virtualization.framework (macOS 27)

- `VZCustomVirtioDeviceConfiguration`: `deviceID = 63`, `pciClassID = 3`, `pciSubclassID = 0x80`, `virtioQueueCount = 2`, `optionalFeatures = VSYNC | RESIZE`, `deviceSpecificConfiguration` = the 64-byte `prds_config`, `sharedMemoryRegions = [ (regionID 0, size) ]`. Region size: 64 MiB by default (two 4K surfaces' worth).
- The pool is `mmap`ed anonymous shared memory (`MAP_ANON | MAP_SHARED`), 16 KiB aligned. `mapMemory(ptr, atOffset: 0, size:)` must be called **on the device queue** (`dispatch_assert_queue` traps otherwise) and **only once the VM is running** (before `start` completes it fails with "The virtual machine is not live"), so `hvgpu` maps it from the `start` completion handler, not from `didCreateDevice`. Measured (macOS 27.0): VZ allows one shared memory region per device; a 64 MiB region maps in well under a millisecond and lands in the guest as a 64-bit memory BAR (BAR 4 at `0x180000000`, above the 4 GiB RAM window). Wrapping the pool itself for Metal is still untested; milestone A copies commits into the presenter's own surface.
- **Tear-free by construction (the three-buffer model: `dispbuf ← fb ← bb`).** Only app_server knows when the front buffer is ready, so the host never presents the pool itself. `COMMIT` is the ready signal: on the device queue the host copies exactly the committed rects from the pool (fb) into its private display buffer (dispbuf) under the *presentation lock*, and completion means the copy is done — the guest's commit is synchronous, so app_server cannot write those rects again while the host reads them (§10.3). The presenter renders from dispbuf under the same lock and waits for the GPU (`waitUntilCompleted`) before releasing it, so a frame can never contain a half-copied rect either. The drawable is acquired before the lock (`nextDrawable()` may block for a frame).
- No fixed frame window: the presenter is change-driven. A display-link tick re-renders only when a commit has landed since the last one; an unchanged screen keeps showing dispbuf, and a guest that commits at the display's rate gets 120 fps on a 120 Hz screen.
- The pool is also mapped for Metal (`makeBuffer(bytesNoCopy:)`, page aligned, shared storage; it maps and needs no explicit synchronization). It is unused today, but it makes both guest buffers GPU-accessible: the copy fb → dispbuf can become a blit kernel, and once the back buffer lives in the pool too (§10.3), bb → dispbuf directly. (Milestone C briefly sampled the pool with the shader; that showed torn rects and is gone.)
- "Black until the first commit after `SET_MODE`" holds: dispbuf is cleared at `SET_MODE` and the presenter paints black until the first commit of the new mode.
- Measured (1280x800 desktop): host copy 1 µs average per commit (typical rects are tiny), 0.6 ms for a full frame at boot; the first touch of pool pages costs ~1.3 ms per 4 MiB on both sides (lazy host allocation), after that the pool reads and writes at RAM speed from the guest (4 MiB in 133 µs, same as the heap).
- Presentation: `CADisplayLink` on the window; each tick presents if a commit landed, and emits `VSYNC` (when enabled) with the tick's timestamp. Window resize → config update + `MODE_HINT`.
- **The display link stops when the host display sleeps** (and in `--headless` runs there is none), so `VSYNC` events can stop at any time for any length of time. The guest must never wait on the retrace semaphore without a timeout; the driver and accelerant treat a missing vsync as "present immediately".
- The RAM console, input handling and the overlay window carry over from S1. VZ's own virtio-gpu stays attached until we have our own input devices (Sprint 3).

### 10.2 Host: QEMU (development baseline and the Windows track)

A `prose-display` device in our QEMU fork: `virtio_pci_add_shm_cap()` exposes a host-allocated RAM `MemoryRegion` as region 0; commits update a `DisplaySurface` created over the pool (`qemu_create_displaysurface_from`, zero-copy on QEMU too), and the console's refresh provides `VSYNC`. Same contract, so the guest driver is developed and debugged here first (gdbstub, serial, `-snapshot`), then run unchanged on VZ.

### 10.3 Guest: Haiku (`vz-fork`)

- `busses/virtio/virtio_pci`: parse capability type 8 (`virtio_pci_cap64`: `id`, `bar`, 64-bit `offset`/`length`); bus-manager API `get_shared_memory(device, id, &physical, &size)`.
- Kernel driver `drivers/graphics/prose_display` (patches 0014/0015): binds to virtio device 63; maps the pool with `map_physical_memory(..., B_ANY_KERNEL_ADDRESS | B_WRITE_BACK_MEMORY, ...)` (the memory type travels in the address-spec argument); `PRDS_CLONE_POOL` clones it into the accelerant's team with `vm_clone_area(..., kernel = true)`, which keeps the memory type; owns both queues; ioctls `PRDS_SET_MODE` (a `display_mode`), `PRDS_COMMIT` (≤ 64 rects), `PRDS_ENABLE_EVENTS`; publishes `/dev/graphics/prose_display/0`. `VSYNC` releases the retrace semaphore only when someone waits on it (no runaway count), and the semaphore's owner becomes the opening team at `open()`: user space may not acquire kernel-owned semaphores (the kernel logs "tried to acquire kernel semaphore"), and a semaphore dies with its owner, so it is recreated for the next opener. The shared-info area is kernel-only + `B_CLONEABLE_AREA` (arm64 cannot map kernel-RW/user-RO). Until the accelerant opens the device the driver shows a test pattern (`PRDS_TEST_PATTERN`), which is how the pipe was verified before the accelerant existed.
- Accelerant `prose_display.accelerant`: the frame buffer app_server draws into *is* the pool (zero copy). Mode list from `create_display_modes()`: the host's preferred size plus the standard modes that fit `max_*`, B_RGB32 only; `B_GET_PREFERRED_DISPLAY_MODE` follows the host window (`pref_*`, updated by `MODE_HINT`); `B_GET_FRAME_BUFFER_CONFIG` = pool + `mode_offset`, `bytes_per_row` = stride; `B_ACCELERANT_RETRACE_SEMAPHORE` = the driver's semaphore. The commit hook is synchronous: it sends `PRDS_COMMIT` itself (≤ 64 rects) and returns only when the host has copied them, which is what keeps app_server from overwriting a rect the host is still reading — the tear-free guarantee lives here, not in any pacing. (An earlier version collected rects and committed once per vsync from a thread; the 16 ms window it left is exactly where tearing came from.) Measured: 50 µs average round trip guest→host→guest, rare 19 ms outliers (interrupt/scheduling latency, see §12), 0 errors.
- app_server (fork): `Accelerant.h` gains the optional hook `B_COMMIT_RECTANGLES` (0x500), `status_t commit_rectangles(const fill_rect_params* list, uint32 count)`. `AccelerantHWInterface` calls it after `_CopyBackToFront(region)` with the region's rectangles (32 per call) and from `_DrawCursor()`, because the software cursor is drawn into the front buffer past the back buffer. Accelerants without the hook are unaffected; `_DrawCursor()` commits only the cursor's own frame. Measured: an idle desktop commits ~70 times/s — almost all of it the Deskbar's CPU-load replicant redrawing eight 2x1-pixel bar columns at 4 Hz, each as its own `Invalidate()` (`hvgpu --commit-stats` shows the histogram); at 50 µs a commit that is 0.4 % of a core.
- Live resize (patch 0016): `MODE_HINT` updates `pref_*` in the shared info and wakes `PRDS_WAIT_MODE_HINT`, an ioctl that blocks (with timeout, interruptible) inside the driver — so the semaphore behind it stays kernel-owned. `prose_display_agent`, a small server launched from `data/launch/system`, waits on it and switches the screen with `BScreen::SetMode()` (timing from `compute_display_timing()`, any size up to `max_*`), coalescing the burst a drag produces; the accelerant accepts the size, the driver sends `SET_MODE`, app_server re-reads the frame buffer config and repaints. Measured: host window resize → guest desktop at the new size in ~160 ms. `REDRAW` bumps `redraw_count` in the shared info and the accelerant's commit thread answers with a full-surface commit (the host does not send `REDRAW` yet).

## 11. Verification

- **Framing tests** (host, no VM): pack/unpack every structure; sizes: `prds_hdr` 16, `prds_rect` 16, `prds_config` 64, events 32.
- **Boot test** (`private_workspace/run-vz.sh <name> --display s2`): guest markers `prose_display: pool at …, mapped write-back`, `prose_display: mode WxH, stride S`, `test pattern committed`, `publish device: … graphics/prose_display/0`; host markers `prds: pool mapped into the guest`, `prds: DRIVER_OK`, `prds: mode …`, and the 10-second stats lines on both sides (`commits`, `vsyncs`; the two vsync counts must match and `events dropped` must stay 0).
- **Pixel test**: milestone A's kernel test pattern (eight colour bars, a diagonal, a moving box); `hvgpu --screenshot PATH` dumps the presentation surface as PNG with every stats line; colours and geometry check out at 1280x800 (B8G8R8X8 read as BGRX, stride 5120 in the pool, 15360 in the 3840-wide presentation surface). Later: a Haiku test app and a pixel-exact compare, the T5 presenter self-test.
- **Latency**: `hvgpu` logs commit → completion time and commit → presented-frame time (from the display link) percentiles; targets: completion < 1 ms for a 1080p rect, presentation on the next refresh.
- **Tear test**: commits must be synchronous (accelerant log `round trip avg`), the host copy must happen under the presentation lock, and `hvgpu --commit-stats` shows what is being committed; a torn rect would need the guest to write a rect between its commit and the host's copy, which the synchronous commit forbids.
- **Live resize** (`run-vz.sh <name> --display s2 --resize-after 45 1600x1000`): host log `prds: window resized, hinting WxH` then `prds: mode WxH stride S`; guest syslog `prose_display_agent: 1280x800 -> WxH: No error`; the screenshot after the switch has the new size. (macOS may constrain the requested window size to the screen; the hint carries the real content size.)
- **Lifecycle**: guest reboot, `SET_MODE` to a different size and back, resize the host window with and without `PRDS_F_RESIZE`, and the black-until-first-commit rule.

## 12. Open questions to settle on the host before freezing version 1

1. ✅ Shared-region alignment and size on VZ: a 16 KiB-aligned `mmap`ed 64 MiB pool maps; one region per device; it must be mapped after the VM starts, on the device queue (§10.1). Memory wrapped for Metal with `makeBuffer(bytesNoCopy:)` maps fine, and the GPU sees the guest's writes without any explicit synchronization (Apple silicon unified memory, shared storage mode).
2. ✅ `update(_:)` does raise the config-change interrupt: the driver's config callback logged "configuration changed" right after the host window resize, alongside the `MODE_HINT` event. `GET_INFO` polling is never needed.
3. Partly measured: with `VSYNC` enabled the guest received every event at 60 Hz (host and guest counts identical, none dropped, eight 32-byte buffers never ran dry) while the ~10 commits/s test pattern ran. Latency numbers and the 120 Hz case are still to be measured, and the display-sleep behaviour in §10.1 means the guest side needs timeouts regardless.
4. Whether VZ's own virtio-gpu can be dropped once we have our own input devices; until then two display devices coexist (patch 0002 keeps Haiku off VZ's).
5. Commit round-trip outliers: 50 µs average but a 19 ms maximum every few thousand commits. Candidates: `virtio_pci`'s interrupt handler returns `B_HANDLED_INTERRUPT` even when a queue callback woke a thread (`B_INVOKE_SCHEDULER` would reschedule at once), or host-side scheduling of the device queue. Worth fixing before a GPU blit path makes commits longer.
6. Next steps toward the ideal `dispbuf ← fb ← bb` with no CPU copies: put app_server's back buffer into the pool (RAM-speed reads make it viable; pre-touch it at init), then let a host blit kernel copy bb → dispbuf on the ready signal and drop the fb from the normal path (it stays for BDirectWindow clients, which draw into the front buffer on their own schedule), and move the cursor to a host overlay through the `B_SET_CURSOR_SHAPE`/`B_MOVE_CURSOR` hooks so cursor moves no longer touch the front buffer at all. Pointer swapping (flipping the three buffers) does not fit app_server's damage model — a flipped-in buffer lacks the previous frames' damage and would need catch-up copies of the same size — so the win is in *who* copies (the GPU), not in avoiding the copy.
