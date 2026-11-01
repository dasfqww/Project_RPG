# The only mutation is expires_at on exactly this run's assigned service credential.
# Never restore/reissue it or relax API permissions to manufacture a successful recovery.
function Invoke-InventoryExpirySql {
    param([string]$Sql)
    $rows=& docker compose -f (Join-Path $projectRoot 'Backend/compose.postgres.yml') exec -T postgres psql -U project_rpg -d project_rpg -At -v ON_ERROR_STOP=1 -c $Sql 2>$null
    if($LASTEXITCODE -ne 0) { throw 'Owned credential expiry PostgreSQL check failed.' }
    return ($rows -join '').Trim()
}

function Assert-InventoryExpiryIdentity {
    param([string]$Value)
    $parsed=[Guid]::Empty
    if(-not [Guid]::TryParseExact($Value,'D',[ref]$parsed) -or $parsed -eq [Guid]::Empty -or $parsed.ToString('D') -cne $Value) {
        throw 'Unsafe credential expiry scope identity.'
    }
}

function Get-InventoryExpiryDigest {
    param([string]$Value)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value)))).Replace('-','') }
    finally { $sha.Dispose() }
}

function Read-InventoryExpiryReceiptDigest {
    param([string]$Request,[string]$Owner)
    Assert-InventoryExpiryIdentity $Request; Assert-InventoryExpiryIdentity $Owner
    $raw=Invoke-InventoryExpirySql "SELECT row_to_json(r)::text FROM item_transaction_receipts r WHERE request_id='$Request' AND actor_id='$Owner';"
    if(-not $raw.StartsWith('{')) { throw 'Original persisted receipt is missing.' }
    return Get-InventoryExpiryDigest $raw # full row comparison without exposing its body
}

function Read-InventoryExpiryMembership {
    Assert-InventoryExpiryIdentity $sessionId
    return Invoke-InventoryExpirySql "SELECT json_build_object('members',(SELECT COALESCE(json_agg(character_id ORDER BY character_id),'[]'::json) FROM dungeon_session_members WHERE dungeon_session_id='$sessionId'),'abandonments',(SELECT count(*) FROM dungeon_session_abandonments WHERE dungeon_session_id='$sessionId'),'expulsions',(SELECT count(*) FROM dungeon_session_disconnection_expulsions WHERE dungeon_session_id='$sessionId'),'settlements',(SELECT count(*) FROM dungeon_reward_settlements WHERE dungeon_session_id='$sessionId'));"
}

