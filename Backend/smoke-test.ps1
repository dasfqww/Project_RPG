param(
    [string]$BaseUrl = 'http://127.0.0.1:3000',
    [string]$AdminToken = $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    throw 'PROJECT_RPG_BACKEND_ADMIN_TOKEN or -AdminToken is required.'
}

function Invoke-ExpectedStatus {
    param(
        [ValidateSet('Get', 'Post', 'Put', 'Delete')]
        [string]$Method,
        [string]$Uri,
        [hashtable]$Headers,
        [string]$Body,
        [int]$ExpectedStatus
    )

    $actualStatus = 0
    try {
        $parameters = @{
            Method = $Method
            Uri = $Uri
            Headers = $Headers
        }
        if (-not [string]::IsNullOrEmpty($Body)) {
            $parameters.ContentType = 'application/json'
            $parameters.Body = $Body
        }
        Invoke-RestMethod @parameters | Out-Null
        $actualStatus = if ($Method -eq 'Post') { 200 } else { 204 }
    }
    catch {
        $actualStatus = [int]$_.Exception.Response.StatusCode
    }

    if ($actualStatus -ne $ExpectedStatus) {
        throw "Expected $Method $Uri to return $ExpectedStatus, got $actualStatus."
    }
    return $actualStatus
}

function New-SmokePlayer {
    param([int]$Index)

    $steamId = "7656119800000000$Index"
    $authBody = @{ ticket = "dev:$steamId" } | ConvertTo-Json -Compress
    $auth = Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/api/auth/steam-ticket" `
        -ContentType 'application/json' `
        -Body $authBody
    $headers = @{ Authorization = "Bearer $($auth.accessToken)" }
    $characterBody = @{
        name = "Smoke$Index$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
    } | ConvertTo-Json -Compress
    $character = Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/api/characters" `
        -Headers $headers `
        -ContentType 'application/json' `
        -Body $characterBody

    return [pscustomobject]@{
        SteamId = $steamId
        Headers = $headers
        Character = $character
    }
}

$players = @(1..5 | ForEach-Object { New-SmokePlayer -Index $_ })
$leader = $players[0]
$serverId = 'smoke-server-a'
$wrongServerId = 'smoke-server-b'
$serverAddress = '127.0.0.1:7777'
$adminHeaders = @{ Authorization = "Bearer $AdminToken" }
$currencyCode = 'Gold.Smoke'

$currencyDefinitionBody = @{
    currencyCode = $currencyCode
    displayName = 'Smoke Gold'
    scope = 'Account'
    maxBalance = 1000000
    enabled = $true
} | ConvertTo-Json -Compress
$playerDefinitionStatus = Invoke-ExpectedStatus `
    -Method Put `
    -Uri "$BaseUrl/api/economy/currency-definitions/$currencyCode" `
    -Headers $leader.Headers `
    -Body $currencyDefinitionBody `
    -ExpectedStatus 403
$currencyDefinition = Invoke-RestMethod `
    -Method Put `
    -Uri "$BaseUrl/api/economy/currency-definitions/$currencyCode" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body $currencyDefinitionBody
if ($currencyDefinition.currencyCode -ne $currencyCode -or
    $currencyDefinition.scope -ne 'Account') {
    throw 'Currency definition was not created with the expected scope.'
}

$characters = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/characters" `
    -Headers $leader.Headers
if (-not ($characters.characterId -contains $leader.Character.characterId)) {
    throw 'Created character was not returned by the character list endpoint.'
}

$createSessionBody = @{
    characterId = $leader.Character.characterId
    dungeonId = 'Dungeon.Smoke.Crypt'
    difficulty = 'Normal'
} | ConvertTo-Json -Compress
$dungeonSession = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions" `
    -Headers $leader.Headers `
    -ContentType 'application/json' `
    -Body $createSessionBody

if ($dungeonSession.state -ne 'Waiting' -or $dungeonSession.members.Count -ne 1) {
    throw 'New dungeon session did not start in Waiting with one member.'
}

$resumedWaitingSession = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/characters/$($leader.Character.characterId)/active-dungeon-session" `
    -Headers $leader.Headers
if ($resumedWaitingSession.dungeonSessionId -ne $dungeonSession.dungeonSessionId) {
    throw 'Active dungeon lookup did not restore the Waiting session.'
}

