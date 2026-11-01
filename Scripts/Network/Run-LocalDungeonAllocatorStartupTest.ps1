[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:/UE_5.8',
    [ValidateRange(1024, 65535)] [int]$Port = 18885,
    [ValidateRange(30, 600)] [int]$TimeoutSeconds = 240,
    [switch]$LobbyEntry,
    [switch]$DepartureResponseLoss,
    [switch]$DepartureRestart,
    [ValidateSet('Cancel', 'Request')] [string]$DepartureLossOperation = 'Cancel',
    [ValidateSet('AfterCommit', 'BeforeCommit')] [string]$DepartureLossPoint = 'AfterCommit',
    [ValidateRange(2, 4)] [int]$ClientCount = 2
)
$ErrorActionPreference = 'Stop'
# ValidateSet is case-insensitive; the fixture's wire contract is canonical.
$DepartureLossOperation = if ($DepartureLossOperation -ieq 'Request') { 'Request' } else { 'Cancel' }
$DepartureLossPoint = if ($DepartureLossPoint -ieq 'BeforeCommit') { 'BeforeCommit' } else { 'AfterCommit' }
$precommitLoss = $DepartureLossPoint -ceq 'BeforeCommit'
if ($DepartureResponseLoss -and (-not $LobbyEntry -or $ClientCount -ne 2)) {
    throw 'DepartureResponseLoss requires exactly two LobbyEntry clients and stops in the lobby before allocation/combat.'
}
if (-not $DepartureResponseLoss -and $DepartureLossOperation -ne 'Cancel') {
    throw 'DepartureLossOperation Request requires DepartureResponseLoss.'
}
if ($DepartureRestart -and -not $DepartureResponseLoss) {
    throw 'DepartureRestart requires DepartureResponseLoss.'
}
if ($precommitLoss -and (-not $DepartureRestart -or -not $DepartureResponseLoss)) {
    throw 'BeforeCommit requires DepartureRestart and DepartureResponseLoss.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project = Join-Path $projectRoot 'Project_RPG.uproject'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$module = Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
$startScript = Join-Path $projectRoot 'Backend/Tools/Start-LocalDungeonServer.ps1'
$supervisorScript = Join-Path $projectRoot 'Backend/Tools/Run-LocalDungeonAllocator.ps1'
$runId = [Guid]::NewGuid().ToString('N')
$root = Join-Path $projectRoot "Saved/Tests/LocalDungeonAllocator/$runId"
$serverRoot = Join-Path $root 'server'
$buildRoot = Join-Path $root 'backend-build'
$null = New-Item -ItemType Directory -Force -Path $buildRoot
$null = New-Item -ItemType Directory -Force -Path $serverRoot
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
foreach ($path in @($project, $editor, $module, $startScript, $supervisorScript)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing prerequisite: $path" }
}
if (@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveUdpListeners() | Where-Object Port -eq $Port).Count) {
    throw 'Requested UE port is already in use.'
}
# Normalize launchers which provide both PATH and Path; restore the effective value.
$environment = [Environment]::GetEnvironmentVariables('Process')
$pathKeys = @($environment.Keys | Where-Object { $_.ToString() -ieq 'Path' })
if ($pathKeys.Count -gt 1) {
    $pathValue = [string]$environment[$pathKeys[0]]
    foreach ($key in $pathKeys) { [Environment]::SetEnvironmentVariable($key.ToString(), $null, 'Process') }
    [Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')
}
$sourcePaths = @(
    'Backend/Tools/Start-LocalDungeonServer.ps1', 'Backend/Tools/Run-LocalDungeonAllocator.ps1',
    'Backend/Tools/LocalDungeonServerReadiness.ps1', 'Backend/Tools/Test-DungeonContentAllocator.ps1',
    'Backend/Tools/Test-LocalDungeonServerReadiness.ps1', 'Scripts/Network/Run-LocalDungeonAllocatorStartupTest.ps1',
    'Backend/ProjectRpg.Backend/Program.cs', 'Backend/ProjectRpg.Backend/appsettings.json',
    'Backend/ProjectRpg.Backend/Application/DungeonContentCatalog.cs',
    'Backend/Database/schema.sql', 'Backend/ProjectRpg.Backend/Domain/DomainModels.cs',
    'Backend/ProjectRpg.Backend/Domain/DungeonDeparturePolicy.cs',
    'Backend/ProjectRpg.Backend/Api/DungeonDepartureApiEndpoints.cs',
    'Backend/ProjectRpg.Backend/Contracts/ApiContracts.cs', 'Backend/ProjectRpg.Backend/Contracts/DungeonSessionResponses.cs',
    'Backend/ProjectRpg.Backend/Configuration/BackendOptions.cs', 'Backend/ProjectRpg.Backend/Data/IGameRepository.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.cs', 'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.Departure.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.cs', 'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.Departure.cs',
    'Backend/Tools/Request-DungeonDeparture.ps1',
    'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp', 'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h',
    'Source/Project_RPG/Private/Online/RPGDungeonServerDefinition.cpp', 'Source/Project_RPG/Public/Online/RPGDungeonServerDefinition.h',
    'Source/Project_RPG/Private/Manager/HttpWebManager.cpp',
    'Source/Project_RPG/Private/UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.cpp',
    'Source/Project_RPG/Public/Manager/HttpWebManager.h',
    'Source/Project_RPG/Public/Online/RPGDungeonSessionSubsystem.h',
    'Source/Project_RPG/Public/Online/RPGDungeonDepartureJournal.h',
    'Source/Project_RPG/Private/Online/RPGDungeonDepartureJournal.cpp',
    'Source/Project_RPG/Private/Online/RPGDungeonDepartureRecovery.cpp',
    'Source/Project_RPG/Private/Online/RPGOnlineLobbySubsystem.cpp',
    'Config/DefaultEngine.ini', 'Config/DefaultGame.ini',
    'Content/System/Dungeons/DA_DungeonServer_RaidSwordMan.uasset',
    'Content/Blueprints/GameMode/BP_BossBattleGameMode.uasset', 'Content/Maps/bossmap.umap'
)
if ($LobbyEntry) {
    $sourcePaths += @(
        'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.cpp',
        'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.h',
        'Source/Project_RPG/Private/Tests/RPGDungeonDepartureRestartE2E.cpp',
        'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp',
        'Source/Project_RPG/Private/Player/RPGPlayerState.cpp',
        'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyDungeonSelection.cpp',
        'Source/Project_RPG/Public/UI/Lobby/RPGOnlineLobbyWidget.h',
        'Source/Project_RPG/Private/Online/RPGDungeonDefinition.cpp',
        'Source/Project_RPG/Public/Online/RPGDungeonDefinition.h',
        'Content/System/Dungeons/DA_Dungeon_RaidSwordMan.uasset',
        'Content/Maps/LobbyMap.umap', 'Backend/seed-game-economy.ps1'
    )
}
if ($DepartureResponseLoss) {
    $sourcePaths += @('Scripts/Network/Fixtures/DungeonDepartureHttpFaultProxy.mjs',
        'Scripts/Network/Fixtures/DungeonDepartureHttpFaultProxy.test.mjs',
        'Backend/ProjectRpg.Backend/Api/PlayerLogoutApiEndpoints.cs',
        'Backend/ProjectRpg.Backend/Authentication/ApiAuthenticationMiddleware.cs')
}
function Get-Inputs {
    @($sourcePaths | ForEach-Object { [pscustomobject]@{ Path = $_; Sha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot $_)).Hash } })
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$before = Get-Inputs
$moduleBefore = (Get-FileHash -LiteralPath $module).Hash
$proof = [ordered]@{ Passed = $false; Scope = 'Development Editor dedicated server + real allocator claim/UE content readiness; isolated HTTP Memory; no clients/combat/rewards/Steam/packaged Server'; Inputs = $before; ModuleSha256 = $moduleBefore }
if ($LobbyEntry) { $proof.Scope = 'Development no-Steam live lobby create/join -> automatic allocator -> actual client ticket admission/Iris; isolated HTTP Memory; no combat/rewards/PG/Steam/packaged Server' }
if ($DepartureResponseLoss) {
    $proof.Scope = "Two actual development lobby clients; committed $DepartureLossOperation 200 loss -> own credential revoked -> real 401 -> same-account UI login -> original command replay; HTTP Memory, no game server/admission/combat/rewards/PG/Steam/Shipping"
    $proof.DepartureLossOperation = $DepartureLossOperation
    $proof.DepartureLossPoint = $DepartureLossPoint
}
if ($DepartureRestart) {
    $proof.Scope = "Two development lobby clients; committed $DepartureLossOperation reply loss -> force-stop only owned leader -> fresh UE process/login/roster -> disk restore and authenticated owner read; HTTP Memory, no mutation replay/game server/combat/rewards/PG/Steam/Shipping"
    if ($precommitLoss) { $proof.Scope = "Two development lobby clients; $DepartureLossOperation dropped before forwarding -> owned leader restart/fresh login -> unchanged owner read -> explicit retry of original command -> first commit; HTTP Memory, no game server/combat/rewards/PG/Steam/Shipping" }
}
$backend = $null; $serverState = $null; $serverProcess = $null; $sessionId = $null
$clients = [Collections.Generic.List[object]]::new()
$expectedMembers = if ($LobbyEntry) { $ClientCount } else { 1 }
$sessionFile = Join-Path $root 'lobby-session.txt'
$entryGate = Join-Path $root 'server-ready.gate'
$departureGate = Join-Path $root 'departure-ready.gate'
$recoveryRetryGate = Join-Path $root 'departure-retry.gate'
$commitRetryGate = Join-Path $root 'departure-commit-retry.gate'
$departureProxy = $null; $departureProxyUrl = $null; $departureControlUrl = $null; $departureControlHeaders = $null
$passed = $false
$envNames = @('ASPNETCORE_ENVIRONMENT', 'DOTNET_ENVIRONMENT', 'PROJECT_RPG_BACKEND_ADMIN_TOKEN', 'POSTGRES_CONNECTION_STRING',
    'PROJECT_RPG_DEPARTURE_PROXY_TOKEN','PROJECT_RPG_DEPARTURE_PROXY_UPSTREAM_PORT')
