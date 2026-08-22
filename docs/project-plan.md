# Waughtal Shell Project Plan

**Document ID:** `WSH-PLAN-0001`

**Status:** Accepted

**Date:** 2026-08-04

## Purpose

Waughtal Shell (`wsh`) will be a Windows-native command interpreter inspired
by Tom Duff's Plan 9 `rc` shell. It will preserve the small, regular language
model that distinguishes `rc`, while defining explicit Windows behavior for
processes, paths, environment variables, consoles, and executable discovery.

The first supported implementation will be written in portable C99 and built
with CMake. CTest will execute all automated tests. Each controlled test run
will produce LaTeX evidence suitable for inclusion in a WSP release report.

## Product Principles

1. Preserve `rc` concepts rather than copying Bourne or PowerShell semantics.
2. Be a native Windows program; do not require POSIX emulation at runtime.
3. Keep parsing, evaluation, and Windows integration as separate components.
4. Make compatibility and intentional deviations observable and tested.
5. Keep the dependency and build baseline reproducible through WPM.
6. Prefer a small correct language over broad, partially compatible syntax.

## Scope

### Version 0.1 — Language Core

- batch execution with `wsh script.wsh` and `wsh -c command`;
- lexer and parser for words, apostrophe quoting, comments, command lists,
  assignments, variable expansion, list construction, and `^` concatenation;
- simple external commands and the `exit`, `cd`, and `echo` built-ins;
- `$status`, `$*`, `$#name`, `$name(n)`, and `$name` list behavior;
- sequential commands and `&&`, `||`, and `!` status composition;
- input, output, append, and standard-error redirection;
- deterministic diagnostics with source locations; and
- x86, x64, and ARM64 build configurations, with execution claims made only
  where tests run on the corresponding architecture.

### Version 0.2 — Structured Shell Language

- blocks, `if`, `if not`, `while`, `for`, `switch`, and `~` matching;
- shell functions and command-local variables;
- linear pipelines and background execution;
- command substitution and configurable `ifs` splitting;
- globbing with `rc`'s unmatched-pattern behavior; and
- script inclusion and initial profile loading.

### Version 0.3 — Interactive Shell

- interactive prompt and multiline parsing;
- console interrupt handling and child-process cancellation;
- command history and a minimal line editor;
- `whatis`, `shift`, `wait`, and job inspection; and
- documented Windows script association and invocation behavior.

### Version 1.0 — Supported Compatibility Baseline

- here documents;
- ordered descriptor duplication and closure where Windows can represent it;
- a documented decision on generalized pipelines and process substitution;
- stable language and Windows-compatibility reference;
- signed Windows release packages and complete release evidence; and
- conformance suite covering every supported `rc`-derived feature and every
  documented deviation.

### Initial Exclusions

- Plan 9 namespaces, `/env`, notes, and `rfork` semantics;
- byte-for-byte compatibility with Plan 9 executables or environment storage;
- POSIX job-control process groups;
- Unix shebang execution by the Windows kernel; and
- a requirement to execute existing Unix `rc` scripts unchanged.

Exclusions may be revisited only through requirements change analysis and an
ADR. Where useful, `wsh` may offer a Windows analogue under a distinct,
documented contract.

## Stakeholders and Responsibilities

| Responsibility | Initial owner |
| --- | --- |
| Product scope and release approval | Project maintainer |
| Requirements and architecture | Project maintainer |
| Implementation and source review | Project contributors |
| Verification and evidence approval | Project maintainer or designated reviewer |
| Dependency and release security | Project maintainer |
| WSP process improvement | Project maintainer |

Material release findings should receive review by someone other than their
author when another reviewer is available. A single-maintainer exception will
be recorded in the release-readiness record.

## Technical Baseline

- Language: portable C99 with compiler extensions disabled.
- Runtime: pinned WCRT, statically linked into official release artifacts.
- Platform API: Win32, isolated below `src/platform/windows/`.
- Build: CMake with checked-in configure, build, and test presets.
- Test runner: CTest, with small C unit-test executables and process-level
  PowerShell acceptance runners where Windows orchestration is required.
- Compiler/runtime: TinyCC/WCRT is the single supported build toolchain.
- Dependencies provisioned by WPM: `wcrt`, `kertex`, and `cv2pdb`.
- Product artifacts: portable `wsh.exe`, static `wshlib`, alternative
  `wshlib.dll`, public ABI header, and required symbols/evidence.
- Debug symbols: DWARF is retained for WSP/GDB evidence; `cv2pdb` generates an
  additional PDB artifact for Windows debugging.
- Documentation: controlled Markdown remains authoritative. Generated test
  evidence is TeX; release documentation is assembled under the WSP process.

The TinyCC/WCRT build baseline is pinned to TinyCC
`0.9.28-rc.1444+9a4be30f` and WCRT `1.0.0`. Build scripts install those exact
versions from their WPM release repositories. KerTeX and cv2pdb will be pinned
when their documentation and symbol gates are enabled:

```powershell
wpm install tinycc --arch <x86|x64|arm64> --version 0.9.28-rc.1444+9a4be30f
wpm install wcrt --arch any --version 1.0.0
wpm install kertex --arch <architecture> --version <pinned-version>
wpm install cv2pdb --arch <architecture> --version <pinned-version>
```

## Proposed Repository Layout

```text
CMakeLists.txt
CMakePresets.json
cmake/
docs/
  adoption-record.md
  architecture.md
  design-for-security.md
  project-process.md
  test-strategy.md
  requirements/
  specification/
  planning/
  adr-*.md
include/wsh/
src/
  main.c
  language/
  runtime/
  platform/windows/
tests/
  unit/
  integration/
  conformance/
  specifications/
  evidence/
tools/
wsp/
```

