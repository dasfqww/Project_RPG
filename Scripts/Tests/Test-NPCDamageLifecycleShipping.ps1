[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $BuildLog,
    [Parameter(Mandatory)] [string] $ProofDirectory,
    [string] $DumpbinPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$objectRoot = Join-Path $projectRoot 'Intermediate\Build\Win64\x64\UnrealGame\Shipping\Project_RPG\SingleFile'
$logPath = [IO.Path]::GetFullPath($BuildLog)
$proofRoot = [IO.Path]::GetFullPath($ProofDirectory)
if (Test-Path -LiteralPath $proofRoot) { throw 'Use a new proof directory; existing evidence is never overwritten.' }
if (-not (Test-Path -LiteralPath $DumpbinPath -PathType Leaf)) { throw 'dumpbin is required.' }
$buildText = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
if ($buildText -notmatch 'Command line: [^\r\n]+ Project_RPG Win64 Shipping\b' -or
    $buildText -notmatch '\s-NoLink\b' -or $buildText -notmatch '(?m)^Result: Succeeded\r?$' -or
    $buildText -match 'error C\d+|fatal error|Result: Failed') {
    throw 'A successful Shipping NoLink build log is required; no link/runtime proof is inferred.'
}
$sources = @('GameplayEffect\GE_ExecCalc_TakeDamage.cpp', 'Tests\RPGSkillDamageSecurityTests.cpp', 'Tests\RPGNPCDamageLifecycleTests.cpp')
$headers = @('Private\Security\RPGDamageExecutionGuard.h', 'Private\Tests\RPGSkillDamageSecurityTestFixture.h',
    'Public\GameplayEffect\GE_ExecCalc_TakeDamage.h', 'Public\Attribute\RPGAttributeSet.h')
if ([regex]::Matches($buildText, '(?m)^\[\d+/\d+\] Compile \[x64\] ').Count -ne 3) {
    throw 'Exactly three actual compile actions are required.'
}
$records = @()
foreach ($source in $sources) {
    $name = [IO.Path]::GetFileName($source)
    if ([regex]::Matches($buildText, '(?m)^\[\d+/\d+\] Compile \[x64\] ' + [regex]::Escape($name) + '(?: \[NoUba\])?\r?$').Count -ne 1) {
        throw "Missing/duplicate actual Shipping compile: $name"
    }
    $sourcePath = Join-Path $projectRoot ('Source\Project_RPG\Private\' + $source)
    $objectPath = Join-Path $objectRoot ($name + '.obj')
    foreach ($dependency in (@($sourcePath) + @($headers | ForEach-Object { Join-Path $projectRoot ('Source\Project_RPG\' + $_) }))) {
        if ((Get-Item -LiteralPath $objectPath).LastWriteTimeUtc -lt (Get-Item -LiteralPath $dependency).LastWriteTimeUtc) {
            throw "Stale Shipping object: $name"
        }
    }
    $objectTime = (Get-Item -LiteralPath $objectPath).LastWriteTimeUtc
    $logItem = Get-Item -LiteralPath $logPath
    if ($objectTime -lt $logItem.CreationTimeUtc -or $objectTime -gt $logItem.LastWriteTimeUtc) {
        throw "Shipping object is not from this build interval: $name"
    }
    $bytes = [IO.File]::ReadAllBytes($objectPath)
    if ($name -ne 'GE_ExecCalc_TakeDamage.cpp') {
        foreach ($encoding in @([Text.Encoding]::ASCII, [Text.Encoding]::Unicode)) {
            $strings = $encoding.GetString($bytes)
            foreach ($marker in @('RPG_NPC_LIFECYCLE', 'ProjectRPG.NPCDamage.', 'ProjectRPG.Security.SkillDamage.',
                '@RPGNPCDamageLifecycle@@', 'FRPGNPCAuthoredDamageLifecycleTest', 'FRPGDeadCombatantDamageTest',
                'FRPGCorpseDamageBudgetTest', 'FRPGRespawnDamageIsolationTest')) {
                if ($strings.Contains($marker)) { throw "Development test marker remains in Shipping: $name / $marker" }
            }
        }
    }
    $symbols = @(& $DumpbinPath /symbols $objectPath)
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed: $name" }
    $symbolText = $symbols -join "`n"
    if ($name -eq 'GE_ExecCalc_TakeDamage.cpp') {
        if ($symbolText -notmatch '\?Execute_Implementation@UGE_ExecCalc_TakeDamage@@') {
            throw 'Production damage execution must remain compiled in Shipping.'
        }
    } elseif ($symbolText -match 'FAutomationTestFramework|FRPG\w+Test|@RPGNPCDamageLifecycle@@') {
        throw "Development automation registration/body remains in Shipping: $name"
    }
    $records += [ordered]@{ original=$objectPath; SHA256=(Get-FileHash -LiteralPath $objectPath -Algorithm SHA256).Hash
        source=$sourcePath; sourceSHA256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash }
}
$headerRecords = @($headers | ForEach-Object {
    $path = Join-Path $projectRoot ('Source\Project_RPG\' + $_)
    [ordered]@{path=$path;SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
})
$logHash = (Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
New-Item -ItemType Directory -Path (Join-Path $proofRoot 'Objects') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $proofRoot 'Sources') | Out-Null
foreach ($record in $records) {
    $record['archive'] = Join-Path $proofRoot ('Objects\' + [IO.Path]::GetFileName($record.original))
    $record['sourceArchive'] = Join-Path $proofRoot ('Sources\' + [IO.Path]::GetFileName($record.source))
    Copy-Item -LiteralPath $record.original -Destination $record.archive
    Copy-Item -LiteralPath $record.source -Destination $record.sourceArchive
    if ((Get-FileHash -LiteralPath $record.archive -Algorithm SHA256).Hash -ne $record.SHA256 -or
        (Get-FileHash -LiteralPath $record.sourceArchive -Algorithm SHA256).Hash -ne $record.sourceSHA256) {
        throw 'Shipping object/source changed while archiving.'
    }
}
foreach ($header in $headerRecords) {
    $header['archive'] = Join-Path $proofRoot ('Sources\' + [IO.Path]::GetFileName($header.path))
    Copy-Item -LiteralPath $header.path -Destination $header.archive
    if ((Get-FileHash -LiteralPath $header.archive -Algorithm SHA256).Hash -ne $header.SHA256) { throw 'Header changed while archiving.' }
}
$logArchive = Join-Path $proofRoot 'build.log'
Copy-Item -LiteralPath $logPath -Destination $logArchive
if ((Get-FileHash -LiteralPath $logArchive -Algorithm SHA256).Hash -ne $logHash) { throw 'Build log changed while archiving.' }
$proof = [ordered]@{schemaVersion=1; shippingNoLinkCompilation=$true; actualCompileActions=3
    shippingLinkVerified=$false; shippingRuntimeVerified=$false; testMarkersAbsent=$true; testRegistrationAbsent=$true
    productionDamageExecutionSymbolPresent=$true; objects=$records; headers=$headerRecords
    buildLog=$logPath; buildLogSHA256=$logHash; buildLogArchive=$logArchive}
$proofPath = Join-Path $proofRoot 'Shipping.proof.json'
$proof | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $proofPath -Encoding UTF8
Write-Host "PASS: three actual Shipping compiles; development lifecycle tests absent, production damage execution present. No link/runtime proof. $proofPath"
