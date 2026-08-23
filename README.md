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
M1 repository skeleton, M2 portable core, M3 lexer/parser, M4 language
evaluator, and M5 Windows execution runtime are implemented.

## Install

With [WPM] installed, open an elevated PowerShell session, add the latest WSH
release as a package repository, and install the package for the current
Windows architecture:

```powershell
Invoke-WebRequest `
  https://github.com/Thewafflication/wsh/releases/latest/download/wpm-release.public `
  -OutFile wpm-release.public
wpm trust add wpm-release.public
wpm repo add https://github.com/Thewafflication/wsh/releases/latest/download
wpm update
wpm install wsh
```

Use `--arch x86`, `--arch x64`, or `--arch arm64` to select an architecture
explicitly. The package installs WSH beneath `%ProgramFiles%` and sets the
machine-level `WSH_HOME` variable. Open a new terminal and verify the install:

```powershell
& "$env:WSH_HOME\wsh.exe" --version
```

Release packages are signed with the published WPM release key and accompanied
by `SHA256SUMS` on the [latest release].

## Interactive Input

Run `wsh.exe` with no source operand to read standard input. When standard
input is a Windows console, WSH displays `% `, uses `; ` while a command is
syntactically incomplete, and returns to the primary prompt after a complete
command or a recoverable syntax error. Press Ctrl+Z and then Enter at an empty
prompt to leave the session.

Use `--interactive` (`-i`) to require console input or `--non-interactive`
(`-I`) to suppress interactive behavior. Redirected standard input is selected
as batch input automatically and never receives prompts.

The front end evaluates each complete immutable parse tree. Variables, lists,
expansion, functions, structured control flow, `source`, `eval`, status,
pattern matching, `echo`, external commands, globbing, redirection, pipelines,
background jobs, and process substitution are active. Child processes launch
directly through `CreateProcessW`; WSH does not invoke a command interpreter,
file association, App Paths entry, or `PATHEXT` search implicitly.

Use `-c` for one command or pass a script and its arguments directly:

```powershell
wsh -c "echo 'hello from wsh'"
wsh build.wsh debug x64
```

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

The primary presets use WPM-installed TinyCC and WCRT packages. From an
elevated PowerShell session, install the project-pinned build dependencies for
the target architecture:

```powershell
./tools/Install-BuildDependencies.ps1 -Architecture x64
```

Replace `x64` with `x86` or `arm64` as needed. The toolchain finds the pinned
dependencies beneath `%ProgramFiles%` and falls back to `TCC_HOME` and
`WCRT_HOME` for custom installation roots. CMake 3.20 or newer and Ninja are
also required. The checked-in presets build the executable, static/shared
library skeletons, portable core, and controlled tests. For example:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug --output-on-failure
```

The same test dispatcher has Debug and Release presets for x86, x64, and
ARM64. Test runs write isolated TeX evidence beneath each build tree and
validate it through the pinned WSP tools.

The x86, x64, and ARM64 presets select TinyCC's architecture-named compiler,
WCRT's matching headers, static library, and console startup object. Executable
links omit the host CRT and retain only the matching WCRT and TinyCC compiler-
support inputs. This is the only supported build path.

GitHub Actions installs the pinned TinyCC/WCRT baseline and runs its warning,
source-lint, and traceability gates before starting the three-architecture
Debug matrix. Each Debug job executes CTest, publishes a
per-test job summary, and retains one downloadable ZIP containing the build
log, JUnit and CTest results, controlled test evidence, exact binaries and
symbols, checksums, and an unsigned architecture-specific Debug WPM package.
ARM64 tests execute on a native ARM64 runner.

Semantic-version tags start Release builds only after all Debug jobs pass.
Successful x86, x64, and ARM64 Release builds become WPM packages in the
corresponding GitHub release. WPM clients consume the published `index.json`;
the release also includes an identical `repository.json` compatibility asset.

## Release Toolchain

CI provisions the project-pinned TinyCC and WCRT packages through WPM before
every architecture build. The dependency script verifies the durable WPM
release key before installing either package. KerTeX and cv2pdb remain separate
documentation and symbol-tool dependencies. CTest remains the top-level test
dispatcher.

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
[WPM]: https://github.com/Thewafflication/wpm
[latest release]: https://github.com/Thewafflication/wsh/releases/latest
