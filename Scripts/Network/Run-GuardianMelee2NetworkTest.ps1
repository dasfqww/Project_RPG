[CmdletBinding()]
param(
 [string]$RuntimeProject,
 [ValidateRange(1024,65535)][int]$Port=19106,
 [ValidateRange(30,300)][int]$TimeoutSeconds=180,
 [string]$AuditRun,
 [string]$AuditResultRun,
 [string]$OutputRoot,
 [switch]$MovingPlatform
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'GuardianMelee2Artifacts.ps1')
. (Join-Path $PSScriptRoot 'GuardianMelee2ResultContract.ps1')
function Test-MotionContract([string]$Directory){
 $contract=@{passed=$false;maximumRiseErrorCm=35}
 try{
  $rises=@{}
  foreach($role in @('server','owner','observer')){
   $result=Get-Content -Raw -Encoding UTF8 (Join-Path $Directory ($role+'.result.json'))|ConvertFrom-Json
   if($result -is [array] -or !$result -or $result.success -isnot [bool] -or !$result.success){throw "Missing or unsuccessful $role motion result"}
   $value=$result.rise
   $numeric=$value -is [int] -or $value -is [long] -or $value -is [double] -or $value -is [float] -or $value -is [decimal]
   # Complete() already requires at least 50cm of actual ascent. A missing
   # value must not become zero through PowerShell's arithmetic coercion.
   if(!$numeric -or [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value) -or [double]$value -lt 50){throw "Invalid $role rise: expected a finite JSON number of at least 50cm"}
   $rises[$role]=[double]$value
  }
  $ownerError=[Math]::Abs($rises.owner-$rises.server)
  $observerError=[Math]::Abs($rises.observer-$rises.server)
  # Preserve the existing visible-path divergence bound.
  $contract=@{passed=($ownerError -le 35 -and $observerError -le 35);maximumRiseErrorCm=35;serverRiseCm=$rises.server;ownerRiseCm=$rises.owner;observerRiseCm=$rises.observer;ownerRiseErrorCm=$ownerError;observerRiseErrorCm=$observerError}
  if(!$contract.passed){throw "Airborne path diverged: owner error $ownerError cm, observer error $observerError cm (limit 35 cm); inspect $Directory"}
 }catch{
  $contract.passed=$false;$contract.error=$_.Exception.Message
  $contract|ConvertTo-Json|Set-Content -Encoding UTF8 (Join-Path $Directory 'MotionContract.proof.json')
  throw
 }
 $contract|ConvertTo-Json|Set-Content -Encoding UTF8 (Join-Path $Directory 'MotionContract.proof.json')
}
if($AuditRun -and $AuditResultRun){throw 'Choose one audit mode.'}
if($AuditRun){Test-MotionContract ([IO.Path]::GetFullPath($AuditRun));return}
if($AuditResultRun){$null=Test-GuardianMelee2Results -Directory ([IO.Path]::GetFullPath($AuditResultRun)) -MovingPlatform:$MovingPlatform;return}
if(!$RuntimeProject){throw 'Specify -RuntimeProject with a coordinated, built project directory; archived verification modules are discarded after use.'}
$RuntimeProject=[IO.Path]::GetFullPath($RuntimeProject)
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Saved/CombatGuardianMelee2Network'))
if($OutputRoot){$taskRoot=[IO.Path]::GetFullPath($OutputRoot)}
$run=Join-Path $taskRoot ([Guid]::NewGuid().ToString('N'))
$module=Join-Path $RuntimeProject 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
if(!(Test-Path -LiteralPath $module)){throw 'Build the authored-attack probe in the selected project first.'}
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw "UDP $Port is already in use"}
$moduleHash=(Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
New-Item -ItemType Directory -Path $run|Out-Null
@{run=$run;runtimeProject=$RuntimeProject;module=$module;moduleSHA256=$moduleHash;port=$Port;timeoutSeconds=$TimeoutSeconds;movingPlatform=[bool]$MovingPlatform;temporaryPaths=@('server-User','owner-User','observer-User');runtimeProjectOwnedByCaller=$true}|ConvertTo-Json -Depth 3|Set-Content -Encoding UTF8 (Join-Path $run 'Invocation.json')
$processes=[Collections.Generic.List[Diagnostics.Process]]::new()
$logs=[Collections.Generic.List[string]]::new()
$processRoles=@{};$loadedModules=@{}
$savedEnvironment=@{}
foreach($name in @('UE_SKIP_UBT_SDK_SETUP','PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')){$savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
function Start-Probe([string]$Role,[string]$Destination,[string]$Mode,[string]$Commands){
 $log=Join-Path $run ($Role+'.log')
 $arguments=@(('"'+(Join-Path $RuntimeProject 'Project_RPG.uproject')+'"'),$Destination,$Mode,
  '-RPGGuardianMelee2Network',('-RPGGuardianMelee2Directory="'+$run+'"'),'-RPGNetTestMode','-RPGRequireIris','-UseIrisReplication=1',
  '-unattended','-nop4','-nosplash','-nosound','-nosteam','-NoLoadingScreen','-DisablePython','-NoLiveCoding','-nullrhi','-DDC=(EnginePak,Local)',
  '-PktLagMin=80','-PktLagMax=160','-PktLoss=5',('-UserDir="'+(Join-Path $run ($Role+'-User'))+'"'),('-abslog="'+$log+'"'),('-ExecCmds="'+$Commands+'"'),
  '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
  '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null')
 if($Role -eq 'owner'){$arguments+='-RPGGuardianMelee2Owner'}
 if($MovingPlatform){$arguments+='-RPGGuardianMelee2MovingPlatform'}
 @{role=$Role;arguments=$arguments}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $run ($Role+'.command.json')) -Encoding UTF8
 $process=Start-Process 'D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList $arguments -WindowStyle Hidden -PassThru
 $processRoles[$process.Id]=$Role
 $processes.Add($process);$logs.Add($log)
 Write-Host "Started $Role PID=$($process.Id) Run=$run"
 return $log
}
function Assert-Healthy{
 foreach($process in $processes){if($process.HasExited){throw "Probe exited early; inspect $run"}}
 foreach($process in $processes){
  $role=$processRoles[$process.Id]
  if(!$loadedModules.ContainsKey($role)){
   $candidate=@();try{$candidate=@((Get-Process -Id $process.Id).Modules|Where-Object {$_.ModuleName -eq 'UnrealEditor-Project_RPG.dll'})}catch{}
   if($candidate.Count -eq 1){
    $actual=$candidate[0].FileName;$hash=(Get-FileHash -LiteralPath $actual -Algorithm SHA256).Hash
    if($actual -ne $module -or $hash -ne $moduleHash){throw "Unexpected actual module for $role"}
    $loadedModules[$role]=@{pid=$process.Id;path=$actual;sha256=$hash}
    $loadedModules|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $run 'LoadedModules.proof.json') -Encoding UTF8
   }
  }
 }
 foreach($log in $logs){
  if((Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Quiet -Pattern 'GUARDIAN_MELEE2_NETWORK_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:|Log(?:Iris\w*|Net\w*): Error|Blueprint Runtime Error|LogScript: Error')){throw "Probe failed; inspect $log"}
 }
}
function Wait-Marker([string]$Log,[string]$Marker){
 $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
 while([DateTime]::UtcNow -lt $deadline){
  Assert-Healthy
  if((Test-Path -LiteralPath $Log) -and (Select-String -LiteralPath $Log -SimpleMatch $Marker -Quiet)){return}
  Start-Sleep -Milliseconds 200
 }
 throw "Missing $Marker; inspect $Log"
}
try{
 $env:UE_SKIP_UBT_SDK_SETUP='1'
 foreach($name in @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
 $server=Start-Probe 'server' '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' ('-server -port='+$Port) 't.MaxFPS 30'
 Wait-Marker $server 'GUARDIAN_MELEE2_NETWORK_SERVER_READY'
 $owner=Start-Probe 'owner' '/Engine/Maps/Entry' '-game' ('t.MaxFPS 30,open 127.0.0.1:'+$Port)
 Wait-Marker $server 'GUARDIAN_MELEE2_NETWORK_FIRST_PLAYER_READY'
 $observer=Start-Probe 'observer' '/Engine/Maps/Entry' '-game' ('t.MaxFPS 30,open 127.0.0.1:'+$Port)
 foreach($log in $logs){Wait-Marker $log 'GUARDIAN_MELEE2_NETWORK_COMPLETE'}
 $results=Test-GuardianMelee2Results -Directory $run -MovingPlatform:$MovingPlatform
 Set-Content -LiteralPath (Join-Path $run 'exit') -Value 'all observations accepted' -Encoding ASCII
 $exitDeadline=[DateTime]::UtcNow.AddSeconds(30)
 while(@($processes|Where-Object{!$_.HasExited}).Count -gt 0 -and [DateTime]::UtcNow -lt $exitDeadline){Start-Sleep -Milliseconds 200}
 foreach($process in $processes){if(!$process.HasExited){throw 'Owned probe did not exit'};$process.WaitForExit();if($process.ExitCode -ne 0){throw 'Owned probe exit was nonzero'}}
 if((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne $moduleHash){throw 'Isolated DLL changed during verification'}
 if($loadedModules.Count -ne 3){throw 'Actual module was not observed in all three probes'}
 Test-MotionContract $run
 @{passed=$true;run=$run;module=$module;moduleSHA256=$moduleHash;port=$Port;processExitCodes=@($processes|ForEach-Object{$_.ExitCode});source='original Guardian Melee2 montage/weapon';server=$results.server;owner=$results.owner;observer=$results.observer}|ConvertTo-Json -Depth 9|Set-Content -Encoding UTF8 (Join-Path $run 'Network.proof.json')
 Write-Output "PASS Guardian Melee2 original attack / BP_Player / owner+observer / Iris lag80-160 loss5: $run"
}catch{
 @{passed=$false;error=$_.Exception.Message;run=$run}|ConvertTo-Json|Set-Content -Encoding UTF8 (Join-Path $run 'Failure.json')
 throw
}finally{
 foreach($entry in $savedEnvironment.GetEnumerator()){[Environment]::SetEnvironmentVariable($entry.Key,$entry.Value,'Process')}
 $cleanup=@{passed=$false;deletedBytes=0L;deletedFiles=0;unlinkedEntries=0;errors=@();runtimeProjectDeleted=$false}
 foreach($process in $processes){
  try{
   if(!$process.HasExited){
    $owned=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if(!$owned -or !$owned.CommandLine -or !$owned.CommandLine.Contains((Split-Path $run -Leaf)) -or !$owned.CommandLine.Contains('-RPGGuardianMelee2Network')){
     throw "Cannot confirm ownership of probe PID $($process.Id); temporary files retained."
    }
    Stop-Process -Id $process.Id -ErrorAction Stop
   }
   if(!$process.WaitForExit(10000)){throw "Probe PID $($process.Id) did not release its files."}
  }catch{$cleanup.errors+= $_.Exception.Message}
 }
 if(!$cleanup.errors.Count){
  foreach($role in @('server','owner','observer')){
   try{
    $deleted=Remove-GuardianProbeArtifact (Join-Path $run ($role+'-User')) $run
    $cleanup.deletedBytes+=$deleted.deletedBytes;$cleanup.deletedFiles+=$deleted.deletedFiles;$cleanup.unlinkedEntries+=$deleted.unlinkedEntries
   }catch{$cleanup.errors+= $_.Exception.Message}
  }
 }
 $cleanup.passed=($cleanup.errors.Count -eq 0)
 $cleanup|ConvertTo-Json -Depth 3|Set-Content -Encoding UTF8 (Join-Path $run 'Cleanup.proof.json')
 if(!$cleanup.passed){throw ('Probe cleanup incomplete: '+($cleanup.errors -join '; '))}
}
