# Automation and testing

Prose can be driven from outside: started and stopped, resized, typed into,
photographed, and — with the guest's cooperation — asked to run a command and
hand back its output. It is how the test scripts in `private_workspace/` will
work once they stop being shell wrappers, and it is what lets a tool help you
debug a running system instead of describing what you should try next.

It is **off until the owner of the machine turns it on**, and it announces
itself while it is on.

## What the owner agrees to

One setting per virtual machine, in the Machine menu and in the VM's settings:

> **Allow automation and testing** — lets other applications on this Mac start
> and stop this machine, send it keyboard and pointer input, capture its screen,
> and run commands inside it.

Three things follow from it, and the order matters:

1. **The setting gates callers, not the machine's own menus.** The Prose
   Portal virtio device is always attached: View ▸ Theme goes through it, and
   the owner choosing from their own machine's menu is not "another
   application". With the setting off, every surface below — AppleScript,
   `--script`, `prose(1)` — is refused before it reaches the device.
   (`--no-portal` leaves the device out altogether, for tests of a machine
   that has none.)
2. **macOS gates the callers.** Every surface below is reached by Apple Events,
   so the first time any application tries, macOS asks the owner — *"Terminal
   wants to control Prose"* — and records the answer per calling application in
   Privacy & Security ▸ Automation. Prose never invents its own permission
   scheme, and revoking there is final.
3. **It is visible.** The status bar shows an indicator while automation is
   enabled, and flashes it on each command, the way the drive and network lights
   already work.

Nothing here reaches the Mac. A command runs *inside the guest*, which can touch
the Mac only through a folder explicitly shared with it. That is the point of
doing it this way round.

## Three layers

```
  AppleScript / Shortcuts        prose(1)              tests
            \                       |                    /
             \                      |                   /
              +---------- Apple Events ----------------+
                              |
                    the automation core          (Prose.app)
                    /                  \
        the window and VM        the Prose Portal device
        (host side: input,          (guest side: run, files,
         display, power)             screen text, state)
```

**The core** holds the operations. Everything else is a thin skin over it, so a
capability is added once and appears everywhere.

**The surfaces** differ only in taste. AppleScript reads like English and is
what Shortcuts and Script Editor speak. `prose(1)` is a small command-line
client that sends the same Apple Events and prints results — plain text for a
person, `--json` for a program. Because it goes through Apple Events, it
inherits the same consent gate rather than opening a second door.

**The guest channel** is a custom virtio device, the same shape as the Prose
MIDI port: a small kernel driver publishing `/dev/misc/prose/portal/0` as a
datagram device — one frame per buffer, never half of one — and a daemon
answering each request on its own thread. Without the guest side
running, the host-side operations still work — a screen capture and a keystroke
need nothing from the guest — and the rest report that the guest is not
answering.

### The guest already speaks this language

Haiku inherits BeOS's scripting system, and it is message-based to the bone.
Every `BApplication`, `BWindow` and `BView` answers `B_GET_PROPERTY`,
`B_SET_PROPERTY`, `B_COUNT_PROPERTIES`, `B_EXECUTE_PROPERTY` and
`B_GET_SUPPORTED_SUITES`, addressed by specifiers — `B_DIRECT_SPECIFIER`,
`B_NAME_SPECIFIER`, `B_INDEX_SPECIFIER`, `B_RANGE_SPECIFIER` and the rest.
`hey`, which the image already ships, is nothing but a command-line front end to
that.

Two consequences, and they are the good news of this whole design:

1. **Applications are already scriptable.** Not something to add — something to
   reach. Every well-behaved Be application answers today, without being
   modified, because the Be API gave it the machinery for free. Reading a
   window's title, a text view's contents or a button's label is a message away.
2. **The channel needs no protocol of its own.** `BMessage::Flatten()` turns a
   message into a byte stream and `Unflatten()` turns it back. The device
   carries flattened `BMessage`s; the guest daemon unflattens, delivers to the
   addressed looper, and flattens the reply. It is `hey` reading the device
   instead of `argv`.

The two scripting models line up term for term, which is not coincidence — they
were drawn from the same idea at about the same time:

| AppleScript / Apple Events | Haiku / BeOS |
|---|---|
| `get`, `set` | `B_GET_PROPERTY`, `B_SET_PROPERTY` |
| `count` | `B_COUNT_PROPERTIES` |
| verbs | `B_EXECUTE_PROPERTY` |
| specifier by name, index, range | `B_NAME_SPECIFIER`, `B_INDEX_SPECIFIER`, `B_RANGE_SPECIFIER` |
| the app's dictionary (`.sdef`) | the app's suites (`B_GET_SUPPORTED_SUITES`) |

So `tell app "NetSurf" to get URL of window 1` is not a translation layer with
a lookup table in the middle. It is one object specifier being rewritten as
another, and a flattened message going down a pipe.

It also means the interesting operations are not the crude ones. `run` a shell
command is the blunt instrument; asking a running application what its window
says, and telling it to do something, is what the guest was built for.

## The operations

Host side, no guest cooperation needed:

