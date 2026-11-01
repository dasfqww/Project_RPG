[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$networkRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Network'))
. (Join-Path $networkRoot 'GuardianMelee2Artifacts.ps1')
$evidenceRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Saved/Maintenance/CombatVerificationCleanup-20261030'))
$null=New-Item -ItemType Directory -Force -Path $evidenceRoot
$fixture=Join-Path $evidenceRoot ('Fixture-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $fixture
$checks=[Collections.Generic.List[string]]::new()
$fixtureState=@{mode='success';processes=@()}
function Assert-Check([bool]$Condition,[string]$Message){if(!$Condition){throw $Message};$checks.Add($Message)}
function Assert-Rejected([scriptblock]$Action,[string]$Message){$rejected=$false;try{& $Action}catch{$rejected=$true};Assert-Check $rejected $Message}

# Exercise the runner's real finally block with small, owned PowerShell children.
# Only logs/results are simulated; Unreal and UDP endpoints are not used.
function Get-NetUDPEndpoint {param($LocalPort,$ErrorAction)}
function Start-Process {
 param($FilePath,$ArgumentList,$WindowStyle,[switch]$PassThru)
 $arguments=$ArgumentList -join ' '
 $run=[regex]::Match($arguments,'-RPGGuardianMelee2Directory="([^"]+)"').Groups[1].Value
 if(!$run){throw 'Mock could not identify the owned run.'}
 $role=if($arguments.Contains('-server -port=')){'server'}elseif($arguments.Contains('-RPGGuardianMelee2Owner')){'owner'}else{'observer'}
 $user=Join-Path $run ($role+'-User')
 $null=New-Item -ItemType Directory -Path $user
 [IO.File]::WriteAllBytes((Join-Path $user 'cache.bin'),[byte[]]::new(1024))
 if($fixtureState.mode -eq 'failure' -and $role -eq 'owner'){throw 'Simulated owner startup failure'}
 @('GUARDIAN_MELEE2_NETWORK_SERVER_READY','GUARDIAN_MELEE2_NETWORK_FIRST_PLAYER_READY','GUARDIAN_MELEE2_NETWORK_COMPLETE')|Set-Content -LiteralPath (Join-Path $run ($role+'.log'))
 @{success=$true;iris=$true;phases=15;poses=14;syntheticDamage=$false;replacementGrant=$false;playerClass='/Game/Blueprints/Character/Player/BP_Player.BP_Player_C';lagMin=80;lagMax=160;loss=5;locallyControlledReceiver=($role -eq 'owner');initialHP=100;finalHP=90;rise=180;finalX=240;finalY=0;finalZ=98}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run ($role+'.result.json'))
 $exitLiteral=(Join-Path $run 'exit').Replace("'","''")
 $tag='-RPGGuardianMelee2Network '+(Split-Path $run -Leaf)
 $childCommand="& { param([string]`$ProbeTag) while(-not [IO.File]::Exists('$exitLiteral')) { Start-Sleep -Milliseconds 100 } } '$tag'"
 $process=Microsoft.PowerShell.Management\Start-Process (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoProfile','-NonInteractive','-Command',('"'+$childCommand+'"')) -WindowStyle Hidden -PassThru
 $fixtureState.processes+= $process
 return $process
}

$passed=$false
try{
 $outside=Join-Path $fixture 'Outside'
 $owned=Join-Path $fixture 'Owned'
 $nested=Join-Path $owned 'Nested'
 $null=New-Item -ItemType Directory -Path $outside,$nested
 $sentinel=Join-Path $outside 'keep.txt'
 [IO.File]::WriteAllText($sentinel,'original target survives')
 $sentinelHash=(Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
 [IO.File]::WriteAllBytes((Join-Path $nested 'scratch.bin'),[byte[]]::new(32))
 $nestedItem=Get-Item -LiteralPath $nested
 $nestedItem.Attributes=$nestedItem.Attributes -bor [IO.FileAttributes]::ReadOnly
 Assert-Check ([bool]((Get-Item -LiteralPath $nested).Attributes -band [IO.FileAttributes]::ReadOnly)) 'Fixture includes a read-only temporary directory'
 $link=Join-Path $owned 'Content'
 $null=New-Item -ItemType Junction -Path $link -Target $outside
 Assert-Rejected {Remove-GuardianProbeArtifact $fixture $fixture} 'Reject deletion of the allowed root itself'
 Assert-Rejected {Remove-GuardianProbeArtifact $outside $owned} 'Reject a sibling outside the allowed root'
 Assert-Rejected {Remove-GuardianProbeArtifact (Join-Path $link 'keep.txt') $owned} 'Reject traversal through a linked parent'
 $deleted=Remove-GuardianProbeArtifact $owned $fixture
 Assert-Check ($deleted.deletedBytes -eq 32 -and $deleted.unlinkedEntries -eq 1 -and !(Test-Path -LiteralPath $owned)) 'Delete ordinary nested files and unlink the junction'
 Assert-Check ((Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -eq $sentinelHash) 'Preserve the junction target and its original bytes'

 $runtime=Join-Path $fixture 'RuntimeProject'
 $moduleDirectory=Join-Path $runtime 'Binaries/Win64'
 $null=New-Item -ItemType Directory -Path $moduleDirectory
 $module=Join-Path $moduleDirectory 'UnrealEditor-Project_RPG.dll'
 [IO.File]::WriteAllText($module,'simulated module; no UE process is launched')
 $moduleHash=(Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
 $envName='PROJECT_RPG_GAME_SERVER_ID'
 $originalEnvironment=[Environment]::GetEnvironmentVariable($envName,'Process')
 try{
  [Environment]::SetEnvironmentVariable($envName,'fixture-environment','Process')
  foreach($mode in @('success','failure')){
   $fixtureState.mode=$mode;$fixtureState.processes=@()
   $output=Join-Path $fixture $mode
   $thrown=$false
   try{& (Join-Path $networkRoot 'Run-GuardianMelee2NetworkTest.ps1') -RuntimeProject $runtime -OutputRoot $output | Out-Null}catch{
    if($mode -ne 'failure' -or $_.Exception.Message -notmatch 'Simulated owner startup failure'){throw}
    $thrown=$true
   }
   $run=@(Get-ChildItem -LiteralPath $output -Directory)[0].FullName
   $cleanup=Get-Content -LiteralPath (Join-Path $run 'Cleanup.proof.json') -Raw -Encoding UTF8|ConvertFrom-Json
   Assert-Check ($cleanup.passed -and $cleanup.deletedBytes -eq $(if($mode -eq 'success'){3072}else{2048})) "$mode run automatically discards owned UserDirs"
   Assert-Check (@(Get-ChildItem -LiteralPath $run -Directory).Count -eq 0) "$mode run leaves no UserDir behind"
   Assert-Check ((Test-Path -LiteralPath (Join-Path $run 'server.log')) -and (Test-Path -LiteralPath (Join-Path $run 'Invocation.json'))) "$mode run retains its result evidence and invocation"
   Assert-Check ((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -eq $moduleHash) "$mode run preserves the caller-owned runtime module"
   Assert-Check ([Environment]::GetEnvironmentVariable($envName,'Process') -eq 'fixture-environment') "$mode run restores the caller environment"
   if($mode -eq 'failure'){
    Assert-Check ($thrown -and (Test-Path -LiteralPath (Join-Path $run 'Failure.json')) -and @($fixtureState.processes|Where-Object {!$_.HasExited}).Count -eq 0) 'Failure remains a failure and its owned process is stopped before deletion'
   }else{
    Assert-Check (Test-Path -LiteralPath (Join-Path $run 'Network.proof.json')) 'Successful simulation retains the network verdict'
   }
  }
 }finally{[Environment]::SetEnvironmentVariable($envName,$originalEnvironment,'Process')}
 $passed=$true
}finally{
 foreach($process in $fixtureState.processes){
  if(!$process.HasExited){
   $owned=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
   if(!$owned.CommandLine.Contains($fixture) -or !$owned.CommandLine.Contains('-RPGGuardianMelee2Network')){throw 'Fixture process ownership could not be confirmed.'}
   Stop-Process -Id $process.Id -ErrorAction Stop
   if(!$process.WaitForExit(10000)){throw 'Fixture child is still running.'}
  }
 }
 $fixtureCleanup=Remove-GuardianProbeArtifact $fixture $evidenceRoot
 @{passed=$passed;checks=@($checks.ToArray());unrealLaunched=$false;fixtureDeletedBytes=$fixtureCleanup.deletedBytes;fixtureRemoved=!(Test-Path -LiteralPath $fixture)}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $evidenceRoot 'RunnerLifecycle.proof.json') -Encoding UTF8
}
Write-Output "PASS $($checks.Count) artifact/lifecycle assertions; no Unreal build or runtime."
