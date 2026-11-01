# Opt-in local development fixture. Only the assigned server may seed a new test character.
# Client identity, projection and the split request are never injected by this helper.
function Initialize-StackSplitFixture {
    $headers = @{ Authorization = "Bearer $($assignment.gameServerAccessToken)" }
    $ownerId = $members[0].CharacterId
    $before = Invoke-Api "items?ownerType=Character&ownerId=$ownerId&includeTerminal=false&limit=500" 'Get' $headers
    if (@($before.items | Where-Object { $_.location.containerType -eq 'Inventory' }).Count -ne 0) {
        throw 'Stack split fixture requires a fresh character with an empty bag.'
    }
    $id = [Guid]::NewGuid().ToString('D')
    $receipt = Invoke-Api 'item-transactions/commit' 'Post' $headers @{
        requestId = [Guid]::NewGuid().ToString('D'); operation = 'E2E.SeedStackSplit'; commandFingerprint = "stack-split-$runId"
        actor = @{ type = 'Character'; ownerId = $ownerId }; affectedQuantity = 7
        mutations = @(@{ expectedRevision = 0; newRecord = @{
            definitionType = 'RPGItemDefinition'; definitionName = 'GameItem.Consume.Potion.Red.Large'; definitionVersion = 1
            owner = @{ type = 'Character'; ownerId = $ownerId }
            location = @{ containerType = 'Inventory'; containerId = $ownerId; slotIndex = 0 }
            state = @{ instanceId = $id; generationSeed = 37; quantity = 7; instanceTags = @(); statValues = @() }
            revision = 0; lifecycleState = 'Active'
            metadata = @{ bindState = 'CharacterBound'; durability = @{ current = 3; maximum = 5 }; expiresAtUtc = $null
                creationSource = 'StackSplitE2E.Seed'; isLocked = $false }
        } })
    }
    if ($receipt.status -cne 'Committed' -or @($receipt.records).Count -ne 1) { throw 'Stack split seed rejected.' }
    return @{ Source = $id; Original = $receipt.records[0]; RetainedItems = @($before.items); Owner = $ownerId; Headers = $headers }
}

function Assert-StackSplitRecord {
    param([object]$Actual, [string]$Id, [int]$Quantity, [long]$Revision, [int]$Slot)
    # Compare the whole DTO, not just quantity: owner/definition/seed/tags/stats/binding/
    # durability/expiry/creation source/lock/lifecycle must survive unchanged.
    $expected = $stackSplitFixture.Original | ConvertTo-Json -Depth 30 -Compress | ConvertFrom-Json
    $expected.state.instanceId = $Id
    $expected.state.quantity = $Quantity
    $expected.revision = $Revision
    $expected.location.slotIndex = $Slot
    if (($Actual | ConvertTo-Json -Depth 30 -Compress) -cne ($expected | ConvertTo-Json -Depth 30 -Compress)) {
        throw 'Stack split record identity, state or metadata mismatch.'
    }
}

function Read-StackSplitActionReceiptCounts {
    param([string]$OwnerId)
    $ownerGuid = [Guid]::Empty
    if (-not [Guid]::TryParseExact($OwnerId, 'D', [ref]$ownerGuid)) { throw 'Unsafe split fixture owner identity.' }
    $countSql = "SELECT count(*) FILTER (WHERE operation='Item.Split'), count(*) FILTER (WHERE operation IN ('Item.Split','Item.StackTransfer','Item.Move','Item.Swap','Item.Consume')) FROM item_transaction_receipts WHERE actor_id='$($ownerGuid.ToString('D'))';"
    $countOutput = & docker compose -f (Join-Path $projectRoot 'Backend/compose.postgres.yml') exec -T postgres psql -U project_rpg -d project_rpg -At -v ON_ERROR_STOP=1 -c $countSql 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'Split receipt count read failed.' }
    return ($countOutput -join '').Trim()
}

