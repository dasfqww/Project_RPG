$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TrainingDevelopmentSessions.ps1')
$allocation=@{DungeonSessionId='session-a';ServerId='server-a';DungeonId='Progression.Training';Difficulty='Normal';MapPackage='/Game/Maps/Progression/L_ProgressionTraining'}
$session=@{dungeonSessionId='session-a';serverId='server-a';dungeonId='Progression.Training';difficulty='Normal';state='Cleared';members=@(@{characterId='hero-a'})}
$training='[time]LogWorld: Bringing World /Game/Maps/Progression/L_ProgressionTraining.L_ProgressionTraining up for play'
$lobby='[time]LogWorld: Bringing World /Game/Maps/LobbyMap.LobbyMap up for play'
$cases=0
function Expect([string]$Name,[string]$Expected,[string]$Log,[int]$Start=0){
    $actual=Get-RPGTrainingServerDisposition $allocation $session $Log $Start
    if($actual -cne $Expected){throw ($Name+': expected '+$Expected+', got '+$actual)}
    $script:cases++
}
Expect 'A confirmed terminal session and actual lobby return retire' Retire ($training+"`r`n"+$lobby)
Expect 'The result screen keeps its server' Keep $training
Expect 'The initial lobby is not a return' Keep $lobby
Expect 'A lobby before training is not a return' Keep ($lobby+"`n"+$training)
Expect 'A new travel after an old return is retained' Keep ($training+"`n"+$lobby+"`n"+$training)
Expect 'An old allocation log is excluded' Keep ($training+"`n"+$lobby) ($training.Length+1)
Expect 'A truncated client log is retained' Keep $training ($training.Length+1)
Expect 'A negative log boundary is retained' Keep $training -1
Expect 'PIE map prefixes normalize' Retire ($training.Replace('L_ProgressionTraining.','UEDPIE_2_L_ProgressionTraining.')+"`n"+$lobby.Replace('LobbyMap.','UEDPIE_2_LobbyMap.'))
foreach($state in @('Waiting','Loading','InProgress','SettlementPending','cleared','Unknown')){
    $session.state=$state;Expect ('A nonterminal or malformed state '+$state) Keep ($training+"`n"+$lobby)
}
foreach($state in @('Failed','Closed')){$session.state=$state;Expect $state Retire ($training+"`n"+$lobby)}
$session.state='Cleared'
foreach($field in @('dungeonSessionId','serverId','dungeonId','difficulty')){
    $before=$session[$field];$session[$field]='other';Expect ('Mismatched '+$field) Keep ($training+"`n"+$lobby);$session[$field]=$before
}
$session.members=@();Expect 'Missing membership cannot retire' Keep ($training+"`n"+$lobby)
$session.members=@(@{},@{});Expect 'A party cannot be stopped through the solo path' Keep ($training+"`n"+$lobby)
$session=$null;Expect 'A failed backend read cannot retire' Keep ($training+"`n"+$lobby)

# Exercise the maintained launcher's exact ownership/journal removal function.
$launcher=Join-Path $PSScriptRoot '../Start-TrainingDevelopment.ps1'
$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($launcher,[ref]$null,[ref]$parseErrors)
if($parseErrors){throw 'Launcher parse failed'}
$stop=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Stop-Registered'},$true)
if(!$stop){throw 'Stop-Registered missing'}
Invoke-Expression $stop.Extent.Text
$script:journalCalls=0
function Update-RPGTrainingLifecycle($RunRoot,$Record,$Processes){$script:journalCalls++;$script:remaining=@($Processes).Count}
$processes=[Collections.Generic.List[object]]::new();$runRoot='fixture-no-files';$ownershipRecord=@{}
$sleeper=$null
try{
    $sleeper=Start-Process (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoProfile','-Command','Start-Sleep -Seconds 90') -WindowStyle Hidden -PassThru
    $null=$sleeper.Handle;$path=$sleeper.Path;$ticks=$sleeper.StartTime.ToUniversalTime().Ticks
    $rejected=$false;try{Stop-Registered $sleeper}catch{$rejected=$_.Exception.Message -match 'registration'}
    if(!$rejected -or $sleeper.HasExited){throw 'An unregistered process was not preserved'};$cases++
    $entry=[pscustomobject]@{process=$sleeper;path=$path;ticks=($ticks+1)};$processes.Add($entry)
    $rejected=$false;try{Stop-Registered $sleeper}catch{$rejected=$_.Exception.Message -match 'identity changed'}
    if(!$rejected -or $sleeper.HasExited -or $journalCalls){throw 'A changed process identity was not preserved'};$cases++
    $entry.ticks=$ticks;Stop-Registered $sleeper;$sleeper=$null
    if($processes.Count -or $journalCalls -ne 1 -or $remaining -ne 0){throw 'Released child remained in the ownership journal'};$cases++
}finally{
    if($sleeper){if(!$sleeper.HasExited -and $sleeper.Path -ieq $path -and $sleeper.StartTime.ToUniversalTime().Ticks -eq $ticks){$sleeper.Kill();$null=$sleeper.WaitForExit(10000)};$sleeper.Dispose()}
}
$rejected=$false;try{& $launcher -SessionLoopSmoke -LobbyUISmoke}catch{$rejected=$_.Exception.Message -match 'separate smoke mode'}
if(!$rejected){throw 'Conflicting session-loop mode was not rejected before startup'};$cases++
Write-Output ('PASS '+$cases+' retirement/ownership/preflight cases; fixture child released; no UE, backend or files created.')
