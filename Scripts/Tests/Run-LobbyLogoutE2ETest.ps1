[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    # Optional already-built isolated Editor project; sources/backend stay in this workspace.
    [string]$ProjectFile = '',
    # Reuse a closed fixture build when another agent owns backend sources.
    [string]$BackendBuildDirectory = '',
    [ValidateRange(1024, 65535)][int]$BackendPort = 3038,
    [ValidateRange(60, 600)][int]$TimeoutSeconds = 300,
    [switch]$CreationOnly,
    [switch]$ConfigurationOnly,
    [switch]$ConfigurationRecovery,
    [switch]$ConfigurationConflict,
    [switch]$ConfigurationReadFailures
)

# Actual native lobby buttons and UE HTTP; isolated Development/Memory only.
# No Steam transport, dungeon server, existing backend or database is used.
$ErrorActionPreference = 'Stop'
if ($CreationOnly -and $ConfigurationOnly) { throw 'Choose one focused lobby mode.' }
if ($ConfigurationRecovery -and -not $ConfigurationOnly) { throw 'Configuration recovery requires ConfigurationOnly.' }
if ($ConfigurationConflict -and -not $ConfigurationOnly) { throw 'Configuration conflict requires ConfigurationOnly.' }
if ($ConfigurationRecovery -and $ConfigurationConflict) { throw 'Choose one configuration scenario.' }
if ($ConfigurationReadFailures -and (-not $ConfigurationOnly -or $ConfigurationRecovery -or $ConfigurationConflict)) {
    throw 'Read failures require ConfigurationOnly and no other configuration scenario.'
}
$logoutMode = -not $CreationOnly -and -not $ConfigurationOnly
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = if ([string]::IsNullOrWhiteSpace($ProjectFile)) { Join-Path $projectRoot 'Project_RPG.uproject' } else { [IO.Path]::GetFullPath($ProjectFile) }
$module = Join-Path (Split-Path -Parent $project) 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$backendProject = Join-Path $projectRoot 'Backend\ProjectRpg.Backend\ProjectRpg.Backend.csproj'
$dotnet = (Get-Command dotnet).Source
$prerequisites = @($editor, $project, $module)
if ([string]::IsNullOrWhiteSpace($BackendBuildDirectory)) { $prerequisites += $backendProject }
else {
    $BackendBuildDirectory = [IO.Path]::GetFullPath($BackendBuildDirectory)
    foreach ($name in @('ProjectRpg.Backend.dll', 'ProjectRpg.Backend.deps.json', 'ProjectRpg.Backend.runtimeconfig.json')) {
        $prerequisites += Join-Path $BackendBuildDirectory $name
    }
}
foreach ($path in $prerequisites) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing prerequisite: $path" }
}
if (@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() |
        Where-Object { $_.Port -eq $BackendPort }).Count) { throw 'Requested loopback backend port is already in use.' }
