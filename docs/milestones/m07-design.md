# M7 Design — Executable-Owned Interactive Session

**Status:** Reviewed for implementation

**Date:** 2026-08-24

## Session Boundary

`wsh.exe` owns an interactive session object. It borrows the isolated context,
evaluator, and Windows runtime but is not linked into the portable core or the
M8 host surface. The front end retains complete-source parsing and recovery;
the interactive reader supplies one complete submitted command after handling
physical lines, editing, and parser-incomplete continuation internally.

The evaluator exposes read-only function enumeration and bounded signal
function invocation. The Windows runtime exposes atomic foreground interrupt,
background count, and explicit cancel-all operations. Neither interface
changes script grammar or batch dispatch.

## Console Editor

Advanced input snapshots the caller's input mode, disables cooked echo/line
processing while editing, and consumes `INPUT_RECORD` values through APIs
available on Windows 2000. Text is stored as bounded UTF-16. Movement and
deletion treat surrogate pairs as one scalar and reject invalid sequences at
submission. Line boundaries remain LF internally.

Every redraw re-queries buffer width, clears only the cells previously owned
by the editor, writes prompts/text with `WriteConsoleW`, and restores the
logical cursor. Resize and focus records have no semantic effect. If both
console input-record and console output editing cannot be initialized, the
session writes one diagnostic and uses `ReadConsoleW` cooked-line input with
the same parser completeness rules.

The two prompt elements are fetched from `$prompt` before each new command.
They are copied and written literally; they are never parsed or expanded.
Missing elements use `% ` and `; `, and excess elements produce one bounded
diagnostic per observed assignment.

## Completion

Completion classifies the token at the cursor as command, variable, or path
position without evaluating it. Sources are persistent function names,
built-ins, the immutable library registry, `.wsh` files, `.exe`/`.com`
resolution directories, variables, and filesystem entries. Candidates are
deduplicated and sorted by Windows ordinal semantics with an exact-byte
tiebreak.

The first Tab inserts the longest common extension. Repeated Tab or Shift+Tab
cycles deterministically. Insertions use apostrophe quotation when a literal
would otherwise split or form syntax, and apostrophes are doubled. Relative
completion never probes a network path; a UNC enumeration begins only after
the user typed a UNC prefix.

## History

History state is bounded by 5,000 entries and 4 MiB using the accepted compiled
defaults. It loads only for an interactive session after profile evaluation.
The parser requires the version header and accepts only JSON strings with
strict UTF-8; unknown properties are skipped, while malformed/oversized records
produce one coalesced warning and are never evaluated.

One path-derived named mutex is retained for the session. Failure to acquire it
leaves reading enabled but disables writes with a warning. Persistence writes
a unique same-directory file with exclusive creation, flushes it, and uses an
atomic replace operation. Failure leaves the prior history file intact.
Adjacent duplicates are suppressed. `history::suppress` affects only the
currently evaluated submission; list results remain exact values.

## Interruption, Signals, and Exit

The console control handler performs no allocation or evaluation. It sets one
atomic runtime request. Foreground wait observes that request at bounded
intervals, requests the existing group cancellation policy, collects every
stage, and publishes status 130. Only after cleanup does the front end invoke
`sigint` in the shell context.

Ctrl+C while editing clears unpublished input, publishes status 130, invokes
`sigint`, and begins a fresh prompt. Normal interactive exit with background
jobs requires console confirmation; `exit --force` cancels and collects them.
An empty EOF refuses once when jobs remain and the next consecutive EOF
cancels and exits. Orderly shutdown invokes `sigexit` once.

## Compatibility and Failure

The implementation statically imports only APIs present on the oldest target;
optional behavior uses existing dynamic fallbacks. Console-output failure is
fatal. History failure and unavailable advanced editing are visible but
nonfatal. Resource, encoding, and syntax failures discard only the affected
pending command and preserve the session unless the accepted fatal condition
applies.

Plan 9 `rc` supplies prompts, signal functions, and interactive intent but not
Windows key records, JSONL persistence, or completion. M7 implements the
accepted Windows adaptation and changes no existing script meaning.
