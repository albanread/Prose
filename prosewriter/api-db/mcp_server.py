#!/usr/bin/env python3
"""MCP server for prose_api.sqlite: the Be/Haiku API and documentation index.

As an MCP server (default):  stdio transport, JSON-RPC 2.0. Supports both the
newline-delimited framing (spec 2025-06-18+) and the Content-Length header
framing (older clients) — it peeks at the first byte.

As a CLI (for humans and scripts):
  mcp_server.py cli api_search BTextView
  mcp_server.py cli api_class BView
  mcp_server.py cli api_method Draw
  mcp_server.py cli docs_search 'word processor' bebook
  mcp_server.py cli db_query 'SELECT count(*) FROM symbols'

Tools exposed (same names as the CLI subcommands):
  api_search(query, kind?, limit?)   FTS over API symbol names/signatures/docs
  api_class(name)                    one class: methods, bases, derived classes
  api_method(name)                   every method with this name, per class
  docs_search(query, source?, limit?) FTS over BeBook/newsletters/HIG/userguide
  db_query(sql)                      read-only SELECT against the database
"""
import json
import re
import sqlite3
import sys
from pathlib import Path

DB = Path(__file__).resolve().parent / "prose_api.sqlite"

TOOLS = [
    dict(name="api_search",
         description="Search the Be/Haiku C++ API index (classes, methods, "
                     "constants) by name, signature or doc text. Returns "
                     "kind, name, signature, kit, header:line.",
         inputSchema=dict(type="object", properties=dict(
             query=dict(type="string", description="search text"),
             kind=dict(type="string", enum=["class", "struct", "function",
                                            "constant", "typedef", "macro"]),
             limit=dict(type="integer", default=20)),
             required=["query"])),
    dict(name="api_class",
         description="Everything about one API class: its header, doc, base "
                     "classes, all derived classes in the index, and every "
                     "method declared in the same header that looks like a "
                     "member.",
         inputSchema=dict(type="object", properties=dict(
             name=dict(type="string")), required=["name"])),
    dict(name="api_method",
         description="All API methods/functions with this exact name, with "
                     "their signatures across classes.",
         inputSchema=dict(type="object", properties=dict(
             name=dict(type="string")), required=["name"])),
    dict(name="docs_search",
         description="Full-text search of the mirrored documentation: the "
                     "BeBook (source=bebook), Be Newsletters (be_news), "
                     "Haiku interface guidelines (hig), the API book "
                     "(apibook) and userguide (userguide).",
         inputSchema=dict(type="object", properties=dict(
             query=dict(type="string"),
             source=dict(type="string", enum=["bebook", "be_news", "hig",
                                              "apibook", "userguide"]),
             limit=dict(type="integer", default=8)),
             required=["query"])),
    dict(name="db_query",
         description="Run a read-only SELECT against prose_api.sqlite. Tables: "
                     "symbols(kind,name,signature,kit,header,line,doc,bases), "
                     "docs(source,title,path,text), symbols_fts, docs_fts.",
         inputSchema=dict(type="object", properties=dict(
             sql=dict(type="string")), required=["sql"])),
]


