param($TestCase, $Executable, $EvidenceDirectory, $RepositoryRoot,
    $Configuration, $Toolchain, $TargetArchitecture)
& "$PSScriptRoot\..\run-m4-test.ps1" @PSBoundParameters
exit $LASTEXITCODE
