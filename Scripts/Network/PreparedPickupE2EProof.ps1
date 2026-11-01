function Get-NPCPickupPreparedDropId {
    param([string]$Row)
    $match=[regex]::Match($Row,'^RPG_NPC_PICKUP SERVER_DROP NPC=([1-9]\d*) Pickup=([1-9]\d*) Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1 DropId=([0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12})$')
    if (-not $match.Success -or $match.Groups[3].Value -eq [Guid]::Empty.ToString()) { throw 'Missing canonical server Drop ID.' }
    return $match.Groups[3].Value
}

function Assert-NPCPickupPreparedObservation {
    param([object]$State,[string]$SessionId,[string]$ServerId,[string]$DropId,[string]$RequestId,[string]$ItemId,[string]$CharacterId,
        [ValidateSet('none','prepare-once','consume-held')][string]$FaultMode='none')
    if ($State.prepared.slotConflict) { $State=ConvertTo-NPCPickupAfterSlotConflictObservation $State }
    if ($FaultMode -ne 'none') {
        Assert-NPCPickupPreparedFaultObservation $State $SessionId $ServerId $DropId $RequestId $ItemId $CharacterId $FaultMode
        return
    }
    foreach ($id in @($SessionId,$DropId,$RequestId,$ItemId,$CharacterId)) {
        if ($id -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$' -or $id -eq [Guid]::Empty.ToString()) { throw 'Invalid prepared proof identity.' }
    }
    $p=$State.prepared; $o=$p.origin
    if ($State.failure -or $State.phase -ne 'observed' -or $State.droppedAcknowledgements -ne 0 -or
        $null -eq $p -or $p.rejectedWrites -ne 0 -or @($p.preparations).Count -ne 1 -or @($p.consumes).Count -ne 1 -or
        $o.sessionId -cne $SessionId -or $o.serverId -cne $ServerId -or $o.dropId -cne $DropId -or
        $o.requestId -cne $RequestId -or $o.itemId -cne $ItemId -or $o.characterId -cne $CharacterId -or
        $o.journalSchemaVersion -ne 2 -or $o.journalVerifiedBeforePrepare -isnot [bool] -or -not $o.journalVerifiedBeforePrepare) {
        throw 'Missing/borrowed prepared pickup boundary.'
    }
    foreach ($value in @($o.commandSHA256,$o.prepareBodySHA256,$o.journalSHA256,$p.consumes[0].recordSHA256)) {
        if ($value -cnotmatch '^[0-9a-f]{64}$') { throw 'Missing prepared proof hash.' }
    }
    $a=$p.preparations[0]; $b=$p.consumes[0]
    if ($a.status -cne 'Prepared' -or $b.status -cne 'Committed' -or $b.requestId -cne $RequestId -or $b.itemId -cne $ItemId -or
        $a.commandSHA256 -cne $o.commandSHA256 -or $b.commandSHA256 -cne $o.commandSHA256 -or
        $a.bodySHA256 -cne $o.prepareBodySHA256 -or $null -eq $b.bodyBytes -or $b.bodyBytes -ne 0 -or
        $b.bodySHA256 -cne 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855') { throw 'Changed prepare/consume boundary.' }
    $previous=0.0
    foreach ($entry in @($a,$b)) {
        $time=$entry.elapsedMilliseconds
        if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or
            [double]::IsInfinity([double]$time) -or $time -lt $previous) { throw 'Invalid prepared monotonic ordering.' }
        $previous=[double]$time
    }
}

function Get-NPCPickupPreparedRetryGap {
    param([object]$State)
    if ($State.prepared.slotConflict) { $State=ConvertTo-NPCPickupAfterSlotConflictObservation $State }
    $p=$State.prepared; $o=$p.origin
    if ($State.failure -or $State.phase -cne 'blocked' -or $State.droppedAcknowledgements -ne 1 -or $p.fault -cne 'consume-held' -or
        @($p.preparations).Count -lt 2 -or @($p.consumes).Count -ne 1 -or @($State.blockedRetries).Count -lt 4 -or
        $p.preparations[1].status -cne 'AlreadyPrepared' -or $p.preparations[1].acknowledgement -cne 'delivered' -or
        $p.preparations[1].consumed -ne $true -or $p.preparations[1].bodySHA256 -cne $o.prepareBodySHA256 -or
        $p.preparations[1].commandSHA256 -cne $o.commandSHA256 -or $p.preparations[1].journalSHA256 -cne $o.journalSHA256 -or
        $State.committed.requestId -cne $o.requestId -or $State.committed.upstreamStatus -cne 'Committed' -or
        $p.consumes[0].acknowledgement -cne 'dropped' -or $State.committed.bodySHA256 -cne 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855') {
        throw 'Missing original consumed reservation/re-prepare timer boundary.'
    }
    # Empty POSTs can produce a different number of wire requests than the
    # configured gateway burst. A new prepare after a delivered prepare and a
    # dropped committed consume is the explicit next native grant context.
    $next=$p.preparations[1].requestElapsedMilliseconds
    $previous=$State.committed.elapsedMilliseconds
    $responded=$p.preparations[1].elapsedMilliseconds
    foreach ($time in @($next,$previous,$responded)) {
        if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or $time -lt 0) { throw 'Invalid re-prepare request timestamp.' }
    }
    if ($next -gt $responded) { throw 'Re-prepare request follows its response.' }
    $before=[double]$previous
    foreach ($retry in $State.blockedRetries) {
        $time=$retry.elapsedMilliseconds
        if ($retry.stage -cne 'consume' -or $retry.requestId -cne $o.requestId -or $retry.bodySHA256 -cne $State.committed.bodySHA256 -or
            $retry.commandSHA256 -cne $o.commandSHA256 -or $retry.journalSHA256 -cne $o.journalSHA256 -or
            $null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or $time -lt $previous) { throw 'Invalid frozen blocked consume.' }
        $previous=[double]$time
        if ($time -lt $next) { $before=[double]$time }
    }
    $gap=([double]$next-$before)/1000.0
    if ($gap -lt 1.5) { throw 'Re-prepare request did not cross the native grant timer.' }
    return $gap
}

