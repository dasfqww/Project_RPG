[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [string[]] $CaseName
)
$ErrorActionPreference = 'Stop'
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$runId = [Guid]::NewGuid().ToString('N')
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $outputRoot $runId))
$null = New-Item -ItemType Directory -Path $fixtureRoot
$hostProcess = Get-Process -Id $PID
$executable = $hostProcess.Path
$hostTicks = $hostProcess.StartTime.ToUniversalTime().Ticks
$sessionId = [Guid]::NewGuid().ToString('D')
$passed = @()
$failure = $null
$deletedBytes = 0L
try {
    # Commands are exercised with real state files and process identity, but all
    # network, launch and stop operations are mocked in the invocation scope.
    $cases = @(
        @{ Name='SameBackend'; Stored='http://localhost/game'; Requested='http://localhost/game'; Reject=$false },
        @{ Name='OtherPort'; Stored='http://localhost:3000/game'; Requested='http://localhost:3001/game'; Reject=$true },
        @{ Name='OtherHost'; Stored='http://localhost/game'; Requested='http://127.0.0.1/game'; Reject=$true },
        @{ Name='OtherScheme'; Stored='http://localhost/game'; Requested='https://localhost/game'; Reject=$true },
        @{ Name='OtherPath'; Stored='http://localhost/game'; Requested='http://localhost/other'; Reject=$true },
        @{ Name='PathCase'; Stored='http://localhost/Game'; Requested='http://localhost/game'; Reject=$true },
        @{ Name='Canonical'; Stored='http://LOCALHOST:80/game/'; Requested='http://localhost/game'; Reject=$false },
        @{ Name='Legacy'; Legacy=$true; Requested='http://localhost/game'; Reject=$true },
        @{ Name='InvalidStored'; Stored='not-a-url'; Requested='http://localhost/game'; Reject=$true },
        @{ Name='StoredCredential'; Stored='http://user:fixture-secret@localhost/game'; Requested='http://localhost/game'; Reject=$true },
        @{ Name='StoredQuery'; Stored='http://localhost/game?scope=other'; Requested='http://localhost/game'; Reject=$true },
        @{ Name='RequestedQuery'; Stored='http://localhost/game'; Requested='http://localhost/game?scope=other'; Reject=$true },
        @{ Name='SupervisorLive'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; Reject=$false },
        @{ Name='SupervisorOtherLive'; Supervisor=$true; Stored='http://localhost/other'; Requested='http://localhost/game'; Reject=$true },
        @{ Name='SupervisorOtherStopped'; Supervisor=$true; Stopped=$true; Stored='http://localhost/other'; Requested='http://localhost/game'; Reject=$true },
        @{ Name='SupervisorLegacy'; Supervisor=$true; Legacy=$true; Stopped=$true; Requested='http://localhost/game'; Reject=$true },
        @{ Name='SupervisorInvalidJson'; Supervisor=$true; InvalidJson=$true; Requested='http://localhost/game'; Reject=$true },
        @{ Name='SupervisorStopped'; Supervisor=$true; Stopped=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; Reject=$false },
        @{ Name='SupervisorEmpty'; Supervisor=$true; Empty=$true; Requested='http://localhost/game'; Reject=$false },
        @{ Name='NewAssignment'; Empty=$true; Requested='http://LOCALHOST:80/game/'; Reject=$false },
        @{ Name='ReuseOtherAddress'; Stored='http://localhost/game'; Requested='http://localhost/game'; PublicHost='192.0.2.1'; Reject=$true },
        @{ Name='ReuseOtherAddressWait'; Stored='http://localhost/game'; Requested='http://localhost/game'; PublicHost='192.0.2.1'; Wait=$true; Reject=$true },
        @{ Name='ReuseMissingAddress'; Stored='http://localhost/game'; Requested='http://localhost/game'; MissingAddress=$true; Reject=$true },
        @{ Name='ReuseIPv6'; Stored='http://localhost/game'; Requested='http://localhost/game'; StoredAddress='[::1]:7777'; PublicHost='::1'; Reject=$false },
        @{ Name='ReuseBracketedIPv6'; Stored='http://localhost/game'; Requested='http://localhost/game'; StoredAddress='[::1]:7777'; PublicHost='[::1]'; Reject=$false },
        @{ Name='ReuseOtherIPv6'; Stored='http://localhost/game'; Requested='http://localhost/game'; StoredAddress='[::1]:7777'; PublicHost='2001:db8::1'; Reject=$true },
        @{ Name='ReuseMapMismatch'; Stored='http://localhost/game'; Requested='http://localhost/game'; Map='/Game/Maps/testmap'; Reject=$true },
        @{ Name='ReuseSessionMismatch'; Stored='http://localhost/game'; Requested='http://localhost/game'; OtherSession=$true; Reject=$true },
        @{ Name='ReuseUserDirectoryUnspecified'; Stored='http://localhost/game'; Requested='http://localhost/game'; StoredUserDirectory='C:\fixture\User'; Reject=$false },
        @{ Name='SupervisorOtherAddress'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; PublicHost='192.0.2.1'; Reject=$true },
        @{ Name='SupervisorOtherAddressStopped'; Supervisor=$true; Stopped=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; PublicHost='192.0.2.1'; Reject=$true },
        @{ Name='SupervisorMissingAddress'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; MissingAddress=$true; Reject=$true },
        @{ Name='SupervisorIdentity'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; StoredIdentity=$true; Reject=$true },
        @{ Name='SupervisorProject'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; StoredProject='C:\fixture\Fixture.uproject'; Reject=$true },
        @{ Name='SupervisorMapMismatch'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; Map='/Game/Maps/testmap'; Reject=$true },
        @{ Name='SupervisorMapSame'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; Map='/Game/Maps/bossmap'; Reject=$false },
        @{ Name='SupervisorIPv6'; Supervisor=$true; Stored='http://localhost/game'; Requested='http://localhost/game'; StoredAddress='[::1]:7777'; PublicHost='::1'; Reject=$false }
    )
    if ($CaseName) {
        foreach ($name in $CaseName) {
            if ($name -cnotin $cases.Name) { throw "Unknown allocator scope case: $name" }
        }
        $cases = @($cases | Where-Object { $_.Name -cin $CaseName })
    }
    foreach ($case in $cases) {
        & {
            $caseRoot = Join-Path $fixtureRoot $case.Name
            $null = New-Item -ItemType Directory -Path $caseRoot
            $statePath = Join-Path $caseRoot 'port-7777.allocator.json'
            if (-not $case.Empty) {
                $state = [ordered]@{
                    processId = $(if ($case.Stopped) { 2147483000 } else { $PID })
                    processStartTimeUtcTicks = $hostTicks
                    backendUrl = $case.Stored
                    editorProject = $null; developmentIdentityTesting = $false
                    dungeonSessionId = $sessionId; serverId = 'fixture-server'
                    serverAddress = '127.0.0.1:7777'; dungeonId = 'Dungeon.RaidSwordMan'
                    difficulty = 'Normal'; mapPackage = '/Game/Maps/bossmap'
                    executable = $executable; logPath = (Join-Path $caseRoot 'server.log')
                }
                if ($case.StoredAddress) { $state.serverAddress = $case.StoredAddress }
                if ($case.MissingAddress) { $state.Remove('serverAddress') }
                if ($case.StoredIdentity) { $state.developmentIdentityTesting = $true }
                if ($case.StoredProject) { $state.editorProject = $case.StoredProject }
                if ($case.StoredUserDirectory) { $state.runtimeUserDirectory = $case.StoredUserDirectory }
                if ($case.Legacy) { $state.Remove('backendUrl') }
                if ($case.InvalidJson) { '{' | Set-Content -LiteralPath $statePath }
                else { $state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8 }
            }
            $beforeHash = if (Test-Path -LiteralPath $statePath) { (Get-FileHash -LiteralPath $statePath).Hash } else { $null }
            $calls = @{ Queries=0; Claims=0; Finishes=0; Launches=0; Stops=0 }
            function Get-Process {
                [CmdletBinding()] param([int]$Id)
                if ($Id -ne $PID) { throw 'Fixture process has exited' }
                return $hostProcess
            }
            function Stop-Process {
                [CmdletBinding()] param($Id, [switch]$Force)
                $calls.Stops++; throw 'No real process may be stopped by these tests'
            }
            function Start-Process {
                param($FilePath, $ArgumentList, $WindowStyle, [switch]$PassThru)
                $calls.Launches++
                return $hostProcess
            }
            function Invoke-RestMethod {
                param($Method, $Uri, $Headers, $ContentType, $Body)
                if ($Method -eq 'Get') { $calls.Queries++; return @{ serverId='fixture-server'; state='InProgress' } }
                if ($Uri.EndsWith('/finish')) { $calls.Finishes++; return @{ state='Failed' } }
                $calls.Claims++
                if ($case.Name -ne 'NewAssignment') { return $null }
                $assignment = $Body | ConvertFrom-Json
                return @{ dungeonSessionId=$sessionId; serverId=$assignment.serverId; serverAddress=$assignment.serverAddress
                    state='Loading'; dungeonId='Dungeon.RaidSwordMan'; difficulty='Normal'
                    gameServerAccessToken='fixture-secret'; serverMapPackage='/Game/Maps/bossmap' }
            }
            $arguments = @{ ServerExecutable=$executable; BackendUrl=$case.Requested
                AdminToken='fixture-admin'; LogDirectory=$caseRoot }
            if ($case.PublicHost) { $arguments.PublicHost = $case.PublicHost }
            if ($case.Map) { $arguments.Map = $case.Map }
            if ($case.OtherSession) { $arguments.DungeonSessionId = '00000000-0000-0000-0000-000000000001' }
            if ($case.Wait) { $arguments.WaitForReady = $true }
            $rejected = $false; $message = ''; $result = $null
            try {
                if ($case.Supervisor) {
                    $result = @(& (Join-Path $PSScriptRoot 'Run-LocalDungeonAllocator.ps1') @arguments -PortCount 1 -RunOnce)
                    $rejected = @($result | Where-Object { $_.Event -eq 'Error' }).Count -gt 0
                } else { $result = & (Join-Path $PSScriptRoot 'Start-LocalDungeonServer.ps1') @arguments }
            } catch { $rejected=$true; $message=$_.Exception.Message }
            if ($rejected -ne $case.Reject) { throw "Wrong allocator-scope decision: $($case.Name)" }
            if ($calls.Stops -ne 0) { throw "Process preservation failed: $($case.Name)" }
            if ($case.Reject) {
                if (($calls.Values | Measure-Object -Sum).Sum -ne 0) { throw "Rejected state reached a side effect: $($case.Name)" }
                if ($message.Contains('fixture-secret')) { throw 'Rejected URL leaked credentials' }
            } elseif ($case.Name -eq 'NewAssignment') {
                if ($result.Reused -or $calls.Claims -ne 1 -or $calls.Launches -ne 1) { throw 'New assignment did not launch once' }
                $saved = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
                if ($saved.backendUrl -cne 'http://localhost/game') { throw 'New state lost canonical backend identity' }
                if ((Get-Content -LiteralPath $statePath -Raw).Contains('fixture-secret')) { throw 'State leaked server credential' }
            } elseif ($case.Name -eq 'SupervisorStopped') {
                if ($calls.Queries -ne 1 -or $calls.Finishes -ne 1 -or $calls.Claims -ne 1) { throw 'Correctly scoped stopped-server reconciliation changed' }
            } elseif ($case.Name -eq 'SupervisorEmpty') {
                if ($calls.Claims -ne 1 -or $calls.Queries -ne 0 -or $calls.Finishes -ne 0) { throw 'Empty port could not claim work' }
            } else {
                if (($calls.Values | Measure-Object -Sum).Sum -ne 0) { throw 'Reuse unexpectedly changed the process/backend' }
                if ($case.Supervisor) {
                    if (@($result | Where-Object { $_.Event -eq 'Running' }).Count -ne 1) { throw 'Matching live server was not observed' }
                } elseif (-not $result.Reused) { throw 'Matching live server was not reused' }
            }
            if ($beforeHash -and $beforeHash -ne (Get-FileHash -LiteralPath $statePath).Hash) { throw 'Existing allocator state was overwritten' }
        }
        $passed += $case.Name
        Write-Output ('PASS allocator scope: ' + $case.Name)
    }
} catch { $failure = $_ }
finally {
    # This directory was created by this invocation; no links or external trees.
    $expected = [IO.Path]::GetFullPath((Join-Path $outputRoot $runId))
    if ($fixtureRoot -ne $expected -or $runId -notmatch '^[a-f0-9]{32}$') { throw 'Fixture cleanup boundary mismatch' }
    $pending = New-Object 'Collections.Generic.Stack[string]'
    $pending.Push($fixtureRoot)
    while ($pending.Count -gt 0) {
        $directory = Get-Item -LiteralPath $pending.Pop() -Force
        if ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing reparse-point cleanup' }
        foreach ($file in @(Get-ChildItem -LiteralPath $directory.FullName -Force)) {
            if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing a reparse-point fixture child' }
            if ($file.PSIsContainer) { $pending.Push($file.FullName) } else { $deletedBytes += $file.Length }
        }
    }
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    [ordered]@{ passed=($null -eq $failure); cases=$passed; count=$passed.Count; mocked=$true
        ueRun=$false; realHttp=$false; deletedBytes=$deletedBytes; tempRemoved=(-not (Test-Path -LiteralPath $fixtureRoot))
        failure=$(if($failure){$failure.Exception.Message}else{$null}) } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot 'AllocatorScope.Result.json') -Encoding UTF8
}
if ($failure) { throw $failure }
