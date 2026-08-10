# Waughtal Shell 1.0 Embedding API

**Document ID:** `WSH-SPEC-EMBED-0001`

**Status:** Accepted

## 1. Scope

WSH 1.0 provides a stable C ABI for hosting the parser, evaluator, standard
commands, and Windows runtime services. The official executable statically
links this library. An alternative build produces `wshlib.dll` and
`wshlib.lib`; the DLL statically links WCRT and requires no WCRT DLL.

The ABI is suitable for C-compatible foreign-function interfaces. It is not a
native plugin loader and WSH scripts cannot load arbitrary DLLs.

## 2. ABI Rules

- ABI version is the integer `1` for the 1.0 line.
- Exported names use C linkage and `__cdecl`.
- Public integers have fixed widths from `<stdint.h>`.
- Public text is validated UTF-8 with explicit byte length; it need not be
  NUL-terminated and shall not contain U+0000.
- Public structures begin with `size` and `abi_version` fields. A callee reads
  only fields covered by `size` and ignores documented trailing extensions.
- Internal structures, `FILE`, Windows C++ types, and WCRT implementation types
  never cross the ABI.
- Every allocation is released by the allocator that created it.
- All functions return a `wsh_result` code and provide structured diagnostics
  through the context.

## 3. Public Types

The public header defines opaque `wsh_context`, `wsh_value`, `wsh_diagnostic`,
and `wsh_command_registration` handles. It defines length-delimited
`wsh_string_view`, stream callbacks, allocator callbacks, policy callbacks,
and the synchronous host-command callback.

Conceptual signatures are normative in name and purpose; exact C spelling is
generated and reviewed during the embedding milestone:

```c
uint32_t wsh_abi_version(void);
wsh_result wsh_version_info(wsh_version *out);
wsh_result wsh_context_create(
    const wsh_context_options *options,
    wsh_context **out_context);
void wsh_context_destroy(wsh_context *context);

wsh_result wsh_eval(
    wsh_context *context,
    wsh_string_view source_name,
    wsh_string_view utf8_source,
    const wsh_eval_options *options,
    wsh_status *out_status);
wsh_result wsh_eval_file(
    wsh_context *context,
    wsh_string_view path,
    const wsh_eval_options *options,
    wsh_status *out_status);

wsh_result wsh_request_cancel(wsh_context *context);
wsh_result wsh_diagnostic_next(
    wsh_context *context,
    wsh_diagnostic_view *out);
```

## 4. Contexts

A context owns variables, functions, exported environment, logical current
directory, child registry, descriptor table, limits, diagnostics, and host
commands. Creating a context has no console, profile, configuration, registry,
filesystem, or process side effect unless its options explicitly request it.

Two contexts are isolated. They may execute concurrently on different threads.
One context is not thread-safe for concurrent evaluation. Destruction requires
no active evaluation and performs the documented child cleanup. Cancellation
is the sole function callable concurrently with evaluation.

The host supplies or selects:

- allocator and deallocator;
- stdin, stdout, and stderr byte-stream callbacks;
- logical initial directory and environment;
- resource limits and deadline source;
- filesystem and process permission policy;
- configuration/profile permission;
- clock hooks for deterministic tests; and
- diagnostic callback or pull queue.

Defaults match `wsh.exe` without loading user state.

## 5. Values and Variables

The ABI exposes list construction, element count, indexed UTF-8 access, clone,
and release. Lists are immutable after publication to a context or callback.

Variable operations get, set, unset, enumerate, export, and unexport by exact
case-sensitive shell name. Environment enumeration is a separate
case-insensitive interface. Host operations obey the same validation and
scalar-export rules as script operations.

No pointer returned from a borrowed view remains valid after the owning value,
diagnostic, or context changes. A host clones data it must retain.

## 6. Evaluation

Evaluation options select source encoding, arguments, interactive allowance,
exit-on-error, trace sinks, deadline, and whether a return/exit request may
close the context. They cannot change grammar or core semantics.

`wsh_eval` accepts complete or incrementally supplied source through separate
documented APIs. It returns a structured status list and a result code that
distinguishes successful evaluation from API misuse, cancellation, syntax
failure, policy denial, and host callback failure.

Host callbacks are never invoked after the evaluation call returns. Stream
callbacks may receive arbitrary bytes from external programs and UTF-8 CRLF
text from WSH commands.

## 7. Host Commands

A host may register a synchronous command on one context before evaluation:

```c
wsh_result wsh_register_command(
    wsh_context *context,
    const wsh_command_definition *definition,
    wsh_command_registration **out_registration);
```

Names shall be valid namespaced WSH names; `host::name` is recommended. A
definition includes version, help text, minimum/maximum argument count, opaque
host data, callback, and optional cleanup callback.

The callback receives immutable ordered string arguments, context stream
access, cancellation observation, and an output status builder. It returns one
status list and optional structured diagnostics. It runs on the evaluation
thread and shall return before evaluation continues.

WSH 1.0 host commands are trusted native code. They are not a security
boundary. They shall not recursively enter the same context, asynchronously
retain borrowed values, change registration during evaluation, unload code
while registered, throw language-runtime exceptions across the ABI, or return
before asynchronous work has released context resources.

Registration can be removed only while the context is idle. Removal invokes
cleanup exactly once after no callback can run.

## 8. Policy and Security

The host policy callback receives normalized operation descriptions for file
open/create/delete, process launch, raw launch, environment export, registry
read, profile load, and named-pipe creation. Denial occurs before the operation
and produces a diagnostic. Machine registry policy is a minimum restriction
for the standard Windows host unless the application documents that it is a
separate product boundary.

Inputs from scripts, callbacks, streams, environment envelopes, and registry
are untrusted. Length and resource limits apply before allocation. Diagnostics
shall not copy secret values or unbounded hostile input.

## 9. Version Compatibility

The ABI major changes only for a binary-incompatible contract. New functions,
result codes, capabilities, and trailing structure fields may be added within
ABI 1 when old hosts can ignore them safely. `wsh_abi_version` is callable
without context creation.

The DLL exports a capability bitset and full semantic product version. Hosts
shall test capabilities rather than infer them from the Windows version. Static
and shared builds execute the same conformance tests, including a second-
language FFI smoke host.
