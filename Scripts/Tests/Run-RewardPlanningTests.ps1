[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$task = 'solid-reward-planning-20261106'
$runId = [Guid]::NewGuid().ToString('N')
$root = Join-Path $workspace 'Saved/BuildChecks/RewardPlanning-20261106'
$proof = Join-Path $root $runId
$tempRoot = Join-Path $root 'Temp'
$temp = Join-Path $tempRoot $runId
$inputs = Join-Path $temp 'Inputs'
$containerName = 'rpg-reward-plan-' + $runId
$containerId = $null
$failure = $null
$overlay = @(
    'Backend/ProjectRpg.Backend/Application/DungeonRewardSettlementWorker.cs',
    'Backend/ProjectRpg.Backend/Application/DungeonRewardCommitPlanner.cs',
    'Backend/ProjectRpg.Backend/Domain/DungeonRewardIdentity.cs',
    'Backend/ProjectRpg.Backend/Api/DungeonRewardSnapshotApiEndpoints.cs',
    'Backend/ProjectRpg.Backend.RewardPlanningTests/ProjectRpg.Backend.RewardPlanningTests.csproj',
    'Backend/ProjectRpg.Backend.RewardPlanningTests/Program.cs'
)
$environmentNames = @('PROJECT_RPG_EQUIPMENT_TEST_RUN_ID','PROJECT_RPG_EQUIPMENT_TEST_POSTGRES',
    'PROJECT_RPG_PERSONAL_LOOT_TEST_POSTGRES')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$result = [ordered]@{ Passed=$false; Head=''; Hashes=@{}; Checks=@(); Cleanup=$null; CleanupComplete=$false }
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Assert-Reservation {
    if (!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 -Pattern '^\|[^|]* / solid-reward-planning-20261106 \|')) { throw 'Reservation missing' }
}
function Invoke-Dotnet([string[]]$Arguments, [string]$Label) {
    & dotnet @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $proof ($Label+'.log'))
    if ($LASTEXITCODE -ne 0) { throw ('dotnet failed: '+$Label) }
    $result.Checks += $Label
}
function Test-Project([string]$Project, [string[]]$Modes) {
    $file = Join-Path $inputs ('Backend/'+$Project+'/'+$Project+'.csproj')
    Invoke-Dotnet @('build',$file,'-c','Release','--nologo','-p:UseSharedCompilation=false') ($Project+'.build')
    $dll = Join-Path $inputs ('Backend/'+$Project+'/bin/Release/net9.0/'+$Project+'.dll')
    foreach ($mode in $Modes) {
        $arguments = @($dll)
        if ($mode) { $arguments += $mode }
        Invoke-Dotnet $arguments ($Project+'.'+$mode.TrimStart('-'))
    }
}
try {
    Assert-Reservation
    $null = New-Item -ItemType Directory -Path $inputs,$proof -Force
    $result.Head = (git -C $workspace rev-parse HEAD).Trim()
    & git -C $workspace archive --format=tar -o (Join-Path $temp 'Backend.tar') $result.Head Backend
    if ($LASTEXITCODE -ne 0) { throw 'Archive failed' }
    & tar -xf (Join-Path $temp 'Backend.tar') -C $inputs
    if ($LASTEXITCODE -ne 0) { throw 'Archive extraction failed' }
    foreach ($relative in $overlay) {
        $source = Join-Path $workspace $relative
        $destination = Join-Path $inputs $relative
        $hash = (Get-FileHash -LiteralPath $source).Hash
        $null = New-Item -ItemType Directory -Path (Split-Path $destination) -Force
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $source).Hash -ne $hash -or (Get-FileHash -LiteralPath $destination).Hash -ne $hash) { throw 'Source changed during snapshot' }
        $result.Hashes[$relative] = $hash
    }
    $password = [Guid]::NewGuid().ToString('N')
    $containerId = & docker run --pull=never --detach --name $containerName --label ('project-rpg.task='+$task) --label ('project-rpg.run='+$runId) --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env 'POSTGRES_DB=equipment_test' --env 'POSTGRES_USER=reward_plan' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine
    if ($LASTEXITCODE -ne 0) { $containerId=$null; throw 'Private PostgreSQL failed to start' }
    $containerId = $containerId.Trim()
    $ready = $false
    for ($attempt=0; $attempt -lt 40; $attempt++) {
        & docker exec $containerId pg_isready -U reward_plan -d equipment_test *> $null
        if ($LASTEXITCODE -eq 0) { $ready=$true; break }
        Start-Sleep -Milliseconds 500
    }
    if (!$ready) { throw 'Private PostgreSQL not ready' }
    & docker exec $containerId createdb -U reward_plan personal_test
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create isolated personal-loot database' }
    $port = (& docker port $containerId 5432/tcp).Trim().Split(':')[-1]
    $connection = 'Host=127.0.0.1;Port='+$port+';Username=reward_plan;Password='+$password
    $env:PROJECT_RPG_EQUIPMENT_TEST_RUN_ID = $runId
    $env:PROJECT_RPG_EQUIPMENT_TEST_POSTGRES = $connection+';Database=equipment_test'
    $env:PROJECT_RPG_PERSONAL_LOOT_TEST_POSTGRES = $connection+';Database=personal_test'
    Test-Project 'ProjectRpg.Backend.RewardPlanningTests' @('')
    Test-Project 'ProjectRpg.Backend.StarterEquipmentTests' @('--equipment-rewards-memory','--equipment-rewards-postgres')
    Test-Project 'ProjectRpg.Backend.ContentProfileTests' @('--personal-loot-only','--personal-loot-postgres-only')
    foreach ($relative in $overlay) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $relative)).Hash -ne $result.Hashes[$relative]) { throw ('Input changed: '+$relative) }
    }
    $result.Passed = $true
} catch { $failure=$_.Exception.Message }
finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
    # Only children whose commands explicitly identify this private snapshot are eligible.
    $owned = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'dotnet.exe' -and $_.CommandLine -and
        $_.CommandLine.Replace('/','\').Contains($temp.Replace('/','\'))
    })
    foreach ($entry in $owned) {
        try { Stop-Process -Id $entry.ProcessId -ErrorAction Stop; Wait-Process -Id $entry.ProcessId -Timeout 10 -ErrorAction SilentlyContinue }
        catch { $failure=([string]$failure+'; owned process cleanup: '+$_.Exception.Message) }
    }
    try {
        Assert-Reservation
        if ($containerId) {
            $info = (& docker inspect $containerId | ConvertFrom-Json)[0]
            if ($info.Id -ne $containerId -or $info.Name -ne ('/'+$containerName) -or
                $info.Config.Labels.'project-rpg.task' -ne $task -or $info.Config.Labels.'project-rpg.run' -ne $runId) { throw 'Container ownership mismatch' }
            & docker rm --force $containerId | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Private PostgreSQL cleanup failed' }
        }
        $remaining = @(Get-CimInstance Win32_Process | Where-Object {
            $_.Name -eq 'dotnet.exe' -and $_.CommandLine -and $_.CommandLine.Replace('/','\').Contains($temp.Replace('/','\'))
        })
        if ($remaining.Count) { throw 'Private process still using temporary inputs' }
        if (Test-Path -LiteralPath $temp) { $result.Cleanup = Remove-GuardianProbeArtifact $temp $tempRoot }
    } catch { $failure=([string]$failure+'; cleanup: '+$_.Exception.Message) }
    $result.CleanupComplete = !(Test-Path -LiteralPath $temp)
    $result.Failure = $failure
    $result.Passed = $result.Passed -and !$failure -and $result.CleanupComplete
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $proof 'Result.json') -Encoding UTF8
    Write-Output ('Passed='+$result.Passed+'; Proof='+ (Join-Path $proof 'Result.json'))
}
if (!$result.Passed) { throw $failure }
