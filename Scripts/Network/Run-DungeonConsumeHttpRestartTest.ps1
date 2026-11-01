[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(60,600)][int]$TimeoutSeconds = 300,
    [switch]$Postgres,
    [switch]$CancelUnconsumed,
    [string]$ConnectionString = 'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only',
    [switch]$NoRestore
)

# Two (or three for cancellation) owned UE processes; production HTTP APIs; isolated journal and optional
# loopback-owned PostgreSQL schema. Does not start/stop Docker or other editors.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $projectRoot 'Project_RPG.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
foreach ($required in @($project,$editor,$module)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing prerequisite: $required" }
}
$runRoot = Join-Path $projectRoot ('Saved\Tests\DungeonConsumeHttpRestart\' + [Guid]::NewGuid().ToString('N'))
$buildRoot = Join-Path $runRoot 'backend-build'
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$testProject = Join-Path $projectRoot 'Backend\ProjectRpg.Backend.DungeonAbandonmentTests\ProjectRpg.Backend.DungeonAbandonmentTests.csproj'
$buildArguments = @('build',$testProject,'-c','Release','-o',$buildRoot,'-p:UseAppHost=false','--nologo')
if ($NoRestore) { $buildArguments += '--no-restore' }
& dotnet @buildArguments
if ($LASTEXITCODE -ne 0) { throw "Backend fixture build failed: $runRoot" }
$arguments = @((Join-Path $buildRoot 'ProjectRpg.Backend.DungeonAbandonmentTests.dll'),
    '--ue-consumption-recovery','--ue-project',$project,'--ue-editor',$editor,
    '--run-directory',$runRoot,'--timeout-seconds',$TimeoutSeconds.ToString())
if ($CancelUnconsumed) { $arguments += '--cancel-unconsumed' }
$previousConnection = $env:PROJECT_RPG_ABANDON_TEST_POSTGRES
try {
    if ($Postgres) { $env:PROJECT_RPG_ABANDON_TEST_POSTGRES = $ConnectionString; $arguments += '--require-postgres' }
    & dotnet @arguments
    if ($LASTEXITCODE -ne 0) { throw "HTTP restart recovery failed; inspect $runRoot" }
} finally { $env:PROJECT_RPG_ABANDON_TEST_POSTGRES = $previousConnection }
Write-Host "PASS: HTTP recovery proof: $(Join-Path $runRoot 'proof.json')"
