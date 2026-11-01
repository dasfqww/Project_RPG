[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ServerExecutable,

    [string] $EditorProject,

    [switch] $DevelopmentIdentityTesting,

    [switch] $WaitForReady,

    [ValidateRange(1, 600)]
    [int] $StartupTimeoutSeconds = 240,

    [string] $BackendUrl = 'http://127.0.0.1:3000',

    [string] $AdminToken =
        $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN,

    [string] $PublicHost = '127.0.0.1',

    [ValidateRange(1, 65535)]
    [int] $StartPort = 7777,

    [ValidateRange(1, 128)]
    [int] $PortCount = 4,

    [ValidateRange(1, 60)]
    [int] $PollIntervalSeconds = 5,

    [string] $Map,

    [string] $LogDirectory =
        (Join-Path $PSScriptRoot 'logs'),

    [switch] $RunOnce
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    throw 'PROJECT_RPG_BACKEND_ADMIN_TOKEN or -AdminToken is required.'
}
if ($StartPort + $PortCount - 1 -gt 65535) {
    throw 'The configured port pool exceeds port 65535.'
}

$resolvedExecutable =
    (Resolve-Path -LiteralPath $ServerExecutable).Path
if ([IO.Path]::GetExtension($resolvedExecutable) -ne '.exe') {
    throw 'ServerExecutable must point to a Windows executable.'
}
$resolvedProject = if ($EditorProject) {
    (Resolve-Path -LiteralPath $EditorProject).Path
} else { $null }

$startServerScript = Join-Path `
    $PSScriptRoot `
    'Start-LocalDungeonServer.ps1'
if (-not (Test-Path -LiteralPath $startServerScript)) {
    throw 'Start-LocalDungeonServer.ps1 is missing.'
}

$null = New-Item -ItemType Directory -Force -Path $LogDirectory
$resolvedLogDirectory =
    (Resolve-Path -LiteralPath $LogDirectory).Path
$normalizedBackendUrl = $BackendUrl.TrimEnd('/')
. (Join-Path $PSScriptRoot 'LocalDungeonServerReadiness.ps1')
$backendIdentity = Get-RPGLocalDungeonBackendIdentity -BackendUrl $normalizedBackendUrl
$adminHeaders = @{
    Authorization = "Bearer $AdminToken"
}
$knownProcessKeys = @{}
$reportedStoppedStates = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::Ordinal)
$queueWasEmpty = $false

function New-AllocatorEvent {
    param(
        [string] $Event,
        [int] $Port,
        [Nullable[int]] $ProcessId,
        [string] $DungeonSessionId,
        [string] $Message
    )

    [pscustomobject]@{
        TimestampUtc = [DateTimeOffset]::UtcNow.ToString('O')
        Event = $Event
        Port = $Port
        ProcessId = $ProcessId
        DungeonSessionId = $DungeonSessionId
        Message = $Message
    }
}

