[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TestCase,
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$Probe,
    [Parameter(Mandatory)][string]$Wsh,
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
    return $builder.ToString()
}

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$resolvedProbe = (Resolve-Path -LiteralPath $Probe).Path
$resolvedWsh = (Resolve-Path -LiteralPath $Wsh).Path
$resolvedRepository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$currentRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
$evidenceName = $TestCase.ToLowerInvariant() + '-execution-evidence.tex'
$evidencePath = Join-Path $currentRoot $evidenceName
$archiveRoot = Join-Path (Split-Path -Parent $currentRoot) 'archive'
$testDigits = $TestCase.Substring(3)
$specifications = @(Get-ChildItem -LiteralPath (
    Join-Path $resolvedRepository 'docs\tests\m5') -File |
    Where-Object { $_.Name -like "tc-$testDigits-*.tex" })
if ($specifications.Count -ne 1) {
    throw "Expected one specification for $TestCase; found " +
        $specifications.Count
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
$captured = [Collections.Generic.List[string]]::new()
$nativeResult = Invoke-CapturedProcess `
    -FilePath $resolvedExecutable `
    -ArgumentList @($TestCase, $resolvedProbe)
$nativeOutput = @($nativeResult.Output)
$exitCode = $nativeResult.ExitCode
foreach ($line in $nativeOutput) { $captured.Add($line) }

if ($exitCode -eq 0 -and $TestCase -eq 'TC-0015') {
    $script = Join-Path $resolvedRepository `
        'tests\fixtures\m5-nested-descriptor.wsh'
    $descriptorOutput = Join-Path $currentRoot 'descriptor-3.txt'
    Remove-Item -LiteralPath $descriptorOutput -Force `
        -ErrorAction SilentlyContinue
    $command = "'$script' >[3] '$descriptorOutput'"
    $descriptorResult = Invoke-CapturedProcess `
        -FilePath $resolvedWsh `
        -ArgumentList @('-c', $command)
    $descriptorRun = @($descriptorResult.Output)
    $exitCode = $descriptorResult.ExitCode
    foreach ($line in $descriptorRun) {
        $captured.Add("descriptor: $line")
    }
    if ($exitCode -eq 0) {
        $descriptorText = Get-Content -LiteralPath $descriptorOutput -Raw
        if ($descriptorText.TrimEnd("`r", "`n") -ne 'descriptor') {
            $captured.Add('nested descriptor output mismatch')
            $exitCode = 1
        }
    }
    Remove-Item -LiteralPath $descriptorOutput -Force `
        -ErrorAction SilentlyContinue
    if ($exitCode -eq 0) {
        $hereCommand = "x=world; '$resolvedProbe' copy <<EOF`n" +
            "hello `$x^`nEOF`n"
        $hereResult = Invoke-CapturedProcess `
            -FilePath $resolvedWsh `
            -ArgumentList @('-c', $hereCommand)
        $here = @($hereResult.Output)
        $exitCode = $hereResult.ExitCode
        foreach ($line in $here) { $captured.Add("here: $line") }
        if ($exitCode -eq 0 -and ($here -join "`n") -ne 'hello world') {
            $captured.Add('unquoted here-document expansion mismatch')
            $exitCode = 1
        }
    }
    if ($exitCode -eq 0) {
        $quotedCommand = "x=world; '$resolvedProbe' copy <<'EOF'`n" +
            "hello `$x`nEOF`n"
        $quotedResult = Invoke-CapturedProcess `
            -FilePath $resolvedWsh `
            -ArgumentList @('-c', $quotedCommand)
        $quoted = @($quotedResult.Output)
        $exitCode = $quotedResult.ExitCode
        foreach ($line in $quoted) { $captured.Add("quoted-here: $line") }
        if ($exitCode -eq 0 -and ($quoted -join "`n") -ne 'hello $x') {
            $captured.Add('quoted here-document literal mismatch')
            $exitCode = 1
        }
    }
}

