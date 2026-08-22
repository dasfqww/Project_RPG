param(
    [string]$BaseUrl = 'http://127.0.0.1:3000',
    [string]$AdminToken = $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    throw 'PROJECT_RPG_BACKEND_ADMIN_TOKEN or -AdminToken is required.'
}

function Invoke-ExpectedError {
    param(
        [ValidateSet('Get', 'Post', 'Put')]
        [string]$Method,
        [string]$Uri,
        [hashtable]$Headers,
        [object]$Body,
        [int]$ExpectedStatus
    )

    try {
        $parameters = @{
            Method = $Method
            Uri = $Uri
            Headers = $Headers
        }
        if ($null -ne $Body) {
            $parameters.ContentType = 'application/json'
            $parameters.Body = $Body | ConvertTo-Json -Depth 8 -Compress
        }
        Invoke-RestMethod @parameters | Out-Null
        throw "Expected $Method $Uri to return $ExpectedStatus."
    }
    catch {
        if ($_.Exception.Response -and
            [int]$_.Exception.Response.StatusCode -eq $ExpectedStatus) {
            return
        }
        throw
    }
}

function New-SmokeAccount {
    param(
        [string]$SteamId,
        [string]$Prefix
    )

    $auth = Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/api/auth/steam-ticket" `
        -ContentType 'application/json' `
        -Body (@{ ticket = "dev:$SteamId" } | ConvertTo-Json -Compress)
    $headers = @{ Authorization = "Bearer $($auth.accessToken)" }
    $character = Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/api/characters" `
        -Headers $headers `
        -ContentType 'application/json' `
        -Body (@{
            name = "$Prefix$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
        } | ConvertTo-Json -Compress)
    return [pscustomobject]@{
        Headers = $headers
        Character = $character
    }
}

function New-Character {
    param(
        [hashtable]$Headers,
        [string]$Prefix
    )

    return Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/api/characters" `
        -Headers $Headers `
        -ContentType 'application/json' `
        -Body (@{
            name = "$Prefix$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
        } | ConvertTo-Json -Compress)
}

