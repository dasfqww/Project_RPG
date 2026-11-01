function Get-DungeonAbandonmentSettlementProof {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ServerText,[Parameter(Mandatory)][string[]]$ClientTexts,
        [ValidateRange(2,4)][int]$ClientCount,[switch]$UnexpectedDisconnect,[switch]$GameplayAbandonment,
        [switch]$GameplayResponseLoss,[switch]$GameplayReauthentication)
    if ($GameplayResponseLoss -and -not $GameplayAbandonment) { throw 'Combat response loss needs combat consent mode.' }
    if ($GameplayReauthentication -and -not $GameplayResponseLoss) { throw 'Combat reauthentication needs response loss mode.' }
    if ($GameplayAbandonment -and ($UnexpectedDisconnect -or $ClientCount -ne 2)) { throw 'Combat consent mode requires exactly two clients and no unexpected-loss mode.' }
    if ($ClientTexts.Count -ne $ClientCount) { throw 'Wrong client log count.' }
    foreach ($log in (@($ServerText)+$ClientTexts)) {
        if ($log -match 'DUNGEON_FLOW_E2E_FAIL|RPG_EJECTION_NET FAILED|Fatal error:|Assertion failed:|Ensure condition failed:|RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED') { throw 'Runtime failure/admission bypass.' }
    }
    $guid='[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}'
    $peers=[regex]::Matches($ServerText,"DUNGEON_ABANDON_SETTLEMENT PEER Character=($guid) Steam=(\d{17}) PlayerId=(\d+) Authenticated=1")
    $ready=[regex]::Matches($ServerText,"DUNGEON_ABANDON_SETTLEMENT PARTY_READY Session=($guid) Server=([A-Za-z0-9-]+) Target=($guid) Players=(\d+) Iris=1 NoIdentitySeed=1 NoReceiptSeed=1")
    $revoked=[regex]::Matches($ServerText,"DUNGEON_ABANDON_SETTLEMENT REVOCATION Character=($guid) Steam=(\d{17}) PlayerId=(\d+) Revoked=1 GameThread=1 ConnectionOpen=1 ActorsRetained=1")
    $loss=[regex]::Matches($ServerText,"DUNGEON_DISCONNECT_SETTLEMENT SOCKET_LOST Target=($guid) Remaining=(\d+) TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 NoPlayerAbandonment=1")
    $expiry=[regex]::Matches($ServerText,"DUNGEON_DISCONNECT_SETTLEMENT EXPIRY_CACHED Target=($guid) Remaining=(\d+) ImmutableReceipt=1 ProductionPolling=1 NoReceiptSeed=1")
    $transition=$revoked
    $combatCached=[regex]::Matches($ServerText,"DUNGEON_GAMEPLAY_ABANDON SERVER_CACHED Target=($guid) Generation=(\d+) Remaining=(\d+) PlannedSocketClosed=1 ProductionPolling=1 SamePeers=1 SameEncounter=1")
    if ($GameplayAbandonment) {
        if ($revoked.Count -or $loss.Count -or $expiry.Count -or $combatCached.Count -ne 1 -or
            [regex]::Matches($ServerText,'DUNGEON_GAMEPLAY_ABANDON SERVER_CACHED ').Count -ne 1 -or
            [long]$combatCached[0].Groups[2].Value -le 0 -or [int]$combatCached[0].Groups[3].Value -ne 1) { throw 'Missing or conflicting planned combat return/cache evidence.' }
        $transition=$combatCached
    } elseif ($ServerText -match 'DUNGEON_GAMEPLAY_ABANDON ' -or $ClientTexts -match 'DUNGEON_GAMEPLAY_ABANDON ') { throw 'Combat consent needs its explicit proof mode.' }
    if ($UnexpectedDisconnect) {
        if ($revoked.Count -or $loss.Count -ne 1 -or $expiry.Count -ne 1 -or $loss[0].Groups[1].Value -ne $expiry[0].Groups[1].Value -or
            [int]$loss[0].Groups[2].Value -ne $ClientCount-1 -or [int]$expiry[0].Groups[2].Value -ne $ClientCount-1 -or
            $loss[0].Index -ge $expiry[0].Index) { throw 'Missing or conflicting unexpected-loss/production-expiry evidence.' }
        $transition=$loss
    } elseif ($loss.Count -or $expiry.Count) { throw 'Unexpected loss cannot stand in for explicit abandonment.' }
    $remaining=[regex]::Matches($ServerText,"DUNGEON_ABANDON_SETTLEMENT REMAINING_READY Target=($guid) Remaining=(\d+) TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 InProgress=1")
    $victory=[regex]::Matches($ServerText,"DUNGEON_ABANDON_SETTLEMENT VICTORY Source=($guid) Remaining=(\d+) ServerGASHit=1 DeadToVictory=1 NoDirectSettlement=1")
    $gas=[regex]::Matches($ServerText,'DUNGEON_FLOW_E2E_SERVER_VICTORY ServerGASHit=1 DeadToVictory=1 Party=(\d+)')
    if ($peers.Count -ne $ClientCount -or $ready.Count -ne 1 -or $transition.Count -ne 1 -or $remaining.Count -ne 1 -or $victory.Count -ne 1 -or $gas.Count -ne 1) { throw 'Missing/duplicate party or victory evidence.' }
    $session=$ready[0].Groups[1].Value; $server=$ready[0].Groups[2].Value; $target=$ready[0].Groups[3].Value
    if ([int]$ready[0].Groups[4].Value -ne $ClientCount -or $transition[0].Groups[1].Value -ne $target -or $remaining[0].Groups[1].Value -ne $target -or
        [int]$remaining[0].Groups[2].Value -ne $ClientCount-1 -or [int]$victory[0].Groups[2].Value -ne $ClientCount-1 -or [int]$gas[0].Groups[1].Value -ne $ClientCount-1 -or
        $victory[0].Groups[1].Value -eq $target -or $ready[0].Index -ge $transition[0].Index -or $transition[0].Index -ge $remaining[0].Index -or $remaining[0].Index -ge $victory[0].Index -or
        ($UnexpectedDisconnect -and $expiry[0].Index -ge $remaining[0].Index)) { throw 'Wrong target, count or lifetime ordering.' }
    $characters=@($peers | ForEach-Object { $_.Groups[1].Value })
    $accounts=@($peers | ForEach-Object { $_.Groups[2].Value })
    $ids=@($peers | ForEach-Object { [int]$_.Groups[3].Value })
    if (@($characters | Select-Object -Unique).Count -ne $ClientCount -or @($accounts | Select-Object -Unique).Count -ne $ClientCount -or
        @($ids | Select-Object -Unique).Count -ne $ClientCount -or $target -notin $characters -or $victory[0].Groups[1].Value -notin $characters) { throw 'Duplicate/foreign party identity.' }
    $targetPeer=@($peers | Where-Object { $_.Groups[1].Value -eq $target })[0]
    $targetId=[int]$targetPeer.Groups[3].Value
    if (-not ($UnexpectedDisconnect -or $GameplayAbandonment) -and ($revoked[0].Groups[2].Value -ne $targetPeer.Groups[2].Value -or [int]$revoked[0].Groups[3].Value -ne $targetId)) { throw 'Revoked account/player mismatch.' }
    $serverSim=[regex]::Matches($ServerText,'DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Server PlayerId=(\d+) Iris=1 LagMin=80 LagMax=160 Loss=5')
    if ($serverSim.Count -ne $ClientCount -or (Compare-Object @($ids | Sort-Object) @($serverSim | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object))) { throw 'Missing server simulation coverage.' }
    $clientIds=@()
    $returnPhaseDisconnects=0
    foreach ($log in $ClientTexts) {
        $admission=[regex]::Matches($log,"DUNGEON_ABANDON_SETTLEMENT ADMITTED Character=($guid) Steam=(\d{17}) PlayerId=(\d+) Session=($guid)")
        if ($admission.Count -ne 1) { throw 'Missing/duplicate actual client admission.' }
        $id=[int]$admission[0].Groups[3].Value
        $matching=@($peers | Where-Object { [int]$_.Groups[3].Value -eq $id -and $_.Groups[1].Value -eq $admission[0].Groups[1].Value -and $_.Groups[2].Value -eq $admission[0].Groups[2].Value })
        if ($matching.Count -ne 1 -or $id -in $clientIds -or $admission[0].Groups[4].Value -ne $session) { throw 'Borrowed client identity/session.' }
        $clientIds+=$id
        $sim=[regex]::Matches($log,"DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Client PlayerId=$id Iris=1 LagMin=80 LagMax=160 Loss=5")
        if ($sim.Count -ne 1 -or [regex]::Matches($log,'DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Client ').Count -ne 1 -or
            [regex]::Matches($log,'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1').Count -ne 1 -or
            [regex]::Matches($log,'DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1').Count -ne 1) { throw 'Missing real admission/simulation.' }
        $disconnect=[regex]::Matches($log,'RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=(\d+) EngineFailure=(\d+) ExpectedCloseType=1')
        $result=[regex]::Matches($log,'DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0')
        $lobby=[regex]::Matches($log,'DUNGEON_FLOW_E2E_LOBBY_COMPLETE ActualMap=1 Standalone=1 Idle=1 AuthSelectionRetained=1 NoJoinTicket=1')
        if ($id -eq $targetId) {
            if ($UnexpectedDisconnect) {
                if ($log -match 'RPG_EJECTION_NET CLIENT_DISCONNECTED ' -or $result.Count -or $lobby.Count) { throw 'Killed target unexpectedly completed a graceful return or reward.' }
            } elseif ($GameplayAbandonment) {
                $stages=@(
                    "OPEN Session=($guid) Character=($guid) PlayerId=(\d+) PublicControllerEntry=1 LiveWidget=1 InProgress=1",
                    "CANCELLED Session=($guid) Character=($guid) PlayerId=(\d+) SameControllerPawnStateConnection=1 InputRestored=1 NoConsent=1",
                    "CONFIRMED Session=($guid) Character=($guid) PlayerId=(\d+) LiveButton=1 ConsentPinned=1 PlannedReturn=1 NoReceiptYet=1",
                    "TRAVEL Session=($guid) Character=($guid) PlannedReturn=1 ConsentRetained=1",
                    "RECEIPT Session=($guid) Character=($guid) PlayerId=(\d+) AbandonedAt=([^ ]+) State=InProgress RewardExcluded=1 ActualLobby=1 SameProcess=1",
                    "LOBBY_COMPLETE Session=($guid) Character=($guid) Receipt=1 SessionCleared=1 AuthSelectionRetained=1 NoJoinTicket=1 SameProcess=1")
                $previous=$admission[0].Index; $confirmedIndex=0; $lobbyIndex=0; $stageIndices=@{}
                foreach ($stage in $stages) {
                    $name=($stage -split ' ')[0]
                    $markers=[regex]::Matches($log,"DUNGEON_GAMEPLAY_ABANDON $stage")
                    if ($markers.Count -ne 1 -or [regex]::Matches($log,"DUNGEON_GAMEPLAY_ABANDON $name ").Count -ne 1 -or
                        $markers[0].Index -le $previous -or $markers[0].Groups[1].Value -cne $session -or $markers[0].Groups[2].Value -cne $target) { throw 'Missing/foreign/duplicate combat consent stage or wrong order.' }
                    if ($name -in @('OPEN','CANCELLED','CONFIRMED','RECEIPT')) {
                        $value=$markers[0].Groups[3].Value
                        if ([int]$value -ne $targetId) { throw 'Combat consent changed its actual admitted PlayerId.' }
                    }
                    if ($name -eq 'CONFIRMED') { $confirmedIndex=$markers[0].Index }
                    if ($name -eq 'LOBBY_COMPLETE') { $lobbyIndex=$markers[0].Index }
                    $stageIndices[$name]=$markers[0].Index
                    $previous=$markers[0].Index
                }
                $lost=[regex]::Matches($log,"DUNGEON_GAMEPLAY_ABANDON LOST_REPLY Session=($guid) Character=($guid) SameProcess=1 ActualLobby=1 IdentityRetained=1 NoImplicitReceipt=1")
                if ($GameplayResponseLoss) {
                    if ($lost.Count -ne 1 -or [regex]::Matches($log,'DUNGEON_GAMEPLAY_ABANDON LOST_REPLY ').Count -ne 1 -or
                        $lost[0].Groups[1].Value -cne $session -or $lost[0].Groups[2].Value -cne $target -or
                        $lost[0].Index -le $stageIndices.TRAVEL -or $lost[0].Index -ge $stageIndices.RECEIPT) { throw 'Missing/foreign/duplicate combat lost acknowledgement.' }
                    $recovery=@('UNCONFIRMED IdentityRetained=1 NewDungeonLocked=1 RetryEnabled=1',
                        'RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1')
                    if ($GameplayReauthentication) {
                        $recovery+=@('AUTH_EXPIRED Real401=1 AccountPinned=1 IdentityRetained=1 NewDungeonLocked=1 LiveLogin=1',
                            'AUTH_RESTORED SameAccount=1 OwnedRoster=1 IdentityRetained=1 RetryEnabled=1',
                            'RENEWED_RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1')
                    } elseif ($log -match 'DUNGEON_ABANDON_UI (?:AUTH_EXPIRED|AUTH_RESTORED|RENEWED_RETRY_SUBMITTED) ') { throw 'Unexpected combat reauthentication evidence.' }
                    $previous=$lost[0].Index
                    foreach ($step in $recovery) {
                        $name=($step -split ' ')[0]
                        $markers=[regex]::Matches($log,"DUNGEON_ABANDON_UI $step")
                        if ($markers.Count -ne 1 -or [regex]::Matches($log,"DUNGEON_ABANDON_UI $name ").Count -ne 1 -or
                            $markers[0].Index -le $previous -or $markers[0].Index -ge $stageIndices.RECEIPT) { throw 'Missing/duplicate/out-of-order live combat recovery step.' }
                        $previous=$markers[0].Index
                    }
                    $recovered=[regex]::Matches($log,'DUNGEON_ABANDON_UI RECOVERED LiveButton=1 ReceiptApplied=1 SelectionRetained=1 NewDungeonEnabled=1')
                    if ($recovered.Count -ne 1 -or [regex]::Matches($log,'DUNGEON_ABANDON_UI RECOVERED ').Count -ne 1 -or
                        $recovered[0].Index -le $lobbyIndex) { throw 'Combat recovery did not unlock the actual lobby after receipt.' }
                } elseif ($log -match 'DUNGEON_GAMEPLAY_ABANDON LOST_REPLY |DUNGEON_ABANDON_UI ') { throw 'Combat recovery requires its explicit proof mode.' }
                $rawCloses=[regex]::Matches($log,'RPG_EJECTION_NET CLIENT_DISCONNECTED ')
                if ($result.Count -or $lobby.Count -or $rawCloses.Count -ne $disconnect.Count -or $disconnect.Count -gt 1 -or
                    ($disconnect.Count -eq 1 -and ([int]$disconnect[0].Groups[1].Value -ne $id -or [int]$disconnect[0].Groups[2].Value -notin @(3,6) -or
                        $disconnect[0].Index -le $confirmedIndex -or $disconnect[0].Index -ge $lobbyIndex))) { throw 'Combat target was rewarded or disconnected outside its confirmed return.' }
            } elseif ($disconnect.Count -ne 1 -or [regex]::Matches($log,'RPG_EJECTION_NET CLIENT_DISCONNECTED ').Count -ne 1 -or
                [int]$disconnect[0].Groups[1].Value -ne $id -or [int]$disconnect[0].Groups[2].Value -notin @(3,6) -or $result.Count -or $lobby.Count) { throw 'Target was rewarded or lacked actual disconnect.' }
        } else {
            if ($GameplayAbandonment -and $log -match 'DUNGEON_GAMEPLAY_ABANDON ') { throw 'Another member supplied the target consent evidence.' }
            if ($result.Count -ne 1 -or $lobby.Count -ne 1 -or $result[0].Index -ge $lobby[0].Index) { throw 'Survivor did not complete rewarded return.' }
            # A confirmed result return now closes its socket on the server. Do
            # not mistake this bounded return-phase engine failure for mid-raid ejection.
            $rawCloses=[regex]::Matches($log,'RPG_EJECTION_NET CLIENT_DISCONNECTED ')
            if ($rawCloses.Count) {
                if ($rawCloses.Count -ne 1 -or $disconnect.Count -ne 1 -or [int]$disconnect[0].Groups[1].Value -ne $id -or
                    [int]$disconnect[0].Groups[2].Value -notin @(3,6) -or $disconnect[0].Index -le $result[0].Index -or
                    $disconnect[0].Index -ge $lobby[0].Index) { throw 'Survivor connection was lost outside its rewarded return phase.' }
                ++$returnPhaseDisconnects
            }
        }
    }
    return [ordered]@{schemaVersion=1;scenario=$(if ($GameplayAbandonment) {'GameplayAbandonmentSettlement'} elseif ($UnexpectedDisconnect) {'UnexpectedDisconnectSettlement'} else {'RemainingPartySettlement'});clientCount=$ClientCount;
        targetPlayerId=$targetId;targetCharacterId=$target;backendSessionId=$session;backendServerId=$server;
        backendPeers=@($peers | ForEach-Object { [ordered]@{characterId=$_.Groups[1].Value;steamId=$_.Groups[2].Value;playerId=[int]$_.Groups[3].Value} });
        actualTicketAdmission=$true;backendHttp=$true;database=$false;syntheticIdentities=$false;syntheticReceipt=$false;
        actualClientDisconnect=$true;automaticMembershipPollingEjection=(-not ($UnexpectedDisconnect -or $GameplayAbandonment));
        gameplayConsent=[bool]$GameplayAbandonment;publicControllerConsentEntry=[bool]$GameplayAbandonment;
        gameplayResponseLoss=[bool]$GameplayResponseLoss;gameplayReauthentication=[bool]$GameplayReauthentication;
        productionAbandonmentCachedAfterPlannedReturn=[bool]$GameplayAbandonment;physicalMenuInput=$false;slatePointerInput=$false;
        gameplayConnectionGeneration=$(if ($GameplayAbandonment) {$combatCached[0].Groups[2].Value} else {$null});
        productionDisconnectionExpiryCached=[bool]$UnexpectedDisconnect;remainingPartySameActorsAndConnections=$true;
        survivorReturnPhaseDisconnects=$returnPhaseDisconnects;
        sameEncounterPreserved=$true;actualBossMap=$true;serverGASHitToVictory=$true;liveRewardResultClick=$true;actualLocalLobbyReturn=$true;
        packetLagMinMs=80;packetLagMaxMs=160;packetLossPercent=5;naturalCombat=$false;steam=$false;shipping=$false;packagedDedicatedServer=$false}
}
