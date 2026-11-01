# Local development only; seed a fresh assigned member via existing server authority.
function Initialize-InventoryMoveSwapFixture {
    param([switch]$Merge,[switch]$Mail,[ValidateSet('None','Equip','Unequip')][string]$Equipment='None')
    $isEquipment=$Equipment -cne 'None'
    if(@(@($Merge,$Mail,$isEquipment) | Where-Object {$_}).Count -gt 1) { throw 'Mail, merge and equipment seed modes are mutually exclusive.' }
    $headers = @{ Authorization = "Bearer $($assignment.gameServerAccessToken)" }
    $owner = $members[0].CharacterId
    $before = Invoke-Api "items?ownerType=Character&ownerId=$owner&includeTerminal=false&limit=500" 'Get' $headers
    if (@($before.items | Where-Object { $_.location.containerType -eq 'Inventory' }).Count) { throw 'Move/swap requires an empty fresh bag.' }
    if($isEquipment -and @($before.items | Where-Object {$_.location.containerType -ceq 'Equipment' -and $_.location.slotIndex -eq 0}).Count) {
        throw 'Equipment recovery requires an empty head slot; existing gear must not be replaced.'
    }
    $ids = @([Guid]::NewGuid().ToString('D'), [Guid]::NewGuid().ToString('D'))
    $mutations = @(for ($slot = 0; $slot -lt 2; ++$slot) {
        @{ expectedRevision=0; newRecord=@{
            definitionType='RPGItemDefinition'; definitionName=$(if($isEquipment){@('GameItem.Equipment.Helm.Default','GameItem.Consume.Potion.Red.Large')[$slot]}elseif($Merge){'GameItem.Consume.Potion.Red.Large'}else{@('GameItem.Consume.Potion.Red.Large','GameItem.Equipment.Helm.Default')[$slot]}); definitionVersion=1
            owner=@{type='Character';ownerId=$owner}; location=@{
                containerType=$(if($Mail -and $slot -eq 0){'Mail'}elseif($Equipment -ceq 'Unequip' -and $slot -eq 0){'Equipment'}else{'Inventory'})
                containerId=$(if($Mail -and $slot -eq 0){'DungeonReward.'+$sessionId}else{$owner});slotIndex=$slot}
            state=@{instanceId=$ids[$slot];generationSeed=37+$slot;quantity=$(if($isEquipment){1}else{@(7,1)[$slot]});instanceTags=@();statValues=@()}
            revision=0; lifecycleState='Active'
            metadata=@{bindState=$(if($Equipment -ceq 'Equip' -and $slot -eq 0){'BindOnEquip'}else{'CharacterBound'});durability=@{current=0;maximum=0};expiresAtUtc=$null;creationSource='InventoryMoveSwapE2E.Seed';isLocked=$false}
        } }
    })
    $receipt = Invoke-Api 'item-transactions/commit' 'Post' $headers @{
        requestId=[Guid]::NewGuid().ToString('D'); operation='E2E.SeedInventoryMoveSwap'; commandFingerprint="move-swap-$runId"
        actor=@{type='Character';ownerId=$owner}; affectedQuantity=$(if($isEquipment){2}else{8}); mutations=$mutations
    }
    if ($receipt.status -cne 'Committed' -or @($receipt.records).Count -ne 2) { throw 'Move/swap seed failed.' }
    return @{Owner=$owner;Headers=$headers;Source=$ids[0];Other=$ids[1];Originals=@($receipt.records);Retained=@($before.items)}
}

