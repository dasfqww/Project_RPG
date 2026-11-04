[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RuntimeProjectRoot,
    [string]$EngineRoot='D:\UE_5.8',
    [ValidateRange(1024,65535)][int]$BackendPort=3074,
    [ValidateRange(1024,65535)][int]$GamePort=18894,
    [ValidateRange(60,600)][int]$TimeoutSeconds=240,
    [switch]$ObserveActionProgressPIE,
    [switch]$ObservePlayerVitalsPIE,
    [switch]$ObserveHUDChangesPIE,
    [switch]$ShowVitalsEditor,
    [switch]$CancelChargePIE,
    [switch]$TrainingCompletionUI,
    [ValidateRange(1024,65535)][int]$McpPort=8012
)

# Real one-player training input and server settlement; no fixture clear request.
# The caller builds a coherent development module containing the explicit probe.
$ErrorActionPreference='Stop'
if($CancelChargePIE -and !$ObserveActionProgressPIE){throw 'CancelChargePIE requires ObserveActionProgressPIE'}
if($ObservePlayerVitalsPIE -and ($ObserveActionProgressPIE -or $CancelChargePIE -or $TrainingCompletionUI)){throw 'Vitals PIE observation requires its own normal four-action run'}
if($ShowVitalsEditor -and !$ObservePlayerVitalsPIE){throw 'ShowVitalsEditor requires ObservePlayerVitalsPIE'}
if($ObserveHUDChangesPIE -and !$ObservePlayerVitalsPIE){throw 'ObserveHUDChangesPIE requires ObservePlayerVitalsPIE'}
if($TrainingCompletionUI -and ($CancelChargePIE -or $ObserveActionProgressPIE)){throw 'TrainingCompletionUI uses the normal four-action game probe'}
$useEditorPIE=$ObserveActionProgressPIE -or $ObservePlayerVitalsPIE
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'TrainingGuideArtifacts.ps1')
if($useEditorPIE){. (Join-Path $PSScriptRoot 'ActionProgressPIEObservation.ps1')}
if($ObservePlayerVitalsPIE){. (Join-Path $PSScriptRoot 'PlayerVitalsPIEObservation.ps1')}
if($CancelChargePIE){. (Join-Path $PSScriptRoot 'ActionProgressCancelPIE.ps1')}
$runtime=[IO.Path]::GetFullPath($RuntimeProjectRoot)
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path $runtime 'Project_RPG.uproject'
$module=Join-Path $runtime 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
foreach($path in @($editor,$project,$module)){if(!(Test-Path -LiteralPath $path)){throw ('Missing prerequisite: '+$path)}}
if($CancelChargePIE){
    $stream=[IO.File]::OpenRead($module)
    try{
        $buffer=[byte[]]::new(262144);$tail='';$found=$false
        while(($read=$stream.Read($buffer,0,$buffer.Length)) -gt 0){
            $text=$tail+[Text.Encoding]::Unicode.GetString($buffer,0,$read)
            if($text.Contains('RPG_ACTION_CANCEL_NATIVE')){$found=$true;break}
            $tail=$text.Substring([Math]::Max(0,$text.Length-32))
        }
    }finally{$stream.Dispose()}
    if(!$found){throw 'Module lacks the native cancellation probe; prepare a current development module first'}
}
foreach($path in @('Project_RPG.uproject','Config/DefaultGame.ini','Config/DefaultEngine.ini')){
    if((Get-FileHash -LiteralPath (Join-Path $runtime $path)).Hash -cne (Get-FileHash -LiteralPath (Join-Path $workspace $path)).Hash){throw ('Runtime configuration differs: '+$path)}
}
if(@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() | Where-Object Port -eq $BackendPort).Count -or
   @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveUdpListeners() | Where-Object Port -eq $GamePort).Count){throw 'Requested port is in use'}
