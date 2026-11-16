[CmdletBinding()]
param([string]$RuntimeProjectRoot='', [string]$BackendRoot='', [int]$BackendPort=3092, [int]$GamePort=18912,
    [switch]$ApproachPractice, [switch]$OpportunityPractice, [switch]$ChapterFive,
    [string]$EngineRoot='D:/UE_5.8')
$ErrorActionPreference='Stop'
if($ChapterFive){$OpportunityPractice=$true}
if($OpportunityPractice){$ApproachPractice=$true}
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$RuntimeProjectRoot){$RuntimeProjectRoot=$workspace}
if(!$BackendRoot){$BackendRoot=Join-Path $workspace 'Backend'}
$ownerRoot=if($ChapterFive){'M05'}elseif($OpportunityPractice){'Intro2'}elseif($ApproachPractice){'M04'}else{'M03'}
$proofFolder=if($ChapterFive){'ChapterM05-20261117'}elseif($OpportunityPractice){'Intro2Opportunity-20261117'}elseif($ApproachPractice){'ChapterM04-20261116'}else{'ChapterM03-20261115'}
$profile=if($OpportunityPractice){'Intro2'}else{'Intro1'}
$id=[Guid]::NewGuid().ToString('N');$temp=Join-Path $workspace ('Saved/'+$ownerRoot+'/play-'+$id)
$proof=Join-Path $workspace ('Saved/BuildChecks/'+$proofFolder+'/play-'+$id)
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
function Peer([string]$Name,[string]$Map,[bool]$Server,[bool]$Training,[bool]$Continue=$false,[int]$OpportunityPhase=0,[int]$SurvivorPhase=0){
    $args=@(('"'+$project+'"'),$Map,'-nosteam','-unattended','-nosound','-nop4','-NoLiveCoding','-NoLoadingScreen','-DisablePython',
        '-UseIrisReplication=1','-RPGRequireIris','-DDC=(InstalledEnginePak,Local)','-ExecCmds="t.MaxFPS 30"',
        ('-UserDir="'+$temp+'/'+$Name+'"'),('-abslog="'+$temp+'/'+$Name+'.log"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$url/api",
        "-ini:Game:[/Script/RPGInventoryRuntime.RPGItemBackendSubsystem]:ApiUrl=$url/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$url/api")
    $envs=@{PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null;PROJECT_RPG_BACKEND_ADMIN_TOKEN=$null}
    if($Server){$args+=@('-server',"-port=$GamePort",'-nullrhi','-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity');$envs.PROJECT_RPG_GAME_SERVER_ID=$serverId;$envs.PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId;$envs.PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken}
    else{$args+=@('-game','-RPGDevelopmentLogin',('-RPGDungeonProfile='+$profile));if($Training){$args+='-nullrhi'}else{$args+=@('-RenderOffscreen','-d3d12','-Windowed','-ResX=1280','-ResY=900',('-M03Screenshot="'+$proof+'/M03.png"'),('-Intro2Screenshot="'+$proof+'/Intro2.png"'))}}
    if($Training -or !$Server){
        $args+=@("-RPGTrainingCharacter=$characterId","-RPGTrainingSteam=$steam","-RPGTrainingSession=$sessionId",('-RPGTrainingGates="'+$temp+'"'))
        $args+=$(if($Training){'-RPGTrainingPlaytest'}elseif($SurvivorPhase){'-RPGChapterFivePlaytest'}elseif($OpportunityPhase){'-RPGIntroOpportunityPlaytest'}else{'-RPGChapterOnePlaytest'})
        if($SurvivorPhase-eq2){$args+='-RPGChapterFiveContinue'}
        if($OpportunityPhase -and $ChapterFive){$args+='-RPGChapterFiveCombat'}
        if($OpportunityPhase-eq2){$args+='-RPGIntroOpportunityContinue'}
        if($Continue -and !$OpportunityPhase){$args+='-RPGChapterOneContinue'}
        if($Continue -and $ApproachPractice -and !$OpportunityPhase){$args+='-RPGChapterOneApproach'}
        if($Training -and $ApproachPractice){$args+='-RPGTrainingObserveMaximumCharge'}
    }
    return Start-Owned $editor $args $envs
}
function Heartbeat {
    if((Api "dungeon-sessions/$sessionId" 'Get' $player).state -in @('Cleared','Failed','Abandoned','SettlementPending')){return}
    $null=Api "dungeon-sessions/$sessionId/heartbeat" 'Post' $serverHeaders @{serverId=$serverId}
}
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
        DungeonContent__DevelopmentProfile=$profile;Narrative__EnableDevelopmentContent='false';Narrative__CatalogPath=(Join-Path $workspace 'Backend/ProjectRpg.Backend/Narrative/Content/chapter01.json')
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
    if($ApproachPractice){
        # Save an acquired skill through the ordinary player configuration API while outside a live session.
        # No acquired skills, levels, SP or milestone values are authored by this driver.
        $slots=@($growth.skillSlots);$whirl='Player.Ability.Skill.WhirlWind'
        if($slots -notcontains $whirl){
            $empty=[Array]::IndexOf($slots,'');if($empty-lt0){throw 'No empty skill slot'};$slots[$empty]=$whirl
            $growth=Api "characters/$characterId/skill-configuration" 'Put' $player @{requestId=[Guid]::NewGuid().ToString('D');expectedRevision=[string]$growth.revision;skills=@($growth.skills);skillSlots=$slots}
        }
        if($growth.experience-ne100){throw 'Skill configuration changed experience'}
    }
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
        $expected=if($phase-eq1){'M03_PLAY CART_SAVED'}elseif($ApproachPractice){'M04_PLAY COMPLETE Saved=1 ConfirmedCaption=1'}else{'M03_PLAY COMPLETE M04_OFFERED'}
        if((Log $name)-notmatch$expected){throw "Missing M03 proof phase $phase"}
        Write-Output "PASS actual M03 phase $phase"
        Start-Sleep -Seconds 2
    }
    $v=Api "characters/$characterId/narrative" 'Get' $player
    if(@($v.quests|Where-Object {$_.questId-eq'CH01.M03'-and$_.status-eq'Completed'}).Count-ne1){throw 'M03 commit missing'}
    $after=Api "characters/$characterId/progression" 'Get' $player
    if($ApproachPractice){
        if($after.experience-ne200 -or ($after.milestones-band 2)-ne2 -or $after.totalSP-ne6 -or
            @($after.skills|Where-Object skillTag -eq 'Player.Ability.Skill.JumpSmash').Count-ne1 -or
            $after.skillSlots -contains 'Player.Ability.Skill.JumpSmash'){throw 'Actual Intro1 growth or explicit skill-slot policy mismatch'}
        if(@($v.quests|Where-Object {$_.questId-eq'CH01.M04'-and$_.status-eq'Completed'}).Count-ne1){throw 'M04 saved completion missing'}
        if((Log 'M03Client2')-notmatch'M04_PLAY INPUT_PASS Steps=3 ActualSkills=2'){throw 'Actual Intro1 actions missing'}
        $repeat=Api "characters/$characterId/narrative" 'Get' $player
        $growthAgain=Api "characters/$characterId/progression" 'Get' $player
        if(($after|ConvertTo-Json -Depth 12 -Compress)-cne($growthAgain|ConvertTo-Json -Depth 12 -Compress)){throw 'M04 refresh duplicated growth'}
        $result.Scope='Real Training, M03 reconnect, M04 UMG acceptance, actual Intro1 skills, settlement and saved quest; Memory HTTP/Iris, not PostgreSQL or Steam'
    }elseif(($growth|ConvertTo-Json -Depth 12 -Compress)-cne($after|ConvertTo-Json -Depth 12 -Compress)){throw 'Investigation altered growth'}
    if($OpportunityPractice){
        Stop-Owned $client;Stop-Owned $server
        Begin-Session 'Progression.Intro2' 'Intro2' '/Game/Maps/Progression/L_ProgressionIntro2' $false
        if($ChapterFive){
            Choice 'CH01.M05.Offer'
            foreach($phase in @(1,2)){
                $name='DaonClient'+$phase;$client=Peer $name '/Game/Maps/LobbyMap' $false $false $false 0 $phase
                $until=[DateTime]::UtcNow.AddSeconds(240);$next=[DateTime]::MinValue
                while(!$client.HasExited-and[DateTime]::UtcNow-lt$until){if([DateTime]::UtcNow-ge$next){Heartbeat;$next=[DateTime]::UtcNow.AddSeconds(10)};Start-Sleep -Milliseconds 250}
                if(!$client.HasExited){throw 'Daon interaction timeout'};$client.WaitForExit()
                $expected=if($phase-eq1){'M05_PLAY DISCOVERY_SAVED'}else{'M05_PLAY TESTIMONY_SAVED CombatPending=1'}
                if($client.ExitCode-ne0 -or (Log $name)-notmatch$expected){throw "Daon interaction failed phase $phase"}
                Start-Sleep -Seconds 2
            }
            if((Api "characters/$characterId/progression" 'Get' $player).experience-ne200){throw 'Daon interaction granted growth'}
        }
        foreach($phase in @(1,2)){
            $name='Intro2Client'+$phase;$client=Peer $name '/Game/Maps/LobbyMap' $false $false $false $phase
            $until=[DateTime]::UtcNow.AddSeconds(240);$next=[DateTime]::MinValue
            while(!$client.HasExited-and[DateTime]::UtcNow-lt$until){if([DateTime]::UtcNow-ge$next){Heartbeat;$next=[DateTime]::UtcNow.AddSeconds(10)};Start-Sleep -Milliseconds 250}
            if(!$client.HasExited){throw 'Intro2 input timed out'};$client.WaitForExit()
            if($client.ExitCode-ne0){throw "Intro2 client failed phase $phase"}
            $expected=if($phase-eq1){'INTRO2_PLAY ONE_SAVED'}else{'INTRO2_PLAY COMPLETE Count=2 DistinctRounds=1'}
            if((Log $name)-notmatch$expected -or (Log $name)-notmatch'INTRO2_PLAY SAME_ROUND Count=1'){throw "Missing Intro2 input proof phase $phase"}
            if($phase-eq2 -and (Log $name)-notmatch'INTRO2_PLAY SETTLED State=Cleared'){throw 'Client left before authoritative settlement'}
            if($phase-eq1 -and (Api "dungeon-sessions/$sessionId" 'Get' $player).state-ne'InProgress'){throw 'One opportunity settled early'}
            Start-Sleep -Seconds 2
        }
        if((Log 'Intro2Server')-notmatch'INTRO2_REJECTED Closed'){throw 'No actual damage outside the window observed'}
        if(@([regex]::Matches((Log 'Intro2Server'),'INTRO2_REJECTED SameRound')).Count-lt2){throw 'No actual same-round damage rejection for both lifetimes'}
        $until=[DateTime]::UtcNow.AddSeconds(30)
        do{$after=Api "characters/$characterId/progression" 'Get' $player;if($after.experience-eq300){break};Start-Sleep -Milliseconds 250}while([DateTime]::UtcNow-lt$until)
        if($after.experience-ne300 -or $after.totalSP-ne9 -or $after.milestones-ne7){throw 'Intro2 actual settlement/growth mismatch'}
        $again=Api "characters/$characterId/progression" 'Get' $player
        if(($after|ConvertTo-Json -Depth 12 -Compress)-cne($again|ConvertTo-Json -Depth 12 -Compress)){throw 'Intro2 repeat read changed growth'}
        $result.Scope='Actual Training/M03/M04/Intro1 then Intro2 closed-hit rejection, same-round rejection, client restart/reset, distinct-round hits2, settlement XP300/SP9. Memory HTTP/Iris; Steam/PostgreSQL not exercised.'
        if($ChapterFive){
            $v=Api "characters/$characterId/narrative" 'Get' $player
            if(@($v.quests|Where-Object {$_.questId-eq'CH01.M05'-and$_.status-eq'Completed'}).Count-ne1 -or (Log 'Intro2Client2')-notmatch'M05_PLAY COMPLETE Saved=1'){throw 'M05 completion not persisted'}
            $result.Scope+=' M05 server discovery, restarted-client restored discovery, actual testimony UMG, combat receipt auto recognition and saved quest completion.'
        }
    }
    if((Get-FileHash "$RuntimeProjectRoot/Binaries/Win64/UnrealEditor-Project_RPG.dll").Hash-cne$result.ModuleSHA256){throw 'Runtime module changed during the playtest'}
    $result.FinalNarrative=$v;$result.Experience=$after.experience;$result.Passed=$true
}catch{$result.Failure=$_.Exception.Message}
finally{
    foreach($p in $owned){Stop-Owned $p}
    foreach($name in @('TrainingServer','TrainingClient','M03Server','M03Client1','M03Client2','Intro2Server','Intro2Client1','Intro2Client2','DaonClient1','DaonClient2')){
        $text=Log $name;if($text){$text -split "`n"|Where-Object {$_-match'INTRO2_|M03_PLAY|M04_PLAY|M05_PLAY|RPG_TRAINING_PLAYTEST|Fatal error|Assertion failed|Error:'}|Set-Content "$proof/$name.log" -Encoding UTF8}
        if($result.Failure -and $text){$text -split "`n"|Select-Object -Last 160|Set-Content "$proof/$name.FailureTail.log" -Encoding UTF8}
    }
    if(Test-Path "$temp/backend.log"){Copy-Item "$temp/backend.log" "$proof/Backend.log"}
    $resolved=[IO.Path]::GetFullPath($temp);$parent=[IO.Path]::GetFullPath((Join-Path $workspace ('Saved/'+$ownerRoot)))+'\'
    if(!$resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe cleanup target'}
    $entries=@(Get-ChildItem -LiteralPath $temp -Recurse -Force)
    if(@($entries|Where-Object {$_.Attributes-band[IO.FileAttributes]::ReparsePoint}).Count){throw 'Unexpected link in owned run'}
    $result.DeletedBytes=($entries|Where-Object {!$_.PSIsContainer}|Measure-Object Length -Sum).Sum
    Remove-Item -LiteralPath $temp -Recurse -Force
    $result|ConvertTo-Json -Depth 20|Set-Content "$proof/Result.json" -Encoding UTF8
}
if(!$result.Passed){throw $result.Failure}
