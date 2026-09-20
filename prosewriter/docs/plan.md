# ProseWriter — plan, sprints, tests

## Goal

A **fully-featured native word processor for Prose/Haiku**: proper fonts,
real page layout, fast on this QEMU/HVF machine, in the BeOS visual
tradition. Built on the host (cross GCC 13), tested in the guest (QEMU),
delivered later as a prose package. Scope order: text first, layout second,
interchange third, print last.

## Non-goals (for now)

- `.doc` binary import (Gobe's grave mistake was pretending this was easy).
- Collaborative editing, macros, mail-merge.
- Spreadsheet/graphics frames inside text (Gobe's `.pve` dream) — but the
  model keeps paragraphs as styled runs so images/objects can be added as
  anchored items in a later sprint without a rewrite.

## Architecture

```
PWDocument      the model: paragraphs → runs → characters, each run styled;
                loads/saves flattened BMessage (.prose), RTF, plain text
PWLayout        paragraphs → lines → pages, using BFont metrics; caches line
                breaks per paragraph; relayouts from the edited paragraph on
PWPageView      BView: draws the pages (grey desk, shadows, text runs,
                selection, caret), takes keyboard and mouse input
PWRuler         top ruler with margins and tabs
PWFindBar       inline find/replace bar
PWApp/PWWindow  application, menus, toolbar, dialogs, wiring, scripting
```

Editing goes through `PWDocument` only; it posts a change notice; layout
consumes it; the view invalidates. Undo is a command stack in the document.
Everything is synchronous single-threaded (app_server looper) except
relayout of huge documents, which if ever needed will be chunked with a
BMessage ticker — measure before adding threads.

Performance budget (guest, QEMU/HVF): full layout of a 100-page document
< 150 ms; keystroke-to-paint < 16 ms at 100 % on a 10-page document;
cold launch < 1 s.

## Sprints

### Sprint 1 — core: model, layout, rendering, basic editing
1. `PWDocument`: paragraphs/runs/char+para styles; insert/remove/replace by
   text offset; dirty tracking; undo/redo (typing, delete, style runs);
   save/load `.prose` (flattened BMessage).
2. `PWLayout`: line breaking (spaces + widths; no hyphenation), paragraph
   metrics, pagination A4/US-Letter with margins; caret positions
   (offset ↔ x,y on a page).
3. `PWPageView`: render pages + text runs; caret; drag selection; keyboard:
   typing, arrows, Home/End, PgUp/PgDn, Backspace/Delete, Enter, Alt-combos;
   mouse: click place, drag select, double/triple click word/paragraph.
4. `PWWindow`: menu bar (File/Edit/Text/Search/Window/Help), status bar,
   New/Open/Save/Save As…/Close/Quit, modified-quit alert, window title
   reflects document name + modified dot.
5. Guest smoke tests via harness (`--selftest` mode + screenshots).

**Exit criteria:** type a page of text, select, cut/copy/paste within app,
undo/redo, save/reopen identical, screenshot shows a correct page on the
desk with yellow-tab window.

### Sprint 2 — rich text, typography, find
1. Font family/style/size menus (live `count_font_families`), B/I/U/S
   toggles, colour (`BColorControl` in a panel), apply to selection.
2. Paragraph alignment, indents/margins via ruler drag, tabs, spacing.
3. Zoom 50–200 % + fit page, non-integer-safe rendering (scale metrics).
4. Find & replace bar: find next/prev, replace, replace all, case option.
5. Word/char/paragraph count; Go to page.
6. RTF import/export (family, size, B/I/U, colour, alignment); plain text
   in/out; `Open With` sanity; styled clipboard (text/plain + runs).

**Exit criteria:** a document with headings/body/quotes in different fonts
round-trips through `.prose` and RTF with styles intact; find-replace-all
correct on a 10-page doc; ruler dragging margins relayouts live.

### Sprint 3 — pages, polish, print, package prep
1. Page setup dialog (size, orientation, margins) + headers/footers
   (basic: text + page number fields).
2. `BPrintJob` printing of the page view's rendering.
3. Recent documents, Open with…, document icon, Icon in Deskbar, About.
4. Performance pass: profile layout on 100 pages; line-cache validation.
5. Packaging notes for `prosepkg` (recipe draft, resources, MIME, deps) —
   to hand to the repo owner when finished.

**Exit criteria:** prints a page from the guest; 100-page perf budget met;
the app is pleasant for an afternoon of writing.

*Sprint 3 status: complete. Printing verified to the ConfigJob boundary
(the guest has no printer, so the honest test is the no-printer alert);
packaging notes in `docs/packaging.md`.*

### Sprint 4 — typography, structure, and scriptability

The distance from "engine with menus" to a word processor people write in.

1. **Paragraph typography**: first-line / left / right indents, line
   spacing (1.0/1.15/1.5/2.0), space before/after — model, ruler handles,
   Text menu, RTF round trip (`\li \ri \fi \sl \sb \sa`).
2. **Real tab stops**: click-to-place tabs on the ruler, drag to move,
   drag off to delete; left/centre/right tab kinds; rendering with
   tab-advance in the layout engine.
3. **Lists**: bulleted and numbered paragraphs (model + toolbar/menu).
4. **Named styles**: a styles panel (paragraph + character styles) in the
   Gobe tradition — define once, apply everywhere, stored in the document.
5. **Scriptability**: `B_GET_PROPERTY`/`B_SET_PROPERTY`/`B_EXECUTE_PROPERTY`
   suites (`Text`, `Selection`, `Header`, `Footer`) so `hey` — and the
   repo's planned automation device — can drive ProseWriter. This also
   replaces pixel-guessing in the guest tests with real assertions.
6. **Measurements**: cold-launch time, keystroke-to-paint on a 50-page
   document, scroll throughput; fix what they expose (incremental
   relayout from the edited paragraph is the expected need).
7. **Polish**: menu mnemonics, toolbar state (B/I/U reflect the caret's
   format), document icon + MIME registration, window placement memory,
   Esc closes the find bar.

*Sprint 4 status: feature work complete and selftested (65/65). Items
delivered: indents, spacing, tab stops, lists, RTF geometry round trip,
spell check as-you-type with red squiggles (user-pulled forward from
Sprint 5; dictionary = macOS's 235k-word list, 2.4 MB, loaded once per
window), Esc-closes-find, B/I/U menu marks reflect the caret's format.*

**Open: scripting via `hey` (experimental).** Implemented: ResolveSpecifier
and app-level B_GET/SET_PROPERTY handling for Text, Header, Footer,
Selection, Modified, WordCount. Verified: messages are delivered to
PWApp::MessageReceived with the specifier stack intact ('PGET',
HasSpecifiers=1, seen via stderr traces), replies are constructed and the
reply mechanism itself is proven (error replies reach hey). Not working:
property extraction at answer time — GetCurrentSpecifier returns nothing
useful on the delivered message and the property field is absent, so the
default looper suite answers instead. Not yet tried: GetSupportedSuites
with a BPropertyInfo suite (the fully documented pattern; the working
receiver examples in the tree — StyledEdit — simply inherit BTextView's
native suites and define none of their own). Harness note: hey
auto-launches by signature, so test only against a known single instance.

*Sprint 5 status: complete. Scripting closed the Sprint 4 open item by
copying SerialApp's pattern verbatim (property_info table, BPropertyInfo,
GetSupportedSuites, ResolveSpecifier claiming our properties, FindMatch
dispatch); the last two session bugs were process, not code: silently
unapplied patches against drifted source (all replacements now assert),
and kills that never killed — `ps` puts the team id after the command
name, and quoting an awk pipeline through two shells expands to nothing;
kills are by explicit numeric id. Sets run on the window looper via a
'pWst' forward; the app acks immediately, so a get racing a set can read
the old value. Styles: PWStyle (name + char format + para format),
persisted in .prose, panel under Document ▸ Styles…. Measures are in the
selftest output ("measure:" lines).*

### Sprint 6 — content, and the typing optimization

*Sprint 6 status: incremental relayout and images landed (part 1);
tables deferred to a focused sprint of their own.*

1. **Incremental relayout — done.** Paragraphs are fingerprinted (length,
   head/tail bytes, paragraph format); unchanged ones keep their measured
   lines verbatim and only y/page assignment is recomputed. 96-page
   keystroke: 183 ms → 9 ms (the fingerprint even dedupes identical
   filler paragraphs). Selftested for identity with a cold layout.
2. **Images in text — done, v1.** A U+FFFC object-replacement character
   in the text marks the spot; the bitmap lives in a per-paragraph table
   keyed by byte offset, so caret, selection, deletion and undo all treat
   it as one character (deleting the marker deletes the image).
   Insertion via File ▸ Insert image… (BTranslationUtils, scaled to the
   column); persistence carries raw BGRA pixels in .prose; images raise
   their line's height and share the line with following text. v2 (wrap
   around anchored images) waits until there's a use for it.
