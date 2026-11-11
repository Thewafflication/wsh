# WSH agent tasks

**Content type:** Agent work order
**Updated:** 2026-11-11.

## WSH-001 — Public embedding header repair

State: ready under the existing owner contract wsh-public-header-repair-1.
Requirement: public headers compile independently without changing the documented
embedding ABI. Deliverable: repair include/wsh/api.h guard and required includes,
reconcile existing ABI declarations and prove the real embedding tests still pass.

First action: invoke `check_acceptance` for the assigned owner task. Use its exact
failed diagnostics to guide the narrow repair. Inspect tests/abi_header_hygiene.c,
include/wsh/api.h and the existing ABI definition rather than restarting a survey.
Keep all existing test/build hashes unchanged, including CMakePresets.json.

Acceptance commands from project root:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug --no-tests=error --output-on-failure
```

The unchanged harness also compiles tests/abi_header_hygiene.c with its exact
configured TinyCC path and include arguments. Run `check_acceptance` again after
edits; all configured checks must pass, not just the three commands above.
Record baseline/final diagnostics, test totals, compiler identity and immutable
output paths. Missing tool prerequisites are blocked, never skipped success.
Commit only owned repairs; the existing contract requires push to origin/master
and independent live-SHA verification before full completion. Do not push these
planning documents or unrelated owner changes merely to satisfy that goal.

## Next work

No second ready task is assigned here. After accepted header repair, request the
next bounded requirement rather than expanding the ABI or refreshing notes.

## Worker completion rules

Preserve every pre-existing owner edit, untracked file, nested dependency and
build artifact. Never broad-stage, clean/reset/stash, rewrite history, weaken
acceptance, manufacture command output or substitute a different toolchain to
claim success. WFC/VB6 and autonomous Overlord implementation remain excluded.
Commit only explicit owned validated changes locally before reporting done.
Publication is allowed only where the unchanged owner task already requires it;
verify the live remote SHA. No force push, tag, release or installation.
If prerequisites fail, retain the command, exit code, actual diagnostic and one
next experiment. A blocked task is unfinished. Once the assigned goal is accepted,
continue only another explicitly ready bounded task, otherwise yield needs-planning.
