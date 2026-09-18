# Prose — brand and rebrand map

Prose is our Haiku fork's identity (Haiku upstream does not accept
AI-assisted contributions — see haiku_virtualized.md §1.5 — and we are a
long-lived fork regardless). Sources: `svg/` (generate with
`make_assets.py`), renders: `png/` (`tools/render.sh`). The applied fork
patches live in [../patches/prose-branding-01-artwork.patch](../patches/prose-branding-01-artwork.patch),
based on `hrev60122` (`55d56e03a0`).

## Identity

* **Name:** Prose — an operating system for people who write.
* **Kernel:** Kronkite — after the most trusted man in news. Sign-off, used
  at shutdown and on the boot splash: *…and that's the way it is.*
* **Mark:** the **pilcrow (¶)**, the paragraph mark — typography itself.
  Geometry: a left half-annulus bowl (outer r57, inner r27, centre 146,107
  in a 240-unit box) fused to a rounded stem (x 118–146, y 50–220). Stroke
  weight of bowl and stem match (28 units).
* **Palette**
  | name | hex | use |
  |---|---|---|
  | Paper | `#F5EFE3` | warm background, dark-mode glyph |
  | Ink | `#1B1917` | primary text and glyph |
  | Prose Blue | `#2F5AA8` | rules, accents (a nod to Haiku blue) |
  | Vermilion | `#D6482B` | the proofreader's red: accents, sub-lines, flourish |
* **Type:** Georgia / Times serif for the wordmark (`Prose`, bold), with
  letterspaced small caps for sub-lines (`K R O N K I T E`).
* **Assets:** logos (paper-on-ink, ink-on-paper, transparent lockups),
  mark-only, boot splash concept, and the **Manuscript** wallpapers
  (light/dark, 3840×2160): ruled paper, a 5%-ink ghost pilcrow, one vermilion
  proofread flourish, the lockup bottom-left, an asterism (⁂) top-right.

## Rebrand map — where Haiku's identity lives

| Touchpoint | Location in the tree | Status |
|---|---|---|
| Logo artwork package | `data/artwork/` + `build/jam/packages/Haiku` (`logoArtwork`) | **patched** — ships PROSE logo + wallpapers, drops trademarked logos |
| AboutSystem logo | `src/apps/aboutsystem/AboutSystem.rdef` (imports `logo.png`, `logo_dark.png`, `walter_logo.png`) | **patched** — all three import the PROSE lockups |
| Kernel name (`uname -s`) | `src/system/libroot/posix/sys/uname.c:39` | **patched** — `sysname` = `Kronkite` |
| Trademark exclusion | `build/jam/BuildSetup` `HAIKU_DISTRO_COMPATIBILITY` (configure `--distro-compatibility compatible`) | recommended at next configure; keeps upstream trademark art out |
| Boot splash (BIOS path) | `src/system/boot/platform/generic/video_splash.cpp` — logo is a compressed byte array baked at build time | TODO: regenerate arrays from `boot-splash.png` |
| EFI loader boot menu title/icons | `src/system/boot/loader/` (menu title, HVIF icons) | TODO — this is what arm64 Prose shows at boot |
| Deskbar leaf icon | `src/apps/deskbar/Deskbar.rdef`, `icons.rdef` (HVIF vector icons) | TODO: needs an HVIF pilcrow (Icon-O-Matic or hvif tooling) |
| "Haiku" strings in apps | `AboutSystem.cpp`, `Deskbar` views, `Tracker` about, `Installer`, `Tour`, `login` | TODO: sweep `grep -rn '"Haiku' src/apps` |
| Default wallpaper | Backgrounds prefs default; set via first-login script or `src/prefs/backgrounds` default | TODO: point at shipped Manuscript wallpaper |
| Package/repository names | `haiku.hpkg`, repo info templates in `build/jam/` | TODO (bigger; affects package management) |

## Applying

```sh
cd /Volumes/HaikuSrc/haiku
git apply --binary /Volumes/xb/HaikuArmQemu/patches/prose-branding-01-artwork.patch
/Volumes/xb/HaikuArmQemu/scripts/build-image.sh   # artwork + AboutSystem + uname land in the image
```

Already applied in the working tree (uncommitted, alongside other fork WIP).
Regenerate assets: `python3 brand/make_assets.py && tools/render.sh`.
