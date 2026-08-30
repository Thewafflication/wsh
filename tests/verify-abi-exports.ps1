<#
.SYNOPSIS
Verify the shared embedding library exports every ABI 1 public symbol.

.DESCRIPTION
Reads the export definition written beside the shared library and asserts that
every symbol in the approved ABI 1 manifest is exported. This is the positive
half of the M8 exit gate: the shared surface must contain the whole public ABI.
Restricting the export set to only the manifest is tracked separately.
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

Write-Output ("verify-abi-exports: all {0} ABI 1 symbols exported" -f $required.Count)
exit 0
