# Mojo on Prose — design

> **Superseded, 2026-09-23 (evening).** The direction changed after review:
> the compiler is **upstream Mojo, built from source with Bazel on the Mac and
> cross-compiled so that it runs on Prose** as a Haiku development tool — not
> MojoCocoa's compiler, which is modified extensively for Cocoa and Darwin.
> A compiler running on Haiku needs Modular's real runtime built for Haiku, so
> the 49-function stand-in runtime below falls away too. No MAX, and no
> telemetry or crash reporting (both serve Modular).
>
> The port lives in its own fork, `/Volumes/xb/mojo2026/MojoProse`, branch
> `prose`: its `PORT-JOURNAL.md` is the record and plan (gates G0-G8), and
> `Haiku/docs/bridge-design.md` is the design of the bridge to the Be API.
>
> What below still stands: the measurements of 2026-09-23 (LLVM's AArch64
> backend emits correct Haiku ELF, and Haiku's toolchain links and runs it;
> `tools/mojo-spike` reproduces that), the standard library's Haiku surface
> (section 3), and the reasoning about C++ that led to the bridge (section 4),
> which the bridge design takes much further.

*2026-09-23. A design for review, grounded in what was measured today; nothing
past milestone M0 is built.*

The aim: write native Prose applications in Mojo. Compile them on the Mac,
run them on Prose, and let them call the operating system — libroot and the
kits of the Be API — as naturally as a C++ program does.

## The short answer

- **The compiler we already have targets Haiku.** The MojoCocoa compiler,
  unchanged and unrebuilt, emits arm64 ELF for `aarch64-unknown-haiku`. Haiku's
  cross toolchain links it, and the result runs on Prose. No Bazel.
- **The runtime is 49 C functions.** Everything compiled Mojo code can call in
  its runtime is named `KGEN_CompilerRT_*`. A Haiku-native library that
  implements those 49 replaces Modular's whole runtime stack.
- **The standard library needs Haiku taught to about a dozen files.** It
  refuses an unknown OS at compile time, loudly, at every place that matters.
  It is Mojo source, so this is a patch, not a build.
- **The Be API is C++, and Mojo speaks C.** The way across is a *generated*
  bridge: C entry points for every method, and C++ shadow classes that forward
  virtual hooks (Draw, MouseDown, MessageReceived…) to Mojo. That needs no
  compiler change either. Teaching the compiler C++ itself, MojoCocoa-style,
  is the later option, not the first.

Bazel comes back only if the compiler has to change. Nothing below requires it.

## Measured on 2026-09-23

| fact | evidence |
|---|---|
| The MojoCocoa compiler (dist 2026.09.06) compiles for `aarch64-unknown-haiku` | `--target-triple aarch64-unknown-haiku --target-cpu apple-m1 --emit object` → `ELF 64-bit LSB relocatable, ARM aarch64` |
| It has only LLVM's AArch64 backend, which is all Haiku arm64 needs | `--print-supported-targets` |
| Haiku's cross gcc links the object into a Prose executable needing only `libroot.so` | `readelf -d`: `NEEDED libroot.so` |
| **It runs on Prose** | `hello from Mojo, total 45`, exit 0, on the 207 image |
| A hello-world object needs 8 runtime entry points and 6 libc functions (`dup fclose fdopen fflush memcpy write`) | `nm -u`; identical for the Linux and Haiku triples |
| The runtime's whole C contract is 49 `KGEN_CompilerRT_*` functions; the stdlib names about 30 | `nm -gU libKGENCompilerRTShared.dylib`; the other two runtime dylibs export only C++ internals |
| The real runtime depends on LLVM Support, Modular's Support, MLRT and AsyncRT (`libMSupportGlobals` alone exports 1,731 symbols) | `KGEN/lib/CompilerRT`, 999 lines, and its Bazel deps |
| The stdlib refuses Haiku at compile time: `Current compilation target does not support operation: get_errno` | the probe of files, time and environment |
| OS-specific surface: 22 refusal sites in 9 files, 33 Linux/macOS branches; about 12 files in all | `grep` over `std/` |
| Recognising Haiku needs no compiler change: `is_linux()` is `_os() == "linux"`, and `_os()` is `"haiku"` for our triple | `std/sys/info.mojo`; the emitted LLVM IR |
| Haiku's clocks and errno differ from Linux's: `CLOCK_REALTIME` -1, `CLOCK_MONOTONIC` 0, `errno` is `*_errnop()` | the Prose sysroot's `time.h`, `errno.h` |
| Mojo functions can be C function pointers: `abi("C")`, with real AAPCS64 argument classification in the compiler (HFA in v0-v3, indirect over 16 bytes) — proved against clang for Darwin; for Haiku at M3 | `std/python/bindings.mojo`; `COCOA_CLASS_DESIGN.md` |
| MojoCocoa's compile-time hook `cocoakb_query` asks Objective-C questions (selectors, init forms); only its struct and enum queries would carry over | `std/sys/_cocoakb.mojo` |
| `prose_api.sqlite` indexes 16,086 Be API symbols for documentation — no layouts, vtables or mangling | its `symbols` table |

