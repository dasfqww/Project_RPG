# Real dedicated-server/client Iris proof on the common development map.
[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(90,600)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024,65000)][int]$Port = 19763,
    [ValidateSet('EarlySelectionFailure','PostReadyFailure','FailureToReadyTravel','LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')]
    [string[]]$Cases = @('EarlySelectionFailure','PostReadyFailure','FailureToReadyTravel')
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$networkRoot = [IO.Path]::GetFullPath($ProjectRoot)
$networkProject = Join-Path $networkRoot 'Project_RPG.uproject'
$networkEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$networkMap = '/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment'
$networkRunId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$networkOutput = Join-Path $networkRoot ('Saved\Logs\ExperienceNetworkTravel-' + $networkRunId)
foreach ($required in @($networkProject,$networkEditor,(Join-Path $networkRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
New-Item -ItemType Directory -Path $networkOutput -Force | Out-Null
$networkResults = @()
$networkProcesses = @()
$networkCaseOutput = ''
$networkCase = ''
$networkCasePort = 0

function Start-ExperienceNetworkRole([string]$Role) {
    $roleOutput = Join-Path $networkCaseOutput $Role
    New-Item -ItemType Directory -Path $roleOutput -Force | Out-Null
    $destination = '127.0.0.1:' + $networkCasePort
    $modeArguments = @('-game','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720')
    if ($Role -eq 'Server') {
        $selection = 'RPGExperienceDefinition:DA_FailureUIAbsent'
        if ($networkCase -in @('PostReadyFailure','LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')) {
            $selection = '/Game/System/Experiences/Development/DA_CommonClientDevelopment.DA_CommonClientDevelopment'
        }
        $destination = $networkMap + '?RPGExperience=' + $selection
        if ($networkCase -in @('LoadingTravel','LoadingSeamlessTravel')) { $destination += '?RPGExperienceProofHold=1' }
        $modeArguments = @('-server','-NullRHI',('-port=' + $networkCasePort))
    }
    $arguments = @($networkProject,$destination) + $modeArguments + @(
        '-unattended','-nop4','-nosplash','-nosound','-nosteam','-NoLiveCoding','-multiprocess','-DisablePython',
        '-RPGExpectedClients=2','-RPGRequireIris','-RPGExperienceNetworkProof',
        ('-RPGExperienceNetworkProofOutput=' + $networkCaseOutput),
        ('-RPGExperienceNetworkProofRole=' + $Role),('-RPGExperienceNetworkProofCase=' + $networkCase),
        '-ExecCmds=RPG.Experience.NetworkProof,t.MaxFPS 30',
        ('-abslog=' + (Join-Path $roleOutput 'Game.log')),('-UserDir=' + (Join-Path $roleOutput 'User')),
        '-DDC=(InstalledEnginePak,ZenLocal,Local)',('-LocalDataCachePath=' + (Join-Path $networkRoot 'Saved\ExperienceNetworkDDC')),
        '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance',
        '-ini:Engine:[/Script/Engine.Engine]:GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient',
        '-ini:Engine:[/Script/Engine.Engine]:LocalPlayerClassName=/Script/Engine.LocalPlayer',
        '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null',
        '-ini:Input:[/Script/Engine.InputSettings]:DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput',
        '-ini:Input:[/Script/Engine.InputSettings]:DefaultInputComponentClass=/Script/Project_RPG.RPGInputComponent'
    )
    if ($networkCase -in @('LoadingTravel','LoadingSeamlessTravel')) {
        $arguments += '-ini:Game:[/Script/Project_RPG.RPGExperienceSettings]:LoadTimeoutSeconds=600'
    }
    if ($networkCase -in @('LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')) {
        $arguments += ('-RPGExperienceNetworkProofTimeout=' + [Math]::Min(900,$TimeoutSeconds * 3))
    }
    $quoted = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw 'Unexpected embedded quote in process argument' }
        '"' + $_ + '"'
    })
    $process = Start-Process -FilePath $networkEditor -ArgumentList $quoted -PassThru -WindowStyle Hidden
    Write-Host ("Started " + $networkCase + '/' + $Role + ", PID " + $process.Id)
    return [PSCustomObject]@{Role=$Role;Process=$process;Output=$roleOutput}
}
function Wait-ExperienceNetworkMarker([string]$Role,[string]$Marker) {
    $path = Join-Path (Join-Path $networkCaseOutput $Role) $Marker
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Test-Path -LiteralPath $path -PathType Leaf) -and (Get-Item -LiteralPath $path).Length -gt 0) {
            Write-Output ("Observed " + $networkCase + '/' + $Role + '/' + $Marker)
            return
        }
        foreach ($entry in $networkProcesses) {
            $entry.Process.Refresh()
            if ($entry.Process.HasExited) { throw ('Probe exited before marker: ' + $entry.Role + ' / ' + $Marker) }
        }
        Start-Sleep -Milliseconds 250
    }
    throw ('Timed out waiting for ' + $Role + '/' + $Marker)
}
function Send-ExperienceNetworkControl([string]$Name) {
    Set-Content -LiteralPath (Join-Path (Join-Path $networkCaseOutput 'Control') $Name) -Value 'Requested by this proof runner' -Encoding UTF8
}
function Get-ExperienceNetworkPort([int]$StartPort) {
    for ($candidate = $StartPort; $candidate -lt $StartPort + 20; ++$candidate) {
        $socket = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork,[Net.Sockets.SocketType]::Dgram,[Net.Sockets.ProtocolType]::Udp)
        try {
            $socket.ExclusiveAddressUse = $true
            $socket.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Any,$candidate))
            return $candidate
        } catch {
            if ($candidate -eq $StartPort + 19) { throw 'No free UDP port in proof range' }
        } finally { $socket.Dispose() }
    }
}
foreach ($networkCase in $Cases) {
    $networkCaseOutput = Join-Path $networkOutput $networkCase
    New-Item -ItemType Directory -Path (Join-Path $networkCaseOutput 'Control') -Force | Out-Null
    $networkCasePort = Get-ExperienceNetworkPort $Port
    $networkProcesses = @()
    try {
        $server = Start-ExperienceNetworkRole 'Server'
        $networkProcesses += $server
        $initial = 'Failed-W1.json'
        if ($networkCase -in @('PostReadyFailure','SeamlessTravel')) { $initial = 'Ready-W1.json' }
        if ($networkCase -in @('LoadingTravel','LoadingSeamlessTravel')) { $initial = 'Loading-W1.json' }
        Wait-ExperienceNetworkMarker 'Server' $initial
        $client = Start-ExperienceNetworkRole 'Client'
        $networkProcesses += $client
        $firstStage = 'Failed-W1.json'
        if ($networkCase -in @('LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')) { $firstStage = $initial }
        Wait-ExperienceNetworkMarker 'Client' $firstStage
        $late = Start-ExperienceNetworkRole 'LateClient'
        $networkProcesses += $late
        Wait-ExperienceNetworkMarker 'LateClient' $firstStage
        if ($networkCase -eq 'FailureToReadyTravel') {
            Send-ExperienceNetworkControl 'Travel1.txt'
            foreach ($role in @('Server','Client','LateClient')) { Wait-ExperienceNetworkMarker $role 'Ready-W2.json' }
            Send-ExperienceNetworkControl 'Travel2.txt'
            foreach ($role in @('Server','Client','LateClient')) { Wait-ExperienceNetworkMarker $role 'Ready-W3.json' }
        }
        if ($networkCase -in @('LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')) {
            Send-ExperienceNetworkControl 'Travel1.txt'
            foreach ($role in @('Server','Client','LateClient')) { Wait-ExperienceNetworkMarker $role 'Ready-W2.json' }
            if ($networkCase -in @('LoadingTravel','LoadingSeamlessTravel')) {
                Send-ExperienceNetworkControl 'ReleaseDeferred.txt'
                foreach ($role in @('Server','Client','LateClient')) { Wait-ExperienceNetworkMarker $role 'Settled-W2.json' }
            }
        }
        foreach ($role in @('Client','LateClient','Server')) {
            $entry = @($networkProcesses | Where-Object { $_.Role -eq $role })[0]
            Send-ExperienceNetworkControl ($role + 'Stop.txt')
            if (-not $entry.Process.WaitForExit(30000)) { throw ('Graceful exit timed out: ' + $role) }
            $entry.Process.Refresh()
            [ordered]@{ExitCode=$entry.Process.ExitCode;CompletedAt=(Get-Date).ToString('o')} |
                ConvertTo-Json | Set-Content -LiteralPath (Join-Path $entry.Output 'ProcessResult.json') -Encoding UTF8
            if ($entry.Process.ExitCode -ne 0) { throw ('Nonzero exit: ' + $role) }
            $report = Get-Content -LiteralPath (Join-Path $entry.Output 'Report.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            $log = Get-Content -LiteralPath (Join-Path $entry.Output 'Game.log') -Raw -Encoding UTF8
            if (-not $report.Passed -or -not $report.Finished) { throw ('Native proof failed: ' + $role) }
            if ($log -match 'Fatal error:|Assertion failed:|Ensure condition failed:|Log(?:Iris\w*|Net\w*):\s*Error:|AddToPlayerScreen Failed') {
                throw ('Fatal/assert/ensure or network error: ' + $role)
            }
            $expectedWorlds = 1
            $expectedReady = 0
            if ($networkCase -eq 'FailureToReadyTravel') { $expectedWorlds = 3; $expectedReady = 2 }
            elseif ($networkCase -in @('LoadingTravel','LoadingSeamlessTravel')) { $expectedWorlds = 2; $expectedReady = 1 }
            elseif ($networkCase -eq 'SeamlessTravel') { $expectedWorlds = 2; $expectedReady = 2 }
            elseif ($networkCase -eq 'PostReadyFailure' -and $role -ne 'LateClient') { $expectedReady = 1 }
            if ($report.Worlds.Count -ne $expectedWorlds -or @($report.Worlds | Where-Object { -not $_.Ended -or -not $_.EndCleanup }).Count -ne 0) {
                throw ('World count or EndPlay cleanup differs: ' + $role)
            }
            if ($networkCase -notin @('LoadingTravel','SeamlessTravel','LoadingSeamlessTravel') -and (-not $report.Worlds[0].FailureRecorded -or -not $report.Worlds[0].ReceivedServerFailureBit)) { throw ('Failure replication missing: ' + $role) }
            if ([regex]::Matches($log,'RPG_EXPERIENCE READY ').Count -ne $expectedReady) { throw ('Unexpected Ready count: ' + $role) }
            if ($networkCase -eq 'PostReadyFailure' -and $role -eq 'Server' -and -not $report.InjectedServerFailure) { throw 'Server-only fault injection did not run' }
            if ($networkCase -in @('LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')) {
                if (-not $report.Worlds[1].ReadyRecorded -or $report.Worlds[1].ReceivedServerFailureBit) { throw ('New travel world is not clean/ready: ' + $role) }
                if ($role -eq 'Server' -and -not $report.Travel1Issued) { throw 'Server did not issue travel' }
                if ($networkCase -ne 'LoadingTravel') {
                    $newReady = @($report.Events | Where-Object { $_.State -eq 'Ready' -and $_.WorldNumber -eq 2 })[0]
                    if (-not $newReady.NetDriverPreserved -or -not $newReady.ConnectionsPreserved -or -not $newReady.LocalControllerReady) { throw ('Seamless transport or local controller binding differs: ' + $role) }
                    if ($role -ne 'Server' -and (-not $newReady.LocalControllerReplaced -or -not $newReady.HUDReplaced -or $newReady.OptionsWidgets -ne 1)) { throw ('GameModeBase replacement controller/HUD UI was not verified: ' + $role) }
                }
                if ($networkCase -ne 'SeamlessTravel') {
                    if (-not $report.Worlds[0].LoadingRecorded -or $report.Worlds[0].SawReady -or -not $report.Worlds[1].SettledRecorded) { throw ('Loading cancellation/late completion proof missing: ' + $role) }
                    $oldSession = $report.Worlds[0].DeferredSession
                    if (-not $oldSession -or [regex]::Matches($log,('RPG_EXPERIENCE PROOF_DEFERRED_RELEASE Session=' + $oldSession + ' [^\r\n]*Current=0 Ending=1')).Count -ne 4) { throw ('Old completion was not rejected four times: ' + $role) }
                    if ($log -notmatch ('RPG_EXPERIENCE ACTIONS_RELEASED Session=' + $oldSession + ' [^\r\n]*Actions=0 ') -or $log -notmatch ('RPG_EXPERIENCE OWNERSHIP_RELEASED Session=' + $oldSession + ' ')) { throw ('Cancelled session ownership was retained: ' + $role) }
                    if (@($report.Events | Where-Object { $_.State -eq 'Settled' -and -not $_.FeaturesActive }).Count -gt 0) { throw ('Old release deactivated new features: ' + $role) }
                }
            }
            if ($networkCase -in @('FailureToReadyTravel','LoadingTravel','SeamlessTravel','LoadingSeamlessTravel')) {
                if ($networkCase -eq 'FailureToReadyTravel' -and (-not $report.Worlds[1].ReadyRecorded -or -not $report.Worlds[2].ReadyRecorded)) { throw ('Travel readiness missing: ' + $role) }
                if ($networkCase -eq 'FailureToReadyTravel' -and $role -eq 'Server' -and (-not $report.Travel1Issued -or -not $report.Travel2Issued)) { throw 'Server did not issue both map reloads' }
                $ready = [regex]::Matches($log,'RPG_EXPERIENCE READY [^\r\n]*Session=(?<session>[A-Fa-f0-9-]+)')
                foreach ($match in $ready) {
                    $session = $match.Groups['session'].Value
                    if ($log -notmatch ("RPG_EXPERIENCE ACTIONS_RELEASED Session=" + $session + " [^\r\n]*Actions=4 ") -or
                        $log -notmatch ("RPG_EXPERIENCE OWNERSHIP_RELEASED Session=" + $session + " ")) { throw ('Session ownership retained: ' + $role) }
                }
            }
        }
        $networkResults += [ordered]@{Case=$networkCase;Passed=$true;Output=$networkCaseOutput;Port=$networkCasePort}
        Write-Output ('Passed ' + $networkCase)
    } finally {
        foreach ($entry in $networkProcesses) {
            if (-not $entry.Process.HasExited) { $entry.Process.Kill(); $entry.Process.WaitForExit() }
            $entry.Process.Dispose()
        }
    }
}
[ordered]@{Passed=$true;Cases=$networkResults;CompletedAt=(Get-Date).ToString('o')} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $networkOutput 'Summary.json') -Encoding UTF8
Write-Output ('Experience network/travel proof passed: ' + $networkOutput)