function Set-CurrencyDefinition {
    param(
        [string]$Code,
        [string]$Scope
    )

    return Invoke-RestMethod `
        -Method Put `
        -Uri "$BaseUrl/api/economy/currency-definitions/$Code" `
        -Headers $adminHeaders `
        -ContentType 'application/json' `
        -Body (@{
            currencyCode = $Code
            displayName = $Code
            scope = $Scope
            maxBalance = 1000000
            enabled = $true
        } | ConvertTo-Json -Compress)
}

function Get-Balance {
    param(
        [object]$Wallet,
        [string]$Code
    )

    return ($Wallet.balances | Where-Object { $_.currencyCode -eq $Code }).balance
}

$adminHeaders = @{ Authorization = "Bearer $AdminToken" }
$accountA = New-SmokeAccount `
    -SteamId '76561198000000991' `
    -Prefix 'EcoA'
$characterA2 = New-Character -Headers $accountA.Headers -Prefix 'EcoB'
$accountB = New-SmokeAccount `
    -SteamId '76561198000000992' `
    -Prefix 'EcoC'

if ($accountA.Character.rosterId -ne $characterA2.rosterId) {
    throw 'Characters from one account were assigned to different rosters.'
}
if ($accountA.Character.rosterId -eq $accountB.Character.rosterId) {
    throw 'Different accounts were assigned to the same roster.'
}

$null = Set-CurrencyDefinition -Code 'AccountCredit' -Scope 'Account'
$null = Set-CurrencyDefinition -Code 'RosterGold' -Scope 'Roster'
$null = Set-CurrencyDefinition -Code 'CharacterToken' -Scope 'Character'

$serverId = 'economy-smoke-server'
$session = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions" `
    -Headers $accountA.Headers `
    -ContentType 'application/json' `
    -Body (@{
        characterId = $accountA.Character.characterId
        dungeonId = 'Dungeon.EconomySmoke'
        difficulty = 'Normal'
    } | ConvertTo-Json -Compress)
$session = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body (@{
        serverId = $serverId
        serverAddress = '127.0.0.1:7791'
    } | ConvertTo-Json -Compress)
if ([string]::IsNullOrWhiteSpace($session.gameServerAccessToken)) {
    throw 'Dungeon activation did not issue a session-scoped game-server token.'
}
$serverHeaders = @{
    Authorization = "Bearer $($session.gameServerAccessToken)"
}
$session = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/start" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body (@{ serverId = $serverId } | ConvertTo-Json -Compress)

$initialWallet = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($accountA.Character.characterId)" `
    -Headers $accountA.Headers
if ($initialWallet.rosterId -ne $accountA.Character.rosterId -or
    (Get-Balance $initialWallet 'AccountCredit') -ne 0 -or
    (Get-Balance $initialWallet 'RosterGold') -ne 0 -or
    (Get-Balance $initialWallet 'CharacterToken') -ne 0) {
    throw 'A new wallet did not expose zero balances for every ownership scope.'
}

$requestId = [Guid]::NewGuid()
$commitBody = @{
    requestId = $requestId
    characterId = $accountA.Character.characterId
    dungeonSessionId = $session.dungeonSessionId
    operation = 'SmokeReward'
    commandFingerprint = "smoke-reward|$requestId"
    reason = 'SmokeTest.Reward'
    changes = @(
        @{ currencyCode = 'AccountCredit'; delta = 100 },
        @{ currencyCode = 'RosterGold'; delta = 200 },
        @{ currencyCode = 'CharacterToken'; delta = 5 }
    )
}
$committed = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body ($commitBody | ConvertTo-Json -Depth 8 -Compress)
if ($committed.status -ne 'Committed' -or $committed.changes.Count -ne 3) {
    throw 'The multi-currency transaction was not committed.'
}

$replayed = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body ($commitBody | ConvertTo-Json -Depth 8 -Compress)
if ($replayed.status -ne 'AlreadyCommitted') {
    throw 'An idempotent replay did not return the original receipt.'
}

$conflictingBody = $commitBody.Clone()
$conflictingBody.commandFingerprint = "conflict|$requestId"
Invoke-ExpectedError `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $serverHeaders `
    -Body $conflictingBody `
    -ExpectedStatus 409

$sharedWallet = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($characterA2.characterId)" `
    -Headers $accountA.Headers
if ((Get-Balance $sharedWallet 'AccountCredit') -ne 100 -or
    (Get-Balance $sharedWallet 'RosterGold') -ne 200 -or
    (Get-Balance $sharedWallet 'CharacterToken') -ne 0) {
    throw 'Account/roster balances were not shared independently of character balances.'
}

$insufficientBody = @{
    requestId = [Guid]::NewGuid()
    characterId = $accountA.Character.characterId
    dungeonSessionId = $session.dungeonSessionId
    operation = 'SmokeSpend'
    commandFingerprint = "smoke-spend|$([Guid]::NewGuid())"
    reason = 'SmokeTest.Spend'
    changes = @(
        @{ currencyCode = 'RosterGold'; delta = 50 },
        @{ currencyCode = 'CharacterToken'; delta = -999 }
    )
}
Invoke-ExpectedError `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $serverHeaders `
    -Body $insufficientBody `
    -ExpectedStatus 409

$unchangedWallet = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($accountA.Character.characterId)" `
    -Headers $accountA.Headers
if ((Get-Balance $unchangedWallet 'RosterGold') -ne 200 -or
    (Get-Balance $unchangedWallet 'CharacterToken') -ne 5) {
    throw 'A rejected batch partially changed a balance.'
}

Invoke-ExpectedError `
    -Method Put `
    -Uri "$BaseUrl/api/economy/currency-definitions/AccountCredit" `
    -Headers $adminHeaders `
    -Body @{
        currencyCode = 'AccountCredit'
        displayName = 'AccountCredit'
        scope = 'Account'
        maxBalance = 99
        enabled = $true
    } `
    -ExpectedStatus 409

Invoke-ExpectedError `
    -Method Post `
    -Uri "$BaseUrl/api/economy/transactions/commit" `
    -Headers $accountA.Headers `
    -Body $commitBody `
    -ExpectedStatus 403
Invoke-ExpectedError `
    -Method Get `
    -Uri "$BaseUrl/api/economy/wallets/$($accountB.Character.characterId)" `
    -Headers $accountA.Headers `
    -Body $null `
    -ExpectedStatus 403

$finished = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/finish" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body (@{
        serverId = $serverId
        outcome = 'Failed'
    } | ConvertTo-Json -Compress)
if ($finished.state -ne 'Failed') {
    throw 'Economy smoke dungeon did not finish cleanly.'
}

Write-Host 'Economy smoke test passed: roster ownership, scoped wallets, atomicity, authorization, and idempotency.'