3. **Tables — done in Sprint 7** (the "own sprint" it was moved to).

### Sprint 7 — tables

*A table is a maximal run of paragraphs containing kCellSep (0x1D, a byte
that cannot occur in UTF-8): rows are paragraphs, cells are separator-
delimited spans.* No stored table structure to maintain — rows appear when
a paragraph gains a separator and leave when it loses the last one:

- Layout: each row wraps its cells (proportional column widths from
  content, minimum 30 pt) and synthesises ONE line of the row height, so
  page flow, pagination and the incremental fingerprints treat a row like
  any other line.
- Caret and hit-testing map through cell geometry (byte ↔ cell ↔ x/y),
  self-tested round trip at every offset of a seeded table.
- Rendering reuses segments: cells emit ordinary segments at their grid
  positions — styles, spell squiggles and inline images work in cells for
  free — with grid rules drawn by the view.
- Editing: Tab inside a row inserts a cell; Enter adds a row (a new
  paragraph with separators stays in the table; a plain one leaves it);
  Backspace across a boundary merges cells. File ▸ Insert table drops a
  3×3 grid. Persistence: separators are ordinary text bytes, so .prose
  round trip needs nothing new. RTF export writes cells tab-separated;
  RTF table import (	rowd) is future work.
- Session note: the C++ hex-escape trap bit the test data ("12" is
  one greedy escape, not 0x1D followed by "12") — all separators are
  written octal (), which cannot be greedy.

