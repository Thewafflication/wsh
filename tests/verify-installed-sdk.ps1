<#
.SYNOPSIS
Verify the installed embedding SDK is consumable by an external host.

.DESCRIPTION
Installs the project to a fresh staging prefix, then compiles the C host
example against only the installed public headers, links the installed import
definition, and runs the result with the installed DLL on PATH. This proves the
shipped SDK layout (headers, import library, DLL) is self-contained and uses no
in-tree path or internal type.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Compiler,
    [Parameter(Mandatory = $true)] [string] $BuildDir,
    [Parameter(Mandatory = $true)] [string] $Config,
    [Parameter(Mandatory = $true)] [string] $Source,
    [Parameter(Mandatory = $true)] [string] $StageDir
)

$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}

& cmake --install $BuildDir --prefix $StageDir --config $Config | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Error "SDK install failed"
    exit 1
}

$include = Join-Path $StageDir 'include'
$importDef = Join-Path $StageDir 'lib/wshlib.def'
$binDir = Join-Path $StageDir 'bin'
$exe = Join-Path $StageDir 'host_installed.exe'

foreach ($required in @($include, $importDef, (Join-Path $binDir 'wshlib.dll'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        Write-Error "Installed SDK is missing: $required"
        exit 1
    }
}

& $Compiler $Source "-I$include" $importDef '-o' $exe
if ($LASTEXITCODE -ne 0) {
    Write-Error "Host compilation against the installed SDK failed"
    exit 1
}

$previousPath = $env:PATH
try {
    $env:PATH = "$binDir;$previousPath"
    $output = & $exe
    $runExit = $LASTEXITCODE
}
finally {
    $env:PATH = $previousPath
}

if ($runExit -ne 0 -or ($output -notcontains 'host: ok')) {
    Write-Error ("Installed host did not complete: exit=$runExit output=" + ($output -join '|'))
    exit 1
}

Write-Output 'verify-installed-sdk: host built and ran against the installed SDK'
exit 0
