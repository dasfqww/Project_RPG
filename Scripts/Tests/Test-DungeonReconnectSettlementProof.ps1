[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Network\DungeonReconnectSettlementProof.ps1')
$script:passed=0
function Test-ReconnectRejected([string]$Name,[scriptblock]$Action) {
    $rejected=$false
    try { & $Action | Out-Null } catch { $rejected=$true }
    if (-not $rejected) { throw "Invalid reconnect evidence accepted: $Name" }
    $script:passed++
}
function New-ReconnectFixture([int]$Count,[long]$Generation=1) {
    $session='20304050-6070-8090-a0b0-c0d0e0f00102'
    $target='10304050-6070-8090-a0b0-c0d0e0f00102'
    $server=@(); $clients=@()
    for ($index=1;$index -le $Count+1;$index++) {
        $owner=if($index -eq $Count+1){1}else{$index}
        $character="${owner}0304050-6070-8090-a0b0-c0d0e0f00102"; $steam="7656119800000000$owner"
        if ($index -le $Count) {
            $server+="DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Server PlayerId=$index Iris=1 LagMin=80 LagMax=160 Loss=5"
            $server+="DUNGEON_ABANDON_SETTLEMENT PEER Character=$character Steam=$steam PlayerId=$index Authenticated=1"
        }
        $client=@('DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1',
            'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1',
            "DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Client PlayerId=$index Iris=1 LagMin=80 LagMax=160 Loss=5",
            "DUNGEON_ABANDON_SETTLEMENT ADMITTED Character=$character Steam=$steam PlayerId=$index Session=$session")
        if ($index -ne 1) {
            $client+=@('DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0',
                "RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=$index EngineFailure=3 ExpectedCloseType=1",
                'DUNGEON_FLOW_E2E_LOBBY_COMPLETE ActualMap=1 Standalone=1 Idle=1 AuthSelectionRetained=1 NoJoinTicket=1')
        }
        $clients+=($client -join "`n")
    }
    $server+="DUNGEON_ABANDON_SETTLEMENT PARTY_READY Session=$session Server=membership-fixture Target=$target Players=$Count Iris=1 NoIdentitySeed=1 NoReceiptSeed=1"
    $server+="DUNGEON_RECONNECT_SETTLEMENT READY Target=$target OriginalGeneration=$Generation OriginalPlayerId=1 Players=$Count"
    $server+="DUNGEON_DISCONNECT_SETTLEMENT SOCKET_LOST Target=$target Remaining=$($Count-1) TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 NoPlayerAbandonment=1"
    $server+="DUNGEON_RECONNECT_SETTLEMENT RECONNECTED Target=$target OriginalGeneration=$Generation Generation=$($Generation+1) PlayerId=$($Count+1) Players=$Count NewConnection=1 SamePeers=1 SameEncounter=1 Authenticated=1"
    $server+="DUNGEON_FLOW_E2E_SERVER_VICTORY ServerGASHit=1 DeadToVictory=1 Party=$Count"
    $server+="DUNGEON_RECONNECT_SETTLEMENT VICTORY Players=$Count ServerGASHit=1 DeadToVictory=1 NoDirectSettlement=1"
    return @{ServerText=($server -join "`n");ClientTexts=$clients;ClientCount=$Count}
}
foreach($count in @(2,4)) {
    $fixture=New-ReconnectFixture $count
    $proof=Get-DungeonReconnectSettlementProof @fixture
    if ($proof.database -or $proof.steam -or $proof.naturalCombat -or -not $proof.replacedTargetConnection -or
        $proof.survivorReturnPhaseDisconnects -ne $count) { throw 'Reconnect evidence scope misread.' }
    $script:passed++
}
$wide=New-ReconnectFixture 2 9007199254740993
$proof=Get-DungeonReconnectSettlementProof @wide
if ($proof.originalGeneration -cne '9007199254740993' -or $proof.newGeneration -cne '9007199254740994') { throw 'Int64 generation lost precision.' }
$script:passed++
$fixture=New-ReconnectFixture 2
Test-ReconnectRejected 'same generation' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace(' Generation=2',' Generation=1'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'same socket player ID' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('Generation=2 PlayerId=3','Generation=2 PlayerId=1'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'foreign replacement Steam' { $f=$fixture.Clone(); $f.ClientTexts=@($fixture.ClientTexts); $f.ClientTexts[2]=$f.ClientTexts[2].Replace('76561198000000001','76561198000000002'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'foreign session' { $f=$fixture.Clone(); $f.ClientTexts=@($fixture.ClientTexts); $f.ClientTexts[2]=$f.ClientTexts[2].Replace('Session=203','Session=303'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'missing replacement log' { $f=$fixture.Clone(); $f.ClientTexts=$f.ClientTexts[0..1]; Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'duplicate client log' { $f=$fixture.Clone(); $f.ClientTexts=@($fixture.ClientTexts); $f.ClientTexts[2]=$f.ClientTexts[1]; Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'new admission without fresh ticket' { $f=$fixture.Clone(); $f.ClientTexts=@($fixture.ClientTexts); $f.ClientTexts[2]=$f.ClientTexts[2].Replace('FreshTicket=1','FreshTicket=0'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'replacement not rewarded' { $f=$fixture.Clone(); $f.ClientTexts=@($fixture.ClientTexts); $f.ClientTexts[2]=$f.ClientTexts[2].Replace('InventoryQuantity=4','InventoryQuantity=0'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'original killed client rewarded' { $f=$fixture.Clone(); $f.ClientTexts=@($fixture.ClientTexts); $f.ClientTexts[0]+="`nDUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0"; Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'other peer replaced' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('SamePeers=1','SamePeers=0'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'conflicting expiry receipt' { $f=$fixture.Clone(); $f.ServerText+="`nDUNGEON_DISCONNECT_SETTLEMENT EXPIRY_CACHED"; Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'early victory' { $f=$fixture.Clone(); $lines=$f.ServerText -split "`n"; $f.ServerText=$lines[-1]+"`n"+($lines[0..($lines.Count-2)] -join "`n"); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'pre-result disconnect' { $f=$fixture.Clone(); $f.ClientTexts=@($fixture.ClientTexts); $f.ClientTexts[2]="RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=3 EngineFailure=3 ExpectedCloseType=1`n"+$f.ClientTexts[2]; Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'admission bypass' { $f=$fixture.Clone(); $f.ServerText+="`nRPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED"; Get-DungeonReconnectSettlementProof @f }
$pending=$fixture.Clone()
$pending.ClientTexts=@($fixture.ClientTexts)
$pending.ClientTexts[2]=$pending.ClientTexts[2].Replace('PlayerId=3','PlayerId=4')
$pending.ServerText=$pending.ServerText.Replace(' Generation=2 PlayerId=3',' Generation=3 PlayerId=4')
$target='10304050-6070-8090-a0b0-c0d0e0f00102'
$pending.ServerText=$pending.ServerText.Replace('DUNGEON_RECONNECT_SETTLEMENT RECONNECTED',
    "DUNGEON_RECONNECT_SETTLEMENT CONFIRMATION_PENDING Target=$target Generation=2 PlayerId=3 NoIdentity=1 NoPawn=1 SamePeers=1 SameEncounter=1`n"+
    "DUNGEON_RECONNECT_SETTLEMENT PENDING_SOCKET_LOST Target=$target Generation=2 NoActivation=1 SamePeers=1 SameEncounter=1`nDUNGEON_RECONNECT_SETTLEMENT RECONNECTED")
$pending.AdmissionLoss=$true
$pending.PendingClientText="DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1`nDUNGEON_RECONNECT_SETTLEMENT ATTEMPT Character=$target Session=20304050-6070-8090-a0b0-c0d0e0f00102 FreshTicket=1"
$proof=Get-DungeonReconnectSettlementProof @pending
if (-not $proof.admissionPendingSocketLoss -or $proof.newGeneration -cne '3') { throw 'Pending reconnect loss scope misread.' }
$script:passed++
Test-ReconnectRejected 'pending connection activated' { $f=$pending.Clone(); $f.ServerText=$f.ServerText.Replace('NoIdentity=1','NoIdentity=0'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'pending cleanup missing' { $f=$pending.Clone(); $f.ServerText=$f.ServerText.Replace('PENDING_SOCKET_LOST','OTHER'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'pending client published admission' { $f=$pending.Clone(); $f.PendingClientText+="`nDUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1"; Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'pending attempt foreign character' { $f=$pending.Clone(); $f.PendingClientText=$f.PendingClientText.Replace('Character=103','Character=903'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'pending failure wrong generation' { $f=$pending.Clone(); $f.ServerText=$f.ServerText.Replace('Generation=2 PlayerId=3 NoIdentity','Generation=1 PlayerId=3 NoIdentity'); Get-DungeonReconnectSettlementProof @f }
Test-ReconnectRejected 'pending mode not requested' { $f=$pending.Clone(); $f.AdmissionLoss=$false; Get-DungeonReconnectSettlementProof @f }
$committed=$pending.Clone(); $committed.ConfirmationResponseLoss=$true
$proof=Get-DungeonReconnectSettlementProof @committed
if (-not $proof.confirmationResponseLossRequested -or $proof.database) { throw 'Committed fault mode or network-only evidence scope misread.' }
$script:passed++
Test-ReconnectRejected 'committed response loss without pending evidence' { $f=$committed.Clone(); $f.AdmissionLoss=$false; Get-DungeonReconnectSettlementProof @f }
Write-Host "PASS: $script:passed reconnect proof checks."