**Exit criteria met:** seeded table renders with grid and cell text in
the guest (screenshot run/s7-table-observe.png); caret round trip, cell
counts, column fill and persistence selftested — 97/97 PASS.

**Sprint 4 hardening ledger** (all root-caused, all fixed): the selftest
case array overflowed its fixed size (now a vector); the agent died on
SIGPIPE from disconnected clients (ignored) and could wedge for 20
minutes on a hung child (deadline now 90 s, units checked twice);
children inherited the listening socket and starved restarts (CLOEXEC);
`cp` over a running binary truncated it (renames now); harness flags
mutated window-owned state from the app thread (delivered as a message to
the window thread); `killall` does not exist on this image — kill by
team id.

**Exit criteria review:** round trip ✓, perf budget ✓ (96 pages ~180 ms),
hey ✗ (open as above), printing still awaits real paper. Sprint 5 takes
the scripting open item, images-in-text with wrap, tables, and the styles
panel deferred from S4.

### Sprint 5+ (drawn from the GoBe research)

Images and frames in text (anchored, with wrap), tables, spell-check as
you type — each sized when Sprint 4 lands.

**Non-goal, decided 2026-09-19: Word .doc/.docx import.** A converter
stack capable of honest OOXML fidelity would be larger than the whole
Prose system image — out of proportion for a word processor on this OS,
and the GoBe post-mortem says fidelity-that-almost-works burns more trust
than no attempt. Interchange is RTF (ours, dependency-free), plain text,
and the native `.prose` format. If Word exchange ever matters, the right
shape is a small external converter tool, never code in ProseWriter.

## Test definitions (guest, automated via `vm/guest.sh`)

| ID | Test | Method | Pass |
|---|---|---|---|
| T01 | boot | `guest.sh wait-boot 180` then `ping` | PONG |
| T02 | launch | `launch /boot/home/apps/ProseWriter`; screenshot | window + page visible |
| T03 | selftest | `run /boot/home/apps/ProseWriter --selftest` | `SELFTEST PASS n/n`, exit 0 |
| T04 | typing | QMP `key` events type "The quick brown fox…"; screenshot + `hey`-free status check via T03 model | caret moved, text drawn |
| T05 | round-trip | selftest case: build doc → save → load → compare model | identical |
| T06 | layout invariants | selftest: line widths ≤ text column; pages = ceil(content/column) | PASS |
| T07 | RTF | selftest: export → import RTF, compare normalized model | styles preserved |
| T08 | find/replace | selftest: seeded doc, replace-all | expected model |
| T09 | perf | selftest: 100-page layout timed | < 150 ms |
| T10 | screenshots | every sprint: `vm/qmp.py shot` archived under `vm/run/` | eyeballed + diff vs previous |

