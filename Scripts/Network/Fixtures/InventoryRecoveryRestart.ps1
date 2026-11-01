# Existing server journal, authenticated re-admission and authored recovery button only.
# Dot-sourcing defines helpers; it starts/stops nothing and changes no database rows.
function Get-InventoryRestartTextSHA256 {
    param([string]$Text)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Read-InventoryRestartReceiptDigest {
    param([string]$Request,[string]$Owner)
    foreach($value in @($Request,$Owner)) {
        $id=[Guid]::Empty
        if(-not [Guid]::TryParseExact($value,'D',[ref]$id) -or $id -eq [Guid]::Empty -or $id.ToString('D') -cne $value) {
            throw 'Unsafe inventory restart receipt identity.'
        }
    }
    $sql="SELECT row_to_json(r)::text FROM public.item_transaction_receipts r WHERE request_id='$Request' AND actor_id='$Owner';"
    $rows=& docker compose -f (Join-Path $projectRoot 'Backend/compose.postgres.yml') exec -T postgres psql -U project_rpg -d project_rpg -At -v ON_ERROR_STOP=1 -c $sql 2>$null
    if($LASTEXITCODE -ne 0) { throw 'Inventory restart receipt read failed.' }
    $raw=($rows -join '').Trim()
    if(-not $raw.StartsWith('{')) { throw 'Original inventory restart receipt is missing.' }
    return Get-InventoryRestartTextSHA256 $raw
}

function Read-InventoryRestartJournal {
    param([hashtable]$Fixture,[string]$Request,[string]$CommandSHA256,[switch]$Resolved)
    $files=@(Get-ChildItem -LiteralPath $inventoryJournalDirectory -File -ErrorAction Stop)
    if($files.Count -ne 1 -or $files[0].Name -cnotmatch '^[0-9a-f]{40}\.json$') { throw 'Expected one complete inventory journal, not a partial or alternate file.' }
    $file=$files[0]
    $journal=Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    if($journal.schemaVersion -ne 1 -or @($journal.PSObject.Properties).Count -ne 9 -or
        $journal.backendUrl -cne "$inventoryProxyUrl/api" -or $journal.serverId -cne $serverId -or
        $journal.sessionId -cne $sessionId -or $journal.characterId -cne $Fixture.Owner -or
        $journal.steamId -cne $members[0].SteamId -or $journal.revoked -isnot [bool] -or $journal.revoked -or
        [string]$journal.generation -cnotmatch '^[1-9][0-9]{0,19}$' -or
        @($journal.commands).Count -ne $(if($Resolved){0}else{1})) { throw 'Inventory restart journal has a foreign scope or invalid state.' }
    if(-not $Resolved) {
        $body=$journal.commands[0]
        if($body -isnot [string] -or (Get-InventoryRestartTextSHA256 $body) -cne $CommandSHA256) { throw 'Journal did not preserve the original wire bytes.' }
        $command=$body | ConvertFrom-Json
        if($command.requestId -cne $Request -or $command.operation -cne $inventoryRecoveryExpectedOperation -or $command.actor.ownerId -cne $Fixture.Owner) {
            throw 'Journal did not preserve the original request/owner.'
        }
    }
    return [pscustomobject]@{Path=$file.FullName;SHA256=(Get-FileHash -LiteralPath $file.FullName).Hash;Generation=[string]$journal.generation}
}

function Stop-InventoryRestartOwnedUE {
    $owned=@($processes.ToArray() | Where-Object { $_.ProcessName -ceq 'UnrealEditor-Cmd' })
    if($owned.Count -ne 2) { throw 'Expected exactly the owned server/client for inventory restart.' }
    [array]::Reverse($owned) # stop the client first; do not manufacture a connection-lost UI failure
    foreach($process in $owned) {
        $id=$process.Id
        if($process.HasExited) { throw 'Inventory restart process exited before the controlled boundary.' }
        Stop-Process -InputObject $process -ErrorAction Stop
        if(-not $process.WaitForExit(10000)) { throw 'Owned inventory restart process exit timed out.' }
        [void]$processes.Remove($process); $process.Dispose()
        Write-Output $id
    }
}

function Assert-InventoryRestartStoredState {
    param([hashtable]$Fixture,[string]$Request,[string]$FrozenActive,[string]$ReceiptDigest)
    $items=Invoke-Api "items?ownerType=Character&ownerId=$($Fixture.Owner)&includeTerminal=false&limit=500" 'Get' $Fixture.Headers
    if(($items | ConvertTo-Json -Depth 30 -Compress) -cne $FrozenActive -or
        (Read-InventoryRestartReceiptDigest $Request $Fixture.Owner) -cne $ReceiptDigest -or
        (Read-InventoryMoveSwapCounts) -cne $inventoryRecoveryExpectedCounts) { throw 'Inventory restart changed an item, revision or original receipt.' }
    if($inventoryRecoveryIsMerge) {
        # Active-only snapshots cannot prove the consumed source was not resurrected or altered.
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Source)" 'Get' $Fixture.Headers) $Fixture.Source 2 -1 -Quantity 0 -Consumed
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Other)" 'Get' $Fixture.Headers) $Fixture.Other 2 1 -Quantity 8
        if(@($items.items | Where-Object {$_.state.instanceId -ceq $Fixture.Source}).Count) { throw 'Consumed merge source became active again.' }
    }
    if($inventoryRecoveryIsSplit) {
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Source)" 'Get' $Fixture.Headers) $Fixture.Source 2 0 -Quantity 5
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Created)" 'Get' $Fixture.Headers) $Fixture.Created 1 3 -Quantity 2 -TemplateId $Fixture.Source
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Other)" 'Get' $Fixture.Headers) $Fixture.Other 1 1 -Quantity 1
    }
    if($inventoryRecoveryIsClaimMail) {
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Source)" 'Get' $Fixture.Headers) $Fixture.Source 2 0 -Quantity 7 -ClaimedMail
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Other)" 'Get' $Fixture.Headers) $Fixture.Other 1 1 -Quantity 1
        if(@($items.items | Where-Object {$_.state.instanceId -ceq $Fixture.Source -and $_.location.containerType -ceq 'Mail'}).Count) {
            throw 'Restoring a claim recreated the mail attachment.'
        }
    }
    if($inventoryRecoveryIsEquipment) {
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Source)" 'Get' $Fixture.Headers) $Fixture.Source 2 0 -Quantity 1 -EquipmentTransition $InventoryRecoveryOperation
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($Fixture.Other)" 'Get' $Fixture.Headers) $Fixture.Other 1 1 -Quantity 1
    }
    $session=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    $membership=Invoke-Api "dungeon-sessions/$sessionId/membership-snapshot" 'Get' $Fixture.Headers
    if($session.state -cne 'InProgress' -or $session.serverId -cne $serverId -or $session.difficulty -cne $LobbyDifficulty -or
        $membership.dungeonSessionId -cne $sessionId -or $membership.serverId -cne $serverId -or
        @($membership.members).Count -ne 1 -or $membership.members[0].characterId -cne $Fixture.Owner -or
        $membership.members[0].steamId -cne $members[0].SteamId -or @($membership.abandonments).Count -ne 0 -or
        @($membership.disconnectionExpulsions).Count -ne 0 -or (Test-Path -LiteralPath $gate)) {
        throw 'Inventory restart changed allocation/membership or started combat.'
    }
}

