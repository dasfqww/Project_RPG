param(
    [string]$BaseUrl = 'http://127.0.0.1:3000',
    [string]$AdminToken = $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    throw 'PROJECT_RPG_BACKEND_ADMIN_TOKEN or -AdminToken is required.'
}

function Invoke-JsonRequest {
    param(
        [ValidateSet('Get', 'Post')]
        [string]$Method,
        [string]$Uri,
        [hashtable]$Headers = @{},
        [object]$Body,
        [int]$ExpectedStatus
    )

    $parameters = @{
        Method = $Method
        Uri = $Uri
        Headers = $Headers
        UseBasicParsing = $true
    }
    if ($null -ne $Body) {
        $parameters.ContentType = 'application/json'
        $parameters.Body = $Body | ConvertTo-Json -Depth 20 -Compress
    }

    $statusCode = 0
    $content = ''
    try {
        $response = Invoke-WebRequest @parameters
        $statusCode = [int]$response.StatusCode
        $content = $response.Content
    }
    catch {
        $errorResponse = $_.Exception.Response
        if ($null -eq $errorResponse) {
            throw
        }

        $statusCode = [int]$errorResponse.StatusCode
        $reader = New-Object System.IO.StreamReader(
            $errorResponse.GetResponseStream())
        try {
            $content = $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }

    if ($statusCode -ne $ExpectedStatus) {
        throw "Expected $Method $Uri to return $ExpectedStatus, got ${statusCode}: $content"
    }

    if ([string]::IsNullOrWhiteSpace($content)) {
        return $null
    }
    return $content | ConvertFrom-Json
}

function New-ItemRecord {
    param(
        [Guid]$ItemId,
        [Guid]$CharacterId,
        [long]$Revision,
        [int]$Quantity,
        [int]$SlotIndex,
        [string]$LifecycleState = 'Active',
        [bool]$IsLocked = $false
    )

    $isTerminal = $LifecycleState -ne 'Active'
    return @{
        definitionType = 'RPGItemDefinition'
        definitionName = 'Smoke.Potion'
        definitionVersion = 1
        owner = @{
            type = 'Character'
            ownerId = $CharacterId.ToString()
        }
        location = @{
            containerType = if ($isTerminal) { 'Terminal' } else { 'Inventory' }
            containerId = if ($isTerminal) { '' } else { $CharacterId.ToString() }
            slotIndex = if ($isTerminal) { -1 } else { $SlotIndex }
        }
        state = @{
            instanceId = $ItemId
            generationSeed = 104729
            quantity = $Quantity
            instanceTags = @('Item.Rarity.Common')
            statValues = @(
                @{
                    statTag = 'Item.Stat.Potency'
                    value = 2.5
                }
            )
        }
        revision = $Revision
        lifecycleState = $LifecycleState
        metadata = @{
            bindState = 'Unbound'
            durability = @{
                current = 0
                maximum = 0
            }
            expiresAtUtc = $null
            creationSource = 'Item.Source.SmokeTest'
            isLocked = $IsLocked
        }
    }
}

function New-CommitRequest {
    param(
        [Guid]$RequestId,
        [Guid]$CharacterId,
        [string]$Operation,
        [string]$Fingerprint,
        [long]$ExpectedRevision,
        [hashtable]$Record,
        [int]$AffectedQuantity
    )

    return @{
        requestId = $RequestId
        operation = $Operation
        commandFingerprint = $Fingerprint
        actor = @{
            type = 'Character'
            ownerId = $CharacterId.ToString()
        }
        affectedQuantity = $AffectedQuantity
        mutations = @(
            @{
                expectedRevision = $ExpectedRevision
                newRecord = $Record
            }
        )
    }
}

$steamId = "76561198$((Get-Random -Minimum 100000000 -Maximum 999999999))"
$auth = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/auth/steam-ticket" `
    -Body @{ ticket = "dev:$steamId" } `
    -ExpectedStatus 200
$playerHeaders = @{ Authorization = "Bearer $($auth.accessToken)" }
$adminHeaders = @{ Authorization = "Bearer $AdminToken" }
$character = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/characters" `
    -Headers $playerHeaders `
    -Body @{ name = "Item$([Guid]::NewGuid().ToString('N').Substring(0, 8))" } `
    -ExpectedStatus 201
$characterId = [Guid]$character.characterId
$serverId = 'item-smoke-server'
$session = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions" `
    -Headers $playerHeaders `
    -Body @{
        characterId = $characterId
        dungeonId = 'Dungeon.ItemSmoke'
        difficulty = 'Normal'
    } `
    -ExpectedStatus 201
$session = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -Body @{
        serverId = $serverId
        serverAddress = '127.0.0.1:7792'
    } `
    -ExpectedStatus 200
if ([string]::IsNullOrWhiteSpace($session.gameServerAccessToken)) {
    throw 'Dungeon activation did not issue a session-scoped game-server token.'
}
$serverHeaders = @{
    Authorization = "Bearer $($session.gameServerAccessToken)"
}
$session = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/start" `
    -Headers $serverHeaders `
    -Body @{ serverId = $serverId } `
    -ExpectedStatus 200
$itemId = [Guid]::NewGuid()

$createRequestId = [Guid]::NewGuid()
$createRecord = New-ItemRecord `
    -ItemId $itemId `
    -CharacterId $characterId `
    -Revision 0 `
    -Quantity 10 `
    -SlotIndex 0
$createBody = New-CommitRequest `
    -RequestId $createRequestId `
    -CharacterId $characterId `
    -Operation 'GrantItem' `
    -Fingerprint "grant|$itemId|10|0" `
    -ExpectedRevision 0 `
    -Record $createRecord `
    -AffectedQuantity 10
$created = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $createBody `
    -ExpectedStatus 200
if ($created.status -ne 'Committed' -or $created.records[0].revision -ne 1) {
    throw 'Initial item commit did not create revision 1.'
}

$replayed = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $createBody `
    -ExpectedStatus 200
if ($replayed.status -ne 'AlreadyCommitted' -or $replayed.records[0].revision -ne 1) {
    throw 'Idempotent retry did not return the original receipt.'
}

$conflictingReplay = $createBody.Clone()
$conflictingReplay.commandFingerprint = "different|$itemId"
$conflict = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $conflictingReplay `
    -ExpectedStatus 409
if ($conflict.status -ne 'IdempotencyConflict') {
    throw 'Request ID reuse was not rejected as an idempotency conflict.'
}

$moveRequestId = [Guid]::NewGuid()
$moveBody = New-CommitRequest `
    -RequestId $moveRequestId `
    -CharacterId $characterId `
    -Operation 'MoveItem' `
    -Fingerprint "move|$itemId|0|1|1" `
    -ExpectedRevision 1 `
    -Record (New-ItemRecord `
        -ItemId $itemId `
        -CharacterId $characterId `
        -Revision 1 `
        -Quantity 10 `
        -SlotIndex 1) `
    -AffectedQuantity 0
$moved = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $moveBody `
    -ExpectedStatus 200
