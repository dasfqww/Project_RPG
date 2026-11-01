. (Join-Path $PSScriptRoot 'SkillDamageProof.ps1')

function Get-SkillFinalCapProof {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ServerText,
        [Parameter(Mandatory)] [string] $ClientText,
        [ValidateRange(0, 2000)] [int] $PacketLagMinMs = 0,
        [ValidateRange(0, 2000)] [int] $PacketLagMaxMs = 0,
        [ValidateRange(0, 50)] [int] $PacketLossPercent = 0,
        [switch] $RequireEndOrigin
    )
    $normal = Get-SkillDamageProof @PSBoundParameters
    function CapRow([string] $Text, [string] $Marker, [string] $Suffix, [int] $Count = 1) {
        $rows = @([regex]::Matches($Text, '(?m)RPG_SKILL_FINALCAP ' + [regex]::Escape($Marker) + $Suffix + '\r?$'))
        if ($rows.Count -ne $Count -or [regex]::Matches($Text, 'RPG_SKILL_FINALCAP ' + [regex]::Escape($Marker) + '\b').Count -ne $Count) {
            throw "Missing/duplicate/malformed final cap evidence: $Marker."
        }
        return $rows
    }
    foreach ($text in @($ServerText, $ClientText)) {
        if ($text -match 'RPG_SKILL_FINALCAP FAILED|RPG_SKILL_(ABUSE|MULTI) ') { throw 'Final cap failure/mixed probe evidence.' }
    }
    $sp = @(CapRow $ServerText 'PROFILE' ' Role=Server SourceASCNetId=([1-9]\d*) Cap=625.000000 OriginalCap=(\d+\.\d{6}) Transient=1')[0]
    $cp = @(CapRow $ClientText 'PROFILE' ' Role=Client SourceASCNetId=([1-9]\d*) Cap=625.000000 OriginalCap=(\d+\.\d{6}) Transient=1')[0]
    $active = @(CapRow $ServerText 'ACTIVE' ' SourceASCNetId=([1-9]\d*) Handle=([1-9]\d*) Original=([1-9]\d*) Cap=625.000000 SetByCaller=250.000000 Attack=2.500000 GlobalCap=1000000000.000000')[0]
    $exec = @(CapRow $ServerText 'EXECUTION' ' Index=([04]) SourceASCNetId=([1-9]\d*) NetId=([1-9]\d*) Handle=([1-9]\d*) Original=([1-9]\d*) Cap=625.000000 Calculated=(625|1250).000000 Bound=1 Approved=([01])' 2)
    $pose = @(CapRow $ServerText 'POSE' ' Index=4 NetId=([1-9]\d*) X=300.000000 Y=-120.000000 Z=0.000000 Friendly=0 Defense=0.500000')[0]
    $ge = @(CapRow $ServerText 'GE_CALLBACK' ' Index=4 NetId=([1-9]\d*) Count=1 SetByCaller=250.000000 Class=GE_Shared_ApplyDamage_C Health=1000000.000000')[0]
    $application = @(CapRow $ServerText 'APPLICATION' ' SourceASCNetId=([1-9]\d*) Handle=([1-9]\d*) Original=([1-9]\d*) AppliedTargets=1 AppliedHits=1 BudgetUsed=1 BudgetMax=([1-9]\d*) ApprovedNetId=([1-9]\d*) RejectedNetId=([1-9]\d*)')[0]
    $baseline = @(CapRow $ClientText 'BASELINE' ' Index=4 NetId=([1-9]\d*) Health=1000000.000000')[0]
    $sr = @(CapRow $ServerText 'RESULT' ' Role=Server Index=4 NetId=([1-9]\d*) Before=1000000.000000 After=1000000.000000 Callbacks=1 HealthChanges=0')[0]
    $cr = @(CapRow $ClientText 'RESULT' ' Role=Client Index=4 NetId=([1-9]\d*) Before=1000000.000000 After=1000000.000000 HealthChanges=0')[0]
    $sc = @(CapRow $ServerText 'SERVER_COMPLETE' ' Targets=5 Accepted=1 Rejected=1')[0]
    $cc = @(CapRow $ClientText 'CLIENT_COMPLETE' ' Targets=5 ReplicatedHealth=1')[0]
    if ([regex]::Matches($ServerText, 'RPG_SKILL_FINALCAP ').Count -ne 9 -or
        [regex]::Matches($ClientText, 'RPG_SKILL_FINALCAP ').Count -ne 4) { throw 'Unexpected final cap evidence.' }
    $asc = [uint64]$sp.Groups[1].Value
    $denied = [uint64]$baseline.Groups[1].Value
    $ids = @($normal.targets | ForEach-Object { [uint64]$_.netRefId })
    if ($normal.expectedHealthReduction -ne 625 -or $asc -in $ids -or $denied -in (@($asc) + $ids) -or
        [uint64]$cp.Groups[1].Value -ne $asc -or [uint64]$active.Groups[1].Value -ne $asc -or
        [double]::Parse($sp.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture) -le 1250 -or
        [double]::Parse($sp.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture) -gt 1000000000 -or
        $sp.Groups[2].Value -ne $cp.Groups[2].Value) { throw 'Fixture profile/ASC/normal boundary mismatch.' }
    foreach ($row in @($pose,$ge,$sr,$cr)) {
        if ([uint64]$row.Groups[1].Value -ne $denied) { throw 'Rejected target identity mismatch.' }
    }
    $handle = [uint64]$active.Groups[2].Value; $original = [int]$active.Groups[3].Value
    if ($normal.abilityEndOriginVerified -and ($normal.actualEndSourceASCNetRefId -ne $asc -or
        $normal.actualEndAbilityHandle -ne $handle)) { throw 'Final cap activation does not own the end receipt.' }
    $endKey = [regex]::Match($ServerText, 'RPG_SKILL_END_TRACE[^\r\n]* Original=([1-9]\d*)').Groups[1].Value
    if ($original -gt 32767 -or $original -ne [int]$endKey) { throw 'Wrong authored activation key.' }
    $release = [regex]::Match($ServerText, 'RPG_SKILL_NETTEST RELEASE_SECTION Role=Server').Index
    $event = [regex]::Match($ServerText, 'RPG_SKILL_DAMAGE EVENT').Index
    foreach ($index in @(0,4)) {
        $rows = @($exec | Where-Object { [int]$_.Groups[1].Value -eq $index })
        if ($rows.Count -ne 1) { throw 'Duplicate/missing execution target.' }
        $row = $rows[0]
        if ([uint64]$row.Groups[2].Value -ne $asc -or [uint64]$row.Groups[3].Value -ne $(if ($index -eq 0) { $ids[0] } else { $denied }) -or
            [uint64]$row.Groups[4].Value -ne $handle -or [int]$row.Groups[5].Value -ne $original -or
            [int]$row.Groups[6].Value -ne $(if ($index -eq 0) { 625 } else { 1250 }) -or
            [int]$row.Groups[7].Value -ne $(if ($index -eq 0) { 1 } else { 0 }) -or
            $row.Index -le $release -or $row.Index -le $active.Index -or $row.Index -ge $application.Index) { throw 'Actual native final cap decision mismatch.' }
        if ($index -eq 4 -and ($row.Index -ge $pose.Index -or $pose.Index -ge $ge.Index)) { throw 'Rejected GE/pose order mismatch.' }
    }
    if ([uint64]$application.Groups[1].Value -ne $asc -or [uint64]$application.Groups[2].Value -ne $handle -or
        [int]$application.Groups[3].Value -ne $original -or [uint64]$application.Groups[5].Value -ne $ids[0] -or
        [uint64]$application.Groups[6].Value -ne $denied -or $ge.Index -ge $application.Index -or $application.Index -ge $event -or
        $sp.Index -ge $active.Index -or $active.Index -ge $release -or
        $application.Index -ge $sr.Index -or $sr.Index -ge $sc.Index -or
        $sc.Index -ge [regex]::Match($ServerText, 'RPG_SKILL_NETTEST SERVER_COMPLETE').Index -or
        $cp.Index -ge $baseline.Index -or $baseline.Index -ge [regex]::Match($ClientText, 'RPG_SKILL_NETTEST CLIENT_INPUT_STARTED').Index -or
        $cr.Index -le [regex]::Match($ClientText, 'RPG_SKILL_END_TRACE').Index -or $cr.Index -ge $cc.Index -or
        $cc.Index -ge [regex]::Match($ClientText, 'RPG_SKILL_NETTEST CLIENT_COMPLETE').Index) { throw 'Final cap application/baseline/completion order mismatch.' }
    return [ordered]@{
        schemaVersion=1; clientCount=1; ability=$normal.ability; damageEffect=$normal.damageEffect
        authoredInputMontageDamageVerified=$true; finalPerSkillHealthDamageCapVerified=$true
        abilityEndOriginVerified=$normal.abilityEndOriginVerified; clientAbilityEndOrigin=$normal.clientAbilityEndOrigin
        exactCapNormalHitVerified=$true; overCapAuthoredHitRejectedVerified=$true
        rejectedTargetServerAndClientHealthUnchangedVerified=$true; rejectedHitAbsentFromAppliedHitsVerified=$true
        serverHitBudgetRefundVerified=$true; realNativeExecutionReceiptVerified=$true; nativeFixtureTargets=$true
        transientDefinitionProfileFixture=$true; loweredDefenseFixture=$true; maliciousWireDamageVerified=$false
        productionNpcLifecycleVerified=$false; shippingRuntimeVerified=$false
        packetLagMinMs=$PacketLagMinMs; packetLagMaxMs=$PacketLagMaxMs; packetLossPercent=$PacketLossPercent
        actualPlayerASCNetRefId=$asc; abilityHandle=$handle; activationPredictionKey=$original
        frozenFinalDamageCap=625; rejectedCalculatedDamage=1250; rejectedTargetNetRefId=$denied
        rejectedTargetHealth=1000000; rejectedGASApplicationCallbacks=1; normalDamageProof=$normal
    }
}
