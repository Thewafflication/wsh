# M7 Closeout — Interactive Shell

**Milestone:** M7

**Status:** Complete

**Completion date:** 2026-08-24

**Inherited Git baseline:** `07c209d` (`Complete M6 embedded standard library`)

**Tested source state:** inherited baseline plus the M7 working-tree changes;
each TeX record retains tested executable, driver, specification, and
source-revision SHA-256 values

## Outcome

M7 replaces the basic console reader with an executable-owned Unicode session.
It preserves parser semantics while adding literal profile-defined prompts,
multiline editing, the complete accepted key contract, scalar-safe movement,
resize/redraw, recoverable invalid input, and a reported cooked-input fallback.

Completion combines functions, built-ins, the 59-command embedded registry,
variables, local scripts/files, and local PATH executables without evaluating
providers. It uses deterministic ordinal ordering, bounded top-N retention,
longest-common-prefix extension, reversible cycling, safe apostrophe quoting,
and explicit network selection.

History loads only after profiles, parses bounded inert UTF-8 JSONL, validates
UTC timestamps, ignores unknown properties, coalesces malformed-record
warnings, suppresses adjacent duplicates, and exposes `history::suppress`,
`history::list`, and `history::clear`. A retained path-derived mutex prevents
silent concurrent writers; flushed same-directory temporary files replace the
prior history atomically.

The console handler publishes only an atomic request. Foreground waits cancel
and collect the tracked group before `sigint`; pending Ctrl+C clears unpublished
input with status 130. Interactive exit lists live background identifiers and
confirms, while force exit and consecutive EOF cancel and collect them.
Orderly shutdown invokes `sigexit` once.

## Exit Criteria

| Criterion | Evidence | Status |
| --- | --- | --- |
| Mode, startup/profile order, prompt, all editor keys, Unicode, resize, fallback | TC-0053 | Pass |
| Deterministic inert completion and safe quotation | TC-0055 | Pass |
| Bounded inert JSONL history, corruption, locking, replacement, commands | TC-0054, TC-0074 | Pass |
| Pending and foreground Ctrl+C/Ctrl+Break, group cleanup, status 130, `sigint` | TC-0020, TC-0056 | Pass |
| Background confirmation, force exit, consecutive EOF, `sigexit` | TC-0056 | Pass |
| Interactive error recovery and batch equivalence | TC-0057 | Pass |
| Disclosure, network, event-storm, and job-cleanup DFS controls | TC-0075 | Pass |
| Public/internal contracts and C quality | Doxygen/source-quality gate | Pass |
| Traceability and TeX evidence | WSP traceability/evidence gates | Pass |
| Current representative native console | Four x64/x86 Debug/Release native runs | Pass |
| Oldest representative API surface | PE/import inspection | Pass (static only) |
| Oldest representative native execution | Windows 2000 host | Not run; retained M9/release gate |

## Verification Summary

| Preset | Architecture/configuration | Build | CTest | M7 controlled |
| --- | --- | --- | --- | --- |
| `x64-debug` | x64 Debug | Pass, TinyCC/WCRT warnings-as-errors | 98/98 Pass | 8/8 Pass |
| `x64-release` | x64 Release | Pass, clean TinyCC/WCRT build | 98/98 Pass | 8/8 Pass |
| `x86-debug` | x86 Debug on WOW64 | Pass, clean TinyCC/WCRT build | 98/98 Pass | 8/8 Pass |
| `x86-release` | x86 Release on WOW64 | Pass, clean TinyCC/WCRT build | 98/98 Pass | 8/8 Pass |
| cross-build | ARM64 Debug/Release | Pass, clean | Not run: no local native ARM64 host | No local execution claim |

The 392 passing native CTest executions include 32 M7 controlled runs,
inherited M2--M6 controlled tests, unit/integration/smoke tests, self-use,
traceability, PE/static-import inspection, and evidence validation. Current M7
evidence is isolated under each build tree at
`test-evidence/m7/<configuration>/current`.

## Size and Process Metrics

| Measure | Result |
| --- | ---: |
| Executable-owned interactive implementation | 3,631 lines |
| Native-console controlled driver | 1,505 lines |
| Evidence/quality runners | 2 primary runners plus 8 controlled wrappers |
| Controlled specifications | 8 files |
| Requirement allocation records | 14 files |
| Review findings | 18 resolved, 0 open |
| M7 controlled functional executions | 32 Pass |
| Total final-matrix CTest executions | 392 Pass |

The accepted budget was 150,000 tokens. No authoritative per-phase token
counter was exposed, so the prompt's requested exact actual cannot be claimed.
The closeout reconstruction therefore retains the approved 150,000-token
forecast: baseline/plan 18k, specify 15k, design 20k, implement 55k, review
15k, verify/evidence 24k, and close 3k. This equals the accepted budget and
does not trigger the 120-percent replanning threshold. Precise elapsed
human/tool effort was not exposed and is recorded as unavailable rather than
fabricated.

## Roadmap and Handoff

M8 is next and shall use ADR-0010's executable-only boundary while adding the
public hosting ABI without importing console/history behavior into library
hosts. M9 remains the compatibility, historical-evidence, hostile-host,
packaging, and release-readiness gate. Native Windows 2000 and ARM64 execution
are not inferred from local static/cross-build evidence.
