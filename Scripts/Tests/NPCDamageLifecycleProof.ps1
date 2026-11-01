function Get-NPCDamageLifecycleProof {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $Log)

    $testPath = 'ProjectRPG.NPCDamage.AuthoredDeathAndRespawn'
    $results = [regex]::Matches($Log, '(?m)^(?:\[[^\]\r\n]+\]\[\s*\d+\])?LogAutomationController: Display: Test Completed\. Result=\{([^}]+)\} Name=\{AuthoredDeathAndRespawn\} Path=\{' + [regex]::Escape($testPath) + '\}\r?$')
    if ($results.Count -ne 1 -or $results[0].Groups[1].Value -ne 'Success' -or
        $Log -match 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:|LogScript: Error:|LogScript: Warning:[^\r\n]*(?:Accessed None|Attempted to access|Infinite loop|Array index out of bounds)') {
        throw 'NPC automation must complete successfully exactly once, without native failures.'
    }
    # Automation may reprint captured rows. Only original native LogTemp rows
    # count; controller copies cannot fill in a missing native observation.
    $nativeLog = (($Log -split '\r?\n' | Where-Object { $_ -notmatch 'LogAutomationController:' }) -join "`n")
    $rows = [regex]::Matches($Log,
        '(?m)^(?:\[[^\]\r\n]+\]\[\s*\d+\])?LogTemp: Display: (?<row>RPG_NPC_LIFECYCLE [^\r\n]+)\r?$')
    if ($rows.Count -ne 9 -or [regex]::Matches($nativeLog, 'RPG_NPC_LIFECYCLE ').Count -ne 9) {
        throw 'Exactly nine original native lifecycle rows are required.'
    }
    if ($results[0].Index -le $rows[8].Index) { throw 'Automation success must follow the completed native lifecycle.' }
    $ready = [regex]::Match($rows[0].Groups['row'].Value,
        '^RPG_NPC_LIFECYCLE READY Actor=([1-9]\d*) ASC=([1-9]\d*) Health=(\d+\.\d{6}) Defense=(\d+\.\d{6}) Class=BP_Dummy_Gdn_C$')
    if ($rows[1].Groups['row'].Value -ne 'RPG_NPC_LIFECYCLE DEATH Health=0 TagCount=1 Activations=1 Montage=1') {
        throw 'Actual death, unique tag/ability activation and active montage are required.'
    }
    for ($index = 0; $index -lt 3; ++$index) {
        if ($rows[2 + $index].Groups['row'].Value -ne "RPG_NPC_LIFECYCLE CORPSE_REPLAY Index=$index Rejected=1 Health=0 Activations=1") {
            throw 'Three ordered corpse replays must be rejected without HP/activation changes.'
        }
    }
    if ($rows[5].Groups['row'].Value -ne 'RPG_NPC_LIFECYCLE DEATH_COMPLETED Ended=1 Cancelled=0' -or
        $rows[8].Groups['row'].Value -ne 'RPG_NPC_LIFECYCLE COMPLETE AuthoredNPC=1 AuthoredDeath=1 ServerForcedReplacement=1 Network=0') {
        throw 'Natural death ability completion and explicit standalone forced-replacement scope are required.'
    }
    $stale = [regex]::Match($rows[6].Groups['row'].Value,
        '^RPG_NPC_LIFECYCLE STALE_REJECTED OldActor=([1-9]\d*) NewActor=([1-9]\d*) OldASC=([1-9]\d*) NewASC=([1-9]\d*) Health=(\d+\.\d{6})$')
    $fresh = [regex]::Match($rows[7].Groups['row'].Value,
        '^RPG_NPC_LIFECYCLE FRESH_ACCEPTED Before=(\d+\.\d{6}) After=(\d+\.\d{6}) Damage=(\d+\.\d{6})$')
    if (-not $ready.Success -or -not $stale.Success -or -not $fresh.Success -or
        $ready.Groups[1].Value -ne $stale.Groups[1].Value -or $ready.Groups[2].Value -ne $stale.Groups[3].Value -or
        $stale.Groups[1].Value -eq $stale.Groups[2].Value -or $stale.Groups[3].Value -eq $stale.Groups[4].Value -or
        $stale.Groups[5].Value -ne $fresh.Groups[1].Value) {
        throw 'Malformed or borrowed NPC/ASC identity and replacement health evidence.'
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $health = [decimal]::Parse($ready.Groups[3].Value, $culture)
    $defense = [decimal]::Parse($ready.Groups[4].Value, $culture)
    $before = [decimal]::Parse($fresh.Groups[1].Value, $culture)
    $after = [decimal]::Parse($fresh.Groups[2].Value, $culture)
    $damage = [decimal]::Parse($fresh.Groups[3].Value, $culture)
    if ($health -le 0 -or $defense -le 0 -or $after -le 0 -or $damage -le 0 -or $before - $after -ne $damage) {
        throw 'Positive startup attributes and exact nonlethal replacement HP loss are required.'
    }
    return [ordered]@{
        authoredNPCClass='BP_Dummy_Gdn_C'; authoredDeathAbility='GA_Guardian_Death_C'; authoredDamageEffect='GE_Shared_ApplyDamage_C'
        actualAuthoredNPCDeathVerified=$true; corpseReplayRejections=3; destroyedHitIsolationVerified=$true
        replacementFreshDamageVerified=$true; serverForcedReplacementFixture=$true; nativeSourceFixture=$true; pausedAIFixture=$true
        inPlaceResetVerified=$false; networkReplicationVerified=$false; shippingRuntimeVerified=$false
        observations=[ordered]@{ oldActorId=$ready.Groups[1].Value; oldASCId=$ready.Groups[2].Value
            replacementActorId=$stale.Groups[2].Value; replacementASCId=$stale.Groups[4].Value
            initialHealth=$health; initialDefense=$defense; replacementBefore=$before; replacementAfter=$after; freshDamage=$damage }
    }
}
