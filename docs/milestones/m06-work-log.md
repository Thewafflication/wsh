# M6 Work Log — Embedded Standard Library and Self-Use

**Date:** 2026-08-23

**Result:** Complete; publication commit follows this record

## Work Performed

1. Baselined the M6 prompt, WSP lifecycle/test/security/C guidance, M5 handoff,
   accepted requirements, standard-library specification, compatibility, and
   DFS.
2. Planned the 160k work package, accepted ADR-0009, allocated ten
   requirements, and specified nine controlled tests before implementation.
3. Added a portable immutable registry and evaluator dispatch with common
   `--into`, deterministic records, library introspection, and failure-atomic
   query assignment.
4. Implemented filesystem, path, text, process, time, system, and test
   namespaces inside the isolated Windows runtime, including bounded encoding,
   protected destructive paths, capture, parallel nested WSH, and SHA-256.
5. Added controlled WSH cases, PowerShell evidence partitions, reparse and
   resource-limit fixtures, traceability, source quality, and the representative
   `examples/build-and-test.wsh` self-use flow.
6. Resolved thirteen review findings, ran the native matrix and ARM64
   cross-builds, updated architecture/compatibility/DFS/usage documentation,
   stamped version 1.4.0, and advanced the roadmap to M7.

## Verification Log

- x64 Debug: build Pass; CTest 88/88 Pass.
- x64 Release: build Pass; CTest 88/88 Pass.
- x86 Debug on WOW64: build Pass; CTest 88/88 Pass.
- x86 Release on WOW64: build Pass; CTest 88/88 Pass.
- ARM64 Debug and Release: cross-build Pass; not executed on this x64 host.
- M6 controlled evidence: 36/36 Pass across four executed configurations.
- M6 traceability, evidence validation, and self-use gates: Pass in every
  executed configuration.
- M6 Doxygen/source quality: Pass using the checked local Doxygen tool.

## Preservation and Follow-up

The stakeholder directed all M6 work to `master`; no milestone branch was
created. The pre-existing dirty `wsp` submodule worktree was not modified or
staged. Generated `out/` trees remain local evidence and are ignored. M7 is
the next implementation milestone. Native Windows 2000, native ARM64 release
evidence, multi-volume testing, hostile races, and historical M1 evidence gaps
remain later compatibility/release work and are not claimed by this local
record.