function Get-PortProcessState {
    param([int] $Port)

    $statePath = Join-Path `
        $resolvedLogDirectory `
        "port-$Port.allocator.json"
    if (-not (Test-Path -LiteralPath $statePath)) {
        return [pscustomobject]@{
            IsRunning = $false
            ScopeVerified = $true # No pre-existing assignment to reconcile.
            StatePath = $statePath
            StateKey = 'none'
            ProcessId = $null
            DungeonSessionId = $null
            ServerId = $null
        }
    }

    $state = $null
    $scopeVerified = $false
    try {
        $state = Get-Content -LiteralPath $statePath -Raw |
            ConvertFrom-Json
        Assert-RPGLocalDungeonBackendScope -State $state -BackendIdentity $backendIdentity
        $expectedAddress = Get-RPGLocalDungeonServerEndpoint -PublicHost $PublicHost -Port $Port
        Assert-RPGLocalDungeonLaunchScope -State $state -ServerAddress $expectedAddress `
            -EditorProject $resolvedProject -DevelopmentIdentityTesting ([bool]$DevelopmentIdentityTesting) -Map $Map
        $scopeVerified = $true
        $process = Get-Process -Id $state.processId -ErrorAction Stop
        $expectedTicks = [long]$state.processStartTimeUtcTicks
        $actualTicks = $process.StartTime.ToUniversalTime().Ticks
        $stateKey = "$($state.processId):$expectedTicks"
        $isRunning = $process.Path -eq $resolvedExecutable -and
            $expectedTicks -gt 0 -and
            $expectedTicks -eq $actualTicks
        return [pscustomobject]@{
            IsRunning = $isRunning
            ScopeVerified = $true
            StatePath = $statePath
            StateKey = $stateKey
            ProcessId = [int]$state.processId
            DungeonSessionId = [string]$state.dungeonSessionId
            ServerId = [string]$state.serverId
        }
    }
    catch {
        $fallbackKey = if ($null -ne $state) {
            "$($state.processId):$($state.processStartTimeUtcTicks)"
        }
        else {
            $stateWriteTicks =
                (Get-Item -LiteralPath $statePath).LastWriteTimeUtc.Ticks
            "invalid:${statePath}:$stateWriteTicks"
        }
        return [pscustomobject]@{
            IsRunning = $false
            ScopeVerified = $scopeVerified
            StatePath = $statePath
            StateKey = $fallbackKey
            ProcessId = if ($null -ne $state) {
                [int]$state.processId
            }
            else {
                $null
            }
            DungeonSessionId = if ($null -ne $state) {
                [string]$state.dungeonSessionId
            }
            else {
                $null
            }
            ServerId = if ($null -ne $state) {
                [string]$state.serverId
            }
            else {
                $null
            }
        }
    }
}

function Resolve-StoppedDungeonSession {
    param($PortState)

    if ([string]::IsNullOrWhiteSpace($PortState.DungeonSessionId) -or
        [string]::IsNullOrWhiteSpace($PortState.ServerId)) {
        return [pscustomobject]@{
            Success = $true
            Message =
                'Stopped process had no valid backend assignment metadata.'
        }
    }

    try {
        $session = Invoke-RestMethod `
            -Method Get `
            -Uri "$normalizedBackendUrl/api/dungeon-sessions/$($PortState.DungeonSessionId)" `
            -Headers $adminHeaders
        if ($session.serverId -ne $PortState.ServerId) {
            return [pscustomobject]@{
                Success = $true
                Message =
                    'Backend assignment changed; no failure was reported.'
            }
        }
        if ($session.state -notin @('Loading', 'InProgress')) {
            return [pscustomobject]@{
                Success = $true
                Message =
                    "Backend session is already terminal: $($session.state)."
            }
        }

        $failureBody = @{
            serverId = $PortState.ServerId
            outcome = 'Failed'
        } | ConvertTo-Json -Compress
        $failedSession = Invoke-RestMethod `
            -Method Post `
            -Uri "$normalizedBackendUrl/api/dungeon-sessions/$($PortState.DungeonSessionId)/finish" `
            -Headers $adminHeaders `
            -ContentType 'application/json' `
            -Body $failureBody
        return [pscustomobject]@{
            Success = $true
            Message =
                "Stopped server was reconciled as $($failedSession.state)."
        }
    }
    catch {
        return [pscustomobject]@{
            Success = $false
            Message =
                'Backend reconciliation failed: ' + $_.Exception.Message
        }
    }
}

do {
    $waitingQueueExhausted = $false

    for ($offset = 0; $offset -lt $PortCount; $offset++) {
        $port = $StartPort + $offset
        $portState = Get-PortProcessState -Port $port

        if (-not $portState.ScopeVerified) {
            # Includes stopped foreign servers, conflicting profiles and corrupt metadata.
            # Do not GET/finish them against this backend or replace their state.
            New-AllocatorEvent -Event 'Error' -Port $port -ProcessId $portState.ProcessId `
                -DungeonSessionId $portState.DungeonSessionId `
                -Message 'Allocator backend or launch scope is unknown or different; port/state preserved without reconciliation or allocation.'
            continue
        }

        if ($portState.IsRunning) {
            if ($knownProcessKeys[$port] -ne $portState.StateKey) {
                $knownProcessKeys[$port] = $portState.StateKey
                New-AllocatorEvent `
                    -Event 'Running' `
                    -Port $port `
                    -ProcessId $portState.ProcessId `
                    -DungeonSessionId $portState.DungeonSessionId `
                    -Message 'Recorded dungeon server process is running; this observation alone is not readiness.'
            }
            continue
        }

        if ($portState.StateKey -ne 'none' -and
            $reportedStoppedStates.Add($portState.StateKey)) {
            $reconciliation =
                Resolve-StoppedDungeonSession -PortState $portState
            if (-not $reconciliation.Success) {
                $null = $reportedStoppedStates.Remove(
                    $portState.StateKey)
            }
            New-AllocatorEvent `
                -Event 'Stopped' `
                -Port $port `
                -ProcessId $portState.ProcessId `
                -DungeonSessionId $portState.DungeonSessionId `
                -Message (
                    'Recorded server process is no longer running. ' +
                    $reconciliation.Message)
        }
        $null = $knownProcessKeys.Remove($port)

        if ($waitingQueueExhausted) {
            continue
        }

        try {
            $allocation = & $startServerScript `
                -ServerExecutable $resolvedExecutable `
                -EditorProject $EditorProject `
                -DevelopmentIdentityTesting:$DevelopmentIdentityTesting `
                -WaitForReady:$WaitForReady `
                -StartupTimeoutSeconds $StartupTimeoutSeconds `
                -BackendUrl $BackendUrl `
                -AdminToken $AdminToken `
                -PublicHost $PublicHost `
                -Port $port `
                -Map $Map `
                -LogDirectory $resolvedLogDirectory

            if (-not $allocation.WaitingSessionAvailable) {
                $waitingQueueExhausted = $true
                if (-not $queueWasEmpty) {
                    New-AllocatorEvent `
                        -Event 'QueueEmpty' `
                        -Port $port `
                        -ProcessId $null `
                        -DungeonSessionId $null `
                        -Message 'No Waiting dungeon session is available.'
                }
                $queueWasEmpty = $true
                continue
            }

            $queueWasEmpty = $false
            $eventName = if ($allocation.Reused) {
                'Reused'
            }
            else {
                'Started'
            }
            New-AllocatorEvent `
                -Event $eventName `
                -Port $port `
                -ProcessId $allocation.ProcessId `
                -DungeonSessionId $allocation.DungeonSessionId `
                -Message ("Dungeon server assignment launched (Ready=$($allocation.Ready)): " +
                    "$($allocation.DungeonId) / $($allocation.Difficulty) / $($allocation.MapPackage).")
        }
        catch {
            New-AllocatorEvent `
                -Event 'Error' `
                -Port $port `
                -ProcessId $null `
                -DungeonSessionId $null `
                -Message $_.Exception.Message
        }
    }

    if (-not $RunOnce) {
        Start-Sleep -Seconds $PollIntervalSeconds
    }
}
while (-not $RunOnce)
