function ConvertTo-CapturedLines {
    param([AllowEmptyString()][string]$Text)

    if ([string]::IsNullOrEmpty($Text)) {
        return @()
    }
    $normalized = $Text.Replace("`r`n", "`n").Replace("`r", "`n")
    $lines = @($normalized.Split("`n"))
    if ($lines.Count -gt 0 -and $lines[-1] -eq '') {
        if ($lines.Count -eq 1) {
            return @()
        }
        return @($lines[0..($lines.Count - 2)])
    }
    return $lines
}

function Invoke-CapturedProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [ValidateRange(1, 2147483)][int]$TimeoutSeconds = 110
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $startInfo.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($argument in $ArgumentList) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            return [pscustomobject]@{
                ExitCode = 125
                Output = @("Failed to start process: $FilePath")
            }
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $completed = $process.WaitForExit($TimeoutSeconds * 1000)
        if (-not $completed) {
            try {
                $process.Kill($true)
            } catch {
                # Preserve the timeout result even if the process already exited.
            }
            $process.WaitForExit()
        }

        $captured = [Collections.Generic.List[string]]::new()
        foreach ($line in (ConvertTo-CapturedLines (
            $stdoutTask.GetAwaiter().GetResult()))) {
            $captured.Add($line)
        }
        foreach ($line in (ConvertTo-CapturedLines (
            $stderrTask.GetAwaiter().GetResult()))) {
            $captured.Add($line)
        }
        if (-not $completed) {
            $captured.Add(
                "Process timed out after $TimeoutSeconds seconds: $FilePath")
        }
        return [pscustomobject]@{
            ExitCode = if ($completed) { $process.ExitCode } else { 124 }
            Output = @($captured)
        }
    } catch {
        return [pscustomobject]@{
            ExitCode = 125
            Output = @("Process launch failed: $($_.Exception.Message)")
        }
    } finally {
        $process.Dispose()
    }
}
