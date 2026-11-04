param([string[]]$AdditionalOverlay = @(), [string[]]$AdditionalProjects = @())
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$task = 'solid-boundaries-20261105'
$runId = [Guid]::NewGuid().ToString('N')
$root = Join-Path $workspace 'Saved/BuildChecks/SolidBackend-20261105'
$temp = Join-Path $root ('Temp/' + $runId)
$proof = Join-Path $root $runId
$inputs = Join-Path $temp 'Inputs'
$container = 'rpg-solid-' + $runId
$containerId = $null
$oldConnection = $env:PROJECT_RPG_PICKUP_INTENT_TEST_POSTGRES
$oldRun = $env:PROJECT_RPG_PICKUP_INTENT_TEST_RUN
$overlay = @(
 'Backend/ProjectRpg.Backend/Application/DungeonPickupCommitService.cs',
 'Backend/ProjectRpg.Backend/Data/IDungeonPickupCommitRepository.cs',
 'Backend/ProjectRpg.Backend/Data/DungeonPickupCommitRepositories.cs',
 'Backend/ProjectRpg.Backend/Program.cs',
 'Backend/ProjectRpg.Backend.PickupIntentTests/Program.cs',
 'Backend/ProjectRpg.Backend.PickupIntentTests/CommitChecks.cs',
 'Backend/ProjectRpg.Backend.PickupIntentTests/PickupCommitBoundaryTests.cs'
) + $AdditionalOverlay | Sort-Object -Unique
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
$result = [ordered]@{ Passed=$false; Head=''; Hashes=@{}; Tests=@(); RemovedBytes=0; CleanupComplete=$false }
function Check-Reservation {
 if (!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 -Pattern '^\|[^|]*solid-boundaries-20261105')) { throw 'Reservation missing' }
}
function Invoke-Dotnet([string[]]$Arguments, [string]$Log) {
 & dotnet @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $proof $Log)
 if ($LASTEXITCODE -ne 0) { throw "dotnet failed: $Log" }
}
try {
 Check-Reservation
 $null = New-Item -ItemType Directory -Path $inputs,$proof -Force
 $result.Head = (& git -C $workspace rev-parse HEAD).Trim()
 & git -C $workspace archive --format=tar -o (Join-Path $temp 'Backend.tar') HEAD Backend
 if ($LASTEXITCODE -ne 0) { throw 'Archive failed' }
 & tar -xf (Join-Path $temp 'Backend.tar') -C $inputs
 if ($LASTEXITCODE -ne 0) { throw 'Archive extraction failed' }
 foreach ($relative in $overlay) {
  $source = [IO.Path]::GetFullPath((Join-Path $workspace $relative))
  $destination = [IO.Path]::GetFullPath((Join-Path $inputs $relative))
  if (!$source.StartsWith(($workspace + '\Backend\'),[StringComparison]::OrdinalIgnoreCase) -or !$destination.StartsWith(($inputs + '\Backend\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Overlay outside backend' }
  $null = New-Item -ItemType Directory -Path (Split-Path $destination) -Force
  Copy-Item -LiteralPath $source -Destination $destination
  $result.Hashes[$relative] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
 }
 $password = [Guid]::NewGuid().ToString('N')
 $database = 'rpg_solid_' + $runId
 $containerId = & docker run --pull=never --detach --name $container --label ('project-rpg.task='+$task) --label ('project-rpg.run='+$runId) --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env ('POSTGRES_DB='+$database) --env 'POSTGRES_USER=solid_test' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine
 if ($LASTEXITCODE -ne 0) { $containerId=$null; throw 'Private PostgreSQL failed to start' }
 $containerId = $containerId.Trim()
 $ready = $false
 for ($attempt=0; $attempt -lt 30; $attempt++) {
  & docker exec $containerId pg_isready -U solid_test -d $database *> $null
  if ($LASTEXITCODE -eq 0) { $ready=$true; break }; Start-Sleep -Milliseconds 500
 }
 if (!$ready) { throw 'Private PostgreSQL not ready' }
 $port = (& docker port $containerId 5432/tcp).Trim().Split(':')[-1]
 $env:PROJECT_RPG_PICKUP_INTENT_TEST_POSTGRES = "Host=127.0.0.1;Port=$port;Database=$database;Username=solid_test;Password=$password"
 $env:PROJECT_RPG_PICKUP_INTENT_TEST_RUN = $runId
 foreach ($project in (@('ProjectRpg.Backend.PickupIntentTests') + $AdditionalProjects | Select-Object -Unique)) {
  if ($project -notmatch '^ProjectRpg\.[A-Za-z.]+$') { throw 'Invalid project name' }
  $projectPath = Join-Path $inputs "Backend/$project/$project.csproj"
  Invoke-Dotnet @('build',$projectPath,'-c','Release','--nologo') ($project+'.build.log')
  $dll = Join-Path $inputs "Backend/$project/bin/Release/net9.0/$project.dll"
  $arguments = @($dll)
  if ($project -eq 'ProjectRpg.Backend.PickupIntentTests') { $arguments += '--require-postgres' }
  Invoke-Dotnet $arguments ($project+'.test.log')
  $result.Tests += $project
 }
 foreach ($relative in $overlay) {
  if ((Get-FileHash -LiteralPath (Join-Path $workspace $relative) -Algorithm SHA256).Hash -ne $result.Hashes[$relative]) { throw "Source changed during verification: $relative" }
 }
 $result.Passed=$true
} finally {
 $env:PROJECT_RPG_PICKUP_INTENT_TEST_POSTGRES=$oldConnection
 $env:PROJECT_RPG_PICKUP_INTENT_TEST_RUN=$oldRun
 Check-Reservation
 if ($containerId) {
  $info = (& docker inspect $containerId | ConvertFrom-Json)[0]
  if ($info.Id -ne $containerId -or $info.Name -ne ('/'+$container) -or $info.Config.Labels.'project-rpg.task' -ne $task -or $info.Config.Labels.'project-rpg.run' -ne $runId) { throw 'Container ownership mismatch' }
  & docker rm --force $containerId | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'Private container cleanup failed' }
 }
 if (Test-Path -LiteralPath $temp) {
  $result.RemovedBytes = (Get-ChildItem -LiteralPath $temp -File -Recurse | Measure-Object Length -Sum).Sum
  Remove-GuardianProbeArtifact $temp (Join-Path $root 'Temp')
 }
 $result.CleanupComplete = !(Test-Path -LiteralPath $temp)
 $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $proof 'Result.json') -Encoding UTF8
 Write-Output ('Result: '+(Join-Path $proof 'Result.json'))
}
