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

**Exit criteria:** a multi-page document with headings, lists, indents and
tabs round-trips through `.prose` and RTF; `hey ProseWriter get Text of
Window 1` returns the document text; the perf measurements are recorded
and within budget. Printing on real paper stays open until a printer
exists on a Prose machine.

### Sprint 5+ (drawn from the GoBe research)

Images and frames in text (anchored, with wrap), tables, spell-check as
you type, .doc import via a converter — each sized when Sprint 4 lands.

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
