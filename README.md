# Waughtal Shell

Waughtal Shell (`wsh`) is a Windows-native command shell inspired by Tom
Duff's Plan 9 `rc` shell.

The project aims to preserve the small, regular ideas that distinguish
`rc`—list-valued variables, apostrophe quoting, caret concatenation,
functions, structured commands, pipelines, and ordered redirection—while
defining explicit native behavior for Windows processes, paths, environment
variables, consoles, and handles.

The implementation is currently in the planning and project-baselining stage.

## Project Goals

- Target Windows without requiring a POSIX compatibility layer.
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

## Planned Development

The proposed roadmap is:

1. Establish the WSP, dependency, build, and test-evidence baseline.
2. Implement and test the lexer and parser.
3. Add variables, built-ins, external command execution, status handling, and
   redirection.
4. Add structured control flow, functions, pipelines, command substitution,
   and globbing.
5. Add an interactive prompt, history, and Windows console interruption.
6. Complete the documented 1.0 compatibility and release baseline.

See the [project plan], [draft requirements], and [architecture decisions] for
details.

## Proposed Toolchain

Dependencies will be installed through WPM and pinned before the first release
baseline:

```powershell
wpm install wcrt --arch <x86|x64|arm64> --version <version>
wpm install kertex --arch <architecture> --version <version>
wpm install cv2pdb --arch <architecture> --version <version>
```

The exact configure, build, and test commands will be added with the initial
CMake skeleton. CTest will remain the top-level test dispatcher.

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
[draft requirements]: docs/requirements/product-requirements.md
[architecture decisions]: docs/adr-0001-rc-inspired-windows-language.md
[adoption record]: docs/adoption-record.md
[rc-manual]: https://9fans.github.io/plan9port/man/man1/rc.html