`tools/mojo-spike/build.sh` reproduces the Prose binary byte for byte.

## Architecture

```
 app.mojo
   │  prosemojo --build app.mojo -o app                       (on the Mac)
   ▼
 [1] compiler   cocoamojo-compiler, unchanged
                --target-triple aarch64-unknown-haiku --target-cpu apple-m1
                --emit object  -I <Haiku stdlib>  -I <haiku package>
   ▼ app.o  (ELF, AAPCS64)
 link           aarch64-unknown-haiku-g++ --sysroot <Prose sysroot>
                app.o  -lmojort  -lmojobe  -lbe  -lroot …
   ▼ app    (a Prose executable)
 on Prose       [2] libmojort.so   the Mojo runtime, Haiku-native
                [4] libmojobe.so   the Be API bridge
                [3] the Haiku stdlib is compiled into each app
```

Everything builds on the Mac. The guest only runs things.

## 1. The compiler — unchanged

The installed CocoaMojo distribution is the compiler. It already produces
correct Haiku objects, so we pin it rather than build one: a fresh compiler
means Bazel, and the Bazel cache on `/Volumes/xc`, and forty-five minutes
whenever the cache misses.

- `prosemojo`, a shell driver like `cocoamojo`, runs the compiler to an
  object and links with the Haiku toolchain. `--emit exe` is not used: with a
  Haiku triple it would call the Mac's linker.
- `--target-cpu apple-m1`: Prose supports M1 to Mn, and the compiler's host
  default is the M4.
- Nothing Cocoa-flavoured is used: no `std.objc`, no `--target-accelerator`.
- `mojo run` cannot work (it would run arm64 Haiku code on macOS). Build,
  then run on the guest.

## 2. The runtime — `libmojort`, Haiku-native

Implement the 49 entry points in C++ on Haiku's own primitives, reading
Modular's sources (`KGEN/lib/CompilerRT`, `AsyncRT`) for their exact
semantics, and test against the stdlib's own tests.

| group | entry points | on Haiku |
|---|---|---|
| globals | `GetOrCreateGlobal`, `…Indexed`, `InsertGlobal`, `GetGlobalOrNull`, `DestroyGlobals` | a locked table, destroyed in reverse order of creation |
| memory | `AlignedAlloc`, `AlignedFree`, `SetAsanAllocators` | `posix_memalign`, `free`; the last is a no-op |
| process | `Initialize`, `SetArgV`, `GetArgV`, `Num{Logical,Physical,Performance}Cores`, `GetStackTrace`, `PrintStackTraceOnFault`, `fprintf` | `get_system_info`, `get_cpu_topology_info`; a fault handler that prints a trace |
| parallelism (AsyncRT) | `GetOrCreateCPUDevice`, `GetCurrentCPUDevice`, `ReleaseCPUDevice`, `ParallelismLevel`, `Execute`, chains (`InitializeChain`, `AndThen`, `Complete`, `Wait`, `Wait_Timeout`, `DestroyChain`), spin waiters | a pool of Haiku threads, one per CPU, with a work queue; chains and waiters as the reference defines them |
| profiling | `TimeTraceProfiler*`, `Range*`, `Tracy*`, `GetNextOpId` | disabled, answering "not recording" |
| Python | `Python_SetPythonPath` | a no-op: no Python interop on Prose |

**Why not port Modular's runtime instead:** it is four libraries and LLVM's
Support library, built only by Bazel; porting it means giving Bazel a Haiku
toolchain, which took the Windows port several gates. The contract the
compiler relies on is 49 C functions. That is smaller to write, and it is ours
to test.

Details the reference settles: `llvm::StringRef` arrives by value as `(const
char*, size_t)` in two registers; globals are created once, thread-safely;
chain objects live in memory Mojo allocates, so their size comes from the
stdlib, not from us.

## 3. The standard library — Haiku as a target

A patched copy of the stdlib that matches the pinned compiler:

