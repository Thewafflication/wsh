# M2 Continuous-Integration Work Log

**Work date:** 2026-08-12

**Scope:** Add a GitHub-hosted runner for M2, then extend the same Windows
matrix to M3 and correct current-Doxygen bootstrap behavior

## Outcome

The `WSH Windows CI` GitHub Actions workflow now runs the same three native
Windows configurations used for M2 closeout and the completed M3 matrix:

| Matrix job | GitHub runner | CMake/CTest preset |
| --- | --- | --- |
| x64 Debug | `windows-2022` | `x64-debug` |
| x64 Release | `windows-2022` | `x64-release` |
| x86 Debug | `windows-2022` under WOW64 | `x86-debug` |

Each job checks out the pinned `wsp` submodule, configures and builds with
Visual Studio 2022, and executes all 37 CTest entries. That includes 14 M2 and
15 M3 controlled tests plus milestone-scoped traceability, source-quality,
evidence-validation, and inherited version/ABI smoke gates.

## Reproducibility and Security Controls

- The runner label is explicitly `windows-2022`, avoiding drift through the
  mutable `windows-latest` label.
- Workflow permissions are read-only and checkout credentials are not
  persisted.
- GitHub-authored actions are pinned to full commit identifiers.
- The current Doxygen Windows x64 release is resolved from the official Doxygen
  download page. The workflow prints the selected and executable-reported
  versions and rejects a mismatch.
- Matrix jobs do not fail fast, so one architecture failure does not suppress
  results from the others.
- Controlled TeX evidence, Doxygen output, CTest logs, native binaries, DLLs,
  and symbols are uploaded for 14 days, including after a test failure when
  those files exist.
- Concurrency cancellation prevents obsolete runs on the same ref from
  consuming runner time.

## Trigger and Publication Record

The workflow runs for pushes to `master`, pull requests, and manual dispatch.
Its source is `.github/workflows/m2.yml`. Local validation checks the workflow
structure, action pins, release-resolution logic, matrix-to-preset mapping,
and the same CMake/CTest commands before publication. The authoritative result
is the GitHub Actions run associated with the publishing commit; it is not
pre-claimed by this log.

## Local Verification

Before publication, actionlint 1.7.12 accepted the workflow with no finding.
The official resolver selected Doxygen 1.17.0; the downloaded executable
reported `1.17.0 (65a43c0aba45cc23b3ca11b6b5334d4eea931726)`, and both M2 and
M3 warnings-as-errors documentation gates passed with it. The workflow's
configure, build, and CTest sequence was rerun locally for all matrix
configurations after the current-release correction and M3 addition:

| Preset | Build | CTest |
| --- | --- | --- |
| `x64-debug` | Pass | 37/37 Pass |
| `x64-release` | Pass | 37/37 Pass |
| `x86-debug` | Pass | 37/37 Pass |

The rerun totals 111 passing CTest executions. Because the validation host has
Visual Studio 2022 and Visual Studio 2026 installed side by side, local CMake
configuration explicitly selected the VS 2022 instance. The pinned
`windows-2022` hosted image does not have that local dual-install ambiguity.

## CI Bootstrap Correction

The first hosted execution exposed that the Doxygen 1.14.0 URL no longer
returned the archive whose historical checksum had been recorded, so the
bootstrap stopped before CMake configuration. On 2026-08-12 the workflow was
changed to resolve the current Windows x64 release from Doxygen's official
download page, download that named release from the official files endpoint,
print `doxygen --version`, and verify that the executable reports the selected
version. This intentionally follows the requested current-release policy; the
tradeoff is that Doxygen is no longer byte-reproducible across future CI runs.
