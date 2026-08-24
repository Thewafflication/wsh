[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TestCase,
    [Parameter(Mandatory)][string]$Driver,
    [Parameter(Mandatory)][string]$Wsh,
    [Parameter(Mandatory)][string]$Probe,
    [Parameter(Mandatory)][string]$Scenario,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$Configuration,
    [Parameter(Mandatory)][string]$Toolchain,
    [Parameter(Mandatory)][string]$TargetArchitecture
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Invoke-CapturedProcess.ps1')

function ConvertTo-TexText {
    param([AllowEmptyString()][string]$Value)
    if ($null -eq $Value) { return '' }
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
    $builder.ToString()
}

$resolvedDriver = (Resolve-Path -LiteralPath $Driver).Path
$resolvedWsh = (Resolve-Path -LiteralPath $Wsh).Path
$resolvedProbe = (Resolve-Path -LiteralPath $Probe).Path
$resolvedRepository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$currentRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
$evidenceName = $TestCase.ToLowerInvariant() + '-execution-evidence.tex'
$evidencePath = Join-Path $currentRoot $evidenceName
$archiveRoot = Join-Path (Split-Path -Parent $currentRoot) 'archive'
$testDigits = $TestCase.Substring(3)
$specifications = @(Get-ChildItem -LiteralPath (
    Join-Path $resolvedRepository 'docs\tests\m7') -File |
    Where-Object { $_.Name -like "tc-$testDigits-*.tex" })
if ($specifications.Count -ne 1) {
    throw "Expected one M7 specification for $TestCase; found $($specifications.Count)"
}
$specification = $specifications[0]
$specificationText = Get-Content -LiteralPath $specification.FullName -Raw
$requirementMatch = [regex]::Match(
    $specificationText,
    '\\def\\TCRequirementRef\{(?<references>.*?)\}',
    [Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $requirementMatch.Success) {
    throw "No requirement references found in $($specification.Name)"
}
$requirementReferences =
    ($requirementMatch.Groups['references'].Value -replace '\s+', ' ').Trim()

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

$caseRoot = [IO.Path]::GetFullPath((Join-Path $currentRoot (
    'native-' + $testDigits + '-' + [guid]::NewGuid().ToString('N'))))
if (-not $caseRoot.StartsWith(
        $currentRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Resolved native test root escaped the evidence directory.'
}
New-Item -ItemType Directory -Force -Path $caseRoot | Out-Null
$start = [DateTime]::UtcNow
try {
    $nativeResult = Invoke-CapturedProcess -FilePath $resolvedDriver `
        -ArgumentList @($resolvedWsh, $resolvedProbe, $Scenario, $caseRoot)
    $captured = @($nativeResult.Output)
    $exitCode = $nativeResult.ExitCode
} finally {
    if (Test-Path -LiteralPath $caseRoot) {
        Remove-Item -LiteralPath $caseRoot -Recurse -Force
    }
}
$finish = [DateTime]::UtcNow
$status = if ($exitCode -eq 0) { 'Pass' } else { 'Fail' }
$revision = (& git -C $resolvedRepository rev-parse HEAD 2>$null).Trim()
if (-not $revision) { $revision = 'Unavailable' }
$dirty = @(& git -C $resolvedRepository status --short 2>$null)
if ($dirty.Count -gt 0) { $revision += ' (working tree modified)' }
$specificationHash = (Get-FileHash -LiteralPath $specification.FullName `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$artifactHash = (Get-FileHash -LiteralPath $resolvedWsh `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$driverHash = (Get-FileHash -LiteralPath $resolvedDriver `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$probeHash = (Get-FileHash -LiteralPath $resolvedProbe `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$runnerArchitecture =
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
$operatingSystem = [Runtime.InteropServices.RuntimeInformation]::OSDescription
$commandText = '"{0}" "{1}" "{2}" {3} "{4}"' -f `
    $resolvedDriver, $resolvedWsh, $resolvedProbe, $Scenario, $caseRoot
$outputText = $captured -join "`n"
if (-not $outputText) { $outputText = '(no output)' }

$evidence = @"
\subsection*{Execution Evidence: $(ConvertTo-TexText $TestCase)}
\begin{description}
\item[Test Case] $(ConvertTo-TexText $TestCase)
\item[Test Specification] $(ConvertTo-TexText $specification.Name)
\item[Test Specification SHA-256] $(ConvertTo-TexText $specificationHash)
\item[Tested WSH SHA-256] $(ConvertTo-TexText $artifactHash)
\item[Native Console Driver SHA-256] $(ConvertTo-TexText $driverHash)
\item[Runtime Probe SHA-256] $(ConvertTo-TexText $probeHash)
\item[Requirement References] $(ConvertTo-TexText $requirementReferences)
\item[Source Revision] $(ConvertTo-TexText $revision)
\item[Target Architecture] $(ConvertTo-TexText $TargetArchitecture)
\item[Runner Architecture] $(ConvertTo-TexText $runnerArchitecture)
\item[Build Configuration] $(ConvertTo-TexText $Configuration)
\item[Operating System] $(ConvertTo-TexText $operatingSystem)
\item[Toolchain] $(ConvertTo-TexText $Toolchain)
\item[Native Standard Input] yes, genuine CONIN\$ INPUT\_RECORD stream
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
