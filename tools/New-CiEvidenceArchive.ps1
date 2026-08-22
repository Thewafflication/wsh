[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x86', 'x64', 'arm64')]
    [string]$Architecture,

    [Parameter(Mandatory)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,

    [Parameter(Mandatory)]
    [string]$BuildDirectory,

    [Parameter(Mandatory)]
    [string]$SourceRevision,

    [Parameter(Mandatory)]
    [string]$OutputPath,

    [string]$BuildLog,
    [string]$TestLog,
    [string]$JUnitPath,
    [string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string]$Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory)][string]$CachePath,
        [Parameter(Mandatory)][string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
        return $null
    }
    $entry = Get-Content -LiteralPath $CachePath |
        Where-Object { $_ -match "^$([regex]::Escape($Name)):[^=]+=" } |
        Select-Object -First 1
    if ($null -eq $entry) {
        return $null
    }
    ($entry -split '=', 2)[1]
}

function Copy-OptionalFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Destination
    )

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $destinationDirectory = Split-Path -Parent $Destination
        New-Item -ItemType Directory -Force -Path $destinationDirectory |
            Out-Null
        Copy-Item -LiteralPath $Path -Destination $Destination
    }
}

function Copy-OptionalDirectory {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Destination
    )

    if (Test-Path -LiteralPath $Path -PathType Container) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        foreach ($item in Get-ChildItem -LiteralPath $Path -Force) {
            Copy-Item -LiteralPath $item.FullName -Destination $Destination `
                -Recurse -Force
        }
    }
}

$resolvedBuildDirectory = Resolve-ProjectPath $BuildDirectory
$resolvedOutputPath = Resolve-ProjectPath $OutputPath
$cmakeCache = Join-Path $resolvedBuildDirectory 'CMakeCache.txt'
$tinyCcVersion = Get-CMakeCacheValue -CachePath $cmakeCache `
    -Name 'WSH_TINYCC_VERSION'
$wcrtVersion = Get-CMakeCacheValue -CachePath $cmakeCache `
    -Name 'WSH_WCRT_VERSION'
$toolchain = if (-not [string]::IsNullOrWhiteSpace($tinyCcVersion)) {
    "TinyCC $tinyCcVersion / WCRT $wcrtVersion"
} else {
    'TinyCC / WCRT (version metadata unavailable)'
}
$archiveName = [IO.Path]::GetFileNameWithoutExtension($resolvedOutputPath)
$staging = Join-Path $repositoryRoot "out/ci-evidence/$archiveName"
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $staging | Out-Null

$buildEvidence = Join-Path $staging 'build'
$testEvidence = Join-Path $staging 'test'
$binaryEvidence = Join-Path $staging 'binaries'
$packageEvidence = Join-Path $staging 'packages'
New-Item -ItemType Directory -Force -Path @(
    $buildEvidence,
    $testEvidence,
    $binaryEvidence,
    $packageEvidence
) | Out-Null

Copy-OptionalFile -Path (Join-Path $resolvedBuildDirectory 'CMakeCache.txt') `
    -Destination (Join-Path $buildEvidence 'CMakeCache.txt')
if (-not [string]::IsNullOrWhiteSpace($BuildLog)) {
    Copy-OptionalFile -Path (Resolve-ProjectPath $BuildLog) `
        -Destination (Join-Path $buildEvidence 'build.log')
}
if (-not [string]::IsNullOrWhiteSpace($TestLog)) {
    Copy-OptionalFile -Path (Resolve-ProjectPath $TestLog) `
        -Destination (Join-Path $testEvidence 'ctest.log')
}
if (-not [string]::IsNullOrWhiteSpace($JUnitPath)) {
    Copy-OptionalFile -Path (Resolve-ProjectPath $JUnitPath) `
        -Destination (Join-Path $testEvidence 'junit.xml')
}

Copy-OptionalDirectory `
    -Path (Join-Path $resolvedBuildDirectory 'test-evidence') `
    -Destination (Join-Path $testEvidence 'controlled-evidence')
Copy-OptionalDirectory `
    -Path (Join-Path $resolvedBuildDirectory 'Testing/Temporary') `
    -Destination (Join-Path $testEvidence 'ctest-temporary')

$configurationDirectory = Join-Path $resolvedBuildDirectory $Configuration
if (Test-Path -LiteralPath $configurationDirectory -PathType Container) {
    Get-ChildItem -LiteralPath $configurationDirectory -File |
        Where-Object Extension -In '.exe', '.dll', '.lib', '.pdb' |
        Copy-Item -Destination $binaryEvidence
}
$testBinaryDirectory = Join-Path $resolvedBuildDirectory `
    "tests/$Configuration"
if (Test-Path -LiteralPath $testBinaryDirectory -PathType Container) {
    $testBinaryEvidence = Join-Path $binaryEvidence 'tests'
    New-Item -ItemType Directory -Force -Path $testBinaryEvidence |
        Out-Null
    Get-ChildItem -LiteralPath $testBinaryDirectory -File |
        Where-Object Extension -In '.exe', '.pdb' |
        Copy-Item -Destination $testBinaryEvidence
}

if (-not [string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $resolvedPackageDirectory = Resolve-ProjectPath $PackageDirectory
    if (Test-Path -LiteralPath $resolvedPackageDirectory -PathType Container) {
        $debugFlavor = if ($Configuration -eq 'Debug') {
            '-debug-'
        } else {
            '-'
        }
        Get-ChildItem -LiteralPath $resolvedPackageDirectory -File `
            -Filter "wsh-$Architecture$debugFlavor*.zip" |
            Copy-Item -Destination $packageEvidence
    }
}

$testStatus = 'NotRun'
$resolvedJUnitPath = if ([string]::IsNullOrWhiteSpace($JUnitPath)) {
    $null
} else {
    Resolve-ProjectPath $JUnitPath
}
if ($resolvedJUnitPath -and
    (Test-Path -LiteralPath $resolvedJUnitPath -PathType Leaf)) {
    [xml]$junit = Get-Content -LiteralPath $resolvedJUnitPath -Raw
    $failedCases = @($junit.SelectNodes('//testcase[failure or error]'))
    $testStatus = if ($failedCases.Count -eq 0) { 'Pass' } else { 'Fail' }
}

$metadata = [ordered]@{
    product = 'wsh'
    sourceRevision = $SourceRevision
    architecture = $Architecture
    configuration = $Configuration
    toolchain = $toolchain
    tinyCcVersion = $tinyCcVersion
    wcrtVersion = $wcrtVersion
    runnerArchitecture = $env:RUNNER_ARCH
    runnerName = $env:RUNNER_NAME
    runnerOperatingSystem = $env:RUNNER_OS
    githubRunId = $env:GITHUB_RUN_ID
    githubRunAttempt = $env:GITHUB_RUN_ATTEMPT
    testStatus = $testStatus
    createdUtc = [DateTime]::UtcNow.ToString('o')
}
$metadata | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $staging 'metadata.json') `
        -Encoding utf8NoBOM

$checksumLines = foreach ($file in Get-ChildItem $staging -Recurse -File) {
    $relativePath = [IO.Path]::GetRelativePath(
        $staging,
        $file.FullName
    ).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    "$($hash.ToLowerInvariant())  $relativePath"
}
$checksumLines | Sort-Object |
    Set-Content -LiteralPath (Join-Path $staging 'SHA256SUMS') `
        -Encoding ascii

$outputDirectory = Split-Path -Parent $resolvedOutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $resolvedOutputPath) {
    Remove-Item -LiteralPath $resolvedOutputPath -Force
}
Compress-Archive -Path (Join-Path $staging '*') `
    -DestinationPath $resolvedOutputPath -CompressionLevel Optimal

Get-Item -LiteralPath $resolvedOutputPath
