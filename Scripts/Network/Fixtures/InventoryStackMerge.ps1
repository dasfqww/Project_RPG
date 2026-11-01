# Opt-in helper for Run-DungeonBossReturnE2ETest.ps1. Uses that runner's owned
# development server/session only; never injects a client token, identity or projection.
function Initialize-StackMergeFixture {
    $serverHeaders = @{ Authorization = "Bearer $($assignment.gameServerAccessToken)" }
    $ownerId = $members[0].CharacterId
    $before = Invoke-Api "items?ownerType=Character&ownerId=$ownerId&includeTerminal=false&limit=500" 'Get' $serverHeaders
    if (@($before.items | Where-Object { $_.location.containerType -eq 'Inventory' }).Count -ne 0) {
        throw 'Stack fixture needs a newly generated character with an empty bag.'
    }
    $ids = @([Guid]::NewGuid().ToString('D'), [Guid]::NewGuid().ToString('D'), [Guid]::NewGuid().ToString('D'))
    $mutations = @(for ($slot = 0; $slot -lt 3; $slot++) {
        @{ expectedRevision = 0; newRecord = @{
            definitionType = 'RPGItemDefinition'; definitionName = 'GameItem.Consume.Potion.Red.Large'; definitionVersion = 1
            owner = @{ type = 'Character'; ownerId = $ownerId }
            location = @{ containerType = 'Inventory'; containerId = $ownerId; slotIndex = $slot }
            state = @{ instanceId = $ids[$slot]; generationSeed = 37 + $slot; quantity = @(7, 8, 3)[$slot]; instanceTags = @(); statValues = @() }
            revision = 0; lifecycleState = 'Active'
            metadata = @{ bindState = 'Unbound'; durability = @{ current = 0; maximum = 0 }; expiresAtUtc = $null
                creationSource = 'StackMergeE2E.Seed'; isLocked = $false }
        } }
    })
    $receipt = Invoke-Api 'item-transactions/commit' 'Post' $serverHeaders @{
        requestId = [Guid]::NewGuid().ToString('D'); operation = 'E2E.SeedStackMerge'; commandFingerprint = "stack-merge-$runId"
        actor = @{ type = 'Character'; ownerId = $ownerId }; affectedQuantity = 18; mutations = $mutations
    }
    if ($receipt.status -cne 'Committed' -or @($receipt.records).Count -ne 3) { throw 'Stack fixture seed rejected.' }
    return @{ Source = $ids[0]; Destination = $ids[1]; Extra = $ids[2]
        RetainedItems = @($before.items); Owner = $ownerId; Headers = $serverHeaders }
}

function Assert-StackMergeRecord {
    param([object]$Record, [string]$Id, [int]$Quantity, [long]$Revision, [int]$Slot, [bool]$Consumed = $false)
    $container = if ($Consumed) { 'Terminal' } else { 'Inventory' }
    $lifecycle = if ($Consumed) { 'Consumed' } else { 'Active' }
    if ($Record.state.instanceId -cne $Id -or $Record.owner.ownerId -cne $stackMergeFixture.Owner -or
        $Record.definitionName -cne 'GameItem.Consume.Potion.Red.Large' -or $Record.definitionVersion -ne 1 -or
        $Record.state.quantity -ne $Quantity -or $Record.revision -ne $Revision -or
        $Record.location.slotIndex -ne $Slot -or $Record.location.containerType -cne $container -or
        $Record.lifecycleState -cne $lifecycle -or $Record.metadata.bindState -cne 'Unbound' -or
        [bool]$Record.metadata.isLocked -ne $Consumed) {
        throw 'Persisted stack record identity/quantity/revision/location mismatch.'
    }
    if (-not $Consumed -and $Record.location.containerId -cne $stackMergeFixture.Owner) { throw 'Stack container identity changed.' }
}

function Read-StackMergeRequestId {
    param([string]$Phase)
    $line = (Select-String -LiteralPath $clientLogs[0] -Pattern "RPG_STACK_MERGE_UI $Phase Request=" | Select-Object -Last 1).Line
    if ($line -notmatch "RPG_STACK_MERGE_UI $Phase Request=([0-9a-f-]{36})") { throw 'Missing stack merge request identity.' }
    $parsed = [Guid]::Empty
    if (-not [Guid]::TryParseExact($Matches[1], 'D', [ref]$parsed)) { throw 'Invalid stack request identity.' }
    return $parsed.ToString('D')
}