if ($moved.records[0].revision -ne 2 -or $moved.records[0].location.slotIndex -ne 1) {
    throw 'Move commit did not advance the item to revision 2.'
}

$staleBody = New-CommitRequest `
    -RequestId ([Guid]::NewGuid()) `
    -CharacterId $characterId `
    -Operation 'MoveItem' `
    -Fingerprint "stale|$itemId|1|2" `
    -ExpectedRevision 1 `
    -Record (New-ItemRecord `
        -ItemId $itemId `
        -CharacterId $characterId `
        -Revision 1 `
        -Quantity 10 `
        -SlotIndex 2) `
    -AffectedQuantity 0
$stale = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $staleBody `
    -ExpectedStatus 409
if ($stale.status -ne 'RevisionConflict') {
    throw 'A stale expected revision was not rejected.'
}

$activeItems = Invoke-JsonRequest `
    -Method Get `
    -Uri "$BaseUrl/api/items?ownerType=Character&ownerId=$characterId&includeTerminal=false&limit=20" `
    -Headers $playerHeaders `
    -Body $null `
    -ExpectedStatus 200
if ($activeItems.items.Count -ne 1 -or $activeItems.items[0].state.instanceId -ne $itemId) {
    throw 'The character owner projection did not return the committed item.'
}

$secondItemId = [Guid]::NewGuid()
$secondCreateBody = New-CommitRequest `
    -RequestId ([Guid]::NewGuid()) `
    -CharacterId $characterId `
    -Operation 'GrantItem' `
    -Fingerprint "grant|$secondItemId|5|2" `
    -ExpectedRevision 0 `
    -Record (New-ItemRecord `
        -ItemId $secondItemId `
        -CharacterId $characterId `
        -Revision 0 `
        -Quantity 5 `
        -SlotIndex 2) `
    -AffectedQuantity 5
$null = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $secondCreateBody `
    -ExpectedStatus 200

$occupiedBody = New-CommitRequest `
    -RequestId ([Guid]::NewGuid()) `
    -CharacterId $characterId `
    -Operation 'MoveItem' `
    -Fingerprint "occupied|$itemId|2|2" `
    -ExpectedRevision 2 `
    -Record (New-ItemRecord `
        -ItemId $itemId `
        -CharacterId $characterId `
        -Revision 2 `
        -Quantity 10 `
        -SlotIndex 2) `
    -AffectedQuantity 0
$occupied = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $occupiedBody `
    -ExpectedStatus 409
if ($occupied.status -ne 'LocationConflict') {
    throw 'An occupied active location was not rejected.'
}

$unchanged = Invoke-JsonRequest `
    -Method Get `
    -Uri "$BaseUrl/api/items/$itemId" `
    -Headers $playerHeaders `
    -Body $null `
    -ExpectedStatus 200
if ($unchanged.revision -ne 2 -or $unchanged.location.slotIndex -ne 1) {
    throw 'A rejected location conflict partially mutated the item.'
}

$transferRequestId = [Guid]::NewGuid()
$transferBody = @{
    requestId = $transferRequestId
    operation = 'TransferStack'
    commandFingerprint = "transfer|$secondItemId|$itemId|5|1|2"
    actor = @{
        type = 'Character'
        ownerId = $characterId.ToString()
    }
    affectedQuantity = 5
    mutations = @(
        @{
            expectedRevision = 1
            newRecord = (New-ItemRecord `
                -ItemId $secondItemId `
                -CharacterId $characterId `
                -Revision 1 `
                -Quantity 0 `
                -SlotIndex -1 `
                -LifecycleState 'Consumed' `
                -IsLocked $true)
        },
        @{
            expectedRevision = 2
            newRecord = (New-ItemRecord `
                -ItemId $itemId `
                -CharacterId $characterId `
                -Revision 2 `
                -Quantity 15 `
                -SlotIndex 1)
        }
    )
}
$transferred = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $transferBody `
    -ExpectedStatus 200
$sourceResult = $transferred.records |
    Where-Object { $_.state.instanceId -eq $secondItemId }
$destinationResult = $transferred.records |
    Where-Object { $_.state.instanceId -eq $itemId }
if ($sourceResult.revision -ne 2 `
    -or $sourceResult.lifecycleState -ne 'Consumed' `
    -or $destinationResult.revision -ne 3 `
    -or $destinationResult.state.quantity -ne 15) {
    throw 'The two-record stack transfer was not committed atomically.'
}

$consumeBody = New-CommitRequest `
    -RequestId ([Guid]::NewGuid()) `
    -CharacterId $characterId `
    -Operation 'ConsumeItem' `
    -Fingerprint "consume|$itemId|15|3" `
    -ExpectedRevision 3 `
    -Record (New-ItemRecord `
        -ItemId $itemId `
        -CharacterId $characterId `
        -Revision 3 `
        -Quantity 0 `
        -SlotIndex -1 `
        -LifecycleState 'Consumed' `
        -IsLocked $true) `
    -AffectedQuantity 15
$consumed = Invoke-JsonRequest `
    -Method Post `
    -Uri "$BaseUrl/api/item-transactions/commit" `
    -Headers $serverHeaders `
    -Body $consumeBody `
    -ExpectedStatus 200
if ($consumed.records[0].revision -ne 4 -or $consumed.records[0].lifecycleState -ne 'Consumed') {
    throw 'Full consumption did not persist a revisioned terminal tombstone.'
}

$remainingItems = Invoke-JsonRequest `
    -Method Get `
    -Uri "$BaseUrl/api/items?ownerType=Character&ownerId=$characterId&includeTerminal=false&limit=20" `
    -Headers $playerHeaders `
    -Body $null `
    -ExpectedStatus 200
if ($remainingItems.items.Count -ne 0) {
    throw 'A terminal tombstone leaked into the active item projection.'
}

$settlement = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/settle-rewards" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body (@{
        serverId = $serverId
        rewardVersion = 'item_smoke_no_reward'
        changes = @()
    } | ConvertTo-Json -Depth 4 -Compress)
if ($settlement.rewardVersion -ne 'item_smoke_no_reward') {
    throw 'Item smoke dungeon settlement was not queued.'
}

$finished = $null
foreach ($attempt in 1..50) {
    $finished = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)" `
        -Headers $adminHeaders
    if ($finished.state -in @('Cleared', 'Failed')) {
        break
    }
    Start-Sleep -Milliseconds 100
}
if ($finished.state -ne 'Cleared') {
    throw 'Item smoke dungeon did not finish cleanly.'
}

Write-Host 'Item V2 smoke test passed: CAS, idempotency, atomicity, location uniqueness, owner read, and tombstones.'
