# M5 Review Record — Windows Execution and Composition

**Review date:** 2026-08-22

**Scope:** M5 requirements, design, native runtime/evaluator/front-end source,
controlled tests, traceability, evidence, compatibility, and DFS controls

**Reviewer:** Current contributor; automated objective gates compensate for
the unavailable independent reviewer but do not create later release approval

## Review Inputs

- accepted language, Windows adaptations, requirements, architecture, and DFS;
- M5 plan, design, ADR-0008, 25 allocations, and 18 controlled specifications;
- launch-plan contracts, evaluator, Windows runtime, front end, probes, test
  runners, traceability, evidence, and CMake dispatch;
- x64/x86 Debug/Release execution and ARM64 Debug/Release cross-builds.

## Checklist Result

| Area | Result | Principal observation |
| --- | --- | --- |
| Requirements | Pass | 25 allocations map bidirectionally to 18 controlled tests |
| Design | Pass | Resolution, ownership, descriptors, containment, and fallbacks are explicit |
| Source | Pass | C99 passes TinyCC warnings-as-errors, 80-column, and Doxygen gates |
| Resolution | Pass | Exact, `.exe`, `.com`, safe-path, logical-directory, and drive-relative cases pass |
| Arguments | Pass | Wide startup parsing and deterministic Microsoft-CRT serialization round-trip Unicode |
| Environment | Pass | Imports, scalar/list adaptation, sorting, collisions, and nested WSH lists pass |
| Descriptors | Pass | Ordered redirection, here documents, descriptors 0--9, and inheritance cleanup pass |
| Composition | Pass | Concurrent pipelines, ordered statuses, capture, and process substitution pass |
| Lifecycle | Pass | Background wait, timeout, cancellation, shutdown, and partial failures collect children |
| Security | Pass | Raw launch is fail-closed; implicit interpreter/association imports are absent |
| Compatibility | Pass for M5 | Native x86/x64 results agree; modern APIs are dynamic; ARM64 compiles cleanly |
| Evidence | Pass | WSP validates 18 current TeX records in every executed configuration |

## Findings and Resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| WCRT narrow startup corrupted non-ASCII command-line input | High | Parse `GetCommandLineW` directly and keep UTF-8 conversion explicit |
| Capture errors could be collapsed into an ordinary child status | High | Preserve encoding and resource failures as runtime failures |
| Background launch in a fresh shell required a nonexistent `$status` | High | Define the preserved initial foreground status as `(0)` |
| Process-substitution providers could outlive a waited background group | High | Register providers with group wait/cancel/shutdown collection |
| Descriptor 3 did not initially cross a nested WSH process | High | Add the bounded private descriptor map and controlled child fixture |
| Here documents initially lacked quoted-marker and expansion behavior | Medium | Expand unquoted bodies and retain quoted bodies literally |
| Shell `echo` could not participate in a native pipeline | Medium | Add a private exact-byte child stage for evaluator-owned `echo` |
| Runtime write translated newlines despite byte-redirection semantics | Medium | Write exact bytes and leave console rendering to the front end |
| Drive-relative resolution used only the runtime's current drive | Medium | Capture per-drive directories and resolve `C:relative` explicitly |
| PE32 import inspection sign-extended its ordinal mask | Low | Construct the unsigned 32-bit mask from hexadecimal text |

No unresolved M5 defect remains. ADR-0008 retains one approved boundary: on
Windows 2000, the serialized fallback can control WSH-created handles but an
embedding host must not expose unrelated inheritable handles during launch.

## Residual Boundaries

The current x86 runs execute under WOW64 and do not prove Windows 2000 runtime
compatibility. ARM64 Debug/Release are cross-build evidence only. M7 retains
console editing, history, completion, resize handling, and console-event
policy. M9 retains native OS/architecture hardening, hostile-host validation,
and the final support claim.
