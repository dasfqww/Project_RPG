[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $BuildLog,
    [Parameter(Mandatory)] [string] $ProofDirectory,
    [string] $DumpbinPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$objectRoot = Join-Path $projectRoot 'Intermediate\Build\Win64\x64\UnrealGame\Shipping\Project_RPG\SingleFile'
$buildBytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($BuildLog))
$encoding = if ($buildBytes.Length -ge 2 -and $buildBytes[0] -eq 255 -and $buildBytes[1] -eq 254) {
    [Text.Encoding]::Unicode
} elseif ($buildBytes.Length -ge 2 -and $buildBytes[0] -eq 254 -and $buildBytes[1] -eq 255) {
    [Text.Encoding]::BigEndianUnicode
} else { [Text.Encoding]::UTF8 }
$buildText = $encoding.GetString($buildBytes)
$sources = @('Tests\RPGSkillEndNetworkObservation.cpp', 'Component\RPGAbilitySystemEndObservation.cpp',
    'Component\RPGAbilitySystemTargetDataSecurity.cpp', 'Skill\RPGGameplayAbility_SkillContainer.cpp', 'Tests\RPGSkillNetworkE2EProbeComponent.cpp')
$headers = @((Join-Path $projectRoot 'Source\Project_RPG\Private\Tests\RPGSkillEndNetworkObservation.h'),
    (Join-Path $projectRoot 'Source\Project_RPG\Public\Component\RPGAbilitySystemComponent.h'))
if ($buildText -notmatch 'Project_RPG Win64 Shipping' -or $buildText -notmatch '\s-NoLink\b' -or
    $buildText -notmatch '(?m)^Result: Succeeded\r?$' -or $buildText -match 'error C\d+|fatal error|Result: Failed') {
    throw 'A successful Shipping NoLink build log is required; no link/runtime proof is inferred.'
}
foreach ($source in $sources) {
    if ([regex]::Matches($buildText, '(?m)^\[\d+/\d+\] Compile \[x64\] ' + [regex]::Escape([IO.Path]::GetFileName($source)) + '(?: \[NoUba\])?\r?$').Count -ne 1) {
        throw "Missing actual compile action: $source"
    }
}
if (Test-Path -LiteralPath $ProofDirectory) { throw 'Use a new proof directory; existing evidence is never overwritten.' }
if (-not (Test-Path -LiteralPath $DumpbinPath -PathType Leaf)) { throw 'dumpbin is required.' }

