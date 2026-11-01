# Development fixture only. Never releases, retargets or repairs the original grant.
function Get-NPCPickupRevisionConflictGap {
    param([object]$State)
    $p=$State.prepared;$o=$p.origin;$c=$p.revisionConflict
    if ($o.requestId -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$' -or
        $o.requestId -ceq [Guid]::Empty.ToString('D') -or $o.commandSHA256 -cnotmatch '^[0-9a-f]{64}$' -or
        $o.journalSHA256 -cnotmatch '^[0-9a-f]{64}$' -or $o.prepareBodySHA256 -cnotmatch '^[0-9a-f]{64}$') {throw 'Invalid revision origin identity or digest.'}
    if ($State.failure -or $State.phase -cne 'revision-conflict-held' -or $State.droppedAcknowledgements -ne 0 -or
        $null -ne $State.committed -or $null -ne $State.delivered -or
        $p.fault -cne 'consume-held' -or @($c.setupBlocked).Count -lt 1 -or @($c.conflicts).Count -ne 2 -or
        $c.phase -cne 'armed' -or @($p.consumes).Count -ne 0 -or @($p.preparations).Count -lt 2) {
        throw 'Missing actual unconsumed RevisionConflict boundary.'
    }
    foreach ($entry in @($c.setupBlocked)+@($c.conflicts)+@($c.heldConsumes)) {
        if ($entry.requestId -cne $o.requestId -or $entry.commandSHA256 -cne $o.commandSHA256 -or
            $entry.journalSHA256 -cne $o.journalSHA256 -or
            $entry.bodySHA256 -cne 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855') {
            throw 'Revision conflict changed the original request, empty body or journal.'
        }
        $time=$entry.elapsedMilliseconds
        if ($null -eq $time -or $time -isnot [ValueType] -or $time -is [bool] -or $time -is [char] -or
            [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or $time -lt 0) {throw 'Invalid revision-conflict timestamp.'}
    }
    $previous=[double]$c.setupBlocked[-1].elapsedMilliseconds
    $armed=$c.armed.elapsedMilliseconds
    if ($null -eq $armed -or $armed -isnot [ValueType] -or $armed -is [bool] -or $armed -is [char] -or $armed -lt $previous -or
        [double]::IsNaN([double]$armed) -or [double]::IsInfinity([double]$armed)) {throw 'Invalid revision fixture setup boundary.'}
    $previous=[double]$armed
    foreach ($entry in $c.conflicts) {
        $requestTime=$entry.requestElapsedMilliseconds
        if ($entry.status -cne 'RevisionConflict' -or $entry.statusCode -ne 409 -or $entry.acknowledgement -cne 'delivered' -or
            $null -eq $entry.bodyBytes -or $entry.bodyBytes -is [bool] -or $entry.bodyBytes -ne 0 -or
            $null -eq $requestTime -or $requestTime -isnot [ValueType] -or $requestTime -is [bool] -or
            [double]::IsNaN([double]$requestTime) -or [double]::IsInfinity([double]$requestTime) -or
            $requestTime -lt $previous -or $requestTime -gt $entry.elapsedMilliseconds) {throw 'Missing ordered, delivered RevisionConflict 409.'}
        $previous=[double]$entry.elapsedMilliseconds
    }
    $gap=([double]$c.conflicts[1].requestElapsedMilliseconds-[double]$c.conflicts[0].elapsedMilliseconds)/1000.0
    if ($gap -lt 1.5) {throw 'Revision rejection did not cross the native grant retry timer.'}
    $previous=0.0;$index=0
    foreach ($entry in $p.preparations) {
        $time=$entry.elapsedMilliseconds
        if ($entry.status -cne $(if ($index -eq 0) {'Prepared'} else {'AlreadyPrepared'}) -or
            $entry.acknowledgement -cne 'delivered' -or $entry.consumed -isnot [bool] -or $entry.consumed -or
            $entry.commandSHA256 -cne $o.commandSHA256 -or $entry.journalSHA256 -cne $o.journalSHA256 -or
            $entry.bodySHA256 -cne $o.prepareBodySHA256 -or $null -eq $time -or $time -isnot [ValueType] -or
            $time -is [bool] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or $time -lt $previous) {
            throw 'Re-prepare changed or consumed the frozen origin.'
        }
        $previous=[double]$time;$index++
    }
    return $gap
}

function Assert-NPCPickupRevisionUnconsumed {
    param([object]$Snapshot,[object]$Intent,[object]$Command,[object]$Origin,[string]$ExistingOwner)
    $p=$Snapshot.origin;$item=$Snapshot.item
    if ($Snapshot.sessionOriginCount -ne 1 -or $null -eq $p -or $null -ne $Snapshot.receipt -or $null -ne $p.consumed_at -or
        $Intent.status -cne 'Prepared' -or $null -ne $Intent.consumedAt -or $p.request_id -cne $Origin.requestId -or
        $p.drop_id -cne $Origin.dropId -or $p.item_id -cne $Origin.itemId -or $p.character_id -cne $Origin.characterId -or
        $p.dungeon_session_id -cne $Origin.sessionId -or $p.server_id -cne $Origin.serverId -or
        $Intent.dungeonSessionId -cne $Origin.sessionId -or $Intent.serverId -cne $Origin.serverId -or
        $Intent.dropId -cne $Origin.dropId -or $Intent.requestId -cne $Origin.requestId -or
        $Intent.characterId -cne $Origin.characterId -or $Intent.itemId -cne $Origin.itemId -or
        (ConvertTo-NPCPickupCanonicalJson $p.command_payload) -cne (ConvertTo-NPCPickupCanonicalJson $Command) -or
        (ConvertTo-NPCPickupCanonicalJson $Intent.command) -cne (ConvertTo-NPCPickupCanonicalJson $Command)) {
        throw 'Revision rejection changed, consumed or rebound the prepared origin.'
    }
    if (!$ExistingOwner) {if ($null -ne $item) {throw 'The initially generated item ID was not empty.'};return}
    if ($null -eq $item -or $ExistingOwner -ceq $Origin.characterId -or $item.item_id -cne $Origin.itemId -or
        $item.owner_type -cne 'Character' -or $item.owner_id -cne $ExistingOwner -or $item.revision -ne 1 -or
        $item.quantity -ne 1 -or $item.lifecycle_state -cne 'Active') {throw 'Existing conflicting item ownership or revision changed.'}
}

function Invoke-NPCPickupRevisionConflict {
    $setup=Wait-ProxyState {param($s) $s.phase -ceq 'revision-setup'}
    $o=$setup.prepared.origin
    if ($o.sessionId -cne $sessionId -or $o.serverId -cne $serverId -or $o.journalSchemaVersion -ne 2 -or
        $members.CharacterId -cnotcontains $o.characterId) {throw 'Unexpected revision fixture origin.'}
    $other=@($members | Where-Object CharacterId -cne $o.characterId)[0]
    $claimant=@($members | Where-Object CharacterId -ceq $o.characterId)[0]
    $journalPath=Join-Path $serverUserDirectory "Saved\ItemPickupJournal\$sessionId\$($o.requestId).json"
    if ((Get-FileHash -LiteralPath $journalPath).Hash.ToLowerInvariant() -cne $o.journalSHA256) {throw 'Original journal changed before revision setup.'}
    $journal=Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $command=$journal.body | ConvertFrom-Json
    $command.mutations[0].expectedRevision=0;$command.mutations[0].newRecord.revision=0
    $intentPath="dungeon-sessions/$sessionId/pickup-intents/$($o.requestId)"
    $initial=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
    Assert-NPCPickupRevisionUnconsumed $initial (Invoke-Api $intentPath 'Get' $serverHeaders) $command $o ''
    $inventories=@{}
    foreach ($member in $members) {$inventories[$member.CharacterId]=Invoke-Api "items?ownerType=Character&ownerId=$($member.CharacterId)&includeTerminal=false&limit=500" 'Get' $member.Headers}
    # Only the fresh generated ID in this run is used. A normal server commit
    # gives it to the other fixture member; no existing user's item is touched.
    $block=$command | ConvertTo-Json -Depth 40 | ConvertFrom-Json
    $block.requestId=[Guid]::NewGuid().ToString('D');$block.operation='Item.FixtureRevisionBlock';$block.commandFingerprint="revision-block-$runId"
    $block.actor.ownerId=$other.CharacterId
    $block.mutations[0].newRecord.owner.ownerId=$other.CharacterId
    $block.mutations[0].newRecord.location.containerId=$other.CharacterId
    $block.mutations[0].newRecord.location.slotIndex=79
    $block.mutations[0].newRecord.metadata.creationSource='NPCPickupRevisionConflictFixture'
    $created=Invoke-Api 'item-transactions/commit' 'Post' $serverHeaders $block
    if ($created.status -cne 'Committed' -or @($created.records).Count -ne 1 -or $created.records[0].revision -ne 1 -or
        $created.records[0].state.instanceId -cne $o.itemId -or $created.records[0].owner.ownerId -cne $other.CharacterId) {throw 'Revision blocker creation failed.'}
    $before=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
    Assert-NPCPickupRevisionUnconsumed $before (Invoke-Api $intentPath 'Get' $serverHeaders) $command $o $other.CharacterId
    $ready=Invoke-ProxyControl 'revision-conflict-ready' @{}
    if ($ready.phase -cne 'revision-conflict') {throw 'Revision fixture was not armed.'}
    $known=Wait-ProxyState {param($s) $s.phase -ceq 'revision-conflict-held' -and @($s.prepared.revisionConflict.conflicts).Count -eq 2}
    $gap=Get-NPCPickupRevisionConflictGap $known
    $after=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
    Assert-NPCPickupRevisionUnconsumed $after (Invoke-Api $intentPath 'Get' $serverHeaders) $command $o $other.CharacterId
    if ((ConvertTo-NPCPickupCanonicalJson $before) -cne (ConvertTo-NPCPickupCanonicalJson $after)) {throw 'Actual RevisionConflict changed an existing row.'}
    New-Item -ItemType File -Path $pickupCommittedFile | Out-Null
    Wait-Marker $serverLog 'RPG_NPC_PICKUP SERVER_UNCERTAIN '
    $rows=Get-NativeRows $serverLog 3
    if ($rows[2] -cnotmatch '^RPG_NPC_PICKUP SERVER_UNCERTAIN Pickup=([1-9]\d*) Pending=1 Available=0 Retired=0 TotalItems=0 GatewayAttempts=([1-8])$') {throw 'Native prepared claim was released or projected.'}
    $pickupNetId=$Matches[1]
    $reboundIds=@()
    foreach ($kind in @('new-request','new-slot','new-item','other-claimant')) {
        $changed=$command | ConvertTo-Json -Depth 40 | ConvertFrom-Json
        if ($kind -in @('new-request','other-claimant')) {$changed.requestId=[Guid]::NewGuid().ToString('D');$changed.mutations[0].newRecord.state.instanceId=[Guid]::NewGuid().ToString('D');$reboundIds+=$changed.requestId}
        if ($kind -ceq 'new-slot') {$changed.mutations[0].newRecord.location.slotIndex++}
        if ($kind -ceq 'new-item') {$changed.mutations[0].newRecord.state.instanceId=[Guid]::NewGuid().ToString('D')}
        if ($kind -ceq 'other-claimant') {$changed.actor.ownerId=$other.CharacterId;$changed.mutations[0].newRecord.owner.ownerId=$other.CharacterId;$changed.mutations[0].newRecord.location.containerId=$other.CharacterId}
        $null=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents" 'Post' $serverHeaders @{dropId=$o.dropId;command=$changed} 409
    }
    foreach ($id in $reboundIds) {$null=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$id" 'Get' $serverHeaders $null 404;$null=Invoke-Api "item-transactions/$id" 'Get' $serverHeaders $null 404}
    $null=Invoke-Api "item-transactions/$($o.requestId)" 'Get' $serverHeaders $null 404
    $existing=Invoke-Api "items/$($o.itemId)" 'Get' $other.Headers
    if ((ConvertTo-NPCPickupCanonicalJson $existing) -cne (ConvertTo-NPCPickupCanonicalJson $created.records[0])) {throw 'Other member existing item changed.'}
    $null=Invoke-Api "items/$($o.itemId)" 'Get' $claimant.Headers $null 403
    foreach ($member in $members) {
        $current=Invoke-Api "items?ownerType=Character&ownerId=$($member.CharacterId)&includeTerminal=false&limit=500" 'Get' $member.Headers
        $retained=@($current.items | Where-Object {$_.state.instanceId -cne $o.itemId})
        if ((ConvertTo-NPCPickupCanonicalJson @($inventories[$member.CharacterId].items)) -cne (ConvertTo-NPCPickupCanonicalJson $retained) -or
            @($current.items | Where-Object {$_.state.instanceId -ceq $o.itemId}).Count -ne $(if ($member.CharacterId -ceq $other.CharacterId) {1} else {0})) {throw 'Revision collision changed another item or granted the claimant.'}
    }
    $pending=@($members | ForEach-Object {Select-String -LiteralPath $_.Log -Pattern 'LogTemp: Display: RPG_NPC_PICKUP CLIENT_REJECTED_PENDING ' | ForEach-Object Line})
    if ($pending.Count -ne 3) {throw 'Expected three live-pickup RPC rejections.'}
    $rejected=@()
    foreach ($line in $pending) {
        if ($line -cnotmatch "RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=$pickupNetId Character=([0-9a-f-]{36}) Request=([0-9a-f-]{36}) Retired=0 TotalItems=0$" -or
            $members.CharacterId -cnotcontains $Matches[1] -or $Matches[2] -ceq $o.requestId -or $rejected -ccontains $Matches[2]) {throw 'Invalid native claimant or rejection identity.'}
        $rejected+=$Matches[2]
    }
    # Keep the known conflict unresolved. Close only our clients, then server;
    # never destroy the blocker, replace the Item ID, or replay a new command.
    $unreal=@($processes | Where-Object ProcessName -ceq 'UnrealEditor-Cmd');[array]::Reverse($unreal)
    foreach ($proc in $unreal) {if (!$proc.HasExited) {Stop-Process -Id $proc.Id;$null=$proc.WaitForExit(10000)};if (!$proc.HasExited) {throw 'Owned UE writer did not exit.'}}
    $closed=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
    if ((ConvertTo-NPCPickupCanonicalJson $closed) -cne (ConvertTo-NPCPickupCanonicalJson $before) -or
        (Get-FileHash -LiteralPath $journalPath).Hash.ToLowerInvariant() -cne $o.journalSHA256) {throw 'Closed original reservation, item or journal changed.'}
    $final=Invoke-ProxyControl 'state'
    $null=Get-NPCPickupRevisionConflictGap $final
    # Proxy stderr is redirected with an exclusive writer. Its final state has
    # already been captured; close only this run's proxy before hashing logs.
    foreach ($proc in @($processes | Where-Object ProcessName -ceq 'node')) {
        if (!$proc.HasExited) {Stop-Process -Id $proc.Id;$null=$proc.WaitForExit(10000)}
        if (!$proc.HasExited) {throw 'Owned proxy log writer did not exit.'}
    }
    foreach ($row in $hashes) {if ((Get-FileHash -LiteralPath $row.path).Hash -cne $row.SHA256) {throw 'Frozen input changed during revision verification.'}}
    if ((Get-FileHash -LiteralPath $module).Hash -cne $moduleHash) {throw 'Frozen module changed during revision verification.'}
    $closedLogs=@(foreach ($log in $logs) {if ((Get-Content -LiteralPath $log -Raw -Encoding UTF8) -match $failure) {throw 'Revision runtime diagnostics failed.'};[ordered]@{path=$log;SHA256=(Get-FileHash -LiteralPath $log).Hash}})
    foreach ($name in @('initial','before','after','closed')) {Get-Variable -Name $name -ValueOnly | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $runRoot "RevisionConflict.$name.database.proof.json") -Encoding UTF8}
    $final | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $runRoot 'RevisionConflict.proxy.proof.json') -Encoding UTF8
    Copy-Item -LiteralPath $journalPath -Destination (Join-Path $runRoot 'RevisionConflict.journal-v2.json')
    [ordered]@{schemaVersion=1;passed=$true;mode='PreparedRevisionConflict';sessionId=$sessionId;origin=$o;existingOwner=$other.CharacterId;
        existingItem=$existing;blockerCreationReceipt=$created;unconsumed=$true;originalReceiptAbsent=$true;originalRowsUnchanged=$true;
        originalJournalUnchanged=$true;nativePendingLocked=$true;nativePartyProjectionEmpty=$true;actualRevisionConflictResponses=2;
        nativeGrantTimerGapSeconds=$gap;forbiddenRebinds=@('new-request','new-slot','new-item','other-claimant');reboundRequestIds=$reboundIds;
        nativeRejectedRequests=$rejected;oldItemsPreserved=$true;otherOwnerIsolation=$true;noRepairOrNewGrant=$true;
        moduleSHA256=$moduleHash;backendModuleSHA256=$backendModuleHash;sourceAndAssetHashes=$hashes;closedLogs=$closedLogs;
        steamShippingOrNaturalCombat=$false;databaseFixtureRowsRetained=$true} | ConvertTo-Json -Depth 40 |
        Set-Content -LiteralPath (Join-Path $runRoot 'NPCPickupRevisionConflict.proof.json') -Encoding UTF8
    Write-Host "PASS: actual PostgreSQL RevisionConflict, frozen reservation/journal and other owner's existing item preserved; no grant or repair. $runRoot"
}
