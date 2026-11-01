# Opt-in loopback development fixture. Requests use real authenticated owner RPCs;
# only fresh test inventory is seeded through the existing assigned-server API.
function Initialize-InventoryConcurrencyFixture {
    if ($members.Count -ne 2) { throw 'Concurrency fixture requires exactly two members.' }
    $headers = @{ Authorization = "Bearer $($assignment.gameServerAccessToken)" }
    for ($index=0; $index -lt 2; ++$index) {
        $owner = $members[$index].CharacterId
        Assert-InventoryConcurrencyGuid $owner
        $before = Invoke-Api "items?ownerType=Character&ownerId=$owner&includeTerminal=false&limit=500" 'Get' $headers
        if (@($before.items | Where-Object { $_.location.containerType -eq 'Inventory' }).Count) { throw 'Concurrency requires fresh empty bags.' }
        $ids = @([Guid]::NewGuid().ToString('D'))
        if ($index -eq 0) { $ids += [Guid]::NewGuid().ToString('D') }
        $mutations = @(for ($slot=0; $slot -lt $ids.Count; ++$slot) {
            @{ expectedRevision=0; newRecord=@{
                definitionType='RPGItemDefinition'; definitionName=@('GameItem.Consume.Potion.Red.Large','GameItem.Equipment.Helm.Default')[$slot]; definitionVersion=1
                owner=@{type='Character';ownerId=$owner}; location=@{containerType='Inventory';containerId=$owner;slotIndex=$slot}
                state=@{instanceId=$ids[$slot];generationSeed=57+$slot;quantity=$(if($index -eq 1){3}else{@(7,1)[$slot]});instanceTags=@();statValues=@()}
                revision=0; lifecycleState='Active'
                metadata=@{bindState='CharacterBound';durability=@{current=0;maximum=0};expiresAtUtc=$null;creationSource='InventoryConcurrencyE2E.Seed';isLocked=$false}
            } }
        })
        $receipt = Invoke-Api 'item-transactions/commit' 'Post' $headers @{
            requestId=[Guid]::NewGuid().ToString('D');operation='E2E.SeedInventoryConcurrency';commandFingerprint="concurrency-$runId-$index"
            actor=@{type='Character';ownerId=$owner};affectedQuantity=$(if($index -eq 0){8}else{3});mutations=$mutations
        }
        if ($receipt.status -cne 'Committed' -or @($receipt.records).Count -ne $ids.Count) { throw 'Concurrency seed failed.' }
        @{Owner=$owner;Headers=$headers;Ids=$ids;Originals=@($receipt.records);Retained=@($before.items)}
    }
}

function Assert-InventoryConcurrencyGuid {
    param([string]$Value)
    $parsed=[Guid]::Empty
    if (-not [Guid]::TryParseExact($Value,'D',[ref]$parsed) -or $parsed -eq [Guid]::Empty -or $parsed.ToString('D') -cne $Value) {
        throw 'Unsafe concurrency fixture identity.'
    }
}

function Assert-InventoryConcurrencyRecord {
    param([object]$Fixture,[object]$Actual,[string]$Id,[long]$Revision,[int]$Slot)
    $original=@($Fixture.Originals | Where-Object { $_.state.instanceId -ceq $Id })
    if ($original.Count -ne 1) { throw 'Unknown concurrency item identity.' }
    $expected=$original[0] | ConvertTo-Json -Depth 30 -Compress | ConvertFrom-Json
    $expected.revision=$Revision; $expected.location.slotIndex=$Slot
    if (($Actual | ConvertTo-Json -Depth 30 -Compress) -cne ($expected | ConvertTo-Json -Depth 30 -Compress)) {
        throw 'Concurrency changed item fields outside the expected location and revision.'
    }
}

function Read-InventoryConcurrencyCounts {
    param([string]$Owner,[string[]]$RejectedRequests=@())
    Assert-InventoryConcurrencyGuid $Owner
    foreach ($id in $RejectedRequests) { Assert-InventoryConcurrencyGuid $id }
    $rejected = if ($RejectedRequests.Count) { "request_id IN ('" + ($RejectedRequests -join "','") + "')" } else { 'FALSE' }
    # Every normal Item.* action is counted, not just the action expected to succeed.
    $sql="SELECT count(*) FILTER (WHERE operation='Item.Move'),count(*) FILTER (WHERE operation LIKE 'Item.%'),count(*) FILTER (WHERE $rejected) FROM item_transaction_receipts WHERE actor_id='$Owner';"
    $rows=& docker compose -f (Join-Path $projectRoot 'Backend/compose.postgres.yml') exec -T postgres psql -U project_rpg -d project_rpg -At -v ON_ERROR_STOP=1 -c $sql 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'Concurrency receipt count failed.' }
    return ($rows -join '').Trim()
}

