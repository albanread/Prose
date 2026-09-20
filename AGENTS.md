# For agents writing Prose software in this repository

Prose is this repo's Haiku (arm64) distribution, and more Prose software
will be written here. The discipline that keeps it working:

- **Build on the host, test on the guest.** Cross-compiler:
  `/Volumes/HaikuSrc/haiku/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++`
  (read-only). Never run jam in, or write to, the Prose build tree
  (`/Volumes/HaikuSrc/haiku`) or its images.
- **Our machine is `prosewriter/vm/run.sh`** — QEMU/HVF, headless, booting
  only the APFS clone `prosewriter/vm/prose-dev.image`. The `prose-qemu-dev`
  skill is the harness manual (`guest.sh run/put/get/launch`, `qmp.py
  shot/key/hmp`); the `prose-api-db` skill indexes the Be/Haiku API and docs.
- **Scripted GUI tests need `hey <signature> do Activate` before typing** —
  background-launched windows on this guest are never activated, and tablet
  clicks do not activate windows. Keyboard works after Activate; treat mouse
  clicks as unreliable for testing (modal-alert buttons are Tab/Enter).
- **Kill guest apps by numeric team id** — `ps` puts the id in the column
  after the command name; `killall` does not exist on this image.
- **API facts that have bitten us:** `BString::IFindLast` returns `int32`
  (B_ERROR when absent — never compare it to NULL); `BFile::Write` returns
  bytes written (a positive return is NOT B_OK); `B_DELETE` is 0x7F and
  passes naive `>= 32` printable checks. Verify against the indexed headers
  or `prosewriter/app/sysroot` before trusting memory.
- **Every app ships a `--selftest`** printing PASS/FAIL per check and a final
  `SELFTEST PASS n/n`; every bug fix lands with its regression test. Build
  with `-Wall -Wextra -Wpointer-arith` and keep the build warning-clean.
- **Docs discipline:** sprints, exit criteria and session findings belong in
  the project's `docs/plan.md` (see ProseWriter's for the pattern, including
  the honest "still owed" lists); keep the README status table current.
  `prosewriter/docs/review-2026-09-20.md` is the model post-mortem.
- **Committed conventions:** atomic saves (write-tmp → Sync → rename, never
  B_ERASE_FILE a user document); failed saves are never silent; check every
  status_t against B_OK, not against "not negative".
