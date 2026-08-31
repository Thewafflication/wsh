# M8 Design — Embedding SDK

**Status:** Reviewed for implementation

**Date:** 2026-08-30

## Library Composition and ABI Scope

The embedding surface is the portable context-based library from ADR-0005: the
source decoder, immutable strings/values/status lists, the isolated context,
the abstract runtime boundary and fake runtime, the evaluator, and the parser,
plus the version accessors. These are declared across `wsh/core.h`,
`wsh/evaluator.h`, `wsh/parser.h`, and `wsh/wsh.h`, and compiled from
`WSH_CORE_SOURCES` and `src/wshlib.c`.

ABI 1 is exactly the 103 `wsh_*` functions in those four headers, recorded in
`tests/embedding/abi1-exports.txt`. The Windows runtime (`wsh_windows_runtime_*`)
is deliberately excluded from the portable embedding DLL: it is a host-side
integration layer linked into `wsh.exe`, not part of the portable, side-effect
free surface an external host embeds.

The static library (`wsh_static`) and the shared library (`wsh_shared`) present
the same surface. The shared library compiles the core sources directly rather
than linking the static core archive; linking the archive would pull in only
the objects referenced by `wshlib.c` and silently omit the rest of the ABI
(defect D-0801). The executable links the static library and adds the Windows
runtime and interactive session, which are not part of ABI 1.

## Export Control

The shared library exports only the ABI 1 surface. Public declarations are
marked `WSH_API`, which expands to `__declspec(dllexport)` when the shared
library is built (`WSH_SHARED_BUILD`) and to nothing otherwise. TinyCC, like
MSVC, exports only marked symbols once any `dllexport` marker is present, so the
DLL export set is exactly the marked functions — runtime, CRT, and internal WSH
symbols stay private. Static consumers and the executable observe an empty
marker and are unchanged; shared consumers resolve the surface through the DLL
import definition and require no import marker.

`tests/verify-abi-exports.ps1` reads the export definition beside the DLL and
fails if the exported set is not exactly the manifest: nothing missing and
nothing extra. Both directions are enforced so a later accidental export or a
dropped symbol fails the gate.

## Objects, Ownership, and Diagnostics

The surface is handle-based and allocator-explicit. Opaque handles
(`wsh_context`, `wsh_value`, builders, status lists) are created through
output pointers, own the allocator that created them, and are released by
matching destroy calls that accept null. Text crossing the boundary is
length-delimited strict UTF-8 through `wsh_string_view`. A context owns its
variables (private or exported), a bounded diagnostic queue, and an optional
abstract runtime; it performs no process-global directory, environment,
console, or locale mutation.

Documented misuse is defined, not undefined: null owners on destroy are no-ops;
null output pointers, absent variables, out-of-range indices, and empty status
queries return `WSH_ERR_INVALID`/`WSH_ERR_MISMATCH` without dereferencing.

## Conformance and Misuse Strategy

One conformance and misuse suite (`abi_conformance_tests.c`) is built twice —
against the static archive and against the shared DLL — so the two linkages are
proven behaviorally equivalent from identical sources. It uses only the public
headers, asserts the frozen ABI value and its runtime accessor, exercises the
context/variable/export/diagnostic lifecycle, and checks that each documented
misuse returns a defined error. The export manifest test proves the shared
surface is exactly ABI 1.

Hosts register exact namespaced commands on an evaluator. The evaluator owns
the copied name while the host retains callback state; callbacks are
synchronous, receive structured arguments and bounded result builders, and
cannot reenter or mutate registration on the active evaluator.

## Host Examples

A C host (`examples/embedding/host.c`) and a Python `ctypes` FFI host
(`examples/embedding/host.py`) demonstrate embedding from C and from a second
language. Both use only the public surface: they create an isolated context,
work with list-valued variables and diagnostics, and release everything without
touching an internal type or process-global state. The FFI host additionally
proves the DLL exports the whole ABI to a non-C caller and runs only when the
interpreter's bitness matches the target, since a 64-bit interpreter cannot
load a 32-bit DLL.

## Version and Compatibility

`WSH_EMBEDDING_ABI` is the compile-time ABI value; `wsh_embedding_abi_version()`
is its runtime counterpart, letting a host negotiate compatibility without
recompiling against the header. A future ABI change increments this single
value. After linking, the build compiles controlled `VERSIONINFO` with Windows
SDK `rc.exe` and installs its raw RT_VERSION payload through
`UpdateResourceW`. This leaves TinyCC and the PE import surface unchanged while
giving the executable and DLL consistent file/product identity.
