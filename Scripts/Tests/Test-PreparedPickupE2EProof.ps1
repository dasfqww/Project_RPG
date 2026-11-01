$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Network\PreparedPickupE2EProof.ps1')
. (Join-Path $PSScriptRoot '..\Network\NPCPickupSlotConflict.ps1')
$session='11111111-1111-1111-1111-111111111111'
$drop='22222222-2222-2222-2222-222222222222'
$request='33333333-3333-3333-3333-333333333333'
$item='44444444-4444-4444-4444-444444444444'
$character='55555555-5555-5555-5555-555555555555'
$server='prepared-proof-fixture'
$hash='a'*64
$empty='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
$count=0
function New-Observation {
    [pscustomobject]@{phase='observed';failure=$null;droppedAcknowledgements=0;prepared=[pscustomobject]@{
        rejectedWrites=0;origin=[pscustomobject]@{sessionId=$session;serverId=$server;dropId=$drop;requestId=$request;itemId=$item;
            characterId=$character;commandSHA256=$hash;prepareBodySHA256=$hash;journalSHA256=$hash;journalSchemaVersion=2;journalVerifiedBeforePrepare=$true};
        preparations=@([pscustomobject]@{status='Prepared';commandSHA256=$hash;bodySHA256=$hash;elapsedMilliseconds=10});
        consumes=@([pscustomobject]@{status='Committed';requestId=$request;itemId=$item;commandSHA256=$hash;recordSHA256=$hash;
            bodyBytes=0;bodySHA256=$empty;elapsedMilliseconds=20})}}
}
Assert-NPCPickupPreparedObservation (New-Observation) $session $server $drop $request $item $character
$count++
foreach ($case in @(
    @{name='borrowed session';change={param($s) $s.prepared.origin.sessionId=$drop}},
    @{name='borrowed server';change={param($s) $s.prepared.origin.serverId='other'}},
    @{name='borrowed drop';change={param($s) $s.prepared.origin.dropId=$session}},
    @{name='borrowed request';change={param($s) $s.prepared.origin.requestId=$session}},
    @{name='borrowed item';change={param($s) $s.prepared.origin.itemId=$session}},
    @{name='borrowed claimant';change={param($s) $s.prepared.origin.characterId=$session}},
    @{name='v1 journal';change={param($s) $s.prepared.origin.journalSchemaVersion=1}},
    @{name='no persisted journal';change={param($s) $s.prepared.origin.journalVerifiedBeforePrepare=$false}},
    @{name='string truth';change={param($s) $s.prepared.origin.journalVerifiedBeforePrepare='true'}},
    @{name='missing journal hash';change={param($s) $s.prepared.origin.journalSHA256=$null}},
    @{name='second prepare';change={param($s) $s.prepared.preparations+= $s.prepared.preparations[0]}},
    @{name='second consume';change={param($s) $s.prepared.consumes+= $s.prepared.consumes[0]}},
    @{name='prepared receipt only';change={param($s) $s.prepared.consumes=@()}},
    @{name='legacy fault result';change={param($s) $s.phase='recovered'}},
    @{name='fault injection';change={param($s) $s.droppedAcknowledgements=1}},
    @{name='unobserved body';change={param($s) $s.prepared.consumes[0].bodyBytes=$null}},
    @{name='nonempty consume';change={param($s) $s.prepared.consumes[0].bodyBytes=2}},
    @{name='changed consume body';change={param($s) $s.prepared.consumes[0].bodySHA256=$hash}},
    @{name='changed full command';change={param($s) $s.prepared.consumes[0].commandSHA256='b'*64}},
    @{name='changed prepare bytes';change={param($s) $s.prepared.preparations[0].bodySHA256='b'*64}},
    @{name='borrowed consume';change={param($s) $s.prepared.consumes[0].requestId=$session}},
    @{name='no first commit';change={param($s) $s.prepared.consumes[0].status='AlreadyCommitted'}},
    @{name='reversed monotonic order';change={param($s) $s.prepared.consumes[0].elapsedMilliseconds=9}},
    @{name='NaN timestamp';change={param($s) $s.prepared.preparations[0].elapsedMilliseconds=[double]::NaN}},
    @{name='missing timestamp';change={param($s) $s.prepared.preparations[0].elapsedMilliseconds=$null}},
    @{name='rejected write';change={param($s) $s.prepared.rejectedWrites=1}},
    @{name='late failure';change={param($s) $s.failure='Failed'}}
)) {
    $s=New-Observation; & $case.change $s; $threw=$false
    try { Assert-NPCPickupPreparedObservation $s $session $server $drop $request $item $character } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid prepared proof: $($case.name)" }; $count++
}
$row="RPG_NPC_PICKUP SERVER_DROP NPC=38 Pickup=62 Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1 DropId=$drop"
if ((Get-NPCPickupPreparedDropId $row) -cne $drop) { throw 'Valid server Drop ID rejected.' }; $count++
foreach ($bad in @($row.Replace($drop,[Guid]::Empty.ToString()),$row.Replace(" DropId=$drop",''),"$row extra=1")) {
    $threw=$false; try { $null=Get-NPCPickupPreparedDropId $bad } catch { $threw=$true }
    if (-not $threw) { throw 'Accepted invalid Drop ID marker.' }; $count++
}
$a='{"z":[],"a":{"c":null,"b":[{"y":2,"x":1}]}}' | ConvertFrom-Json
$b='{"a":{"b":[{"x":1,"y":2}],"c":null},"z":[]}' | ConvertFrom-Json
if ((ConvertTo-NPCPickupCanonicalJson $a) -cne (ConvertTo-NPCPickupCanonicalJson $b)) { throw 'jsonb key order changed semantic proof.' }; $count++
if ((ConvertTo-NPCPickupCanonicalJson $a) -cne '{"a":{"b":[{"x":1,"y":2}],"c":null},"z":[]}') { throw 'Canonicalization lost array/null shape.' }; $count++
function New-DatabaseFixture {
    $record=[pscustomobject]@{state=[pscustomobject]@{instanceId=$item;quantity=1;generationSeed=123;statValues=@();instanceTags=@()};
        owner=[pscustomobject]@{type='Character';ownerId=$character};revision=0;metadata=[pscustomobject]@{creationSource='WorldPickup'}}
    $command=[pscustomobject]@{requestId=$request;operation='Item.Pickup';commandFingerprint='same-fingerprint';
        actor=[pscustomobject]@{type='Character';ownerId=$character};affectedQuantity=1;
        mutations=@([pscustomobject]@{expectedRevision=0;newRecord=$record})}
    $committed=$record | ConvertTo-Json -Depth 20 | ConvertFrom-Json; $committed.revision=1
    $domain=$committed | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $domain | Add-Member -NotePropertyName itemId -NotePropertyValue $item
    [pscustomobject]@{
        snapshot=[pscustomobject]@{sessionOriginCount=1;origin=[pscustomobject]@{dungeon_session_id=$session;server_id=$server;
            drop_id=$drop;character_id=$character;request_id=$request;item_id=$item;prepared_at='2026-10-15T00:00:00Z';
            consumed_at='2026-10-15T00:00:01Z';command_payload=$command};
            item=[pscustomobject]@{item_id=$item;quantity=1;revision=1;owner_type='Character';owner_id=$character;
                container_type='Inventory';container_id=$character;slot_index=0};
            receipt=[pscustomobject]@{request_id=$request;operation='Item.Pickup';command_fingerprint='same-fingerprint';
                actor_type='Character';actor_id=$character;affected_quantity=1;result_records=@($domain)}};
        intent=[pscustomobject]@{dungeonSessionId=$session;serverId=$server;dropId=$drop;characterId=$character;
            requestId=$request;itemId=$item;consumedAt='2026-10-15T00:00:01Z';command=($command|ConvertTo-Json -Depth 20|ConvertFrom-Json)};
        receipt=[pscustomobject]@{requestId=$request;operation='Item.Pickup';commandFingerprint='same-fingerprint';
            actor=[pscustomobject]@{type='Character';ownerId=$character};affectedQuantity=1;records=@($committed)}}
}
$f=New-DatabaseFixture
Assert-NPCPickupPreparedDatabaseSnapshot $f.snapshot $f.intent $f.receipt $session $server $drop $character
$count++
foreach ($case in @(
    @{name='domain alias mismatch';change={param($f) $f.snapshot.receipt.result_records[0].itemId=$drop}},
    @{name='no consumed marker';change={param($f) $f.snapshot.origin.consumed_at=$null}},
    @{name='duplicate origin';change={param($f) $f.snapshot.sessionOriginCount=2}},
    @{name='wrong API scope';change={param($f) $f.intent.dungeonSessionId=$drop}},
    @{name='changed command seed';change={param($f) $f.snapshot.origin.command_payload.mutations[0].newRecord.state.generationSeed++}},
    @{name='changed domain seed';change={param($f) $f.snapshot.receipt.result_records[0].state.generationSeed++}},
    @{name='changed receipt seed';change={param($f) $f.receipt.records[0].state.generationSeed++}},
    @{name='changed command actor';change={param($f) $f.snapshot.origin.command_payload.actor.ownerId=$drop}},
    @{name='unknown record field';change={param($f) $f.snapshot.receipt.result_records[0] | Add-Member extra 'hidden'}}
)) {
    $f=New-DatabaseFixture; & $case.change $f; $threw=$false
    try { Assert-NPCPickupPreparedDatabaseSnapshot $f.snapshot $f.intent $f.receipt $session $server $drop $character } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid database proof: $($case.name)" }; $count++
}
function New-FaultObservation([string]$Mode) {
    $s=New-Observation; $held=$Mode -ceq 'consume-held'
    $s.phase=if ($held) {'recovered'} else {'observed'}; $s.droppedAcknowledgements=1
    $s.prepared | Add-Member fault $Mode
    $s | Add-Member blockedRetries @()
    $s | Add-Member committed $null
    $s | Add-Member delivered $null
    $a=$s.prepared.preparations[0]; $b=$s.prepared.consumes[0]
    $a | Add-Member acknowledgement $(if ($held) {'delivered'} else {'dropped'})
    $a | Add-Member consumed $false; $a | Add-Member journalSHA256 $hash
    $b | Add-Member acknowledgement $(if ($held) {'dropped'} else {'delivered'})
    $b | Add-Member journalSHA256 $hash
    $a2=$a|ConvertTo-Json|ConvertFrom-Json
    $a2.status='AlreadyPrepared';$a2.acknowledgement='delivered';$a2.consumed=$held;$a2.elapsedMilliseconds=if ($held) {60} else {15}
    $s.prepared.preparations+= $a2
    if ($held) {
        $b2=$b|ConvertTo-Json|ConvertFrom-Json; $b2.status='AlreadyCommitted';$b2.acknowledgement='delivered';$b2.elapsedMilliseconds=100
        $s.prepared.consumes+=$b2
        $s.blockedRetries=@(foreach ($time in @(30,40,70,80)) { [pscustomobject]@{stage='consume';requestId=$request;
            bodySHA256=$empty;commandSHA256=$hash;journalSHA256=$hash;elapsedMilliseconds=$time} })
        $s.committed=$b|ConvertTo-Json|ConvertFrom-Json; $s.committed | Add-Member upstreamStatus 'Committed'
        $s.delivered=$b2|ConvertTo-Json|ConvertFrom-Json; $s.delivered | Add-Member upstreamStatus 'AlreadyCommitted'
    }
    return $s
}
foreach ($mode in @('prepare-once','consume-held')) {
    Assert-NPCPickupPreparedObservation (New-FaultObservation $mode) $session $server $drop $request $item $character -FaultMode $mode
    $count++
    foreach ($case in @(
        @{name='wrong fault';change={param($s) $s.prepared.fault='unknown'}},
        @{name='not exactly one drop';change={param($s) $s.droppedAcknowledgements=2}},
        @{name='wrong original acknowledgement';change={param($s) $s.prepared.preparations[0].acknowledgement='pending'}},
        @{name='changed envelope';change={param($s) $s.prepared.preparations[1].bodySHA256='b'*64}},
        @{name='changed journal';change={param($s) $s.prepared.preparations[1].journalSHA256='b'*64}},
        @{name='wrong consumed state';change={param($s) $s.prepared.preparations[1].consumed=-not $s.prepared.preparations[1].consumed}},
        @{name='changed consume body';change={param($s) $s.prepared.consumes[-1].bodyBytes=2}},
        @{name='missing receipt hash';change={param($s) $s.prepared.consumes[-1].recordSHA256='not-a-hash'}},
        @{name='wrong retry status';change={param($s) $s.prepared.preparations[1].status='Prepared'}}
    )) {
        $s=New-FaultObservation $mode; & $case.change $s; $threw=$false
        try { Assert-NPCPickupPreparedObservation $s $session $server $drop $request $item $character -FaultMode $mode } catch { $threw=$true }
        if (-not $threw) { throw "Accepted invalid $mode proof: $($case.name)" }; $count++
    }
}
foreach ($case in @(
    @{name='missing native burst';change={param($s) $s.blockedRetries=@($s.blockedRetries[0])}},
    @{name='wrong blocked stage';change={param($s) $s.blockedRetries[0].stage='prepare'}},
    @{name='changed blocked request';change={param($s) $s.blockedRetries[0].requestId=$drop}},
    @{name='changed blocked command';change={param($s) $s.blockedRetries[0].commandSHA256='b'*64}},
    @{name='changed blocked body';change={param($s) $s.blockedRetries[0].bodySHA256='b'*64}},
    @{name='changed original record';change={param($s) $s.committed.recordSHA256='b'*64}},
    @{name='changed replay record';change={param($s) $s.prepared.consumes[-1].recordSHA256='b'*64}},
    @{name='changed recovered command';change={param($s) $s.delivered.commandSHA256='b'*64}},
    @{name='wrong recovered status';change={param($s) $s.delivered.upstreamStatus='Committed'}},
    @{name='nonfinite blocked time';change={param($s) $s.blockedRetries[0].elapsedMilliseconds=[double]::NaN}},
    @{name='prepare before durable commit';change={param($s) $s.prepared.preparations[1].elapsedMilliseconds=19}}
)) {
    $s=New-FaultObservation 'consume-held'; & $case.change $s; $threw=$false
    try { Assert-NPCPickupPreparedObservation $s $session $server $drop $request $item $character -FaultMode 'consume-held' } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid consume retry proof: $($case.name)" }; $count++
}
function New-TimerObservation {
    $s=New-FaultObservation 'consume-held'; $s.phase='blocked'
    $s.prepared.consumes=@($s.prepared.consumes[0]); $s.delivered=$null
    $s.prepared.preparations[1] | Add-Member requestElapsedMilliseconds 2050
    $s.prepared.preparations[1].elapsedMilliseconds=2060
    $s.blockedRetries[0].elapsedMilliseconds=30; $s.blockedRetries[1].elapsedMilliseconds=40
    $s.blockedRetries[2].elapsedMilliseconds=2100; $s.blockedRetries[3].elapsedMilliseconds=2110
    return $s
}
$gap=Get-NPCPickupPreparedRetryGap (New-TimerObservation)
if ([Math]::Abs($gap-2.01) -gt 0.0001) { throw 'Explicit re-prepare boundary misread as a configured consume wire-count index.' }; $count++
foreach ($case in @(
    @{name='missing arrival';change={param($s) $s.prepared.preparations[1].requestElapsedMilliseconds=$null}},
    @{name='NaN arrival';change={param($s) $s.prepared.preparations[1].requestElapsedMilliseconds=[double]::NaN}},
    @{name='response before request';change={param($s) $s.prepared.preparations[1].elapsedMilliseconds=100}},
    @{name='short real gap';change={param($s) $s.prepared.preparations[1].requestElapsedMilliseconds=100}},
    @{name='unconsumed origin';change={param($s) $s.prepared.preparations[1].consumed=$false}},
    @{name='missing delivered reservation';change={param($s) $s.prepared.preparations[1].acknowledgement='pending'}},
    @{name='borrowed reservation';change={param($s) $s.prepared.preparations[1].commandSHA256='b'*64}},
    @{name='changed consume hash';change={param($s) $s.blockedRetries[0].bodySHA256='b'*64}},
    @{name='reversed retry time';change={param($s) $s.blockedRetries[1].elapsedMilliseconds=29}},
    @{name='no dropped original';change={param($s) $s.prepared.consumes[0].acknowledgement='delivered'}},
    @{name='no fresh native context';change={param($s) $s.prepared.preparations=@($s.prepared.preparations[0])}}
)) {
    $s=New-TimerObservation; & $case.change $s; $threw=$false
    try { $null=Get-NPCPickupPreparedRetryGap $s } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid explicit retry boundary: $($case.name)" }; $count++
}
function New-SlotObservation {
    $s=New-FaultObservation 'consume-held'
    $s.prepared.consumes[0].elapsedMilliseconds=5000; $s.prepared.consumes[1].elapsedMilliseconds=9000
    $s.committed.elapsedMilliseconds=5000; $s.delivered.elapsedMilliseconds=9000
    $s.prepared.preparations[1].elapsedMilliseconds=8000
    $s.prepared.preparations[1] | Add-Member requestElapsedMilliseconds 7900
    $unconsumed=$s.prepared.preparations[0] | ConvertTo-Json | ConvertFrom-Json
    $unconsumed.status='AlreadyPrepared';$unconsumed.elapsedMilliseconds=2000
    $again=$unconsumed | ConvertTo-Json | ConvertFrom-Json; $again.elapsedMilliseconds=4000
    $s.prepared.preparations=@($s.prepared.preparations[0],$unconsumed,$again,$s.prepared.preparations[1])
    $times=@(5010,5020,8020,8030)
    for ($index=0;$index -lt 4;$index++) { $s.blockedRetries[$index].elapsedMilliseconds=$times[$index] }
    $entry=[pscustomobject]@{requestId=$request;bodySHA256=$empty;commandSHA256=$hash;journalSHA256=$hash;elapsedMilliseconds=12}
    $first=$entry | ConvertTo-Json | ConvertFrom-Json
    $first | Add-Member status 'LocationConflict';$first | Add-Member statusCode 409;$first | Add-Member acknowledgement 'delivered'
    $first | Add-Member bodyBytes 0;$first | Add-Member requestElapsedMilliseconds 30;$first.elapsedMilliseconds=40
    $second=$first | ConvertTo-Json | ConvertFrom-Json;$second.requestElapsedMilliseconds=2040;$second.elapsedMilliseconds=2050
    $held=$entry | ConvertTo-Json | ConvertFrom-Json;$held.elapsedMilliseconds=2060
    $s.prepared | Add-Member slotConflict ([pscustomobject]@{phase='released';armed=[pscustomobject]@{elapsedMilliseconds=20};
        released=[pscustomobject]@{elapsedMilliseconds=3000};setupBlocked=@($entry);heldConsumes=@($held);conflicts=@($first,$second)})
    return $s
}
$s=New-SlotObservation
if ((Get-NPCPickupSlotConflictGap $s) -ne 2.0) { throw 'Known conflict request timer boundary mismatch.' }; $count++
Assert-NPCPickupPreparedObservation $s $session $server $drop $request $item $character -FaultMode 'consume-held'; $count++
$s.phase='blocked';$s.prepared.consumes=@($s.prepared.consumes[0]);$s.delivered=$null
if ([Math]::Abs((Get-NPCPickupPreparedRetryGap $s)-2.88) -gt 0.0001) { throw 'Slot-conflict ack recovery used a pre-commit reservation retry.' }; $count++
foreach ($case in @(
    @{name='missing real conflict';change={param($s) $s.prepared.slotConflict.conflicts=@($s.prepared.slotConflict.conflicts[0])}},
    @{name='not a LocationConflict';change={param($s) $s.prepared.slotConflict.conflicts[0].status='RevisionConflict'}},
    @{name='fake conflict status';change={param($s) $s.prepared.slotConflict.conflicts[0].statusCode=200}},
    @{name='undelivered known conflict';change={param($s) $s.prepared.slotConflict.conflicts[0].acknowledgement='pending'}},
    @{name='changed request during conflict';change={param($s) $s.prepared.slotConflict.conflicts[1].requestId=$drop}},
    @{name='nonempty conflict consume';change={param($s) $s.prepared.slotConflict.conflicts[1].bodyBytes=1}},
    @{name='changed journal during conflict';change={param($s) $s.prepared.slotConflict.conflicts[1].journalSHA256='b'*64}},
    @{name='changed raw prepare during conflict';change={param($s) $s.prepared.preparations[1].bodySHA256='b'*64}},
    @{name='consumed reservation during known rejection';change={param($s) $s.prepared.preparations[1].consumed=$true}},
    @{name='no pre-commit setup barrier';change={param($s) $s.prepared.slotConflict.setupBlocked=@()}},
    @{name='short known rejection timer';change={param($s) $s.prepared.slotConflict.conflicts[1].requestElapsedMilliseconds=100}},
    @{name='NaN known rejection request';change={param($s) $s.prepared.slotConflict.conflicts[1].requestElapsedMilliseconds=[double]::NaN}},
    @{name='response before known rejection request';change={param($s) $s.prepared.slotConflict.conflicts[1].elapsedMilliseconds=2000}},
    @{name='grant before original slot release';change={param($s) $s.prepared.slotConflict.released.elapsedMilliseconds=6000}},
    @{name='release before conflict';change={param($s) $s.prepared.slotConflict.released.elapsedMilliseconds=2000}},
    @{name='never released original request';change={param($s) $s.prepared.slotConflict.phase='armed'}}
)) {
    $s=New-SlotObservation; & $case.change $s; $threw=$false
    try { Assert-NPCPickupPreparedObservation $s $session $server $drop $request $item $character -FaultMode 'consume-held' } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid slot-conflict proof: $($case.name)" }; $count++
}
function New-UnconsumedFixture {
    $f=New-DatabaseFixture; $f.snapshot.origin.consumed_at=$null;$f.snapshot.item=$null;$f.snapshot.receipt=$null
    $f.intent.consumedAt=$null;$f.intent | Add-Member status 'Prepared'
    return $f
}
$f=New-UnconsumedFixture;$o=(New-Observation).prepared.origin
Assert-NPCPickupUnconsumedSlotSnapshot $f.snapshot $f.intent $f.snapshot.origin.command_payload $o;$count++
foreach ($case in @(
    @{name='item appeared before release';change={param($f) $f.snapshot.item=[pscustomobject]@{quantity=1}}},
    @{name='receipt appeared before release';change={param($f) $f.snapshot.receipt=[pscustomobject]@{request_id=$request}}},
    @{name='original marked consumed';change={param($f) $f.snapshot.origin.consumed_at='2026-10-16T00:00:00Z'}},
    @{name='second original reservation';change={param($f) $f.snapshot.sessionOriginCount=2}},
    @{name='different full intent command';change={param($f) $f.intent.command.mutations[0].newRecord.state.generationSeed++}},
    @{name='different Drop binding';change={param($f) $f.intent.dropId=$request}},
    @{name='server switched';change={param($f) $f.snapshot.origin.server_id='other'}}
)) {
    $f=New-UnconsumedFixture;& $case.change $f;$threw=$false
    try { Assert-NPCPickupUnconsumedSlotSnapshot $f.snapshot $f.intent $f.snapshot.origin.command_payload $o } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid unconsumed snapshot: $($case.name)" };$count++
}
Write-Host "PASS: $count prepared pickup proof cases (no UE/HTTP/PostgreSQL runtime claim)."
