# Waughtal Shell 1.0 Specification Set

**Document ID:** `WSH-SPEC-INDEX`

**Status:** Accepted

**Target release:** 1.0

## Purpose

This index defines the proposed Waughtal Shell (`wsh`) 1.0 contract. The
documents are normative unless a section is explicitly labeled informative.
Implementation shall not begin for a milestone until the requirements and
specification sections allocated to that milestone have completed WSP review.

## Normative Documents

| Document | Scope |
| --- | --- |
| [Language](language.md) | Grammar, values, evaluation, control flow, status, redirection, and pipelines |
| [Command-line interface](command-line.md) | Process invocation modes, options, exit codes, help, and version output |
| [Configuration](configuration.md) | Data configuration, profiles, locations, syntax, and precedence |
| [Registry](registry.md) | Optional settings, policy, installation discovery, and file association keys |
| [Standard library](standard-library.md) | Embedded commands for files, paths, processes, text, time, system information, and tests |
| [Interactive shell](interactive-shell.md) | Console editing, history, completion, prompts, jobs, and interruption |
| [Embedding API](embedding-api.md) | Stable C ABI for static and shared-library hosts |
| [Platform compatibility](platform-compatibility.md) | Windows versions, architectures, WCRT, Unicode, console, paths, and process behavior |
| [`rc` compatibility](rc-compatibility.md) | Feature-by-feature source, disposition, and Windows rationale |

The [product requirements](../requirements/product-requirements.md) state the
stakeholder and system obligations. The specification defines the behavior
needed to satisfy them. If the two conflict, the conflict shall be resolved by
requirements change review rather than by silently choosing one document.

## Normative Language

The terms *shall*, *should*, and *may* have the meanings defined by WSP
documentation style. Code examples are informative unless a normative clause
explicitly incorporates them.

## Compatibility Method

Every language feature derived from Plan 9 `rc` has one disposition:

- **Adopted** — WSH preserves the relevant observable behavior.
- **Adapted** — WSH preserves the purpose but defines Windows-native behavior.
- **Extended** — WSH adds behavior without claiming it exists in `rc`.
- **Excluded** — WSH intentionally provides no analogue.

The principal references are the [Plan 9 from User Space `rc(1)` manual][rc]
and Tom Duff's [`Rc — The Plan 9 Shell`][paper]. WSH references and annotates
those sources; it does not reproduce them as its own specification.

## Stability

The 1.0 grammar and observable semantics become compatibility commitments when
the specification is accepted. Configuration, registry policy, host APIs, or
newer Windows capabilities shall not silently alter existing script meaning.
An incompatible change requires requirements impact analysis, an ADR, a major
version change, and migration documentation.

[paper]: https://doc.cat-v.org/plan_9/4th_edition/papers/rc
[rc]: https://9fans.github.io/plan9port/man/man1/rc.html
