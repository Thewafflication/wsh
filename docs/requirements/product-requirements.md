# Waughtal Shell Product Requirements

**Document ID:** `WSH-REQ-INDEX`

**Status:** Accepted

**Baseline target:** WSH 1.0

## 1. Scope and Use

These requirements define the proposed product baseline. Detailed observable
behavior is allocated to the [specification set](../specification/README.md).
Each requirement retains its identifier permanently. Acceptance requires WSP
requirements review, a planned verification artifact, and approval of every
referenced compatibility or security decision.

The principal stakeholder need is one portable, predictable interactive and
batch shell for Windows 2000 through current Windows, without dependence on
the different `cmd.exe` and PowerShell behaviors supplied by each OS.

Verification methods are test, inspection, analysis, review, or demonstration.
`TC-NNNN` identifiers below are planned controlled test-case families; one
family may contain architecture or OS configurations and one test may verify
multiple requirements.

## 2. Existing Core Requirements

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0001` | The product shall provide a Windows-native command interpreter executable named `wsh.exe`. | Stakeholder need | `TC-0001`, test |
| `WSH-REQ-0002` | Project-owned production source shall be C99 with compiler extensions disabled. | Project constraint | Inspection and build |
| `WSH-REQ-0003` | Every supported configuration shall configure and build through checked-in CMake presets. | WSP/toolchain ADR | `TC-0003`, test |
| `WSH-REQ-0004` | CTest shall be the top-level dispatcher for every automated release-gate test. | WSP test process | Inspection and controlled failure test |
| `WSH-REQ-0005` | Each controlled test execution shall generate complete traceable TeX evidence. | WSP test process | `TC-0005`, positive and negative test |
| `WSH-REQ-0006` | WPM shall provision pinned WCRT, KerTeX, and cv2pdb dependencies for supported build architectures. | Toolchain decision | Inventory inspection and clean provision test |
| `WSH-REQ-0007` | The interpreter shall represent every language value as an ordered flat list of Unicode strings. | `rc` value model | `TC-0007`, unit and conformance test |
| `WSH-REQ-0008` | The interpreter shall implement `rc` apostrophe quotation and doubled-apostrophe behavior. | `rc(1)` quotation | `TC-0008`, unit and conformance test |
| `WSH-REQ-0009` | The interpreter shall implement explicit and free-caret concatenation with the specified list distribution behavior. | `rc(1)` caret | `TC-0009`, unit and conformance test |
| `WSH-REQ-0010` | The interpreter shall parse the controlled WSH grammar without delegating parsing to another command interpreter. | Language decision | Parser inspection and `TC-0010` negative test |
| `WSH-REQ-0011` | The interpreter shall execute Windows programs without implicitly invoking another command interpreter. | Stakeholder need | `TC-0011`, integration test |
| `WSH-REQ-0012` | The interpreter shall apply the specified executable-discovery order for explicit paths, the current directory, extensions, and `$path`. | Windows adaptation | `TC-0012`, decision-table test |
| `WSH-REQ-0013` | Structured launch shall preserve argument boundaries for WSH/WCRT children through the documented Windows serializer. | Nested-call need | `TC-0013`, exhaustive argument round trip |
| `WSH-REQ-0014` | The interpreter shall implement sequential, conditional, and inverted status composition using the all-zero truth rule. | `rc(1)` compound commands | `TC-0014`, unit/integration test |
| `WSH-REQ-0015` | The interpreter shall apply input, output, append, duplication, closure, and standard-error redirections from left to right. | `rc(1)` redirection | `TC-0015`, integration test |
| `WSH-REQ-0016` | The interpreter shall execute linear pipelines without leaking handles and shall retain every stage status. | `rc(1)` pipelines | `TC-0016`, integration/resource test |
| `WSH-REQ-0017` | The interpreter shall implement the specified `rc`-derived control-flow and function syntax. | `rc(1)` compound commands | `TC-0017`, conformance test |
| `WSH-REQ-0018` | The interpreter shall use validated UTF-8 internally and wide-character Win32 APIs at operating-system text boundaries. | Unicode decision | Inspection and `TC-0018` round trip |
| `WSH-REQ-0019` | A diagnostic shall identify source and location when a source location exists. | Usability/test need | `TC-0019`, output inspection |
| `WSH-REQ-0020` | Interactive interruption shall return control to the prompt after applying the documented child-tree cancellation policy. | Interactive need | `TC-0020`, console integration test |
| `WSH-REQ-0021` | The release documentation shall identify every intentional difference from the supported Plan 9 `rc` reference. | Stakeholder decision | Compatibility-matrix review |
| `WSH-REQ-0022` | Debug release evidence shall retain GDB-usable DWARF symbols; PDB output shall be additional. | WSP test profile | Artifact inspection and failing-test backtrace |
| `WSH-REQ-0023` | Malformed input shall not execute a partial affected command. | Security and predictability | `TC-0023`, negative/fuzz test |
| `WSH-REQ-0024` | The interpreter shall release owned memory, handles, and child resources on normal and tested error paths. | Reliability/security | Analysis, instrumentation, and `TC-0024` |

## 3. Portability and Deployment

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0025` | One unchanged x86 release binary shall execute on Windows 2000 and every later claimed x86/WOW64 configuration. | Stakeholder decision | `TC-0025`, release matrix |
| `WSH-REQ-0026` | One unchanged x64 release binary shall execute on every claimed x64 Windows version beginning with XP/Server 2003 x64. | Stakeholder decision | `TC-0026`, release matrix |
| `WSH-REQ-0027` | One unchanged ARM64 release binary shall execute on every claimed Windows ARM64 version. | Stakeholder decision | `TC-0027`, native release matrix |
| `WSH-REQ-0028` | The official architecture-specific distribution shall perform core operation after copying only `wsh.exe` to the target. | Portability decision | `TC-0028`, clean-machine demonstration |
| `WSH-REQ-0029` | Official `wsh.exe` binaries shall statically link the pinned WCRT and WSH standard library. | Stakeholder decision | PE/import and build inspection |
| `WSH-REQ-0030` | Core operation shall not require UCRT, a Visual C++ redistributable, .NET, PowerShell, `cmd.exe`, POSIX emulation, installation, registration, or administrator rights. | Stakeholder need | Clean-machine test and import inspection |
| `WSH-REQ-0031` | Optional newer Windows APIs shall be dynamically resolved and shall have a specified old-Windows fallback. | Cross-version constraint | Static-import inspection and matrix test |
| `WSH-REQ-0032` | Interactive and batch execution shall expose identical language semantics on every supported Windows version. | Stakeholder need | Cross-mode/cross-OS conformance comparison |