$runId = [Guid]::NewGuid().ToString('N')
$runDomain = if ($ConfigurationReadFailures) { 'LobbyLookupFailures' } elseif ($ConfigurationConflict) { 'LobbyConfigurationConflict' } elseif ($ConfigurationRecovery) { 'LobbyConfigurationRecovery' } elseif ($ConfigurationOnly) { 'LobbyConfiguration' } elseif ($CreationOnly) { 'LobbyCreation' } else { 'LobbyLogout' }
$runRoot = Join-Path $projectRoot "Saved\Tests\$runDomain\$runId"
$buildRoot = Join-Path $runRoot 'backend-build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$baseUrl = "http://127.0.0.1:$BackendPort"
$clientLog = Join-Path $runRoot 'client.log'
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
$environmentOverrides = @{
    ASPNETCORE_ENVIRONMENT = 'Development'
    PROJECT_RPG_BACKEND_ADMIN_TOKEN = [Guid]::NewGuid().ToString('N')
    POSTGRES_CONNECTION_STRING = $null
    ConnectionStrings__Postgres = $null
    Storage__Provider = 'Memory'
    Kestrel__Endpoints__Http__Url = $baseUrl
    Steam__AllowDevelopmentAuthentication = 'true'
    PROJECT_RPG_GAME_SERVER_ID = $null
    PROJECT_RPG_DUNGEON_SESSION_ID = $null
    PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN = $null
}
if ($ConfigurationOnly) {
    # Isolated fixture selection only. This does not register authored training
    # content in the real backend or launch a gameplay server/map.
    $environmentOverrides['DungeonContent__Entries__1__DungeonId'] = 'Progression.Training'
    $environmentOverrides['DungeonContent__Entries__1__MapPackage'] = '/Game/Maps/testmap'
    $environmentOverrides['DungeonContent__Entries__1__Difficulties__0'] = 'Normal'
}
$previousEnvironment = @{}
$sourcePaths = @(
    'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.h',
    'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonLobbyLogoutE2EProbe.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonLobbyConfigurationE2EProbe.cpp',
    'Source/Project_RPG/Public/Manager/HttpWebManager.h',
    'Source/Project_RPG/Private/Manager/HttpWebManager.cpp',
    'Source/Project_RPG/Private/Online/RPGOnlineLobbySubsystem.cpp',
    'Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
    'Source/Project_RPG/Private/Online/RPGSteamAuthSubsystem.cpp',
    'Source/Project_RPG/Public/UI/Lobby/RPGOnlineLobbyWidget.h',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyCharacters.cpp',
    'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyLayout.h',
    'Source/Project_RPG/Private/Manager/HttpWebManagerCharacterCreation.cpp',
    'Source/Project_RPG/Private/Manager/HttpWebManagerProgression.cpp',
    'Source/Project_RPG/Public/UI/Lobby/RPGCharacterConfigurationWidget.h',
    'Source/Project_RPG/Private/UI/Lobby/RPGCharacterConfigurationWidget.cpp',
    'Source/Project_RPG/Public/Progression/RPGCharacterProgressionSnapshot.h',
    'Source/Project_RPG/Private/Progression/RPGCharacterProgressionSnapshot.cpp',
    'Source/Project_RPG/Private/Online/RPGBackendCharacter.cpp',
    'Config/DefaultGame.ini', 'Config/DefaultEngine.ini',
    'Scripts/Tests/Run-LobbyLogoutE2ETest.ps1',
    'Scripts/Network/Fixtures/CharacterConfigurationHttpFaultProxy.mjs'
)
$moduleTime = (Get-Item -LiteralPath $module).LastWriteTimeUtc
if ($ConfigurationOnly) {
    $sourcePaths += @(
        'Source/Project_RPG/Private/UI/Lobby/RPGCharacterConfigurationPreview.h',
        'Source/Project_RPG/Private/UI/Lobby/RPGCharacterConfigurationPreview.cpp',
        'Source/Project_RPG/Public/Skill/RPGSkillCatalogSubsystem.h',
        'Source/Project_RPG/Private/Skill/RPGSkillCatalogSubsystem.cpp',
        'Source/Project_RPG/Public/Skill/RPGSkillDefinition.h',
        'Source/Project_RPG/Private/Skill/RPGSkillDefinition.cpp',
        'Source/Project_RPG/Public/Skill/RPGSkillRuntimeTypes.h',
        'Source/Project_RPG/Private/Skill/RPGSkillRuntimeTypes.cpp',
        'Content/Blueprints/GameData/Skill/DA_Skill_AssultBlade.uasset',
        'Content/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.uasset',
        'Content/Blueprints/GameData/Skill/DA_Skill_WhirlWind.uasset',
        'Content/Blueprints/GameData/Skill/DA_Skill_JumpSmash.uasset'
    )
}
foreach ($source in $sourcePaths | Where-Object { [IO.Path]::GetExtension($_) -in @('.h', '.cpp') }) {
    if ((Get-Item -LiteralPath (Join-Path $projectRoot $source)).LastWriteTimeUtc -gt $moduleTime) {
        throw "Build the changed source before executing the real lobby flow: $source"
    }
}
$sourceHashes = @($sourcePaths | ForEach-Object {
    [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash }
})
$moduleHash = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
[ordered]@{ moduleSha256 = $moduleHash; selectedSources = $sourceHashes } | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $runRoot 'InputsBeforeRun.json') -Encoding UTF8

function Invoke-FixtureApi {
    param([string]$Path, [string]$Method = 'Get', [hashtable]$Headers = @{}, [object]$Body = $null)
    $parameters = @{ Uri = "$baseUrl/api/$Path"; Method = $Method; Headers = $Headers; TimeoutSec = 10; UseBasicParsing = $true }
    if ($null -ne $Body) { $parameters.ContentType = 'application/json'; $parameters.Body = $Body | ConvertTo-Json -Depth 8 -Compress }
    try { $response = Invoke-WebRequest @parameters }
    catch {
        $status = if ($_.Exception.Response) { [int]$_.Exception.Response.StatusCode } else { 0 }
        throw "Fixture HTTP $Method $Path failed (HTTP $status); credentials and response body omitted."
    }
    if (-not [string]::IsNullOrWhiteSpace($response.Content)) { return $response.Content | ConvertFrom-Json }
}

function Invoke-ConfigurationControl {
    param([string]$Path, [string]$Method = 'Get', [object]$Body = $null)
    try {
        $parameters = @{ Uri = ($configurationControlUrl + $Path); Method = $Method; Headers = $configurationControlHeaders; TimeoutSec = 10; UseBasicParsing = $true }
        if ($null -ne $Body) { $parameters.ContentType = 'application/json'; $parameters.Body = $Body | ConvertTo-Json -Compress }
        return (Invoke-WebRequest @parameters).Content | ConvertFrom-Json
    } catch { throw 'Configuration fault control failed; credentials and response body omitted.' }
}

