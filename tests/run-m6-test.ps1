[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TestCase,
    [Parameter(Mandatory)][string]$Wsh,
    [Parameter(Mandatory)][string]$Probe,
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

$resolvedWsh = (Resolve-Path -LiteralPath $Wsh).Path
$resolvedProbe = (Resolve-Path -LiteralPath $Probe).Path
$resolvedRepository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$script = Join-Path $resolvedRepository 'tests\m6\library-tests.wsh'
$currentRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
$evidenceName = $TestCase.ToLowerInvariant() + '-execution-evidence.tex'
$evidencePath = Join-Path $currentRoot $evidenceName
$archiveRoot = Join-Path (Split-Path -Parent $currentRoot) 'archive'
$testDigits = $TestCase.Substring(3)
$specifications = @(Get-ChildItem -LiteralPath (
    Join-Path $resolvedRepository 'docs\tests\m6') -File |
    Where-Object { $_.Name -like "tc-$testDigits-*.tex" })
if ($specifications.Count -ne 1) {
    throw "Expected one M6 specification for $TestCase; found $($specifications.Count)"
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

$start = [DateTime]::UtcNow
Push-Location $resolvedRepository
try {
    $nativeResult = Invoke-CapturedProcess -FilePath $resolvedWsh `
        -ArgumentList @($script, $TestCase, $resolvedProbe, $resolvedRepository)
} finally {
    Pop-Location
}
$captured = @($nativeResult.Output)
$exitCode = $nativeResult.ExitCode
if ($exitCode -eq 0 -and $TestCase -eq 'TC-0068') {
    $negativeCases = @(
        @('incomplete', "test::begin TC-0068-INCOMPLETE 'missing end'"),
        @('duplicate', "test::begin TC-0068-DUP first; " +
            "test::begin TC-0068-DUP second"),
        @('blocked', "test::begin TC-0068-BLOCKED blocked; " +
            "test::blocked prerequisite; test::end"),
        @('skipped', "test::begin TC-0068-SKIPPED skipped; " +
            "test::skip not-applicable; test::end"),
        @('failed', "test::begin TC-0068-FAILED failed; " +
            "test::fail expected-failure; test::end")
    )
    foreach ($negativeCase in $negativeCases) {
        $negative = Invoke-CapturedProcess -FilePath $resolvedWsh `
            -ArgumentList @('-c', $negativeCase[1])
        $captured += @($negative.Output | ForEach-Object {
            "$($negativeCase[0]): $_"
        })
        if ($negative.ExitCode -eq 0) {
            $captured += "$($negativeCase[0]): unexpectedly succeeded"
            $exitCode = 1
        }
    }
}
if ($exitCode -eq 0 -and $TestCase -eq 'TC-0074') {
    $limitRoot = [IO.Path]::GetFullPath((Join-Path $currentRoot (
        'limits-' + [guid]::NewGuid().ToString('N'))))
    if (-not $limitRoot.StartsWith(
            $currentRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Resolved limit test root escaped the evidence directory'
    }
    $oversized = Join-Path $limitRoot 'oversized.bin'
    $small = Join-Path $limitRoot 'small.txt'
    try {
        New-Item -ItemType Directory -Force -Path $limitRoot | Out-Null
        $stream = [IO.File]::Open(
            $oversized,
            [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        try {
            $stream.SetLength((16MB) + 1)
        } finally {
            $stream.Dispose()
        }
        Set-Content -LiteralPath $small -Value bounded -NoNewline
        $largeRead = Invoke-CapturedProcess -FilePath $resolvedWsh `
            -ArgumentList @('-c', "fs::read --into data '$oversized'")
        $captured += @($largeRead.Output | ForEach-Object {
            "oversized-read: $_"
        })
        if ($largeRead.ExitCode -eq 0) {
            $captured += 'oversized-read: unexpectedly succeeded'
            $exitCode = 1
        }
        $retry = Invoke-CapturedProcess -FilePath $resolvedWsh `
            -ArgumentList @('-c', "fs::read --into data '$small'")
        $captured += @($retry.Output | ForEach-Object { "retry: $_" })
        if ($retry.ExitCode -ne 0) {
            $captured += 'retry: valid operation failed after limit rejection'
            $exitCode = 1
        }
        $jobs = Invoke-CapturedProcess -FilePath $resolvedWsh `
            -ArgumentList @(
                '-c', "process::parallel --jobs 999999 -- 'time::sleep 1'")
        $captured += @($jobs.Output | ForEach-Object { "jobs-limit: $_" })
        if ($jobs.ExitCode -eq 0) {
            $captured += 'jobs-limit: unexpectedly succeeded'
            $exitCode = 1
        }
    } finally {
        if (Test-Path -LiteralPath $limitRoot) {
            Remove-Item -LiteralPath $limitRoot -Recurse -Force
        }
    }
}
if ($exitCode -eq 0 -and $TestCase -eq 'TC-0066') {
    $caseRoot = [IO.Path]::GetFullPath((Join-Path $currentRoot (
        'reparse-' + [guid]::NewGuid().ToString('N'))))
    if (-not $caseRoot.StartsWith(
            $currentRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Resolved reparse test root escaped the evidence directory'
    }
    $outside = Join-Path $caseRoot 'outside'
    $junction = Join-Path $caseRoot 'junction'
    $sentinel = Join-Path $outside 'sentinel.txt'
    try {
        New-Item -ItemType Directory -Force -Path $outside | Out-Null
        Set-Content -LiteralPath $sentinel -Value retained -NoNewline
        New-Item -ItemType Junction -Path $junction -Target $outside |
            Out-Null
        $command = "fs::remove --recursive '$junction'; " +
            "fs::exists '$sentinel'"
        $reparse = Invoke-CapturedProcess -FilePath $resolvedWsh `
            -ArgumentList @('-c', $command)
        $captured += @($reparse.Output | ForEach-Object { "reparse: $_" })
        if ($reparse.ExitCode -ne 0 -or
            (Test-Path -LiteralPath $junction) -or
            -not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
            $captured += 'reparse: link traversal/removal contract failed'
            $exitCode = 1
        }
    } finally {
        if (Test-Path -LiteralPath $caseRoot) {
            Remove-Item -LiteralPath $caseRoot -Recurse -Force
        }
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
$scriptHash = (Get-FileHash -LiteralPath $script `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$runnerArchitecture =
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
$operatingSystem = [Runtime.InteropServices.RuntimeInformation]::OSDescription
$commandText = '"{0}" "{1}" {2} "{3}"' -f `
    $resolvedWsh, $script, $TestCase, $resolvedProbe
$outputText = $captured -join "`n"
if (-not $outputText) { $outputText = '(no output)' }

$evidence = @"
\subsection*{Execution Evidence: $(ConvertTo-TexText $TestCase)}
\begin{description}
\item[Test Case] $(ConvertTo-TexText $TestCase)
\item[Test Specification] $(ConvertTo-TexText $specification.Name)
\item[Test Specification SHA-256] $(ConvertTo-TexText $specificationHash)
\item[Tested Artifact SHA-256] $(ConvertTo-TexText $artifactHash)
\item[WSH Test Script SHA-256] $(ConvertTo-TexText $scriptHash)
\item[Requirement References] $(ConvertTo-TexText $requirementReferences)
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
