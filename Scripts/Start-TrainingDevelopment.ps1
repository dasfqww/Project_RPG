[CmdletBinding()]
param(
    [string]$EngineRoot='D:\UE_5.8',
    [ValidateSet('Training','Intro1')][string]$DungeonProfile='Training',
    [ValidateRange(1024,65535)][int]$BackendPort=3076,
    [ValidateRange(1024,65535)][int]$GamePort=18896,
    [ValidateRange(60,600)][int]$StartupTimeoutSeconds=240,
    [switch]$SmokeTest,
    [switch]$SessionLoopSmoke,
    [switch]$LobbyUISmoke,
    [switch]$RecoverOnly,
    [ValidateRange(1024,65535)][int]$McpPort=8014
)

# Disposable local training sessions. The normal mode opens an interactive
# lobby; SmokeTest drives existing client ability input, not lobby button clicks.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Tests/TrainingDevelopmentContent.ps1')
$contentMaps=Get-RPGTrainingDevelopmentContent $DungeonProfile
if($DungeonProfile -ceq 'Intro1' -and ($SmokeTest -or $SessionLoopSmoke -or $LobbyUISmoke)){throw 'Intro1 requires interactive mode; existing smoke drivers cover Training only'}
if($SessionLoopSmoke -and ($SmokeTest -or $LobbyUISmoke -or $RecoverOnly)){throw 'SessionLoopSmoke is a separate smoke mode'}
if($SessionLoopSmoke){$SmokeTest=[switch]$true}
if($SmokeTest -and $LobbyUISmoke){throw 'Choose gameplay smoke or lobby UI smoke'}
if($RecoverOnly -and ($SmokeTest -or $LobbyUISmoke)){throw 'Recovery-only cannot launch a smoke test'}
$workspace=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Tests/TrainingDevelopmentRecovery.ps1')
$recovered=@(Repair-RPGTrainingDevelopmentRuns)
if($RecoverOnly){$recovered|ConvertTo-Json -Depth 4;return}
foreach($entry in $recovered){if($entry.status -eq 'Recovered'){Write-Host ('Recovered interrupted training run '+$entry.runId+'; removed bytes='+$entry.deletedBytes)}}
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$sourceModule=Join-Path $workspace 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
foreach($path in @($editor,$sourceModule,(Join-Path $workspace 'Project_RPG.uproject'))){
    if(!(Test-Path -LiteralPath $path)){throw ('Missing prerequisite: '+$path)}
}
if(@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() | Where-Object Port -eq $BackendPort).Count -or
   @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveUdpListeners() | Where-Object Port -eq $GamePort).Count){throw 'Requested local port is already in use'}
if($LobbyUISmoke -and @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() | Where-Object Port -eq $McpPort).Count){throw 'Owned UI MCP port is already in use'}
. (Join-Path $PSScriptRoot 'Tests/TrainingGuideArtifacts.ps1')
. (Join-Path $PSScriptRoot 'Tests/TrainingDevelopmentSessions.ps1')
if($LobbyUISmoke){. (Join-Path $PSScriptRoot 'Tests/TrainingLobbyUIObservation.ps1')}
$runId=[Guid]::NewGuid().ToString('N')
$runRoot=Join-Path $workspace ('Saved/TrainingDevelopment/'+$runId)
$temp=Join-Path $runRoot 'Temp'
$runtime=Join-Path $temp 'RuntimeProject'
$userParent=Join-Path $workspace ('Saved/Tests/TP/'+$runId.Substring(0,12))
$users=Join-Path $userParent 'U'
$journal=Join-Path $users "s\Saved\Online\DungeonDisconnections\$('a'*32)\$('b'*32)\$([Guid]::Empty)\ConsumeRecoveries"
if($journal.Length+46 -ge 260){throw 'Workspace path is too long for scoped training journals'}
if(Test-Path -LiteralPath $userParent){throw 'Refusing to reuse a runtime user directory'}
$url="http://127.0.0.1:$BackendPort"
$map='/Game/Maps/Progression/L_ProgressionTraining'
$admin=[Guid]::NewGuid().ToString('N')
$processes=[Collections.Generic.List[object]]::new()
$inputs=[Collections.Generic.List[object]]::new()
$loaded=[Collections.Generic.List[object]]::new()
$allocation=$null;$server=$null;$client=$null
$clientLogPath=Join-Path $temp 'client.log'
$round=1;$allocationLogStart=0
$retired=[Collections.Generic.List[object]]::new()
$rounds=[Collections.Generic.List[object]]::new()
$ownershipRecord=$null
$result=[ordered]@{passed=$false;mode=$(if($SessionLoopSmoke){'Two-character session loop smoke'}elseif($LobbyUISmoke){'Slate lobby UI smoke'}elseif($SmokeTest){'Headless smoke'}else{'Interactive lifecycle'});failure=$null;cleanup=$null}

