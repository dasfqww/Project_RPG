[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Network\DungeonEjectionNetworkProof.ps1')
$script:passed = 0
function Test-Rejected([string]$Name, [scriptblock]$Action) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid proof accepted: $Name" }
    ++$script:passed
}
function New-ProofFixture([string]$Scenario, [int]$Count) {
    $remaining = if ($Scenario -eq 'Ejection') { $Count - 1 } else { $Count }
    $server = @()
    $clients = @()
    for ($i=1; $i -le $Count; ++$i) {
        $server += "RPG_EJECTION_NET SIMULATION Role=Server PlayerId=$i Iris=1 LagMin=80 LagMax=160 Loss=5"
        $server += "RPG_EJECTION_NET BASELINE PlayerId=$i Moves=100 Distance=1500.0"
        if ($i -gt 1) { $server += "RPG_EJECTION_NET PEER_PRESERVED PlayerId=$i Moves=100 Distance=1500.0 Authenticated=1 SameConnection=1" }
        $client = @("RPG_EJECTION_NET SIMULATION Role=Client PlayerId=$i Iris=1 LagMin=80 LagMax=160 Loss=5",
            "RPG_EJECTION_NET CLIENT_READY PlayerId=$i")
        if ($Scenario -eq 'Ejection' -and $i -eq 1) {
            $client += 'RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=1 EngineFailure=3 ExpectedCloseType=1'
        } else {
            $revoked = if ($i -eq 1) { 1 } else { 0 }
            $client += "RPG_EJECTION_NET CLIENT_COMPLETE PlayerId=$i Revoked=$revoked Connections=$remaining Iris=1"
        }
        $clients += $client -join "`n"
    }
    $server += "RPG_EJECTION_NET ACTION Scenario=$Scenario PlayerId=1 Revoked=1 ActorsRetained=1 Logouts=0"
    if ($Scenario -eq 'Ejection') {
        $server += "RPG_EJECTION_NET CLEANUP PlayerId=1 Logouts=1 PawnRemoved=1 ControllerRemoved=1 Remaining=$remaining RetryStates=$remaining"
    } else {
        $server += 'RPG_EJECTION_NET QUARANTINE PlayerId=1 PackedRpcs=100 NormalAfter=0 Frozen=1 ConnectionOpen=1'
    }
    $server += "RPG_EJECTION_NET SERVER_COMPLETE Scenario=$Scenario Players=$Count Target=1 Remaining=$remaining Iris=1 SyntheticReceipt=1"
    return @{ServerText=($server -join "`n"); ClientTexts=$clients; ClientCount=$Count; Scenario=$Scenario
        PacketLagMinMs=80; PacketLagMaxMs=160; PacketLossPercent=5}
}
$ejection = New-ProofFixture Ejection 2
$quarantine = New-ProofFixture Quarantine 4
$proof = Get-DungeonEjectionNetworkProof @ejection
if (-not $proof.actualEngineEjectionAndLogout -or $proof.livePostRevocationPackedRpcs -ne 0 -or $proof.backendHttp) { throw 'Ejection scope misread.' }
++ $script:passed
$proof = Get-DungeonEjectionNetworkProof @quarantine
if ($proof.actualEngineEjectionAndLogout -or $proof.livePostRevocationPackedRpcs -ne 100 -or $proof.closedConnectionQueuedDrainObserved) { throw 'Quarantine scope misread.' }
++ $script:passed
Test-Rejected 'duplicate baseline' { $f=$ejection.Clone(); $f.ServerText=$f.ServerText.Replace('BASELINE PlayerId=2','BASELINE PlayerId=1'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'duplicate client' { $f=$ejection.Clone(); $f.ClientTexts=@($f.ClientTexts[0],$f.ClientTexts[0]); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'survivor disconnected' { $f=$ejection.Clone(); $f.ClientTexts=@($f.ClientTexts[0],($f.ClientTexts[1]+"`nRPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=2 EngineFailure=3 ExpectedCloseType=1")); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'self preservation' { $f=$ejection.Clone(); $f.ServerText=$f.ServerText.Replace('PEER_PRESERVED PlayerId=2','PEER_PRESERVED PlayerId=1'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'packet simulation mismatch' { $f=$ejection.Clone(); $f.ClientTexts=@($f.ClientTexts[0],$f.ClientTexts[1].Replace('LagMax=160','LagMax=0')); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'insufficient baseline' { $f=$ejection.Clone(); $f.ServerText=$f.ServerText.Replace('Moves=100 Distance=1500.0','Moves=0 Distance=0.0'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'no actual cleanup' { $f=$ejection.Clone(); $f.ServerText=$f.ServerText.Replace('PawnRemoved=1','PawnRemoved=0'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'wrong close type' { $f=$ejection.Clone(); $f.ClientTexts=@($f.ClientTexts[0].Replace('ExpectedCloseType=1','ExpectedCloseType=0'),$f.ClientTexts[1]); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'no post-revocation RPCs' { $f=$quarantine.Clone(); $f.ServerText=$f.ServerText.Replace('PackedRpcs=100','PackedRpcs=0'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'stale survivor snapshot' { $f=$ejection.Clone(); $f.ClientTexts=@($f.ClientTexts[0],$f.ClientTexts[1].Replace('Connections=1','Connections=2')); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'runtime failure' { $f=$ejection.Clone(); $f.ServerText+="`nEnsure condition failed: fixture"; Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'duplicate completion' { $f=$ejection.Clone(); $f.ServerText+="`n"+($f.ServerText -split "`n")[-1]; Get-DungeonEjectionNetworkProof @f }
$backend=$ejection.Clone()
$backend.RequireBackendMembership=$true
$backend.ServerText=$backend.ServerText.Replace('SyntheticReceipt=1','SyntheticReceipt=0')
$session='20304050-6070-8090-a0b0-c0d0e0f00102'
$target='30304050-6070-8090-a0b0-c0d0e0f00102'
$other='40304050-6070-8090-a0b0-c0d0e0f00102'
$backend.ServerText+="`nRPG_EJECTION_NET BACKEND_PEER PlayerId=1 Character=$target Steam=76561198000000001 Session=$session Authenticated=1 TicketIdentity=1"
$backend.ServerText+="`nRPG_EJECTION_NET BACKEND_PEER PlayerId=2 Character=$other Steam=76561198000000002 Session=$session Authenticated=1 TicketIdentity=1"
$backend.ServerText+="`nRPG_EJECTION_NET BACKEND_READY Session=$session Server=membership-fixture Character=$target Players=2 NoIdentitySeed=1 NoReceiptSeed=1"
$backend.ServerText+="`nRPG_EJECTION_NET HTTP_REVOCATION PlayerId=1 Session=$session Server=membership-fixture Character=$target ReceiptCache=1 GameThread=1 ConnectionOpen=1 ActorsRetained=1 Logouts=0"
$proof=Get-DungeonEjectionNetworkProof @backend
if (-not $proof.backendHttp -or -not $proof.actualTicketAdmission -or -not $proof.automaticMembershipPollingEjection -or
    $proof.syntheticReceipt -or $proof.syntheticIdentities -or $proof.database -or $proof.backendPeers.Count -ne 2) { throw 'Backend scope misread.' }
++ $script:passed
Test-Rejected 'backend synthetic receipt' { $f=$backend.Clone(); $f.ServerText=$f.ServerText.Replace('SyntheticReceipt=0','SyntheticReceipt=1'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'backend missing HTTP receipt' { $f=$backend.Clone(); $f.ServerText=$f.ServerText.Replace('HTTP_REVOCATION','OTHER'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'backend wrong receipt target' { $f=$backend.Clone(); $f.ServerText=$f.ServerText.Replace('HTTP_REVOCATION PlayerId=1','HTTP_REVOCATION PlayerId=2'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'backend mismatched HTTP scope' { $f=$backend.Clone(); $f.ServerText=$f.ServerText.Replace("HTTP_REVOCATION PlayerId=1 Session=$session", "HTTP_REVOCATION PlayerId=1 Session=$other"); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'backend seeded identity' { $f=$backend.Clone(); $f.ServerText=$f.ServerText.Replace('NoIdentitySeed=1','NoIdentitySeed=0'); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'backend duplicate identity' { $f=$backend.Clone(); $f.ServerText=$f.ServerText.Replace("Character=$other", "Character=$target"); Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'backend admission bypass' { $f=$backend.Clone(); $f.ServerText+="`nRPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED"; Get-DungeonEjectionNetworkProof @f }
Test-Rejected 'backend notification after close' { $f=$backend.Clone(); $f.ServerText=$f.ServerText.Replace('ConnectionOpen=1','ConnectionOpen=0'); Get-DungeonEjectionNetworkProof @f }
Write-Host "PASS: $script:passed dungeon ejection network proof validation checks."
