# ProseWriter UI design — the BeOS look, done properly

Grounding: the Haiku HIG (indexed in `api-db`), the BeBook's Interface Kit
chapters, and GoBe Productive's Be-era design (see `research/gobe-productive.md`).

## Principles, from the HIG

> "Haiku is an operating system which is known for its speed and being easy
> for anyone to use."

1. **The user is intelligent and busy.** No wizards, no modal dialogs for
   things that can be panels. (Gobe's live Font panel, not Word's modal one.)
2. **Consistency with the system beats personality.** Yellow tab, standard
   menu bar, standard shortcuts, standard alert colors. ProseWriter should
   look like StyledEdit's bigger sibling, not like a foreigner.
3. **Everything immediate**: launch fast (Gobe: 2–3 s in 1998 — on this VM,
   a cold launch under 1 s is the target), keystrokes appear instantly,
   zoom never blocks.
4. **Panels and inspectors, not dialogs**, for anything reversible.

## Window layout

```
┌───────────────────────────────▟▙──────────────────────────┐  ← yellow title tab (default BWindow)
│ File  Edit  Text  Search  Document  Window  Help           │  ← standard BMenuBar
├───────────────────────────────────────────────────────────┤
│ Family ▾  Style ▾  Size ▾ | B I U S | ⟲ ⟳ | ≡ ≡ | 100% ▾  │  ← toolbar: BMenuFields + flat toggle buttons
├───────────────────────────────────────────────────────────┤
│  ruler: margin ▸▸ ·····tab markers····· ▸◂                 │  ← PW Ruler view
│ ┌───────────────────────────────────────────────────────┐ │
│ │                    grey desk (panel background)       │ │
│ │   ┌─────────────────────────────┐   ← page shadow     │ │
│ │   │  A4 page, white             │                      │ │
│ │   │  text with proper margins   │                      │ │
│ │   └─────────────────────────────┘                      │ │
│ │                                            ▲ scrollbars │ │
├───────────────────────────────────────────────────────────┤
│ page 1 of 1 · 42 words · 12 pt Noto Sans          zoom ▾  │  ← status bar
└───────────────────────────────────────────────────────────┘
```

- **Find bar** slides in above the status bar (Search ▸ Find, `Alt+F`),
  the way WebPositive does it — not a floating dialog.
- **Zoom**: 50/75/100/150/200 % and "fit page"; zoom keeps the caret's
  document position anchored on screen.

## BeOS look details

| Element | Choice |
|---|---|
| Window | stock `B_TITLED_WINDOW`, `B_QUIT_ON_WINDOW_CLOSE`, `B_ASYNCHRONOUS_CONTROLS`, `B_AUTO_UPDATE_SIZE_LIMITS`; yellow tab and thin borders come free — never custom chrome |
| Colours | only `ui_color()` constants: `B_PANEL_BACKGROUND` desk, `B_DOCUMENT_BACKGROUND`/`B_DOCUMENT_TEXT` page, `B_KEYBOARD_NAVIGATION_COLOR` focus. No hard-coded palette |
| Toolbar | one row, 16 px bevel-flat toggles (drawn `BButton` with `B_WILL_DRAW`), separators `B_SEPARATOR_V_LINE`; greyscale icons with the Haiku icon yellow used sparingly (state on) |
| Ruler | 1 px ticks, 5 px major ticks; margin triangles and tab markers in `B_CONTROL_TEXT_COLOR`; drag with live feedback |
| Selection | `B_CONTROL_HIGHLIGHT`-style inversion using `B_DOCUMENT_TEXT`/`B_DOCUMENT_BACKGROUND` mix — exactly what BTextView users expect |
| Menus | standard items with Alt-key shortcuts on the right in menu-item grey; ellipsis (…) only when a dialog follows (`Open…`, `Find…` → panel gets no ellipsis) |
| Fonts UI | `be_plain_font` for chrome; the *document* default is Noto Sans 12 pt; `be_fixed_font` offered for code-ish styles. Font menu lists families by `count_font_families()` with styles submenus — the BeBook's own pattern |
| Alerts | `BAlert` only for real decisions (Save changes? Revert?) — 3 buttons max, escape cancels |
| Status bar | `BStringView`s, right-aligned zoom menu field, thin top separator |

## Keyboard map (Be/Haiku conventions)

`Alt` is Command: `Alt+N/S/O/Q/W` new/save/open/quit/close, `Alt+Z/Y`
undo/redo, `Alt+X/C/V` cut/copy/paste, `Alt+F` find, `Alt+G` find next,
`Alt+B/I/U` bold/italic/underline, `Alt+plus/minus` zoom, `Alt+Enter`
page setup. Cursor keys with `Alt` = word-wise, with `Shift` = extend,
`Home/End` line bounds, `PgUp/PgDn` page bounds.

## Why not BTextView

BTextView is single-flow and unpaginated: no page model, no margins, no
per-paragraph layout control, no headers/footers, and its run-styles API is
coarse. A word processor needs a document model and a layout engine. So
ProseWriter = custom document model (`PWDocument`) + layout engine
(`PWLayout`) + one custom `BView` (`PWPageView`) that renders pages from
the layout. BTextView knowledge still applies — cursor/selection semantics
and rendering conventions follow what its users expect.

## App identity

- Signature `application/x-vnd.prose.ProseWriter`; document MIME type
  `application/x-vnd.prose.ProseWriter-doc`.
- Documents: flattened `BMessage` file (`.prose`), RTF, plain text.
- Icon: HVIF later; placeholder comes from `B_APP_ICON`-style resource.
- About: name, "a word processor for Prose", version, Haiku/Prose credit.
