# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable
)

$ErrorActionPreference = 'Stop'

function Invoke-RedirectedWsh {
    param(
        [string]$Arguments,
        [string]$InputText,
        [ValidateRange(1, 2147483)][int]$TimeoutSeconds = 110
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
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            try {
                $process.Kill($true)
            } catch {
                # Preserve the timeout result if the process exited concurrently.
            }
            throw "WSH timed out after $TimeoutSeconds seconds: $Arguments"
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdout
            Stderr = $stderr
        }
    } finally {
        $process.Dispose()
    }
}

$unicode = Invoke-RedirectedWsh `
    -Arguments '-c "echo snowman-☃"' `
    -InputText ''
if ($unicode.ExitCode -ne 0 -or
    $unicode.Stdout -notin @("snowman-☃`n", "snowman-☃`r`n") -or
    $unicode.Stderr -ne '') {
    throw 'wide command-line input did not round-trip as strict UTF-8'
}

$automatic = Invoke-RedirectedWsh -Arguments '' -InputText "echo ok`n"
if ($automatic.ExitCode -ne 0 -or
    $automatic.Stdout -notin @("ok`n", "ok`r`n") -or
    $automatic.Stderr -ne '') {
    throw "redirected stdin did not evaluate through the M4 write runtime"
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
