# M1 Work Log — Toolchain and Repository Skeleton

**Milestone:** M1

**Inherited baseline:** `3ea772b2d271a80ffe8337782b9e095be0a9cf31`

**Recorded completion baseline:** `b8d1e67b63923703467ae4e857d5e025b1c57e9c`

**Execution period:** 2026-08-10, as recorded by Git and CTest

**Log status:** Retrospective record created during M2

## Scope and Outcome

Commit `b8d1e67` records `M1 complete M2 started`. It added the C99 CMake
project, x64 Debug/Release and x86 Debug presets, executable and static/shared
library skeletons, version and ABI smoke interfaces, two CTest smoke tests,
and the initial portable-core work. The same commit retained a generated x64
Debug build tree and binaries as historical execution evidence.

The retained CTest log records two passing tests on 2026-08-10:
`version-smoke` and `print-abi-smoke`. The checked-in
`LastTestsFailed.log` still lists `version-smoke`, inconsistent with the final
passing log, so it is retained as an unresolved evidence anomaly rather than
silently discarded.

## WSP Phase Record

| Phase | Retrospective evidence | Result |
| --- | --- | --- |
| Baseline/plan | M0 baseline and accepted M1 definition | Recorded |
| Specify | Smoke behavior is represented in CTest regular-expression gates | Partial; no controlled TC files found |
| Design | ADR-0003 through ADR-0005 describe toolchain and library shape | Recorded |
| Implement | CMake project, three targets, presets, headers, and smoke program | Recorded |
| Review | No separate review record was found | Not captured |
| Verify | Retained x64 Debug CTest log: 2/2 Pass | Partial configuration evidence |
| Close | Completion commit exists | Recorded with gaps below |

## Metrics

| Measure | Planned | Actual retained record |
| --- | ---: | ---: |
| Token budget | 80,000 | Not captured during original execution |
| Changed files in completion commit | Not specified | 146 |
| Insertions in completion commit | Not specified | 9,693 |
| CTest results retained | Required matrix | 2 Pass on x64 Debug |
| Presets added | x86/x64 initial hosts | x64 Debug, x64 Release, x86 Debug |
| Production/library targets | Executable, static, shared | 3 plus portable-core helper |
| Recorded defects/findings | Not specified | 1 evidence anomaly identified retrospectively |

Most changed files are generated Visual Studio build outputs. Original token
actuals and elapsed effort were not retained, so estimation variance cannot be
calculated honestly.

## Exit-Gate Gap Assessment

The historical commit calls M1 complete, but the retained repository does not
substantiate every exit criterion in the M1 prompt:

- no clean-provision record or pinned WPM dependency inventory is present;
- no retained x86 configure/build/test execution was found;
- no PE import, version-resource, Windows 2000 compatibility, DWARF/GDB, or
  cv2pdb evidence was found;
- no controlled negative evidence test, traceability result, TeX evidence, or
  KerTeX release-PDF record was found; and
- generated build outputs were committed under `out/`, contrary to the later
  workflow rule that generated outputs stay outside controlled source.

These are historical record or verification gaps. M2 does not rewrite the M1
commit or claim those gates passed. They remain release-readiness follow-up,
while the available CMake/CTest skeleton is sufficient to dispatch M2's
portable component verification on the current host.

## Process Improvement

M2 adds controlled test specifications, automated traceability, isolated TeX
evidence, explicit review/closeout records, and failure-preserving reruns. A
future toolchain-hardening work item should close the M1 matrix and dependency
evidence gaps before release claims depend on them.
