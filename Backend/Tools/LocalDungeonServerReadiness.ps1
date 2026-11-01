# Shared, read-only assignment scope and readiness checks.
function Get-RPGLocalDungeonBackendIdentity {
    param([string] $BackendUrl)
    $normalized = $BackendUrl.TrimEnd('/')
    $uri = $null
    if (-not [Uri]::TryCreate($normalized, [UriKind]::Absolute, [ref]$uri) -or
        $uri.Scheme -notin @('http', 'https') -or $uri.UserInfo -or
        $uri.Query -or $uri.Fragment -or $normalized -match '[\s"\x00-\x1f]') {
        throw 'BackendUrl must be an absolute HTTP(S) URL without credentials, query or fragment.'
    }
    # Canonical host/default port; preserve the case-sensitive deployment path.
    return $uri.AbsoluteUri.TrimEnd('/')
}

function Assert-RPGLocalDungeonBackendScope {
    param($State, [string] $BackendIdentity)
    if (-not $State.PSObject.Properties['backendUrl'] -or
        [string]::IsNullOrWhiteSpace([string]$State.backendUrl)) {
        throw 'Allocator state has no backend identity; existing process/state were preserved. Use a separate log directory/port or explicitly recover the old server.'
    }
    $storedIdentity = Get-RPGLocalDungeonBackendIdentity -BackendUrl ([string]$State.backendUrl)
    if ($storedIdentity -cne $BackendIdentity) {
        throw 'Allocator state belongs to a different backend; existing process/state were preserved.'
    }
}

function Get-RPGLocalDungeonServerEndpoint {
    param([string] $PublicHost, [int] $Port)
    # MULTIHOME still takes the caller's raw host. Only the endpoint brackets IPv6.
    $addressHost = if ($PublicHost.Contains(':') -and -not $PublicHost.StartsWith('[')) { "[$PublicHost]" } else { $PublicHost }
    return "${addressHost}:$Port"
}

function Assert-RPGLocalDungeonLaunchScope {
    param(
        $State,
        [string] $ServerAddress,
        [string] $EditorProject,
        [bool] $DevelopmentIdentityTesting,
        [string] $Map,
        [Guid] $DungeonSessionId = [Guid]::Empty,
        [string] $RuntimeUserDirectory
    )
    if ([string]$State.serverAddress -cne $ServerAddress -or
        ($DungeonSessionId -ne [Guid]::Empty -and $DungeonSessionId.ToString('D') -cne [string]$State.dungeonSessionId) -or
        ($Map -and $Map -cne [string]$State.mapPackage) -or
        ($DevelopmentIdentityTesting -ne [bool]$State.developmentIdentityTesting) -or
        ($EditorProject -ine [string]$State.editorProject) -or
        ($RuntimeUserDirectory -and (-not $State.PSObject.Properties['runtimeUserDirectory'] -or
            $RuntimeUserDirectory -ine [string]$State.runtimeUserDirectory))) {
        throw 'Allocator state has a different address/session/map or launch profile; existing process/state were preserved.'
    }
}

# A PID or backend InProgress alone is not
# evidence that the intended UE world accepted the authenticated content.
function Wait-RPGLocalDungeonServerReady {
    param(
        [Parameter(Mandatory)] $State,
        [Parameter(Mandatory)] [string] $Executable,
        [Parameter(Mandatory)] [string] $BackendUrl,
        [Parameter(Mandatory)] [hashtable] $Headers,
        [ValidateRange(1, 600)] [int] $TimeoutSeconds = 240
    )
    $marker = "RPG_DUNGEON_SERVER_READY Session=$($State.dungeonSessionId) Server=$($State.serverId) Dungeon=$($State.dungeonId) Map=$($State.mapPackage) Difficulty=$($State.difficulty)"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        try { $process = Get-Process -Id $State.processId -ErrorAction Stop }
        catch { throw 'The assigned server process exited before readiness.' }
        if ($process.Path -ine $Executable -or
            $process.StartTime.ToUniversalTime().Ticks -ne [long]$State.processStartTimeUtcTicks) {
            throw 'The recorded PID no longer belongs to the assigned server.'
        }
        $session = $null
        try {
            $session = Invoke-RestMethod -Method Get -Uri "$BackendUrl/api/dungeon-sessions/$($State.dungeonSessionId)" -Headers $Headers -TimeoutSec 3
        }
        catch { } # A failed query never supplies positive readiness evidence.
        if ($null -ne $session) {
            if ([string]$session.dungeonSessionId -cne [string]$State.dungeonSessionId -or
                [string]$session.serverId -cne [string]$State.serverId -or
                [string]$session.serverAddress -cne [string]$State.serverAddress -or
                [string]$session.dungeonId -cne [string]$State.dungeonId -or
                [string]$session.difficulty -cne [string]$State.difficulty) {
                throw 'Backend assignment changed while waiting for server readiness.'
            }
            if ($session.state -notin @('Loading', 'InProgress')) {
                throw 'The assigned session became terminal before server readiness.'
            }
            if ($session.state -ceq 'InProgress' -and
                (Test-Path -LiteralPath $State.logPath) -and
                (Select-String -LiteralPath $State.logPath -SimpleMatch -Pattern $marker -Quiet)) {
                return $true
            }
        }
        Start-Sleep -Milliseconds 250
    }
    throw 'Server readiness timed out without matching backend and authenticated UE content confirmation.'
}

function Stop-RPGOwnedLocalDungeonServer {
    param($Process, [string]$Executable, [long]$StartTicks)
    if ($null -eq $Process -or $StartTicks -le 0) { return }
    try {
        $current = Get-Process -Id $Process.Id -ErrorAction Stop
        if ($current.Path -ieq $Executable -and $current.StartTime.ToUniversalTime().Ticks -eq $StartTicks) {
            Stop-Process -Id $current.Id -Force -ErrorAction Stop
            $Process.WaitForExit()
        }
    }
    catch { } # Never substitute a broad process-name kill.
}