try {
    # Normalize duplicated PATH casing in this child script process only.
    $processEnvironment = [Environment]::GetEnvironmentVariables('Process')
    $pathKeys = @($processEnvironment.Keys | Where-Object { $_.ToString() -ieq 'Path' })
    if ($pathKeys.Count -gt 1) {
        $pathValue = [string]$processEnvironment[$pathKeys[0]]
        foreach ($key in $pathKeys) { [Environment]::SetEnvironmentVariable($key.ToString(), $null, 'Process') }
        [Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')
    }
    if ([string]::IsNullOrWhiteSpace($BackendBuildDirectory)) {
        & $dotnet build $backendProject -c Release --artifacts-path (Join-Path $runRoot 'backend-artifacts') --output $buildRoot *> (Join-Path $runRoot 'backend-build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Isolated backend build failed; inspect backend-build.log.' }
    } else {
        Get-ChildItem -LiteralPath $BackendBuildDirectory | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $buildRoot -Recurse
        }
    }
    foreach ($name in $environmentOverrides.Keys) {
        $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $environmentOverrides[$name], 'Process')
    }
    try {
        $backend = Start-Process -FilePath $dotnet -ArgumentList ('"' + (Join-Path $buildRoot 'ProjectRpg.Backend.dll') + '"') `
            -WorkingDirectory $buildRoot -RedirectStandardOutput (Join-Path $runRoot 'backend.stdout.log') `
            -RedirectStandardError (Join-Path $runRoot 'backend.stderr.log') -PassThru -WindowStyle Hidden
        $processes.Add($backend)
    } finally {
        foreach ($name in $previousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process') }
    }
    $ready = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($backend.HasExited) { throw 'Owned backend exited before readiness.' }
        try {
            $health = Invoke-RestMethod "$baseUrl/health/ready" -TimeoutSec 2
            if ($health.status -eq 'ready' -and $health.storageProvider -eq 'Memory') { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw 'Isolated Memory backend did not become ready.' }
    $ownedListeners = @(Get-NetTCPConnection -State Listen -LocalPort $BackendPort -ErrorAction Stop |
        Where-Object { $_.OwningProcess -eq $backend.Id -and $_.LocalAddress -eq '127.0.0.1' })
    if ($backend.HasExited -or $ownedListeners.Count -ne 1) { throw 'Readiness listener is not the owned loopback backend.' }
    $number = [uint64][Convert]::ToUInt32($runId.Substring(0, 8), 16)
    $members = @(for ($i = 0; $i -lt 2; $i++) {
        $steamId = ([uint64]76561300000000000 + $number * 4 + [uint64]$i).ToString()
        $auth = Invoke-FixtureApi 'auth/steam-ticket' 'Post' @{} @{ ticket = "dev:$steamId" }
        $headers = @{ Authorization = "Bearer $($auth.accessToken)" }
        $characterId = ''
        if (-not $CreationOnly) {
            $character = Invoke-FixtureApi 'characters' 'Post' $headers @{ name = "Logout$($runId.Substring(0, 10))$i" }
            $characterId = [string]$character.characterId
        }
        [pscustomobject]@{ SteamId = $steamId; CharacterId = $characterId; Headers = $headers }
    })
    if ($ConfigurationOnly) {
        $configurationSibling = Invoke-FixtureApi 'characters' 'Post' $members[0].Headers @{ name = "CfgAlt$($runId.Substring(0, 10))" }
        # Earn three SP through the existing authenticated settlement path. No
        # direct XP/SP mutation or test-only backend endpoint is introduced.
        $training = Invoke-FixtureApi 'dungeon-sessions' 'Post' $members[0].Headers @{
            characterId = $members[0].CharacterId; dungeonId = 'Progression.Training'; difficulty = 'Normal'
        }
        $sessionPath = 'dungeon-sessions/' + $training.dungeonSessionId
        $null = Invoke-FixtureApi "$sessionPath/departure" 'Post' $members[0].Headers @{
            characterId = $members[0].CharacterId; requestId = [Guid]::NewGuid().ToString('D'); expectedLobbyRevision = $training.lobbyRevision
        }
        $fixtureServer = 'configuration-' + $runId
        $adminHeaders = @{ Authorization = 'Bearer ' + $environmentOverrides.PROJECT_RPG_BACKEND_ADMIN_TOKEN }
        $assigned = Invoke-FixtureApi "$sessionPath/activate" 'Post' $adminHeaders @{
            serverId = $fixtureServer; serverAddress = '127.0.0.1:7799'
        }
        if ([string]::IsNullOrWhiteSpace($assigned.gameServerAccessToken)) { throw 'Training fixture did not issue server authority.' }
        $fixtureServerHeaders = @{ Authorization = 'Bearer ' + $assigned.gameServerAccessToken }
        $null = Invoke-FixtureApi "$sessionPath/start" 'Post' $fixtureServerHeaders @{ serverId = $fixtureServer }
        $null = Invoke-FixtureApi "$sessionPath/settle-rewards" 'Post' $fixtureServerHeaders @{
            serverId = $fixtureServer; rewardVersion = 'FirstSlice.v1'; changes = @(); itemRewards = @()
        }
        $settlementDeadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            $training = Invoke-FixtureApi $sessionPath 'Get' $members[0].Headers
            if ($training.state -eq 'Cleared') { break }
            if ($training.state -eq 'Failed') { throw 'Training fixture settlement failed.' }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $settlementDeadline)
        if ($training.state -ne 'Cleared') { throw 'Training fixture settlement did not complete.' }
        $configurationInitial = Invoke-FixtureApi "characters/$($members[0].CharacterId)/progression" 'Get' $members[0].Headers
        $configurationSiblingInitial = Invoke-FixtureApi "characters/$($configurationSibling.characterId)/progression" 'Get' $members[0].Headers
        $configurationOtherInitial = Invoke-FixtureApi "characters/$($members[1].CharacterId)/progression" 'Get' $members[1].Headers
        $expectedSkills = @('Player.Ability.Skill.AssultBlade', 'Player.Ability.Skill.Charge', 'Player.Ability.Skill.WhirlWind')
        if ($configurationInitial.experience -ne 100 -or $configurationInitial.totalSP -ne 3 -or $configurationInitial.usedSP -ne 0 -or
            (@($configurationInitial.skills.skillTag) -join '|') -cne ($expectedSkills -join '|')) {
            throw 'Training fixture did not yield the confirmed three-SP, three-skill recipe.'
        }
    }
    $frontendUrl = $baseUrl
    if ($ConfigurationRecovery -or $ConfigurationConflict -or $ConfigurationReadFailures) {
        $node = (Get-Command node).Source
        $proxyOutput = Join-Path $runRoot 'proxy.stdout.log'
        $proxyEnvironment = @{
            PROJECT_RPG_E2E_PROXY_CONTROL_TOKEN = ([Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N'))
            PROJECT_RPG_E2E_PROXY_UPSTREAM_PORT = $BackendPort.ToString()
            PROJECT_RPG_E2E_PROXY_LISTEN_PORT = '0'
        }
        $proxyPreviousEnvironment = @{}
        foreach ($name in $proxyEnvironment.Keys) {
            $proxyPreviousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
            [Environment]::SetEnvironmentVariable($name, $proxyEnvironment[$name], 'Process')
        }
        try {
            $proxyPath = Join-Path $projectRoot 'Scripts\Network\Fixtures\CharacterConfigurationHttpFaultProxy.mjs'
            $configurationProxy = Start-Process -FilePath $node -ArgumentList ('"' + $proxyPath + '"') -WorkingDirectory $projectRoot `
                -RedirectStandardOutput $proxyOutput -RedirectStandardError (Join-Path $runRoot 'proxy.stderr.log') -PassThru -WindowStyle Hidden
            $processes.Add($configurationProxy)
        } finally {
            foreach ($name in $proxyPreviousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $proxyPreviousEnvironment[$name], 'Process') }
        }
        $proxyDeadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            if ($configurationProxy.HasExited) { throw 'Owned configuration proxy exited before readiness.' }
            $readyLine = if (Test-Path -LiteralPath $proxyOutput) { Get-Content -LiteralPath $proxyOutput -Raw -Encoding UTF8 } else { '' }
            if ($readyLine -match 'CONFIGURATION_PROXY_READY listen=(\d+) control=(\d+)') { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $proxyDeadline)
        if ($readyLine -notmatch 'CONFIGURATION_PROXY_READY listen=(\d+) control=(\d+)') { throw 'Configuration proxy readiness timed out.' }
        $frontendUrl = 'http://127.0.0.1:' + $Matches[1]
        $configurationControlUrl = 'http://127.0.0.1:' + $Matches[2]
        $configurationControlHeaders = @{ Authorization = 'Bearer ' + $proxyEnvironment.PROJECT_RPG_E2E_PROXY_CONTROL_TOKEN }
        $scenario = if ($ConfigurationReadFailures) { 'ReadFailures' } elseif ($ConfigurationConflict) { 'Conflict' } else { 'Recovery' }
        $proxyScope = @{ characterId = $members[0].CharacterId; scenario = $scenario }
        if ($ConfigurationReadFailures) {
            $proxyScope['forbiddenCharacterId'] = $members[1].CharacterId
            $proxyScope['missingCharacterId'] = [Guid]::NewGuid().ToString('D')
        }
        $null = Invoke-ConfigurationControl '/arm' 'Post' $proxyScope
        $configurationGates = Join-Path $runRoot 'gates'
        New-Item -ItemType Directory -Path $configurationGates -Force | Out-Null
        $nextProxyCheck = [DateTime]::UtcNow
    }
    $arguments = @(('"' + $project + '"'), '/Game/Maps/LobbyMap', '-game', '-RPGDungeonFlowE2E', '-RPGFlowE2ELobbyUI',
        '-RPGFlowE2ELobbyLogout', '-RPGDevelopmentLogin', "-RPGFlowE2ESteam=$($members[0].SteamId)",
        "-RPGFlowE2ECharacter=$($members[0].CharacterId)", "-RPGFlowE2ELogoutNextSteam=$($members[1].SteamId)",
        "-RPGFlowE2ELogoutNextCharacter=$($members[1].CharacterId)", '-unattended', '-nop4', '-nosplash', '-nosound',
        '-nullrhi', '-nosteam', '-NoLiveCoding', '-NoLoadingScreen', '-DisablePython', '-DDC-ForceMemoryCache',
        ('-UserDir="' + (Join-Path $runRoot 'User') + '"'), ('-abslog="' + $clientLog + '"'), '-ExecCmds="t.MaxFPS 30"',
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$frontendUrl/api")
    $creationName = 'Create' + $runId.Substring(0, 10)
    if ($CreationOnly) {
        $arguments = @($arguments | Where-Object {
            $_ -ne '-RPGFlowE2ELobbyLogout' -and $_ -notlike '-RPGFlowE2ECharacter=*' -and $_ -notlike '-RPGFlowE2ELogoutNext*'
        }) + @('-RPGFlowE2ECreateCharacter', '-RPGFlowE2ELobbyCreationOnly', ('-RPGFlowE2ECharacterName=' + $creationName))
    }
    if ($ConfigurationOnly) {
        $arguments = @($arguments | Where-Object {
            $_ -ne '-RPGFlowE2ELobbyLogout' -and $_ -notlike '-RPGFlowE2ELogoutNext*'
        }) + @('-RPGFlowE2ELobbyConfigurationOnly', "-RPGFlowE2EConfigurationOtherCharacter=$($configurationSibling.characterId)")
    }
    if ($ConfigurationRecovery -or $ConfigurationConflict -or $ConfigurationReadFailures) {
        $scenarioFlag = if ($ConfigurationReadFailures) { '-RPGFlowE2EConfigurationReadFailures' } elseif ($ConfigurationConflict) { '-RPGFlowE2EConfigurationConflict' } else { '-RPGFlowE2EConfigurationRecovery' }
        $arguments += @($scenarioFlag, ('-RPGFlowE2EConfigurationGates="' + $configurationGates + '"'))
    }
    $client = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -PassThru -WindowStyle Hidden
    $clientHandle = $client.Handle
    $processes.Add($client)
    Write-Host "Isolated lobby flow E2E running ($runDomain): $runRoot"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $client.HasExited) {
        if ($backend.HasExited) { throw 'Owned backend exited during lobby flow.' }
        if (($ConfigurationRecovery -or $ConfigurationConflict -or $ConfigurationReadFailures) -and [DateTime]::UtcNow -ge $nextProxyCheck) {
            if ($configurationProxy.HasExited) { throw 'Owned configuration proxy exited during the flow.' }
            $proxyState = Invoke-ConfigurationControl '/state'
            if ($proxyState.failure) { throw ('Configuration proxy rejected the flow: ' + $proxyState.failure) }
            $readGate = Join-Path $configurationGates 'read-revoked.ready'
            if (-not (Test-Path -LiteralPath $readGate) -and (Test-Path -LiteralPath $clientLog) -and
                (Select-String -LiteralPath $clientLog -Pattern 'LOBBY_CONFIGURATION_RECOVERY DRAFT_READY' -Quiet)) {
                $null = Invoke-ConfigurationControl '/revoke-read' 'Post'
                Set-Content -LiteralPath $readGate -Value 'ready' -Encoding ASCII
            }
            $writeGate = Join-Path $configurationGates 'write-revoked.ready'
            if (-not (Test-Path -LiteralPath $writeGate) -and (Test-Path -LiteralPath $clientLog) -and
                (Select-String -LiteralPath $clientLog -Pattern 'LOBBY_CONFIGURATION_RECOVERY UNKNOWN' -Quiet)) {
                $null = Invoke-ConfigurationControl '/revoke-write' 'Post'
                Set-Content -LiteralPath $writeGate -Value 'ready' -Encoding ASCII
            }
            if ($ConfigurationConflict) {
                $competingGate = Join-Path $configurationGates 'competitor-stored.ready'
                if (-not (Test-Path -LiteralPath $competingGate) -and (Test-Path -LiteralPath $clientLog) -and
                    (Select-String -LiteralPath $clientLog -Pattern 'LOBBY_CONFIGURATION_CONFLICT DRAFT_READY' -Quiet)) {
                    $competingSkills = @(foreach ($skill in $configurationInitial.skills) {
                        $entry = @{ skillTag = $skill.skillTag; level = $skill.level; tripods = @($skill.tripods) }
                        if ($skill.skillTag -ceq 'Player.Ability.Skill.Charge') { $entry.level = 4; $entry.tripods = @('QuickCharge', '') }
                        $entry
                    })
                    # A distinct player token for this same Steam account commits
                    # through the real API after the UE has edited revision two.
                    $configurationCompetingState = Invoke-FixtureApi "characters/$($members[0].CharacterId)/skill-configuration" 'Put' $members[0].Headers @{
                        requestId = [Guid]::NewGuid().ToString('D'); expectedRevision = $configurationInitial.revision
                        skills = $competingSkills; skillSlots = @($configurationInitial.skillSlots)
                    }
                    if ([long]$configurationCompetingState.revision -ne ([long]$configurationInitial.revision + 1)) {
                        throw 'Competing device did not commit the next server revision.'
                    }
                    Set-Content -LiteralPath $competingGate -Value 'ready' -Encoding ASCII
                }
                $preservedGate = Join-Path $configurationGates 'competitor-preserved.ready'
                if (-not (Test-Path -LiteralPath $preservedGate) -and (Test-Path -LiteralPath $clientLog) -and
                    (Select-String -LiteralPath $clientLog -Pattern 'LOBBY_CONFIGURATION_CONFLICT BLOCKED' -Quiet)) {
                    $afterConflict = Invoke-FixtureApi "characters/$($members[0].CharacterId)/progression" 'Get' $members[0].Headers
                    if (($afterConflict | ConvertTo-Json -Depth 8 -Compress) -cne ($configurationCompetingState | ConvertTo-Json -Depth 8 -Compress)) {
                        throw 'Rejected stale UI write changed the competing device configuration.'
                    }
                    [ordered]@{ competing = $configurationCompetingState; afterRejectedWrite = $afterConflict; unchanged = $true } |
                        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'ConflictRejectedState.proof.json') -Encoding UTF8
                    Set-Content -LiteralPath $preservedGate -Value 'ready' -Encoding ASCII
                }
            }
            if ($ConfigurationReadFailures -and (Test-Path -LiteralPath $clientLog)) {
                $readSteps = @(
                    @{ Marker = 'CATALOG_FAILED'; Control = '/release-catalog'; Gate = 'catalog-ready.ready' },
                    @{ Marker = 'CATALOG_RESTORED'; Control = '/empty-catalog'; Gate = 'catalog-empty.ready' },
                    @{ Marker = 'CATALOG_EMPTY'; Control = '/release-catalog'; Gate = 'catalog-release.ready' },
                    @{ Marker = 'DRAFT_READY'; Control = '/deny-read'; Gate = 'read-forbidden.ready' },
                    @{ Marker = 'FORBIDDEN'; Control = '/missing-read'; Gate = 'read-missing.ready' },
                    @{ Marker = 'MISSING'; Control = '/release-read'; Gate = 'read-release.ready' }
                )
                foreach ($step in $readSteps) {
                    $gate = Join-Path $configurationGates $step.Gate
                    if ((Test-Path -LiteralPath $gate) -or -not (Select-String -LiteralPath $clientLog -Pattern ('LOBBY_READ_FAILURES ' + $step.Marker) -Quiet)) { continue }
                    if ($step.Marker -eq 'MISSING') {
                        $afterFailedReads = Invoke-FixtureApi "characters/$($members[0].CharacterId)/progression" 'Get' $members[0].Headers
                        if (($afterFailedReads | ConvertTo-Json -Depth 8 -Compress) -cne ($configurationInitial | ConvertTo-Json -Depth 8 -Compress)) {
                            throw 'Failed reads or blocked UI actions modified the stored progression.'
                        }
                        [ordered]@{ initial = $configurationInitial; afterFailedReads = $afterFailedReads; unchanged = $true } |
                            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'FailedReadsState.proof.json') -Encoding UTF8
                    }
                    $null = Invoke-ConfigurationControl $step.Control 'Post'
                    Set-Content -LiteralPath $gate -Value 'ready' -Encoding ASCII
                }
            }
            $nextProxyCheck = [DateTime]::UtcNow.AddSeconds(1)
        }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Lobby logout E2E timed out; inspect client.log.' }
        if ((Test-Path -LiteralPath $clientLog) -and (Select-String -LiteralPath $clientLog -Pattern 'DUNGEON_FLOW_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet)) {
            throw 'Lobby logout runtime failed; inspect retained client.log (raw credentials are not printed).'
        }
        Start-Sleep -Milliseconds 250
    }
    $client.WaitForExit()
    if ($client.ExitCode -ne 0) { throw 'Lobby logout client exited unsuccessfully.' }
    $log = Get-Content -LiteralPath $clientLog -Raw -Encoding UTF8
    $expectedMarkers = if ($CreationOnly) {
        @('LOBBY_FRONTEND_E2E START LiveMap=1', 'DUNGEON_FLOW_E2E_LOBBY_CHARACTER_GUARDS InvalidNames=3',
          'DUNGEON_FLOW_E2E_LOBBY_CHARACTER_ROSTER OwnedCharacter=1', 'LOBBY_FRONTEND_E2E PASS LiveMap=1')
    } elseif ($ConfigurationOnly) {
        @('LOBBY_CONFIGURATION_E2E START LiveMap=1',
          'LOBBY_CONFIGURATION_E2E PASS LiveMap=1 SavedAndReloaded=1 SiblingIndependent=1 ReturnedToSavedCharacter=1') +
          $(if ($ConfigurationReadFailures) { @('LOBBY_READ_FAILURES CATALOG_FAILED', 'LOBBY_READ_FAILURES CATALOG_EMPTY',
              'LOBBY_READ_FAILURES FORBIDDEN', 'LOBBY_READ_FAILURES MISSING',
              'LOBBY_READ_FAILURES PASS Catalog503=1 EmptyCatalog=1 Backend403=1 Backend404=1 Preserved=1 Recovered=1') }
            elseif ($ConfigurationConflict) { @('LOBBY_CONFIGURATION_CONFLICT DRAFT_READY', 'LOBBY_CONFIGURATION_CONFLICT BLOCKED',
              'LOBBY_CONFIGURATION_CONFLICT RELOADED', 'LOBBY_CONFIGURATION_CONFLICT RESOLVED ServerConflict=1 Reloaded=1 ExplicitNewIntent=1') }
            elseif ($ConfigurationRecovery) { @('LOBBY_CONFIGURATION_RECOVERY DRAFT_READY', 'LOBBY_CONFIGURATION_RECOVERY UNKNOWN',
              'LOBBY_CONFIGURATION_RECOVERY RECOVERED Read401Draft=1 UnknownWrite401=1 SameAccountReauthentication=2') }
            else { @('LOBBY_CONFIGURATION_E2E SAVING SPGuard=1',
                'LOBBY_SKILL_PREVIEW_E2E DRAFT VisibleRows=3 FullCharge=1 QuickCharge=1 Reset=1 NoSaveFromPreview=1',
                'LOBBY_SKILL_PREVIEW_E2E PASS SavedAndReloaded=1 SiblingIndependent=1 ReturnedPreview=1 ClearedBeforeRead=1') })
    } else { @('LOBBY_LOGOUT_E2E REQUEST LiveMap=1', 'LOBBY_LOGOUT_E2E CONFIRMED BackendRevoked=1', 'LOBBY_LOGOUT_E2E PASS SameProcess=1') }
    foreach ($marker in $expectedMarkers) {
        if (-not $log.Contains($marker)) { throw 'Required lobby milestone missing; inspect client.log.' }
    }
    if ($log -match 'DUNGEON_FLOW_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:') { throw 'Lobby logout log contains a failure.' }
    if ($ConfigurationRecovery) {
        $proxyState = Invoke-ConfigurationControl '/state'
        $proxyState | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'ConfigurationRecovery.proxy.proof.json') -Encoding UTF8
        if ($proxyState.failure -or $proxyState.phase -ne 'replayed' -or $proxyState.readUnauthorized -ne 1 -or
            $proxyState.droppedAcknowledgements -ne 1 -or $proxyState.writeUnauthorized -ne 1 -or $proxyState.replayed -ne 1 -or
            -not $proxyState.replayCredentialChanged -or $proxyState.upstreamWrites -ne 3 -or
            $proxyState.committedResponseSHA256 -cne $proxyState.replayResponseSHA256) {
            throw 'Configuration recovery did not prove the original command, real unauthorized responses and identical committed replay.'
        }
    }
    if ($ConfigurationConflict) {
        $proxyState = Invoke-ConfigurationControl '/state'
        $proxyState | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'ConfigurationConflict.proxy.proof.json') -Encoding UTF8
        if ($proxyState.failure -or $proxyState.phase -ne 'conflict-resolved' -or $proxyState.conflictedWrites -ne 1 -or
            $proxyState.successfulWrites -ne 1 -or $proxyState.upstreamWrites -ne 2 -or $proxyState.droppedAcknowledgements -ne 0 -or
            $proxyState.conflictRequestId -eq $proxyState.successfulRequestId -or
            $proxyState.conflictExpectedRevision -cne $configurationInitial.revision -or
            $proxyState.conflictReloadRevision -cne $configurationCompetingState.revision -or
            $proxyState.successfulExpectedRevision -cne $configurationCompetingState.revision -or
            [long]$proxyState.successfulRevision -ne ([long]$configurationInitial.revision + 2)) {
            throw 'Conflict did not prove a real backend denial, fresh read and explicit new command.'
        }
    }
    if ($ConfigurationReadFailures) {
        $proxyState = Invoke-ConfigurationControl '/state'
        $proxyState | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'LookupFailures.proxy.proof.json') -Encoding UTF8
        if ($proxyState.failure -or $proxyState.phase -ne 'lookups-resolved' -or $proxyState.catalogUnavailableReplies -ne 1 -or
            $proxyState.catalogEmptyReplies -ne 1 -or $proxyState.catalogSuccessfulReads -ne 2 -or
            $proxyState.progressionForbiddenReads -ne 1 -or $proxyState.progressionMissingReads -ne 1 -or
            -not $proxyState.progressionReadRecovered -or $proxyState.creationPosts -ne 0 -or
            $proxyState.upstreamWrites -ne 1 -or $proxyState.successfulWrites -ne 1) {
            throw 'Lookup failures did not prove scoped read denials, no creation fallback and confirmed recovery.'
        }
    }
    if ($CreationOnly) {
        $roster = Invoke-FixtureApi 'characters' 'Get' $members[0].Headers
        if ($roster.Count -ne 1 -or $roster[0].name -ne $creationName -or $roster[0].progression.state -ne 'Available' -or
            $roster[0].progression.classId -ne 'RPGPlayerClassDefinition:GreatSwordWarrior' -or $roster[0].progression.level -ne 1) {
            throw 'UI-created character did not persist the selected supported class and initial growth.'
        }
        $otherRoster = Invoke-FixtureApi 'characters' 'Get' $members[1].Headers
        if ($null -ne $otherRoster -and @($otherRoster).Count -ne 0) { throw 'UI creation mutated another account.' }
    } elseif ($ConfigurationOnly) {
        $stored = Invoke-FixtureApi "characters/$($members[0].CharacterId)/progression" 'Get' $members[0].Headers
        $charge = @($stored.skills | Where-Object { $_.skillTag -ceq 'Player.Ability.Skill.Charge' })
        $expectedSlots = @('Player.Ability.Skill.Charge', 'Player.Ability.Skill.AssultBlade', '', '', '', '', '', 'Player.Ability.Skill.WhirlWind')
        $expectedAdvance = if ($ConfigurationConflict) { 2 } else { 1 }
        if ([long]$stored.revision -ne ([long]$configurationInitial.revision + $expectedAdvance) -or $stored.experience -ne 100 -or
            $stored.totalSP -ne 3 -or $stored.usedSP -ne 3 -or $stored.skills.Count -ne 3 -or $charge.Count -ne 1 -or
            $charge[0].level -ne 4 -or $charge[0].tripods[0] -cne 'FullCharge' -or $charge[0].tripods[1] -cne '' -or
            (@($stored.skillSlots) -join '|') -cne ($expectedSlots -join '|')) {
            throw 'UI configuration did not persist the expected SP/tripod/eight-slot revision.'
        }
        $sibling = Invoke-FixtureApi "characters/$($configurationSibling.characterId)/progression" 'Get' $members[0].Headers
        $other = Invoke-FixtureApi "characters/$($members[1].CharacterId)/progression" 'Get' $members[1].Headers
        if (($sibling | ConvertTo-Json -Depth 6 -Compress) -cne ($configurationSiblingInitial | ConvertTo-Json -Depth 6 -Compress) -or
            ($other | ConvertTo-Json -Depth 6 -Compress) -cne ($configurationOtherInitial | ConvertTo-Json -Depth 6 -Compress)) {
            throw 'Configuration mutated a sibling or another Steam account.'
        }
        $ownRoster = Invoke-FixtureApi 'characters' 'Get' $members[0].Headers
        $otherRoster = Invoke-FixtureApi 'characters' 'Get' $members[1].Headers
        if ($ownRoster.Count -ne 2 -or $members[0].CharacterId -notin @($ownRoster.characterId) -or
            $configurationSibling.characterId -notin @($ownRoster.characterId) -or
            $otherRoster.Count -ne 1 -or $otherRoster[0].characterId -ne $members[1].CharacterId) {
            throw 'Configuration changed an owned roster or other-device authentication.'
        }
        $configurationStateProof = [ordered]@{ initial = $configurationInitial; stored = $stored; siblingUnchanged = $true; otherAccountUnchanged = $true }
        if ($ConfigurationConflict) { $configurationStateProof['competing'] = $configurationCompetingState; $configurationStateProof['staleWritePreservedCompetingState'] = $true }
        $configurationStateProof |
            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'ConfigurationState.proof.json') -Encoding UTF8
    } else { foreach ($member in $members) {
        $roster = @(Invoke-FixtureApi 'characters' 'Get' $member.Headers)
        if ($roster.Count -ne 1 -or $roster[0].characterId -ne $member.CharacterId) { throw 'Other-device token or original roster was changed.' }
    } }
    $changedSources = @($sourceHashes | Where-Object {
        $_.sha256 -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot $_.path) -Algorithm SHA256).Hash
    } | ForEach-Object { $_.path })
    $moduleHashAfter = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
    $boundaryMarker = if ($ConfigurationOnly) { 'LOBBY_CONFIGURATION_E2E START LiveMap=1' } elseif ($CreationOnly) { 'LOBBY_FRONTEND_E2E START LiveMap=1' } else { 'LOBBY_LOGOUT_E2E REQUEST LiveMap=1' }
    $boundary = $log.IndexOf($boundaryMarker)
    $errorsBefore = [regex]::Matches($log.Substring(0, $boundary), 'Log\w+: Error:').Count
    $errorsAfter = [regex]::Matches($log.Substring($boundary), 'Log\w+: Error:').Count
    $proof = [ordered]@{
        scope = 'Actual standalone lobby/native handlers/UE HTTP, Development Memory'
        creationOnly = [bool]$CreationOnly; uiCreationAndPreparationNavigation = [bool]$CreationOnly
        configurationOnly = [bool]$ConfigurationOnly; uiConfigurationSaveReloadAndSiblingIsolation = [bool]$ConfigurationOnly
        uiSkillPreviewDraftResetSaveAndSiblingIsolation = [bool]($ConfigurationOnly -and -not $ConfigurationRecovery -and -not $ConfigurationConflict -and -not $ConfigurationReadFailures)
        configurationRecovery = [bool]$ConfigurationRecovery; read401DraftAndUnknownWriteRecovery = [bool]$ConfigurationRecovery
        configurationConflict = [bool]$ConfigurationConflict; realRevisionConflictReloadAndNewIntent = [bool]$ConfigurationConflict
        configurationReadFailures = [bool]$ConfigurationReadFailures; catalogFailureAndScoped403404Recovery = [bool]$ConfigurationReadFailures
        clientExitCode = $client.ExitCode; currentTokenHttp401 = $logoutMode; rosterAndSelectionReset = $logoutMode
        otherAccountLoginAndOwnedSelection = $logoutMode; otherDeviceTokensAndRostersPreserved = $true
        selectedSources = $sourceHashes; changedSources = $changedSources
        moduleSha256Before = $moduleHash; moduleSha256After = $moduleHashAfter
        binaryProvenancePassed = ($moduleHash -eq $moduleHashAfter -and $changedSources.Count -eq 0)
        logSha256 = (Get-FileHash -LiteralPath $clientLog -Algorithm SHA256).Hash
        runnerSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
        backendDllSha256 = (Get-FileHash -LiteralPath (Join-Path $buildRoot 'ProjectRpg.Backend.dll') -Algorithm SHA256).Hash
        backendBuildDirectory = $BackendBuildDirectory
        errorsBeforeFlow = $errorsBefore; errorsAfterFlow = $errorsAfter
        errorsBeforeLogoutRequest = $(if ($logoutMode) { $errorsBefore } else { $null })
        errorsAfterLogoutRequest = $(if ($logoutMode) { $errorsAfter } else { $null })
        strictLogAuditPassed = ($errorsBefore -eq 0 -and $errorsAfter -eq 0)
        notVerified = @('PostgreSQL', 'Steam platform or different PC', 'Physical mouse/GPU layout', 'Shipping',
            'Response loss during actual HTTP logout', 'Combat application of saved configuration', 'Configuration recovery across process restart') +
            $(if (-not $ConfigurationConflict) { @('Concurrent configuration revision conflict') } else { @() }) +
            $(if (-not $ConfigurationRecovery) { @('Configuration response loss or same-account reauthentication') } else { @() }) +
            $(if (-not $ConfigurationReadFailures) { @('Progression 403/404 and creation catalog failure UI') } else { @('Actual ownership transfer or deletion of the selected character save') })
    }
    $proof | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'Verification.proof.json') -Encoding UTF8
    if (-not $proof.binaryProvenancePassed) { throw 'Shared module/selected source changed; functional milestones retained in proof, current-binary coverage not established.' }
    if ($errorsAfter -gt 0) { throw 'Runtime Error after logout started; inspect proof and client.log.' }
    Write-Host "PASS actual lobby flow ($runDomain). Pre-flow Error count: $errorsBefore"
} finally {
    foreach ($ownedProcess in $processes) {
        if (-not $ownedProcess.HasExited) { $ownedProcess.Kill(); $ownedProcess.WaitForExit(10000) | Out-Null }
    }
    Write-Host "Owned test processes stopped; artifacts retained: $runRoot"
}