function Read-InventoryConcurrencyMarker {
    param([int]$Role,[string]$Pattern)
    $line=(Select-String -LiteralPath $clientLogs[$Role-1] -Pattern $Pattern | Select-Object -Last 1).Line
    if (-not $line -or $line -notmatch $Pattern) { throw 'Concurrency marker missing or malformed.' }
    return $Matches.Clone()
}

function Complete-InventoryConcurrencyFixture {
    $foreignRequests=@()
    for($role=1;$role -le 2;++$role) {
        Wait-Marker $clientLogs[$role-1] "RPG_ITEM_CONCURRENCY FOREIGN_REJECTED Role=$role Request="
        $marker=Read-InventoryConcurrencyMarker $role "RPG_ITEM_CONCURRENCY FOREIGN_REJECTED Role=$role Request=([0-9a-f-]{36}) OwnerOnly=1"
        Assert-InventoryConcurrencyGuid $marker[1]; $foreignRequests+=$marker[1]
    }
    foreach($f in $concurrencyFixture) {
        foreach($original in $f.Originals) {
            Assert-InventoryConcurrencyRecord $f (Invoke-Api "items/$($original.state.instanceId)" 'Get' $f.Headers) $original.state.instanceId 1 $original.location.slotIndex
        }
        if ((Read-InventoryConcurrencyCounts $f.Owner $foreignRequests) -cne '0|0|0') { throw 'Foreign RPC changed persistent inventory.' }
    }
    New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-concurrency-start.gate') | Out-Null
    Wait-Marker $clientLogs[0] 'RPG_ITEM_CONCURRENCY RACE WinnerRequest='
    Wait-Marker $clientLogs[1] 'RPG_ITEM_CONCURRENCY PASS Role=2 ForeignRejected=1 IndependentSlot=3 Revision=2 RpcIngress=1 Physical=0'
    $race=Read-InventoryConcurrencyMarker 1 'RPG_ITEM_CONCURRENCY RACE WinnerRequest=([0-9a-f-]{36}) LoserRequest=([0-9a-f-]{36}) WinnerItem=([0-9a-f-]{36}) OneSuccess=1 OneConflict=1'
    $winner=$race[1]; $loser=$race[2]; $item=$race[3]
    $first=Read-InventoryConcurrencyMarker 1 'RPG_ITEM_CONCURRENCY DISPATCH Role=1 A=([0-9a-f-]{36}) B=([0-9a-f-]{36}) RpcIngress=1'
    $second=Read-InventoryConcurrencyMarker 2 'RPG_ITEM_CONCURRENCY DISPATCH Role=2 A=([0-9a-f-]{36}) B=00000000-0000-0000-0000-000000000000 RpcIngress=1'
    $requests=@($foreignRequests)+@($first[1],$first[2],$second[1])
    foreach($id in $requests+@($winner,$loser,$item)) { Assert-InventoryConcurrencyGuid $id }
    if (@($requests | Select-Object -Unique).Count -ne 5 -or $winner -notin @($first[1],$first[2]) -or
        $loser -notin @($first[1],$first[2]) -or $winner -ceq $loser -or
        $item -cne $concurrencyFixture[0].Ids[[int]($winner -ceq $first[2])]) { throw 'Concurrency request correlation mismatch.' }
    Wait-Marker $serverLog "RPG_ITEM_CONCURRENCY_SERVER OUTCOME Request=$loser Stage=BackendRejected Backend=LocationConflict"
    $frozenReceipts=@()
    for($index=0;$index -lt 2;++$index) {
        $f=$concurrencyFixture[$index]
        $request=if($index -eq 0){$winner}else{$second[1]}
        $moved=if($index -eq 0){$item}else{$f.Ids[0]}
        $quantity=if($index -eq 1){3}elseif($moved -ceq $f.Ids[0]){7}else{1}
        $receipt=Invoke-Api "item-transactions/$request" 'Get' $f.Headers
        if($receipt.status -cne 'Committed' -or $receipt.operation -cne 'Item.Move' -or $receipt.requestId -cne $request -or
            $receipt.actor.type -cne 'Character' -or $receipt.actor.ownerId -cne $f.Owner -or $receipt.affectedQuantity -ne $quantity -or @($receipt.records).Count -ne 1) {
            throw 'Concurrency successful receipt mismatch.'
        }
        Assert-InventoryConcurrencyRecord $f $receipt.records[0] $moved 2 3
        $frozenReceipts+=($receipt | ConvertTo-Json -Depth 30 -Compress)
        foreach($original in $f.Originals) {
            $id=$original.state.instanceId; $changed=$id -ceq $moved
            Assert-InventoryConcurrencyRecord $f (Invoke-Api "items/$id" 'Get' $f.Headers) $id $(if($changed){2}else{1}) $(if($changed){3}else{$original.location.slotIndex})
        }
        $active=Invoke-Api "items?ownerType=Character&ownerId=$($f.Owner)&includeTerminal=false&limit=500" 'Get' $f.Headers
        if (@($active.items).Count -ne $f.Retained.Count+$f.Ids.Count) { throw 'Concurrency created or removed another item.' }
        foreach($original in $f.Retained) {
            $current=Invoke-Api "items/$($original.state.instanceId)" 'Get' $f.Headers
            if(($current | ConvertTo-Json -Depth 30 -Compress) -cne ($original | ConvertTo-Json -Depth 30 -Compress)) { throw 'Concurrency changed a starter item.' }
        }
        if ((Read-InventoryConcurrencyCounts $f.Owner (@($foreignRequests)+@($loser))) -cne '1|1|0') { throw 'Concurrency committed extra or rejected actions.' }
    }
    # Freeze actual pre-replay receipts and persistent snapshots first; only then
    # permit the identical stale-revision request to reach the server again.
    New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-concurrency-start.gate.replay') | Out-Null
    Wait-Marker $clientLogs[0] 'RPG_ITEM_CONCURRENCY PASS Role=1 ForeignRejected=1 OneWinner=1 OneConflict=1 CachedReplay=1 OwnerOnly=1 RpcIngress=1 Physical=0'
    Wait-Marker $serverLog "RPG_ITEM_CONCURRENCY_SERVER OUTCOME Request=$winner Stage=Completed Backend=AlreadyApplied"
    for($index=0;$index -lt 2;++$index) {
        $request=if($index -eq 0){$winner}else{$second[1]}
        $receipt=Invoke-Api "item-transactions/$request" 'Get' $concurrencyFixture[$index].Headers
        if(($receipt | ConvertTo-Json -Depth 30 -Compress) -cne $frozenReceipts[$index]) { throw 'Concurrency receipt changed on replay.' }
        if ((Read-InventoryConcurrencyCounts $concurrencyFixture[$index].Owner (@($foreignRequests)+@($loser))) -cne '1|1|0') { throw 'Replay committed another action.' }
    }
    $session=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if ($session.state -cne 'InProgress' -or (Test-Path -LiteralPath $gate)) { throw 'Concurrency unexpectedly started combat or settlement.' }
    Assert-Healthy
    $sameSources=$true
    foreach($path in $resultSourcePaths) { if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { $sameSources=$false } }
    $sameModule=(Get-FileHash -LiteralPath $unrealModulePath).Hash -ceq $unrealModuleHash
    # Freeze only owned UE processes, clients before server. Backend is closed by runner finally.
    $owned=@($processes | Where-Object { $_.ProcessName -ceq 'UnrealEditor-Cmd' }); [array]::Reverse($owned)
    foreach($process in $owned) { if(-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null } }
    $diagnostics=0
    $logHashes=@(foreach($log in $logs) {
        $content=Get-Content -LiteralPath $log -Raw
        $diagnostics += [regex]::Matches($content,': Error:|Fatal error:|Assertion failed:|Ensure condition failed:').Count
        @{Path=$log;SHA256=(Get-FileHash -LiteralPath $log).Hash}
    })
    [ordered]@{Passed=($sameSources -and $sameModule -and $diagnostics -eq 0);Mode='InventoryConcurrency';SessionId=$sessionId
        Clients=2;ForeignOwnerRequestsRejected=2;NoForeignRecordsProjected=$true;ForeignRequestsVerifiedBeforeRace=$true
        WinnerRequest=$winner;LoserRequest=$loser;IndependentOwnerRequest=$second[1];DurableMoveReceipts=2;BackendLocationConflict=1
        OneWinnerPerOwnerDestination=$true;SameSlotIndependentAcrossOwners=$true;CachedReplayAlreadyApplied=$true;NoReplayCommit=$true;PreReplayReceiptUnchanged=$true
        NoReceiptsForRejectedRequests=$true;RecordFieldsExceptRevisionAndSlotPreserved=$true;StarterItemsPreserved=$true
        AuthenticatedJoinTicketAdmission=$true;AdmissionBypass=$false;DevelopmentUnverifiedPlatformIdentityFixture=$true
        BackendPostgreSQL=$true;OwnerRpcIngress=$true;BypassesClientUiReservation=$true;ProjectionInjected=$false;CombatGateOpened=$false
        AuthoredWBPInputVerified=$false;PhysicalInputVerified=$false;SteamShippingVerified=$false;IndependentRestartVerified=$false
        ArtificialLatencyOrLossVerified=$false;ClosedRuntimeDiagnostics=$diagnostics
        BackendAssemblySHA256=(Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash
        EditorModuleSHA256=$unrealModuleHash;SelectedInputs=$resultSourcePaths.Count;SourcesUnchanged=$sameSources;ModuleUnchanged=$sameModule;ClosedLogs=$logHashes
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if(-not $sameSources -or -not $sameModule -or $diagnostics -ne 0) { throw 'Concurrency provenance or runtime diagnostics failed.' }
}