function Get-BytesHash([byte[]] $Bytes) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($Bytes)).Replace('-', '') }
    finally { $hasher.Dispose() }
}
function Read-CodeSection([byte[]] $Bytes, [string[]] $Symbols, [string] $Prefix) {
    $rows = @($Symbols | ForEach-Object {
        $row = [regex]::Match($_, '^\s*[0-9A-F]+\s+(?<offset>[0-9A-F]{8})\s+SECT(?<section>[0-9A-F]+)\s+notype\s+\(\)\s+External\s+\|\s+' + [regex]::Escape($Prefix))
        if ($row.Success) { $row }
    })
    if ($rows.Count -ne 1 -or $Bytes.Length -lt 56) { throw "Missing/ambiguous/truncated function: $Prefix" }
    $big = [BitConverter]::ToUInt16($Bytes,0) -eq 0 -and [BitConverter]::ToUInt16($Bytes,2) -eq 65535
    $count = if ($big) { [BitConverter]::ToUInt32($Bytes,44) } else { [BitConverter]::ToUInt16($Bytes,2) }
    $first = if ($big) { 56 } else { 20 + [BitConverter]::ToUInt16($Bytes,16) }
    $index = [Convert]::ToInt32($rows[0].Groups['section'].Value,16)
    $offset = [Convert]::ToInt32($rows[0].Groups['offset'].Value,16)
    $header = $first + ($index-1)*40
    if ($index -lt 1 -or $index -gt $count -or $header+40 -gt $Bytes.Length) { throw 'Invalid COFF section.' }
    $length = [BitConverter]::ToUInt32($Bytes,$header+16)
    $start = [BitConverter]::ToUInt32($Bytes,$header+20)
    if ($offset -ne 0 -or $length -lt 1 -or $length -gt 64 -or [uint64]$start+$length -gt $Bytes.Length) { throw 'Expected an isolated small Shipping function.' }
    return @{ code=[BitConverter]::ToString($Bytes,$start,$length).Replace('-',' '); header=$header; big=$big }
}
function Read-SuperRelocation([byte[]] $Bytes, [hashtable] $Section) {
    $header = $Section.header
    $count = [BitConverter]::ToUInt16($Bytes,$header+32)
    $ptr = [BitConverter]::ToUInt32($Bytes,$header+24)
    if ($count -ne 1 -or [uint64]$ptr+10 -gt $Bytes.Length -or
        [BitConverter]::ToUInt32($Bytes,$ptr) -ne 27 -or [BitConverter]::ToUInt16($Bytes,$ptr+8) -ne 4) {
        throw 'Expected exactly one REL32 relocation at the superclass call.'
    }
    $symbolIndex = [BitConverter]::ToUInt32($Bytes,$ptr+4)
    $table = if ($Section.big) { [BitConverter]::ToUInt32($Bytes,48) } else { [BitConverter]::ToUInt32($Bytes,8) }
    $symbolCount = if ($Section.big) { [BitConverter]::ToUInt32($Bytes,52) } else { [BitConverter]::ToUInt32($Bytes,12) }
    $size = if ($Section.big) { 20 } else { 18 }
    $symbol = [uint64]$table + [uint64]$symbolIndex*$size
    $stringTable = [uint64]$table + [uint64]$symbolCount*$size
    if ($symbolIndex -ge $symbolCount -or $symbol+$size -gt $Bytes.Length -or $stringTable+4 -gt $Bytes.Length -or
        [BitConverter]::ToUInt32($Bytes,[int]$symbol) -ne 0) { throw 'Invalid superclass symbol.' }
    $stringSize = [BitConverter]::ToUInt32($Bytes,[int]$stringTable)
    $nameOffset = [BitConverter]::ToUInt32($Bytes,[int]$symbol+4)
    if ($nameOffset -lt 4 -or $nameOffset -ge $stringSize -or $stringTable+$stringSize -gt $Bytes.Length) { throw 'Invalid COFF string table.' }
    $start = [int]($stringTable+$nameOffset)
    $end = $start
    while ($end -lt $stringTable+$stringSize -and $Bytes[$end] -ne 0) { ++$end }
    if ($end -eq $stringTable+$stringSize) { throw 'Unterminated superclass symbol.' }
    $name = [Text.Encoding]::ASCII.GetString($Bytes,$start,$end-$start)
    if ($name -ne '?ClientEndAbility_Implementation@UAbilitySystemComponent@@UEAAXUFGameplayAbilitySpecHandle@@UFGameplayAbilityActivationInfo@@@Z') {
        throw 'Shipping override does not call the unmodified GAS superclass implementation.'
    }
    return $name
}

