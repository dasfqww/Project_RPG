. (Join-Path $PSScriptRoot 'TargetDataWireProof.ps1')

function Get-TargetDataRateProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ServerText,
        [Parameter(Mandatory)] [string[]] $ClientTexts,
        [ValidateRange(0, 2000)] [int] $PacketLagMinMs = 0,
        [ValidateRange(0, 2000)] [int] $PacketLagMaxMs = 0,
        [ValidateRange(0, 50)] [int] $PacketLossPercent = 0
    )
    if ($PacketLagMaxMs -lt $PacketLagMinMs) { throw 'Invalid packet lag range.' }
    $proof = Get-TargetDataWireProof -ServerText $ServerText -ClientTexts $ClientTexts
    foreach ($logText in (@($ServerText) + @($ClientTexts))) {
        if ($logText -match 'RPG_TARGETDATA_RATE FAILED') { throw 'TargetData rate runtime failure.' }
        foreach ($setting in @(
            @{ name = 'PktLoss'; value = $PacketLossPercent },
            @{ name = 'PktLagMin'; value = $PacketLagMinMs },
            @{ name = 'PktLagMax'; value = $PacketLagMaxMs }
        )) {
            $settings = [regex]::Matches($logText, 'LogNet: ' + $setting.name + ' set to (\d+)\b')
            if ($settings.Count -eq 0 -or [int]$settings[$settings.Count - 1].Groups[1].Value -ne $setting.value) {
                throw "Packet simulation was not applied as requested: $($setting.name)."
            }
        }
    }
    $peers = @($proof.ownershipRejections | ForEach-Object { $_.sendingPeer } | Sort-Object)
    $roles = @{ "$($peers[0])" = 'Attacker'; "$($peers[1])" = 'Control' }
    $config = [regex]::Matches($ServerText, '(?m)RPG_TARGETDATA_RATE CONFIG Peer=(\d+) Role=(Attacker|Control) Limit=64 Burst=96 Normal=48 Idle=2\r?$')
    if ($config.Count -ne 2 -or [regex]::Matches($ServerText, 'RPG_TARGETDATA_RATE CONFIG').Count -ne 2) {
        throw 'Missing/duplicate/malformed rate configuration.'
    }
    foreach ($peer in $peers) {
        if (@($config | Where-Object { $_.Groups[1].Value -eq "$peer" -and $_.Groups[2].Value -eq $roles["$peer"] }).Count -ne 1) {
            throw 'Rate roles do not match the independent wire peers.'
        }
    }
    $windowPattern = '(?m)RPG_TARGETDATA_RATE WINDOW Peer=(\d+) Stage=([0-2]) Role=(Attacker|Control) Index=(\d+) First=(\d+) RX=(\d+) ConfirmRX=(\d+) CancelRX=(\d+) Accepted=(\d+) Cancelled=(\d+) Started=(\d+\.\d{6}) Ended=(\d+\.\d{6}) Limit=64\r?$'
    $stagePattern = '(?m)RPG_TARGETDATA_RATE STAGE Peer=(\d+) Stage=([0-2]) Role=(Attacker|Control) RX=(\d+) CancelRX=(\d+) Accepted=(\d+) Cancelled=(\d+) RateDropped=(\d+) Windows=(\d+) CacheSafe=1\r?$'
    $normalPattern = '(?m)RPG_TARGETDATA_RATE NORMAL Peer=(\d+) Stage=([0-2]) Index=(\d+) At=(\d+\.\d{6}) Accepted=1\r?$'
    $windows = [regex]::Matches($ServerText, $windowPattern)
    $stages = [regex]::Matches($ServerText, $stagePattern)
    $normals = [regex]::Matches($ServerText, $normalPattern)
    if ($windows.Count -eq 0 -or $windows.Count -ne [regex]::Matches($ServerText, 'RPG_TARGETDATA_RATE WINDOW').Count -or
        $stages.Count -ne 6 -or [regex]::Matches($ServerText, 'RPG_TARGETDATA_RATE STAGE').Count -ne 6 -or
        $normals.Count -ne 97 -or [regex]::Matches($ServerText, 'RPG_TARGETDATA_RATE NORMAL').Count -ne 97) {
        throw 'Missing/duplicate/malformed receipt-window, stage or normal-peer evidence.'
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $epsilon = 0.000002
    $summaries = @()
    $receiptWindows = @{}
    $verifiedWindowCount = 0
    foreach ($peer in $peers) {
        $role = $roles["$peer"]
        $previousEnd = -1.0
        for ($stage = 0; $stage -lt 3; ++$stage) {
            $row = @($stages | Where-Object { $_.Groups[1].Value -eq "$peer" -and $_.Groups[2].Value -eq "$stage" -and $_.Groups[3].Value -eq $role })
            if ($row.Count -ne 1) { throw 'Missing/duplicate/mismatched peer stage.' }
            $phaseWindows = @($windows | Where-Object { $_.Groups[1].Value -eq "$peer" -and $_.Groups[2].Value -eq "$stage" -and $_.Groups[3].Value -eq $role } |
                Sort-Object { [int]$_.Groups[4].Value })
            if ($phaseWindows.Count -eq 0 -or $phaseWindows.Count -ne [int]$row[0].Groups[9].Value) { throw 'Incorrect stage window count.' }
            $verifiedWindowCount += $phaseWindows.Count
            $count = if ($stage -eq 2) { 1 } elseif ($role -eq 'Attacker') { 96 } else { 48 }
            $first = 0; $confirm = 0; $cancel = 0; $accepted = 0; $cancelled = 0; $dropped = 0
            $previousWindow = $null
            $verifiedWindows = @()
            for ($wi = 0; $wi -lt $phaseWindows.Count; ++$wi) {
                $window = $phaseWindows[$wi]
                $rx = [int]$window.Groups[6].Value
                $started = [double]::Parse($window.Groups[11].Value, $culture)
                $ended = [double]::Parse($window.Groups[12].Value, $culture)
                if ([int]$window.Groups[4].Value -ne $wi -or [int]$window.Groups[5].Value -ne $first -or
                    $rx -lt 1 -or $rx -gt $count - $first -or $ended + $epsilon -lt $started -or $ended - $started -ge 1.0 + $epsilon) {
                    throw 'Invalid/discontinuous receipt window.'
                }
                if ($null -ne $previousWindow -and
                    ($started + $epsilon -lt $previousWindow.started + 1.0 -or $started + $epsilon -lt $previousWindow.ended)) {
                    throw 'Receipt windows reset before one second elapsed.'
                }
                if ($wi -eq 0 -and $previousEnd -ge 0.0 -and $started + $epsilon -lt $previousEnd + 2.0) {
                    throw 'Recovery phase did not let the real limiter expire.'
                }
                $wc = 0; $wx = 0; $wa = 0; $wk = 0
                for ($offset = 0; $offset -lt $rx; ++$offset) {
                    $index = $first + $offset
                    $isCancel = $role -eq 'Attacker' -and (($stage -eq 0 -and $index % 2 -ne 0) -or
                        ($stage -eq 1 -and $index -lt 64 -and $index % 3 -eq 2))
                    $fresh = -not ($role -eq 'Attacker' -and $stage -eq 1 -and $index -lt 64)
                    if ($isCancel) { ++$wx } else { ++$wc }
                    if ($fresh -and $offset -lt 64) {
                        if ($isCancel) { ++$wk } else { ++$wa }
                    } elseif ($fresh) { ++$dropped }
                }
                if ([int]$window.Groups[7].Value -ne $wc -or [int]$window.Groups[8].Value -ne $wx -or
                    [int]$window.Groups[9].Value -ne $wa -or [int]$window.Groups[10].Value -ne $wk -or $wa + $wk -gt 64) {
                    throw 'Receipt-time cap, confirm/cancel budget or rejected-request accounting mismatch.'
                }
                $verified = [ordered]@{ index = $wi; firstRequest = $first; requests = $rx; confirms = $wc;
                    cancels = $wx; accepted = $wa; cancelled = $wk; started = $started; ended = $ended }
                $verifiedWindows += $verified
                $previousWindow = $verified
                $first += $rx; $confirm += $wc; $cancel += $wx; $accepted += $wa; $cancelled += $wk
            }
            if ($first -ne $count -or [int]$row[0].Groups[4].Value -ne $confirm -or [int]$row[0].Groups[5].Value -ne $cancel -or
                [int]$row[0].Groups[6].Value -ne $accepted -or [int]$row[0].Groups[7].Value -ne $cancelled -or
                [int]$row[0].Groups[8].Value -ne $dropped -or ($role -eq 'Attacker' -and $stage -lt 2 -and $dropped -lt 1)) {
                throw 'Incomplete burst, wrong stage totals or no valid fresh request actually rate-limited.'
            }
            $previousEnd = $verifiedWindows[-1].ended
            $receiptWindows["$peer/$stage"] = $verifiedWindows
            $summaries += [ordered]@{ peer = $peer; role = $role; stage = $stage; confirmRequests = $confirm;
                cancelRequests = $cancel; accepted = $accepted; cancelled = $cancelled; rateDroppedFreshRequests = $dropped;
                windows = $verifiedWindows }
            if ($role -eq 'Control') {
                $normalRows = @($normals | Where-Object { $_.Groups[1].Value -eq "$peer" -and $_.Groups[2].Value -eq "$stage" } |
                    Sort-Object { [int]$_.Groups[3].Value })
                if ($normalRows.Count -ne $count) { throw 'Normal peer did not receive every expected callback.' }
                $lastAt = -1.0
                for ($ni = 0; $ni -lt $count; ++$ni) {
                    $at = [double]::Parse($normalRows[$ni].Groups[4].Value, $culture)
                    $containing = @($verifiedWindows | Where-Object { $ni -ge $_.firstRequest -and $ni -lt $_.firstRequest + $_.requests })
                    if ([int]$normalRows[$ni].Groups[3].Value -ne $ni -or $at + $epsilon -lt $lastAt -or
                        $containing.Count -ne 1 -or $at + $epsilon -lt $containing[0].started -or $at - $epsilon -gt $containing[0].ended) {
                        throw 'Normal callback identities/times do not match actual receipt windows.'
                    }
                    if (($ni -eq $containing[0].firstRequest -and [Math]::Abs($at - $containing[0].started) -gt $epsilon) -or
                        ($ni -eq $containing[0].firstRequest + $containing[0].requests - 1 -and [Math]::Abs($at - $containing[0].ended) -gt $epsilon)) {
                        throw 'Normal callback endpoints do not match the observed window.'
                    }
                    $lastAt = $at
                }
            }
        }
        $complete = "RPG_TARGETDATA_RATE SERVER_COMPLETE Peer=$peer Role=$role Stages=3 Recovery=1"
        if ([regex]::Matches($ServerText, '(?m)' + [regex]::Escape($complete) + '\r?$').Count -ne 1) { throw 'Missing/duplicate rate server completion.' }
    }
    if ($verifiedWindowCount -ne $windows.Count) { throw 'Unaccounted/unknown peer receipt window.' }
    if ([regex]::Matches($ServerText, 'RPG_TARGETDATA_RATE SERVER_COMPLETE').Count -ne 2) { throw 'Unexpected rate server completion.' }
    $isolation = @()
    for ($stage = 0; $stage -lt 2; ++$stage) {
        $controlFirst = $receiptWindows["$($peers[1])/$stage"][0].started
        if ($receiptWindows["$($peers[0])/$stage"][0].started + $epsilon -lt $controlFirst) {
            throw 'Attacker began before any normal-peer request reached the server.'
        }
        $hotWindows = @($receiptWindows["$($peers[0])/$stage"] | Where-Object { $_.requests -gt 64 })
        $overlap = $null
        foreach ($window in $hotWindows) {
            foreach ($normal in @($normals | Where-Object { $_.Groups[1].Value -eq "$($peers[1])" -and $_.Groups[2].Value -eq "$stage" })) {
                $at = [double]::Parse($normal.Groups[4].Value, $culture)
                if ($at + $epsilon -ge $window.started -and $at -lt $window.started + 1.0) {
                    $overlap = [ordered]@{ stage = $stage; attackerWindow = $window.index;
                        controlRequest = [int]$normal.Groups[3].Value; controlReceivedAt = $at }
                    break
                }
            }
            if ($null -ne $overlap) { break }
        }
        if ($null -eq $overlap) { throw 'No normal-peer callback inside an actually overloaded attacker window.' }
        $isolation += $overlap
    }
    foreach ($clientText in $ClientTexts) {
        $baseComplete = [regex]::Match($clientText, 'RPG_TARGETDATA_WIRE CLIENT_COMPLETE Peer=(\d+) Cases=12')
        $peer = $baseComplete.Groups[1].Value
        $role = $roles[$peer]
        if ([regex]::Matches($clientText, 'RPG_TARGETDATA_RATE CLIENT_STAGE').Count -ne 3 -or
            [regex]::Matches($clientText, 'RPG_TARGETDATA_RATE CLIENT_COMPLETE').Count -ne 1) { throw 'Missing/duplicate rate client evidence.' }
        for ($stage = 0; $stage -lt 3; ++$stage) {
            $count = if ($stage -eq 2) { 1 } elseif ($role -eq 'Attacker') { 96 } else { 48 }
            $expected = "RPG_TARGETDATA_RATE CLIENT_STAGE Peer=$peer Stage=$stage Role=$role Submitted=$count"
            if ([regex]::Matches($clientText, '(?m)' + [regex]::Escape($expected) + '\r?$').Count -ne 1) { throw 'Incorrect client submission count/identity.' }
        }
        $complete = "RPG_TARGETDATA_RATE CLIENT_COMPLETE Peer=$peer Role=$role Stages=3"
        if ([regex]::Matches($clientText, '(?m)' + [regex]::Escape($complete) + '\r?$').Count -ne 1) { throw 'Incorrect rate client completion.' }
    }
    $proof['schemaVersion'] = 5
    $proof['wireRateFloodVerified'] = $true
    $proof['receiptTimeRequestCapVerified'] = $true
    $proof['rejectedAndCancellationRequestsConsumeBudgetVerified'] = $true
    $proof['normalPeerIsolationVerified'] = $true
    $proof['normalPeerReadyBeforeBurstVerified'] = $true
    $proof['rateRecoveryVerified'] = $true
    $proof['actualPacketSettingsVerified'] = $true
    $proof['unboundedFloodResilienceVerified'] = $false
    $proof['rateLimit'] = 64
    $proof['burstRequestsPerStage'] = 96
    $proof['normalRequestsPerPressureStage'] = 48
    $proof['rateStages'] = $summaries
    $proof['normalPeerIsolationChecks'] = $isolation
    return $proof
}