$otherPlayerResumeStatus = Invoke-ExpectedStatus `
    -Method Get `
    -Uri "$BaseUrl/api/characters/$($leader.Character.characterId)/active-dungeon-session" `
    -Headers $players[1].Headers `
    -ExpectedStatus 403

$duplicateLockStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions" `
    -Headers $leader.Headers `
    -Body $createSessionBody `
    -ExpectedStatus 409

foreach ($player in $players[1..3]) {
    $joinBody = @{
        characterId = $player.Character.characterId
    } | ConvertTo-Json -Compress
    $dungeonSession = Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/members" `
        -Headers $player.Headers `
        -ContentType 'application/json' `
        -Body $joinBody
}

if ($dungeonSession.members.Count -ne 4) {
    throw "Expected four dungeon members, got $($dungeonSession.members.Count)."
}

$fifthJoinBody = @{
    characterId = $players[4].Character.characterId
} | ConvertTo-Json -Compress
$partyFullStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/members" `
    -Headers $players[4].Headers `
    -Body $fifthJoinBody `
    -ExpectedStatus 409

$invalidAddressActivateBody = @{
    serverId = $serverId
    serverAddress = 'missing-port'
} | ConvertTo-Json -Compress
$invalidAddressStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -Body $invalidAddressActivateBody `
    -ExpectedStatus 400

$activateBody = @{
    serverId = $serverId
    serverAddress = $serverAddress
} | ConvertTo-Json -Compress
$dungeonSession = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body $activateBody
if ($dungeonSession.state -ne 'Loading' -or
    $dungeonSession.serverId -ne $serverId -or
    $dungeonSession.serverAddress -ne $serverAddress -or
    [string]::IsNullOrWhiteSpace($dungeonSession.gameServerAccessToken)) {
    throw 'Dungeon session was not assigned to the expected server.'
}
$serverHeaders = @{
    Authorization = "Bearer $($dungeonSession.gameServerAccessToken)"
}

$activateReplay = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body $activateBody
if ($activateReplay.state -ne 'Loading' -or
    $activateReplay.serverAddress -ne $serverAddress -or
    [string]::IsNullOrWhiteSpace($activateReplay.gameServerAccessToken)) {
    throw 'Idempotent dungeon activation did not preserve the assignment.'
}
$serverHeaders = @{
    Authorization = "Bearer $($activateReplay.gameServerAccessToken)"
}

$differentAddressActivateBody = @{
    serverId = $serverId
    serverAddress = '127.0.0.1:7778'
} | ConvertTo-Json -Compress
$differentAddressStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -Body $differentAddressActivateBody `
    -ExpectedStatus 409

$wrongHeartbeatBody = @{ serverId = $wrongServerId } | ConvertTo-Json -Compress
$wrongServerHeartbeatStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/heartbeat" `
    -Headers $serverHeaders `
    -Body $wrongHeartbeatBody `
    -ExpectedStatus 403

$heartbeatBody = @{ serverId = $serverId } | ConvertTo-Json -Compress
$heartbeat = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/heartbeat" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body $heartbeatBody
if ($heartbeat.state -ne 'Loading') {
    throw 'Loading heartbeat changed the dungeon session state unexpectedly.'
}

$joinTicketRequestBody = @{
    characterId = $leader.Character.characterId
    dungeonSessionId = $dungeonSession.dungeonSessionId
} | ConvertTo-Json -Compress
$otherPlayerJoinStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/join-tickets" `
    -Headers $players[1].Headers `
    -Body $joinTicketRequestBody `
    -ExpectedStatus 409

$joinTicket = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/join-tickets" `
    -Headers $leader.Headers `
    -ContentType 'application/json' `
    -Body $joinTicketRequestBody
$wrongConsumeBody = @{
    joinTicket = $joinTicket.joinTicket
    serverId = $wrongServerId
} | ConvertTo-Json -Compress
$wrongServerConsumeStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/join-tickets/consume" `
    -Headers $serverHeaders `
    -Body $wrongConsumeBody `
    -ExpectedStatus 403

