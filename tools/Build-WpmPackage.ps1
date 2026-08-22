[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x86', 'x64', 'arm64')]
    [string]$Architecture,

    [Parameter(Mandatory)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,

    [Parameter(Mandatory)]
    [string]$Version,

    [Parameter(Mandatory)]
    [string]$BuildDirectory,

    [string]$TinyCcVersion,
    [string]$WcrtVersion,
    [string]$Wpm = 'wpm.exe',
    [string]$SigningKey,
    [string]$OutputDirectory = 'out/packages'
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

$packageVersion = $Version -replace '^v', ''
$semanticVersion =
    '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$'
if ($packageVersion -notmatch $semanticVersion) {
    throw "The WPM package version is not semantic: $Version"
}

$resolvedBuildDirectory = Resolve-ProjectPath $BuildDirectory
$cmakeCache = Join-Path $resolvedBuildDirectory 'CMakeCache.txt'
if ([string]::IsNullOrWhiteSpace($TinyCcVersion)) {
    $TinyCcVersion = Get-CMakeCacheValue -CachePath $cmakeCache `
        -Name 'WSH_TINYCC_VERSION'
}
if ([string]::IsNullOrWhiteSpace($WcrtVersion)) {
    $WcrtVersion = Get-CMakeCacheValue -CachePath $cmakeCache `
        -Name 'WSH_WCRT_VERSION'
}
$binaryDirectory = Join-Path $resolvedBuildDirectory $Configuration
$executable = Join-Path $binaryDirectory 'wsh.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "$Configuration executable was not found: $executable"
}

$configurationName = $Configuration.ToLowerInvariant()
$staging = Join-Path $repositoryRoot `
    "out/wpm-staging/wsh-$Architecture-$configurationName"
$metadataDirectory = Join-Path $staging '.wpm'
$resolvedOutputDirectory = Resolve-ProjectPath $OutputDirectory
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Force -Path @(
    $staging,
    $metadataDirectory,
    $resolvedOutputDirectory
) | Out-Null

Copy-Item -LiteralPath $executable -Destination $staging
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE') `
    -Destination $staging
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'README.md') `
    -Destination $staging

$sourceRevision = $env:GITHUB_SHA
if ([string]::IsNullOrWhiteSpace($sourceRevision)) {
    $sourceRevision = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw 'Git failed to identify the package source revision.'
    }
}

$isDebug = ($Configuration -eq 'Debug').ToString().ToLowerInvariant()
$metadata = @(
    'name=wsh'
    "version=$packageVersion"
    "arch=$Architecture"
    "debug=$isDebug"
    'description=Waughtal Shell for Windows'
    'maintainer=Jordan Waughtal'
    'homepage=https://github.com/Thewafflication/wsh'
    'repository=https://github.com/Thewafflication/wsh'
    'license=GPL-3.0-or-later'
    "source-version=$Version"
    "source-revision=$sourceRevision"
)
if (-not [string]::IsNullOrWhiteSpace($TinyCcVersion)) {
    $metadata += "tinycc-version=$TinyCcVersion"
}
if (-not [string]::IsNullOrWhiteSpace($WcrtVersion)) {
    $metadata += "wcrt-version=$WcrtVersion"
}
$metadataPath = Join-Path $metadataDirectory 'package.txt'
Set-Content -LiteralPath $metadataPath -Value $metadata -Encoding ascii
Set-Content -LiteralPath (Join-Path $metadataDirectory 'wpmignore.txt') `
    -Value ".wpm/`n" -Encoding ascii

$installDirectory = "%ProgramFiles%\WSH\$packageVersion"
$installScript = @(
    '@echo off'
    'setlocal'
    ('set "WSH_DEST={0}"' -f $installDirectory)
    'if not exist "%WSH_DEST%" mkdir "%WSH_DEST%" || exit /b 1'
    'xcopy "%~dp0..\*" "%WSH_DEST%\" /E /I /Q /Y >nul || exit /b 1'
    'if exist "%WSH_DEST%\.wpm" rmdir /S /Q "%WSH_DEST%\.wpm"'
    'reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v WSH_HOME /t REG_EXPAND_SZ /d "%WSH_DEST%" /f >nul || exit /b 1'
    'exit /b 0'
)
$removeScript = @(
    '@echo off'
    'setlocal'
    ('set "WSH_DEST={0}"' -f $installDirectory)
    'if exist "%WSH_DEST%" rmdir /S /Q "%WSH_DEST%" || exit /b 1'
    'set "WSH_CURRENT="'
    'for /f "tokens=2,*" %%A in (''reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v WSH_HOME 2^>nul'') do set "WSH_CURRENT=%%B"'
    'if /I "%WSH_CURRENT%"=="%WSH_DEST%" reg delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v WSH_HOME /f >nul 2>&1'
    'exit /b 0'
)
Set-Content -LiteralPath (Join-Path $metadataDirectory 'install.cmd') `
    -Value $installScript -Encoding ascii
Set-Content -LiteralPath (Join-Path $metadataDirectory 'remove.cmd') `
    -Value $removeScript -Encoding ascii

$arguments = @('build', $staging, $resolvedOutputDirectory)
if (-not [string]::IsNullOrWhiteSpace($SigningKey)) {
    $resolvedSigningKey = (Resolve-Path -LiteralPath $SigningKey).Path
    $arguments += @('--sign', $resolvedSigningKey)
}

& $Wpm @arguments
if ($LASTEXITCODE -ne 0) {
    throw "WPM failed to build the $Architecture $Configuration package."
}

$debugFlavor = if ($Configuration -eq 'Debug') { '-debug' } else { '' }
$expectedName = "wsh-$Architecture$debugFlavor-$packageVersion.zip"
$packagePath = Join-Path $resolvedOutputDirectory $expectedName
if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
    throw "WPM did not produce the expected package: $packagePath"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($packagePath)
try {
    $entries = @{}
    foreach ($entry in $archive.Entries) {
        $entries[$entry.FullName.Replace('\', '/')] = $entry.Length
    }

    $requiredEntries = @(
        'wsh.exe',
        '.wpm/package.txt',
        '.wpm/install.cmd',
        '.wpm/remove.cmd',
        '.wpm/index.csv'
    )
    if (-not [string]::IsNullOrWhiteSpace($SigningKey)) {
        $requiredEntries += '.wpm/signature.json'
    }
    foreach ($requiredEntry in $requiredEntries) {
        if (-not $entries.ContainsKey($requiredEntry) -or
            $entries[$requiredEntry] -eq 0) {
            throw "WPM package is missing a required entry: $requiredEntry"
        }
    }
} finally {
    $archive.Dispose()
}

Get-Item -LiteralPath $packagePath
