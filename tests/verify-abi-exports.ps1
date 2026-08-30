<#
.SYNOPSIS
Verify the shared embedding library exports every ABI 1 public symbol.

.DESCRIPTION
Reads the export definition written beside the shared library and asserts that
the exported set is exactly the approved ABI 1 manifest: every manifest symbol
is present (nothing missing) and no other symbol is exported (nothing extra).
This enforces both halves of the M8 export exit gate, keeping runtime, CRT, and
internal WSH symbols out of the public surface.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SharedLibrary,

    [Parameter(Mandatory = $true)]
    [string] $Manifest
)

$ErrorActionPreference = 'Stop'

$definition = [System.IO.Path]::ChangeExtension($SharedLibrary, '.def')
if (-not (Test-Path -LiteralPath $definition)) {
    Write-Error "Export definition not found beside library: $definition"
    exit 1
}

$exported = Get-Content -LiteralPath $definition |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and $_ -notmatch '^(LIBRARY|EXPORTS)\b' }

$required = Get-Content -LiteralPath $Manifest |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and -not $_.StartsWith('#') }

$missing = @($required | Where-Object { $exported -notcontains $_ })
if ($missing.Count -gt 0) {
    Write-Error ("Missing ABI 1 exports: " + ($missing -join ', '))
    exit 1
}

$unexpected = @($exported | Where-Object { $required -notcontains $_ })
if ($unexpected.Count -gt 0) {
    Write-Error ("Unexpected non-ABI exports: " + ($unexpected -join ', '))
    exit 1
}

Write-Output (
    "verify-abi-exports: exported set is exactly the {0} ABI 1 symbols" -f `
        $required.Count)
exit 0
