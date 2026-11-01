$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Network\NPCPickupBackendSnapshot.ps1')
$testRoot=Join-Path (Split-Path $PSScriptRoot -Parent) ('..\Saved\Tests\NPCPickupBackend\snapshot-tests-'+[Guid]::NewGuid().ToString('N'))
$testRoot=[IO.Path]::GetFullPath($testRoot)
$count=0
function New-Fixture {
    $root=Join-Path $testRoot ([Guid]::NewGuid().ToString('N').Substring(0,6))
    $source=Join-Path $root 'Backend\ProjectRpg.Backend'
    New-Item -ItemType Directory -Path $source,(Join-Path $root 'Backend\Database'),(Join-Path $source 'bin'),(Join-Path $source 'obj') -Force | Out-Null
    '<Project Sdk="Microsoft.NET.Sdk.Web"><PropertyGroup><TargetFramework>net9.0</TargetFramework></PropertyGroup><ItemGroup><Content Include="..\Database\schema.sql" /></ItemGroup></Project>' | Set-Content -LiteralPath (Join-Path $source 'ProjectRpg.Backend.csproj') -Encoding UTF8
    'class Fixture {}' | Set-Content -LiteralPath (Join-Path $source 'Fixture.cs') -Encoding UTF8
    '{"fixture":true}' | Set-Content -LiteralPath (Join-Path $source 'appsettings.json') -Encoding UTF8
    'SELECT 1;' | Set-Content -LiteralPath (Join-Path $root 'Backend\Database\schema.sql') -Encoding UTF8
    'must not be compiled' | Set-Content -LiteralPath (Join-Path $source 'bin\Stale.cs') -Encoding UTF8
    'must not be compiled' | Set-Content -LiteralPath (Join-Path $source 'obj\Stale.cs') -Encoding UTF8
    [pscustomobject]@{root=$root;source=$source;project=(Join-Path $source 'ProjectRpg.Backend.csproj');destination=(Join-Path $root 'Saved\Tests\NPCPickupBackend\owned\backend-source')}
}
function Assert-Rejected([string]$Name,[scriptblock]$Action) {
    $threw=$false; try { & $Action } catch { $threw=$true }
    if (-not $threw) { throw "Accepted unsafe backend snapshot: $Name" }; $script:count++
}
try {
$f=New-Fixture
$snapshot=New-NPCPickupBackendSnapshot $f.root $f.destination
if ($snapshot.inputs.Count -ne 4 -or $snapshot.restoreMetadata.Count -ne 0 -or -not (Test-Path -LiteralPath $snapshot.projectFile)) { throw 'Incorrect default input inventory.' }; $count++
foreach ($inputFile in $snapshot.inputs) { if ((Get-FileHash -LiteralPath $inputFile.path).Hash -cne $inputFile.SHA256) { throw 'Copy/hash mismatch.' } }; $count++
if (-not (Test-Path -LiteralPath (Join-Path $f.destination 'Database\schema.sql')) -or
    (Test-Path -LiteralPath (Join-Path $f.destination 'ProjectRpg.Backend\bin')) -or
    (Test-Path -LiteralPath (Join-Path $f.destination 'ProjectRpg.Backend\obj'))) { throw 'Stale outputs copied or schema link missing.' }; $count++
'changed after capture' | Set-Content -LiteralPath (Join-Path $f.source 'Fixture.cs')
'SELECT 2;' | Set-Content -LiteralPath (Join-Path $f.root 'Backend\Database\schema.sql')
foreach ($inputFile in $snapshot.inputs) { if ((Get-FileHash -LiteralPath $inputFile.path).Hash -cne $inputFile.SHA256) { throw 'Live edits contaminated frozen input.' } }; $count++
Assert-Rejected 'overwrite existing snapshot' { New-NPCPickupBackendSnapshot $f.root $f.destination }
$f=New-Fixture
Assert-Rejected 'outside owned test path' { New-NPCPickupBackendSnapshot $f.root (Join-Path $f.root 'outside') }
Assert-Rejected 'unrestored NoBackendRestore' { New-NPCPickupBackendSnapshot $f.root $f.destination -ReuseRestoreCache }
foreach ($name in @('project.assets.json','ProjectRpg.Backend.csproj.nuget.g.props','ProjectRpg.Backend.csproj.nuget.g.targets')) { 'fixture cache bytes' | Set-Content -LiteralPath (Join-Path $f.source "obj\$name") }
$snapshot=New-NPCPickupBackendSnapshot $f.root $f.destination -ReuseRestoreCache
if ($snapshot.restoreMetadata.Count -ne 3) { throw 'Restore metadata missing.' }; $count++
foreach ($inputFile in $snapshot.restoreMetadata) { if ((Get-FileHash -LiteralPath $inputFile.path).Hash -cne $inputFile.SHA256) { throw 'Restore metadata mismatch.' } }; $count++
foreach ($item in @('<ProjectReference Include="..\Shared.csproj" />','<Import Project="outside.props" />','<Compile Include="..\Shared.cs" />','<Content Include="..\outside.json" />')) {
    $f=New-Fixture
    ('<Project><ItemGroup>'+ $item +'</ItemGroup></Project>') | Set-Content -LiteralPath $f.project
    Assert-Rejected 'uncaptured explicit build dependency' { New-NPCPickupBackendSnapshot $f.root $f.destination }
}
$f=New-Fixture
'<Project />' | Set-Content -LiteralPath (Join-Path $f.root 'Directory.Build.props')
Assert-Rejected 'uncaptured parent build import' { New-NPCPickupBackendSnapshot $f.root $f.destination }
$f=New-Fixture
$script:raceInput=Join-Path $f.source 'Fixture.cs'
function Copy-Item {
    param([string]$LiteralPath,[string]$Destination)
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination
    'concurrent edit' | Set-Content -LiteralPath $script:raceInput
}
try { Assert-Rejected 'concurrent source mutation during capture' { New-NPCPickupBackendSnapshot $f.root $f.destination } }
finally { Remove-Item -LiteralPath 'Function:\Copy-Item' }
$f=New-Fixture
$migration=Join-Path $f.root 'Backend\Database\Migrations\0002_item_equipment_progression.sql'
New-Item -ItemType Directory -Path (Split-Path -Parent $migration) | Out-Null
'ALTER TABLE item_records ADD COLUMN equipment_progression JSONB;' | Set-Content -LiteralPath $migration -Encoding UTF8
'<Project><ItemGroup><Content Include="..\Database\schema.sql" /><Content Include="..\Database\Migrations\0002_item_equipment_progression.sql" /></ItemGroup></Project>' | Set-Content -LiteralPath $f.project -Encoding UTF8
$snapshot=New-NPCPickupBackendSnapshot $f.root $f.destination
if ($snapshot.inputs.Count -ne 5 -or !(Test-Path -LiteralPath (Join-Path $f.destination 'Database\Migrations\0002_item_equipment_progression.sql')) -or
    (Get-FileHash -LiteralPath $migration).Hash -cne (Get-FileHash -LiteralPath (Join-Path $f.destination 'Database\Migrations\0002_item_equipment_progression.sql')).Hash) {throw 'Version 2 migration input was not copied exactly'}
$count++
Write-Host "PASS: $count backend input snapshot cases"
} finally {
    $allowedRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Saved/Tests/NPCPickupBackend'))
    if ([IO.Path]::GetDirectoryName($testRoot) -ne $allowedRoot -or [IO.Path]::GetFileName($testRoot) -cnotmatch '^snapshot-tests-[a-f0-9]{32}$') {throw 'Fixture cleanup boundary mismatch'}
    . (Join-Path $PSScriptRoot '../Network/GuardianMelee2Artifacts.ps1')
    $stats=Remove-GuardianProbeArtifact $testRoot $allowedRoot
    Write-Host ('SNAPSHOT_FIXTURE_CLEANUP deletedBytes='+$stats.deletedBytes+' remaining='+(Test-Path -LiteralPath $testRoot))
}
