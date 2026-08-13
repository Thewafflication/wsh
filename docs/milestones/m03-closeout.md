# M3 Closeout — Lexer, Parser, and Immutable AST

**Milestone:** M3

**Status:** Complete

**Completion date:** 2026-08-12

**Inherited Git baseline:** `53a6d95` (`M2 complete`)

**Tested source state:** inherited baseline plus the M3 working-tree changes;
each TeX record retains the tested executable SHA-256, controlled
specification SHA-256, and source-input manifest SHA-256

## Outcome

M3 now provides a bounded, side-effect-free lexer and recursive-descent parser
over decoded `wsh_source`, immutable owned token and AST results, complete/
incomplete/error classification, stable diagnostic codes and spans, and a
deterministic escaped S-expression inspection format. It covers every accepted
grammar production, including quotation, free carets, substitutions,
redirections, here documents, pipelines, blocks, functions, and control flow.

No evaluation, substitution capture, named pipe, process, filesystem,
environment, registry, console, or process-global mutation was introduced.

## Exit Criteria

| Criterion | Evidence | Status |
| --- | --- | --- |
| Grammar conformance | TC-0008--0017, 0038, 0051, 0052 | Pass |
| Malformed/incomplete partition | TC-0023 | Pass |
| Immutable AST and ownership | TC-0010, TC-0082 | Pass |
| Source/encoding equivalence | TC-0038, TC-0083 | Pass |
| Parser resource limits | TC-0084 | Pass |
| Deterministic generated corpus | TC-0085, 4,096 inputs parsed twice | Pass |
| Runtime effects impossible | API/source inspection, TC-0010, 0051, 0052 | Pass |
| Public contracts and C quality | Doxygen/source-quality gate | Pass |
| Traceability and TeX evidence | WSP traceability/evidence gates | Pass |

## Verification Summary

| Configuration | Build | CTest | M3 controlled evidence |
| --- | --- | --- | --- |
| x64 Debug | Pass, MSVC `/W4 /WX` | 37/37 Pass | 15/15 Pass |
| x64 Release | Pass, MSVC `/W4 /WX` | 37/37 Pass | 15/15 Pass |
| x86 Debug on WOW64 | Pass, MSVC `/W4 /WX` | 37/37 Pass | 15/15 Pass |

The 111 passing CTest executions include 45 M3 controlled functional runs,
the inherited 42 M2 controlled runs, six smokes, and 18 traceability,
Doxygen/source-quality, and evidence gates. Current generated M3 evidence is
isolated under each build tree at
`test-evidence/m3/<configuration>/current`.

## Size and Process Metrics

| Measure | Result |
| --- | ---: |
| Parser public contract | 416 lines |
| Parser implementation | 3,398 lines |
| Native M3 test implementation | 742 lines |
| Controlled TC specifications | 15 files, 348 lines |
| Requirement allocation records | 21 files, 130 lines |
| PowerShell evidence/quality runners | 17 files, 237 lines |
| M3 review findings | 9 resolved, 0 open |
| M3 controlled functional executions | 45 Pass |
| Total final-matrix CTest executions | 111 Pass |

The planned budget was 150,000 tokens. No authoritative per-phase token
counter was exposed, so an exact actual is not claimed. The closeout
reconstruction is 132,000 tokens: baseline/plan 15k, specify 14k, design 16k,
implement 48k, review 12k, verify/evidence 24k, and close 3k. That estimate is
18k (12 percent) below budget and does not trigger the 120 percent replanning
threshold. Precise elapsed human/tool effort was not exposed and is recorded
as unavailable rather than fabricated.

## Recalibration and Handoff

Observed complexity matched the M2-adjusted M3 budget. The roadmap remains at
1,510,000 tokens, with M4 at 160,000. M4 shall consume only complete immutable
ASTs, retain the syntax diagnostics/status partition, evaluate behind the
abstract fake runtime, and avoid all real Windows effects. Parser changes made
to support evaluation still require grammar, compatibility, allocation,
negative-test, and evidence review.