- `CompilationTarget.is_haiku()` — `_os() == "haiku"`.
- A Haiku branch at each refusal site and each Linux/macOS branch, about 12
  files: `sys/_libc_errno` (`_errnop`, and Haiku's own errno values),
  `sys/_libc`, `io/file_descriptor`, `io/io`, `os/os`, `os/fstat` (Haiku's
  `struct stat`), `os/process`, `os/path`, `time/time` (Haiku's clock ids:
  `CLOCK_REALTIME` is -1, `CLOCK_MONOTONIC` 0), `pwd`, `ffi` (`.so`), `sys/info`.
- **Constants and struct layouts are generated from the Prose sysroot's
  headers**, never typed from memory: a program built with the cross
  toolchain prints them, and the stdlib asserts them at compile time.
- The stdlib's own tests, built for Haiku and run in the guest, are the
  measure of done.

GPU, MAX and Python modules are out of scope.

## 4. The Be API — C++ from Mojo

### Why this is not Cocoa

Cocoa is Objective-C, and everything in Objective-C is a C call:
`objc_msgSend(object, selector, …)`. Classes and methods are data in a
runtime, which you can query and even create classes in. That is why
MojoCocoa could treat Cocoa as a database plus one way to call.

C++ has no runtime:

- a method is a call to a mangled symbol (`_ZN7BWindow4ShowEv`) with `this`
  in `x0`;
- a virtual method is a slot in a vtable, and the slot numbers exist only in
  the compiler that read the headers;
- objects are made by constructors and ended by destructors, which run code;
- many methods are inline and have no symbol at all (`BRect::Width()`);
  templates exist only once instantiated;
- and the Be API is driven by **subclassing**: an application overrides
  `BView::Draw`, `MouseDown`, `MessageReceived`, `BWindow::QuitRequested`, and
  the kits call into the application through the vtable.

Mojo's foreign-function interface speaks C: it calls C symbols, and it can
hand out C function pointers (`abi("C")`). So something has to make C++ look
like C, in both directions.

### Three ways across

| | how | compiler change | handles inline, templates, multiple inheritance | verdict |
|---|---|---|---|---|
| A. hand-written C shim | write `extern "C"` wrappers by hand | no | yes | open-ended, and drifts from the headers |
| B. **generated bridge** | generate the C wrappers and forwarding subclasses from the headers | **no** | yes — a C++ compiler builds it | **first** |
| C. compiler knows C++ | a Haiku database of layouts, vtable slots and mangled names; the compiler calls methods directly and synthesizes vtables for Mojo subclasses | yes (Bazel) | inline and templates still need compiled C++ | later, where B's cost shows |

C is the MojoCocoa ideal, and Swift's C++ interoperability shows it can be
done — but Swift embeds a whole C++ compiler for the inline functions and
templates. B gets working applications without touching the compiler, and
nothing in it has to be thrown away if C comes later: C would replace the
bridge underneath, not the Mojo API above it.

### The bridge

A generator reads the Haiku headers with clang (target `aarch64-unknown-haiku`,
the Prose sysroot) for an allowlist of classes, growing as applications need
them, and writes:

- **`libmojobe.so`**, C++ built by the Haiku cross g++:
  - an `extern "C"` function per constructor, method and inline function:
    `mojobe_BWindow_Show(BWindow*)`, `mojobe_BView_FillRect(BView*, BRect,
    pattern)`;
  - a **shadow subclass** per class an application subclasses — `MojoView :
    BView` — which overrides every virtual hook and calls a Mojo function
    pointer from a per-object table, or the base class when the table's slot
    is empty. The C++ compiler handles thunks, multiple inheritance and RTTI.
- **the `haiku` Mojo package**:
  - value types laid out exactly like the headers' (`BRect`, `BPoint`,
    `rgb_color`, `pattern`), each with a compile-time assertion of its size and
    offsets against what clang computed;
  - handle types with the methods, under **the Be Book's own names**
    (`FillRect`, `SetHighColor`), so the Be Book and `prose_api.sqlite`
    document the Mojo API as they stand;
  - enums and constants (`B_TITLED_WINDOW`, `B_OP_COPY`) as compile-time
    values.

What an application would look like:

```mojo
from haiku import Application, BPoint, BRect, View, ViewHooks, Window
from haiku import B_QUIT_ON_WINDOW_CLOSE, B_TITLED_WINDOW, rgb


struct Canvas(ViewHooks):
    var clicks: Int

    def Draw(mut self, view: View.Ref, update: BRect):
        view.SetHighColor(rgb(40, 40, 60))
        view.FillRect(view.Bounds())
        view.SetHighColor(rgb(255, 220, 0))
        view.DrawString("clicks: " + String(self.clicks), BPoint(20, 40))

    def MouseDown(mut self, view: View.Ref, where: BPoint):
        self.clicks += 1
        view.Invalidate()


def main() raises:
    var app = Application("application/x-vnd.Prose-mojo-demo")
    var window = Window(BRect(100, 100, 500, 400), "Mojo on Prose",
        B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
    window.AddChild(View(window.Bounds(), "canvas", Canvas(0)))
    window.Show()
    app.Run()
```

