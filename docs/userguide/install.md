# Installing Prose

## What you need

- An **Apple Silicon Mac** running **macOS 27 or later**. Prose's display,
  keyboard and MIDI devices need virtualization features that do not exist
  before macOS 27.
- About **2 GB of free space**: the application and the machine together.
- Nothing else. The machine's disk image comes inside the installer; there is
  no separate download and no account.

## The installer

Prose arrives as a signed, notarized disk image — a `.dmg` holding exactly
one thing, **Installer**. Double-click the disk image, then double-click
Installer. There is nothing to dismiss and nothing to drag.

Prose.app travels *inside* the installer, so the disk image has no second
item and nobody has to guess whether to drag the application somewhere or
open something else. The installer puts each part where it belongs:

```text
/Applications/Prose.app                                         the application
~/Library/Application Support/Prose/Machines/Prose.image         the machine
~/Documents/HostFS                            made by Prose, never touched
```

### The choices

The installer shows each part as its own line, with a checkbox:

| Choice | Where it goes | Default |
|---|---|---|
| **Application** | Replaces or installs Prose in Applications. | On |
| **File system** | The machine the guest runs, with its drivers and the portal Prose talks to it through, copied into Application Support. | On for a first install |
| **Reset settings** | Presenter mode, whether automation is allowed, window and toolbar. | Off |

One rule runs through the defaults: **a default never destroys something you
made.** If a machine already exists, replacing it is off by default and
marked in orange, because replacing it loses everything in it — files,
settings, anything you installed. Replacing it is sometimes the right thing
(it says so below), but it is your decision, not the installer's.

### First run

When the installer finishes it offers **Open Prose**. The first run takes a
moment: Prose sets up its folders and starts the machine. You will see the
boot splash, then the desktop.

## Updating

Run the installer from the new disk image. It reads what is on the Mac and
offers the same choices, with the same rule:

- **Application** is ticked by default — replacing it is always safe.
- **File system** is unticked by default when you have a machine. If the
  installer knows your machine was made by an earlier version of Prose, it
  says so plainly: the old machine keeps that version's drivers and portal,
  so automation and other new plumbing may not work until it is replaced.
  The symptom is Prose reporting that its own portal will not answer.
  Replacing it still loses everything in it — move files you care about out
  first (the shared folder, described in [Settings](settings.md), is the easy
  way), then tick the box.

Your shared folder — `~/Documents/HostFS` by default — is never touched by
an install or an update.

## Taking it off again

The installer's **Uninstall…** button turns it around, part by part:

| Choice | What goes |
|---|---|
| **Application** | Prose from Applications, and its logs. |
| **File system** | The machine, and everything in it. Shown with its size; asks once more before proceeding. |
| **Settings** | The preferences file. |
| **Shared folder** | `~/Documents/HostFS` — your own files. Off by default, and it says how many items would go. |

Uninstalling cannot be undone; removing the file system frees its space and
that is all that can honestly be said about it.

## Building it yourself

Everything is built from source in the project repository, if you would
rather make your own:

```bash
scripts/build-image.sh                     # applies the patches, builds the image
tools/build.sh                             # builds Prose.app
private_workspace/run-vz.sh desktop --keep # boots it
```

That path needs Xcode 27 and a case-sensitive volume for the Haiku source
tree; the repository's `BUILDING.md` covers it. Everyone else should use the
installer.

[[newpage]]