`--selftest` is the reliable core: model/layout/RTF tests run in-process in
the guest and print PASS/FAIL lines; the QMP input tests cover the
integration layer (keyboard → view → app_server paint).

## Working agreement with the repo

Everything lives under `prosewriter/`; the Prose build tree and its image
are used read-only; our VM boots only `prosewriter/vm/prose-dev.image`
(an APFS clone). ProseWriter becomes a prose package when finished — until
then it installs by hand into `/boot/home/apps/` of the dev image only.


### Session ledger, 2026-09-20 (evening)

**Base image:** the owner's Sep 20 08:14 build ships prose_display +
prose_display_agent, and under plain QEMU + ramfb its app_server never
brings the framebuffer up (apps and the agent run; the screen stays
black). prose-dev.image is therefore cloned from
private_workspace/work/autoboot3/haiku.img (the Sep 19 base, 719 MB,
which boots and displays under ramfb). When the owner's QEMU-side
display story lands, re-clone to the current base.

**Never hand-type partition offsets.** The corrupted-image incident
was an inline bfs_shell run with the previous build's bounds against
the new 1 GB-partition image. All image writes go through
vm/install.sh and vm/extract.sh, which parse the MBR.

**Stale-instance discipline:** scripts target the app by signature;
the registrar answers with the OLDEST registered team. Every deploy
cycle: list team ids (`ps`), kill them BY NUMBER, relaunch, then test.
Quoting an awk pipeline through two shells expands to nothing — the
in-guest kill patterns never worked; parse `ps` output on the host.

**Save/load verified end to end** on the restored base: set Text,
do Save to a path, relaunch with the file as the launch argument —
the page renders the saved content. The file panels were correct all
along; what failed was testing against dead instances.


### Sprint 10 — print to PDF (paper metrics as the single truth)

**Goal:** a real PDF out of ProseWriter, paginated EXACTLY like the screen.

**Design — how layout reaches the printed page:**

1. `PWPageSetup` (paper + orientation + margins, points at 72 dpi) is the
   only source of truth. It already drives the on-screen layout; now the
   PDF consumes it too: each PDF page's MediaBox is exactly
   `pageWidth × pageHeight`, so an A4 document yields a true A4 PDF and a
   Letter document a true Letter PDF (595×842 / 612×792 points).
2. Pagination is `PWLayout`'s page spans — the same line→page assignment
   the screen shows. The writer never re-wraps or re-paginates: it asks
   the layout for the page count and renders page N through the view's
   existing print-mode path (`fPrinting` + `fPrintPage`: no desk, no
   shadow, no selection/caret, origin at 0,0).
3. Rendering: per page, the view records a `BPicture` at
   `scale = dpi/72` (default 144 dpi), replayed onto a white offscreen
   `BBitmap`. This reuses the whole on-screen renderer — fonts, styled
   runs, tables, inline images, headers/footers with `{page}`/`{pages}` —
   so WYSIWYG holds by construction and pagination cannot diverge.
4. Bitmaps are packed to RGB rows, Flate-compressed with the system
   `libz.so.1` (zlib 1.2.13; headers copied under `app/zlib/`, link set
   up by `mksysroot.sh`), embedded as Image XObjects; each content
   stream is one `cm` + `Do` painting the page edge to edge.
5. **Known cost, accepted for v1:** raster pages — text is not selectable
   in the PDF. Selectable text needs font embedding + text operators and
   waits for a real need; ~60–150 KB/page at 144 dpi is fine. There is
   no PDF printer driver in this image (only PS/PCL/Preview), so ours is
   the only PDF path; `Print` (BPrintJob) stays for real printers.

**Deliverables:** File ▸ Print to PDF… (save panel, StyledEdit pattern);
scripting property `PDF` (execute, `data:` path) so tests and `hey` can
drive it; paper table gains A3 and B5; the paper metrics chain
(dialog → `PWPageSetup` → layout → PDF MediaBox) verified end to end.

