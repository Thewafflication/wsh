[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Doxygen
)

$ErrorActionPreference = 'Stop'
$tool = Join-Path $RepositoryRoot 'wsp\tools\Test-CSourceQuality.ps1'
$output = Join-Path $RepositoryRoot 'out\doxygen\m5'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $tool `
    -RepositoryRoot $RepositoryRoot `
    -SourcePath @(
        'include/wsh/core.h',
        'include/wsh/windows_runtime.h',
        'src/evaluator.c',
        'src/main.c',
        'src/platform/windows/windows_runtime.c',
        'tests/helpers/runtime_probe.c',
        'tests/windows_runtime_tests.c') `
    -Doxyfile 'docs/tests/m5/Doxyfile' `
    -Doxygen $Doxygen
exit $LASTEXITCODE
