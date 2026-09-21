#!/usr/bin/env python3
"""Generate the Prose brand assets (SVG sources) into brand/svg/.

Identity: Prose — an operating system for people who write.
  mark:     the pilcrow (paragraph mark): a half-annulus bowl fused to a stem
  kernel:   Kronkite ("...and that's the way it is.")
  palette:  paper #F5EFE3, ink #1B1917, prose-blue #2F5AA8, vermilion #D6482B

Rendered PNGs are produced by tools/render.sh (qlmanage).
"""
from pathlib import Path

SVG = Path(__file__).resolve().parent / "svg"
SVG.mkdir(parents=True, exist_ok=True)

PAPER = "#F5EFE3"
INK = "#1B1917"
BLUE = "#2F5AA8"
VERMILION = "#D6482B"

CLIP = ('<defs><clipPath id="leftHalf"><rect x="0" y="0" width="146.5" '
        'height="240"/></clipPath></defs>')


def write(name: str, body: str) -> None:
    (SVG / name).write_text(body.strip() + "\n")


def pilcrow(x: int, y: int, s: float, fill: str, opacity: float = 1.0) -> str:
    """The Prose mark in a 240x240 box: bowl (left half-annulus, centre 146,107,
    outer r57 / inner r27) fused to a stem (x 118-146, y 50-220)."""
    return f"""<g transform="translate({x},{y}) scale({s/240})" opacity="{opacity}">
  {CLIP}
  <g clip-path="url(#leftHalf)">
    <path fill-rule="evenodd" fill="{fill}"
          d="M146,107 m-57,0 a57,57 0 1,0 114,0 a57,57 0 1,0 -114,0
             M146,107 m-27,0 a27,27 0 1,0 54,0 a27,27 0 1,0 -54,0"/>
  </g>
  <rect x="118" y="50" width="28" height="170" rx="14" fill="{fill}"/>
</g>"""


def wordmark_left(x: int, y: int, size: int, fill: str, sub: str = "",
                  subfill: str = "") -> str:
    """Left-anchored lockup: 'Prose' with an optional letterspaced sub-line."""
    sub_tspan = ""
    if sub:
        sub_tspan = (f'\n    <tspan x="{x}" dy="{size * 0.78}" font-size="{size * 0.24}" '
                     f'letter-spacing="{size * 0.075}" fill="{subfill or fill}">{sub}</tspan>')
    return (f'<text x="{x}" y="{y}" font-family="Georgia, \'Times New Roman\', serif" '
            f'font-size="{size}" font-weight="700" fill="{fill}">Prose{sub_tspan}</text>')


def wordmark_center(x: int, y: int, size: int, fill: str, sub: str = "",
                    subfill: str = "", anchor: str = "middle") -> str:
    """Centered lockup for splash screens."""
    sub_tspan = ""
    if sub:
        sub_tspan = (f'\n    <tspan x="{x}" dy="{size * 0.78}" font-size="{size * 0.24}" '
                     f'letter-spacing="{size * 0.075}" fill="{subfill or fill}">{sub}</tspan>')
    return (f'<text x="{x}" y="{y}" text-anchor="{anchor}" '
            f'font-family="Georgia, \'Times New Roman\', serif" '
            f'font-size="{size}" font-weight="700" fill="{fill}">Prose{sub_tspan}</text>')


# ---------------------------------------------------------------- logos ------

for variant, fg, bg in [("light", PAPER, INK), ("dark", INK, PAPER)]:
    write(f"prose-logo-{variant}.svg", f"""
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 980 320">
  <rect width="980" height="320" fill="{bg}"/>
  {pilcrow(40, 40, 240, fg)}
  {wordmark_left(330, 190, 130, fg, "T H E   K R O N K I T E   K E R N E L", VERMILION)}
</svg>""")

write("prose-mark.svg", f"""
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 240 240">
  {pilcrow(0, 0, 240, INK)}
</svg>""")

# Transparent-background lockups and marks for embedding over UI colours
# (AboutSystem logo.png / logo_dark.png imports).
write("prose-mark-paper.svg", f"""
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 240 240">
  {pilcrow(0, 0, 240, PAPER)}
</svg>""")

