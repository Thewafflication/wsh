[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x86', 'x64', 'arm64')]
    [string]$Architecture,

    [string]$TinyCcVersion = '0.9.28-rc.1444+9a4be30f',
    [string]$WcrtVersion = '1.0.0',
    [string]$Wpm = 'wpm.exe'
)

$ErrorActionPreference = 'Stop'

$tinyCcRelease =
    "https://github.com/Thewafflication/tcc_package/releases/download/v$TinyCcVersion"
$wcrtRelease =
    "https://github.com/Thewafflication/wcrt/releases/download/$WcrtVersion"
$releaseKey = Join-Path $env:TEMP 'wsh-build-wpm-release.public'
$releaseKeySha256 =
    '81eba415ad604016193f875eb041e10252c39d31b3543ac6172330595952cb59'

Invoke-WebRequest -UseBasicParsing "$wcrtRelease/wpm-release.public" `
    -OutFile $releaseKey
$actualKeySha256 = (Get-FileHash -LiteralPath $releaseKey `
    -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualKeySha256 -ne $releaseKeySha256) {
    throw "WPM release-key digest mismatch: $actualKeySha256"
}

& $Wpm trust add $releaseKey
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Wpm repo add $tinyCcRelease
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Wpm repo add $wcrtRelease
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Wpm config set prerelease true --package tinycc
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Wpm update
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Wpm install tinycc --arch $Architecture --version $TinyCcVersion
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Wpm install wcrt --arch any --version $WcrtVersion
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$tinyCcHome = [Environment]::GetEnvironmentVariable('TCC_HOME', 'Machine')
$wcrtHome = [Environment]::GetEnvironmentVariable('WCRT_HOME', 'Machine')
if ([string]::IsNullOrWhiteSpace($tinyCcHome) -or
    (Split-Path -Leaf $tinyCcHome) -ne $TinyCcVersion) {
    throw "TinyCC $TinyCcVersion did not set TCC_HOME: $tinyCcHome"
}
if ([string]::IsNullOrWhiteSpace($wcrtHome) -or
    (Split-Path -Leaf $wcrtHome) -ne $WcrtVersion) {
    throw "WCRT $WcrtVersion did not set WCRT_HOME: $wcrtHome"
}

$compilerPrefix = @{
    x86 = 'i386-win32'
    x64 = 'x86_64-win32'
    arm64 = 'arm64-win32'
}[$Architecture]
$compiler = Join-Path $tinyCcHome "$compilerPrefix-tcc.exe"
$wcrtLibrary = Join-Path $wcrtHome "$Architecture/lib/libwcrt.a"
foreach ($path in @($compiler, $wcrtLibrary)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "A required build dependency file is missing: $path"
    }
}

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_ENV)) {
    "TCC_HOME=$tinyCcHome" >> $env:GITHUB_ENV
    "WCRT_HOME=$wcrtHome" >> $env:GITHUB_ENV
    "WSH_TINYCC_VERSION=$TinyCcVersion" >> $env:GITHUB_ENV
    "WSH_WCRT_VERSION=$WcrtVersion" >> $env:GITHUB_ENV
}

[PSCustomObject]@{
    Architecture = $Architecture
    TinyCcVersion = $TinyCcVersion
    TinyCc = $compiler
    WcrtVersion = $WcrtVersion
    WcrtLibrary = $wcrtLibrary
    ReleaseKeySha256 = $actualKeySha256
}
