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
# Windows PowerShell Tee-Object emits UTF-16; pwsh commonly emits UTF-8.
$buildEncoding = if ($buildBytes.Length -ge 2 -and $buildBytes[0] -eq 255 -and $buildBytes[1] -eq 254) {
    [Text.Encoding]::Unicode
} elseif ($buildBytes.Length -ge 2 -and $buildBytes[0] -eq 254 -and $buildBytes[1] -eq 255) {
    [Text.Encoding]::BigEndianUnicode
} else { [Text.Encoding]::UTF8 }
$buildText = $buildEncoding.GetString($buildBytes)
$sourceNames = @('RPGSkillDamageNetworkProbe.cpp', 'RPGSkillNetworkE2EProbeComponent.cpp', 'RPGGameplayAbility_SkillContainer.cpp')
if ($buildText -notmatch 'Project_RPG Win64 Shipping' -or $buildText -notmatch '\s-NoLink\b' -or
    $buildText -notmatch '(?m)^Result: Succeeded\r?$' -or $buildText -match 'error C\d+|fatal error|Result: Failed') {
    throw 'A successful Shipping NoLink build log is required; no runtime or link proof is inferred.'
}
foreach ($name in $sourceNames) {
    if ([regex]::Matches($buildText, 'Compile \[x64\] ' + [regex]::Escape($name) + ' \[NoUba\]').Count -ne 1) {
        throw "Missing actual compile action for $name (zero-action builds are not evidence)."
    }
}
if (-not (Test-Path -LiteralPath $DumpbinPath -PathType Leaf)) { throw "Missing dumpbin: $DumpbinPath" }
if (Test-Path -LiteralPath $ProofDirectory) { throw 'Use a new proof directory; existing evidence is never overwritten.' }

function Get-BytesHash([byte[]] $Bytes) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($Bytes)).Replace('-', '') }
    finally { $hasher.Dispose() }
}

function Read-FunctionBytes([byte[]] $Bytes, [string[]] $Symbols, [string] $Function, [string] $Class) {
    $symbolPattern = [regex]::Escape('?' + $Function + '@' + $Class + '@@')
    $matches = @($Symbols | ForEach-Object {
        $row = [regex]::Match($_, '^\s*[0-9A-F]+\s+(?<offset>[0-9A-F]{8})\s+SECT(?<section>[0-9A-F]+)\s+notype\s+\(\)\s+External\s+\|\s+' + $symbolPattern)
        if ($row.Success) { $row }
    })
    if ($matches.Count -ne 1) { throw "Missing/ambiguous compiled function: ${Class}::$Function" }
    # dumpbin supplies the symbol/COMDAT section mapping. Read that section's raw code,
    # supporting both regular COFF and bigobj headers (not the generated RPC wrapper).
    if ($Bytes.Length -lt 56) { throw 'Truncated COFF header.' }
    $bigobj = [BitConverter]::ToUInt16($Bytes, 0) -eq 0 -and [BitConverter]::ToUInt16($Bytes, 2) -eq 65535
    $sections = if ($bigobj) { [BitConverter]::ToUInt32($Bytes, 44) } else { [BitConverter]::ToUInt16($Bytes, 2) }
    $sectionStart = if ($bigobj) { 56 } else { 20 + [BitConverter]::ToUInt16($Bytes, 16) }
    $section = [Convert]::ToInt32($matches[0].Groups['section'].Value, 16)
    $offset = [Convert]::ToInt32($matches[0].Groups['offset'].Value, 16)
    $header = $sectionStart + ($section - 1) * 40
    if ($section -lt 1 -or $section -gt $sections -or $header + 40 -gt $Bytes.Length) { throw 'Invalid COFF section.' }
    $length = [BitConverter]::ToUInt32($Bytes, $header + 16)
    $start = [BitConverter]::ToUInt32($Bytes, $header + 20)
    if ($offset -ne 0 -or $length -lt 1 -or $length -gt 16 -or [uint64]$start + $length -gt $Bytes.Length) {
        throw "Expected an isolated trivial Shipping function: ${Class}::$Function"
    }
    return [BitConverter]::ToString($Bytes, $start, $length).Replace('-', ' ')
}

