---
name: prose-qemu-dev
description: Develop and test Prose/Haiku arm64 applications on QEMU in this repo. Use when building, deploying, launching, scripting or screenshot-testing apps in the Prose guest, or when working in prosewriter/. Covers the headless QEMU machine, the guest automation agent, cross-compiling with the Prose toolchain, and installing files into the dev image.
---

# Developing Prose/Haiku apps under QEMU

Everything lives under `prosewriter/` in this repo. **Never touch the Prose
build tree** (`/Volumes/HaikuSrc/haiku`) or the built image — read-only use
only, and boot only `prosewriter/vm/prose-dev.image` (our own APFS clone).

## The machine

- VM: `prosewriter/vm/run.sh` — QEMU/HVF arm64, headless, ramfb display,
  user-mode networking (host is `10.0.2.2` from the guest), QMP+HMP sockets
  in `prosewriter/vm/`, agent port forwarded `127.0.0.1:9000 → guest :9000`.
- Start it: `prosewriter/vm/run.sh &` (background), then wait for the agent:
  `prosewriter/vm/guest.sh wait-boot 180`.
- The guest's `UserBootscript` (`prosewriter/vm/harness-bootscript`, baked
  into the image) keeps **proseagent** alive; it answers over TCP.

## The commands (all from the repo root)

| Do this | Command |
|---|---|
| Run a shell command in the guest | `prosewriter/vm/guest.sh run 'ls /boot/system/apps'` |
| Put a file into the guest | `prosewriter/vm/guest.sh put <local> <guest-path>` |
| Get a file out | `prosewriter/vm/guest.sh get <guest-path> [out]` |
| Launch a GUI app | `prosewriter/vm/guest.sh launch /boot/home/apps/Foo` |
| Update the agent itself | `prosewriter/vm/guest.sh deploy prosewriter/app/proseagent /boot/home/apps/proseagent` |
| Screenshot (host side, whole screen) | `prosewriter/vm/qmp.py shot out.png` |
| Press keys / type / click | `prosewriter/vm/qmp.py key t`, `… key ctrl-alt-q`, `… move 400 300`, `… click` |
| Screenshot (guest side) | `prosewriter/vm/guest.sh screenshot out.png` |
| Shut down cleanly | `prosewriter/vm/qmp.py hmp 'system_powerdown'` |
| Install into the image (VM off) | `prosewriter/vm/install.sh <local> /boot/home/...` |
| Extract from the image (VM off) | `prosewriter/vm/extract.sh /home/... [out]` |

`guest.sh run` returns the guest command's stdout/stderr plus an `EXIT n`
line; its own exit status mirrors the guest's.

## Building Haiku arm64 apps on this Mac

```
cd prosewriter/app
./mksysroot.sh        # once: private symlink sysroot from the Prose packages
make ProseWriter      # or: make proseagent / make hello
```

- Cross-compiler: `/Volumes/HaikuSrc/haiku/generated/cross-tools-arm64/bin/
  aarch64-unknown-haiku-g++` (GCC 13, used read-only).
- The Makefile does the rest: sysroot include/lib paths, `-lbe -lroot
  -ltracker -lnetwork -lstdc++ -lgcc_s`.
- The guest image ships `libstdc++.so.6`, so plain dynamic linking is fine.

## The dev loop

1. Edit sources in `prosewriter/app/src/`.
2. `make -C prosewriter/app ProseWriter`
3. `prosewriter/vm/guest.sh put prosewriter/app/ProseWriter /boot/home/apps/ProseWriter`
   (overwrite of a *running* app is unsafe; `run killall ProseWriter` first,
   or use `guest.sh deploy`)
4. `guest.sh launch /boot/home/apps/ProseWriter`
5. `vm/qmp.py shot vm/run/shot.png`, then look at the PNG; drive input with
   `qmp.py key/move/click`; check console output with `guest.sh run`.

If the guest wedges: `qmp.py hmp 'system_powerdown'` (clean) or `hmp 'quit'`
(instant). The image boots to the desktop in ~2 min; first boot after a
fresh copy is slower (package daemon).

## Testing patterns

- In-process self tests: give the app a `--selftest` mode that prints
  `PASS`/`FAIL` lines and exits — run it with `guest.sh run`. ProseWriter is
  the example (`src/PWApp.cpp`, `SelfTest()`).
- Screenshot diffs: keep shots under `prosewriter/vm/run/` and compare.
- The Be way: every app answers `hey` (scripting suites) if it's installed
  in the image — `guest.sh run 'hey App get Title of Window 1'`.

## Failures seen here

- `put` over the **running** proseagent binary wedges it — always `deploy`.
- fs_shell (`install.sh`) paths are volume-relative: `/boot/home/x` must be
  written as `/home/x`.
- A fresh image copy shows FirstBootPrompt; `skip-first-boot-prompt.sh`
  (already applied to prose-dev.image) seeds the Locale settings file.
