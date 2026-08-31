<#
.SYNOPSIS
Adds a compiled VERSIONINFO resource to an already-linked Windows PE file.

.DESCRIPTION
TinyCC accepts GNU windres objects but not Microsoft .res or cvtres output.
This tool uses the installed Windows SDK rc.exe to compile the controlled
resource, extracts its raw RT_VERSION payload, and installs that payload with
the documented UpdateResourceW API. The PE import table is unchanged.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ResourceCompiler,
    [Parameter(Mandatory)] [string] $ResourceSource,
    [Parameter(Mandatory)] [string] $Target
)

$ErrorActionPreference = 'Stop'
$resourcePath = [IO.Path]::ChangeExtension($ResourceSource, '.res')
& $ResourceCompiler /nologo /fo $resourcePath $ResourceSource
if ($LASTEXITCODE -ne 0) {
    throw "rc.exe failed with exit code $LASTEXITCODE"
}

$bytes = [IO.File]::ReadAllBytes($resourcePath)
function Read-UInt16([int] $Offset) {
    [BitConverter]::ToUInt16($bytes, $Offset)
}
function Read-UInt32([int] $Offset) {
    [BitConverter]::ToUInt32($bytes, $Offset)
}
function Read-Identifier([ref] $Offset) {
    $marker = Read-UInt16 $Offset.Value
    if ($marker -eq 0xffff) {
        $value = Read-UInt16 ($Offset.Value + 2)
        $Offset.Value += 4
        return [int]$value
    }
    $characters = [Collections.Generic.List[char]]::new()
    while (($character = Read-UInt16 $Offset.Value) -ne 0) {
        $characters.Add([char]$character)
        $Offset.Value += 2
    }
    $Offset.Value += 2
    return -join $characters
}

$payload = $null
$language = [uint16]0x0409
$offset = 0
while ($offset + 8 -le $bytes.Length) {
    $dataSize = Read-UInt32 $offset
    $headerSize = Read-UInt32 ($offset + 4)
    if ($headerSize -lt 8 -or $offset + $headerSize + $dataSize -gt $bytes.Length) {
        throw "Malformed compiled resource at byte $offset"
    }
    $cursor = $offset + 8
    $type = Read-Identifier ([ref]$cursor)
    $name = Read-Identifier ([ref]$cursor)
    $cursor = ($cursor + 3) -band -bnot 3
    $cursor += 4 # DataVersion
    $cursor += 2 # MemoryFlags
    $entryLanguage = Read-UInt16 $cursor
    if ($dataSize -gt 0 -and $type -eq 16 -and $name -eq 1) {
        $payload = [byte[]]::new($dataSize)
        [Array]::Copy($bytes, $offset + $headerSize, $payload, 0, $dataSize)
        $language = $entryLanguage
        break
    }
    $offset = ($offset + $headerSize + $dataSize + 3) -band -bnot 3
}
if ($null -eq $payload) {
    throw 'Compiled resource did not contain numeric RT_VERSION/1.'
}

if (-not ('Wsh.NativeResource' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Wsh {
    public static class NativeResource {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr BeginUpdateResource(string file, bool deleteExisting);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name,
            ushort language, byte[] data, uint size);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool EndUpdateResource(IntPtr update, bool discard);
    }
}
'@
}

$targetPath = (Resolve-Path -LiteralPath $Target).Path
$update = [Wsh.NativeResource]::BeginUpdateResource($targetPath, $false)
if ($update -eq [IntPtr]::Zero) {
    throw [ComponentModel.Win32Exception]::new(
        [Runtime.InteropServices.Marshal]::GetLastWin32Error())
}
$committed = $false
try {
    if (-not [Wsh.NativeResource]::UpdateResource(
            $update, [IntPtr]16, [IntPtr]1, $language,
            $payload, [uint32]$payload.Length)) {
        throw [ComponentModel.Win32Exception]::new(
            [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    if (-not [Wsh.NativeResource]::EndUpdateResource($update, $false)) {
        throw [ComponentModel.Win32Exception]::new(
            [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    $committed = $true
} finally {
    if (-not $committed) {
        [void][Wsh.NativeResource]::EndUpdateResource($update, $true)
    }
}
Write-Output "version-resource: $targetPath"
