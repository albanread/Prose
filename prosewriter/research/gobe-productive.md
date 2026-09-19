# GoBe Productive — research for ProseWriter

What it was, what it looked like, and what is worth carrying forward.
Sources are linked at the end; the four `images/gp*test.png` files are
screenshots saved from the OSNews preview.

## The product

**GoBe Productive** (styled *gobeProductive*) was the flagship third-party
application of the BeOS era: an *integrated* office suite — word processing,
spreadsheet, vector graphics, image processing and a simple presentation
tool — in one application, one UI and one document format (`.pve`), where a
text page, a spreadsheet frame and a vector drawing could live in the same
file and be edited in place.

It was written in C++, closed source, by **Gobe Software, Inc.** of
Portland, Oregon, founded in 1997 by ex-ClarisWorks engineers (Scott
Holdaway, Tom Hoke, Scott Lindsey, Bruce Q. Hammond, Carl Grice, Bob Hearn)
— the people who had built ClarisWorks/AppleWorks after the Apple II
*StyleWare* days. Productive was, deliberately, "ClarisWorks for BeOS".

| Version | Date | Notes |
|---|---|---|
| 1.0 | Aug 1998 | first BeOS release |
| 2.0 / 2.0.1 | Aug 1999 / Feb 2000 | final BeOS release; "by far the most polished" of BeOS word processors |
| 3.0 | Dec 2001 | Windows port (Be Inc. was dead); Linux only reached pre-alpha |

Gobe also became BeOS's publisher in North America around 2000. When Be
folded, Gobe ported the Be API itself to Windows — the GP3 install carried a
`libbe.dll` and a Translators folder — which is one of the more remarkable
facts of the era. The company could not raise capital after the 2000 crash,
suspended operations in 2002 (an OSNews story announced a GPL release that
never materialised), and the domain lapsed in 2010. The source was never
released; Productive is abandonware.

## What the word processor did (GP2 on BeOS / GP3)

From reviews and the OSNews hands-on (Nov 2001):

- **Page-based editing**: real pages with headers/footers, grid and rulers;
  SDI (one window per document) with **floating panels** — Font, Styles,
  Ink, Transparency, Zoom — toggled from a View ▸ Panels menu.
- **Styles panel**: named paragraph/character styles, editable; a
  Styles/Link option made text behave like an HTML link.
- **Live font panel**: changes applied immediately ("live review"), no
  modal preview.
- **Format painting** by right-click (copy character/paragraph format).
- **Spell check as you type** (new in GP3, most-requested GP2 feature),
  thesaurus, user-learnable dictionary.
- **Insert anything**: charts, images, tables, vector shapes, hyperlinks;
  images could act as *glyphs* (inline characters) or as anchored objects.
- **Export**: native `.pve`, Word `.doc`, text, RTF, HTML, PDF (new in GP3).
- Praised: fast launch (2–3 s even on 1998 hardware), tight integration,
  one file for everything. Criticised: weak `.doc` import (>1.5 MB or
  pictures = failure), feature-starved text engine by Word standards, no
  text wrap around image glyphs.

## What it looked like

Classic BeOS aesthetics (per the screenshots and contemporary accounts):

- Be-native **yellow window tabs**, thin borders, grey `B_PANEL_BACKGROUND`
  chrome; Deskbar at the top-right of the desktop in BeOS R4/R5 style.
- A **single formatting toolbar row**: font family/size popups, B/I/U
  toggles, alignment, list toggles — small 16×16 monochrome-ish icons,
  flat, no borders until hover.
- **Rulers** on top and left of the page area, with draggable margin and
  tab markers; the page sat on a grey desk background with a subtle shadow.
- **Floating palettes** rather than modal dialogs for styles, ink (colour)
  and zoom.
- Status bar at the bottom: page number, zoom factor, view mode.

## What ProseWriter takes from this

1. **The integration lesson** — one document model that treats a paragraph,
   an image and (later) a table as first-class citizens, rather than a text
   buffer with pasted pictures. ProseWriter starts text-first but the model
   keeps the door open.
2. **The immediacy lesson** — panels, not modal dialogs; live preview;
   instant launch. Gobe's 2-second cold start is the benchmark.
3. **The BeOS look** — yellow tabs, flat toolbars, floating palette, rulers,
   page-on-grey-desk. Detailed in `docs/ui-design.md`.
4. **The cautionary tales** — .doc import fidelity broke trust. Decision
   (2026-09-19): ProseWriter ships no .doc/.docx support at all — the
   converter would outweigh the OS. RTF is the interchange format, kept
   honest about what it preserves.

## Sources

- [Gobe Software — Wikipedia](https://en.wikipedia.org/wiki/Gobe_Software)
- [World's First Preview of gobeProductive 3 — OSNews, Nov 2001](https://www.osnews.com/story/265/worlds-first-preview-of-gobeproductive-3)
  (screenshots saved as `images/gp1test.png` … `gp4test.png`: live spell
  check, 164 % zoom view, the formula panel, text-wrap options)
- [EXCLUSIVE: gobeProductive to be Released under the GPL — OSNews, Aug 2002](https://www.osnews.com/story/968/exclusive-gobeproductive-to-be-released-under-the-gpl/)
- [Gobe Productive 2.0 review — Target PC Magazine (via archive.org)](https://web.archive.org/web/2000*/targetpc.com/software/reviews/productive20/)
- [Gobe Productive 2.0 (clone) — Haiku community discussion, 2021](https://discuss.haiku-os.org/t/gobe-productive-2-0-clone/)

Save-for-later: the Haiku discussion thread values most "one file with
everything in it" — worth re-reading when ProseWriter grows beyond text.
