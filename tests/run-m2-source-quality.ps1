[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Doxygen
)

$ErrorActionPreference = 'Stop'
$tool = Join-Path $RepositoryRoot 'wsp\tools\Test-CSourceQuality.ps1'
$output = Join-Path $RepositoryRoot 'out\doxygen\m2'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $tool `
    -RepositoryRoot $RepositoryRoot `
    -SourcePath @(
        'include/wsh/core.h',
        'src/portable_core.c',
        'tests/portable_core_tests.c') `
    -Doxyfile 'docs/tests/m2/Doxyfile' `
    -Doxygen $Doxygen
exit $LASTEXITCODE