$savedEnvironment = @{}
foreach ($name in $envNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Api([string]$Method, [string]$Route, $Body, [hashtable]$Headers) {
    $requestArgs = @{ Method = $Method; Uri = "$baseUrl/api/$Route"; Headers = $Headers; TimeoutSec = 5 }
    if ($null -ne $Body) { $requestArgs.Body = $Body | ConvertTo-Json -Compress; $requestArgs.ContentType = 'application/json' }
    Invoke-RestMethod @requestArgs
}
function Get-StableDepartureState($Snapshot) {
    $copy = $Snapshot | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    foreach ($name in @('lobbyRevision','departureRequestId','departureRequestRevision','departureRequestedAt')) {
        $copy.PSObject.Properties.Remove($name)
    }
    $copy | ConvertTo-Json -Depth 10 -Compress
}
function Wait-Marker([string]$LogPath, [string]$Marker) {
    $wait = [Diagnostics.Stopwatch]::StartNew()
    while ($wait.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        foreach ($client in $clients) {
            if ($client.Process.HasExited) { throw 'A lobby client exited before admission.' }
            if ((Test-Path -LiteralPath $client.Log) -and
                (Select-String -LiteralPath $client.Log -Pattern 'DUNGEON_FLOW_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet)) {
                throw 'A lobby client reported a flow/runtime failure; inspect its preserved log.'
            }
        }
        if ((Test-Path -LiteralPath $LogPath) -and (Select-String -LiteralPath $LogPath -Pattern $Marker -SimpleMatch -Quiet)) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "Missing expected lobby/admission marker; inspect $LogPath"
}
function Start-LobbyClient($Member, [int]$Index, [switch]$Restart) {
    $log = Join-Path $root $(if ($Restart) { "client$Index-restart.log" } else { "client$Index.log" })
    $runtime = Join-Path $projectRoot ("Saved/Tests/LAE/" + $runId.Substring(0, 12) + "/c$Index")
    $webApiBase = if ($DepartureResponseLoss -and $Index -eq 1) { $departureProxyUrl } else { $baseUrl }
    $arguments = @(('"' + $project + '"'), '/Game/Maps/LobbyMap', '-game',
        '-RPGDungeonFlowE2E', '-RPGDevelopmentLogin', '-RPGFlowE2ELobbyUI', '-RPGFlowE2EAdmissionOnly',
        '-RPGFlowE2ELobbyDifficulty=Normal', "-RPGFlowE2ESteam=$($Member.SteamId)", "-RPGFlowE2ECharacter=$($Member.CharacterId)",
        ('-RPGFlowE2ESessionFile="' + $sessionFile + '"'), ('-RPGFlowE2EServerReadyFile="' + $entryGate + '"'),
        '-RPGFlowE2EDepartureGate', ('-RPGFlowE2EDepartureGateFile="' + $departureGate + '"'), "-RPGFlowE2EPlayers=$ClientCount",
        '-UseIrisReplication=1', '-RPGRequireIris', '-unattended', '-nop4', '-nosplash', '-nosound',
        '-nosteam', '-NoLiveCoding', '-NoLoadingScreen', '-DisablePython', '-nullrhi', '-DDC=(EnginePak,Local)',
        ('-UserDir="' + $runtime + '"'), ('-abslog="' + $log + '"'), '-ExecCmds="t.MaxFPS 30"',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$webApiBase/api",
        "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$baseUrl/api",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$baseUrl/api")
    if ($Index -eq 1) { $arguments += '-RPGFlowE2ELobbyLeader' }
    if ($DepartureResponseLoss -and $Index -eq 1) {
        $arguments += @('-RPGFlowE2EDepartureRecovery', ('-RPGFlowE2ERecoveryRetryFile="' + $recoveryRetryGate + '"'))
        if ($DepartureLossOperation -eq 'Request') { $arguments += '-RPGFlowE2EDepartureRequestRecovery' }
        if ($DepartureRestart) {
            if ($Restart) {
                $arguments += @('-RPGFlowE2EDepartureRestartRecovery', "-RPGFlowE2ESession=$sessionId")
                if ($precommitLoss) { $arguments += @('-RPGFlowE2EDeparturePrecommitRecovery', ('-RPGFlowE2EDepartureCommitRetryFile="' + $commitRetryGate + '"')) }
            }
            else { $arguments += '-RPGFlowE2EDeparturePrepareRestart' }
        }
    }
    $credentialNames = @('PROJECT_RPG_BACKEND_ADMIN_TOKEN', 'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
        'PROJECT_RPG_GAME_SERVER_ID', 'PROJECT_RPG_DUNGEON_SESSION_ID',
        'PROJECT_RPG_DEPARTURE_PROXY_TOKEN','PROJECT_RPG_DEPARTURE_PROXY_UPSTREAM_PORT')
    $savedCredentials = @{}
    try {
        foreach ($name in $credentialNames) {
            $savedCredentials[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru -WorkingDirectory $projectRoot
    }
    finally { foreach ($name in $credentialNames) { [Environment]::SetEnvironmentVariable($name, $savedCredentials[$name], 'Process') } }
    $clients.Add([pscustomobject]@{ Process = $process; StartTicks = $process.StartTime.ToUniversalTime().Ticks; Log = $log })
}
try {
    foreach ($path in $sourcePaths | Where-Object { $_.EndsWith('.ps1') }) {
        $tokens = $null; $errors = $null
        $null = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot $path), [ref]$tokens, [ref]$errors)
        Require (@($errors).Count -eq 0) "Parser failure: $path"
    }
    & (Join-Path $projectRoot 'Backend/Tools/Test-DungeonContentAllocator.ps1') -OutputDirectory (Join-Path $root 'allocator-mocks') `
        -EditorExecutable $editor -ProjectFile $project *> (Join-Path $root 'allocator-mocks.log')
    & (Join-Path $projectRoot 'Backend/Tools/Test-LocalDungeonServerReadiness.ps1') -OutputDirectory (Join-Path $root 'readiness-mocks') *> (Join-Path $root 'readiness-mocks.log')
    & $dotnet build (Join-Path $projectRoot 'Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj') -c Release --output $buildRoot --nologo -v minimal *> (Join-Path $root 'backend-build.log')
    Require ($LASTEXITCODE -eq 0) 'Isolated backend build failed.'
    $backendDll = Join-Path $buildRoot 'ProjectRpg.Backend.dll'
    $proof.BackendSha256 = (Get-FileHash -LiteralPath $backendDll).Hash
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start(); $backendPort = $listener.LocalEndpoint.Port; $listener.Stop()
    $baseUrl = "http://127.0.0.1:$backendPort"
    $adminToken = [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
    $adminHeaders = @{ Authorization = "Bearer $adminToken" }
    [Environment]::SetEnvironmentVariable('ASPNETCORE_ENVIRONMENT', 'Development', 'Process')
    [Environment]::SetEnvironmentVariable('DOTNET_ENVIRONMENT', 'Development', 'Process')
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', $adminToken, 'Process')
    [Environment]::SetEnvironmentVariable('POSTGRES_CONNECTION_STRING', $null, 'Process')
    $backendArgs = @(('"' + $backendDll + '"'), '--Storage:Provider=Memory', '--Steam:AllowDevelopmentAuthentication=true', "--Kestrel:Endpoints:Http:Url=$baseUrl")
    $backend = Start-Process -FilePath $dotnet -ArgumentList $backendArgs -WorkingDirectory $buildRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $root 'backend.stdout.log') -RedirectStandardError (Join-Path $root 'backend.stderr.log')
    $ready = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        if ($backend.HasExited) { throw 'Isolated backend exited before readiness.' }
        try { $null = Invoke-RestMethod "$baseUrl/health/ready" -TimeoutSec 1; $ready = $true; break } catch { Start-Sleep -Milliseconds 250 }
    }
    Require $ready 'Backend readiness timed out.'
    if ($DepartureResponseLoss) {
        $proxyScript = Join-Path $projectRoot 'Scripts/Network/Fixtures/DungeonDepartureHttpFaultProxy.mjs'
        $proxyOut = Join-Path $root 'departure-proxy.stdout.log'; $proxyErr = Join-Path $root 'departure-proxy.stderr.log'
        $controlToken = [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
        try {
            [Environment]::SetEnvironmentVariable('PROJECT_RPG_DEPARTURE_PROXY_TOKEN', $controlToken, 'Process')
            [Environment]::SetEnvironmentVariable('PROJECT_RPG_DEPARTURE_PROXY_UPSTREAM_PORT', $backendPort.ToString(), 'Process')
            $departureProxy = Start-Process -FilePath (Get-Command node -ErrorAction Stop).Source -ArgumentList ('"' + $proxyScript + '"') -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $proxyOut -RedirectStandardError $proxyErr
        }
        finally {
            foreach ($name in @('PROJECT_RPG_DEPARTURE_PROXY_TOKEN','PROJECT_RPG_DEPARTURE_PROXY_UPSTREAM_PORT')) {
                [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
            }
        }
        $proxyReady = $false
        for ($attempt = 0; $attempt -lt 120; $attempt++) {
            if ($departureProxy.HasExited) { throw 'Departure proxy exited before readiness.' }
            if (Test-Path -LiteralPath $proxyOut) {
                $ports = Select-String -LiteralPath $proxyOut -Pattern 'DEPARTURE_PROXY_READY ProxyPort=(\d+) ControlPort=(\d+)' | Select-Object -First 1
                if ($ports) {
                    $departureProxyUrl = "http://127.0.0.1:$($ports.Matches[0].Groups[1].Value)"
                    $departureControlUrl = "http://127.0.0.1:$($ports.Matches[0].Groups[2].Value)"
                    $departureControlHeaders = @{Authorization="Bearer $controlToken"}; $proxyReady = $true; break
                }
            }
            Start-Sleep -Milliseconds 250
        }
        Require $proxyReady 'Departure proxy readiness timed out.'
    }
    if ($LobbyEntry) {
        & (Join-Path $projectRoot 'Backend/seed-game-economy.ps1') -BaseUrl $baseUrl -AdminToken $adminToken | Out-Null
        $members = @(for ($index = 0; $index -lt $ClientCount; $index++) {
            $steam = ([uint64]76561198000999882 + [uint64]$index).ToString()
            $login = Api Post 'auth/steam-ticket' @{ ticket = "dev:$steam" } @{}
            $headers = @{ Authorization = "Bearer $($login.accessToken)" }
            $character = Api Post 'characters' @{ name = ('Alloc' + $runId.Substring(0, 10) + $index) } $headers
            [pscustomobject]@{ SteamId = $steam; CharacterId = [string]$character.characterId; Headers = $headers }
        })
        for ($index = 0; $index -lt $members.Count; $index++) { Start-LobbyClient $members[$index] ($index + 1) }
        Wait-Marker $clients[0].Log 'DUNGEON_FLOW_E2E_LOBBY_SESSION Created=1 Session='
        $createdLine = Select-String -LiteralPath $clients[0].Log -Pattern 'DUNGEON_FLOW_E2E_LOBBY_SESSION Created=1 Session=([0-9a-fA-F-]{36})' | Select-Object -Last 1
        Require ($null -ne $createdLine) 'Live lobby did not return a canonical session.'
        $sessionId = $createdLine.Matches[0].Groups[1].Value
        $sessionId | Set-Content -LiteralPath $sessionFile -Encoding UTF8
        foreach ($client in @($clients | Select-Object -Skip 1)) { Wait-Marker $client.Log 'DUNGEON_FLOW_E2E_LOBBY_SESSION Joined=1 Session=' }
        foreach ($client in @($clients | Select-Object -Skip 1)) { Wait-Marker $client.Log 'DUNGEON_DEPARTURE_UI NONLEADER_LOCKED RequestCancel=0' }
        $created = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
        Require ($created.state -ceq 'Waiting' -and $created.dungeonId -ceq 'Dungeon.RaidSwordMan' -and
            $created.difficulty -ceq 'Normal' -and $created.members.Count -eq $ClientCount -and
            @($members | Where-Object { $_.CharacterId -cnotin @($created.members | ForEach-Object characterId) }).Count -eq 0) 'UI-created party did not reach the exact backend Waiting session.'
        # Windows PowerShell returns an empty string, not necessarily $null,
        # for HTTP 204. Assert the status and the unchanged authoritative lobby.
        $earlyBody = @{ serverId = 'early-claim-denied'; serverAddress = "127.0.0.1:$Port" } | ConvertTo-Json -Compress
        $early = Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$baseUrl/api/dungeon-sessions/claim" `
            -Headers $adminHeaders -ContentType 'application/json' -Body $earlyBody -TimeoutSec 5
        $stillRecruiting = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
        Require ([int]$early.StatusCode -eq 204 -and $stillRecruiting.state -ceq 'Waiting' -and
            -not $stillRecruiting.serverId -and -not $stillRecruiting.departureRequestedAt) 'Allocator took a recruiting lobby before leader departure.'
        $proof.RecruitingClaimStatus = [int]$early.StatusCode
        if ($DepartureResponseLoss) {
            $armBody = @{sessionId=$sessionId;characterId=$members[0].CharacterId;operation=$DepartureLossOperation;lossPoint=$DepartureLossPoint} | ConvertTo-Json -Compress
            $null = Invoke-RestMethod -Method Post -Uri "$departureControlUrl/arm" -Headers $departureControlHeaders -ContentType 'application/json' -Body $armBody
        }
        $null = New-Item -ItemType File -Path $departureGate
        if ($DepartureResponseLoss) {
            Wait-Marker $clients[0].Log 'DUNGEON_DEPARTURE_RECOVERY UNCONFIRMED SameContext=1 NewActionsLocked=1 RetryEnabled=1'
            $fault = Invoke-RestMethod "$departureControlUrl/state" -Headers $departureControlHeaders
            Require ($fault.dropped -eq 1 -and -not $fault.failure -and $fault.operation -ceq $DepartureLossOperation -and
                $fault.lossPoint -ceq $DepartureLossPoint) 'Departure transport was not interrupted at the selected boundary.'
            if ($precommitLoss) { Require ($fault.precommitDrops -eq 1 -and $fault.targetRequestsForwarded -eq 0 -and
                $null -eq $fault.originalReplySHA256) 'BeforeCommit forwarded a mutation or invented a committed response.' }
            $committed = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
            $expectQueued = $DepartureLossOperation -eq 'Request'
            $beforeRecoveryQueued = if ($precommitLoss) { -not $expectQueued } else { $expectQueued }
            Require ($committed.state -ceq 'Waiting' -and -not $committed.serverId -and
                $committed.members.Count -eq 2 -and [bool]$committed.departureRequestedAt -eq $beforeRecoveryQueued) 'Interrupted command did not preserve the expected party/queue state.'
            $beforeReplay = $committed | ConvertTo-Json -Depth 10 -Compress
            $snapshotName = if ($expectQueued) { 'queued-before-recovery.proof.json' } else { 'cancelled-before-recovery.proof.json' }
            if ($precommitLoss) { $snapshotName = 'precommit-before-recovery.proof.json' }
            $committed | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $root $snapshotName) -Encoding UTF8
            if ($DepartureRestart) {
                Wait-Marker $clients[0].Log 'DUNGEON_DEPARTURE_RECOVERY JOURNAL_FILE Path='
                $journalLine = (Select-String -LiteralPath $clients[0].Log -Pattern 'DUNGEON_DEPARTURE_RECOVERY JOURNAL_FILE Path="([^"]+)"' | Select-Object -Last 1)
                Require ($null -ne $journalLine) 'Owned leader did not expose its noncredential journal path.'
                $journalPath = [IO.Path]::GetFullPath($journalLine.Matches[0].Groups[1].Value)
                Require ($journalPath.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
                    [IO.Path]::GetFileName($journalPath) -cmatch '^[0-9a-f]{40}\.json$') 'Journal path escaped the development workspace/scope.'
                $journalBefore = (Get-FileHash -LiteralPath $journalPath).Hash
                $stored = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
                $fieldNames = @($stored.PSObject.Properties.Name | Sort-Object) -join ','
                Require ($fieldNames -ceq 'backendUrl,cancel,characterId,lobbyRevision,requestId,schemaVersion,sessionId,steamId' -and
                    $stored.backendUrl -ceq "$departureProxyUrl/api" -and $stored.steamId -ceq $members[0].SteamId -and
                    $stored.characterId -ceq $members[0].CharacterId -and $stored.sessionId -ceq $sessionId -and
                    $stored.cancel -is [bool] -and $stored.cancel -eq (-not $expectQueued)) 'Persisted journal did not match the original noncredential scope/intent.'
                $canonical = [ordered]@{ characterId=[string]$stored.characterId; requestId=[string]$stored.requestId; expectedLobbyRevision=[long]$stored.lobbyRevision } | ConvertTo-Json -Compress
                $digest = [Security.Cryptography.SHA256]::Create()
                try { $commandHash = -join ($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical)) | ForEach-Object { $_.ToString('x2') }) }
                finally { $digest.Dispose() }
                Require ($commandHash -ceq $fault.commandSHA256) 'Persisted command differed from the original intercepted HTTP command.'
                . (Join-Path $projectRoot 'Backend/Tools/LocalDungeonServerReadiness.ps1')
                $oldLeader = $clients[0]
                Stop-RPGOwnedLocalDungeonServer -Process $oldLeader.Process -Executable $editor -StartTicks $oldLeader.StartTicks
                Require $oldLeader.Process.HasExited 'The exact old leader process was not stopped.'
                $clients.RemoveAt(0)
                Start-LobbyClient $members[0] 1 -Restart
                $newLeader = $clients[$clients.Count - 1]
                Require ($newLeader.Process.Id -ne $oldLeader.Process.Id -or $newLeader.StartTicks -ne $oldLeader.StartTicks) 'Leader process identity was reused without a restart.'
                Wait-Marker $newLeader.Log 'DUNGEON_DEPARTURE_COLD_START EMPTY_STATE Credential=0 Selection=0 Session=0 Pending=0'
                Wait-Marker $newLeader.Log 'DUNGEON_DEPARTURE_COLD_START AUTHENTICATED FreshLogin=1 OwnedRoster=1 Pending=0'
                Require ((Get-FileHash -LiteralPath $journalPath).Hash -ceq $journalBefore) 'Original command changed during process replacement/fresh login.'
                $null = Invoke-RestMethod -Method Post -Uri "$departureControlUrl/cold-read" -Headers $departureControlHeaders -ContentType 'application/json' -Body '{}'
                $null = New-Item -ItemType File -Path $recoveryRetryGate
                Wait-Marker $newLeader.Log 'DUNGEON_DEPARTURE_COLD_START RESTORED OriginalScope=1 OriginalCommand=1 NoFabricatedSession=1'
                if ($precommitLoss) {
                    Wait-Marker $newLeader.Log 'DUNGEON_DEPARTURE_COLD_START PRECOMMIT_UNCONFIRMED OwnerRead=1 OriginalCommand=1 RetryEnabled=1 NewActionsLocked=1'
                    $pending = Invoke-RestMethod "$departureControlUrl/state" -Headers $departureControlHeaders
                    Require ($pending.targetRequestsForwarded -eq 0 -and $pending.replayForwarded -eq 0 -and $pending.blockedReplays -eq 0 -and
                        $pending.coldReadsForwarded -ge 1 -and $pending.coldReadChangedCredential -and -not $pending.failure -and
                        (Get-FileHash -LiteralPath $journalPath).Hash -ceq $journalBefore) 'Owner read discarded or automatically retransmitted the uncommitted command.'
                    $unchanged = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
                    Require (($unchanged | ConvertTo-Json -Depth 10 -Compress) -ceq $beforeReplay) 'Precommit owner read changed backend state before explicit retry.'
                    $permit = Invoke-RestMethod -Method Post -Uri "$departureControlUrl/cold-retry" -Headers $departureControlHeaders -ContentType 'application/json' -Body '{}'
                    Require ($permit.allowRetry -and $permit.targetRequestsForwarded -eq 0) 'Explicit original-command retry was not released safely.'
                    $null = New-Item -ItemType File -Path $commitRetryGate
                    Wait-Marker $newLeader.Log 'DUNGEON_DEPARTURE_COLD_START RETRY_DISPATCHED LiveButton=1 OriginalIntent=1'
                    Wait-Marker $newLeader.Log 'DUNGEON_DEPARTURE_COLD_START REPLAY_COMMITTED NewProcess=1 OriginalCommand=1 JournalRemoved=1 Mutation=1'
                }
                else { Wait-Marker $newLeader.Log 'DUNGEON_DEPARTURE_COLD_START RECOVERED NewProcess=1 OwnerRead=1 JournalRemoved=1 NoMutation=1' }
                $cold = Invoke-RestMethod "$departureControlUrl/state" -Headers $departureControlHeaders
                $cold | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'departure-proxy.proof.json') -Encoding UTF8
                Require (-not $cold.failure -and $cold.dropped -eq 1 -and $cold.coldReadsForwarded -ge 1 -and $cold.coldReadChangedCredential -and
                    $cold.replayForwarded -eq [int]$precommitLoss -and $cold.unauthorizedForwarded -eq 0 -and $cold.blockedReplays -eq 0 -and
                    $cold.targetRequestsForwarded -eq 1 -and $cold.targetSuccesses -eq 1 -and
                    $cold.departureRequestsForwarded -eq 1 -and $cold.unexpectedNewDepartures -eq 0 -and $cold.unexpectedCancellations -eq 0 -and
                    [bool]$cold.allowRetry -eq $precommitLoss -and -not (Test-Path -LiteralPath $journalPath)) 'Cold-start recovery did not preserve the original command/authority boundary.'
                $afterRead = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
                if ($precommitLoss) {
                    Require ($cold.precommitDrops -eq 1 -and $cold.changedCredentialOnReplay -and $cold.commandSHA256 -ceq $commandHash -and
                        $afterRead.lobbyRevision -eq ([long]$committed.lobbyRevision + 1) -and
                        [bool]$afterRead.departureRequestedAt -eq $expectQueued -and $afterRead.departureRequestId -ceq $stored.requestId -and
                        $afterRead.departureRequestRevision -eq [long]$stored.lobbyRevision -and
                        (Get-StableDepartureState $afterRead) -ceq (Get-StableDepartureState $committed)) 'Original retry did not commit exactly one revision while preserving party and leases.'
                }
                else { Require (($afterRead | ConvertTo-Json -Depth 10 -Compress) -ceq $beforeReplay) 'Process restart/owner read changed the committed party, revision or leases.' }
                $peer = Api Get "dungeon-sessions/$sessionId" $null $members[1].Headers
                Require ($peer.members.Count -eq 2 -and $peer.state -ceq 'Waiting') 'Peer party/read authority was lost during restart.'
                $proof.SessionId=$sessionId; $proof.ClientCount=2; $proof.ActualLeaderProcessRestart=$true
                $proof.OldLeaderProcessId=$oldLeader.Process.Id; $proof.NewLeaderProcessId=$newLeader.Process.Id
                $proof.OldLeaderStartTicks=$oldLeader.StartTicks.ToString(); $proof.NewLeaderStartTicks=$newLeader.StartTicks.ToString()
                $proof.OriginalJournalSha256=$journalBefore; $proof.OriginalCommandSha256=$commandHash
                $proof.JournalSurvivedRestartAndFreshLogin=$true
                $proof.FreshCredentialAndOwnedRoster=$true; $proof.ReadOnlyRecoveryNoMutationReplay=(-not $precommitLoss)
                if ($precommitLoss) {
                    $proof.UnchangedOwnerReadPreservedPending=$true; $proof.OriginalCommandSubmittedOnce=$true
                    $proof.JournalRemovedAfterCommit=$true; $proof.LobbyRevisionAdvancedOnce=$true; $proof.PartyAndLeasesPreserved=$true
                }
                else { $proof.JournalRemovedAfterOwnerRead=$true; $proof.PartyRevisionAndLeasesUnchanged=$true }
                $proof.InputsUnchanged=(($before|ConvertTo-Json -Compress) -ceq ((Get-Inputs)|ConvertTo-Json -Compress))
                $proof.ModuleUnchanged=($moduleBefore -ceq (Get-FileHash -LiteralPath $module).Hash)
                Require ($proof.InputsUnchanged -and $proof.ModuleUnchanged) 'Cold-start inputs/module changed during execution.'
                $passed=$true
                return
            }
            $revoked = Invoke-RestMethod -Method Post -Uri "$departureControlUrl/revoke" -Headers $departureControlHeaders -ContentType 'application/json' -Body '{}'
            Require $revoked.credentialRevoked 'The scoped real credential revoke was not confirmed.'
            $null = New-Item -ItemType File -Path $recoveryRetryGate
            Wait-Marker $clients[0].Log 'DUNGEON_DEPARTURE_RECOVERY AUTH_REQUIRED OriginalContext=1 RetryLocked=1 LiveLoginButton=1'
            Wait-Marker $clients[0].Log 'DUNGEON_DEPARTURE_RECOVERY REAUTHENTICATED SameAccount=1 OriginalContext=1'
            Wait-Marker $clients[0].Log 'DUNGEON_DEPARTURE_RECOVERY RECOVERED SameProcess=1 SameContext=1 OriginalCommand=1 NoNewDeparture=1'
            if ($expectQueued) { Wait-Marker $clients[0].Log 'DUNGEON_DEPARTURE_RECOVERY REQUEST_QUEUE_RESTORED RequestLocked=1 CancelEnabled=1' }
            $fault = Invoke-RestMethod "$departureControlUrl/state" -Headers $departureControlHeaders
            $fault | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'departure-proxy.proof.json') -Encoding UTF8
            Require (-not $fault.failure -and $fault.unauthorizedForwarded -eq 1 -and $fault.replayForwarded -eq 1 -and
                $fault.departureRequestsForwarded -eq 1 -and $fault.unexpectedNewDepartures -eq 0 -and
                $fault.unexpectedCancellations -eq 0 -and $fault.operation -ceq $DepartureLossOperation -and
                $fault.changedCredentialOnReplay -and $fault.originalReplySHA256 -ceq $fault.replayReplySHA256) 'Original departure/credential/replay proof mismatch.'
            $afterReplay = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
            Require (($afterReplay | ConvertTo-Json -Depth 10 -Compress) -ceq $beforeReplay) 'Recovery changed the committed party, revision or leases.'
            $peer = Api Get "dungeon-sessions/$sessionId" $null $members[1].Headers
            Require ($peer.members.Count -eq 2 -and $peer.state -ceq 'Waiting') 'Peer authority/party was lost during credential recovery.'
            $proof.SessionId=$sessionId; $proof.ClientCount=2
            $proof.CancelReplyLossVerified=-not $expectQueued; $proof.RequestReplyLossVerified=$expectQueued
            $proof.QueuedDeparturePreserved=$expectQueued
            $proof.Actual401AndSameAccountLogin=$true; $proof.OriginalCommandAndReplyMatched=$true; $proof.PartyRevisionAndLeasesUnchanged=$true
            $proof.InputsUnchanged=(($before|ConvertTo-Json -Compress) -ceq ((Get-Inputs)|ConvertTo-Json -Compress))
            $proof.ModuleUnchanged=($moduleBefore -ceq (Get-FileHash -LiteralPath $module).Hash)
            Require ($proof.InputsUnchanged -and $proof.ModuleUnchanged) 'Recovery inputs or module changed during execution.'
            $passed=$true
            return
        }
        Wait-Marker $clients[0].Log 'DUNGEON_DEPARTURE_UI READY_FOR_CLAIM Leader=1 RequestCancelRequeue=1'
        $proof.RecruitingClaimDenied = $true; $proof.LiveLeaderRequestCancelRequeue = $true; $proof.NonleaderDepartureDisabled = $true
    }
    else {
        $login = Api Post 'auth/steam-ticket' @{ ticket = 'dev:76561198000999882' } @{}
        $playerHeaders = @{ Authorization = "Bearer $($login.accessToken)" }
        $character = Api Post 'characters' @{ name = ('Alloc' + $runId.Substring(0, 12)) } $playerHeaders
        $created = Api Post 'dungeon-sessions' @{ characterId = $character.characterId; dungeonId = 'Dungeon.RaidSwordMan'; difficulty = 'Normal' } $playerHeaders
        $sessionId = [string]$created.dungeonSessionId
        Require ($created.state -ceq 'Waiting') 'Fixture did not start in Waiting.'
        . (Join-Path $projectRoot 'Backend/Tools/Request-DungeonDeparture.ps1')
        $null = Request-RPGDungeonDeparture $baseUrl $sessionId $character.characterId $playerHeaders
    }
    # No activate, world URL, server token, start POST or readiness injection in
    # this harness: the real supervisor/launcher and actual UE must perform them.
    $events = @(& $supervisorScript -ServerExecutable $editor -EditorProject $project -DevelopmentIdentityTesting:$LobbyEntry -BackendUrl $baseUrl -AdminToken $adminToken `
        -StartPort $Port -PortCount 1 -RunOnce -WaitForReady -StartupTimeoutSeconds $TimeoutSeconds -LogDirectory $serverRoot)
    $events | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'allocator-events.json') -Encoding UTF8
    $statePath = Join-Path $serverRoot "port-$Port.allocator.json"
    if (Test-Path -LiteralPath $statePath) {
        $serverState = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $serverProcess = Get-Process -Id $serverState.processId -ErrorAction SilentlyContinue
    }
    Require (@($events | Where-Object Event -eq 'Started').Count -eq 1 -and @($events | Where-Object Event -eq 'Error').Count -eq 0) 'Allocator failed to start a ready server; inspect allocator events and server log.'
    Require ($null -ne $serverState -and $serverState.dungeonSessionId -ceq $sessionId -and
        $serverState.startupState -ceq 'Ready' -and $serverState.mapPackage -ceq '/Game/Maps/bossmap') 'Allocator state did not confirm the exact real server/map.'
    $session = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
    Require ($session.state -ceq 'InProgress' -and $session.serverId -ceq $serverState.serverId -and $session.members.Count -eq $expectedMembers) 'Real UE did not start its exact assigned session.'
    Require (Select-String -LiteralPath $serverState.logPath -Pattern "IpNetDriver listening on port $Port" -SimpleMatch -Quiet) 'Real server did not bind the expected game network port.'
    Require (Select-String -LiteralPath $serverState.logPath -Pattern 'RPG_NETTEST SERVER_STATE Iris=1' -SimpleMatch -Quiet) 'Real GameNetDriver did not report Iris.'
    $reused = & $startScript -ServerExecutable $editor -EditorProject $project -DevelopmentIdentityTesting:$LobbyEntry -DungeonSessionId ([Guid]$sessionId) -BackendUrl $baseUrl `
        -AdminToken $adminToken -Port $Port -WaitForReady -StartupTimeoutSeconds 5 -LogDirectory $serverRoot
    Require ($reused.Reused -and $reused.Ready -and $reused.ProcessId -eq $serverState.processId) 'Ready process was duplicated or lost on reuse.'
    $wrongMapRejected = $false
    try {
        $null = & $startScript -ServerExecutable $editor -EditorProject $project -DevelopmentIdentityTesting:$LobbyEntry -DungeonSessionId ([Guid]$sessionId) -BackendUrl $baseUrl `
            -AdminToken $adminToken -Port $Port -Map '/Game/Maps/testmap' -LogDirectory $serverRoot
    }
    catch { $wrongMapRejected = $true }
    Require $wrongMapRejected 'A different-map request reused the existing server.'
    $afterRejected = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
    Require ($afterRejected.state -ceq 'InProgress' -and $afterRejected.serverId -ceq $session.serverId -and
        $afterRejected.members.Count -eq $expectedMembers -and (Get-Process -Id $serverState.processId -ErrorAction SilentlyContinue)) 'Rejected reuse damaged the existing server/party.'
    if ($LobbyEntry) {
        Require (Select-String -LiteralPath $serverState.logPath -Pattern "Created socket for bind address: 127.0.0.1:$Port" -SimpleMatch -Quiet) 'Development identity comparison exception did not bind the actual socket to loopback.'
        # File is only a test-button timing barrier, not permission to enter.
        # The existing client still refreshes the backend and requests a new ticket.
        $null = New-Item -ItemType File -Path $entryGate
        for ($index = 0; $index -lt $clients.Count; $index++) {
            $client = $clients[$index]; $member = $members[$index]
            Wait-Marker $client.Log 'DUNGEON_FLOW_E2E_LOBBY_ENTRY LiveButton=1 FreshTicket=1 TrustedBackendAddress=1'
            Wait-Marker $client.Log 'DUNGEON_FLOW_E2E_ADMITTED Iris=1 Identity=1 PawnGASInput=1'
            Wait-Marker $client.Log "DUNGEON_FLOW_E2E_ADMISSION_ONLY_COMPLETE Session=$sessionId Character=$($member.CharacterId) Steam=$($member.SteamId) ActualBossMap=1 NoSettlementRequested=1"
        }
        Wait-Marker $serverState.logPath "RPG_NETTEST SERVER_STATE Iris=1 Connected=$ClientCount"
        Require (-not (Select-String -LiteralPath $serverState.logPath -Pattern 'RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED' -SimpleMatch -Quiet)) 'Whole admission bypass must remain disabled.'
        $admitted = Api Get "dungeon-sessions/$sessionId" $null $adminHeaders
        Require ($admitted.state -ceq 'InProgress' -and $admitted.members.Count -eq $ClientCount -and $admitted.serverId -ceq $serverState.serverId) 'Party/session changed during real admission.'
        $proof.LobbyEntryVerified = $true; $proof.ClientCount = $ClientCount
        $proof.ExactAdmittedMembers = @($members | ForEach-Object { [pscustomobject]@{ CharacterId = $_.CharacterId; SteamId = $_.SteamId } })
        $proof.DevelopmentSteamIdentityComparisonException = $true
        $proof.ActualGameSocketLoopbackOnly = $true
        $proof.JoinTicketMembershipAndServerIdentityBypass = $false
    }
    $proof.SessionId = $sessionId; $proof.ServerId = $serverState.serverId; $proof.Port = $Port
    $proof.Map = $serverState.mapPackage; $proof.Difficulty = $serverState.difficulty
    $proof.AutomaticClaim = $true; $proof.ActualUEContentReady = $true; $proof.ReuseAndWrongMapPreservation = $true
    $proof.InputsUnchanged = (($before | ConvertTo-Json -Compress) -ceq ((Get-Inputs) | ConvertTo-Json -Compress))
    $proof.ModuleUnchanged = ($moduleBefore -ceq (Get-FileHash -LiteralPath $module).Hash)
    Require ($proof.InputsUnchanged -and $proof.ModuleUnchanged) 'Selected sources or shared UE module changed during startup verification.'
    $passed = $true
}
finally {
    . (Join-Path $projectRoot 'Backend/Tools/LocalDungeonServerReadiness.ps1')
    foreach ($client in $clients) { Stop-RPGOwnedLocalDungeonServer -Process $client.Process -Executable $editor -StartTicks $client.StartTicks }
    if ($null -ne $serverState) {
        Stop-RPGOwnedLocalDungeonServer -Process $serverProcess -Executable $editor -StartTicks $serverState.processStartTimeUtcTicks
    }
    if ($null -ne $departureProxy -and -not $departureProxy.HasExited) { Stop-Process -Id $departureProxy.Id -Force; $departureProxy.WaitForExit() }
    if ($null -ne $backend -and -not $backend.HasExited) { Stop-Process -Id $backend.Id -Force; $backend.WaitForExit() }
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    $proof.Passed = $passed
    $proof.ClosedLogs = @(Get-ChildItem -LiteralPath $root -Recurse -Filter '*.log' | ForEach-Object { [pscustomobject]@{ Path = $_.FullName; Sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash } })
    $proof | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $root 'Verification.proof.json') -Encoding UTF8
    Write-Output "Local dungeon allocator startup: Passed=$passed Root=$root"
}
