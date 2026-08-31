<#
.SYNOPSIS
Controlled M8 embedding-SDK test harness.

.DESCRIPTION
Dispatches one controlled M8 test case by identifier, running the already-built
embedding artifacts and asserting their objective results. Every case uses only
the public surface, the shared library, or the installed SDK. The harness
returns zero only when the selected case passes.
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
    [string] $TargetArchitecture
)

$ErrorActionPreference = 'Stop'

function Invoke-Executable {
    param([string] $Path, [string] $Expect)

    $output = & $Path
    if ($LASTEXITCODE -ne 0) {
        Write-Error "$Path exited with $LASTEXITCODE"
        return $false
    }
    if ($Expect -and ($output -notcontains $Expect)) {
        Write-Error ("$Path missing expected output '$Expect': " + ($output -join '|'))
        return $false
    }
    return $true
}

switch ($TestCase) {
    'TC-0100' {
        if (-not (Invoke-Executable -Path $ConformanceStatic)) { exit 1 }
        if (-not (Invoke-Executable -Path $ConformanceShared)) { exit 1 }
        Write-Output 'TC-0100: static and shared ABI conformance and misuse passed'
    }
    'TC-0101' {
        $result = & pwsh -NoProfile -File $VerifyExports `
            -SharedLibrary $SharedLibrary -Manifest $Manifest
        if ($LASTEXITCODE -ne 0) { Write-Error ($result -join '|'); exit 1 }
        Write-Output 'TC-0101: shared library exports exactly the ABI 1 manifest'
    }
    'TC-0102' {
        if (-not (Invoke-Executable -Path $HostExample -Expect 'host: ok')) { exit 1 }
        Write-Output 'TC-0102: C embedding host ran against the public surface'
    }
    'TC-0103' {
        if ([string]::IsNullOrEmpty($Python)) {
            Write-Output 'TC-0103: skipped (no Python interpreter)'
            exit 0
        }
        $bits = (& $Python -c "import struct;print(struct.calcsize('P')*8)").Trim()
        $targetBits = if ($TargetArchitecture -eq 'x86') { '32' } else { '64' }
        if ($bits -ne $targetBits) {
            Write-Output "TC-0103: skipped (Python $bits-bit vs target $targetBits-bit)"
            exit 0
        }
        $output = & $Python $PythonScript $SharedLibrary
        if ($LASTEXITCODE -ne 0 -or ($output -notcontains 'host.py: ok')) {
            Write-Error ('FFI host failed: ' + ($output -join '|'))
            exit 1
        }
        Write-Output 'TC-0103: Python FFI host ran against the shared ABI'
    }
    'TC-0104' {
        if (-not (Invoke-Executable -Path $HeaderHygiene)) { exit 1 }
        Write-Output 'TC-0104: public headers compile standalone with no internal type'
    }
    'TC-0105' {
        $result = & pwsh -NoProfile -File $VerifyInstalled `
            -Compiler $Compiler -BuildDir $BuildDir -Config $Config `
            -Source $HostSource -StageDir $StageDir
        if ($LASTEXITCODE -ne 0) { Write-Error ($result -join '|'); exit 1 }
        Write-Output 'TC-0105: host built and ran against the installed SDK'
    }
    default {
        Write-Error "Unknown M8 test case: $TestCase"
        exit 2
    }
}

exit 0