for variant, fg in [("ink", INK), ("paper", PAPER)]:
    write(f"prose-lockup-{variant}.svg", f"""
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 980 320">
  {pilcrow(40, 40, 240, fg)}
  {wordmark_left(330, 190, 130, fg, "T H E   K R O N K I T E   K E R N E L", VERMILION)}
</svg>""")

# ------------------------------------------------------------- wallpapers ----

RULE = "rgba(47,90,168,0.16)"     # faint prose-blue rules on paper
RULE_DARK = "rgba(245,239,227,0.07)"
W, H = 3840, 2160


def ruled(step: int, color: str, top: int = 220, bottom: int = H - 220) -> str:
    lines = "".join(f'<line x1="0" y1="{y}" x2="{W}" y2="{y}" stroke="{color}" '
                    f'stroke-width="2"/>' for y in range(top, bottom, step))
    return f'<g>{lines}</g>'


def lockup(x: int, y: int, fill: str, opacity: float) -> str:
    """The wallpapers' wordmark: 'Prose' in bold with the mark in vermilion."""
    return (f'<text x="{x}" y="{y}" font-family="Georgia, serif" font-size="74" '
            f'font-weight="700" fill="{fill}" opacity="{opacity}">Prose'
            f'<tspan fill="{VERMILION}">&#182;</tspan></text>')


def asterism(x: int, y: int, size: int, fill: str, opacity: float) -> str:
    """The asterism (three asterisks in a triangle), built from Georgia's
    asterisk: Georgia has no U+2042 of its own."""
    star = (f'font-family="Georgia, serif" font-size="{size}" fill="{fill}" '
            f'opacity="{opacity}"')
    half = size * 0.3
    return (f'<text x="{x}" y="{y}" {star}>*</text>'
            f'<text x="{x + 2 * half}" y="{y}" {star}>*</text>'
            f'<text x="{x + half}" y="{y - size * 0.5}" {star}>*</text>')


def editor_flourish() -> str:
    """The proofreader's circle-and-caret, in vermilion."""
    return f"""
  <path d="M 620 1480 C 640 1330, 1180 1320, 1210 1470 C 1235 1600, 700 1640, 640 1560"
        fill="none" stroke="{VERMILION}" stroke-width="14" stroke-linecap="round"/>
  <path d="M 1310 1580 C 1420 1500, 1560 1500, 1650 1560" fill="none"
        stroke="{VERMILION}" stroke-width="14" stroke-linecap="round"/>"""


# Manuscript light: warm paper, ghost pilcrow, one vermilion editor's flourish.
write("wallpaper-manuscript-light.svg", f"""
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}">
  <rect width="{W}" height="{H}" fill="{PAPER}"/>
  {ruled(58, RULE)}
  {pilcrow(2230, 420, 1290, INK, 0.05)}
  {editor_flourish()}
  {lockup(240, 1930, INK, 0.85)}
  {asterism(3540, 360, 88, BLUE, 0.65)}
</svg>""")

# Manuscript dark: ink field, chalk rules, the mark in paper.
write("wallpaper-manuscript-dark.svg", f"""
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}">
  <rect width="{W}" height="{H}" fill="#15130F"/>
  {ruled(58, RULE_DARK)}
  {pilcrow(2230, 420, 1290, PAPER, 0.055)}
  {editor_flourish()}
  {lockup(240, 1930, PAPER, 0.9)}
  {asterism(3540, 360, 88, PAPER, 0.5)}
</svg>""")

# ------------------------------------------------------------- boot splash --

write("boot-splash.svg", f"""
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 768">
  <rect width="1024" height="768" fill="{INK}"/>
  {pilcrow(424, 128, 176, PAPER)}
  {wordmark_center(512, 430, 96, PAPER, "K R O N K I T E", VERMILION)}
  <text x="512" y="610" text-anchor="middle" font-family="Georgia, serif"
        font-size="30" fill="{PAPER}" opacity="0.55">hrev60122 &#183; arm64</text>
  <text x="512" y="700" text-anchor="middle" font-family="Georgia, serif"
        font-style="italic" font-size="26" fill="{PAPER}" opacity="0.45"
        >&#8230;and that&#8217;s the way it is.</text>
</svg>""")

print("wrote:", ", ".join(sorted(p.name for p in SVG.glob("*.svg"))))