$consumeBody = @{
    joinTicket = $joinTicket.joinTicket
    serverId = $serverId
} | ConvertTo-Json -Compress
$playerConsumeStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/join-tickets/consume" `
    -Headers $leader.Headers `
    -Body $consumeBody `
    -ExpectedStatus 403

$admission = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/join-tickets/consume" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body $consumeBody
if ($admission.dungeonSessionId -ne $dungeonSession.dungeonSessionId `
    -or $admission.characterId -ne $leader.Character.characterId `
    -or $admission.steamId -ne $leader.SteamId) {
    throw 'Consumed join ticket did not resolve to the expected session and character.'
}

$replayConsumeStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/join-tickets/consume" `
    -Headers $serverHeaders `
    -Body $consumeBody `
    -ExpectedStatus 401

$start = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/start" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body $heartbeatBody
if ($start.state -ne 'InProgress') {
    throw 'Dungeon session did not enter InProgress.'
}

$inventoryBody = @{
    characterId = $leader.Character.characterId
    inventory = @(
        @{
            item_id = 'Potion.Small'
            quantity = 3
            slot_index = 0
            category = 'Consumable'
            instance_id = "smoke-$([Guid]::NewGuid().ToString('N'))"
        },
        @{
            item_id = 'Sword.Iron'
            quantity = 1
            slot_index = 4
            category = 'Weapon'
            instance_id = "smoke-$([Guid]::NewGuid().ToString('N'))"
        }
    )
} | ConvertTo-Json -Depth 6 -Compress
$playerSaveStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/saveInventory" `
    -Headers $leader.Headers `
    -Body $inventoryBody `
    -ExpectedStatus 403

Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/saveInventory" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body $inventoryBody | Out-Null
$loaded = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/loadInventory?characterId=$($leader.Character.characterId)" `
    -Headers $leader.Headers
if ($loaded.inventory.Count -ne 2) {
    throw "Expected two inventory entries, got $($loaded.inventory.Count)."
}

$rewardRequestId = [Guid]::NewGuid()
$rewardFingerprint = "dungeon:$($dungeonSession.dungeonSessionId):character:$($leader.Character.characterId):reward:v1"
$rewardBody = @{
    requestId = $rewardRequestId
    characterId = $leader.Character.characterId
    dungeonSessionId = $dungeonSession.dungeonSessionId
    operation = 'DungeonReward'
    commandFingerprint = $rewardFingerprint
    reason = 'DungeonClear'
    changes = @(
        @{
            currencyCode = $currencyCode
            delta = 100
        }
    )
} | ConvertTo-Json -Depth 6 -Compress
$playerRewardStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $leader.Headers `
    -Body $rewardBody `
    -ExpectedStatus 403
$rewardCommit = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body $rewardBody
if ($rewardCommit.status -ne 'Committed' -or
    $rewardCommit.changes.Count -ne 1 -or
    $rewardCommit.changes[0].newBalance -ne 100) {
    throw 'Dungeon currency reward was not committed as expected.'
}

$rewardReplay = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body $rewardBody
if ($rewardReplay.status -ne 'AlreadyCommitted' -or
    $rewardReplay.changes[0].newBalance -ne 100) {
    throw 'Dungeon currency reward replay was not idempotent.'
}

$conflictingRewardBody = @{
    requestId = $rewardRequestId
    characterId = $leader.Character.characterId
    dungeonSessionId = $dungeonSession.dungeonSessionId
    operation = 'DungeonReward'
    commandFingerprint = "$rewardFingerprint-conflict"
    reason = 'DungeonClear'
    changes = @(
        @{
            currencyCode = $currencyCode
            delta = 200
        }
    )
} | ConvertTo-Json -Depth 6 -Compress
$rewardConflictStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $serverHeaders `
    -Body $conflictingRewardBody `
    -ExpectedStatus 409

$wallet = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($leader.Character.characterId)" `
    -Headers $leader.Headers
$goldBalances = @($wallet.balances | Where-Object {
    $_.currencyCode -eq $currencyCode
})
if ($goldBalances.Count -ne 1 -or $goldBalances[0].balance -ne 100) {
    throw 'Idempotent reward replay changed the wallet more than once.'
}

