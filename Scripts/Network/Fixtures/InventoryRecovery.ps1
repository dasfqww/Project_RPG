function Start-InventoryRecoveryProxy {
    $node=(Get-Command node -ErrorAction Stop).Source
    $script:inventoryProxyHeaders=@{ Authorization=('Bearer '+[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N')) }
    $out=Join-Path $runRoot 'inventory-proxy.stdout.log'; $err=Join-Path $runRoot 'inventory-proxy.stderr.log'
    $names=@('PROJECT_RPG_INVENTORY_PROXY_CONTROL_TOKEN','PROJECT_RPG_INVENTORY_PROXY_UPSTREAM_PORT'); $old=@{}
    foreach($name in $names) { $old[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
    try {
        $env:PROJECT_RPG_INVENTORY_PROXY_CONTROL_TOKEN=$inventoryProxyHeaders.Authorization.Substring(7)
        $env:PROJECT_RPG_INVENTORY_PROXY_UPSTREAM_PORT=[string]$backendUri.Port
        $process=Start-Process -FilePath $node -ArgumentList ('"'+(Join-Path $projectRoot 'Scripts/Network/Fixtures/InventoryRecoveryHttpProxy.mjs')+'"') `
            -WorkingDirectory $projectRoot -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
    } finally { foreach($name in $names) { [Environment]::SetEnvironmentVariable($name,$old[$name],'Process') } }
    $processes.Add($process); $logs.Add($out); $logs.Add($err)
    Wait-Marker $out 'INVENTORY_RECOVERY_PROXY_READY'
    $line=(Select-String -LiteralPath $out -Pattern 'INVENTORY_RECOVERY_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)' | Select-Object -Last 1).Line
    if($line -notmatch 'ControlPort=(\d+) ProxyPort=(\d+)') { throw 'Inventory proxy did not bind loopback ports.' }
    $script:inventoryControlUrl="http://127.0.0.1:$($Matches[1])"; $script:inventoryProxyUrl="http://127.0.0.1:$($Matches[2])"
}

function Arm-InventoryRecoveryProxy {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $authHash=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($recoveryFixture.Headers.Authorization)))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $scope=@{owner=$recoveryFixture.Owner;source=$recoveryFixture.Source;authorizationSHA256=$authHash}
    if($inventoryRecoveryIsSwap -or $inventoryRecoveryIsMerge -or $inventoryRecoveryIsSplit -or $inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsEquipment -or $inventoryRecoveryIsConsume) { $scope.operation=$inventoryRecoveryExpectedOperation; $scope.other=$recoveryFixture.Other }
    if($InventoryRecoveryRevocation) { $scope.additionalReadOwner=$members[1].CharacterId }
    if($InventoryRecoveryConsumeDeath) { $scope.holdRecoveryResponse=$true }
    $state=Invoke-RestMethod -Uri "$inventoryControlUrl/arm" -Method Post -Headers $inventoryProxyHeaders -ContentType 'application/json' `
        -Body ($scope | ConvertTo-Json -Compress)
    if($state.phase -cne 'armed') { throw 'Inventory proxy did not arm the exact server/owner/item.' }
}

function Read-InventoryRecoveryProxy {
    $state=Invoke-RestMethod -Uri "$inventoryControlUrl/state" -Headers $inventoryProxyHeaders
    if($state.failure) { throw 'Inventory proxy rejected a command or receipt.' }
    return $state
}

function Read-InventoryConsumeObservation {
    param([string]$Request,[switch]$Complete,[switch]$DeathPending)
    [string]$body=Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8
    $number='([0-9]+(?:\.[0-9]+)?)'
    $armed=[regex]::Matches($body,"RPG_CONSUME_RECOVERY_SERVER ARMED Source=$([regex]::Escape($recoveryFixture.Source)) Health=$number Max=$number AuthorizedDamage=1 DirectHPWrite=0 Effects=0")
    $locked=[regex]::Matches($body,"RPG_CONSUME_RECOVERY_SERVER LOCKED Request=$Request Health=$number Effects=0 SamePawn=1")
    $effects=[regex]::Matches($body,'RPG_CONSUME_RECOVERY_SERVER EFFECT Request=')
    $heals=[regex]::Matches($body,"RPG_CONSUME_RECOVERY_SERVER HEALED Request=$Request HealthBefore=$number HealthAfter=$number Effects=1 SamePawn=1")
    $stopped=[regex]::Matches($body,'RPG_CONSUME_RECOVERY_SERVER STOPPED Effects=1 SamePawn=1')
    $culture=[Globalization.CultureInfo]::InvariantCulture
    if($armed.Count -ne 1 -or $locked.Count -ne 1) { throw 'Consume fixture did not observe the original damaged pawn and unknown receipt without an effect.' }
    $before=[double]::Parse($armed[0].Groups[1].Value,$culture)
    $maximum=[double]::Parse($armed[0].Groups[2].Value,$culture)
    $lockedHealth=[double]::Parse($locked[0].Groups[1].Value,$culture)
    if($before -le 0 -or $before -ge $maximum -or [math]::Abs($before-$lockedHealth) -gt 0.001) { throw 'Consume effect changed health before receipt recovery.' }
    if($InventoryRecoveryConsumeDeath) {
        $dead=[regex]::Matches($body,"RPG_CONSUME_RECOVERY_SERVER DEAD Request=$Request Source=$([regex]::Escape($recoveryFixture.Source)) Health=0 Effects=0 SamePawn=1 AuthorizedDamage=1 DirectHPWrite=0")
        $resolved=[regex]::Matches($body,"RPG_CONSUME_RECOVERY_SERVER DEATH_RESOLVED Request=$Request Health=0 Effects=0 SamePawn=1 Quantity=6 Revision=2")
        $deathStopped=[regex]::Matches($body,'RPG_CONSUME_RECOVERY_SERVER DEATH_STOPPED Effects=0 SamePawn=1')
        if($effects.Count -or $heals.Count -or $stopped.Count) { throw 'A potion effect or heal was observed in death recovery.' }
        if(-not $Complete -and -not $DeathPending) {
            if($dead.Count -or $resolved.Count -or $deathStopped.Count) { throw 'Death happened before the controlled replay-response hold.' }
            return [pscustomobject]@{HealthBefore=$before;HealthAfter=$null;Effects=0;DeathMode=$true}
        }
        if($dead.Count -ne 1 -or ($DeathPending -and ($resolved.Count -or $deathStopped.Count))) { throw 'Missing or premature death recovery boundary.' }
        [string]$client=Get-Content -LiteralPath $clientLogs[0] -Raw -Encoding UTF8
        if([regex]::Matches($client,"RPG_INVENTORY_RECOVERY_UI DEAD_WAIT Request=$Request Health=0 Pending=1 OldProjection=1 SamePawn=1").Count -ne 1) {
            throw 'The owning client did not observe death while the exact replay was still pending.'
        }
        if($Complete -and ($resolved.Count -ne 1 -or $deathStopped.Count -ne 1 -or
            [regex]::Matches($client,'RPG_INVENTORY_RECOVERY_UI CONSUME_DEATH_PASS Input=Bag SameRequest=1 SameItemId=1 Quantity=6 Revision=2 HealthAfter=0 Unlocked=1 SamePawn=1').Count -ne 1 -or
            [regex]::Matches($client,"RPG_INVENTORY_RECOVERY_UI CONSUME_CLICK Input=Bag Quantity=7 Revision=1 Health=$number HitTest=1 Routed=1 Physical=0").Count -ne 1 -or
            [regex]::Matches($client,"RPG_INVENTORY_RECOVERY_UI CLICKED Request=$Request").Count -ne 1 -or
            [regex]::Matches($client,'RPG_INVENTORY_RECOVERY_UI DRAG Source=').Count -ne 0)) { throw 'Death recovery did not finish through the original UI request without healing.' }
        return [pscustomobject]@{HealthBefore=$before;HealthAfter=0;Effects=0;DeathMode=$true;OriginalPawnPreserved=$true;Complete=[bool]$Complete}
    }
    if(-not $Complete) {
        if($effects.Count -ne 0 -or $heals.Count -ne 0 -or $stopped.Count -ne 0) { throw 'Consume effect ran before manual recovery.' }
        return [pscustomobject]@{HealthBefore=$before;HealthAfter=$null;Effects=0}
    }
    if($effects.Count -ne 1 -or $heals.Count -ne 1 -or $stopped.Count -ne 1 -or
        -not $body.Contains("RPG_CONSUME_RECOVERY_SERVER EFFECT Request=$Request Source=$($recoveryFixture.Source) Effects=1 SamePawn=1")) {
        throw 'Consume manual recovery did not execute one effect on the original pawn.'
    }
    $healBefore=[double]::Parse($heals[0].Groups[1].Value,$culture)
    $after=[double]::Parse($heals[0].Groups[2].Value,$culture)
    if([math]::Abs($before-$healBefore) -gt 0.001 -or $after -le $before -or $after -gt $maximum+0.001) { throw 'Consume recovery health evidence is invalid.' }
    [string]$client=Get-Content -LiteralPath $clientLogs[0] -Raw -Encoding UTF8
    $inputName=if($InventoryRecoveryConsumeQuickSlot){'QuickSlot'}else{'Bag'}
    if([regex]::Matches($client,"RPG_INVENTORY_RECOVERY_UI CONSUME_CLICK Input=$inputName Quantity=7 Revision=1 Health=$number HitTest=1 Routed=1 Physical=0").Count -ne 1 -or
        [regex]::Matches($client,'RPG_INVENTORY_RECOVERY_UI CLICKED Request=').Count -ne 1 -or
        [regex]::Matches($client,'RPG_INVENTORY_RECOVERY_UI DRAG Source=').Count -ne 0) { throw 'Consume did not use one routed original action and one routed recovery click.' }
    return [pscustomobject]@{HealthBefore=$before;HealthAfter=$after;Effects=1;Input=$inputName;OriginalPawnPreserved=$true}
}

function Complete-InventoryRecoveryFixture {
    $moveSwapFixture=$recoveryFixture # reuse full-record comparison and read-only counts, not the old scenario
    $f=$recoveryFixture
    Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI BLOCKED Request='
    $state=Read-InventoryRecoveryProxy
    if($state.phase -cne 'dropped' -or $state.forwarded -ne 1 -or $state.droppedAcknowledgements -ne 1 -or $state.replayed -ne 0 -or $state.released) {
        throw 'Inventory recovery did not reach committed-response-loss boundary.'
    }
    $id=[Guid]::Empty
    if(-not [Guid]::TryParseExact($state.requestId,'D',[ref]$id) -or $id -eq [Guid]::Empty) { throw 'Unsafe inventory recovery request identity.' }
    $request=$id.ToString('D')
    Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI BLOCKED Request=$request Reopened=1 OldProjection=1 Locked=1 Button=1"
    $receipt=Invoke-Api "item-transactions/$request" 'Get' $f.Headers
    $sourceSlot=if($inventoryRecoveryIsSplit -or $inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsEquipment -or $inventoryRecoveryIsConsume){0}elseif($inventoryRecoveryIsMerge){-1}elseif($inventoryRecoveryIsSwap){1}else{3}
    $sourceQuantity=if($inventoryRecoveryIsConsume){6}elseif($inventoryRecoveryIsEquipment){1}elseif($inventoryRecoveryIsSplit){5}elseif($inventoryRecoveryIsMerge){0}else{7}
    $equipmentTransition=if($inventoryRecoveryIsEquipment){$InventoryRecoveryOperation}else{''}
    $otherSlot=if($inventoryRecoveryIsSwap){0}else{1}
    $otherRevision=if($inventoryRecoveryIsSwap -or $inventoryRecoveryIsMerge){2}else{1}
    if($receipt.status -cne 'Committed' -or $receipt.operation -cne $inventoryRecoveryExpectedOperation -or $receipt.actor.ownerId -cne $f.Owner -or
        $receipt.affectedQuantity -ne $(if($inventoryRecoveryIsEquipment -or $inventoryRecoveryIsConsume){1}elseif($inventoryRecoveryIsSplit){2}elseif($inventoryRecoveryIsSwap){0}else{7}) -or
        @($receipt.records).Count -ne $(if($inventoryRecoveryIsSwap -or $inventoryRecoveryIsMerge -or $inventoryRecoveryIsSplit){2}else{1})) { throw 'Lost inventory command was not atomically committed.' }
    $sourceRecord=@($receipt.records | Where-Object {$_.state.instanceId -ceq $f.Source})
    if($sourceRecord.Count -ne 1) { throw 'Original source receipt identity is missing or duplicated.' }
    Assert-InventoryMoveSwapRecord $sourceRecord[0] $f.Source 2 $sourceSlot -Quantity $sourceQuantity -Consumed:$inventoryRecoveryIsMerge -ClaimedMail:$inventoryRecoveryIsClaimMail -EquipmentTransition $equipmentTransition
    if($inventoryRecoveryIsSplit) {
        $created=@($receipt.records | Where-Object {$_.state.instanceId -cne $f.Source -and $_.state.instanceId -cne $f.Other})
        $createdId=[Guid]::Empty
        if($created.Count -ne 1 -or -not [Guid]::TryParseExact([string]$created[0].state.instanceId,'D',[ref]$createdId) -or
            $createdId -eq [Guid]::Empty -or $createdId.ToString('D') -cne $created[0].state.instanceId) { throw 'Original generated split identity is missing or invalid.' }
        $f.Created=$createdId.ToString('D')
        Assert-InventoryMoveSwapRecord $created[0] $f.Created 1 3 -Quantity 2 -TemplateId $f.Source
        Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Created)" 'Get' $f.Headers) $f.Created 1 3 -Quantity 2 -TemplateId $f.Source
    }
    if($inventoryRecoveryIsSwap -or $inventoryRecoveryIsMerge) {
        $otherRecord=@($receipt.records | Where-Object {$_.state.instanceId -ceq $f.Other})
        if($otherRecord.Count -ne 1) { throw 'Original paired receipt identity is missing or duplicated.' }
        Assert-InventoryMoveSwapRecord $otherRecord[0] $f.Other 2 $otherSlot -Quantity $(if($inventoryRecoveryIsMerge){8}else{1})
    }
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Source)" 'Get' $f.Headers) $f.Source 2 $sourceSlot -Quantity $sourceQuantity -Consumed:$inventoryRecoveryIsMerge -ClaimedMail:$inventoryRecoveryIsClaimMail -EquipmentTransition $equipmentTransition
    Assert-InventoryMoveSwapRecord (Invoke-Api "items/$($f.Other)" 'Get' $f.Headers) $f.Other $otherRevision $otherSlot -Quantity $(if($inventoryRecoveryIsMerge){8}else{1})
    if((Read-InventoryMoveSwapCounts) -cne $inventoryRecoveryExpectedCounts) { throw 'Expected exactly one original inventory commit while the client still has the old snapshot.' }
    $frozenReceipt=$receipt | ConvertTo-Json -Depth 30 -Compress
    $before=Invoke-Api "items?ownerType=Character&ownerId=$($f.Owner)&includeTerminal=false&limit=500" 'Get' $f.Headers
    $frozenActive=$before | ConvertTo-Json -Depth 30 -Compress
    $mailPending=$null; $mailResolved=$null; $mailReceiptDigest=$null
    $consumeObservation=$null; $consumeReceiptDigest=$null
    if($inventoryRecoveryIsConsume) {
        Wait-Marker $serverLog "RPG_CONSUME_RECOVERY_SERVER LOCKED Request=$request"
        $consumeObservation=Read-InventoryConsumeObservation $request
        $consumeReceiptDigest=Read-InventoryRestartReceiptDigest $request $f.Owner
    }
    if($inventoryRecoveryIsClaimMail -and -not $InventoryRecoveryRestart) {
        $mailReceiptDigest=Read-InventoryRestartReceiptDigest $request $f.Owner
        $mailPending=Read-InventoryRestartJournal $f $request $state.commandSHA256
        Copy-Item -LiteralPath $mailPending.Path -Destination (Join-Path $runRoot 'inventory-journal-pending.json')
        if(@($before.items | Where-Object {$_.state.instanceId -ceq $f.Source -and $_.location.containerType -ceq 'Mail'}).Count) {
            throw 'Committed claim still has an active mail attachment in the database.'
        }
    }
    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'inventory-proxy-lost.json') -Encoding UTF8
    if ($InventoryRecoveryRestart) {
        Complete-InventoryRecoveryRestartFixture $f $request $state $frozenActive $frozenReceipt
        return # only the restored process may perform the manual successful retry
    }
    if ($InventoryRecoveryRevocation) {
        Complete-InventoryRecoveryRevocationFixture $f $request $state $frozenActive
        return # a revoked identity does not perform successful recovery or roll back its stored command
    }
    if ($InventoryRecoveryCredentialExpiry) {
        Complete-InventoryRecoveryCredentialExpiryFixture $f $request $state $frozenActive
        return # deliberately keep the credential expired and command pending, never enter success/release flow
    }
    if ($InventoryRecoveryRetryFailure) {
        $armed=Invoke-RestMethod -Uri "$inventoryControlUrl/fail-recovery" -Method Post -Headers $inventoryProxyHeaders -ContentType 'application/json' -Body '{}'
        if(-not $armed.recoveryFailureArmed) { throw 'Controlled recovery rejection did not arm.' }
        New-Item -ItemType File -Path (Join-Path $runRoot 'inventory-recovery-verified.gate') | Out-Null
        Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI RETRY_FAILED Request=$request Pending=1 OldProjection=1 Cooldown=1 SameAckObserved=1"
        Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI RETRY_READY Request=$request Pending=1 CooldownFinished=1"
        $failed=Read-InventoryRecoveryProxy
        if($failed.injectedRecovery401 -ne 1 -or $failed.forwarded -ne 1 -or $failed.replayed -ne 0 -or $failed.released -or
            $failed.recoveryFailureArmed -or $failed.commandSHA256 -cne $state.commandSHA256) { throw 'Failed recovery did not retain its frozen boundary.' }
        $failedItems=Invoke-Api "items?ownerType=Character&ownerId=$($f.Owner)&includeTerminal=false&limit=500" 'Get' $f.Headers
        if(($failedItems | ConvertTo-Json -Depth 30 -Compress) -cne $frozenActive -or
            ((Invoke-Api "item-transactions/$request" 'Get' $f.Headers) | ConvertTo-Json -Depth 30 -Compress) -cne $frozenReceipt -or
            (Read-InventoryMoveSwapCounts) -cne $inventoryRecoveryExpectedCounts) { throw 'Failed recovery changed the stored inventory or receipt.' }
        $failed | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'inventory-proxy-retry-failed.json') -Encoding UTF8
    }
    $release=Invoke-RestMethod -Uri "$inventoryControlUrl/release" -Method Post -Headers $inventoryProxyHeaders -ContentType 'application/json' -Body '{}'
    if(-not $release.released) { throw 'Inventory replay was not released.' }
    $continueGate=Join-Path $runRoot $(if($InventoryRecoveryRetryFailure){'inventory-recovery-verified.gate.retry'}else{'inventory-recovery-verified.gate'})
    New-Item -ItemType File -Path $continueGate | Out-Null
    if($InventoryRecoveryConsumeDeath) {
        Wait-Marker (Join-Path $runRoot 'inventory-proxy.stdout.log') 'INVENTORY_RECOVERY_PROXY_RECOVERY_HELD'
        Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI CLICKED Request=$request"
        $held=Read-InventoryRecoveryProxy
        if($held.phase -cne 'recovery-response-held' -or $held.requestId -cne $request -or
            $held.forwarded -ne 1 -or $held.replayed -ne 1 -or $held.heldRecoveryResponses -ne 1 -or $held.releasedRecoveryResponses -ne 0 -or
            $held.commandSHA256 -cne $state.commandSHA256 -or $held.recordReceiptSHA256 -cne $held.replayRecordReceiptSHA256) {
            throw 'The original exact consume recovery receipt was not held before death.'
        }
        $deathTemp=Join-Path $runRoot 'consume-death.tmp'
        @{request=$request;source=$f.Source;owner=$f.Owner} | ConvertTo-Json -Compress | Set-Content -LiteralPath $deathTemp -Encoding UTF8
        Move-Item -LiteralPath $deathTemp -Destination (Join-Path $runRoot 'consume-death.gate')
        Wait-Marker $serverLog "RPG_CONSUME_RECOVERY_SERVER DEAD Request=$request"
        Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI DEAD_WAIT Request=$request"
        $null=Read-InventoryConsumeObservation $request -DeathPending
        if((Read-InventoryRestartReceiptDigest $request $f.Owner) -cne $consumeReceiptDigest -or
            (Read-InventoryMoveSwapCounts) -cne $inventoryRecoveryExpectedCounts) { throw 'Death changed the original durable consumption.' }
        $released=Invoke-RestMethod -Uri "$inventoryControlUrl/deliver-recovery" -Method Post -Headers $inventoryProxyHeaders -ContentType 'application/json' -Body '{}'
        if($released.releasedRecoveryResponses -ne 1) { throw 'Original held recovery response was not released once.' }
    }
    Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 OldSnapshotHeld=1 Reopened=1 SameRequest=1 AlreadyApplied=1 ReservationsReleased=1 Physical=0'
    if($inventoryRecoveryIsConsume) {
        $inputName=if($InventoryRecoveryConsumeQuickSlot){'QuickSlot'}else{'Bag'}
        if($InventoryRecoveryConsumeDeath) {
            Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI CONSUME_DEATH_PASS Input=Bag SameRequest=1 SameItemId=1 Quantity=6 Revision=2'
            Wait-Marker $serverLog "RPG_CONSUME_RECOVERY_SERVER DEATH_RESOLVED Request=$request"
        } else {
            Wait-Marker $clientLogs[0] "RPG_INVENTORY_RECOVERY_UI CONSUME_PASS Input=$inputName SameRequest=1 SameItemId=1 Quantity=6 Revision=2"
            Wait-Marker $serverLog "RPG_CONSUME_RECOVERY_SERVER HEALED Request=$request"
        }
        New-Item -ItemType File -Path (Join-Path $runRoot 'consume-observation-stop.gate') | Out-Null
        Wait-Marker $serverLog $(if($InventoryRecoveryConsumeDeath){'RPG_CONSUME_RECOVERY_SERVER DEATH_STOPPED Effects=0 SamePawn=1'}else{'RPG_CONSUME_RECOVERY_SERVER STOPPED Effects=1 SamePawn=1'})
        $consumeObservation=Read-InventoryConsumeObservation $request -Complete
        if((Read-InventoryRestartReceiptDigest $request $f.Owner) -cne $consumeReceiptDigest) { throw 'Consume recovery changed the original receipt row.' }
    }
    if($inventoryRecoveryIsClaimMail) {
        Wait-Marker $clientLogs[0] 'RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_PASS NativeMail=1 ClaimClick=1 MailReopened=1 SameRequest=1 AlreadyApplied=1 SameItemId=1 Quantity=7 Revision=2 MailRemoved=1 InventoryAdded=1 ReservationReleased=1'
        $mailResolved=Read-InventoryRestartJournal $f $request $state.commandSHA256 -Resolved
        if($mailResolved.Path -cne $mailPending.Path -or [uint64]$mailResolved.Generation -ne ([uint64]$mailPending.Generation+1) -or
            (Read-InventoryRestartReceiptDigest $request $f.Owner) -cne $mailReceiptDigest) { throw 'Mail recovery changed the original receipt or did not resolve its journal once.' }
    }
    Wait-Marker $serverLog "RPG_ITEM_CONCURRENCY_SERVER OUTCOME Request=$request Stage=Completed Backend=AlreadyApplied"
    $final=Read-InventoryRecoveryProxy
    if($InventoryRecoveryConsumeDeath -and ($final.heldRecoveryResponses -ne 1 -or $final.releasedRecoveryResponses -ne 1)) {
        throw 'Consume death recovery released an unexpected number of held responses.'
    }
    if($final.forwarded -ne 1 -or $final.droppedAcknowledgements -ne 1 -or $final.replayed -ne 1 -or
        $final.commandSHA256 -cne $state.commandSHA256 -or $final.recordReceiptSHA256 -cne $final.replayRecordReceiptSHA256 -or
        $final.injectedRecovery401 -ne [int][bool]$InventoryRecoveryRetryFailure) {
        throw 'Recovery did not replay one byte-identical command and matching record receipt.'
    }
    $after=Invoke-Api "items?ownerType=Character&ownerId=$($f.Owner)&includeTerminal=false&limit=500" 'Get' $f.Headers
    if(($after | ConvertTo-Json -Depth 30 -Compress) -cne $frozenActive -or
        ((Invoke-Api "item-transactions/$request" 'Get' $f.Headers) | ConvertTo-Json -Depth 30 -Compress) -cne $frozenReceipt -or
        (Read-InventoryMoveSwapCounts) -cne $inventoryRecoveryExpectedCounts) { throw 'Recovery changed the committed inventory or immutable receipt.' }
    if(@($after.items).Count -ne $f.Retained.Count+2) { throw 'Recovery created or removed another item.' }
    foreach($original in $f.Retained) {
        $current=Invoke-Api "items/$($original.state.instanceId)" 'Get' $f.Headers
        if(($current | ConvertTo-Json -Depth 30 -Compress) -cne ($original | ConvertTo-Json -Depth 30 -Compress)) { throw 'Recovery changed a starter item.' }
    }
    $session=Invoke-Api "dungeon-sessions/$sessionId" 'Get' $adminHeaders
    if($session.state -cne 'InProgress' -or (Test-Path -LiteralPath $gate)) { throw 'Recovery unexpectedly started combat or settlement.' }
    Assert-Healthy
    $final | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'inventory-proxy-final.json') -Encoding UTF8
    $sameSources=$true
    foreach($path in $resultSourcePaths) { if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { $sameSources=$false } }
    $sameModule=(Get-FileHash -LiteralPath $unrealModulePath).Hash -ceq $unrealModuleHash
    $owned=@($processes | Where-Object { $_.ProcessName -cin @('UnrealEditor-Cmd','node') }); [array]::Reverse($owned)
    foreach($process in $owned) { if(-not $process.HasExited) { Stop-Process -Id $process.Id; $process.WaitForExit(10000) | Out-Null } }
    if($inventoryRecoveryIsConsume) {
        $consumeObservation=Read-InventoryConsumeObservation $request -Complete
        if((Read-InventoryRestartReceiptDigest $request $f.Owner) -cne $consumeReceiptDigest -or
            (Read-InventoryMoveSwapCounts) -cne $inventoryRecoveryExpectedCounts) { throw 'Consume receipt or item count changed during shutdown.' }
        foreach($path in $resultSourcePaths) {
            if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { $sameSources=$false }
        }
        $sameModule=(Get-FileHash -LiteralPath $unrealModulePath).Hash -ceq $unrealModuleHash
    }
    if($inventoryRecoveryIsClaimMail) {
        if((Get-FileHash -LiteralPath $mailResolved.Path).Hash -cne $mailResolved.SHA256 -or
            (Read-InventoryRestartReceiptDigest $request $f.Owner) -cne $mailReceiptDigest -or
            (Read-InventoryMoveSwapCounts) -cne $inventoryRecoveryExpectedCounts) { throw 'Mail state changed during owned shutdown.' }
        Copy-Item -LiteralPath $mailResolved.Path -Destination (Join-Path $runRoot 'inventory-journal-resolved.json')
        foreach($path in $resultSourcePaths) {
            if((Get-FileHash -LiteralPath (Join-Path $projectRoot $path)).Hash -cne $resultSourceHashes[$path]) { $sameSources=$false }
        }
        $sameModule=(Get-FileHash -LiteralPath $unrealModulePath).Hash -ceq $unrealModuleHash
    }
    $diagnostics=0
    $logHashes=@(foreach($log in $logs) {
        $text=[string](Get-Content -LiteralPath $log -Raw)
        $diagnostics += [regex]::Matches($text,': Error:|Fatal error:|Assertion failed:|Ensure condition failed:').Count
        @{Path=$log;SHA256=(Get-FileHash -LiteralPath $log).Hash}
    })
    [ordered]@{Passed=($sameSources -and $sameModule -and $diagnostics -eq 0);Mode='InventoryRecovery';Operation=$inventoryRecoveryExpectedOperation;SessionId=$sessionId
        RequestId=$request;DurableMoveReceipts=$(if($inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsConsume){0}else{1});DurableMailClaimReceipts=$(if($inventoryRecoveryIsClaimMail){1}else{0})
        DurableConsumeReceipts=$(if($inventoryRecoveryIsConsume){1}else{0});ConsumeObservation=$consumeObservation
        ConsumeDeathMode=[bool]$InventoryRecoveryConsumeDeath;ConsumeDeathObservedBeforeReceipt=[bool]$InventoryRecoveryConsumeDeath
        ConsumeOriginalItemId=$(if($inventoryRecoveryIsConsume){$f.Source}else{$null});ConsumeReceiptRowSHA256=$consumeReceiptDigest
        NativeMailRecoveryVerified=[bool]$inventoryRecoveryIsClaimMail;MailAndInventoryProjectionReconciled=[bool]$inventoryRecoveryIsClaimMail
        PendingJournal=$mailPending;ResolvedJournal=$mailResolved;ImmutableReceiptRowSHA256=$mailReceiptDigest
        DroppedCommittedHttpResponses=1;GatewayRetriesBlocked=$final.blocked;ManualFrozenReplayForwarded=1
        RetryFailureMode=[bool]$InventoryRecoveryRetryFailure;SyntheticRecovery401=$final.injectedRecovery401;ActualCredentialExpiryVerified=$false
        FailedRetryPreservesOriginalAndDatabase=[bool]$InventoryRecoveryRetryFailure;CooldownReenablesWithoutUnlocking=[bool]$InventoryRecoveryRetryFailure
        FrozenCommandSHA256=$final.commandSHA256;RecordReceiptSHA256=$final.recordReceiptSHA256;SameByteCommandReplay=$true
        DatabaseVerifiedWhileClientOldSnapshotLocked=$true;ReopenedAuthoredWBPRecoveryState=(-not $inventoryRecoveryIsClaimMail);ReopenedNativeMailRecoveryState=[bool]$inventoryRecoveryIsClaimMail;RecoveryAlreadyApplied=$true
        NoExtraCommitOrRevisionIncrease=$true;ImmutableDatabaseReceiptUnchanged=$true;FullInventoryUnchangedOnReplay=$true;StarterItemsPreserved=$true
        SyntheticSlateDrags=$(if($inventoryRecoveryIsClaimMail -or $inventoryRecoveryIsConsume){0}else{1});SyntheticSlateMailClaimClicks=$(if($inventoryRecoveryIsClaimMail){1}else{0});SyntheticSlateRecoveryButtonClicks=$(if($InventoryRecoveryRetryFailure){2}else{1});ProgrammaticWidgetTeardownReopen=1;DirectUiOrRpcSubmission=$false
        SyntheticSlateConsumeRightClicks=$(if($inventoryRecoveryIsConsume -and -not $InventoryRecoveryConsumeQuickSlot){1}else{0})
        SyntheticSlateConsumeQuickSlotClicks=$(if($InventoryRecoveryConsumeQuickSlot){1}else{0});ClientLocalShortcutAssignedThroughExistingComponent=[bool]$InventoryRecoveryConsumeQuickSlot
        AuthenticatedJoinTicketAdmission=$true;AdmissionBypass=$false;DevelopmentUnverifiedPlatformIdentityFixture=$true;ActualHttpPostgreSQL=$true
        ProjectionInjected=$false;CombatGateOpened=$false;PhysicalInputVerified=$false;ArtificialPacketLatencyLossVerified=$false;SteamShippingVerified=$false;ServerRestartVerified=$false
        BackendAssemblySHA256=(Get-FileHash -LiteralPath $backendAssemblyPath).Hash
        EditorModuleSHA256=$unrealModuleHash;SelectedInputs=$resultSourcePaths.Count;SourcesUnchanged=$sameSources;ModuleUnchanged=$sameModule
        ClosedRuntimeDiagnostics=$diagnostics;ClosedLogs=$logHashes
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    if(-not $sameSources -or -not $sameModule -or $diagnostics -ne 0) { throw 'Inventory recovery provenance or runtime diagnostics failed.' }
}

function Clear-InventoryConsumeDeathArtifacts {
    param([Parameter(Mandatory)][bool]$OwnedShutdownSucceeded)
    $expectedRun=[IO.Path]::GetFullPath((Join-Path $projectRoot ("Saved/Tests/DungeonBossReturn/"+$runId)))
    $expectedRuntime=[IO.Path]::GetFullPath((Join-Path $projectRoot ("Saved/Tests/DBR/"+$runId.Substring(0,12))))
    if($runId -cnotmatch '^[0-9a-f]{32}$' -or [IO.Path]::GetFullPath($runRoot) -ne $expectedRun -or
        [IO.Path]::GetFullPath($runtimeRoot) -ne $expectedRuntime -or [IO.Path]::GetFullPath($buildRoot) -ne (Join-Path $expectedRun 'backend-build')) {
        throw 'Consume cleanup refused unexpected allocated paths.'
    }
    $targets=@([IO.Path]::GetFullPath($buildRoot),$expectedRuntime)
    $deleted=0L;$failure=$null
    try {
        if(-not $OwnedShutdownSucceeded) { throw 'Owned consume writers did not shut down cleanly.' }
        foreach($process in $processes) { if(-not $process.HasExited) { throw 'An owned writer still uses the consume artifacts.' } }
        foreach($target in $targets) {
            if(-not (Test-Path -LiteralPath $target)) { continue }
            # Parent roots must themselves be real directories, not links into another workspace.
            $ancestor=$target
            while($ancestor -and $ancestor -ne [IO.Path]::GetFullPath($projectRoot)) {
                if((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Consume cleanup refused a reparse ancestor.' }
                $ancestor=[IO.Path]::GetDirectoryName($ancestor)
            }
            if(-not $ancestor) { throw 'Consume cleanup escaped the workspace.' }
            $files=[Collections.Generic.List[string]]::new();$directories=[Collections.Generic.List[string]]::new()
            $stack=[Collections.Generic.Stack[string]]::new();$stack.Push($target)
            while($stack.Count) {
                $path=$stack.Pop();$item=Get-Item -LiteralPath $path -Force
                if(-not ($path -eq $target -or $path.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)) -or
                    ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Consume cleanup refused a linked or outside child.' }
                if($item.PSIsContainer) {$directories.Add($path);foreach($child in Get-ChildItem -LiteralPath $path -Force){$stack.Push($child.FullName)}}else{$files.Add($path)}
            }
            foreach($file in $files) {$handle=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$handle.Dispose()}
            foreach($file in $files) {$size=(Get-Item -LiteralPath $file).Length;Remove-Item -LiteralPath $file -Force;$deleted+=$size}
            foreach($directory in $directories | Sort-Object Length -Descending){[IO.Directory]::Delete($directory,$false)}
        }
    } catch {$failure=$_.Exception.GetType().Name}
    [ordered]@{complete=($null -eq $failure);deletedBytes=$deleted;failureType=$failure;remaining=@($targets | Where-Object {Test-Path -LiteralPath $_})} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'consume-cleanup.json') -Encoding UTF8
    $resultPath=Join-Path $runRoot 'result.json'
    if(Test-Path -LiteralPath $resultPath) {
        $result=Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $result | Add-Member -NotePropertyName ConsumeTemporaryCleanupComplete -NotePropertyValue ($null -eq $failure) -Force
        $result | Add-Member -NotePropertyName ConsumeTemporaryBytesDeleted -NotePropertyValue $deleted -Force
        $result.Passed=[bool]$result.Passed -and ($null -eq $failure)
        $result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    }
    if($failure) { throw 'Owned consume death artifacts could not be fully cleaned; inspect consume-cleanup.json.' }
}
