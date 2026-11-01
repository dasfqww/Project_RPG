# Shares the real owned-client loss observation; does not synthesize current
# generation reports, admission confirmations, expiry receipts or database rows.
function Invoke-DungeonReconnectWithinGraceScenario {
    $observation=Start-DungeonUnexpectedDisconnectObservation
    $target=$members[0]
    if ($ObservePartyCombatHud) {
        Wait-PartyHudRenderedState 'client-2' 'grace' $members[1].PublicName $members[0].PublicName
    }
    $all=@($members | ForEach-Object CharacterId | Sort-Object)
    $expiryAt=$observation.disconnectedAt.AddMinutes(10)
    $originalGeneration=[long]$observation.original.connection_generation
    if ($originalGeneration -eq [long]::MaxValue) { throw 'Fixture has no next connection generation.' }
    $expectedGeneration=$originalGeneration+1
    $pendingLoss=$null
    if ($VerifyReconnectAdmissionLoss) {
        $pendingLoss=Invoke-DungeonPendingReconnectLoss $observation
        $expectedGeneration++
        if ($VerifyReconnectConfirmationResponseLoss) { $expiryAt=[DateTimeOffset]::Parse($pendingLoss.disconnectedAt).AddMinutes(10) }
    }
    $script:reconnectedClientLog=Start-DungeonReconnectClient
    Wait-Marker $reconnectedClientLog 'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1'
    Wait-Marker $serverLog 'DUNGEON_RECONNECT_SETTLEMENT RECONNECTED '
    if ($ObservePartyCombatHud) {
        Wait-PartyHudRenderedState 'client-2' 'reconnected' $members[1].PublicName
        Wait-PartyHudRenderedState 'client-reconnect' 'reconnected' $members[0].PublicName
    }
    $confirmedAt=[DateTimeOffset]::UtcNow
    if ($confirmedAt -ge $expiryAt) { throw 'Actual reconnect did not complete inside the effective grace period.' }
    $before=Get-DungeonDisconnectDurableState
    $current=@($before.members | Where-Object character_id -eq $target.CharacterId)
    if ($current.Count -ne 1 -or $current[0].steam_id -cne $target.SteamId -or $current[0].disconnected_at -or
        [long]$current[0].connection_generation -ne $expectedGeneration -or
        $current[0].connection_generation -cne $current[0].confirmed_connection_generation -or
        @($before.members).Count -ne $ClientCount -or (Compare-Object $all @($before.leases | Sort-Object)) -or
        @($before.receipts | Where-Object { $null -ne $_ }).Count -or $before.abandonments -ne 0) {
        throw 'New actual admission did not clear the disconnect while preserving the full party.'
    }
    foreach ($peer in @($observation.initial.members | Where-Object character_id -ne $target.CharacterId)) {
        $preserved=@($before.members | Where-Object character_id -eq $peer.character_id)
        if ($preserved.Count -ne 1 -or ($preserved[0] | ConvertTo-Json -Compress) -cne ($peer | ConvertTo-Json -Compress)) {
            throw 'Another member was changed by reconnect.'
        }
    }
    # Deliberate stale old-socket HTTP probes only. The new generation's successful
    # confirmation came from the UE server above, not from this harness.
    $path="dungeon-sessions/$sessionId/members/$($target.CharacterId)"
    $null=Invoke-Api "$path/disconnection" 'Post' $serverHeaders @{
        steamId=$target.SteamId;connectionGeneration=$originalGeneration;observedAtUtc=$observation.disconnectedAt.ToString('o')} 403
    $null=Invoke-Api "$path/admission-confirmation" 'Post' $serverHeaders @{
        steamId=$target.SteamId;connectionGeneration=$originalGeneration} 403
    for ($sample=0;$sample -lt 3;$sample++) {
        Assert-Healthy
        $after=Get-DungeonDisconnectDurableState
        if (($before.members | ConvertTo-Json -Depth 8 -Compress) -cne ($after.members | ConvertTo-Json -Depth 8 -Compress) -or
            @($after.receipts | Where-Object { $null -ne $_ }).Count -or $after.abandonments -ne 0 -or
            (Compare-Object $all @($after.leases | Sort-Object))) { throw 'A late original-generation callback undid recovered membership.' }
        if ($sample -lt 2) { Start-Sleep -Seconds 3 }
    }
    Write-Host "Reconnected inside grace: generation $originalGeneration -> $expectedGeneration; stale loss/confirmation rejected, full party retained."
    return [ordered]@{ownedClientProcessKilled=$true;originalProcessId=$target.Process.Id;
        processExitAtUtc=$observation.exitAt.ToString('o');disconnectedAt=$observation.disconnectedAt.ToString('o');
        originalExpiresAt=$observation.disconnectedAt.AddMinutes(10).ToString('o');effectiveExpiresAt=$expiryAt.ToString('o');
        reconfirmedAtUtc=$confirmedAt.ToString('o');
        originalGeneration=$originalGeneration.ToString();newGeneration=$expectedGeneration.ToString();
        replacementClientLog=$reconnectedClientLog;actualFreshTicketAdmission=$true;
        currentConfirmationFromUnrealServer=$true;staleReportRejected403=$true;staleConfirmationRejected403=$true;
        partyRetained=$true;expulsionCount=0;playerAbandonment=$false;clockOverride=$false;databaseWrites=$false;
        pendingAdmissionLoss=$pendingLoss;recoveredMembers=$after.members}
}
