# Controlled M8 test implementation for TC-0105 (WSH-REQ-0105).
param(
    [string] $ConformanceStatic, [string] $ConformanceShared, [string] $HostExample,
    [string] $HeaderHygiene, [string] $SharedLibrary, [string] $Manifest,
    [string] $VerifyExports, [string] $VerifyInstalled, [string] $Python,
    [string] $PythonScript, [string] $Compiler, [string] $BuildDir, [string] $Config,
    [string] $HostSource, [string] $StageDir, [string] $TargetArchitecture)
& "$PSScriptRoot\run-m8-test.ps1" -TestCase TC-0105 @PSBoundParameters
exit $LASTEXITCODE
