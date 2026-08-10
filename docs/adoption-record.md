# WSP Adoption Record — Waughtal Shell

**Content type:** Adoption record

**Project:** Waughtal Shell (`wsh`)

**WSP baseline:** Immutable commit
`2198ccab08f969a789448767fe7017b774369adc`

**Submodule path:** `wsp/`

**Pinned commit:** `2198ccab08f969a789448767fe7017b774369adc`

**Status:** Accepted

**Approval:** Approved during M0 specification-baseline review

## Common Baseline

| Requirement set or practice | Applicability | Project artifact or scope |
| --- | --- | --- |
| Common requirements management | Yes | `docs/requirements/` and traceability records |
| WSP software lifecycle | Yes | `docs/project-plan.md`, `docs/planning/`, and milestone records |
| Project process | Yes | `docs/project-process.md`, milestone prompts, and release records |
| Documentation requirements | Yes | Controlled project and release documentation |
| Documentation style and identifiers | Yes | Project-authored artifacts |
| Testing requirements | Yes | `docs/test-strategy.md`, CTest specifications, evidence, and reports |

## Selected Profiles

| Profile | Selected | Project scope or rationale |
| --- | --- | --- |
| Personal process | No | Deferred until individual planning records are useful |
| Security/DFS | Yes | Shell parses untrusted text and launches processes |
| C source style | Yes | All project-owned `.c` and `.h` files |
| PowerShell style | Yes | Project-owned Windows automation |
| CMake style | Yes | All project-owned CMake files and presets |
| Windows version resources | Yes | Every shipped project-owned executable and DLL |
| Windows code signing and Defender | Yes | Windows release artifacts |
| Common tools | Yes | Traceability, source quality, evidence, reports, and documentation |

## Initial Tailoring Decisions

Detailed row-by-row WSP dispositions were accepted as part of the M0 baseline
review. No adopted requirement is silently omitted.

The accepted project baseline includes the complete WSH 1.0 specification,
architecture, Design for Security, project process, test strategy, M0--M10
milestone plan, and token-budgeted prompt library. These artifacts are the
approved baseline for subsequent implementation milestones.

### WSP-DOC toolchain — KerTeX selection

- **Disposition:** Accepted tailoring, with M0 KerTeX compatibility validation completed and recorded
- **Rationale:** The project requires KerTeX installed through WPM, while the
  current WSP documentation guidance describes a MiKTeX/PDFLaTeX pipeline.
- **Impact:** WSP TeX inputs or automation may depend on MiKTeX-specific
  behavior and fail to produce the required release PDF.
- **Compensating control:** Compile representative WSP test and release inputs
  with the pinned KerTeX engine; retain logs; propose portable fixes upstream
  or document an approved equivalent pipeline.
- **Owner:** Project maintainer
- **Completion condition:** M0 documentation-toolchain spike passes and the
  exact engine/version is recorded.
- **Approval:** Approved in M0 baseline review

### WSP-TEST-0018 — cv2pdb output

- **Disposition:** Applicable without weakening
- **Rationale:** PDB output is useful for Windows debugging but does not replace
  WSP's GDB-compatible DWARF requirement.
- **Impact:** Debug builds retain both symbol forms, increasing artifact size.
- **Compensating control:** Not required.
- **Owner:** Project maintainer
- **Completion condition:** N/A
- **Approval:** Approved in M0 baseline review

## Baseline History

| Date | WSP baseline | Project change | Summary |
| --- | --- | --- | --- |
| 2026-08-04 | `2198ccab08f969a789448767fe7017b774369adc` | Initial plan | Proposed WSP adoption |
