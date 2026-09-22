# Prose User Guide

Prose is an operating system for people who write: the Haiku system, built
for arm64 and running on your Mac under macOS's Virtualization.framework,
with its display drawn by the Mac's own GPU. To the Mac it is one application,
**Prose.app**; inside its window is a whole machine, with its own desktop,
applications, files, and a folder it shares with the Mac.

![Prose running on macOS: the guest desktop in a Prose.app window](prose-desktop.jpg)

This guide is about that application: putting it on a Mac, and driving it
once it is there. It covers the menus, the toolbar and the status bar, the
keyboard, and the settings. It does not cover the software *inside* the
machine — that is the guest's own territory.

## What is in this guide

| Page | What it covers |
|---|---|
| [Installing Prose](install.md) | What you need, the installer, updating, and taking it off again. |
| [The Prose Window](the-window.md) | The display, the toolbar, the status bar, and the states in between. |
| [The Menus](menus.md) | Every menu item, menu by menu, with its shortcut. |
| [The Keyboard](keyboard.md) | Who owns which key, and the shortcuts that are the Mac's. |
| [Settings](settings.md) | Processors, memory, networking, sound, and the shared folder. |

## The shape of the thing

An installation of Prose is more than one file, and the parts have different
owners:

- **The application** — Prose.app, ours, always safe to replace.
- **The machine** — one disk image that is the guest's whole computer: its
  system, its drivers, and everything you have ever put in it. Yours.
- **Your settings** — how big, how loud, what is shared.
- **Your shared folder** — a folder of yours the machine can see. None of our
  business.

The installer knows the difference. That is the whole reason it exists:
drag-and-drop can only ever replace the application, and the one thing it
silently cannot do is update the machine — which is how a newer Prose comes
to boot an older machine and sit there reporting that its portal will not
answer.

## A note on names

Prose is not Haiku, and Haiku is not Prose. Prose is an unofficial,
experimental port that owes everything to the Haiku project; everything that
makes the system inside the window good is their work. Haiku itself — the
real thing, and very much worth your time — is at
[haiku-os.org](https://www.haiku-os.org).

[[newpage]]
