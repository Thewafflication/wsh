# M3 Review Record — Lexer, Parser, and Immutable AST

**Review date:** 2026-08-12

**Scope:** M3 requirements, design, lexer/parser source, immutable AST,
controlled tests, traceability, evidence, compatibility, and DFS controls

**Reviewer:** Current contributor; objective automated gates compensate for
the unavailable independent reviewer but do not create release approval

## Review Inputs

- accepted requirements and language/compatibility specifications;
- ADR-0002, architecture, DFS, test strategy, and C style;
- M3 plan, design, 21 allocation records, and 15 controlled specifications;
- `include/wsh/parser.h`, `src/parser.c`, native tests, runners, and CMake;
- x64 Debug/Release and x86 Debug CTest/evidence results.

## Checklist Result

| Area | Result | Principal observation |
| --- | --- | --- |
| Requirements | Pass | 21 allocations link bidirectionally to 15 controlled tests |
| Design | Pass | Lexer, AST, ownership, statuses, diagnostics, limits, and no-effect boundary are explicit |
| Source | Pass | Portable C99 passes MSVC `/W4 /WX`, 80-column, and Doxygen gates |
| Grammar | Pass | Normative productions and precedence have positive and negative coverage |
| Source fidelity | Pass | Quotes, here bodies, paths, spans, Unicode, and line endings retain defined meaning |
| Ownership | Pass | Every injected allocation failure returns to zero outstanding blocks |
| Malformed input | Pass | Error and Incomplete publish diagnostics and no partial root |
| Security | Pass | Tokens, nodes, recursion, and diagnostics are bounded before publication |
| Effects | Pass | Parser interface has no runtime or operating-system capability |
| Evidence | Pass | WSP validates 15 current TeX records in each configuration |
| Compatibility | Pass for M3 | x86 Debug and x64 Debug/Release execute the complete suite |
| Scope | Pass | No evaluator, process, file, registry, or console implementation was added |

## Findings and Resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| Failed simple-command node allocation could be dereferenced | High | Added the missing failure check; ordinal fault injection passes with zero outstanding blocks |
| Representative flatten test initially used the wrong prefix character | High | Corrected it to `$"name`; token/AST tests pass |
| Formatter appended one byte at a time for non-ASCII text | High | Append complete UTF-8 scalar sequences; source-equivalence tests pass |
| One parser-test source line exceeded 80 columns | Low | Wrapped the assertion; source-quality and Doxygen rerun pass |
| M2/M3 wrappers shared one trace-discovery directory | Medium | Segregated them under `tests/m2` and `tests/m3`; both gates pass |
| First hosted M2 workflow used a stale Doxygen checksum | Medium | Resolve the current official GitHub release asset and report/verify its version |
| Official HTML release discovery failed on the hosted image | Medium | Use Doxygen's GitHub latest-release API and exact Windows asset metadata |
| Doxygen 1.17 version output includes a source hash suffix | Medium | Parse and compare the leading semantic version while logging the complete output |

No unresolved M3 defect remains. Dynamic latest-Doxygen selection intentionally
trades byte-for-byte tool reproducibility for avoiding stale upstream archive
checksums; the exact executable version remains visible in every Actions log.
Historical M1 release-evidence gaps retain their existing disposition.

## Residual Boundaries

The AST describes redirection, pipeline, substitution, and control-flow syntax
but does not implement their runtime semantics. Native ARM64 execution and
legacy-Windows final-artifact claims remain M9 work. The generated fuzz corpus
is bounded deterministic robustness evidence, not a proof over all strings;
later hardening can add sanitizers and corpora without changing the grammar.
