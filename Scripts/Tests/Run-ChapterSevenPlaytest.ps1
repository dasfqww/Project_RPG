[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RuntimeProjectRoot,[string]$BackendRoot='', [int]$BackendPort=3094, [int]$GamePort=18914,[string]$EngineRoot='D:/UE_5.8',[switch]$PriorBoss)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$BackendRoot){$BackendRoot=Join-Path $workspace 'Backend'}
$ownerRoot='M07';$id=[guid]::NewGuid().ToString('N');$temp=Join-Path $workspace ('Saved/M07/play-'+$id)
$proof=Join-Path $workspace ('Saved/BuildChecks/ChapterM07-20261118/play-'+$id)
$null=[IO.Directory]::CreateDirectory($temp);$null=[IO.Directory]::CreateDirectory($proof)
$owned=[Collections.Generic.List[Diagnostics.Process]]::new()
$result=[ordered]@{Passed=$false;Scope='M07 Memory/Iris actual UMG acceptance, boss HP10 fixture and native attack, extraction input/recovery';Failure=$null;DeletedBytes=0}
$url="http://127.0.0.1:$BackendPort";$admin=[guid]::NewGuid().ToString('N')
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$project=Join-Path $RuntimeProjectRoot 'Project_RPG.uproject'
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
function Peer([string]$Name,[string]$Map,[bool]$Server,[bool]$Unused=$false){
    $args=@(('"'+$project+'"'),$Map,'-nosteam','-unattended','-Multiprocess','-nosound','-nop4','-NoLiveCoding','-NoLoadingScreen','-DisablePython',
        '-UseIrisReplication=1','-RPGRequireIris','-DDC=(InstalledEnginePak,Local)','-ExecCmds="t.MaxFPS 30"',
        ('-UserDir="'+$temp+'/'+$Name+'"'),('-abslog="'+$temp+'/'+$Name+'.log"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$url/api",
        "-ini:Game:[/Script/RPGInventoryRuntime.RPGItemBackendSubsystem]:ApiUrl=$url/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$url/api",'-RPGChapterSevenPlaytest')
    $envs=@{PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null;PROJECT_RPG_BACKEND_ADMIN_TOKEN=$null}
    if($Server){$args+=@('-server',"-port=$GamePort",'-nullrhi','-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity');$envs.PROJECT_RPG_GAME_SERVER_ID=$serverId;$envs.PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId;$envs.PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken;if($Name-eq'BossServer'){$args+='-RPGChapterSevenBossFixture'}}
    else{$args+=@('-game','-RPGDevelopmentLogin','-RPGDungeonProfile=ChapterOne','-RenderOffscreen','-d3d12','-Windowed','-ResX=1280','-ResY=900',"-RPGTrainingCharacter=$characterId","-RPGTrainingSteam=$steam","-RPGTrainingSession=$sessionId",("-M07Phase="+$script:phase),('-M07Screenshot="'+$proof+'/'+$Name+'.png"'))}
    return Start-Owned $editor $args $envs
}
function Heartbeat {
    if((Api "dungeon-sessions/$sessionId" 'Get' $player).state -in @('Cleared','Failed','Abandoned','SettlementPending')){return}
    $null=Api "dungeon-sessions/$sessionId/heartbeat" 'Post' $serverHeaders @{serverId=$serverId}
}
function Play([string]$Name,[int]$Phase,[string]$Marker){
    $script:phase=$Phase;$client=Peer $Name '/Game/Maps/LobbyMap' $false
    $until=[DateTime]::UtcNow.AddSeconds(600);$next=[DateTime]::MinValue
    while(!$client.HasExited-and[DateTime]::UtcNow-lt$until){if($server.HasExited){throw 'Owned game server exited'};if([DateTime]::UtcNow-ge$next){Heartbeat;$next=[DateTime]::UtcNow.AddSeconds(10)};Start-Sleep -Milliseconds 250}
    if(!$client.HasExited){throw ('Input timeout '+$Name)};$client.WaitForExit()
    if($client.ExitCode-ne0-or(Log $Name)-notmatch$Marker){throw ('Play failure '+$Name)}
    Write-Output ('PASS '+$Name);Start-Sleep -Seconds 2
}
function Leave-Return {
    $null=Api "dungeon-sessions/$sessionId/members/$characterId/abandon" 'Post' $player
    Stop-Owned $server
}
function Begin-Session([string]$Dungeon,[string]$Name,[string]$Map,[bool]$Training){
    $s=Api 'dungeon-sessions' 'Post' $player @{characterId=$characterId;dungeonId=$Dungeon;difficulty='Normal'}
    $script:sessionId=[string]$s.dungeonSessionId;$script:serverId=$Name+'-'+$id
    $null=Api "dungeon-sessions/$sessionId/departure" 'Post' $player @{characterId=$characterId;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$s.lobbyRevision}
    $script:assignment=Api "dungeon-sessions/$sessionId/activate" 'Post' @{Authorization='Bearer '+$admin} @{serverId=$serverId;serverAddress="127.0.0.1:$GamePort"}
    $script:serverHeaders=@{Authorization='Bearer '+$assignment.gameServerAccessToken}
    $script:server=Peer ($Name+'Server') $Map $true $Training
    $until=[DateTime]::UtcNow.AddSeconds(600);$next=[DateTime]::MinValue
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
        DungeonContent__DevelopmentProfile='ChapterOne';Narrative__EnableDevelopmentContent='false';Narrative__CatalogPath=(Join-Path $workspace 'Backend/ProjectRpg.Backend/Narrative/Content/chapter01.json')
    } "$temp/backend.log"
    $ready=$false;for($n=0;$n-lt100;++$n){try{if((Invoke-RestMethod "$url/health/ready" -TimeoutSec 1).status-eq'ready'){$ready=$true;break}}catch{};if($backend.HasExited){throw 'Backend exited'};Start-Sleep -Milliseconds 250}
    if(!$ready){throw 'Backend readiness timeout'}
    $steam=([uint64]76561300000000000+[uint64][Convert]::ToUInt32($id.Substring(0,8),16)).ToString()
    $auth=Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$steam"};$player=@{Authorization='Bearer '+$auth.accessToken}
    $character=Api 'characters' 'Post' $player @{name=('M03'+$id.Substring(0,10));classId='RPGPlayerClassDefinition:GreatSwordWarrior'};$characterId=[string]$character.characterId
    # Test setup: previous quests/admissions/combat receipts use private authenticated APIs.
    # M07 itself uses native input/UMG, boss HP10 fixture and extraction; earlier quests are API fixtures, never goal injection.
    $result.Scope='M07 Memory/Iris UMG acceptance, existing boss with HP10 fixture/native attack/settlement, extraction input and new-process recovery. Prior M01-M06 and growth prerequisites are authenticated API fixtures; not Steam/PG/multiplayer or natural combat balance evidence.'
    $result.SeededPrerequisites=$true
    function Goal([string]$Quest,[string]$Objective,[string]$Session){
        $v=Api "characters/$characterId/narrative" 'Get' $player
        $null=Api "characters/$characterId/narrative/dungeon-objectives" 'Post' $player @{requestId=[guid]::NewGuid().ToString('D');expectedRevision=[string]$v.revision;catalogVersion=$v.catalogVersion;dungeonSessionId=$Session;questId=$Quest;objectiveId=$Objective}
    }
    Choice 'CH01.M01.Offer';Choice 'CH01.D01' 'confirm';Choice 'CH01.D02' 'confirm';Choice 'CH01.M02.Offer'
    $level=0
    $stages=@('Progression.Training','Progression.Intro1','Progression.Intro2','Progression.Intro3')
    if($PriorBoss){$stages+='Progression.FirstBoss';$result.Scope+=' Prior Normal is a seeded receipt, not a second combat playthrough.'}
    foreach($stage in $stages){
        if($stage-eq'Progression.Intro1'){Choice 'CH01.M03.Offer'}
        if($stage-eq'Progression.Intro2'){Choice 'CH01.M05.Offer'}
        if($stage-eq'Progression.Intro3'){Choice 'CH01.M06.Offer';Choice 'CH01.D06' 'confirm'}
        $ss=Api 'dungeon-sessions' 'Post' $player @{characterId=$characterId;dungeonId=$stage;difficulty='Normal'}
        $sid=[string]$ss.dungeonSessionId;$fixtureServer='fixture-'+[guid]::NewGuid().ToString('N')
        $null=Api "dungeon-sessions/$sid/departure" 'Post' $player @{characterId=$characterId;requestId=[guid]::NewGuid().ToString('D');expectedLobbyRevision=$ss.lobbyRevision}
        $assigned=Api "dungeon-sessions/$sid/activate" 'Post' @{Authorization='Bearer '+$admin} @{serverId=$fixtureServer;serverAddress="127.0.0.1:$GamePort"}
        $fh=@{Authorization='Bearer '+$assigned.gameServerAccessToken}
        $null=Api "dungeon-sessions/$sid/start" 'Post' $fh @{serverId=$fixtureServer}
        if($stage-ne'Progression.Training'-and$stage-ne'Progression.FirstBoss'){
            $ticket=Api 'join-tickets' 'Post' $player @{characterId=$characterId;dungeonSessionId=$sid}
            $member=Api 'join-tickets/consume' 'Post' $fh @{joinTicket=$ticket.joinTicket;serverId=$fixtureServer;consumptionRequestId=[guid]::NewGuid().ToString('D')}
            $null=Api "dungeon-sessions/$sid/members/$characterId/admission-confirmation" 'Post' $fh @{steamId=$steam;connectionGeneration=$member.connectionGeneration}
            $targets=if($stage-eq'Progression.Intro1'){@('CH01.CL01','CH01.CL02','CH01.CL03')}elseif($stage-eq'Progression.Intro2'){@('CH01.Daon')}else{@('CH01.M06.Patient')}
            foreach($target in $targets){$null=Api "dungeon-sessions/$sid/narrative/interactions/$characterId" 'Post' $fh @{eventId=[guid]::NewGuid().ToString('D');targetId=$target;connectionGeneration=[string]$member.connectionGeneration;observedAtUtc=[DateTime]::UtcNow.ToString('o')}}
            if($stage-eq'Progression.Intro1'){Choice 'CH01.M04.Offer'}elseif($stage-eq'Progression.Intro2'){Choice 'CH01.D05' 'confirm'}
        }
        $items=@();if($stage-ne'Progression.Training'){$items=@(@{definitionType='RPGItemDefinition';definitionName='GameItem.Material.Progression.Ore';definitionVersion=1;quantity=2;bindState='CharacterBound';durabilityCurrent=0;durabilityMaximum=0;instanceTags=@();statValues=@();maxStackSize=999})}
        if($stage-eq'Progression.FirstBoss'){
            $items[0].quantity=4
            $items+=@{definitionType='RPGItemDefinition';definitionName='GameItem.Material.Progression.Token';definitionVersion=1;quantity=2;bindState='CharacterBound';durabilityCurrent=0;durabilityMaximum=0;instanceTags=@();statValues=@();maxStackSize=999}
        }
        $null=Api "dungeon-sessions/$sid/settle-rewards" 'Post' $fh @{serverId=$fixtureServer;rewardVersion='FirstSlice.v1';changes=@();itemRewards=$items}
        $level++;$until=[DateTime]::UtcNow.AddSeconds(30)
        do{$after=Api "characters/$characterId/progression" 'Get' $player;if($after.experience-eq($level*100)){break};Start-Sleep -Milliseconds 200}while([DateTime]::UtcNow-lt$until)
        if($after.experience-ne($level*100)){throw 'Prerequisite fixture did not settle'}
        if($stage-eq'Progression.Training'){Goal 'CH01.M02' 'training' $sid}elseif($stage-eq'Progression.Intro1'){Goal 'CH01.M04' 'approach' $sid}elseif($stage-eq'Progression.Intro2'){Goal 'CH01.M05' 'opportunity' $sid}elseif($stage-eq'Progression.Intro3'){Goal 'CH01.M06' 'rescue' $sid}else{$priorBossSession=$sid}
    }
    $v=Api "characters/$characterId/narrative" 'Get' $player
    if(@($v.quests|Where-Object {$_.questId-eq'CH01.M05'-and$_.status-eq'Completed'}).Count-ne1){throw 'Prior story fixture incomplete'}
    Begin-Session 'Story.ChapterOneReturn' 'Offer' '/Game/Maps/Progression/L_ChapterOneReturn' $false
    Play 'OfferClient' 0 'M07_PLAY ACCEPTED_EARLY_EXPORT_REJECTED'
    Leave-Return
    if(!$PriorBoss){
        Begin-Session 'Progression.FirstBoss' 'Boss' '/Game/Maps/Progression/L_ChapterOneBoss' $false
        Play 'BossClient' 10 'M07_PLAY BOSS_SETTLED Export=0'
        $bossSession=$sessionId;Stop-Owned $server
    }else{$bossSession=$priorBossSession}
    $afterBoss=Api "characters/$characterId/progression" 'Get' $player
    if($afterBoss.experience-ne500-or$afterBoss.milestones-ne31-or$afterBoss.totalSP-ne14){throw 'Normal settlement/Hard unlock mismatch'}
    $itemsBefore=Api "items?ownerType=Character&ownerId=$characterId&includeTerminal=false" 'Get' $player
    Begin-Session 'Story.ChapterOneReturn' 'Return' '/Game/Maps/Progression/L_ChapterOneReturn' $false
    Play 'SafetyClient' 1 'M07_PLAY SAFETY_SAVED Export=0'
    # Actual client and server processes are both gone before a new recovery instance.
    Leave-Return
    Begin-Session 'Story.ChapterOneReturn' 'Resume' '/Game/Maps/Progression/L_ChapterOneReturn' $false
    Play 'ExportClient' 2 'M07_PLAY EXPORT_COMPLETE'
    if((Log 'ExportClient')-notmatch'M07_PLAY EXPORTED_PATIENT_OUTSIDE_B'){throw 'Final committed scene not rendered outside B'}
    if((Log 'ExportClient')-notmatch'M07_PLAY RESTORED Phase=2 Boss=1 Safe=1 Brace=0 Export=0'){throw 'Partial extraction not restored in new processes'}
    $after=Api "characters/$characterId/progression" 'Get' $player
    if(($after|ConvertTo-Json -Depth 12 -Compress)-cne($afterBoss|ConvertTo-Json -Depth 12 -Compress)){throw 'Recovery duplicated growth'}
    $itemsAfter=Api "items?ownerType=Character&ownerId=$characterId&includeTerminal=false" 'Get' $player
    if(($itemsBefore|ConvertTo-Json -Depth 20 -Compress)-cne($itemsAfter|ConvertTo-Json -Depth 20 -Compress)){throw 'Recovery changed inventory rewards'}
    $v=Api "characters/$characterId/narrative" 'Get' $player
    if(@($v.quests|Where-Object {$_.questId-eq'CH01.M07'-and$_.status-eq'Completed'}).Count-ne1){throw 'M07 not saved'}
    if((Get-FileHash "$RuntimeProjectRoot/Binaries/Win64/UnrealEditor-Project_RPG.dll").Hash-cne$result.ModuleSHA256){throw 'Module drift'}
    $result.FinalNarrative=$v;$result.Experience=$after.experience;$result.BossSession=$bossSession;$result.Passed=$true
}catch{$result.Failure=$_.Exception.Message}
finally{
    foreach($p in $owned){Stop-Owned $p}
    foreach($name in @('OfferServer','OfferClient','BossServer','BossClient','ReturnServer','SafetyClient','ResumeServer','ExportClient')){
        $text=Log $name;if($text){$text -split "`n"|Where-Object {$_-match'M07_PLAY|Dungeon session completion|RPG_DUNGEON_SERVER_READY|Fatal error|Assertion failed|Error:'}|Set-Content "$proof/$name.log" -Encoding UTF8}
        if($result.Failure -and $text){$text -split "`n"|Select-Object -Last 160|Set-Content "$proof/$name.FailureTail.log" -Encoding UTF8}
    }
    if(Test-Path "$temp/backend.log"){Copy-Item "$temp/backend.log" "$proof/Backend.log"}
    $resolved=[IO.Path]::GetFullPath($temp);$parent=[IO.Path]::GetFullPath((Join-Path $workspace ('Saved/'+$ownerRoot)))+'\'
    if(!$resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe cleanup target'}
    # Native disconnection journals can exceed MAX_PATH on Windows PowerShell.
    # Use bounded, non-following .NET traversal with extended absolute paths.
    $result|ConvertTo-Json -Depth 20|Set-Content "$proof/Result.json" -Encoding UTF8
    $script:runDeleted=[long]0
    function Remove-RunEntry([string]$normal){
        if(!$normal.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Outside own run boundary'}
        $long='\\?\'+$normal
        if(![IO.File]::Exists($long)-and![IO.Directory]::Exists($long)){return}
        $attr=[IO.File]::GetAttributes($long)
        if($attr-band[IO.FileAttributes]::ReparsePoint){
            if($attr-band[IO.FileAttributes]::Directory){[IO.Directory]::Delete($long)}else{[IO.File]::Delete($long)}
            return
        }
        if($attr-band[IO.FileAttributes]::Directory){
            foreach($entry in [IO.Directory]::EnumerateFileSystemEntries($long)){Remove-RunEntry $entry.Substring(4)}
            [IO.Directory]::Delete($long)
        }else{$script:runDeleted+=([IO.FileInfo]::new($long)).Length;[IO.File]::Delete($long)}
    }
    Remove-RunEntry $resolved
    $result.DeletedBytes=$script:runDeleted
    $result|ConvertTo-Json -Depth 20|Set-Content "$proof/Result.json" -Encoding UTF8
}
if(!$result.Passed){throw $result.Failure}
