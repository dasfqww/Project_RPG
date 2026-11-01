[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{32}$')][string]$RunId)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$runRoot=Join-Path $projectRoot "Saved\Tests\DungeonMembershipBackend\$RunId"
$attemptPath=Join-Path $runRoot 'Attempt.proof.json'
$failurePath=Join-Path $runRoot 'Failed.proof.json'
$attempt=Get-Content -LiteralPath $attemptPath -Raw -Encoding UTF8 | ConvertFrom-Json
$failure=Get-Content -LiteralPath $failurePath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($attempt.runId -cne $RunId -or $failure.runId -cne $RunId -or -not $attempt.unexpectedDisconnect -or
    $failure.passed -or $failure.failure -cne 'Abandoned character received rewards.') {
    throw 'This read-only review handles only the preserved legacy starter-equipment assertion failure.'
}
$schema="rpg_membership_e2e_$RunId"
if ($attempt.postgresSchema -cne $schema -or $failure.postgresSchema -cne $schema) { throw 'Owned schema mismatch.' }
. (Join-Path $PSScriptRoot 'DungeonAbandonmentSettlementProof.ps1')
$serverLog=Join-Path $runRoot 'server.log'
$clientLogs=@(1..$attempt.clients | ForEach-Object { Join-Path $runRoot "client-$_.log" })
$network=Get-DungeonAbandonmentSettlementProof -ServerText (Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8) `
    -ClientTexts @($clientLogs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -Encoding UTF8 }) -ClientCount $attempt.clients -UnexpectedDisconnect
$session=$network.backendSessionId
$target=$network.targetCharacterId
$remaining=@($network.backendPeers | Where-Object characterId -ne $target | ForEach-Object characterId | Sort-Object)
$compose=Join-Path $projectRoot 'Backend\compose.postgres.yml'
# Only SELECT on the exact GUID-owned schema; no service/process/clock/row mutation.
$sql="SELECT json_build_object('session',(SELECT row_to_json(d) FROM (SELECT state,server_id FROM $schema.dungeon_sessions WHERE dungeon_session_id='$session') d),'receipts',(SELECT json_agg(row_to_json(r)) FROM $schema.dungeon_session_disconnection_expulsions r WHERE dungeon_session_id='$session'),'members',(SELECT json_agg(character_id ORDER BY character_id) FROM $schema.dungeon_session_members WHERE dungeon_session_id='$session'),'abandonments',(SELECT COUNT(*) FROM $schema.dungeon_session_abandonments WHERE dungeon_session_id='$session'),'tickets',(SELECT COUNT(*) FROM $schema.game_join_tickets WHERE dungeon_session_id='$session'),'settlement',(SELECT row_to_json(s) FROM (SELECT state,character_ids FROM $schema.dungeon_reward_settlements WHERE dungeon_session_id='$session') s),'items',(SELECT json_agg(row_to_json(i)) FROM (SELECT item_id,owner_id,definition_name,container_type,slot_index,quantity,revision,lifecycle_state,creation_source,created_at,updated_at FROM $schema.item_records WHERE owner_type='Character') i),'wallets',(SELECT json_agg(json_build_object('characterId',c.character_id,'gold',COALESCE(b.balance,0))) FROM $schema.characters c LEFT JOIN $schema.currency_balances b ON b.owner_type='Roster' AND b.owner_id=c.roster_id::text AND b.currency_code='RosterGold'));"
$rows=& docker compose -f $compose exec -T postgres psql -U project_rpg -d project_rpg -v ON_ERROR_STOP=1 -t -A -c $sql
if ($LASTEXITCODE -ne 0) { throw 'Read-only evidence query failed.' }
$db=$rows | ConvertFrom-Json
$receipts=@($db.receipts | Where-Object { $null -ne $_ })
$targetPeer=@($network.backendPeers | Where-Object characterId -eq $target)[0]
if ($db.session.state -cne 'Cleared' -or $db.session.server_id -cne $network.backendServerId -or
    $db.abandonments -ne 0 -or $db.tickets -ne 0 -or $receipts.Count -ne 1 -or
    $receipts[0].character_id -cne $target -or $receipts[0].steam_id -cne $targetPeer.steamId -or
    (Compare-Object $remaining @($db.members | Sort-Object)) -or $db.settlement.state -cne 'Completed' -or
    (Compare-Object $remaining @($db.settlement.character_ids | Sort-Object))) { throw 'Wrong durable terminal membership, expulsion, or settlement.' }
$lost=[DateTimeOffset]::Parse($receipts[0].disconnected_at)
$expiry=[DateTimeOffset]::Parse($receipts[0].expired_at)
$processed=[DateTimeOffset]::Parse($receipts[0].processed_at)
if ($expiry -ne $lost.AddMinutes(10) -or $processed -lt $expiry) { throw 'Early or inconsistent grace expiry.' }
foreach ($peer in $network.backendPeers) {
    $items=@($db.items | Where-Object owner_id -eq $peer.characterId)
    $sword=@($items | Where-Object { $_.definition_name -ceq 'GameItem.Equipment.Weapon.GreatSword' -and
        $_.container_type -ceq 'Equipment' -and $_.slot_index -eq 9 -and $_.quantity -eq 1 -and
        $_.revision -eq 1 -and $_.lifecycle_state -ceq 'Active' -and $_.creation_source -ceq 'CharacterCreation' -and
        [DateTimeOffset]::Parse($_.created_at) -lt $lost -and
        [DateTimeOffset]::Parse($_.created_at) -eq [DateTimeOffset]::Parse($_.updated_at) })
    if ($sword.Count -ne 1) { throw 'Missing unchanged pre-disconnect character-creation sword.' }
    $wallet=@($db.wallets | Where-Object characterId -eq $peer.characterId)
    $excluded=$peer.characterId -eq $target
    if ($wallet.Count -ne 1 -or $wallet[0].gold -ne $(if ($excluded) {0} else {500}) -or
        $items.Count -ne $(if ($excluded) {1} else {3})) { throw 'Wrong reward exclusion or total items including starter equipment.' }
    if (-not $excluded) {
        $grants=@($items | Where-Object item_id -ne $sword[0].item_id)
        if (@($grants | Where-Object { $_.container_type -cne 'Inventory' -or $_.revision -ne 1 -or
            $_.lifecycle_state -cne 'Active' -or [DateTimeOffset]::Parse($_.created_at) -lt $expiry }).Count -or
            @($grants | Where-Object { $_.definition_name -ceq 'GameItem.Consume.Potion.Red.Large' -and $_.quantity -eq 3 }).Count -ne 1 -or
            @($grants | Where-Object { $_.definition_name -ceq 'GameItem.Equipment.Helm.Default' -and $_.quantity -eq 1 }).Count -ne 1) {
            throw 'Wrong durable survivor inventory grants.'
        }
    }
}
$inputDrift=@($attempt.sourceAndRunnerHashes | Where-Object { (Get-FileHash -LiteralPath $_.path).Hash -cne $_.SHA256 } | ForEach-Object path)
$moduleMatches=(Get-FileHash -LiteralPath (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll')).Hash -ceq $attempt.moduleSHA256
$logs=@($serverLog)+$clientLogs+@(Join-Path $runRoot 'backend.stdout.log'; Join-Path $runRoot 'backend.stderr.log')
$analysis=[ordered]@{schemaVersion=1;analysisPassed=$true;scope='Read-only post-run logs/SQL review, not a rerun or promotion of the failed original runner';
    runId=$RunId;originalRunnerPassed=$false;runnerRerun=$false;scenarioRepeated=$false;originalFailurePreserved=$true;
    freshBaselineSnapshotAvailable=$false;preDisconnectStarterCreationAndUnchangedRevisionVerified=$true;
    initialGraceSamplesNotPersistedByOriginalFailedRunner=$true;analysedAtUtc=[DateTimeOffset]::UtcNow.ToString('o');
    network=$network;durable=$db;originalModuleSHA256=$attempt.moduleSHA256;moduleStillMatches=$moduleMatches;
    changedInputsSinceOriginalRun=$inputDrift;originalSourceHashes=$attempt.sourceAndRunnerHashes;
    closedLogHashes=@($logs | ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} });
    analyserSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash;proofParserSHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'DungeonAbandonmentSettlementProof.ps1')).Hash}
$output=Join-Path $runRoot 'ReadOnlyReview.proof.json'
$analysis | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $output -Encoding UTF8
Write-Host "PASS: read-only post-run logs/SQL review; original runner remains failed and was not repeated: $output"