function Assert-NPCPickupPreparedFaultObservation {
    param([object]$State,[string]$SessionId,[string]$ServerId,[string]$DropId,[string]$RequestId,[string]$ItemId,[string]$CharacterId,[string]$Mode)
    $p=$State.prepared; $o=$p.origin
    $held=$Mode -ceq 'consume-held'
    if ($State.failure -or $p.fault -cne $Mode -or $State.droppedAcknowledgements -ne 1 -or
        $State.phase -cne $(if ($held) {'recovered'} else {'observed'}) -or
        @($p.preparations).Count -lt 2 -or @($p.consumes).Count -ne $(if ($held) {2} else {1}) -or
        (-not $held -and (@($p.preparations).Count -ne 2 -or @($State.blockedRetries).Count -ne 0))) { throw 'Missing prepared acknowledgement-loss boundary.' }
    # Reuse the unchanged strict basic identity/full-body/hash checks for the
    # original prepare and original Committed receipt, not a later substitute.
    $basic=$State | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $basic.phase='observed'; $basic.droppedAcknowledgements=0
    $basic.prepared.preparations=@($basic.prepared.preparations[0]); $basic.prepared.consumes=@($basic.prepared.consumes[0])
    Assert-NPCPickupPreparedObservation $basic $SessionId $ServerId $DropId $RequestId $ItemId $CharacterId
    $index=0; $previous=0.0
    foreach ($entry in $p.preparations) {
        $expectedAck=if (-not $held -and $index -eq 0) {'dropped'} else {'delivered'}
        if ($entry.status -cne $(if ($index -eq 0) {'Prepared'} else {'AlreadyPrepared'}) -or $entry.acknowledgement -cne $expectedAck -or
            $entry.commandSHA256 -cne $o.commandSHA256 -or $entry.bodySHA256 -cne $o.prepareBodySHA256 -or
            $entry.journalSHA256 -cne $o.journalSHA256 -or $entry.consumed -isnot [bool] -or $entry.consumed -ne ($held -and $index -gt 0)) { throw 'Changed prepared retry origin/body/journal.' }
        $time=$entry.elapsedMilliseconds
        if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or
            $time -lt $previous -or ($held -and $index -gt 0 -and $time -lt $p.consumes[0].elapsedMilliseconds)) { throw 'Invalid prepared retry time.' }
        $previous=[double]$time; $index++
    }
    $previous=[double]$p.preparations[0].elapsedMilliseconds
    $index=0
    foreach ($entry in $p.consumes) {
        if ($entry.status -cne $(if ($index -eq 0) {'Committed'} else {'AlreadyCommitted'}) -or
            $entry.acknowledgement -cne $(if ($held -and $index -eq 0) {'dropped'} else {'delivered'}) -or
            $entry.requestId -cne $RequestId -or $entry.itemId -cne $ItemId -or $entry.bodyBytes -ne 0 -or $null -eq $entry.bodyBytes -or
            $entry.bodySHA256 -cne 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855' -or
            $entry.commandSHA256 -cne $o.commandSHA256 -or $entry.journalSHA256 -cne $o.journalSHA256 -or
            $entry.recordSHA256 -cne $p.consumes[0].recordSHA256) { throw 'Changed prepared consume retry/receipt.' }
        $time=$entry.elapsedMilliseconds
        if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or
            $time -lt $previous) { throw 'Invalid consume retry time.' }
        $previous=[double]$time; $index++
    }
    if ($p.consumes[-1].elapsedMilliseconds -lt $p.preparations[-1].elapsedMilliseconds) { throw 'Consume completed before final prepare.' }
    if ($held) {
        if (@($State.blockedRetries).Count -lt 4) { throw 'Missing held native retries.' }
        $previous=[double]$p.consumes[0].elapsedMilliseconds
        foreach ($retry in $State.blockedRetries) {
            if ($retry.stage -cne 'consume' -or $retry.requestId -cne $RequestId -or $retry.bodySHA256 -cne $p.consumes[0].bodySHA256 -or
                $retry.commandSHA256 -cne $o.commandSHA256 -or $retry.journalSHA256 -cne $o.journalSHA256) { throw 'Changed blocked consume retry.' }
            $time=$retry.elapsedMilliseconds
            if ($null -eq $time -or $time -isnot [ValueType] -or [double]::IsNaN([double]$time) -or [double]::IsInfinity([double]$time) -or
                $time -lt $previous -or $time -gt $p.consumes[-1].elapsedMilliseconds) { throw 'Invalid blocked retry time.' }
            $previous=[double]$time
        }
        foreach ($boundary in @($State.committed,$State.delivered)) {
            if ($boundary.requestId -cne $RequestId -or $boundary.itemId -cne $ItemId -or $boundary.bodySHA256 -cne $p.consumes[0].bodySHA256 -or
                $boundary.commandSHA256 -cne $o.commandSHA256 -or $boundary.recordSHA256 -cne $p.consumes[0].recordSHA256 -or
                $boundary.journalSHA256 -cne $o.journalSHA256) { throw 'Borrowed committed/delivered prepared boundary.' }
        }
        if ($State.committed.upstreamStatus -cne 'Committed' -or $State.delivered.upstreamStatus -cne 'AlreadyCommitted') { throw 'Prepared replay was not idempotent.' }
    }
}

