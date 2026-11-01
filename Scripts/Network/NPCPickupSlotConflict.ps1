# Development fixture only. Dot-sourcing does not start processes or write data.
function Get-NPCPickupSlotConflictGap {
    param([object]$State)
    $p=$State.prepared; $o=$p.origin; $c=$p.slotConflict
    if ($State.failure -or $p.fault -cne 'consume-held' -or @($c.setupBlocked).Count -lt 1 -or
        @($c.conflicts).Count -lt 2 -or $c.phase -cnotin @('armed','released')) { throw 'Missing actual slot-conflict boundary.' }
    $previous=0.0
    foreach ($entry in @($c.setupBlocked)+@($c.conflicts)+@($c.heldConsumes)) {
        if ($entry.requestId -cne $o.requestId -or $entry.commandSHA256 -cne $o.commandSHA256 -or $entry.journalSHA256 -cne $o.journalSHA256 -or
            $entry.bodySHA256 -cne 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855') { throw 'Slot conflict changed original request/body/journal.' }
        $time=$entry.elapsedMilliseconds
        if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or $time -lt $previous) { throw 'Invalid slot-conflict time.' }
        $previous=[double]$time
    }
    $armed=$c.armed.elapsedMilliseconds
    if ($null -eq $armed -or $armed -isnot [ValueType] -or [double]::IsNaN([double]$armed) -or [double]::IsInfinity([double]$armed) -or
        $armed -lt $c.setupBlocked[-1].elapsedMilliseconds -or $armed -gt $c.conflicts[0].elapsedMilliseconds) { throw 'Invalid slot-conflict setup boundary.' }
    $previous=0.0
    foreach ($entry in $c.conflicts) {
        if ($entry.status -cne 'LocationConflict' -or $entry.statusCode -ne 409 -or $entry.acknowledgement -cne 'delivered' -or
            $null -eq $entry.bodyBytes -or $entry.bodyBytes -ne 0) { throw 'Missing actual delivered LocationConflict.' }
        $time=$entry.requestElapsedMilliseconds
        if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or
            $time -lt $previous -or $time -gt $entry.elapsedMilliseconds) { throw 'Invalid slot-conflict request order.' }
        $previous=[double]$entry.elapsedMilliseconds
    }
    $gap=([double]$c.conflicts[1].requestElapsedMilliseconds-[double]$c.conflicts[0].elapsedMilliseconds)/1000.0
    if ($gap -lt 1.5) { throw 'Known slot conflict did not cross the native grant timer.' }
    return $gap
}

function ConvertTo-NPCPickupAfterSlotConflictObservation {
    param([object]$State)
    $null=Get-NPCPickupSlotConflictGap $State
    $p=$State.prepared; $o=$p.origin; $c=$p.slotConflict
    if ($c.phase -cne 'released' -or @($p.consumes).Count -lt 1) { throw 'Slot conflict never resumed the original grant.' }
    $commit=$p.consumes[0].elapsedMilliseconds
    $released=$c.released.elapsedMilliseconds
    if ($null -eq $released -or $released -isnot [ValueType] -or [double]::IsNaN([double]$released) -or [double]::IsInfinity([double]$released) -or
        $released -lt $c.conflicts[-1].elapsedMilliseconds -or $released -gt $commit) { throw 'Invalid slot-conflict release boundary.' }
    $index=0; $previous=0.0
    foreach ($a in $p.preparations) {
        $time=$a.elapsedMilliseconds
        if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or $time -lt $previous -or
            $a.status -cne $(if ($index -eq 0) {'Prepared'} else {'AlreadyPrepared'}) -or $a.acknowledgement -cne 'delivered' -or
            $a.bodySHA256 -cne $o.prepareBodySHA256 -or $a.commandSHA256 -cne $o.commandSHA256 -or $a.journalSHA256 -cne $o.journalSHA256 -or
            $a.consumed -isnot [bool] -or $a.consumed -ne ($time -gt $commit)) { throw 'Slot conflict re-prepared a different/consumed origin.' }
        $previous=[double]$time; $index++
    }
    # All physical requests above remain in the raw proof. Reuse the existing
    # ack-loss checks on its original prepare + post-commit re-prepares only.
    $copy=$State | ConvertTo-Json -Depth 30 | ConvertFrom-Json
    $copy.prepared.preparations=@($copy.prepared.preparations[0])+@($copy.prepared.preparations | Where-Object consumed -eq $true)
    $copy.prepared.PSObject.Properties.Remove('slotConflict')
    return $copy
}

