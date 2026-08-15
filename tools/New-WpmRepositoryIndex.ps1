[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageDirectory,

    [Parameter(Mandatory)]
    [string]$ReleaseTag,

    [Parameter(Mandatory)]
    [string]$Repository,

    [string]$OutputDirectory = $PackageDirectory
)

$ErrorActionPreference = 'Stop'

$semanticVersion =
    '^v?\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$'
if ($ReleaseTag -notmatch $semanticVersion) {
    throw "The release tag is not semantic: $ReleaseTag"
}
if ($Repository -notmatch '^[0-9A-Za-z_.-]+/[0-9A-Za-z_.-]+$') {
    throw "The GitHub repository name is invalid: $Repository"
}

$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).Path
if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
}
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$expectedVersion = $ReleaseTag -replace '^v', ''
$architectures = @('x86', 'x64', 'arm64')
$packages = foreach ($architecture in $architectures) {
    $matchingPackages = @(Get-ChildItem -LiteralPath $packageRoot -File |
        Where-Object Name -Match `
            "^wsh-$architecture-(?!debug-).+\.zip$")
    if ($matchingPackages.Count -ne 1) {
        throw "Expected one $architecture release package; found " +
            $matchingPackages.Count
    }

    $package = $matchingPackages[0]
    if ($package.Name -notmatch "^wsh-$architecture-(.+)\.zip$") {
        throw "The release package name is invalid: $($package.Name)"
    }
    if ($Matches[1] -ne $expectedVersion) {
        throw "Package version $($Matches[1]) does not match $ReleaseTag."
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($package.FullName)
    try {
        $metadataEntry = $archive.GetEntry('.wpm/package.txt')
        if (-not $metadataEntry) {
            throw "Package metadata is missing: $($package.Name)"
        }
        $reader = [IO.StreamReader]::new($metadataEntry.Open())
        try {
            $metadata = @{}
            foreach ($line in ($reader.ReadToEnd() -split "`r?`n")) {
                if ($line -match '^([^=]+)=(.*)$') {
                    $metadata[$Matches[1]] = $Matches[2]
                }
            }
        } finally {
            $reader.Dispose()
        }
    } finally {
        $archive.Dispose()
    }

    if ($metadata.name -ne 'wsh' -or
        $metadata.version -ne $expectedVersion -or
        $metadata.arch -ne $architecture -or
        $metadata.debug -ne 'false') {
        throw "Package metadata does not match its release asset: " +
            $package.Name
    }

    [ordered]@{
        name = 'wsh'
        version = $expectedVersion
        arch = $architecture
        url = "https://github.com/$Repository/releases/download/" +
            "$ReleaseTag/$($package.Name)"
    }
}

$index = [ordered]@{
    version = 1
    packages = @($packages)
}
$indexPath = Join-Path $outputRoot 'index.json'
$repositoryPath = Join-Path $outputRoot 'repository.json'
$indexJson = $index | ConvertTo-Json -Depth 4
$indexJson | Set-Content -LiteralPath $indexPath -Encoding utf8NoBOM
$indexJson | Set-Content -LiteralPath $repositoryPath -Encoding utf8NoBOM

Get-Item -LiteralPath $indexPath, $repositoryPath
