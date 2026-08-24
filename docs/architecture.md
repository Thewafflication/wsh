# Waughtal Shell Architecture

**Document ID:** `WSH-ARCH-0001`

**Status:** Accepted

## 1. Architectural Goals

The architecture shall preserve one language across Windows 2000 through
current Windows, keep the official artifact portable and statically linked,
make parsing independently testable, and expose the same behavior through an
executable and a stable embedding library.

## 2. Components

```text
wsh.exe / embedding host
          |
          v
  public C embedding ABI
          |
          v
  context and evaluator
   |       |        |
   v       v        v
lexer/   values/   standard-library commands
parser   scopes             |
   |       |                v
   +-------+-------> abstract runtime services
                           |
                           v
                    Win32 + WCRT platform layer
```

### 2.1 Front Ends

`wsh.exe` owns process argument parsing, console-mode selection, configuration,
profiles, history, and interactive editing. An embedding host supplies the
same concerns through explicit ABI options. Neither front end implements
language semantics.

### 2.2 Source, Lexer, and Parser

The source layer validates and decodes input, retains byte and scalar
positions, and normalizes line-ending tokens. The lexer produces immutable
tokens and performs free-caret insertion. The parser consumes tokens through
an explicit grammar and produces an immutable abstract syntax tree (AST).

The parser performs no filesystem, environment, registry, console, or process
operation. Malformed source cannot partially execute because evaluation begins
only for a complete parsed command unit.

### 2.3 Values, Context, and Evaluator

Values are immutable, reference-counted or explicitly owned lists of immutable
UTF-8 strings. A context owns variable bindings, function ASTs, logical working
directory, environment export state, descriptors, child registry, diagnostics,
limits, and registered host commands.

The evaluator operates on abstract runtime services and produces structured
status lists. It contains no direct Win32 calls. A subshell clones semantic
context state without requiring a process-global directory or Unix `fork`.

### 2.4 Standard Library

Native primitives implement operations that cannot be expressed portably in
the language. User-facing commands provide stable namespaced contracts. The
same command implementations are registered in the executable and embedding
contexts.

One immutable descriptor table in the portable core supplies canonical names,
signatures, summaries, result modes, and policy classifications. The evaluator
recognizes that table, validates common `--into` assignment, and crosses the
runtime boundary with `WSH_RUNTIME_LIBRARY`. The isolated Windows runtime owns
filesystem paths, process wrappers, clocks, system queries, and active test
state; it reuses the same logical directory, allocator, limits, descriptors,
child registry, and cleanup paths as ordinary command execution. ADR-0009
controls this boundary.

### 2.5 Runtime Service Boundary

The runtime boundary covers files, paths, clocks, entropy, environment,
registry, console, named pipes, processes, jobs, handles, and cancellation.
Each interface states ownership, UTF-8/UTF-16 conversion, blocking behavior,
resource limits, error mapping, and test substitution.

The Windows implementation uses the minimum static import set. Optional APIs
are resolved dynamically and represented as capabilities. Tests can supply a
deterministic fake runtime without linking Win32 process behavior into parser
tests.

For process composition, the evaluator supplies a borrowed typed launch plan:
one or more structured commands, ordered logical-descriptor actions, pipeline
edges, execution mode, and the context whose exported variables form a
snapshot. The Windows runtime validates and deep-prepares that plan before the
first effect. It owns logical working-directory state and every created handle,
pipe, child, job, wait, and cancellation transition.

## 3. Build Artifacts

| Artifact | Purpose | Release status |
| --- | --- | --- |
| `wsh.exe` | Statically linked portable shell | Required |
| `wshlib.lib` | Static embedding library | Supported SDK artifact |
| `wshlib.dll` | Alternative shared embedding library | Supported SDK artifact |
| `wshlib.dll.lib` | Import library, toolchain-specific name finalized in build design | Supported SDK artifact |
| Public C header | ABI declarations | Required with SDK artifacts |
| Debug symbols | DWARF and additional PDB where supported | Required evidence/debug artifact |

The shared DLL statically links WCRT. The executable may be built against the
DLL for conformance testing, but the portable distribution uses the static
library.

## 4. State and Concurrency

Mutable shell state belongs to a context. Process-wide immutable tables may
contain Unicode folding data, keyword metadata, and command definitions.
Process-global current directory, locale, code page, standard stream, and
environment mutation are prohibited in the library core.

One context executes on one thread at a time. Multiple contexts may execute
concurrently. Cancellation uses a narrow thread-safe request path. Child wait
and stream pumps have explicit lifetimes and join before context destruction.

## 5. Ownership and Failure

Every allocation and Windows handle has one documented owner. Transfers are
explicit. Cleanup paths are idempotent. A failure returns a structured error
without leaking partially initialized state.

AST, value, and standard-library query construction use commit-or-discard
builders. Process launch uses a prepared launch record: arguments,
environment, directory, descriptors, and policy are validated before
`CreateProcessW`. A partially started pipeline is cancelled and collected
before evaluation returns. Filesystem tree copy verifies the destination
before a cross-volume move removes its source.

## 6. Dependency Direction

Dependencies point inward:

```text
front ends -> public ABI -> evaluator/core -> runtime interfaces
                                           -> standard-library interfaces
Windows/WCRT platform --------------------^ implements interfaces
```

The core does not include headers from the interactive front end or concrete
Windows platform implementation. The public ABI does not expose WCRT internals.

## 7. Verification Allocation

- Source, lexer, grammar, values, and evaluator use deterministic unit tests.
- Process serialization uses argument-echo helpers built with WCRT and other
  representative runtimes.
- Files, registry, console, handles, named pipes, jobs, and cancellation use
  Windows integration tests.
- The executable, static library, shared DLL, and foreign-language host run a
  shared conformance suite.
- Every supported OS/architecture claim uses the final statically linked
  artifact and records native execution evidence.

## 8. Durable Decisions

The ADR set records the `rc` adaptation, parser/runtime separation,
WPM/WCRT/CMake/CTest toolchain, single-binary compatibility, embedding boundary,
configuration/registry model, process/status model, old/new Windows handle
inheritance strategy, and embedded standard-library boundary. Proposed ADRs
become authoritative only through the WSP baseline review.
