function Get-SkillDamageProof {
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
    if ($PacketLagMaxMs -lt $PacketLagMinMs) { throw 'Invalid packet lag range.' }
    if ($ServerText -match 'RPG_SKILL_END_RACE ' -or $ClientText -match 'RPG_SKILL_END_RACE ') {
        throw 'Delayed montage fixture cannot be classified as an ordinary authored damage run.'
    }
    function Read-Rows([string] $Text, [string] $Marker, [string] $Suffix, [int] $Count) {
        $rows = [regex]::Matches($Text, '(?m)' + [regex]::Escape($Marker) + $Suffix + '\r?$')
        if ($rows.Count -ne $Count -or [regex]::Matches($Text, [regex]::Escape($Marker)).Count -ne $Count) {
            throw "Missing/duplicate/malformed evidence: $Marker."
        }
        return $rows
    }
    $number = '(-?\d+\.\d{6})'
    $culture = [Globalization.CultureInfo]::InvariantCulture
    function Number([string] $Value) { return [double]::Parse($Value, $culture) }
    if ($ServerText -notmatch "RPG_NETTEST SERVER_STATE Iris=1 Connected=$ExpectedClientCount Expected=$ExpectedClientCount Revision=\d+" -or
        $ClientText -notmatch "RPG_NETTEST CLIENT_READY ServerIris=1 ClientIris=1 Connected=$ExpectedClientCount Expected=$ExpectedClientCount Revision=\d+") {
        throw 'Missing expected Iris client readiness.'
    }
    $ends = @()
    $origins = @()
    foreach ($text in @($ServerText, $ClientText)) {
        if ($text -match 'RPG_SKILL_(DAMAGE|NETTEST) FAILED|Fatal error:|Assertion failed:|Ensure condition failed:') {
            throw 'Skill damage runtime failure/assertion/ensure.'
        }
        foreach ($setting in @(
            @{ name = 'PktLoss'; value = $PacketLossPercent },
            @{ name = 'PktLagMin'; value = $PacketLagMinMs },
            @{ name = 'PktLagMax'; value = $PacketLagMaxMs }
        )) {
            $settings = [regex]::Matches($text, 'LogNet: ' + $setting.name + ' set to (\d+)\b')
            if ($settings.Count -eq 0 -or [int]$settings[$settings.Count - 1].Groups[1].Value -ne $setting.value) {
                throw "Packet simulation was not applied as requested: $($setting.name)."
            }
        }
        $end = @(Read-Rows $text 'RPG_SKILL_END_TRACE' ' Avatar=\S+ Ability=GA_Player_ChargeSlash_C_\d+ Original=([1-9]\d*) Cancelled=0 Replicate=([01]) Active=1 Ending=0 NetSecurity=0' 1)
        if ([int]$end[0].Groups[1].Value -gt 32767) { throw 'Invalid activation key.' }
        $role = if ($ends.Count -eq 0) { 'Server' } else { 'Client' }
        $origin = $null
        if ([regex]::Matches($text, 'RPG_SKILL_END_ORIGIN').Count -gt 0) {
            $origin = @(Read-Rows $text 'RPG_SKILL_END_ORIGIN' ' Role=(Server|Client) ASCNetId=([1-9]\d*) Handle=([1-9]\d*) Original=([1-9]\d*) Source=(AuthorityLocal|ClientLocal|AuthorityRPC) RpcMatched=([01]) RpcOriginal=(\d+)' 1)[0]
            if ($origin.Groups[1].Value -ne $role -or $origin.Groups[4].Value -ne $end[0].Groups[1].Value -or
                $origin.Index -ge $end[0].Index) { throw 'Ability end provenance role/key/order mismatch.' }
            $remote = $role -eq 'Client' -and $origin.Groups[5].Value -eq 'AuthorityRPC'
            if ($remote) {
                if ($end[0].Groups[2].Value -ne '0' -or $origin.Groups[6].Value -ne '1' -or
                    $origin.Groups[7].Value -ne $origin.Groups[4].Value) { throw 'Remote end requires an actual matching authority RPC receipt.' }
            } elseif ($end[0].Groups[2].Value -ne '1' -or $origin.Groups[6].Value -ne '0' -or
                $origin.Groups[7].Value -ne '0' -or $origin.Groups[5].Value -ne $(if ($role -eq 'Server') { 'AuthorityLocal' } else { 'ClientLocal' })) {
                throw 'Local/server end provenance mismatch.'
            }
        } elseif ($RequireEndOrigin -or $end[0].Groups[2].Value -ne '1') {
            throw 'Missing actual ability end provenance; an unproven non-replicated end is never accepted.'
        }
        $origins += $origin
        $ends += $end[0]
    }
    if ($ends[0].Groups[1].Value -ne $ends[1].Groups[1].Value) { throw 'Server/client activation identity mismatch.' }
    $originVerified = $null -ne $origins[0] -and $null -ne $origins[1]
    if (($null -ne $origins[0]) -ne ($null -ne $origins[1])) { throw 'Partial server/client end provenance.' }
    if ($originVerified -and ($origins[0].Groups[2].Value -ne $origins[1].Groups[2].Value -or
        $origins[0].Groups[3].Value -ne $origins[1].Groups[3].Value)) { throw 'End provenance ASC/handle identity mismatch.' }
    $input = @(Read-Rows $ClientText 'RPG_SKILL_NETTEST CLIENT_INPUT_STARTED' ' Action=IA_IdentitySkill' 1)
    $clientActive = @(Read-Rows $ClientText 'RPG_SKILL_NETTEST CLIENT_ACTIVE' ' Ability=Default__GA_Player_ChargeSlash_C' 1)
    $stopped = @(Read-Rows $ClientText 'RPG_SKILL_NETTEST CLIENT_INPUT_STOPPED' '' 1)
    $released = @(Read-Rows $ClientText 'RPG_SKILL_NETTEST CLIENT_RELEASED' '' 1)
    $clientRelease = @(Read-Rows $ClientText 'RPG_SKILL_NETTEST RELEASE_SECTION' ' Role=Client Controller=\S+' 1)
    $serverActive = @(Read-Rows $ServerText 'RPG_SKILL_NETTEST SERVER_ACTIVE' ' Controller=\S+ Ability=Default__GA_Player_ChargeSlash_C' 1)
    $serverRelease = @(Read-Rows $ServerText 'RPG_SKILL_NETTEST RELEASE_SECTION' ' Role=Server Controller=\S+' 1)
    $clientComplete = @(Read-Rows $ClientText 'RPG_SKILL_NETTEST CLIENT_COMPLETE' '' 1)
    $serverComplete = @(Read-Rows $ServerText 'RPG_SKILL_NETTEST SERVER_COMPLETE' ' Controller=\S+' 1)
    if ($input[0].Index -ge $clientActive[0].Index -or $clientActive[0].Index -ge $stopped[0].Index -or
        $stopped[0].Index -gt $clientRelease[0].Index -or $clientRelease[0].Index -gt $released[0].Index -or
        $released[0].Index -ge $clientComplete[0].Index -or $serverActive[0].Index -ge $serverRelease[0].Index) {
        throw 'Authored input/release ordering mismatch.'
    }
    if ($originVerified -and ($serverRelease[0].Index -ge $origins[0].Index -or
        $released[0].Index -ge $origins[1].Index)) { throw 'Ability ended before the authored release completed.' }
    $config = @(Read-Rows $ServerText 'RPG_SKILL_DAMAGE CONFIG' (" Ability=GA_Player_ChargeSlash_C Radius=650 Angle=120 Base=$number Scalar=$number Attack=$number Defense=1 Crit=0 ExpectedDamage=$number") 1)
    $base = Number $config[0].Groups[1].Value
    $scalar = Number $config[0].Groups[2].Value
    $attack = Number $config[0].Groups[3].Value
    $damage = Number $config[0].Groups[4].Value
    # Mirror the float SetByCaller product and the production execution's integer truncation.
    $setByCaller = [single]([single]$base * [single]$scalar)
    $calculated = [Math]::Truncate([double]$setByCaller * [single]$attack)
    if ($base -le 0 -or $scalar -le 0 -or $attack -le 0 -or $calculated -lt 1 -or
        $calculated -ge 1000000 -or $damage -ne $calculated) { throw 'Invalid expected production damage.' }
    $effect = @(Read-Rows $ServerText 'RPG_SKILL_DAMAGE EFFECT' (" Index=0 NetId=(\d+) Count=1 SetByCaller=$number Class=GE_Shared_ApplyDamage_C") 1)
    if ([Math]::Abs((Number $effect[0].Groups[2].Value) - $setByCaller) -gt 0.0001 -or
        $config[0].Index -ge $effect[0].Index -or $serverRelease[0].Index -ge $effect[0].Index) {
        throw 'Authored damage GE/magnitude/release mismatch.'
    }
    $event = @(Read-Rows $ServerText 'RPG_SKILL_DAMAGE EVENT' ' Tag=Player.Event.AOE Count=1' 1)
    $poses = @(Read-Rows $ServerText 'RPG_SKILL_DAMAGE POSE' (" Index=([0-3]) NetId=(\d+) X=$number Y=$number Z=$number Friendly=([01])") 4)
    $serverRows = @(Read-Rows $ServerText 'RPG_SKILL_DAMAGE RESULT' (" Role=Server Index=([0-3]) NetId=(\d+) Before=$number After=$number Effects=([01])") 4)
    $clientRows = @(Read-Rows $ClientText 'RPG_SKILL_DAMAGE RESULT' (" Role=Client Index=([0-3]) NetId=(\d+) Before=$number After=$number Notify=([01])") 4)
    $baselines = @(Read-Rows $ClientText 'RPG_SKILL_DAMAGE BASELINE' (" Index=([0-3]) NetId=(\d+) Health=$number") 4)
    $notify = @(Read-Rows $ClientText 'RPG_SKILL_DAMAGE HP_NOTIFY' (" Index=0 NetId=(\d+) Old=$number New=$number") 1)
    $serverDamageComplete = @(Read-Rows $ServerText 'RPG_SKILL_DAMAGE SERVER_COMPLETE' ' Targets=4 Events=1' 1)
    $clientDamageComplete = @(Read-Rows $ClientText 'RPG_SKILL_DAMAGE CLIENT_COMPLETE' ' Targets=4 ReplicatedHealth=1' 1)
    if ([regex]::Matches($ServerText, 'RPG_SKILL_DAMAGE ').Count -ne 12 -or
        [regex]::Matches($ClientText, 'RPG_SKILL_DAMAGE ').Count -ne 10) { throw 'Unexpected damage evidence.' }
    $targets = @()
    $ids = @()
    $offsets = @(@(300,0,0), @(-300,0,0), @(300,120,0), @(1500,0,0))
    $roles = @('ApprovedEnemy', 'BehindSector', 'Friendly', 'OutOfRange')
    for ($i = 0; $i -lt 4; ++$i) {
        $s = @($serverRows | Where-Object { $_.Groups[1].Value -eq "$i" })
        $c = @($clientRows | Where-Object { $_.Groups[1].Value -eq "$i" })
        $b = @($baselines | Where-Object { $_.Groups[1].Value -eq "$i" })
        $p = @($poses | Where-Object { $_.Groups[1].Value -eq "$i" })
        if ($s.Count -ne 1 -or $c.Count -ne 1 -or $b.Count -ne 1 -or $p.Count -ne 1) { throw 'Duplicate/missing target index.' }
        $id = [uint64]$s[0].Groups[2].Value
        if ($id -eq 0 -or $ids -contains $id -or $id -ne [uint64]$c[0].Groups[2].Value -or
            $id -ne [uint64]$b[0].Groups[2].Value -or $id -ne [uint64]$p[0].Groups[2].Value) { throw 'Target Iris identities mismatch.' }
        $ids += $id
        $after = if ($i -eq 0) { 1000000 - $damage } else { 1000000 }
        $changed = if ($i -eq 0) { 1 } else { 0 }
        foreach ($row in @($s[0], $c[0])) {
            if ((Number $row.Groups[3].Value) -ne 1000000 -or (Number $row.Groups[4].Value) -ne $after -or
                [int]$row.Groups[5].Value -ne $changed) { throw 'Target HP/effect/notification mismatch.' }
        }
        if ((Number $b[0].Groups[3].Value) -ne 1000000 -or $b[0].Index -ge $input[0].Index -or
            $s[0].Index -le $effect[0].Index -or $s[0].Index -ge $serverDamageComplete[0].Index -or
            $c[0].Index -le $notify[0].Index -or $c[0].Index -ge $clientDamageComplete[0].Index) { throw 'HP evidence ordering mismatch.' }
        for ($axis = 0; $axis -lt 3; ++$axis) {
            if ([Math]::Abs((Number $p[0].Groups[3+$axis].Value) - $offsets[$i][$axis]) -gt 1) { throw 'Damage-time query geometry mismatch.' }
        }
        if ([int]$p[0].Groups[6].Value -ne $(if ($i -eq 2) { 1 } else { 0 })) { throw 'Damage-time team mismatch.' }
        $targets += [ordered]@{ index=$i; role=$roles[$i]; netRefId=$id; before=1000000; after=$after; effects=$changed; clientHealthNotified=($changed -eq 1) }
    }
    if ([uint64]$effect[0].Groups[1].Value -ne $ids[0] -or [uint64]$notify[0].Groups[1].Value -ne $ids[0] -or
        (Number $notify[0].Groups[2].Value) -ne 1000000 -or (Number $notify[0].Groups[3].Value) -ne (1000000-$damage) -or
        $notify[0].Index -le $input[0].Index -or $event[0].Index -ge $serverDamageComplete[0].Index -or
        $serverRelease[0].Index -ge $ends[0].Index -or $ends[0].Index -ge $serverDamageComplete[0].Index -or
        $clientRelease[0].Index -ge $ends[1].Index -or $ends[1].Index -ge $clientDamageComplete[0].Index -or
        $serverDamageComplete[0].Index -ge $serverComplete[0].Index -or $clientDamageComplete[0].Index -ge $clientComplete[0].Index) {
        throw 'Final HP/notification/completion mismatch.'
    }
    if ($originVerified -and [uint64]$origins[0].Groups[2].Value -in $ids) { throw 'Source end receipt reused a target ASC/actor identity.' }
    return [ordered]@{
        schemaVersion=1; clientCount=1; ability='GA_Player_ChargeSlash_C'; damageEffect='GE_Shared_ApplyDamage_C'
        networkClientCount=$ExpectedClientCount
        irisReplicationVerified=$true
        authoredInputMontageDamageVerified=$true; serverHealthAndRejectionVerified=$true; clientAttributeNotificationAndReplicationVerified=$true
        nativeFixtureTargets=$true; criticalChanceForcedZero=$true; maliciousWireDamageVerified=$false
        multiPeerDamageIsolationVerified=$false; productionNpcLifecycleVerified=$false; renderedHealthUiVerified=$false
        finalPerSkillHealthDamageCapVerified=$false; shippingRuntimeVerified=$false
        abilityEndOriginVerified=$originVerified
        clientAbilityEndOrigin=$(if ($originVerified) { $origins[1].Groups[5].Value } else { 'LegacyUnclassified' })
        actualEndSourceASCNetRefId=$(if ($originVerified) { [uint64]$origins[0].Groups[2].Value } else { 0 })
        actualEndAbilityHandle=$(if ($originVerified) { [uint64]$origins[0].Groups[3].Value } else { 0 })
        packetLagMinMs=$PacketLagMinMs; packetLagMaxMs=$PacketLagMaxMs; packetLossPercent=$PacketLossPercent
        baseDamage=$base; statScalar=$scalar; sourceAttack=$attack; targetDefense=1; setByCaller=[double]$setByCaller; expectedHealthReduction=$damage
        targets=$targets
    }
}
