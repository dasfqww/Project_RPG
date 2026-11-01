[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'NPCDamageLifecycleProof.ps1')
$rows = @(
    'READY Actor=101 ASC=102 Health=70.000000 Defense=1.000000 Class=BP_Dummy_Gdn_C',
    'DEATH Health=0 TagCount=1 Activations=1 Montage=1',
    'CORPSE_REPLAY Index=0 Rejected=1 Health=0 Activations=1',
    'CORPSE_REPLAY Index=1 Rejected=1 Health=0 Activations=1',
    'CORPSE_REPLAY Index=2 Rejected=1 Health=0 Activations=1',
    'DEATH_COMPLETED Ended=1 Cancelled=0',
    'STALE_REJECTED OldActor=101 NewActor=201 OldASC=102 NewASC=202 Health=70.000000',
    'FRESH_ACCEPTED Before=70.000000 After=60.000000 Damage=10.000000',
    'COMPLETE AuthoredNPC=1 AuthoredDeath=1 ServerForcedReplacement=1 Network=0'
)
$completed = 'LogAutomationController: Display: Test Completed. Result={Success} Name={AuthoredDeathAndRespawn} Path={ProjectRPG.NPCDamage.AuthoredDeathAndRespawn}'
$nativeRows = @($rows | ForEach-Object { '[2026.10.10-11.35.36:484][268]LogTemp: Display: RPG_NPC_LIFECYCLE ' + $_ })
$golden = ($nativeRows + $completed) -join "`n"
$script:checks = 0
function Assert-Accept([string] $Name, [string] $Log) {
    $proof = Get-NPCDamageLifecycleProof -Log $Log
    if (-not $proof.actualAuthoredNPCDeathVerified -or $proof.corpseReplayRejections -ne 3 -or
        $proof.observations.oldActorId -ne '101' -or $proof.observations.replacementASCId -ne '202' -or
        $proof.observations.freshDamage -ne 10 -or -not $proof.serverForcedReplacementFixture -or
        $proof.inPlaceResetVerified -or $proof.networkReplicationVerified -or $proof.shippingRuntimeVerified) {
        throw "Unexpected proof scope/observations: $Name"
    }
    ++$script:checks
}
function Assert-Reject([string] $Name, [string] $Log) {
    $rejected = $false
    try { $null = Get-NPCDamageLifecycleProof -Log $Log } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid synthetic evidence accepted: $Name" }
    ++$script:checks
}
Assert-Accept 'Original native rows' $golden
Assert-Accept 'CRLF' ($golden.Replace("`n", "`r`n"))
Assert-Accept 'Controller reprints are ignored' ($golden + "`n" + (($nativeRows | ForEach-Object { 'LogAutomationController: ' + $_ + ' [log]' }) -join "`n"))
Assert-Accept 'Unrelated logs and headless viewport warning' ("LogScript: Warning: Script Msg: No game viewport was found.`n" + $golden)
Assert-Reject 'Controller-only evidence' ((($nativeRows | ForEach-Object { 'LogAutomationController: ' + $_ + ' [log]' }) + $completed) -join "`n")
Assert-Reject 'Missing automation result' ($nativeRows -join "`n")
Assert-Reject 'Duplicate result' ($golden + "`n" + $completed)
Assert-Reject 'Completion result before native observations' ($completed + "`n" + ($nativeRows -join "`n"))
Assert-Reject 'Borrowed completion channel' ($golden.Replace('LogAutomationController: Display:', 'OtherLog: Display:'))
Assert-Reject 'Controller reprinted result only' ($golden.Replace($completed, 'LogAutomationController: ' + $completed + ' [log]'))
Assert-Reject 'Failed result' ($golden.Replace('Result={Success}', 'Result={Fail}'))
Assert-Reject 'Wrong result path' ($golden.Replace('Path={ProjectRPG.NPCDamage.AuthoredDeathAndRespawn}', 'Path={Other.Test}'))
foreach ($failure in @('Fatal error:', 'Assertion failed:', 'Ensure condition failed:', 'Other.Test Result={Fail}',
    'LogScript: Error: Blueprint runtime error', 'LogScript: Warning: Accessed None trying to read property',
    'LogScript: Warning: Attempted to access a pending kill actor', 'LogScript: Warning: Infinite loop detected',
    'LogScript: Warning: Array index out of bounds')) {
    Assert-Reject "Failure: $failure" ($golden + "`n" + $failure)
}
for ($index = 0; $index -lt $nativeRows.Count; ++$index) {
    Assert-Reject "Missing row $index" ((@($nativeRows | Where-Object { $_ -ne $nativeRows[$index] }) + $completed) -join "`n")
    Assert-Reject "Duplicate row $index" ($golden + "`n" + $nativeRows[$index])
}
foreach ($pair in @(
    @('Montage=1','Montage=0'), @('TagCount=1','TagCount=2'), @('Activations=1','Activations=2'),
    @('Cancelled=0','Cancelled=1'), @('Ended=1','Ended=2'), @('Rejected=1','Rejected=0'),
    @('Index=1','Index=0'), @('OldActor=101','OldActor=999'), @('OldASC=102','OldASC=999'),
    @('NewActor=201','NewActor=101'), @('NewASC=202','NewASC=102'), @('Actor=101','Actor=0'),
    @('Class=BP_Dummy_Gdn_C','Class=Other_C'), @('Defense=1.000000','Defense=0.000000'),
    @('READY Actor=101 ASC=102 Health=70.000000','READY Actor=101 ASC=102 Health=0.000000'),
    @('STALE_REJECTED OldActor=101 NewActor=201 OldASC=102 NewASC=202 Health=70.000000','STALE_REJECTED OldActor=101 NewActor=201 OldASC=102 NewASC=202 Health=69.000000'),
    @('After=60.000000','After=0.000000'), @('Damage=10.000000','Damage=0.000000'),
    @('Damage=10.000000','Damage=11.000000'), @('Before=70.000000','Before=NaN'),
    @('AuthoredNPC=1','AuthoredNPC=0'), @('AuthoredDeath=1','AuthoredDeath=0'),
    @('ServerForcedReplacement=1','ServerForcedReplacement=0'), @('Network=0','Network=1'),
    @('LogTemp: Display:','OtherLog: Display:'), @('LogTemp: Display:','LogTemp: Warning:')
)) { Assert-Reject "Altered: $($pair[0])" ($golden.Replace($pair[0], $pair[1])) }
$swapped = @($nativeRows)
$swapped[2] = $nativeRows[3]; $swapped[3] = $nativeRows[2]
Assert-Reject 'Reordered replay indexes' (($swapped + $completed) -join "`n")
$swapped = @($nativeRows)
$swapped[5] = $nativeRows[6]; $swapped[6] = $nativeRows[5]
Assert-Reject 'Replacement before death completed' (($swapped + $completed) -join "`n")
Assert-Reject 'Unrecognized native stage' ($golden + "`nLogTemp: Display: RPG_NPC_LIFECYCLE UNVERIFIED Extra=1")
Write-Host "PASS: $script:checks synthetic NPC lifecycle proof checks. Synthetic logs are not runtime evidence."
