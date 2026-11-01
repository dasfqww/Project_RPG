function Get-TargetDataWireProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ServerText,
        [Parameter(Mandatory)] [string[]] $ClientTexts
    )
    if ($ClientTexts.Count -ne 2) { throw 'TargetData ownership proof requires exactly two independent clients.' }
    foreach ($logText in (@($ServerText) + @($ClientTexts))) {
        if ($logText -match 'RPG_TARGETDATA_WIRE FAILED|Fatal error:|Assertion failed:|Ensure condition failed:') {
            throw 'TargetData runtime failure/assertion/ensure.'
        }
    }
    $identities = @([regex]::Matches($ServerText, '(?m)RPG_TARGETDATA_WIRE IDENTIFIED Peer=(\d+) Root=(TargetDataWirePeer_\d+) NetId=(\d+)\r?$'))
    if ($identities.Count -ne 2 -or
        [regex]::Matches($ServerText, 'RPG_TARGETDATA_WIRE IDENTIFIED').Count -ne 2) { throw 'Missing/malformed server identities.' }
    $roots = @{}
    $netIds = @{}
    foreach ($row in $identities) {
        $peer = $row.Groups[1].Value
        $root = $row.Groups[2].Value
        $netId = $row.Groups[3].Value
        if ($roots.ContainsKey($peer) -or $roots.ContainsValue($root) -or $root -ne "TargetDataWirePeer_$peer" -or
            [uint64]$netId -eq 0 -or $netIds.ContainsValue($netId)) {
            throw 'Duplicate/mismatched server identity.'
        }
        $roots[$peer] = $root
        $netIds[$peer] = $netId
    }
    $names = @('Baseline', 'Duplicate', 'Reversed', 'WrongActivation', 'ForeignSpec', 'OversizedActors',
        'DuplicateCancel', 'Recovery', 'EndedReplay', 'Reactivate', 'OldActivationReplay', 'ForeignOwner')
    $rx = @(1, 4, 2, 1, 1, 1, 0, 1, 1, 0, 2, 1)
    $cancelRx = @(0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0)
    $accepted = @(1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1)
    $cancelled = @(0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0)
    if ([regex]::Matches($ServerText, 'RPG_TARGETDATA_WIRE CASE').Count -ne 24 -or
        [regex]::Matches($ServerText, 'RPG_TARGETDATA_WIRE SERVER_COMPLETE').Count -ne 2 -or
        [regex]::Matches($ServerText, 'RPG_TARGETDATA_WIRE FOREIGN_CHALLENGE').Count -ne 2) {
        throw 'Missing/duplicate case or completion evidence.'
    }
    foreach ($peer in $roots.Keys) {
        for ($index = 0; $index -lt 12; ++$index) {
            $expected = "RPG_TARGETDATA_WIRE CASE Peer=$peer Index=$index Name=$($names[$index]) RX=$($rx[$index]) CancelRX=$($cancelRx[$index]) Accepted=$($accepted[$index]) Cancelled=$($cancelled[$index]) CacheSafe=1"
            if ([regex]::Matches($ServerText, '(?m)' + [regex]::Escape($expected) + '\r?$').Count -ne 1) {
                throw "Invalid/missing receiver, callback or cache evidence: peer=$peer case=$index."
            }
        }
        $complete = "RPG_TARGETDATA_WIRE SERVER_COMPLETE Peer=$peer Cases=12 RX=15 CancelRX=2 Accepted=5 Cancelled=1"
        if ([regex]::Matches($ServerText, '(?m)' + [regex]::Escape($complete) + '\r?$').Count -ne 1) {
            throw 'Invalid server totals.'
        }
    }
    $seenClients = @{}
    $ownership = @()
    foreach ($clientText in $ClientTexts) {
        $complete = @([regex]::Matches($clientText, '(?m)RPG_TARGETDATA_WIRE CLIENT_COMPLETE Peer=(\d+) Cases=12\r?$'))
        $queued = @([regex]::Matches($clientText, '(?m)RPG_TARGETDATA_WIRE FOREIGN_QUEUED Peer=(\d+) ForeignPeer=(\d+) RootLocal=([A-Za-z0-9_]+) NetId=(\d+) Handle=(\d+) Original=(\d+)\r?$'))
        if ($complete.Count -ne 1 -or $queued.Count -ne 1 -or
            [regex]::Matches($clientText, 'RPG_TARGETDATA_WIRE CLIENT_COMPLETE').Count -ne 1 -or
            [regex]::Matches($clientText, 'RPG_TARGETDATA_WIRE FOREIGN_QUEUED').Count -ne 1) {
            throw 'Missing/duplicate client completion or foreign send evidence.'
        }
        $peer = $complete[0].Groups[1].Value
        $foreignPeer = $queued[0].Groups[2].Value
        $localRoot = $queued[0].Groups[3].Value
        $netId = $queued[0].Groups[4].Value
        $handle = $queued[0].Groups[5].Value
        $original = $queued[0].Groups[6].Value
        if (-not $roots.ContainsKey($peer) -or $seenClients.ContainsKey($peer) -or
            $queued[0].Groups[1].Value -ne $peer -or $foreignPeer -eq $peer -or
            -not $roots.ContainsKey($foreignPeer) -or $netIds[$foreignPeer] -ne $netId) {
            throw 'Client/foreign ownership identities do not match independent server peers.'
        }
        if ([long]$handle -lt 1 -or [long]$original -lt 1 -or [long]$original -gt 32767) {
            throw 'Foreign target token was invalid or zeroed by owner-only prediction serialization.'
        }
        $root = $roots[$foreignPeer]
        $challenge = "RPG_TARGETDATA_WIRE FOREIGN_CHALLENGE Peer=$peer ForeignPeer=$foreignPeer Root=$root NetId=$netId Handle=$handle Original=$original Active=1"
        if ([regex]::Matches($ServerText, '(?m)' + [regex]::Escape($challenge) + '\r?$').Count -ne 1) {
            throw 'Foreign forgery does not match the server active-activation challenge.'
        }
        $seenClients[$peer] = $true
        # This is the actual Iris receiving-side rejection, not a local "no owning connection" warning.
        $rejection = 'Rejected reliable RPC function ServerSetReplicatedTargetData due to target not being owned by the connection\.[^\r\n]*NetRefHandle \(Id=' + [regex]::Escape($netId) + '\)[^\r\n]*' + [regex]::Escape($root) + '\.WireASC\b'
        if ([regex]::Matches($ServerText, $rejection).Count -ne 1) {
            throw "Missing/duplicate server-side Iris ownership rejection for $root."
        }
        $ownership += [ordered]@{ sendingPeer = [int]$peer; rejectedTargetPeer = [int]$foreignPeer;
            targetRoot = $root; grantedHandle = [long]$handle; activePredictionKey = [int]$original }
        $ownership[-1]['clientTargetLocalRoot'] = $localRoot
        $ownership[-1]['netRefId'] = [uint64]$netId
    }
    return [ordered]@{
        schemaVersion = 3
        clientCount = 2
        casesPerPeer = 12
        nativeReceiverVerified = $true
        callbackAndCacheVerified = $true
        irisReceivingOwnershipVerified = $true
        lowerLevelOwnedPositiveControlVerified = $true
        foreignActiveTokenForgeryVerified = $true
        networkIdentityKind = 'TargetASC'
        ownershipRejections = $ownership
        productionAdmissionOnIsolatedWireFixture = $true
        actualCombatEffectsVerified = $false
        wireRateFloodVerified = $false
        shippingRuntimeVerified = $false
    }
}
