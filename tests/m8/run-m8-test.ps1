<#
.SYNOPSIS
Controlled M8 embedding-SDK test harness.

.DESCRIPTION
Dispatches one controlled M8 test case by identifier, running the already-built
embedding artifacts and asserting their objective results. Every case uses only
the public surface, the shared library, or the installed SDK. When an evidence
directory is supplied the harness writes a controlled execution-evidence record
for the case. It returns zero only when the selected case passes.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $TestCase,
    [string] $ConformanceStatic,
    [string] $ConformanceShared,
    [string] $HostExample,
    [string] $HeaderHygiene,
    [string] $SharedLibrary,
    [string] $Manifest,
    [string] $VerifyExports,
    [string] $VerifyInstalled,
    [string] $Python,
    [string] $PythonScript,
    [string] $Compiler,
    [string] $BuildDir,
    [string] $Config,
    [string] $HostSource,
    [string] $StageDir,
    [string] $Wsh,
    [string] $EquivalenceStatic,
    [string] $EquivalenceShared,
    [string] $TargetArchitecture,
    [string] $EvidenceDirectory,
    [string] $RepositoryRoot,
    [string] $Toolchain
)

$ErrorActionPreference = 'Stop'

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

$output = [Collections.Generic.List[string]]::new()
$status = 'Pass'

function Add-Output { param([object]$Value) foreach ($line in @($Value)) { $output.Add([string]$line) } }
function Fail { param([string]$Message) $script:status = 'Fail'; $script:output.Add($Message) }

function Invoke-Case {
    param([string] $Path, [string[]] $Arguments = @(), [string] $Expect)
    $result = & $Path @Arguments
    Add-Output $result
    if ($LASTEXITCODE -ne 0) { Fail "$Path exited with $LASTEXITCODE"; return }
    if ($Expect -and (@($result) -notcontains $Expect)) {
        Fail "$Path missing expected output '$Expect'"
    }
}

$start = [DateTime]::UtcNow

