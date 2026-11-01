# Dot-sourced by the existing local NPC runner. Loading this file starts nothing.
function Get-NPCPickupRestartBoundary {
    param([string[]]$ServerRows,[object[]]$Clients,[object]$Committed,[string]$PreparedDropId='')
    if ($PreparedDropId) {
        if ($ServerRows.Count -ne 3 -or (Get-NPCPickupPreparedDropId $ServerRows[1]) -cne $PreparedDropId) { throw 'Borrowed prepared restart Drop ID.' }
        $ServerRows=$ServerRows.Clone(); $ServerRows[1]=$ServerRows[1] -replace ' DropId=[0-9a-f-]+$',''
    }
    $guid='[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}'
    $zero='00000000-0000-0000-0000-000000000000'
    foreach ($id in @($Committed.requestId,$Committed.characterId,$Committed.itemId)) {
        if ($id -cnotmatch "^$guid`$" -or $id -eq $zero) { throw 'Invalid durable restart identity.' }
    }
    if ($ServerRows.Count -ne 3 -or $Clients.Count -ne 2) { throw 'Wrong restart boundary observation count.' }
    $death=[regex]::Match($ServerRows[0],'^RPG_NPC_PICKUP SERVER_DEATH NPC=([1-9]\d*) Deaths=1 Authenticated=1 NativeSource=1$')
    $drop=[regex]::Match($ServerRows[1],'^RPG_NPC_PICKUP SERVER_DROP NPC=([1-9]\d*) Pickup=([1-9]\d*) Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1$')
    $pending=[regex]::Match($ServerRows[2],'^RPG_NPC_PICKUP SERVER_UNCERTAIN Pickup=([1-9]\d*) Pending=1 Available=0 Retired=0 TotalItems=0 GatewayAttempts=([1-8])$')
    if (-not $death.Success -or -not $drop.Success -or -not $pending.Success -or
        $death.Groups[1].Value -ne $drop.Groups[1].Value -or $drop.Groups[2].Value -ne $pending.Groups[1].Value) { throw 'Borrowed/malformed pending pickup.' }
    $characters=[Collections.Generic.HashSet[string]]::new()
    $requests=[Collections.Generic.HashSet[string]]::new()
    foreach ($client in $Clients) {
        $character=[string]$client.characterId
        $rows=@($client.rows)
        $expected=if ($character -ceq $Committed.characterId) {3} else {4}
        if ($character -cnotmatch "^$guid`$" -or $character -eq $zero -or
            -not $characters.Add($character) -or $rows.Count -ne $expected) { throw 'Duplicate/malformed restart claimant.' }
        $ready=[regex]::Match($rows[0],"^RPG_NPC_PICKUP CLIENT_READY Pickup=([1-9]\d*) Character=($guid) Available=1 Empty=1 InRange=1$")
        $send=[regex]::Match($rows[1],"^RPG_NPC_PICKUP CLIENT_SEND Pickup=([1-9]\d*) Requests=2 ExistingRPC=1 Character=($guid)$")
        if (-not $ready.Success -or -not $send.Success -or $ready.Groups[1].Value -ne $drop.Groups[2].Value -or
            $send.Groups[1].Value -ne $drop.Groups[2].Value -or $ready.Groups[2].Value -cne $character -or $send.Groups[2].Value -cne $character) { throw 'Borrowed restart claimant/pickup.' }
        foreach ($row in $rows[2..($rows.Count-1)]) {
            $rejected=[regex]::Match($row,"^RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=([1-9]\d*) Character=($guid) Request=($guid) Retired=0 TotalItems=0$")
            if (-not $rejected.Success -or $rejected.Groups[1].Value -ne $drop.Groups[2].Value -or
                $rejected.Groups[2].Value -cne $character -or $rejected.Groups[3].Value -eq $zero -or
                $rejected.Groups[3].Value -ceq $Committed.requestId -or -not $requests.Add($rejected.Groups[3].Value)) { throw 'Borrowed/duplicate pending rejection.' }
        }
    }
    if (-not $characters.Contains($Committed.characterId) -or $requests.Count -ne 3) { throw 'Missing original owner or rejections.' }
    [pscustomobject]@{npcNetId=$death.Groups[1].Value;pickupNetId=$drop.Groups[2].Value;
        gatewayAttempts=[int]$pending.Groups[2].Value;rejectedRequestIds=@($requests)}
}

