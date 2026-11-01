# Invoked only by the owned-process PostgreSQL membership runner. No production
# clock override, direct DB write, synthetic loss report, or abandonment POST.
function Get-DungeonDisconnectDurableState {
    foreach ($id in @($sessionId)+@($members | ForEach-Object CharacterId)) {
        if ($id -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$') { throw 'Noncanonical disconnect fixture UUID.' }
    }
    if ($schema -cnotmatch '^rpg_membership_e2e_[0-9a-f]{32}$') { throw 'Disconnect fixture schema ownership mismatch.' }
    $sql="SELECT json_build_object('members',(SELECT json_agg(row_to_json(m)) FROM (SELECT character_id,steam_id,disconnected_at,connection_generation::text,confirmed_connection_generation::text,disconnected_generation::text FROM $schema.dungeon_session_members WHERE dungeon_session_id='$sessionId' ORDER BY character_id) m),'receipts',(SELECT json_agg(row_to_json(r)) FROM (SELECT character_id,steam_id,disconnected_at,expired_at,processed_at FROM $schema.dungeon_session_disconnection_expulsions WHERE dungeon_session_id='$sessionId') r),'abandonments',(SELECT COUNT(*) FROM $schema.dungeon_session_abandonments WHERE dungeon_session_id='$sessionId'),'leases',(SELECT json_agg(character_id ORDER BY character_id) FROM $schema.character_session_leases WHERE dungeon_session_id='$sessionId'),'targetTickets',(SELECT COUNT(*) FROM $schema.game_join_tickets WHERE dungeon_session_id='$sessionId' AND character_id='$($members[0].CharacterId)'));"
    $rows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
    if ($LASTEXITCODE -ne 0) { throw 'Independent disconnect PostgreSQL read failed.' }
    return ($rows | ConvertFrom-Json)
}

function Start-DungeonUnexpectedDisconnectObservation {
    $initial=Get-DungeonDisconnectDurableState
    $all=@($members | ForEach-Object CharacterId | Sort-Object)
    $remaining=@($members | Select-Object -Skip 1 | ForEach-Object CharacterId | Sort-Object)
    $target=$members[0]
    if (@($initial.members).Count -ne $ClientCount -or @($initial.receipts | Where-Object { $null -ne $_ }).Count -or $initial.abandonments -ne 0 -or
        (Compare-Object $all @($initial.leases | Sort-Object)) -or
        @($initial.members | Where-Object { $_.disconnected_at -or [long]$_.connection_generation -le 0 -or
            $_.connection_generation -cne $_.confirmed_connection_generation }).Count) { throw 'Party was not fully confirmed before unexpected loss.' }
    $original=@($initial.members | Where-Object character_id -eq $target.CharacterId)[0]
    $target.Process.Refresh()
    if ($target.Process.HasExited -or -not $processes.Contains($target.Process)) { throw 'Loss target is not a live process owned by this run.' }
    # Exact retained Process handle, not a PID lookup or an editor/server-wide stop.
    $script:expectedExitedProcess=$target.Process
    $exitAt=[DateTimeOffset]::UtcNow
    $target.Process.Kill()
    if (-not $target.Process.WaitForExit(10000)) { throw 'Owned client loss fixture did not exit.' }
    Write-Host "Unexpected loss: terminated only owned client PID $($target.Process.Id); waiting for real server observation."
    $reportDeadline=[DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Assert-Healthy
        $state=Get-DungeonDisconnectDurableState
        $reported=@($state.members | Where-Object { $_.character_id -eq $target.CharacterId -and $_.disconnected_at })
        if ($reported.Count -eq 1) { break }
        Start-Sleep -Seconds 2
    } while ([DateTimeOffset]::UtcNow -lt $reportDeadline)
    if ($reported.Count -ne 1) { throw 'Actual socket cleanup never produced a durable disconnect report.' }
    $disconnectedAt=[DateTimeOffset]::Parse($reported[0].disconnected_at)
    if ($disconnectedAt -lt $exitAt.AddSeconds(-1) -or $disconnectedAt -gt [DateTimeOffset]::UtcNow.AddSeconds(5) -or
        $reported[0].connection_generation -cne $original.connection_generation -or
        $reported[0].disconnected_generation -cne $original.connection_generation) { throw 'Disconnect observation/generation does not match the lost socket.' }
    Wait-Marker $serverLog 'DUNGEON_DISCONNECT_SETTLEMENT SOCKET_LOST '
    [ordered]@{processId=$target.Process.Id;processExitAtUtc=$exitAt.ToString('o');
        originalGeneration=$original.connection_generation;disconnectedAt=$disconnectedAt.ToString('o');
        initialMembers=$initial.members;reportedMembers=$state.members} | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $runRoot 'DisconnectObservation.proof.json') -Encoding UTF8
    return @{initial=$initial;original=$original;exitAt=$exitAt;disconnectedAt=$disconnectedAt}
}

