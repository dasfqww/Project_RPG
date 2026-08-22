[CmdletBinding()]
param(
    [Guid] $DungeonSessionId = [Guid]::Empty,

    [Parameter(Mandatory)]
    [string] $ServerExecutable,

    [string] $BackendUrl = 'http://127.0.0.1:3000',

    [string] $AdminToken =
        $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN,

    [string] $PublicHost = '127.0.0.1',

    [ValidateRange(1, 65535)]
    [int] $Port = 7777,

    [string] $Map = '/Game/Maps/testmap',

    [string] $LogDirectory =
        (Join-Path $PSScriptRoot 'logs')
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    throw 'PROJECT_RPG_BACKEND_ADMIN_TOKEN or -AdminToken is required.'
}

$resolvedExecutable =
    (Resolve-Path -LiteralPath $ServerExecutable).Path
if ([IO.Path]::GetExtension($resolvedExecutable) -ne '.exe') {
    throw 'ServerExecutable must point to a Windows executable.'
}

$normalizedBackendUrl = $BackendUrl.TrimEnd('/')
$serverAddress = "${PublicHost}:$Port"
$serverId = if ($DungeonSessionId -eq [Guid]::Empty) {
    "local-$([Guid]::NewGuid().ToString('N'))-$Port"
}
else {
    "local-$($DungeonSessionId.ToString('N'))-$Port"
}

$null = New-Item -ItemType Directory -Force -Path $LogDirectory
$resolvedLogDirectory =
    (Resolve-Path -LiteralPath $LogDirectory).Path
$statePath = Join-Path $resolvedLogDirectory "port-$Port.allocator.json"

if (Test-Path -LiteralPath $statePath) {
    try {
        $existing = Get-Content -LiteralPath $statePath -Raw |
            ConvertFrom-Json
        $existingProcess = Get-Process -Id $existing.processId `
            -ErrorAction Stop
        $existingStartTicks = [long]$existing.processStartTimeUtcTicks
        $actualStartTicks =
            $existingProcess.StartTime.ToUniversalTime().Ticks
        if ($existingProcess.Path -eq $resolvedExecutable -and
            $existingStartTicks -gt 0 -and
            $existingStartTicks -eq $actualStartTicks) {
            [pscustomobject]@{
                ProcessId = $existingProcess.Id
                ServerId = $existing.serverId
                ServerAddress = $existing.serverAddress
                DungeonSessionId = $existing.dungeonSessionId
                LogPath = $existing.logPath
                Reused = $true
                WaitingSessionAvailable = $true
            }
            return
        }
    }
    catch {
        # A stale state file is replaced after a new process starts.
    }
}

$headers = @{
    Authorization = "Bearer $AdminToken"
}
$activationBody = @{
    serverId = $serverId
    serverAddress = $serverAddress
} | ConvertTo-Json -Compress
$assignmentUri = if ($DungeonSessionId -eq [Guid]::Empty) {
    "$normalizedBackendUrl/api/dungeon-sessions/claim"
}
else {
    "$normalizedBackendUrl/api/dungeon-sessions/$($DungeonSessionId.ToString('D'))/activate"
}
$session = Invoke-RestMethod `
    -Method Post `
    -Uri $assignmentUri `
    -Headers $headers `
    -ContentType 'application/json' `
    -Body $activationBody

if ($null -eq $session -or
    [string]::IsNullOrWhiteSpace(
        [string]$session.dungeonSessionId)) {
    [pscustomobject]@{
        ProcessId = $null
        ServerId = $serverId
        ServerAddress = $serverAddress
        DungeonSessionId = $null
        LogPath = $null
        Reused = $false
        WaitingSessionAvailable = $false
    }
    return
}

if ($session.state -ne 'Loading' -or
    $session.serverId -ne $serverId -or
    $session.serverAddress -ne $serverAddress -or
    [string]::IsNullOrWhiteSpace(
        [string]$session.gameServerAccessToken)) {
    throw 'Backend returned an unexpected dungeon server assignment.'
}
$sessionId = [Guid]::Parse($session.dungeonSessionId).ToString('D')
$gameServerToken = [string]$session.gameServerAccessToken
$logPath = Join-Path $resolvedLogDirectory "$sessionId.server.log"

$environmentNames = @(
    'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
    'PROJECT_RPG_GAME_SERVER_ID',
    'PROJECT_RPG_DUNGEON_SESSION_ID'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, 'Process')
}

$serverProcess = $null
$launchError = $null
try {
    [Environment]::SetEnvironmentVariable(
        'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
        $gameServerToken,
        'Process')
    [Environment]::SetEnvironmentVariable(
        'PROJECT_RPG_GAME_SERVER_ID',
        $serverId,
        'Process')
    [Environment]::SetEnvironmentVariable(
        'PROJECT_RPG_DUNGEON_SESSION_ID',
        $sessionId,
        'Process')

    $serverArguments = @(
        "${Map}?listen",
        '-server',
        "-port=$Port",
        '-log',
        "-abslog=`"$logPath`""
    )
    $serverProcess = Start-Process `
        -FilePath $resolvedExecutable `
        -ArgumentList $serverArguments `
        -WindowStyle Hidden `
        -PassThru
}
catch {
    $launchError = $_
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $previousEnvironment[$name],
            'Process')
    }
}

if ($null -ne $launchError) {
    try {
        $failureBody = @{
            serverId = $serverId
            outcome = 'Failed'
        } | ConvertTo-Json -Compress
        Invoke-RestMethod `
            -Method Post `
            -Uri "$normalizedBackendUrl/api/dungeon-sessions/$sessionId/finish" `
            -Headers $headers `
            -ContentType 'application/json' `
            -Body $failureBody | Out-Null
    }
    catch {
        Write-Warning (
            'Server launch failed and backend compensation also failed: ' +
            $_.Exception.Message)
    }
    throw $launchError
}

$allocatorState = [ordered]@{
    processId = $serverProcess.Id
    processStartTimeUtcTicks =
        $serverProcess.StartTime.ToUniversalTime().Ticks
    dungeonSessionId = $sessionId
    serverId = $serverId
    serverAddress = $serverAddress
    executable = $resolvedExecutable
    logPath = $logPath
    startedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
}
$allocatorState |
    ConvertTo-Json |
    Set-Content -LiteralPath $statePath -Encoding UTF8

[pscustomobject]@{
    ProcessId = $serverProcess.Id
    ServerId = $serverId
    ServerAddress = $serverAddress
    DungeonSessionId = $sessionId
    LogPath = $logPath
    Reused = $false
    WaitingSessionAvailable = $true
}