function Assert-NPCPickupRestartRecovery {
    param([string[]]$ServerRows,[object[]]$Clients,[object]$Committed,[object]$ProxyState,[switch]$Prepared)
    $guid='[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}'
    foreach ($id in @($Committed.requestId,$Committed.characterId,$Committed.itemId)) {
        if ($id -cnotmatch "^$guid`$" -or $id -eq '00000000-0000-0000-0000-000000000000') { throw 'Invalid restored identity.' }
    }
    if ($ServerRows.Count -ne 1 -or $Clients.Count -ne 2 -or $ProxyState.failure -or
        $ProxyState.phase -ne 'restart-read-only' -or $ProxyState.droppedAcknowledgements -ne 1 -or
        $null -ne $ProxyState.delivered -or $null -eq $ProxyState.restartRecovery -or $ProxyState.restartRecovery.rejectedWrites -ne 0) { throw 'Invalid read-only restart recovery.' }
    $server=[regex]::Match($ServerRows[0],"^RPG_NPC_PICKUP SERVER_RESTART_RECOVERED Owner=($guid) Request=($guid) Item=($guid) Quantity=1 Revision=1 Party=2 TotalItems=1 NewPickup=0$")
    if (-not $server.Success -or $server.Groups[1].Value -cne $Committed.characterId -or
        $server.Groups[2].Value -cne $Committed.requestId -or $server.Groups[3].Value -cne $Committed.itemId) { throw 'Restart server item/owner/request mismatch.' }
    $seen=[Collections.Generic.HashSet[string]]::new()
    foreach ($client in $Clients) {
        if (@($client.rows).Count -ne 1 -or -not $seen.Add($client.characterId)) { throw 'Duplicate/missing restarted client.' }
        $row=[regex]::Match($client.rows[0],"^RPG_NPC_PICKUP CLIENT_RESTART_RECOVERED Character=($guid) Owner=($guid) Request=($guid) Item=($guid) TotalItems=([01]) Quantity=([01]) Revision=([01]) NewPickup=0$")
        $quantity=if ($client.characterId -ceq $Committed.characterId) {'1'} else {'0'}
        if (-not $row.Success -or $row.Groups[1].Value -cne $client.characterId -or $row.Groups[2].Value -cne $Committed.characterId -or
            $row.Groups[3].Value -cne $Committed.requestId -or $row.Groups[4].Value -cne $Committed.itemId -or
            $row.Groups[5].Value -ne $quantity -or $row.Groups[6].Value -ne $quantity -or $row.Groups[7].Value -ne $quantity) { throw 'Restart client owner isolation mismatch.' }
    }
    if (-not $seen.Contains($Committed.characterId) -or $Committed.recordSHA256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Missing original owner/full record hash.' }
    if ($Prepared) {
        Assert-NPCPickupPreparedRestartReads $ProxyState $Committed @($seen)
        return
    }
    foreach ($field in @('requestId','characterId','itemId','bodySHA256','recordSHA256')) {
        if ($ProxyState.committed.$field -cne $Committed.$field) { throw 'Borrowed restart proxy state.' }
    }
    $receipts=0
    $inventories=[Collections.Generic.HashSet[string]]::new()
    $began=$ProxyState.restartRecovery.elapsedMilliseconds
    if ($null -eq $began -or $began -isnot [ValueType] -or [double]::IsNaN([double]$began) -or
        [double]::IsInfinity([double]$began) -or $began -lt 0) { throw 'Invalid restart monotonic boundary.' }
    foreach ($read in $ProxyState.restartRecovery.reads) {
        if ($read.statusCode -ne 200 -or $null -eq $read.elapsedMilliseconds -or $read.elapsedMilliseconds -isnot [ValueType] -or
            [double]::IsNaN([double]$read.elapsedMilliseconds) -or [double]::IsInfinity([double]$read.elapsedMilliseconds) -or
            $read.elapsedMilliseconds -lt $began) { throw 'Invalid read-only HTTP observation.' }
        if ($read.kind -eq 'receipt' -and $read.requestId -ceq $Committed.requestId -and $read.recordSHA256 -ceq $Committed.recordSHA256) { $receipts++ }
        elseif ($read.kind -eq 'inventory' -and $seen.Contains($read.characterId)) { [void]$inventories.Add($read.characterId) }
        else { throw 'Unexpected/borrowed restart GET.' }
    }
    if ($receipts -lt 1 -or $inventories.Count -ne 2) { throw 'Missing original receipt/each member inventory HTTP reads.' }
}

function Assert-NPCPickupPreparedRestartReads {
    param([object]$State,[object]$Committed,[string[]]$Characters)
    $p=$State.prepared; $o=$p.origin; $r=$State.restartRecovery
    if ($p.fault -cne 'consume-held' -or $p.rejectedWrites -ne 0 -or $r.preparationCount -ne @($p.preparations).Count -or
        $r.consumeCount -ne 1 -or @($p.consumes).Count -ne 1 -or $r.blockedRetryCount -ne @($State.blockedRetries).Count) { throw 'Prepared restart issued more writes/retries or lacks the original boundary.' }
    $basic=$State | ConvertTo-Json -Depth 30 | ConvertFrom-Json
    $basic.phase='observed'; $basic.droppedAcknowledgements=0; $basic.prepared.preparations=@($basic.prepared.preparations[0])
    Assert-NPCPickupPreparedObservation $basic $o.sessionId $o.serverId $o.dropId $Committed.requestId $Committed.itemId $Committed.characterId
    $blocked=$State | ConvertTo-Json -Depth 30 | ConvertFrom-Json; $blocked.phase='blocked'
    $null=Get-NPCPickupPreparedRetryGap $blocked
    if ($p.consumes[0].status -cne 'Committed' -or $p.consumes[0].acknowledgement -cne 'dropped') { throw 'Original receipt was already delivered.' }
    foreach ($field in @('requestId','characterId','itemId','bodySHA256','recordSHA256','commandSHA256','journalSHA256')) {
        if ($State.committed.$field -cne $Committed.$field -or $State.committed.$field -cnotmatch $(if ($field -like '*SHA256') {'^[0-9a-f]{64}$'} else {'^[0-9a-f-]{36}$'})) { throw 'Borrowed prepared committed boundary.' }
    }
    $began=$r.elapsedMilliseconds
    if ($null -eq $began -or $began -isnot [ValueType] -or [double]::IsNaN([double]$began) -or [double]::IsInfinity([double]$began) -or $began -lt 0) { throw 'Invalid prepared restart boundary time.' }
    $index=0
    foreach ($a in $p.preparations) {
        if ($a.status -cne $(if ($index -eq 0) {'Prepared'} else {'AlreadyPrepared'}) -or $a.acknowledgement -cne 'delivered' -or
            $a.consumed -isnot [bool] -or $a.consumed -ne ($index -gt 0) -or $a.commandSHA256 -cne $o.commandSHA256 -or
            $a.bodySHA256 -cne $o.prepareBodySHA256 -or $a.journalSHA256 -cne $o.journalSHA256 -or
            $null -eq $a.elapsedMilliseconds -or $a.elapsedMilliseconds -gt $began) { throw 'Changed prepared restart history.' }
        $index++
    }
    if ($p.consumes[0].recordSHA256 -cne $Committed.recordSHA256 -or $o.commandSHA256 -cne $Committed.commandSHA256 -or
        $o.journalSHA256 -cne $Committed.journalSHA256 -or @($State.blockedRetries | Where-Object elapsedMilliseconds -gt $began).Count) { throw 'Changed original command/journal/record after restart.' }
    $intent=$null; $receipt=$null; $inventories=[Collections.Generic.HashSet[string]]::new(); $previous=[double]$began
    foreach ($read in $r.reads) {
        foreach ($time in @($read.requestElapsedMilliseconds,$read.elapsedMilliseconds)) {
            if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or $time -lt $began) { throw 'Invalid prepared restart GET time.' }
        }
        if ($read.statusCode -ne 200 -or $read.acknowledgement -cne 'delivered' -or $read.requestElapsedMilliseconds -gt $read.elapsedMilliseconds -or
            $read.elapsedMilliseconds -lt $previous) { throw 'Undelivered/out-of-order prepared restart GET.' }
        $previous=[double]$read.elapsedMilliseconds
        if ($read.kind -ceq 'intent') {
            if ($read.requestId -cne $o.requestId -or $read.dropId -cne $o.dropId -or $read.commandSHA256 -cne $o.commandSHA256 -or
                $read.journalSHA256 -cne $o.journalSHA256 -or $read.consumed -isnot [bool] -or -not $read.consumed -or
                -not $read.preparedAt -or -not $read.consumedAt) { throw 'Changed/unconsumed prepared restart intent.' }
            $intent=$read
        } elseif ($read.kind -ceq 'receipt') {
            if (-not $intent -or $read.requestElapsedMilliseconds -lt $intent.elapsedMilliseconds -or
                $read.requestId -cne $o.requestId -or $read.recordSHA256 -cne $Committed.recordSHA256) { throw 'Missing original intent before receipt GET.' }
            $receipt=$read
        } elseif ($read.kind -ceq 'inventory' -and $Characters -ccontains $read.characterId) {
            $owner=$read.characterId -ceq $Committed.characterId
            if ($read.itemCount -ne $(if ($owner) {1} else {0}) -or $null -eq $read.itemCount -or
                ($owner -and (-not $receipt -or $read.requestElapsedMilliseconds -lt $receipt.elapsedMilliseconds -or $read.recordSHA256 -cne $Committed.recordSHA256)) -or
                (-not $owner -and $null -ne $read.recordSHA256)) { throw 'Owner inventory loaded before receipt or leaked another owner.' }
            [void]$inventories.Add($read.characterId)
        } else { throw 'Unexpected prepared restart GET.' }
    }
    if (-not $intent -or -not $receipt -or $inventories.Count -ne 2) { throw 'Missing intent/receipt/each inventory GET chain.' }
}

