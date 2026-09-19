---
name: prose-api-db
description: Query the indexed Be/Haiku API and documentation (BeBook, Be Newsletters, Haiku HIG, userguide, API book) in prosewriter/api-db. Use when writing or reviewing ProseWriter/Haiku C++ and needing exact class members, method signatures, header locations, or Be-era documentation and design guidance.
---

# The Prose API database

`prosewriter/api-db/prose_api.sqlite` indexes the complete public Be/Haiku
API (16k symbols: classes, methods, constants, from the arm64 devel header
set) and the mirrored documentation (BeBook, Be Newsletters, the Haiku HIG,
the Haiku API book, the userguide — 980 pages), with SQLite FTS5 search.

Two ways to use it:

## MCP (preferred in ZCode)

The server is registered in this repo's `.zcode/config.json` as
`prose-api-db`; its tools appear automatically:

- `api_search(query, kind?, limit?)` — FTS over names/signatures/doc text
- `api_class(name)` — one class: header, doc, **bases, derived classes**,
  and its methods with signatures
- `api_method(name)` — every method with this name across classes
- `docs_search(query, source?, limit?)` — full text of bebook / be_news /
  hig / apibook / userguide, with highlighted excerpts
- `db_query(sql)` — read-only SELECT/WITH against the database

Tables: `symbols(kind,name,signature,kit,header,line,doc,bases)`,
`docs(source,title,path,text)`, `symbols_fts`, `docs_fts`.

## CLI (anywhere)

```
python3 prosewriter/api-db/mcp_server.py cli api_class BTextView
python3 prosewriter/api-db/mcp_server.py cli api_search SetFontSize
python3 prosewriter/api-db/mcp_server.py cli docs_search 'word wrap' bebook
python3 prosewriter/api-db/mcp_server.py cli db_query \
  "SELECT name,header FROM symbols WHERE kind='class' AND bases LIKE '%BView%'"
```

## Rebuilding

`python3 prosewriter/api-db/build_db.py` — re-scans the headers and the
research mirrors (safe to re-run; the DB is gitignored).

Good queries for writing app code: exact signatures before you guess one
(`api_method SetFamilyAndStyle`); the Be idiom for a feature
(`docs_search 'font panel' bebook`); what derives from a base class
(`db_query … bases LIKE '%\"BView\"%'`).
