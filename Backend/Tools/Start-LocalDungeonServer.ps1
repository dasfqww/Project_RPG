[CmdletBinding()]
param(
    [Guid] $DungeonSessionId = [Guid]::Empty,

    [Parameter(Mandatory)]
    [string] $ServerExecutable,

    # Explicit development profile; omitted for a packaged Server executable.
    [string] $EditorProject,

    # Optional short, caller-owned directory for scoped connection journals.
    [string] $RuntimeUserDirectory,

    # Local no-Steam integration only. Backend tickets/membership stay required.
    [switch] $DevelopmentIdentityTesting,

    [switch] $WaitForReady,

    [ValidateRange(1, 600)]
    [int] $StartupTimeoutSeconds = 240,

    [string] $BackendUrl = 'http://127.0.0.1:3000',

    [string] $AdminToken =
        $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN,

    [string] $PublicHost = '127.0.0.1',

    [ValidateRange(1, 65535)]
    [int] $Port = 7777,

    # Optional operator assertion, not a map override.
    [string] $Map,

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
$resolvedProject = $null
if ($EditorProject) {
    $resolvedProject = (Resolve-Path -LiteralPath $EditorProject).Path
    if ([IO.Path]::GetExtension($resolvedProject) -ine '.uproject' -or
        [IO.Path]::GetFileNameWithoutExtension($resolvedExecutable) -inotmatch '^UnrealEditor(?:-Cmd)?$') {
        throw 'EditorProject requires a .uproject and an explicit UnrealEditor[-Cmd].exe executable.'
    }
}
elseif ([IO.Path]::GetFileNameWithoutExtension($resolvedExecutable) -imatch '^UnrealEditor(?:-Cmd)?$') {
    throw 'An Editor executable requires the explicit development EditorProject profile.'
}
$resolvedUserDirectory = $null
if ($RuntimeUserDirectory) {
    if (-not $resolvedProject -or -not [IO.Path]::IsPathRooted($RuntimeUserDirectory) -or
        [IO.Path]::GetPathRoot($RuntimeUserDirectory).Length -lt 3 -or
        $RuntimeUserDirectory -match '["\x00-\x1f]') {
        throw 'RuntimeUserDirectory requires an EditorProject and an absolute safe path.'
    }
    $resolvedUserDirectory = [IO.Path]::GetFullPath($RuntimeUserDirectory)
}
. (Join-Path $PSScriptRoot 'LocalDungeonServerReadiness.ps1')

$normalizedBackendUrl = $BackendUrl.TrimEnd('/')
$backendIdentity = Get-RPGLocalDungeonBackendIdentity -BackendUrl $normalizedBackendUrl
$backendUri = [Uri]$normalizedBackendUrl
$backendIp = $null
$isLiteralBackendLoopback = [Net.IPAddress]::TryParse($backendUri.DnsSafeHost.Trim([char[]]'[]'), [ref]$backendIp) -and
    ($backendIp.Equals([Net.IPAddress]::Loopback) -or $backendIp.Equals([Net.IPAddress]::IPv6Loopback))
if ($DevelopmentIdentityTesting -and (-not $resolvedProject -or
    $PublicHost -cnotin @('127.0.0.1', '::1') -or
    -not $isLiteralBackendLoopback)) {
    throw 'DevelopmentIdentityTesting requires an explicit EditorProject and literal loopback backend/public host.'
}
$serverAddress = Get-RPGLocalDungeonServerEndpoint -PublicHost $PublicHost -Port $Port
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
    $runningState = $null
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
            $runningState = $existing
        }
    }
    catch {
        # A stale state file is replaced after a new process starts.
    }
    if ($null -ne $runningState) {
        # Process identity proves liveness, not assignment to this backend.
        # Never wait against another backend, reuse it, stop it or compensate it.
        Assert-RPGLocalDungeonBackendScope -State $runningState -BackendIdentity $backendIdentity
        Assert-RPGLocalDungeonLaunchScope -State $runningState -ServerAddress $serverAddress `
            -EditorProject $resolvedProject -DevelopmentIdentityTesting ([bool]$DevelopmentIdentityTesting) `
            -Map $Map -DungeonSessionId $DungeonSessionId -RuntimeUserDirectory $resolvedUserDirectory
        $ready = $false
        if ($WaitForReady) {
            # This process predates this invocation. Failure must not terminate
            # it or submit a compensating finish for somebody else's launch.
            $ready = Wait-RPGLocalDungeonServerReady -State $runningState -Executable $resolvedExecutable `
                -BackendUrl $normalizedBackendUrl -Headers @{ Authorization = "Bearer $AdminToken" } -TimeoutSeconds $StartupTimeoutSeconds
        }
        [pscustomobject]@{
            ProcessId = $runningState.processId
            ServerId = $runningState.serverId
            ServerAddress = $runningState.serverAddress
            DungeonSessionId = $runningState.dungeonSessionId
            DungeonId = $runningState.dungeonId
            MapPackage = $runningState.mapPackage
            Difficulty = $runningState.difficulty
            LogPath = $runningState.logPath
            Reused = $true
            Ready = $ready
            WaitingSessionAvailable = $true
        }
        return
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
if ($DungeonSessionId -ne [Guid]::Empty -and $sessionId -cne $DungeonSessionId.ToString('D')) {
    throw 'Backend returned a different session than the explicit activation target.'
}
$gameServerToken = [string]$session.gameServerAccessToken
$logPath = Join-Path $resolvedLogDirectory "$sessionId.server.log"

$environmentNames = @(
    'PROJECT_RPG_BACKEND_ADMIN_TOKEN',
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
$serverStartTicks = 0L
$launchError = $null
try {
    $assignedMap = [string]$session.serverMapPackage
    if ($assignedMap.Length -gt 256 -or
        $assignedMap -cnotmatch '\A/(?:Game|Engine)/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+\z' -or
        [string]$session.dungeonId -cnotmatch '\A[A-Za-z0-9_.-]{1,64}\z' -or
        [string]$session.difficulty -cnotin @('Easy', 'Normal', 'Hard', 'Hell')) {
        throw 'Backend assignment has no valid catalog map/content; refusing a default-map fallback.'
    }
    if ($Map -and $Map -cne $assignedMap) {
        throw 'The asserted map differs from the backend catalog assignment.'
    }
    # The allocator may read this ambient token, but the gameplay child must
    # receive only the scoped server credential, not allocation authority.
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', $null, 'Process')
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
        "${assignedMap}?listen",
        '-server',
        "-port=$Port",
        '-log',
        "-abslog=`"$logPath`"",
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$normalizedBackendUrl/api",
        "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$normalizedBackendUrl/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$normalizedBackendUrl/api"
    )
    if ($resolvedProject) {
        $runtimeDirectory = if ($resolvedUserDirectory) { $resolvedUserDirectory } else {
            Join-Path (Split-Path $resolvedProject -Parent) ("Saved/LocalServers/" + $sessionId.Replace('-', '').Substring(0, 12))
        }
        $serverArguments = @(('"' + $resolvedProject + '"')) + $serverArguments + @(
            '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi', '-nosteam',
            '-NoLiveCoding', '-NoLoadingScreen', '-DisablePython', '-DDC=(EnginePak,Local)',
            '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
            ('-UserDir="' + $runtimeDirectory + '"'))
    }
    if ($DevelopmentIdentityTesting) {
        # Existing non-Shipping Steam UniqueId comparison fixture only; never
        # RPGNetTestMode or a direct admission/ticket/character-state injection.
        $serverArguments += @('-RPGItemE2EServer', '-RPGItemE2EAllowUnverifiedIdentity', "-MULTIHOME=$PublicHost")
    }
    $serverProcess = Start-Process `
        -FilePath $resolvedExecutable `
        -ArgumentList $serverArguments `
        -WindowStyle Hidden `
        -PassThru
    $serverStartTicks = $serverProcess.StartTime.ToUniversalTime().Ticks
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
    Stop-RPGOwnedLocalDungeonServer -Process $serverProcess -Executable $resolvedExecutable -StartTicks $serverStartTicks
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
    backendUrl = $backendIdentity
    processId = $serverProcess.Id
    processStartTimeUtcTicks =
        $serverStartTicks
    editorProject = $resolvedProject
    runtimeUserDirectory = $resolvedUserDirectory
    developmentIdentityTesting = [bool]$DevelopmentIdentityTesting
    startupState = 'Launched'
    dungeonSessionId = $sessionId
    dungeonId = [string]$session.dungeonId
    difficulty = [string]$session.difficulty
    mapPackage = $assignedMap
    serverId = $serverId
    serverAddress = $serverAddress
    executable = $resolvedExecutable
    logPath = $logPath
    startedAtUtc = [DateTimeOffset]::UtcNow.ToString('O')
}
$ready = $false
try {
    $allocatorState | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
    if ($WaitForReady) {
        $ready = Wait-RPGLocalDungeonServerReady -State ([pscustomobject]$allocatorState) -Executable $resolvedExecutable `
            -BackendUrl $normalizedBackendUrl -Headers $headers -TimeoutSeconds $StartupTimeoutSeconds
        $allocatorState.startupState = 'Ready'
        $allocatorState | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
    }
}
catch {
    $startupError = $_
    Stop-RPGOwnedLocalDungeonServer -Process $serverProcess -Executable $resolvedExecutable -StartTicks $serverStartTicks
    try {
        $failureBody = @{ serverId = $serverId; outcome = 'Failed' } | ConvertTo-Json -Compress
        Invoke-RestMethod -Method Post -Uri "$normalizedBackendUrl/api/dungeon-sessions/$sessionId/finish" `
            -Headers $headers -ContentType 'application/json' -Body $failureBody | Out-Null
    }
    catch { Write-Warning 'Server startup failed; scoped backend compensation could not be confirmed.' }
    throw $startupError
}

[pscustomobject]@{
    ProcessId = $serverProcess.Id
    ServerId = $serverId
    ServerAddress = $serverAddress
    DungeonSessionId = $sessionId
    DungeonId = [string]$session.dungeonId
    Difficulty = [string]$session.difficulty
    MapPackage = $assignedMap
    LogPath = $logPath
    Reused = $false
    Ready = $ready
    WaitingSessionAvailable = $true
}
