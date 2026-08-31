# Controlled M8 test implementation for TC-0106 (WSH-REQ-0073).
param(
    [string] $ConformanceStatic, [string] $ConformanceShared, [string] $HostExample,
    [string] $HeaderHygiene, [string] $SharedLibrary, [string] $Manifest,
    [string] $VerifyExports, [string] $VerifyInstalled, [string] $Python,
    [string] $PythonScript, [string] $Compiler, [string] $BuildDir, [string] $Config,
    [string] $HostSource, [string] $StageDir, [string] $Wsh,
    [string] $EquivalenceStatic, [string] $EquivalenceShared,
    [string] $TargetArchitecture, [string] $EvidenceDirectory,
    [string] $RepositoryRoot, [string] $Toolchain)
& "$PSScriptRoot\run-m8-test.ps1" -TestCase TC-0106 @PSBoundParameters
exit $LASTEXITCODE
