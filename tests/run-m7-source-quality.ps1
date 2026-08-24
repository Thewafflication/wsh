[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Doxygen
)

$ErrorActionPreference = 'Stop'
$tool = Join-Path $RepositoryRoot 'wsp\tools\Test-CSourceQuality.ps1'
$output = Join-Path $RepositoryRoot 'out\doxygen\m7'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $tool `
    -RepositoryRoot $RepositoryRoot `
    -SourcePath @(
        'include/wsh/evaluator.h',
        'include/wsh/windows_runtime.h',
        'src/evaluator.c',
        'src/frontend.c',
        'src/frontend.h',
        'src/interactive.c',
        'src/interactive.h',
        'src/main.c',
        'src/platform/windows/windows_runtime.c',
        'tests/helpers/console_driver.c') `
    -Doxyfile 'docs/tests/m7/Doxyfile' `
    -Doxygen $Doxygen
exit $LASTEXITCODE