if ($exitCode -eq 0 -and $TestCase -eq 'TC-0016') {
    $command = "echo 'pipeline ☃' | '$resolvedProbe' copy"
    $pipelineResult = Invoke-CapturedProcess `
        -FilePath $resolvedWsh `
        -ArgumentList @('-c', $command)
    $pipeline = @($pipelineResult.Output)
    $exitCode = $pipelineResult.ExitCode
    foreach ($line in $pipeline) { $captured.Add("pipeline: $line") }
    if ($exitCode -eq 0 -and ($pipeline -join "`n") -ne 'pipeline ☃') {
        $captured.Add('shell-stage pipeline output mismatch')
        $exitCode = 1
    }
}

if ($exitCode -eq 0 -and $TestCase -eq 'TC-0043') {
    $script = Join-Path $resolvedRepository `
        'tests\fixtures\m5-nested-list.wsh'
    $command = "m5_nested_list=(one 'two words'); " +
        "export m5_nested_list; '$script'"
    $nestedResult = Invoke-CapturedProcess `
        -FilePath $resolvedWsh `
        -ArgumentList @('-c', $command)
    $nested = @($nestedResult.Output)
    $exitCode = $nestedResult.ExitCode
    foreach ($line in $nested) { $captured.Add("nested: $line") }
    if ($exitCode -eq 0 -and ($nested -join "`n") -ne 'one two words') {
        $captured.Add('nested envelope output mismatch')
        $exitCode = 1
    }
}

if ($exitCode -eq 0 -and $TestCase -eq 'TC-0051') {
    $command = "'$resolvedProbe' copy < <{'$resolvedProbe' emit pipe-data}"
    $substitutionResult = Invoke-CapturedProcess `
        -FilePath $resolvedWsh `
        -ArgumentList @('-c', $command)
    $substitution = @($substitutionResult.Output)
    $exitCode = $substitutionResult.ExitCode
    foreach ($line in $substitution) {
        $captured.Add("process-substitution: $line")
    }
    if ($exitCode -eq 0 -and
        ($substitution -join "`n") -ne 'pipe-data') {
        $captured.Add('process substitution output mismatch')
        $exitCode = 1
    }
}

if ($exitCode -eq 0 -and $TestCase -eq 'TC-0052') {
    $command = "'$resolvedProbe' emit outer " + [char]96 +
        "{'$resolvedProbe' invalid}"
    $captureResult = Invoke-CapturedProcess `
        -FilePath $resolvedWsh `
        -ArgumentList @('-c', $command)
    $captureFailure = @($captureResult.Output)
    $captureExit = $captureResult.ExitCode
    foreach ($line in $captureFailure) {
        $captured.Add("invalid-capture: $line")
    }
    if ($captureExit -eq 0 -or
        ($captureFailure -join "`n") -match '(^|\s)outer($|\s)') {
        $captured.Add('invalid capture did not suppress the outer launch')
        $exitCode = 1
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
$artifactHash = (Get-FileHash -LiteralPath $resolvedExecutable `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$probeHash = (Get-FileHash -LiteralPath $resolvedProbe `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$wshHash = (Get-FileHash -LiteralPath $resolvedWsh `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$runnerArchitecture =
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
$operatingSystem = [Runtime.InteropServices.RuntimeInformation]::OSDescription
$commandText = '"{0}" {1} "{2}"' -f (
    $resolvedExecutable), $TestCase, $resolvedProbe
$outputText = $captured -join "`n"
if (-not $outputText) { $outputText = '(no output)' }

$evidence = @"
\subsection*{Execution Evidence: $(ConvertTo-TexText $TestCase)}
\begin{description}
\item[Test Case] $(ConvertTo-TexText $TestCase)
\item[Test Specification] $(ConvertTo-TexText $specification.Name)
\item[Test Specification SHA-256] $(ConvertTo-TexText $specificationHash)
\item[Tested Artifact SHA-256] $(ConvertTo-TexText $artifactHash)
\item[Probe Artifact SHA-256] $(ConvertTo-TexText $probeHash)
\item[WSH Artifact SHA-256] $(ConvertTo-TexText $wshHash)
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