def db():
    conn = sqlite3.connect(f"file:{DB}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    return conn


def fts_quote(q):
    return " ".join(f'"{t}"' for t in re.split(r"\s+", q.strip()) if t)


def rows_to_dicts(rows):
    return [dict(r) for r in rows]


def api_search(query, kind=None, limit=20):
    with db() as c:
        sql = ("SELECT symbols.* FROM symbols_fts "
               "JOIN symbols ON symbols.id = symbols_fts.rowid "
               "WHERE symbols_fts MATCH ?")
        args = [fts_quote(query)]
        if kind:
            sql += " AND symbols.kind = ?"
            args.append(kind)
        sql += " LIMIT ?"
        args.append(int(limit))
        return rows_to_dicts(c.execute(sql, args).fetchall())


def api_class(name):
    out = {}
    with db() as c:
        cls = c.execute("SELECT * FROM symbols WHERE kind IN ('class','struct')"
                        " AND name = ? ORDER BY length(header) LIMIT 1",
                        (name,)).fetchone()
        if not cls:
            return {"error": f"no class named {name}"}
        out.update(dict(cls))
        out["bases"] = json.loads(cls["bases"] or "[]")
        out["methods"] = rows_to_dicts(c.execute(
            "SELECT name, signature, line, doc FROM symbols "
            "WHERE kind='function' AND header = ? AND line > ? ORDER BY line",
            (cls["header"], cls["line"])).fetchall())
        out["derived"] = [r["name"] for r in c.execute(
            "SELECT name FROM symbols WHERE kind IN ('class','struct') "
            "AND bases LIKE ? ORDER BY name", (f'%"{name}"%',))]
    return out


def api_method(name):
    with db() as c:
        return rows_to_dicts(c.execute(
            "SELECT name, signature, kit, header, line, doc FROM symbols "
            "WHERE kind='function' AND name = ? ORDER BY header, line",
            (name,)).fetchall())


def docs_search(query, source=None, limit=8):
    with db() as c:
        sql = ("SELECT docs.id, docs.source, docs.title, docs.path, "
               "snippet(docs_fts, 1, '«', '»', '…', 24) AS excerpt "
               "FROM docs_fts JOIN docs ON docs.id = docs_fts.rowid "
               "WHERE docs_fts MATCH ?")
        args = [fts_quote(query)]
        if source:
            sql += " AND docs.source = ?"
            args.append(source)
        sql += " LIMIT ?"
        args.append(int(limit))
        return rows_to_dicts(c.execute(sql, args).fetchall())


def db_query(sql):
    sql = sql.strip().rstrip(";")
    if not re.match(r"(?is)^(\s|/\*.*?\*/)*(select|with)\s", sql) or \
            re.search(r"(?is)\b(insert|update|delete|drop|attach|create|"
                      r"replace|pragma\s+(!|\w+\s*=))", sql):
        raise ValueError("only read-only SELECT/WITH queries are allowed")
    if not re.search(r"(?i)\blimit\b", sql):
        sql += " LIMIT 200"
    with db() as c:
        return rows_to_dicts(c.execute(sql).fetchall())


FUNCS = dict(api_search=api_search, api_class=api_class, api_method=api_method,
             docs_search=docs_search, db_query=db_query)


# ------------------------------------------------------------------- MCP ----
def run_mcp():
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer

    def send(obj):
        stdout.write(json.dumps(obj).encode() + b"\n")
        stdout.flush()

    def result(rid, value):
        text = json.dumps(value, indent=1, default=str)
        send({"jsonrpc": "2.0", "id": rid,
              "result": {"content": [dict(type="text", text=text)]}})

    def error(rid, code, message):
        send({"jsonrpc": "2.0", "id": rid,
              "error": {"code": code, "message": message}})

    # Peek at the first byte to pick a framing.
    first = stdin.read(1)
    framed = bool(first) and first != b"{"

    def read_msg():
        nonlocal first
        if framed:
            headers = {}
            while True:
                line = stdin.readline()
                if not line:
                    return None
                if first:
                    line, first = first + line, None
                text = line.decode("latin1").strip()
                if not text:
                    break
                if ":" in text:
                    k, v = text.split(":", 1)
                    headers[k.strip().lower()] = v.strip()
            n = int(headers.get("content-length", 0))
            return json.loads(stdin.read(n))
        line = stdin.readline()
        if first:
            line, first = first + line, None
        if not line:
            raise EOFError
        if not line.strip():
            return None
        return json.loads(line)

    while True:
        try:
            msg = read_msg()
        except EOFError:
            break
        except json.JSONDecodeError:
            continue
        if msg is None:
            continue
        method = msg.get("method", "")
        rid = msg.get("id")
        if method == "initialize":
            send({"jsonrpc": "2.0", "id": rid, "result": dict(
                protocolVersion="2025-06-18",
                capabilities=dict(tools=dict()),
                serverInfo=dict(name="prose-api-db", version="1.0"))})
        elif method == "notifications/initialized":
            pass
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": rid, "result": dict(tools=TOOLS)})
        elif method == "tools/call":
            name = msg["params"]["name"]
            args = msg["params"].get("arguments", {}) or {}
            try:
                result(rid, FUNCS[name](**args))
            except Exception as e:
                error(rid, -32000, f"{type(e).__name__}: {e}")
        elif rid is not None:
            error(rid, -32601, f"unknown method {method}")


def run_cli():
    if len(sys.argv) < 3 or sys.argv[1] != "cli" or sys.argv[2] not in FUNCS:
        print(__doc__)
        return 2
    name = sys.argv[2]
    args = sys.argv[3:]
    if name == "api_search":
        r = api_search(args[0], args[1] if len(args) > 1 else None,
                       int(args[2]) if len(args) > 2 else 20)
    elif name == "api_class":
        r = api_class(args[0])
    elif name == "api_method":
        r = api_method(args[0])
    elif name == "docs_search":
        r = docs_search(args[0], args[1] if len(args) > 1 else None,
                        int(args[2]) if len(args) > 2 else 8)
    else:
        r = db_query(args[0])
    print(json.dumps(r, indent=1, default=str))
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "cli":
        sys.exit(run_cli())
    run_mcp()
