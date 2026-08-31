param([string]$FuzzSmoke,[string]$ResourceBounds,[string]$DfsRecord,[string]$MatrixRecord,[string]$Executable,[string]$SharedLibrary,[string]$ProjectVersion,[string]$TargetArchitecture,[string]$EvidenceDirectory,[string]$RepositoryRoot,[string]$Configuration,[string]$Toolchain)
& "$PSScriptRoot\run-m9-test.ps1" -TestCase TC-0077 @PSBoundParameters
exit $LASTEXITCODE
