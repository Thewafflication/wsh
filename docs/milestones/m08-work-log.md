# M8 Work Log — Embedding SDK

**Milestone:** M8 — Embedding SDK (stable ABI 1, static/shared libraries, host
examples, conformance and misuse tests).

**Inherited baseline:** `60b8061` (`Completed M7`).

**Author:** Claude (Overlord cross-project assistant), on behalf of the owner.

**Status:** Active (2026-08-30 session).

This log records the chronological execution of the M8 milestone. It
supplements, and does not replace, the accepted [M8 plan](m08-plan.md), the
controlled Git history, and the retained CTest/evidence records. Token figures
are recorded per the owner's request; the Claude Code environment does not
expose a live token counter to the assistant, so per-increment figures are
assistant-side estimates (marked `est.`) pending an environment goal-level
total.

## Work Performed

| Date or order | Phase | Activity | Output |
| --- | --- | --- | --- |
| 2026-08-30 #1 | Baseline/Plan | Draft accepted M8 plan; record M7 as complete in the status record | Commit `eb755d6` |
| 2026-08-30 #2 | Implement | Add ABI conformance and misuse test linked against the static SDK using only public headers | Commit `7f3527a` |
| 2026-08-30 #3 | Implement | Add C embedding host example built against public headers and the static library | Commit `89fd0e4` |
| 2026-08-30 #4 | Implement | Run the ABI conformance cases against the shared library for static/shared parity | Commit `170f937` |

## Verification Log

| Date | Configuration or method | Result | Evidence or failure reference |
| --- | --- | --- | --- |
| 2026-08-30 | `cmake --build --preset x64-debug` (baseline `60b8061`) | Up to date; fast unit tests 4/4 | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R abi-conformance` (post `7f3527a`) | Pass (4/4 cases) | Local CTest run |
| 2026-08-30 | Fast unit subset incl. new test (post `7f3527a`) | Pass (5/5) | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R embedding-host-example` (post `89fd0e4`) | Pass (deterministic host output) | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R abi-conformance` (post `170f937`) | Pass (2/2 static + shared) | Local CTest run |

## Decisions and Scope Changes

| Decision or change | Authority | Impact | Reference |
| --- | --- | --- | --- |
| Proceed into M8 after M7 completion | Owner request ("M8 plan looks good, work until we run out of tokens") | Begins the Embedding SDK milestone under the accepted plan | This log |

## Problems, Defects, and Recovery

| Item | Effect | Response | Status or owner |
| --- | --- | --- | --- |

## Measurements

| Measure | Value | Source or interpretation |
| --- | ---: | --- |

## Resource Usage

| Goal or work period | Tokens used | Elapsed time | Source |
| --- | ---: | ---: | --- |
| M8 Baseline/Plan (`eb755d6`) | ~30,000 est. | Not reported | Claude Code, assistant estimate |
| ABI conformance/misuse test (`7f3527a`) | ~40,000 est. | Not reported | Claude Code, assistant estimate |
| C embedding host example (`89fd0e4`) | ~22,000 est. | Not reported | Claude Code, assistant estimate |
| Shared conformance parity (`170f937`) | ~14,000 est. | Not reported | Claude Code, assistant estimate |

## Preservation and Handoff

Retained evidence is the CTest output and the Git commit history on
`origin/master`. The `wsp` submodule worktree carries unrelated user-owned
changes and remains untouched. Work remains on `master`, matching current
repository practice.
