# M2 Continuous-Integration Work Log

**Work date:** 2026-08-12

**Scope:** Add a GitHub-hosted runner for the completed M2 portable-core
verification matrix

## Outcome

The `M2 Portable Core` GitHub Actions workflow now runs the same three native
Windows configurations used for M2 closeout:

| Matrix job | GitHub runner | CMake/CTest preset |
| --- | --- | --- |
| x64 Debug | `windows-2022` | `x64-debug` |
| x64 Release | `windows-2022` | `x64-release` |
| x86 Debug | `windows-2022` under WOW64 | `x86-debug` |

Each job checks out the pinned `wsp` submodule, configures and builds with
Visual Studio 2022, and executes all 19 CTest entries. That includes the 14
controlled tests plus traceability, source-quality, evidence-validation, and
inherited version/ABI smoke gates.

## Reproducibility and Security Controls

- The runner label is explicitly `windows-2022`, avoiding drift through the
  mutable `windows-latest` label.
- Workflow permissions are read-only and checkout credentials are not
  persisted.
- GitHub-authored actions are pinned to full commit identifiers.
- Doxygen 1.14.0 is downloaded from its official distribution URL and rejected
  unless its SHA-256 is
  `3843742c604e145dab26f74ebd386af0656bc2feb6f834c12c1abb7b3c019d8b`.
- Matrix jobs do not fail fast, so one architecture failure does not suppress
  results from the others.
- Controlled TeX evidence, Doxygen output, and CTest logs are uploaded for 14
  days, including after a test failure when those files exist.
- Concurrency cancellation prevents obsolete runs on the same ref from
  consuming runner time.

## Trigger and Publication Record

The workflow runs for pushes to `master`, pull requests, and manual dispatch.
Its source is `.github/workflows/m2.yml`. Local validation checks the workflow
structure, action pins, dependency checksum, matrix-to-preset mapping, and the
same CMake/CTest commands before publication. The authoritative hosted result
is the GitHub Actions run associated with the publishing commit; it is not
pre-claimed by this log.

## Local Verification

Before publication, actionlint 1.7.12 accepted the workflow with no finding.
Its downloaded Windows x64 archive was verified against SHA-256
`6e7241b51e6817ea6a047693d8e6fed13b31819c9a0dd6c5a726e1592d22f6e9`.
The workflow's configure, build, and CTest sequence was then rerun locally for
all matrix presets:

| Preset | Build | CTest |
| --- | --- | --- |
| `x64-debug` | Pass | 19/19 Pass |
| `x64-release` | Pass | 19/19 Pass |
| `x86-debug` | Pass | 19/19 Pass |

The rerun totals 57 passing CTest executions. Because the validation host has
Visual Studio 2022 and Visual Studio 2026 installed side by side, local CMake
configuration explicitly selected the VS 2022 instance. The pinned
`windows-2022` hosted image does not have that local dual-install ambiguity.
