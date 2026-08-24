# M7 Review Record — Interactive Shell

**Review date:** 2026-08-24

**Scope:** M7 requirements, ADR-0010, interactive design, executable and
runtime changes, native-console driver, controlled evidence, compatibility,
and DFS controls

**Reviewer:** Current contributor; automated objective gates compensate for
the unavailable independent reviewer but do not create later release approval

## Review Inputs

- the accepted interactive-shell specification, product requirements,
  compatibility contract, DFS, M7 prompt, plan, design, and ADR-0010;
- the executable-owned session, evaluator signal/exit boundary, Windows child
  interruption boundary, history commands, and startup/profile handling;
- eight controlled specifications, fourteen allocations, native-console
  automation, source-quality checks, traceability, and WSP evidence;
- x64/x86 Debug/Release execution and ARM64 Debug/Release cross-builds.

## Checklist Result

| Area | Result | Principal observation |
| --- | --- | --- |
| Requirements | Pass | Fourteen allocations map bidirectionally to eight controlled tests |
| Ownership | Pass | Console, prompt, completion, and history state remain executable-owned |
| Editor | Pass | Input-record editing covers every accepted key and scalar-safe UTF-16 movement |
| Prompt/startup | Pass | Profiles run in accepted order; prompts remain literal wide text |
| Completion | Pass | Providers are inert, local by default, bounded, safely quoted, and deterministically ordered |
| History | Pass | Strict bounded JSONL, locking, suppression, dedupe, flushing, and same-directory replacement pass |
| Interruption | Pass | Ctrl+C/Ctrl+Break collect foreground groups before `sigint` and publish status 130 |
| Exit/jobs | Pass | Confirmation, force, consecutive EOF, identifier listing, cancellation, and `sigexit` pass |
| Recovery | Pass | Syntax, encoding, resize, completion, history, and fallback failures preserve the accepted session boundary |
| Source | Pass | Ten affected C files pass TinyCC warnings, 80-column, and Doxygen gates |
| Compatibility | Pass for M7 | x86/x64 agree; PE imports remain at the Windows 2000 ceiling; ARM64 compiles cleanly |
| Evidence | Pass | WSP validates eight current TeX records in every executed configuration |

## Interactive Defect Patterns and Resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| A stale frontend-test object retained the pre-M7 callback layout in incremental build trees | High | Require clean rebuild after public/internal structure changes; final nonbaseline presets were rebuilt cleanly |
| History parent directories were created in leaf-first order | High | Create the product directory before its `WSH` child |
| JSON strings could be encoded twice and unknown property values were not skipped generally | High | Decode once into strict UTF-8 and use a bounded recursive inert-value skipper |
| Ctrl+Z state was cleared before Enter could consume the EOF transition | High | Preserve the pending EOF chord through the immediately following Enter |
| Completion token discovery stopped at whitespace inside apostrophe quotes | High | Scan quote state across the complete pending prefix and collapse doubled apostrophes |
| The inherited ignored-Ctrl+C process state suppressed foreground delivery | High | Explicitly re-enable Ctrl+C before installing the scoped WSH handler |
| A control event could be observed again after cooked-input cancellation | High | Atomically consume the shared observation before invoking pending-input `sigint` |
| `history::clear` was immediately persisted again as the active submission | Medium | Clear history and suppress that same submission |
| Raw command bytes, rather than escaped JSONL bytes, governed the history file ceiling | Medium | Track exact serialized record sizes including header and CRLF overhead |
| A profile `exit 0` allowed later profiles to continue | Medium | Stop profile iteration on the evaluator exit state independent of numeric status |
| First completion with no common extension selected a candidate prematurely | Medium | Leave the input unchanged and activate bounded cycling |
| The first repeated Tab skipped the first sorted candidate | Medium | Seed the cycling index so forward and reverse repetition begin at the proper edge |
| Signal-scope cleanup could pop an unrelated scope after a failed push | Medium | Track successful scope ownership explicitly before rollback |
| History functions were callable before post-profile history initialization | Medium | Add an explicit history-ready state at the runtime boundary |
| Timestamp validation checked only punctuation shape | Medium | Validate calendar fields, leap years, time ranges, and the UTC suffix |
| Boundary input and invalid UTF-16 paths could terminate advanced editing | Medium | Bell or discard the affected pending input, diagnose it, and return to the prompt |
| Native input records could overrun console consumption during rapid test injection | Test | Pace state-changing records and wait on observable markers |
| The test child's private environment hid the intended PATH fixture | Test | Assign the isolated shell `$path` directly and retain the network-negative entry |

No unresolved M7 product defect remains from this review.

## Residual Boundaries

The local x86 runs execute under WOW64 and are not a Windows 2000 runtime
demonstration. The final x64 PE imports declare an OS 4.0 header and contain
only kernel32 symbols at or below the accepted Windows 2000 API ceiling, but
that inspection is not promoted to native oldest-host Pass. ARM64
Debug/Release are cross-build evidence only on this x64 host. Native Windows
2000 and ARM64 execution remain M9/release gates, as do hostile console-host
and final packaging demonstrations.

All locally feasible console interactions were automated through genuine
`INPUT_RECORD` values and console control events. No separate manual-only
current-host case remained. The unavailable oldest-host demonstration is
recorded above rather than fabricated.