# One read-only statement: snapshot the three atomic rows in the existing local
# development database. Only canonical UUIDs are interpolated, never server IDs,
# user strings, credentials or an arbitrary connection string.
function Get-NPCPickupPreparedDatabaseSnapshot {
    param([string]$ProjectRoot,[string]$SessionId,[string]$RequestId,[string]$ItemId)
    foreach ($id in @($SessionId,$RequestId,$ItemId)) {
        if ($id -cnotmatch '^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$' -or $id -eq [Guid]::Empty.ToString()) { throw 'Invalid database snapshot identity.' }
    }
    $sql=@"
SELECT json_build_object(
 'origin', (SELECT to_jsonb(p) FROM dungeon_pickup_intents p WHERE dungeon_session_id='$SessionId'::uuid AND request_id='$RequestId'::uuid),
 'item', (SELECT to_jsonb(i) FROM item_records i WHERE item_id='$ItemId'::uuid),
 'receipt', (SELECT to_jsonb(r) FROM item_transaction_receipts r WHERE request_id='$RequestId'::uuid),
 'sessionOriginCount', (SELECT count(*) FROM dungeon_pickup_intents WHERE dungeon_session_id='$SessionId'::uuid));
"@
    $output=@(& docker compose -f (Join-Path $ProjectRoot 'Backend\compose.postgres.yml') exec -T postgres psql -X -t -A -q -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -c $sql 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Read-only prepared PostgreSQL snapshot failed; database output omitted.' }
    try { return ($output -join "`n") | ConvertFrom-Json } catch { throw 'Invalid PostgreSQL snapshot response.' }
}

function Assert-NPCPickupPreparedDatabaseSnapshot {
    param([object]$Snapshot,[object]$Intent,[object]$Receipt,[string]$SessionId,[string]$ServerId,[string]$DropId,[string]$CharacterId)
    $p=$Snapshot.origin; $i=$Snapshot.item; $r=$Snapshot.receipt
    if ($Snapshot.sessionOriginCount -ne 1 -or $null -eq $p -or $null -eq $i -or $null -eq $r -or
        $p.dungeon_session_id -cne $SessionId -or $p.server_id -cne $ServerId -or $p.drop_id -cne $DropId -or
        $p.character_id -cne $CharacterId -or $p.request_id -cne $Receipt.requestId -or $p.item_id -cne $i.item_id -or
        -not $p.prepared_at -or -not $p.consumed_at -or $Intent.consumedAt -eq $null -or
        $Intent.dungeonSessionId -cne $SessionId -or $Intent.serverId -cne $ServerId -or $Intent.characterId -cne $CharacterId -or
        $Intent.dropId -cne $DropId -or $Intent.requestId -cne $Receipt.requestId -or $Intent.itemId -cne $i.item_id -or
        $i.item_id -cne $Receipt.records[0].state.instanceId -or $i.quantity -ne 1 -or $i.revision -ne 1 -or
        $i.owner_type -cne 'Character' -or $i.owner_id -cne $CharacterId -or $i.container_type -cne 'Inventory' -or
        $i.container_id -cne $CharacterId -or $i.slot_index -ne 0 -or
        $r.request_id -cne $Receipt.requestId -or $r.operation -cne 'Item.Pickup' -or
        $r.command_fingerprint -cne $Receipt.commandFingerprint -or $r.actor_type -cne 'Character' -or
        $r.actor_id -cne $CharacterId -or $r.affected_quantity -ne 1 -or @($r.result_records).Count -ne 1 -or
        $r.result_records[0].itemId -cne $i.item_id -or $r.result_records[0].state.instanceId -cne $i.item_id -or $r.result_records[0].state.quantity -ne 1 -or
        $r.result_records[0].revision -ne 1) { throw 'Prepared origin/item/receipt atomic row mismatch.' }
    # PostgreSQL jsonb key ordering differs from Web DTO ordering. Compare the
    # full normalized documents, not a fingerprint/subset of item fields.
    # The repository serializes the domain ItemRecord, including its computed
    # ItemId alias; the Web contract intentionally omits just that alias. Check
    # the alias against the primary/state ID before projecting it away.
    $webRecord=$r.result_records[0] | ConvertTo-Json -Depth 40 | ConvertFrom-Json
    $webRecord.PSObject.Properties.Remove('itemId')
    $c=$p.command_payload
    $expected=$c.mutations[0].newRecord | ConvertTo-Json -Depth 40 | ConvertFrom-Json
    $expected.revision=1
    if ($c.requestId -cne $Receipt.requestId -or $c.operation -cne $Receipt.operation -or $c.affectedQuantity -ne 1 -or
        $c.commandFingerprint -cne $Receipt.commandFingerprint -or @($c.mutations).Count -ne 1 -or
        $c.mutations[0].expectedRevision -ne 0 -or $c.mutations[0].newRecord.revision -ne 0 -or
        (ConvertTo-NPCPickupCanonicalJson $c.actor) -cne (ConvertTo-NPCPickupCanonicalJson $Receipt.actor) -or
        (ConvertTo-NPCPickupCanonicalJson $p.command_payload) -cne (ConvertTo-NPCPickupCanonicalJson $Intent.command) -or
        (ConvertTo-NPCPickupCanonicalJson $expected) -cne (ConvertTo-NPCPickupCanonicalJson $Receipt.records[0]) -or
        (ConvertTo-NPCPickupCanonicalJson $webRecord) -cne (ConvertTo-NPCPickupCanonicalJson $Receipt.records[0])) {
        throw 'Prepared database full command/receipt mismatch.'
    }
}

function ConvertTo-NPCPickupCanonicalJson {
    param([object]$Value)
    function Convert-OrderedValue([object]$Node) {
        if ($null -eq $Node) { return $null }
        if ($Node -is [Collections.IDictionary]) {
            $result=[ordered]@{}; foreach ($key in @($Node.Keys | Sort-Object -CaseSensitive)) { $result[$key]=Convert-OrderedValue $Node[$key] }; return $result
        }
        if ($Node -is [pscustomobject]) {
            $result=[ordered]@{}; foreach ($key in @($Node.PSObject.Properties.Name | Sort-Object -CaseSensitive)) { $result[$key]=Convert-OrderedValue $Node.$key }; return $result
        }
        if ($Node -is [Collections.IEnumerable] -and $Node -isnot [string]) { return ,@($Node | ForEach-Object { Convert-OrderedValue $_ }) }
        return $Node
    }
    Convert-OrderedValue $Value | ConvertTo-Json -Depth 40 -Compress
}
