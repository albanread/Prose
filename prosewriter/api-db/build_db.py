#!/usr/bin/env python3
"""Build prose_api.sqlite: a searchable index of the Be/Haiku public API and docs.

Sources
  headers  the arm64 devel header set (complete public API, arranged tree)
  bebook   research/bebook mirror (HTML)
  be_news  research/benewsletters mirror (HTML)
  hig      the interface guidelines from the reference source (DocBook XML)
  userguide  docs/userguide/en from the reference source

Run:  python3 build_db.py
Out:  prose_api.sqlite (next to this script)
"""
import html as htmllib
import json
import re
import sqlite3
import sys
import zipfile
from html.parser import HTMLParser
from pathlib import Path

HERE = Path(__file__).resolve().parent
HEADERS = Path("/Volumes/HaikuSrc/haiku/generated/objects/haiku/arm64/"
               "packaging/packages_build/regular/hpkg_-haiku_devel.hpkg/"
               "contents/develop/headers")
REFERENCE = Path("/Volumes/HaikuSrc/haiku-reference")
BEBOOK = HERE.parent / "research/bebook/www.haiku-os.org/legacy-docs/bebook"
BENEWS = HERE.parent / "research/benewsletters/www.haiku-os.org/legacy-docs/benewsletter"

KITS = {
    "app": "Application Kit", "interface": "Interface Kit",
    "storage": "Storage Kit", "support": "Support Kit",
    "kernel": "Kernel Kit", "net": "Net Kit", "network": "Network Kit",
    "mail": "Mail Kit", "media": "Media Kit", "midi": "Midi Kit",
    "midi2": "Midi Kit (Midi2)", "game": "Game Kit", "locale": "Locale Kit",
    "device": "Device Kit", "add-ons": "Add-on interfaces",
    "accelerants": "Accelerant API", "drivers": "Driver API",
    "translation": "Translation Kit", "tracker": "Tracker add-on API",
    "debugger": "Debugger API", "gl": "GL Kit", "drm": "DRM API",
    "midi-experimental": "Midi Kit (experimental)",
}


def kit_of(rel):
    top = rel.parts[0] if rel.parts else ""
    if top == "os":
        second = rel.parts[1] if len(rel.parts) > 1 else ""
        return KITS.get(second, "os/" + second)
    return top or "?"


# ---------------------------------------------------------------- scanning --
NAME_RE = re.compile(r"[A-Za-z_~][A-Za-z0-9_]*")