$runId=[Guid]::NewGuid().ToString('N')
$runRoot=Join-Path $workspace ('Saved/Tests/TrainingPlaytest/'+$runId)
$temp=Join-Path $runRoot 'Temp'
$backendSource=Join-Path $temp 'Backend'
$build=Join-Path $temp 'Build'
# Keep user directories short enough for the scoped disconnection journal.
$userParent=Join-Path $workspace ('Saved/Tests/TP/'+$runId.Substring(0,12))
$userRoot=Join-Path $userParent 'U'
if(Test-Path -LiteralPath $userParent){throw 'Refusing to reuse a user directory'}
$maximumJournalPath=Join-Path $userRoot "s\Saved\Online\DungeonDisconnections\$('a'*32)\$('b'*32)\$([Guid]::Empty)\ConsumeRecoveries"
if($maximumJournalPath.Length+46 -ge 260){throw 'Workspace path is too long for scoped admission journals'}
$null=New-Item -ItemType Directory -Path $build -Force
$url='http://127.0.0.1:'+$BackendPort
$dotnet=(Get-Command dotnet).Source
$adminToken=[Guid]::NewGuid().ToString('N')
$processes=[Collections.Generic.List[Diagnostics.Process]]::new()
$observedModules=[Collections.Generic.List[object]]::new()
$proof=[ordered]@{passed=$false;scope='Development/Memory/HTTP ticket/Iris/one-player authored training map/nullRHI';failure=$null;cleanup=$null}
$moduleHash=(Get-FileHash -LiteralPath $module).Hash
$backendHashes=[Collections.Generic.List[object]]::new()