$otherWalletStatus = Invoke-ExpectedStatus `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($leader.Character.characterId)" `
    -Headers $players[1].Headers `
    -ExpectedStatus 403

$finishBody = @{
    serverId = $serverId
    outcome = 'Cleared'
} | ConvertTo-Json -Compress
$directClearStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/finish" `
    -Headers $serverHeaders `
    -Body $finishBody `
    -ExpectedStatus 409

$settlementBody = @{
    serverId = $serverId
    rewardVersion = 'smoke_clear_v2'
    changes = @(
        @{
            currencyCode = $currencyCode
            delta = 25
        }
    )
    itemRewards = @(
        @{
            definitionType = 'Consumable'
            definitionName = 'Potion.SmokeReward'
            definitionVersion = 1
            quantity = 2
            bindState = 'CharacterBound'
            durabilityCurrent = 0
            durabilityMaximum = 0
            instanceTags = @('Reward.Dungeon.Smoke')
            statValues = @(
                @{
                    statTag = 'Item.Power'
                    value = 1.5
                }
            )
        }
    )
} | ConvertTo-Json -Depth 6 -Compress
$settlement = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/settle-rewards" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body $settlementBody
if ($settlement.rewardVersion -ne 'smoke_clear_v2' -or
    $settlement.memberCount -lt 1) {
    throw 'Dungeon reward settlement was not queued as expected.'
}

$finished = $null
foreach ($attempt in 1..50) {
    $finished = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)" `
        -Headers $adminHeaders
    if ($finished.state -in @('Cleared', 'Failed')) {
        break
    }
    Start-Sleep -Milliseconds 100
}
if ($finished.state -ne 'Cleared') {
    throw "Dungeon settlement ended in unexpected state '$($finished.state)'."
}

$settledWallet = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($leader.Character.characterId)" `
    -Headers $leader.Headers
$settledGold = @($settledWallet.balances | Where-Object {
    $_.currencyCode -eq $currencyCode
})
if ($settledGold.Count -ne 1 -or $settledGold[0].balance -ne 125) {
    throw 'Persisted dungeon settlement did not update the wallet exactly once.'
}

$settledItemCount = 0
foreach ($player in $players[0..3]) {
    $items = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/items?ownerType=Character&ownerId=$($player.Character.characterId)&includeTerminal=false" `
        -Headers $player.Headers
    $rewardItems = @($items.items | Where-Object {
        $_.definitionName -eq 'Potion.SmokeReward'
    })
    if ($rewardItems.Count -ne 1 -or
        $rewardItems[0].state.quantity -ne 2 -or
        $rewardItems[0].location.containerType -ne 'Mail') {
        throw "Dungeon item reward was not delivered exactly once for '$($player.Character.characterId)'."
    }
    $settledItemCount += $rewardItems.Count
}

$lowerMaximumBody = @{
    currencyCode = $currencyCode
    displayName = 'Smoke Gold'
    scope = 'Account'
    maxBalance = 100
    enabled = $true
} | ConvertTo-Json -Compress
$lowerMaximumStatus = Invoke-ExpectedStatus `
    -Method Put `
    -Uri "$BaseUrl/api/economy/currency-definitions/$currencyCode" `
    -Headers $adminHeaders `
    -Body $lowerMaximumBody `
    -ExpectedStatus 409
$walletAfterRejectedMaximum = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($leader.Character.characterId)" `
    -Headers $leader.Headers
$balanceAfterRejectedMaximum = @(
    $walletAfterRejectedMaximum.balances | Where-Object {
        $_.currencyCode -eq $currencyCode
    })
if ($balanceAfterRejectedMaximum.Count -ne 1 -or
    $balanceAfterRejectedMaximum[0].balance -ne 125 -or
    $balanceAfterRejectedMaximum[0].maxBalance -ne 1000000) {
    throw 'Rejected MaxBalance update corrupted the wallet invariant.'
}

# Force an item delivery-location conflict in a separate session. The worker
# applies currency first internally, so a zero balance afterward proves the
# shared reward transaction rolled the currency write back with the item write.
$rollbackPlayer = $players[4]
$rollbackSessionBody = @{
    characterId = $rollbackPlayer.Character.characterId
    dungeonId = 'Dungeon.Smoke.Rollback'
    difficulty = 'Normal'
} | ConvertTo-Json -Compress
$rollbackSession = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions" `
    -Headers $rollbackPlayer.Headers `
    -ContentType 'application/json' `
    -Body $rollbackSessionBody
