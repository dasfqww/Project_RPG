[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1024, 65535)][int]$Port = 18791,
    [ValidateRange(20, 240)][int]$TimeoutSeconds = 90,
    [switch]$ListenServer,
    [switch]$BossDash,
    [switch]$BossSummon,
    [switch]$SkipSDKSetup
)

$ErrorActionPreference = 'Stop'
if ($BossDash -and $BossSummon) { throw 'Choose either -BossDash or -BossSummon, not both.' }
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editorPath)) { throw "Missing editor: $editorPath" }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'))) {
    throw 'Build Project_RPGEditor Win64 Development before running this test.'
}
$logRoot = Join-Path $projectRoot ('Saved\Tests\BossState\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$dashGateBase = Join-Path $logRoot 'DashGate'
$summonGateBase = Join-Path $logRoot 'SummonGate'
$modulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$moduleHashBefore = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$probeLogPaths = [Collections.Generic.List[string]]::new()
$expectedExits = [Collections.Generic.HashSet[int]]::new()
$runtimeFailurePattern = 'RAID_[A-Z0-9_]*FAIL(?:ED)?\b|Fatal error:|Assertion failed:|Ensure condition failed:|Log(?:Iris\w*|Net\w*):\s*Error:'
$cachePath = Join-Path $projectRoot 'Saved\Tests\BossState\DerivedDataCache'
$previousSkipSDKSetup = $env:UE_SKIP_UBT_SDK_SETUP

function Start-Probe {
    param([string]$Name, [string]$Destination, [string]$Mode)
    $logPath = Join-Path $logRoot ($Name + '.log')
    $userPath = Join-Path $logRoot ($Name + '-User')
    $arguments = @(
        ('"' + $projectPath + '"'), $Destination, $Mode,
        '-RPGBossNetworkTest', '-unattended', '-nosplash', '-nosound', '-nullrhi',
        '-nosteam', '-NoLoadingScreen', '-DisablePython',
        '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=20.0',
        '-DDC=(EnginePak,Local)', ('-LocalDataCachePath="' + $cachePath + '"'),
        ('-UserDir="' + $userPath + '"'),
        '-log', ('-abslog="' + $logPath + '"'), '-ExecCmds="t.MaxFPS 30"'
    )
    if ($BossDash) {
        $arguments += @('-RPGBossNetworkDash', ('-RPGBossDashGate="' + $dashGateBase + '"'))
    }
    if ($BossSummon) {
        $arguments += @('-RPGBossNetworkSummon', ('-RPGBossSummonGate="' + $summonGateBase + '"'))
    }
    $process = Start-Process -FilePath $editorPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $processes.Add($process)
    $probeLogPaths.Add($logPath)
    Write-Host "Started $Name (PID $($process.Id)): $logPath"
    return $logPath
}

function Wait-Marker {
    param([string]$Path, [string]$Marker)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Test-Path -LiteralPath $Path) -and
            (Select-String -LiteralPath $Path -SimpleMatch $Marker -Quiet)) { return }
        foreach ($process in $processes) {
            if ($process.HasExited -and -not $expectedExits.Contains($process.Id)) { throw "Probe process exited ($($process.ExitCode)); inspect $logRoot" }
        }
        foreach ($probeLog in $probeLogPaths) {
            if ((Test-Path -LiteralPath $probeLog) -and (Select-String -LiteralPath $probeLog -Pattern $runtimeFailurePattern -Quiet)) {
                throw "Runtime/replication error in $probeLog"
            }
        }
        Start-Sleep -Milliseconds 250
    }
    $tail = if (Test-Path -LiteralPath $Path) { (Get-Content -LiteralPath $Path -Tail 12) -join "`n" } else { 'Log not created.' }
    throw "Missing marker '$Marker' in $Path`n$tail"
}