| | |
|---|---|
| `start`, `shut down`, `restart`, `force stop`, `pause`, `resume` | as the Machine menu |
| `state` | off, starting, running, paused, stopping |
| `type` *text*, `press` *key* [`with` modifiers] | through the virtio keyboard |
| `click at` {x, y}, `move to` {x, y}, `drag` | through the virtio tablet, in guest pixels |
| `capture screen to` *file* | the display buffer, at the guest's real resolution |
| `display size`, `presenter mode`, `full screen` | read and write |
| `share` *folder* [`read only`] | add a HostFS share |
| `guest address` | the VM's address on the NAT bridge |
| `theme` [*name*] | the machine's themes and which is current; with a name, apply it (`prosetheme` in the guest, docs/decorators.md) |

Guest side, over the automation device:

| | |
|---|---|
| `run` *command* | returns exit status, stdout and stderr |
| `read` *path*, `write` *path* | files, without going through a share |
| `guest state` | uptime, memory, running teams |
| `applications` | what is running, with signatures |
| `get` / `set` *property* `of` *specifier* | any scriptable object in any running application |
| `count` *property* | how many windows, views, rows |
| `execute` *verb* | press a button, open a document, quit |
| `suites of` *specifier* | what that object can be asked — discovery, at runtime |

The last four are `B_GET_PROPERTY` and its siblings, flattened onto the wire.
`screen text` is then a `get` rather than an optical guess: ask the window what
it says.

## Written for a program to call

The surfaces are pleasant to type, but the shape underneath is chosen for
something that cannot look at the screen and improvise.

**Every operation returns a result, including the ones that look like verbs.**
`run` returns status, stdout and stderr rather than printing them. `type`
returns how many events were delivered. A caller never has to infer what
happened from a screenshot.

**Errors are typed, not prose.** `guest not answering`, `timed out`,
`not permitted`, `no such window`. A caller can branch on them; a person still
gets a readable sentence.

**Waiting is a primitive, not a sleep.** This is the difference between
automation that works and automation that works most of the time:

| | |
|---|---|
| `wait until booted` | the desktop is up and app_server is answering |
| `wait until idle` *for* n seconds | no commits for that long: drawing has settled |
| `wait for text` *string* | it has appeared on screen |
| `wait while running` *command* | a guest command finishes |

Each takes a timeout and returns whether the condition was met or the time ran
out. Nothing in the API encourages a caller to guess how long something takes —
we spent a day of this project's life discovering that a fresh-copy boot renders
the wallpaper one boot later than you expect, and a sleep would have hidden it
either way.

**Operations say what they changed.** Setting a display size returns the size
actually applied, which is not always the one asked for: macOS constrains a
window to the screen, and the guest's mode follows the window.

## Reading it as a person

```applescript
tell application "Prose"
    start
    wait until booted with timeout 180
    set display size to {1920, 1080}
    set result to run "pkgman search -a midi"
    if exit status of result is not 0 then error stderr of result
    capture screen to POSIX file "/tmp/prose.png"

    -- and into the guest's own applications, through their Be scripting suites
    tell guest application "NetSurf"
        get URL of window 1
        execute "Go" of window 1 with "https://www.haiku-os.org/"
    end tell
end tell
```

```sh
prose start --wait booted
prose run 'ls /boot/system/apps' 
prose capture /tmp/prose.png
prose run 'syslog -t 200' --json | jq -r .stdout
```

**Where the application has to live.** AppleScript reaches Prose only once
LaunchServices knows the bundle — installed in `/Applications`, or registered
with `lsregister`. Run straight from a build directory, every verb hangs until
the caller gives up: the dictionary loads, the suite is registered, and no
event is ever delivered. That cost an afternoon to establish, so: install it
first, then script it.

## What it deliberately cannot do

- **Reach the Mac.** There is no operation that reads or writes a file on the
  host, runs anything on the host, or adds a share the owner did not ask for.
  The only host-side writes are the files a capture is asked to produce.
- **Survive the setting being turned off.** No token, no remembered grant of its
  own. Off means the device is gone at the next start, and macOS's own grant can
  be revoked independently.
- **Hide.** There is no quiet mode.

## Order to build it

1. The **core** and the **host-side** operations. They need no guest work at
   all, and they already exist in scattered form: `--script`, `--input-test`,
   `--screenshot`, `--resize-after` are these operations with no way to call
   them.
2. The **`.sdef`** and Apple Event handling, which turns the core into something
   the OS can gate and Shortcuts can see.
3. **`prose(1)`**, a hundred lines over the same events.
4. **Done, patch 0058:** the **Prose Portal device** and its guest daemon — the patch that makes
   `run` real. Model it on `prose_midi` (patch 0023), which is the same problem:
   a custom virtio device, a small driver, a device node, a byte stream.
5. **Flattened `BMessage`s over the same device**, and the specifier rewriting
   between the two models. The daemon is `hey` with the device for a front end;
   read `src/bin/hey.cpp` first, because the hard part — turning a path like
   `window 1 of NetSurf` into a specifier stack — is solved there already.
6. Move `private_workspace`'s probes onto it, and delete the shell that
   `PW_INJECT_SCRIPT` needs.
