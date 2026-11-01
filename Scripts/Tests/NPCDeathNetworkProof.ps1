function Get-NPCDeathNetworkProof {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $ServerText, [Parameter(Mandatory)] [string[]] $ClientTexts)
    if ($ClientTexts.Count -ne 2) { throw 'Exactly two actual NPC client logs are required.' }
    $nativePrefix = '(?m)^(?:\[[^\]\r\n]+\]\[\s*\d+\])?LogTemp: Display: RPG_NPC_NET '
    foreach ($log in (@($ServerText) + @($ClientTexts))) {
        if ($log -match 'RPG_NPC_NET FAILED|RPG_SKILL_NETTEST FAILED|Fatal error:|Assertion failed:|Ensure condition failed:|LogScript: Error:|LogScript: Warning:[^\r\n]*(?:Accessed None|Attempted to access|Infinite loop)') {
            throw 'Native or BP failure during NPC network lifecycle.'
        }
        if ([regex]::Matches($log, 'RPG_NPC_NET ').Count -ne 4) { throw 'Exactly four original lifecycle rows per process are required.' }
    }
    function Read-Row([string] $Log, [string] $Pattern) {
        $matches = [regex]::Matches($Log, $nativePrefix + $Pattern + '\r?$')
        if ($matches.Count -ne 1) { throw "Missing/duplicate/malformed native NPC row: $Pattern" }
        return $matches[0]
    }
    $id = '([1-9]\d*)'; $hp = '(\d+\.\d{6})'
    $ready = Read-Row $ServerText "SERVER_READY Actor=$id ASC=$id Health=$hp"
    $death = Read-Row $ServerText "SERVER_DEATH Actor=$id ASC=$id Health=0 TagCount=1 Activations=1 Montage=AM_Guardian_Death CorpseRejected=3"
    $replacement = Read-Row $ServerText "SERVER_REPLACEMENT OldActor=$id OldASC=$id Actor=$id ASC=$id Health=$hp TagCount=0 StaleRejected=1 Ended=1 Cancelled=0"
    $complete = Read-Row $ServerText "SERVER_COMPLETE Actor=$id ASC=$id Before=$hp After=$hp Damage=10.000000 ForcedReplacement=1 NativeSource=1 PausedAI=1"
    $oldActor=$ready.Groups[1].Value; $oldASC=$ready.Groups[2].Value
    $newActor=$replacement.Groups[3].Value; $newASC=$replacement.Groups[4].Value
    $culture=[Globalization.CultureInfo]::InvariantCulture
    if ($oldActor -ne $death.Groups[1].Value -or $oldASC -ne $death.Groups[2].Value -or
        $oldActor -ne $replacement.Groups[1].Value -or $oldASC -ne $replacement.Groups[2].Value -or
        $newActor -ne $complete.Groups[1].Value -or $newASC -ne $complete.Groups[2].Value -or
        $oldActor -eq $newActor -or $oldASC -eq $newASC -or
        $replacement.Groups[5].Value -ne $complete.Groups[3].Value -or
        [decimal]::Parse($ready.Groups[3].Value,$culture) -le 0 -or
        [decimal]::Parse($complete.Groups[3].Value,$culture)-[decimal]::Parse($complete.Groups[4].Value,$culture) -ne 10 -or
        [decimal]::Parse($complete.Groups[4].Value,$culture) -le 0 -or
        $ready.Index -ge $death.Index -or $death.Index -ge $replacement.Index -or $replacement.Index -ge $complete.Index) {
        throw 'Server NPC identities, HP or lifecycle order disagree.'
    }
    $peers=@()
    foreach ($log in $ClientTexts) {
        $cr=Read-Row $log "CLIENT_READY Peer=$id Actor=$id ASC=$id Health=$hp"
        $cd=Read-Row $log "CLIENT_DEATH Peer=$id Actor=$id ASC=$id Health=0 TagCount=1 HPNotified=1 Montage=AM_Guardian_Death"
        $cn=Read-Row $log "CLIENT_REPLACEMENT Peer=$id OldActor=$id OldASC=$id Actor=$id ASC=$id Health=$hp TagCount=0 OldRemoved=1"
        $cc=Read-Row $log "CLIENT_COMPLETE Peer=$id Actor=$id ASC=$id Before=$hp After=$hp Damage=10.000000"
        $peer=$cr.Groups[1].Value
        if ($peer -in $peers -or $peer -ne $cd.Groups[1].Value -or $peer -ne $cn.Groups[1].Value -or $peer -ne $cc.Groups[1].Value -or
            $cr.Groups[2].Value -ne $oldActor -or $cd.Groups[2].Value -ne $oldActor -or $cn.Groups[2].Value -ne $oldActor -or
            $cr.Groups[3].Value -ne $oldASC -or $cd.Groups[3].Value -ne $oldASC -or $cn.Groups[3].Value -ne $oldASC -or
            $cn.Groups[4].Value -ne $newActor -or $cc.Groups[2].Value -ne $newActor -or
            $cn.Groups[5].Value -ne $newASC -or $cc.Groups[3].Value -ne $newASC -or
            $cr.Groups[4].Value -ne $ready.Groups[3].Value -or $cn.Groups[6].Value -ne $replacement.Groups[5].Value -or
            $cc.Groups[4].Value -ne $complete.Groups[3].Value -or $cc.Groups[5].Value -ne $complete.Groups[4].Value -or
            $cr.Index -ge $cd.Index -or $cd.Index -ge $cn.Index -or $cn.Index -ge $cc.Index) {
            throw 'Client observations are missing, borrowed, reordered or inconsistent with actual server actor/ASC/HP.'
        }
        $peers += $peer
    }
    return [ordered]@{schemaVersion=1;clientCount=2;peers=$peers;oldActorNetId=$oldActor;oldASCNetId=$oldASC
        replacementActorNetId=$newActor;replacementASCNetId=$newASC;authoredNPCClass='BP_Dummy_Gdn_C'
        authoredDeathAbility='GA_Guardian_Death_C';authoredDamageEffect='GE_Shared_ApplyDamage_C'
        deathHealthReplicationVerified=$true;deathTagReplicationVerified=$true;deathMontageReplicationObserved=$true
        oldActorRemovalOnBothClientsVerified=$true;replacementIdentityAndHealthIsolationVerified=$true
        corpseRejectionsOnServer=3;staleHitRejectedOnServer=$true;freshReplacementDamageOnBothClientsVerified=$true
        serverForcedReplacementFixture=$true;nativeSourceFixture=$true;pausedAIFixture=$true;alwaysRelevantFixture=$true
        fixedObservationBarriersFixture=$true;playerAuthoredInputVerified=$false;inPlaceResetVerified=$false
        lootSettlementVerified=$false;shippingRuntimeVerified=$false}
}
