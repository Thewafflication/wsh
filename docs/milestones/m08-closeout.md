# M8 Closeout — Embedding SDK

**Milestone:** M8

**Status:** Complete

**Completion date:** 2026-08-30

**Inherited Git baseline:** `60b8061` (`Completed M7`)

**Tested source state:** the inherited baseline plus the M8 working-tree
changes; each controlled case retains an execution-evidence record with the
tested specification, shared-library, and source-revision SHA-256 values.

## Outcome

M8 freezes the WSH embedding surface as a stable ABI 1 SDK. The static library
and the shared library present the same 103-function surface — source decoding,
immutable strings/values/status lists, the isolated context, the abstract
runtime and fake runtime, the evaluator, the parser, and the version
accessors — drawn from `core.h`, `evaluator.h`, `parser.h`, and `wsh.h`. The
Windows runtime remains a host-side static library and is deliberately outside
the portable embedding DLL.

The shared library exports exactly the approved ABI 1 manifest through `WSH_API`
markers; runtime, CRT, and internal symbols stay private, verified in both
directions. A runtime `wsh_embedding_abi_version()` accessor exposes the ABI for
negotiation. A C host and a Python `ctypes` FFI host embed the library through
only the public surface, and the installed SDK (headers, import library, DLL) is
proven consumable by an external host.

## Exit Criteria

| Criterion | Result |
| --- | --- |
| ABI and misuse tests pass | Met — static and shared conformance and misuse cases pass |
| Static, shared, and executable conformance match | Met — one suite runs against the static archive and the shared DLL; the executable links the static surface |
| Exported symbols verified | Met — the DLL exports exactly the 103-symbol manifest, enforced for missing and extra symbols |
| Public examples use no internal type | Met — header hygiene and installed-SDK consumption compile against the public/installed headers only |
| Second-language host | Met — the Python FFI host drives the shared ABI (bitness-gated) |
| Version resource verified | Met — `wsh.exe` and `wshlib.dll` contain consistent file/product version 1.5.0 resources, inspected by `TC-0077` |

## Verification Summary

The corrected M8 controlled suite maps canonical product requirements
`WSH-REQ-0069`–`0073` and six derived allocations to seven specifications and
implementations (`TC-0100`–`TC-0106`), gated by `m8-traceability`. The
controlled `m8-TC-*` cases execute the built artifacts and each writes an
execution-evidence record; `m8-evidence` validates seven passing records. The
full embedding suite passes on `x64-debug`, and the
export surface and C conformance also pass on `x86-debug` with clean,
undecorated symbols.

Defect D-0801 (the shared library omitted the ABI because it linked the static
core archive) was found by the FFI host and closed by compiling the core
sources directly into the DLL.

## Size and Process Metrics

- One embedding ABI 1 surface of 103 functions with an explicit export marker.
- Nine built embedding artifacts consolidated into seven controlled cases plus
  traceability and evidence gates.
- DLL export set reduced from 286 (export-all) to exactly 103.
- WSP phases executed: Baseline/Plan, Specify, Design, Implement, Review,
  Verify/evidence, and Close, recorded in `m08-work-log.md`.

## Roadmap and Handoff

One environment boundary carries forward to the M9 matrix and release work:

- **ARM64 execution.** The embedding libraries and suite cross-compile for
  ARM64 but were not executed on ARM64 hardware from the x64 review host. Run
  the M8 suite, including an architecture-matched FFI interpreter, on native or
  emulated ARM64.
M9 (compatibility and security closure) is the next milestone. The post-M8
roadmap recalibration is recorded in the milestone plan.

## Post-closeout correction

A later traceability audit found that the original gate covered only derived
allocations `0100`–`0105`, omitted canonical requirements `0069`–`0073`, lacked
host-command registration, and did not compare `wsh.exe` with both library
linkages. The correction added canonical allocations, synchronous host-command
ABI and C/Python coverage, `TC-0106` cross-artifact language/library
equivalence, and the previously absent PE resources. M8 is complete only with
this correction applied.