function Assert-NPCPickupUnconsumedSlotSnapshot {
    param([object]$Snapshot,[object]$Intent,[object]$Command,[object]$Origin)
    if ($Snapshot.sessionOriginCount -ne 1 -or $null -eq $Snapshot.origin -or $null -ne $Snapshot.item -or $null -ne $Snapshot.receipt -or
        $null -ne $Snapshot.origin.consumed_at -or $null -ne $Intent.consumedAt -or $Intent.status -cne 'Prepared' -or
        $Intent.dungeonSessionId -cne $Origin.sessionId -or $Intent.serverId -cne $Origin.serverId -or $Intent.dropId -cne $Origin.dropId -or
        $Intent.requestId -cne $Origin.requestId -or $Intent.itemId -cne $Origin.itemId -or $Intent.characterId -cne $Origin.characterId -or
        $Snapshot.origin.request_id -cne $Origin.requestId -or $Snapshot.origin.drop_id -cne $Origin.dropId -or
        $Snapshot.origin.dungeon_session_id -cne $Origin.sessionId -or $Snapshot.origin.server_id -cne $Origin.serverId -or
        (ConvertTo-NPCPickupCanonicalJson $Snapshot.origin.command_payload) -cne (ConvertTo-NPCPickupCanonicalJson $Command) -or
        (ConvertTo-NPCPickupCanonicalJson $Intent.command) -cne (ConvertTo-NPCPickupCanonicalJson $Command)) { throw 'Known conflict consumed/rebound the original reservation or created an item/receipt.' }
}

