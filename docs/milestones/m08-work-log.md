# M8 Work Log — Embedding SDK

**Milestone:** M8 — Embedding SDK (stable ABI 1, static/shared libraries, host
examples, conformance and misuse tests).

**Inherited baseline:** `60b8061` (`Completed M7`).

**Author:** Claude (Overlord cross-project assistant), on behalf of the owner.

**Status:** Active (2026-08-30 session).

This log records the chronological execution of the M8 milestone. It
supplements, and does not replace, the accepted [M8 plan](m08-plan.md), the
controlled Git history, and the retained CTest/evidence records. Token figures
are recorded per the owner's request; the Claude Code environment does not
expose a live token counter to the assistant, so per-increment figures are
assistant-side estimates (marked `est.`) pending an environment goal-level
total.

## Work Performed

| Date or order | Phase | Activity | Output |
| --- | --- | --- | --- |
| 2026-08-30 #1 | Baseline/Plan | Draft accepted M8 plan; record M7 as complete in the status record | Commit `eb755d6` |
| 2026-08-30 #2 | Implement | Add ABI conformance and misuse test linked against the static SDK using only public headers | Commit `7f3527a` |
| 2026-08-30 #3 | Implement | Add C embedding host example built against public headers and the static library | Commit `89fd0e4` |
| 2026-08-30 #4 | Implement | Run the ABI conformance cases against the shared library for static/shared parity | Commit `170f937` |
| 2026-08-30 #5 | Implement | Compile the core sources into the shared library so the DLL exports the whole ABI; add a Python ctypes FFI host | Commit `048c664` |
| 2026-08-30 #6 | Implement | Add the ABI 1 export manifest and a verification test asserting every public symbol is exported | Commit `5dc42ad` |

## Verification Log

| Date | Configuration or method | Result | Evidence or failure reference |
| --- | --- | --- | --- |
| 2026-08-30 | `cmake --build --preset x64-debug` (baseline `60b8061`) | Up to date; fast unit tests 4/4 | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R abi-conformance` (post `7f3527a`) | Pass (4/4 cases) | Local CTest run |
| 2026-08-30 | Fast unit subset incl. new test (post `7f3527a`) | Pass (5/5) | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R embedding-host-example` (post `89fd0e4`) | Pass (deterministic host output) | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R abi-conformance` (post `170f937`) | Pass (2/2 static + shared) | Local CTest run |
| 2026-08-30 | `python host.py wshlib.dll` (pre-fix) | Fail: `wsh_context_create` not found in DLL | Defect D-0801 |
| 2026-08-30 | Fast unit + embedding subset (post `048c664`) | Pass (7/7) | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R embedding-host-python` (post `048c664`) | Pass (FFI host) | Local CTest run |
| 2026-08-30 | `ctest --preset x64-debug -R abi-export-manifest` (post `5dc42ad`) | Pass (all 100 ABI 1 symbols exported) | Local CTest run |

## Decisions and Scope Changes

| Decision or change | Authority | Impact | Reference |
| --- | --- | --- | --- |
| Proceed into M8 after M7 completion | Owner request ("M8 plan looks good, work until we run out of tokens") | Begins the Embedding SDK milestone under the accepted plan | This log |
| Compile the core sources directly into the shared library and link the runtime instead of the static core archive | Defect D-0801 root cause; ADR-0005 requires the DLL to expose ABI 1 | The whole ABI is present in and exported from the DLL; shared consumers resolve it from the DLL import definition | Commit `048c664`, ADR-0005 |
| Define ABI 1 as the 100 portable `wsh_*` functions from core/evaluator/parser/version headers; exclude `wsh_windows_runtime_*` from the portable embedding DLL | ADR-0005 (portable context-based library) plus the observed shared-library composition | Establishes the approved export manifest the DLL must contain; the Windows runtime remains a host-side static library, not part of the portable DLL surface | Commit `5dc42ad`, `tests/embedding/abi1-exports.txt` |

## Problems, Defects, and Recovery

| Item | Effect | Response | Status or owner |
| --- | --- | --- | --- |
| D-0801: `wsh_shared` was built from `wshlib.c` linking the static core archive | The linker pulled only version objects referenced by `wshlib.c`; the DLL omitted `wsh_context_create` and the rest of the ABI. The C shared "parity" test passed only because it also linked static `wsh_core`, masking the gap | Compiled `WSH_CORE_SOURCES` into `wsh_shared`; a Python ctypes host confirmed the symbols now resolve from the DLL | Closed (`048c664`) |

## Measurements

| Measure | Value | Source or interpretation |
| --- | ---: | --- |
| M8 CTest cases added | 5 | `abi-conformance`, `abi-conformance-shared`, `embedding-host-example`, `embedding-host-python`, `abi-export-manifest` |
| ABI 1 public symbols (manifest) | 100 | `tests/embedding/abi1-exports.txt` |
| Current DLL exported symbols | 286 | `wshlib.def`; excess is runtime/CRT/internal, pending export restriction |
| Defects found and closed | 1 | D-0801 (shared library omitted the ABI) |

## Resource Usage

| Goal or work period | Tokens used | Elapsed time | Source |
| --- | ---: | ---: | --- |
| M8 Baseline/Plan (`eb755d6`) | ~30,000 est. | Not reported | Claude Code, assistant estimate |
| ABI conformance/misuse test (`7f3527a`) | ~40,000 est. | Not reported | Claude Code, assistant estimate |
| C embedding host example (`89fd0e4`) | ~22,000 est. | Not reported | Claude Code, assistant estimate |
| Shared conformance parity (`170f937`) | ~14,000 est. | Not reported | Claude Code, assistant estimate |
| ABI export fix + Python FFI host (`048c664`) | ~48,000 est. | Not reported | Claude Code, assistant estimate |
| ABI 1 export manifest + verification (`5dc42ad`) | ~34,000 est. | Not reported | Claude Code, assistant estimate |

## Preservation and Handoff

Retained evidence is the CTest output and the Git commit history on
`origin/master`. The `wsp` submodule worktree carries unrelated user-owned
changes and remains untouched. Work remains on `master`, matching current
repository practice.

**M8 progress to date (Implement phase, partial):** the static and shared
embedding libraries build and pass a conformance and misuse suite through both
linkages; a C host and a Python ctypes FFI host build and run against the
public headers and the shared DLL; the shared library now contains the whole
ABI (D-0801 fixed); and the approved 100-symbol ABI 1 manifest is verified as
exported. All committed and pushed; validated on `x64-debug`.

**Remaining M8 work for the next session:**

1. **Restrict the export surface.** The DLL still exports 286 symbols via
   `-Wl,-export-all-symbols`, including runtime/CRT and internal WSH functions.
   Replace export-all with an explicit export set limited to the manifest, then
   extend `verify-abi-exports.ps1` to also fail on any *unexpected* export.
   This is the negative half of the export exit gate and needs deliberate
   TinyCC linker work (a curated `.def` input or per-symbol export attributes).
2. **Version resource.** Verify the shared library's version resource and
   `SOVERSION`/ABI value against `WSH_EMBEDDING_ABI`.
3. **WSP phase artifacts.** Produce the M8 Specify (requirements under
   `docs/requirements/m8`), Design (`m08-design.md`), controlled test
   specifications (`docs/tests/m8`), traceability wiring, TeX evidence, review
   (`m08-review.md`), and closeout (`m08-closeout.md`); then record the
   post-M8 roadmap recalibration.
4. **Cross-architecture.** Re-run the embedding suite on `x86` and `arm64`
   before the exit claim.

**Next responsible party:** the maintainer or a subsequent assistant session,
continuing M8 from the export-restriction task.
