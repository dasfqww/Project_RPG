[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024,65535)][int]$Port = 18841,
    [ValidateRange(30,600)][int]$TimeoutSeconds = 300,
    [switch]$PendingAdmissionOnly,
    [switch]$SkipSDKSetup
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $projectRoot 'Project_RPG.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
if (-not (Test-Path -LiteralPath $editor) -or -not (Test-Path -LiteralPath $module)) { throw 'Build the Development Editor module before running recovery E2E.' }
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "UDP $Port is already in use." }
$hash = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
$runRoot = Join-Path $projectRoot ('Saved\Tests\DungeonRecovery\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$logs = [Collections.Generic.List[string]]::new()
$expectedExits = [Collections.Generic.HashSet[int]]::new()
$clientLogs = [Collections.Generic.HashSet[string]]::new()
$serverTerminated = $false
$sourcePaths = @(
    'Source/Project_RPG/Private/Tests/RPGDungeonRecoveryTestSubsystem.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonRecoveryTestSubsystem.h',
    'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
    'Source/Project_RPG/Public/Online/RPGDungeonSessionSubsystem.h',
    'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp',
    'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h',
    'Scripts/Network/Run-DungeonRecoveryE2ETest.ps1'
)
$sourceHashes = @($sourcePaths | ForEach-Object {
    [ordered]@{path=$_; SHA256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash}
})
$previousSDK = $env:UE_SKIP_UBT_SDK_SETUP
$failurePattern = 'DUNGEON_RECOVERY_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:'
$networkErrorPattern = 'Log(?:Iris\w*|Net\w*):\s*Error:'
# UE logs the deliberately induced client timeout as Error. Accept only this
# exact game-driver failure, once per owned client, after our server was stopped.
$expectedTimeoutPattern = 'LogNet:\s*Error: UEngine::BroadcastNetworkFailure: FailureType = ConnectionTimeout, ErrorString = UNetConnection::Tick: Connection TIMED OUT\. Closing connection\..*RemoteAddr: 127\.0\.0\.1:' + $Port + ', Name: [^,]+, Driver: Name:GameNetDriver Def:GameNetDriver [^,]+, IsServer: NO, PC: [^,]+, Owner: [^,]+, UniqueId: [^,]+, Driver = Name:GameNetDriver Def:GameNetDriver [^\r\n]+$'
if ($PendingAdmissionOnly) {
    $expectedTimeoutPattern = 'LogNet:\s*Error: UEngine::BroadcastNetworkFailure: FailureType = PendingConnectionFailure, ErrorString = missing_join_ticket, Driver = Name:PendingNetDriver Def:GameNetDriver [^\r\n]+$'
}

function Assert-Healthy {
    foreach ($process in $processes) {
        if ($process.HasExited -and -not $expectedExits.Contains($process.Id)) { throw "Unexpected probe exit; inspect $runRoot" }
    }
    foreach ($log in $logs) {
        if (-not (Test-Path -LiteralPath $log)) { continue }
        if (Select-String -LiteralPath $log -Pattern $failurePattern -Quiet) { throw "Recovery/runtime failure; inspect $log" }
        $networkErrors = @(Select-String -LiteralPath $log -Pattern $networkErrorPattern)
        if ($networkErrors.Count -gt 1) { throw "Repeated or unexpected network error; inspect $log" }
        foreach ($networkError in $networkErrors) {
            if ((-not $serverTerminated -and -not $PendingAdmissionOnly) -or -not $clientLogs.Contains($log) -or $networkError.Line -notmatch $expectedTimeoutPattern) {
                throw "Unexpected network error; inspect $log"
            }
        }
    }
}
function Start-Probe([string]$Name, [string]$Destination, [string]$Mode) {
    $log = Join-Path $runRoot ($Name + '.log')
    $arguments = @(('"'+$project+'"'), $Destination, $Mode, '-unattended', '-nosplash', '-nosound', '-nullrhi',
        '-nosteam', '-DisablePython', '-NoLoadingScreen', '-RPGDungeonRecoveryTest',
        '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=5.0',
        '-NoLiveCoding',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-UserDir="'+(Join-Path $runRoot ($Name+'-User'))+'"'), ('-abslog="'+$log+'"'), '-ExecCmds="t.MaxFPS 30"')
    if ($PendingAdmissionOnly) { $arguments += '-DDC-ForceMemoryCache' }
    else {
        $arguments += @('-DDC=(EnginePak,Local)', ('-LocalDataCachePath="'+(Join-Path $projectRoot 'Saved\Tests\DungeonRecovery\DerivedDataCache')+'"'))
    }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $processes.Add($process)
    $logs.Add($log)
    Write-Host "Started $Name (PID $($process.Id)): $log"
    return $log
}
function Wait-Marker([string]$Path, [string]$Marker) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        if ((Test-Path -LiteralPath $Path) -and (Select-String -LiteralPath $Path -SimpleMatch $Marker -Quiet)) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Missing recovery checkpoint '$Marker'; inspect $Path"
}
try {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = '1' }
    if ($PendingAdmissionOnly) {
        $serverLog = Start-Probe 'server' '/Engine/Maps/Entry?game=/Script/Project_RPG.RPGGameModeBase' ('-server -RPGItemE2EServer -port='+$Port)
        Wait-Marker $serverLog ('listening on port '+$Port)
        $clientLog = Start-Probe 'pending-client' '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' ('-game -RPGDungeonPendingRecoveryTest -RPGPendingRecoveryPort='+$Port)
        $clientLogs.Add($clientLog) | Out-Null
        Wait-Marker $clientLog 'DUNGEON_PENDING_RECOVERY_E2E_START OfflineFixture=1 NoBackend=1'
        Wait-Marker $serverLog 'PreLogin failure: missing_join_ticket'
        Wait-Marker $clientLog 'DUNGEON_PENDING_RECOVERY_E2E_FAILURE WorldNull=1 DriverOwned=1 NoBackend=1'
        Wait-Marker $clientLog 'DUNGEON_PENDING_RECOVERY_E2E_COMPLETE LobbyLoaded=1 StateRetained=1 MembershipRetained=1 CharacterRetained=1 JoinOptionsRemoved=1 InputGuardReleased=1 NoBackend=1'
        Assert-Healthy
        if (@(Select-String -LiteralPath $clientLog -Pattern $expectedTimeoutPattern).Count -ne 1) { throw 'Missing exact pending admission failure.' }
        if ((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne $hash) { throw 'Module changed during pending recovery E2E.' }
        foreach ($entry in $sourceHashes) {
            if ($entry.SHA256 -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot $entry.path) -Algorithm SHA256).Hash) { throw "Changed recovery input: $($entry.path)" }
        }
        [ordered]@{schemaVersion=1; editorModuleSHA256=$hash; passed=$true; remoteClients=1; pendingAdmissionOnly=$true;
            expectedAdmissionRejections=1; unexpectedNetworkErrors=0; sourceHashes=$sourceHashes;
            scope=[ordered]@{actualPreLoginRejection=$true; actualNullWorldPendingFailure=$true; actualLobbyMapLoad=$true;
                offlineMembershipFixture=$true; backendAuthentication=$false; backendAbandonment=$false; steam=$false; shipping=$false};
            logRoot=$runRoot} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
        Write-Host "PASS: real missing-ticket admission rejection recovered to lobby; offline membership fixture, no backend. Logs: $runRoot"
        return
    }
    $serverLog = Start-Probe 'server' '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase' ('-server -port='+$Port)
    $server = $processes[0]
    Wait-Marker $serverLog ('listening on port '+$Port)
    $firstLog = Start-Probe 'client1' ('127.0.0.1:'+$Port+'?JoinTicket=offline-recovery-fixture') '-game'
    $secondLog = Start-Probe 'client2' ('127.0.0.1:'+$Port+'?JoinTicket=offline-recovery-fixture') '-game'
    $clientLogs.Add($firstLog) | Out-Null
    $clientLogs.Add($secondLog) | Out-Null
    foreach ($log in @($firstLog,$secondLog)) { Wait-Marker $log 'DUNGEON_RECOVERY_E2E_CONNECTED OfflineFixture=1 NoBackend=1' }
    foreach ($log in $logs) { Wait-Marker $log 'LogIris: Display: Iris ReplicationSystem[' }
    # Terminate only the server created by this run, after both real clients connected.
    $expectedExits.Add($server.Id) | Out-Null
    Stop-Process -Id $server.Id
    $serverTerminated = $true
    foreach ($log in @($firstLog,$secondLog)) {
        Wait-Marker $log 'DUNGEON_RECOVERY_E2E_COMPLETE LobbyLoaded=1 StateRetained=1 MembershipRetained=1 CharacterRetained=1 JoinOptionsRemoved=1 InputGuardReleased=1 NoBackend=1'
    }
    $settleUntil = [DateTime]::UtcNow.AddSeconds(2)
    do { Assert-Healthy; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $settleUntil)
    foreach ($log in $clientLogs) {
        if (@(Select-String -LiteralPath $log -Pattern $expectedTimeoutPattern).Count -ne 1) { throw "Missing exact induced timeout; inspect $log" }
    }
    if ((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne $hash) { throw 'Editor module changed during recovery E2E.' }
    [ordered]@{ schemaVersion=1; editorModuleSHA256=$hash; passed=$true; remoteClients=2;
        expectedClientTimeouts=2; unexpectedNetworkErrors=0;
        scope=[ordered]@{ realServerTermination=$true; actualLobbyMapLoad=$true; offlineMembershipFixture=$true; backendAuthentication=$false; backendAbandonment=$false; steam=$false; shipping=$false };
        logRoot=$runRoot } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding UTF8
    Write-Host "PASS: two disconnected clients loaded the local lobby and preserved session/character state. Offline fixture; no backend abandonment/authentication. Logs: $runRoot"
}
finally {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = $previousSDK }
    foreach ($process in $processes) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
