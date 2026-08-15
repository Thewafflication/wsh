# Waughtal Shell Test Strategy

**Document ID:** `WSH-TEST-STRATEGY-0001`

**Status:** Accepted

## 1. Scope

This strategy applies the WSP test requirements to every verification result
used for WSH compatibility, security, quality, or release claims. Exploratory
checks may be lighter but cannot satisfy a release requirement.

## 2. Levels

| Level | Scope |
| --- | --- |
| Unit | UTF conversion, source, lexer, parser, values, expansion, evaluator, serializers |
| Component | Context, standard-library namespaces, ABI, fake runtime, configuration parsers |
| Integration | Win32 files, registry, environment, processes, handles, pipes, jobs, console |
| Conformance | Every adopted/adapted `rc` row and WSH extension |
| System | Interactive, batch, nested WSH, build/test self-use, static/shared hosts |
| Security/negative | Malformed, boundary, resource, injection, traversal, policy, cleanup, fuzz |
| Quality | Traceability, style, docs, imports, resources, dependencies, evidence validation |

## 3. Controlled Tests

Every cited test has a reviewed `TC-NNNN` specification defining purpose,
priority, requirements, design references, environment, preconditions, data,
procedure, objective expected results, cleanup, and test-design technique.
Specifications are authoritative; reports reference rather than duplicate
them.

## 4. Release Matrix

The matrix covers x86 Windows 2000 through current claimed x86/WOW64 systems,
x64 beginning with XP/Server 2003 x64, and Windows ARM64. It distinguishes
architecture, OS/edition/service pack, Debug/Release, static executable,
static-library host, shared-library host, filesystem, console host, and
relevant compiler configuration.

Native execution is required for architecture claims. Old/intermediate/current
OS equivalence classes require analysis of API, console, filesystem, registry,
job, and loader differences. Emulation is identified and never reported as
native evidence.

Normal CI first runs source lint, traceability validation, Doxygen warnings as
errors, and MSVC static analysis. Only that gate can start the x86, x64, and
native ARM64 Debug matrix. Each Debug entry builds, runs the complete
architecture-dependent CTest suite, validates controlled evidence, and builds
an architecture-specific Debug WPM package. A semantic-version tag can start
the corresponding three Release builds only after every Debug entry passes.
Release publication requires all three Release WPM packages.

## 5. Test Design

Use equivalence partitioning and boundary analysis for encodings, lengths,
indices, statuses, paths, registry data, configuration, and resource limits;
decision tables for resolution, option precedence, policies, redirections, and
status flow; state-transition tests for parser completeness, interactive edit,
jobs, cancellation, and history; syntax testing and fuzzing for WSH/INI/history;
and use-case testing for build/test orchestration.

Argument serialization uses a generated Cartesian corpus including empty
arguments, whitespace, quotes, backslash runs, trailing backslashes, Unicode,
and multiple runtime parsers. Destructive tests run only beneath resolved
test-owned temporary roots.

## 6. Execution and Evidence

CTest dispatches all automated tests. A wrapper records test/spec revisions,
requirements, source revision, artifact hash, architecture, OS, toolchain,
dependencies, start/end, exact structured command, exit status, verdict,
stdout/stderr, and attachments. It emits machine-readable results and TeX
fragments validated by WSP tools.

Failures and original diagnostics are retained when rerun. Test outputs are
isolated by execution ID. Evidence generation or traceability failure is a test
failure.

Every Debug job publishes a per-test GitHub job summary and one downloadable
ZIP. The ZIP identifies the source revision, target and runner architectures,
toolchain, and configuration, and contains machine-readable JUnit results,
human-readable CTest diagnostics, controlled TeX evidence, build logs, exact
tested binaries, debugger symbols, file checksums, and the Debug WPM package.
Debug CI evidence is retained for 90 days. Release packages, checksums, and the
WPM `index.json` repository index are retained with the GitHub release. A
byte-identical `repository.json` alias is published for compatibility with
external automation; WPM itself consumes `index.json`.

## 7. Console and Legacy Systems

Native standard-input tests distinguish data, EOF, console events, and input
failure. Console tests use a controlled helper where feasible and documented
manual observation only when automation cannot provide reliable evidence.
Virtual-machine snapshots are reset to declared preconditions and final
portable binaries are copied in without installing undeclared runtimes.

## 8. Gates

A milestone gate requires every allocated requirement to have an applicable
passed verification, no unresolved gate-level defect, complete evidence,
validated traceability, successful documentation build where affected, and
approved risk/equivalence decisions. Blocked, Inconclusive, Not run, and Not
applicable do not pass a required gate.

Release evidence is retained with the release record and protected by hashes,
published checksums, and provenance appropriate to the artifact.
