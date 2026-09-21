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
- **Before believing a guest failure, prove the guest binary is the host
  build** — compare sizes (`stat -f%z` host vs `ls -l` guest). A ProseDraw
  session lost an hour to "the fix didn't work" against an hour-stale
  deployed copy plus three zombie teams blocked in modal alerts; kill the
  zombies, redeploy, retest. Smoke scripts deploy-verify in step 1.
- **A crashed app's team is debugger-suspended and ignores `kill -9`** —
  the debug_server holds it in a crash alert that waits forever headless,
  and one zombie poisons the roster for every later `hey` (scripting goes
  to dead instances and everything "fails"). If teams survive a kill
  sweep, `qmp.py` a `system_reset` (disk persists, RAM clears) and sync
  after deploys: `guest.sh put` verifies against the guest's page cache,
  so an unflushed deploy is lost to a reset.
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