function Invoke-SummonScenario {
    $map = '/Engine/Maps/Entry?game=/Script/Project_RPG.RPGBossNetworkTestGameMode'
    $mode = '-server'
    if ($ListenServer) { $map += '?listen'; $mode = '-game' }
    $serverLog = Start-Probe 'server' $map ($mode + ' -port=' + $Port)
    Wait-Marker $serverLog 'RAID_SUMMON_READY Startup=1'
    Wait-Marker $serverLog 'LogIris: Display: Iris ReplicationSystem['
    $firstLog = Start-Probe 'client1' ('127.0.0.1:' + $Port) '-game'
    Wait-Marker $firstLog 'LogIris: Display: Iris ReplicationSystem['
    Wait-Marker $serverLog 'RAID_SUMMON_FIRST_CONNECTED RemotePlayers=1'
    $lateLog = Start-Probe 'late-client' '/Engine/Maps/Entry?game=/Script/Project_RPG.RPGBossNetworkTestGameMode' ('-game -RPGBossSummonDeferredJoin=127.0.0.1:' + $Port)
    Wait-Marker $lateLog 'RAID_SUMMON_JOIN_READY LocalWorld=1'
    Set-Content -LiteralPath ($summonGateBase + '.request') -Value 'request' -Encoding ASCII
    Wait-Marker $serverLog 'RAID_SUMMON_ACTIVE_SERVER NPCs=2 Ready=1 CastEnded=1 BeforeLateJoin=1'
    Wait-Marker $serverLog 'RAID_SUMMON_LATE_JOIN_SERVER ExistingNPCsAtLogin=2'
    foreach ($path in @($firstLog, $lateLog)) {
        Wait-Marker $path 'LogIris: Display: Iris ReplicationSystem['
        Wait-Marker $path 'RAID_SUMMON_STATE_REPLICATED NPCs=2 Metadata=1 Position=1 GASHealth=1 AuthorityDenied=1'
    }
    Set-Content -LiteralPath ($summonGateBase + '.reset') -Value 'reset' -Encoding ASCII
    Wait-Marker $serverLog 'RAID_SUMMON_RESET_SERVER NPCsRemoved=2 OwnedAIRemoved=2 BeforeExpiry=1'
    foreach ($path in @($firstLog, $lateLog)) {
        Wait-Marker $path 'RAID_SUMMON_CLEARED_REPLICATED RegistryEmpty=1 NPCsDestroyed=2 BeforeExpiry=1'
    }
    Wait-Marker $serverLog 'RAID_SUMMON_RESET_QUIET NPCs=0 OwnedAI=0'
    foreach ($process in $processes) {
        if ($process.HasExited) { throw "Summon probe exited ($($process.ExitCode)); inspect $logRoot" }
    }
    foreach ($path in @($serverLog, $firstLog, $lateLog)) {
        if (Select-String -LiteralPath $path -Pattern $runtimeFailurePattern -Quiet) {
            throw "Runtime/replication error in $path"
        }
    }
    if ($moduleHashBefore -ne (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) {
        throw 'Editor module changed during summon validation.'
    }
    $proof = [ordered]@{
        schemaVersion = 1
        editorModuleSHA256 = $moduleHashBefore
        listenServer = [bool]$ListenServer
        serverLog = $serverLog
        clientLogs = @($firstLog, $lateLog)
        scope = [ordered]@{
            irisSummonNPCsAndMetadata = $true
            positionsAndGASHealth = $true
            lateConnectionAfterNPCInitialization = $true
            clientMutationDenied = $true
            resetRemovesNPCsAndOwnedAI = $true
            removalBeforeNaturalExpiry = $true
            testFileRendezvous = $true
            reconnectGracePeriod = $false
            authoredNPCBehaviorTree = $false
            shippingRuntime = $false
        }
    }
    $proofPath = Join-Path $logRoot 'BossSummon.proof.json'
    $proof | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: Iris summons, initialized-NPC late join, GAS health, authority denial and reset cleanup. Proof: $proofPath"
}

try {
    # Optional for prebuilt, uncooked test runs on restricted CI workers. This does not build or validate SDKs.
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = '1' }
    if ($BossSummon) { Invoke-SummonScenario; return }
    $map = '/Engine/Maps/Entry?game=/Script/Project_RPG.RPGBossNetworkTestGameMode'
    $mode = '-server'
    if ($ListenServer) { $map += '?listen'; $mode = '-game' }
    $serverLog = Start-Probe 'server' $map ($mode + ' -port=' + $Port)
    Wait-Marker $serverLog 'RAID_PROBE_READY'
    Wait-Marker $serverLog 'LogIris: Display: Iris ReplicationSystem['
    Wait-Marker $serverLog 'RAID_STARTUP_READY Applied=1 EarlyStartDenied=1'
    if (-not $ListenServer) { Wait-Marker $serverLog 'RAID_PRESENTATION_DEDICATED Skipped=1' }
    $firstLog = Start-Probe 'client1' ('127.0.0.1:' + $Port) '-game'
    $firstProcess = $processes[$processes.Count - 1]
    Wait-Marker $serverLog 'RAID_PROBE_LATE_JOIN_READY'
	Wait-Marker $serverLog 'RAID_ENRAGE_AUTO_SERVER Attack=2 DeadlineConsumed=1'
	Wait-Marker $firstLog 'RAID_ENRAGE_CLOCK_REPLICATED DeadlinePositive=1 Attack=1'
	Wait-Marker $serverLog 'RAID_BOSS_TELEGRAPH_STARTED Encounter=1 Sequence=1'
    Wait-Marker $firstLog 'RAID_PROBE_PHASE2 Health=69 AuthorityDenied=1'
    Wait-Marker $serverLog 'RAID_HAZARD_SERVER_STARTED Persistent=1 Targets=2 CastEnded=1'
    if ($BossDash) {
        # Warm the late client's local world before requesting a bounded 5-second dash.
        # Its test GameMode travels only after the server writes the active-dash gate.
        $lateLog = Start-Probe 'late-client' '/Engine/Maps/Entry?game=/Script/Project_RPG.RPGBossNetworkTestGameMode' ('-game -RPGBossDashDeferredJoin=127.0.0.1:' + $Port)
        Wait-Marker $lateLog 'RAID_DASH_JOIN_READY LocalWorld=1'
        Set-Content -LiteralPath ($dashGateBase + '.request') -Value 'request' -Encoding ASCII
        Wait-Marker $serverLog 'RAID_DASH_ACTIVE_SERVER Active=1 BeforeLateJoin=1'
        Wait-Marker $serverLog 'RAID_DASH_LATE_JOIN_SERVER ActiveAtLogin=1'
        foreach ($path in @($firstLog, $lateLog)) {
            Wait-Marker $path 'RAID_DASH_STATE_REPLICATED Active=1 EndpointLocked=1 AuthorityDenied=1'
            Wait-Marker $path 'RAID_DASH_MOVE_REPLICATED TwoPositions=1'
            Wait-Marker $path 'RAID_DASH_DAMAGE_REPLICATED Targets=2 OnceEach=1'
        }
        Wait-Marker $serverLog 'RAID_DASH_MOVE_SERVER Moved=1'
        Wait-Marker $serverLog 'RAID_DASH_DAMAGE_SERVER Targets=2 OnceEach=1'
        Set-Content -LiteralPath ($dashGateBase + '.reset') -Value 'reset' -Encoding ASCII
        Wait-Marker $serverLog 'RAID_DASH_RESET_SERVER Cancelled=1 VelocityZero=1 BeforeExpiry=1'
        foreach ($path in @($firstLog, $lateLog)) {
            Wait-Marker $path 'RAID_DASH_CLEARED_REPLICATED Active=0'
            Wait-Marker $path 'RAID_DASH_RESET_REPLICATED Stationary=1 BeforeExpiry=1'
        }
        Wait-Marker $serverLog 'RAID_DASH_RESET_QUIET Stationary=1 Targets=2 HealthUnchanged=1'
    }
    else { $lateLog = Start-Probe 'late-client' ('127.0.0.1:' + $Port) '-game' }
    Wait-Marker $lateLog 'RAID_PROBE_PHASE2 Health=69 AuthorityDenied=1'
    foreach ($path in @($firstLog, $lateLog)) {
		Wait-Marker $path 'RAID_HAZARD_CLIENT_STATE Encounter=1 Radius=200 Active=1 AuthorityDenied=1'
		Wait-Marker $path 'RAID_HAZARD_PULSE_REPLICATED PulseCountAtLeast2=1'
		Wait-Marker $path 'RAID_HAZARD_TARGET_DAMAGE_REPLICATED Targets=2 Lowered=1'
		Wait-Marker $path 'RAID_HAZARD_REMOVED_REPLICATED Removed=1'
		Wait-Marker $path 'RAID_ENRAGE_REPLICATED Attack=2 DeadlineConsumed=1'
		Wait-Marker $path 'RAID_ENRAGE_RESET_REPLICATED Attack=1 Deadline=0'
        Wait-Marker $path 'LogIris: Display: Iris ReplicationSystem['
        Wait-Marker $path 'RAID_STARTUP_REPLICATED Health=100 AvatarReady=1 AuthorityDenied=1'
		Wait-Marker $path 'RAID_BOSS_TELEGRAPH_REPLICATED Active=1'
		Wait-Marker $path 'RAID_BOSS_TELEGRAPH_CLEARED Active=0'
		Wait-Marker $path 'RAID_BOSS_TIMED_STRIKE_TELEGRAPH_REPLICATED Active=1'
        Wait-Marker $path 'RAID_PRESENTATION_VISIBLE Visible=1 ProgressFromServer=1'
        Wait-Marker $path 'RAID_PRESENTATION_CLEARED Visible=0'
        Wait-Marker $path 'RAID_PROBE_RESET Clean=1'
        Wait-Marker $path 'RAID_PROBE_RESTART Encounter=2 Health=100'
        Wait-Marker $path 'RAID_PROBE_DEAD Health=0'
    }
    Wait-Marker $serverLog 'RAID_PROBE_SERVER_COMPLETE'
	Wait-Marker $serverLog 'RAID_HAZARD_RESET_SERVER Removed=1'
	Wait-Marker $serverLog 'RAID_HAZARD_RESET_QUIET Targets=2 HealthUnchanged=1'
	Wait-Marker $serverLog 'RAID_BOSS_TELEGRAPH_ENDED Sequence=1'
	Wait-Marker $serverLog 'RAID_BOSS_ATTACK_DAMAGE Health=69 ServerOnly=1 DuplicateBlocked=1'
	Wait-Marker $firstLog 'RAID_BOSS_ATTACK_REPLICATED Health=69'
	Wait-Marker $serverLog 'RAID_BOSS_TIMED_STRIKE_WINDUP Health=69'
	Wait-Marker $serverLog 'RAID_BOSS_TIMED_STRIKE_DAMAGE Health=38 ServerOnly=1'
	Wait-Marker $serverLog 'RAID_BOSS_TIMED_STRIKE_DODGED Health=38'
	Wait-Marker $serverLog 'RAID_BOSS_TIMED_STRIKE_CANCELLED Health=38 Telegraph=0'
	Wait-Marker $firstLog 'RAID_BOSS_TIMED_STRIKE_REPLICATED Health=38'
    Wait-Marker $serverLog 'RAID_DAMAGE_APPLIED Health=69 Threat=31'
    Wait-Marker $serverLog 'RAID_DAMAGE_STALE_REJECTED Encounter=2 Health=100'
    Wait-Marker $serverLog 'RAID_DAMAGE_LETHAL Health=0 ThreatCleared=1'
    Wait-Marker $serverLog 'RAID_THREAT_SELECTED Priority=1'
    Wait-Marker $serverLog 'RAID_ABILITY_STARTED ServerOnly=1'
    # Intentionally terminate only our first client and wait for server-side disconnect handling.
    $expectedExits.Add($firstProcess.Id) | Out-Null
    Stop-Process -Id $firstProcess.Id
    Wait-Marker $serverLog 'RAID_THREAT_DISCONNECT Fallback=1'
    Wait-Marker $serverLog 'RAID_ABILITY_DISCONNECT Cancelled=1 TaskFailed=1'
    Wait-Marker $serverLog 'RAID_THREAT_RESET Clean=1'
    # Completion markers can precede delayed replication diagnostics. Keep the
    # same fatal/Iris*/Net* contract as the actual-map harness before declaring PASS.
    $settleDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        foreach ($process in $processes) {
            if ($process.HasExited -and -not $expectedExits.Contains($process.Id)) {
                throw "Probe process exited ($($process.ExitCode)); inspect $logRoot"
            }
        }
        foreach ($path in @($serverLog, $firstLog, $lateLog)) {
            if (Select-String -LiteralPath $path -Pattern $runtimeFailurePattern -Quiet) {
                throw "Runtime/replication error in $path"
            }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $settleDeadline)
    if ($moduleHashBefore -ne (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) {
        throw 'Editor module changed during boss state network validation.'
    }
    if ($BossDash) {
        $proof = [ordered]@{
            schemaVersion = 1
            editorModuleSHA256 = $moduleHashBefore
            listenServer = [bool]$ListenServer
            serverLog = $serverLog
            clientLogs = @($firstLog, $lateLog)
            scope = [ordered]@{
                dashStateAndActualMovement = $true
                lateConnectionDuringActiveDash = $true
                damageToTwoAITestTargetsOnceEach = $true
                resetStopsBeforeNaturalExpiry = $true
                testFileRendezvous = $true
                remotePlayerDashDamage = $false
                authoredBossBTDash = $false
                shippingRuntime = $false
            }
        }
        $proof | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $logRoot 'BossDash.proof.json') -Encoding UTF8
        Write-Host "PASS: Iris dash state/movement/damage, active-dash late join and reset cancellation. Module SHA256: $moduleHashBefore"
    }
    Write-Host "PASS: boss state/threat, timed strike, Iris telegraph/HP and local decal presentation, late join, reset/restart/death, BT/GAS disconnect cancellation. Logs: $logRoot"
}
finally {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = $previousSkipSDKSetup }
    foreach ($process in $processes) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
