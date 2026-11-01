. (Join-Path $PSScriptRoot 'SkillTargetDataAbuseProof.ps1')

function Get-SkillMultiPeerAbuseProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ServerText,
        [Parameter(Mandatory)] [string[]] $ClientTexts,
        [ValidateRange(0, 2000)] [int] $PacketLagMinMs = 0,
        [ValidateRange(0, 2000)] [int] $PacketLagMaxMs = 0,
        [ValidateRange(0, 50)] [int] $PacketLossPercent = 0,
        [switch] $RequireEndOrigin,
        [switch] $RequireEndRaceFixture
    )
    if ($ClientTexts.Count -ne 2) { throw 'Exactly two actual client logs are required.' }
    foreach ($raw in @($ServerText) + $ClientTexts) {
        if ($raw -match 'RPG_SKILL_\S* FAILED|Fatal error:|Assertion failed:|Ensure condition failed:') { throw 'Multi-peer runtime failure.' }
    }
    if ($ServerText -match 'RPG_SKILL_END_RACE ' -or
        (-not $RequireEndRaceFixture -and @($ClientTexts | Where-Object { $_ -match 'RPG_SKILL_END_RACE ' }).Count -gt 0)) {
        throw 'Delayed client montage fixture must be explicit and must not run on the server.'
    }
    function MultiRows([string] $Text, [string] $Marker, [string] $Suffix, [int] $Count) {
        $rows = @([regex]::Matches($Text, '(?m)' + [regex]::Escape($Marker) + $Suffix + '\r?$'))
        if ($rows.Count -ne $Count -or [regex]::Matches($Text, [regex]::Escape($Marker)).Count -ne $Count) {
            throw "Missing/duplicate/malformed multi-peer evidence: $Marker."
        }
        return $rows
    }
    $ready = @(MultiRows $ServerText 'RPG_SKILL_MULTI READY' ' Peer=([1-9]\d*) ASCNetId=([1-9]\d*) Targets=8' 2)
    $inputReady = @(MultiRows $ServerText 'RPG_SKILL_MULTI INPUT_READY' ' Peer=([1-9]\d*) ASCNetId=([1-9]\d*) Targets=8' 2)
    $peers = @($ready | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object -Unique)
    if ($peers.Count -ne 2) { throw 'Both connections reused one peer identity.' }
    function Scoped([string] $Raw, [int] $Peer) {
        $lines = foreach ($line in ($Raw -split '\r?\n')) {
            $scoped = [regex]::Match($line, 'RPG_SKILL_PEER Peer=(-?\d+) (RPG_SKILL_.*)$')
            if ($scoped.Success) {
                $id = [int]$scoped.Groups[1].Value
                $body = $scoped.Groups[2].Value
                $important = $body -match '^RPG_SKILL_(DAMAGE |ABUSE |ABUSE_CONSUMER |END_TRACE |END_ORIGIN |END_RACE |NETTEST (CLIENT_INPUT_|CLIENT_ACTIVE|CLIENT_RELEASED|CLIENT_COMPLETE|SERVER_ACTIVE|SERVER_COMPLETE|RELEASE_SECTION))'
                if ($important -and $id -notin $peers) { throw 'Combat evidence has no known peer scope.' }
                # The explicit race fixture is validated below, never by the ordinary damage parser.
                if ($id -eq $Peer -and $body -notmatch '^RPG_SKILL_END_RACE ') { $line.Substring(0, $scoped.Index) + $body }
            } elseif ($line -match 'RPG_SKILL_(DAMAGE |ABUSE |ABUSE_CONSUMER |END_TRACE |END_ORIGIN |END_RACE |NETTEST (CLIENT_INPUT_|CLIENT_ACTIVE|CLIENT_RELEASED|CLIENT_COMPLETE|SERVER_ACTIVE|SERVER_COMPLETE|RELEASE_SECTION))') {
                throw 'Unscoped combat evidence cannot prove multi-peer isolation.'
            } else { $line }
        }
        return $lines -join "`n"
    }
    $clientByPeer = @{}
    $sourceProofs = @{}
    $clientReadyRows = @{}
    foreach ($rawClient in $ClientTexts) {
        $row = @(MultiRows $rawClient 'RPG_SKILL_MULTI FIXTURE_READY' ' Peer=([1-9]\d*) ASCNetId=([1-9]\d*) Targets=8' 1)[0]
        $peer = [int]$row.Groups[1].Value
        if ($peer -notin $peers -or $clientByPeer.ContainsKey($peer)) { throw 'Duplicate/unknown client connection.' }
        $clientByPeer[$peer] = $rawClient
        $clientReadyRows[$peer] = $row
        $sourceProofs[$peer] = Get-SkillTargetDataAbuseProof -ServerText (Scoped $ServerText $peer) -ClientText (Scoped $rawClient $peer) `
            -ExpectedClientCount 2 -PacketLagMinMs $PacketLagMinMs -PacketLagMaxMs $PacketLagMaxMs -PacketLossPercent $PacketLossPercent -RequireEndOrigin:$RequireEndOrigin
        if ($RequireEndRaceFixture) {
            $delay = @(MultiRows $rawClient 'RPG_SKILL_END_RACE CLIENT_MONTAGE_DELAY' ' Handle=([1-9]\d*) Original=([1-9]\d*) Before=1\.000000 After=0\.500000 Fixture=1' 1)[0]
            $p = $sourceProofs[$peer]
            if (-not $p.abilityEndOriginVerified -or $p.clientAbilityEndOrigin -ne 'AuthorityRPC' -or
                [uint64]$delay.Groups[1].Value -ne $p.abilityHandle -or [int]$delay.Groups[2].Value -ne $p.activationPredictionKey -or
                $delay.Index -le [regex]::Match($rawClient,"RPG_SKILL_PEER Peer=$peer RPG_SKILL_NETTEST RELEASE_SECTION").Index -or
                $delay.Index -ge [regex]::Match($rawClient,"RPG_SKILL_PEER Peer=$peer RPG_SKILL_END_ORIGIN").Index -or
                [regex]::Matches($rawClient,"RPG_SKILL_PEER Peer=$peer RPG_SKILL_END_RACE ").Count -ne 1 -or
                [regex]::Matches($rawClient,'RPG_SKILL_END_RACE ').Count -ne 1) { throw 'Delayed local montage did not finish via the real matching authority RPC.' }
        }
    }
    $allIds = @()
    foreach ($peer in $peers) {
        $p = $sourceProofs[$peer]
        $ids = @($p.actualPlayerASCNetRefId) + @($p.targets | ForEach-Object { $_.netRefId })
        foreach ($id in $ids) {
            if ($id -eq 0 -or $allIds -contains $id) { throw 'Player ASC/target identities are shared between connections.' }
            $allIds += $id
        }
        foreach ($rows in @($ready, $inputReady)) {
            $own = @($rows | Where-Object { [int]$_.Groups[1].Value -eq $peer })
            if ($own.Count -ne 1 -or [uint64]$own[0].Groups[2].Value -ne $p.actualPlayerASCNetRefId) { throw 'Readiness does not belong to the actual player ASC.' }
        }
        if ([uint64]$clientReadyRows[$peer].Groups[2].Value -ne $p.actualPlayerASCNetRefId) { throw 'Client readiness ASC mismatch.' }
    }
    $primed = @([regex]::Matches($ServerText, 'RPG_SKILL_PEER Peer=([1-9]\d*) RPG_SKILL_ABUSE PRIMED Role=Server[^\r\n]*'))
    $initial = @([regex]::Matches($ServerText, 'RPG_SKILL_PEER Peer=([1-9]\d*) RPG_SKILL_ABUSE INITIAL Role=Server[^\r\n]*'))
    $receipts = @([regex]::Matches($ServerText, 'RPG_SKILL_PEER Peer=([1-9]\d*) RPG_SKILL_ABUSE RECEIVED Index=([0-6])[^\r\n]*'))
    $verified = @([regex]::Matches($ServerText, 'RPG_SKILL_PEER Peer=([1-9]\d*) RPG_SKILL_ABUSE VERIFIED[^\r\n]*'))
    $recovery = @([regex]::Matches($ServerText, 'RPG_SKILL_PEER Peer=([1-9]\d*) RPG_SKILL_ABUSE RECOVERY[^\r\n]*'))
    if ($primed.Count -ne 2 -or $initial.Count -ne 2 -or $receipts.Count -ne 14 -or $verified.Count -ne 2 -or $recovery.Count -ne 2 -or
        ($ready | Measure-Object Index -Maximum).Maximum -ge ($inputReady | Measure-Object Index -Minimum).Minimum -or
        ($inputReady | Measure-Object Index -Maximum).Maximum -ge ($initial | Measure-Object Index -Minimum).Minimum -or
        ($primed | Measure-Object Index -Maximum).Maximum -ge ($receipts | Measure-Object Index -Minimum).Minimum -or
        ($verified | Measure-Object Index -Maximum).Maximum -ge ($recovery | Measure-Object Index -Minimum).Minimum) {
        throw 'Both real activations were not primed and challenged before either normal release.'
    }
    $active = @(MultiRows $ServerText 'RPG_SKILL_MULTI PEER_ACTIVE' ' Peer=([1-9]\d*) OtherPeer=([1-9]\d*) Index=([0-6]) ASCNetId=([1-9]\d*) Handle=([1-9]\d*) Original=([1-9]\d*) Active=1 Events=0' 14)
    $crossHP = @(MultiRows $ServerText 'RPG_SKILL_MULTI CROSS_HP' ' Peer=([1-9]\d*) OtherPeer=([1-9]\d*) Index=([0-6]) Target=([0-3]) NetId=([1-9]\d*) Health=1000000.000000 Effects=0' 56)
    $forged = @(MultiRows $ServerText 'RPG_SKILL_MULTI FORGED_TARGET' ' Role=Server Peer=([1-9]\d*) OtherPeer=([1-9]\d*) NetId=([1-9]\d*)' 2)
    if ([regex]::Matches($ServerText, 'RPG_SKILL_MULTI ').Count -ne 76) { throw 'Unexpected server multi-peer evidence.' }
    $resultPeers = @()
    foreach ($peer in $peers) {
        $other = @($peers | Where-Object { $_ -ne $peer })[0]
        $p = $sourceProofs[$peer]; $op = $sourceProofs[$other]; $raw = $clientByPeer[$peer]
        $previousReceipt = ($primed | Measure-Object Index -Maximum).Maximum
        for ($case=0; $case -lt 7; ++$case) {
            $receipt = @($receipts | Where-Object { [int]$_.Groups[1].Value -eq $peer -and [int]$_.Groups[2].Value -eq $case })[0]
            $ar = @($active | Where-Object { [int]$_.Groups[1].Value -eq $peer -and [int]$_.Groups[3].Value -eq $case })
            if ($ar.Count -ne 1 -or [int]$ar[0].Groups[2].Value -ne $other -or [uint64]$ar[0].Groups[4].Value -ne $op.actualPlayerASCNetRefId -or
                [uint64]$ar[0].Groups[5].Value -ne $op.abilityHandle -or [int]$ar[0].Groups[6].Value -ne $op.activationPredictionKey -or
                $ar[0].Index -ge $receipt.Index -or $ar[0].Index -le $previousReceipt) { throw 'Other actual activation was not active during a malicious request.' }
            for ($target=0; $target -lt 4; ++$target) {
                $hp = @($crossHP | Where-Object { [int]$_.Groups[1].Value -eq $peer -and [int]$_.Groups[3].Value -eq $case -and [int]$_.Groups[4].Value -eq $target })
                if ($hp.Count -ne 1 -or [int]$hp[0].Groups[2].Value -ne $other -or [uint64]$hp[0].Groups[5].Value -ne $op.targets[$target].netRefId -or
                    $hp[0].Index -le $ar[0].Index -or $hp[0].Index -ge $receipt.Index) { throw 'Missing/misbound other-player HP invariant.' }
            }
            $previousReceipt = $receipt.Index
        }
        $sf = @($forged | Where-Object { [int]$_.Groups[1].Value -eq $peer })
        $cf = @(MultiRows $raw 'RPG_SKILL_MULTI FORGED_TARGET' ' Role=Client Peer=([1-9]\d*) OtherPeer=([1-9]\d*) NetId=([1-9]\d*)' 1)[0]
        $case4 = @($receipts | Where-Object { [int]$_.Groups[1].Value -eq $peer -and $_.Groups[2].Value -eq '4' })[0]
        $case3 = @($receipts | Where-Object { [int]$_.Groups[1].Value -eq $peer -and $_.Groups[2].Value -eq '3' })[0]
        if ($sf.Count -ne 1 -or [int]$cf.Groups[1].Value -ne $peer -or $sf[0].Index -le $case3.Index -or $sf[0].Index -ge $case4.Index) { throw 'Foreign actor was not received in the actual forged case.' }
        foreach ($f in @($sf[0], $cf)) {
            if ([int]$f.Groups[2].Value -ne $other -or [uint64]$f.Groups[3].Value -ne $op.targets[0].netRefId) { throw 'Submitted foreign actor is not the other real target.' }
        }
        $baseline = @(MultiRows $raw 'RPG_SKILL_MULTI CROSS_BASELINE' ' Peer=([1-9]\d*) OtherPeer=([1-9]\d*) Index=([0-3]) NetId=([1-9]\d*) Health=1000000.000000' 4)
        $results = @(MultiRows $raw 'RPG_SKILL_MULTI CROSS_RESULT' ' Peer=([1-9]\d*) OtherPeer=([1-9]\d*) Index=([0-3]) NetId=([1-9]\d*) Before=1000000.000000 After=(\d+\.\d{6}) Notify=([01])' 4)
        $notify = @(MultiRows $raw 'RPG_SKILL_MULTI CROSS_NOTIFY' ' Peer=([1-9]\d*) OtherPeer=([1-9]\d*) Index=0 NetId=([1-9]\d*) Old=1000000.000000 New=(\d+\.\d{6})' 1)[0]
        $allVerified = @(MultiRows $raw 'RPG_SKILL_MULTI ALL_VERIFIED' ' Peer=([1-9]\d*) OtherPeer=([1-9]\d*) Cases=14 HealthUnchanged=1' 1)[0]
        $complete = @(MultiRows $raw 'RPG_SKILL_MULTI CLIENT_COMPLETE' ' Peer=([1-9]\d*) OtherPeer=([1-9]\d*) Targets=8' 1)[0]
        foreach ($row in @($baseline) + @($results) + @($notify, $allVerified, $complete)) {
            if ([int]$row.Groups[1].Value -ne $peer -or [int]$row.Groups[2].Value -ne $other) { throw 'Cross-client evidence belongs to the wrong connection.' }
        }
        $input = [regex]::Match($raw, 'RPG_SKILL_NETTEST CLIENT_INPUT_STARTED').Index
        $inputPrimed = @(MultiRows $raw "RPG_SKILL_PEER Peer=$peer RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED" ' Action=IA_IdentitySkill FixtureBinding=1' 1)[0]
        $clientActive = [regex]::Match($raw, 'RPG_SKILL_NETTEST CLIENT_ACTIVE').Index
        $primedClient = [regex]::Match($raw, 'RPG_SKILL_ABUSE PRIMED Role=Client').Index
        $firstSent = [regex]::Match($raw, 'RPG_SKILL_ABUSE SENT Index=0').Index
        $ownBaselines = @([regex]::Matches($raw, 'RPG_SKILL_DAMAGE BASELINE[^\r\n]*'))
        $ownSends = @([regex]::Matches($raw, 'RPG_SKILL_ABUSE SENT[^\r\n]*'))
        $releaseReady = [regex]::Match($raw, 'RPG_SKILL_ABUSE RELEASE_READY').Index
        $sent4 = [regex]::Match($raw, 'RPG_SKILL_ABUSE SENT Index=4').Index
        $stop = [regex]::Match($raw, 'RPG_SKILL_NETTEST CLIENT_INPUT_STOPPED').Index
        $clientDone = [regex]::Match($raw, 'RPG_SKILL_NETTEST CLIENT_COMPLETE').Index
        if ($clientReadyRows[$peer].Index -ge $input -or ($ownBaselines | Measure-Object Index -Maximum).Maximum -ge $clientReadyRows[$peer].Index -or
            [regex]::Matches($raw, 'RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED').Count -ne 1 -or
            $inputPrimed.Index -le $clientActive -or $inputPrimed.Index -ge $primedClient -or $primedClient -ge $firstSent -or
            $cf.Index -ge $sent4 -or $allVerified.Index -le ($ownSends | Measure-Object Index -Maximum).Maximum -or
            $allVerified.Index -ge $releaseReady -or $allVerified.Index -ge $stop -or
            $notify.Index -le $allVerified.Index -or $complete.Index -ge $clientDone -or $complete.Index -le $notify.Index) { throw 'Cross-client readiness/normal-release order mismatch.' }
        for ($target=0; $target -lt 4; ++$target) {
            $b = @($baseline | Where-Object { [int]$_.Groups[3].Value -eq $target })
            $r = @($results | Where-Object { [int]$_.Groups[3].Value -eq $target })
            $expected = $op.targets[$target].after
            if ($b.Count -ne 1 -or $r.Count -ne 1 -or [uint64]$b[0].Groups[4].Value -ne $op.targets[$target].netRefId -or
                [uint64]$r[0].Groups[4].Value -ne $op.targets[$target].netRefId -or
                [double]::Parse($r[0].Groups[5].Value, [Globalization.CultureInfo]::InvariantCulture) -ne $expected -or
                [int]$r[0].Groups[6].Value -ne $(if ($target -eq 0) { 1 } else { 0 }) -or
                $b[0].Index -ge $clientReadyRows[$peer].Index -or $r[0].Index -le $notify.Index -or $r[0].Index -ge $complete.Index) { throw 'Other-player HP replication/notification mismatch.' }
        }
        if ([uint64]$notify.Groups[3].Value -ne $op.targets[0].netRefId -or
            [double]::Parse($notify.Groups[4].Value, [Globalization.CultureInfo]::InvariantCulture) -ne $op.targets[0].after -or
            [regex]::Matches($raw, 'RPG_SKILL_MULTI ').Count -ne 13) { throw 'Unexpected other-player HP notification/evidence.' }
        $resultPeers += [ordered]@{ peer=$peer; actualPlayerASCNetRefId=$p.actualPlayerASCNetRefId; abilityHandle=$p.abilityHandle
            abilityEndOriginVerified=$p.abilityEndOriginVerified; clientAbilityEndOrigin=$p.clientAbilityEndOrigin
            activationPredictionKey=$p.activationPredictionKey; expectedNormalHealthReduction=$p.expectedNormalHealthReduction
            submittedMessages=$p.submittedMessages; targets=$p.targets }
    }
    return [ordered]@{
        schemaVersion=1; clientCount=2; casesPerPeer=7; actualMaliciousRequests=14; ability='GA_Player_ChargeSlash_C'
        damageEffect='GE_Shared_ApplyDamage_C'; authoredInputMontageDamageVerified=$true
        simultaneousAuthoredActivationsVerified=$true; authoredTargetDataConsumerVerified=$true; multiPeerDamageIsolationVerified=$true
        foreignSubmittedTargetRejectedVerified=$true; actualHPUnchangedDuringMaliciousRequestsVerified=$true
        normalInputReleaseRecoveredDamageVerified=$true; bothTargetGroupsReplicatedOnBothClientsVerified=$true
        abilityEndOriginVerified=(@($sourceProofs.Values | Where-Object { -not $_.abilityEndOriginVerified }).Count -eq 0)
        delayedClientMontageFixture=[bool]$RequireEndRaceFixture
        authorityRpcEndOnBothClientsVerified=(@($sourceProofs.Values | Where-Object { -not $_.abilityEndOriginVerified -or $_.clientAbilityEndOrigin -ne 'AuthorityRPC' }).Count -eq 0)
        nativeFixtureTargets=$true; stationarySeparatedSourcesFixture=$true; productionNpcLifecycleVerified=$false
        controlledRepressThroughAuthoredInputBindingFixture=$true; physicalSecondPressVerified=$false
        finalCapRejectionOverWireVerified=$false; wireRateFloodVerified=$false; shippingRuntimeVerified=$false
        packetLagMinMs=$PacketLagMinMs; packetLagMaxMs=$PacketLagMaxMs; packetLossPercent=$PacketLossPercent; peers=$resultPeers
    }
}