function Complete-InventoryRecoveryCredentialExpiryFixture {
    param([hashtable]$Fixture,[string]$Request,[object]$OriginalProxy,[string]$OriginalActive)
    $f=$Fixture; $moveSwapFixture=$f
    Assert-InventoryExpiryIdentity $sessionId; Assert-InventoryExpiryIdentity $f.Owner; Assert-InventoryExpiryIdentity $Request
    if($serverId -cnotmatch '^boss-return-[0-9a-f]{12}$') { throw 'Unsafe generated server scope.' }
    $receiptDigest=Read-InventoryExpiryReceiptDigest $Request $f.Owner
    $membershipBefore=Read-InventoryExpiryMembership
    $membership=$membershipBefore | ConvertFrom-Json
    if(@($membership.members).Count -ne 1 -or $membership.members[0] -cne $f.Owner -or $membership.abandonments -ne 0 -or
        $membership.expulsions -ne 0 -or $membership.settlements -ne 0) { throw 'Unexpected membership before service credential expiry.' }
    # Backend stores uppercase SHA256 of the raw token, not SHA256 of the Authorization header.
    $credentialHash=Get-InventoryExpiryDigest $f.Headers.Authorization.Substring(7)
    if($credentialHash -cnotmatch '^[0-9A-F]{64}$') { throw 'Invalid credential digest scope.' }
    $scope="token_hash='$credentialHash' AND dungeon_session_id='$sessionId' AND server_id='$serverId'"
    $changed=Invoke-InventoryExpirySql "WITH changed AS (UPDATE game_server_credentials SET expires_at=CURRENT_TIMESTAMP - INTERVAL '1 second' WHERE $scope AND expires_at>CURRENT_TIMESTAMP RETURNING 1) SELECT count(*) FROM changed;"
    if($changed -cne '1') { throw 'Expected exactly one fresh assigned credential to expire.' }
    $expired=Invoke-InventoryExpirySql "SELECT count(*) FROM game_server_credentials WHERE $scope AND expires_at<=CURRENT_TIMESTAMP;"
    if($expired -cne '1') { throw 'Assigned service credential is not durably expired.' }
    # Normal API authentication must reject that actual credential. Do not parse/print its body.
    $status=0
    try { $response=Invoke-WebRequest -Uri "$baseUrl/api/item-transactions/$Request" -Headers $f.Headers -UseBasicParsing -TimeoutSec 10; $status=[int]$response.StatusCode }
    catch { if($_.Exception.Response) { $status=[int]$_.Exception.Response.StatusCode } else { throw 'Expired credential probe transport failed.' } }
    if($status -ne 401) { throw 'The backend did not enforce service credential expiry.' }
    $armed=Invoke-RestMethod -Uri "$inventoryControlUrl/observe-expired" -Method Post -Headers $inventoryProxyHeaders -ContentType 'application/json' -Body '{}'
    if(-not $armed.observeCredentialExpiry -or $armed.released) { throw 'Expired response observation did not arm.' }
    New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-recovery-verified.gate') | Out-Null
    Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI RETRY_FAILED Request=$Request Pending=1 OldProjection=1 Cooldown=1 SameAckObserved=1"
    Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI RETRY_READY Request=$Request Pending=1 CooldownFinished=1"
    Wait-Marker $serverLog "RPG_ITEM_CONCURRENCY_SERVER OUTCOME Request=$Request Stage=Uncertain Backend=Unauthorized Pending=1"
    $final=Read-InventoryRecoveryProxy
    if($final.backendUnauthorized -ne 1 -or $final.expiredForwarded -ne 1 -or $final.injectedRecovery401 -ne 0 -or
        $final.forwarded -ne 1 -or $final.replayed -ne 0 -or $final.released -or $final.commandSHA256 -cne $OriginalProxy.commandSHA256) {
        throw 'Expected one real backend 401 for the unchanged frozen command, without a successful replay.'
    }
    # Receipt GET is server-only. After expiry, compare its complete DB row directly;
    # active inventory uses the still-valid owning player token, never an auth bypass.
    $playerHeaders=$members[0].Headers
    $active=Invoke-Api "items?ownerType=Character&ownerId=$($f.Owner)&includeTerminal=false&limit=500" 'Get' $playerHeaders
    if(($active | ConvertTo-Json -Depth 30 -Compress) -cne $OriginalActive -or
        (Read-InventoryExpiryReceiptDigest $Request $f.Owner) -cne $receiptDigest -or (Read-InventoryMoveSwapCounts) -cne '1|0|1') {
        throw 'Expired recovery changed inventory or the original receipt.'
    }
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Source)" 'Get' $playerHeaders) $f.Source 2 3
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Other)" 'Get' $playerHeaders) $f.Other 1 1
    foreach($original in $f.Retained) {
        $current=Invoke-Api "items/$($original.state.instanceId)" 'Get' $playerHeaders
        if(($current | ConvertTo-Json -Depth 30 -Compress) -cne ($original | ConvertTo-Json -Depth 30 -Compress)) { throw 'Expired recovery changed starter equipment.' }
    }
    if((Read-InventoryExpiryMembership) -cne $membershipBefore) { throw 'Credential expiry mutated membership or manufactured a terminal receipt.' }
    $session=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if($session.state -cne 'InProgress' -or $session.serverId -cne $serverId -or (Test-Path -LiteralPath $gate)) {
        throw 'Credential expiry changed the assignment or started settlement.'
    }
    if((Invoke-InventoryExpirySql "SELECT count(*) FROM game_server_credentials WHERE $scope AND expires_at<=CURRENT_TIMESTAMP;") -cne '1') {
        throw 'Expired credential was unexpectedly renewed or replaced.'
    }
    New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-recovery-verified.gate.expired') | Out-Null
    Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI EXPIRED_PASS Authenticated=1 Pending=1 OldProjection=1 OwnerIdentityRetained=1 NoForcedUnlock=1 Physical=0'
    Assert-Healthy
    $final | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'inventory-proxy-expired.json') -Encoding UTF8
    $sameSources=$true
    foreach($path in $resultSourcePaths) { if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { $sameSources=$false } }
    $sameModule=(Get-FileHash -LiteralPath $unrealModulePath).Hash -ceq $unrealModuleHash
    $owned=@($processes | Where-Object { $_.ProcessName -cin @('UnrealEditor-Cmd','node') }); [array]::Reverse($owned)
    foreach($process in $owned) { if(-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null } }
    $diagnostics=0
    $closedLogs=@(foreach($log in $logs) {
        $content=[string](Get-Content -LiteralPath $log -Raw)
        $diagnostics += [regex]::Matches($content,': Error:|Fatal error:|Assertion failed:|Ensure condition failed:').Count
        @{Path=$log;SHA256=(Get-FileHash -LiteralPath $log).Hash}
    })
    [ordered]@{Passed=($sameSources -and $sameModule -and $diagnostics -eq 0);Mode='InventoryRecoveryCredentialExpiry';SessionId=$sessionId
        RequestId=$Request;ActualAssignedServerCredentialExpired=$true;OwnedCredentialRowsModified=1;NaturalTtlElapsed=$false
        CredentialRestoredOrReissued=$false;DirectBackendExpiredCredentialStatus=401;BackendRecovery401=$final.backendUnauthorized;Synthetic401=$final.injectedRecovery401
        FrozenCommandSHA256=$final.commandSHA256;ExpiredFrozenRequestsForwarded=$final.expiredForwarded;DurableMoveReceipts=1;SuccessfulReplayAfterExpiry=0
        OriginalCommandPendingAtEnd=$true;OldClientSnapshotAndInventoryLockRetained=$true;PlayerIdentityAndMembershipRetained=$true
        ImmutableDatabaseReceiptSHA256=$receiptDigest;FullInventoryAndReceiptUnchanged=$true;StarterEquipmentUnchanged=$true
        SyntheticSlateDrags=1;SyntheticSlateRecoveryClicks=1;WidgetReopened=1;DirectUiOrRpcSubmission=$false
        AuthenticatedJoinTicketAdmission=$true;AdmissionBypass=$false;DevelopmentUnverifiedPlatformIdentityFixture=$true;ActualHttpPostgreSQL=$true
        PlayerTokenExpired=$false;IdentityRevocationVerified=$false;CombatGateOpened=$false;PhysicalInputSteamShippingOrRestartVerified=$false
        EditorModuleSHA256=$unrealModuleHash;BackendAssemblySHA256=(Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash
        SelectedInputs=$resultSourcePaths.Count;SourcesUnchanged=$sameSources;ModuleUnchanged=$sameModule;ClosedRuntimeDiagnostics=$diagnostics;ClosedLogs=$closedLogs
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if(-not $sameSources -or -not $sameModule -or $diagnostics -ne 0) { throw 'Expired recovery provenance or runtime diagnostics failed.' }
}
