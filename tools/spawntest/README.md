# posix_spawn test

The regression test for Haiku patches 0133 and 0134.

- **0133:** `posix_spawn()` runs the path it is given and passes `argv[0]`
  through as it is. Before, a spawn with no file actions or attributes went
  through `load_image()`, whose kernel side runs `argv[0]` — so
  `posix_spawn("/some/dir/tool", {"tool"})`, what most callers write, failed
  with ENOENT (Mojo's `Process.run()` does exactly that).
- **0134:** a spawn whose program cannot be executed no longer writes the
  parent's buffered output a second time. Its child is a copying `vfork()`,
  and Haiku's `_exit()` flushes stdio.

On Prose, with Prose's own compiler:

	sh build-and-run.sh

Seven checks, each printing PASS or FAIL, then `SELFTEST PASS 7/7`. The helper
it spawns exits with 7 only when it was run and its `argv[0]` is the one it
was given. On the image before 0133, the first version of the test printed
`SELFTEST FAIL 3/7` (measured 2026-09-23): only a spawn whose `argv[0]` is the
path itself, a PATH search, and a missing program behaved.

Not tested, and not changed: Haiku's exec looks a path without a slash up on
PATH, where POSIX takes it relative to the working directory. Haiku's own
programs rely on that (RemoteDesktop runs `execl("ssh", ...)`).
