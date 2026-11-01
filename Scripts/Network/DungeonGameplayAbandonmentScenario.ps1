# Private-schema reads and file barriers. The optional auth fixture expires only
# this run's target account; it never edits membership, receipts or reward rows.
function Start-DungeonGameplayAbandonmentProxy {
    $controlToken=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N')
    $proxyOut=Join-Path $runRoot 'gameplay-proxy.stdout.log'
    $proxyErr=Join-Path $runRoot 'gameplay-proxy.stderr.log'
    try {
        $env:PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN=$controlToken
        $env:PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT=[string]$uri.Port
        $env:PROJECT_RPG_ABANDON_PROXY_TRANSPARENT='1'
        $proxy=Start-Process -FilePath (Get-Command node -ErrorAction Stop).Source -ArgumentList ('"'+$abandonProxyScript+'"') `
            -WorkingDirectory $projectRoot -RedirectStandardOutput $proxyOut -RedirectStandardError $proxyErr -PassThru -WindowStyle Hidden
    } finally {
        foreach ($name in @('PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN','PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT','PROJECT_RPG_ABANDON_PROXY_TRANSPARENT')) {
            [Environment]::SetEnvironmentVariable($name,$previousEnv[$name],'Process')
        }
    }
    $processes.Add($proxy); $logs.Add($proxyOut); $logs.Add($proxyErr)
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    $ready=[regex]::Match('','ABANDON_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)')
    do {
        Assert-Healthy
        $proxyText=[string](Get-Content -LiteralPath $proxyOut -Raw -ErrorAction SilentlyContinue)
        $ready=[regex]::Match($proxyText,'ABANDON_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)')
        if ($ready.Success) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $ready.Success) { throw 'Combat abandonment proxy did not start.' }
    $script:gameplayControlUrl="http://127.0.0.1:$($ready.Groups[1].Value)"
    $script:gameplayProxyUrl="http://127.0.0.1:$($ready.Groups[2].Value)"
    $script:gameplayControlHeaders=@{Authorization="Bearer $controlToken"}
    $armed=Invoke-RestMethod -Method Post -Uri "$gameplayControlUrl/arm" -Headers $gameplayControlHeaders -ContentType 'application/json' `
        -Body (@{sessionId=$sessionId;characterId=$members[0].CharacterId;authorizationSHA256=$null} | ConvertTo-Json -Compress)
    if ($armed.phase -cne 'armed') { throw 'Combat abandonment proxy scope not armed.' }
}
function Read-DungeonGameplayAbandonmentProxy {
    $state=Invoke-RestMethod -Uri "$gameplayControlUrl/state" -Headers $gameplayControlHeaders
    if ($state.failure) { throw 'Combat abandonment fault proxy reported a failure.' }
    return $state
}
function Get-DungeonGameplayConsentDurableState {
    if ($schema -cnotmatch '^rpg_membership_e2e_[0-9a-f]{32}$' -or $sessionId -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$') { throw 'Combat consent fixture scope mismatch.' }
    $sql="SELECT json_build_object('state',(SELECT state FROM $schema.dungeon_sessions WHERE dungeon_session_id='$sessionId'),'members',(SELECT json_agg(row_to_json(m)) FROM (SELECT character_id,steam_id,connection_generation::text,confirmed_connection_generation::text,disconnected_generation::text,disconnected_at FROM $schema.dungeon_session_members WHERE dungeon_session_id='$sessionId' ORDER BY character_id) m),'leases',(SELECT json_agg(character_id ORDER BY character_id) FROM $schema.character_session_leases WHERE dungeon_session_id='$sessionId'),'abandonments',(SELECT COUNT(*) FROM $schema.dungeon_session_abandonments WHERE dungeon_session_id='$sessionId'),'expulsions',(SELECT COUNT(*) FROM $schema.dungeon_session_disconnection_expulsions WHERE dungeon_session_id='$sessionId'),'settlements',(SELECT COUNT(*) FROM $schema.dungeon_reward_settlements WHERE dungeon_session_id='$sessionId'));"
    $rows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
    if ($LASTEXITCODE -ne 0) { throw 'Independent combat consent SQL read failed.' }
    return ($rows | ConvertFrom-Json)
}
function Invoke-DungeonGameplayAbandonmentScenario {
    $before=Get-DungeonGameplayConsentDurableState
    $all=@($members | ForEach-Object CharacterId | Sort-Object)
    if ($before.state -cne 'InProgress' -or @($before.members).Count -ne 2 -or $before.abandonments -ne 0 -or
        $before.expulsions -ne 0 -or $before.settlements -ne 0 -or (Compare-Object $all @($before.leases | Sort-Object)) -or
        @($before.members | Where-Object { $_.disconnected_at -or [long]$_.connection_generation -le 0 -or $_.connection_generation -cne $_.confirmed_connection_generation }).Count) { throw 'Combat consent did not start with the actual confirmed party.' }
    New-Item -ItemType File -Path $gameplayConsentGate | Out-Null
    Wait-Marker $members[0].Log 'DUNGEON_GAMEPLAY_ABANDON CANCELLED '
    $cancelled=Get-DungeonGameplayConsentDurableState
    if (($before | ConvertTo-Json -Depth 8 -Compress) -cne ($cancelled | ConvertTo-Json -Depth 8 -Compress)) { throw 'Cancelling the live confirmation changed durable party/reward eligibility.' }
    [ordered]@{passed=$true;scope='Read-only cancellation checkpoint before confirmation';before=$before;after=$cancelled} |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $runRoot 'GameplayConsentCancel.proof.json') -Encoding UTF8
    New-Item -ItemType File -Path $gameplayConfirmGate | Out-Null
    Wait-Marker $members[0].Log 'DUNGEON_GAMEPLAY_ABANDON CONFIRMED '
    if ($VerifyGameplayAbandonmentResponseLoss) {
        Wait-Marker $members[0].Log 'DUNGEON_ABANDON_UI UNCONFIRMED IdentityRetained=1 NewDungeonLocked=1 RetryEnabled=1'
        $lost=Read-DungeonGameplayAbandonmentProxy
        # libcurl need not automatically replay a disconnected response. Count
        # observed attempts; require none to reach the backend before release.
        if ($lost.phase -cne 'dropped' -or $lost.forwarded -ne 1 -or $lost.droppedAcknowledgements -ne 1 -or
            $lost.blockedReplays -lt 0 -or $lost.replayForwarded -ne 0 -or $lost.unauthorizedForwarded -ne 0 -or
            $lost.receiptSHA256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Combat first POST response loss/automatic retry hold was not observed.' }
        $lost | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'gameplay-proxy-lost.json') -Encoding UTF8
    } else {
        Wait-Marker $members[0].Log 'DUNGEON_GAMEPLAY_ABANDON RECEIPT '
        Wait-Marker $members[0].Log 'DUNGEON_GAMEPLAY_ABANDON LOBBY_COMPLETE '
    }
    $guid='[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}'
    $text=Get-Content -LiteralPath $members[0].Log -Raw -Encoding UTF8
    $receipt=[regex]::Matches($text,"DUNGEON_GAMEPLAY_ABANDON RECEIPT Session=($guid) Character=($guid) PlayerId=(\d+) AbandonedAt=([^ ]+) State=InProgress RewardExcluded=1 ActualLobby=1 SameProcess=1")
    $admission=[regex]::Matches($text,"DUNGEON_ABANDON_SETTLEMENT ADMITTED Character=$($members[0].CharacterId) Steam=$($members[0].SteamId) PlayerId=(\d+) Session=$sessionId")
    $target=@($before.members | Where-Object character_id -eq $members[0].CharacterId)
    if ($target.Count -ne 1 -or $admission.Count -ne 1) { throw 'Combat consent actual admission scope mismatch.' }
    if ($VerifyGameplayAbandonmentResponseLoss) {
        if ($receipt.Count -ne 0 -or $text -notmatch "DUNGEON_GAMEPLAY_ABANDON LOST_REPLY Session=$sessionId Character=$($members[0].CharacterId) SameProcess=1 ActualLobby=1 IdentityRetained=1 NoImplicitReceipt=1") { throw 'Combat lost response fabricated a receipt or lost the pinned member.' }
    } elseif ($receipt.Count -ne 1 -or $receipt[0].Groups[1].Value -cne $sessionId -or
        $receipt[0].Groups[2].Value -cne $members[0].CharacterId -or $receipt[0].Groups[3].Value -cne $admission[0].Groups[1].Value) { throw 'Live consent receipt scope differs from the actual admitted member.' }
    Wait-Marker $serverLog 'DUNGEON_GAMEPLAY_ABANDON SERVER_CACHED '
    $serverText=Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8
    $serverGeneration=[regex]::Matches($serverText,"DUNGEON_GAMEPLAY_ABANDON SERVER_CACHED Target=$($members[0].CharacterId) Generation=(\d+) Remaining=1 ")
    if ($serverGeneration.Count -ne 1 -or $serverGeneration[0].Groups[1].Value -cne $target[0].connection_generation) { throw 'Server-only connection generation did not match durable admission.' }
    $script:gameplayEvidence=[ordered]@{publicControllerEntry=$true;liveCancelAndConfirm=$true;durableCancelUnchanged=$true;
        actualPostFromUnrealLobby=$true;noPostFromHarnessBeforeReceipt=$true;cancelCheckpoint=$cancelled;
        serverOnlyGenerationMatchesDatabase=$true;connectionGeneration=$target[0].connection_generation;
        enhancedInputMenuRouting=$false;physicalInput=$false;slatePointerInput=$false}
    if ($VerifyGameplayAbandonmentResponseLoss) {
        $script:gameplayEvidence.responseLoss=[ordered]@{upstream200Dropped=$true;automaticReplaysHeld=$lost.blockedReplays;
            automaticTransportReplayObserved=($lost.blockedReplays -gt 0);
            lostReceiptSHA256=$lost.receiptSHA256;unconfirmedUntilPartySettlement=$false;recovered=$false}
        # Read-only server authority observation, NOT a player acknowledgement.
        # The main runner independently checks the exact SQL row before settlement.
        $snapshot=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $serverHeaders
        $observed=@($snapshot.abandonments)
        if ($snapshot.dungeonSessionId -cne $sessionId -or $snapshot.serverId -cne $serverId -or $observed.Count -ne 1 -or
            $observed[0].characterId -cne $members[0].CharacterId -or $observed[0].steamId -cne $members[0].SteamId -or
            $observed[0].rewardExcluded -ne $true -or $observed[0].sessionStateAtAbandonment -cne 'InProgress') { throw 'Lost combat response has no exact authoritative receipt.' }
        return [ordered]@{dungeonSessionId=$sessionId;characterId=$members[0].CharacterId;
            abandonedAt=$observed[0].abandonedAt;sessionStateAtAbandonment='InProgress';rewardExcluded=$true}
    }
    # Fields from the UE's validated real response. The existing runner compares
    # them with an owner HTTP replay and independent membership/receipt SQL next.
    return [ordered]@{dungeonSessionId=$sessionId;characterId=$members[0].CharacterId;
        abandonedAt=$receipt[0].Groups[4].Value;sessionStateAtAbandonment='InProgress';rewardExcluded=$true}
}

function Get-DungeonGameplayRecoveryCheckpoint {
    if ($schema -cnotmatch '^rpg_membership_e2e_[0-9a-f]{32}$' -or
        $sessionId -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$' -or
        $members[0].CharacterId -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$') { throw 'Noncanonical combat recovery scope.' }
    $sql="SELECT json_build_object('state',(SELECT state FROM $schema.dungeon_sessions WHERE dungeon_session_id='$sessionId'),'receipt',(SELECT row_to_json(a) FROM $schema.dungeon_session_abandonments a WHERE dungeon_session_id='$sessionId' AND character_id='$($members[0].CharacterId)'),'settlement',(SELECT row_to_json(s) FROM $schema.dungeon_reward_settlements s WHERE dungeon_session_id='$sessionId'),'members',(SELECT json_agg(json_build_object('characterId',character_id,'steamId',steam_id,'generation',connection_generation::text,'confirmed',confirmed_connection_generation::text,'disconnectedAt',disconnected_at) ORDER BY character_id) FROM $schema.dungeon_session_members WHERE dungeon_session_id='$sessionId'),'leases',(SELECT json_agg(character_id ORDER BY character_id) FROM $schema.character_session_leases WHERE dungeon_session_id='$sessionId'));"
    $rows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
    if ($LASTEXITCODE -ne 0) { throw 'Combat recovery SQL checkpoint failed.' }
    return ($rows | ConvertFrom-Json)
}
function Complete-DungeonGameplayAbandonmentRecovery([object]$ExpectedReceipt) {
    $before=Get-DungeonGameplayRecoveryCheckpoint
    $remaining=@($members | Select-Object -Skip 1 | ForEach-Object CharacterId | Sort-Object)
    $text=[string](Get-Content -LiteralPath $members[0].Log -Raw -Encoding UTF8)
    if ($before.state -cne 'Cleared' -or $before.settlement.state -cne 'Completed' -or
        (Compare-Object $remaining @($before.settlement.character_ids | Sort-Object)) -or
        (Compare-Object $remaining @($before.members | ForEach-Object characterId | Sort-Object)) -or
        -not $before.receipt -or $text -match 'DUNGEON_GAMEPLAY_ABANDON (?:RECEIPT|LOBBY_COMPLETE) ') { throw 'Combat recovery was not held unresolved through completed party settlement.' }
    $expiredCount=0
    if ($VerifyGameplayAbandonmentReauthentication) {
        $fixtureSteam=[string]$members[0].SteamId
        if ($fixtureSteam -cnotmatch '^[0-9]{17}$') { throw 'Invalid owned combat recovery account.' }
        $sql="WITH expired AS (UPDATE $schema.auth_sessions SET expires_at=CURRENT_TIMESTAMP - INTERVAL '1 second' WHERE steam_id='$fixtureSteam' AND expires_at>CURRENT_TIMESTAMP RETURNING 1) SELECT COUNT(*) FROM expired;"
        $rows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
        if ($LASTEXITCODE -ne 0 -or @($rows).Count -ne 1 -or [string](@($rows)[0]) -cnotmatch '^[0-9]+$') { throw 'Owned combat account auth expiry fixture failed.' }
        $expiredCount=[int](@($rows)[0])
        if ($expiredCount -lt 2) { throw 'UE and independent harness auth were not both expired.' }
    }
    $released=Invoke-RestMethod -Method Post -Uri "$gameplayControlUrl/release-replay" -Headers $gameplayControlHeaders
    if (-not $released.replayReleased -or $released.replayForwarded -ne 0) { throw 'Combat manual replay release mismatch.' }
    New-Item -ItemType File -Path $lobbyRetryGate | Out-Null
    Wait-Marker $members[0].Log 'DUNGEON_ABANDON_UI RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1'
    if ($VerifyGameplayAbandonmentReauthentication) {
        Wait-Marker $members[0].Log 'DUNGEON_ABANDON_UI AUTH_EXPIRED Real401=1 AccountPinned=1 IdentityRetained=1 NewDungeonLocked=1 LiveLogin=1'
        $after401=Read-DungeonGameplayAbandonmentProxy
        if ($after401.unauthorizedForwarded -ne 1 -or $after401.replayForwarded -ne 0) { throw 'Combat retry did not observe exactly one real backend 401.' }
        $auth=Invoke-Api 'auth/steam-ticket' 'Post' @{} @{ticket="dev:$fixtureSteam"}
        if ([string]::IsNullOrWhiteSpace([string]$auth.accessToken)) { throw 'Independent combat harness auth renewal failed.' }
        $members[0].Headers=@{Authorization="Bearer $($auth.accessToken)"} # never injected into UE
        Wait-Marker $members[0].Log 'DUNGEON_ABANDON_UI AUTH_RESTORED SameAccount=1 OwnedRoster=1 IdentityRetained=1 RetryEnabled=1'
        New-Item -ItemType File -Path $lobbyRenewedRetryGate | Out-Null
        Wait-Marker $members[0].Log 'DUNGEON_ABANDON_UI RENEWED_RETRY_SUBMITTED LiveConfirmation=1 SameIdentity=1'
    }
    Wait-Marker $members[0].Log 'DUNGEON_ABANDON_UI RECOVERED LiveButton=1 ReceiptApplied=1 SelectionRetained=1 NewDungeonEnabled=1'
    Wait-Marker $members[0].Log 'DUNGEON_GAMEPLAY_ABANDON LOBBY_COMPLETE '
    $state=Read-DungeonGameplayAbandonmentProxy
    if ($state.forwarded -ne 1 -or $state.droppedAcknowledgements -ne 1 -or $state.replayForwarded -ne 1 -or
        $state.unauthorizedForwarded -ne [int][bool]$VerifyGameplayAbandonmentReauthentication -or
        $state.receiptSHA256 -cne $gameplayEvidence.responseLoss.lostReceiptSHA256 -or
        $state.replayReceiptSHA256 -cne $state.receiptSHA256) { throw 'Combat recovery did not return the exact immutable receipt once.' }
    $after=Get-DungeonGameplayRecoveryCheckpoint
    if (($before | ConvertTo-Json -Depth 20 -Compress) -cne ($after | ConvertTo-Json -Depth 20 -Compress)) { throw 'Combat recovery altered settled recipients or the durable receipt.' }
    $text=[string](Get-Content -LiteralPath $members[0].Log -Raw -Encoding UTF8)
    $receipt=[regex]::Matches($text,"DUNGEON_GAMEPLAY_ABANDON RECEIPT Session=$sessionId Character=$($members[0].CharacterId) PlayerId=(\d+) AbandonedAt=([^ ]+) State=InProgress RewardExcluded=1 ActualLobby=1 SameProcess=1")
    $admission=[regex]::Matches($text,"DUNGEON_ABANDON_SETTLEMENT ADMITTED Character=$($members[0].CharacterId) Steam=$($members[0].SteamId) PlayerId=(\d+) Session=$sessionId")
    if ($receipt.Count -ne 1 -or $admission.Count -ne 1 -or $receipt[0].Groups[1].Value -cne $admission[0].Groups[1].Value -or
        $receipt[0].Groups[2].Value -cne $ExpectedReceipt.abandonedAt) { throw 'Recovered combat UE receipt differs from the original committed member/time.' }
    $gameplayEvidence.responseLoss.unconfirmedUntilPartySettlement=$true
    $gameplayEvidence.responseLoss.recovered=$true
    $gameplayEvidence.responseLoss.replayedReceiptSHA256=$state.replayReceiptSHA256
    $gameplayEvidence.responseLoss.durableReceiptAndSettlementUnchanged=$true
    $gameplayEvidence.responseLoss.reauthentication=[ordered]@{enabled=[bool]$VerifyGameplayAbandonmentReauthentication;
        expiredOwnedSessions=$expiredCount;backend401Forwarded=$state.unauthorizedForwarded;
        sameAccountLiveLoginAndRoster=[bool]$VerifyGameplayAbandonmentReauthentication;noCredentialInjection=$true}
    [ordered]@{passed=$true;before=$before;after=$after;proxy=$state} | ConvertTo-Json -Depth 22 |
        Set-Content -LiteralPath (Join-Path $runRoot 'GameplayRecovery.proof.json') -Encoding UTF8
}