function Assert-InventoryEquipmentRuntime {
    param([string]$Log,[hashtable]$Fixture,[switch]$Restored,[object]$VerifiedPrefix)
    if(-not $inventoryRecoveryIsEquipment) { return }
    $equipped=if($Restored){$inventoryRecoveryIsEquip}else{$inventoryRecoveryIsUnequip}
    $expected=if($equipped){"Head=1 Item=$($Fixture.Source) Revision=$(if($Restored){2}else{1}) Applied=1"}else{'Head=0 Item=00000000-0000-0000-0000-000000000000 Revision=0 Applied=0'}
    if(-not $VerifiedPrefix) { Wait-Marker $Log "RPG_EQUIPMENT_RECOVERY_STATE $expected" }
    [string]$body=Get-Content -LiteralPath $Log -Raw -Encoding UTF8
    if($VerifiedPrefix) {
        if($VerifiedPrefix.Log -cne $Log -or $VerifiedPrefix.Characters -le 0 -or $body.Length -lt $VerifiedPrefix.Characters) {
            throw 'Equipment runtime active boundary is missing or belongs to another log.'
        }
        $body=$body.Substring(0,$VerifiedPrefix.Characters)
        if((Get-InventoryRestartTextSHA256 $body) -cne $VerifiedPrefix.SHA256) { throw 'Equipment runtime active log boundary changed after shutdown.' }
    }
    $states=@([regex]::Matches($body,'RPG_EQUIPMENT_RECOVERY_STATE Head=[^\r\n]+'))
    $applies=@([regex]::Matches($body,'RPG_EQUIPMENT_RECOVERY_RESOURCE APPLY Item=[^\r\n]+'))
    $removes=@([regex]::Matches($body,'RPG_EQUIPMENT_RECOVERY_RESOURCE REMOVE Item=[^\r\n]+'))
    if($states.Count -eq 0 -or -not $states[-1].Value.StartsWith("RPG_EQUIPMENT_RECOVERY_STATE $expected Total=") -or
        $applies.Count -ne $(if($equipped){1}else{0}) -or $removes.Count -ne 0 -or
        ($equipped -and -not $applies[0].Value.StartsWith("RPG_EQUIPMENT_RECOVERY_RESOURCE APPLY Item=$($Fixture.Source) Revision=$(if($Restored){2}else{1})"))) {
        throw 'Equipment runtime did not preserve the original head state or duplicated/removed applied resources.'
    }
    # Client-first shutdown can legitimately destroy its pawn and remove gear. Freeze the
    # active play prefix before stopping our processes; closed-log diagnostics still scan all bytes.
    return [pscustomobject]@{Log=$Log;Characters=$body.Length;SHA256=(Get-InventoryRestartTextSHA256 $body)
        HeadEquipped=[bool]$equipped;ResourceApplies=$applies.Count;ResourceRemoves=$removes.Count;StateObservations=$states.Count}
}

