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

1. Images anchored in text with wrap (BTranslationUtils → BBitmap in the
   paragraph model as an anchored object; wrap = column shaping).
2. Tables (paragraph-embedded grid, tab-stop rendering generalised).
3. Incremental relayout: reuse line runs before the damage point; the
   183 ms single-key figure on 96 pages is the number to beat.

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
