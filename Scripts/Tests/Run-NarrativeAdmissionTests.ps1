[CmdletBinding()]
param([switch]$Postgres, [string]$BaseCommit='HEAD')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId=[Guid]::NewGuid().ToString('N')
$run=Join-Path $workspace ('Saved/BuildChecks/NarrativeAdmission-20261105/'+$runId)
$temp=Join-Path $run 'Temp';$inputs=Join-Path $temp 'Repo';$artifacts=Join-Path $temp 'artifacts'
$task='narrative-admission-20261105';$containerName='rpg-narrative-admission-'+$runId
$containerId=$null;$hostProcess=$null;$http=$null;$postgresConnection=$null
$result=[ordered]@{Passed=$false;BaseCommit=$null;Postgres=[bool]$Postgres;Narrative=$false;Recovery=$false;HostHttp=$false;InvalidStartup=$false;InputsUnchanged=$false;Inputs=@();Cleanup=$null;TempDeleted=$false;ContainerDeleted=$false;Failure=$null}
$overlay=@(
 'Backend/ProjectRpg.Backend/Configuration/DungeonContentOptions.cs',
 'Backend/ProjectRpg.Backend/Application/DungeonContentCatalog.cs',
 'Backend/ProjectRpg.Backend/Narrative/DungeonStoryAdmission.cs',
 'Backend/ProjectRpg.Backend/Narrative/NarrativeCoordinator.cs',
 'Backend/ProjectRpg.Backend/Program.cs',
 'Backend/ProjectRpg.Backend/Api/JoinTicketApiEndpoints.cs',
 'Backend/ProjectRpg.Backend/Api/DungeonAdmissionApiEndpoints.cs',
 'Backend/ProjectRpg.Backend.NarrativeTests/Program.cs',
 'Backend/ProjectRpg.Backend.NarrativeTests/NarrativeStoryAdmissionTests.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/Program.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/ConnectionExpirationTests.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/JoinTicketConsumptionSealTests.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/JoinTicketConsumptionEvidenceTests.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/JoinTicketConsumptionTests.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/UnrealConsumptionRecoveryTests.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/JoinTicketIssuanceTests.cs',
 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/StoryAdmissionTestServices.cs'
)
$null=New-Item -ItemType Directory -Path $inputs -Force
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Reservation {
 if(@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match '^\|[^|]*narrative-admission-20261105'}).Count -ne 1){throw 'Own reservation missing'}
}
function Start-Child([string]$Arguments) {
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=(Get-Command dotnet).Source
 $start.Arguments=$Arguments;$start.WorkingDirectory=Join-Path $inputs 'Backend/ProjectRpg.Backend'
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden'
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 foreach($key in @('POSTGRES_CONNECTION_STRING','PROJECT_RPG_BACKEND_ADMIN_TOKEN','STEAM_WEB_API_PUBLISHER_KEY','STEAM_APP_ID','STEAM_TICKET_IDENTITY','PROJECT_RPG_NARRATIVE_TEST_POSTGRES','PROJECT_RPG_NARRATIVE_TEST_RUN','PROJECT_RPG_ABANDON_TEST_POSTGRES')){$start.EnvironmentVariables.Remove($key)}
 foreach($key in @($start.EnvironmentVariables.Keys)){if($key -match '^(DungeonContent__|Narrative__|Operations__)'){$start.EnvironmentVariables.Remove($key)}}
 $values=@{
  ASPNETCORE_ENVIRONMENT='Development';DOTNET_ENVIRONMENT='Development';Storage__Provider='Memory';Auth__AdminToken=$admin
  Steam__AllowDevelopmentAuthentication='true';Narrative__EnableDevelopmentContent='false';Narrative__CatalogPath=$catalogPath
  Kestrel__Endpoints__Http__Url=$url;DungeonSessions__MinimumDeparturePartySize='2';DungeonSessions__MaxPartySize='4'
  DungeonContent__Entries__0__DungeonId='Test.Story';DungeonContent__Entries__0__MapPackage='/Game/Test'
  DungeonContent__Entries__0__RequiredStoryUnlocks__0=$requiredUnlock
  DungeonContent__Entries__1__DungeonId='Progression.Intro1';DungeonContent__Entries__1__MapPackage='/Game/Test'
  DungeonContent__Entries__1__Difficulties__0='Normal';DungeonContent__Entries__1__RequiredStoryUnlocks__0='FILE.Story.Next'
 }
 foreach($key in $values.Keys){$start.EnvironmentVariables[$key]=$values[$key]}
 if($postgresConnection){$start.EnvironmentVariables['PROJECT_RPG_NARRATIVE_TEST_POSTGRES']=$postgresConnection;$start.EnvironmentVariables['PROJECT_RPG_NARRATIVE_TEST_RUN']=$runId}
 $child=[Diagnostics.Process]::new();$child.StartInfo=$start
 if(!$child.Start()){throw 'Own dotnet child did not start'}
 return $child
}
function Run-Child([string]$Arguments,[string]$Log,[switch]$ExpectFailure){
 $child=Start-Child $Arguments
 try{
  $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
  if(!$child.WaitForExit(180000)){$child.Kill();$child.WaitForExit();throw ('Own child timed out: '+$Log)}
  $output=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText((Join-Path $run $Log),$output)
  if(($child.ExitCode -ne 0) -ne [bool]$ExpectFailure){throw ('Unexpected child exit; inspect '+$Log)}
  return $output
 }finally{if(!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
function Stop-Host {
 if(!$script:hostProcess){return}
 if(!$script:hostProcess.HasExited){$script:hostProcess.Kill();$script:hostProcess.WaitForExit()}
 [IO.File]::WriteAllText((Join-Path $run 'Host.log'),$script:hostOut.GetAwaiter().GetResult()+$script:hostErr.GetAwaiter().GetResult())
 $script:hostProcess.Dispose();$script:hostProcess=$null
}
function Owned-Container {
 $found=@(& docker container ls --all --filter ('name=^/'+$containerName+'$') --format '{{.ID}}')
 if($LASTEXITCODE -ne 0){throw 'Cannot inspect own container'}
 if($found.Count -eq 0){return $null}
 if($found.Count -ne 1){throw 'Ambiguous container identity'}
 $info=((& docker inspect $found[0]) | ConvertFrom-Json)[0]
 if($LASTEXITCODE -ne 0 -or $info.Name -cne ('/'+$containerName) -or $info.Config.Labels.'project-rpg.run' -cne $runId -or $info.Config.Labels.'project-rpg.task' -cne $task -or ($containerId -and $info.Id -cne $containerId)){throw 'Container ownership mismatch'}
 return $info
}
function Api([string]$Method,[string]$Path,$Body=$null,[string]$Token='',[int]$Expected=200){
 $request=[Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::new($Method),$url+'/api'+$Path)
 try{
  if($Token){$request.Headers.Authorization=[Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer',$Token)}
  if($null -ne $Body){$request.Content=[Net.Http.StringContent]::new(($Body|ConvertTo-Json -Depth 8 -Compress),[Text.Encoding]::UTF8,'application/json')}
  $response=$http.SendAsync($request).GetAwaiter().GetResult()
  try{
   if([int]$response.StatusCode -ne $Expected){throw ('Unexpected HTTP '+[int]$response.StatusCode+' for '+$Method+' '+$Path+'; expected '+$Expected)}
   $bodyText=$response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
   if($bodyText){return ($bodyText|ConvertFrom-Json)}
  }finally{$response.Dispose()}
 }finally{$request.Dispose()}
}
function Complete-Story($Character,[string]$Token){
 $nodes=@('Contact.Offer','Contact.Explain','Instructor.Explain','Contact.Report');$choices=@('accept','confirm','confirm','report')
 for($i=0;$i -lt 4;$i++){$null=Api POST ('/characters/'+$Character.characterId+'/narrative/choices') @{requestId=[Guid]::NewGuid().ToString();expectedRevision=([string]$i);catalogVersion=7;dialogueId=('FILE.'+$nodes[$i]);choiceId=$choices[$i]} $Token}
}
try{
 Reservation
 $commit=(& git -C $workspace rev-parse ($BaseCommit+'^{commit}')).Trim()
 if($LASTEXITCODE -ne 0 -or $commit -notmatch '^[a-f0-9]{40}$'){throw 'Invalid base commit'}
 $result.BaseCommit=$commit
 $archive=Join-Path $temp 'Backend.zip'; & git -C $workspace archive --format=zip ('--output='+$archive) $commit Backend
 if($LASTEXITCODE -ne 0){throw 'Backend snapshot archive failed'}
 Expand-Archive -LiteralPath $archive -DestinationPath $inputs
 foreach($relative in $overlay){
  $source=Join-Path $workspace $relative;$before=(Get-FileHash -LiteralPath $source).Hash
  if((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked input refused'}
  $target=Join-Path $inputs $relative;$null=New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
  Copy-Item -LiteralPath $source -Destination $target
  if((Get-FileHash -LiteralPath $source).Hash -cne $before -or (Get-FileHash -LiteralPath $target).Hash -cne $before){throw 'Mixed input snapshot'}
  $result.Inputs+=@{Path=$relative;SHA256=$before}
 }
 $admin=[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N');$requiredUnlock='FILE.Story.Next'
 $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$url='http://127.0.0.1:'+$listener.LocalEndpoint.Port;$listener.Stop()
 $catalogPath=Join-Path $temp 'authored.json'
 $authored=([IO.File]::ReadAllText((Join-Path $inputs 'Backend/ProjectRpg.Backend/Narrative/Content/development.json'))).Replace('DEV.','FILE.').Replace('"catalogVersion": 1','"catalogVersion": 7')
 [IO.File]::WriteAllText($catalogPath,$authored,[Text.UTF8Encoding]::new($false))
 if($Postgres){
  & docker image inspect postgres:17-alpine --format '{{.Id}}' *> $null
  if($LASTEXITCODE -ne 0){throw 'Cached postgres:17-alpine required'}
  $password=[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N')
  $created=& docker run --pull=never --detach --name $containerName --label ('project-rpg.run='+$runId) --label ('project-rpg.task='+$task) --memory 512m --cpus 1 --pids-limit 128 --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env 'POSTGRES_DB=narrative_test' --env 'POSTGRES_USER=narrative_test' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine -c shared_buffers=16MB -c max_connections=32
  if($LASTEXITCODE -ne 0){throw 'Owned PostgreSQL startup failed'}
  $containerId=([string]$created).Trim();$info=Owned-Container
  if(@($info.Mounts|Where-Object Type -ne 'tmpfs').Count -ne 0){throw 'Unexpected persistent mounts'}
  $ready=$false;for($i=0;$i -lt 40;$i++){& docker exec $containerId pg_isready -U narrative_test -d narrative_test *> $null;if($LASTEXITCODE -eq 0){$ready=$true;break};Start-Sleep -Milliseconds 250}
  if(!$ready){throw 'Own PostgreSQL readiness timeout'}
  $binding=[string](& docker port $containerId 5432/tcp)
  if($LASTEXITCODE -ne 0 -or $binding.Trim() -notmatch '^127\.0\.0\.1:(\d+)$'){throw 'Unexpected DB binding'}
  $postgresConnection='Host=127.0.0.1;Port='+$Matches[1]+';Database=narrative_test;Username=narrative_test;Password='+$password
 }
 $null=Run-Child ('build "'+(Join-Path $inputs 'Backend/ProjectRpg.Backend.NarrativeTests/ProjectRpg.Backend.NarrativeTests.csproj')+'" --artifacts-path "'+$artifacts+'" -v minimal') 'Build.log'
 $testDll=Join-Path $artifacts 'bin/ProjectRpg.Backend.NarrativeTests/debug/ProjectRpg.Backend.NarrativeTests.dll'
 $arguments='"'+$testDll+'"';if($Postgres){$arguments+=' --postgres'}
 $tests=Run-Child $arguments 'Narrative.log'
 if(!$tests.Contains('PASS Narrative story admission Memory:') -or ($Postgres -and !$tests.Contains('PASS Narrative story admission PostgreSQL:')) -or !$tests.Contains('PASS Narrative: all requested groups completed.')){throw 'Required narrative groups missing'}
 $result.Narrative=$true;Write-Output 'PASS story requirements, Memory/HTTP and requested PostgreSQL'
 $null=Run-Child ('build "'+(Join-Path $inputs 'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/ProjectRpg.Backend.DungeonAbandonmentTests.csproj')+'" --artifacts-path "'+$artifacts+'" -v minimal') 'RecoveryBuild.log'
 $recoveryDll=Join-Path $artifacts 'bin/ProjectRpg.Backend.DungeonAbandonmentTests/debug/ProjectRpg.Backend.DungeonAbandonmentTests.dll'
 foreach($group in @('join-ticket-issuance','join-ticket-consumption','consumption-evidence','consumption-seal','disconnection-observation')){$null=Run-Child ('"'+$recoveryDll+'" --'+$group+'-only') ($group+'.log')}
 $result.Recovery=$true;Write-Output 'PASS existing ticket, consumption, receipt recovery and admission HTTP regression'
 $dll=Join-Path $artifacts 'bin/ProjectRpg.Backend/debug/ProjectRpg.Backend.dll'
 $hostProcess=Start-Child ('"'+$dll+'"');$hostOut=$hostProcess.StandardOutput.ReadToEndAsync();$hostErr=$hostProcess.StandardError.ReadToEndAsync()
 $ready=$false;for($i=0;$i -lt 80;$i++){if($hostProcess.HasExited){throw 'Own host exited'};try{$null=Invoke-RestMethod -Uri ($url+'/health/ready') -TimeoutSec 2;$ready=$true;break}catch{};Start-Sleep -Milliseconds 250}
 if(!$ready){throw 'Own host readiness timeout'}
 Add-Type -AssemblyName System.Net.Http;$http=[Net.Http.HttpClient]::new();$http.Timeout=[TimeSpan]::FromSeconds(15)
 $login=Api POST '/auth/steam-ticket' @{ticket='dev:76561198000000001'};$playerA=$login.accessToken;$login=$null
 $login=Api POST '/auth/steam-ticket' @{ticket='dev:76561198000000002'};$playerB=$login.accessToken;$login=$null
 $a=Api POST '/characters' @{name=('Leader'+$runId.Substring(0,8))} $playerA 201
 $alt=Api POST '/characters' @{name=('Alt'+$runId.Substring(0,8))} $playerA 201
 $b=Api POST '/characters' @{name=('Member'+$runId.Substring(0,8))} $playerB 201
 $create=@{characterId=$a.characterId;dungeonId='Test.Story';difficulty='Normal'}
 $denied=Api POST '/dungeon-sessions' $create $playerA 409;if($denied.error -cne 'story_prerequisite_not_met'){throw 'Unstarted create did not fail story gate'}
 $growth=Api GET ('/characters/'+$a.characterId+'/progression') $null $playerA
 Complete-Story $a $playerA
 $denied=Api POST '/dungeon-sessions' @{characterId=$a.characterId;dungeonId='Progression.Intro1';difficulty='Normal'} $playerA 409
 if($denied.error -cne 'progression_prerequisite_not_met'){throw 'Story completion bypassed growth requirements'}
 $session=Api POST '/dungeon-sessions' $create $playerA 201
 foreach($case in @(@{character=$b;token=$playerB},@{character=$alt;token=$playerA})){
  $denied=Api POST ('/dungeon-sessions/'+$session.dungeonSessionId+'/members') @{characterId=$case.character.characterId} $case.token 409
  if($denied.error -cne 'story_prerequisite_not_met'){throw 'Leader/account qualification bypassed join gate'}
 }
 $unchanged=Api GET ('/dungeon-sessions/'+$session.dungeonSessionId) $null $playerA
 if($unchanged.members.Count -ne 1 -or $unchanged.lobbyRevision -ne $session.lobbyRevision){throw 'Denied join changed roster/revision'}
 Complete-Story $b $playerB
 $session=Api POST ('/dungeon-sessions/'+$session.dungeonSessionId+'/members') @{characterId=$b.characterId} $playerB
 $session=Api POST ('/dungeon-sessions/'+$session.dungeonSessionId+'/departure') @{characterId=$a.characterId;requestId=[Guid]::NewGuid().ToString();expectedLobbyRevision=[long]$session.lobbyRevision} $playerA
 $session=Api POST ('/dungeon-sessions/'+$session.dungeonSessionId+'/activate') @{serverId='story-host';serverAddress='127.0.0.1:17779'} $admin
 $serverToken=$session.gameServerAccessToken
 foreach($case in @(@{character=$a;token=$playerA;owner='76561198000000001'},@{character=$b;token=$playerB;owner='76561198000000002'})){
  $ticket=Api POST '/join-tickets' @{dungeonSessionId=$session.dungeonSessionId;characterId=$case.character.characterId} $case.token
  $consumed=Api POST '/join-tickets/consume' @{joinTicket=$ticket.joinTicket;serverId='story-host';consumptionRequestId=[Guid]::NewGuid().ToString()} $serverToken
  $confirmed=Api POST ('/dungeon-sessions/'+$session.dungeonSessionId+'/members/'+$case.character.characterId+'/admission-confirmation') @{steamId=$case.owner;connectionGeneration=$consumed.connectionGeneration} $serverToken
  if(!$confirmed.admissionConfirmed){throw 'Qualified party could not confirm admission'}
 }
 $after=Api GET ('/characters/'+$a.characterId+'/progression') $null $playerA
 if(($after|ConvertTo-Json -Depth 8 -Compress) -cne ($growth|ConvertTo-Json -Depth 8 -Compress)){throw 'Story/entry changed growth'}
 $result.HostHttp=$true;Write-Output 'PASS real host: story-locked create/join/alt, unchanged roster, growth gate, qualified party departure/ticket/confirmation'
 Stop-Host
 $requiredUnlock='FILE.Missing'
 $failure=Run-Child ('"'+$dll+'"') 'InvalidStartup.log' -ExpectFailure
 if(!$failure.Contains('RequiredStoryUnlocks references an undefined narrative unlock')){throw 'Undefined unlock not rejected at startup'}
 $result.InvalidStartup=$true
 foreach($entry in $result.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash -cne $entry.SHA256){throw 'Own source changed after snapshot'}}
 $result.InputsUnchanged=$true;$result.Passed=$true
}catch{$result.Failure=$_.Exception.Message;throw}
finally{
 try{Stop-Host;if($http){$http.Dispose()}}catch{$result.Failure=([string]$result.Failure+'; host cleanup: '+$_.Exception.Message)}
 try{if($Postgres){$owned=Owned-Container;if($owned){& docker rm --force $owned.Id *> $null;if($LASTEXITCODE -ne 0){throw 'Own DB removal failed'}};$result.ContainerDeleted=!(Owned-Container)}else{$result.ContainerDeleted=$true}}catch{$result.Failure=([string]$result.Failure+'; DB cleanup: '+$_.Exception.Message)}
 try{Reservation;$result.Cleanup=Remove-GuardianProbeArtifact -Path $temp -AllowedRoot $run;$result.TempDeleted=!(Test-Path -LiteralPath $temp)}catch{$result.Failure=([string]$result.Failure+'; file cleanup: '+$_.Exception.Message)}
 if($result.Failure -or !$result.TempDeleted -or !$result.ContainerDeleted){$result.Passed=$false}
 $result|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $run 'Result.json') -Encoding UTF8
 Write-Output ('Result: '+(Join-Path $run 'Result.json'))
 if(!$result.Passed){throw ('Story admission verification failed: '+$result.Failure)}
}
