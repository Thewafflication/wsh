# M9 Work Log — Compatibility and Security Closure

**Milestone:** M9 — full OS/architecture matrix, old-API fallbacks, path and
hostile-input hardening, fuzzing, child containment, dependency assessment, and
DFS residual-risk closure.

**Inherited baseline:** `650c8de` (`Close out M8 embedding SDK milestone`).

**Author:** Claude (Overlord cross-project assistant), on behalf of the owner.

**Status:** Active (2026-08-30 session).

This log records the chronological execution of the M9 milestone. It supplements
the accepted [M9 plan](m09-plan.md), the [Design for Security](../design-for-security.md),
the controlled Git history, and the retained CTest/evidence records. Token
figures are recorded per the owner's request; the Claude Code environment does
not expose a live token counter to the assistant, so per-increment figures are
assistant-side estimates (marked `est.`) pending an environment total.

Many M9 deliverables — native Windows 2000, native ARM64, and hostile-host
execution — require environments other than the x64 review host. Those are
recorded as explicit gaps with the evidence produced where the environment
exists, and are not inferred locally.

## Work Performed

| Date or order | Phase | Activity | Output |
| --- | --- | --- | --- |
| 2026-08-30 #1 | Baseline/Plan | Draft accepted M9 plan grounded in the M9 scope and WSH-DFS-0001 | Commit `812d5f9` |
| 2026-08-30 #2 | Implement | Add the input fuzzing smoke harness over the decode/lex/parse surface; verify recursion/length bounds | Commit `b2fa22a` |
| 2026-08-30 #3 | Implement | Add resource-exhaustion bound checks for source, string, list, variable, diagnostic, token, and AST-node ceilings | Commit `12b080d` |

## Verification Log

| Date | Configuration or method | Result | Evidence or failure reference |
| --- | --- | --- | --- |
| 2026-08-30 | `ctest --preset x64-debug -R fuzz-smoke` (post `b2fa22a`) | Pass: 30,029 inputs decoded/lexed/parsed without fault; 100k-deep nesting bounded | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R resource-bounds` (post `12b080d`) | Pass (7/7 ceilings enforced) | Local CTest run |

## Decisions and Scope Changes

| Decision or change | Authority | Impact | Reference |
| --- | --- | --- | --- |
| Begin M9 after M8 completion | Owner request ("continue") | Starts compatibility and security closure under the accepted plan | This log |
| Verify the parser depth/length bounds with pathological fuzz inputs rather than only random ones | WSH-DFS-0001 availability control (unbounded parsing) | Confirms 100k-deep nesting reaches a defined status instead of a stack overflow | Commit `b2fa22a` |

## Problems, Defects, and Recovery

| Item | Effect | Response | Status or owner |
| --- | --- | --- | --- |
| The token-ceiling case first assumed `wsh_lex` returns `WSH_OK` with an error status | One assertion failed; the lexer instead rejects an over-limit source with `WSH_ERR_RESOURCE` | Reclassified the outcome to accept either bounding mechanism (rejected or non-complete); reran to green | Closed (`12b080d`) |

## Measurements

| Measure | Value | Source or interpretation |
| --- | ---: | --- |

## Resource Usage

| Goal or work period | Tokens used | Elapsed time | Source |
| --- | ---: | ---: | --- |
| M9 Baseline/Plan | ~26,000 est. | Not reported | Claude Code, assistant estimate |
| Input fuzzing smoke harness (`b2fa22a`) | ~30,000 est. | Not reported | Claude Code, assistant estimate |
| Resource-exhaustion bound checks (`12b080d`) | ~30,000 est. | Not reported | Claude Code, assistant estimate |

## Preservation and Handoff

Retained evidence is the CTest output and the Git commit history on
`origin/master`. The `wsp` submodule worktree carries unrelated user-owned
changes and remains untouched. Work remains on `master`.