function Assert-InventoryMoveSwapRecord {
    param([object]$Actual,[string]$Id,[long]$Revision,[int]$Slot,[int]$Quantity=-1,[switch]$Consumed,[string]$TemplateId='',[switch]$ClaimedMail,
        [ValidateSet('','Equip','Unequip')][string]$EquipmentTransition='')
    $originalId=if($TemplateId){$TemplateId}else{$Id}
    $originals = @($moveSwapFixture.Originals | Where-Object { $_.state.instanceId -ceq $originalId })
    if ($originals.Count -ne 1) { throw 'Unknown move/swap fixture identity.' }
    $expected = $originals[0] | ConvertTo-Json -Depth 30 -Compress | ConvertFrom-Json
    $expected.revision=$Revision; $expected.location.slotIndex=$Slot
    $expected.state.instanceId=$Id
    if($ClaimedMail) {
        if($Consumed -or $expected.location.containerType -cne 'Mail' -or $expected.owner.ownerId -cne $moveSwapFixture.Owner) {
            throw 'Claim comparison requires the original owned mail attachment.'
        }
        $expected.location.containerType='Inventory'; $expected.location.containerId=$moveSwapFixture.Owner
    }
    if($EquipmentTransition) {
        $sourceContainer=if($EquipmentTransition -ceq 'Equip'){'Inventory'}else{'Equipment'}
        if($Consumed -or $ClaimedMail -or $TemplateId -or $expected.location.containerType -cne $sourceContainer -or
            $expected.owner.ownerId -cne $moveSwapFixture.Owner -or $expected.state.quantity -ne 1) { throw 'Equipment comparison requires the original single owned item.' }
        $expected.location.containerType=if($EquipmentTransition -ceq 'Equip'){'Equipment'}else{'Inventory'}
        $expected.location.containerId=$moveSwapFixture.Owner
        if($EquipmentTransition -ceq 'Equip' -and $expected.metadata.bindState -ceq 'BindOnEquip') { $expected.metadata.bindState='CharacterBound' }
    }
    if($Quantity -ge 0) { $expected.state.quantity=$Quantity }
    if($Consumed) {
        $expected.location.containerType='Terminal'; $expected.location.containerId=''
        $expected.lifecycleState='Consumed'; $expected.metadata.isLocked=$true
    }
    if (($Actual | ConvertTo-Json -Depth 30 -Compress) -cne ($expected | ConvertTo-Json -Depth 30 -Compress)) {
        throw 'Move/swap changed identity, quantity, owner, lifecycle or metadata.'
    }
}

function Read-InventoryMoveSwapRequestId {
    param([string]$Stage)
    $line=(Select-String -LiteralPath $clientLogs[0] -SimpleMatch "RPG_MOVE_SWAP_UI $Stage Request=" | Select-Object -Last 1).Line
    if ($line -notmatch "RPG_MOVE_SWAP_UI $Stage Request=([0-9a-f-]{36})") { throw 'Move/swap request marker missing.' }
    $id=[Guid]::Empty
    if (-not [Guid]::TryParseExact($Matches[1],'D',[ref]$id) -or $id -eq [Guid]::Empty) { throw 'Invalid move/swap request identity.' }
    return $id.ToString('D')
}

function Read-InventoryMoveSwapCounts {
    $owner=[Guid]::Empty
    if (-not [Guid]::TryParseExact($moveSwapFixture.Owner,'D',[ref]$owner)) { throw 'Unsafe move/swap owner identity.' }
    $sql="SELECT count(*) FILTER (WHERE operation='Item.Move'),count(*) FILTER (WHERE operation='Item.Swap'),count(*) FILTER (WHERE operation IN ('Item.Move','Item.Swap','Item.Split','Item.StackTransfer','Item.Consume','Item.Equip','Item.Unequip','Item.ClaimMail')) FROM item_transaction_receipts WHERE actor_id='$($owner.ToString('D'))';"
    $rows=& docker compose -f (Join-Path $projectRoot 'Backend/compose.postgres.yml') exec -T postgres psql -U project_rpg -d project_rpg -At -v ON_ERROR_STOP=1 -c $sql 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'Move/swap database count failed.' }
    return ($rows -join '').Trim()
}

