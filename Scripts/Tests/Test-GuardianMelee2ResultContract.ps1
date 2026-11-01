[CmdletBinding()]
param([string[]]$HistoricalRun=@())
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runner=Join-Path $PSScriptRoot '../Network/Run-GuardianMelee2NetworkTest.ps1'
$helper=Join-Path $PSScriptRoot '../Network/GuardianMelee2ResultContract.ps1'
. (Join-Path $PSScriptRoot '../Network/GuardianMelee2Artifacts.ps1')
$output=Join-Path $workspace ('Saved/Tests/GuardianResultContract-20261101/'+[guid]::NewGuid().ToString('N'))
$temp=Join-Path $output 'Temp';$fixture=Join-Path $temp 'Fixture'
New-Item -ItemType Directory -Path $fixture -Force|Out-Null
$checks=[Collections.Generic.List[string]]::new();$history=@();$failure=$null;$inputs=@{}
foreach($path in @($runner,$helper,$PSCommandPath)){$inputs[[IO.Path]::GetFullPath($path)]=(Get-FileHash -LiteralPath $path).Hash}
function Check([bool]$Value,[string]$Name){if(!$Value){throw $Name};$checks.Add($Name)}
function Write-Result([string]$Role,$Value){$Value|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $fixture ($Role+'.result.json')) -Encoding UTF8}
function Read-Result([string]$Role){Get-Content -Raw -Encoding UTF8 (Join-Path $fixture ($Role+'.result.json'))|ConvertFrom-Json}
function Reset-Fixture([bool]$Platform=$true){
 foreach($role in @('server','owner','observer')){
  $report=@{success=$true;role=$role;phases=15;poses=14;initialHP=100;finalHP=95;rise=180;initialZ=96;finalX=100;finalY=0;finalZ=96;
   locallyControlledReceiver=($role -eq 'owner');lagMin=80;lagMax=160;loss=5;iris=$true;playerClass='/Game/Blueprints/Character/Player/BP_Player.BP_Player_C';
   syntheticDamage=$false;replacementGrant=$false;securityViolations=0;movementAuthorizationReleased=$true;movingPlatform=$Platform}
  if($Platform){foreach($entry in @{platformTravel=600;finalRelativeX=0;finalRelativeY=0;maximumRecoveryRelativeDrift=0;downBasedSamples=2;getUpBasedSamples=2;recoveryBaseLost=$false}.GetEnumerator()){$report[$entry.Key]=$entry.Value}}
  Write-Result $role $report
 }
}
function Audit([bool]$Platform=$true){& $runner -AuditResultRun $fixture -MovingPlatform:$Platform}
function Reject([string]$Name,[scriptblock]$Edit,[bool]$Platform=$true){
 Reset-Fixture $Platform;& $Edit
 [IO.File]::WriteAllText((Join-Path $fixture 'ResultContract.proof.json'),'{"passed":true}')
 $rejected=$false
 try{Audit $Platform}catch{$rejected=$true}
 $proof=Get-Content -Raw -Encoding UTF8 (Join-Path $fixture 'ResultContract.proof.json')|ConvertFrom-Json
 Check ($rejected -and $proof.passed -eq $false -and [bool]$proof.error) $Name
}
try{
 foreach($platform in @($true,$false)){
  Reset-Fixture $platform;Audit $platform
  $proof=Get-Content -Raw -Encoding UTF8 (Join-Path $fixture 'ResultContract.proof.json')|ConvertFrom-Json
  Check ($proof.passed -and $proof.positionErrors.owner -eq 0) "Accept valid results platform=$platform"
  $r=Read-Result owner
  if($platform){$r.finalRelativeX=30;$r.finalX=10000;$r.maximumRecoveryRelativeDrift=50;$r.platformTravel=500}else{$r.finalX=130}
  Write-Result owner $r;Audit $platform
  $proof=Get-Content -Raw -Encoding UTF8 (Join-Path $fixture 'ResultContract.proof.json')|ConvertFrom-Json
  Check ($proof.passed -and $proof.positionErrors.owner -eq 30) "Preserve inclusive convergence and platform bounds platform=$platform"
  Reject "Reject position divergence platform=$platform" {$r=Read-Result owner;if($platform){$r.finalRelativeX=30.01}else{$r.finalX=130.01};Write-Result owner $r} $platform
 }
 foreach($role in @('server','owner','observer')){
  foreach($field in @('initialHP','finalHP','finalX','finalY','finalZ','finalRelativeX','finalRelativeY','maximumRecoveryRelativeDrift','platformTravel','downBasedSamples','getUpBasedSamples')){
   Reject "Reject missing $role.$field" {$r=Read-Result $role;$r.PSObject.Properties.Remove($field);Write-Result $role $r}
   Reject "Reject null $role.$field" {$r=Read-Result $role;$r.$field=$null;Write-Result $role $r}
  }
 }
 foreach($field in @('success','iris','syntheticDamage','replacementGrant','movingPlatform','locallyControlledReceiver','movementAuthorizationReleased','recoveryBaseLost')){
  Reject "Reject missing boolean $field" {$r=Read-Result owner;$r.PSObject.Properties.Remove($field);Write-Result owner $r}
  Reject "Reject string boolean $field" {$r=Read-Result owner;$r.$field=[string]$r.$field;Write-Result owner $r}
  Reject "Reject wrong boolean $field" {$r=Read-Result owner;$r.$field=!$r.$field;Write-Result owner $r}
 }
 foreach($field in @('phases','poses','lagMin','lagMax','loss','securityViolations','initialZ','rise','finalX','maximumRecoveryRelativeDrift')){
  Reject "Reject numeric string $field" {$r=Read-Result owner;$r.$field=[string]$r.$field;Write-Result owner $r}
 }
 Reject 'Reject duplicate role identity' {$r=Read-Result owner;$r.role='server';Write-Result owner $r}
 Reject 'Reject missing role identity' {$r=Read-Result owner;$r.PSObject.Properties.Remove('role');Write-Result owner $r}
 Reject 'Reject wrong player class' {$r=Read-Result owner;$r.playerClass='/Script/Project_RPG.RPGPlayer';Write-Result owner $r}
 Reject 'Reject no actual damage' {$r=Read-Result owner;$r.finalHP=100;Write-Result owner $r}
 Reject 'Reject dead receiver' {$r=Read-Result owner;$r.finalHP=0;Write-Result owner $r}
 Reject 'Reject HP disagreement' {$r=Read-Result owner;$r.finalHP=94;Write-Result owner $r}
 Reject 'Reject negative drift' {$r=Read-Result owner;$r.maximumRecoveryRelativeDrift=-1;Write-Result owner $r}
 Reject 'Reject excessive drift' {$r=Read-Result owner;$r.maximumRecoveryRelativeDrift=50.01;Write-Result owner $r}
 Reject 'Reject inadequate platform travel' {$r=Read-Result owner;$r.platformTravel=499.99;Write-Result owner $r}
 Reject 'Reject fractional sample count' {$r=Read-Result owner;$r.downBasedSamples=1.5;Write-Result owner $r}
 Reject 'Reject missing get-up observation' {$r=Read-Result owner;$r.getUpBasedSamples=0;Write-Result owner $r}
 Reject 'Reject ground security violation' {$r=Read-Result server;$r.securityViolations=1;Write-Result server $r} $false
 Reject 'Reject excessive landing height' {$r=Read-Result owner;$r.finalZ=104.01;Write-Result owner $r}
 Reject 'Reject inadequate ascent' {$r=Read-Result owner;$r.rise=49.99;Write-Result owner $r}
 Reject 'Reject overflowed distance arithmetic' {$r=Read-Result owner;$r.finalRelativeX=1e308;Write-Result owner $r}
 Reject 'Reject overflowing JSON exponent' {$path=Join-Path $fixture 'owner.result.json';$json=Get-Content -Raw $path;$json=$json -replace '"finalRelativeX"\s*:\s*0','"finalRelativeX":1e400';[IO.File]::WriteAllText($path,$json)}
 Reject 'Reject one-element result array' {$path=Join-Path $fixture 'owner.result.json';[IO.File]::WriteAllText($path,('['+(Get-Content -Raw $path)+']'))}
 Reject 'Reject invalid JSON' {[IO.File]::WriteAllText((Join-Path $fixture 'owner.result.json'),'{invalid')}
 Reject 'Reject missing result file' {Remove-Item -LiteralPath (Join-Path $fixture 'owner.result.json')}
 # Exercise the same all-role omission that passed the old live validator.
 Reject 'Reject all roles missing final state' {foreach($role in @('server','owner','observer')){$r=Read-Result $role;foreach($field in @('finalX','finalY','finalZ','initialHP','finalHP','finalRelativeX','finalRelativeY')){$r.PSObject.Properties.Remove($field)};Write-Result $role $r}}
 $index=0
 foreach($directory in $HistoricalRun){
  $copy=Join-Path $temp ('Historical-'+$index);New-Item -ItemType Directory -Path $copy|Out-Null
  $records=@()
  foreach($role in @('server','owner','observer')){
   $path=[IO.Path]::GetFullPath((Join-Path $directory ($role+'.result.json')))
   $hash=(Get-FileHash -LiteralPath $path).Hash
   Copy-Item -LiteralPath $path -Destination $copy
   if((Get-FileHash -LiteralPath (Join-Path $copy ($role+'.result.json'))).Hash -ne $hash){throw 'Historical copy changed'}
   $records+=@{path=$path;sha256=$hash}
  }
  $platform=(Get-Content -Raw -Encoding UTF8 (Join-Path $copy 'server.result.json')|ConvertFrom-Json).movingPlatform
  if($platform -isnot [bool]){throw 'Historical run lacks a boolean platform mode'}
  & $runner -AuditResultRun $copy -MovingPlatform:$platform
  $proof=Get-Content -Raw -Encoding UTF8 (Join-Path $copy 'ResultContract.proof.json')|ConvertFrom-Json
  foreach($record in $records){if((Get-FileHash -LiteralPath $record.path).Hash -ne $record.sha256){throw 'Historical input changed'}}
  Check ([bool]$proof.passed) "Accept historical three-role results $index"
  $history+=@{inputs=$records;result=$proof};++$index
 }
 foreach($path in $inputs.Keys){if((Get-FileHash -LiteralPath $path).Hash -ne $inputs[$path]){throw 'Verification input changed'}}
}catch{$failure=$_.Exception.Message;throw}finally{
 $cleanup=Remove-GuardianProbeArtifact $temp $output
 @{passed=(!$failure);failure=$failure;checks=@($checks.ToArray());inputs=$inputs;historicalAudits=$history;cleanup=$cleanup;temporaryArtifactsRemoved=(!(Test-Path -LiteralPath $temp));unrealLaunched=$false;gameBuildRun=$false}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $output 'Verification.proof.json') -Encoding UTF8
}
Write-Output "PASS $($checks.Count) result-contract checks; no UE launch; Evidence=$output"
