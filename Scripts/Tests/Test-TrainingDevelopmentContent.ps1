$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TrainingDevelopmentSessions.ps1')
$cases=0
function Check([bool]$Value,[string]$Message){if(!$Value){throw $Message};$script:cases++}
$training='/Game/Maps/Progression/L_ProgressionTraining'
$intro='/Game/Maps/Progression/L_ProgressionIntro1'
$train=Get-RPGTrainingDevelopmentContent
$both=Get-RPGTrainingDevelopmentContent Intro1
Check ($train.Count -eq 1 -and $train['Progression.Training'] -ceq $training) 'Default training plan changed'
Check ($both.Count -eq 2 -and $both['Progression.Training'] -ceq $training -and $both['Progression.Intro1'] -ceq $intro) 'Intro1 plan must contain exactly both maps'
foreach($bad in @('intro1','Intro1 ','../Intro1','Training.json','')){
    $rejected=$false;try{$null=Get-RPGTrainingDevelopmentContent $bad}catch{$rejected=$true}
    Check $rejected ('Unknown plan accepted: '+$bad)
}
$allocation=@{DungeonSessionId='one';ServerId='owned';DungeonId='Progression.Intro1';Difficulty='Normal';MapPackage=$intro}
$session=@{dungeonSessionId='one';serverId='owned';dungeonId='Progression.Intro1';difficulty='Normal';state='Cleared';members=@(@{characterId='hero'})}
function World([string]$Package){return ('[time]LogWorld: Bringing World '+$Package+'.Map up for play')}
$entered=World $intro;$lobby=World '/Game/Maps/LobbyMap';$return=$entered+"`n"+$lobby
Check ((Get-RPGTrainingServerDisposition $allocation $session $return 0 Intro1) -ceq 'Retire') 'Intro1 terminal+actual return did not retire'
Check ((Get-RPGTrainingServerDisposition $allocation $session $return) -ceq 'Keep') 'Training-only plan accepted Intro1'
foreach($log in @($entered,$lobby,($lobby+"`n"+$entered),($return+"`n"+$entered),((World $training)+"`n"+$lobby),
    ($entered+"`n"+(World '/Game/Maps/Other')+"`n"+$lobby),((World '/Game/Other/L_ProgressionIntro1')+"`n"+$lobby))){
    Check ((Get-RPGTrainingServerDisposition $allocation $session $log 0 Intro1) -ceq 'Keep') 'Unrelated/stale map sequence retired Intro1'
}
Check ((Get-RPGTrainingServerDisposition $allocation $session $return ($entered.Length+1) Intro1) -ceq 'Keep') 'Old allocation log retired Intro1'
Check ((Get-RPGTrainingServerDisposition $allocation $session ($return.Replace('/L_ProgressionIntro1.','/UEDPIE_7_L_ProgressionIntro1.').Replace('/LobbyMap.','/UEDPIE_7_LobbyMap.')) 0 Intro1) -ceq 'Retire') 'PIE return was not recognized'
Check ((Get-RPGTrainingServerDisposition $allocation $session ((World '/Game/UEDPIE_7_Maps/Progression/L_ProgressionIntro1')+"`n"+$lobby) 0 Intro1) -ceq 'Keep') 'A PIE-like directory was aliased to the expected dungeon'
Check ((Get-RPGTrainingServerDisposition $allocation $session ($entered+"`n"+(World '/Game/UEDPIE_7_Maps/LobbyMap')) 0 Intro1) -ceq 'Keep') 'A PIE-like directory was aliased to the lobby'
foreach($field in @('DungeonId','Difficulty','MapPackage')){
    $before=$allocation[$field];$allocation[$field]='other'
    Check ((Get-RPGTrainingServerDisposition $allocation $session $return 0 Intro1) -ceq 'Keep') ('Mismatched allocation '+$field+' retired')
    $rejected=$false;try{Assert-RPGTrainingDevelopmentAllocation $allocation Intro1}catch{$rejected=$true}
    Check $rejected ('Mismatched allocation '+$field+' accepted')
    $allocation[$field]=$before
}
foreach($field in @('dungeonSessionId','serverId','dungeonId','difficulty')){
    $before=$session[$field];$session[$field]='other'
    Check ((Get-RPGTrainingServerDisposition $allocation $session $return 0 Intro1) -ceq 'Keep') ('Mismatched backend '+$field+' retired')
    $session[$field]=$before
}
$launcher=Join-Path $PSScriptRoot '../Start-TrainingDevelopment.ps1'
foreach($mode in @('SmokeTest','SessionLoopSmoke','LobbyUISmoke')){
    $modeArguments=@{$mode=$true}
    $rejected=$false;try{& $launcher -DungeonProfile Intro1 @modeArguments}catch{$rejected=$_.Exception.Message -match 'interactive mode'}
    Check $rejected ('Intro1 accepted the Training-only smoke driver '+$mode)
}
# Exercise the maintained launcher's real dispatch function against an owned
# argument recorder. In particular Intro1 must not pin every claim to Training.
$workspace=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixtureRoot=Join-Path $workspace ('Saved/BuildChecks/ProgressionIntro1Entry-20261102/ScriptTests/'+[Guid]::NewGuid().ToString('N'))
$temp=Join-Path $fixtureRoot 'Temp'
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
try{
    $null=New-Item -ItemType Directory -Path (Join-Path $temp 'Tools') -Force
    @'
param($ServerExecutable,$EditorProject,[switch]$DevelopmentIdentityTesting,$RuntimeUserDirectory,$BackendUrl,$AdminToken,$PublicHost,$Port,$Map,$LogDirectory)
[pscustomobject]@{mapSpecified=$PSBoundParameters.ContainsKey('Map');map=$Map}
'@ | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $temp 'Tools/Start-LocalDungeonServer.ps1')
    $parseErrors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($launcher,[ref]$null,[ref]$parseErrors)
    Check (!$parseErrors) 'Launcher parse failed'
    $poll=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Poll-Allocator'},$true)
    if(!$poll){throw 'Maintained allocator dispatch missing'}
    Invoke-Expression $poll.Extent.Text
    $editor='fixture.exe';$runtime=Join-Path $temp 'Runtime';$users=Join-Path $temp 'U'
    $url='http://127.0.0.1:1';$admin='fixture-only';$GamePort=18905;$map=$training
    $DungeonProfile='Training';$record=Poll-Allocator
    Check ($record.mapSpecified -and $record.map -ceq $training) 'Training map assertion changed'
    $DungeonProfile='Intro1';$record=Poll-Allocator
    Check (!$record.mapSpecified) 'Intro1 dispatch incorrectly pinned its map to Training'
}finally{
    $cleanup=Remove-GuardianProbeArtifact $temp $fixtureRoot
    [pscustomobject]@{cases=$cases;cleanup=$cleanup;tempAbsent=!(Test-Path -LiteralPath $temp)}|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 -LiteralPath (Join-Path $fixtureRoot 'Result.json')
}
Write-Output ('PASS '+$cases+' development content/Intro1 dispatch-retirement/preflight cases; no UE/backend; own fixture removed='+$cleanup.deletedBytes+' bytes.')