$objects = @(); $functions = @()
foreach ($source in $sources) {
    $name = [IO.Path]::GetFileName($source)
    $path = Join-Path $objectRoot ($name + '.obj')
    $sourcePath = Join-Path $projectRoot ('Source\Project_RPG\Private\' + $source)
    foreach ($dependency in (@($sourcePath) + $headers)) {
        if ((Get-Item $path).LastWriteTimeUtc -lt (Get-Item $dependency).LastWriteTimeUtc) { throw "Stale Shipping object: $name" }
    }
    $bytes = [IO.File]::ReadAllBytes($path)
    foreach ($stringEncoding in @([Text.Encoding]::ASCII,[Text.Encoding]::Unicode)) {
        $strings = $stringEncoding.GetString($bytes)
        foreach ($marker in @('RPG_SKILL_END_ORIGIN','RPGSkillEndOriginE2E','RPG_SKILL_END_RACE ', 'RPGSkillClientEndRaceE2E','?Current@FRPGSkillEndNetworkObservation@@')) {
            if ($strings.Contains($marker)) { throw "Development end-origin/TLS marker remains in Shipping: $name / $marker" }
        }
    }
    if ($name -in @('RPGSkillEndNetworkObservation.cpp','RPGAbilitySystemEndObservation.cpp')) {
        $symbols = @(& $DumpbinPath /symbols $path)
        if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed.' }
        if ($name -eq 'RPGSkillEndNetworkObservation.cpp') {
            foreach ($check in @(
                @('??0FRPGSkillEndNetworkObservation@@','^(48 8B C1 C3)$'),
                @('??1FRPGSkillEndNetworkObservation@@','^(C3|C2 00 00)$'),
                @('?ObserveEnd@FRPGSkillEndNetworkObservation@@','^(C3|C2 00 00)$')
            )) {
                $section = Read-CodeSection $bytes $symbols $check[0]
                if ($section.code -notmatch $check[1]) { throw "Shipping observer is not inert: $($check[0]) / $($section.code)" }
                $functions += [ordered]@{ symbol=$check[0]; rawCodeBytes=$section.code; inert=$true }
            }
        } else {
            $section = Read-CodeSection $bytes $symbols '?ClientEndAbility_Implementation@URPGAbilitySystemComponent@@'
            # MSVC x64: copy the 20-byte by-value activation info, call Super once, return.
            # There is no test scope, conditional branch or changed argument value in Shipping.
            $expected = '48 83 EC 48 41 0F 10 00 41 8B 40 10 4C 8D 44 24 20 89 44 24 30 0F 29 44 24 20 E8 00 00 00 00 48 83 C4 48 C3'
            if ($section.code -ne $expected) { throw "Review the Shipping superclass forwarding body for this toolchain: $($section.code)" }
            $target = Read-SuperRelocation $bytes $section
            $functions += [ordered]@{ symbol='?ClientEndAbility_Implementation@URPGAbilitySystemComponent@@'
                rawCodeBytes=$section.code; superclassForwardOnly=$true; relocationTarget=$target }
        }
    }
    $objects += [ordered]@{ original=$path; SHA256=(Get-BytesHash $bytes); source=$sourcePath
        sourceSHA256=(Get-FileHash $sourcePath -Algorithm SHA256).Hash }
}
if ($functions.Count -ne 4) { throw 'Three inert observer functions and the superclass-only forwarding body are required.' }
New-Item -ItemType Directory -Path (Join-Path $ProofDirectory 'Objects') | Out-Null
foreach ($record in $objects) {
    $archive = Join-Path $ProofDirectory ('Objects\' + [IO.Path]::GetFileName($record.original))
    Copy-Item -LiteralPath $record.original -Destination $archive
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $record.SHA256) { throw 'Object changed while archiving.' }
    $record['archive'] = [IO.Path]::GetFullPath($archive)
}
$proof = [ordered]@{ schemaVersion=1; shippingNoLinkCompilation=$true; shippingLinkVerified=$false
    shippingRuntimeVerified=$false; endOriginDevelopmentAndTLSMarkersAbsent=$true; delayedClientFixtureMarkersAbsent=$true; observerInert=$true
    clientEndSuperclassForwardOnly=$true; functions=$functions; objects=$objects
    headers=@($headers | ForEach-Object { [ordered]@{ path=$_; SHA256=(Get-FileHash $_ -Algorithm SHA256).Hash } })
    buildLog=[IO.Path]::GetFullPath($BuildLog); buildLogSHA256=(Get-BytesHash $buildBytes) }
$proofPath = Join-Path $ProofDirectory 'Shipping.proof.json'
$proof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $proofPath -Encoding UTF8
Write-Host "PASS: five actual Shipping compiles; inert observer, absent development/TLS/fixture markers and unchanged superclass forwarding. No link/runtime proof. $proofPath"
