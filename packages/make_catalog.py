#!/usr/bin/env python3
"""Generate packages/CATALOG.md from saved GitHub API pages (data/repos-N.json).

Curates the HaikuArchives native-software collection: Be/Haiku API C++
software for the Prose fork. No foreign GUI toolkits (GTK/Qt/wx) — entries
that are clearly ports of those are classed 'foreign' and excluded from the
default batch.
"""
import json
from pathlib import Path

DATA = Path(__file__).resolve().parent / "data"
OUT = Path(__file__).resolve().parent / "CATALOG.md"

# Curated first batch: high value, native, tractable size.
FIRST_BATCH = {
    "BeSampleCode", "ArtPaint", "Vision", "BePDF", "Pe", "Paladin", "Filer",
    "DeskNotes", "PecoRename", "TaskManager", "Calendar", "Slayer", "Tipster",
    "TheAwesomeResizer", "CopyNameToClipboard", "StreamRadio", "MidiSynth",
    "HexVexed", "BeSpider", "WakeUp", "LibWalter", "Peek", "FontBoy", "Calc",
    "Archiver", "Bong", "Conway", "RpnCalc", "Minesweeper", "Toner",
    "PhantomLimb", "BeLife", "DateReplicant", "Album", "BeSol", "BabyBe",
    "SlaveMind", "Dynamate", "BShisen", "Cygnus", "Peek",
    "FtpPositive", "CapitalBe", "Weather", "Lingua", "MeV", "Sequitur",
    "PecoBeat", "BeVexed", "PonpokoDiff", "ResourceEdit", "FontBoy",
}

# Dead software and categories we do not want: CD/DVD burners (no burner
# hardware in a VM), Napster (network long gone).
EXCLUDED = {"BurnItNow", "Helios", "BeNapster"}

RULES = [
    ("games", ["game", "solitaire", "tetris", "puzzle", "chess", "mine",
               "spider", "pong", "life", "mastermind", "bomber", "battleship",
               "tris", "goban", "reversi", "snake"]),
    ("demos-and-samples", ["sample", "demo", "example", "screensaver", "saver",
                           "replicant", "blanker", "mascot"]),
    ("editors-and-ides", ["editor", "ide", "programming", "developer",
                          "resource edit", "designer", "word processor"]),
    ("graphics-and-media", ["paint", "image", "photo", "draw", "vector",
                            "pdf", "djvu", "audio", "midi", "music", "sound",
                            "cd ", "burn", "mp3", "lame", "radio", "animator",
                            "camera", "font"]),
    ("internet-and-network", ["irc", "mail", "e-mail", "ftp", "web", "http",
                              "chat", "napster", "nntp", "usenet", "network",
                              "twitter", "xmpp", "direct connect", "hotline",
                              "ppp", "news"]),
    ("utilities", ["rename", "organizer", "filer", "archive", "zip", "notes",
                   "note", "calendar", "weather", "clock", "timer", "calc",
                   "calculator", "monitor", "process", "tracker add-on",
                   "utility", "explorer", "search", "backup", "snapshot",
                   "diff", "resizer", "converter", "system", "deskbar",
                   "workspace", "usb", "lock", "tips", "translat"]),
    ("libraries", ["library", "libraries", "bindings", "classes",
                   "framework", "api", "glue"]),
]

FOREIGN = ["gtk", "qt", "wxwidgets", "sdl", "java", "python port", "browser",
           "firefox", "chromium"]


def classify(name: str, desc: str) -> str:
    text = f"{name} {desc}".lower()
    if any(k in text for k in FOREIGN):
        return "foreign"
    for cat, keys in RULES:
        if any(k in text for k in keys):
            return cat
    return "misc"


def main() -> None:
    repos = {}
    for p in sorted(DATA.glob("repos-*.json")):
        for r in json.loads(p.read_text()):
            repos[r["name"]] = (r.get("description") or "").strip()

    cats: dict[str, list] = {}
    for name, desc in sorted(repos.items()):
        if name in EXCLUDED:
            continue
        cat = classify(name, desc)
        cats.setdefault(cat, []).append((name, desc))

    lines = [
        "# Prose packages — catalog of Haiku-native software",
        "",
        "The [HaikuArchives](https://github.com/haikuarchives) collection"
        f" (auto-indexed via `make_catalog.py`: {len(repos)} repos in `data/`,"
        f" {sum(len(v) for k, v in cats.items() if k != 'foreign')} listed here).",
        "Criteria: native Be/Haiku API C++ only — no GTK/Qt/wx ports.",
        "Codecs and POSIX/shell software are in scope.",
        "",
        "Batch 1 = curated first wave to clone, cross-build (arm64) and"
        " install into the Prose image. Most have haikuports recipes, which"
        " `scripts/prosepkg build <port>` builds (see README.md).",
        "",
    ]
    for cat in ["editors-and-ides", "graphics-and-media", "utilities",
                "internet-and-network", "games", "demos-and-samples",
                "libraries", "misc"]:
        items = cats.get(cat, [])
        if not items:
            continue
        lines += [f"## {cat.replace('-', ' ').title()} ({len(items)})", "",
                  "| repo | description | batch 1 |",
                  "|---|---|---|"]
        for name, desc in items:
            batch = " **yes**" if name in FIRST_BATCH else ""
            desc = desc.replace("|", "/")
            lines.append(f"| [{name}](https://github.com/haikuarchives/{name})"
                         f" | {desc} |{batch} |")
        lines.append("")

    if cats.get("foreign"):
        lines += ["## Excluded (foreign toolkits / out of scope)", ""]
        lines += ", ".join(sorted(n for n, _ in cats["foreign"]))
        lines.append("")

    OUT.write_text("\n".join(lines) + "\n")
    print(f"catalog: {len(repos)} repos -> {OUT}")


if __name__ == "__main__":
    main()
