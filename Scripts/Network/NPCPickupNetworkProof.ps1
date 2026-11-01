function Get-NPCPickupNativeRetryGap {
    param([object]$State, [ValidateRange(1,8)][int]$GatewayAttempts=4)
    if ($State.failure -or $State.phase -ne 'blocked' -or $State.droppedAcknowledgements -ne 1 -or
        @($State.blockedRetries).Count -lt [Math]::Max(4,$GatewayAttempts) -or $State.committed.bodySHA256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Missing frozen retry boundary.' }
    $previous=$State.committed.elapsedMilliseconds
    if ($null -eq $previous -or $previous -isnot [ValueType] -or
        [double]::IsNaN([double]$previous) -or [double]::IsInfinity([double]$previous) -or $previous -lt 0) { throw 'Invalid monotonic commit timestamp.' }
    foreach ($retry in $State.blockedRetries) {
        $elapsed=$retry.elapsedMilliseconds
        if ($null -eq $elapsed -or $elapsed -isnot [ValueType] -or
            [double]::IsNaN([double]$elapsed) -or [double]::IsInfinity([double]$elapsed) -or $elapsed -lt $previous -or
            $retry.requestId -ne $State.committed.requestId -or $retry.bodySHA256 -cne $State.committed.bodySHA256) { throw 'Invalid monotonic/frozen retry observation.' }
        $previous=$elapsed
    }
    $timerIndex=$GatewayAttempts-1
    $beforeTimer=if ($timerIndex -eq 0) { [double]$State.committed.elapsedMilliseconds } else { [double]$State.blockedRetries[$timerIndex-1].elapsedMilliseconds }
    $gap=([double]$State.blockedRetries[$timerIndex].elapsedMilliseconds-$beforeTimer)/1000.0
    if ($gap -lt 1.5) { throw 'Retry did not cross the native grant timer boundary.' }
    return $gap
}

function Get-NPCPickupPartyObservations {
    param([string[]]$ServerRows, [object[]]$Clients, [switch]$UnknownAck)
    $guid='[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}'
    $zero='00000000-0000-0000-0000-000000000000'
    if ($UnknownAck) {
        if ($ServerRows.Count -ne 4 -or $Clients.Count -ne 2 -or @($Clients | Where-Object {$_.rows.Count -ne 5}).Count) { throw 'Wrong recovery observation count.' }
        $uncertain=[regex]::Match($ServerRows[2], '^RPG_NPC_PICKUP SERVER_UNCERTAIN Pickup=([1-9]\d*) Pending=1 Available=0 Retired=0 TotalItems=0 GatewayAttempts=([1-8])$')
        if (-not $uncertain.Success) { throw 'Missing native pending/empty projection boundary.' }
        $basicClients=@($Clients | ForEach-Object { [pscustomobject]@{characterId=$_.characterId;rows=@($_.rows[0],$_.rows[1],$_.rows[4])} })
        $observed=Get-NPCPickupPartyObservations @($ServerRows[0],$ServerRows[1],$ServerRows[3]) $basicClients
        if ($uncertain.Groups[1].Value -ne $observed.pickupNetId) { throw 'Borrowed uncertainty pickup.' }
        $seenPending=[Collections.Generic.HashSet[string]]::new()
        $recovered=0
        foreach ($client in $Clients) {
            $claimant=@($observed.claimants | Where-Object characterId -eq $client.characterId)[0]
            foreach ($row in @($client.rows[2],$client.rows[3])) {
                $pending=[regex]::Match($row, "^RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=([1-9]\d*) Character=($guid) Request=($guid) Retired=0 TotalItems=0$")
                $recovery=[regex]::Match($row, "^RPG_NPC_PICKUP CLIENT_RECOVERED Pickup=([1-9]\d*) Request=($guid) Code=AlreadyApplied$")
                if ($pending.Success) {
                    $id=$pending.Groups[3].Value
                    if ($pending.Groups[1].Value -ne $observed.pickupNetId -or $pending.Groups[2].Value -cne $client.characterId -or
                        $id -notin $claimant.rejectedRequestIds -or -not $seenPending.Add($id)) { throw 'Borrowed/duplicate pending rejection.' }
                } elseif ($recovery.Success) {
                    if (-not $claimant.won -or $recovery.Groups[1].Value -ne $observed.pickupNetId -or $recovery.Groups[2].Value -ne $observed.requestId) { throw 'Borrowed recovered request.' }
                    $recovered++
                } else { throw 'Malformed recovery observation.' }
            }
        }
        if ($seenPending.Count -ne 3 -or $recovered -ne 1) { throw 'Missing/multiple recovery acknowledgements.' }
        $observed | Add-Member -NotePropertyName gatewayAttempts -NotePropertyValue ([int]$uncertain.Groups[2].Value)
        return $observed
    }
    if ($ServerRows.Count -ne 3 -or $Clients.Count -lt 2 -or $Clients.Count -gt 4) { throw 'Wrong party observation count.' }
    $death=[regex]::Match($ServerRows[0], '^RPG_NPC_PICKUP SERVER_DEATH NPC=([1-9]\d*) Deaths=1 Authenticated=1 NativeSource=1$')
    $drop=[regex]::Match($ServerRows[1], '^RPG_NPC_PICKUP SERVER_DROP NPC=([1-9]\d*) Pickup=([1-9]\d*) Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1$')
    $commit=[regex]::Match($ServerRows[2], "^RPG_NPC_PICKUP SERVER_COMMIT Pickup=([1-9]\d*) Item=($guid) Definition=([A-Za-z0-9_.]+) Quantity=1 Revision=1 Retired=1 Owner=($guid) Party=([2-4]) TotalItems=1$")
    if (-not $death.Success -or -not $drop.Success -or -not $commit.Success -or
        $death.Groups[1].Value -ne $drop.Groups[1].Value -or $drop.Groups[2].Value -ne $commit.Groups[1].Value -or
        [int]$commit.Groups[5].Value -ne $Clients.Count -or $commit.Groups[2].Value -eq $zero) { throw 'Malformed party server observations.' }
    $seenCharacters=[Collections.Generic.HashSet[string]]::new()
    $seenRequests=[Collections.Generic.HashSet[string]]::new()
    $rejected=[Collections.Generic.List[string]]::new()
    $claimants=@(foreach ($client in $Clients) {
        $character=[string]$client.characterId
        $rows=@($client.rows)
        if ($character -cnotmatch "^$guid`$" -or $character -eq $zero -or -not $seenCharacters.Add($character) -or $rows.Count -ne 3) { throw 'Duplicate/malformed claimant identity.' }
        $ready=[regex]::Match($rows[0], "^RPG_NPC_PICKUP CLIENT_READY Pickup=([1-9]\d*) Character=($guid) Available=1 Empty=1 InRange=1$")
        $send=[regex]::Match($rows[1], "^RPG_NPC_PICKUP CLIENT_SEND Pickup=([1-9]\d*) Requests=2 ExistingRPC=1 Character=($guid)$")
        $done=[regex]::Match($rows[2], "^RPG_NPC_PICKUP CLIENT_PARTY_COMPLETE Pickup=([1-9]\d*) Character=($guid) Success=([01]) Rejected=([12]) Request=($guid) Reject1=($guid) Reject2=($guid) Item=($guid) Quantity=([01]) Revision=([01]) Retired=1$")
        if (-not $ready.Success -or -not $send.Success -or -not $done.Success -or
            $ready.Groups[1].Value -ne $drop.Groups[2].Value -or $send.Groups[1].Value -ne $drop.Groups[2].Value -or $done.Groups[1].Value -ne $drop.Groups[2].Value -or
            $ready.Groups[2].Value -cne $character -or $send.Groups[2].Value -cne $character -or $done.Groups[2].Value -cne $character) { throw 'Borrowed/malformed claimant observations.' }
        $won=$done.Groups[3].Value -eq '1'
        $requests=@($done.Groups[6].Value)
        if ($won) {
            if ($done.Groups[4].Value -ne '1' -or $done.Groups[5].Value -eq $zero -or $done.Groups[7].Value -ne $zero -or
                $done.Groups[8].Value -cne $commit.Groups[2].Value -or $character -cne $commit.Groups[4].Value -or
                $done.Groups[9].Value -ne '1' -or $done.Groups[10].Value -ne '1') { throw 'Winner record/owner mismatch.' }
            $requests += $done.Groups[5].Value
        } else {
            if ($done.Groups[4].Value -ne '2' -or $done.Groups[5].Value -ne $zero -or $done.Groups[8].Value -ne $zero -or
                $done.Groups[9].Value -ne '0' -or $done.Groups[10].Value -ne '0') { throw 'Loser projection leaked or incomplete rejection.' }
            $requests += $done.Groups[7].Value
            $rejected.Add($done.Groups[7].Value)
        }
        $rejected.Add($done.Groups[6].Value)
        foreach ($request in $requests) { if ($request -eq $zero -or -not $seenRequests.Add($request)) { throw 'Borrowed/duplicate command identity.' } }
        [pscustomobject]@{characterId=$character;won=$won;requestId=$done.Groups[5].Value;rejectedRequestIds=@($requests | Where-Object {$_ -ne $done.Groups[5].Value})}
    })
    $winner=@($claimants | Where-Object won)
    if ($winner.Count -ne 1 -or $rejected.Count -ne ($Clients.Count * 2 - 1) -or $seenRequests.Count -ne ($Clients.Count * 2)) { throw 'Multiple/missing winners or requests.' }
    [pscustomobject]@{npcNetId=$death.Groups[1].Value;pickupNetId=$drop.Groups[2].Value;itemId=$commit.Groups[2].Value;
        definition=$commit.Groups[3].Value;winnerCharacterId=$winner[0].characterId;requestId=$winner[0].requestId;
        rejectedRequestIds=$rejected.ToArray();claimants=$claimants}
}
