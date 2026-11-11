# wsh worker instructions

Read PROJECT-WORK.md and AGENT-TASKS.md before planning.

## Current bounded goal

Repair the existing public-header/embedding ABI regression under the ready
owner contract wsh-public-header-repair-1. Preserve the existing public API and
acceptance tests; header compilation must succeed independently. Read README.md,
CMakePresets.json, the relevant public headers and tests. The documented baseline
commands are `cmake --preset x64-debug`, `cmake --build --preset x64-debug`, and
`ctest --preset x64-debug --output-on-failure`; the unchanged owner queue provides
additional authoritative checks. Do not invent a replacement ABI or weaken tests.

## Execution and completion

Follow the current owner schedule and unchanged harness acceptance contract.
Reconcile current source and existing worker/owner edits once; use exact commands
and real return codes. Missing prerequisites require a precise blocked handoff
with the next discriminating experiment. Do not cycle through broad discovery,
fabricate test output, create filler milestones or mark a partial check complete.

Preserve all pre-existing tracked/untracked files, nested dependency work and
build artifacts. Never bulk-stage, reset/clean/stash, rewrite history, kill
unrelated processes or update a dependency merely to obtain a clean tree.
Stage only explicit owned validated paths and make a local commit before reporting
done. Remote publication is permitted only where the existing owner task explicitly
requires it; verify the actual remote SHA. No force push, tags, release or install.
WFC, VB6 and autonomous Overlord implementation remain excluded.

After the assigned goal is accepted, continue only another explicitly defined
ready bounded task. With no such task, report needs-planning rather than invent
busywork. Prior handoffs are subordinate to current owner instructions and source.