function Invoke-NPCPickupSlotConflict {
    $setup=Wait-ProxyState {param($s) $s.phase -eq 'slot-setup'}
    $o=$setup.prepared.origin
    if ($o.sessionId -cne $sessionId -or $o.serverId -cne $serverId -or $o.journalSchemaVersion -ne 2 -or
        $members.CharacterId -cnotcontains $o.characterId) { throw 'Unexpected slot-conflict scope.' }
    $file=Join-Path $runRoot "server-User\Saved\ItemPickupJournal\$sessionId\$($o.requestId).json"
    if ((Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -cne $o.journalSHA256) { throw 'Slot-conflict journal changed.' }
    $archive=Join-Path $runRoot 'SlotConflict.journal-v2.json'; Copy-Item -LiteralPath $file -Destination $archive
    $journal=Get-Content -LiteralPath $archive -Raw -Encoding UTF8 | ConvertFrom-Json
    $command=$journal.body | ConvertFrom-Json
    $command.mutations[0].expectedRevision=0; $command.mutations[0].newRecord.revision=0
    $path="dungeon-sessions/$sessionId/pickup-intents/$($o.requestId)"
    $intent=Invoke-Api $path 'Get' $serverHeaders
    $initial=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
    Assert-NPCPickupUnconsumedSlotSnapshot $initial $intent $command $o
    $initial | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $runRoot 'SlotConflict.original.database.proof.json') -Encoding UTF8
    # A separate harness command creates one temporary blocker. It never changes
    # the native request or writes directly to SQL / UE projection / saved assets.
    $block=$command | ConvertTo-Json -Depth 40 | ConvertFrom-Json
    $block.requestId=[Guid]::NewGuid().ToString(); $block.operation='Item.FixtureSlotBlock'; $block.commandFingerprint="slot-block-$runId"
    $block.mutations[0].newRecord.state.instanceId=[Guid]::NewGuid().ToString()
    $block.mutations[0].newRecord.metadata.creationSource='NPCPickupSlotConflictFixture'
    $created=Invoke-Api 'item-transactions/commit' 'Post' $serverHeaders $block
    if ($created.status -cne 'Committed' -or @($created.records).Count -ne 1 -or $created.records[0].state.instanceId -cne $block.mutations[0].newRecord.state.instanceId -or
        $created.records[0].revision -ne 1 -or $created.records[0].location.slotIndex -ne $command.mutations[0].newRecord.location.slotIndex -or
        $created.records[0].owner.ownerId -cne $o.characterId) { throw 'Failed to occupy the exact original slot.' }
    $ready=Invoke-ProxyControl 'slot-conflict-ready' @{}
    if ($ready.phase -cne 'slot-conflict') { throw 'Slot conflict fixture not armed.' }
    $known=Wait-ProxyState {param($s) $s.phase -eq 'slot-conflict-held' -and @($s.prepared.slotConflict.conflicts).Count -ge 2}
    $known | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $runRoot 'SlotConflict.known.proxy.proof.json') -Encoding UTF8
    $script:slotConflictGap=Get-NPCPickupSlotConflictGap $known
    $intentAfter=Invoke-Api $path 'Get' $serverHeaders
    $after=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
    Assert-NPCPickupUnconsumedSlotSnapshot $after $intentAfter $command $o
    if ((ConvertTo-NPCPickupCanonicalJson $initial) -cne (ConvertTo-NPCPickupCanonicalJson $after)) { throw 'Known 409 changed the original full DB rows.' }
    $after | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $runRoot 'SlotConflict.known.database.proof.json') -Encoding UTF8
    New-Item -ItemType File -Path $pickupCommittedFile | Out-Null
    Wait-Marker $serverLog 'RPG_NPC_PICKUP SERVER_UNCERTAIN '
    # The existing native marker measures pending/locked/empty state. At this
    # boundary the DB outcome is known: no original item, no receipt, unconsumed.
    $other=@($members | Where-Object CharacterId -cne $o.characterId)[0].CharacterId
    $reboundRequests=@()
    foreach ($kind in @('new-request','new-slot','other-claimant')) {
        $changed=$command | ConvertTo-Json -Depth 40 | ConvertFrom-Json
        if ($kind -ne 'new-slot') {
            $changed.requestId=[Guid]::NewGuid().ToString(); $changed.mutations[0].newRecord.state.instanceId=[Guid]::NewGuid().ToString()
            $reboundRequests+=$changed.requestId
        }
        if ($kind -eq 'new-slot') { $changed.mutations[0].newRecord.location.slotIndex++ }
        if ($kind -eq 'other-claimant') {
            $changed.actor.ownerId=$other; $changed.mutations[0].newRecord.owner.ownerId=$other; $changed.mutations[0].newRecord.location.containerId=$other
        }
        $null=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents" 'Post' $serverHeaders @{dropId=$o.dropId;command=$changed} 409
    }
    foreach ($id in $reboundRequests) {
        $null=Invoke-Api "dungeon-sessions/$sessionId/pickup-intents/$id" 'Get' $serverHeaders $null 404
        $null=Invoke-Api "item-transactions/$id" 'Get' $serverHeaders $null 404
    }
    # Retire only this run's blocker using the normal server commit API; keep
    # its terminal record/receipts for evidence instead of deleting any rows.
    $terminal=$created.records[0] | ConvertTo-Json -Depth 40 | ConvertFrom-Json
    $terminal.lifecycleState='Destroyed'; $terminal.state.quantity=0
    $terminal.location=[pscustomobject]@{containerType='Terminal';containerId='';slotIndex=-1}
    $retireRequest=[Guid]::NewGuid().ToString()
    $retired=Invoke-Api 'item-transactions/commit' 'Post' $serverHeaders @{requestId=$retireRequest;operation='Item.Destroy';commandFingerprint="slot-retire-$runId";
        actor=$command.actor;affectedQuantity=1;mutations=@(@{expectedRevision=1;newRecord=$terminal})}
    $blocker=Invoke-Api "items/$($terminal.state.instanceId)" 'Get' $serverHeaders
    if ($retired.status -cne 'Committed' -or $blocker.revision -ne 2 -or $blocker.lifecycleState -cne 'Destroyed' -or
        $blocker.state.quantity -ne 0 -or $blocker.location.containerType -cne 'Terminal' -or
        (ConvertTo-NPCPickupCanonicalJson $blocker) -cne (ConvertTo-NPCPickupCanonicalJson $retired.records[0])) { throw 'Owned blocker not retired exactly once.' }
    $free=Get-NPCPickupPreparedDatabaseSnapshot $projectRoot $sessionId $o.requestId $o.itemId
    Assert-NPCPickupUnconsumedSlotSnapshot $free (Invoke-Api $path 'Get' $serverHeaders) $command $o
    if ((ConvertTo-NPCPickupCanonicalJson $initial) -cne (ConvertTo-NPCPickupCanonicalJson $free) -or
        (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -cne $o.journalSHA256) { throw 'Reservation/journal changed while freeing the original slot.' }
    [ordered]@{schemaVersion=1;passed=$true;scope='Known LocationConflict boundary only; eventual native grant not yet asserted';
        origin=$o;originalRowsUnchanged=$true;unconsumed=$true;originalItemAndReceiptAbsent=$true;nativePendingAndEmptyProjection=$true;
        forbiddenRebinds=@('new-request','new-slot','other-claimant');reboundRequestIds=$reboundRequests;
        blockerCreateReceipt=$created;blockerRetireReceipt=$retired;retiredBlocker=$blocker;nativeConflictTimerGapSeconds=$slotConflictGap;
        journalArchive=$archive;journalSHA256=$o.journalSHA256} | ConvertTo-Json -Depth 40 |
        Set-Content -LiteralPath (Join-Path $runRoot 'SlotConflict.boundary.proof.json') -Encoding UTF8
    $resumed=Invoke-ProxyControl 'slot-conflict-resume' @{}
    if ($resumed.prepared.slotConflict.phase -cne 'released') { throw 'Original-slot retry not released.' }
}
