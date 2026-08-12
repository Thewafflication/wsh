# M2 Closeout — Portable Core Library

**Milestone:** M2

**Status:** Complete

**Completion date:** 2026-08-11

**Inherited Git baseline:** `b8d1e67b63923703467ae4e857d5e025b1c57e9c`

**Tested source state:** inherited baseline plus the M2 working-tree changes;
each TeX record retains the exact tested executable SHA-256, controlled test
specification SHA-256, and source-input manifest SHA-256

## Outcome

M2 now provides the portable, side-effect-free foundation required by M3 and
M4: strict decoded source with original/scalar/display positions; UTF-8/UTF-16
conversion; immutable strings, flat values, and status lists; injected
allocators and fault-atomic builders; bounded structured diagnostics;
per-context variables and export metadata; resource limits; an abstract
runtime interface; and an ordered deterministic fake runtime.

No grammar, parser, evaluator, real process, filesystem, registry, console, or
process-global mutation was introduced.

## Exit Criteria

| Criterion | Evidence | Status |
| --- | --- | --- |
| Boundary and malformed input | TC-0018, 0034--0037, 0074, 0075 | Pass |
| Allocation-failure and leak sweep | TC-0024, every ordinal through first complete graph | Pass |
| Immutable Unicode values/lists | TC-0007 and TC-0037 | Pass |
| Diagnostics and source locations | TC-0019 | Pass |
| Context ownership and variables | TC-0046, 0048, 0070 | Pass |
| Unsigned ordered statuses | TC-0049 | Pass |
| Abstract/fake runtime | TC-0070, 0074, 0075 | Pass |
| Separate-context concurrency | TC-0070 native Windows threads | Pass |
| No process-global effects | TC-0075 plus source inspection | Pass |
| Public internal contracts | Doxygen 1.14 warnings-as-errors and review record | Pass |
| Traceability and TeX evidence | WSP traceability/evidence CTest gates | Pass |

## Verification Summary

| Preset | Architecture/configuration | Build | CTest | Controlled evidence |
| --- | --- | --- | --- | --- |
| `x64-debug` | x64 Debug | Pass, MSVC `/W4 /WX` | 19/19 Pass | 14/14 Pass |
| `x64-release` | x64 Release | Pass, MSVC `/W4 /WX` | 19/19 Pass | 14/14 Pass |
| `x86-debug` | x86 Debug on WOW64 | Pass, MSVC `/W4 /WX` | 19/19 Pass | 14/14 Pass |
| ad hoc review | ARM64 Debug cross-build | Pass, MSVC `/W4 /WX` | Not run: no native ARM64 host | No execution claim |

The 57 passing CTest results include 42 controlled functional executions,
three traceability gates, three Doxygen/source-quality gates, three evidence
validators, and the inherited version/ABI smokes in each configuration.
Current evidence is isolated beneath:

- `out/build/x64-debug/test-evidence/Debug/current`;
- `out/build/x64-release/test-evidence/Release/current`; and
- `out/build/x86-debug/test-evidence/Debug/current`.

Earlier records are moved to each configuration's `archive` directory before
a rerun, preserving prior failures and passes.

## Size and Process Metrics

| Measure | Result |
| --- | ---: |
| Core public contract | 827 lines |
| Core implementation | 2,606 lines |
| Native controlled test implementation | 1,368 lines |
| Controlled TC specifications | 14 files, 374 lines |
| Requirement allocation records | 14 files, 107 lines |
| PowerShell evidence/quality runners | 16 files, 234 lines |
| M2 review findings | 11 resolved, 0 open |
| Controlled functional executions | 42 Pass |
| Total CTest executions in final matrix | 57 Pass |

The planned budget was 110,000 tokens. The execution environment did not
expose an authoritative per-phase token counter, so an exact actual cannot be
honestly claimed. A reconstruction used for planning is 97,000 tokens:
baseline/plan 12k, specify 9k, design 11k, implement 35k, review 9k,
verify/evidence 17k, and close 4k. That estimate is 13k (11.8 percent) below
budget and does not trigger the 120 percent replanning threshold. Work occurred
in one agent session; precise elapsed human/tool effort was likewise not
exposed and is recorded as unavailable rather than fabricated.

## Estimate Revision and Handoff

Observed source-map, ownership, negative-test, and evidence complexity raised
M3 and M4 by 10k tokens each and M8 and M9 by 5k each. The roadmap total is now
1,510,000 tokens. M3 shall consume decoded `wsh_source` and immutable builders,
use the fake runtime only as an inert dependency seam, and add no runtime
effect. Historical M1 evidence gaps remain release follow-up and do not alter
the M2 component result.

## Post-Closeout CI Addition

On 2026-08-12, M2 gained a GitHub-hosted `windows-2022` workflow covering the
same x64 Debug, x64 Release, and x86 Debug presets as the closeout matrix. The
workflow pins GitHub actions and Doxygen inputs, preserves controlled evidence
as run artifacts, and is documented in `docs/milestones/m02-ci-log.md`. This
automation adds continuous execution of the existing gates; it does not alter
the M2 functional scope or the closeout results above. The CI-equivalent local
matrix was rerun during publication and passed all 57 CTest executions.
