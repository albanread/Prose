# The Prose Window

The Prose window is three things stacked: a toolbar of machine controls
across the top, the guest's display filling the window, and a thin status
bar along the bottom. Everything the machine does to the window — pausing,
shutting down, failing to start — happens inside the display area, as an
overlay you can act on.

## The display

The display is the guest's screen, live. Drag the window's corner and the
guest changes screen mode to follow it — the machine behaves like a monitor
that happens to be a window. How sharp that is on your Mac is a pair of
settings under **View**:

- **View ▸ Scale** is how big one guest pixel is on your Mac: **1×** is one
  guest pixel per screen pixel — sharpest, and on a Retina display half the
  size things appear elsewhere; **2×** and **4×** magnify.
- **View ▸ Presenter** (only meaningful above 1×) chooses what happens to a
  guest pixel that covers more than one of the Mac's: **Crisp** leaves it a
  hard block and invents nothing; **Smooth** interpolates, so edges soften
  instead of stepping.
- **View ▸ Screen Mode** sets the guest's resolution outright, from
  1024 × 768 up to 3840 × 2160; the window resizes to suit.

**View ▸ Actual Size** (⌃⌘0) returns the display to one guest pixel per
point. **View ▸ Enter Full Screen** (⌃⌘F) gives the machine the whole
screen.

## The toolbar

The toolbar holds the machine's controls as buttons:

| Button | What it does |
|---|---|
| **Start / Shut Down** | The power button. It says Start when the machine is off, Shut Down when it is running — and becomes a Stop button once a shutdown is under way, for the one that will not wait. |
| **Pause** | Pause the virtual machine. Pausing is instant and resumable; nothing runs while it is paused. |
| **Restart** | Shut Prose down, then start it again. |
| **Send Keys** | A menu of keys a Mac keyboard doesn't have. |
| **Screenshot** | Save a picture of Prose's screen; the flash and the saved-file note in the status bar confirm it. |
| **Full Screen** | Show Prose on the whole screen. |

Two more buttons are available but not on the default bar: **Force Stop**
(turn the machine off without shutting Prose down — the virtual equivalent
of holding the power button) and **Status Bar** (show or hide the bar).
**View ▸ Customize Toolbar…** arranges all of them; the Mac's usual
drag-and-drop applies, and Prose remembers what you kept.

## The status bar

A thin strip under the display, in the manner of a machine's front panel.

On the left: the **state light and label** — Starting…, Running, Paused,
Shutting down…, Restarting…, Shut down, or Failed to start — beside the
**uptime** clock, then the **display mode** (the guest's resolution, with
the scale beside it whenever it is not 1×), and room for a passing remark
("Screenshot saved…").

On the right, the machine's activity:

| Indicator | Meaning |
|---|---|
| **Processor** | The machine's share of the Mac's processors, as a percentage of its virtual CPUs. Hover for memory use. |
| **Disk** | Two lights: green reading, orange writing. Hover for the image's name and totals. |
| **Network** | The machine's address ("no address" until the lease arrives) and two lights: green receiving, orange sending. Hover for the MAC address and totals. |
| **Sound** | Lit while Prose is playing sound. |
| **Microphone** | Appears only while Prose is recording from the Mac's sound input. |
| **MIDI** | Flashes on MIDI messages from Prose, played by the Mac's synthesizer. |

Every indicator has a tooltip with the detail behind its number. **View ▸
Show Status Bar** (⌃⌘/) hides the bar or brings it back; with a window, the
window grows or shrinks by the bar so the guest's display keeps its size.

## The states in between

When the machine is not simply running, the display is dimmed under an
overlay with the one button that matters:

| State | The overlay says | The button |
|---|---|---|
| Paused | Paused | Resume |
| Shut down | Prose is shut down | Start |
| Failed | The virtual machine stopped, with the reason | Start |

Closing the window is the ordinary way to finish: Prose shuts the machine
down and quits. The Dock icon carries the machine's controls too — Pause,
Restart, Shut Down, Start, Take Screenshot — for when the window is not
handy.

[[newpage]]
