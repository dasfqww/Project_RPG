<#
.SYNOPSIS
One human + real bot clients in the local raid map. No backend or Steam account required.
.EXAMPLE
.\Scripts\Start-SoloRaid.ps1 -Players 4
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(1, 4)][int]$Players = 4,
    [ValidateRange(1024, 65535)][int]$Port = 18842,
    [ValidateRange(10, 60)][int]$MaxFPS = 30,
    [ValidateRange(30, 900)][int]$StartupTimeoutSeconds = 600,
    [switch]$ShowBots,
    [ValidateRange(0, 3600)][int]$DurationSeconds = 0,
    [switch]$Smoke,
    [switch]$RenderLeader,
    [switch]$HeadlessLeader,
    [ValidateSet('None','Mechanic','HumanMechanic')][string]$Scenario = 'None',
    [ValidateRange(10, 300)][int]$SmokeSeconds = 10
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$project = Join-Path $root 'Project_RPG.uproject'
$module = Join-Path $root 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
if (!(Test-Path -LiteralPath $editor) -or !(Test-Path -LiteralPath $module)) { throw 'Build Project_RPGEditor first.' }
if (![Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($module)).Contains('SOLO_RAID_UI_POLICY')) {
    throw 'Build Project_RPGEditor with the current solo raid panel and bot implementation first.'
}
if ($Smoke -and $Scenario -ne 'None') { throw 'Use either -Smoke or -Scenario, not both.' }
$automateLeader = $Smoke -or $Scenario -eq 'Mechanic'
# Fail before launching any process if another session owns the UDP port.
$probe = [Net.Sockets.UdpClient]::new()
try {
    $probe.ExclusiveAddressUse = $true
    $probe.Client.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, $Port))
} finally { $probe.Dispose() }
$run = Join-Path $root ('Saved\SoloRaid\' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($run) | Out-Null
$control = Join-Path $run 'control.txt'
$owned = [Collections.Generic.List[Diagnostics.Process]]::new()
$previous = @{}
$serial = 0
$success = $false
$failure = $null
$deletedBytes = [long]0
$runStarted = [DateTime]::UtcNow
$cleanupFailures = @()
$retryReady = @()
function Set-PartyMode([string]$Mode) {
    $script:serial++
    $temp = Join-Path $run 'control.next'
    [IO.File]::WriteAllText($temp, "$Mode $script:serial", [Text.UTF8Encoding]::new($false))
    for ($attempt = 0; ; $attempt++) {
        try {
            if (Test-Path -LiteralPath $control) { [IO.File]::Replace($temp, $control, (Join-Path $run 'control.previous')) }
            else { [IO.File]::Move($temp, $control) }
            break
        } catch [IO.IOException] { if ($attempt -ge 20) { throw }; Start-Sleep -Milliseconds 25 }
    }
    Write-Host "Party: $Mode"
}
function Start-RaidProcess([string]$Name, [string]$Destination, [string]$Role, [int]$Bot = -1, [int]$Slot = 0) {
    $log = Join-Path $run ($Name + '.log')
    $userDir = Join-Path $run ($Name + '-User')
    $arguments = @(('"'+$project+'"'), $Destination, $Role, '-RPGSoloRaid', '-RPGNetTestMode',
        ('-RPGSoloRaidControl="'+$control+'"'), ('-RPGSoloRaidBot='+$Bot),
        ('-RPGSoloRaidSlot='+$Slot), ('-RPGSoloRaidPlayers='+$Players),
        '-nosteam', '-NoLoadingScreen', '-DisablePython', '-nosplash', '-nosound',
        '-UseIrisReplication=1', '-RPGRequireIris', '-unattended',
        ('-UserDir="'+$userDir+'"'), ('-abslog="'+$log+'"'),
        ('-ExecCmds="t.MaxFPS '+$MaxFPS+'"'), '-DDC=(InstalledEnginePak,Local,Shared(ReadOnly=true,DeleteUnused=false))',
        ('-SharedDataCachePath="'+(Join-Path $EngineRoot 'Engine\DerivedDataCache')+'"'),
        ('-ShaderWorkingDir="'+(Join-Path $userDir 'Shaders')+'"'),
        ('-LocalDataCachePath="'+(Join-Path $run 'Cache')+'"'))
    $visible = $Role -eq '-game' -and (($RenderLeader -and $Name -eq 'player0') -or (!$automateLeader -and ($Bot -lt 0 -or $ShowBots)))
    if ($HeadlessLeader -and $Name -eq 'player0') { $visible = $false }
    if ($visible) { $arguments += @('-d3d12', '-windowed', '-ResX=1280', '-ResY=720', '-WinX=30', '-WinY=30') }
    else { $arguments += '-nullrhi' }
    $style = if ($visible) { 'Normal' } else { 'Hidden' }
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle $style -PassThru
    $owned.Add($process)
    Write-Host "$Name PID=$($process.Id)"
    return $log
}
function Assert-Healthy {
    foreach ($process in $owned) {
        if ($process.HasExited) { throw "Raid process $($process.Id) exited ($($process.ExitCode)). See $run" }
    }
    foreach ($log in Get-ChildItem -LiteralPath $run -Filter '*.log') {
        if (Select-String -LiteralPath $log.FullName -Pattern 'Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet) {
            throw "Runtime failure in $($log.Name)"
        }
    }
}
function Wait-Marker([string]$Log, [string]$Marker) {
    $end = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $end) {
        Assert-Healthy
        if ((Test-Path -LiteralPath $Log) -and (Select-String -LiteralPath $Log -SimpleMatch $Marker -Quiet)) { return }
        Start-Sleep -Milliseconds 300
    }
    throw "Timed out waiting for $Marker in $Log"
}
function Assert-SmokeCombat {
    $serverText = Get-Content -LiteralPath $server -Raw
    $start = $serverText.IndexOf('SOLO_RAID_MODE fight')
    if ($start -lt 0) { throw 'Server did not start combat.' }
    $before = [regex]::Matches($serverText.Substring(0, $start), 'SOLO_RAID_STATUS Players=\d+ BossHP=([\d.]+)')
    $after = [regex]::Matches($serverText.Substring($start), 'SOLO_RAID_STATUS Players=(\d+) BossHP=([\d.]+)')
    if (!$before.Count -or !$after.Count) { throw 'Combat health observations missing.' }
    $initialHP = [double]::Parse($before[$before.Count-1].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    $damage = @($after | Where-Object {
        [int]$_.Groups[1].Value -eq $Players -and
        [double]::Parse($_.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture) -lt $initialHP
    })
    if ($initialHP -le 0 -or !$damage.Count) { throw 'Clients connected, but no actual boss damage was observed.' }
    for ($i=0; $i -lt $Players; $i++) {
        $log = Join-Path $run ('player'+$i+'.log')
        Wait-Marker $log ('SOLO_RAID_BOT Index='+$i+' Mode=fight')
    }
}
function Set-BotPolicies([string]$Policy) {
    $first = if ($automateLeader) { 0 } else { 1 }
    for ($slot=$first; $slot -lt $Players; $slot++) {
        [IO.File]::WriteAllText((Join-Path $run ('bot'+$slot+'.txt')), $Policy, [Text.UTF8Encoding]::new($false))
    }
}
function Wait-ServerAfter([int]$Offset, [string]$Pattern, [int]$Seconds = 180) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Assert-Healthy
        $text = Get-Content -LiteralPath $server -Raw
        if ($text.Length -gt $Offset -and $text.Substring($Offset) -match $Pattern) { return }
        Start-Sleep -Milliseconds 300
    }
    throw "Scenario timed out: $Pattern. See $run"
}
function Test-MechanicSequence {
    # Failure is caused by real invulnerable-phase timeout, never an injected objective or HP edit.
    Set-BotPolicies 'ignore-mechanic'
    $start = (Get-Content -LiteralPath $server -Raw).Length
    Set-PartyMode fight
    Wait-ServerAfter $start 'SOLO_RAID_MECHANIC .*Active=1 .*Required=2'
    # Holding avoids incidental wide swings killing summons next to the invulnerable boss.
    Set-BotPolicies 'hold'
    Wait-ServerAfter $start 'SOLO_RAID_MECHANIC .*TimedOut=1'
    Wait-ServerAfter $start 'SOLO_RAID_STATUS .*State=2'
    $retry = (Get-Content -LiteralPath $server -Raw).Length
    $oldPawns = @{}
    for ($i=0; $i -lt $Players; $i++) { $oldPawns[$i] = (Get-Content -LiteralPath (Join-Path $run "status$i.json") -Raw -Encoding UTF8 | ConvertFrom-Json).pawn }
    Set-PartyMode restart
    Wait-ServerAfter $retry 'SOLO_RAID_RESTART State=[03]'
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    do {
        Assert-Healthy
        $ready = @()
        for ($i=0; $i -lt $Players; $i++) {
            try {
                $status = Get-Content -LiteralPath (Join-Path $run "status$i.json") -Raw -Encoding UTF8 | ConvertFrom-Json
                if ($status.slot -eq $i -and $status.ready -and $status.hp -gt 0 -and $status.pawn -ne $oldPawns[$i]) { $ready += $status }
            } catch { }
        }
        if ($ready.Count -eq $Players) { $script:retryReady = $ready; break }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Replacement party did not recover equipment/input readiness.' }
        Start-Sleep -Milliseconds 200
    } while ($true)
    Set-BotPolicies 'assist'
    Set-PartyMode fight
    Wait-ServerAfter $retry 'SOLO_RAID_MECHANIC .*Active=1 .*Required=2'
    Wait-ServerAfter $retry 'SOLO_RAID_MECHANIC Phase=2'
    Wait-ServerAfter $retry 'SOLO_RAID_STATUS .*BossHP=0[.0]* .*State=1' 240
    Write-Host 'Natural mechanic failure, restart and victory observed.'
}
try {
    foreach ($name in @('PROJECT_RPG_GAME_SERVER_ID', 'PROJECT_RPG_DUNGEON_SESSION_ID', 'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN')) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $previous['UE_SKIP_UBT_SDK_SETUP'] = [Environment]::GetEnvironmentVariable('UE_SKIP_UBT_SDK_SETUP', 'Process')
    $env:UE_SKIP_UBT_SDK_SETUP = '1'
    Set-PartyMode prepare
    $server = Start-RaidProcess server '/Game/Maps/bossmap' ('-server -port='+$Port+' -MULTIHOME=127.0.0.1')
    Wait-Marker $server 'SOLO_RAID_STATUS'
    $clientLogs = @()
    for ($i = 0; $i -lt $Players; $i++) {
        $bot = if ($i -eq 0 -and !$automateLeader) { -1 } else { $i }
        $client = Start-RaidProcess ('player'+$i) ('127.0.0.1:'+$Port+'?Name=Solo'+$i) '-game' $bot $i
        $clientLogs += $client
    }
    foreach ($client in $clientLogs) { Wait-Marker $client 'SOLO_RAID_CLIENT_READY' }
    Write-Host "Ready: $Players players. Logs: $run"
    if ($Scenario -ne 'None') { Test-MechanicSequence }
    elseif ($Smoke) {
        Set-PartyMode fight
        Start-Sleep -Seconds $SmokeSeconds
        Assert-Healthy
        Assert-SmokeCombat
        Set-PartyMode hold
        Start-Sleep -Seconds 2
        Set-PartyMode spread
        Start-Sleep -Seconds 7
        for ($i=0; $i -lt $Players; $i++) {
            $log = Join-Path $run ('player'+$i+'.log')
            if (!(Select-String -LiteralPath $log -SimpleMatch ('SOLO_RAID_BOT Index='+$i+' Mode=spread') -Quiet)) {
                throw "Player $i did not execute spread movement while alive in combat."
            }
        }
        Set-PartyMode restart
        Wait-Marker $server 'SOLO_RAID_RESTART'
        Start-Sleep -Seconds 2
        Set-PartyMode fight
        Start-Sleep -Seconds 5
        Assert-Healthy
        if (!(Select-String -LiteralPath $server -Pattern 'SOLO_RAID_RESTART State=[03]' -Quiet)) {
            throw 'Server observed the restart command but did not restore the encounter.'
        }
        for ($i=0; $i -lt $Players; $i++) {
            $log = Join-Path $run ('player'+$i+'.log')
            foreach ($mode in @('hold','spread','restart')) { Wait-Marker $log ('SOLO_RAID_MODE '+$mode) }
        }
    } else {
        if ($DurationSeconds -gt 0) { Set-PartyMode fight }
        Write-Host 'Use the in-game panel: F8 open/close, F5 fight, F6 hold, F7 gather, F9 spread, F10 restart.'
        Write-Host 'Optional console fallback: F fight | H hold | G gather | S spread | R restart | Q stop.'
        $end = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
        while ($DurationSeconds -eq 0 -or [DateTime]::UtcNow -lt $end) {
            $controlText = $null
            try { $controlText = Get-Content -LiteralPath $control -Raw } catch { } # A writer may be replacing the file.
            if ($controlText -match '^quit\s') { break }
            if ($owned.Count -gt 1 -and $owned[1].HasExited -and $owned[1].ExitCode -eq 0) { break }
            Assert-Healthy
            if ($DurationSeconds -eq 0 -and ![Console]::IsInputRedirected -and [Console]::KeyAvailable) {
                $key = [Console]::ReadKey($true).Key
                if ($key -eq 'Q') { break }
                switch ($key) {
                    'F' { Set-PartyMode fight }
                    'H' { Set-PartyMode hold }
                    'G' { Set-PartyMode gather }
                    'S' { Set-PartyMode spread }
                    'R' { Set-PartyMode restart }
                }
            }
            Start-Sleep -Milliseconds 200
        }
    }
    $success = $true
} catch { $failure = $_.Exception.Message; throw }
finally {
    # Stop only process objects created by this run, before touching their files.
    foreach ($process in $owned) {
        if (!$process.HasExited) {
            & taskkill.exe /PID $process.Id /T /F | Out-Null
            if (!$process.WaitForExit(15000)) { throw "Could not stop owned process $($process.Id)" }
        }
    }
    # UE's crash monitor can respawn outside the original process tree. Match only
    # this run's monitored process IDs and creation time before releasing dumps.
    $ownedIds = @($owned | ForEach-Object Id)
    foreach ($reporter in Get-CimInstance Win32_Process -Filter "Name='CrashReportClientEditor.exe'") {
        if ($reporter.CreationDate.ToUniversalTime() -ge $runStarted -and
            $reporter.CommandLine -match '-MONITOR=(\d+)(?:\s|$)' -and $ownedIds -contains [int]$Matches[1]) {
            $monitor = Get-Process -Id $reporter.ProcessId -ErrorAction SilentlyContinue
            if ($monitor) { Stop-Process -Id $monitor.Id -Force -ErrorAction SilentlyContinue; $null=$monitor.WaitForExit(5000) }
        }
    }
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
    $fullRun = [IO.Path]::GetFullPath($run)
    $allowed = [IO.Path]::GetFullPath((Join-Path $root 'Saved\SoloRaid')) + '\'
    if (!$fullRun.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup path outside owned workspace.' }
    foreach ($dir in Get-ChildItem -LiteralPath $run -Directory) {
        if ($dir.Name -notmatch '^(server|player\d+)-User$|^Cache$') { continue }
        $all = @(Get-ChildItem -LiteralPath $dir.FullName -Recurse -Force)
        if (($dir.Attributes -band [IO.FileAttributes]::ReparsePoint) -or @($all | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
            throw "Refusing cleanup with reparse points: $($dir.FullName)"
        }
        $deletedBytes += [long](($all | Where-Object { !$_.PSIsContainer } | Measure-Object Length -Sum).Sum)
        try { Remove-Item -LiteralPath $dir.FullName -Recurse -Force }
        catch { $cleanupFailures += $_.Exception.Message }
    }
    # Keep a bounded diagnostic tail and the useful workbench markers, not full repeated logs.
    foreach ($log in Get-ChildItem -LiteralPath $run -Filter '*.log') {
        $lines = @(Get-Content -LiteralPath $log.FullName)
        $keep = @($lines | Where-Object { $_ -match 'SOLO_RAID_|Fatal error:|Assertion failed:|Ensure condition failed:|LogNet.*Error' }) + @($lines | Select-Object -Last 60)
        $before = $log.Length
        [IO.File]::WriteAllLines($log.FullName, [string[]]$keep, [Text.UTF8Encoding]::new($false))
        $deletedBytes += [Math]::Max(0, $before - (Get-Item -LiteralPath $log.FullName).Length)
    }
    [ordered]@{ Success=$success; Failure=$failure; Players=$Players; Smoke=[bool]$Smoke; Scenario=$Scenario;
        LeaderInput=$(if($automateLeader){'Automated Enhanced Input'}else{'Human slot; no bot input'});
        RetryReady=$retryReady;
        ModuleSHA256=(Get-FileHash -LiteralPath $module).Hash; DeletedBytes=$deletedBytes;
        CleanupComplete=($cleanupFailures.Count -eq 0); CleanupFailures=$cleanupFailures;
        ProcessesStopped=$true; Scope='Local raid fixture; no backend persistence or Steam; bots use normal input' } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'Result.json') -Encoding UTF8
    Write-Host "Stopped. Temporary data removed: $deletedBytes bytes. Summary: $run\Result.json"
    if ($cleanupFailures.Count) { Write-Warning ($cleanupFailures -join "`n") }
}