function Complete-StackSplitFixture {
    $fixture = $stackSplitFixture
    $createdQuantity = if ($InventorySplitQuantity) { 2 } else { 3 }
    $sourceQuantity = 7 - $createdQuantity
    if ($InventorySplitQuantity) {
        Wait-Marker $clientLogs[0] 'RPG_STACK_QUANTITY_UI CANCELLED Authenticated=1 Iris=1 AuthoredWBP=1 NoCommand=1 Source=7 Physical=0'
        Assert-StackSplitRecord (Invoke-Api "items/$($fixture.Source)" 'Get' $fixture.Headers) $fixture.Source 7 1 0
        $afterCancel = Invoke-Api "items?ownerType=Character&ownerId=$($fixture.Owner)&includeTerminal=false&limit=500" 'Get' $fixture.Headers
        if (@($afterCancel.items).Count -ne $fixture.RetainedItems.Count + 1 -or
            (Read-StackSplitActionReceiptCounts $fixture.Owner) -cne '0|0') { throw 'Cancelled quantity selection made a commit or changed inventory.' }
        New-Item -ItemType File -Path (Join-Path $runRoot 'stack-quantity-cancel-verified.gate') | Out-Null
        Wait-Marker $clientLogs[0] 'RPG_STACK_QUANTITY_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Cancel=1 InvalidQuantityRejected=1 Selected=2 OccupiedTargetRejected=1 Commands=1 Physical=0'
    } else {
        Wait-Marker $clientLogs[0] 'RPG_STACK_SPLIT_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Split=1 OccupiedTargetRejected=1 Commands=1 Physical=0'
    }
    $line = (Select-String -LiteralPath $clientLogs[0] -SimpleMatch 'RPG_STACK_SPLIT_UI SPLIT Request=' | Select-Object -Last 1).Line
    if ($line -notmatch 'RPG_STACK_SPLIT_UI SPLIT Request=([0-9a-f-]{36}) Created=([0-9a-f-]{36})') { throw 'Missing split request/created identity.' }
    $requestGuid = [Guid]::Empty; $createdGuid = [Guid]::Empty; $ownerGuid = [Guid]::Empty
    if (-not [Guid]::TryParseExact($Matches[1], 'D', [ref]$requestGuid) -or
        -not [Guid]::TryParseExact($Matches[2], 'D', [ref]$createdGuid) -or
        -not [Guid]::TryParseExact($fixture.Owner, 'D', [ref]$ownerGuid) -or
        $requestGuid -eq [Guid]::Empty -or $createdGuid -eq [Guid]::Empty -or $createdGuid.ToString('D') -ceq $fixture.Source) {
        throw 'Invalid split fixture identity.'
    }
    $requestId = $requestGuid.ToString('D'); $createdId = $createdGuid.ToString('D')
    $receipt = Invoke-Api "item-transactions/$requestId" 'Get' $fixture.Headers
    if ($receipt.status -cne 'Committed' -or $receipt.operation -cne 'Item.Split' -or
        $receipt.actor.ownerId -cne $fixture.Owner -or $receipt.affectedQuantity -ne $createdQuantity -or @($receipt.records).Count -ne 2) {
        throw 'Split two-record receipt mismatch.'
    }
    $sourceRecord = @($receipt.records | Where-Object { $_.state.instanceId -ceq $fixture.Source })
    $createdRecord = @($receipt.records | Where-Object { $_.state.instanceId -ceq $createdId })
    if ($sourceRecord.Count -ne 1 -or $createdRecord.Count -ne 1) { throw 'Split receipt record identities mismatch.' }
    Assert-StackSplitRecord $sourceRecord[0] $fixture.Source $sourceQuantity 2 0
    Assert-StackSplitRecord $createdRecord[0] $createdId $createdQuantity 1 3
    Assert-StackSplitRecord (Invoke-Api "items/$($fixture.Source)" 'Get' $fixture.Headers) $fixture.Source $sourceQuantity 2 0
    Assert-StackSplitRecord (Invoke-Api "items/$createdId" 'Get' $fixture.Headers) $createdId $createdQuantity 1 3
    $frozenReceipt = $receipt | ConvertTo-Json -Depth 30 -Compress
    $active = Invoke-Api "items?ownerType=Character&ownerId=$($fixture.Owner)&includeTerminal=false&limit=500" 'Get' $fixture.Headers
    if (@($active.items).Count -ne $fixture.RetainedItems.Count + 2) { throw 'Split created or lost another active item.' }
    foreach ($item in $fixture.RetainedItems) {
        $current = Invoke-Api "items/$($item.state.instanceId)" 'Get' $fixture.Headers
        if (($current | ConvertTo-Json -Depth 30 -Compress) -cne ($item | ConvertTo-Json -Depth 30 -Compress)) {
            throw 'Split modified a retained starter item.'
        }
    }
    if ((Read-StackSplitActionReceiptCounts $fixture.Owner) -cne '1|1') { throw 'Expected one durable split and no fallback/consume commits.' }
    $receiptAfter = Invoke-Api "item-transactions/$requestId" 'Get' $fixture.Headers
    if (($receiptAfter | ConvertTo-Json -Depth 30 -Compress) -cne $frozenReceipt) { throw 'Split receipt changed after rejected drop.' }
    $sessionNow = Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if ($sessionNow.state -cne 'InProgress' -or (Test-Path -LiteralPath $gate)) { throw 'Split fixture unexpectedly started settlement.' }
    Assert-Healthy
    $sameSources = $true
    foreach ($path in $resultSourcePaths) {
        if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $path) -Algorithm SHA256).Hash -cne $resultSourceHashes[$path]) { $sameSources = $false }
    }
    $sameModule = (Get-FileHash -LiteralPath $unrealModulePath -Algorithm SHA256).Hash -ceq $unrealModuleHash
    $ownedUnreal = @($processes | Where-Object { $_.ProcessName -ceq 'UnrealEditor-Cmd' })
    [array]::Reverse($ownedUnreal)
    foreach ($process in $ownedUnreal) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null }
    }
    $logHashes = @($logs | ForEach-Object { @{ Path = $_; SHA256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } })
    $runtimeErrors = 0
    foreach ($log in $logs) {
        $text = Get-Content -LiteralPath $log -Raw
        $runtimeErrors += [regex]::Matches($text, ': Error:|Fatal error:|Assertion failed:|Ensure condition failed:').Count
    }
    [ordered]@{ Passed = ($sameSources -and $sameModule -and $runtimeErrors -eq 0); Mode = $(if ($InventorySplitQuantity) { 'InventorySplitQuantity' } else { 'InventoryStackSplit' }); SessionId = $sessionId
        Source = $fixture.Source; Created = $createdId; SplitRequest = $requestId; ConservedQuantity = 7
        SourceQuantity = $sourceQuantity; CreatedQuantity = $createdQuantity; DurableSplitReceiptCount = 1; SplitAndFallbackConsumeReceiptCount = 1
        BackendPostgreSQL = $true; BackendAssemblySHA256 = (Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll') -Algorithm SHA256).Hash
        AuthenticatedJoinTicketAdmission = $true; NoSteamIdentityFixture = $true; AuthoredInventoryWBP = $true
        SyntheticShiftSlateDrags = $(if ($InventorySplitQuantity) { 0 } else { 2 })
        SyntheticCtrlSlateDrags = $(if ($InventorySplitQuantity) { 3 } else { 0 }); QuantityKeyboardRouted = [bool]$InventorySplitQuantity
        QuantityCancelVerifiedBeforeConfirm = [bool]$InventorySplitQuantity; FullSourceQuantityRejected = [bool]$InventorySplitQuantity
        DirectQuantitySetText = $false; DirectUiSubmission = $false
        OccupiedTargetRejectedWithoutCommit = $true; OwnerProjectionAndCellQuantities = $true
        SourceQuickslotRetained = $true; NewQuickslotUnassigned = $true; SourceAndNewReservationsReleased = $true
        AllRecordFieldsPreservedExceptSplitIdentityQuantityRevisionAndSlot = $true; UnaffectedStarterItemsPreserved = $true
        OriginalReceiptUnchanged = $true; CombatGateOpened = $false; ClosedRuntimeDiagnostics = $runtimeErrors
        PhysicalInputVerified = $false; SteamShippingVerified = $false; IndependentRestartVerified = $false
        EditorModuleSHA256 = $unrealModuleHash; SelectedInputs = $resultSourcePaths.Count
        SourcesUnchanged = $sameSources; ModuleUnchanged = $sameModule; ClosedLogs = $logHashes
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if (-not $sameSources -or -not $sameModule -or $runtimeErrors -ne 0) { throw 'Split source/module provenance or closed runtime diagnostics failed.' }
}
