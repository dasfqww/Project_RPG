[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
# Reuse the independently validated twelve wire cases and ownership-rejection fixture.
. (Join-Path $PSScriptRoot 'Test-TargetDataWireProof.ps1')
. (Join-Path $PSScriptRoot '..\Network\TargetDataDamageProof.ps1')
$packet = "LogNet: PktLoss set to 0`nLogNet: PktLagMin set to 0`nLogNet: PktLagMax set to 0"
$ids = @{ '256' = '4000'; '257' = '4001' }
$health = @{ '256' = 1000; '257' = 1000 }
$count = @{ '256' = 0; '257' = 0 }
$damageServer = @($packet, 'RPG_NETTEST SERVER_STATE Iris=1 Connected=2 Expected=2 Revision=2')
foreach ($peer in '256', '257') {
    $damageServer += "RPG_TARGETDATA_DAMAGE CONFIG Peer=$peer TargetNetId=$($ids[$peer]) Base=10 Attack=1 Defense=1 Crit=0 FinalCap=10 NativeHit=1"
}
foreach ($line in $serverLines) {
    if ($line -match 'WIRE CASE Peer=(\d+) Index=(\d+)') {
        $peer = $Matches[1]; $i = [int]$Matches[2]
        $foreign = if ($peer -eq '256') { '257' } else { '256' }
        $before = $health[$peer]
        if ($accepted[$i] -eq 1) {
            ++$count[$peer]; $health[$peer] -= 10
            $ignored = if ($i -eq 11) { 0 } else { 1 }
            $damageServer += "RPG_TARGETDATA_DAMAGE EFFECT Peer=$peer TargetNetId=$($ids[$peer]) ForeignNetId=$($ids[$foreign]) Count=$($count[$peer]) Before=$before.000000 After=$($health[$peer]).000000 ForeignBefore=$($health[$foreign]).000000 ForeignAfter=$($health[$foreign]).000000 IgnoredActors=$ignored"
        }
        $damageServer += "RPG_TARGETDATA_DAMAGE CASE Peer=$peer Index=$i TargetNetId=$($ids[$peer]) Before=$before.000000 After=$($health[$peer]).000000 Effects=$($accepted[$i])"
    }
    $damageServer += $line
}
foreach ($peer in '256', '257') {
    $damageServer += "RPG_TARGETDATA_DAMAGE SERVER_COMPLETE Peer=$peer TargetNetId=$($ids[$peer]) Health=950.000000 Effects=5 Quiet=3"
}
$damageServer = $damageServer -join "`n"
$damageClients = @(foreach ($peer in '256', '257') {
    $lines = @($packet, 'RPG_NETTEST CLIENT_READY ServerIris=1 ClientIris=1 Connected=2 Expected=2 Revision=2')
    foreach ($target in '256', '257') {
        $lines += "RPG_TARGETDATA_DAMAGE BASELINE Peer=$peer TargetPeer=$target TargetNetId=$($ids[$target]) Health=1000.000000"
    }
    foreach ($target in '256', '257') {
        for ($i = 0; $i -lt 5; ++$i) {
            $before = 1000 - 10 * $i; $after = $before - 10
            $lines += "RPG_TARGETDATA_DAMAGE HP_NOTIFY Peer=$peer TargetPeer=$target TargetNetId=$($ids[$target]) Old=$before.000000 New=$after.000000"
        }
    }
    $lines += $clients[([int]$peer - 256)]
    foreach ($target in '256', '257') {
        $lines += "RPG_TARGETDATA_DAMAGE RESULT Peer=$peer TargetPeer=$target TargetNetId=$($ids[$target]) Health=950.000000 Notify=5"
    }
    $lines += "RPG_TARGETDATA_DAMAGE CLIENT_COMPLETE Peer=$peer Targets=2"
    $lines -join "`n"
})
$proof = Get-TargetDataDamageProof -ServerText $damageServer -ClientTexts $damageClients
if (-not $proof.actualCombatEffectsVerified -or -not $proof.rejectedWireHPInvariantVerified -or
    -not $proof.bothTargetsHPReplicationOnBothClientsVerified -or $proof.authoredBPCombatVerified -or
    $proof.overlapQueryVerified -or $proof.finalCapRejectionOverWireVerified -or $proof.shippingRuntimeVerified) {
    throw 'Valid HP evidence or fixture scope was misread.'
}
$script:damageChecks = 1
# Iris may coalesce several legitimate HP changes into one attribute notification.
$coalesced = @(foreach ($log in $damageClients) {
    $lines = foreach ($line in ($log -split "`n")) {
        if ($line -match 'RPG_TARGETDATA_DAMAGE HP_NOTIFY') {
            if ($line -match 'Old=1000.000000') { $line.Replace('New=990.000000', 'New=950.000000') }
        } else { $line.Replace('Notify=5', 'Notify=1') }
    }
    $lines -join "`n"
})
Get-TargetDataDamageProof -ServerText $damageServer -ClientTexts $coalesced | Out-Null
++ $script:damageChecks
function Reject-Damage([string] $Name, [string] $Text = $damageServer, [string[]] $Logs = $damageClients) {
    $rejected = $false
    try { Get-TargetDataDamageProof -ServerText $Text -ClientTexts $Logs | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid HP proof accepted: $Name" }
    ++$script:damageChecks
}
Reject-Damage 'missing case' ($damageServer.Replace('DAMAGE CASE Peer=256 Index=1', 'DAMAGE MISSING Peer=256 Index=1'))
Reject-Damage 'duplicate case' ($damageServer + "`nRPG_TARGETDATA_DAMAGE CASE Peer=256 Index=1 TargetNetId=4000 Before=990.000000 After=990.000000 Effects=0")
Reject-Damage 'rejected request damaged HP' ($damageServer.Replace('Index=1 TargetNetId=4000 Before=990.000000 After=990.000000', 'Index=1 TargetNetId=4000 Before=990.000000 After=980.000000'))
Reject-Damage 'no positive HP change' ($damageServer.Replace('Index=0 TargetNetId=4000 Before=1000.000000 After=990.000000', 'Index=0 TargetNetId=4000 Before=1000.000000 After=1000.000000'))
Reject-Damage 'wrong target identity' ($damageServer.Replace('TargetNetId=4000', 'TargetNetId=4001'))
Reject-Damage 'zero target identity' ($damageServer.Replace('TargetNetId=4000', 'TargetNetId=0'))
Reject-Damage 'borrow wire ASC identity' ($damageServer.Replace('TargetNetId=4000', 'TargetNetId=2560'))
Reject-Damage 'extra effect' ($damageServer + "`nRPG_TARGETDATA_DAMAGE EFFECT Peer=256 TargetNetId=4000 ForeignNetId=4001 Count=5 Before=950.000000 After=940.000000 ForeignBefore=950.000000 ForeignAfter=950.000000 IgnoredActors=0")
Reject-Damage 'foreign HP changed' ($damageServer.Replace('ForeignAfter=1000.000000', 'ForeignAfter=990.000000'))
Reject-Damage 'forged target not tested' ($damageServer.Replace('IgnoredActors=1', 'IgnoredActors=0'))
Reject-Damage 'effect counter changed' ($damageServer.Replace('Count=2 Before=', 'Count=1 Before='))
Reject-Damage 'incorrect quiet duration' ($damageServer.Replace('Quiet=3', 'Quiet=0'))
Reject-Damage 'incorrect final HP' ($damageServer.Replace('Health=950.000000 Effects=5', 'Health=940.000000 Effects=5'))
Reject-Damage 'final cap unavailable' ($damageServer.Replace('FinalCap=10', 'FinalCap=0'))
Reject-Damage 'wrong packet setting' ($damageServer.Replace('PktLoss set to 0', 'PktLoss set to 10'))
Reject-Damage 'runtime failure' ($damageServer + "`nRPG_TARGETDATA_DAMAGE FAILED")
Reject-Damage 'server Iris not ready' ($damageServer.Replace('Connected=2 Expected=2', 'Connected=1 Expected=2'))
Reject-Damage 'client HP never replicated' $damageServer @($damageClients[0].Replace('HP_NOTIFY', 'MISSING'), $damageClients[1])
Reject-Damage 'client foreign target not replicated' $damageServer @($damageClients[0].Replace('TargetPeer=257', 'TargetPeer=256'), $damageClients[1])
Reject-Damage 'client final HP differs' $damageServer @($damageClients[0].Replace('Health=950.000000', 'Health=940.000000'), $damageClients[1])
Reject-Damage 'client notify count differs' $damageServer @($damageClients[0].Replace('Notify=5', 'Notify=4'), $damageClients[1])
Reject-Damage 'client notify chain broken' $damageServer @($damageClients[0].Replace('Old=980.000000', 'Old=990.000000'), $damageClients[1])
Reject-Damage 'client notified increase' $damageServer @($damageClients[0].Replace('New=990.000000', 'New=1010.000000'), $damageClients[1])
Reject-Damage 'client different owner' $damageServer @($damageClients[0].Replace('DAMAGE CLIENT_COMPLETE Peer=256', 'DAMAGE CLIENT_COMPLETE Peer=257'), $damageClients[1])
Reject-Damage 'duplicate client completion' $damageServer @($damageClients[0] + "`nRPG_TARGETDATA_DAMAGE CLIENT_COMPLETE Peer=256 Targets=2", $damageClients[1])
Reject-Damage 'client Iris not ready' $damageServer @($damageClients[0].Replace('ClientIris=1', 'ClientIris=0'), $damageClients[1])
Reject-Damage 'duplicate configuration' ($damageServer + "`nRPG_TARGETDATA_DAMAGE CONFIG Peer=256 TargetNetId=4000 Base=10 Attack=1 Defense=1 Crit=0 FinalCap=10 NativeHit=1")
Reject-Damage 'client baseline never observed' $damageServer @($damageClients[0].Replace('DAMAGE BASELINE', 'DAMAGE MISSING'), $damageClients[1])
Reject-Damage 'client packet settings differ' $damageServer @($damageClients[0].Replace('PktLagMin set to 0', 'PktLagMin set to 100'), $damageClients[1])
Reject-Damage 'client wrong replicated identity' $damageServer @($damageClients[0].Replace('TargetNetId=4000', 'TargetNetId=9999'), $damageClients[1])
Reject-Damage 'missing actual GE' ($damageServer.Replace('DAMAGE EFFECT Peer=256 TargetNetId=4000 ForeignNetId=4001 Count=2', 'DAMAGE MISSING Peer=256 TargetNetId=4000 ForeignNetId=4001 Count=2'))
$earlyEnd = [regex]::Match($damageServer, '(?m)^RPG_TARGETDATA_DAMAGE SERVER_COMPLETE Peer=256[^\r\n]*').Value
Reject-Damage 'server completed before last peer case' ($earlyEnd + "`n" + $damageServer.Replace($earlyEnd, ''))
$earlyResult = [regex]::Match($damageClients[0], '(?m)^RPG_TARGETDATA_DAMAGE RESULT Peer=256 TargetPeer=256[^\r\n]*').Value
Reject-Damage 'client final HP before baseline/notification/wire completion' $damageServer @($earlyResult + "`n" + $damageClients[0].Replace($earlyResult, ''), $damageClients[1])

# CLI must fail before build/process/output creation for incompatible probes.
foreach ($caseOptions in @(
    @{ ClientCount = 1 }, @{ Runtime = 'Game' }, @{ VerifyTargetDataRate = $true },
    @{ VerifySkillDamage = $true }, @{ VerifySkillInput = $true }, @{ VerifyMovementSecurity = $true }
)) {
    $rejected = $false
    try { & (Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1') -VerifyTargetDataDamage -SkipBuild @caseOptions | Out-Null }
    catch { $rejected = $true }
    if (-not $rejected) { throw 'Incompatible damage CLI combination accepted.' }
    ++$script:damageChecks
}
Write-Host "PASS: $script:damageChecks TargetData damage proof/CLI validator checks."