class Scanner:
    """Extract documented declarations from one header."""

    def __init__(self, text):
        self.text = text

    def scan(self):
        """Yield dicts: kind, name, signature, line, doc."""
        # Keep comments (they are the docs); mark them so statement-splitting
        # ignores their contents but the doc extractor can read them.
        text = self.text
        out = []
        # --- preprocessor constants
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)"
                             r"[ \t]*(.*)$", text, re.M):
            name, val = m.group(1), m.group(2).strip()
            if "(" == (val[:1] if val else ""):   # function-like macro
                continue
            out.append(dict(kind="macro", name=name, signature=f"#define {name} {val}",
                            line=text[:m.start()].count("\n") + 1, doc=""))
        # --- statements with preceding doc comments
        stmt_iter = self._statements(text)
        pending_doc, pending_line = "", 0
        for flag, stmt, line, doc in stmt_iter:
            if flag == "doc":
                pending_doc, pending_line = doc, line
                continue
            if flag == "close":
                continue
            # "open" carries the head of a class/struct/enum scope, "code" a
            # ;-terminated declaration; both go to the same interpreter.
            for d in self._decls_of(stmt, line, pending_doc if pending_line else "",
                                     text, flag == "open"):
                out.append(d)
            if flag == "code":
                pending_doc, pending_line = "", 0
        return out

    def _statements(self, text):
        """Yield ('code'|'open'|'doc', text, line, doc). One pass, no nesting
        detail beyond depth accounting is done here; _decls_of interprets."""
        i, n = 0, len(text)
        buf, buf_start = [], 0
        depth_stack = []        # depths at which '{' opened a scope
        depth = 0
        in_doc = None
        doc_buf, doc_start = [], 0
        while i < n:
            c = text[i]
            nxt = text[i + 1] if i + 1 < n else ""
            if in_doc:
                if in_doc == "line" and c == "\n":
                    d = "".join(doc_buf)
                    yield ("doc", d, text.count("\n", 0, doc_start) + 1, _clean_doc(d))
                    in_doc, doc_buf = None, []
                elif in_doc == "block" and c == "*" and nxt == "/":
                    d = "".join(doc_buf) + "*/"
                    yield ("doc", d, text.count("\n", 0, doc_start) + 1, _clean_doc(d))
                    in_doc, doc_buf = None, []
                    i += 1
                else:
                    doc_buf.append(c)
                if c == "\n" and in_doc == "block":
                    pass
                i += 1
                continue
            if c == "/" and nxt == "/":
                in_doc, doc_buf, doc_start = "line", [], i
                i += 2
                continue
            if c == "/" and nxt == "*":
                in_doc, doc_buf, doc_start = "block", [], i
                i += 2
                continue
            if c == '"' or c == "'":
                j = _skip_string(text, i)
                buf.append(text[i:j])
                i = j
                continue
            if c == "{":
                if buf and _is_scope_open(buf):
                    yield ("open", "".join(buf).strip(), buf_start, "")
                    depth_stack.append(depth)
                else:
                    depth_stack.append(-1)          # function body / initializer
                depth += 1
                buf, buf_start = [], i + 1
                i += 1
                continue
            if c == "}":
                depth -= 1
                if depth_stack and depth_stack.pop() == depth:
                    yield ("close", "}", i, "")
                buf, buf_start = [], i + 1
                i += 1
                continue
            if c == ";":
                s = "".join(buf).strip()
                if s:
                    yield ("code", re.sub(r"\s+", " ", s), buf_start, "")
                buf, buf_start = [], i + 1
            else:
                if not buf:
                    buf_start = i
                buf.append(c)
            i += 1
        if buf and "".join(buf).strip():
            yield ("code", re.sub(r"\s+", " ", "".join(buf).strip()), buf_start, "")

    def _decls_of(self, stmt, line, doc, full_text, is_open=False):
        out = []
        # access specifiers / empty / extern "C" wrappers
        s = re.sub(r"^(public|protected|private)\s*:\s*", "", stmt.strip())
        if not s or re.match(r"^(public|protected|private)\s*$", s):
            return out
        m = re.match(r"^(?:template\s*<[^>]*>\s*)?"
                     r"(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::(.*))?$", s)
        if m and is_open:   # with a body; `class X;` forward decls are skipped
            kind, name, bases = m.group(1), m.group(2), m.group(3)
            base_list = []
            if bases:
                for b in bases.split(","):
                    b = re.sub(r"\b(public|protected|private|virtual)\b", "", b).strip()
                    if b:
                        base_list.append(b.split("<")[0].strip())
            out.append(dict(kind=kind, name=name, signature=s + " { … }",
                            line=full_text[:line].count("\n") + 1, doc=doc,
                            bases=base_list))
            return out
        m = re.match(r"^(?:typedef\s+)?enum(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*(?:\{.*)?$", s)
        if m and "{" not in s:
            return out
        if s.startswith(("enum", "typedef", "friend", "using", "namespace",
                         "#", "extern \"C\"", "B_DECLARE", "struct _")):
            if s.startswith("typedef"):
                nm = NAME_RE.findall(s.replace("typedef", "", 1))
                if nm:
                    out.append(dict(kind="typedef", name=nm[-1], signature=s,
                                    line=full_text[:line].count("\n") + 1, doc=doc))
            return out
        # constants: `extern const type name` / `const type name = …`
        m = re.match(r"^extern\s+(?:const\s+)?[\w:<>*, ]+?\b\s*\**([A-Za-z_][A-Za-z0-9_]*)$", s)
        if m:
            out.append(dict(kind="constant", name=m.group(1), signature=s,
                            line=full_text[:line].count("\n") + 1, doc=doc))
            return out
        # functions & methods: something( args ) [const] [override] [=0]
        m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*"
                      r"(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?"
                      r"(?:=\s*(?:0|default|delete)\s*)?$", s)
        if m and "(" in s:
            name = m.group(1)
            # skip flow-control-looking tokens and pure macros
            if name in ("if", "while", "for", "switch", "return", "sizeof", "defined"):
                return out
            sig = s.rstrip("=").strip()
            kind = "function"
            ctor = re.match(r"^[A-Za-z_][A-Za-z0-9_]*\s*\(", s)
            out.append(dict(kind=kind, name=name, signature=s,
                            line=full_text[:line].count("\n") + 1, doc=doc))
            return out
        # bare constant lists inside enums arrive as 'code' too (A = 3, B)
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*(\s*=\s*[^,]+)?$", s):
            out.append(dict(kind="constant", name=s.split("=")[0].strip(),
                            signature=s, line=full_text[:line].count("\n") + 1,
                            doc=doc))
        return out


def _is_scope_open(buf):
    s = re.sub(r"\s+", " ", "".join(buf)).strip()
    return bool(re.match(r"^(?:template\s*<[^>]*>\s*)?(class|struct|enum|union|namespace)\b", s))


def _skip_string(text, i):
    q = text[i]
    i += 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == q:
            return i + 1
        i += 1
    return i


def _clean_doc(raw):
    d = raw
    if d.startswith("//"):
        d = d[2:]
    d = re.sub(r"^/\*\*?|\*/$", "", d.strip(), flags=re.M)
    lines = [re.sub(r"^[*/ ]+", "", ln).rstrip() for ln in d.split("\n")]
    d = "\n".join(ln.strip() for ln in lines if ln.strip())
    return d.strip()