function Invoke-Api([string]$Path,[string]$Method='Get',[hashtable]$Headers=@{},[object]$Body=$null){
    $args=@{Uri="$url/api/$Path";Method=$Method;Headers=$Headers;TimeoutSec=10;UseBasicParsing=$true}
    if($null -ne $Body){$args.ContentType='application/json';$args.Body=$Body | ConvertTo-Json -Depth 8 -Compress}
    try{$response=Invoke-WebRequest @args}catch{
        $status=if($_.Exception.Response){[int]$_.Exception.Response.StatusCode}else{0}
        throw "HTTP $Method $Path failed ($status); credentials and body omitted"
    }
    if($response.Content){return $response.Content | ConvertFrom-Json}
}
function Start-Owned([string]$File,[string[]]$Arguments,[string]$Directory,[hashtable]$Environment=@{},[string]$Output='',[switch]$Visible){
    $prior=@{}
    try{
        foreach($key in $Environment.Keys){$prior[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$Environment[$key],'Process')}
        $args=@{FilePath=$File;ArgumentList=$Arguments;WorkingDirectory=$Directory;PassThru=$true;WindowStyle=$(if($Visible){'Normal'}else{'Hidden'})}
        if($Output){$args.RedirectStandardOutput=$Output;$args.RedirectStandardError=$Output+'.stderr'}
        $p=Start-Process @args
        $handle=$p.Handle
        $processes.Add($p)
        return $p
    }finally{foreach($key in $prior.Keys){[Environment]::SetEnvironmentVariable($key,$prior[$key],'Process')}}
}
function Start-Peer([string]$Name,[string]$Map,[string[]]$Extra,[hashtable]$Environment){
    $args=@(('"'+$project+'"'),$Map)+$Extra+@(
        '-RPGTrainingPlaytest','-UseIrisReplication=1','-RPGRequireIris',
        '-unattended','-nop4','-nosplash','-nosound','-nosteam','-NoLiveCoding','-NoLoadingScreen','-DisablePython','-nullrhi','-DDC=(EnginePak,Local)',
        "-RPGTrainingCharacter=$characterId","-RPGTrainingSteam=$steamId","-RPGTrainingSession=$sessionId",
        ('-RPGTrainingGates="'+$runRoot+'"'),('-UserDir="'+(Join-Path $userRoot $Name.Substring(0,1))+'"'),
        ('-abslog="'+(Join-Path $temp ($Name+'.log'))+'"'),'-ExecCmds="t.MaxFPS 60"',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$url/api",
        "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$url/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$url/api")
    if($CancelChargePIE -and $Name -eq 'server'){$args+='-RPGActionProgressCancelPIE'}
    if($ObserveHUDChangesPIE){$args+='-RPGHUDChangesPIE'}
    if($TrainingCompletionUI -and $Name -eq 'client'){
        # UI completion needs the real Slate viewport tick, not a null renderer.
        $args=@($args|Where-Object {$_ -ne '-nullrhi'})
        $args+=@('-RPGTrainingCompletionUI','-RenderOffscreen','-d3d12','-Windowed','-ResX=1280','-ResY=900')
    }
    if($useEditorPIE -and $Name -eq 'client'){
        $args=@($args | Where-Object {$_ -notin @('-game','-nullrhi','-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')})
        # Bootstrap the same authenticated connect flow without loading the lobby twice.
        $args[1]='/Engine/Maps/Entry?game=/Script/Engine.GameModeBase'
        $args+=@('-editor','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720',
            '-ModelContextProtocolStartServer',"-ModelContextProtocolPort=$McpPort",
            '-ini:Engine:HTTPServer.Listeners:DefaultBindAddress=127.0.0.1',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:PlayNumberOfClients=1',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:PlayNetMode=PIE_Standalone',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:bLaunchSeparateServer=False',
            '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=True',
            "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:ServerPortNumber=$McpPort")
        if($CancelChargePIE){
            $args+='-RPGActionProgressCancelPIE'
        }
        if($ObservePlayerVitalsPIE){
            $observer=(Join-Path $PSScriptRoot 'Observe-PlayerVitalsPIE.py').Replace('\','/')
            if($observer -match '[\s,"'']'){throw 'Vitals observer path must be safe for the py console command'}
            $args=@($args | Where-Object {$_ -ne '-DisablePython' -and $_ -notlike '-ExecCmds=*'})
            $args+=@('-RPGObservePlayerVitalsPIE',('-RPGVitalsPIEOutput="'+(Join-Path $runRoot 'Vitals.json')+'"'),
                '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:NewWindowWidth=1280',
                '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:NewWindowHeight=720',
                ('-ExecCmds="t.MaxFPS 60,py '+$observer+'"'))
            if($ShowVitalsEditor){$args=@($args | Where-Object {$_ -ne '-RenderOffscreen'})}
        }
        return Start-Owned (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') $args $runtime $Environment -Visible:$ShowVitalsEditor
    }
    return Start-Owned $editor $args $runtime $Environment
}
function Observe-Module($Process,[string]$Role){
    if(@($observedModules | Where-Object role -ceq $Role).Count){return}
    try{
        $loaded=@((Get-Process -Id $Process.Id).Modules | Where-Object ModuleName -ceq 'UnrealEditor-Project_RPG.dll')
        if($loaded.Count -eq 1){
            $hash=(Get-FileHash -LiteralPath $loaded[0].FileName).Hash
            if($loaded[0].FileName -ine $module -or $hash -cne $moduleHash){throw 'Loaded module mismatch'}
            $observedModules.Add([pscustomobject]@{role=$Role;pid=$Process.Id;path=$loaded[0].FileName;sha256=$hash})
        }
    }catch{if($_.Exception.Message -eq 'Loaded module mismatch'){throw}}
}
function Read-ActiveLog([string]$Path){
    if(!(Test-Path -LiteralPath $Path)){return ''}
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader=[IO.StreamReader]::new($stream)
    try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
}
function Get-TrainingRuntimeDiagnostics([string]$Text,[bool]$AllowPlannedReturn,[int]$ServerPlannedDisconnects){
    $start=$Text.IndexOf('RPG_TRAINING_PLAYTEST START DevelopmentOnly=1',[StringComparison]::Ordinal)
    if($start -lt 0){throw 'Missing explicit probe startup'}
    $body=$Text.Substring($start)
    $errors=@([regex]::Matches($body,'(?m)^.*: Error:.*$'))
    $ready=$body.IndexOf('RPG_TRAINING_PLAYTEST COMPLETION_UI_READY Widgets=1 ReturnEnabled=1',[StringComparison]::Ordinal)
    $returned=$body.IndexOf('RPG_TRAINING_PLAYTEST COMPLETION_UI_RETURNED Lobby=1 IdentityPreserved=1',[StringComparison]::Ordinal)
    $expected=@()
    if($AllowPlannedReturn -and $ServerPlannedDisconnects -eq 1 -and $ready -ge 0 -and $returned -gt $ready){
        $expected=@($errors|Where-Object {$_.Index -gt $ready -and $_.Index -lt $returned -and $_.Value -match 'LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost, ErrorString = Your connection to the host has been lost\., Driver = Name:GameNetDriver Def:GameNetDriver [^\r\n]+\r?$'})
        if($expected.Count -gt 1){$expected=@()}
    }
    @{afterProbeStart=$errors.Count;plannedReturnConnectionLost=$expected.Count;unexpected=($errors.Count-$expected.Count)}
}
try{
    # Capture backend inputs once. All builds and database state are private.
    $backend=Join-Path $workspace 'Backend'
    foreach($part in @('ProjectRpg.Backend','ProjectRpg.Operations.Core','Database')){
        $source=Join-Path $backend $part
        foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object FullName -notmatch '\\(bin|obj|artifacts)\\'){
            $relative=$file.FullName.Substring($backend.Length+1)
            $destination=Join-Path $backendSource $relative
            $null=New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
            Copy-Item -LiteralPath $file.FullName -Destination $destination
            if((Get-FileHash -LiteralPath $destination).Hash -cne $hash){throw 'Backend snapshot changed during copy'}
            $backendHashes.Add([pscustomobject]@{path=$relative;sha256=$hash})
        }
    }
    # Another owner may edit backend sources while this runner uses a private build.
    # Require one coherent file set at the snapshot boundary, then use only the copy.
    $currentBackendFiles=@(foreach($part in @('ProjectRpg.Backend','ProjectRpg.Operations.Core','Database')){
        Get-ChildItem -LiteralPath (Join-Path $backend $part) -File -Recurse | Where-Object FullName -notmatch '\\(bin|obj|artifacts)\\' | ForEach-Object {$_.FullName.Substring($backend.Length+1)}
    })
    if(Compare-Object @($backendHashes.ToArray().path | Sort-Object) @($currentBackendFiles | Sort-Object)){throw 'Backend file set changed while snapshotting'}
    foreach($entry in $backendHashes){if((Get-FileHash -LiteralPath (Join-Path $backend $entry.path)).Hash -cne $entry.sha256){throw 'Backend source changed while snapshotting'}}
    $backendHashes.ToArray() | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'BackendInputs.json') -Encoding UTF8
    & $dotnet build (Join-Path $backendSource 'ProjectRpg.Backend/ProjectRpg.Backend.csproj') -c Release --artifacts-path (Join-Path $temp 'Artifacts') --output $build *> (Join-Path $runRoot 'BackendBuild.log')
    if($LASTEXITCODE -ne 0){throw 'Isolated backend build failed'}
    $proof.backendSha256=(Get-FileHash -LiteralPath (Join-Path $build 'ProjectRpg.Backend.dll')).Hash
    $envs=@{
        ASPNETCORE_ENVIRONMENT='Development';PROJECT_RPG_BACKEND_ADMIN_TOKEN=$adminToken
        POSTGRES_CONNECTION_STRING=$null;ConnectionStrings__Postgres=$null;Storage__Provider='Memory'
        Kestrel__Endpoints__Http__Url=$url;Steam__AllowDevelopmentAuthentication='true'
        PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null
        DungeonContent__Entries__1__DungeonId='Progression.Training'
        DungeonContent__Entries__1__MapPackage='/Game/Maps/Progression/L_ProgressionTraining'
        DungeonContent__Entries__1__Difficulties__0='Normal'
    }
    $backendProcess=Start-Owned $dotnet @(('"'+(Join-Path $build 'ProjectRpg.Backend.dll')+'"')) $build $envs (Join-Path $temp 'backend.log')
    $ready=$false;$until=[DateTime]::UtcNow.AddSeconds(45)
    while([DateTime]::UtcNow -lt $until){
        if($backendProcess.HasExited){throw 'Owned backend exited'}
        try{$health=Invoke-RestMethod "$url/health/ready" -TimeoutSec 2;if($health.status -eq 'ready' -and $health.storageProvider -eq 'Memory'){$ready=$true;break}}catch{}
        Start-Sleep -Milliseconds 250
    }
    if(!$ready){throw 'Memory backend readiness timeout'}
    if(@(Get-NetTCPConnection -State Listen -LocalPort $BackendPort | Where-Object {$_.OwningProcess -eq $backendProcess.Id -and $_.LocalAddress -eq '127.0.0.1'}).Count -ne 1){throw 'Backend listener ownership mismatch'}
    $steamId=([uint64]76561300000000000+[uint64][Convert]::ToUInt32($runId.Substring(0,8),16)).ToString()
    $auth=Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$steamId"}
    $headers=@{Authorization='Bearer '+$auth.accessToken}
    $character=Invoke-Api 'characters' 'Post' $headers @{name=('Training'+$runId.Substring(0,10));classId='RPGPlayerClassDefinition:GreatSwordWarrior'}
    $characterId=[string]$character.characterId
    $sibling=Invoke-Api 'characters' 'Post' $headers @{name=('Sibling'+$runId.Substring(0,10))}
    $before=Invoke-Api "characters/$characterId/progression" 'Get' $headers
    $siblingBefore=Invoke-Api "characters/$($sibling.characterId)/progression" 'Get' $headers
    if($before.experience -ne 0 -or $before.level -ne 1 -or $before.totalSP -ne 0 -or $before.identityUnlocked){throw 'Fresh character mismatch'}
    $session=Invoke-Api 'dungeon-sessions' 'Post' $headers @{characterId=$characterId;dungeonId='Progression.Training';difficulty='Normal'}
    $sessionId=[string]$session.dungeonSessionId;$sessionPath='dungeon-sessions/'+$sessionId
    $null=Invoke-Api "$sessionPath/departure" 'Post' $headers @{characterId=$characterId;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$session.lobbyRevision}
    $serverId='training-'+$runId
    $assignment=Invoke-Api "$sessionPath/activate" 'Post' @{Authorization='Bearer '+$adminToken} @{serverId=$serverId;serverAddress="127.0.0.1:$GamePort"}
    $serverHeaders=@{Authorization='Bearer '+$assignment.gameServerAccessToken}
    if($ObservePlayerVitalsPIE){
        # Full Editor boot can exceed the server probe's remaining admission
        # window. Boot the owned client concurrently, then start PIE only after
        # the real server is ready; no gameplay timeout policy is changed.
        $client=Start-Peer 'client' '/Game/Maps/LobbyMap' @('-game','-RPGDevelopmentLogin') @{
            PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null}
    }
    $server=Start-Peer 'server' '/Game/Maps/Progression/L_ProgressionTraining' @('-server',"-port=$GamePort",'-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity') @{
        PROJECT_RPG_GAME_SERVER_ID=$serverId;PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken}
    $until=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$nextHeartbeat=[DateTime]::MinValue
    do{
        if($server.HasExited){throw 'Owned server exited before start'}
        Observe-Module $server 'server'
        if([DateTime]::UtcNow -ge $nextHeartbeat){$null=Invoke-Api "$sessionPath/heartbeat" 'Post' $serverHeaders @{serverId=$serverId};$nextHeartbeat=[DateTime]::UtcNow.AddSeconds(10)}
        $session=Invoke-Api $sessionPath 'Get' $headers
        if($session.state -eq 'InProgress'){break}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow -lt $until)
    if($session.state -ne 'InProgress'){throw 'Actual server did not start the assignment'}
    if(!$ObservePlayerVitalsPIE){
        $client=Start-Peer 'client' '/Game/Maps/LobbyMap' @('-game','-RPGDevelopmentLogin') @{
            PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null}
    }
    if($useEditorPIE){
        # Editor startup can outlast an active lease. Renew the same assigned
        # server's existing lease without changing backend expiration policy.
        $heartbeatState=@{next=[DateTime]::MinValue}
        $keepPIELeaseAlive={
            if([DateTime]::UtcNow -ge $heartbeatState.next){
                $null=Invoke-Api "$sessionPath/heartbeat" 'Post' $serverHeaders @{serverId=$serverId}
                $heartbeatState.next=[DateTime]::UtcNow.AddSeconds(10)
            }
        }
        if($ObservePlayerVitalsPIE){
            Wait-PlayerVitalsEditorReady $client $keepPIELeaseAlive
            Start-PlayerVitalsPIE $client $McpPort $keepPIELeaseAlive
        }else{Start-ActionProgressPIE $client $McpPort -KeepAlive $keepPIELeaseAlive}
        # Startup and authored input each receive their own bounded wait.
        $until=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    }
    $completed=$false
    while([DateTime]::UtcNow -lt $until){
        foreach($p in @($server,$client,$backendProcess)){if($p.HasExited){throw 'Owned peer exited before completion'}}
        Observe-Module $client 'client'
        $serverLog=Read-ActiveLog (Join-Path $temp 'server.log')
        $clientLog=Read-ActiveLog (Join-Path $temp 'client.log')
        if($CancelChargePIE){
            if(Read-CancelChargePIE $clientLog){$completed=$true;break}
        }elseif($ObserveActionProgressPIE){Read-ActionProgressPIE $clientLog}
        if($ObservePlayerVitalsPIE -and (Read-PlayerVitalsPIE)){$completed=$true;break}
        if(($serverLog+$clientLog) -match 'RPG_TRAINING_PLAYTEST FAIL|Fatal error:|Assertion failed:|Ensure condition failed:'){throw 'Training probe/runtime failed'}
        $session=Invoke-Api $sessionPath 'Get' $headers
        if(!$ObservePlayerVitalsPIE -and !$CancelChargePIE -and $session.state -eq 'Cleared' -and $serverLog -match 'RPG_TRAINING_PLAYTEST PROGRESS Role=Server Mask=15' -and
            $clientLog -match 'RPG_TRAINING_PLAYTEST INPUT_PASS Actions=4 Mask=15 ActualInput=1' -and
            (!$TrainingCompletionUI -or $clientLog -match 'RPG_TRAINING_PLAYTEST COMPLETION_UI_RETURNED Lobby=1 IdentityPreserved=1')){$completed=$true;break}
        Start-Sleep -Milliseconds $(if($useEditorPIE){25}else{250})
    }
    if(!$completed){throw 'Input to server settlement timed out'}
    if($ObservePlayerVitalsPIE){
        # This option verifies the actual authored initial HUD. It does not
        # claim the four-action training/settlement scenario has completed.
        $after=Invoke-Api "characters/$characterId/progression" 'Get' $headers
        $siblingAfter=Invoke-Api "characters/$($sibling.characterId)/progression" 'Get' $headers
        if(($siblingAfter | ConvertTo-Json -Depth 8 -Compress) -cne ($siblingBefore | ConvertTo-Json -Depth 8 -Compress)){throw 'Sibling progression changed'}
        $proof.playerVitalsPIE=Complete-PlayerVitalsPIE
        $proof.trainingSemanticsPassed=$false
        $proof.trainingScope='Initial Vitals presentation only; four-action completion and rewards are not asserted'
    }elseif($CancelChargePIE){
        $after=Invoke-Api "characters/$characterId/progression" 'Get' $headers
        $session=Invoke-Api $sessionPath 'Get' $headers
        $serverLog=Read-ActiveLog (Join-Path $temp 'server.log')
        if($session.state -ne 'InProgress' -or $serverLog -notmatch 'RPG_TRAINING_PLAYTEST PROGRESS Role=Server Mask=0' -or
            $serverLog -match 'RPG_TRAINING_PLAYTEST PROGRESS Role=Server Mask=[1-9][0-9]*\b' -or
            ($before | ConvertTo-Json -Depth 8 -Compress) -cne ($after | ConvertTo-Json -Depth 8 -Compress)){throw 'Cancelled charge incorrectly completed training or changed progression'}
        $siblingAfter=Invoke-Api "characters/$($sibling.characterId)/progression" 'Get' $headers
        if(($siblingAfter | ConvertTo-Json -Depth 8 -Compress) -cne ($siblingBefore | ConvertTo-Json -Depth 8 -Compress)){throw 'Sibling progression changed'}
        $proof.actionProgressPIE=Complete-CancelChargePIE
        $proof.trainingMask=0
        $proof.trainingSemanticsPassed=$true
    }else{
    $after=Invoke-Api "characters/$characterId/progression" 'Get' $headers
    if($after.experience -ne 100 -or $after.level -ne 2 -or $after.totalSP -ne 3 -or !$after.identityUnlocked -or
        @($after.skills | Where-Object skillTag -ceq 'Player.Ability.Skill.WhirlWind').Count -ne 1){throw 'Training reward/unlock mismatch'}
    # Repeated reads must not credit the same completed session twice.
    for($i=0;$i -lt 3;$i++){
        $again=Invoke-Api "characters/$characterId/progression" 'Get' $headers
        if(($again | ConvertTo-Json -Depth 8 -Compress) -cne ($after | ConvertTo-Json -Depth 8 -Compress)){throw 'Repeated progression read changed reward'}
    }
    $siblingAfter=Invoke-Api "characters/$($sibling.characterId)/progression" 'Get' $headers
    if(($siblingAfter | ConvertTo-Json -Depth 8 -Compress) -cne ($siblingBefore | ConvertTo-Json -Depth 8 -Compress)){throw 'Sibling progression changed'}
    $proof.trainingSemanticsPassed=$true;$proof.initial=$before;$proof.final=$after;$proof.siblingUnchanged=$true;$proof.sessionState=$session.state
    if($TrainingCompletionUI){
        if($clientLog -notmatch 'RPG_TRAINING_PLAYTEST COMPLETION_UI_READY Widgets=1 ReturnEnabled=1'){throw 'Training completion UI was not uniquely displayed and ready'}
        $proof.completionUI=@{passed=$true;visibleResultWidgets=1;returnEnabled=$true;returnedToLobby=$true;identityPreserved=$true;existingUMGButtonHandler=$true;physicalInput=$false}
    }
    if($ObserveActionProgressPIE){$proof.actionProgressPIE=Complete-ActionProgressPIE}
    }
    Set-Content -LiteralPath (Join-Path $runRoot 'stop.ready') -Value 'stop' -Encoding ASCII
    $proof.peerExits=@()
    foreach($peer in @(@{role='client';process=$client},@{role='server';process=$server})){
        $exited=$peer.process.WaitForExit($(if($useEditorPIE){90000}else{30000}))
        $code=if($exited){$peer.process.ExitCode}else{$null}
        $proof.peerExits+=@{role=$peer.role;exited=$exited;exitCode=$code}
        if(!$exited -or $code -ne 0){throw ('Peer '+$peer.role+' did not exit cleanly; exited='+$exited+'; code='+$code)}
    }
    if($observedModules.Count -ne 2 -or (Get-FileHash -LiteralPath $module).Hash -cne $moduleHash){throw 'Module provenance incomplete'}
    $runtimeErrors=[Collections.Generic.List[object]]::new()
    $plannedDisconnects=[regex]::Matches([IO.File]::ReadAllText((Join-Path $temp 'server.log')),'ErrorContext="dungeon_planned_lobby_disconnect"').Count
    foreach($role in @('server','client')){
        $text=[IO.File]::ReadAllText((Join-Path $temp ($role+'.log')))
        $diagnostics=Get-TrainingRuntimeDiagnostics $text ($TrainingCompletionUI -and $role -eq 'client' -and $proof.completionUI.passed -and $session.state -eq 'Cleared') $plannedDisconnects
        $diagnostics.role=$role;$runtimeErrors.Add([pscustomobject]$diagnostics)
        if($diagnostics.unexpected){throw 'Unexpected runtime errors after probe startup'}
    }
    $proof.passed=$true;$proof.initial=$before;$proof.final=$after;$proof.siblingUnchanged=$true;$proof.runtimeErrors=$runtimeErrors.ToArray()
    $proof.serverExit=$server.ExitCode;$proof.clientExit=$client.ExitCode;$proof.sessionState=$session.state
    if($TrainingCompletionUI){$proof.completionUI.serverPlannedDisconnects=$plannedDisconnects}
}catch{$proof.failure=$_.Exception.Message}
finally{
    foreach($p in $processes){if(!$p.HasExited){$p.Kill();$null=$p.WaitForExit(10000)};$p.Dispose()}
    # Keep only curated, credential-free diagnostics; all full runtime logs are temporary.
    foreach($role in @('server','client')){
        $path=Join-Path $temp ($role+'.log')
        if(Test-Path -LiteralPath $path){
            Get-Content -LiteralPath $path | Where-Object {$_ -match 'RPG_TRAINING_PLAYTEST |ACTION_PROGRESS_CANCEL_PIE |RPG_ACTION_CANCEL_|dungeon_planned_lobby_disconnect|LogExit:|RequestExit|: Error:|Fatal error:|Assertion failed:|Ensure condition failed:' -and $_ -notmatch '(?i)token|ticket|authorization|command line|Browse:'} |
                Set-Content -LiteralPath (Join-Path $runRoot ($role+'.proof.log')) -Encoding UTF8
        }
    }
    $proof.moduleSHA256=$moduleHash;$proof.loadedModules=$observedModules.ToArray();$proof.backendInputCount=$backendHashes.Count
    if($ObserveActionProgressPIE){$proof.scope='Development/Memory/HTTP ticket/Iris/authored training map/owned full Editor PIE; input API probe, not physical keyboard';$proof.actionProgressSamples=$script:ActionProgressPIESamples.ToArray()}
    if($ObservePlayerVitalsPIE){$proof.scope='Development/Memory/HTTP ticket/Iris/authored training map/owned full Editor PIE; read-only actual Vitals controls and GAS attributes; existing input API probe, not physical keyboard'}
    if($ObserveHUDChangesPIE){
        # Missing/malformed proof on an early failure must never bypass the cleanup below.
        try{
            $serverChange=Get-Content -Raw -Encoding UTF8 -ErrorAction Stop -LiteralPath (Join-Path $runRoot 'HUDChangesServer.json') | ConvertFrom-Json
            $clientNotice=Get-Content -Raw -Encoding UTF8 -ErrorAction Stop -LiteralPath (Join-Path $runRoot 'HUDChangesClient.json') | ConvertFrom-Json
            $serverRespawn=Get-Content -Raw -Encoding UTF8 -ErrorAction Stop -LiteralPath (Join-Path $runRoot 'HUDRespawnServer.json') | ConvertFrom-Json
            if(!$serverChange.passed -or !$serverChange.authority -or !$clientNotice.passed -or !$serverRespawn.passed){throw 'HUD changes proof missing or failed'}
            $proof.hudChanges=@{server=$serverChange;notice=$clientNotice;respawn=$serverRespawn;actualHitOrAbilityCostInput=$false}
            $proof.scope+='; opt-in server gameplay effect HP/MP deltas and production notice event; no direct UMG state setters'
        }catch{
            $proof.passed=$false
            $proof.failure=([string]$proof.failure+'; HUD changes proof: '+$_.Exception.Message).Trim(';',' ')
        }
    }
    if($CancelChargePIE){$proof.scope+='; charge input only; real GAS cancellation/late release, no completion or XP';$proof.cancelCharge=$true}
    $proof.notVerified=@('Steam','PostgreSQL','two to four players','physical input/GPU layout','process restart/reconnect','normal shared module')
    if($ObservePlayerVitalsPIE){$proof.notVerified=@('Steam','PostgreSQL','two to four players','physical input',
        'damage and mana-consumption changes','four-action training completion/rewards','process restart/reconnect','original shared module launch')}
    if($ObserveHUDChangesPIE){$proof.notVerified=@('Steam','PostgreSQL','two to four players','physical input',
        'actual hit or ability-cost execution','holding/casting gameplay execution','four-action training completion/rewards','process restart/reconnect','original shared module launch')}
    $cleanupError=$null
    for($cleanupAttempt=1;$cleanupAttempt -le 20;$cleanupAttempt++){
        try{
            if(!$proof.Contains('userCleanup')){$proof.userCleanup=Remove-RPGTrainingGuideTemporaryTree -Path $userRoot -OwnedParent $userParent}
            if(Test-Path -LiteralPath $userParent){[IO.Directory]::Delete($userParent,$false)}
            if(!$proof.cleanup){$proof.cleanup=Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $runRoot}
            $cleanupError=$null;break
        }catch{$cleanupError=$_.Exception.Message;if($cleanupAttempt -lt 20){Start-Sleep -Milliseconds 500}}
    }
    $proof.cleanupAttempts=[Math]::Min($cleanupAttempt,20)
    if($cleanupError){$proof.passed=$false;$proof.failure=([string]$proof.failure+'; cleanup: '+$cleanupError).Trim(';',' ')}
    $proof.historicalHashesOnly=$true
    $proof | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $runRoot 'Verification.proof.json') -Encoding UTF8
}
Write-Output ('Evidence='+$runRoot)
if(!$proof.passed){throw $proof.failure}
if($ObservePlayerVitalsPIE){Write-Output ('PASS: actual PIE Vitals initial presentation/GAS bindings; temporary bytes removed='+$proof.cleanup.deletedBytes)}
elseif($CancelChargePIE){Write-Output ('PASS: PIE GAS cancellation/late release; training remains InProgress/XP 0; temporary bytes removed='+$proof.cleanup.deletedBytes)}
else{Write-Output ('PASS: training input/server settlement/XP 100/level 2/SP 3; temporary bytes removed='+$proof.cleanup.deletedBytes)}