function Complete-StackMergeFixture {
    $fixture = $stackMergeFixture
    $headers = $fixture.Headers
    Wait-Marker $clientLogs[0] 'RPG_STACK_MERGE_UI PARTIAL Request='
    $partialId = Read-StackMergeRequestId 'PARTIAL'
    $partial = Invoke-Api "item-transactions/$partialId" 'Get' $headers
    if ($partial.operation -cne 'Item.StackTransfer' -or $partial.actor.ownerId -cne $fixture.Owner -or
        $partial.affectedQuantity -ne 2 -or @($partial.records).Count -ne 2) { throw 'Partial merge receipt mismatch.' }
    $partialFrozen = $partial | ConvertTo-Json -Depth 30 -Compress
    Assert-StackMergeRecord (Invoke-Api "items/$($fixture.Source)" 'Get' $headers) $fixture.Source 5 2 0
    Assert-StackMergeRecord (Invoke-Api "items/$($fixture.Destination)" 'Get' $headers) $fixture.Destination 10 2 1
    Assert-StackMergeRecord (Invoke-Api "items/$($fixture.Extra)" 'Get' $headers) $fixture.Extra 3 1 2
    $partial | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $runRoot 'stack-partial-receipt.json') -Encoding UTF8
    New-Item -ItemType File -Path (Join-Path $runRoot 'stack-partial-verified.gate') | Out-Null
    Wait-Marker $clientLogs[0] 'RPG_STACK_MERGE_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Partial=1 Full=1 FullTargetRejected=1 Commands=2 Physical=0'
    $fullId = Read-StackMergeRequestId 'FULL'
    if ($fullId -ceq $partialId) { throw 'Merge requests reused their identity.' }
    $full = Invoke-Api "item-transactions/$fullId" 'Get' $headers
    if ($full.operation -cne 'Item.StackTransfer' -or $full.actor.ownerId -cne $fixture.Owner -or
        $full.affectedQuantity -ne 5 -or @($full.records).Count -ne 2) { throw 'Full merge receipt mismatch.' }
    Assert-StackMergeRecord (Invoke-Api "items/$($fixture.Source)" 'Get' $headers) $fixture.Source 0 3 -1 $true
    Assert-StackMergeRecord (Invoke-Api "items/$($fixture.Destination)" 'Get' $headers) $fixture.Destination 10 2 1
    Assert-StackMergeRecord (Invoke-Api "items/$($fixture.Extra)" 'Get' $headers) $fixture.Extra 8 2 2
    $replayedPartial = Invoke-Api "item-transactions/$partialId" 'Get' $headers
    if (($replayedPartial | ConvertTo-Json -Depth 30 -Compress) -cne $partialFrozen) { throw 'Original merge receipt changed after a later merge.' }
    $active = Invoke-Api "items?ownerType=Character&ownerId=$($fixture.Owner)&includeTerminal=false&limit=500" 'Get' $headers
    if (@($active.items).Count -ne $fixture.RetainedItems.Count + 2) { throw 'Merge created or lost another active item.' }
    foreach ($item in $fixture.RetainedItems) {
        $now = Invoke-Api "items/$($item.state.instanceId)" 'Get' $headers
        if (($now | ConvertTo-Json -Depth 30 -Compress) -cne ($item | ConvertTo-Json -Depth 30 -Compress)) {
            throw 'Merge changed a retained starter item.'
        }
    }
    $ownerGuid = [Guid]::Empty
    if (-not [Guid]::TryParseExact($fixture.Owner, 'D', [ref]$ownerGuid)) { throw 'Unsafe fixture owner identifier.' }
    # Read only this generated character's receipts from the development database.
    $countSql = "SELECT count(*) FROM item_transaction_receipts WHERE actor_id='$($ownerGuid.ToString('D'))' AND operation='Item.StackTransfer';"
    $receiptCountOutput = & docker exec backend-postgres-1 psql -U project_rpg -d project_rpg -At -v ON_ERROR_STOP=1 -c $countSql 2>$null
    if ($LASTEXITCODE -ne 0 -or ($receiptCountOutput -join '').Trim() -cne '2') { throw 'Expected exactly two persisted stack merge receipts.' }
    $sessionNow = Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if ($sessionNow.state -cne 'InProgress' -or (Test-Path -LiteralPath $gate)) { throw 'Inventory fixture unexpectedly started settlement.' }
    Assert-Healthy
    $sameSources = $true
    foreach ($path in $resultSourcePaths) {
        if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $path) -Algorithm SHA256).Hash -cne $resultSourceHashes[$path]) { $sameSources = $false }
    }
    $sameModule = (Get-FileHash -LiteralPath $unrealModulePath -Algorithm SHA256).Hash -ceq $unrealModuleHash
    # Freeze owned Unreal logs before hashing; the existing finally also owns cleanup on failure.
    $ownedUnreal = @($processes | Where-Object { $_.ProcessName -ceq 'UnrealEditor-Cmd' })
    [array]::Reverse($ownedUnreal) # client first: do not create a cleanup-only ConnectionLost diagnostic
    foreach ($process in $ownedUnreal) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null }
    }
    $logHashes = @($logs | ForEach-Object { @{ Path = $_; SHA256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } })
    [ordered]@{ Passed = ($sameSources -and $sameModule); Mode = 'InventoryStackMerge'; SessionId = $sessionId
        Source = $fixture.Source; Destination = $fixture.Destination; Extra = $fixture.Extra
        PartialRequest = $partialId; FullRequest = $fullId; ConservedQuantity = 18; DurableMergeReceiptCount = 2
        BackendPostgreSQL = $true; BackendAssemblySHA256 = (Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll') -Algorithm SHA256).Hash
        AuthenticatedJoinTicketAdmission = $true; NoSteamIdentityFixture = $true
        AuthoredInventoryWBP = $true; SyntheticSlateDrags = 3; PartialAndFullMerge = $true
        FullTargetRejectedWithoutCommit = $true; OwnerProjectionAndCellQuantities = $true; ConsumedSourceQuickslotCleared = $true
        UnaffectedStarterItemsPreserved = $true; OriginalReceiptUnchanged = $true; CombatGateOpened = $false
        PhysicalInputVerified = $false; SteamShippingVerified = $false; IndependentRestartVerified = $false
        EditorModuleSHA256 = $unrealModuleHash; SelectedInputs = $resultSourcePaths.Count
        SourcesUnchanged = $sameSources; ModuleUnchanged = $sameModule; ClosedLogs = $logHashes
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if (-not $sameSources -or -not $sameModule) { throw 'Selected inventory inputs/module changed during the run.' }
}