## 4. Modes, Text, and Paths

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0033` | WSH shall support interactive input, script files, `-c` command text, and redirected standard input. | Stakeholder decision | `TC-0033`, mode matrix |
| `WSH-REQ-0034` | Source input shall treat CRLF, LF, and lone CR as equivalent line endings and shall accept a final unterminated line. | Windows adaptation | `TC-0034`, partition test |
| `WSH-REQ-0035` | Canonical scripts and redirected WSH text shall use UTF-8, while WSH console text shall use wide console APIs. | Unicode decision | `TC-0035`, byte/console inspection |
| `WSH-REQ-0036` | WSH shall accept BOM-marked UTF-16LE and UTF-16BE script input without changing internal semantics. | Legacy editor compatibility | `TC-0036`, encoding round trip |
| `WSH-REQ-0037` | WSH shall preserve valid supplementary Unicode characters through parsing, values, files, environment, and Unicode-aware child launch. | Emoji/use-case decision | `TC-0037`, non-BMP round trip |
| `WSH-REQ-0038` | WSH syntax shall permit native drive, drive-relative, UNC, relative, verbatim, and device path spellings without treating backslash as an escape. | Windows path decision | `TC-0038`, path grammar/integration test |
| `WSH-REQ-0039` | WSH shall pass external path arguments exactly as supplied and shall not globally normalize slash direction. | Windows path decision | Argument-echo test |
| `WSH-REQ-0040` | A bare external command shall search the current directory before `$path` unless explicit safe-path policy applies. | Stakeholder decision | `TC-0040`, resolution test |
| `WSH-REQ-0041` | Extension inference shall consider only the exact name, `.exe`, and `.com` in the specified order. | Windows execution decision | `TC-0041`, decision-table test |
| `WSH-REQ-0042` | WSH shall not implicitly execute `.bat`, `.cmd`, PowerShell, file associations, or URL handlers. | Stakeholder decision | `TC-0042`, negative launch test |
| `WSH-REQ-0043` | Executing an explicit `.wsh` path shall start an isolated child WSH, while `source` shall evaluate in the caller context. | Nested-call decision | `TC-0043`, state-isolation test |

## 5. Processes, Variables, and Status

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0044` | Structured external launch shall use a deterministic documented command-line serializer and a separately resolved `CreateProcessW` application name. | Process decision | Serializer review and `TC-0044` |
| `WSH-REQ-0045` | WSH shall provide a policy-controlled raw launch that passes one complete command-line string without reconstruction. | Stakeholder decision | `TC-0045`, exact-byte/unit and policy test |
| `WSH-REQ-0046` | Newly assigned variables shall remain private until explicitly exported; imported Windows variables shall begin exported. | Stakeholder decision | `TC-0046`, inheritance test |
| `WSH-REQ-0047` | An exported variable for an ordinary external program shall be scalar unless an identified adapter defines another representation. | Stakeholder decision | `TC-0047`, boundary test |
| `WSH-REQ-0048` | Private variable names shall be case-sensitive and exported names shall be unique under Windows case-insensitive comparison. | Stakeholder decision | `TC-0048`, equivalence partition test |
| `WSH-REQ-0049` | `$status` shall contain one unsigned Windows exit code per simple command or pipeline stage and a status shall succeed only when all elements are zero. | `rc` pipeline decision | `TC-0049`, conformance test |
| `WSH-REQ-0050` | WSH shall track every child it starts and apply documented wait, timeout, interruption, and shutdown behavior to the child tree. | Test automation need | `TC-0050`, process-tree integration test |
| `WSH-REQ-0051` | Process substitution shall use access-controlled Windows named pipes with bounded lifetime and cleanup. | `rc` Windows adaptation | `TC-0051`, integration/security test |
| `WSH-REQ-0052` | Command substitution shall decode captured text explicitly and shall not launch the containing command after a failed substitution. | Test reliability decision | `TC-0052`, failure-propagation test |

