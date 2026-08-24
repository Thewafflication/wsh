param($TestCase, $Wsh, $Probe, $EvidenceDirectory, $RepositoryRoot,
    $Configuration, $Toolchain, $TargetArchitecture)
& "$PSScriptRoot\..\run-m6-test.ps1" @PSBoundParameters
exit $LASTEXITCODE
