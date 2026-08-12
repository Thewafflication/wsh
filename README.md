# Waughtal Shell

Waughtal Shell (`wsh`) is a Windows-native command shell inspired by Tom
Duff's Plan 9 `rc` shell.

The project aims to preserve the small, regular ideas that distinguish
`rc`—list-valued variables, apostrophe quoting, caret concatenation,
functions, structured commands, pipelines, and ordered redirection—while
defining explicit native behavior for Windows processes, paths, environment
variables, consoles, and handles.

The 1.0 requirements, specification, architecture, and project process
completed the M0 baseline review and form the accepted project baseline. The
M1 repository skeleton and M2 portable core are implemented. Lexer, parser,
evaluator, and Windows process behavior remain allocated to later milestones.

## Project Goals

- Target Windows without requiring a POSIX compatibility layer.
- Ship one portable, statically WCRT-linked binary per architecture.
- Run the same x86 binary from Windows 2000 through current Windows.
- Support both interactive use and deterministic batch automation.
- Implement the shell in portable C99.
- Keep language parsing, evaluation, and Win32 integration separate.
- Configure and build the project with CMake presets.
- Dispatch all automated tests through CTest.
- Generate traceable TeX test evidence for release reports.
- Provision WCRT, KerTeX, and cv2pdb through WPM.
- Support x86, x64, and ARM64, with test claims backed by execution on the
  corresponding architecture.

Waughtal Shell is inspired by `rc`; it does not initially promise that every
Plan 9 `rc` script will run unchanged. Platform-dependent behavior and
intentional differences will be documented and tested.

## Specification

The proposed 1.0 contract defines:

- the complete `rc`-inspired language and Windows adaptations;
- command-line modes and options;
- inert configuration, executable profiles, and optional registry integration;
- an embedded filesystem/process/test standard library;
- interactive editing, history, completion, and interruption; and
- a stable static/shared C embedding API.

See the [specification index], [product requirements], [project plan], and
[milestone plan]. The [`rc` compatibility record] identifies every adopted,
adapted, extended, or excluded reference behavior.

## Build and Test

The checked-in presets build the executable, static/shared library skeletons,
portable core, and controlled tests. For example:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug --output-on-failure
```

The same test dispatcher has x64 Release and x86 Debug presets. M2 test runs
write isolated TeX evidence beneath each build tree and validate it through
the pinned WSP tools.

## Planned Release Toolchain

Dependencies will be installed through WPM and pinned before the first release
baseline:

```powershell
wpm install wcrt --arch <x86|x64|arm64> --version <version>
wpm install kertex --arch <architecture> --version <version>
wpm install cv2pdb --arch <architecture> --version <version>
```

Release dependency pinning and the remaining cross-version matrix are tracked
as historical M1 evidence gaps and later hardening work. CTest remains the
top-level test dispatcher.

## Project Process

The repository adopts the Waughtal Software Process through the `wsp/` Git
submodule. Requirements, architecture decisions, tests, evidence, and release
artifacts are intended to remain bidirectionally traceable.

See the proposed [adoption record] for the selected WSP profiles and initial
tailoring decisions.

## References

- [Rc — The Plan 9 Shell](https://doc.cat-v.org/plan_9/4th_edition/papers/rc),
  by Tom Duff
- [Plan 9 from User Space rc(1)][rc-manual]

## License

Waughtal Shell is free software licensed under the GNU General Public License,
version 3 or, at your option, any later version. See [LICENSE](LICENSE).

Project-owned source files should use:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
```

[project plan]: docs/project-plan.md
[product requirements]: docs/requirements/product-requirements.md
[specification index]: docs/specification/README.md
[milestone plan]: docs/planning/milestones.md
[rc compatibility record]: docs/specification/rc-compatibility.md
[architecture decisions]: docs/adr-0001-rc-inspired-windows-language.md
[adoption record]: docs/adoption-record.md
[rc-manual]: https://9fans.github.io/plan9port/man/man1/rc.html
