# Existing player abandonment API and server membership poller only. No receipt/identity injection.
function Test-InventoryRevocationExpectedNetworkClose {
    param([string]$Log,[string]$Line)
    if(-not $script:inventoryRevocationReceiptVerified -or $Log -cne $clientLogs[0]) { return $false }
    if($Line -match 'Fatal error:|Assertion failed:|Ensure condition failed:') { return $false }
    # Defer the exact server/actor/remaining-party confirmation below. Never allow another client's network error.
    return $Line -match 'LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = FailureReceived, ErrorString = dungeon_member_abandoned, Driver = Name:GameNetDriver' -or
        $Line -match 'LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = ConnectionLost, ErrorString = Your connection to the host has been lost\., Driver = Name:GameNetDriver'
}

function Read-InventoryRevocationReceiptDigest {
    param([string]$Request,[string]$Owner)
    foreach($value in @($Request,$Owner)) {
        $parsed=[Guid]::Empty
        if(-not [Guid]::TryParseExact($value,'D',[ref]$parsed) -or $parsed -eq [Guid]::Empty -or $parsed.ToString('D') -cne $value) {
            throw 'Unsafe inventory revocation receipt identity.'
        }
    }
    $sql="SELECT row_to_json(r)::text FROM public.item_transaction_receipts r WHERE request_id='$Request' AND actor_id='$Owner';"
    $rows=& docker compose -f (Join-Path $projectRoot 'Backend/compose.postgres.yml') exec -T postgres psql -U project_rpg -d project_rpg -At -v ON_ERROR_STOP=1 -c $sql 2>$null
    if($LASTEXITCODE -ne 0) { throw 'Inventory revocation receipt read failed.' }
    $raw=($rows -join '').Trim()
    if(-not $raw.StartsWith('{')) { throw 'Original inventory receipt is missing.' }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($raw)))).Replace('-','') }
    finally { $sha.Dispose() }
}