# ------------------------------------------------------------- html to text --
class TextGrab(HTMLParser):
    SKIP = {"script", "style", "head"}

    def __init__(self):
        super().__init__()
        self.parts, self.title, self._in_title, self._skip = [], [], False, 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            self._skip += 1
        if tag == "title":
            self._in_title = True

    def handle_endtag(self, tag):
        if tag in self.SKIP and self._skip:
            self._skip -= 1
        if tag == "title":
            self._in_title = False
        if tag in ("p", "br", "div", "tr", "li", "h1", "h2", "h3", "h4", "pre"):
            self.parts.append("\n")

    def handle_data(self, data):
        if self._skip:
            return
        if self._in_title:
            self.title.append(data)
        self.parts.append(data)


def html_text(path):
    g = TextGrab()
    g.feed(path.read_text(errors="replace"))
    text = re.sub(r"[ \t]+", " ", "".join(g.parts))
    text = re.sub(r"\n\s*\n+", "\n\n", text)
    title = " ".join("".join(g.title).split())
    return title, text.strip()


def xml_text(path):
    raw = path.read_text(errors="replace")
    title_m = re.search(r"<title>(.*?)</title>", raw, re.S)
    raw = re.sub(r"<(programlisting|screen)[^>]*>.*?</\1>", " ", raw, flags=re.S)
    text = re.sub(r"<[^>]+>", " ", raw)
    text = htmllib.unescape(re.sub(r"[ \t]+", " ", text))
    text = re.sub(r"\n\s*\n+", "\n\n", text)
    return (title_m.group(1).strip() if title_m else path.name,
            text.strip())


# ------------------------------------------------------------------- main --
def main():
    db_path = HERE / "prose_api.sqlite"
    db_path.unlink(missing_ok=True)
    db = sqlite3.connect(db_path)
    db.executescript("""
PRAGMA journal_mode=wal;
CREATE TABLE symbols(
  id INTEGER PRIMARY KEY, kind TEXT, name TEXT, signature TEXT,
  kit TEXT, header TEXT, line INTEGER, doc TEXT, bases TEXT);
CREATE TABLE docs(
  id INTEGER PRIMARY KEY, source TEXT, title TEXT, path TEXT, text TEXT);
CREATE VIRTUAL TABLE symbols_fts USING fts5(
  name, signature, doc, header, content='symbols', content_rowid='id');
CREATE VIRTUAL TABLE docs_fts USING fts5(
  title, text, path, source, content='docs', content_rowid='id');
CREATE INDEX symbols_name ON symbols(name);
CREATE INDEX symbols_kit ON symbols(kit);
""")

    n_hdr = n_sym = 0
    if HEADERS.is_dir():
        for h in sorted((HEADERS / "os").rglob("*.h")):
            rel = h.relative_to(HEADERS)
            n_hdr += 1
            syms = Scanner(h.read_text(errors="replace")).scan()
            if not syms:
                continue
            kit = kit_of(rel)
            rows = [(s["kind"], s["name"], s.get("signature", ""),
                     kit, str(rel), s["line"], s.get("doc", ""),
                     json.dumps(s.get("bases", [])))
                    for s in syms if s.get("name")]
            db.executemany(
                "INSERT INTO symbols(kind,name,signature,kit,header,line,doc,bases)"
                " VALUES (?,?,?,?,?,?,?,?)", rows)
            n_sym += len(rows)
        db.execute("INSERT INTO symbols_fts(symbols_fts) VALUES('rebuild')")
    print(f"headers: {n_hdr} files, {n_sym} symbols")

    n_doc = 0
    for base, source in ((BEBOOK, "bebook"), (BENEWS, "be_news")):
        if not base.is_dir():
            print(f"(skipping {source}: not downloaded)")
            continue
        for f in sorted(base.rglob("*.html")):
            if "LegalNotice" in f.name or "favicon" in f.name:
                continue
            title, text = html_text(f)
            if len(text) < 200:
                continue
            db.execute("INSERT INTO docs(source,title,path,text) VALUES (?,?,?,?)",
                       (source, title or f.name, str(f.relative_to(base)), text))
            n_doc += 1
    for sub, source in (("docs/interface_guidelines", "hig"),
                        ("docs/user", "apibook"),
                        (".", "userguide")):
        base = REFERENCE / sub if sub != "." else Path(
            "/Volumes/HaikuSrc/haiku-userguide/userguide")
        if not base.is_dir():
            print(f"(skipping {source}: missing)")
            continue
        if source == "userguide":
            files = sorted((base / "en").rglob("*.html"))
        else:
            files = sorted(list(base.rglob("*.xml"))
                           + list(base.rglob("*.dox"))
                           + list(base.rglob("*.htm*")))
        for f in files:
            title, text = xml_text(f)
            if len(text) < 200:
                continue
            db.execute("INSERT INTO docs(source,title,path,text) VALUES (?,?,?,?)",
                       (source, title, f.name, text))
            n_doc += 1
    db.execute("INSERT INTO docs_fts(docs_fts) VALUES('rebuild')")
    db.commit()
    print(f"docs: {n_doc} pages")
    for k, c in db.execute("SELECT kind, count(*) FROM symbols GROUP BY kind"):
        print(f"  {k}: {c}")
    print(f"wrote {db_path}")


if __name__ == "__main__":
    main()
