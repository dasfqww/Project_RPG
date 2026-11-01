# Shared by the live runner and offline result audits. No engine/process access.
function Test-GuardianMelee2Results {
 [CmdletBinding()]
 param([Parameter(Mandatory)][string]$Directory,[switch]$MovingPlatform)
 $proof=@{passed=$false;movingPlatform=[bool]$MovingPlatform;maximumPositionErrorCm=30;maximumRecoveryDriftCm=50;inputs=@{};positionErrors=@{}}
 function Require-Number($Report,[string]$Field,[string]$Role){
  $value=$Report.$Field
  $numeric=$value -is [int] -or $value -is [long] -or $value -is [double] -or $value -is [float] -or $value -is [decimal]
  if(!$numeric -or [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value)){
   throw "Invalid $Role.$Field : expected a finite JSON number"
  }
  return [double]$value
 }
 function Require-Boolean($Report,[string]$Field,[bool]$Expected,[string]$Role){
  if($Report.$Field -isnot [bool] -or $Report.$Field -ne $Expected){throw "Invalid $Role.$Field : expected boolean $Expected"}
 }
 try{
  $results=@{}
  foreach($role in @('server','owner','observer')){
   $path=Join-Path $Directory ($role+'.result.json')
   # Hash the exact bytes parsed, without a second file read.
   $bytes=[IO.File]::ReadAllBytes($path)
   $sha=[Security.Cryptography.SHA256]::Create()
   try{$proof.inputs[$role]=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')}finally{$sha.Dispose()}
   $json=[Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff).Trim()
   # PowerShell can unwrap a single-element JSON array; require an object first.
   if(!$json.StartsWith('{')){throw "Invalid $role result: expected a JSON object"}
   $result=ConvertFrom-Json -InputObject $json -ErrorAction Stop
   if($result -isnot [pscustomobject]){throw "Invalid $role result object"}
   foreach($field in @('success','iris','movementAuthorizationReleased')){Require-Boolean $result $field $true $role}
   foreach($field in @('syntheticDamage','replacementGrant')){Require-Boolean $result $field $false $role}
   Require-Boolean $result 'movingPlatform' ([bool]$MovingPlatform) $role
   Require-Boolean $result 'locallyControlledReceiver' ($role -eq 'owner') $role
   if($result.role -isnot [string] -or $result.role -cne $role){throw "Mismatched $role role identity"}
   if($result.playerClass -isnot [string] -or $result.playerClass -cne '/Game/Blueprints/Character/Player/BP_Player.BP_Player_C'){throw "Unexpected $role player class"}
   foreach($entry in @{phases=15;lagMin=80;lagMax=160;loss=5;securityViolations=0}.GetEnumerator()){
    if((Require-Number $result $entry.Key $role) -ne $entry.Value){throw "Unexpected $role.$($entry.Key)"}
   }
   $poses=Require-Number $result 'poses' $role
   if($poses -lt 0 -or $poses -gt 15 -or [Math]::Floor($poses) -ne $poses -or ($role -ne 'server' -and $poses -ne 14)){throw "Invalid $role pose phases"}
   foreach($field in @('initialHP','finalHP','initialZ','finalX','finalY','finalZ','rise')){$null=Require-Number $result $field $role}
   if($result.initialHP -le 0 -or $result.finalHP -le 0 -or $result.finalHP -ge $result.initialHP){throw "Invalid $role damage observation"}
   if($result.rise -lt 50 -or [Math]::Abs($result.finalZ-$result.initialZ) -gt 8){throw "Invalid $role ascent or landing observation"}
   if($MovingPlatform){
    foreach($field in @('finalRelativeX','finalRelativeY','platformTravel','maximumRecoveryRelativeDrift','downBasedSamples','getUpBasedSamples')){$null=Require-Number $result $field $role}
    Require-Boolean $result 'recoveryBaseLost' $false $role
    if($result.platformTravel -lt 500 -or $result.maximumRecoveryRelativeDrift -lt 0 -or $result.maximumRecoveryRelativeDrift -gt 50){throw "Invalid $role platform travel or drift"}
    foreach($field in @('downBasedSamples','getUpBasedSamples')){
     if($result.$field -lt 1 -or [Math]::Floor($result.$field) -ne $result.$field){throw "Invalid $role.$field sample count"}
    }
   }
   $results[$role]=$result
  }
  foreach($role in @('owner','observer')){
   $baseline=$results.server;$client=$results[$role]
   # A moving base advances between completion times; compare relative X/Y.
   $dx=if($MovingPlatform){$baseline.finalRelativeX-$client.finalRelativeX}else{$baseline.finalX-$client.finalX}
   $dy=if($MovingPlatform){$baseline.finalRelativeY-$client.finalRelativeY}else{$baseline.finalY-$client.finalY}
   $distance=[Math]::Sqrt([Math]::Pow($dx,2)+[Math]::Pow($dy,2)+[Math]::Pow($baseline.finalZ-$client.finalZ,2))
   if([double]::IsNaN($distance) -or [double]::IsInfinity($distance) -or $distance -gt 30 -or $baseline.finalHP -ne $client.finalHP -or $baseline.initialHP -ne $client.initialHP){throw "$role final state did not converge"}
   $proof.positionErrors[$role]=$distance
  }
  $proof.passed=$true
 }catch{
  $proof.passed=$false;$proof.error=$_.Exception.Message
  $proof|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Directory 'ResultContract.proof.json') -Encoding UTF8
  throw
 }
 $proof|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Directory 'ResultContract.proof.json') -Encoding UTF8
 return $results
}
