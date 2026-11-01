# Test/operations helper. Uses the leader's player token, never allocator authority.
function Request-RPGDungeonDeparture {
    param([string]$BaseUrl, [string]$SessionId, [string]$CharacterId, [hashtable]$Headers)
    $base = $BaseUrl.TrimEnd('/')
    $session = Invoke-RestMethod -Method Get -Uri "$base/api/dungeon-sessions/$SessionId" -Headers $Headers
    if ([string]$session.leaderCharacterId -cne $CharacterId -or [long]$session.lobbyRevision -lt 1) {
        throw 'Departure requires the exact current leader and versioned lobby.'
    }
    if ($session.departureRequestedAt) { return $session }
    $body = @{ characterId = $CharacterId; requestId = [Guid]::NewGuid().ToString('D'); expectedLobbyRevision = $session.lobbyRevision } | ConvertTo-Json -Compress
    Invoke-RestMethod -Method Post -Uri "$base/api/dungeon-sessions/$SessionId/departure" -Headers $Headers -ContentType 'application/json' -Body $body
}
