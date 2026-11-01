function Get-DungeonConfirmationHoldState {
    $path=Join-Path $runRoot 'confirmation-hold.proof.json'
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try { return (Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json) } catch { return $null }
}
function Invoke-DungeonPendingReconnectLoss {
    param($Observation)
    $afterCommit=[bool]$VerifyReconnectConfirmationResponseLoss
    if ([long]$Observation.original.connection_generation -ne 1) { throw 'Confirmation-hold fixture requires a fresh generation-one party.' }
    $script:pendingReconnectClientLog=Start-DungeonReconnectClient 'client-pending'
    $pendingProcess=$processes[$processes.Count-1]
    $deadline=[DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Assert-Healthy
        $fault=Get-DungeonConfirmationHoldState
        if ($fault -and $fault.failure) { throw 'Scoped confirmation fault failed.' }
        if ($fault -and $fault.held -gt 0 -and
            (($afterCommit -and $fault.committedResponses -gt 0 -and $fault.discardedResponses -eq 0) -or
             (-not $afterCommit -and $fault.forwarded -eq 0))) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTimeOffset]::UtcNow -lt $deadline)
    $expectedMode=if ($afterCommit) {'after-commit'} else {'before-commit'}
    if (-not $fault -or $fault.held -lt 1 -or $fault.mode -cne $expectedMode -or
        ($afterCommit -and ($fault.committedResponses -lt 1 -or $fault.discardedResponses -ne 0)) -or
        (-not $afterCommit -and $fault.forwarded -ne 0)) { throw 'Actual confirmation did not reach its scoped hold boundary.' }
    Wait-Marker $serverLog 'DUNGEON_RECONNECT_SETTLEMENT CONFIRMATION_PENDING '
    $before=Get-DungeonDisconnectDurableState
    $pending=@($before.members | Where-Object character_id -eq $members[0].CharacterId)
    $expectedConfirmed=if ($afterCommit) {'2'} else {'1'}
    if ($pending.Count -ne 1 -or $pending[0].connection_generation -cne '2' -or
        $pending[0].confirmed_connection_generation -cne $expectedConfirmed) { throw 'Wrong confirmation generation at the hold boundary.' }
    if ($afterCommit) {
        if ($pending[0].disconnected_at -or $pending[0].disconnected_generation -cne '0') {
            throw 'Committed confirmation did not clear the previous disconnect.'
        }
    } elseif ([DateTimeOffset]::Parse($pending[0].disconnected_at) -ne $Observation.disconnectedAt) {
        throw 'Uncommitted confirmation incorrectly cleared the original loss.'
    }
    if (-not $processes.Contains($pendingProcess)) { throw 'Pending process is not owned.' }
    $script:expectedExitedProcesses.Add($pendingProcess)
    $exitAt=[DateTimeOffset]::UtcNow
    $pendingProcess.Kill()
    if (-not $pendingProcess.WaitForExit(10000)) { throw 'Pending owned client did not exit.' }
    $reportDeadline=[DateTimeOffset]::UtcNow.AddSeconds(30)
    do {
        Assert-Healthy
        $reported=Get-DungeonDisconnectDurableState
        $target=@($reported.members | Where-Object character_id -eq $members[0].CharacterId)
        if ($target.Count -eq 1 -and $target[0].disconnected_generation -ceq '2') { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTimeOffset]::UtcNow -lt $reportDeadline)
    if ($target.Count -ne 1 -or $target[0].disconnected_generation -cne '2' -or
        $target[0].confirmed_connection_generation -cne $expectedConfirmed -or -not $target[0].disconnected_at) {
        throw 'Pending socket loss was not fenced on the exact consumed generation.'
    }
    $lossAt=[DateTimeOffset]::Parse($target[0].disconnected_at)
    if ($afterCommit) {
        if ($lossAt -le $Observation.disconnectedAt -or $lossAt -lt $exitAt.AddSeconds(-1) -or
            $lossAt -gt [DateTimeOffset]::UtcNow.AddSeconds(5)) { throw 'Postcommit loss did not retain the new actual socket observation.' }
    } elseif ($lossAt -ne $Observation.disconnectedAt) {
        throw 'Precommit socket loss changed the original grace.'
    }
    Wait-Marker $serverLog 'DUNGEON_RECONNECT_SETTLEMENT PENDING_SOCKET_LOST '
    New-Item -ItemType File -Path (Join-Path $runRoot 'release-confirmation.gate') | Out-Null
    $releaseDeadline=[DateTimeOffset]::UtcNow.AddSeconds(30)
    do {
        Assert-Healthy
        $fault=Get-DungeonConfirmationHoldState
        if ($fault -and $fault.failure) { throw 'Delayed confirmation forwarding failed.' }
        if ($fault -and $fault.forwarded -ge 1 -and @($fault.statuses).Count -eq $fault.held -and
            (-not $afterCommit -or $fault.discardedResponses -eq $fault.held)) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTimeOffset]::UtcNow -lt $releaseDeadline)
    if (-not $fault -or $fault.forwarded -ne $fault.held -or $fault.forwarded -lt 1 -or
        (-not $afterCommit -and @($fault.statuses | Where-Object { $_ -ne 409 }).Count) -or
        ($afterCommit -and ($fault.committedResponses -lt 1 -or $fault.discardedResponses -ne $fault.held -or
            @($fault.statuses | Where-Object { $_ -notin @(200,409) }).Count))) { throw 'Held confirmation did not complete at its expected fault boundary.' }
    for ($sample=0;$sample -lt 3;$sample++) {
        Assert-Healthy
        $after=Get-DungeonDisconnectDurableState
        if (($reported.members | ConvertTo-Json -Depth 8 -Compress) -cne ($after.members | ConvertTo-Json -Depth 8 -Compress) -or
            @($after.receipts | Where-Object { $null -ne $_ }).Count -or $after.abandonments -ne 0 -or
            (Compare-Object @($before.leases | Sort-Object) @($after.leases | Sort-Object))) {
            throw 'Late confirmation completion changed durable grace or remaining membership.'
        }
        if ($sample -lt 2) { Start-Sleep -Seconds 2 }
    }
    $result=[ordered]@{pendingProcessId=$pendingProcess.Id;pendingClientLog=$pendingReconnectClientLog;
        faultMode=$expectedMode;processExitAtUtc=$exitAt.ToString('o');disconnectedAt=$lossAt.ToString('o');
        consumedGeneration='2';confirmedGenerationBefore=$expectedConfirmed;disconnectedGenerationAfter='2';
        committedConfirmationNotRolledBack=$afterCommit;newActualDisconnectObservation=$afterCommit;
        committedResponses=$fault.committedResponses;discardedResponses=$fault.discardedResponses;
        originalDisconnectedAtPreserved=(-not $afterCommit);noPawnOrIdentityBeforeConfirmation=$true;noActivationAfterCleanup=$true;
        delayedOriginalStatuses=@($fault.statuses);before=$before.members;after=$after.members}
    $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $runRoot 'PendingReconnectLoss.proof.json') -Encoding UTF8
    return $result
}