function Invoke-DungeonUnexpectedDisconnectScenario {
    $receipt=$null
    $observation=Start-DungeonUnexpectedDisconnectObservation
    if ($ObservePartyCombatExpiry) {
        Wait-PartyHudRenderedState 'client-2' 'grace' $members[1].PublicName $members[0].PublicName
    }
    $initial=$observation.initial; $original=$observation.original
    $exitAt=$observation.exitAt; $disconnectedAt=$observation.disconnectedAt
    $all=@($members | ForEach-Object CharacterId | Sort-Object)
    $remaining=@($members | Select-Object -Skip 1 | ForEach-Object CharacterId | Sort-Object)
    $target=$members[0]
    $expiryAt=$disconnectedAt.AddMinutes(10)
    $expiryDeadline=$expiryAt.AddSeconds(90)
    $nextProgress=[DateTimeOffset]::MinValue
    $samples=0
    $lastGraceSample=$null
    $nearExpiryHudCaptured=$false
    do {
        Assert-Healthy
        $state=Get-DungeonDisconnectDurableState
        $sampleAt=[DateTimeOffset]::UtcNow
        if ($state.abandonments -ne 0) { throw 'Unexpected loss was converted into explicit abandonment.' }
        foreach ($peer in @($initial.members | Where-Object character_id -ne $target.CharacterId)) {
            $current=@($state.members | Where-Object character_id -eq $peer.character_id)
            if ($current.Count -ne 1 -or $current[0].steam_id -cne $peer.steam_id -or $current[0].disconnected_at -or
                $current[0].connection_generation -cne $peer.connection_generation -or
                $current[0].confirmed_connection_generation -cne $peer.confirmed_connection_generation) { throw 'Remaining party membership/generation was changed.' }
        }
        $receipts=@($state.receipts | Where-Object { $null -ne $_ })
        if ($receipts.Count) {
            if ($receipts.Count -ne 1) { throw 'Unexpected number of disconnect expulsions.' }
            $receipt=$receipts[0]
            if ($sampleAt -lt $expiryAt -or $receipt.character_id -ne $target.CharacterId -or $receipt.steam_id -cne $target.SteamId -or
                [DateTimeOffset]::Parse($receipt.disconnected_at) -ne $disconnectedAt -or
                [DateTimeOffset]::Parse($receipt.expired_at) -ne $expiryAt -or
                [DateTimeOffset]::Parse($receipt.processed_at) -lt $expiryAt -or
                @($state.members).Count -ne $ClientCount-1 -or (Compare-Object $remaining @($state.leases | Sort-Object)) -or
                $state.targetTickets -ne 0) { throw 'Wrong, early, or nonlocal disconnect expulsion.' }
            break
        }
        $currentTarget=@($state.members | Where-Object character_id -eq $target.CharacterId)
        if ($currentTarget.Count -ne 1 -or [DateTimeOffset]::Parse($currentTarget[0].disconnected_at) -ne $disconnectedAt -or
            $currentTarget[0].connection_generation -cne $original.connection_generation -or
            $currentTarget[0].disconnected_generation -cne $original.connection_generation -or
            (Compare-Object $all @($state.leases | Sort-Object))) { throw 'Grace membership, original observation, or lease was lost before its receipt.' }
        if ($sampleAt -lt $expiryAt) { $samples++; $lastGraceSample=$sampleAt }
        if ($ObservePartyCombatExpiry -and -not $nearExpiryHudCaptured -and
            $sampleAt -ge $expiryAt.AddSeconds(-20) -and $sampleAt -lt $expiryAt) {
            Wait-PartyHudRenderedState 'client-2' 'grace-near-expiry' $members[1].PublicName $members[0].PublicName -MaximumWaitSeconds 5
            $nearExpiryHudCaptured=$true
        }
        if ($sampleAt -ge $nextProgress) {
            Write-Host "Real-clock disconnect grace: $([int][Math]::Max(0,($expiryAt-$sampleAt).TotalSeconds)) seconds remaining; party and original generation retained."
            $nextProgress=$sampleAt.AddSeconds(45)
        }
        Start-Sleep -Seconds 5
    } while ([DateTimeOffset]::UtcNow -lt $expiryDeadline)
    if (-not $receipt -or $samples -lt 2 -or $null -eq $lastGraceSample -or $lastGraceSample -lt $expiryAt.AddSeconds(-15) -or
        ($ObservePartyCombatExpiry -and -not $nearExpiryHudCaptured)) {
        throw 'Missing naturally elapsed ten-minute grace/immutable expiry evidence.'
    }
    Wait-Marker $serverLog 'DUNGEON_DISCONNECT_SETTLEMENT EXPIRY_CACHED '
    $snapshot=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $serverHeaders
    $httpReceipt=@($snapshot.disconnectionExpulsions)
    if ($snapshot.dungeonSessionId -ne $sessionId -or $snapshot.serverId -cne $serverId -or $snapshot.state -ne 'InProgress' -or
        @($snapshot.abandonments).Count -or $httpReceipt.Count -ne 1 -or
        $httpReceipt[0].characterId -ne $target.CharacterId -or $httpReceipt[0].steamId -cne $target.SteamId -or
        -not $httpReceipt[0].rewardExcluded -or [DateTimeOffset]::Parse($httpReceipt[0].disconnectedAt) -ne $disconnectedAt -or
        [DateTimeOffset]::Parse($httpReceipt[0].expiredAt) -ne $expiryAt -or
        [DateTimeOffset]::Parse($httpReceipt[0].processedAt) -ne [DateTimeOffset]::Parse($receipt.processed_at) -or
        (Compare-Object $remaining @($snapshot.members | ForEach-Object characterId | Sort-Object))) { throw 'HTTP receipt did not match durable expiry.' }
    $null=Invoke-Api 'join-tickets' 'Post' $target.Headers @{dungeonSessionId=$sessionId;characterId=$target.CharacterId} 409
    $null=Invoke-Api "dungeon-sessions/$sessionId/members" 'Post' $target.Headers @{characterId=$target.CharacterId} 409
    return [ordered]@{ownedClientProcessKilled=$true;clientProcessId=$target.Process.Id;processExitAtUtc=$exitAt.ToString('o');
        originalGeneration=$original.connection_generation;disconnectedAt=$disconnectedAt.ToString('o');
        expiresAt=$expiryAt.ToString('o');lastGraceSampleAt=$lastGraceSample.ToString('o');graceSamples=$samples;
        naturalTenMinuteWait=$true;clockOverride=$false;databaseWrites=$false;syntheticReport=$false;playerAbandonment=$false;
        harnessExpiryPost=$false;durableReceipt=$receipt;httpReceipt=$httpReceipt[0];
        freshTicketRejected409=$true;partyRejoinRejected409=$true}
}
