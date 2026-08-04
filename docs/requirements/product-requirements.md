# Waughtal Shell Product Requirements

**Document ID:** `WSH-REQ-INDEX`

**Status:** Proposed

These are planning-level requirements. Each will be split into a controlled
requirement file with complete rationale, relationships, implementation
allocation, and `TC-NNNN` references before acceptance into a release baseline.

| ID | Requirement | Planned verification |
| --- | --- | --- |
| `WSH-REQ-0001` | The product shall provide a Windows-native command interpreter executable named `wsh`. | Test |
| `WSH-REQ-0002` | Project-owned production source shall be written in C99 with compiler extensions disabled. | Inspection and build |
| `WSH-REQ-0003` | The project shall configure and build supported configurations with CMake presets. | Test |
| `WSH-REQ-0004` | CTest shall be the top-level dispatcher for every automated release-gate test. | Inspection and test |
| `WSH-REQ-0005` | Each controlled test execution shall generate complete, traceable TeX evidence. | Test |
| `WSH-REQ-0006` | WPM shall provision pinned `wcrt`, `kertex`, and `cv2pdb` dependencies for supported build architectures. | Test and inspection |
| `WSH-REQ-0007` | The interpreter shall represent a shell variable as an ordered list of strings. | Unit test |
| `WSH-REQ-0008` | The interpreter shall implement `rc` apostrophe quotation and doubled-apostrophe behavior. | Unit and conformance test |
| `WSH-REQ-0009` | The interpreter shall implement explicit and free-caret concatenation with defined list distribution behavior. | Unit and conformance test |
| `WSH-REQ-0010` | The interpreter shall parse a documented grammar without delegating parsing to `cmd.exe` or PowerShell. | Inspection and negative test |
| `WSH-REQ-0011` | The interpreter shall execute Windows programs without implicitly invoking another command interpreter. | Integration test |
| `WSH-REQ-0012` | The interpreter shall define and test executable discovery for explicit paths, extensions, and `$path`. | Integration test |
| `WSH-REQ-0013` | The interpreter shall preserve argument boundaries through its Windows command-line serialization contract for conforming child programs. | Integration test with echo helper |
| `WSH-REQ-0014` | The interpreter shall support sequential, conditional, and inverted status composition. | Unit and integration test |
| `WSH-REQ-0015` | The interpreter shall support ordered input, output, append, and standard-error redirections. | Integration test |
| `WSH-REQ-0016` | The interpreter shall support linear pipelines without leaking inherited handles. | Integration and resource test |
| `WSH-REQ-0017` | The interpreter shall support documented `rc` control-flow and function syntax. | Conformance test |
| `WSH-REQ-0018` | The interpreter shall use UTF-8 internally and wide-character Win32 APIs at operating-system boundaries. | Inspection and round-trip test |
| `WSH-REQ-0019` | Syntax and runtime diagnostics shall identify the source and location when one is available. | Unit and acceptance test |
| `WSH-REQ-0020` | Interactive interruption shall return control to the prompt and terminate or detach children according to a documented policy. | Console integration test |
| `WSH-REQ-0021` | The product shall document every intentional difference from the supported Plan 9 `rc` language baseline. | Review |
| `WSH-REQ-0022` | Debug release evidence shall retain GDB-usable DWARF symbols; PDB output shall be additional. | Artifact inspection |
| `WSH-REQ-0023` | The interpreter shall reject malformed input without executing a partial command unless the documented interactive continuation rule applies. | Negative test |
| `WSH-REQ-0024` | The interpreter shall release owned memory, handles, and child-process resources on normal and tested error paths. | Analysis and test |

