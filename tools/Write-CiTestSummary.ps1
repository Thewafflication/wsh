[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Architecture,

    [Parameter(Mandatory)]
    [string]$Configuration,

    [Parameter(Mandatory)]
    [string]$BuildOutcome,

    [Parameter(Mandatory)]
    [string]$TestOutcome,

    [string]$PackageOutcome = 'not-applicable',
    [string]$JUnitPath,
    [string]$EvidenceArtifactUrl,
    [string]$PackagePath,
    [string]$SummaryPath = $env:GITHUB_STEP_SUMMARY
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string]$Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Get-OutcomeCell {
    param([Parameter(Mandatory)][string]$Outcome)

    switch ($Outcome.ToLowerInvariant()) {
        'success' { return '🟢 **PASS**' }
        'not-applicable' { return '⚪ **N/A**' }
        'skipped' { return '⚪ **NOT RUN**' }
        default { return '🔴 **FAIL**' }
    }
}

function ConvertTo-MarkdownText {
    param([AllowEmptyString()][string]$Text)

    if ($null -eq $Text) {
        return ''
    }

    return $Text.Replace('|', '\|').Replace("`r", '').Replace("`n", ' ')
}

if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
    throw 'A GitHub step-summary path was not provided.'
}

$lines = [Collections.Generic.List[string]]::new()
$lines.Add('# WSH Verification Report')
$lines.Add('')
$lines.Add("## $Architecture $Configuration")
$lines.Add('')
$lines.Add('| Gate | Result |')
$lines.Add('| --- | --- |')
$lines.Add("| Build | $(Get-OutcomeCell $BuildOutcome) |")
$lines.Add("| Test suite | $(Get-OutcomeCell $TestOutcome) |")
if ($PackageOutcome -ne 'not-applicable') {
    $lines.Add("| WPM package | $(Get-OutcomeCell $PackageOutcome) |")
}
$lines.Add('')

$resolvedJUnitPath = if ([string]::IsNullOrWhiteSpace($JUnitPath)) {
    $null
} else {
    Resolve-ProjectPath $JUnitPath
}
if ($resolvedJUnitPath -and
    (Test-Path -LiteralPath $resolvedJUnitPath -PathType Leaf)) {
    [xml]$junit = Get-Content -LiteralPath $resolvedJUnitPath -Raw
    $testCases = @($junit.SelectNodes('//testcase'))
    $passed = 0
    $failed = 0
    $skipped = 0

    $lines.Add('### Test suite')
    $lines.Add('')
    $lines.Add('| Test | Result | Seconds |')
    $lines.Add('| --- | --- | ---: |')
    foreach ($testCase in $testCases) {
        $result = if ($testCase.failure -or $testCase.error) {
            $failed++
            '🔴 **FAIL**'
        } elseif ($testCase.skipped) {
            $skipped++
            '⚪ **SKIP**'
        } else {
            $passed++
            '🟢 **PASS**'
        }
        $name = ConvertTo-MarkdownText ([string]$testCase.name)
        $seconds = if ($testCase.time) { [string]$testCase.time } else { '0' }
        $lines.Add("| $name | $result | $seconds |")
    }
    $lines.Add('')
    $lines.Add(
        "**Total:** $($testCases.Count); **passed:** $passed; " +
        "**failed:** $failed; **skipped:** $skipped."
    )
    $lines.Add('')
} else {
    $lines.Add('No machine-readable test result was produced.')
    $lines.Add('')
}

$lines.Add('### Downloads')
$lines.Add('')
if (-not [string]::IsNullOrWhiteSpace($EvidenceArtifactUrl)) {
    $lines.Add("- [Build and test evidence ZIP]($EvidenceArtifactUrl)")
} else {
    $lines.Add('- Build and test evidence ZIP: upload unavailable')
}
if (-not [string]::IsNullOrWhiteSpace($PackagePath)) {
    $resolvedPackagePath = Resolve-ProjectPath $PackagePath
    if (Test-Path -LiteralPath $resolvedPackagePath -PathType Leaf) {
        $lines.Add(
            '- WPM package included in the evidence ZIP: `' +
            [IO.Path]::GetFileName($resolvedPackagePath) + '`'
        )
    }
}
$lines.Add('')
$runnerArchitecture = if ($env:RUNNER_ARCH) {
    $env:RUNNER_ARCH
} else {
    'local'
}
$lines.Add("Runner architecture: ``$runnerArchitecture``.")

$lines | Out-File -LiteralPath $SummaryPath -Encoding utf8 -Append
