# WSH project work

**Content type:** Project work index
**Updated:** 2026-11-11, owner-directed scheduled-work setup.

## Goal and scope

Restore independently usable public embedding headers and preserve the existing
embedding ABI. The assigned ready contract is `wsh-public-header-repair-1`;
it remains authoritative and unchanged. Its scope is the unmatched guard/type
include regression in include/wsh/api.h and reconciliation against the existing
wsh_embedding_abi_version API. Do not create a replacement API.

Read [AGENT-TASKS.md](AGENT-TASKS.md), [AGENTS.md](AGENTS.md) and
[README.md](README.md). The original owner checks remain the acceptance gates,
including protected baseline hashes for tests and build definitions.

## Prerequisites and evidence

TinyCC/WCRT, CMake, Ninja and the required Windows resource tools must be
available. Use the existing documented x64-debug preset and compile the actual
header-hygiene fixture; do not patch the test or presets to pass. No fresh
compiler/runtime success is claimed by this documentation change.

Inspect actual branch/HEAD and existing changes once before edits. Preserve
owner work and configured origin/master publication intent from the queue.

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
