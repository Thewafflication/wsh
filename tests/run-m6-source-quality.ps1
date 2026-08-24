[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Doxygen
)

$ErrorActionPreference = 'Stop'
$tool = Join-Path $RepositoryRoot 'wsp\tools\Test-CSourceQuality.ps1'
$output = Join-Path $RepositoryRoot 'out\doxygen\m6'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $tool `
    -RepositoryRoot $RepositoryRoot `
    -SourcePath @(
        'include/wsh/core.h',
        'include/wsh/windows_runtime.h',
        'src/evaluator.c',
        'src/parser.c',
        'src/platform/windows/windows_runtime.c',
        'src/sha256.c',
        'src/sha256.h',
        'src/standard_library.c',
        'src/standard_library.h') `
    -Doxyfile 'docs/tests/m6/Doxyfile' `
    -Doxygen $Doxygen
exit $LASTEXITCODE
