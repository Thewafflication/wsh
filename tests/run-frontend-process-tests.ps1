# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable
)

$ErrorActionPreference = 'Stop'

function Invoke-RedirectedWsh {
    param(
        [string]$Arguments,
        [string]$InputText
    )

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.Arguments = $Arguments
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.StandardInput.Write($InputText)
        $process.StandardInput.Close()
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdout
            Stderr = $stderr
        }
    } finally {
        $process.Dispose()
    }
}

$automatic = Invoke-RedirectedWsh -Arguments '' -InputText "echo ok`n"
if ($automatic.ExitCode -ne 0 -or $automatic.Stdout -ne '' -or
    $automatic.Stderr -ne '') {
    throw "redirected stdin was not selected as quiet batch input"
}

$syntax = Invoke-RedirectedWsh `
    -Arguments '--non-interactive' `
    -InputText "}`n"
if ($syntax.ExitCode -ne 3 -or $syntax.Stdout -ne '' -or
    $syntax.Stderr -notmatch '^wsh: 1:1:') {
    throw "non-interactive syntax failure did not use exit 3 and stderr"
}

$forced = Invoke-RedirectedWsh `
    -Arguments '--interactive' `
    -InputText "echo unavailable`n"
if ($forced.ExitCode -ne 5 -or
    $forced.Stderr -notmatch 'requires a console on stdin') {
    throw "forced interactive mode did not reject redirected stdin"
}

$conflict = Invoke-RedirectedWsh -Arguments '-iI' -InputText ''
if ($conflict.ExitCode -ne 2 -or
    $conflict.Stderr -notmatch 'mutually exclusive') {
    throw "conflicting interactive modes were not a usage error"
}

Write-Output 'frontend process tests passed'
