[CmdletBinding()]
param([string]$RuntimeProjectRoot='', [string]$BackendRoot='', [int]$BackendPort=3092, [int]$GamePort=18912,
    [string]$EngineRoot='D:/UE_5.8')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$RuntimeProjectRoot){$RuntimeProjectRoot=$workspace}
if(!$BackendRoot){$BackendRoot=Join-Path $workspace 'Backend'}
$id=[Guid]::NewGuid().ToString('N');$temp=Join-Path $workspace ('Saved/M03/play-'+$id)
$proof=Join-Path $workspace ('Saved/BuildChecks/ChapterM03-20261115/play-'+$id)
$null=[IO.Directory]::CreateDirectory($temp);$null=[IO.Directory]::CreateDirectory($proof)
$owned=[Collections.Generic.List[Diagnostics.Process]]::new()
$result=[ordered]@{Passed=$false;Scope='Real Training input/settlement then network M03 movement/interactions, client process restart, M04 offer; private Memory HTTP, not PostgreSQL or Steam';Failure=$null;DeletedBytes=0}
$url="http://127.0.0.1:$BackendPort";$admin=[Guid]::NewGuid().ToString('N')
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path $RuntimeProjectRoot 'Project_RPG.uproject'
function Start-Owned([string]$Exe,[string[]]$Arguments,[hashtable]$Environment=@{},[string]$Output=''){
    $previous=@{}
    try{
        foreach($key in $Environment.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$Environment[$key],'Process')}
        $params=@{FilePath=$Exe;ArgumentList=$Arguments;WorkingDirectory=$RuntimeProjectRoot;WindowStyle='Hidden';PassThru=$true}
        if($Output){$params.RedirectStandardOutput=$Output;$params.RedirectStandardError=$Output+'.stderr'}
        $p=Start-Process @params;$null=$p.Handle;$owned.Add($p);return $p
    }finally{foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}}
}
function Stop-Owned($p){if($p -and !$p.HasExited){$p.Kill();$p.WaitForExit()}}
function Api([string]$Path,[string]$Method='Get',[hashtable]$Headers=@{},$Body=$null){
    $params=@{Uri="$url/api/$Path";Method=$Method;Headers=$Headers;TimeoutSec=10;UseBasicParsing=$true}
    if($null-ne$Body){$params.ContentType='application/json';$params.Body=($Body|ConvertTo-Json -Depth 10 -Compress)}
    $r=Invoke-WebRequest @params;if($r.Content){return $r.Content|ConvertFrom-Json}
}
function Log([string]$Name){
    $path=Join-Path $temp ($Name+'.log');if(!(Test-Path $path)){return ''}
    $s=[IO.File]::Open($path,'Open','Read','ReadWrite');$r=[IO.StreamReader]::new($s)
    try{return $r.ReadToEnd()}finally{$r.Dispose()}
}
function Peer([string]$Name,[string]$Map,[bool]$Server,[bool]$Training,[bool]$Continue=$false){
    $args=@(('"'+$project+'"'),$Map,'-nosteam','-unattended','-nosound','-nop4','-NoLiveCoding','-NoLoadingScreen','-DisablePython',
        '-UseIrisReplication=1','-RPGRequireIris','-DDC=(InstalledEnginePak,Local)','-ExecCmds="t.MaxFPS 30"',
        ('-UserDir="'+$temp+'/'+$Name+'"'),('-abslog="'+$temp+'/'+$Name+'.log"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$url/api",
        "-ini:Game:[/Script/RPGInventoryRuntime.RPGItemBackendSubsystem]:ApiUrl=$url/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$url/api")
    $envs=@{PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null;PROJECT_RPG_BACKEND_ADMIN_TOKEN=$null}
    if($Server){$args+=@('-server',"-port=$GamePort",'-nullrhi','-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity');$envs.PROJECT_RPG_GAME_SERVER_ID=$serverId;$envs.PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId;$envs.PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken}
    else{$args+=@('-game','-RPGDevelopmentLogin','-RPGDungeonProfile=Intro1');if($Training){$args+='-nullrhi'}else{$args+=@('-RenderOffscreen','-d3d12','-Windowed','-ResX=1280','-ResY=900',('-M03Screenshot="'+$proof+'/M03.png"'))}}
    if($Training -or !$Server){
        $args+=@("-RPGTrainingCharacter=$characterId","-RPGTrainingSteam=$steam","-RPGTrainingSession=$sessionId",('-RPGTrainingGates="'+$temp+'"'))
        $args+=$(if($Training){'-RPGTrainingPlaytest'}else{'-RPGChapterOnePlaytest'})
        if($Continue){$args+='-RPGChapterOneContinue'}
    }
    return Start-Owned $editor $args $envs
}
function Heartbeat { $null=Api "dungeon-sessions/$sessionId/heartbeat" 'Post' $serverHeaders @{serverId=$serverId} }
function Begin-Session([string]$Dungeon,[string]$Name,[string]$Map,[bool]$Training){
    $s=Api 'dungeon-sessions' 'Post' $player @{characterId=$characterId;dungeonId=$Dungeon;difficulty='Normal'}
    $script:sessionId=[string]$s.dungeonSessionId;$script:serverId=$Name+'-'+$id
    $null=Api "dungeon-sessions/$sessionId/departure" 'Post' $player @{characterId=$characterId;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$s.lobbyRevision}
    $script:assignment=Api "dungeon-sessions/$sessionId/activate" 'Post' @{Authorization='Bearer '+$admin} @{serverId=$serverId;serverAddress="127.0.0.1:$GamePort"}
    $script:serverHeaders=@{Authorization='Bearer '+$assignment.gameServerAccessToken}
    $script:server=Peer ($Name+'Server') $Map $true $Training
    $until=[DateTime]::UtcNow.AddSeconds(180);$next=[DateTime]::MinValue
    do{
        if($server.HasExited){throw 'Game server exited'}
        if([DateTime]::UtcNow-ge$next){Heartbeat;$next=[DateTime]::UtcNow.AddSeconds(10)}
        if((Api "dungeon-sessions/$sessionId" 'Get' $player).state-eq'InProgress'){return}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow-lt$until)
    throw 'Game server start timeout'
}
function Choice([string]$Node,[string]$Option='accept'){
    $v=Api "characters/$characterId/narrative" 'Get' $player
    $null=Api "characters/$characterId/narrative/choices" 'Post' $player @{requestId=[Guid]::NewGuid().ToString('D');expectedRevision=[string]$v.revision;catalogVersion=$v.catalogVersion;dialogueId=$Node;choiceId=$Option}
}
try{
    if(@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()|Where-Object Port -eq $BackendPort).Count -or @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveUdpListeners()|Where-Object Port -eq $GamePort).Count){throw 'Private port occupied'}
    $result.ModuleSHA256=(Get-FileHash "$RuntimeProjectRoot/Binaries/Win64/UnrealEditor-Project_RPG.dll").Hash
    dotnet build "$BackendRoot/ProjectRpg.Backend" -c Release --artifacts-path "$temp/artifacts" --output "$temp/backend" --nologo *> "$proof/BackendBuild.log"
    if($LASTEXITCODE){throw 'Private backend build failed'}
    $backend=Start-Owned (Get-Command dotnet).Source @(('"'+$temp+'/backend/ProjectRpg.Backend.dll"')) @{
        ASPNETCORE_ENVIRONMENT='Development';DOTNET_ENVIRONMENT='Development';Storage__Provider='Memory';POSTGRES_CONNECTION_STRING=$null;ConnectionStrings__Postgres=$null
        ASPNETCORE_CONTENTROOT="$temp/backend";Kestrel__Endpoints__Http__Url=$url;PROJECT_RPG_BACKEND_ADMIN_TOKEN=$admin;Steam__AllowDevelopmentAuthentication='true'
        DungeonContent__DevelopmentProfile='Intro1';Narrative__EnableDevelopmentContent='false';Narrative__CatalogPath=(Join-Path $workspace 'Backend/ProjectRpg.Backend/Narrative/Content/chapter01.json')
    } "$temp/backend.log"
    $ready=$false;for($n=0;$n-lt100;++$n){try{if((Invoke-RestMethod "$url/health/ready" -TimeoutSec 1).status-eq'ready'){$ready=$true;break}}catch{};if($backend.HasExited){throw 'Backend exited'};Start-Sleep -Milliseconds 250}
    if(!$ready){throw 'Backend readiness timeout'}
    $steam=([uint64]76561300000000000+[uint64][Convert]::ToUInt32($id.Substring(0,8),16)).ToString()
    $auth=Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$steam"};$player=@{Authorization='Bearer '+$auth.accessToken}
    $character=Api 'characters' 'Post' $player @{name=('M03'+$id.Substring(0,10));classId='RPGPlayerClassDefinition:GreatSwordWarrior'};$characterId=[string]$character.characterId
    Choice 'CH01.M01.Offer';Choice 'CH01.D01' 'confirm';Choice 'CH01.D02' 'confirm';Choice 'CH01.M02.Offer'
    Begin-Session 'Progression.Training' 'Training' '/Game/Maps/Progression/L_ProgressionTraining' $true
    $trainingSession=$sessionId;$client=Peer 'TrainingClient' '/Game/Maps/LobbyMap' $false $true
    $until=[DateTime]::UtcNow.AddSeconds(240);$next=[DateTime]::MinValue;$passed=$false
    do{
        if($client.HasExited -or $server.HasExited){throw 'Training peer exited'}
        if([DateTime]::UtcNow-ge$next){Heartbeat;$next=[DateTime]::UtcNow.AddSeconds(10)}
        if((Api "dungeon-sessions/$sessionId" 'Get' $player).state-eq'Cleared' -and (Log 'TrainingClient')-match'INPUT_PASS Actions=4 Mask=15'){$passed=$true;break}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow-lt$until)
    if(!$passed){throw 'Actual Training completion timeout'}
    Stop-Owned $client;Stop-Owned $server
    $growth=Api "characters/$characterId/progression" 'Get' $player
    if($growth.experience-ne100){throw 'Training settlement not persisted'}
    $v=Api "characters/$characterId/narrative" 'Get' $player
    if(@($v.quests|Where-Object {$_.questId-eq'CH01.M02'-and$_.status-eq'Completed'}).Count-eq0){
        $null=Api "characters/$characterId/narrative/dungeon-objectives" 'Post' $player @{requestId=[Guid]::NewGuid().ToString('D');expectedRevision=[string]$v.revision;catalogVersion=$v.catalogVersion;dungeonSessionId=$trainingSession;questId='CH01.M02';objectiveId='training'}
    }
    Choice 'CH01.M03.Offer'
    Begin-Session 'Progression.Intro1' 'M03' '/Game/Maps/Progression/L_ProgressionIntro1' $false
    foreach($phase in @(1,2)){
        $name='M03Client'+$phase;$client=Peer $name '/Game/Maps/LobbyMap' $false $false ($phase-eq2)
        $until=[DateTime]::UtcNow.AddSeconds(240);$next=[DateTime]::MinValue
        while(!$client.HasExited-and[DateTime]::UtcNow-lt$until){if([DateTime]::UtcNow-ge$next){Heartbeat;$next=[DateTime]::UtcNow.AddSeconds(10)};Start-Sleep -Milliseconds 250}
        if(!$client.HasExited){throw 'M03 client timed out'};$client.WaitForExit()
        if($client.ExitCode-ne0){throw "M03 client failed phase $phase"}
        $expected=if($phase-eq1){'M03_PLAY CART_SAVED'}else{'M03_PLAY COMPLETE M04_OFFERED'}
        if((Log $name)-notmatch$expected){throw "Missing M03 proof phase $phase"}
        Write-Output "PASS actual M03 phase $phase"
        Start-Sleep -Seconds 2
    }
    $v=Api "characters/$characterId/narrative" 'Get' $player
    if(@($v.quests|Where-Object {$_.questId-eq'CH01.M03'-and$_.status-eq'Completed'}).Count-ne1){throw 'M03 commit missing'}
    $after=Api "characters/$characterId/progression" 'Get' $player
    if(($growth|ConvertTo-Json -Depth 12 -Compress)-cne($after|ConvertTo-Json -Depth 12 -Compress)){throw 'Investigation altered growth'}
    $result.FinalNarrative=$v;$result.Experience=$after.experience;$result.Passed=$true
}catch{$result.Failure=$_.Exception.Message}
finally{
    foreach($p in $owned){Stop-Owned $p}
    foreach($name in @('TrainingServer','TrainingClient','M03Server','M03Client1','M03Client2')){
        $text=Log $name;if($text){$text -split "`n"|Where-Object {$_-match'M03_PLAY|RPG_TRAINING_PLAYTEST|Fatal error|Assertion failed|Error:'}|Set-Content "$proof/$name.log" -Encoding UTF8}
    }
    if(Test-Path "$temp/backend.log"){Copy-Item "$temp/backend.log" "$proof/Backend.log"}
    $resolved=[IO.Path]::GetFullPath($temp);$parent=[IO.Path]::GetFullPath((Join-Path $workspace 'Saved/M03'))+'\'
    if(!$resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe cleanup target'}
    $entries=@(Get-ChildItem -LiteralPath $temp -Recurse -Force)
    if(@($entries|Where-Object {$_.Attributes-band[IO.FileAttributes]::ReparsePoint}).Count){throw 'Unexpected link in owned run'}
    $result.DeletedBytes=($entries|Where-Object {!$_.PSIsContainer}|Measure-Object Length -Sum).Sum
    Remove-Item -LiteralPath $temp -Recurse -Force
    $result|ConvertTo-Json -Depth 20|Set-Content "$proof/Result.json" -Encoding UTF8
}
if(!$result.Passed){throw $result.Failure}
