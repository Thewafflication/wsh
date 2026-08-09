# Waughtal Shell 1.0 Registry Contract

**Document ID:** `WSH-SPEC-REG-0001`

**Status:** Proposed

## 1. Scope and Principles

Registry integration is optional. The portable executable shall run correctly
when every WSH registry key is absent or inaccessible. WSH reads settings and
policy but does not self-register, install, or modify the registry during
ordinary execution.

An inaccessible product-settings key is treated as absent with one bounded
warning. An inaccessible policy key fails closed only for policy-controlled
features: profiles, persistent history, raw launch, and implicit current-
directory search are disabled. Core structured batch execution remains
available, making a copied executable usable under a registry-denying sandbox.

An installer or WPM package may create the installation and file-association
keys defined here. Unknown values are ignored for forward compatibility and
reported by `--dump-config` when they occur under a recognized WSH key.

## 2. Product Settings

The versioned settings keys are:

```text
HKEY_LOCAL_MACHINE\Software\Waughtal\WSH\1.0
HKEY_CURRENT_USER\Software\Waughtal\WSH\1.0
```

| Value | Type | Meaning |
| --- | --- | --- |
| `ConfigFile` | `REG_SZ` or `REG_EXPAND_SZ` | Replaces the default configuration path for that scope |
| `ProfileFile` | `REG_SZ` or `REG_EXPAND_SZ` | Replaces the default profile path for that scope |
| `HistoryFile` | `REG_SZ` or `REG_EXPAND_SZ` | Replaces the default user history path; ignored at machine scope |
| `InstallDirectory` | `REG_SZ` | Informative absolute installation directory |
| `CurrentVersion` | `REG_SZ` | Informative semantic product version |

`REG_EXPAND_SZ` expands against the startup environment once. Undefined
references are errors. Embedded U+0000, relative paths, and values exceeding
32,767 UTF-16 code units are rejected.

User values override machine values only for the corresponding user-scoped
selection. They never override policy.

## 3. Policy

Policy keys are:

```text
HKEY_LOCAL_MACHINE\Software\Policies\Waughtal\WSH
HKEY_CURRENT_USER\Software\Policies\Waughtal\WSH
```

| Value | Type | Values | Effect |
| --- | --- | --- | --- |
| `DisableUserConfig` | `REG_DWORD` | 0 or 1 | Ignore user and `WSH_CONFIG` data files |
| `DisableProfiles` | `REG_DWORD` | 0 or 1 | Forbid every executable profile |
| `DisableHistory` | `REG_DWORD` | 0 or 1 | Forbid history loading and persistence |
| `DisableRawExec` | `REG_DWORD` | 0 or 1 | Forbid `rawexec` and equivalent embedding calls |
| `SafePath` | `REG_DWORD` | 0 or 1 | Disable implicit current-directory executable search |
| `MaximumHistoryEntries` | `REG_DWORD` | 0--100000 | Upper bound on retained entries |
| `AllowedConfigRoot` | `REG_SZ` or `REG_EXPAND_SZ` | absolute path | Require every selected config/profile path to remain under this root |

Machine and user policy combine restrictively: a disable value of 1 in either
scope applies; `SafePath` applies if enabled in either; numeric maxima use the
smaller nonzero bound; allowed roots must both contain the selected path.
Malformed policy fails closed for the affected capability and produces a
configuration diagnostic.

Policy is evaluated for interactive, batch, nested, and embedded execution.
An embedding host may impose stricter policy but cannot weaken machine policy.

## 4. Registry Views

On a 64-bit operating system, all WSH architectures read and write the native
64-bit product and policy view. The x86 build explicitly opens that view when
the operating system supports it. On a 32-bit operating system, it uses the
only available view. WSH shall not merge 32-bit and 64-bit redirected product
keys.

The implementation detects capability before using WOW64 registry flags so the
same x86 binary remains valid on Windows 2000. Registry strings are read with
explicit lengths, termination is validated, and expandable values are bounded
before and after expansion.

## 5. Application Paths

An installed package may register:

```text
HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\App Paths\wsh.exe
```

The unnamed `REG_SZ` value is the absolute path to `wsh.exe`. Optional `Path`
is the installation directory only. WSH itself does not use App Paths for
external command discovery and does not invoke `ShellExecute`.

## 6. Script File Association

An installer may create the following under either
`HKEY_LOCAL_MACHINE\Software\Classes` or
`HKEY_CURRENT_USER\Software\Classes`:

```text
.wsh                                  = Waughtal.WSH.Script.1
Waughtal.WSH.Script.1                 = Waughtal Shell Script
Waughtal.WSH.Script.1\DefaultIcon     = "<wsh.exe>",0
Waughtal.WSH.Script.1\shell\open\command
                                      = "<wsh.exe>" "%1" %*
```

The `.wsh` key should also contain `Content Type` = `text/plain` and
`PerceivedType` = `text`. Paths in command values shall use Windows command-line
quoting appropriate to registry associations.

Association is a user convenience, not language execution. WSH never consults
the association when resolving a command. Double-click execution is
non-interactive unless a console is supplied by the launching environment, and
scripts shall not rely on a transient window remaining visible.

## 7. Uninstallation and Ownership

Installer-owned keys shall be recorded by the installer and removed only when
their current values still belong to that installation. A portable copy shall
not remove keys belonging to another copy. User configuration, profiles, and
history are retained by default unless an uninstall operation explicitly asks
the user to remove them.
