function Get-DungeonEjectionNetworkProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ServerText,
        [Parameter(Mandatory)][string[]]$ClientTexts,
        [ValidateRange(2,4)][int]$ClientCount,
        [ValidateSet('Ejection','Quarantine')][string]$Scenario = 'Ejection',
        [int]$PacketLagMinMs = 0, [int]$PacketLagMaxMs = 0, [int]$PacketLossPercent = 0,
        [switch]$RequireBackendMembership
    )
    if ($ClientTexts.Count -ne $ClientCount) { throw 'Wrong client log count.' }
    foreach ($log in (@($ServerText) + $ClientTexts)) {
        if ($log -match 'RPG_EJECTION_NET FAILED|Fatal error:|Assertion failed:|Ensure condition failed:') { throw 'Runtime probe failure.' }
    }
    $end = [regex]::Matches($ServerText, 'RPG_EJECTION_NET SERVER_COMPLETE Scenario=(\w+) Players=(\d+) Target=(\d+) Remaining=(\d+) Iris=1 SyntheticReceipt=([01])')
    $remaining = if ($Scenario -eq 'Quarantine') { $ClientCount } else { $ClientCount - 1 }
    if ($end.Count -ne 1 -or $end[0].Groups[1].Value -ne $Scenario -or
        [int]$end[0].Groups[2].Value -ne $ClientCount -or [int]$end[0].Groups[4].Value -ne $remaining) { throw 'Missing or mismatched server completion.' }
    $targetId = [int]$end[0].Groups[3].Value
    $synthetic = [int]$end[0].Groups[5].Value -eq 1
    if ($synthetic -eq [bool]$RequireBackendMembership -or ($RequireBackendMembership -and $Scenario -ne 'Ejection')) { throw 'Receipt source/scope mismatch.' }
    $baseline = [regex]::Matches($ServerText, 'RPG_EJECTION_NET BASELINE PlayerId=(\d+) Moves=(\d+) Distance=([0-9.]+)')
    $ids = @($baseline | ForEach-Object { [int]$_.Groups[1].Value })
    if ($baseline.Count -ne $ClientCount -or @($ids | Select-Object -Unique).Count -ne $ClientCount -or $targetId -notin $ids) { throw 'Missing/duplicate baseline identities.' }
    foreach ($entry in $baseline) {
        if ([int]$entry.Groups[2].Value -lt 20 -or [double]::Parse($entry.Groups[3].Value, [cultureinfo]::InvariantCulture) -lt 200) { throw 'Insufficient pre-action movement/RPC traffic.' }
    }
    $action = [regex]::Matches($ServerText, 'RPG_EJECTION_NET ACTION Scenario=(\w+) PlayerId=(\d+) Revoked=1 ActorsRetained=1 Logouts=0')
    if ($action.Count -ne 1 -or $action[0].Groups[1].Value -ne $Scenario -or [int]$action[0].Groups[2].Value -ne $targetId) { throw 'Incorrect revocation/close ordering.' }
    $survivors = [regex]::Matches($ServerText, 'RPG_EJECTION_NET PEER_PRESERVED PlayerId=(\d+) Moves=(\d+) Distance=([0-9.]+) Authenticated=1 SameConnection=1')
    $survivorIds = @($survivors | ForEach-Object { [int]$_.Groups[1].Value })
    if ($survivors.Count -ne ($ClientCount-1) -or @($survivorIds | Select-Object -Unique).Count -ne ($ClientCount-1)) { throw 'Duplicate/missing surviving peer evidence.' }
    foreach ($entry in $survivors) {
        $id = [int]$entry.Groups[1].Value
        if ($id -eq $targetId -or $id -notin $ids -or [int]$entry.Groups[2].Value -lt 20 -or
            [double]::Parse($entry.Groups[3].Value, [cultureinfo]::InvariantCulture) -lt 200) { throw 'Invalid post-action peer isolation/movement.' }
    }
    $clientIds = @()
    for ($i=0; $i -lt $ClientTexts.Count; ++$i) {
        $log = $ClientTexts[$i]
        $ready = [regex]::Matches($log, 'RPG_EJECTION_NET CLIENT_READY PlayerId=(\d+)')
        if ($ready.Count -ne 1) { throw 'Missing/duplicate client identity.' }
        $id = [int]$ready[0].Groups[1].Value
        if ($id -notin $ids -or $id -in $clientIds) { throw 'Unmatched/duplicate client identity.' }
        $clientIds += $id
        $sim = [regex]::Matches($log, 'RPG_EJECTION_NET SIMULATION Role=Client PlayerId=(\d+) Iris=1 LagMin=(\d+) LagMax=(\d+) Loss=(\d+)')
        if ($sim.Count -ne 1 -or [int]$sim[0].Groups[1].Value -ne $id -or
            [int]$sim[0].Groups[2].Value -ne $PacketLagMinMs -or [int]$sim[0].Groups[3].Value -ne $PacketLagMaxMs -or
            [int]$sim[0].Groups[4].Value -ne $PacketLossPercent) { throw 'Client packet simulation mismatch.' }
        $disconnect = [regex]::Matches($log, 'RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=(\d+) EngineFailure=(\d+) ExpectedCloseType=(\d+)')
        $complete = [regex]::Matches($log, 'RPG_EJECTION_NET CLIENT_COMPLETE PlayerId=(\d+) Revoked=(\d+) Connections=(\d+) Iris=1')
        if ($Scenario -eq 'Ejection' -and $id -eq $targetId) {
            if ($disconnect.Count -ne 1 -or [int]$disconnect[0].Groups[1].Value -ne $id -or
                [int]$disconnect[0].Groups[3].Value -ne 1 -or $complete.Count) { throw 'Target client did not report actual close.' }
        } else {
            $revoked = if ($id -eq $targetId) { 1 } else { 0 }
            if ($disconnect.Count -or $complete.Count -ne 1 -or [int]$complete[0].Groups[1].Value -ne $id -or
                [int]$complete[0].Groups[2].Value -ne $revoked -or [int]$complete[0].Groups[3].Value -ne $remaining) { throw 'Invalid client survival/snapshot evidence.' }
        }
    }
    $serverSim = [regex]::Matches($ServerText, 'RPG_EJECTION_NET SIMULATION Role=Server PlayerId=(\d+) Iris=1 LagMin=(\d+) LagMax=(\d+) Loss=(\d+)')
    $serverSimIds = @($serverSim | ForEach-Object { [int]$_.Groups[1].Value })
    if ($serverSim.Count -ne $ClientCount -or @($serverSimIds | Select-Object -Unique).Count -ne $ClientCount) { throw 'Incomplete server simulation coverage.' }
    foreach ($entry in $serverSim) {
        if ([int]$entry.Groups[1].Value -notin $ids -or [int]$entry.Groups[2].Value -ne $PacketLagMinMs -or
            [int]$entry.Groups[3].Value -ne $PacketLagMaxMs -or [int]$entry.Groups[4].Value -ne $PacketLossPercent) { throw 'Server simulation mismatch.' }
    }
    $packedRpcs = 0
    $backendPeers = @()
    $backendSession = $null
    $backendServer = $null
    if ($RequireBackendMembership) {
        if ($ServerText -match 'RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED') { throw 'Backend run bypassed admission.' }
        $peers = [regex]::Matches($ServerText, 'RPG_EJECTION_NET BACKEND_PEER PlayerId=(\d+) Character=([a-f0-9-]{36}) Steam=(\d{17}) Session=([a-f0-9-]{36}) Authenticated=1 TicketIdentity=1')
        $ready = [regex]::Matches($ServerText, 'RPG_EJECTION_NET BACKEND_READY Session=([a-f0-9-]{36}) Server=([A-Za-z0-9-]+) Character=([a-f0-9-]{36}) Players=(\d+) NoIdentitySeed=1 NoReceiptSeed=1')
        $revoked = [regex]::Matches($ServerText, 'RPG_EJECTION_NET HTTP_REVOCATION PlayerId=(\d+) Session=([a-f0-9-]{36}) Server=([A-Za-z0-9-]+) Character=([a-f0-9-]{36}) ReceiptCache=1 GameThread=1 ConnectionOpen=1 ActorsRetained=1 Logouts=0')
        if ($peers.Count -ne $ClientCount -or $ready.Count -ne 1 -or $revoked.Count -ne 1 -or
            [int]$ready[0].Groups[4].Value -ne $ClientCount -or [int]$revoked[0].Groups[1].Value -ne $targetId -or
            $ready[0].Groups[1].Value -ne $revoked[0].Groups[2].Value -or $ready[0].Groups[2].Value -ne $revoked[0].Groups[3].Value -or
            $ready[0].Groups[3].Value -ne $revoked[0].Groups[4].Value) { throw 'Missing actual backend receipt/notification evidence.' }
        $backendSession=$ready[0].Groups[1].Value; $backendServer=$ready[0].Groups[2].Value
        $peerIds=@($peers | ForEach-Object { [int]$_.Groups[1].Value })
        $characters=@($peers | ForEach-Object { $_.Groups[2].Value })
        $accounts=@($peers | ForEach-Object { $_.Groups[3].Value })
        if (@($peerIds | Select-Object -Unique).Count -ne $ClientCount -or @($characters | Select-Object -Unique).Count -ne $ClientCount -or
            @($accounts | Select-Object -Unique).Count -ne $ClientCount) { throw 'Duplicate admitted backend identity.' }
        foreach ($peer in $peers) {
            if ([int]$peer.Groups[1].Value -notin $ids -or $peer.Groups[4].Value -ne $backendSession -or
                ([int]$peer.Groups[1].Value -eq $targetId -and $peer.Groups[2].Value -ne $ready[0].Groups[3].Value)) { throw 'Backend target/session identity mismatch.' }
            $backendPeers += [ordered]@{playerId=[int]$peer.Groups[1].Value;characterId=$peer.Groups[2].Value;steamId=$peer.Groups[3].Value}
        }
    }
    if ($Scenario -eq 'Quarantine') {
        $quarantine = [regex]::Matches($ServerText, 'RPG_EJECTION_NET QUARANTINE PlayerId=(\d+) PackedRpcs=(\d+) NormalAfter=0 Frozen=1 ConnectionOpen=1')
        if ($quarantine.Count -ne 1 -or [int]$quarantine[0].Groups[1].Value -ne $targetId -or
            [int]$quarantine[0].Groups[2].Value -lt 20 -or $ServerText -match 'RPG_EJECTION_NET CLEANUP') { throw 'No live revoked packed-RPC quarantine proof.' }
        $packedRpcs = [int]$quarantine[0].Groups[2].Value
    } else {
        $cleanup = [regex]::Matches($ServerText, 'RPG_EJECTION_NET CLEANUP PlayerId=(\d+) Logouts=1 PawnRemoved=1 ControllerRemoved=1 Remaining=(\d+) RetryStates=(\d+)')
        if ($cleanup.Count -ne 1 -or [int]$cleanup[0].Groups[1].Value -ne $targetId -or
            [int]$cleanup[0].Groups[2].Value -ne $remaining -or [int]$cleanup[0].Groups[3].Value -ne $remaining -or
            $ServerText -match 'RPG_EJECTION_NET QUARANTINE') { throw 'Missing actual engine cleanup/party bookkeeping.' }
    }
    return [ordered]@{
        schemaVersion=1; scenario=$Scenario; clientCount=$ClientCount; targetPlayerId=$targetId
        actualSockets=$true; irisRuntimeVerified=$true; syntheticReceipt=$synthetic; syntheticIdentities=$synthetic
        nativeMovementInjection=$true; authoredEnhancedInput=$false
        nativePlayerAuthorityRevocation=$true; automaticMembershipPollingEjection=[bool]$RequireBackendMembership
        actualEngineEjectionAndLogout=($Scenario -eq 'Ejection'); actualClientDisconnect=($Scenario -eq 'Ejection')
        livePostRevocationPackedRpcs=$packedRpcs; closedConnectionQueuedDrainObserved=$false
        remainingPartyMovementVerified=$true; clientConnectionSnapshotVerified=$true
        packetLagMinMs=$PacketLagMinMs; packetLagMaxMs=$PacketLagMaxMs; packetLossPercent=$PacketLossPercent
        backendHttp=[bool]$RequireBackendMembership; actualTicketAdmission=[bool]$RequireBackendMembership
        backendSessionId=$backendSession; backendServerId=$backendServer; backendPeers=$backendPeers
        database=$false; steam=$false; shipping=$false; packagedDedicatedServer=$false
    }
}
