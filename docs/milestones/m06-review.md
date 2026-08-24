# M6 Review Record — Embedded Standard Library and Self-Use

**Review date:** 2026-08-23

**Scope:** M6 requirements, ADR-0009, standard-library design and source,
controlled tests, self-use, traceability, evidence, compatibility, and DFS
controls

**Reviewer:** Current contributor; automated objective gates compensate for
the unavailable independent reviewer but do not create later release approval

## Review Inputs

- accepted requirements, language, architecture, compatibility, and DFS;
- M6 plan, design, ADR-0009, ten allocations, and nine controlled
  specifications;
- portable registry, evaluator boundary, Windows implementation, SHA-256,
  self-use example, controlled runners, and WSP evidence;
- x64/x86 Debug/Release execution and ARM64 Debug/Release cross-builds.

## Checklist Result

| Area | Result | Principal observation |
| --- | --- | --- |
| Requirements | Pass | Ten allocations map bidirectionally to nine controlled tests |
| Design | Pass | Registry, evaluator, platform ownership, limits, and policy are explicit |
| Source | Pass | Nine affected C files pass TinyCC warnings, 80-column, and Doxygen gates |
| Registry | Pass | All 56 canonical commands have deterministic descriptors and introspection |
| Filesystem | Pass | Mutation is opt-in; roots, self-targets, subtrees, and reparse traversal are guarded |
| Path/text | Pass | Exact list results, explicit encoding, and Windows lexical rules are covered |
| Process | Pass | M5 resolution, environments, capture, timeout, raw policy, wait, and cancel are reused |
| Time/system | Pass | Stable UTC, monotonic, architecture, version, and environment-name records pass |
| Test state | Pass | Pass/fail/blocked/skipped/duplicate/incomplete transitions are distinguished |
| Isolation | Pass | Context data, children, captures, temporary objects, and test state do not escape |
| Compatibility | Pass for M6 | Native x86/x64 results agree; optional APIs are dynamic; ARM64 compiles cleanly |
| Evidence | Pass | WSP validates nine current TeX records in every executed configuration |

## Findings and Resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| Assignment-looking process options were parsed as shell assignments | High | Limit assignment syntax to the leading command prefix while preserving `local` |
| Path joining could duplicate separators and normalization could roll back across a root | High | Track the prior separator and preserve rooted rollback boundaries |
| Recursive copy and move accepted self/subtree destinations | High | Resolve both sides and reject identity or destination-within-source before effect |
| Cross-volume directory move did not verify the copied tree before deletion | High | Recursively compare topology and SHA-256 file content before removing the source |
| Recursive deletion mishandled dot enumeration and a prepared-target cleanup path | High | Skip synthetic entries correctly and release every initialized target |
| Raw file reads bypassed active command redirection | High | Route bytes through the existing descriptor-aware runtime write boundary |
| File metadata could use uninitialized state and lost subsecond precision | Medium | Zero metadata and format the full seven-digit FILETIME fraction |
| Drive-root directory extraction returned a truncated root | Medium | Preserve the complete `C:\` directory result |
| UNC parent creation began inside the share root | Medium | Start creation only after the parsed root prefix |
| Protected current-directory spellings could not use explicit authorization | Medium | Apply the spelling guard only when authorization is absent |
| Registry metadata omitted result/policy fields | Medium | Report version, signature, result mode, status, and policy deterministically |
| Metadata builders used the default allocator outside runtime fault policy | Medium | Use the runtime allocator and limits for every metadata field builder |
| Dual-stream capture could publish stdout before a stderr assignment failure | Medium | Wrap both context assignments in one rollback scope and reject duplicate destinations |

No unresolved M6 defect remains. Directory cross-volume verification is
implemented and reviewed but requires a multi-volume host for a direct effect
test. M9 retains that hostile-host/platform partition.

## Residual Boundaries

The local x86 runs execute under WOW64 and do not prove Windows 2000 runtime
compatibility. ARM64 Debug/Release are cross-build evidence only in this local
record; the native ARM64 GitHub runner remains the architecture execution gate.
M7 retains interactive interruption and history commands. M8 retains the
public host-command ABI, and M9 retains old-OS, multi-volume, hostile-race, and
final support-matrix hardening.