function Copy-Fixed([string]$From,[string]$To){
    $hash=Get-SharedInputHash $From
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $To) -Force
    $source=[IO.File]::Open($From,'Open','Read',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try{$destination=[IO.File]::Open($To,'CreateNew','Write','None');try{$source.CopyTo($destination)}finally{$destination.Dispose()}}finally{$source.Dispose()}
    if((Get-FileHash -LiteralPath $To).Hash -cne $hash -or (Get-SharedInputHash $From) -cne $hash){throw 'Input changed during private snapshot'}
    $inputs.Add([pscustomobject]@{source=$From;snapshot=$To;sha256=$hash})
}
function Get-SharedInputHash([string]$Path){
    $stream=[IO.File]::Open($Path,'Open','Read',([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $sha=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$sha.Dispose();$stream.Dispose()}
}
function Register-Owned($Process,[string]$Role){
    $handle=$Process.Handle
    $processes.Add([pscustomobject]@{process=$Process;role=$Role;path=$Process.Path;ticks=$Process.StartTime.ToUniversalTime().Ticks})
    if($ownershipRecord){Update-RPGTrainingLifecycle $runRoot $ownershipRecord $processes.ToArray()}
    return $Process
}
function Stop-Registered($Process){
    $matches=@($processes|Where-Object {$_.process -eq $Process})
    if($matches.Count -ne 1){throw 'Exact owned process registration is required'}
    $entry=$matches[0];$Process.Refresh()
    if(!$Process.HasExited){
        if($Process.Path -ine $entry.path -or $Process.StartTime.ToUniversalTime().Ticks -ne $entry.ticks){throw 'Owned process identity changed'}
        $Process.Kill()
    }
    if(!$Process.WaitForExit(10000)){throw 'Owned process did not release files'}
    $processes.Remove($entry)|Out-Null
    Update-RPGTrainingLifecycle $runRoot $ownershipRecord $processes.ToArray()
    $Process.Dispose()
}
function New-SmokeTraining([int]$Round){
    $character=Api 'characters' 'Post' $player @{name=('Training'+$runId.Substring(0,8)+'_'+$Round);classId='RPGPlayerClassDefinition:GreatSwordWarrior'}
    $id=[string]$character.characterId
    $growth=Api "characters/$id/progression" 'Get' $player
    if($growth.experience -ne 0 -or $growth.level -ne 1 -or $growth.totalSP -ne 0){throw 'New character inherited another character growth'}
    $session=Api 'dungeon-sessions' 'Post' $player @{characterId=$id;dungeonId='Progression.Training';difficulty='Normal'}
    $sid=[string]$session.dungeonSessionId
    $null=Api "dungeon-sessions/$sid/departure" 'Post' $player @{characterId=$id;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$session.lobbyRevision}
    return @{characterId=$id;sessionId=$sid}
}
function Get-SmokeArguments {
    $extra=@('-unattended','-nosound','-RPGTrainingPlaytest',"-RPGTrainingCharacter=$characterId","-RPGTrainingSteam=$steam","-RPGTrainingSession=$sessionId",('-RPGTrainingGates="'+$smokeGates+'"'))
    if($SessionLoopSmoke){$extra+=@('-RPGTrainingCompletionUI','-RenderOffscreen','-d3d12','-Windowed','-ResX=1280','-ResY=900')}
    else{$extra+='-nullrhi'}
    return @($clientBaseArguments)+$extra
}
function Start-Owned([string]$File,[string[]]$Arguments,[string]$Directory,[hashtable]$Environment,[string]$Log='',[switch]$Visible){
    $previous=@{}
    try{
        foreach($key in $Environment.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$Environment[$key],'Process')}
        $args=@{FilePath=$File;ArgumentList=$Arguments;WorkingDirectory=$Directory;PassThru=$true;WindowStyle=$(if($Visible){'Normal'}else{'Hidden'})}
        if($Log){$args.RedirectStandardOutput=$Log;$args.RedirectStandardError=$Log+'.stderr'}
        return Register-Owned (Start-Process @args) ([IO.Path]::GetFileName($File))
    }finally{foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key],'Process')}}
}
function Api([string]$Path,[string]$Method='Get',[hashtable]$Headers=@{},[object]$Body=$null){
    $args=@{Uri="$url/api/$Path";Method=$Method;Headers=$Headers;TimeoutSec=10;UseBasicParsing=$true}
    if($null -ne $Body){$args.ContentType='application/json';$args.Body=$Body | ConvertTo-Json -Depth 8 -Compress}
    try{$response=Invoke-WebRequest @args}catch{throw "Local training API $Method $Path failed; response/credentials omitted"}
    if($response.Content){return $response.Content | ConvertFrom-Json}
}
function Read-Log([string]$Path){
    if(!(Test-Path -LiteralPath $Path)){return ''}
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader=[IO.StreamReader]::new($stream)
    try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
}
function Poll-Allocator {
    # Existing allocator scripts support optional JSON properties; scope their
    # original semantics here, without changing the cleanup helper's strictness.
    Set-StrictMode -Off
    $mapAssertion=@{}
    if($DungeonProfile -ceq 'Training'){$mapAssertion.Map=$map}
    & (Join-Path $temp 'Tools/Start-LocalDungeonServer.ps1') -ServerExecutable $editor `
        -EditorProject (Join-Path $runtime 'Project_RPG.uproject') -DevelopmentIdentityTesting `
        -RuntimeUserDirectory (Join-Path $users 's') -BackendUrl $url -AdminToken $admin `
        -PublicHost '127.0.0.1' -Port $GamePort @mapAssertion -LogDirectory (Join-Path $temp 'Allocator')
}
function Observe-Module($Process,[string]$Role){
    if(!$Process -or @($loaded | Where-Object role -ceq $Role).Count){return}
    try{$modules=@((Get-Process -Id $Process.Id -ErrorAction Stop).Modules | Where-Object ModuleName -ceq 'UnrealEditor-Project_RPG.dll')}catch{return}
    if($modules.Count -eq 1){
        if($modules[0].FileName -ine $module -or (Get-FileHash -LiteralPath $modules[0].FileName).Hash -cne $result.moduleSha256){throw 'Private game module provenance mismatch'}
        $loaded.Add([pscustomobject]@{role=$Role;pid=$Process.Id;path=$modules[0].FileName;sha256=$result.moduleSha256})
    }
}
try{
    $null=New-Item -ItemType Directory -Path $temp -Force
    $ownershipRecord=New-RPGTrainingLifecycle $runRoot $EngineRoot
    Copy-Fixed (Join-Path $workspace 'Project_RPG.uproject') (Join-Path $runtime 'Project_RPG.uproject')
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $workspace 'Config') -File -Recurse){
        Copy-Fixed $file.FullName (Join-Path (Join-Path $runtime 'Config') $file.FullName.Substring((Join-Path $workspace 'Config').Length+1))
    }
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $workspace 'Binaries/Win64') -File){
        if($file.Extension -eq '.dll' -or $file.Name -in @('UnrealEditor.modules','Project_RPGEditor.target')){
            Copy-Fixed $file.FullName (Join-Path (Join-Path $runtime 'Binaries/Win64') $file.Name)
        }
    }
    foreach($name in @('Content','Plugins')){$null=New-Item -ItemType Junction -Path (Join-Path $runtime $name) -Target (Join-Path $workspace $name)}
    foreach($name in @('Start-LocalDungeonServer.ps1','LocalDungeonServerReadiness.ps1')){
        Copy-Fixed (Join-Path $workspace ('Backend/Tools/'+$name)) (Join-Path $temp ('Tools/'+$name))
    }
    $backendSource=Join-Path $temp 'Backend'
    foreach($part in @('ProjectRpg.Backend','Database')){
        foreach($file in Get-ChildItem -LiteralPath (Join-Path $workspace ('Backend/'+$part)) -File -Recurse | Where-Object FullName -notmatch '\\(bin|obj|artifacts)\\'){
            Copy-Fixed $file.FullName (Join-Path $backendSource $file.FullName.Substring((Join-Path $workspace 'Backend').Length+1))
        }
    }
    # Finish a coherent read window before using any closed copies.
    foreach($entry in $inputs){if((Get-SharedInputHash $entry.source) -cne $entry.sha256){throw 'Snapshot cohort changed before launch'}}
    $inputs.ToArray() | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'Inputs.json') -Encoding UTF8
    $module=Join-Path $runtime 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
    $result.moduleSha256=(Get-FileHash -LiteralPath $module).Hash
    $build=Join-Path $temp 'Build'
    $dotnet=(Get-Command dotnet).Source
    & $dotnet build (Join-Path $backendSource 'ProjectRpg.Backend/ProjectRpg.Backend.csproj') -c Release --artifacts-path (Join-Path $temp 'Artifacts') --output $build *> (Join-Path $runRoot 'Build.log')
    if($LASTEXITCODE -ne 0){throw 'Private backend build failed'}
    $backend=Start-Owned $dotnet @(('"'+(Join-Path $build 'ProjectRpg.Backend.dll')+'"')) $build @{
        ASPNETCORE_ENVIRONMENT='Development';DOTNET_ENVIRONMENT='Development';Storage__Provider='Memory'
        POSTGRES_CONNECTION_STRING=$null;ConnectionStrings__Postgres=$null;ASPNETCORE_CONTENTROOT=$null
        PROJECT_RPG_BACKEND_ADMIN_TOKEN=$admin;Steam__AllowDevelopmentAuthentication='true'
        Kestrel__Endpoints__Http__Url=$url;DungeonContent__DevelopmentProfile=$DungeonProfile
        PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null
    } (Join-Path $temp 'backend.log')
    $ready=$false;$until=[DateTime]::UtcNow.AddSeconds(45)
    while([DateTime]::UtcNow -lt $until){
        if($backend.HasExited){throw 'Private backend exited before readiness'}
        try{$health=Invoke-RestMethod "$url/health/ready" -TimeoutSec 2;if($health.status -eq 'ready' -and $health.storageProvider -eq 'Memory'){$ready=$true;break}}catch{}
        Start-Sleep -Milliseconds 200
    }
    if(!$ready -or @(Get-NetTCPConnection -State Listen -LocalPort $BackendPort | Where-Object {$_.OwningProcess -eq $backend.Id -and $_.LocalAddress -eq '127.0.0.1'}).Count -ne 1){throw 'Private Memory backend listener was not confirmed'}
    if($SmokeTest){
        $steam=([uint64]76561300000000000+[uint64][Convert]::ToUInt32($runId.Substring(0,8),16)).ToString()
        $login=Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$steam"}
        $player=@{Authorization='Bearer '+$login.accessToken}
        $fixture=New-SmokeTraining $round
        $characterId=$fixture.characterId;$sessionId=$fixture.sessionId
        $smokeGates=if($SessionLoopSmoke){Join-Path $runRoot 'Round1'}else{$runRoot}
        $null=New-Item -ItemType Directory -Path $smokeGates -Force
    }
    $args=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Game/Maps/LobbyMap','-game',
        ('-RPGDungeonProfile='+$DungeonProfile),'-RPGDevelopmentLogin','-nosteam','-UseIrisReplication=1','-RPGRequireIris',
        '-nop4','-nosplash','-NoLiveCoding','-NoLoadingScreen','-DisablePython','-DDC=(EnginePak,Local)',
        ('-UserDir="'+(Join-Path $users 'c')+'"'),('-abslog="'+(Join-Path $temp 'client.log')+'"'),'-ExecCmds="t.MaxFPS 60"',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$url/api",
        "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$url/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$url/api")
    $clientBaseArguments=@($args)
    if($SmokeTest){$args=Get-SmokeArguments}
    elseif($LobbyUISmoke){
        $args=@($args | Where-Object {$_ -notin @('-game','-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')})
        $args+=@('-editor','-RenderOffscreen','-Windowed','-ResX=1280','-ResY=900','-ForceRes','-nosound','-d3d12',
            '-ModelContextProtocolStartServer',"-ModelContextProtocolPort=$McpPort",'-ini:Engine:HTTPServer.Listeners:DefaultBindAddress=127.0.0.1',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:PlayNumberOfClients=1',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:NewWindowWidth=1280',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:NewWindowHeight=900',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:PlayNetMode=PIE_Standalone',
            '-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:bLaunchSeparateServer=False',
            '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=True',
            "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:ServerPortNumber=$McpPort")
    }
    else{$args+=@('-Windowed','-ResX=1280','-ResY=720');Write-Host 'Temporary training lobby: development login > server selection > character > connect > lobby > enter tutorial. Close the game to clean up. Memory saves end with this run.'}
    # The smoke client resumes only after the real assigned server is ready.
    if(!$SmokeTest){
        $clientExe=if($LobbyUISmoke){Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'}else{$editor}
        $client=Start-Owned $clientExe $args $runtime @{PROJECT_RPG_BACKEND_ADMIN_TOKEN=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null;PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null} -Visible:(!$LobbyUISmoke)
        if($LobbyUISmoke){
            $steam=([uint64]76561300000000000+[uint64][Convert]::ToUInt32($runId.Substring(0,8),16)).ToString()
            Start-TrainingLobbyUI $client $McpPort $steam ('UI'+$runId.Substring(0,8))
        }
    }
    $deadline=[DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    $nextPoll=[DateTime]::MinValue;$smokePassed=$false
    while($true){
        Observe-Module $server $(if($SessionLoopSmoke){'server-'+$round}else{'server'})
        Observe-Module $client $(if($SessionLoopSmoke){'client-'+$round}else{'client'})
        if($backend.HasExited){throw 'Private backend exited'}
        if($client -and $client.HasExited){if($SmokeTest -or $LobbyUISmoke){throw 'Smoke client exited before completion'};break}
        if($server -and (!$SmokeTest -or $SessionLoopSmoke)){
            if([DateTime]::UtcNow -ge $nextPoll -or $server.HasExited){
                $nextPoll=[DateTime]::UtcNow.AddSeconds(2)
                $snapshot=$null
                try{$snapshot=Api ('dungeon-sessions/'+$allocation.DungeonSessionId) 'Get' @{Authorization='Bearer '+$admin}}catch{Write-Warning 'Training session status unavailable; existing server was preserved.'}
                $clientLog=Read-Log $clientLogPath
                if((Get-RPGTrainingServerDisposition $allocation $snapshot $clientLog $allocationLogStart $DungeonProfile) -ceq 'Retire'){
                    $retired.Add(@{sessionId=$allocation.DungeonSessionId;serverId=$allocation.ServerId;processId=$server.Id;clientAlive=($client -and !$client.HasExited);backendState=$snapshot.state;returnToLobbyObserved=$true})
                    Stop-Registered $server
                    $server=$null;$allocation=$null
                    Write-Host 'Completed training server released after this client returned to the lobby.'
                }
            }
        }
        if($server -and $server.HasExited){throw 'Owned training server exited before confirmed terminal state and lobby return'}
        if(!$server -and [DateTime]::UtcNow -ge $nextPoll){
            $allocationLogStart=(Read-Log $clientLogPath).Length
            $candidate=Poll-Allocator
            $nextPoll=[DateTime]::UtcNow.AddSeconds(2)
            if($candidate.WaitingSessionAvailable){
                if($candidate.Reused){throw 'Private allocator unexpectedly reused a process'}
                $allocation=$candidate
                $server=Register-Owned (Get-Process -Id $candidate.ProcessId -ErrorAction Stop) 'training-server'
                Assert-RPGTrainingDevelopmentAllocation $candidate $DungeonProfile
                Write-Host 'Training server allocated on the private loopback port.'
            }
        }
        if($SmokeTest){
            if([DateTime]::UtcNow -ge $deadline){throw 'Training smoke timed out'}
            if($server -and !$client){
                $session=Api "dungeon-sessions/$sessionId" 'Get' $player
                $marker="RPG_DUNGEON_SERVER_READY Session=$sessionId Server=$($allocation.ServerId) Dungeon=Progression.Training Map=$map Difficulty=Normal"
                if($session.state -eq 'InProgress' -and (Read-Log $allocation.LogPath).Contains($marker)){
                    $client=Start-Owned $editor $args $runtime @{PROJECT_RPG_BACKEND_ADMIN_TOKEN=$null;PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=$null;PROJECT_RPG_GAME_SERVER_ID=$null;PROJECT_RPG_DUNGEON_SESSION_ID=$null}
                }
            }
            if($client){
                $log=Read-Log $clientLogPath
                if($log -match 'RPG_TRAINING_PLAYTEST FAIL|Fatal error:|Assertion failed:|Ensure condition failed:'){throw 'Training smoke client failed'}
                $session=Api "dungeon-sessions/$sessionId" 'Get' $player
                if($session.state -eq 'Cleared' -and $log -match 'RPG_TRAINING_PLAYTEST INPUT_PASS Actions=4 Mask=15 ActualInput=1 Iris=1'){
                    if(!$SessionLoopSmoke){$smokePassed=$true;break}
                    if($server -or $log -notmatch 'RPG_TRAINING_PLAYTEST COMPLETION_UI_RETURNED Lobby=1 IdentityPreserved=1'){continue}
                    $growth=Api "characters/$characterId/progression" 'Get' $player
                    if($growth.experience -ne 100 -or $growth.level -ne 2 -or $growth.totalSP -ne 3 -or !$growth.identityUnlocked){throw 'Training round reward mismatch'}
                    $rounds.Add(@{round=$round;characterId=$characterId;sessionId=$sessionId;growth=$growth;resultButtonReturn=$true})
                    $log -split '\r?\n'|Where-Object {$_ -match 'RPG_TRAINING_PLAYTEST |Bringing World /Game/Maps/' -and $_ -notmatch '(?i)token|ticket|authorization|Browse:|command\s*line'}|Set-Content (Join-Path $runRoot ('round-'+$round+'.proof.log')) -Encoding UTF8
                    Set-Content -LiteralPath (Join-Path $smokeGates 'stop.ready') -Value 'stop' -Encoding ASCII
                    if(!$client.WaitForExit(30000) -or $client.ExitCode -ne 0){throw 'Completed round client did not exit cleanly'}
                    Stop-Registered $client;$client=$null
                    if($round -eq 2){
                        $first=Api ('characters/'+$rounds[0].characterId+'/progression') 'Get' $player
                        if(($first|ConvertTo-Json -Depth 10 -Compress) -cne ($rounds[0].growth|ConvertTo-Json -Depth 10 -Compress)){throw 'Second training changed the first character growth'}
                        $smokePassed=$true;break
                    }
                    $round=2;$fixture=New-SmokeTraining $round
                    $clientLogPath=Join-Path $temp 'client-2.log'
                    $clientBaseArguments=@($clientBaseArguments|ForEach-Object {if($_.StartsWith('-abslog=')){'-abslog="'+$clientLogPath+'"'}else{$_}})
                    $characterId=$fixture.characterId;$sessionId=$fixture.sessionId
                    $smokeGates=Join-Path $runRoot 'Round2';$null=New-Item -ItemType Directory -Path $smokeGates
                    $args=Get-SmokeArguments;$nextPoll=[DateTime]::MinValue
                    $deadline=[DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
                }
            }
        }
        if($LobbyUISmoke){
            if([DateTime]::UtcNow -ge $deadline){throw 'Slate lobby UI smoke timed out'}
            if(Tick-TrainingLobbyUI){$result.lobbyUI=Complete-TrainingLobbyUI;break}
        }
        Start-Sleep -Milliseconds 200
    }
    if($SmokeTest -and !$SessionLoopSmoke){
        $growth=Api "characters/$characterId/progression" 'Get' $player
        if(!$smokePassed -or $growth.experience -ne 100 -or $growth.level -ne 2 -or $growth.totalSP -ne 3 -or !$growth.identityUnlocked){throw 'Training profile reward mismatch'}
        $result.progression=$growth;$result.state=$session.state
        Set-Content -LiteralPath (Join-Path $runRoot 'stop.ready') -Value 'stop' -Encoding ASCII
        if(!$client.WaitForExit(30000) -or $client.ExitCode -ne 0){throw 'Smoke client did not exit cleanly'}
        $result.clientExit=$client.ExitCode
        if($loaded.Count -ne 2){throw 'Smoke did not observe both private game modules'}
    }
    if($SessionLoopSmoke){
        if(!$smokePassed -or $rounds.Count -ne 2 -or $retired.Count -ne 2 -or $loaded.Count -ne 4 -or
            $rounds[0].characterId -ceq $rounds[1].characterId -or $retired[0].serverId -ceq $retired[1].serverId){throw 'Sequential training scope mismatch'}
        $result.rounds=$rounds.ToArray();$result.firstCharacterUnchanged=$true;$result.backendPreservedBetweenRounds=$true;$result.gamePort=$GamePort
    }
    if($LobbyUISmoke -and $loaded.Count -ne 2){throw 'Lobby UI smoke did not observe both private game modules'}
    foreach($entry in $inputs){if((Get-FileHash -LiteralPath $entry.snapshot).Hash -cne $entry.sha256){throw 'Private snapshot input changed'}}
    $result.passed=$true;$result.dungeonProfile=$DungeonProfile;$result.claimedTrainingServer=($retired.Count -gt 0 -or $null -ne $allocation);$result.snapshotInputs=$inputs.Count
}catch{$result.failure=$_.Exception.Message}
finally{
    if($LobbyUISmoke){Save-TrainingLobbyUIFailure}
    # Recover a launch interrupted between Start-Process and its public return.
    $statePath=Join-Path $temp ("Allocator/port-$GamePort.allocator.json")
    if(!$server -and (Test-Path -LiteralPath $statePath)){
        try{
            $state=Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
            $orphan=Get-Process -Id $state.processId -ErrorAction Stop
            if($orphan.Path -ieq $editor -and $orphan.StartTime.ToUniversalTime().Ticks -eq [long]$state.processStartTimeUtcTicks -and $state.editorProject -ieq (Join-Path $runtime 'Project_RPG.uproject')){$null=Register-Owned $orphan 'training-server-recovered'}
        }catch{}
    }
    foreach($entry in $processes){
        try{
            $p=$entry.process
            if(!$p.HasExited -and $p.Path -ieq $entry.path -and $p.StartTime.ToUniversalTime().Ticks -eq $entry.ticks){$p.Kill();if(!$p.WaitForExit(10000)){throw 'Owned process did not release its files'}}
            $p.Dispose()
        }catch{$result.passed=$false;$result.failure=([string]$result.failure+'; owned process cleanup failed').Trim(';',' ')}
    }
    foreach($pair in @(@('client',$clientLogPath),@('server',$(if($allocation){$allocation.LogPath}else{''})))){
        if($pair[1] -and (Test-Path -LiteralPath $pair[1])){
            Get-Content -LiteralPath $pair[1] | Where-Object {$_ -match 'RPG_TRAINING_PLAYTEST |RPG_DUNGEON_SERVER_READY |Fatal error:|Assertion failed:' -and $_ -notmatch '(?i)token|ticket|authorization|commandline|Browse:'} |
                Set-Content -LiteralPath (Join-Path $runRoot ($pair[0]+'.proof.log')) -Encoding UTF8
        }
    }
    try{
        $result.userCleanup=Remove-RPGTrainingGuideTemporaryTree -Path $users -OwnedParent $userParent
        if(Test-Path -LiteralPath $userParent){[IO.Directory]::Delete($userParent,$false)}
        $result.cleanup=Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $runRoot
    }catch{$result.passed=$false;$result.failure=([string]$result.failure+'; cleanup: '+$_.Exception.Message).Trim(';',' ')}
    if($ownershipRecord){
        try{
            $ownershipRecord.completed=($result.cleanup -and $result.Contains('userCleanup') -and $result.userCleanup.absent -and $result.cleanup.absent)
            Write-RPGTrainingLifecycle (Join-Path $runRoot 'Lifecycle.json') $ownershipRecord
        }catch{$result.passed=$false;$result.failure=([string]$result.failure+'; ownership journal finalization failed').Trim(';',' ')}
    }
    $result.historicalHashesOnly=$true;$result.manualUIVerified=$false;$result.backendStorage='Memory'
    $result.loadedModules=$loaded.ToArray();$result.retiredServers=$retired.ToArray();$result.serverStop='Terminal training servers stop only after observed client lobby return; remaining owned servers stop during launcher cleanup'
    $result.notVerified=@('physical lobby buttons/GPU','Steam','PostgreSQL','multiple players','process restart persistence')
    if($SessionLoopSmoke){
        $result.completionAndReturnTested=$smokePassed
        $result.sameClientCharacterSwitchTested=$false
        $result.testScope='Same Memory backend and UDP port; two separate native-probe client processes, each with actual four ability inputs and the existing result-button return'
    }else{$result.notVerified+='result/return UI'}
    $result | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $runRoot 'Result.json') -Encoding UTF8
}
Write-Output ('Evidence='+$runRoot)
if(!$result.passed){throw $result.failure}
Write-Output ('Training development run completed; temporary bytes removed='+([long]$result.cleanup.deletedBytes+[long]$result.userCleanup.deletedBytes))