## 6. Interactive Operation

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0053` | Interactive mode shall provide Unicode line editing and multiline input without requiring VT support. | Stakeholder decision | `TC-0053`, legacy/modern console test |
| `WSH-REQ-0054` | Interactive mode shall provide bounded persistent history stored as inert data. | Interactive need | `TC-0054`, corruption/concurrency test |
| `WSH-REQ-0055` | Completion shall be syntax-aware, deterministic, quoted safely, and shall not execute completion code. | Interactive/security need | `TC-0055`, completion test |
| `WSH-REQ-0056` | Ctrl+C shall cancel pending input or the foreground child tree and shall leave the session with a defined status. | Stakeholder need | `TC-0056`, console test |
| `WSH-REQ-0057` | Interactive errors shall return to the prompt unless an explicit exit policy or unrecoverable console failure applies. | Interactive need | `TC-0057`, recovery test |

## 7. Configuration and Registry

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0058` | WSH shall use the specified strict inert UTF-8 INI format for data configuration. | Configuration decision | `TC-0058`, parser/negative test |
| `WSH-REQ-0059` | Batch execution shall load no executable profile unless the invocation explicitly requests one. | Predictability/security | `TC-0059`, startup matrix |
| `WSH-REQ-0060` | WSH shall operate correctly when all product registry keys are absent or inaccessible. | Portability decision | `TC-0060`, clean/restricted registry test |
| `WSH-REQ-0061` | Registry policy shall combine restrictively and shall not be weakened by configuration, command line, nested WSH, or embedding. | Security decision | `TC-0061`, policy matrix |
| `WSH-REQ-0062` | Configuration and registry values shall not change accepted language grammar or core semantics. | Compatibility decision | Inspection and cross-config conformance |
| `WSH-REQ-0063` | Portable adjacent configuration shall load only after explicit `--portable`. | Security decision | `TC-0063`, executable-planting test |
| `WSH-REQ-0064` | Optional `.wsh` association and App Paths registry entries shall be installer-owned and shall not affect WSH command resolution. | Registry decision | Registry inspection and launch test |

