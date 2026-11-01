[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Network\MovementSecurityProof.ps1')
$script:passed = 0

function Test-RejectedProof([string] $Name, [scriptblock] $Action) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid movement proof was accepted: $Name" }
    ++$script:passed
}

$baseServer = @'
RPG_MOVEMENT_NETTEST PEER_IDENTIFIED Role=Server PlayerId=256
RPG_MOVEMENT_NETTEST PEER_IDENTIFIED Role=Server PlayerId=257
RPG_MOVEMENT_NETTEST NORMAL_RESULT Duration=12.10 Distance=4800.0 Samples=122 Walking=122 FalsePositives=0 PlayerId=256
RPG_MOVEMENT_NETTEST NORMAL_RESULT Duration=12.10 Distance=4810.0 Samples=122 Walking=122 FalsePositives=0 PlayerId=257
RPG_MOVEMENT_NETTEST SERVER_COMPLETE FalsePositives=0 PositiveControl=1 PlayerId=256
RPG_MOVEMENT_NETTEST SERVER_COMPLETE FalsePositives=0 PositiveControl=1 PlayerId=257
'@
$rollServer = $baseServer + "`n" + @'
RPG_MOVEMENT_NETTEST ROLL_BARRIER Players=2 StartAt=25.000
RPG_MOVEMENT_NETTEST ROLL_SERVER_RESULT Run=1 Cancelled=0 Distance=390.0 MontageSamples=5 BudgetUsed=0 AuthorizationRevoked=1 PlayerId=256
RPG_MOVEMENT_NETTEST ROLL_SERVER_RESULT Run=2 Cancelled=1 Distance=220.0 MontageSamples=3 BudgetUsed=0 AuthorizationRevoked=1 PlayerId=256
RPG_MOVEMENT_NETTEST ROLL_SERVER_RESULT Run=1 Cancelled=0 Distance=395.0 MontageSamples=5 BudgetUsed=0 AuthorizationRevoked=1 PlayerId=257
RPG_MOVEMENT_NETTEST ROLL_SERVER_RESULT Run=2 Cancelled=1 Distance=225.0 MontageSamples=3 BudgetUsed=0 AuthorizationRevoked=1 PlayerId=257
RPG_MOVEMENT_NETTEST ROLL_SERVER_COMPLETE Runs=2 Natural=1 Cancelled=1 AuthorizationRevoked=1 PlayerId=256 MaxConcurrent=2 IsolationChecks=1
RPG_MOVEMENT_NETTEST ROLL_SERVER_COMPLETE Runs=2 Natural=1 Cancelled=1 AuthorizationRevoked=1 PlayerId=257 MaxConcurrent=2 IsolationChecks=0
RPG_MOVEMENT_NETTEST ROLL_PEER_PRESERVED PlayerId=256 OtherPlayerId=257 Remaining=550.0 Preserved=1
'@
$clients = @(foreach ($playerId in 256, 257) {
    @"
RPG_MOVEMENT_NETTEST PEER_IDENTIFIED Role=Client PlayerId=$playerId
RPG_MOVEMENT_NETTEST CLIENT_COMPLETE Duration=28.01 PlayerId=$playerId
RPG_MOVEMENT_NETTEST ROLL_CLIENT_RESULT Run=1 Cancelled=0 MontageSamples=6 PlayerId=$playerId
RPG_MOVEMENT_NETTEST ROLL_CLIENT_RESULT Run=2 Cancelled=0 MontageSamples=6 PlayerId=$playerId
RPG_MOVEMENT_NETTEST ROLL_CLIENT_COMPLETE Runs=2 Natural=1 Inactive=1 PlayerId=$playerId
"@
})

$proof = Get-MovementSecurityProof -ServerText $rollServer -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
if (-not $proof.concurrentRollObserved -or $proof.maximumConcurrentRollPlayers -ne 2 -or
    $proof.peerCancellationIsolationChecks -ne 1 -or $proof.serverRollMeasurements.Count -ne 4 -or
    $proof.clientRollMeasurements[1].cancelledLocally) { throw 'Valid concurrent Roll proof was misread.' }
