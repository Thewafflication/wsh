# M3 Work Log — Lexer, Parser, and Immutable AST

**Date:** 2026-08-12

**Result:** Complete; committed publication follows this record

## Work Performed

1. Baselined the M3 prompt, WSP lifecycle/test/security/C guidance, accepted
   requirements, language specification, architecture, ADR-0002, and `rc`
   compatibility record.
2. Planned the 150k work package, produced the parser design, allocated 21
   requirements, and specified 15 controlled tests before implementation.
3. Added the public lexer/parser/AST contract and portable C99 implementation
   over decoded source, with immutable ownership, source spans, status/
   diagnostic partitioning, ceilings, and deterministic AST formatting.
4. Added conformance, malformed-input, Windows-path, substitution, ownership,
   source-equivalence, limit, and deterministic fuzz tests.
5. Integrated traceability, Doxygen/source quality, controlled TeX evidence,
   and evidence validation into CTest while preserving the M2 gates.
6. Resolved seven review findings, including allocation-failure dereference and
   non-ASCII formatter defects, and reran the complete matrix.
7. Updated DFS, compatibility verification, project handoff, milestone status,
   review, and closeout records.

## Verification Log

- x64 Debug: build Pass; CTest 37/37 Pass.
- x64 Release: build Pass; CTest 37/37 Pass.
- x86 Debug: build Pass; CTest 37/37 Pass.
- M3 controlled evidence: 45/45 Pass across the matrix.
- M3 generated corpus: 4,096 bounded inputs parsed twice per controlled run
  with identical result/status/diagnostic or complete formatted AST.
- M2 regression evidence: 42/42 controlled runs Pass across the matrix.

## CI Maintenance Logged with M3

The GitHub Actions Windows workflow now discovers the newest official Doxygen
Windows x64 archive from the upstream download page, downloads it, prints the
installed `doxygen --version`, and rejects a mismatch between the selected and
reported versions. This replaces the stale archive/checksum pair that failed
the first hosted x64 run. The workflow also runs the M3 gates and retains M2/
M3 evidence, Doxygen output, executables, DLLs, and symbols per matrix job.

## Preservation and Follow-up

The pre-existing dirty `wsp` submodule working tree was not edited or staged.
Generated `out/` verification trees remain local and are not publication
inputs. M4 is the next implementation milestone; M1's historical release-
evidence gaps remain separate release follow-up.