function Invoke-NPCPickupServerRestart {
    param([object]$Committed,[int]$GatewayAttempts,[double]$Gap)
    $oldLogs=@($members | ForEach-Object { $_.Log })
    $pendingDirectory=Join-Path $runRoot "server-User\Saved\ItemPickupJournal\$sessionId"
    $pendingFile=Join-Path $pendingDirectory ($Committed.requestId+'.json')
    if (@(Get-ChildItem -LiteralPath $pendingDirectory -File).Count -ne 1) { throw 'Expected exactly one durable pending pickup.' }
    $journal=Get-Content -LiteralPath $pendingFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $bodyHash=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($journal.body)))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $command=$journal.body | ConvertFrom-Json
    $prepared=-not $LegacyProtocol
    $originalState=Invoke-ProxyControl 'state'
    $dropId=if ($prepared) { [string]$originalState.prepared.origin.dropId } else { '' }
    if ($journal.schemaVersion -ne $(if ($prepared) {2} else {1}) -or $journal.serverId -cne $serverId -or $journal.sessionId -cne $sessionId -or
        (-not $prepared -and $bodyHash -cne $Committed.bodySHA256) -or $command.requestId -cne $Committed.requestId -or
        $command.operation -cne 'Item.Pickup' -or $command.actor.type -cne 'Character' -or $command.actor.ownerId -cne $Committed.characterId -or
        $command.affectedQuantity -ne 1 -or @($command.mutations).Count -ne 1 -or $command.mutations[0].expectedRevision -ne 0 -or
        $command.mutations[0].newRecord.revision -ne 0 -or $command.mutations[0].newRecord.state.instanceId -cne $Committed.itemId) { throw 'Durable journal does not match the actual frozen HTTP commit.' }
    $pendingHash=(Get-FileHash -LiteralPath $pendingFile).Hash
    $before=Assert-Persisted $Committed.itemId $Committed.requestId $Committed.definition
    if ($prepared) {
        if ($journal.dropId -cne $dropId -or $pendingHash.ToLowerInvariant() -cne $Committed.journalSHA256) { throw 'Original v2 Drop ID/journal bytes changed.' }
        $command.mutations[0].expectedRevision=0; $command.mutations[0].newRecord.revision=0
        if ((ConvertTo-NPCPickupCanonicalJson $command) -cne (ConvertTo-NPCPickupCanonicalJson $blockedDatabaseSnapshot.origin.command_payload)) { throw 'Journal v2 full command differs from the committed reservation.' }
        $beforeDb=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $Committed.requestId $Committed.itemId
        if ((ConvertTo-NPCPickupCanonicalJson $beforeDb) -cne (ConvertTo-NPCPickupCanonicalJson $blockedDatabaseSnapshot)) { throw 'Original pending PostgreSQL rows changed before UE restart.' }
    }
    $script:maintainStartupLease=$true
    $script:nextStartupHeartbeatUtc=[DateTime]::MinValue
    Assert-Healthy
    $oldPids=@()
    foreach ($proc in $processes.ToArray()) {
        if ($proc.ProcessName -ne 'UnrealEditor-Cmd') { continue }
        $oldPids+=$proc.Id
        Stop-Process -InputObject $proc -ErrorAction Stop
        if (-not $proc.WaitForExit(10000)) { throw 'Owned pre-restart UE exit timed out.' }
        [void]$processes.Remove($proc); $proc.Dispose()
    }
    if ($oldPids.Count -ne 3 -or (Get-FileHash -LiteralPath $pendingFile).Hash -ne $pendingHash) { throw 'Pending file changed during owned process shutdown.' }
    $boundary=Get-NPCPickupRestartBoundary (Get-NativeRows $serverLog 3) @($members | ForEach-Object {
        $count=if ($_.CharacterId -ceq $Committed.characterId) {3} else {4}
        [pscustomobject]@{characterId=$_.CharacterId;rows=@(Get-NativeRows $_.Log $count)}
    }) $Committed -PreparedDropId $dropId
    if ($boundary.gatewayAttempts -ne $GatewayAttempts) { throw 'Borrowed native attempt limit.' }
    $script:rejectedIds=@($boundary.rejectedRequestIds)
    Assert-PartyIsolation $Committed.itemId $Committed.requestId
    $archivedJournal=Join-Path $runRoot 'PendingPickup.before-restart.json'
    Copy-Item -LiteralPath $pendingFile -Destination $archivedJournal
    [ordered]@{passed=$false;scope='Confirmed durable pending state after owned UE shutdown; recovery not yet observed';
        oldPids=$oldPids;pendingFile=$pendingFile;pendingSHA256=$pendingHash;frozenBodySHA256=$bodyHash;
        boundary=$boundary;committed=$Committed;nativeGrantTimerGapSeconds=$Gap} | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $runRoot 'ServerRestart.boundary.proof.json') -Encoding UTF8
    $mode=Invoke-ProxyControl 'restart-recovery' @{}
    if ($mode.phase -ne 'restart-read-only') { throw 'Read-only restart fixture not armed.' }
    $recoveryArgs=@('-RPGNPCPickupRestartRecoveryE2E',"-RPGNPCPickupRestartOwner=$($Committed.characterId)",
        "-RPGNPCPickupRestartItem=$($Committed.itemId)","-RPGNPCPickupRestartRequest=$($Committed.requestId)")
    try {
        $env:PROJECT_RPG_GAME_SERVER_ID=$serverId; $env:PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId
        $env:PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken
        $newServerLog=Start-Unreal 'server-restarted' '/Game/Maps/testmap?listen?game=/Game/Blueprints/GameMode/BP_GameModeBase.BP_GameModeBase_C' (
            @('-server',"-port=$Port",'-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity')+$recoveryArgs) 'server'
    } finally { foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) { [Environment]::SetEnvironmentVariable($name,$null,'Process') } }
    Wait-Marker $newServerLog "IpNetDriver listening on port $Port"
    for ($index=0;$index -lt $members.Count;$index++) {
        $member=$members[$index]
        $extra=@('-game','-RPGDungeonFlowE2E',"-RPGFlowE2ESteam=$($member.SteamId)","-RPGFlowE2ECharacter=$($member.CharacterId)","-RPGFlowE2ESession=$sessionId")+$recoveryArgs
        $log=Start-Unreal "client-$($index+1)-restarted" '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' $extra
        $member | Add-Member -NotePropertyName RecoveryLog -NotePropertyValue $log -Force
    }
    foreach ($member in $members) { Wait-Marker $member.RecoveryLog 'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1' }
    $script:maintainStartupLease=$false
    Wait-Marker $newServerLog 'RPG_NPC_PICKUP SERVER_RESTART_RECOVERED '
    foreach ($member in $members) { Wait-Marker $member.RecoveryLog 'RPG_NPC_PICKUP CLIENT_RESTART_RECOVERED ' }
    $after=Assert-Persisted $Committed.itemId $Committed.requestId $Committed.definition
    if (($before | ConvertTo-Json -Depth 25 -Compress) -cne ($after | ConvertTo-Json -Depth 25 -Compress)) { throw 'Persistent receipt changed across UE restart.' }
    Assert-PartyIsolation $Committed.itemId $Committed.requestId
    if ($prepared) {
        $afterIntent=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$($Committed.requestId)" 'Get' $serverHeaders
        foreach ($member in $members) {
            $null=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$($Committed.requestId)" 'Get' $member.Headers $null 403
            $null=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$($Committed.requestId)/commit" 'Post' $member.Headers $null 403
        }
        foreach ($id in $boundary.rejectedRequestIds) { $null=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$id" 'Get' $serverHeaders $null 404 }
        $afterDb=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $Committed.requestId $Committed.itemId
        Assert-NPCPickupPreparedDatabaseSnapshot $afterDb $afterIntent $after $sessionId $serverId $dropId $Committed.characterId
        if ((ConvertTo-NPCPickupCanonicalJson $afterDb) -cne (ConvertTo-NPCPickupCanonicalJson $beforeDb)) { throw 'Prepared PostgreSQL origin/item/receipt changed across UE restart.' }
        $afterDb | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $runRoot 'ServerRestart.database.proof.json') -Encoding UTF8
    }
    if (@(Get-ChildItem -LiteralPath $pendingDirectory -File).Count -ne 0) { throw 'Verified pending journal was not resolved.' }
    $newPids=@()
    foreach ($proc in $processes.ToArray()) {
        if ($proc.ProcessName -ne 'UnrealEditor-Cmd') { continue }
        $newPids+=$proc.Id
        Stop-Process -InputObject $proc -ErrorAction Stop
        if (-not $proc.WaitForExit(10000)) { throw 'Owned recovery log freeze timed out.' }
        [void]$processes.Remove($proc); $proc.Dispose()
    }
    if ($newPids.Count -ne 3 -or @($newPids | Where-Object { $_ -in $oldPids }).Count) { throw 'Restart did not use distinct owned processes.' }
    $state=Invoke-ProxyControl 'state'
    Assert-NPCPickupRestartRecovery (Get-NativeRows $newServerLog 1) @($members | ForEach-Object {
        [pscustomobject]@{characterId=$_.CharacterId;rows=@(Get-NativeRows $_.RecoveryLog 1)}
    }) $Committed $state -Prepared:$prepared
    foreach ($member in $members) {
        $text=Get-Content -LiteralPath $member.RecoveryLog -Raw -Encoding UTF8
        if ([regex]::Matches($text,'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1').Count -ne 1 -or
            [regex]::Matches($text,'DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1').Count -ne 1) { throw 'Missing distinct authenticated restart admission/fresh ticket.' }
    }
    if ($moduleHash -ne (Get-FileHash -LiteralPath $module).Hash -or $runnerHash -ne (Get-FileHash -LiteralPath $runnerSourcePath).Hash -or
        $partyProofHash -ne (Get-FileHash -LiteralPath $partyProofPath).Hash -or
        $backendModuleHash -ne (Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash) { throw 'Restart binary/runner provenance changed.' }
    foreach ($entry in $hashes) { if ($entry.SHA256 -ne (Get-FileHash -LiteralPath $entry.path).Hash) { throw 'Restart source/asset provenance changed.' } }
    $frozenLogs=@($serverLog)+$oldLogs+@($newServerLog)+@($members | ForEach-Object { $_.RecoveryLog })
    $logHashes=@($frozenLogs | ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_ -ErrorAction Stop).Hash} })
    if ($logHashes.Count -ne 6 -or @($logHashes | Where-Object { $_.SHA256 -notmatch '^[A-F0-9]{64}$' }).Count -or
        (Get-FileHash -LiteralPath $archivedJournal).Hash -ne $pendingHash) { throw 'Missing/changed final logs or frozen journal archive.' }
    [ordered]@{schemaVersion=2;passed=$true;clients=2;sessionId=$sessionId;serverId=$serverId;characterId=$Committed.characterId;
        pickupProtocol=$(if ($prepared) {'prepared'} else {'legacy'});journalSchemaVersion=$journal.schemaVersion;dropId=$dropId;
        readOnlyConsumedIntentThenReceiptRecovery=$prepared;noAdditionalNativeItemWrites=$true;preparedPostgresFullRowsUnchanged=$prepared;
        itemId=$Committed.itemId;requestId=$Committed.requestId;oldOwnedPids=$oldPids;newOwnedPids=$newPids;
        sameAssignmentAndServerSavedDirectory=$true;durableJournalMatchesOriginalHttpBody=$true;
        pendingJournalSHA256=$pendingHash;pendingJournalArchive=$archivedJournal;proxyState=$state;
        actualGameServerProcessRestart=$true;readOnlyReceiptRecovery=$true;freshAuthenticatedClientAdmissions=$true;
        postgresRecordAndReceiptUnchanged=$true;ownerIsolationVerified=$true;resolvedJournalEmpty=$true;
        normalNativeGrantTimerGapSeconds=$Gap;gatewayAttempts=$GatewayAttempts;rejectedRequestIds=$boundary.rejectedRequestIds;
        moduleSHA256=$moduleHash;runnerSHA256=$runnerHash;partyProofSHA256=$partyProofHash;backendModuleSHA256=$backendModuleHash;
        sourceAndAssetHashes=$hashes;frozenLogHashes=$logHashes;proofIntegrityVerified=$true;
        startupLeaseHeartbeatFixture=$true;startupLeaseHeartbeatCount=$startupLeaseHeartbeatCount;
        scope=[ordered]@{actualSavedNpcDrop=$true;scriptedDamageAndPositioning=$true;transientDropRow=$true;
            explicitNewClientProcesses=$true;automaticReconnect=$false;uncommittedCommandReplay=$false;
            worldDropRecreation=$false;hostMigration=$false;multiProcessFencing=$false;steam=$false;shipping=$false}
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'NPCPickupServerRestart.proof.json') -Encoding UTF8
    Write-Host "PASS: owned UE process restart; same allocation/journal; read-only recovery; two fresh authenticated admissions; one unchanged PostgreSQL item. $runRoot"
}
