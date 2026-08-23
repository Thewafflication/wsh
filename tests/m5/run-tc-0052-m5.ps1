param($TestCase, $Executable, $Probe, $Wsh, $EvidenceDirectory,
    $RepositoryRoot, $Configuration, $Toolchain, $TargetArchitecture)
& "$PSScriptRoot\..\run-m5-test.ps1" @PSBoundParameters
exit $LASTEXITCODE
