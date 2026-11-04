[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [string]$ExpectedModuleSHA256='',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$TaskId='narrative-dialogue-20261104',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ProofName='NarrativeDialogue-20261104',
    [string]$BackendRoot='')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$BackendRoot){$BackendRoot=Join-Path $workspace 'Backend'}
if(!$ExpectedModuleSHA256){$ExpectedModuleSHA256=(Get-FileHash -LiteralPath (Join-Path $workspace 'Binaries/Win64/UnrealEditor-Project_RPG.dll') -Algorithm SHA256).Hash}
$owner=$TaskId
$runId=[Guid]::NewGuid().ToString('N')
$run=Join-Path $workspace ('Saved/BuildChecks/'+$ProofName+'/'+$runId)
$temp=Join-Path $run 'Temp'
$inputs=Join-Path $temp 'Backend'
$artifacts=Join-Path $temp 'artifacts'
$containerName='rpg-narrative-ui-'+$runId
$containerId=$null; $backend=$null; $backendOut=$null; $backendErr=$null
$copies=[Collections.Generic.List[object]]::new()
$result=[ordered]@{Passed=$false;Run=$runId;ModuleSHA256=$ExpectedModuleSHA256;Phases=@();Revision=$null;Receipts=$null;AltIsolated=$false;GrowthUnchanged=$false;FreshBackend=$false;ContainerRemoved=$false;Cleanup=$null;TempDeleted=$false;Failure=$null}
$null=New-Item -ItemType Directory -Path $inputs -Force
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Assert-Reservation {
    $rows=@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match ('^\|[^|]*'+$owner)})
    if($rows.Count -ne 1){throw 'Own task reservation missing'}
}
function Owned-Container {
    if(!$containerId){return $null}
    $found=(& docker inspect $containerId | ConvertFrom-Json)[0]
    if($LASTEXITCODE -ne 0 -or $found.Id -cne $containerId -or $found.Name -cne ('/'+$containerName) -or
        $found.Config.Labels.'project-rpg.run' -cne $runId -or $found.Config.Labels.'project-rpg.task' -cne $owner){throw 'Container identity changed'}
    return $found
}
function New-BackendProcess([string]$Arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=(Get-Command dotnet).Source; $start.Arguments=$Arguments
    $start.WorkingDirectory=Join-Path $inputs 'ProjectRpg.Backend'
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.WindowStyle='Hidden'
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    foreach($key in @('POSTGRES_CONNECTION_STRING','PROJECT_RPG_BACKEND_ADMIN_TOKEN','STEAM_WEB_API_PUBLISHER_KEY','STEAM_APP_ID','STEAM_TICKET_IDENTITY')){$start.EnvironmentVariables.Remove($key)}
    $start.EnvironmentVariables['ASPNETCORE_ENVIRONMENT']='Development'
    $start.EnvironmentVariables['DOTNET_ENVIRONMENT']='Development'
    $start.EnvironmentVariables['Storage__Provider']='Postgres'
    $start.EnvironmentVariables['ConnectionStrings__Postgres']=$connection
    $start.EnvironmentVariables['Auth__AdminToken']=$adminToken
    $start.EnvironmentVariables['Steam__AllowDevelopmentAuthentication']='true'
    $start.EnvironmentVariables['Narrative__EnableDevelopmentContent']='true'
    $start.EnvironmentVariables['Kestrel__Endpoints__Http__Url']=$url
    $start.EnvironmentVariables['RateLimits__Enabled']='false'
    $child=[Diagnostics.Process]::new(); $child.StartInfo=$start
    if(!$child.Start()){throw 'Backend process did not start'}
    return $child
}
function Stop-Backend {
    if(!$script:backend){return}
    # The retained Process handle identifies our own child, never a port/PID search.
    if(!$script:backend.HasExited){$script:backend.Kill();$script:backend.WaitForExit()}
    $output=$script:backendOut.GetAwaiter().GetResult()+$script:backendErr.GetAwaiter().GetResult()
    # Framework logs contain endpoints/statuses, not bearer tokens or DB settings.
    [IO.File]::WriteAllText((Join-Path $run ('Backend-'+$script:backend.Id+'.log')), $output)
    $script:backend.Dispose();$script:backend=$null
}
function Start-Backend {
    $script:backend=New-BackendProcess ('"'+$backendDll+'"')
    $script:backendOut=$script:backend.StandardOutput.ReadToEndAsync()
    $script:backendErr=$script:backend.StandardError.ReadToEndAsync()
    for($i=0;$i -lt 60;$i++){
        if($script:backend.HasExited){throw 'Backend exited before readiness'}
        try{$null=Invoke-RestMethod -Uri ($url+'/health/ready') -TimeoutSec 2;return}catch{}
        Start-Sleep -Milliseconds 250
    }
    throw 'Backend readiness timed out'
}
function Api([string]$Method,[string]$Path,$Body=$null) {
    $args=@{Uri=($url+'/api'+$Path);Method=$Method;TimeoutSec=20}
    if($headers){$args.Headers=$headers}
    if($null -ne $Body){$args.ContentType='application/json';$args.Body=($Body|ConvertTo-Json -Depth 8 -Compress)}
    Invoke-RestMethod @args
}
function Sql([string]$Query) {
    $answer=& docker exec $containerId psql -X -A -t -v ON_ERROR_STOP=1 -U narrative_ui -d narrative_ui -c $Query
    if($LASTEXITCODE -ne 0){throw 'Own PostgreSQL proof query failed'}
    return ([string]::Join("`n",$answer)).Trim()
}
function Run-Frontend([string]$Phase) {
    $output=@(& (Join-Path $PSScriptRoot 'Run-NarrativeFrontendTests.ps1') -EngineRoot $EngineRoot -ExpectedModuleSHA256 $ExpectedModuleSHA256 -Phase $Phase -BackendUrl ($url+'/api') -CharacterId $character.characterId -SteamId $steam -TaskId $TaskId -ProofName $ProofName)
    $line=@($output | Where-Object {$_ -like 'Result: *'})
    if($line.Count -ne 1){throw 'Missing native proof result'}
    $path=$line[0].Substring(8); $proof=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if(!$proof.Passed -or !$proof.RuntimeDeleted){throw 'HTTP frontend proof failed/unclean'}
    $native=Get-Content -LiteralPath (Join-Path (Split-Path -Parent $path) 'Native.log') -Raw -Encoding UTF8
    if(!$native.Contains('Narrative HTTP persistence verified: '+$Phase)){throw 'Real HTTP completion marker missing'}
    $result.Phases+=@{Phase=$Phase;Proof=$path;ModuleSHA256=$proof.SnapshotModuleSHA256}
    Write-Output ('PASS UE HTTP '+$Phase)
}
try {
    Assert-Reservation
    & docker image inspect postgres:17-alpine --format '{{.Id}}' *> $null
    if($LASTEXITCODE -ne 0){throw 'Docker with cached postgres:17-alpine is required'}
    foreach($directory in @('ProjectRpg.Backend','Database','ProjectRpg.Operations.Core')){
        $from=Join-Path $BackendRoot $directory
        # Newer game hosts reference Operations.Core and compile Razor pages.
        # Older closed narrative snapshots have neither dependency.
        if(!(Test-Path -LiteralPath $from) -and $directory -eq 'ProjectRpg.Operations.Core'){continue}
        foreach($file in Get-ChildItem -LiteralPath $from -File -Recurse | Where-Object {$_.FullName -notmatch '[\\/](bin|obj)[\\/]' -and $_.Extension -in '.cs','.csproj','.sql','.json','.cshtml'}){
            if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked backend input refused'}
            $before=(Get-FileHash -LiteralPath $file.FullName).Hash
            $target=Join-Path $inputs ($directory+'/'+$file.FullName.Substring($from.Length+1))
            $null=New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
            Copy-Item -LiteralPath $file.FullName -Destination $target
            if((Get-FileHash -LiteralPath $file.FullName).Hash -cne $before -or (Get-FileHash -LiteralPath $target).Hash -cne $before){throw 'Backend input changed during snapshot'}
            $copies.Add(@{Path=$file.FullName;SHA256=$before})
        }
    }
    foreach($entry in $copies){if((Get-FileHash -LiteralPath $entry.Path).Hash -cne $entry.SHA256){throw 'Backend input cohort changed'}}
    $copies|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'BackendInputs.json') -Encoding UTF8
    $build=@(& dotnet build (Join-Path $inputs 'ProjectRpg.Backend/ProjectRpg.Backend.csproj') --artifacts-path $artifacts -v minimal 2>&1)
    $build|Set-Content -LiteralPath (Join-Path $run 'BackendBuild.log') -Encoding UTF8
    if($LASTEXITCODE -ne 0){throw 'Isolated backend build failed'}
    $backendDll=Join-Path $artifacts 'bin/ProjectRpg.Backend/debug/ProjectRpg.Backend.dll'
    $password=[Guid]::NewGuid().ToString('N');$adminToken=[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N')
    $created=& docker run --pull=never --detach --name $containerName --label ('project-rpg.run='+$runId) --label ('project-rpg.task='+$owner) --memory 512m --cpus 1 --pids-limit 128 --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env POSTGRES_DB=narrative_ui --env POSTGRES_USER=narrative_ui --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine -c shared_buffers=16MB -c max_connections=32
    if($LASTEXITCODE -ne 0){throw 'Own PostgreSQL failed to start'}
    $containerId=([string]$created).Trim();$info=Owned-Container
    if(@($info.Mounts|Where-Object Type -ne 'tmpfs').Count -ne 0){throw 'Unexpected persistent DB mount'}
    for($i=0;$i -lt 40;$i++){& docker exec $containerId pg_isready -U narrative_ui -d narrative_ui *> $null;if($LASTEXITCODE -eq 0){break};Start-Sleep -Milliseconds 500}
    $binding=[string](& docker port $containerId 5432/tcp)
    if($binding.Trim() -notmatch '^127\.0\.0\.1:(\d+)$'){throw 'Unexpected DB binding'}
    $connection='Host=127.0.0.1;Port='+$Matches[1]+';Database=narrative_ui;Username=narrative_ui;Password='+$password
    $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start()
    $port=$listener.LocalEndpoint.Port;$listener.Stop();$url='http://127.0.0.1:'+$port
    $migration=New-BackendProcess ('"'+$backendDll+'" --migrate-schema')
    try{
        $mo=$migration.StandardOutput.ReadToEndAsync();$me=$migration.StandardError.ReadToEndAsync()
        if(!$migration.WaitForExit(60000)){$migration.Kill();$migration.WaitForExit();throw 'Migration timed out'}
        [IO.File]::WriteAllText((Join-Path $run 'Migration.log'),$mo.GetAwaiter().GetResult()+$me.GetAwaiter().GetResult())
        if($migration.ExitCode -ne 0){throw 'Own DB migration failed'}
    }finally{if(!$migration.HasExited){$migration.Kill();$migration.WaitForExit()};$migration.Dispose()}
    Start-Backend
    $steam='76561198'+([string](Get-Random -Minimum 100000000 -Maximum 999999999))
    $headers=$null;$login=Api POST '/auth/steam-ticket' @{ticket=('dev:'+$steam)}
    $headers=@{Authorization=('Bearer '+$login.accessToken)};$login=$null
    $character=Api POST '/characters' @{name=('N'+$runId.Substring(0,12))}
    $alt=Api POST '/characters' @{name=('A'+$runId.Substring(0,12))}
    $id=[Guid]$character.characterId;$altId=[Guid]$alt.characterId
    # Compare persistent growth/economy state, excluding nondeterministic session/timestamp data.
    $growthQuery="SELECT COALESCE(jsonb_agg(to_jsonb(p) - 'updated_at' ORDER BY character_id)::text,'[]') FROM character_progression p WHERE character_id IN ('$id','$altId');"
    $growthBefore=Sql $growthQuery
    Run-Frontend complete
    $snapshot=Api GET ('/characters/'+$id+'/narrative')
    $other=Api GET ('/characters/'+$altId+'/narrative')
    if($snapshot.revision -cne '4' -or $snapshot.completedChapters -notcontains 'DEV.Prologue' -or $snapshot.storyUnlocks -notcontains 'DEV.Story.Next'){throw 'Backend completion mismatch'}
    if($other.revision -cne '0' -or $other.quests.Count -ne 0){throw 'Alt state leaked'}
    $result.AltIsolated=$true;$result.Revision=$snapshot.revision
    $firstBackend=$backend.Id;Stop-Backend;Start-Backend
    if($backend.Id -eq $firstBackend){throw 'Backend did not restart'}
    $result.FreshBackend=$true
    Run-Frontend restore
    $receipts=Sql "SELECT count(*) FROM character_narrative_commands WHERE character_id='$id';"
    if($receipts -cne '4'){throw 'Unexpected extra command after reopen/reconnect'}
    $result.Receipts=[int]$receipts
    if((Sql $growthQuery) -cne $growthBefore){throw 'Story UI changed persistent growth'}
    $result.GrowthUnchanged=$true;$result.Passed=$true
    Write-Output 'PASS real UE lobby/dialogue HTTP -> PostgreSQL; fresh backend and UE restore; alt/growth isolated'
}catch{$result.Failure=$_.Exception.Message;throw}
finally {
    try{Stop-Backend}catch{$result.Failure=([string]$result.Failure+'; backend cleanup: '+$_.Exception.Message)}
    try{if($containerId){$owned=Owned-Container;& docker rm --force $owned.Id|Out-Null;if($LASTEXITCODE -ne 0){throw 'Own container removal failed'};$result.ContainerRemoved=$true}}catch{$result.Failure=([string]$result.Failure+'; DB cleanup: '+$_.Exception.Message)}
    try{Assert-Reservation;$result.Cleanup=Remove-GuardianProbeArtifact -Path $temp -AllowedRoot $run;$result.TempDeleted=!(Test-Path -LiteralPath $temp)}catch{$result.Failure=([string]$result.Failure+'; file cleanup: '+$_.Exception.Message)}
    $headers=$null;$connection=$password=$adminToken=$null
    if(($containerId -and !$result.ContainerRemoved) -or !$result.TempDeleted -or $result.Failure){$result.Passed=$false}
    $result|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $run 'Result.json') -Encoding UTF8
    Write-Output ('Result: '+(Join-Path $run 'Result.json'))
    if(($containerId -and !$result.ContainerRemoved) -or !$result.TempDeleted){throw 'Owned E2E cleanup incomplete; inspect Result.json'}
}
