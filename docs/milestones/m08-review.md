# M8 Review Record — Embedding SDK

**Review date:** 2026-08-30

**Scope:** M8 plan, ADR-0005, the M8 design, the shared/static library
composition and export control, the ABI 1 header surface, the conformance,
misuse, hygiene, export-manifest, host-example, FFI, and installed-SDK tests,
and cross-architecture verification

**Reviewer:** Current contributor; automated objective gates compensate for the
unavailable independent reviewer but do not create later release approval

## Review Inputs

- the accepted M8 plan and the M8 design document;
- ADR-0005 (context-based library and stable embedding API);
- the four public headers (`core.h`, `evaluator.h`, `parser.h`, `wsh.h`) and
  the export marker `api.h`;
- the shared/static library and executable build definitions;
- the ABI 1 export manifest and the export-verification, conformance, misuse,
  hygiene, host, FFI, and installed-SDK tests;
- local `x64-debug` and `x86-debug` build and CTest results.

## Checklist Result

| Area | Result | Principal observation |
| --- | --- | --- |
| ABI scope | Pass | ABI 1 is the 101 portable `wsh_*` functions; the Windows runtime is excluded from the portable DLL |
| Library composition | Pass | Shared and static libraries present the same surface; the shared library compiles the core sources directly |
| Export control | Pass | `WSH_API` marks the surface; the DLL exports exactly the manifest (100→ from 286), enforced both directions |
| Conformance | Pass | One suite runs against the static archive and the shared DLL with matching results |
| Misuse | Pass | Null owners/outputs, absent variables, out-of-range indices, and empty status queries return defined errors |
| Header hygiene | Pass | All four public headers compile standalone and link from the static library using no internal type |
| Host examples | Pass | The C host and the Python FFI host build and run using only the public surface |
| Installed SDK | Pass | The host compiles against the installed headers and import library and runs on the installed DLL |
| Versioning | Pass for negotiation | `wsh_embedding_abi_version()` exposes the ABI at runtime; the PE version resource remains open |
| Compatibility | Pass for x86/x64 | x86 exports the same undecorated surface and passes; the FFI test is bitness-gated; ARM64 is unverified |

## Defect Patterns and Resolution

| Finding | Severity | Resolution |
| --- | --- | --- |
| D-0801: the shared library was built from `wshlib.c` linking the static core archive, so the linker pulled only version objects and the DLL omitted the rest of the ABI | High | Compile `WSH_CORE_SOURCES` directly into `wsh_shared`; a Python `ctypes` host confirmed the symbols resolve from the DLL |
| The shared conformance test initially masked D-0801 by also linking static `wsh_core` | High | After the composition fix the shared consumer resolves from the DLL import definition, so the test genuinely exercises the DLL |
| `-Wl,-export-all-symbols` leaked 286 runtime/CRT/internal symbols into the ABI | High | Replace export-all with `WSH_API` markers; verified empirically that one marker switches TinyCC to annotated-only exports |
| The FFI test used a 64-bit interpreter against a 32-bit DLL and failed under the x86 target | Medium | Gate the FFI test at configure time on interpreter/target bitness, skipping with a clear status message |
| The export-verification test initially checked only for missing symbols | Medium | Extend it to also fail on unexpected exports, enforcing an exact surface |

No unresolved M8 product defect remains from this review.

## Residual Boundaries

The shared library carries no PE version resource: the TinyCC toolchain embeds
no `VERSIONINFO` from CMake's `VERSION`, and adding one needs a resource
compiler not present in the toolchain. Runtime ABI negotiation is available
through `wsh_embedding_abi_version()`; the resource itself is deferred to a
resource-compilation step or an approved release limitation.

ARM64 cross-compiles but is not executable on the x64 review host. Its
embedding suite, including an architecture-matched FFI interpreter, remains a
required verification before the M8 exit claim, alongside the WSP Specify,
traceability, and TeX-evidence artifacts.
