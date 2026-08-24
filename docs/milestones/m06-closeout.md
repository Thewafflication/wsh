# M6 Closeout — Embedded Standard Library and Self-Use

**Milestone:** M6

**Status:** Complete

**Completion date:** 2026-08-23

**Inherited Git baseline:** `1187b1e` (`WSH 1.3.3 ARM64 loader fix`)

**Tested source state:** inherited baseline plus the M6 working-tree changes;
each TeX record retains tested executable, WSH script, specification, and
source-revision SHA-256 values

## Outcome

M6 adds an immutable 56-command embedded registry spanning filesystem, path,
text, process, time, system, test, and library namespaces. Query commands
return exact lists through `--into`; other records use deterministic UTF-8
output. The evaluator keeps assignment failure-atomic and crosses one typed
library request into the isolated Windows runtime.

Filesystem operations use logical absolute paths, explicit overwrite and
recursion, protected-root checks, non-traversal of reparse points, verified
cross-volume moves, bounded I/O, precise metadata, exclusive temporary
objects, and embedded SHA-256. Process wrappers reuse M5 resolution,
environment, descriptors, capture, timeout, jobs, and cancellation. Test
commands produce controlled verdict state and fail missing finalization.
`examples/build-and-test.wsh` uses the new library to build the project and run
a representative CTest selection.

## Exit Criteria

| Criterion | Evidence | Status |
| --- | --- | --- |
| All accepted namespace signatures and introspection | TC-0065 | Pass |
| Unicode and explicit encoding | TC-0037 | Pass |
| Protected roots, overwrite, recursion, and reparse boundaries | TC-0066 | Pass |
| Process run/raw/capture/parallel/wait/cancel | TC-0067 | Pass |
| Test transitions and WSP evidence | TC-0068, evidence validator | Pass |
| Context/runtime isolation | TC-0070 | Pass |
| File, capture, jobs, recursion, and recovery limits | TC-0074 | Pass |
| Cleanup and deterministic traversal | TC-0024, TC-0075 | Pass |
| Representative WSH-authored build/test flow | `m6-self-use` | Pass |
| Public contracts and C quality | Doxygen/source-quality gate | Pass |
| Traceability and TeX evidence | WSP traceability/evidence gates | Pass |

## Verification Summary

| Preset | Architecture/configuration | Build | CTest | M6 controlled |
| --- | --- | --- | --- | --- |
| `x64-debug` | x64 Debug | Pass, TinyCC/WCRT warnings-as-errors | 88/88 Pass | 9/9 Pass |
| `x64-release` | x64 Release | Pass, TinyCC/WCRT warnings-as-errors | 88/88 Pass | 9/9 Pass |
| `x86-debug` | x86 Debug on WOW64 | Pass, TinyCC/WCRT warnings-as-errors | 88/88 Pass | 9/9 Pass |
| `x86-release` | x86 Release on WOW64 | Pass, TinyCC/WCRT warnings-as-errors | 88/88 Pass | 9/9 Pass |
| cross-build | ARM64 Debug/Release | Pass | Not run: no local native ARM64 host | No local execution claim |

The 352 passing native CTest executions include 36 M6 controlled runs, four
self-use runs, inherited M2--M5 tests, unit/integration/smoke tests,
traceability, static-import inspection, and evidence validation. Current M6
evidence is isolated under each build tree at
`test-evidence/m6/<configuration>/current`.

## Size and Process Metrics

| Measure | Result |
| --- | ---: |
| Immutable registry | 56 commands |
| Windows runtime implementation after M6 | 9,615 lines |
| Portable registry and SHA-256 | 348 lines |
| Native M6 WSH test workflow | 183 lines |
| Evidence/quality runners | 2 primary runners plus 9 controlled wrappers |
| Controlled specifications | 9 files |
| Requirement allocation records | 10 files |
| Review findings | 13 resolved, 0 open |
| M6 controlled functional executions | 36 Pass |
| Total final-matrix CTest executions | 352 Pass |

The accepted budget was 160,000 tokens. No authoritative per-phase token
counter was exposed, so an exact actual is not claimed. The closeout
reconstruction follows the approved forecast at 158,000 tokens: baseline/plan
20k, specify 16k, design 20k, implement 57k, review 16k, verify/evidence 25k,
and close 4k. That estimate is 2k (1.3 percent) below budget and does not
trigger the 120-percent replanning threshold. Precise elapsed human/tool effort
was not exposed and is recorded as unavailable rather than fabricated.

## Roadmap and Handoff

M7 is next with its accepted 150k budget. It shall add console editing,
history, completion, interruption, and interactive recovery while reusing M6
time, path, process, and test services. M8 remains the next scheduled roadmap
recalibration point. Native old-Windows, ARM64, multi-volume, and hostile-host
release claims remain allocated to CI/M9 and are not inferred from this local
closeout.
