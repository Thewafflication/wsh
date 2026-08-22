# M4 Closeout — Evaluator and Language Semantics

**Milestone:** M4

**Status:** Complete

**Completion date:** 2026-08-22

**Inherited Git baseline:** `02d96a4` (`WSH 1.1.0 interactive input`)

**Tested source state:** inherited baseline plus the M4 working-tree changes;
each TeX record retains the tested executable, specification, and source-input
manifest SHA-256 values

## Outcome

M4 now evaluates complete immutable M3 ASTs against an injected deterministic
runtime. It implements ordered flat lists, empty identity, assignments,
dynamic/local/command/function/source/subshell scopes, explicit export,
subscripts, count/flatten, carets, status logic, deterministic patterns and
globbing, command substitution, functions, structured control, transfer,
source, eval, diagnostics, ownership, and hostile-input limits.

The standard-input front end retains evaluator state across complete commands
and adapts `echo` writes to its selected Unicode/byte output path. It does not
create child processes. Real Windows execution and composition remain M5.

## Exit Criteria

| Criterion | Evidence | Status |
| --- | --- | --- |
| Value and expansion semantics | TC-0007, TC-0009, TC-0037 | Pass |
| Status and conditional semantics | TC-0014, TC-0049 | Pass |
| Functions and control flow | TC-0017 | Pass |
| Source/eval and scope | TC-0043, TC-0046, TC-0048 | Pass |
| Failed-substitution no-effect behavior | TC-0023, TC-0052 | Pass |
| Diagnostics and source location | TC-0019 | Pass |
| Allocation ownership | TC-0024, every ordinal through success | Pass |
| Hostile semantic limits | TC-0074 | Pass |
| DFS isolation/effect mediation | TC-0075 | Pass |
| Public contracts and C quality | Doxygen/source-quality gate | Pass |
| Traceability and TeX evidence | WSP traceability/evidence gates | Pass |

## Verification Summary

| Preset | Architecture/configuration | Build | CTest | M4 evidence |
| --- | --- | --- | --- | --- |
| `x64-debug` | x64 Debug | Pass, TinyCC/WCRT warnings-as-errors | 55/55 Pass | 15/15 Pass |
| `x64-release` | x64 Release | Pass, TinyCC/WCRT warnings-as-errors | 55/55 Pass | 15/15 Pass |
| `x86-debug` | x86 Debug on WOW64 | Pass, TinyCC/WCRT warnings-as-errors | 55/55 Pass | 15/15 Pass |
| `x86-release` | x86 Release on WOW64 | Pass, TinyCC/WCRT warnings-as-errors | 55/55 Pass | 15/15 Pass |
| cross-build | ARM64 Debug/Release | Pass | Not run: no native ARM64 host | No execution claim |

The 220 passing CTest executions include 60 M4 controlled runs, 116 inherited
M2/M3 controlled runs, evaluator/front-end/smoke tests, traceability, and
evidence validation. Current M4 evidence is isolated under each build tree at
`test-evidence/m4/<configuration>/current`.

## Size and Process Metrics

| Measure | Result |
| --- | ---: |
| Evaluator public contract | 71 lines |
| Evaluator implementation | 3,390 lines |
| Native M4 test implementation | 764 lines |
| Controlled specifications | 15 files, 340 lines |
| Requirement allocation records | 15 files, 59 lines |
| PowerShell evidence/quality runners | 17 files, 219 lines |
| M4 review findings | 9 resolved, 0 open |
| M4 controlled functional executions | 60 Pass |
| Total final-matrix CTest executions | 220 Pass |

The accepted budget was 160,000 tokens. No authoritative per-phase token
counter was exposed, so an exact actual is not claimed. The closeout
reconstruction is 150,000 tokens: baseline/plan 15k, specify 16k, design 21k,
implement 58k, review 14k, verify/evidence 23k, and close 3k. That estimate is
10k (6.25 percent) below budget and does not trigger the 120-percent
replanning threshold. Precise elapsed human/tool effort was not exposed and is
recorded as unavailable rather than fabricated.

## Roadmap and Handoff

The roadmap remains 1,510,000 tokens. M5 retains its accepted 180k budget and
is the next milestone; the planned recalibration occurs after M5. M5 shall
consume M4's structured arguments/statuses and abstract effect requests,
preserve no-effect preparation failures and scope isolation, and keep all
Win32 path, environment, handle, pipe, process, job, and cancellation behavior
outside the portable evaluator.