function Complete-InventoryRecoveryRevocationFixture {
    param([hashtable]$Fixture,[string]$Request,[object]$OriginalProxy,[string]$OriginalActive)
    $script:inventoryRevocationReceiptVerified=$false
    $f=$Fixture; $moveSwapFixture=$f
    Wait-Marker $serverLog "DUNGEON_ABANDON_SETTLEMENT PARTY_READY Session=$sessionId Server=$serverId Target=$($f.Owner) Players=2 Iris=1 NoIdentitySeed=1 NoReceiptSeed=1"
    $databaseBefore=Read-ResultAbandonmentDatabase
    if($databaseBefore.state -cne 'InProgress' -or @($databaseBefore.members).Count -ne 2 -or
        @($databaseBefore.receipts).Count -ne 0 -or @($databaseBefore.settlements).Count -ne 0) { throw 'Unexpected initial revocation membership.' }
    $receiptDigest=Read-InventoryRevocationReceiptDigest $Request $f.Owner
    $other=$members[1]
    $otherBefore=Invoke-Api "items?ownerType=Character&ownerId=$($other.CharacterId)&includeTerminal=false&limit=500" 'Get' $other.Headers
    $otherFrozen=$otherBefore | ConvertTo-Json -Depth 30 -Compress
    $peerLine=(Select-String -LiteralPath $serverLog -Pattern "DUNGEON_ABANDON_SETTLEMENT PEER Character=$($f.Owner) Steam=$($members[0].SteamId) PlayerId=(\d+) Authenticated=1" | Select-Object -Last 1).Line
    if($peerLine -notmatch 'PlayerId=(\d+) Authenticated=1') { throw 'Authenticated target player identity was not observed.' }
    $targetPlayerId=[int]$Matches[1]
    # Finish the input probe while the original request is still locked. Production network failure owns subsequent travel.
    New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-recovery-verified.gate') | Out-Null
    Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI REVOCATION_WAIT Request=$Request Pending=1 OldProjection=1 Locked=1 ManualReplay=0"
    $abandon=Invoke-Api "dungeon-sessions/$sessionId/members/$($f.Owner)/abandon" 'Post' $members[0].Headers
    if($abandon.dungeonSessionId -cne $sessionId -or $abandon.characterId -cne $f.Owner -or
        $abandon.rewardExcluded -isnot [bool] -or -not $abandon.rewardExcluded -or
        $abandon.sessionStateAtAbandonment -cne 'InProgress' -or -not $abandon.abandonedAt) { throw 'Invalid real player abandonment receipt.' }
    $snapshot=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $f.Headers
    $receipts=@($snapshot.abandonments)
    if($snapshot.dungeonSessionId -cne $sessionId -or $snapshot.serverId -cne $serverId -or $snapshot.state -cne 'InProgress' -or
        @($snapshot.members).Count -ne 1 -or $snapshot.members[0].characterId -cne $other.CharacterId -or
        $snapshot.members[0].steamId -cne $other.SteamId -or $receipts.Count -ne 1 -or
        $receipts[0].characterId -cne $f.Owner -or $receipts[0].steamId -cne $members[0].SteamId -or
        $receipts[0].rewardExcluded -isnot [bool] -or -not $receipts[0].rewardExcluded -or
        $receipts[0].abandonedAt -cne $abandon.abandonedAt -or @($snapshot.disconnectionExpulsions).Count -ne 0) {
        throw 'Membership snapshot did not contain the exact immutable abandonment scope.'
    }
    $script:inventoryRevocationReceiptVerified=$true
    Wait-Marker $serverLog "RPG_INVENTORY_RECOVERY_REVOKED Request=$Request WasUncertain=1 Pending=0 Recovery=0 Revoked=1 NoRollback=1"
    Wait-Marker $serverLog "DUNGEON_ABANDON_SETTLEMENT REVOCATION Character=$($f.Owner) Steam=$($members[0].SteamId) PlayerId=$targetPlayerId Revoked=1 GameThread=1 ConnectionOpen=1 ActorsRetained=1"
    Wait-Marker $serverLog "DUNGEON_ABANDON_SETTLEMENT REMAINING_READY Target=$($f.Owner) Remaining=1 TargetActorsRemoved=1 SamePeers=1 SameEncounter=1 InProgress=1"
    Wait-Marker $clientLogs[0] "RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=$targetPlayerId"
    $closed=@(Select-String -LiteralPath $clientLogs[0] -Pattern "RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=$targetPlayerId EngineFailure=\d+ ExpectedCloseType=1")
    if($closed.Count -ne 1) { throw 'Target client did not observe exactly one expected network close.' }
    if(Select-String -LiteralPath $clientLogs[1] -SimpleMatch 'RPG_EJECTION_NET CLIENT_DISCONNECTED' -Quiet) { throw 'Remaining client was disconnected.' }
    # Use the owning player for inventory reads after membership removal. Do not widen server receipt permissions.
    $after=Invoke-Api "items?ownerType=Character&ownerId=$($f.Owner)&includeTerminal=false&limit=500" 'Get' $members[0].Headers
    $otherAfter=Invoke-Api "items?ownerType=Character&ownerId=$($other.CharacterId)&includeTerminal=false&limit=500" 'Get' $other.Headers
    if(($after | ConvertTo-Json -Depth 30 -Compress) -cne $OriginalActive -or
        ($otherAfter | ConvertTo-Json -Depth 30 -Compress) -cne $otherFrozen -or
        (Read-InventoryRevocationReceiptDigest $Request $f.Owner) -cne $receiptDigest -or
        (Read-InventoryMoveSwapCounts) -cne '1|0|1') { throw 'Revocation changed committed inventory, another owner or the original receipt.' }
    $databaseAfter=Read-ResultAbandonmentDatabase
    if($databaseAfter.state -cne 'InProgress' -or @($databaseAfter.members).Count -ne 1 -or
        $databaseAfter.members[0] -cne $other.CharacterId -or @($databaseAfter.receipts).Count -ne 1 -or
        $databaseAfter.receipts[0].character_id -cne $f.Owner -or $databaseAfter.receipts[0].steam_id -cne $members[0].SteamId -or
        @($databaseAfter.settlements).Count -ne 0 -or (Test-Path -LiteralPath $gate)) { throw 'Unexpected remaining-party database state.' }
    $final=Read-InventoryRecoveryProxy
    if($final.forwarded -ne 1 -or $final.droppedAcknowledgements -ne 1 -or $final.replayed -ne 0 -or $final.released -or
        $final.blocked -ne $OriginalProxy.blocked -or $final.injectedRecovery401 -ne 0 -or $final.expiredForwarded -ne 0 -or
        $final.additionalOwnerReads -lt 1 -or $final.commandSHA256 -cne $OriginalProxy.commandSHA256) { throw 'Revocation unexpectedly replayed or altered the original command.' }
    Assert-Healthy
    New-Item -ItemType File -Path ($gate+'.inventory-revocation-complete') | Out-Null
    Wait-Marker $serverLog "RPG_INVENTORY_REVOCATION_OBSERVATION_COMPLETE Target=$($f.Owner) Remaining=1 ActorsRemoved=1"
    $sameSources=$true
    foreach($path in $resultSourcePaths) { if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { $sameSources=$false } }
    $sameModule=(Get-FileHash -LiteralPath $unrealModulePath).Hash -ceq $unrealModuleHash
    $owned=@($processes | Where-Object { $_.ProcessName -cin @('UnrealEditor-Cmd','node') }); [array]::Reverse($owned)
    foreach($process in $owned) { if(-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null } }
    $databaseClosed=Read-ResultAbandonmentDatabase
    if(($databaseClosed | ConvertTo-Json -Depth 15 -Compress) -cne ($databaseAfter | ConvertTo-Json -Depth 15 -Compress) -or
        (Read-InventoryRevocationReceiptDigest $Request $f.Owner) -cne $receiptDigest) { throw 'Fixture shutdown changed the stored receipt or membership/settlement.' }
    $diagnostics=0; $expectedNetworkErrors=0
    $closedLogs=@(foreach($log in $logs) {
        $text=[string](Get-Content -LiteralPath $log -Raw)
        foreach($line in ($text -split '\r?\n')) {
            if($line -match 'Fatal error:|Assertion failed:|Ensure condition failed:') { ++$diagnostics }
            elseif($line -match ': Error:') {
                if(Test-InventoryRevocationExpectedNetworkClose $log $line) { ++$expectedNetworkErrors } else { ++$diagnostics }
            }
        }
        @{Path=$log;SHA256=(Get-FileHash -LiteralPath $log).Hash}
    })
    [ordered]@{Passed=($sameSources -and $sameModule -and $diagnostics -eq 0);Mode='InventoryRecoveryRevocation';SessionId=$sessionId;RequestId=$Request
        Clients=2;DurableMoveReceipts=1;DroppedCommittedResponses=1;GatewayRetriesBlocked=$final.blocked;ManualFrozenReplayForwarded=0
        ActualPlayerAbandonmentReceipt=$true;ReceiptAndMembershipScopeValidated=$true;ProductionMembershipPolling=$true;ActualIrisEjection=$true
        PendingCommandRetiredOnlyForRevokedIdentity=$true;TargetClientExpectedNetworkClose=1;TargetActorsRemoved=$true;SameRemainingPeerPawnConnectionAndEncounter=$true
        ObserverCompletedBeforeFixtureShutdown=$true;DatabaseCheckedAfterFixtureShutdown=$true;CombatGateOpened=$false
        ImmutableItemReceiptSHA256=$receiptDigest;FullInventoriesAndStarterEquipmentUnchanged=$true;ExtraItemCommitsOrRevisions=0;SettlementStarted=$false
        NativeOwnerTerminalReplicationDeliveryBeforeEjectionVerified=$false;LateInFlightRecoveryReplyCrossVerified=$false;SuccessfulRecoveryClaimed=$false
        SyntheticSlateDrags=1;ProgrammaticWidgetReopen=1;RecoveryButtonClicks=0;AbandonmentSubmittedByHarnessPlayerApi=$true;AbandonmentUiInputVerified=$false
        AuthenticatedJoinTicketAdmission=$true;AdmissionBypass=$false;DevelopmentUnverifiedPlatformIdentityFixture=$true;ReceiptOrIdentitySeeded=$false
        ServerOutgoingPacketSimulationVerified=$true;ServerLagMinMs=80;ServerLagMaxMs=160;ServerPacketLossPercent=5;ClientOutgoingSimulationVerified=$false
        CredentialExpiryOrRenewalVerified=$false;SteamShippingOrRestartVerified=$false
        EditorModuleSHA256=$unrealModuleHash;BackendAssemblySHA256=(Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash
        SelectedInputs=$resultSourcePaths.Count;SourcesUnchanged=$sameSources;ModuleUnchanged=$sameModule;ExpectedTargetNetworkCloseDiagnostics=$expectedNetworkErrors
        ClosedUnexpectedRuntimeDiagnostics=$diagnostics;ClosedLogs=$closedLogs
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if(-not $sameSources -or -not $sameModule -or $diagnostics -ne 0) { throw 'Revocation provenance or closed runtime diagnostics failed.' }
}