$rollbackServerId = 'smoke-rollback-server'
$rollbackActivateBody = @{
    serverId = $rollbackServerId
    serverAddress = '127.0.0.1:7781'
} | ConvertTo-Json -Compress
$rollbackSession = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($rollbackSession.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body $rollbackActivateBody
$rollbackServerHeaders = @{
    Authorization = "Bearer $($rollbackSession.gameServerAccessToken)"
}
$rollbackStartBody = @{ serverId = $rollbackServerId } | ConvertTo-Json -Compress
$rollbackSession = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($rollbackSession.dungeonSessionId)/start" `
    -Headers $rollbackServerHeaders `
    -ContentType 'application/json' `
    -Body $rollbackStartBody

$rollbackCharacterId = [Guid]$rollbackPlayer.Character.characterId
$rollbackDungeonSessionId = [Guid]$rollbackSession.dungeonSessionId
$blockerItemId = [Guid]::NewGuid()
$blockerBody = @{
    requestId = [Guid]::NewGuid()
    operation = 'GrantItem'
    commandFingerprint = "rollback-blocker|$blockerItemId"
    actor = @{
        type = 'Character'
        ownerId = $rollbackCharacterId.ToString('D')
    }
    affectedQuantity = 1
    mutations = @(
        @{
            expectedRevision = 0
            newRecord = @{
                definitionType = 'System'
                definitionName = 'Settlement.Blocker'
                definitionVersion = 1
                owner = @{
                    type = 'Character'
                    ownerId = $rollbackCharacterId.ToString('D')
                }
                location = @{
                    containerType = 'Mail'
                    containerId = "DungeonReward.$($rollbackDungeonSessionId.ToString('N'))"
                    slotIndex = 0
                }
                state = @{
                    instanceId = $blockerItemId
                    generationSeed = 1
                    quantity = 1
                    instanceTags = @()
                    statValues = @()
                }
                revision = 0
                lifecycleState = 'Active'
                metadata = @{
                    bindState = 'CharacterBound'
                    durability = @{ current = 0; maximum = 0 }
                    expiresAtUtc = $null
                    creationSource = 'Smoke.RollbackBlocker'
                    isLocked = $false
                }
            }
        }
    )
} | ConvertTo-Json -Depth 20 -Compress
$blockerCommit = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $rollbackServerHeaders `
    -ContentType 'application/json' `
    -Body $blockerBody
if ($blockerCommit.status -ne 'Committed') {
    throw 'Failed to create the deterministic settlement rollback blocker.'
}

$rollbackSettlementBody = @{
    serverId = $rollbackServerId
    rewardVersion = 'rollback_v1'
    changes = @(
        @{ currencyCode = $currencyCode; delta = 10 }
    )
    itemRewards = @(
        @{
            definitionType = 'Consumable'
            definitionName = 'Potion.ShouldRollback'
            definitionVersion = 1
            quantity = 1
            bindState = 'CharacterBound'
            durabilityCurrent = 0
            durabilityMaximum = 0
            instanceTags = @()
            statValues = @()
        }
    )
} | ConvertTo-Json -Depth 10 -Compress
Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($rollbackSession.dungeonSessionId)/settle-rewards" `
    -Headers $rollbackServerHeaders `
    -ContentType 'application/json' `
    -Body $rollbackSettlementBody | Out-Null

$rollbackFinished = $null
foreach ($attempt in 1..50) {
    $rollbackFinished = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/dungeon-sessions/$($rollbackSession.dungeonSessionId)" `
        -Headers $adminHeaders
    if ($rollbackFinished.state -in @('Cleared', 'Failed')) {
        break
    }
    Start-Sleep -Milliseconds 100
}
if ($rollbackFinished.state -ne 'Failed') {
    throw "Conflicting item settlement ended in '$($rollbackFinished.state)' instead of Failed."
}
$rollbackWallet = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($rollbackPlayer.Character.characterId)" `
    -Headers $rollbackPlayer.Headers
$rollbackGold = @($rollbackWallet.balances | Where-Object {
    $_.currencyCode -eq $currencyCode
})
if ($rollbackGold.Count -ne 1 -or $rollbackGold[0].balance -ne 0) {
    throw 'Failed item settlement did not atomically roll back its currency reward.'
}

$expiredServerReplayStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($dungeonSession.dungeonSessionId)/settle-rewards" `
    -Headers $serverHeaders `
    -Body $settlementBody `
    -ExpectedStatus 401
$finishReplay = $finished

$noActiveSessionResponse = Invoke-WebRequest `
    -UseBasicParsing `
    -Method Get `
    -Uri "$BaseUrl/api/characters/$($leader.Character.characterId)/active-dungeon-session" `
    -Headers $leader.Headers
if ([int]$noActiveSessionResponse.StatusCode -ne 204) {
    throw 'Completed dungeon was still returned as an active session.'
}

$nextSession = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions" `
    -Headers $leader.Headers `
    -ContentType 'application/json' `
    -Body $createSessionBody
if ($nextSession.state -ne 'Waiting') {
    throw 'Character lease was not released after dungeon completion.'
}


$resumedNextSession = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/characters/$($leader.Character.characterId)/active-dungeon-session" `
    -Headers $leader.Headers
if ($resumedNextSession.dungeonSessionId -ne $nextSession.dungeonSessionId) {
    throw 'Active dungeon lookup did not restore the replacement session.'
}

$claimServerId = 'smoke-claim-server'
$claimServerAddress = '127.0.0.1:7780'
$claimBody = @{
    serverId = $claimServerId
    serverAddress = $claimServerAddress
} | ConvertTo-Json -Compress
$playerClaimStatus = Invoke-ExpectedStatus `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/claim" `
    -Headers $leader.Headers `
    -Body $claimBody `
    -ExpectedStatus 403
$claimedSession = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/claim" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body $claimBody
if ($claimedSession.dungeonSessionId -ne $nextSession.dungeonSessionId -or
    $claimedSession.state -ne 'Loading' -or
    $claimedSession.serverId -ne $claimServerId -or
    $claimedSession.serverAddress -ne $claimServerAddress) {
    throw 'Allocator claim did not atomically assign the waiting session.'
}

$emptyClaimResponse = Invoke-WebRequest `
    -UseBasicParsing `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/claim" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body $claimBody
if ([int]$emptyClaimResponse.StatusCode -ne 204) {
    throw 'Empty allocator claim did not return 204.'
}

[pscustomobject]@{
    SteamId = $leader.SteamId
    CharacterId = $leader.Character.characterId
    DungeonSessionId = $dungeonSession.dungeonSessionId
    PartySize = $dungeonSession.members.Count
    DuplicateLockStatus = $duplicateLockStatus
    PartyFullStatus = $partyFullStatus
    InvalidAddressStatus = $invalidAddressStatus
    DifferentAddressStatus = $differentAddressStatus
    WrongHeartbeatStatus = $wrongServerHeartbeatStatus
    WrongServerConsumeStatus = $wrongServerConsumeStatus
    PlayerConsumeStatus = $playerConsumeStatus
    ReplayConsumeStatus = $replayConsumeStatus
    PlayerSaveStatus = $playerSaveStatus
    LoadedCount = $loaded.inventory.Count
    FinishedState = $finished.state
    FinishReplayState = $finishReplay.state
    WaitingResumeState = $resumedWaitingSession.state
    NoActiveResumeStatus = [int]$noActiveSessionResponse.StatusCode
    ReplacementResumeState = $resumedNextSession.state
    CurrencyDefinition = $currencyDefinition.currencyCode
    PlayerDefinitionStatus = $playerDefinitionStatus
    PlayerRewardStatus = $playerRewardStatus
    RewardCommitStatus = $rewardCommit.status
    RewardReplayStatus = $rewardReplay.status
    RewardConflictStatus = $rewardConflictStatus
    WalletGoldBalance = $goldBalances[0].balance
    SettledItemCount = $settledItemCount
    RollbackState = $rollbackFinished.state
    RollbackGoldBalance = $rollbackGold[0].balance
    OtherWalletStatus = $otherWalletStatus
    LeaseReleased = ($nextSession.state -eq 'Waiting')
    PlayerClaimStatus = $playerClaimStatus
    ClaimedSessionId = $claimedSession.dungeonSessionId
    EmptyClaimStatus = [int]$emptyClaimResponse.StatusCode
}
