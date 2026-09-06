param(
    [string]$BaseUrl = 'http://127.0.0.1:3000',
    [string]$AdminToken = $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN,
    [string]$RunId = [Guid]::NewGuid().ToString('N').Substring(0, 12)
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    throw 'PROJECT_RPG_BACKEND_ADMIN_TOKEN or -AdminToken is required.'
}
if ($RunId -notmatch '^[a-fA-F0-9]{8,24}$') {
    throw 'RunId must contain 8 to 24 hexadecimal characters.'
}

$BaseUrl = $BaseUrl.TrimEnd('/')
$RunId = $RunId.ToLowerInvariant()
$runNumber = [Convert]::ToUInt32($RunId.Substring(0, 8), 16)
$steamIdBase = [uint64]76561280000000000 + ([uint64]$runNumber * 10)
$seedScript = Join-Path $PSScriptRoot 'seed-game-economy.ps1'

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

function New-RewardItem {
    param(
        [string]$DefinitionName,
        [int]$Quantity
    )

    return @{
        definitionType = 'RPGItemDefinition'
        definitionName = $DefinitionName
        definitionVersion = 1
        quantity = $Quantity
        bindState = 'Unbound'
        durabilityCurrent = 0
        durabilityMaximum = 0
        instanceTags = @()
        statValues = @()
    }
}

$rewardSpecs = @(
    [pscustomobject]@{
        Difficulty = 'Easy'
        RewardVersion = 'pve_easy_v1'
        Gold = 100
        PotionQuantity = 1
        IncludeHelm = $false
    },
    [pscustomobject]@{
        Difficulty = 'Normal'
        RewardVersion = 'pve_normal_v1'
        Gold = 250
        PotionQuantity = 2
        IncludeHelm = $false
    },
    [pscustomobject]@{
        Difficulty = 'Hard'
        RewardVersion = 'pve_hard_v1'
        Gold = 500
        PotionQuantity = 3
        IncludeHelm = $true
    },
    [pscustomobject]@{
        Difficulty = 'Hell'
        RewardVersion = 'pve_hell_v1'
        Gold = 1000
        PotionQuantity = 5
        IncludeHelm = $true
    }
)

$adminHeaders = @{ Authorization = "Bearer $AdminToken" }
& $seedScript -BaseUrl $BaseUrl -AdminToken $AdminToken | Out-Null

