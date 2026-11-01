[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Network\DungeonAbandonmentSettlementProof.ps1')
$script:passed=0
function Test-Rejected([string]$Name,[scriptblock]$Action) {
    $rejected=$false
    try { & $Action | Out-Null } catch { $rejected=$true }
    if (-not $rejected) { throw "Invalid proof accepted: $Name" }
    $script:passed++
}
function New-SettlementFixture([int]$Count) {
    $session='20304050-6070-8090-a0b0-c0d0e0f00102'
    $characters=@(1..$Count | ForEach-Object { "${_}0304050-6070-8090-a0b0-c0d0e0f00102" })
    $server=@(); $clients=@()
    for ($i=1;$i -le $Count;$i++) {
        $character=$characters[$i-1]; $steam="7656119800000000$i"
        $server+="DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Server PlayerId=$i Iris=1 LagMin=80 LagMax=160 Loss=5"
        $server+="DUNGEON_ABANDON_SETTLEMENT PEER Character=$character Steam=$steam PlayerId=$i Authenticated=1"
        $client=@('DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1',
            'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1',
            "DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Client PlayerId=$i Iris=1 LagMin=80 LagMax=160 Loss=5",
            "DUNGEON_ABANDON_SETTLEMENT ADMITTED Character=$character Steam=$steam PlayerId=$i Session=$session")
        if ($i -eq 1) { $client+='RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=1 EngineFailure=3 ExpectedCloseType=1' }
        else { $client+=@('DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0',
            'DUNGEON_FLOW_E2E_LOBBY_COMPLETE ActualMap=1 Standalone=1 Idle=1 AuthSelectionRetained=1 NoJoinTicket=1') }
        $clients+=$client -join "`n"
    }
    $server+="DUNGEON_ABANDON_SETTLEMENT PARTY_READY Session=$session Server=membership-fixture Target=$($characters[0]) Players=$Count Iris=1 NoIdentitySeed=1 NoReceiptSeed=1"
    $server+="DUNGEON_ABANDON_SETTLEMENT REVOCATION Character=$($characters[0]) Steam=76561198000000001 PlayerId=1 Revoked=1 GameThread=1 ConnectionOpen=1 ActorsRetained=1"
    $server+="DUNGEON_ABANDON_SETTLEMENT REMAINING_READY Target=$($characters[0]) Remaining=$($Count-1) TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 InProgress=1"
    $server+="DUNGEON_FLOW_E2E_SERVER_VICTORY ServerGASHit=1 DeadToVictory=1 Party=$($Count-1)"
    $server+="DUNGEON_ABANDON_SETTLEMENT VICTORY Source=$($characters[1]) Remaining=$($Count-1) ServerGASHit=1 DeadToVictory=1 NoDirectSettlement=1"
    return @{ServerText=($server -join "`n");ClientTexts=$clients;ClientCount=$Count}
}
$fixture=New-SettlementFixture 2
foreach ($count in @(2,4)) {
    $case=New-SettlementFixture $count
    $proof=Get-DungeonAbandonmentSettlementProof @case
    if ($proof.database -or -not $proof.actualLocalLobbyReturn -or -not $proof.serverGASHitToVictory -or $proof.syntheticReceipt -or $proof.naturalCombat) { throw 'Proof scope misread.' }
    $script:passed++
}
Test-Rejected 'duplicate peer' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('PlayerId=2 Authenticated=1','PlayerId=1 Authenticated=1'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'foreign client session' { $f=$fixture.Clone(); $f.ClientTexts=@($f.ClientTexts[0].Replace('Session=203','Session=303'),$f.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'duplicate client' { $f=$fixture.Clone(); $f.ClientTexts=@($f.ClientTexts[0],$f.ClientTexts[0]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'wrong revoked account' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('REVOCATION Character=10304050-6070-8090-a0b0-c0d0e0f00102 Steam=76561198000000001','REVOCATION Character=10304050-6070-8090-a0b0-c0d0e0f00102 Steam=76561198000000002'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'seeded identity' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('NoIdentitySeed=1','NoIdentitySeed=0'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'actors already removed at notification' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('ActorsRetained=1','ActorsRetained=0'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'target damage source' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('VICTORY Source=203','VICTORY Source=103'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'rewarded target' { $f=$fixture.Clone(); $f.ClientTexts=@($f.ClientTexts[0]+"`nDUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0",$f.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'survivor disconnect' { $f=$fixture.Clone(); $f.ClientTexts=@($f.ClientTexts[0],$f.ClientTexts[1]+"`nRPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=2 EngineFailure=3 ExpectedCloseType=1"); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'no live reward refresh' { $f=$fixture.Clone(); $f.ClientTexts=@($f.ClientTexts[0],$f.ClientTexts[1].Replace('InventoryQuantity=4','InventoryQuantity=0')); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'encounter reset' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('SameEncounter=1','SameEncounter=0'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'missing actual GAS victory' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('DUNGEON_FLOW_E2E_SERVER_VICTORY','OTHER'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'admission bypass' { $f=$fixture.Clone(); $f.ServerText+="`nRPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED"; Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'runtime failure' { $f=$fixture.Clone(); $f.ServerText+="`nDUNGEON_FLOW_E2E_FAIL"; Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'wrong packet profile' { $f=$fixture.Clone(); $f.ClientTexts=@($f.ClientTexts[0],$f.ClientTexts[1].Replace('Loss=5','Loss=0')); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'foreign victory source' { $f=$fixture.Clone(); $f.ServerText=$f.ServerText.Replace('VICTORY Source=203','VICTORY Source=903'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'early victory' { $f=$fixture.Clone(); $lines=$f.ServerText -split "`n"; $f.ServerText=$lines[-1]+"`n"+($lines[0..($lines.Count-2)] -join "`n"); Get-DungeonAbandonmentSettlementProof @f }
foreach ($count in @(2,4)) {
    $disconnect=New-SettlementFixture $count
    $disconnect.ServerText=[regex]::Replace($disconnect.ServerText,'DUNGEON_ABANDON_SETTLEMENT REVOCATION [^\n]+',
        "DUNGEON_DISCONNECT_SETTLEMENT SOCKET_LOST Target=10304050-6070-8090-a0b0-c0d0e0f00102 Remaining=$($count-1) TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 NoPlayerAbandonment=1`n"+
        "DUNGEON_DISCONNECT_SETTLEMENT EXPIRY_CACHED Target=10304050-6070-8090-a0b0-c0d0e0f00102 Remaining=$($count-1) ImmutableReceipt=1 ProductionPolling=1 NoReceiptSeed=1")
    $disconnect.ClientTexts[0]=$disconnect.ClientTexts[0].Replace("`nRPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=1 EngineFailure=3 ExpectedCloseType=1",'')
    $proof=Get-DungeonAbandonmentSettlementProof @disconnect -UnexpectedDisconnect
    if ($proof.automaticMembershipPollingEjection -or -not $proof.productionDisconnectionExpiryCached -or
        $proof.scenario -ne 'UnexpectedDisconnectSettlement' -or $proof.database) { throw 'Unexpected-disconnect proof overstated scope.' }
    $script:passed++
}
Test-Rejected 'explicit opt-out cannot prove unexpected loss' { Get-DungeonAbandonmentSettlementProof @fixture -UnexpectedDisconnect }
Test-Rejected 'loss cannot prove explicit opt-out' { Get-DungeonAbandonmentSettlementProof @disconnect }
Test-Rejected 'expiry missing' { $f=$disconnect.Clone(); $f.ServerText=$f.ServerText.Replace('EXPIRY_CACHED','OTHER'); Get-DungeonAbandonmentSettlementProof @f -UnexpectedDisconnect }
Test-Rejected 'foreign expiry identity' { $f=$disconnect.Clone(); $f.ServerText=$f.ServerText.Replace('EXPIRY_CACHED Target=103','EXPIRY_CACHED Target=903'); Get-DungeonAbandonmentSettlementProof @f -UnexpectedDisconnect }
Test-Rejected 'synthetic expiry' { $f=$disconnect.Clone(); $f.ServerText=$f.ServerText.Replace('ImmutableReceipt=1 ProductionPolling=1','ImmutableReceipt=1 ProductionPolling=0'); Get-DungeonAbandonmentSettlementProof @f -UnexpectedDisconnect }
Test-Rejected 'rewarded killed target' { $f=$disconnect.Clone(); $f.ClientTexts=@($disconnect.ClientTexts); $f.ClientTexts[0]+="`nDUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0"; Get-DungeonAbandonmentSettlementProof @f -UnexpectedDisconnect }
$returnClose=New-SettlementFixture 2
$returnClose.ClientTexts[1]=$returnClose.ClientTexts[1].Replace('DUNGEON_FLOW_E2E_LOBBY_COMPLETE',"RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=2 EngineFailure=3 ExpectedCloseType=1`nDUNGEON_FLOW_E2E_LOBBY_COMPLETE")
$proof=Get-DungeonAbandonmentSettlementProof @returnClose
if ($proof.survivorReturnPhaseDisconnects -ne 1) { throw 'Expected server-closed rewarded return was not distinguished.' }
$script:passed++
Test-Rejected 'foreign return-phase close' { $f=$returnClose.Clone(); $f.ClientTexts=@($returnClose.ClientTexts); $f.ClientTexts[1]=$f.ClientTexts[1].Replace('CLIENT_DISCONNECTED PlayerId=2','CLIENT_DISCONNECTED PlayerId=9'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'unknown return-phase failure' { $f=$returnClose.Clone(); $f.ClientTexts=@($returnClose.ClientTexts); $f.ClientTexts[1]=$f.ClientTexts[1].Replace('EngineFailure=3','EngineFailure=5'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'duplicate return-phase close' { $f=$returnClose.Clone(); $f.ClientTexts=@($returnClose.ClientTexts); $f.ClientTexts[1]=$f.ClientTexts[1].Replace('DUNGEON_FLOW_E2E_LOBBY_COMPLETE',"RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=2 EngineFailure=3 ExpectedCloseType=1`nDUNGEON_FLOW_E2E_LOBBY_COMPLETE"); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'pre-result survivor close' { $f=$returnClose.Clone(); $f.ClientTexts=@($returnClose.ClientTexts); $f.ClientTexts[1]="RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=2 EngineFailure=3 ExpectedCloseType=1`n"+$fixture.ClientTexts[1]; Get-DungeonAbandonmentSettlementProof @f }
$combat=New-SettlementFixture 2
$combat.GameplayAbandonment=$true
$combat.ServerText=[regex]::Replace($combat.ServerText,'DUNGEON_ABANDON_SETTLEMENT REVOCATION [^\n]+',
    'DUNGEON_GAMEPLAY_ABANDON SERVER_CACHED Target=10304050-6070-8090-a0b0-c0d0e0f00102 Generation=1 Remaining=1 PlannedSocketClosed=1 ProductionPolling=1 SamePeers=1 SameEncounter=1')
$combat.ClientTexts=@($combat.ClientTexts)
$combat.ClientTexts[0]=$combat.ClientTexts[0].Replace("`nRPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=1 EngineFailure=3 ExpectedCloseType=1",'')
$scope='Session=20304050-6070-8090-a0b0-c0d0e0f00102 Character=10304050-6070-8090-a0b0-c0d0e0f00102'
$combat.ClientTexts[0]+=@(
    "`nDUNGEON_GAMEPLAY_ABANDON OPEN $scope PlayerId=1 PublicControllerEntry=1 LiveWidget=1 InProgress=1",
    "DUNGEON_GAMEPLAY_ABANDON CANCELLED $scope PlayerId=1 SameControllerPawnStateConnection=1 InputRestored=1 NoConsent=1",
    "DUNGEON_GAMEPLAY_ABANDON CONFIRMED $scope PlayerId=1 LiveButton=1 ConsentPinned=1 PlannedReturn=1 NoReceiptYet=1",
    'RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=1 EngineFailure=3 ExpectedCloseType=1',
    "DUNGEON_GAMEPLAY_ABANDON TRAVEL $scope PlannedReturn=1 ConsentRetained=1",
    "DUNGEON_GAMEPLAY_ABANDON RECEIPT $scope PlayerId=1 AbandonedAt=2026-10-22T01:00:00+00:00 State=InProgress RewardExcluded=1 ActualLobby=1 SameProcess=1",
    "DUNGEON_GAMEPLAY_ABANDON LOBBY_COMPLETE $scope Receipt=1 SessionCleared=1 AuthSelectionRetained=1 NoJoinTicket=1 SameProcess=1") -join "`n"
$proof=Get-DungeonAbandonmentSettlementProof @combat
if ($proof.automaticMembershipPollingEjection -or -not $proof.productionAbandonmentCachedAfterPlannedReturn -or
    -not $proof.gameplayConsent -or $proof.physicalMenuInput -or $proof.slatePointerInput -or $proof.database) { throw 'Combat consent evidence overstated its scope.' }
$script:passed++
$graceful=$combat.Clone(); $graceful.ClientTexts=@($combat.ClientTexts)
$graceful.ClientTexts[0]=$graceful.ClientTexts[0].Replace("RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=1 EngineFailure=3 ExpectedCloseType=1`n",'')
$null=Get-DungeonAbandonmentSettlementProof @graceful; $script:passed++
Test-Rejected 'combat without explicit mode' { $f=$combat.Clone(); $f.GameplayAbandonment=$false; Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat mixed with unexpected loss' { Get-DungeonAbandonmentSettlementProof @combat -UnexpectedDisconnect }
Test-Rejected 'combat four-player claim' { $f=$combat.Clone(); $f.ClientCount=4; Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat missing cancel' { $f=$combat.Clone(); $f.ClientTexts=@($combat.ClientTexts[0].Replace('CANCELLED','OTHER'),$combat.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat changed PlayerId' { $f=$combat.Clone(); $f.ClientTexts=@($combat.ClientTexts[0].Replace('PlayerId=1 LiveButton','PlayerId=2 LiveButton'),$combat.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat lost input lock' { $f=$combat.Clone(); $f.ClientTexts=@($combat.ClientTexts[0].Replace('InputRestored=1','InputRestored=0'),$combat.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat receipt before lobby' { $f=$combat.Clone(); $f.ClientTexts=@($combat.ClientTexts[0].Replace('ActualLobby=1','ActualLobby=0'),$combat.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat missing production cache' { $f=$combat.Clone(); $f.ServerText=$f.ServerText.Replace('SERVER_CACHED','OTHER'); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat old open-socket revocation is not planned recovery' { $f=$combat.Clone(); $f.ServerText=$fixture.ServerText; Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat early target close' { $f=$combat.Clone(); $f.ClientTexts=@("RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=1 EngineFailure=3 ExpectedCloseType=1`n"+$graceful.ClientTexts[0],$combat.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat target result reward' { $f=$combat.Clone(); $f.ClientTexts=@($combat.ClientTexts[0]+"`nDUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0",$combat.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'combat peer borrowed target marker' { $f=$combat.Clone(); $f.ClientTexts=@($combat.ClientTexts[0],$combat.ClientTexts[1]+"`nDUNGEON_GAMEPLAY_ABANDON OPEN"); Get-DungeonAbandonmentSettlementProof @f }
$wideCombat=$combat.Clone(); $wideCombat.ServerText=$wideCombat.ServerText.Replace('Generation=1 Remaining','Generation=9007199254740993 Remaining')
$wideProof=Get-DungeonAbandonmentSettlementProof @wideCombat
if ($wideProof.gameplayConnectionGeneration -cne '9007199254740993') { throw 'Server-only generation lost Int64 precision.' }
$script:passed++
$combatLoss=$combat.Clone(); $combatLoss.ClientTexts=@($combat.ClientTexts); $combatLoss.GameplayResponseLoss=$true
$recoveryLines=@("DUNGEON_GAMEPLAY_ABANDON LOST_REPLY $scope SameProcess=1 ActualLobby=1 IdentityRetained=1 NoImplicitReceipt=1",
    'DUNGEON_ABANDON_UI UNCONFIRMED IdentityRetained=1 NewDungeonLocked=1 RetryEnabled=1',
    'DUNGEON_ABANDON_UI RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1') -join "`n"
$combatLoss.ClientTexts[0]=$combatLoss.ClientTexts[0].Replace('DUNGEON_GAMEPLAY_ABANDON RECEIPT',"$recoveryLines`nDUNGEON_GAMEPLAY_ABANDON RECEIPT")+
    "`nDUNGEON_ABANDON_UI RECOVERED LiveButton=1 ReceiptApplied=1 SelectionRetained=1 NewDungeonEnabled=1"
$proof=Get-DungeonAbandonmentSettlementProof @combatLoss
if (-not $proof.gameplayResponseLoss -or $proof.gameplayReauthentication -or $proof.database) { throw 'Combat loss scope overstated.' }
$script:passed++
$combatReauth=$combatLoss.Clone(); $combatReauth.ClientTexts=@($combatLoss.ClientTexts); $combatReauth.GameplayReauthentication=$true
$authLines=@('DUNGEON_ABANDON_UI AUTH_EXPIRED Real401=1 AccountPinned=1 IdentityRetained=1 NewDungeonLocked=1 LiveLogin=1',
    'DUNGEON_ABANDON_UI AUTH_RESTORED SameAccount=1 OwnedRoster=1 IdentityRetained=1 RetryEnabled=1',
    'DUNGEON_ABANDON_UI RENEWED_RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1') -join "`n"
$combatReauth.ClientTexts[0]=$combatReauth.ClientTexts[0].Replace('DUNGEON_GAMEPLAY_ABANDON RECEIPT',"$authLines`nDUNGEON_GAMEPLAY_ABANDON RECEIPT")
$proof=Get-DungeonAbandonmentSettlementProof @combatReauth
if (-not $proof.gameplayReauthentication -or $proof.database) { throw 'Combat reauthentication scope overstated.' }
$script:passed++
Test-Rejected 'loss without combat mode' { Get-DungeonAbandonmentSettlementProof @fixture -GameplayResponseLoss }
Test-Rejected 'reauth without loss mode' { Get-DungeonAbandonmentSettlementProof @combat -GameplayReauthentication }
Test-Rejected 'loss accepted as normal combat' { $f=$combatLoss.Clone(); $f.GameplayResponseLoss=$false; Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'normal combat cannot prove lost receipt' { Get-DungeonAbandonmentSettlementProof @combat -GameplayResponseLoss }
Test-Rejected 'lost foreign member' { $f=$combatLoss.Clone(); $f.ClientTexts=@($combatLoss.ClientTexts[0].Replace('LOST_REPLY Session=203','LOST_REPLY Session=903'),$combatLoss.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'lost reply duplicate' { $f=$combatLoss.Clone(); $f.ClientTexts=@($combatLoss.ClientTexts[0]+"`nDUNGEON_GAMEPLAY_ABANDON LOST_REPLY $scope SameProcess=1 ActualLobby=1 IdentityRetained=1 NoImplicitReceipt=1",$combatLoss.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'recovery missing new dungeon lock' { $f=$combatLoss.Clone(); $f.ClientTexts=@($combatLoss.ClientTexts[0].Replace('NewDungeonLocked=1','NewDungeonLocked=0'),$combatLoss.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'recovery missing actual 401' { $f=$combatReauth.Clone(); $f.ClientTexts=@($combatReauth.ClientTexts[0].Replace('Real401=1','Real401=0'),$combatReauth.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'recovery changed login account' { $f=$combatReauth.Clone(); $f.ClientTexts=@($combatReauth.ClientTexts[0].Replace('SameAccount=1','SameAccount=0'),$combatReauth.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'recovery missing renewed player POST' { $f=$combatReauth.Clone(); $f.ClientTexts=@($combatReauth.ClientTexts[0].Replace('RENEWED_RETRY_SUBMITTED','OTHER'),$combatReauth.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'unrequested reauthentication' { $f=$combatReauth.Clone(); $f.GameplayReauthentication=$false; Get-DungeonAbandonmentSettlementProof @f }
Test-Rejected 'recovery unlock absent' { $f=$combatLoss.Clone(); $f.ClientTexts=@($combatLoss.ClientTexts[0].Replace('NewDungeonEnabled=1','NewDungeonEnabled=0'),$combatLoss.ClientTexts[1]); Get-DungeonAbandonmentSettlementProof @f }
Write-Host "PASS: $script:passed abandonment settlement proof checks."
