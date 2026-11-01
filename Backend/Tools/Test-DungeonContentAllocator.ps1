[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [string] $EditorExecutable,
    [string] $ProjectFile
)
$ErrorActionPreference = 'Stop'
$startScript = Join-Path $PSScriptRoot 'Start-LocalDungeonServer.ps1'
$supervisorScript = Join-Path $PSScriptRoot 'Run-LocalDungeonAllocator.ps1'
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$root = (Resolve-Path -LiteralPath $OutputDirectory).Path
$executable = (Get-Process -Id $PID).Path
$savedToken = [Environment]::GetEnvironmentVariable('PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN', 'Process')

foreach ($path in @($startScript, $supervisorScript)) {
    $tokens = $null; $errors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
    if (@($errors).Count) { throw "Allocator parser failure: $path" }
}

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

# Mocks affect only each invocation's child scope. No UE process, real token,
# external HTTP request, or existing allocator state is used by these cases.
$cases = @('SelectedMap', 'DifferentMap', 'MissingMap', 'UnsafeMap', 'LaunchFailure', 'QueueEmpty', 'StartupExited',
    'SelectedIPv6', 'SelectedBracketedIPv6')
if ($EditorExecutable -and $ProjectFile) {
    $cases += @('EditorIdentitySelected', 'EditorDefaultSelected', 'IdentityWithoutEditor', 'IdentityRemoteBackend', 'IdentityRemoteHost', 'IdentityReuseMismatch',
        'EditorShortUser', 'EditorRelativeUser', 'EditorUnsafeUser', 'UserWithoutEditor',
        'EditorIdentityIPv6', 'IdentityRemoteIPv6', 'IdentityHostnameLoopback')
}
foreach ($case in $cases) {
    & {
        $observed = @{ Launches = 0; Finishes = 0; Assignments = 0; Arguments = @(); Env = @{} }
        $sessionId = [Guid]::NewGuid().ToString('D')
        $gameToken = 'fixture-token-not-a-real-credential'
        $mapPackage = switch ($case) {
            'MissingMap' { $null }
            'UnsafeMap' { '/Game/Maps/A?game=Untrusted' }
            default { '/Game/Maps/bossmap' }
        }
        function Invoke-RestMethod {
            param($Method, $Uri, $Headers, $ContentType, $Body)
            if ($Uri.EndsWith('/finish')) { $observed.Finishes++; return @{ state = 'Failed' } }
            if ($case -eq 'QueueEmpty') { return $null }
            $observed.Assignments++
            $assignment = $Body | ConvertFrom-Json
            $observed.ServerAddress = $assignment.serverAddress
            return [pscustomobject]@{
                dungeonSessionId = $sessionId; dungeonId = 'Dungeon.RaidSwordMan'; difficulty = 'Normal'
                serverId = $assignment.serverId; serverAddress = $assignment.serverAddress; state = 'Loading'
                gameServerAccessToken = $gameToken; serverMapPackage = $mapPackage
            }
        }
        function Start-Process {
            param($FilePath, $ArgumentList, $WindowStyle, [switch]$PassThru)
            $observed.Launches++
            $observed.Arguments = $ArgumentList
            $observed.Env.Token = [Environment]::GetEnvironmentVariable('PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN', 'Process')
            $observed.Env.Session = [Environment]::GetEnvironmentVariable('PROJECT_RPG_DUNGEON_SESSION_ID', 'Process')
            $observed.Env.Admin = [Environment]::GetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', 'Process')
            Require ($WindowStyle -ceq 'Hidden') 'Allocator must launch hidden.'
            if ($case -eq 'LaunchFailure') { throw 'Fixture launch failure' }
            return [pscustomobject]@{ Id = 2147483000; StartTime = [DateTime]::UtcNow }
        }
        $arguments = @{
            ServerExecutable = $executable; BackendUrl = 'http://127.0.0.1:39999'
            AdminToken = 'fixture-admin-not-a-real-credential'; LogDirectory = (Join-Path $root $case)
        }
        if ($case -eq 'DifferentMap') { $arguments.Map = '/Game/Maps/testmap' }
        if ($case -eq 'StartupExited') { $arguments.WaitForReady = $true; $arguments.StartupTimeoutSeconds = 1 }
        if ($case -in @('EditorIdentitySelected', 'EditorDefaultSelected', 'IdentityRemoteBackend', 'IdentityRemoteHost', 'EditorShortUser', 'EditorRelativeUser', 'EditorUnsafeUser', 'EditorIdentityIPv6', 'IdentityRemoteIPv6', 'IdentityHostnameLoopback')) {
            $arguments.ServerExecutable = $EditorExecutable; $arguments.EditorProject = $ProjectFile
        }
        if ($case -in @('EditorIdentitySelected', 'IdentityWithoutEditor', 'IdentityRemoteBackend', 'IdentityRemoteHost', 'EditorIdentityIPv6', 'IdentityRemoteIPv6', 'IdentityHostnameLoopback')) {
            $arguments.DevelopmentIdentityTesting = $true
        }
        if ($case -eq 'IdentityRemoteBackend') { $arguments.BackendUrl = 'http://192.0.2.1:39999' }
        if ($case -eq 'IdentityRemoteHost') { $arguments.PublicHost = '192.0.2.1' }
        if ($case -in @('SelectedIPv6', 'EditorIdentityIPv6', 'IdentityRemoteIPv6')) { $arguments.PublicHost = '::1' }
        if ($case -eq 'SelectedBracketedIPv6') { $arguments.PublicHost = '[::1]' }
        if ($case -eq 'EditorIdentityIPv6') { $arguments.BackendUrl = 'http://[::1]:39999' }
        if ($case -eq 'IdentityRemoteIPv6') { $arguments.BackendUrl = 'http://[2001:db8::1]:39999' }
        if ($case -eq 'IdentityHostnameLoopback') { $arguments.BackendUrl = 'http://localhost:39999' }
        if ($case -in @('EditorShortUser', 'UserWithoutEditor')) { $arguments.RuntimeUserDirectory = Join-Path $root 'ShortUser' }
        if ($case -eq 'EditorRelativeUser') { $arguments.RuntimeUserDirectory = 'relative-user' }
        if ($case -eq 'EditorUnsafeUser') { $arguments.RuntimeUserDirectory = (Join-Path $root 'bad"user') }
        if ($case -eq 'IdentityReuseMismatch') {
            $null = New-Item -ItemType Directory -Force -Path $arguments.LogDirectory
            $parentProcess = Get-Process -Id $PID
            @{ processId = $PID; processStartTimeUtcTicks = $parentProcess.StartTime.ToUniversalTime().Ticks;
                dungeonSessionId = $sessionId; mapPackage = '/Game/Maps/bossmap'; developmentIdentityTesting = $true } |
                ConvertTo-Json | Set-Content -LiteralPath (Join-Path $arguments.LogDirectory 'port-7777.allocator.json') -Encoding UTF8
        }
        $failed = $false
        $previousAdmin = [Environment]::GetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', 'Process')
        try {
            [Environment]::SetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', 'fixture-admin-ambient-not-a-real-credential', 'Process')
            try { $result = & $startScript @arguments }
            catch { $failed = $true }
            Require ([Environment]::GetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', 'Process') -ceq 'fixture-admin-ambient-not-a-real-credential') 'Parent allocator admin environment was not restored.'
        }
        finally { [Environment]::SetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', $previousAdmin, 'Process') }
        $shouldFail = $case -notin @('SelectedMap', 'QueueEmpty', 'EditorIdentitySelected', 'EditorDefaultSelected', 'EditorShortUser', 'SelectedIPv6', 'SelectedBracketedIPv6', 'EditorIdentityIPv6')
        Require ($failed -eq $shouldFail) "Unexpected allocator outcome: $case"
        $expectedLaunches = if ($case -in @('SelectedMap', 'LaunchFailure', 'StartupExited', 'EditorIdentitySelected', 'EditorDefaultSelected', 'EditorShortUser', 'SelectedIPv6', 'SelectedBracketedIPv6', 'EditorIdentityIPv6')) { 1 } else { 0 }
        Require ($observed.Launches -eq $expectedLaunches) "Unexpected launch count: $case"
        $preflightReject = $case -in @('IdentityWithoutEditor', 'IdentityRemoteBackend', 'IdentityRemoteHost', 'IdentityReuseMismatch', 'EditorRelativeUser', 'EditorUnsafeUser', 'UserWithoutEditor', 'IdentityRemoteIPv6', 'IdentityHostnameLoopback')
        Require ($observed.Finishes -eq [int]($shouldFail -and -not $preflightReject)) "Assignment failure compensation escaped its scope: $case"
        if ($preflightReject) { Require ($observed.Assignments -eq 0) "Unsafe identity profile reached backend assignment: $case" }
        Require ([Environment]::GetEnvironmentVariable('PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN', 'Process') -ceq $savedToken) 'Parent token environment was not restored.'
        if ($case -in @('SelectedIPv6', 'SelectedBracketedIPv6', 'EditorIdentityIPv6')) {
            Require ($observed.ServerAddress -ceq '[::1]:7777' -and $result.ServerAddress -ceq '[::1]:7777') 'IPv6 assignment did not use brackets.'
            $state=Get-Content -LiteralPath (Join-Path $arguments.LogDirectory 'port-7777.allocator.json') -Raw | ConvertFrom-Json
            Require ($state.serverAddress -ceq '[::1]:7777') 'IPv6 endpoint state did not round-trip.'
            if ($case -eq 'EditorIdentityIPv6') {
                Require ($observed.Arguments -contains '-MULTIHOME=::1') 'IPv6 socket bind must use a raw IP, not host:port brackets.'
                Require ($observed.Arguments -contains '-RPGItemE2EAllowUnverifiedIdentity') 'IPv6 did not preserve explicit identity opt-in.'
            }
        }
        if ($case -eq 'SelectedMap') {
            Require ($observed.Arguments[0] -ceq '/Game/Maps/bossmap?listen') 'Catalog-selected map was not launched.'
            Require (($observed.Arguments -join ' ') -notmatch 'fixture-(token|admin)') 'Credential leaked to arguments.'
            Require ($observed.Arguments -contains '-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=http://127.0.0.1:39999/api') 'Assigned backend URL was not propagated.'
            Require ($observed.Env.Token -ceq $gameToken -and $observed.Env.Session -ceq $sessionId) 'Child assignment scope was not inherited.'
            Require ([string]::IsNullOrEmpty($observed.Env.Admin)) 'Allocator admin token leaked into the gameplay child.'
            Require ($result.MapPackage -ceq '/Game/Maps/bossmap' -and $result.DungeonSessionId -ceq $sessionId) 'Public allocation metadata differs.'
            $state = Get-Content -LiteralPath (Join-Path $arguments.LogDirectory 'port-7777.allocator.json') -Raw | ConvertFrom-Json
            Require ($state.mapPackage -ceq $result.MapPackage -and $state.dungeonId -ceq 'Dungeon.RaidSwordMan') 'Allocator state lost content metadata.'
            Require ((Get-Content -LiteralPath (Join-Path $arguments.LogDirectory 'port-7777.allocator.json') -Raw) -notmatch 'fixture-(token|admin)') 'Credential leaked to allocator state.'
        }
        if ($case -eq 'QueueEmpty') { Require (-not $result.WaitingSessionAvailable) 'Empty queue was treated as an assignment.' }
        if ($case -eq 'EditorShortUser') {
            Require ($observed.Arguments -contains ('-UserDir="'+$arguments.RuntimeUserDirectory+'"')) 'Short UserDir was not forwarded exactly.'
            $state=Get-Content -LiteralPath (Join-Path $arguments.LogDirectory 'port-7777.allocator.json') -Raw | ConvertFrom-Json
            Require ($state.runtimeUserDirectory -ceq $arguments.RuntimeUserDirectory) 'UserDir ownership was not recorded.'
        }
        if ($case -in @('EditorIdentitySelected', 'EditorDefaultSelected')) {
            Require ($observed.Arguments[1] -ceq '/Game/Maps/bossmap?listen') 'Editor profile did not use the assigned map.'
            Require (($observed.Arguments -contains '-RPGItemE2EAllowUnverifiedIdentity') -eq ($case -eq 'EditorIdentitySelected')) 'Steam identity comparison exception was not opt-in.'
            Require (($observed.Arguments -contains '-MULTIHOME=127.0.0.1') -eq ($case -eq 'EditorIdentitySelected')) 'Identity comparison exception must bind only the loopback socket.'
            Require (-not ($observed.Arguments -contains '-RPGNetTestMode')) 'Whole admission bypass must remain disabled.'
        }
        Write-Output "PASS allocator mock: $case"
    }
}
Write-Output 'Scope: parser and mocked launch boundary; not an actual UE/server binary launch.'