switch ($TestCase) {
    'TC-0100' {
        Invoke-Case -Path $ConformanceStatic
        Invoke-Case -Path $ConformanceShared
    }
    'TC-0101' {
        Invoke-Case -Path 'pwsh' -Arguments @(
            '-NoProfile', '-File', $VerifyExports,
            '-SharedLibrary', $SharedLibrary, '-Manifest', $Manifest)
    }
    'TC-0102' {
        Invoke-Case -Path $HostExample -Expect 'host: ok'
    }
    'TC-0103' {
        if ([string]::IsNullOrEmpty($Python)) {
            Add-Output 'skipped: no Python interpreter'
        }
        else {
            $bits = (& $Python -c "import struct;print(struct.calcsize('P')*8)").Trim()
            $targetBits = if ($TargetArchitecture -eq 'x86') { '32' } else { '64' }
            if ($bits -ne $targetBits) {
                Add-Output "skipped: Python $bits-bit vs target $targetBits-bit"
            }
            else {
                Invoke-Case -Path $Python -Arguments @($PythonScript, $SharedLibrary) `
                    -Expect 'host.py: ok'
            }
        }
    }
    'TC-0104' {
        Invoke-Case -Path $HeaderHygiene
    }
    'TC-0105' {
        Invoke-Case -Path 'pwsh' -Arguments @(
            '-NoProfile', '-File', $VerifyInstalled,
            '-Compiler', $Compiler, '-BuildDir', $BuildDir, '-Config', $Config,
            '-Source', $HostSource, '-StageDir', $StageDir)
    }
    'TC-0106' {
        $cases = @(
            @{ Source = 'x=alpha; fn set { x=beta }; set'; Status = 0 },
            @{ Source = 'text::join --separator : --into joined alpha beta'; Status = 0 },
            @{ Source = 'system::architecture --into arch'; Status = 0 },
            @{ Source = 'exit 7'; Status = 7 }
        )
        foreach ($case in $cases) {
            $observed = @{}
            foreach ($artifact in @(
                @{ Name = 'static'; Path = $EquivalenceStatic; Prefix = @() },
                @{ Name = 'shared'; Path = $EquivalenceShared; Prefix = @() },
                @{ Name = 'executable'; Path = $Wsh; Prefix = @('-c') }
            )) {
                $caseOutput = & $artifact.Path @($artifact.Prefix) $case.Source
                $observed[$artifact.Name] = $LASTEXITCODE
                Add-Output "$($artifact.Name): $($caseOutput -join ' ')"
            }
            $values = @($observed.Values | Sort-Object -Unique)
            if ($values.Count -ne 1 -or $values[0] -ne $case.Status) {
                Fail ("equivalence mismatch for '{0}': static={1}, shared={2}, " +
                    "executable={3}, expected={4}" -f $case.Source,
                    $observed.static, $observed.shared, $observed.executable,
                    $case.Status)
            }
        }
    }
    default {
        Write-Error "Unknown M8 test case: $TestCase"
        exit 2
    }
}

$finish = [DateTime]::UtcNow
$outputText = ($output -join "`n")
if (-not $outputText) { $outputText = '(no output)' }

if ($EvidenceDirectory) {
    $repo = if ($RepositoryRoot) { (Resolve-Path -LiteralPath $RepositoryRoot).Path } else { '' }
    $testDigits = $TestCase.Substring(3)
    $specPath = $null
    if ($repo) {
        $specs = @(Get-ChildItem -LiteralPath (Join-Path $repo 'docs/tests/m8') -File |
            Where-Object { $_.Name -like "tc-$testDigits-*.tex" })
        if ($specs.Count -eq 1) { $specPath = $specs[0].FullName }
    }
    $requirementReferences = 'WSH-REQ-' + $testDigits
    if ($specPath) {
        $specText = Get-Content -LiteralPath $specPath -Raw
        $refMatch = [regex]::Match($specText,
            '\\def\\TCRequirementRef\{(?<r>.*?)\}',
            [Text.RegularExpressions.RegexOptions]::Singleline)
        if ($refMatch.Success) {
            $requirementReferences = ($refMatch.Groups['r'].Value -replace '\s+', ' ').Trim()
        }
    }
    $specHash = if ($specPath) { (Get-FileHash -LiteralPath $specPath -Algorithm SHA256).Hash.ToLowerInvariant() } else { 'unavailable' }
    $artifactHash = if ($SharedLibrary -and (Test-Path -LiteralPath $SharedLibrary)) {
        (Get-FileHash -LiteralPath $SharedLibrary -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { 'unavailable' }
    $revision = if ($repo) { (& git -C $repo rev-parse HEAD 2>$null).Trim() } else { '' }
    if (-not $revision) { $revision = 'Unavailable' }
    if ($repo) {
        $dirty = @(& git -C $repo status --short 2>$null)
        if ($dirty.Count -gt 0) { $revision += ' (working tree modified)' }
    }
    $runnerArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
    $operatingSystem = [Runtime.InteropServices.RuntimeInformation]::OSDescription

    New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null
    $evidenceName = $TestCase.ToLowerInvariant() + '-execution-evidence.tex'
    $evidencePath = Join-Path $EvidenceDirectory $evidenceName
    $evidence = @"
\subsection*{Execution Evidence: $(ConvertTo-TexText $TestCase)}
\begin{description}
\item[Test Case] $(ConvertTo-TexText $TestCase)
\item[Test Specification SHA-256] $(ConvertTo-TexText $specHash)
\item[Tested Shared Library SHA-256] $(ConvertTo-TexText $artifactHash)
\item[Requirement References] $(ConvertTo-TexText $requirementReferences)
\item[Source Revision] $(ConvertTo-TexText $revision)
\item[Target Architecture] $(ConvertTo-TexText $TargetArchitecture)
\item[Runner Architecture] $(ConvertTo-TexText $runnerArchitecture)
\item[Build Configuration] $(ConvertTo-TexText $Config)
\item[Operating System] $(ConvertTo-TexText $operatingSystem)
\item[Toolchain] $(ConvertTo-TexText $Toolchain)
\item[Start UTC] $(ConvertTo-TexText $start.ToString('o'))
\item[Finish UTC] $(ConvertTo-TexText $finish.ToString('o'))
\item[Overall Status] $status
\end{description}
\begin{verbatim}
$outputText
\end{verbatim}
"@
    Set-Content -LiteralPath $evidencePath -Value $evidence -Encoding utf8
}

Write-Output $outputText
if ($status -ne 'Pass') { exit 1 }
exit 0