Underneath, `View(…, Canvas(0))` moves the `Canvas` to the heap, creates a
`MojoView` with a table of `abi("C")` trampolines generated for `Canvas`, and
the C++ destructor calls a trampoline that destroys the `Canvas`. A hook
`Canvas` does not define stays empty, and the base `BView` handles it.

**Threads.** The kits call hooks on the window's own thread, holding its lock,
exactly as for C++. The runtime is thread-safe, so Mojo code there is fine,
and the rule is the Haiku one: do not block a window thread. Drawing from any
other thread takes the lock, and the Mojo API makes that a
`with window.Locked():` block.

**Ownership.** Mojo's ownership says what the kits need said. `AddChild`
*consumes* the view: the window owns it now, and Mojo keeps a `View.Ref`. A
window deletes itself when it quits, so Mojo holds only references to one.
A `BMessage` or `BBitmap` that Mojo made is Mojo's to delete. Which methods
adopt their arguments is a short table the generator reads, written by hand
from the Be Book.

**Errors.** Methods returning `status_t` raise when it is not `B_OK`, and
`InitCheck()` is called where the Be Book says to.

**Calls that are not the Be API**, such as `OS.h` (threads, ports, areas),
`find_directory` and POSIX, are plain C. Mojo calls them directly.

**ABI, proved rather than assumed.** An oracle test, like MojoCocoa's
`abi_oracle_test.mojo`, links a library built by the Haiku cross g++ (the
compiler that built libbe) and checks that value types by value (`BRect` is
a four-float HFA, in v0-v3), 16-byte structs and indirect returns cross
correctly.

## Where Bazel comes back

Only for option C, or for a compiler bug that stops us. Then the rules are
MojoCocoa's: `tools/mojo-build.sh`, never `bazel` by hand, the `xc` drive
mounted, `local.bazelrc` untouched, and compiler changes batched.

## Where it lives

A sibling fork beside the other ports, `/Volumes/xb/mojo2026/MojoProse`,
carrying the stdlib patch, `libmojort`, the bridge generator and
`prosemojo`. The stdlib is Modular's Apache-2.0 source and belongs with the
other Mojo forks. The Prose repo keeps the Prose half: a `mojo_runtime`
package (`libmojort.so`, `libmojobe.so`) in the image, and this document.

## Milestones

| | what | done when |
|---|---|---|
| **M0** | the compiler to Prose, end to end | **done 2026-09-23**: hello world built on the Mac printed on Prose (`tools/mojo-spike`) |
| M1 | the stdlib knows Haiku; `prosemojo` | `probe.mojo` (files, time, environment) builds and prints the right answers on Prose; the stdlib tests for those modules pass there |
| M2 | `libmojort` complete | the stdlib's CPU test suite runs in the guest with its pass count recorded; `parallelize` spreads across the guest's CPUs |
| M3 | the bridge: generator, `libmojobe`, `haiku` package; the ABI oracle | the demo window above draws, counts clicks and quits cleanly; its pixels are checked through app_server as `tools/blittest` does |
| M4 | the showcase | Galaxigans Deluxe, the Mojo original, playing on Prose through `BGamePane` |
| M5 | shipping it | `mojo_runtime` in the image; the SDK on the Mac; `docs/writing-a-mojo-app.md` |
| later | option C; a compiler running on Prose itself | when there is a reason |

## Risks and unknowns

- **The stdlib patch is tied to one compiler.** A new CocoaMojo distribution
  means rebasing it. Keep each file's Haiku change small and in one place.
- **Code the spike did not reach:** thread-local storage (Haiku's
  runtime_loader and TLS descriptor relocations), stack protector symbols,
  atomics. The stdlib tests in M1–M2 will find them.
- **AsyncRT's semantics** must be what the compiler's coroutine lowering
  expects. Read from the reference, proved by the async tests.
- **Headers on the Mac:** the generator needs a clang that parses Haiku's
  headers for the Haiku target — Apple's clang, Homebrew's LLVM or Prose's own
  clang 23. To check at M3.
- **The compiler is Cocoa-flavoured:** keep `std.objc`, `cocoa.sqlite` and
  `apple-m4` defaults out of Haiku builds.

## For you to decide

1. **Where it lives** — a sibling fork `MojoProse` (recommended), or inside
   the Prose repo.
2. **Which compiler** — pin the installed CocoaMojo 2026.09.06 (recommended:
   known good, no Bazel), or build a fresh one.
3. **Names** — the Be Book's (`FillRect`, recommended: the Be Book documents
   it), or Mojo-style (`fill_rect`).
4. **The bridge first, compiler knowledge later** — or go straight to C.
