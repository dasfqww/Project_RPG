[CmdletBinding()]
param([string[]]$HistoricalRun=@())
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runner=Join-Path $PSScriptRoot '../Network/Run-GuardianMelee2NetworkTest.ps1'
. (Join-Path $PSScriptRoot '../Network/GuardianMelee2Artifacts.ps1')
$output=Join-Path $workspace ('Saved/Tests/GuardianMotionContract-20261031/'+[Guid]::NewGuid().ToString('N'))
$temp=Join-Path $output 'Temp';$fixture=Join-Path $temp 'Fixture'
New-Item -ItemType Directory -Path $fixture -Force|Out-Null
$checks=[Collections.Generic.List[string]]::new();$history=@();$cleanup=$null;$failure=$null
function Assert-Check([bool]$Value,[string]$Name){if(!$Value){throw $Name};$checks.Add($Name)}
function Reset-Fixture {
 foreach($role in @('server','owner','observer')){[IO.File]::WriteAllText((Join-Path $fixture ($role+'.result.json')),'{"success":true,"rise":180}')}
}
function Set-Result([string]$Role,$Value){$Value|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $fixture ($Role+'.result.json')) -Encoding UTF8}
function Assert-Rejected([string]$Name,[scriptblock]$Edit){
 Reset-Fixture;& $Edit
 # Rejected re-audits must replace stale PASS evidence with a failed verdict.
 [IO.File]::WriteAllText((Join-Path $fixture 'MotionContract.proof.json'),'{"passed":true}')
 $rejected=$false
 try{& $runner -AuditRun $fixture|Out-Null}catch{$rejected=$true}
 $proof=Get-Content -LiteralPath (Join-Path $fixture 'MotionContract.proof.json') -Raw -Encoding UTF8|ConvertFrom-Json
 Assert-Check ($rejected -and $proof.passed -eq $false -and $proof.maximumRiseErrorCm -eq 35 -and [bool]$proof.error) $Name
}
try{
 Reset-Fixture;& $runner -AuditRun $fixture
 $proof=Get-Content -LiteralPath (Join-Path $fixture 'MotionContract.proof.json') -Raw -Encoding UTF8|ConvertFrom-Json
 Assert-Check ($proof.passed -and $proof.ownerRiseErrorCm -eq 0 -and $proof.observerRiseErrorCm -eq 0) 'Accept actual matching numeric ascent'
 Reset-Fixture;Set-Result owner @{success=$true;rise=215};Set-Result observer @{success=$true;rise=145}
 & $runner -AuditRun $fixture
 $proof=Get-Content -LiteralPath (Join-Path $fixture 'MotionContract.proof.json') -Raw -Encoding UTF8|ConvertFrom-Json
 Assert-Check ($proof.passed -and $proof.ownerRiseErrorCm -eq 35 -and $proof.observerRiseErrorCm -eq 35) 'Preserve the 35cm inclusive boundary'
 foreach($role in @('server','owner','observer')){
  Assert-Rejected "Reject missing $role rise" {Set-Result $role @{success=$true}}
  Assert-Rejected "Reject null $role rise" {Set-Result $role @{success=$true;rise=$null}}
 }
 foreach($value in @('180','NaN','Infinity',$true,$false,0,-180,49.999)){
  Assert-Rejected ('Reject invalid numeric value: '+[string]$value) {Set-Result owner @{success=$true;rise=$value}}
 }
 Assert-Rejected 'Reject overflowing JSON exponent' {[IO.File]::WriteAllText((Join-Path $fixture 'observer.result.json'),'{"success":true,"rise":1e400}')}
 Assert-Rejected 'Reject failed report with a plausible rise' {Set-Result owner @{success=$false;rise=180}}
 Assert-Rejected 'Reject string success marker' {Set-Result server @{success='true';rise=180}}
 Assert-Rejected 'Reject missing success marker' {Set-Result observer @{rise=180}}
 Assert-Rejected 'Reject a result array' {[IO.File]::WriteAllText((Join-Path $fixture 'server.result.json'),'[{"success":true,"rise":180},{"success":true,"rise":180}]')}
 Assert-Rejected 'Reject malformed JSON' {[IO.File]::WriteAllText((Join-Path $fixture 'server.result.json'),'{invalid')}
 Assert-Rejected 'Reject missing role file' {Remove-Item -LiteralPath (Join-Path $fixture 'owner.result.json')}
 Assert-Rejected 'Reject owner divergence beyond 35cm' {Set-Result owner @{success=$true;rise=215.01}}
 Assert-Rejected 'Reject observer divergence beyond 35cm' {Set-Result observer @{success=$true;rise=215.01}}
 $index=0
 foreach($directory in $HistoricalRun){
  $copy=Join-Path $temp ('Historical-'+$index);New-Item -ItemType Directory -Path $copy|Out-Null
  $records=@()
  foreach($role in @('server','owner','observer')){
   $path=[IO.Path]::GetFullPath((Join-Path $directory ($role+'.result.json')))
   $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
   Copy-Item -LiteralPath $path -Destination $copy
   $records+=@{path=$path;sha256=$hash}
  }
  & $runner -AuditRun $copy
  $proof=Get-Content -LiteralPath (Join-Path $copy 'MotionContract.proof.json') -Raw -Encoding UTF8|ConvertFrom-Json
  foreach($record in $records){if((Get-FileHash -LiteralPath $record.path -Algorithm SHA256).Hash -ne $record.sha256){throw 'Historical result changed'}}
  Assert-Check ([bool]$proof.passed) ('Accept preserved real three-role results '+$index)
  $history+=@{inputs=$records;motion=$proof};++$index
 }
}catch{$failure=$_.Exception.Message;throw}finally{
 $cleanup=Remove-GuardianProbeArtifact $temp $output
 @{passed=(!$failure);failure=$failure;checks=@($checks.ToArray());historicalAudits=$history;cleanup=$cleanup;temporaryArtifactsRemoved=(!(Test-Path -LiteralPath $temp));unrealLaunched=$false;gameBuildRun=$false}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $output 'Verification.proof.json') -Encoding UTF8
}
Write-Output "PASS $($checks.Count) motion-contract checks; no UE launch; Evidence=$output"
