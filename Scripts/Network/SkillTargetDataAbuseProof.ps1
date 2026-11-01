. (Join-Path $PSScriptRoot 'SkillDamageProof.ps1')
function Get-SkillTargetDataAbuseProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ServerText,
        [Parameter(Mandatory)] [string] $ClientText,
        [ValidateRange(0, 2000)] [int] $PacketLagMinMs = 0,
        [ValidateRange(0, 2000)] [int] $PacketLagMaxMs = 0,
        [ValidateRange(0, 50)] [int] $PacketLossPercent = 0,
        [ValidateSet(1, 2)] [int] $ExpectedClientCount = 1,
        [switch] $RequireEndOrigin
    )
    $damage = Get-SkillDamageProof @PSBoundParameters
    foreach ($text in @($ServerText, $ClientText)) {
        if ($text -match 'RPG_SKILL_ABUSE FAILED|Fatal error:|Assertion failed:|Ensure condition failed:') {
            throw 'Authored TargetData abuse runtime failure.'
        }
    }
    function Rows([string] $Text, [string] $Marker, [string] $Suffix, [int] $Count) {
        $rows = [regex]::Matches($Text, '(?m)' + [regex]::Escape($Marker) + $Suffix + '\r?$')
        if ($rows.Count -ne $Count -or [regex]::Matches($Text, [regex]::Escape($Marker)).Count -ne $Count) {
            throw "Missing/duplicate/malformed evidence: $Marker."
        }
        return $rows
    }
    function Newer([int] $New, [int] $Old) {
        $delta = ($New - $Old + 32767) % 32767
        return $New -ge 1 -and $New -le 32767 -and $delta -gt 0 -and $delta -le 16383
    }
    $initialSuffix = ' Role=(Server|Client) ASCNetId=(\d+) Handle=(\d+) Original=(\d+) Message=(\d+) Entries=([12]) Valid=1'
    $initialServer = @(Rows $ServerText 'RPG_SKILL_ABUSE INITIAL' $initialSuffix 1)[0]
    $initialClient = @(Rows $ClientText 'RPG_SKILL_ABUSE INITIAL' $initialSuffix 1)[0]
    $primedServer = @(Rows $ServerText 'RPG_SKILL_ABUSE PRIMED' $initialSuffix 1)[0]
    $primedClient = @(Rows $ClientText 'RPG_SKILL_ABUSE PRIMED' $initialSuffix 1)[0]
    if ($initialServer.Groups[1].Value -ne 'Server' -or $initialClient.Groups[1].Value -ne 'Client') { throw 'Incorrect initial capture roles.' }
    for ($g = 2; $g -le 6; ++$g) {
        if ($initialServer.Groups[$g].Value -ne $initialClient.Groups[$g].Value) { throw 'Initial authored wire identity mismatch.' }
    }
    $ascId = $initialServer.Groups[2].Value
    $handle = $initialServer.Groups[3].Value
    $original = [int]$initialServer.Groups[4].Value
    if ($damage.abilityEndOriginVerified -and ($damage.actualEndSourceASCNetRefId -ne [uint64]$ascId -or
        $damage.actualEndAbilityHandle -ne [uint64]$handle)) { throw 'Authored TargetData activation does not own the end receipt.' }
    $firstMessage = [int]$initialServer.Groups[5].Value
    $initialEntries = [int]$initialServer.Groups[6].Value
    $consumers = @(Rows $ServerText 'RPG_SKILL_ABUSE_CONSUMER' ' Handle=(\d+) Original=(\d+) KnownTag=([01]) Decoded=([01]) Validated=([01]) Waiting=([01])' 6)
    if ([regex]::Matches($ClientText, 'RPG_SKILL_ABUSE_CONSUMER').Count -ne 0) { throw 'Client synthesized a server validation record.' }
    foreach ($consumer in $consumers) {
        if ($consumer.Groups[1].Value -ne $handle -or [int]$consumer.Groups[2].Value -ne $original) {
            throw 'Consumer trace is not from the challenged authored activation.'
        }
    }
    if ($consumers[0].Index -ge $initialServer.Index -or $consumers[1].Index -le $initialServer.Index -or
        $consumers[1].Index -ge $primedServer.Index) { throw 'Initial/pressed TargetData did not reach the real consumer.' }
    for ($k=0; $k -lt 2; ++$k) {
        if ($consumers[$k].Groups[3].Value -ne '1' -or $consumers[$k].Groups[4].Value -ne '1' -or
            $consumers[$k].Groups[5].Value -ne '1' -or [int]$consumers[$k].Groups[6].Value -ne (1-$k)) {
            throw 'Authored initial targeting was not validated.'
        }
    }
    for ($g = 2; $g -le 6; ++$g) {
        if ($primedServer.Groups[$g].Value -ne $primedClient.Groups[$g].Value -or
            ($g -le 4 -and $primedServer.Groups[$g].Value -ne $initialServer.Groups[$g].Value)) {
            throw 'Authored input press did not prime the same player ASC/activation.'
        }
    }
    if ($primedServer.Groups[1].Value -ne 'Server' -or $primedClient.Groups[1].Value -ne 'Client' -or
        $primedServer.Index -le $initialServer.Index -or $primedClient.Index -le $initialClient.Index -or
        -not (Newer ([int]$primedServer.Groups[5].Value) $firstMessage)) { throw 'Invalid authored input-press ordering.' }
    if ([uint64]$ascId -eq 0 -or [uint64]$handle -eq 0 -or $original -lt 1 -or $original -gt 32767 -or
        $firstMessage -lt 1 -or $firstMessage -gt 32767 -or @($damage.targets | Where-Object { [string]$_.netRefId -eq $ascId }).Count -ne 0) {
        throw 'Invalid real player ASC/activation identity.'
    }
    foreach ($text in @($ServerText, $ClientText)) {
        $end = [regex]::Match($text, 'RPG_SKILL_END_TRACE[^\r\n]* Original=(\d+) Cancelled=0')
        if ([int]$end.Groups[1].Value -ne $original) { throw 'Abuse did not use the completed authored activation.' }
    }
    $sent = @(Rows $ClientText 'RPG_SKILL_ABUSE SENT' ' Index=([0-6]) Name=(\w+) ASCNetId=(\d+) Handle=(\d+) Original=(\d+) Message=(\d+) Entries=([123])' 7)
    $received = @(Rows $ServerText 'RPG_SKILL_ABUSE RECEIVED' ' Index=([0-6]) Name=(\w+) ASCNetId=(\d+) Handle=(\d+) Original=(\d+) Message=(\d+) Entries=([123]) Admitted=([01]) TargetValid=([01]) Active=1 Events=0' 7)
    $hp = @(Rows $ServerText 'RPG_SKILL_ABUSE HP' ' Index=([0-6]) Target=([0-3]) NetId=(\d+) Health=1000000.000000 Effects=0' 28)
    $verified = @(Rows $ServerText 'RPG_SKILL_ABUSE VERIFIED' ' Cases=7 HealthUnchanged=1' 1)[0]
    $releaseReady = @(Rows $ClientText 'RPG_SKILL_ABUSE RELEASE_READY' ' ASCNetId=(\d+) Original=(\d+) Cases=7 HealthUnchanged=1' 1)[0]
    $recovery = @(Rows $ServerText 'RPG_SKILL_ABUSE RECOVERY' ' ASCNetId=(\d+) Handle=(\d+) Original=(\d+) Message=(\d+) Valid=1 Events=0 Effects=0' 1)[0]
    $serverEnd = @(Rows $ServerText 'RPG_SKILL_ABUSE SERVER_COMPLETE' ' Cases=7 Recovery=1 Targets=4' 1)[0]
    $clientEnd = @(Rows $ClientText 'RPG_SKILL_ABUSE CLIENT_COMPLETE' ' Cases=7 Recovery=1 Targets=4' 1)[0]
    if ([regex]::Matches($ServerText, 'RPG_SKILL_ABUSE ').Count -ne 40 -or
        [regex]::Matches($ClientText, 'RPG_SKILL_ABUSE ').Count -ne 11) { throw 'Unexpected abuse evidence.' }
    $names = @('ReplayInitial','AlteredReplay','WrongActivation','OversizedActors','ActorForgery','OutOfRange','UnexpectedTag')
    $previousS = $primedServer.Index
    $previousC = [regex]::Match($ClientText, 'RPG_SKILL_NETTEST CLIENT_ACTIVE').Index
    if ($initialClient.Index -ge $previousC) { throw 'Initial capture did not precede the authored activation observation.' }
    $previousC = [Math]::Max($previousC, $primedClient.Index)
    $previousMessage = [int]$primedServer.Groups[5].Value
    $messages = @()
    for ($i = 0; $i -lt 7; ++$i) {
        $s = @($received | Where-Object { [int]$_.Groups[1].Value -eq $i })
        $c = @($sent | Where-Object { [int]$_.Groups[1].Value -eq $i })
        if ($s.Count -ne 1 -or $c.Count -ne 1) { throw 'Duplicate/missing malicious case.' }
        $s = $s[0]; $c = $c[0]
        for ($g = 1; $g -le 7; ++$g) {
            if ($s.Groups[$g].Value -ne $c.Groups[$g].Value) { throw 'Server receipt differs from the actual client RPC.' }
        }
        $expectedEntries = if ($i -eq 3) { 3 } elseif ($i -in 1,4,5) { 1 } else { $initialEntries }
        $expectedOriginal = if ($i -eq 2) { if ($original -eq 1) { 32767 } else { $original - 1 } } else { $original }
        $admitted = if ($i -ge 4) { 1 } else { 0 }
        if ($s.Groups[2].Value -ne $names[$i] -or $s.Groups[3].Value -ne $ascId -or $s.Groups[4].Value -ne $handle -or
            [int]$s.Groups[5].Value -ne $expectedOriginal -or [int]$s.Groups[7].Value -ne $expectedEntries -or
            [int]$s.Groups[8].Value -ne $admitted -or [int]$s.Groups[9].Value -ne (1 - $admitted) -or
            $s.Index -le $previousS -or $c.Index -le $previousC) { throw "Invalid case identity/admission/skill state: $i." }
        $message = [int]$s.Groups[6].Value
        $caseConsumers = @($consumers | Where-Object { $_.Index -gt $previousS -and $_.Index -lt $s.Index })
        if ($caseConsumers.Count -ne $admitted) { throw 'Admission/real authored consumer dispatch mismatch.' }
        if ($admitted -eq 1) {
            $known = if ($i -eq 6) { '0' } else { '1' }
            $decoded = if ($i -eq 4) { '0' } else { '1' }
            if ($caseConsumers[0].Groups[3].Value -ne $known -or $caseConsumers[0].Groups[4].Value -ne $decoded -or
                $caseConsumers[0].Groups[5].Value -ne '0' -or $caseConsumers[0].Groups[6].Value -ne '0') {
                throw 'Malformed targeting did not fail the authored skill validator.'
            }
        }
        if ($i -lt 2) {
            if ($message -ne $firstMessage) { throw 'Replay was not the actual initial message.' }
        } else {
            if (-not (Newer $message $previousMessage)) { throw 'Fresh malicious message did not use advancing GAS keys.' }
            $previousMessage = $message
        }
        $messages += $message
        for ($target = 0; $target -lt 4; ++$target) {
            $row = @($hp | Where-Object { [int]$_.Groups[1].Value -eq $i -and [int]$_.Groups[2].Value -eq $target })
            if ($row.Count -ne 1 -or [uint64]$row[0].Groups[3].Value -ne $damage.targets[$target].netRefId -or
                $row[0].Index -le $previousS -or $row[0].Index -ge $s.Index) { throw 'Missing or misplaced actual HP invariant.' }
        }
        $previousS = $s.Index; $previousC = $c.Index
    }
    if ($verified.Index -le $previousS -or $recovery.Index -le $verified.Index -or
        $releaseReady.Index -le $previousC -or $releaseReady.Groups[1].Value -ne $ascId -or
        [int]$releaseReady.Groups[2].Value -ne $original -or $recovery.Groups[1].Value -ne $ascId -or
        $recovery.Groups[2].Value -ne $handle -or [int]$recovery.Groups[3].Value -ne $original -or
        -not (Newer ([int]$recovery.Groups[4].Value) $previousMessage)) { throw 'Normal release did not recover the same activation.' }
    if ($consumers[5].Index -le $verified.Index -or $consumers[5].Index -ge $recovery.Index -or
        $consumers[5].Groups[3].Value -ne '1' -or $consumers[5].Groups[4].Value -ne '1' -or
        $consumers[5].Groups[5].Value -ne '1' -or $consumers[5].Groups[6].Value -ne '0') {
        throw 'Recovery did not pass the real authored validator.'
    }
    $stop = [regex]::Match($ClientText, 'RPG_SKILL_NETTEST CLIENT_INPUT_STOPPED').Index
    $serverRelease = [regex]::Match($ServerText, 'RPG_SKILL_NETTEST RELEASE_SECTION Role=Server').Index
    $effect = [regex]::Match($ServerText, 'RPG_SKILL_DAMAGE EFFECT').Index
    $notify = [regex]::Match($ClientText, 'RPG_SKILL_DAMAGE HP_NOTIFY').Index
    if ($releaseReady.Index -ge $stop -or $recovery.Index -ge $serverRelease -or $verified.Index -ge $effect -or
        $releaseReady.Index -ge $notify -or $serverEnd.Index -le $effect -or $clientEnd.Index -le $notify) {
        throw 'Damage happened before verified hostile requests and normal input release.'
    }
    return [ordered]@{
        schemaVersion=1; clientCount=1; cases=7; ability=$damage.ability; damageEffect=$damage.damageEffect
        actualPlayerASCNetRefId=[uint64]$ascId; abilityHandle=[uint64]$handle; activationPredictionKey=$original
        malformedWireRequestsVerified=$true; authoredTargetDataConsumerVerified=$true
        actualHPUnchangedDuringMaliciousRequestsVerified=$true; normalInputReleaseRecoveredDamageVerified=$true
        authoredInputMontageDamageVerified=$true; clientAttributeNotificationAndReplicationVerified=$true
        nativeFixtureTargets=$true; multiPeerDamageIsolationVerified=$false; shippingRuntimeVerified=$false
        finalCapRejectionOverWireVerified=$false; productionNpcLifecycleVerified=$false
        abilityEndOriginVerified=$damage.abilityEndOriginVerified; clientAbilityEndOrigin=$damage.clientAbilityEndOrigin
        expectedNormalHealthReduction=$damage.expectedHealthReduction; submittedMessages=$messages; targets=$damage.targets
        packetLagMinMs=$PacketLagMinMs; packetLagMaxMs=$PacketLagMaxMs; packetLossPercent=$PacketLossPercent
    }
}