$definitions = Invoke-JsonRequest `
    -Method Get `
    -Uri "$BaseUrl/api/economy/currency-definitions" `
    -Headers $adminHeaders `
    -Body $null `
    -ExpectedStatus 200
$rosterGold = @($definitions.definitions | Where-Object {
    $_.currencyCode -eq 'RosterGold'
})
if ($rosterGold.Count -ne 1 -or
    $rosterGold[0].scope -ne 'Roster' -or
    $rosterGold[0].maxBalance -ne 1000000000 -or
    -not $rosterGold[0].enabled) {
    throw 'RosterGold was not seeded with the expected production definition.'
}

$results = @()
for ($index = 0; $index -lt $rewardSpecs.Count; $index++) {
    $spec = $rewardSpecs[$index]
    $steamId = ($steamIdBase + [uint64]$index + 1).ToString()
    $auth = Invoke-JsonRequest `
        -Method Post `
        -Uri "$BaseUrl/api/auth/steam-ticket" `
        -Body @{ ticket = "dev:$steamId" } `
        -ExpectedStatus 200
    $playerHeaders = @{ Authorization = "Bearer $($auth.accessToken)" }
    $character = Invoke-JsonRequest `
        -Method Post `
        -Uri "$BaseUrl/api/characters" `
        -Headers $playerHeaders `
        -Body @{
            name = "R$index$($RunId.Substring(0, 8))"
        } `
        -ExpectedStatus 201
    $characterId = [Guid]$character.characterId
    $serverId = "reward-$($spec.Difficulty.ToLowerInvariant())-$RunId"
    $session = Invoke-JsonRequest `
        -Method Post `
        -Uri "$BaseUrl/api/dungeon-sessions" `
        -Headers $playerHeaders `
        -Body @{
            characterId = $characterId
            dungeonId = 'Dungeon.PvE.ContentValidation'
            difficulty = $spec.Difficulty
        } `
        -ExpectedStatus 201
    $session = Invoke-JsonRequest `
        -Method Post `
        -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/activate" `
        -Headers $adminHeaders `
        -Body @{
            serverId = $serverId
            serverAddress = "127.0.0.1:$([int]7794 + $index)"
        } `
        -ExpectedStatus 200
    if ([string]::IsNullOrWhiteSpace($session.gameServerAccessToken)) {
        throw "Dungeon activation did not issue a token for '$($spec.Difficulty)'."
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

    $itemRewards = @(
        New-RewardItem `
            -DefinitionName 'GameItem.Consume.Potion.Red.Large' `
            -Quantity $spec.PotionQuantity
    )
    if ($spec.IncludeHelm) {
        $itemRewards += New-RewardItem `
            -DefinitionName 'GameItem.Equipment.Helm.Default' `
            -Quantity 1
    }

    $settlement = Invoke-JsonRequest `
        -Method Post `
        -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/settle-rewards" `
        -Headers $serverHeaders `
        -Body @{
            serverId = $serverId
            rewardVersion = $spec.RewardVersion
            changes = @(
                @{
                    currencyCode = 'RosterGold'
                    delta = $spec.Gold
                }
            )
            itemRewards = $itemRewards
        } `
        -ExpectedStatus 202
    if ($settlement.rewardVersion -ne $spec.RewardVersion -or
        $settlement.memberCount -ne 1) {
        throw "Unexpected settlement receipt for '$($spec.Difficulty)'."
    }

    $finished = $null
    foreach ($attempt in 1..50) {
        $finished = Invoke-JsonRequest `
            -Method Get `
            -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)" `
            -Headers $adminHeaders `
            -Body $null `
            -ExpectedStatus 200
        if ($finished.state -in @('Cleared', 'Failed')) {
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if ($finished.state -ne 'Cleared') {
        throw "Configured '$($spec.Difficulty)' settlement ended in '$($finished.state)'."
    }

    $wallet = Invoke-JsonRequest `
        -Method Get `
        -Uri "$BaseUrl/api/economy/wallets/$characterId" `
        -Headers $playerHeaders `
        -Body $null `
        -ExpectedStatus 200
    $goldBalance = @($wallet.balances | Where-Object {
        $_.currencyCode -eq 'RosterGold'
    })
    if ($goldBalance.Count -ne 1 -or
        $goldBalance[0].balance -ne $spec.Gold) {
        throw "RosterGold did not settle exactly once for '$($spec.Difficulty)'."
    }

    $items = Invoke-JsonRequest `
        -Method Get `
        -Uri "$BaseUrl/api/items?ownerType=Character&ownerId=$characterId&includeTerminal=false" `
        -Headers $playerHeaders `
        -Body $null `
        -ExpectedStatus 200
    $potions = @($items.items | Where-Object {
        $_.definitionName -eq 'GameItem.Consume.Potion.Red.Large'
    })
    if ($potions.Count -ne 1 -or
        $potions[0].state.quantity -ne $spec.PotionQuantity -or
        $potions[0].location.containerType -ne 'Mail') {
        throw "Potion reward was not delivered correctly for '$($spec.Difficulty)'."
    }
    $helms = @($items.items | Where-Object {
        $_.definitionName -eq 'GameItem.Equipment.Helm.Default'
    })
    $expectedHelmCount = if ($spec.IncludeHelm) { 1 } else { 0 }
    if ($helms.Count -ne $expectedHelmCount -or
        ($expectedHelmCount -eq 1 -and
            ($helms[0].state.quantity -ne 1 -or
             $helms[0].location.containerType -ne 'Mail'))) {
        throw "Helm reward was not delivered correctly for '$($spec.Difficulty)'."
    }

    $results += [pscustomobject]@{
        RunId = $RunId
        SteamId = $steamId
        CharacterId = $characterId
        DungeonSessionId = [Guid]$session.dungeonSessionId
        Difficulty = $spec.Difficulty
        RewardVersion = $spec.RewardVersion
        GoldBalance = [long]$goldBalance[0].balance
        PotionQuantity = [int]$potions[0].state.quantity
        HelmQuantity = if ($expectedHelmCount -eq 1) {
            [int]$helms[0].state.quantity
        }
        else {
            0
        }
        DungeonState = $finished.state
    }
}

$results
