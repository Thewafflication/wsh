<# Controlled M9 compatibility/security test dispatcher and evidence writer. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $TestCase,
    [string] $FuzzSmoke,
    [string] $ResourceBounds,
    [string] $DfsRecord,
    [string] $MatrixRecord,
    [string] $Executable,
    [string] $SharedLibrary,
    [string] $ProjectVersion,
    [string] $TargetArchitecture,
    [string] $EvidenceDirectory,
    [string] $RepositoryRoot,
    [string] $Configuration,
    [string] $Toolchain
)

$ErrorActionPreference = 'Stop'
$output = [Collections.Generic.List[string]]::new()
$status = 'Pass'
function Add-Output([object] $Value) {
    foreach ($line in @($Value)) { $script:output.Add([string]$line) }
}
function Fail([string] $Message) {
    $script:status = 'Fail'
    $script:output.Add($Message)
}
function Invoke-Checked([string] $Path) {
    $result = & $Path
    Add-Output $result
    if ($LASTEXITCODE -ne 0) { Fail "$Path exited with $LASTEXITCODE" }
}

$start = [DateTime]::UtcNow
switch ($TestCase) {
    'TC-0074' {
        Invoke-Checked $FuzzSmoke
        Invoke-Checked $ResourceBounds
    }
    'TC-0075' {
        $rows = @(Get-Content -LiteralPath $DfsRecord | Where-Object {
            $_ -match '^\| [^|-].*\| (Passed|Approved residual|Open) \|'
        })
        if ($rows.Count -ne 13) { Fail "expected 13 DFS threat rows; found $($rows.Count)" }
        $threats = @($rows | ForEach-Object { ($_ -split '\|')[1].Trim() })
        if (($threats | Sort-Object -Unique).Count -ne $threats.Count) {
            Fail 'DFS threat rows are not unique'
        }
        foreach ($row in $rows) {
            $columns = $row -split '\|'
            if ([string]::IsNullOrWhiteSpace($columns[3])) {
                Fail "DFS row has no evidence/closure: $row"
            }
        }
        Add-Output "dfs-control-map: $($rows.Count) threats, $(@($rows | Where-Object { $_ -match '\| Open \|' }).Count) open"
    }
    'TC-0076' {
        $rows = @(Get-Content -LiteralPath $MatrixRecord | Where-Object {
            $_ -match '^\| (x86|x64|ARM64) \|.*\| (Passed|Open|Not applicable) \|'
        })
        if ($rows.Count -ne 6) { Fail "expected 6 platform rows; found $($rows.Count)" }
        foreach ($architecture in @('x86', 'x64', 'ARM64')) {
            if (@($rows | Where-Object { $_ -match "^\| $architecture \|" }).Count -ne 2) {
                Fail "$architecture does not have exactly two representative rows"
            }
        }
        if ($TargetArchitecture -eq 'arm64' -and $env:GITHUB_ACTIONS -eq 'true' -and
            $env:RUNNER_ARCH -ne 'ARM64') {
            Fail "ARM64 evidence requires RUNNER_ARCH=ARM64; got $env:RUNNER_ARCH"
        }
        Add-Output "platform-matrix: 6 explicit rows; target=$TargetArchitecture runner=$([Runtime.InteropServices.RuntimeInformation]::OSArchitecture)"
    }
    'TC-0077' {
        foreach ($artifact in @(
            @{ Path = $Executable; Name = 'wsh.exe'; Description = 'Waughtal Shell' },
            @{ Path = $SharedLibrary; Name = 'wshlib.dll'; Description = 'Waughtal Shell Embedding Library' }
        )) {
            $item = Get-Item -LiteralPath $artifact.Path
            $version = $item.VersionInfo
            if ($version.FileVersion -ne $ProjectVersion -or
                $version.ProductVersion -ne $ProjectVersion -or
                $version.ProductName -ne 'Waughtal Shell' -or
                $version.FileDescription -ne $artifact.Description -or
                $version.OriginalFilename -ne $artifact.Name) {
                Fail "invalid VERSIONINFO for $($artifact.Path)"
            }
            Add-Output "$($artifact.Name): file=$($version.FileVersion) product=$($version.ProductVersion)"
        }
    }
    default { throw "Unknown M9 test case: $TestCase" }
}
$finish = [DateTime]::UtcNow

if ($EvidenceDirectory) {
    New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null
    $digits = $TestCase.Substring(3)
    $spec = @(Get-ChildItem (Join-Path $RepositoryRoot 'docs/tests/m9') -File |
        Where-Object Name -Like "tc-$digits-*.tex")
    $specHash = if ($spec.Count -eq 1) {
        (Get-FileHash $spec[0].FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    } else { 'unavailable' }
    $revision = (& git -C $RepositoryRoot rev-parse HEAD).Trim()
    if (@(& git -C $RepositoryRoot status --short).Count -gt 0) {
        $revision += ' (working tree modified)'
    }
    $escapedOutput = (($output -join "`n") -replace '\\','\textbackslash{}' -replace '_','\_')
    $evidence = @"
\subsection*{Execution Evidence: $TestCase}
\begin{description}
\item[Test Case] $TestCase
\item[Test Specification SHA-256] $specHash
\item[Requirement References] WSH-REQ-$digits
\item[Source Revision] $revision
\item[Target Architecture] $TargetArchitecture
\item[Build Configuration] $Configuration
\item[Toolchain] $Toolchain
\item[Start UTC] $($start.ToString('o'))
\item[Finish UTC] $($finish.ToString('o'))
\item[Overall Status] $status
\end{description}
\begin{verbatim}
$escapedOutput
\end{verbatim}
"@
    Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$($TestCase.ToLowerInvariant())-execution-evidence.tex") `
        -Value $evidence -Encoding utf8
}

$output
if ($status -ne 'Pass') { exit 1 }
exit 0