## 8. Standard Library and Embedding

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0065` | The embedded standard library shall provide the specified filesystem, path, text, process, time, system, and test namespaces. | Test-script use case | Namespace conformance suite |
| `WSH-REQ-0066` | Filesystem mutation commands shall require explicit recursion and overwrite behavior and shall protect roots from accidental recursive removal. | Security requirement | `TC-0066`, destructive negative test |
| `WSH-REQ-0067` | Process library commands shall support exact capture, working directory, environment changes, timeout, and bounded parallel execution. | Build/test use case | `TC-0067`, orchestration test |
| `WSH-REQ-0068` | Test library commands shall produce statuses and metadata compatible with controlled WSP evidence. | WSP planning decision | `TC-0068`, evidence validation |
| `WSH-REQ-0069` | WSH shall expose stable embedding ABI 1 through static and shared library builds. | Stakeholder decision | ABI inspection and host tests |
| `WSH-REQ-0070` | An embedding context shall own isolated variables, functions, environment, working directory, children, descriptors, and diagnostics. | Embedding decision | `TC-0070`, concurrent-context test |
| `WSH-REQ-0071` | A host shall be able to register a synchronous namespaced command with structured arguments, status, and diagnostics. | Stakeholder decision | `TC-0071`, C and second-language host test |
| `WSH-REQ-0072` | Public ABI memory ownership, threading, cancellation, encoding, versioning, and structure-size rules shall be documented and verified. | ABI reliability | Review, static checks, misuse tests |
| `WSH-REQ-0073` | Static executable, static library, and shared-library hosts shall pass the same language and standard-library conformance suite. | Reuse decision | Cross-artifact result comparison |

## 9. Security, Quality, and Release

| ID | Requirement | Source | Planned verification |
| --- | --- | --- | --- |
| `WSH-REQ-0074` | WSH shall enforce documented bounds on source, tokens, AST depth, expansion, globbing, capture, processes, history, and diagnostics before uncontrolled resource consumption. | WSP-SEC-0006 | Boundary/fuzz/resource tests |
| `WSH-REQ-0075` | WSH shall implement the controls and residual-risk review identified by `WSH-DFS-0001`. | WSP Security/DFS | Security traceability review |
| `WSH-REQ-0076` | Every release shall identify the exact OS/architecture support matrix and native or approved-equivalent evidence for each claim. | WSP-TEST-0013 | Matrix/evidence inspection |
| `WSH-REQ-0077` | Every released executable and DLL shall contain one consistent WSP-compliant Windows version resource. | WSP Windows resource profile | Automated PE resource inspection |
| `WSH-REQ-0078` | `wsh --version` shall follow the WPM-style banner and report product, dependency, library-link, ABI, and system-library identity. | Stakeholder decision | `TC-0078`, golden/semantic inspection |
| `WSH-REQ-0079` | The release shall provide the complete controlled specification and compatibility record in the WSP documentation artifact. | Stakeholder/WSP documentation | Manifest/PDF inspection |
| `WSH-REQ-0080` | The release process shall retain source revision, dependency versions/digests, checksums, provenance, signing decision, malware-scan evidence, and known limitations. | WSP release/security | Release-readiness inspection |
| `WSH-REQ-0081` | The project shall verify that representative build and test suites can be authored and executed entirely in WSH without implicit `cmd` or PowerShell. | Primary use case | End-to-end acceptance demonstration |

## 10. Traceability Rules

The specification document ID and section implementing each requirement shall
be added to an automated traceability inventory before that requirement enters
an implementation milestone. Controlled test specifications shall link back to
all requirements they verify. A release gate fails for a missing requirement,
missing planned verification, missing result, duplicate identifier, or claimed
platform without applicable evidence.