function Complete-InventoryMoveSwapFixture {
    $f=$moveSwapFixture
    Wait-Marker $clientLogs[0] 'RPG_MOVE_SWAP_UI MOVED Request='
    $moveId=Read-InventoryMoveSwapRequestId 'MOVED'
    $move=Invoke-Api "item-transactions/$moveId" 'Get' $f.Headers
    if ($move.status -cne 'Committed' -or $move.operation -cne 'Item.Move' -or $move.actor.ownerId -cne $f.Owner -or
        $move.affectedQuantity -ne 7 -or @($move.records).Count -ne 1) { throw 'Move receipt mismatch.' }
    Assert-InventoryMoveSwapRecord $move.records[0] $f.Source 2 3
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Source)" 'Get' $f.Headers) $f.Source 2 3
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Other)" 'Get' $f.Headers) $f.Other 1 1
    if ((Read-InventoryMoveSwapCounts) -cne '1|0|1') { throw 'Move should be the only committed action before swap.' }
    $frozenMove=$move | ConvertTo-Json -Depth 30 -Compress
    New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-move-verified.gate') | Out-Null
    Wait-Marker $clientLogs[0] 'RPG_MOVE_SWAP_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Move=1 AtomicSwap=1 SelfDropRejected=1 Commands=2 Physical=0'
    $swapId=Read-InventoryMoveSwapRequestId 'SWAPPED'
    if ($swapId -ceq $moveId) { throw 'Move/swap must use distinct request identities.' }
    $swap=Invoke-Api "item-transactions/$swapId" 'Get' $f.Headers
    if ($swap.status -cne 'Committed' -or $swap.operation -cne 'Item.Swap' -or $swap.actor.ownerId -cne $f.Owner -or @($swap.records).Count -ne 2) {
        throw 'Atomic swap receipt mismatch.'
    }
    $source=@($swap.records | Where-Object { $_.state.instanceId -ceq $f.Source })
    $other=@($swap.records | Where-Object { $_.state.instanceId -ceq $f.Other })
    if ($source.Count -ne 1 -or $other.Count -ne 1) { throw 'Atomic swap record identities mismatch.' }
    Assert-InventoryMoveSwapRecord $source[0] $f.Source 3 1
    Assert-InventoryMoveSwapRecord $other[0] $f.Other 2 3
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Source)" 'Get' $f.Headers) $f.Source 3 1
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Other)" 'Get' $f.Headers) $f.Other 2 3
    if ((Read-InventoryMoveSwapCounts) -cne '1|1|2') { throw 'Expected exactly one move and one swap without fallback actions.' }
    $oldReceipt=Invoke-Api "item-transactions/$moveId" 'Get' $f.Headers
    if (($oldReceipt | ConvertTo-Json -Depth 30 -Compress) -cne $frozenMove) { throw 'Original move receipt changed after swap.' }
    $active=Invoke-Api "items?ownerType=Character&ownerId=$($f.Owner)&includeTerminal=false&limit=500" 'Get' $f.Headers
    if (@($active.items).Count -ne $f.Retained.Count+2) { throw 'Move/swap created or removed another item.' }
    foreach ($item in $f.Retained) {
        $now=Invoke-Api "items/$($item.state.instanceId)" 'Get' $f.Headers
        if (($now | ConvertTo-Json -Depth 30 -Compress) -cne ($item | ConvertTo-Json -Depth 30 -Compress)) { throw 'Move/swap modified a starter item.' }
    }
    $session=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if ($session.state -cne 'InProgress' -or (Test-Path -LiteralPath $gate)) { throw 'Move/swap unexpectedly started settlement.' }
    Assert-Healthy
    $sameSources=$true
    foreach ($path in $resultSourcePaths) { if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $path) -Algorithm SHA256).Hash -cne $resultSourceHashes[$path]) { $sameSources=$false } }
    $sameModule=(Get-FileHash -LiteralPath $unrealModulePath -Algorithm SHA256).Hash -ceq $unrealModuleHash
    $owned=@($processes | Where-Object { $_.ProcessName -ceq 'UnrealEditor-Cmd' }); [array]::Reverse($owned)
    foreach ($process in $owned) { if (-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null } }
    $diagnostics=0
    $logHashes=@(foreach ($log in $logs) {
        $text=Get-Content -LiteralPath $log -Raw
        $diagnostics += [regex]::Matches($text,': Error:|Fatal error:|Assertion failed:|Ensure condition failed:').Count
        @{Path=$log;SHA256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash}
    })
    [ordered]@{Passed=($sameSources -and $sameModule -and $diagnostics -eq 0);Mode='InventoryMoveSwap';SessionId=$sessionId
        MoveRequest=$moveId;SwapRequest=$swapId;Source=$f.Source;Other=$f.Other;SourceQuantity=7;OtherQuantity=1
        SourceFinalSlot=1;OtherFinalSlot=3;SourceRevision=3;OtherRevision=2;DurableMoveReceiptCount=1;DurableSwapReceiptCount=1
        AuthenticatedJoinTicketAdmission=$true;NoSteamIdentityFixture=$true;BackendPostgreSQL=$true;AuthoredInventoryWBP=$true
        SyntheticPlainSlateDrags=3;DirectUiSubmission=$false;SelfDropRejectedWithoutCommit=$true;NoOptimisticMutation=$true
        BothSwapReservationsReleased=$true;TwoSourceQuickslotReferencesRetained=$true;OtherShortcutUnassigned=$true
        AllRecordFieldsExceptRevisionAndSlotPreserved=$true;StarterItemsPreserved=$true;MoveReceiptUnchanged=$true;CombatGateOpened=$false
        PhysicalInputVerified=$false;SteamShippingVerified=$false;IndependentRestartVerified=$false;ClosedRuntimeDiagnostics=$diagnostics
        BackendAssemblySHA256=(Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll') -Algorithm SHA256).Hash
        EditorModuleSHA256=$unrealModuleHash;SelectedInputs=$resultSourcePaths.Count;SourcesUnchanged=$sameSources;ModuleUnchanged=$sameModule;ClosedLogs=$logHashes
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if (-not $sameSources -or -not $sameModule -or $diagnostics -ne 0) { throw 'Move/swap provenance or runtime diagnostics failed.' }
}