$objects = @()
$functions = @()
$headerPath = Join-Path $projectRoot 'Source\Project_RPG\Private\Tests\RPGSkillDamageNetworkProbe.h'
foreach ($name in $sourceNames) {
    $path = Join-Path $objectRoot ($name + '.obj')
    $sourcePath = Join-Path $projectRoot ('Source\Project_RPG\Private\' + $(if ($name -eq 'RPGGameplayAbility_SkillContainer.cpp') { 'Skill\' } else { 'Tests\' }) + $name)
    $objectTime = (Get-Item -LiteralPath $path).LastWriteTimeUtc
    if ($objectTime -lt (Get-Item -LiteralPath $sourcePath).LastWriteTimeUtc -or
        $objectTime -lt (Get-Item -LiteralPath $headerPath).LastWriteTimeUtc) { throw "Stale Shipping object: $name" }
    $bytes = [IO.File]::ReadAllBytes($path)
    foreach ($encoding in @([Text.Encoding]::ASCII, [Text.Encoding]::Unicode)) {
        $strings = $encoding.GetString($bytes)
        foreach ($marker in @('RPG_SKILL_MULTI ', 'CLIENT_INPUT_PRIMED', 'RPGSkillMultiPeerE2E')) {
            if ($strings.Contains($marker)) { throw "Development multi-peer marker remains in Shipping: $name / $marker" }
        }
    }
    if ($name -eq 'RPGSkillDamageNetworkProbe.cpp') {
        $symbols = @(& $DumpbinPath /symbols $path)
        if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed.' }
        foreach ($symbol in @('?execServerFixtureReady@ARPGSkillMultiPeerCoordinator@@', '?Z_Construct_UClass_ARPGSkillMultiPeerCoordinator@@')) {
            if (@($symbols | Where-Object { $_.Contains($symbol) -and $_ -match 'SECT[0-9A-F]+\s+notype.*External' }).Count -ne 1) {
                throw 'Expected generated coordinator/RPC metadata to remain; only the implementation is inert.'
            }
        }
        $checks = @(
            @('ServerFixtureReady_Implementation', 'ARPGSkillMultiPeerCoordinator', '^(C3|C2 00 00)$'),
            @('Tick', 'FRPGSkillDamageNetworkProbe', '^(C3|C2 00 00)$'),
            @('ObserveSentTargetData', 'FRPGSkillDamageNetworkProbe', '^(C3|C2 00 00)$'),
            @('ObserveReceivedTargetData', 'FRPGSkillDamageNetworkProbe', '^(C3|C2 00 00)$'),
            @('HoldForAbuseVerification', 'FRPGSkillDamageNetworkProbe', '^(32 C0 C3|33 C0 C3)$')
        )
        foreach ($check in $checks) {
            $code = Read-FunctionBytes $bytes $symbols $check[0] $check[1]
            if ($code -notmatch $check[2]) { throw "Shipping function is not inert: $($check[1])::$($check[0]) / $code" }
            $functions += [ordered]@{ function=($check[1] + '::' + $check[0]); rawCodeBytes=$code }
        }
    }
    $objects += [ordered]@{ original=$path; SHA256=(Get-BytesHash $bytes)
        source=$sourcePath; sourceSHA256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash }
}

New-Item -ItemType Directory -Path (Join-Path $ProofDirectory 'Objects') | Out-Null
foreach ($record in $objects) {
    $archivePath = Join-Path $ProofDirectory ('Objects\' + [IO.Path]::GetFileName($record.original))
    Copy-Item -LiteralPath $record.original -Destination $archivePath
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $record.SHA256) { throw 'Object changed while archiving.' }
    $record['archive'] = [IO.Path]::GetFullPath($archivePath)
}
$proof = [ordered]@{ schemaVersion=1; shippingNoLinkCompilation=$true; shippingRuntimeVerified=$false
    shippingLinkVerified=$false; generatedClassMetadataRemains=$true; multiPeerDevelopmentMarkersAbsent=$true
    functions=$functions; objects=$objects; headerSHA256=(Get-FileHash -LiteralPath $headerPath -Algorithm SHA256).Hash
    buildLog=[IO.Path]::GetFullPath($BuildLog); buildLogSHA256=(Get-BytesHash $buildBytes) }
$proofPath = Join-Path $ProofDirectory 'Shipping.proof.json'
$proof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $proofPath -Encoding UTF8
Write-Host "PASS: three actual Shipping compiles, five inert functions and absent multi-peer development markers. No link/runtime proof. $proofPath"
