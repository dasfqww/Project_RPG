[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
# Reuse the independent base-wire golden evidence and all of its negative checks.
. (Join-Path $PSScriptRoot 'Test-TargetDataWireProof.ps1')
. (Join-Path $PSScriptRoot '..\Network\TargetDataRateProof.ps1')
$wireServer = $server
$wireClients = $clients
$packetLines = "LogNet: PktLoss set to 10`nLogNet: PktLagMin set to 250`nLogNet: PktLagMax set to 450"
$culture = [Globalization.CultureInfo]::InvariantCulture
function New-RateEvidence([double] $ControlOffset = 0) {
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add($wireServer)
    $lines.Add($packetLines)
    foreach ($peer in 256, 257) {
        $role = if ($peer -eq 256) { 'Attacker' } else { 'Control' }
        $lines.Add("RPG_TARGETDATA_RATE CONFIG Peer=$peer Role=$role Limit=64 Burst=96 Normal=48 Idle=2")
        for ($stage = 0; $stage -lt 3; ++$stage) {
            $start = 100.05 + $stage * 10
            if ($role -eq 'Attacker') {
                $rx = if ($stage -eq 2) { 1 } else { 96 }
                $confirm = if ($stage -eq 0) { 48 } elseif ($stage -eq 1) { 75 } else { 1 }
                $cancel = $rx - $confirm
                $accepted = if ($stage -eq 0) { 32 } elseif ($stage -eq 1) { 0 } else { 1 }
                $cancelled = if ($stage -eq 0) { 32 } else { 0 }
                $drop = if ($stage -lt 2) { 32 } else { 0 }
                $end = $start + $(if ($stage -lt 2) { 0.08 } else { 0 })
                $lines.Add("RPG_TARGETDATA_RATE WINDOW Peer=$peer Stage=$stage Role=$role Index=0 First=0 RX=$rx ConfirmRX=$confirm CancelRX=$cancel Accepted=$accepted Cancelled=$cancelled Started=$($start.ToString('F6',$culture)) Ended=$($end.ToString('F6',$culture)) Limit=64")
                $lines.Add("RPG_TARGETDATA_RATE STAGE Peer=$peer Stage=$stage Role=$role RX=$confirm CancelRX=$cancel Accepted=$accepted Cancelled=$cancelled RateDropped=$drop Windows=1 CacheSafe=1")
            } else {
                $count = if ($stage -eq 2) { 1 } else { 48 }
                $start += $ControlOffset
                $windows = if ($stage -eq 2) { 1 } else { 6 }
                for ($wi = 0; $wi -lt $windows; ++$wi) {
                    $first = $wi * 8
                    $rx = [Math]::Min(8, $count - $first)
                    $ws = $start + $first * 0.125
                    $we = $ws + ($rx - 1) * 0.125
                    $lines.Add("RPG_TARGETDATA_RATE WINDOW Peer=$peer Stage=$stage Role=$role Index=$wi First=$first RX=$rx ConfirmRX=$rx CancelRX=0 Accepted=$rx Cancelled=0 Started=$($ws.ToString('F6',$culture)) Ended=$($we.ToString('F6',$culture)) Limit=64")
                }
                for ($index = 0; $index -lt $count; ++$index) {
                    $at = ($start + $index * 0.125).ToString('F6', $culture)
                    $lines.Add("RPG_TARGETDATA_RATE NORMAL Peer=$peer Stage=$stage Index=$index At=$at Accepted=1")
                }
                $lines.Add("RPG_TARGETDATA_RATE STAGE Peer=$peer Stage=$stage Role=$role RX=$count CancelRX=0 Accepted=$count Cancelled=0 RateDropped=0 Windows=$windows CacheSafe=1")
            }
        }
        $lines.Add("RPG_TARGETDATA_RATE SERVER_COMPLETE Peer=$peer Role=$role Stages=3 Recovery=1")
    }
    $clientTexts = @(for ($ci = 0; $ci -lt 2; ++$ci) {
        $peer = 256 + $ci
        $role = if ($ci -eq 0) { 'Attacker' } else { 'Control' }
        $text = $wireClients[$ci] + "`n" + $packetLines
        for ($stage = 0; $stage -lt 3; ++$stage) {
            $count = if ($stage -eq 2) { 1 } elseif ($ci -eq 0) { 96 } else { 48 }
            $text += "`nRPG_TARGETDATA_RATE CLIENT_STAGE Peer=$peer Stage=$stage Role=$role Submitted=$count"
        }
        $text + "`nRPG_TARGETDATA_RATE CLIENT_COMPLETE Peer=$peer Role=$role Stages=3"
    })
    return @{ server = $lines -join "`n"; clients = $clientTexts }
}
$golden = New-RateEvidence
$rateServer = $golden.server
$rateClients = $golden.clients
$proof = Get-TargetDataRateProof -ServerText $rateServer -ClientTexts $rateClients -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
if ($proof.schemaVersion -ne 5 -or -not $proof.wireRateFloodVerified -or -not $proof.normalPeerIsolationVerified -or
    -not $proof.normalPeerReadyBeforeBurstVerified -or
    -not $proof.rejectedAndCancellationRequestsConsumeBudgetVerified -or -not $proof.rateRecoveryVerified -or
    $proof.rateStages.Count -ne 6 -or $proof.normalPeerIsolationChecks.Count -ne 2 -or
    $proof.actualCombatEffectsVerified -or $proof.shippingRuntimeVerified -or $proof.unboundedFloodResilienceVerified) {
    throw 'Valid rate evidence or scope flags were misread.'
}
$script:ratePassed = 1
function Test-RateRejected([string] $Name, [string] $Text, [string[]] $ClientLogs = $rateClients) {
    $rejected = $false
    try {
        Get-TargetDataRateProof -ServerText $Text -ClientTexts $ClientLogs -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10 | Out-Null
    } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid rate proof accepted: $Name" }
    ++$script:ratePassed
}
# A real lossy delivery may span multiple windows. Accept its actual budget, not a fixed 64 approvals per run.
$oneWindow = 'RPG_TARGETDATA_RATE WINDOW Peer=256 Stage=0 Role=Attacker Index=0 First=0 RX=96 ConfirmRX=48 CancelRX=48 Accepted=32 Cancelled=32 Started=100.050000 Ended=100.130000 Limit=64'
$multiWindow = 'RPG_TARGETDATA_RATE WINDOW Peer=256 Stage=0 Role=Attacker Index=0 First=0 RX=70 ConfirmRX=35 CancelRX=35 Accepted=32 Cancelled=32 Started=100.050000 Ended=100.130000 Limit=64' + "`n" +
    'RPG_TARGETDATA_RATE WINDOW Peer=256 Stage=0 Role=Attacker Index=1 First=70 RX=26 ConfirmRX=13 CancelRX=13 Accepted=13 Cancelled=13 Started=101.200000 Ended=101.280000 Limit=64'
$multiServer = $rateServer.Replace($oneWindow, $multiWindow).Replace(
    'Stage=0 Role=Attacker RX=48 CancelRX=48 Accepted=32 Cancelled=32 RateDropped=32 Windows=1',
    'Stage=0 Role=Attacker RX=48 CancelRX=48 Accepted=45 Cancelled=45 RateDropped=6 Windows=2')
$multiProof = Get-TargetDataRateProof -ServerText $multiServer -ClientTexts $rateClients -PacketLagMinMs 250 -PacketLagMaxMs 450 -PacketLossPercent 10
if ($multiProof.rateStages[0].accepted -ne 45 -or $multiProof.rateStages[0].windows.Count -ne 2) { throw 'Multi-window receipts were misread.' }
++$script:ratePassed
Test-RateRejected 'base wire evidence missing' ($rateServer.Replace('RPG_TARGETDATA_WIRE SERVER_COMPLETE', 'UNKNOWN'))
Test-RateRejected 'missing configuration' ($rateServer.Replace('CONFIG Peer=257', 'UNKNOWN Peer=257'))
Test-RateRejected 'wrong cap' ($rateServer.Replace('Limit=64', 'Limit=128'))
Test-RateRejected 'wrong role' ($rateServer.Replace('CONFIG Peer=257 Role=Control', 'CONFIG Peer=257 Role=Attacker'))
Test-RateRejected 'missing receipt window' ($rateServer.Replace('WINDOW Peer=256 Stage=0', 'UNKNOWN Peer=256 Stage=0'))
Test-RateRejected 'duplicate receipt window' ($rateServer + "`n" + $oneWindow)
Test-RateRejected 'unknown peer extra window' ($rateServer + "`n" + $oneWindow.Replace('Peer=256', 'Peer=999'))
Test-RateRejected 'separate confirm and cancel budgets' ($rateServer.Replace('ConfirmRX=48 CancelRX=48 Accepted=32 Cancelled=32', 'ConfirmRX=48 CancelRX=48 Accepted=48 Cancelled=48'))
Test-RateRejected 'rejected requests did not consume budget' ($rateServer.Replace('ConfirmRX=75 CancelRX=21 Accepted=0 Cancelled=0', 'ConfirmRX=75 CancelRX=21 Accepted=32 Cancelled=0'))
Test-RateRejected 'incomplete actual burst' ($rateServer.Replace('First=0 RX=96', 'First=0 RX=64'))
Test-RateRejected 'discontinuous request indices' ($rateServer.Replace('Index=1 First=8', 'Index=1 First=9'))
Test-RateRejected 'window exceeded one second' ($rateServer.Replace('Ended=100.130000', 'Ended=101.130000'))
Test-RateRejected 'window reset early' ($rateServer.Replace('Started=101.050000', 'Started=100.950000'))
Test-RateRejected 'recovery did not expire window' ($rateServer.Replace('Started=110.050000 Ended=110.130000', 'Started=100.500000 Ended=100.580000'))
Test-RateRejected 'no fresh request rate-limited' ($rateServer.Replace('RateDropped=32', 'RateDropped=0'))
Test-RateRejected 'wrong stage totals' ($rateServer.Replace('STAGE Peer=256 Stage=0 Role=Attacker RX=48', 'STAGE Peer=256 Stage=0 Role=Attacker RX=47'))
Test-RateRejected 'rejected request cache created' ($rateServer.Replace('CacheSafe=1', 'CacheSafe=0'))
Test-RateRejected 'normal peer request rejected' ($rateServer.Replace('NORMAL Peer=257 Stage=0 Index=0 At=100.050000 Accepted=1', 'NORMAL Peer=257 Stage=0 Index=0 At=100.050000 Accepted=0'))
Test-RateRejected 'normal callback missing' ($rateServer.Replace('NORMAL Peer=257 Stage=0 Index=0 ', 'UNKNOWN Peer=257 Stage=0 Index=0 '))
Test-RateRejected 'normal callback time outside window' ($rateServer.Replace('Index=0 At=100.050000', 'Index=0 At=99.050000'))
Test-RateRejected 'normal callback endpoint changed' ($rateServer.Replace('Index=0 At=100.050000', 'Index=0 At=100.100000'))
$noOverlap = New-RateEvidence -ControlOffset 5
Test-RateRejected 'normal peer never overlapped pressure' $noOverlap.server $noOverlap.clients
Test-RateRejected 'burst began before control readiness' ($rateServer.Replace('Started=100.050000 Ended=100.130000', 'Started=99.050000 Ended=99.130000'))
Test-RateRejected 'missing stage' ($rateServer.Replace('STAGE Peer=256 Stage=1', 'UNKNOWN Peer=256 Stage=1'))
Test-RateRejected 'missing recovery completion' ($rateServer.Replace('Recovery=1', 'Recovery=0'))
Test-RateRejected 'missing client stage' $rateServer @($rateClients[0].Replace('CLIENT_STAGE Peer=256 Stage=0', 'UNKNOWN Peer=256 Stage=0'), $rateClients[1])
Test-RateRejected 'client submitted fewer requests' $rateServer @($rateClients[0].Replace('Submitted=96', 'Submitted=95'), $rateClients[1])
Test-RateRejected 'client completed under wrong role' $rateServer @($rateClients[0].Replace('CLIENT_COMPLETE Peer=256 Role=Attacker', 'CLIENT_COMPLETE Peer=256 Role=Control'), $rateClients[1])
Test-RateRejected 'packet simulation missing' ($rateServer.Replace('LogNet: PktLoss', 'Unknown: PktLoss'))
Test-RateRejected 'packet simulation later disabled' ($rateServer + "`nLogNet: PktLoss set to 0")
Test-RateRejected 'client packet simulation missing' $rateServer @($rateClients[0].Replace('LogNet: PktLagMin', 'Unknown: PktLagMin'), $rateClients[1])
Test-RateRejected 'runtime failure' ($rateServer + "`nRPG_TARGETDATA_RATE FAILED")
foreach ($invalidMode in @(
    @{ ClientCount = 1 }, @{ ClientCount = 3 }, @{ Runtime = 'Game' },
    @{ VerifySkillInput = $true }, @{ VerifySkillTripods = $true },
    @{ VerifyMovementSecurity = $true }, @{ VerifyAuthorizedMovement = $true }, @{ VerifyAuthoredRoll = $true }
)) {
    $invalidMode['SkipBuild'] = $true
    $invalidMode['VerifyTargetDataRate'] = $true
    $blocked = $false
    try {
        & (Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1') @invalidMode
    } catch {
        if ($_.Exception.Message -notlike 'Run TargetData abuse separately*') { throw }
        $blocked = $true
    }
    if (-not $blocked) { throw 'Unsafe combined rate test mode reached process/build setup.' }
    ++$script:ratePassed
}
Write-Host "PASS: $script:ratePassed TargetData rate proof validator checks."
