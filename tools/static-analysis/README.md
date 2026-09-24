# Static analysis

Scans the Prose Haiku tree with the analyzers on the Mac. The method and the
findings of the first run are in `docs/static-analysis.md`.

	tools/static-analysis/run.sh /tmp/sa            # the core, everything
	tools/static-analysis/run.sh /tmp/sa all quick  # the whole image, no clang-tidy

What each step writes into the output directory:

- **`compile_commands.json`**: clang commands for
  `--target=aarch64-unknown-haiku`, made from jam's dry run by `mkcdb.py`.
  jam builds nothing and writes nothing in the tree.
- **`clang-warnings.txt`**: clang's bug-finding warnings (`warncheck.py`).
- **`dangling.txt`**: pointers kept, assigned or returned from a temporary
  that dies at the end of the statement (`queries/dangling.query`), such as
  `const char* p = partition->ContentName();`. A hit is a real
  use-after-free when the temporary owns its buffer. It is only latent when
  the temporary shares its buffer with an object that lives on. Haiku's
  `BString` shares buffers by reference count, and
  `BStringList::StringAt()` returns such a shared copy.
  `queries/dangling_cases.cpp` holds the matcher's test cases:
  `clang-query -f queries/dangling.query queries/dangling_cases.cpp --
  -std=c++17` must report the five BAD lines and nothing else.
- **`ioresult.txt`**: calls returning `ssize_t` (Read, Write, ReadAttr,
  ...) compared with 0 or `B_OK` (`queries/ioresult.query`).
- **`ast-grep.json`**: the rules in `rules/`, which catch Haiku API traps
  and kernel hazards by syntax.
- **`gcc-analyzer.txt`**: GCC 13's `-fanalyzer` over the kernel's and
  libroot's C files, with the cross compiler and the real flags.
- **`flawfinder.csv`**: flawfinder's level 4 and 5 hits (lexical: `strcpy`,
  `sprintf`, `system`, check-then-use races).
- **`cppcheck.txt`**: cppcheck at warning and portability level, run from
  a copy of the database without the gcc and libstdc++ headers
  (`cppcheckdb.py`), which cppcheck cannot parse. It is told the
  target's architecture and that the environment is hosted. Without those
  it stops at `HaikuConfig.h`'s "Unsupported architecture!" in every file
  and reports nothing.
- **`clang-tidy.txt`** and **`clang-tidy-findings.json`**: the clang
  static analyzer and bugprone checks (`clang-tidy-checks.txt`),
  deduplicated and classified by `tidysum.py`.

The needs: Homebrew's `llvm` (clang, clang-tidy, clang-query), `cppcheck`
and `flawfinder` (Homebrew; skipped if absent), ast-grep
(on PATH, or the copy in NewReview's tool cache), jam in `~/bin`, and the
tree at `/Volumes/HaikuSrc/haiku` with its cross tools built.
