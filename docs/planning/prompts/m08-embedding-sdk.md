# Prompt — M8 Embedding SDK

**Token budget:** 130,000 (baseline/plan 16k; specify 14k; design 20k;
implement 43k; review 14k; verify 20k; close 3k)

```text
Complete WSH milestone M8 using the mandatory WSP milestone workflow.

Objective: freeze and deliver embedding ABI 1 for static and shared hosts.

Review every public type/function contract before freezing names. Specify tests
for ABI/version queries, sized structures, opaque handles, allocators, UTF-8
views, context isolation, streams, evaluation, status/diagnostics, variables,
policy, concurrent cancellation, destruction, synchronous host commands,
registration lifetime, callback failure, forbidden reentry, and resource
limits.

Build wshlib static and DLL artifacts with WCRT linkage as specified. Provide a
C host and one second-language FFI host that register host:: commands and run
the shared conformance suite. Verify exported symbols, calling convention,
version resources, import dependencies, memory ownership, and static/shared/
executable behavioral equality.

Exit with reviewed public documentation/examples, ABI misuse/security evidence,
and actual token data. Any incompatible public change after exit requires the
documented ABI/version process.
```
