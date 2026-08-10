# Waughtal Shell Design for Security

**Document ID:** `WSH-DFS-0001`

**Status:** Accepted

## 1. Scope

This Design for Security (DFS) covers `wsh.exe`, static and shared `wshlib`,
embedded standard commands, configuration and profile loading, registry
integration, child-process launch, interactive history, and release artifacts.

WSH executes user-selected code with the user's operating-system authority. It
is not a sandbox, antivirus product, policy enforcement boundary, or privilege
broker. A trusted host callback has the host process's authority.

## 2. Assets and Consequences

| Asset | Adverse consequence |
| --- | --- |
| Command and argument boundaries | Injection or execution of a different operation |
| User files and directories | Unauthorized disclosure, overwrite, or recursive deletion |
| Environment and credentials | Secret disclosure to children, logs, history, or diagnostics |
| Process and handle boundaries | Leaked handles, orphaned children, or unintended I/O access |
| Configuration and profiles | Persistent execution or policy bypass |
| Host process integrity | Crash or corruption through ABI/callback misuse |
| Release identity | Execution of modified or incorrectly attributed binaries |
| Availability | Unbounded parsing, expansion, globbing, output, or process creation |
| Verification evidence | False pass, overwritten failure, or untraceable result |

## 3. Trust Model

Untrusted inputs include script text, interactive text, command arguments,
environment blocks, the private WSH environment envelope, configuration,
profiles not separately approved, registry data, paths and directory entries,
reparse points, external output, history files, named-pipe peers, and embedded
host input.

External executables and explicit profiles are trusted to perform the actions
the user authorizes, but their output and exit status remain untrusted data.
WCRT, pinned build tools, reviewed project source, the operating-system kernel,
and approved release keys are trusted dependencies within their stated scope.
Host commands are fully trusted native code.

Trust is not inferred from a local path, network path, successful decoding,
valid syntax, registry location, file ownership, or transport encryption.

## 4. Entry Points and Boundaries

- process command line and inherited handles;
- console, redirected stdin, scripts, `-c`, and sourced files;
- INI files, executable profiles, environment, and registry;
- executable resolution and raw/structured process launch;
- filesystem and named-pipe standard-library commands;
- history load and persistence;
- public embedding ABI, stream callbacks, policy callbacks, and host commands;
- WPM packages, build inputs, release artifacts, and update channels.

The principal privilege boundary is the Windows user token. WSH does not
elevate. Machine configuration and policy may be administrator-controlled but
are still validated before use.

## 5. Security Goals

1. Structured commands shall reach `CreateProcessW` without unintended shell
   reparsing.
2. No interpreter, association, profile, or current-directory configuration
   shall execute implicitly outside the documented rules.
3. Untrusted input shall be bounded and validated before allocation or effect.
4. Handles, environment, directories, and child processes shall be isolated to
   the requested operation.
5. Destructive filesystem operations shall resist empty, root, traversal, and
   reparse-point mistakes.
6. Secrets shall not be exposed through ordinary diagnostics, trace output,
   history, or avoidable child arguments.
7. Policy shall fail closed for the affected capability and shall not be
   weakened by configuration or an embedding host.
8. Release artifacts and evidence shall be traceable to pinned source and
   dependencies.

## 6. Threats and Controls

| Threat | Principal controls | Residual risk |
| --- | --- | --- |
| Command injection | Explicit parser; structured argument lists; `CreateProcessW` application name; no implicit `cmd`; raw launch is explicit and policy-controlled | Target programs may implement incompatible parsers |
| Malicious source | Encoding/length limits; immutable AST; no execution before complete parse; resource budgets | Authorized scripts can intentionally delete or execute with user rights |
| Path traversal/deletion | Absolute resolution, protected-root refusal, reparse non-traversal, explicit recursion/overwrite | Races remain possible on hostile shared filesystems |
| Executable planting | Documented current-directory search; `--safe-path`; policy; diagnostic resolution tools | Traditional default accepts current-directory risk by stakeholder decision |
| Environment collision/injection | Case-fold duplicate rejection; explicit export; scalar validation; authenticated bounded WSH envelope | Same-authority processes can observe or modify their own environment |
| Handle leakage | Explicit inheritance allowlist; post-launch closure; pipeline negative tests | Third-party child behavior is outside WSH control |
| Orphaned process trees | Child registry, job objects, tracked fallback, bounded cancellation and collection | Nested-job restrictions can reduce containment on old Windows |
| Named-pipe substitution | Unpredictable names, restrictive DACL, expected-instance handshake, bounded lifetime | Same-user attackers may race on systems with weak isolation |
| Profile/config persistence | Inert strict INI, no current-directory auto-load, profiles excluded from batch by default, policy disable, absolute-path checks | A selected profile is executable user code |
| History disclosure | Optional/policy-disable, bounded data parser, atomic replace, `history::suppress`, restrictive ACL where available | Users may enter secrets that cannot be recognized reliably |
| ABI memory corruption | Opaque handles, sized structs, fixed-width types, allocator ownership, synchronous callbacks, fuzz/negative tests | Trusted native callbacks can corrupt the process |
| Resource exhaustion | Limits on source, token, AST, expansion, recursion, glob results, captures, processes, history, diagnostics, and time | Deliberately authorized external programs can consume resources |
| Dependency/release compromise | WPM pinning, digests, provenance, signing, malware scan, reproducible identity, retained evidence | Windows 2000 lacks modern platform mitigations and trust services |

## 7. Secure Failure and Recovery

Parsing and launch preparation fail before effect. Partially started pipelines
are cancelled and collected. Configuration errors do not fall back to a less
restricted file. Malformed policy disables the affected capability. Evidence
generation failure fails the controlled test.

WSH preserves the original failure when cleanup also fails and emits a
secondary diagnostic. It never converts a blocked, inconclusive, or incomplete
verification into a pass.

## 8. Cryptography and Secrets

The product designs no cryptographic algorithm. It uses reviewed operating-
system or pinned dependency implementations for entropy, SHA-256, signatures,
and provenance verification. The private environment-envelope nonce is an
instance-authentication aid, not a durable secret or cross-user security token.

Private signing keys never enter the repository or ordinary build logs. WSH
does not claim command-line arguments are secret. Trace and version output
shall not disclose credentials, developer paths, host names, or unrelated
environment content.

## 9. Verification

Security verification includes malformed encodings, parser fuzzing, expansion
limits, argument round trips, raw-launch policy denial, executable planting,
path traversal, reparse races, recursive-delete protected roots, environment
collisions, inherited-handle enumeration, process-tree cancellation, named-
pipe impersonation, history corruption, registry type/length errors, ABI misuse,
and incomplete-evidence failures.

Every accepted threat traces to a `WSH-REQ` requirement and one or more test,
analysis, inspection, or review artifacts under the WSP process.

## 10. Residual-Risk Decisions Required Before 1.0

- continued traditional current-directory command search by default;
- reduced exploit mitigations and unsupported vendor security updates on
  Windows 2000 and Windows XP;
- limits of descendant containment when WSH runs inside an existing job;
- third-party nonstandard Windows command-line parsers;
- same-user history and named-pipe attacks on legacy filesystems; and
- the support and signing story for binaries usable on obsolete Windows while
  preserving modern trust requirements.
