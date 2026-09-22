# The Menus

One convention covers them all: **the guest owns the keyboard.** While the
Prose window is active, ⌘ is Prose's Command key and ⌥ its Option key, so the
shortcuts you know inside the machine are the ones it answers to. The
window's own shortcuts add Control: they are ⌃⌘ chords, listed with each
item below (and collected on [the next page](keyboard.md)).

## The Prose menu

| Item | Shortcut | What it does |
|---|---|---|
| About Prose | | Version, and what the machine is made of: virtual CPUs, memory, display, and where its disk image lives. |
| Settings… | ⌘, | Opens [Settings](settings.md). |
| Services | | The Mac's Services menu. |
| Hide Prose | ⌃⌘H | Hide the window; the machine keeps running. |
| Hide Others / Show All | | The Mac's usual window management. |
| Quit Prose | | Quits, shutting the machine down first. There is deliberately no shortcut — ⌃⌘Q would lock the Mac's screen — and closing the window does the same job. |

## The Machine menu

| Item | Shortcut | What it does |
|---|---|---|
| Start | | Start the machine when it is off. |
| Shut Down | | Shut Prose down, as if you pressed its power button. Hold ⌥ for **Force Stop**: off without shutting down, for a machine that will not listen. |
| Restart | ⌃⌘R | Shut Prose down, then start it again. Hold ⌥ for **Force Restart**. |
| Pause / Resume | ⌃⌘P | Pause the machine, or pick it up where it left off. The title says which. |
| Send Keys ▸ | | Keys a Mac keyboard doesn't have: **Control-Alt-Delete** (the guest's Team Monitor) and **Print Screen**. |
| Take Screenshot | ⌃⌘S | Save a picture of Prose's screen. |
| Open Guest Log | ⌃⌘L | The machine's own boot and debug log, kept from its memory as it ran. |
| Allow Automation and Testing | | Off by default. When on, other applications on this Mac may start and stop this machine, send it keyboard and pointer input, capture its screen, and run commands inside it. macOS asks before each application may do so. |

The Dock menu carries the machine's core controls — Pause, Restart, Shut
Down, Start, Take Screenshot — without opening the window.

## The View menu

| Item | Shortcut | What it does |
|---|---|---|
| Show Toolbar | ⌃⌘T | Hide or show the toolbar (see [The Prose Window](the-window.md)). |
| Customize Toolbar… | | Arrange the toolbar's buttons; Prose remembers what you kept. |
| Show Status Bar | ⌃⌘/ | Hide or show the status bar. The window grows or shrinks by the bar, so the display keeps its size. |
| Actual Size | ⌃⌘0 | One guest pixel per point. |
| Screen Mode ▸ | | The guest's resolution, from 1024 × 768 to 3840 × 2160. The window resizes to suit. |
| Scale ▸ | | How big one guest pixel is here: 1×, 2× or 4×. |
| Presenter ▸ | | Above 1×: Crisp (hard pixels, nothing invented) or Smooth (interpolated edges). |
| Theme ▸ | | The machine's themes — a decorator, the system's colours and the wallpaper, kept by the machine itself. Available while it is running. |
| Host Files in Finder | | Reveal the folder the machine mounts as its HostFS volume. |
| Enter Full Screen | ⌃⌘F | The machine takes the whole screen. |

## The Window menu

| Item | Shortcut | What it does |
|---|---|---|
| Minimize | ⌃⌘M | The Mac's Minimize; the machine keeps running. |
| Zoom | | The Mac's Zoom. |
| Bring All to Front | | The Mac's usual. |

## The Help menu

| Item | Shortcut | What it does |
|---|---|---|
| Keyboard Shortcuts | ⌃⌘? | The one-panel summary of who owns which key. |

[[newpage]]
