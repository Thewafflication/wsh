# M4 Review Record — Evaluator and Language Semantics

**Review date:** 2026-08-22

**Scope:** M4 requirements, design, evaluator/core/front-end source,
controlled tests, traceability, evidence, compatibility, and DFS controls

**Reviewer:** Current contributor; automated objective gates compensate for
the unavailable independent reviewer but do not create release approval

## Review Inputs

- accepted language, compatibility, requirements, architecture, and DFS;
- M4 plan, design, 15 allocations, and 15 controlled specifications;
- evaluator/core public contracts, implementation, front-end adapter, tests,
  evidence runners, and CMake dispatch;
- x64/x86 Debug/Release execution and ARM64 Debug/Release cross-builds.

## Checklist Result

| Area | Result | Principal observation |
| --- | --- | --- |
| Requirements | Pass | 15 allocations link bidirectionally to 15 controlled tests |
| Design | Pass | State, scope, status, expansion, effects, ownership, and limits are explicit |
| Source | Pass | Portable C99 passes TinyCC warnings-as-errors, 80-column, and Doxygen gates |
| Values/expansion | Pass | Empty identity, lists, carets, variables, glob sorting, and substitution are deterministic |
| Control | Pass | Functions, blocks, branches, loops, switch, and transfer are bounded and scoped |
| State | Pass | Private/export identity, locals, command-local state, source, and subshell isolation pass |
| Ownership | Pass | Every injected evaluator allocation failure returns to zero outstanding blocks |
| Security | Pass | Failed expansion suppresses effects; M5-owned nodes fail explicitly |
| Effects | Pass | Evaluator imports no OS API and all allowed effects cross the runtime interface |
| Evidence | Pass | WSP validates 15 current TeX records in every executed configuration |
| Compatibility | Pass for M4 | x86/x64 Debug/Release results agree; ARM64 compiles cleanly |
| Scope | Pass | No process creation, filesystem enumeration, handles, pipes, jobs, or registry behavior was added |

## Findings and Resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| Partial AST-copy allocation could destroy a null child array | High | Guarded partial child teardown; the complete ordinal fault sweep passes |
| Failed nested scope creation could pop the caller's scope | High | Track successful pushes explicitly before restoration |
| Initial write request did not retain rendered `echo -n` output | Medium | Pass one exact rendered string to the abstract write adapter |
| Redirected-input integration still expected parse-only behavior | Medium | Updated it to require evaluated `echo` output and zero diagnostics |
| Glob fake request compared null arguments with an empty value | Low | Use subject-only expectation for an operation with no argument value |
| Leading-period matching used aligned byte indexes across components | Medium | Enforce the rule at recursive component entry instead |
| Initial command-local test expanded a prefix assignment too early | Low | Verify visibility inside a called function and restoration afterward |
| New source exceeded one line-length and documentation gate | Low | Wrapped the line and documented every extracted private symbol |
| Prompt estimate conflicted with the accepted M3 recalibration | Low | Use the controlling 160k M4 budget and record the supersession |

No unresolved M4 defect remains. The portable ASCII case-fold used when no
Windows comparator exists is an intentional deterministic pre-M5 fallback;
M5 owns native ordinal comparison and filesystem enumeration.

## Residual Boundaries

External launch requests from a standalone evaluator require a host runtime;
the executable currently adapts only logical stdout writes. Process search,
environment construction, files, redirection, pipelines, jobs, process
substitution, and child control remain M5. Interactive editing beyond the
native line-mode baseline, history, completion, and interruption policy remain
M7.
