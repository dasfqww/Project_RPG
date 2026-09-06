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
$steamId = ([uint64]76561300000000000 + [uint64]$runNumber).ToString()
$adminHeaders = @{ Authorization = "Bearer $AdminToken" }

function Invoke-ExpectedError {
    param(
        [ValidateSet('Get', 'Post')]
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

$auth = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/auth/steam-ticket" `
    -ContentType 'application/json' `
    -Body (@{ ticket = "dev:$steamId" } | ConvertTo-Json -Compress)
$playerHeaders = @{ Authorization = "Bearer $($auth.accessToken)" }
$character = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/characters" `
    -Headers $playerHeaders `
    -ContentType 'application/json' `
    -Body (@{
        name = "Sec$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
    } | ConvertTo-Json -Compress)

$serverId = "security-smoke-$RunId"
$session = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions" `
    -Headers $playerHeaders `
    -ContentType 'application/json' `
    -Body (@{
        characterId = $character.characterId
        dungeonId = 'Dungeon.SecuritySmoke'
        difficulty = 'Normal'
    } | ConvertTo-Json -Compress)
$session = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)/activate" `
    -Headers $adminHeaders `
    -ContentType 'application/json' `
    -Body (@{
        serverId = $serverId
        serverAddress = '127.0.0.1:7795'
    } | ConvertTo-Json -Compress)
if ([string]::IsNullOrWhiteSpace($session.gameServerAccessToken)) {
    throw 'Dungeon activation did not issue a game-server access token.'
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

$firstEventId = [Guid]::NewGuid()
$secondEventId = [Guid]::NewGuid()
$thirdEventId = [Guid]::NewGuid()
$batch = @{
    dungeonSessionId = $session.dungeonSessionId
    events = @(
        @{
            eventId = $firstEventId
            characterId = $character.characterId
            steamId = $steamId
            type = 'MovementSpeed'
            severity = 'Medium'
            score = 3.0
            riskAfter = 3.0
            serverTimeSeconds = 10.25
            detail = 'Authoritative speed sample exceeded policy.'
        },
        @{
            eventId = $secondEventId
            characterId = $character.characterId
            steamId = $steamId
            type = 'InvalidDamage'
            severity = 'Critical'
            score = 20.0
            riskAfter = 23.0
            serverTimeSeconds = 11.5
            detail = 'Rejected a non-finite or oversized damage request.'
        },
        @{
            eventId = $thirdEventId
            characterId = $character.characterId
            steamId = $steamId
            type = 'EnforcementStateChanged'
            severity = 'High'
            score = 0.0
            riskAfter = 23.0
            serverTimeSeconds = 11.75
            detail = 'Enforcement state changed from Elevated to Restricted.'
        }
    )
}

$accepted = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/security/events/batch" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body ($batch | ConvertTo-Json -Depth 8 -Compress)
if ($accepted.acceptedCount -ne 3 -or $accepted.duplicateCount -ne 0) {
    throw 'The initial security batch was not accepted exactly once.'
}

$replayed = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/security/events/batch" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body ($batch | ConvertTo-Json -Depth 8 -Compress)
if ($replayed.acceptedCount -ne 0 -or $replayed.duplicateCount -ne 3) {
    throw 'An idempotent security batch replay was not classified as duplicate.'
}

$conflict = $batch | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$conflict.events[0].detail = 'Conflicting content for the same immutable event ID.'
Invoke-ExpectedError `
    -Method Post `
    -Uri "$BaseUrl/api/security/events/batch" `
    -Headers $serverHeaders `
    -Body $conflict `
    -ExpectedStatus 409

$unauthorizedBatch = $batch | ConvertTo-Json -Depth 8 | ConvertFrom-Json
foreach ($event in $unauthorizedBatch.events) {
    $event.eventId = [Guid]::NewGuid()
    $event.steamId = '76561999999999999'
}
Invoke-ExpectedError `
    -Method Post `
    -Uri "$BaseUrl/api/security/events/batch" `
    -Headers $serverHeaders `
    -Body $unauthorizedBatch `
    -ExpectedStatus 403

Invoke-ExpectedError `
    -Method Get `
    -Uri "$BaseUrl/api/security/sessions/$($session.dungeonSessionId)/summary" `
    -Headers $playerHeaders `
    -Body $null `
    -ExpectedStatus 403

$summary = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/security/sessions/$($session.dungeonSessionId)/summary" `
    -Headers $serverHeaders
$movement = @($summary.byType | Where-Object { $_.key -eq 'MovementSpeed' })
$damage = @($summary.byType | Where-Object { $_.key -eq 'InvalidDamage' })
$enforcement = @($summary.byType | Where-Object {
    $_.key -eq 'EnforcementStateChanged'
})
$characterSummary = @($summary.byCharacter | Where-Object {
    $_.characterId -eq $character.characterId
})
if ($summary.totalEvents -ne 3 -or
    $summary.totalScore -ne 23 -or
    $summary.maximumRiskAfter -ne 23 -or
    $movement.Count -ne 1 -or $movement[0].eventCount -ne 1 -or
    $damage.Count -ne 1 -or $damage[0].eventCount -ne 1 -or
    $enforcement.Count -ne 1 -or $enforcement[0].eventCount -ne 1 -or
    $characterSummary.Count -ne 1 -or
    $characterSummary[0].eventCount -ne 3) {
    throw 'The security session summary did not match the accepted events.'
}

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
    throw 'Security telemetry smoke dungeon did not finish cleanly.'
}

Invoke-ExpectedError `
    -Method Get `
    -Uri "$BaseUrl/api/dungeon-sessions/$($session.dungeonSessionId)" `
    -Headers $serverHeaders `
    -Body $null `
    -ExpectedStatus 403

$postFinishBatch = @{
    dungeonSessionId = $session.dungeonSessionId
    events = @(
        @{
            eventId = [Guid]::NewGuid()
            characterId = $character.characterId
            steamId = $steamId
            type = 'PlayerRemoval'
            severity = 'Critical'
            score = 0.0
            riskAfter = 23.0
            serverTimeSeconds = 12.0
            detail = 'Final audit event drained after session finish.'
        }
    )
}
$postFinishAccepted = Invoke-RestMethod `
    -Method Post `
    -Uri "$BaseUrl/api/security/events/batch" `
    -Headers $serverHeaders `
    -ContentType 'application/json' `
    -Body ($postFinishBatch | ConvertTo-Json -Depth 8 -Compress)
if ($postFinishAccepted.acceptedCount -ne 1 -or
    $postFinishAccepted.duplicateCount -ne 0) {
    throw 'The post-session telemetry grace window did not accept the final audit event.'
}

$adminSummary = Invoke-RestMethod `
    -Method Get `
    -Uri "$BaseUrl/api/security/sessions/$($session.dungeonSessionId)/summary" `
    -Headers $adminHeaders
if ($adminSummary.totalEvents -ne 4 -or $adminSummary.totalScore -ne 23) {
    throw 'An administrator could not read telemetry after the session ended.'
}

Write-Host 'Security telemetry smoke test passed: authorization, idempotency, conflict detection, post-session audit isolation, and session aggregation.'
[pscustomobject]@{
    DungeonSessionId = $session.dungeonSessionId
    CharacterId = $character.characterId
    EventCount = $adminSummary.totalEvents
    TotalScore = $adminSummary.totalScore
}