Generated files will be written beneath `out/`, `tmp/`, or `output/`, never
under `wsp/` or the controlled source directories.

## Architecture Work Breakdown

1. **Source and diagnostics:** input abstraction, UTF decoding policy, source
   spans, and diagnostic rendering.
2. **Lexer:** tokens, apostrophe quoting, comments, operators, and free-caret
   insertion rules.
3. **Parser:** explicit grammar producing an immutable abstract syntax tree.
4. **Values and scope:** owned lists of strings, variable tables, local scope,
   functions, and status values.
5. **Expansion:** variables, subscripts, counts, concatenation, globbing, and
   command substitution.
6. **Evaluator:** commands, control flow, functions, pipelines, and status
   propagation.
7. **Windows process layer:** executable search, argument quoting, environment
   blocks, handles, pipes, process creation, waiting, and cancellation.
8. **Interactive layer:** console input, prompt, history, and interrupts.

Each layer will expose narrow C interfaces with explicit ownership and error
contracts. Parser tests will not launch processes; process tests will not
depend on the interactive console.

## Verification Strategy

CTest will be the sole top-level test dispatcher. Labels will separate:

- `unit`: lexer, parser, list, scope, expansion, and status behavior;
- `integration`: process creation, environment, pipes, redirections, and
  filesystem behavior;
- `conformance`: examples and semantic cases derived from the `rc` paper and
  manual, with Windows deviations stated in the test specification;
- `negative`: malformed syntax, unavailable executables, invalid handles,
  resource exhaustion, and interrupted execution; and
- `quality`: traceability, C source documentation, line length, Doxygen,
  dependency inventory, and generated-evidence validation.

Every controlled test will have a `TC-NNNN` specification and link backward to
one or more `WSH-REQ-NNNN` requirements. A CTest fixture or wrapper will record
the source revision, preset, architecture, compiler, dependency versions,
command, timestamps, exit code, and captured output. It will generate a TeX
evidence fragment per test and a configuration-level TeX summary. Failure to
generate complete evidence is itself a test failure.

The evidence generator will use WSP's `New-TestReport.ps1`,
`Test-TestEvidence.ps1`, and `Test-Traceability.ps1` where their contracts fit.
KerTeX will compile the assembled TeX report in CI. A short toolchain spike
must prove that the WSP TeX inputs and selected KerTeX engine are compatible;
otherwise an approved tailoring decision or a preamble change upstream in WSP
is required before release claims are made.

## Milestones and Exit Criteria

The controlled [milestone plan](planning/milestones.md) divides WSH 1.0 into
M0 through M10. Each milestone repeats Baseline, Plan, Specify, Design,
Implement, Review, Verify, and Close activities under WSP. The plan includes
scope, dependencies, exit gates, prompt-library entry, and explicit token
budget for each milestone.

No production implementation begins until M0 accepts the complete proposed
requirements, language, CLI, configuration, registry, platform, standard-
library, embedding, architecture, and security baseline.

## Principal Risks

| Risk | Planned control |
| --- | --- |
| Windows command-line quoting cannot represent arbitrary argument lists for legacy programs | Define `CreateProcessW` serialization rules; test with an argument-echo helper; document irreducible application-specific parsing differences |
| Windows environment variables cannot natively preserve `rc` lists | Keep lists internally; define a reversible WSH encoding only for child `wsh`; use a documented scalar join policy for other children |
| Windows handles do not equal Plan 9 file descriptors | Model logical descriptors internally; support standard handles first; gate arbitrary-descriptor claims on a feasibility spike |
| Console interrupts and background children leak processes | Use job objects and explicit control-event tests; verify cleanup after abnormal exit |
| Unicode is corrupted at API boundaries | Use UTF-8 internally and wide Win32 APIs; define invalid-input behavior and round-trip tests |
| Parser ambiguity or accidental shell injection | Use a formal grammar and direct `CreateProcessW`; never invoke `cmd.exe` implicitly |
| KerTeX does not compile the WSP-oriented TeX pipeline | Complete an M0 compatibility spike and record the selected engine and required tailoring |
| TinyCC diagnostics or code generation miss defects | Maintain compiler-conformance probes, warnings-as-errors, controlled tests, and cross-architecture/native execution gates |
| Feature growth prevents a correct usable core | Enforce milestone scope and require ADR/requirements impact analysis for additions |

## Release Gate

A release candidate is acceptable only when:

- all requirements in its baseline have an approved disposition;
- all required CTest tests pass on each claimed configuration;
- TeX evidence is complete, traceable, and compiles successfully;
- C source and CMake quality gates pass;
- exact WPM dependency versions and digests are retained;
- Debug symbols, including required DWARF evidence, are retained;
- release documentation, checksums, provenance, signing decisions, and known
  limitations satisfy the adopted WSP baseline; and
- no unresolved critical defect or unapproved security risk remains.

## Immediate Next Work

1. Use the completed M4 evaluator and abstract-runtime request contract as the
   sole semantic foundation for M5 Windows process composition.
2. Implement M5 resolution, environment, process, descriptor, pipeline, job,
   and process-substitution adapters without moving Win32 behavior into the
   portable evaluator.
3. Retain the M0/M1 retrospective logs and M2 evidence. Close the historical
   M1 dependency, x86-oldest-host, PE/import, DWARF, and release-PDF evidence
   gaps before a later release claim depends on them.
4. Preserve M4's no-effect preparation failures, structured argument lists,
   status ordering, subshell isolation, scope restoration, and evaluator
   limits at every M5 operating-system boundary.

## References

- Tom Duff, *Rc — The Plan 9 Shell*.
- Plan 9 from User Space, `rc(1)` manual.
- WSP software lifecycle, requirements management, C style, CMake style,
  testing strategy, documentation requirements, and security profiles.
