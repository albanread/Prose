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
	uint8_t  reserved1[16];  // must read as zero
};
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
- The pool is `mmap`ed anonymous shared memory, 16 KiB aligned, wrapped once with `makeBuffer(bytesNoCopy:)`; `mapMemory(ptr, atOffset: 0, size:)` maps it into the guest in `didCreateDevice`. The pointer must be plain host memory (the VM runs in an XPC service) [measure: alignment and size limits, whether Metal-wrapped memory maps].
- `COMMIT`: for each rect, a compute or blit pass copies from the buffer (any stride, via the same direct-sampling shader the presenter uses) into the presentation texture; the element is returned from the command buffer's completion handler, so completion means the GPU is done reading. A first implementation may `memcpy` on the device queue instead; the contract is the same.
- Presentation: `CADisplayLink` on the window; each tick presents if a commit landed, and emits `VSYNC` (when enabled) with the tick's timestamp. Window resize → config update + `MODE_HINT`.
- The RAM console, input handling and the overlay window carry over from S1. VZ's own virtio-gpu stays attached until we have our own input devices (Sprint 3).

### 10.2 Host: QEMU (development baseline and the Windows track)

A `prose-display` device in our QEMU fork: `virtio_pci_add_shm_cap()` exposes a host-allocated RAM `MemoryRegion` as region 0; commits update a `DisplaySurface` created over the pool (`qemu_create_displaysurface_from`, zero-copy on QEMU too), and the console's refresh provides `VSYNC`. Same contract, so the guest driver is developed and debugged here first (gdbstub, serial, `-snapshot`), then run unchanged on VZ.

### 10.3 Guest: Haiku (`vz-fork`)

- `busses/virtio/virtio_pci`: parse capability type 8 (`virtio_pci_cap64`: `id`, `bar`, 64-bit `offset`/`length`); bus-manager API `get_shared_memory(device, id, &physical, &size)`.
- Kernel driver `drivers/graphics/prose_display`: binds to virtio device 63; maps the pool with `B_WRITE_BACK_MEMORY` into a kernel area the accelerant can clone; owns both queues; ioctls for mode set, commit (dirty rects), event mask; releases the retrace semaphore on `VSYNC`; publishes `/dev/graphics/prose_display/0`.
- Accelerant `prose_display.accelerant`: mode list from `pref_*`/`max_*` plus standard modes that fit; `B_GET_FRAME_BUFFER_CONFIG` returns the cloned pool mapping with the mode's stride; real `B_ACCELERANT_RETRACE_SEMAPHORE`; a new private hook `PRDS_COMMIT(rects)` reached from app_server.
- app_server (fork): after `_CopyBackToFront(region)`, call the accelerant's commit hook with the region's rectangles (≤ 64 per call; coalesce beyond that). Optional pacing: bounded wait on the retrace semaphore before copying (Sprint 3).

## 11. Verification

- **Framing tests** (host, no VM): pack/unpack every structure; sizes: `prds_hdr` 16, `prds_rect` 16, `prds_config` 64, events 32.
- **Boot test** (`private_workspace/run-vz.sh`): markers `prose_display: bound`, mode set, first commit, `VSYNC` count > 0 with events enabled.
- **Pixel test**: a Haiku test app draws a known pattern; `hvgpu --screenshot` dumps the presentation texture; compare pixel-exact (alpha 255, colours intact) — the same check the T5 presenter self-test does.
- **Latency**: `hvgpu` logs commit → completion time and commit → presented-frame time (from the display link) percentiles; targets: completion < 1 ms for a 1080p rect, presentation on the next refresh.
- **Lifecycle**: guest reboot, `SET_MODE` to a different size and back, resize the host window with and without `PRDS_F_RESIZE`, and the black-until-first-commit rule.

## 12. Open questions to settle on the host before freezing version 1

1. Shared-region alignment and maximum size on VZ, and whether `mapMemory` accepts memory already wrapped by Metal (or must be `mmap`ed first and wrapped after).
2. Whether `update(_:)` raises the config-change interrupt; the protocol does not depend on it, but the answer decides if `GET_INFO` polling is ever needed.
3. Interrupt latency from `returnToQueue()` to the guest handler at 60–120 events per second, to decide whether `VSYNC` can stay on permanently.
4. Whether VZ's own virtio-gpu can be dropped once we have our own input devices; until then two display devices coexist (patch 0002 keeps Haiku off VZ's).