**Test matrix and exit criteria — all met (2026-09-20):**

| ID | Test | Result |
|---|---|---|
| P1 | selftest: paper metrics (A4/Letter/A3 exact points; narrower column ⇒ more pages; A3 ⇒ fewer) | PASS |
| P2 | selftest: PDF writer structure — off-screen window renders a 9-page doc; `%PDF` magic, `/MediaBox` count == layout CountPages, A4 box `[0 0 595 842]`, Flate, xref+`%%EOF` | PASS |
| P3 | guest: 18,000-word doc → `PDF do /tmp/out.pdf` in 0.8 s, 414 KB, fetched to host: 28 pages, all `[0 0 595 842]`, `/Count 28` agrees | PASS |
| P4 | guest smoke step 6: PDF magic + page count on every run | PASS (7/7) |
| P5 | host QuickLook thumbnail of page 1: typeset A4 page, correct margins, clean wraps | PASS (vm/run/s10-out.pdf) |

**Status: complete.** `--selftest` **156/156**. Two bugs found on the way,
both fixed with this sprint: `ReadFileToString` did a single `Read()` (short
reads truncate — the dictionary bug's last surviving sibling), and the
selftest originally read the PDF back through `BString::SetTo`, which stops
at NUL bytes — a PDF is binary; the check now reads into `std::string`.
Also: `EndPicture()` returns the stack `BPicture` you passed to
`BeginPicture()` — deleting that return value deletes a stack object.



### Sprint 9 — the fix pass (2026-09-20, driven by `docs/review-2026-09-20.md` + agent re-review)

Scope: every Part-1 bug from the review plus the additional bugs the second
review found, each with a regression test. **`--selftest` 144/144 PASS**
(was 97/97; the `|| true` non-test is gone, the benchmark-hiding debug
printfs are gone).

| | |
|---|---|
| Open works | `IFindLast()!=NULL` (int32 vs pointer, 9 sites) → `IEndsWith` helper; Open/launch-with-file/RTF/text all verified in the guest |
| Typing works | multi-byte UTF-8 routed as text; `B_DELETE` (0x7F) excluded from printable (it typed a garbage glyph); coalescing is per-UTF-8-character |
| Save is safe | write-tmp → Sync → rename (never `B_ERASE_FILE` on the document); the return value is a real status (Write()'s byte count made every successful save read as a failure — title/Modified/recent all dead); failures alert; failed quit-save keeps the window |
| Undo is real | `FORMAT` steps (bold/colour/family/styles undo+redo); coalesced typing redoes the whole run (was: first character only); removed images ride the undo step and come back |
| Layout cache asks the doc | fingerprints deleted; paragraphs carry a stable `id` + `revision` (bumped by every mutation incl. `ApplyFormat`), page setup bumps an epoch; margin changes, mid-paragraph bolding and same-length edits past byte 32 all re-measure now |
| Tables follow edits | row cache keyed by paragraph id, pruned of dead ids — a table no longer vanishes when a paragraph is inserted above it |
| Images own themselves | `shared_ptr<BBitmap>`: no leak on Open/AdoptDoc, no loss on cross-paragraph delete (the merge now moves them), inserts shift them with their markers |
| Windows die properly | panels announce their death (`'pWpl'`) — no dangling `fSetupWin`-class pointers; the app caches no window (the old `fWindow` dangled after the second window closed); quit counts only `PWWindow`s (hidden panels no longer keep a windowless app alive); Quit targets `be_app` and asks every window |
| RTF honesty | multi-entry font tables parse (fonts after the first no longer leak their names into the text); `\cf0` is emitted on return-to-black; cell separators export as `\tab` |
| Geometry | justified slack against the paragraph's column (indented justified text no longer overflows); End key stops on UTF-8 boundaries; print-mode underlines on their glyphs; scrollbar range zoom-aware; caret blink invalidation covers the caret; drag selection continues outside the view |
| Guard rails | Open over unsaved changes asks; page-setup margins can't swallow the page; print cancel is not an error alert; Cut checks the clipboard lock |

New harness property: **`Activate`** (execute) brings the window forward and
focuses the page view — on this headless guest a background-launched window
is never activated, so keyboard events reach nothing until it is called.
`hey ProseWriter do Activate` is now step one of every scripted GUI test.

**Verified in the guest, end to end:** selftest 144/144; typing via QMP
keyboard (on empty and loaded documents); forward-delete; Save (file on
disk, title = name, Modified cleared); reopen of a saved file (content,
title, caret); quit with changes → the save alert appears; quit clean →
the app exits.

**Session finding (input):** this guest's keyboard events reach the
input server (Ctrl+Alt+Del works) but a background-launched window is
never activated, and mouse clicks via the tablet do not activate windows
either — `Activate()` (scripting) is the reliable way in. The window also
loses activation moments after launch (something takes it); `do Activate`
before typing, every time.

### Sprint 9, part 2 — the owed list, closed as far as the harness allows

| | |
|---|---|
| F4: MIME registration | `RegisterDocumentType()` (PWApp ReadyToRun) installs `application/x-vnd.prose.ProseWriter-doc` with sniffer `1.0 ([0:3] "HMF1") ([4:7] "&dWp")` — the flattened-message magic plus our `'pWd&'` what-code — and preferred app = us. Verified on a fresh boot: `mimeset -f` types a saved file to OUR type (beats the built-in `haiku-bmessage` 0.40 rule) with the pref-app riding along. Syntax lesson: sniffer patterns MUST be parenthesized (`setmime -checkSniffRule` validates) |
| Alert buttons by keyboard | What is actually reachable: **Enter presses the default button (Cancel) — verified twice** (alert dismissed, window survived, Modified intact). **Escape does nothing** on Haiku's BAlert, and **BAlert buttons take no Tab focus** — so "Don't save" and "Save" cannot be driven headlessly. An earlier report claimed Tab+Enter worked; that was a bad verification (a leading question to the image reader on a screenshot with no alert) — retracted, re-tested with `ps`/hey probes as the ground truth |
| tests/ filled | `tests/guest-smoke.sh` (host side, drives the harness): selftest, launch+activate, set/get Text, save + title + clean, relaunch-with-file, clean quit. **GUEST SMOKE PASS 6/6** |

**Still human-owed (a real mouse):** the alert's "Don't save" and "Save"
buttons, Tracker double-click of a `.prose` file (the type + preferred app
now resolve; only the click itself is missing), drag-and-drop onto the
window, mouse text selection.

### Sprint 8 — files (defined retroactively; the work ran without one)

Scope: a document's whole life — new, open, save, save-as, export,
close, quit, and the window title that reflects it.

**Delivered and verified (headless round trip + selftest):**

| | |
|---|---|
| Save to path / Save (no path → save panel) / Save as… | panels wired since S2; `Save` scripting property verified by round trip |
| Open (.prose / RTF / text by extension) | `Open…` panel; launch-with-file argument; `B_REFS_RECEIVED` on the app (drop on icon / Tracker open) |
| Title = file name + `•` when modified | UpdateTitle on edit, open, save |
| Export as RTF | panel, own message |
| Recent documents | menu, persisted in settings |
| Quit with unsaved changes | Save changes? alert (Save / Don't save / Cancel) |
| Modified guard | document flags every mutation |

**Delivered, NOT yet verified — the exit criteria still owed:**

| ID | Test | Method |
|---|---|---|
| F1 | Save via the panel in the windowed VM writes the file; title updates | human, one document |
| F2 | Open via the panel loads; title = name; caret at start | human |
| F3 | Save as… writes a second file; recent menu shows both | human |
| F4 | `.prose` double-click in Tracker opens ProseWriter (needs MIME type `application/x-vnd.prose.ProseWriter-doc` registered + document suffix attr) | human; MIME registration is open work |
| F5 | Drag a .prose from Tracker onto the window → opens | human |
| F6 | Quit-with-modifications alert: each of the three buttons does the right thing | human |
| F7 | New window per New; close last = quit | human |

**Open work the sprint would have surfaced earlier:**
- MIME registration for `.prose` (sniffer rule, preferred app, document
  icon) — currently only the app signature is stamped, so F4 fails.
- The title after `hey set Text` shows no dot (scripted wholesale
  replace reads as clean) — acceptable, documented.
- Save panel's "name" pre-filled with the current file name.

Lesson recorded: requests that arrive mid-session still get a sprint
block — scope, test matrix, exit criteria — BEFORE code, even a small
one. The discipline is cheapest exactly when it feels skippable.
