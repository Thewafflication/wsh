# M5 Closeout — Windows Execution and Composition

**Milestone:** M5

**Status:** Complete

**Completion date:** 2026-08-22

**Inherited Git baseline:** `af0c039` (`WSH 1.2.0 M4 evaluator`)

**Tested source state:** inherited baseline plus the M5 working-tree changes;
each TeX record retains tested executable, probe, WSH, specification, and
source-revision SHA-256 values

## Outcome

M5 adds a concrete Windows runtime behind M4's portable effect boundary.
Structured arguments launch through `CreateProcessW` with explicit resolution,
environment, working directory, and ordered descriptor actions. Pipelines,
capture, here documents, logical descriptors 0--9, process substitution,
background jobs, waits, timeouts, cancellation, and nested WSH list transport
now execute without an implicit command interpreter.

The front end now supports `-c`, script operands and arguments, console or
redirected standard input, direct external commands, and the evaluator's full
M4 state. Optional modern handle-list and job APIs are dynamically resolved;
the Windows 2000-compatible import surface retains the reviewed serialized
fallback in ADR-0008.

## Exit Criteria

| Criterion | Evidence | Status |
| --- | --- | --- |
| Direct launch and argument round-trip | TC-0011, TC-0013, TC-0018 | Pass |
| Deterministic safe resolution and logical directory | TC-0012, TC-0039, TC-0042 | Pass |
| Environment blocks and nested WSH lists | TC-0043, TC-0047 | Pass |
| Ordered descriptors and here documents | TC-0015 | Pass |
| Concurrent pipeline and ordered statuses | TC-0016 | Pass |
| Partial-launch and ownership cleanup | TC-0024, TC-0070 | Pass |
| Raw policy and no implicit launch route | TC-0042, TC-0045 | Pass |
| Jobs, timeout, cancellation, and fallback | TC-0050 | Pass |
| Process substitution and native capture | TC-0051, TC-0052 | Pass |
| Resource/DFS controls | TC-0074, TC-0075, PE import gate | Pass |
| Public contracts and C quality | Doxygen/source-quality gate | Pass |
| Traceability and TeX evidence | WSP traceability/evidence gates | Pass |

## Verification Summary

| Preset | Architecture/configuration | Build | CTest | M5 controlled |
| --- | --- | --- | --- | --- |
| `x64-debug` | x64 Debug | Pass, TinyCC/WCRT warnings-as-errors | 76/76 Pass | 18/18 Pass |
| `x64-release` | x64 Release | Pass, TinyCC/WCRT warnings-as-errors | 76/76 Pass | 18/18 Pass |
| `x86-debug` | x86 Debug on WOW64 | Pass, TinyCC/WCRT warnings-as-errors | 76/76 Pass | 18/18 Pass |
| `x86-release` | x86 Release on WOW64 | Pass, TinyCC/WCRT warnings-as-errors | 76/76 Pass | 18/18 Pass |
| cross-build | ARM64 Debug/Release | Pass | Not run: no native ARM64 host | No execution claim |

The 304 passing CTest executions include 72 M5 controlled runs, 176 inherited
M2--M4 controlled runs, runtime/evaluator/front-end/smoke tests, traceability,
static import inspection, and evidence validation. Current M5 evidence is
isolated under each build tree at `test-evidence/m5/<configuration>/current`.

## Size and Process Metrics

| Measure | Result |
| --- | ---: |
| Windows runtime public contract | 120 lines |
| Windows runtime implementation | 4,400 lines |
| Native runtime/probe tests | 1,316 lines |
| Controlled specifications | 18 files, 396 lines |
| Requirement allocation records | 25 files, 75 lines |
| PowerShell evidence/quality runners | 21 files, 440 lines |
| M5 review findings | 10 resolved, 0 open |
| M5 controlled functional executions | 72 Pass |
| Total final-matrix CTest executions | 304 Pass |

The accepted budget was 180,000 tokens. No authoritative per-phase token
counter was exposed, so an exact actual is not claimed. The closeout
reconstruction follows the approved forecast at 176,000 tokens: baseline/plan
20k, specify 18k, design 24k, implement 64k, review 18k, verify/evidence 28k,
and close 4k. That estimate is 4k (2.2 percent) below budget and does not
trigger the 120-percent replanning threshold. Precise elapsed human/tool effort
was not exposed and is recorded as unavailable rather than fabricated.

## Roadmap and Handoff

The roadmap remains 1,510,000 tokens after the scheduled M5 recalibration. M6
is next with its accepted 160k budget. It shall reuse the runtime's structured
arguments, environment snapshots, directory isolation, containment, timeout,
cancellation, and cleanup controls while adding the filesystem, path, text,
process, time, system, and WSP test namespaces and representative `.wsh`
build/test orchestration.