function Complete-InventoryRecoveryRestartFixture {
    param([hashtable]$Fixture,[string]$Request,[object]$OriginalProxy,[string]$FrozenActive,[string]$FrozenReceipt)
    $f=$Fixture; $moveSwapFixture=$f
    $pending=Read-InventoryRestartJournal $f $Request $OriginalProxy.commandSHA256
    $receiptDigest=Read-InventoryRestartReceiptDigest $Request $f.Owner
    Assert-InventoryRestartStoredState $f $Request $FrozenActive $receiptDigest
    Assert-Healthy
    $oldServerLog=$serverLog; $oldClientLog=$clientLogs[0]
    $oldEquipmentBoundary=Assert-InventoryEquipmentRuntime $oldServerLog $f
    $oldPids=@(Stop-InventoryRestartOwnedUE)
    if((Get-FileHash -LiteralPath $pending.Path).Hash -cne $pending.SHA256) { throw 'Pending journal changed during owned shutdown.' }
    Copy-Item -LiteralPath $pending.Path -Destination (Join-Path $runRoot 'inventory-journal-pending.json')
    [ordered]@{Passed=$false;Scope='Durable pending boundary only; restart recovery not yet observed';OldPids=$oldPids
        SessionId=$sessionId;ServerId=$serverId;RequestId=$Request;PendingJournal=$pending;FrozenCommandSHA256=$OriginalProxy.commandSHA256
        ImmutableReceiptRowSHA256=$receiptDigest} | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath (Join-Path $runRoot 'inventory-restart-boundary.proof.json') -Encoding UTF8
    # Keep the same proxy, backend, assignment credential, server UserDir and explicit journal volume.
    $script:inventoryRestartRequest=$Request
    try {
        $env:PROJECT_RPG_GAME_SERVER_ID=$serverId
        $env:PROJECT_RPG_DUNGEON_SESSION_ID=$sessionId
        $env:PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN=[string]$assignment.gameServerAccessToken
        $script:serverLog=Start-Unreal 'server' '/Game/Maps/bossmap' @('-server',"-port=$Port",
            '-RPGItemE2EServer','-RPGItemE2EAllowUnverifiedIdentity',"-RPGFlowE2EPlayers=$ClientCount",('-RPGFlowE2EGate="'+$gate+'"')) -Stage 'restart'
    } finally {
        foreach($name in @('PROJECT_RPG_GAME_SERVER_ID','PROJECT_RPG_DUNGEON_SESSION_ID','PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
            [Environment]::SetEnvironmentVariable($name,$null,'Process')
        }
    }
    Wait-Marker $serverLog 'DUNGEON_FLOW_E2E_SERVER_READY ActualBossMap=1 Iris=1 Assignment=1 AdmissionBypass=0'
    $script:clientLogs=@(Start-Unreal 'client1' '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' @('-game',
        "-RPGFlowE2ESteam=$($members[0].SteamId)","-RPGFlowE2ECharacter=$($f.Owner)","-RPGFlowE2ESession=$sessionId") -Stage 'restart')
    Wait-Marker $clientLogs[0] 'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1'
    Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI RESTORED Request=$Request FreshProjection=1 Locked=1 Button=1 NoDrag=1"
    if($inventoryRecoveryIsClaimMail) { Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_RESTORED SameItemId=1 Quantity=7 Revision=2 MailEmpty=1 InventoryLocked=1 ClaimClick=0' }
    if($inventoryRecoveryIsEquipment) {
        Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI EQUIPMENT_RESTORED Operation=$inventoryRecoveryExpectedOperation SameItemId=1 Quantity=1 Revision=2 RuntimeSnapshot=1 Locked=1 ActionClicks=0"
        $null=Assert-InventoryEquipmentRuntime $serverLog $f -Restored
    }
    if((Get-FileHash -LiteralPath $pending.Path).Hash -cne $pending.SHA256) { throw 'Read-only restore altered the pending journal.' }
    $restored=Read-InventoryRecoveryProxy
    if($restored.replayed -ne 0 -or $restored.released -or $restored.blocked -ne $OriginalProxy.blocked -or
        $restored.forwarded -ne 1 -or $restored.commandSHA256 -cne $OriginalProxy.commandSHA256) { throw 'Restoring automatically sent or replanned an item command.' }
    Assert-InventoryRestartStoredState $f $Request $FrozenActive $receiptDigest
    $restored | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'inventory-proxy-restored.json') -Encoding UTF8
    $release=Invoke-RestMethod -Uri "$inventoryControlUrl/release" -Method Post -Headers $inventoryProxyHeaders -ContentType 'application/json' -Body '{}'
    if(-not $release.released) { throw 'Restored manual recovery was not released.' }
    New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-recovery-restart.gate') | Out-Null
    Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI RESTORED_CLICKED Request=$Request HitTest=1 Routed=1 Physical=0"
    Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI RESTART_PASS Authenticated=1 Iris=1 AuthoredWBP=1 FreshProjection=1 SameRequest=1 AlreadyApplied=1 Unlocked=1 Physical=0'
    if($inventoryRecoveryIsSwap) { Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI SWAP_RESTART_PASS Records=2 AffectedQuantity=0 BothRevision=2 BothUnlocked=1' }
    if($inventoryRecoveryIsMerge) { Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI MERGE_RESTART_PASS Records=2 AffectedQuantity=7 SourceConsumed=1 DestinationQuantity=8 DestinationRevision=2 DestinationUnlocked=1' }
    if($inventoryRecoveryIsSplit) { Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI SPLIT_RESTART_PASS Records=2 AffectedQuantity=2 SourceQuantity=5 SourceRevision=2 CreatedQuantity=2 CreatedRevision=1 SameCreatedId=1 BothUnlocked=1' }
    if($inventoryRecoveryIsClaimMail) { Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_RESTART_PASS NativeMail=1 SameRequest=1 SameItemId=1 Quantity=7 Revision=2 MailEmpty=1 InventoryUnlocked=1' }
    if($inventoryRecoveryIsEquipment) {
        Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI $($InventoryRecoveryOperation.ToUpperInvariant())_RESTART_PASS AuthoredEquipmentWBP=1 SameRequest=1 SameItemId=1 Quantity=1 Revision=2 RuntimeSnapshot=1 Unlocked=1"
        $null=Assert-InventoryEquipmentRuntime $serverLog $f -Restored
    }
    Wait-Marker $serverLog "RPG_ITEM_CONCURRENCY_SERVER OUTCOME Request=$Request Stage=Completed Backend=AlreadyApplied"
    $resolved=Read-InventoryRestartJournal $f $Request $OriginalProxy.commandSHA256 -Resolved
    if($resolved.Path -cne $pending.Path -or [uint64]$resolved.Generation -ne ([uint64]$pending.Generation+1)) { throw 'Original journal was not durably resolved exactly once.' }
    $final=Read-InventoryRecoveryProxy
    if($final.forwarded -ne 1 -or $final.replayed -ne 1 -or $final.droppedAcknowledgements -ne 1 -or
        $final.blocked -ne $OriginalProxy.blocked -or $final.commandSHA256 -cne $OriginalProxy.commandSHA256 -or
        $final.recordReceiptSHA256 -cne $final.replayRecordReceiptSHA256 -or $final.injectedRecovery401 -ne 0 -or
        $final.expiredForwarded -ne 0) { throw 'Restored recovery did not use one exact immutable receipt replay.' }
    if(((Invoke-Api "item-transactions/$Request" 'Get' $f.Headers) | ConvertTo-Json -Depth 30 -Compress) -cne $FrozenReceipt) {
        throw 'Restored recovery changed the original receipt.'
    }
    Assert-InventoryRestartStoredState $f $Request $FrozenActive $receiptDigest
    Assert-Healthy
    $equipmentBoundary=Assert-InventoryEquipmentRuntime $serverLog $f -Restored
    $newPids=@(Stop-InventoryRestartOwnedUE)
    if(@($newPids | Where-Object {$_ -in $oldPids}).Count) { throw 'Inventory restart reused an old process identity.' }
    foreach($process in @($processes.ToArray() | Where-Object {$_.ProcessName -ceq 'node'})) {
        Stop-Process -InputObject $process; if(-not $process.WaitForExit(10000)) { throw 'Proxy log freeze timed out.' }
        [void]$processes.Remove($process); $process.Dispose()
    }
    Assert-InventoryRestartStoredState $f $Request $FrozenActive $receiptDigest
    if((Get-FileHash -LiteralPath $resolved.Path).Hash -cne $resolved.SHA256) { throw 'Resolved journal changed during shutdown.' }
    Copy-Item -LiteralPath $resolved.Path -Destination (Join-Path $runRoot 'inventory-journal-resolved.json')
    foreach($path in $resultSourcePaths) {
        if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { throw 'Selected inventory restart source changed.' }
    }
    if((Get-FileHash -LiteralPath $unrealModulePath).Hash -cne $unrealModuleHash) { throw 'Inventory restart module changed.' }
    $diagnostics=0
    $logHashes=@(foreach($log in $logs) {
        $text=[string](Get-Content -LiteralPath $log -Raw -Encoding UTF8)
        $diagnostics += [regex]::Matches($text,': Error:|Fatal error:|Assertion failed:|Ensure condition failed:').Count
        @{Path=$log;SHA256=(Get-FileHash -LiteralPath $log).Hash}
    })
    foreach($log in @($oldClientLog,$clientLogs[0])) {
        $text=[string](Get-Content -LiteralPath $log -Raw -Encoding UTF8)
        if([regex]::Matches($text,'DUNGEON_FLOW_E2E_CONNECT RealAuth=1 Resume=1 FreshTicket=1').Count -ne 1 -or
            [regex]::Matches($text,'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1').Count -ne 1) { throw 'Missing distinct authenticated client admission/fresh ticket.' }
    }
    if($inventoryRecoveryIsEquipment) {
        [string]$originalClient=Get-Content -LiteralPath $oldClientLog -Raw -Encoding UTF8
        [string]$restoredClient=Get-Content -LiteralPath $clientLogs[0] -Raw -Encoding UTF8
        if([regex]::Matches($originalClient,"RPG_INVENTORY_RECOVERY_UI EQUIPMENT_ACTION Operation=$([regex]::Escape($inventoryRecoveryExpectedOperation)) Quantity=1 Destination=0 RightMouse=1 HitTest=1 Routed=1 Physical=0").Count -ne 1 -or
            [regex]::Matches($restoredClient,'RPG_INVENTORY_RECOVERY_UI EQUIPMENT_ACTION ').Count -ne 0) { throw 'Equipment action was not one original routed click and zero restored re-actions.' }
        $null=Assert-InventoryEquipmentRuntime $oldServerLog $f -VerifiedPrefix $oldEquipmentBoundary
        $null=Assert-InventoryEquipmentRuntime $serverLog $f -Restored -VerifiedPrefix $equipmentBoundary
    }
    $final | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'inventory-proxy-final.json') -Encoding UTF8
    [ordered]@{Passed=($diagnostics -eq 0);Mode='InventoryRecoveryRestart';Operation=$inventoryRecoveryExpectedOperation;SessionId=$sessionId;RequestId=$Request
        OldPids=$oldPids;NewPids=$newPids;ServerRestartVerified=$true;SameAssignmentAndJournalVolume=$true;FreshClientUserDir=$true
        PendingJournal=$pending;ResolvedJournal=$resolved;NoAutomaticReplayOnRestore=$true;ManualFrozenReplayForwarded=1
        FrozenCommandSHA256=$final.commandSHA256;RecordReceiptSHA256=$final.recordReceiptSHA256;ImmutableReceiptRowSHA256=$receiptDigest
        DurableMoveReceipts=$(if($inventoryRecoveryIsSwap -or $inventoryRecoveryIsMerge -or $inventoryRecoveryIsSplit -or $inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsEquipment){0}else{1});DurableSwapReceipts=$(if($inventoryRecoveryIsSwap){1}else{0})
        DurableMergeReceipts=$(if($inventoryRecoveryIsMerge){1}else{0});ConservedMergeQuantity=$(if($inventoryRecoveryIsMerge){8}else{$null})
        ConsumedMergeSourcePreserved=[bool]$inventoryRecoveryIsMerge;MergeDestinationUnlocked=[bool]$inventoryRecoveryIsMerge
        DurableSplitReceipts=$(if($inventoryRecoveryIsSplit){1}else{0});CreatedSplitItemId=$(if($inventoryRecoveryIsSplit){$f.Created}else{$null})
        ConservedSplitQuantity=$(if($inventoryRecoveryIsSplit){7}else{$null});OriginalSplitIdentityPreserved=[bool]$inventoryRecoveryIsSplit
        SyntheticSlateSplitConfirmationClicks=$(if($inventoryRecoveryIsSplit){1}else{0})
        DurableMailClaimReceipts=$(if($inventoryRecoveryIsClaimMail){1}else{0});OriginalMailItemId=$(if($inventoryRecoveryIsClaimMail){$f.Source}else{$null})
        ConservedMailQuantity=$(if($inventoryRecoveryIsClaimMail){7}else{$null});NativeMailRecoveryVerified=[bool]$inventoryRecoveryIsClaimMail
        MailAbsentOnRestoreAndAfterRecovery=[bool]$inventoryRecoveryIsClaimMail;RestoredClaimClicks=0
        DurableEquipReceipts=$(if($inventoryRecoveryIsEquip){1}else{0});DurableUnequipReceipts=$(if($inventoryRecoveryIsUnequip){1}else{0})
        OriginalEquipmentItemId=$(if($inventoryRecoveryIsEquipment){$f.Source}else{$null});AuthoredEquipmentWBP=[bool]$inventoryRecoveryIsEquipment
        EquipmentRuntimeSnapshotVerified=[bool]$inventoryRecoveryIsEquipment;EquipmentResourcesNotReapplied=[bool]$inventoryRecoveryIsEquipment
        OriginalEquipmentRuntimeBoundary=$oldEquipmentBoundary;RestoredEquipmentRuntimeBoundary=$equipmentBoundary
        SyntheticSlateEquipmentRightClicks=$(if($inventoryRecoveryIsEquipment){1}else{0});RestoredEquipmentActionClicks=0
        AtomicRecords=$(if($inventoryRecoveryIsSwap -or $inventoryRecoveryIsMerge -or $inventoryRecoveryIsSplit){2}else{1});BothSwapRecordsPreserved=[bool]$inventoryRecoveryIsSwap
        NoExtraCommitOrRevisionIncrease=$true;FullInventoryAndStarterItemsPreserved=$true
        AuthenticatedJoinTicketAdmission=$true;AdmissionBypass=$false;DevelopmentUnverifiedPlatformIdentityFixture=$true
        ActualHttpPostgreSQL=$true;AuthoredInventoryWBP=$true;SyntheticSlateDrags=$(if($inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsEquipment){0}else{1});SyntheticSlateRecoveryButtonClicks=1
        SyntheticSlateMailClaimClicks=$(if($inventoryRecoveryIsClaimMail){1}else{0})
        ProjectionOrRecoveryInjected=$false;CombatGateOpened=$false;CredentialReissued=$false;DatabaseRollback=$false
        PhysicalInputVerified=$false;SteamShippingVerified=$false;ArtificialPacketLatencyLossVerified=$false
        OtherThreeBagOperationsRestartVerified=$false;BackendRestartVerified=$false
        BackendAssemblySHA256=(Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll')).Hash
        EditorModuleSHA256=$unrealModuleHash;SelectedInputs=$resultSourcePaths.Count;SourcesUnchanged=$true;ModuleUnchanged=$true
        ClosedRuntimeDiagnostics=$diagnostics;ClosedLogs=$logHashes
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if($diagnostics -ne 0) { throw 'Closed inventory restart logs contain runtime errors.' }
}
