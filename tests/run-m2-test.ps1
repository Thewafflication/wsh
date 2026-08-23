[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TestCase,
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Configuration,
    [Parameter(Mandatory)][string]$Toolchain,
    [Parameter(Mandatory)][string]$TargetArchitecture
)

$ErrorActionPreference = 'Stop'
trap {
    [Console]::Error.WriteLine($_.ToString())
    exit 1
}
. (Join-Path $PSScriptRoot 'Invoke-CapturedProcess.ps1')

function ConvertTo-TexText {
    param([AllowEmptyString()][string]$Value)

    if ($null -eq $Value) {
        return ''
    }
    $builder = [Text.StringBuilder]::new()
    foreach ($character in $Value.ToCharArray()) {
        $escaped = switch ($character) {
            '\' { '\textbackslash{}' }
            '&' { '\&' }
            '%' { '\%' }
            '$' { '\$' }
            '#' { '\#' }
            '_' { '\_' }
            '{' { '\{' }
            '}' { '\}' }
            '^' { '\textasciicircum{}' }
            '~' { '\textasciitilde{}' }
            default { $character }
        }
        [void]$builder.Append($escaped)
    }
    return $builder.ToString()
}

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$resolvedRepository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$currentRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
$evidenceName = $TestCase.ToLowerInvariant() + '-execution-evidence.tex'
$evidencePath = Join-Path $currentRoot $evidenceName
$archiveRoot = Join-Path (Split-Path -Parent $currentRoot) 'archive'
$testDigits = $TestCase.Substring(3)
$specifications = @(Get-ChildItem -LiteralPath (
    Join-Path $resolvedRepository 'docs\tests\m2') -File |
    Where-Object { $_.Name -like "tc-$testDigits-*.tex" })
if ($specifications.Count -ne 1) {
    throw "Expected one specification for $TestCase; found " +
        $specifications.Count
}
$specification = $specifications[0]

New-Item -ItemType Directory -Force -Path $currentRoot | Out-Null
if (Test-Path -LiteralPath $evidencePath) {
    New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
    $archiveName = '{0}-{1}-{2}' -f (
        [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')),
        ([guid]::NewGuid().ToString('N')),
        $evidenceName
    Move-Item -LiteralPath $evidencePath -Destination (
        Join-Path $archiveRoot $archiveName)
}

$start = [DateTime]::UtcNow
$nativeResult = Invoke-CapturedProcess `
    -FilePath $resolvedExecutable `
    -ArgumentList @($TestCase)
$captured = @($nativeResult.Output)
$exitCode = $nativeResult.ExitCode
$finish = [DateTime]::UtcNow
$status = if ($exitCode -eq 0) { 'Pass' } else { 'Fail' }
$revision = (& git -C $resolvedRepository rev-parse HEAD 2>$null).Trim()
if (-not $revision) {
    $revision = 'Unavailable'
}
$dirty = @(& git -C $resolvedRepository status --short 2>$null)
if ($dirty.Count -gt 0) {
    $revision += ' (working tree modified)'
}
$specificationHash = (Get-FileHash -LiteralPath $specification.FullName `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$artifactHash = (Get-FileHash -LiteralPath $resolvedExecutable `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceInputs = @(
    'CMakeLists.txt',
    'CMakePresets.json',
    'include/wsh/core.h',
    'src/portable_core.c',
    'tests/CMakeLists.txt',
    'tests/portable_core_tests.c',
    'tests/Invoke-CapturedProcess.ps1',
    'tests/run-m2-test.ps1',
    "tests/m2/run-tc-$testDigits-m2.ps1",
    "docs/tests/m2/$($specification.Name)")
$manifestLines = @($sourceInputs | Sort-Object | ForEach-Object {
    $inputPath = Join-Path $resolvedRepository $_
    $inputHash = (Get-FileHash -LiteralPath $inputPath `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    ('{0}  {1}' -f $inputHash, $_.Replace('\', '/'))
})
$manifestBytes = [Text.Encoding]::UTF8.GetBytes(
    ($manifestLines -join "`n"))
$manifestHash = [Convert]::ToHexString(
    [Security.Cryptography.SHA256]::HashData($manifestBytes)
).ToLowerInvariant()
$runnerArchitecture =
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
$operatingSystem = [Runtime.InteropServices.RuntimeInformation]::OSDescription
$commandText = '"{0}" {1}' -f $resolvedExecutable, $TestCase
$outputText = $captured -join "`n"
if (-not $outputText) {
    $outputText = '(no output)'
}

$evidence = @"
\subsection*{Execution Evidence: $(ConvertTo-TexText $TestCase)}
\begin{description}
\item[Test Case] $(ConvertTo-TexText $TestCase)
\item[Test Specification] $(ConvertTo-TexText $specification.Name)
\item[Test Specification SHA-256]
$(ConvertTo-TexText $specificationHash)
\item[Tested Artifact SHA-256] $(ConvertTo-TexText $artifactHash)
\item[Source Input Manifest SHA-256] $(ConvertTo-TexText $manifestHash)
\item[Requirement References] WSH-REQ-$testDigits
\item[Source Revision] $(ConvertTo-TexText $revision)
\item[Target Architecture] $(ConvertTo-TexText $TargetArchitecture)
\item[Runner Architecture] $(ConvertTo-TexText $runnerArchitecture)
\item[Build Configuration] $(ConvertTo-TexText $Configuration)
\item[Operating System] $(ConvertTo-TexText $operatingSystem)
\item[Toolchain] $(ConvertTo-TexText $Toolchain)
\item[Start UTC] $(ConvertTo-TexText $start.ToString('o'))
\item[Finish UTC] $(ConvertTo-TexText $finish.ToString('o'))
\item[Command] $(ConvertTo-TexText $commandText)
\item[Process Exit Status] $exitCode
\item[Overall Status] $status
\end{description}
\begin{verbatim}
$outputText
\end{verbatim}
"@

Set-Content -LiteralPath $evidencePath -Value $evidence -Encoding utf8
Write-Output $outputText
Write-Output "Evidence: $evidencePath"
exit $exitCode
