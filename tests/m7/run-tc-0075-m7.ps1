param($TestCase, $Driver, $Wsh, $Probe, $Scenario, $EvidenceDirectory,
    $RepositoryRoot, $Configuration, $Toolchain, $TargetArchitecture)
& "$PSScriptRoot\..\run-m7-test.ps1" @PSBoundParameters
exit $LASTEXITCODE
