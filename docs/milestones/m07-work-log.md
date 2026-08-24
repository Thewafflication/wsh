# M7 Work Log — Interactive Shell

**Date:** 2026-08-24

**Result:** Complete; publication commit follows this record

## Work Performed

1. Baselined the M7 prompt, WSP lifecycle/test/security/C guidance, M6 handoff,
   accepted interactive specification, requirements, compatibility, and DFS.
2. Planned the 150k work package, accepted ADR-0010, allocated fourteen
   requirements, and specified eight controlled tests before implementation.
3. Added an executable-owned Unicode console session with literal prompts,
   multiline parsing, the complete editing-key contract, resize/redraw, and a
   visible cooked-input fallback.
4. Added inert deterministic completion for built-ins, functions, library
   commands, variables, local files, scripts, and PATH commands with safe
   quotation, finite top-N selection, and network-enumeration guards.
5. Added bounded strict JSONL history, path-derived locking, adjacent dedupe,
   suppression/list/clear commands, exact serialized-byte accounting, flush,
   and atomic same-directory replacement.
6. Added profile selection/order, evaluator exit and signal functions,
   foreground Ctrl+C/Ctrl+Break collection, background-job exit/EOF policy,
   and one orderly `sigexit` boundary.
7. Built a native console driver that allocates a genuine console, injects
   input records and control events, observes process cleanup, isolates
   history/profile/PATH fixtures, and produces controlled TeX evidence.
8. Resolved eighteen review findings, ran the native and cross-build matrix,
   updated DFS/usage/roadmap documentation, stamped version 1.5.0, and advanced
   immediate work to M8 and M9.

## Verification Log

- x64 Debug: build Pass; CTest 98/98 Pass.
- x64 Release: clean build Pass; CTest 98/98 Pass.
- x86 Debug on WOW64: clean build Pass; CTest 98/98 Pass.
- x86 Release on WOW64: clean build Pass; CTest 98/98 Pass.
- ARM64 Debug and Release: clean cross-build Pass; not executed on this x64
  host.
- M7 controlled native-console evidence: 32/32 Pass across four executed
  configurations.
- M7 traceability and evidence validation: Pass in every executed
  configuration.
- M7 Doxygen/source quality: Pass for ten affected C files using the checked
  local Doxygen tool.
- Windows 2000 PE/static-import gate: Pass; native Windows 2000 execution was
  unavailable and is not claimed.

## Preservation and Follow-up

The stakeholder directed completion in the existing `master` worktree; no
milestone branch was created. The pre-existing dirty `wsp` submodule worktree
was not modified or staged. Generated `out/` trees remain local evidence and
are ignored. M8 is the next implementation milestone. Native Windows 2000,
native ARM64, hostile console hosts, and final release/package evidence remain
M9/release work.