++ $script:passed
$basic = Get-MovementSecurityProof -ServerText $baseServer -ClientTexts $clients -ClientCount 2
if ($basic.authoredRollGraphVerified -or -not $basic.playerIdentityCoverageVerified) { throw 'Basic movement proof was misread.' }
++ $script:passed
$singleServer = ($rollServer -split "`n" | Where-Object { $_ -notmatch 'PlayerId=257|ROLL_BARRIER|ROLL_PEER_PRESERVED' }) -join "`n"
$singleServer = $singleServer.Replace('MaxConcurrent=2 IsolationChecks=1', 'MaxConcurrent=1 IsolationChecks=0')
$single = Get-MovementSecurityProof -ServerText $singleServer -ClientTexts @($clients[0]) -ClientCount 1 -VerifyAuthoredRoll $true
if ($single.concurrentRollObserved -or $single.peerCancellationIsolationChecks -ne 0) { throw 'Single Roll proof was misread.' }
++ $script:passed

Test-RejectedProof 'duplicate server completion' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('PositiveControl=1 PlayerId=257', 'PositiveControl=1 PlayerId=256')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'duplicate normal player' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('FalsePositives=0 PlayerId=257', 'FalsePositives=0 PlayerId=256')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'duplicate server identity' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('Role=Server PlayerId=257', 'Role=Server PlayerId=256')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'duplicate client identity' {
    Get-MovementSecurityProof -ServerText $rollServer -ClientTexts @($clients[0], $clients[0]) -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'missing client completion' {
    Get-MovementSecurityProof -ServerText $rollServer -ClientTexts @($clients[0], $clients[1].Replace('CLIENT_COMPLETE', 'INCOMPLETE')) -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'normal movement violation' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('Walking=122 FalsePositives=0', 'Walking=122 FalsePositives=1')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'duplicate Roll run' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('Run=2 Cancelled=1 Distance=220.0', 'Run=1 Cancelled=1 Distance=220.0')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'server cancellation misclassified' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('Run=2 Cancelled=1 Distance=220.0', 'Run=2 Cancelled=0 Distance=220.0')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'no montage playback' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('MontageSamples=5', 'MontageSamples=0')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'no overlapping Roll' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('MaxConcurrent=2', 'MaxConcurrent=1')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'no live peer preservation' {
    $withoutChecks = ($rollServer -split "`n" | Where-Object { $_ -notmatch 'ROLL_PEER_PRESERVED' }) -join "`n"
    Get-MovementSecurityProof -ServerText ($withoutChecks.Replace('IsolationChecks=1', 'IsolationChecks=0')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'self preservation is not peer isolation' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('OtherPlayerId=257', 'OtherPlayerId=256')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'unknown peer identity' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('OtherPlayerId=257', 'OtherPlayerId=999')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'duplicate peer preservation' {
    $duplicateEvidence = $rollServer.Replace('IsolationChecks=1', 'IsolationChecks=2') + "`n" +
        'RPG_MOVEMENT_NETTEST ROLL_PEER_PRESERVED PlayerId=256 OtherPlayerId=257 Remaining=550.0 Preserved=1'
    Get-MovementSecurityProof -ServerText $duplicateEvidence -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'missing server Roll completion' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('AuthorizationRevoked=1 PlayerId=257 MaxConcurrent=', 'AuthorizationRevoked=1 PlayerId=256 MaxConcurrent=')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'missing common start barrier' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('ROLL_BARRIER', 'MISSING_BARRIER')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'stale binary without identity' {
    Get-MovementSecurityProof -ServerText ($rollServer.Replace('FalsePositives=0 PlayerId=', 'FalsePositives=0 UnknownId=')) -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Test-RejectedProof 'runtime failure' {
    Get-MovementSecurityProof -ServerText ($rollServer + "`nEnsure condition failed: fixture") -ClientTexts $clients -ClientCount 2 -VerifyAuthoredRoll $true
}
Write-Host "PASS: $script:passed movement proof validation checks."
