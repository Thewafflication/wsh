[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable)

$ErrorActionPreference = 'Stop'
$bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Executable))

function Read-U16([int]$Offset) {
    return [BitConverter]::ToUInt16($bytes, $Offset)
}

function Read-U32([int]$Offset) {
    return [BitConverter]::ToUInt32($bytes, $Offset)
}

function Read-U64([int]$Offset) {
    return [BitConverter]::ToUInt64($bytes, $Offset)
}

function Read-AsciiZ([int]$Offset) {
    $end = $Offset
    while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
    if ($end -eq $bytes.Length) { throw 'Unterminated PE import string.' }
    return [Text.Encoding]::ASCII.GetString($bytes, $Offset, $end - $Offset)
}

if ((Read-U16 0) -ne 0x5a4d) { throw 'Artifact is not an MZ image.' }
$pe = [int](Read-U32 0x3c)
if ((Read-U32 $pe) -ne 0x00004550) { throw 'Artifact has no PE signature.' }
$sectionCount = Read-U16 ($pe + 6)
$optionalSize = Read-U16 ($pe + 20)
$optional = $pe + 24
$magic = Read-U16 $optional
$is64 = $magic -eq 0x20b
if (-not $is64 -and $magic -ne 0x10b) {
    throw ('Unexpected PE optional-header magic: 0x{0:x}' -f $magic)
}
$majorOs = Read-U16 ($optional + 40)
$minorOs = Read-U16 ($optional + 42)
if ($majorOs -gt 5 -or ($majorOs -eq 5 -and $minorOs -gt 0)) {
    throw "PE minimum OS version is $majorOs.$minorOs, not 5.0 or older."
}
$directory = $optional + $(if ($is64) { 112 } else { 96 })
$importRva = Read-U32 ($directory + 8)
if ($importRva -eq 0) { throw 'PE image has no import directory.' }
$sections = @()
$sectionOffset = $optional + $optionalSize
for ($index = 0; $index -lt $sectionCount; $index++) {
    $entry = $sectionOffset + $index * 40
    $sections += [pscustomobject]@{
        VirtualSize = Read-U32 ($entry + 8)
        VirtualAddress = Read-U32 ($entry + 12)
        RawSize = Read-U32 ($entry + 16)
        RawOffset = Read-U32 ($entry + 20)
    }
}

function Convert-Rva([uint32]$Rva) {
    foreach ($section in $sections) {
        $size = [Math]::Max($section.VirtualSize, $section.RawSize)
        if ($Rva -ge $section.VirtualAddress -and
            $Rva -lt $section.VirtualAddress + $size) {
            return [int]($section.RawOffset + $Rva -
                $section.VirtualAddress)
        }
    }
    throw ('RVA 0x{0:x} is outside PE sections.' -f $Rva)
}

$dlls = [Collections.Generic.List[string]]::new()
$symbols = [Collections.Generic.List[string]]::new()
$descriptor = Convert-Rva $importRva
while ($true) {
    $originalThunk = Read-U32 $descriptor
    $nameRva = Read-U32 ($descriptor + 12)
    $firstThunk = Read-U32 ($descriptor + 16)
    if ($originalThunk -eq 0 -and $nameRva -eq 0 -and
        $firstThunk -eq 0) { break }
    $dlls.Add((Read-AsciiZ (Convert-Rva $nameRva)))
    $thunkRva = if ($originalThunk -ne 0) { $originalThunk } else { $firstThunk }
    $thunk = Convert-Rva $thunkRva
    $stride = if ($is64) { 8 } else { 4 }
    while ($true) {
        $value = if ($is64) { Read-U64 $thunk } else { Read-U32 $thunk }
        if ($value -eq 0) { break }
        $ordinalMask = if ($is64) {
            [uint64]::Parse('9223372036854775808')
        } `
            else { [Convert]::ToUInt64('80000000', 16) }
        if (($value -band $ordinalMask) -eq 0) {
            $nameOffset = (Convert-Rva ([uint32]$value)) + 2
            $symbols.Add((Read-AsciiZ $nameOffset))
        }
        $thunk += $stride
    }
    $descriptor += 20
}

$unexpectedDlls = @($dlls | Where-Object {
    $_ -notin @('kernel32.dll', 'KERNEL32.dll', 'KERNEL32.DLL')
})
if ($unexpectedDlls.Count -ne 0) {
    throw "Unexpected static DLL imports: $($unexpectedDlls -join ', ')"
}
$forbidden = @(
    'InitializeProcThreadAttributeList',
    'UpdateProcThreadAttribute',
    'DeleteProcThreadAttributeList',
    'CompareStringOrdinal',
    'CreateJobObjectW',
    'SetInformationJobObject',
    'AssignProcessToJobObject',
    'TerminateJobObject',
    'SearchPathW',
    'ShellExecuteW',
    'WinExec')
$present = @($symbols | Where-Object { $_ -in $forbidden })
if ($present.Count -ne 0) {
    throw "Forbidden static imports: $($present -join ', ')"
}
if ('CreateProcessW' -notin $symbols) {
    throw 'CreateProcessW is not present in the native runtime import set.'
}

Write-Output (
    "M5 PE import gate passed: $($symbols.Count) kernel32 symbol(s), " +
    "minimum OS $majorOs.$minorOs.")
