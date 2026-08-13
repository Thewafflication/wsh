# M3 Design — Portable Lexer, Parser, and Immutable AST

**Status:** Reviewed for implementation

**Date:** 2026-08-12

## Boundary

`wsh_lex` and `wsh_parse` accept an immutable decoded `wsh_source`, an injected
allocator, and parser limits. They have no context, runtime callback, path,
environment, registry, console, stream, clock, or process parameter. Their
only observable effects are allocation through the supplied allocator and
publication of owned immutable results.

## Interfaces and Ownership

- `wsh_token_stream` owns copied token spelling, decoded quoted text,
  here-document bodies, diagnostics, and token storage.
- `wsh_parse_tree` owns every AST node in a private arena, node text,
  diagnostics, and child-pointer arrays.
- Token and AST views are borrowed and immutable. Destroying their owning
  stream/tree invalidates the views.
- Syntax and lexical rejection are successful analyses with `ERROR` status;
  syntactic continuation needs use `INCOMPLETE`. Allocation/resource failures
  return a `wsh_result` error and publish no owner.
- A complete tree is published only when lexing and parsing both complete and
  consume EOF. Error and incomplete trees contain diagnostics but no root.

## Lexer

The lexer consumes M2-normalized strict UTF-8. ASCII grammar bytes are tested
directly; non-ASCII bytes remain ordinary word bytes and never consult locale
or a mutable Unicode whitespace table. It:

- treats space, tab, form feed, and normalized newline as separators;
- removes comments but retains their newline separator;
- decodes apostrophe quotation and doubled apostrophes;
- recognizes compound operators before their single-byte prefixes;
- recognizes variable, count, and flatten prefixes with ASCII names;
- leaves backslash, slash, colon, period, hyphen, and square brackets in words;
- retains exact half-open source spans; and
- identifies here-document markers, captures body text without tokenizing it,
  and resumes after the exact terminating line.

Free carets are not materialized by byte rewriting. The parser constructs the
same concatenation node when two argument-producing forms have adjacent spans.
This retains whether an explicit caret existed in the token stream while
giving explicit and free carets identical AST semantics.

## Parser and AST

Recursive descent follows the normative EBNF. Command lists contain ordered
children; background separators wrap the preceding command. Conditional and
pipeline operators associate left-to-right. Unary prefixes wrap in source
order. Simple-command children preserve assignments, arguments, and
redirections exactly in input order.

The AST distinguishes command lists, background commands, conditionals,
pipelines and pipe decorations, unary operations, simple commands, blocks,
if/while/for/switch/case/function forms, assignments, redirections,
here-document bodies, words, quoted words, lists, variables, subscripts,
count/flatten forms, command/process substitutions, and concatenation.

Each node has a normalized UTF-8 source span, optional primary/auxiliary text,
and ordered immutable children. The inspection formatter emits a canonical
escaped S-expression using normalized offsets, so equivalent UTF-8, UTF-16LE,
UTF-16BE, CRLF, LF, and CR sources compare deterministically.

## Incomplete Input and Recovery

EOF while a quote, parenthesis, brace, here document, required operand,
operator right side, or compound-command body remains open is `INCOMPLETE`.
An unexpected token where continuation cannot repair the construct is
`ERROR`. M3 stops at the first diagnostic and never returns a partial root;
later milestones may add bounded interactive recovery without changing this
failure-atomic batch contract.

## Limits and Failure Behavior

Defaults bound tokens, AST nodes, recursive parse depth, and retained syntax
diagnostics. Recursive depth has a portable hard maximum of 512; callers can
only lower it. Unary prefixes are consumed iteratively and also count against
that depth, preventing a prefix-only C-stack exhaustion path. Every growth
calculation checks overflow and the applicable
ceiling before allocating. Arrays use allocate-copy-commit rather than
`realloc`. A tree-owned arena makes partial graph teardown independent of
attachment state and keeps allocation failure idempotent.

## Compatibility and Security Review

Plan 9 `rc` lexical/list/control constructs are represented without runtime
behavior. Windows adaptations remain explicit: backslash is ordinary, source
encodings are handled by M2, process substitution is a future named-pipe seam,
and descriptor decorations remain numeric text until M5 validation. This is
compatible with all targeted Windows versions because the implementation is
portable C99 and calls no OS API.

The design enforces the DFS malicious-source boundary before evaluation:
bounded input graphs, no partial AST publication, deterministic diagnostics,
no execution capability, and fault-atomic cleanup. No new dependency or
durable architecture decision is introduced, so ADR-0002 remains sufficient.
