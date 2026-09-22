# Settings

**Prose ▸ Settings… (⌘,)** is what the machine is made of, and the window for
changing it. None of it can change while the machine is running — the
virtualization layer fixes a machine's make-up at the moment it is created —
so what you change here takes effect the **next time the machine starts**.
The window offers **Restart Now** for exactly that.

| Setting | What it chooses |
|---|---|
| Processors | How many virtual CPUs the machine gets. The window stops at the Mac's own core count — handing the guest more than the host has would leave nowhere for macOS to run. |
| Memory | Gigabytes of memory. The window keeps 8 GB back for the Mac for the same reason. |
| Networking | The machine's network, on or off. On, it is NAT: the machine has its own address and reaches the world through the Mac. |
| Sound | The machine's audio, out to (and, when an app asks, in from) the Mac. |
| Share a folder with the guest | The Mac folder the machine mounts as a volume, with a **Read only** option. |

## The shared folder (HostFS)

The shared folder is the two-way bridge: a folder of yours that appears
inside Prose as a disk volume, so files move between the Mac and the machine
by ordinary dragging. It is `~/Documents/HostFS` unless you choose another,
writable unless you say otherwise, and — worth saying twice — **never touched
by the installer, an update, or an uninstall** unless you tick the box that
says to. **View ▸ Host Files in Finder** reveals it from the Prose window.

Inside the machine the volume is called HostFS and behaves like a local
disk, including Haiku attributes, which are kept as extended attributes on
the Mac side.

Changes travel both ways without reopening anything: files you put into
the folder appear in the machine's windows within a couple of seconds, and
ones you take away disappear. Dragging files **onto the Prose window**
copies them into the folder too — the copy lands beside anything of the
same name under a "copy" name, never over it.

## Automation

**Machine ▸ Allow Automation and Testing** is the one setting that lives in a
menu rather than the Settings window, because it is not about the machine —
it is about who else on the Mac may drive it. Off by default. When on, other
applications on this Mac can start and stop the machine, send it keyboard and
pointer input, capture its screen, and run commands inside it; macOS itself
asks before each application may do so, through Privacy & Security ▸
Automation. Nothing it offers reaches the Mac beyond the Prose window and the
shared folder you chose to give it.

[[newpage]]
