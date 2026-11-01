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
$sources = @(
    'Tests\RPGSkillFinalCapNetworkProbe.cpp',
    'Tests\RPGSkillNetworkE2EProbeComponent.cpp',
    'Security\RPGDamageExecutionGuard.cpp',
    'Skill\RPGGameplayAbility_SkillContainer.cpp'
)
$headerPaths = @(
    (Join-Path $projectRoot 'Source\Project_RPG\Private\Tests\RPGSkillFinalCapNetworkProbe.h'),
    (Join-Path $projectRoot 'Source\Project_RPG\Private\Tests\RPGSkillNetworkE2EProbeComponent.h')
)
if ($buildText -notmatch 'Project_RPG Win64 Shipping' -or $buildText -notmatch '\s-NoLink\b' -or
    $buildText -notmatch '(?m)^Result: Succeeded\r?$' -or $buildText -match 'error C\d+|fatal error|Result: Failed') {
    throw 'A successful Shipping NoLink build log is required; no link/runtime proof is inferred.'
}
foreach ($source in $sources) {
    $name = [IO.Path]::GetFileName($source)
    if ([regex]::Matches($buildText, '(?m)^\[\d+/\d+\] Compile \[x64\] ' + [regex]::Escape($name) + '(?: \[NoUba\])?\r?$').Count -ne 1) {
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
    $pattern = [regex]::Escape('?' + $Function + '@' + $Class + '@@')
    $rows = @($Symbols | ForEach-Object {
        $row = [regex]::Match($_, '^\s*[0-9A-F]+\s+(?<offset>[0-9A-F]{8})\s+SECT(?<section>[0-9A-F]+)\s+notype\s+\(\)\s+External\s+\|\s+' + $pattern)
        if ($row.Success) { $row }
    })
    if ($rows.Count -ne 1) { throw "Missing/ambiguous compiled function: ${Class}::$Function" }
    if ($Bytes.Length -lt 56) { throw 'Truncated COFF header.' }
    $bigobj = [BitConverter]::ToUInt16($Bytes, 0) -eq 0 -and [BitConverter]::ToUInt16($Bytes, 2) -eq 65535
    $sections = if ($bigobj) { [BitConverter]::ToUInt32($Bytes, 44) } else { [BitConverter]::ToUInt16($Bytes, 2) }
    $sectionStart = if ($bigobj) { 56 } else { 20 + [BitConverter]::ToUInt16($Bytes, 16) }
    $section = [Convert]::ToInt32($rows[0].Groups['section'].Value, 16)
    $offset = [Convert]::ToInt32($rows[0].Groups['offset'].Value, 16)
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
foreach ($source in $sources) {
    $name = [IO.Path]::GetFileName($source)
    $path = Join-Path $objectRoot ($name + '.obj')
    $sourcePath = Join-Path $projectRoot ('Source\Project_RPG\Private\' + $source)
    $objectTime = (Get-Item -LiteralPath $path).LastWriteTimeUtc
    foreach ($dependency in (@($sourcePath) + $headerPaths)) {
        if ($objectTime -lt (Get-Item -LiteralPath $dependency).LastWriteTimeUtc) { throw "Stale Shipping object: $name" }
    }
    $bytes = [IO.File]::ReadAllBytes($path)
    foreach ($stringEncoding in @([Text.Encoding]::ASCII, [Text.Encoding]::Unicode)) {
        $strings = $stringEncoding.GetString($bytes)
        foreach ($marker in @('RPG_SKILL_FINALCAP ', 'RPGSkillFinalCapE2E')) {
            if ($strings.Contains($marker)) { throw "Development final-cap marker remains in Shipping: $name / $marker" }
        }
    }
    $checks = if ($name -eq 'RPGSkillFinalCapNetworkProbe.cpp') {
        @(
            @('PrepareProfile', 'FRPGSkillFinalCapNetworkProbe', '^(32 C0 C3|33 C0 C3)$'),
            @('Tick', 'FRPGSkillFinalCapNetworkProbe', '^(C3|C2 00 00)$'),
            @('ObserveExecution', 'FRPGSkillFinalCapNetworkProbe', '^(C3|C2 00 00)$'),
            @('ObserveApplication', 'FRPGSkillFinalCapNetworkProbe', '^(C3|C2 00 00)$')
        )
    } elseif ($name -eq 'RPGSkillNetworkE2EProbeComponent.cpp') {
        @(
            @('ObserveFinalCapExecution', 'URPGSkillNetworkE2EProbeComponent', '^(C3|C2 00 00)$'),
            @('ObserveFinalCapApplication', 'URPGSkillNetworkE2EProbeComponent', '^(C3|C2 00 00)$')
        )
    } else { @() }
    if ($checks.Count -gt 0) {
        $symbols = @(& $DumpbinPath /symbols $path)
        if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed.' }
        foreach ($check in $checks) {
            $code = Read-FunctionBytes $bytes $symbols $check[0] $check[1]
            if ($code -notmatch $check[2]) { throw "Shipping function is not inert: $($check[1])::$($check[0]) / $code" }
            $functions += [ordered]@{ function=($check[1] + '::' + $check[0]); rawCodeBytes=$code }
        }
    }
    $objects += [ordered]@{ original=$path; SHA256=(Get-BytesHash $bytes)
        source=$sourcePath; sourceSHA256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash }
}
if ($functions.Count -ne 6) { throw 'Six inert final-cap helper/observation functions are required.' }

New-Item -ItemType Directory -Path (Join-Path $ProofDirectory 'Objects') | Out-Null
foreach ($record in $objects) {
    $archive = Join-Path $ProofDirectory ('Objects\' + [IO.Path]::GetFileName($record.original))
    Copy-Item -LiteralPath $record.original -Destination $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $record.SHA256) { throw 'Object changed while archiving.' }
    $record['archive'] = [IO.Path]::GetFullPath($archive)
}
$proof = [ordered]@{ schemaVersion=1; shippingNoLinkCompilation=$true; shippingLinkVerified=$false
    shippingRuntimeVerified=$false; finalCapDevelopmentMarkersAbsent=$true; testHelpersAndObserversInert=$true
    functions=$functions; objects=$objects; headers=@($headerPaths | ForEach-Object {
        [ordered]@{ path=$_; SHA256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
    }); buildLog=[IO.Path]::GetFullPath($BuildLog); buildLogSHA256=(Get-BytesHash $buildBytes) }
$proofPath = Join-Path $ProofDirectory 'Shipping.proof.json'
$proof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $proofPath -Encoding UTF8
Write-Host "PASS: four actual Shipping compiles, six inert final-cap functions and absent development markers. No link/runtime proof. $proofPath"
