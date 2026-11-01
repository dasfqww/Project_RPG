function Get-DungeonReconnectSettlementProof {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ServerText,[Parameter(Mandatory)][string[]]$ClientTexts,
        [ValidateRange(2,4)][int]$ClientCount,[switch]$AdmissionLoss,[switch]$ConfirmationResponseLoss,[string]$PendingClientText)
    if ($ConfirmationResponseLoss -and -not $AdmissionLoss) { throw 'Committed response loss needs pending-admission evidence.' }
    if ($ClientTexts.Count -ne $ClientCount+1) { throw 'Reconnect needs original party logs plus one replacement log.' }
    foreach ($log in @($ServerText)+$ClientTexts) {
        if ($log -match 'DUNGEON_FLOW_E2E_FAIL|RPG_EJECTION_NET FAILED|Fatal error:|Assertion failed:|Ensure condition failed:|RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED') { throw 'Reconnect runtime failure or admission bypass.' }
    }
    $guid='[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}'
    $peers=[regex]::Matches($ServerText,"DUNGEON_ABANDON_SETTLEMENT PEER Character=($guid) Steam=(\d{17}) PlayerId=(\d+) Authenticated=1")
    $party=[regex]::Matches($ServerText,"DUNGEON_ABANDON_SETTLEMENT PARTY_READY Session=($guid) Server=([A-Za-z0-9-]+) Target=($guid) Players=(\d+) Iris=1 NoIdentitySeed=1 NoReceiptSeed=1")
    $ready=[regex]::Matches($ServerText,"DUNGEON_RECONNECT_SETTLEMENT READY Target=($guid) OriginalGeneration=(\d+) OriginalPlayerId=(\d+) Players=(\d+)")
    $loss=[regex]::Matches($ServerText,"DUNGEON_DISCONNECT_SETTLEMENT SOCKET_LOST Target=($guid) Remaining=(\d+) TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 NoPlayerAbandonment=1")
    $rejoin=[regex]::Matches($ServerText,"DUNGEON_RECONNECT_SETTLEMENT RECONNECTED Target=($guid) OriginalGeneration=(\d+) Generation=(\d+) PlayerId=(\d+) Players=(\d+) NewConnection=1 SamePeers=1 SameEncounter=1 Authenticated=1")
    $victory=[regex]::Matches($ServerText,'DUNGEON_RECONNECT_SETTLEMENT VICTORY Players=(\d+) ServerGASHit=1 DeadToVictory=1 NoDirectSettlement=1')
    $gas=[regex]::Matches($ServerText,'DUNGEON_FLOW_E2E_SERVER_VICTORY ServerGASHit=1 DeadToVictory=1 Party=(\d+)')
    if ($peers.Count -ne $ClientCount -or $party.Count -ne 1 -or $ready.Count -ne 1 -or $loss.Count -ne 1 -or
        $rejoin.Count -ne 1 -or $victory.Count -ne 1 -or $gas.Count -ne 1 -or
        $ServerText -match 'DUNGEON_DISCONNECT_SETTLEMENT EXPIRY_CACHED|DUNGEON_ABANDON_SETTLEMENT REVOCATION') { throw 'Missing/duplicate reconnect stages or conflicting expulsion.' }
    $session=$party[0].Groups[1].Value; $server=$party[0].Groups[2].Value; $target=$party[0].Groups[3].Value
    $oldGeneration=[long]$ready[0].Groups[2].Value; $newGeneration=[long]$rejoin[0].Groups[3].Value
    $oldPlayer=[int]$ready[0].Groups[3].Value; $newPlayer=[int]$rejoin[0].Groups[4].Value
    $increment=1+[int][bool]$AdmissionLoss
    if ($oldGeneration -le 0 -or $oldGeneration -gt [long]::MaxValue-$increment -or $newGeneration -ne $oldGeneration+$increment -or
        $rejoin[0].Groups[2].Value -cne $ready[0].Groups[2].Value -or $oldPlayer -eq $newPlayer -or
        $ready[0].Groups[1].Value -cne $target -or $loss[0].Groups[1].Value -cne $target -or $rejoin[0].Groups[1].Value -cne $target -or
        [int]$party[0].Groups[4].Value -ne $ClientCount -or [int]$ready[0].Groups[4].Value -ne $ClientCount -or
        [int]$loss[0].Groups[2].Value -ne $ClientCount-1 -or [int]$rejoin[0].Groups[5].Value -ne $ClientCount -or
        [int]$victory[0].Groups[1].Value -ne $ClientCount -or [int]$gas[0].Groups[1].Value -ne $ClientCount -or
        $party[0].Index -ge $ready[0].Index -or $ready[0].Index -ge $loss[0].Index -or
        $loss[0].Index -ge $rejoin[0].Index -or $rejoin[0].Index -ge $gas[0].Index -or $gas[0].Index -ge $victory[0].Index) { throw 'Wrong reconnect generation, target, party size, or order.' }
    $characters=@($peers | ForEach-Object { $_.Groups[1].Value })
    $accounts=@($peers | ForEach-Object { $_.Groups[2].Value })
    $ids=@($peers | ForEach-Object { [int]$_.Groups[3].Value })
    $targetPeer=@($peers | Where-Object { $_.Groups[1].Value -ceq $target -and [int]$_.Groups[3].Value -eq $oldPlayer })
    if (@($characters | Select-Object -Unique).Count -ne $ClientCount -or @($accounts | Select-Object -Unique).Count -ne $ClientCount -or
        @($ids | Select-Object -Unique).Count -ne $ClientCount -or $targetPeer.Count -ne 1 -or $newPlayer -in $ids) { throw 'Foreign/duplicate original or replacement identity.' }
    $pending=[regex]::Matches($ServerText,"DUNGEON_RECONNECT_SETTLEMENT CONFIRMATION_PENDING Target=($guid) Generation=(\d+) PlayerId=(\d+) NoIdentity=1 NoPawn=1 SamePeers=1 SameEncounter=1")
    $cleaned=[regex]::Matches($ServerText,"DUNGEON_RECONNECT_SETTLEMENT PENDING_SOCKET_LOST Target=($guid) Generation=(\d+) NoActivation=1 SamePeers=1 SameEncounter=1")
    if ($AdmissionLoss) {
        if ($pending.Count -ne 1 -or $cleaned.Count -ne 1 -or $pending[0].Groups[1].Value -cne $target -or
            $cleaned[0].Groups[1].Value -cne $target -or [long]$pending[0].Groups[2].Value -ne $oldGeneration+1 -or
            [long]$cleaned[0].Groups[2].Value -ne $oldGeneration+1 -or [int]$pending[0].Groups[3].Value -in $ids -or
            [int]$pending[0].Groups[3].Value -eq $newPlayer -or $loss[0].Index -ge $pending[0].Index -or
            $pending[0].Index -ge $cleaned[0].Index -or $cleaned[0].Index -ge $rejoin[0].Index -or
            $PendingClientText -match 'DUNGEON_FLOW_E2E_(ADMITTED|RETURN_CLICK|LOBBY_COMPLETE|FAIL)|CLIENT_DISCONNECTED|Fatal error:|Ensure condition failed:' -or
            [regex]::Matches($PendingClientText,"DUNGEON_RECONNECT_SETTLEMENT ATTEMPT Character=$target Session=$session FreshTicket=1").Count -ne 1 -or
            [regex]::Matches($PendingClientText,'DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1').Count -ne 1) { throw 'Missing unconfirmed reconnect isolation/cleanup or wrong failed client scope.' }
    } elseif ($pending.Count -or $cleaned.Count -or $PendingClientText) { throw 'Pending-admission failure needs its explicit evidence mode.' }
    $simulation=[regex]::Matches($ServerText,'DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Server PlayerId=(\d+) Iris=1 LagMin=80 LagMax=160 Loss=5')
    if ($simulation.Count -ne $ClientCount -or (Compare-Object @($ids | Sort-Object) @($simulation | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object))) { throw 'Missing original server simulation coverage.' }
    $clientIds=@(); $returnCloses=0
    foreach ($log in $ClientTexts) {
        $admission=[regex]::Matches($log,"DUNGEON_ABANDON_SETTLEMENT ADMITTED Character=($guid) Steam=(\d{17}) PlayerId=(\d+) Session=($guid)")
        if ($admission.Count -ne 1) { throw 'Missing/duplicate actual client admission.' }
        $id=[int]$admission[0].Groups[3].Value
        $character=$admission[0].Groups[1].Value; $steam=$admission[0].Groups[2].Value
        $owner=@($peers | Where-Object { $_.Groups[1].Value -ceq $character -and $_.Groups[2].Value -ceq $steam })
        if ($owner.Count -ne 1 -or $id -in $clientIds -or $admission[0].Groups[4].Value -cne $session -or
            ($id -eq $newPlayer -and $character -cne $target) -or
            ($id -ne $newPlayer -and [int]$owner[0].Groups[3].Value -ne $id) -or
            [regex]::Matches($log,'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1').Count -ne 1 -or
            [regex]::Matches($log,'DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1').Count -ne 1 -or
            [regex]::Matches($log,"DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Client PlayerId=$id Iris=1 LagMin=80 LagMax=160 Loss=5").Count -ne 1 -or
            [regex]::Matches($log,'DUNGEON_ABANDON_SETTLEMENT SIMULATION Role=Client ').Count -ne 1) { throw 'Foreign client/session or missing fresh-ticket simulation.' }
        $clientIds+=$id
        $result=[regex]::Matches($log,'DUNGEON_FLOW_E2E_RETURN_CLICK LiveWidget=1 Gold=500 InventoryQuantity=4 MailQuantity=0')
        $lobby=[regex]::Matches($log,'DUNGEON_FLOW_E2E_LOBBY_COMPLETE ActualMap=1 Standalone=1 Idle=1 AuthSelectionRetained=1 NoJoinTicket=1')
        $close=[regex]::Matches($log,'RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=(\d+) EngineFailure=(\d+) ExpectedCloseType=1')
        $rawClose=[regex]::Matches($log,'RPG_EJECTION_NET CLIENT_DISCONNECTED ')
        if ($id -eq $oldPlayer) {
            if ($result.Count -or $lobby.Count -or $rawClose.Count) { throw 'Killed original process unexpectedly returned or was rewarded.' }
        } else {
            if ($result.Count -ne 1 -or $lobby.Count -ne 1 -or $result[0].Index -ge $lobby[0].Index) { throw 'An active party member did not complete rewarded return.' }
            if ($rawClose.Count) {
                if ($rawClose.Count -ne 1 -or $close.Count -ne 1 -or [int]$close[0].Groups[1].Value -ne $id -or
                    [int]$close[0].Groups[2].Value -notin @(3,6) -or $close[0].Index -le $result[0].Index -or $close[0].Index -ge $lobby[0].Index) { throw 'Active member disconnected before confirmed return.' }
                $returnCloses++
            }
        }
    }
    if ($newPlayer -notin $clientIds -or (Compare-Object @($ids+$newPlayer | Sort-Object) @($clientIds | Sort-Object))) { throw 'Original/replacement client coverage mismatch.' }
    return [ordered]@{schemaVersion=1;scenario='ReconnectWithinGraceSettlement';clientCount=$ClientCount;
        targetPlayerId=$oldPlayer;reconnectedPlayerId=$newPlayer;targetCharacterId=$target;backendSessionId=$session;backendServerId=$server;
        originalGeneration=$oldGeneration.ToString();newGeneration=$newGeneration.ToString();
        admissionPendingSocketLoss=[bool]$AdmissionLoss;noIdentityOrPawnBeforeConfirmation=[bool]$AdmissionLoss;
        confirmationResponseLossRequested=[bool]$ConfirmationResponseLoss;
        backendPeers=@($peers | ForEach-Object { [ordered]@{characterId=$_.Groups[1].Value;steamId=$_.Groups[2].Value;playerId=[int]$_.Groups[3].Value} });
        actualFreshTicketAdmission=$true;replacedTargetConnection=$true;remainingPartySameActorsAndConnections=$true;sameEncounterPreserved=$true;
        fullPartyVictory=$true;liveRewardResultClick=$true;actualLocalLobbyReturn=$true;survivorReturnPhaseDisconnects=$returnCloses;
        backendHttp=$true;database=$false;syntheticIdentities=$false;syntheticReceipt=$false;naturalCombat=$false;steam=$false;shipping=$false;
        packetLagMinMs=80;packetLagMaxMs=160;packetLossPercent=5}
}
