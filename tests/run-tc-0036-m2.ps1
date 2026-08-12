param($TestCase, $Executable, $EvidenceDirectory, $RepositoryRoot,
    $Configuration, $Toolchain, $TargetArchitecture)
& "$PSScriptRoot\run-m2-test.ps1" @PSBoundParameters
exit $LASTEXITCODE
