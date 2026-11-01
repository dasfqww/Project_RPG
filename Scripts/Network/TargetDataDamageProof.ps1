# Evidence from native wire fixtures; this is not authored BP/montage combat coverage.
. (Join-Path $PSScriptRoot 'TargetDataWireProof.ps1')
function Get-TargetDataDamageProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ServerText,
        [Parameter(Mandatory)] [string[]] $ClientTexts,
        [ValidateRange(0, 2000)] [int] $PacketLagMinMs = 0,
        [ValidateRange(0, 2000)] [int] $PacketLagMaxMs = 0,
        [ValidateRange(0, 50)] [int] $PacketLossPercent = 0
    )
    if ($PacketLagMaxMs -lt $PacketLagMinMs) { throw 'Invalid packet lag range.' }
    $wire = Get-TargetDataWireProof -ServerText $ServerText -ClientTexts $ClientTexts
    function Rows([string] $Text, [string] $Marker, [string] $Suffix, [int] $Count) {
        $rows = [regex]::Matches($Text, '(?m)' + [regex]::Escape($Marker) + $Suffix + '\r?$')
        if ($rows.Count -ne $Count -or [regex]::Matches($Text, [regex]::Escape($Marker)).Count -ne $Count) {
            throw "Missing/duplicate/malformed evidence: $Marker."
        }
        return $rows
    }
    $number = '(\d+\.\d{6})'
    $culture = [Globalization.CultureInfo]::InvariantCulture
    function Number([string] $Value) { return [double]::Parse($Value, $culture) }
    foreach ($text in (@($ServerText) + @($ClientTexts))) {
        if ($text -match 'RPG_TARGETDATA_DAMAGE FAILED|Fatal error:|Assertion failed:|Ensure condition failed:') {
            throw 'Damage fixture runtime failure.'
        }
        foreach ($setting in @(
            @{ name = 'PktLoss'; value = $PacketLossPercent },
            @{ name = 'PktLagMin'; value = $PacketLagMinMs },
            @{ name = 'PktLagMax'; value = $PacketLagMaxMs }
        )) {
            $values = [regex]::Matches($text, 'LogNet: ' + $setting.name + ' set to (\d+)\b')
            if ($values.Count -eq 0 -or [int]$values[$values.Count - 1].Groups[1].Value -ne $setting.value) {
                throw "Requested packet setting not active: $($setting.name)."
            }
        }
    }
    if ($ServerText -notmatch 'RPG_NETTEST SERVER_STATE Iris=1 Connected=2 Expected=2 Revision=\d+') {
        throw 'Missing two-client server Iris readiness.'
    }
    $configs = @(Rows $ServerText 'RPG_TARGETDATA_DAMAGE CONFIG' ' Peer=(\d+) TargetNetId=(\d+) Base=10 Attack=1 Defense=1 Crit=0 FinalCap=10 NativeHit=1' 2)
    $ids = @{}
    $wireIds = @($wire.ownershipRejections | ForEach-Object { [string]$_.netRefId })
    foreach ($config in $configs) {
        $peer = $config.Groups[1].Value
        $id = $config.Groups[2].Value
        if ($ids.ContainsKey($peer) -or $ids.ContainsValue($id) -or [uint64]$id -eq 0 -or
            $wireIds -contains $id -or @($wire.ownershipRejections | Where-Object { $_.sendingPeer -eq [int]$peer }).Count -ne 1) {
            throw 'Invalid native target/peer network identity.'
        }
        $ids[$peer] = $id
    }
    $cases = @(Rows $ServerText 'RPG_TARGETDATA_DAMAGE CASE' (" Peer=(\d+) Index=(\d+) TargetNetId=(\d+) Before=$number After=$number Effects=([01])") 24)
    $effects = @(Rows $ServerText 'RPG_TARGETDATA_DAMAGE EFFECT' (" Peer=(\d+) TargetNetId=(\d+) ForeignNetId=(\d+) Count=([1-5]) Before=$number After=$number ForeignBefore=$number ForeignAfter=$number IgnoredActors=([01])") 10)
    $serverEnds = @(Rows $ServerText 'RPG_TARGETDATA_DAMAGE SERVER_COMPLETE' (" Peer=(\d+) TargetNetId=(\d+) Health=$number Effects=5 Quiet=3") 2)
    $deltas = @(1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1)
    foreach ($peer in $ids.Keys) {
        $config = @($configs | Where-Object { $_.Groups[1].Value -eq $peer })[0]
        $health = 1000
        $previous = $config.Index
        $previousWire = $config.Index
        $count = 0
        for ($i = 0; $i -lt 12; ++$i) {
            $row = @($cases | Where-Object { $_.Groups[1].Value -eq $peer -and [int]$_.Groups[2].Value -eq $i })
            if ($row.Count -ne 1) { throw 'Missing/duplicate HP case.' }
            $row = $row[0]
            $wireCase = [regex]::Match($ServerText, 'RPG_TARGETDATA_WIRE CASE Peer=' + $peer + ' Index=' + $i + ' Name=')
            if (-not $wireCase.Success -or $row.Index -ge $wireCase.Index -or $row.Index -le $previousWire) {
                throw 'HP checkpoint does not belong to the actual wire receiver case.'
            }
            $after = $health - 10 * $deltas[$i]
            if ($row.Groups[3].Value -ne $ids[$peer] -or (Number $row.Groups[4].Value) -ne $health -or
                (Number $row.Groups[5].Value) -ne $after -or [int]$row.Groups[6].Value -ne $deltas[$i] -or $row.Index -le $previous) {
                throw "HP changed on rejected request or positive damage missing: peer=$peer case=$i."
            }
            $between = @($effects | Where-Object { $_.Groups[1].Value -eq $peer -and $_.Index -gt $previous -and $_.Index -lt $row.Index })
            if ($between.Count -ne $deltas[$i]) { throw 'Actual effect application ordering/count mismatch.' }
            if ($between.Count -eq 1) {
                ++$count
                $ignored = if ($i -eq 11) { 0 } else { 1 }
                if ([int]$between[0].Groups[4].Value -ne $count -or [int]$between[0].Groups[9].Value -ne $ignored) {
                    throw 'Effect counter or submitted foreign target was not verified.'
                }
            }
            $health = $after
            $previous = $row.Index
            $previousWire = $wireCase.Index
        }
        $end = @($serverEnds | Where-Object { $_.Groups[1].Value -eq $peer })
        if ($end.Count -ne 1 -or $end[0].Groups[2].Value -ne $ids[$peer] -or
            (Number $end[0].Groups[3].Value) -ne 950 -or $end[0].Index -le ($cases | Measure-Object Index -Maximum).Maximum) {
            throw 'Missing final server HP or cross-peer quiet phase.'
        }
    }
    # Reconstruct both targets' actual effect order, including the other target's unchanged HP.
    $current = @{}
    foreach ($peer in $ids.Keys) { $current[$peer] = 1000 }
    foreach ($effect in ($effects | Sort-Object Index)) {
        $peer = $effect.Groups[1].Value
        if (-not $ids.ContainsKey($peer)) { throw 'Unknown effect peer.' }
        $foreign = @($ids.Keys | Where-Object { $_ -ne $peer })[0]
        if ($effect.Groups[2].Value -ne $ids[$peer] -or $effect.Groups[3].Value -ne $ids[$foreign] -or
            (Number $effect.Groups[5].Value) -ne $current[$peer] -or
            (Number $effect.Groups[6].Value) -ne ($current[$peer] - 10) -or
            (Number $effect.Groups[7].Value) -ne $current[$foreign] -or
            (Number $effect.Groups[8].Value) -ne $current[$foreign]) { throw 'Damage redirected to foreign target or inconsistent HP history.' }
        $current[$peer] -= 10
    }
    foreach ($client in $ClientTexts) {
        if ($client -notmatch 'RPG_NETTEST CLIENT_READY ServerIris=1 ClientIris=1 Connected=2 Expected=2 Revision=\d+') {
            throw 'Missing independent client Iris readiness.'
        }
        $end = @(Rows $client 'RPG_TARGETDATA_DAMAGE CLIENT_COMPLETE' ' Peer=(\d+) Targets=2' 1)[0]
        $peer = $end.Groups[1].Value
        $wireEnd = [regex]::Match($client, 'RPG_TARGETDATA_WIRE CLIENT_COMPLETE Peer=(\d+) Cases=12')
        if (-not $ids.ContainsKey($peer) -or $wireEnd.Groups[1].Value -ne $peer) { throw 'Client HP evidence belongs to a different owner.' }
        $baselines = @(Rows $client 'RPG_TARGETDATA_DAMAGE BASELINE' ' Peer=(\d+) TargetPeer=(\d+) TargetNetId=(\d+) Health=1000.000000' 2)
        $results = @(Rows $client 'RPG_TARGETDATA_DAMAGE RESULT' (" Peer=(\d+) TargetPeer=(\d+) TargetNetId=(\d+) Health=$number Notify=([1-5])") 2)
        $notifyCount = [regex]::Matches($client, 'RPG_TARGETDATA_DAMAGE HP_NOTIFY').Count
        if ($notifyCount -lt 2 -or $notifyCount -gt 10) { throw 'Missing/excess HP notifications.' }
        $notifications = @(Rows $client 'RPG_TARGETDATA_DAMAGE HP_NOTIFY' (" Peer=(\d+) TargetPeer=(\d+) TargetNetId=(\d+) Old=$number New=$number") $notifyCount)
        foreach ($targetPeer in $ids.Keys) {
            $baseline = @($baselines | Where-Object { $_.Groups[2].Value -eq $targetPeer })
            $result = @($results | Where-Object { $_.Groups[2].Value -eq $targetPeer })
            $changes = @($notifications | Where-Object { $_.Groups[2].Value -eq $targetPeer } | Sort-Object Index)
            if ($baseline.Count -ne 1 -or $result.Count -ne 1 -or $changes.Count -lt 1 -or
                $baseline[0].Groups[1].Value -ne $peer -or $baseline[0].Groups[3].Value -ne $ids[$targetPeer] -or
                $result[0].Groups[1].Value -ne $peer -or $result[0].Groups[3].Value -ne $ids[$targetPeer] -or
                (Number $result[0].Groups[4].Value) -ne 950 -or [int]$result[0].Groups[5].Value -ne $changes.Count -or
                $result[0].Index -ge $end.Index -or $result[0].Index -le $wireEnd.Index -or
                $baseline[0].Index -ge [regex]::Match($client, 'RPG_TARGETDATA_WIRE FOREIGN_QUEUED').Index) {
                throw 'Client baseline/final HP/notification identity mismatch.'
            }
            $health = 1000
            $previous = ($baselines | Measure-Object Index -Maximum).Maximum
            foreach ($change in $changes) {
                $new = Number $change.Groups[5].Value
                if ($change.Groups[1].Value -ne $peer -or $change.Groups[3].Value -ne $ids[$targetPeer] -or
                    (Number $change.Groups[4].Value) -ne $health -or $new -ge $health -or $new -lt 950 -or
                    (($health - $new) % 10) -ne 0 -or $change.Index -le $previous -or $change.Index -ge $result[0].Index) {
                    throw 'Client HP notification was not an ordered real damage delta.'
                }
                $previous = $change.Index
                $health = $new
            }
            if ($health -ne 950) { throw 'Client HP notifications did not reach authoritative final HP.' }
        }
    }
    return [ordered]@{
        schemaVersion = 1; clientCount = 2; casesPerPeer = 12; approvedDamagePerPeer = 50
        actualCombatEffectsVerified = $true; rejectedWireHPInvariantVerified = $true
        foreignSubmittedTargetIgnoredVerified = $true; irisReceivingOwnershipVerified = $true
        bothTargetsHPReplicationOnBothClientsVerified = $true; serverQuietSeconds = 3
        nativeServerAuthoredHitFixture = $true; authoredBPCombatVerified = $false
        overlapQueryVerified = $false; productionEnemyLifecycleVerified = $false
        finalCapRejectionOverWireVerified = $false; wireRateFloodVerified = $false; shippingRuntimeVerified = $false
        targetNetRefIds = $ids; ownershipRejections = $wire.ownershipRejections
        packetLagMinMs = $PacketLagMinMs; packetLagMaxMs = $PacketLagMaxMs; packetLossPercent = $PacketLossPercent
    }
}
