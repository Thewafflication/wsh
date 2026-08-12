# M2 Review Record — Portable Core Library

**Review date:** 2026-08-11

**Scope:** M2 requirements, design, core source, controlled tests, build,
traceability, evidence, compatibility, and applicable DFS controls

**Reviewer:** Current contributor; automated objective gates compensate for
the unavailable independent reviewer but do not create release approval

## Review Inputs

- accepted requirements and language/embedding specifications;
- ADR-0002, ADR-0005, architecture, DFS, and test strategy;
- M2 plan, design, allocation records, and 14 controlled TC specifications;
- `include/wsh/core.h`, `src/portable_core.c`, native tests, runners, and CMake;
- x64 Debug/Release and x86 Debug CTest/evidence output.

## Checklist Result

| Area | Result | Principal observation |
| --- | --- | --- |
| Requirements | Pass | 14 M2 allocations link bidirectionally to 14 controlled tests |
| Design | Pass | Ownership, limits, failure, positions, runtime substitution, and concurrency are explicit |
| Source | Pass | Portable C99 core has no OS header/effect; MSVC `/W4 /WX` and Doxygen pass |
| Unicode | Pass | Strict UTF-8/UTF-16 rejects hostile forms and preserves supplementary scalars |
| Ownership | Pass | Custom-allocator ordinal sweep returns to zero outstanding blocks |
| Tests | Pass | Boundary, state, malformed, fault, diagnostic, isolation, and concurrency techniques are represented |
| Evidence | Pass | CTest dispatches all gates and WSP validates 14 current TeX records per configuration |
| Security | Pass | Bounds precede allocation/commit; global-state observations remain unchanged |
| Compatibility | Pass for M2 matrix | x86 Debug and x64 Debug/Release execute; ARM64 Debug compiles warning-free |
| Scope | Pass | No lexer, parser, evaluator, real file, registry, console, or process implementation was added |

## Findings and Resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| Starter UTF-8 validation accepted overlong, surrogate, and out-of-range forms | High | Replaced with strict scalar decoder; hostile partitions pass |
| Starter growth used unchecked doubling/reallocation and had partial-state/leak paths | High | Replaced with checked allocate-copy-commit ownership and fault sweep |
| Starter tests omitted most M2 obligations and had no controlled specifications/evidence | High | Added 14 specifications, runners, traceability, TeX evidence, and CTest gates |
| Test CMake path resolved `tests/tests/portable_core_tests.c` on a fresh configure | Medium | Corrected target source path and proved three fresh configurations |
| Unbounded custom limits could permit sentinel overflow or oversized UTF-16 traversal | Medium | Reject unbounded sentinel limits and precheck UTF-16 unit length |
| Fake runtime consumed an expectation before output builders committed | Medium | Consume successful expectations only after output/status append succeeds |
| Doxygen gate initially could not create its nested output directory | Low | Runner creates the isolated generated-output directory; failure retained in review history |
| Doxygen then rejected recursive copy documentation and undocumented internal members | Medium | Replaced recursive annotations and documented internal members; rerun passes |
| TeX escaping could re-escape braces introduced while escaping a backslash | Medium | Replaced chained substitutions with character-wise escaping before final evidence rerun |
| Evidence initially reported the x64 PowerShell runner architecture for an x86 target | High | Record target (`Win32`/`x64`) and runner architecture separately; regenerated all evidence |
| Multi-configuration presets passed unused `CMAKE_BUILD_TYPE` values | Low | Removed the unused variables; clean x64 Release/x86 configure output passes |

No unresolved M2 defect remains. Historical M1 provisioning, oldest-OS,
PE/import, DWARF, and release-PDF gaps are recorded in the M1 work log and are
not reclassified by this review.

## Compatibility and Residual Boundaries

The core is platform-neutral and was compiled warning-free for x86, x64, and
ARM64 MSVC. Native ARM64 and legacy-Windows final-artifact execution belong to
M9's support matrix; the ARM64 result is build-only and M2 makes no ARM64 or
old-OS execution claim. The runtime
name-comparison callback is the seam for full Windows ordinal folding; the
deterministic fake proves ASCII folding only, and the concrete Windows
implementation retains the non-ASCII boundary obligation. Doxygen 1.14.0 was
used from a checksum-verified workspace tool cache because M1 did not retain a
provisioned Doxygen path.
