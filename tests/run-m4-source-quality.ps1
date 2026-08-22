[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Doxygen
)

$ErrorActionPreference = 'Stop'
$tool = Join-Path $RepositoryRoot 'wsp\tools\Test-CSourceQuality.ps1'
$output = Join-Path $RepositoryRoot 'out\doxygen\m4'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $tool `
    -RepositoryRoot $RepositoryRoot `
    -SourcePath @(
        'include/wsh/evaluator.h',
        'src/evaluator.c',
        'tests/evaluator_tests.c') `
    -Doxyfile 'docs/tests/m4/Doxyfile' `
    -Doxygen $Doxygen
exit $LASTEXITCODE
