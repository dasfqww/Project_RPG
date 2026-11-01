[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int] $Port = 17795,

    [ValidateRange(30, 300)]
    [int] $TimeoutSeconds = 90,

    [string] $BackendUrl = 'http://127.0.0.1:3000',

    [string] $AdminToken =
        $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN,

    [string] $Map = '/Game/Maps/testmap',

    [string] $GameMode =
        '/Game/Blueprints/GameMode/BP_GameModeBase.BP_GameModeBase_C',

    [switch] $TestTelemetryRestartRecovery,

    [switch] $TestTelemetryBackendRecovery,

    [switch] $TestTelemetryHttpRecovery
)

$ErrorActionPreference = 'Stop'

if (@($TestTelemetryRestartRecovery, $TestTelemetryBackendRecovery,
        $TestTelemetryHttpRecovery | Where-Object { [bool] $_ }).Count -gt 1) {
    throw 'Run the three telemetry recovery scenarios separately.'
}
$testUsesPostgres = $TestTelemetryBackendRecovery -or $TestTelemetryHttpRecovery

function Repair-DuplicatePathEnvironment {
    $processEnvironment =
        [Environment]::GetEnvironmentVariables('Process')
    $pathKeys = @($processEnvironment.Keys | Where-Object {
        $_.ToString() -ieq 'Path'
    })
    if ($pathKeys.Count -le 1) {
        return
    }

    $pathValue = [string] $processEnvironment['Path']
    if ([string]::IsNullOrWhiteSpace($pathValue)) {
        $pathValue = [string] $processEnvironment[$pathKeys[0]]
    }
    foreach ($pathKey in $pathKeys) {
        [Environment]::SetEnvironmentVariable(
            $pathKey.ToString(),
            $null,
            'Process')
    }
    [Environment]::SetEnvironmentVariable(
        'Path',
        $pathValue,
        'Process')
}

Repair-DuplicatePathEnvironment

function Invoke-JsonRequest {
    param(
        [ValidateSet('Get', 'Post')]
        [string] $Method,

        [string] $Uri,

        [hashtable] $Headers = @{},

        [object] $Body,

        [int[]] $ExpectedStatus
    )

    $parameters = @{
        Method = $Method
        Uri = $Uri
        Headers = $Headers
        UseBasicParsing = $true
        TimeoutSec = 20
    }
    if ($null -ne $Body) {
        $parameters.ContentType = 'application/json'
        $parameters.Body = $Body |
            ConvertTo-Json -Depth 20 -Compress
    }

    $statusCode = 0
    $content = ''
    try {
        $response = Invoke-WebRequest @parameters
        $statusCode = [int] $response.StatusCode
        $content = $response.Content
    }
    catch {
        $errorResponse = $_.Exception.Response
        if ($null -eq $errorResponse) {
            throw
        }

        $statusCode = [int] $errorResponse.StatusCode
        $reader = New-Object System.IO.StreamReader(
            $errorResponse.GetResponseStream())
        try {
            $content = $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }

    if ($statusCode -notin $ExpectedStatus) {
        throw (
            "Expected $Method $Uri to return one of " +
            "[$($ExpectedStatus -join ', ')], " +
            "got ${statusCode}: $content")
    }
    if ([string]::IsNullOrWhiteSpace($content)) {
        return $null
    }
    return $content | ConvertFrom-Json
}

function Test-BackendHealth {
    param([string] $BaseUrl)

    try {
        $health = Invoke-RestMethod `
            -Method Get `
            -Uri "$BaseUrl/health" `
            -TimeoutSec 2
        return $health.status -eq 'ok'
    }
    catch {
        return $false
    }
}

function Test-LogMarker {
    param(
        [string] $Path,
        [string] $Marker
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    return [bool] (Select-String `
        -LiteralPath $Path `
        -SimpleMatch $Marker `
        -Quiet `
        -ErrorAction SilentlyContinue)
}

function Get-LogTail {
    param([string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return "Log file was not created: $Path"
    }
    return (Get-Content `
        -LiteralPath $Path `
        -Tail 60 `
        -ErrorAction SilentlyContinue) -join [Environment]::NewLine
}

function Stop-OwnedProcess {
    param([System.Diagnostics.Process] $Process)

    if ($null -eq $Process) {
        return
    }
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -ErrorAction Stop
            $Process.WaitForExit(5000) | Out-Null
        }
    }
    catch {
        Write-Warning (
            "Could not stop owned process $($Process.Id): " +
            $_.Exception.Message)
    }
}

function New-ItemRecord {
    param(
        [string] $DefinitionName,
        [Guid] $ItemId,
        [Guid] $CharacterId,
        [long] $Revision,
        [int] $Quantity,
        [int] $SlotIndex
    )

    return @{
        definitionType = 'RPGItemDefinition'
        definitionName = $DefinitionName
        definitionVersion = 1
        owner = @{
            type = 'Character'
            ownerId = $CharacterId.ToString('D')
        }
        location = @{
            containerType = 'Inventory'
            containerId = $CharacterId.ToString('D')
            slotIndex = $SlotIndex
        }
        state = @{
            instanceId = $ItemId
            generationSeed = 104729
            quantity = $Quantity
            # The backend accepts arbitrary tag strings, whereas Unreal only
            # restores registered GameplayTags.  This command-flow fixture
            # intentionally has no instance modifiers.
            instanceTags = @()
            statValues = @()
        }
        revision = $Revision
        lifecycleState = 'Active'
        metadata = @{
            bindState = 'Unbound'
            durability = @{
                current = 0
                maximum = 0
            }
            expiresAtUtc = $null
            creationSource = ''
            isLocked = $false
        }
    }
}

$projectRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
$gamePath = Join-Path $projectRoot 'Binaries\Win64\Project_RPG.exe'
$cookedRoot = Join-Path $projectRoot 'Saved\Cooked\Windows'
$assetRegistryPath = Join-Path `
    $cookedRoot `
    'Project_RPG\AssetRegistry.bin'
$potionAssetPath = Join-Path `
    $cookedRoot `
    'Project_RPG\Content\Blueprints\GameData\Item\V2\DA_Item_Potion_Red_Large.uasset'
$helmAssetPath = Join-Path `
    $cookedRoot `
    'Project_RPG\Content\Blueprints\GameData\Item\V2\DA_Item_Helm_Leather.uasset'
$backendProject = Join-Path `
    $projectRoot `
    'Backend\ProjectRpg.Backend\ProjectRpg.Backend.csproj'

foreach ($requiredPath in @(
    $projectPath,
    $gamePath,
    $assetRegistryPath,
    $potionAssetPath,
    $helmAssetPath,
    $backendProject)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required E2E input was not found: $requiredPath"
    }
}

$gameBinaryTimestamp =
    (Get-Item -LiteralPath $gamePath).LastWriteTimeUtc
$runtimeModuleRoot = Join-Path $projectRoot 'Source\Project_RPG'
$e2eBuildInputs = @(
    (Get-Item -LiteralPath $projectPath),
    (Get-Item -LiteralPath (Join-Path $projectRoot `
        'Source\Project_RPG.Target.cs')),
    (Get-Item -LiteralPath (Join-Path $runtimeModuleRoot `
        'Project_RPG.Build.cs'))
)
$e2eBuildInputs += @(
    Get-ChildItem -LiteralPath $runtimeModuleRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.h', '.cpp') }
)
$newerE2EBuildInput = $e2eBuildInputs |
    Where-Object { $_.LastWriteTimeUtc -gt $gameBinaryTimestamp } |
    Select-Object -First 1
if ($null -ne $newerE2EBuildInput) {
    throw (
        'Project_RPG.exe is older than a runtime build input. ' +
        'Build the Project_RPG Development Win64 target before running ' +
        "this test. Newer file: $($newerE2EBuildInput.FullName)")
}

$normalizedBackendUrl = $BackendUrl.TrimEnd('/')
if ($normalizedBackendUrl -notin @(
    'http://127.0.0.1:3000',
    'http://localhost:3000')) {
    throw (
        'The current UE runtime config uses localhost:3000. ' +
        'Use -BackendUrl http://127.0.0.1:3000.')
}
$backendHostUrl = $normalizedBackendUrl
$proxyScript = Join-Path $PSScriptRoot 'Fixtures\TelemetryHttpFaultProxy.mjs'
if ($TestTelemetryHttpRecovery) {
    Get-Command node -ErrorAction Stop | Out-Null
    if (-not (Test-Path -LiteralPath $proxyScript -PathType Leaf)) {
        throw 'The local telemetry HTTP fault proxy fixture was not found.'
    }
    $backendHostUrl = 'http://127.0.0.1:3002'
    if (@(Get-NetTCPConnection -State Listen -LocalPort 3002 `
            -ErrorAction SilentlyContinue).Count -gt 0) {
        throw 'HTTP recovery requires an unused localhost port 3002.'
    }
}

$runStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runHex = [Guid]::NewGuid().ToString('N').Substring(0, 12)
$logRoot = Join-Path `
    $projectRoot `
    "Saved\Logs\ItemBackendE2E\$runStamp"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

$serverLogName = "ItemE2E-$runStamp-Server.log"
$clientLogName = "ItemE2E-$runStamp-Client.log"
$recoveryServerLogName =
    "ItemE2E-$runStamp-RecoveryServer.log"
$serverRequestedLog = Join-Path $logRoot $serverLogName
$clientRequestedLog = Join-Path $logRoot $clientLogName
$recoveryServerRequestedLog = Join-Path `
    $logRoot `
    $recoveryServerLogName
$runtimeLogRoot = Join-Path `
    $cookedRoot `
    "Project_RPG\Saved\Logs\ItemBackendE2E\$runStamp"
$serverRuntimeLog = Join-Path $runtimeLogRoot $serverLogName
$clientRuntimeLog = Join-Path $runtimeLogRoot $clientLogName
$recoveryServerRuntimeLog = Join-Path `
    $runtimeLogRoot `
    $recoveryServerLogName
$backendOutputLog = Join-Path $logRoot 'Backend.stdout.log'
$backendErrorLog = Join-Path $logRoot 'Backend.stderr.log'
$proxyOutputLog = Join-Path $logRoot 'HttpProxy.stdout.log'
$proxyErrorLog = Join-Path $logRoot 'HttpProxy.stderr.log'
$restrictedReleasePath = Join-Path $logRoot 'Restriction.release'

$backendProcess = $null
$serverProcess = $null
$clientProcess = $null
$proxyProcess = $null
$proxyControlUrl = $null
$proxyControlHeaders = $null
$serverHeaders = $null
$sessionId = $null
$serverId = "item-e2e-$runHex"
$settled = $false
$postgresContainerName = "project-rpg-telemetry-e2e-$runHex"
$ownsPostgresContainer = $false
$postgresPaused = $false
$testPostgresConnectionString = $null
$outageInjected = $false
$outageBatch = $null

try {
    $backendWasRunning = Test-BackendHealth $normalizedBackendUrl
    if ($testUsesPostgres) {
        if ($backendWasRunning -or
            @(Get-NetTCPConnection -State Listen -LocalPort 3000 `
                -ErrorAction SilentlyContinue).Count -gt 0) {
            throw (
                'Backend recovery requires an unused port 3000 so the ' +
                'test can own its backend and isolated PostgreSQL instance.')
        }
        $postgresPassword = [Guid]::NewGuid().ToString('N')
        $containerId = & docker run --detach --rm `
            --name $postgresContainerName `
            --label 'project_rpg.test=telemetry-recovery' `
            --tmpfs '/var/lib/postgresql/data:rw' `
            --env 'POSTGRES_DB=project_rpg' `
            --env 'POSTGRES_USER=project_rpg' `
            --env "POSTGRES_PASSWORD=$postgresPassword" `
            --publish '127.0.0.1::5432' `
            --health-cmd 'pg_isready -U project_rpg -d project_rpg' `
            --health-interval 1s --health-timeout 3s --health-retries 30 `
            'postgres:17-alpine'
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not start the isolated PostgreSQL test container.'
        }
        $ownsPostgresContainer = $true
        $postgresReady = $false
        $postgresDeadline = (Get-Date).AddSeconds(45)
        while ((Get-Date) -lt $postgresDeadline) {
            $healthStatus = & docker inspect --format `
                '{{.State.Health.Status}}' $postgresContainerName
            if ($LASTEXITCODE -ne 0) {
                throw 'Could not inspect the isolated PostgreSQL container.'
            }
            if ($healthStatus -eq 'healthy') {
                $postgresReady = $true
                break
            }
            Start-Sleep -Milliseconds 200
        }
        if (-not $postgresReady) {
            throw 'The isolated PostgreSQL container did not become healthy.'
        }
        $portBinding = & docker port $postgresContainerName '5432/tcp'
        if ($LASTEXITCODE -ne 0 -or
            $portBinding -notmatch '^127\.0\.0\.1:(\d+)$') {
            throw 'Could not resolve the isolated PostgreSQL localhost port.'
        }
        $testPostgresConnectionString =
            "Host=127.0.0.1;Port=$($Matches[1]);Database=project_rpg;" +
            "Username=project_rpg;Password=$postgresPassword;" +
            'Timeout=2;Command Timeout=2'
        Write-Host 'Started an isolated, temporary PostgreSQL test container.'
    }
    if ($backendWasRunning) {
        if ([string]::IsNullOrWhiteSpace($AdminToken)) {
            throw (
                'A backend is already running on port 3000. ' +
                'Pass its -AdminToken or set ' +
                'PROJECT_RPG_BACKEND_ADMIN_TOKEN.')
        }
        Write-Host 'Using the existing local backend.'
    }
    else {
        if ([string]::IsNullOrWhiteSpace($AdminToken)) {
            $AdminToken =
                "item-e2e-admin-$([Guid]::NewGuid().ToString('N'))"
        }

        $environmentNames = @(
            'ASPNETCORE_ENVIRONMENT',
            'PROJECT_RPG_BACKEND_ADMIN_TOKEN',
            'POSTGRES_CONNECTION_STRING',
            'Storage__Provider',
            'Kestrel__Endpoints__Http__Url')
        $previousEnvironment = @{}
        foreach ($name in $environmentNames) {
            $previousEnvironment[$name] =
                [Environment]::GetEnvironmentVariable($name, 'Process')
        }
        try {
            [Environment]::SetEnvironmentVariable(
                'ASPNETCORE_ENVIRONMENT',
                'Development',
                'Process')
            [Environment]::SetEnvironmentVariable(
                'PROJECT_RPG_BACKEND_ADMIN_TOKEN',
                $AdminToken,
                'Process')
            [Environment]::SetEnvironmentVariable(
                'POSTGRES_CONNECTION_STRING',
                $testPostgresConnectionString,
                'Process')
            $testStorageProvider = 'Memory'
            if ($testUsesPostgres) {
                $testStorageProvider = 'Postgres'
            }
            [Environment]::SetEnvironmentVariable(
                'Storage__Provider', $testStorageProvider, 'Process')
            [Environment]::SetEnvironmentVariable(
                'Kestrel__Endpoints__Http__Url',
                $backendHostUrl,
                'Process')
            # Start the DLL directly so the process we own is the HTTP host,
            # rather than a dotnet-run parent with a surviving child process.
            $backendBuildRoot = Join-Path $logRoot 'BackendBuild'
            & dotnet build $backendProject --no-restore `
                --output $backendBuildRoot --verbosity quiet
            if ($LASTEXITCODE -ne 0) {
                throw 'The local backend build failed.'
            }
            $backendDll = Join-Path $backendBuildRoot 'ProjectRpg.Backend.dll'
            $backendProcess = Start-Process `
                -FilePath 'dotnet' `
                -ArgumentList @("`"$backendDll`"",
                    '--DungeonContent:Entries:1:DungeonId=Dungeon.ItemE2E',
                    "--DungeonContent:Entries:1:MapPackage=$Map",
                    '--DungeonContent:Entries:1:Difficulties:0=Normal') `
                -WorkingDirectory $backendBuildRoot `
                -RedirectStandardOutput $backendOutputLog `
                -RedirectStandardError $backendErrorLog `
                -WindowStyle Hidden `
                -PassThru
        }
        finally {
            foreach ($name in $environmentNames) {
                [Environment]::SetEnvironmentVariable(
                    $name,
                    $previousEnvironment[$name],
                    'Process')
            }
        }

        $backendDeadline = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $backendDeadline) {
            $backendProcess.Refresh()
            if ($backendProcess.HasExited) {
                throw (
                    'The local backend exited during startup.' +
                    [Environment]::NewLine +
                    (Get-LogTail $backendErrorLog))
            }
            if (Test-BackendHealth $backendHostUrl) {
                break
            }
            Start-Sleep -Milliseconds 200
        }
        if (-not (Test-BackendHealth $backendHostUrl)) {
            throw (
                'The local backend did not become healthy.' +
                [Environment]::NewLine +
                (Get-LogTail $backendOutputLog))
        }
        Write-Host "Started a Development/$testStorageProvider backend."
    }

    if ($testUsesPostgres) {
        $readiness = Invoke-JsonRequest -Method Get `
            -Uri "$backendHostUrl/health/ready" -Body $null -ExpectedStatus 200
        if ($readiness.storageProvider -ne 'Postgres' -or
            $readiness.status -ne 'ready') {
            throw 'The recovery fixture must use ready PostgreSQL storage.'
        }
    }
    if ($TestTelemetryHttpRecovery) {
        $proxyControlToken = [Guid]::NewGuid().ToString('N')
        $proxyControlHeaders = @{ Authorization = "Bearer $proxyControlToken" }
        $proxyEnvironment = @{
            PROJECT_RPG_E2E_PROXY_CONTROL_TOKEN = $proxyControlToken
            PROJECT_RPG_E2E_PROXY_LISTEN_PORT = '3000'
            PROJECT_RPG_E2E_PROXY_UPSTREAM_PORT = '3002'
        }
        $previousProxyEnvironment = @{}
        try {
            foreach ($name in $proxyEnvironment.Keys) {
                $previousProxyEnvironment[$name] =
                    [Environment]::GetEnvironmentVariable($name, 'Process')
                [Environment]::SetEnvironmentVariable(
                    $name, $proxyEnvironment[$name], 'Process')
            }
            $proxyProcess = Start-Process -FilePath 'node' `
                -ArgumentList @("`"$proxyScript`"") `
                -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $proxyOutputLog `
                -RedirectStandardError $proxyErrorLog
        }
        finally {
            foreach ($name in $previousProxyEnvironment.Keys) {
                [Environment]::SetEnvironmentVariable(
                    $name, $previousProxyEnvironment[$name], 'Process')
            }
        }
        $proxyDeadline = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $proxyDeadline) {
            $proxyProcess.Refresh()
            if ($proxyProcess.HasExited) {
                throw ('The HTTP fault proxy exited during startup.' +
                    [Environment]::NewLine + (Get-LogTail $proxyErrorLog))
            }
            if (Test-Path -LiteralPath $proxyOutputLog) {
                $readyLine = Get-Content -LiteralPath $proxyOutputLog |
                    Where-Object { $_ -match '^E2E_PROXY_READY ControlPort=\d+ ProxyPort=3000$' } |
                    Select-Object -Last 1
                if ($null -ne $readyLine -and
                    $readyLine -match 'ControlPort=(\d+)') {
                    $proxyControlUrl = "http://127.0.0.1:$($Matches[1])"
                    break
                }
            }
            Start-Sleep -Milliseconds 100
        }
        if ($null -eq $proxyControlUrl -or
            -not (Test-BackendHealth $normalizedBackendUrl)) {
            throw 'The local HTTP fault proxy did not become ready.'
        }
        Write-Host 'Started the local HTTP fault proxy on port 3000.'
    }

    $runNumber = [Convert]::ToUInt32($runHex.Substring(0, 8), 16)
    $steamId =
        ([uint64] 76561260000000000 + [uint64] $runNumber).ToString()
    $auth = Invoke-JsonRequest `
        -Method Post `
        -Uri "$normalizedBackendUrl/api/auth/steam-ticket" `
        -Body @{ ticket = "dev:$steamId" } `
        -ExpectedStatus 200
    $playerHeaders = @{
        Authorization = "Bearer $($auth.accessToken)"
    }
    $adminHeaders = @{
        Authorization = "Bearer $AdminToken"
    }

    $character = Invoke-JsonRequest `
        -Method Post `
        -Uri "$normalizedBackendUrl/api/characters" `
        -Headers $playerHeaders `
        -Body @{
            name = "ItemE2E$($runHex.Substring(0, 8))"
        } `
        -ExpectedStatus 201
    $characterId = [Guid] $character.characterId

    $session = Invoke-JsonRequest `
        -Method Post `
        -Uri "$normalizedBackendUrl/api/dungeon-sessions" `
        -Headers $playerHeaders `
        -Body @{
            characterId = $characterId
            dungeonId = 'Dungeon.ItemE2E'
            difficulty = 'Normal'
        } `
        -ExpectedStatus 201
    $sessionId = [Guid] $session.dungeonSessionId
    . (Join-Path $projectRoot 'Backend/Tools/Request-DungeonDeparture.ps1')
    $null = Request-RPGDungeonDeparture $normalizedBackendUrl $sessionId $characterId $playerHeaders
    $session = Invoke-JsonRequest `
        -Method Post `
        -Uri (
            "$normalizedBackendUrl/api/dungeon-sessions/" +
            "$($sessionId.ToString('D'))/activate") `
        -Headers $adminHeaders `
        -Body @{
            serverId = $serverId
            serverAddress = "127.0.0.1:$Port"
        } `
        -ExpectedStatus 200
    if ([string]::IsNullOrWhiteSpace(
        [string] $session.gameServerAccessToken)) {
        throw 'Dungeon activation did not issue a game-server token.'
    }
    $serverHeaders = @{
        Authorization =
            "Bearer $($session.gameServerAccessToken)"
    }

    $serverEnvironmentNames = @(
        'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
        'PROJECT_RPG_GAME_SERVER_ID',
        'PROJECT_RPG_DUNGEON_SESSION_ID')
    $previousServerEnvironment = @{}
    foreach ($name in $serverEnvironmentNames) {
        $previousServerEnvironment[$name] =
            [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    try {
        [Environment]::SetEnvironmentVariable(
            'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
            [string] $session.gameServerAccessToken,
            'Process')
        [Environment]::SetEnvironmentVariable(
            'PROJECT_RPG_GAME_SERVER_ID',
            $serverId,
            'Process')
        [Environment]::SetEnvironmentVariable(
            'PROJECT_RPG_DUNGEON_SESSION_ID',
            $sessionId.ToString('D'),
            'Process')

        $serverArguments = @(
            "-project=$projectPath",
            "-basedir=$cookedRoot",
            "${Map}?listen?game=$GameMode",
            '-server',
            "-port=$Port",
            '-log',
            "-abslog=$serverRequestedLog",
            '-unattended',
            '-nosplash',
            '-NoSound',
            '-nullrhi',
            '-multiprocess',
            '-nosteam',
            '-NoLiveCoding',
            '-UseIrisReplication=1',
            '-RPGItemE2EServer',
            '-RPGItemE2EAllowUnverifiedIdentity',
            '-RPGSecurityTelemetryDevListenServer',
            '-RPGItemE2ERestricted')
        $serverProcess = Start-Process `
            -FilePath $gamePath `
            -ArgumentList $serverArguments `
            -WorkingDirectory $projectRoot `
            -WindowStyle Hidden `
            -PassThru
    }
    finally {
        foreach ($name in $serverEnvironmentNames) {
            [Environment]::SetEnvironmentVariable(
                $name,
                $previousServerEnvironment[$name],
                'Process')
        }
    }
    Write-Host (
        "Started the UE Development listen-server fixture on " +
        "127.0.0.1:$Port.")

    $serverDeadline = (Get-Date).AddSeconds(
        [Math]::Min(45, $TimeoutSeconds))
    $serverReady = $false
    while ((Get-Date) -lt $serverDeadline) {
        $serverProcess.Refresh()
        if ($serverProcess.HasExited) {
            throw (
                'The UE server exited before session start.' +
                [Environment]::NewLine +
                (Get-LogTail $serverRuntimeLog))
        }

        try {
            $currentSession = Invoke-JsonRequest `
                -Method Get `
                -Uri (
                    "$normalizedBackendUrl/api/dungeon-sessions/" +
                    $sessionId.ToString('D')) `
                -Headers $adminHeaders `
                -Body $null `
                -ExpectedStatus 200
            if ($currentSession.state -eq 'InProgress' -and
                (Test-LogMarker `
                    $serverRuntimeLog `
                    'Item Cache Refreshed. Found 0 legacy and 2 native definitions.')) {
                $serverReady = $true
                break
            }
        }
        catch {
            # The backend and UE logs may become visible on different ticks.
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $serverReady) {
        throw (
            'The UE server did not start the backend session with two ' +
            'native item definitions.' +
            [Environment]::NewLine +
            (Get-LogTail $serverRuntimeLog))
    }

    $itemId = [Guid]::NewGuid()
    $grantBody = @{
        requestId = [Guid]::NewGuid()
        operation = 'GrantItem'
        commandFingerprint = "grant|$itemId|3|0"
        actor = @{
            type = 'Character'
            ownerId = $characterId.ToString('D')
        }
        affectedQuantity = 3
        mutations = @(
            @{
                expectedRevision = 0
                newRecord = New-ItemRecord `
                    -DefinitionName 'GameItem.Consume.Potion.Red.Large' `
                    -ItemId $itemId `
                    -CharacterId $characterId `
                    -Revision 0 `
                    -Quantity 3 `
                    -SlotIndex 0
            }
        )
    }
    $grant = Invoke-JsonRequest `
        -Method Post `
        -Uri "$normalizedBackendUrl/api/item-transactions/commit" `
        -Headers $serverHeaders `
        -Body $grantBody `
        -ExpectedStatus 200
    if ($grant.status -ne 'Committed' -or
        $grant.records[0].revision -ne 1 -or
        $grant.records[0].state.quantity -ne 3) {
        throw 'The E2E item grant did not create quantity 3 at revision 1.'
    }

    $equipmentItemId = [Guid]::NewGuid()
    $equipmentGrantBody = @{
        requestId = [Guid]::NewGuid()
        operation = 'GrantItem'
        commandFingerprint = "grant|$equipmentItemId|1|1"
        actor = @{
            type = 'Character'
            ownerId = $characterId.ToString('D')
        }
        affectedQuantity = 1
        mutations = @(
            @{
                expectedRevision = 0
                newRecord = New-ItemRecord `
                    -DefinitionName 'GameItem.Equipment.Helm.Default' `
                    -ItemId $equipmentItemId `
                    -CharacterId $characterId `
                    -Revision 0 `
                    -Quantity 1 `
                    -SlotIndex 1
            }
        )
    }
    $equipmentGrant = Invoke-JsonRequest `
        -Method Post `
        -Uri "$normalizedBackendUrl/api/item-transactions/commit" `
        -Headers $serverHeaders `
        -Body $equipmentGrantBody `
        -ExpectedStatus 200
    if ($equipmentGrant.status -ne 'Committed' -or
        $equipmentGrant.records[0].revision -ne 1 -or
        $equipmentGrant.records[0].state.quantity -ne 1) {
        throw (
            'The E2E equipment grant did not create quantity 1 at ' +
            'revision 1.')
    }

    $joinTicket = Invoke-JsonRequest `
        -Method Post `
        -Uri "$normalizedBackendUrl/api/join-tickets" `
        -Headers $playerHeaders `
        -Body @{
            characterId = $characterId
            dungeonSessionId = $sessionId
        } `
        -ExpectedStatus 200
    $encodedTicket = [Uri]::EscapeDataString(
        [string] $joinTicket.joinTicket)
    $clientUrl =
        "127.0.0.1:$Port`?JoinTicket=$encodedTicket"
    $clientArguments = @(
        "-project=$projectPath",
        "-basedir=$cookedRoot",
        $clientUrl,
        '-game',
        '-log',
        "-abslog=$clientRequestedLog",
        '-unattended',
        '-nosplash',
        '-NoSound',
        '-nullrhi',
        '-multiprocess',
        '-nosteam',
        '-NoLiveCoding',
        '-RPGItemE2ERestricted',
        "-RPGItemE2EConsume=$($itemId.ToString('D'))",
        "-RPGItemE2EEquip=$($equipmentItemId.ToString('D'))")
    if ($testUsesPostgres) {
        $clientArguments +=
            "-RPGItemE2ERestrictedReleaseFile=`"$restrictedReleasePath`""
    }
    $clientProcess = Start-Process `
        -FilePath $gamePath `
        -ArgumentList $clientArguments `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden `
        -PassThru
    Write-Host 'Started the authenticated headless UE client.'

    $clientDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $clientPassed = $false
    while ((Get-Date) -lt $clientDeadline) {
        $serverProcess.Refresh()
        $clientProcess.Refresh()
        if ($serverProcess.HasExited) {
            throw (
                'The UE server exited during the item command.' +
                [Environment]::NewLine +
                (Get-LogTail $serverRuntimeLog))
        }
        if ($testUsesPostgres -and -not $outageInjected -and
            (Test-LogMarker $clientRuntimeLog 'RPG_ITEM_E2E RESTRICTION_READY')) {
            if ($TestTelemetryBackendRecovery) {
                & docker pause $postgresContainerName | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    throw 'Could not pause the isolated PostgreSQL test container.'
                }
                $postgresPaused = $true
                Write-Host 'Paused the test database after normal item RPCs.'
            }
            else {
                Invoke-JsonRequest -Method Post -Uri "$proxyControlUrl/mode" `
                    -Headers $proxyControlHeaders -ExpectedStatus 200 -Body @{
                        mode = 'blocked'
                        dungeonSessionId = $sessionId.ToString('D')
                    } | Out-Null
                Write-Host 'Disconnected the test HTTP path after normal item RPCs.'
            }
            $outageInjected = $true
            New-Item -ItemType File -Path $restrictedReleasePath | Out-Null
            Write-Host 'Released the Restricted-state probe during the outage.'
        }
        if (Test-LogMarker $clientRuntimeLog 'RPG_ITEM_E2E PASS') {
            $clientPassed = $true
            break
        }
        if (Test-LogMarker $clientRuntimeLog 'RPG_ITEM_E2E FAIL') {
            throw (
                'The UE item command probe reported failure.' +
                [Environment]::NewLine +
                (Get-LogTail $clientRuntimeLog))
        }
        if ($clientProcess.HasExited) {
            throw (
                'The UE client exited before the item command completed.' +
                [Environment]::NewLine +
                (Get-LogTail $clientRuntimeLog))
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $clientPassed) {
        throw (
            'Timed out waiting for the UE item command probe.' +
            [Environment]::NewLine +
            (Get-LogTail $clientRuntimeLog))
    }

    $telemetrySpoolPath = Join-Path `
        $cookedRoot `
        ("Project_RPG\Saved\SecurityTelemetry\" +
            "$($sessionId.ToString('D')).jsonl")
    if ($testUsesPostgres) {
        if (-not $outageInjected) {
            throw 'The backend outage checkpoint was not reached.'
        }
        $outageDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $retryMatches = @()
        while ((Get-Date) -lt $outageDeadline) {
            $serverProcess.Refresh()
            $backendProcess.Refresh()
            if ($serverProcess.HasExited -or $backendProcess.HasExited) {
                throw 'A server process exited during the backend outage.'
            }
            $retryMatches = @(Select-String -LiteralPath $serverRuntimeLog `
                -Pattern ('Security telemetry retry scheduled: HTTP=(\d+) ' +
                    'Attempt=(\d+) Delay=([\d.]+)s Queued=(\d+)\.'))
            if ($retryMatches.Count -ge 2) {
                break
            }
            Start-Sleep -Milliseconds 100
        }
        if ($retryMatches.Count -lt 2) {
            throw ('Did not observe two telemetry failures and retries.' +
                [Environment]::NewLine + (Get-LogTail $serverRuntimeLog))
        }
        for ($retryIndex = 0; $retryIndex -lt 2; $retryIndex++) {
            $groups = $retryMatches[$retryIndex].Matches[0].Groups
            $httpStatus = [int] $groups[1].Value
            $expectedAttempt = $retryIndex + 1
            $expectedDelay = 2 * [Math]::Pow(2, $retryIndex)
            if (($httpStatus -ne 0 -and $httpStatus -lt 500) -or
                ($TestTelemetryHttpRecovery -and $httpStatus -ne 0) -or
                [int] $groups[2].Value -ne $expectedAttempt -or
                [double]::Parse($groups[3].Value,
                    [Globalization.CultureInfo]::InvariantCulture) -ne $expectedDelay -or
                [int] $groups[4].Value -ne 3) {
                throw 'The observed outage retries did not retain three events with 2s/4s backoff.'
            }
        }
        $outageRecords = @(Get-Content -LiteralPath $telemetrySpoolPath `
            -Encoding utf8 | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_)
            } | ForEach-Object { $_ | ConvertFrom-Json })
        $outageEvents = @($outageRecords | ForEach-Object { $_.events })
        if ($outageRecords.Count -ne 3 -or $outageEvents.Count -ne 3 -or
            @($outageEvents.eventId | Select-Object -Unique).Count -ne 3 -or
            @($outageRecords | Where-Object {
                $_.dungeonSessionId -ne $sessionId.ToString('D')
            }).Count -ne 0) {
            throw 'The outage did not preserve exactly three unique session events on disk.'
        }
        $outageBatch = @{
            dungeonSessionId = $sessionId.ToString('D')
            events = $outageEvents
        }
        Copy-Item -LiteralPath $telemetrySpoolPath `
            -Destination (Join-Path $logRoot 'Telemetry.outage.jsonl')
        $outageSpoolHash = (Get-FileHash -LiteralPath $telemetrySpoolPath).Hash
        if ($TestTelemetryBackendRecovery) {
            & docker unpause $postgresContainerName | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw 'Could not restore the isolated PostgreSQL test container.'
            }
            $postgresPaused = $false
            $storageDeadline = (Get-Date).AddSeconds(30)
            $storageReady = $false
            while ((Get-Date) -lt $storageDeadline) {
                try {
                    $ready = Invoke-JsonRequest -Method Get `
                        -Uri "$backendHostUrl/health/ready" -Body $null `
                        -ExpectedStatus @(200, 503)
                    if ($ready.status -eq 'ready') {
                        $storageReady = $true
                        break
                    }
                }
                catch { }
                Start-Sleep -Milliseconds 100
            }
            if (-not $storageReady) {
                throw 'PostgreSQL readiness did not recover after the outage.'
            }
            Write-Host 'Observed retained evidence and 2s/4s backoff; restored the database.'
        }
        else {
            $beforeCommit = Invoke-JsonRequest -Method Get `
                -Uri ("$backendHostUrl/api/security/sessions/" +
                    "$($sessionId.ToString('D'))/summary") `
                -Headers $adminHeaders -Body $null -ExpectedStatus 200
            if ($beforeCommit.totalEvents -ne 0) {
                throw 'HTTP-disconnected telemetry unexpectedly reached PostgreSQL.'
            }
            Invoke-JsonRequest -Method Post -Uri "$proxyControlUrl/mode" `
                -Headers $proxyControlHeaders -ExpectedStatus 200 -Body @{
                    mode = 'drop_next_ack'
                    dungeonSessionId = $sessionId.ToString('D')
                } | Out-Null
            $lostAckDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
            $lostAckObserved = $false
            while ((Get-Date) -lt $lostAckDeadline) {
                foreach ($ownedHost in @($serverProcess, $backendProcess, $proxyProcess)) {
                    $ownedHost.Refresh()
                    if ($ownedHost.HasExited) {
                        throw 'A test host exited before the lost-ack checkpoint.'
                    }
                }
                $proxyState = Invoke-JsonRequest -Method Get `
                    -Uri "$proxyControlUrl/state" -Headers $proxyControlHeaders `
                    -Body $null -ExpectedStatus 200
                if ($proxyState.droppedAcknowledgements -eq 1 -and
                    (Test-LogMarker $serverRuntimeLog `
                        'HTTP=0 Attempt=3 Delay=8.00s Queued=3.')) {
                    $lostAckObserved = $true
                    break
                }
                Start-Sleep -Milliseconds 100
            }
            if (-not $lostAckObserved -or
                $proxyState.blockedTelemetryRequests -lt 2 -or
                $proxyState.committedBatch.acceptedCount -ne 3 -or
                $proxyState.committedBatch.duplicateCount -ne 0 -or
                @(Compare-Object -ReferenceObject @($outageEvents.eventId) `
                    -DifferenceObject @($proxyState.committedBatch.eventIds)).Count -ne 0) {
                throw 'The proxy did not drop one committed acknowledgement for the original three events.'
            }
            $committedSummary = Invoke-JsonRequest -Method Get `
                -Uri ("$backendHostUrl/api/security/sessions/" +
                    "$($sessionId.ToString('D'))/summary") `
                -Headers $adminHeaders -Body $null -ExpectedStatus 200
            if ($committedSummary.totalEvents -ne 3 -or
                -not (Test-Path -LiteralPath $telemetrySpoolPath) -or
                (Get-FileHash -LiteralPath $telemetrySpoolPath).Hash -ne $outageSpoolHash) {
                throw 'Lost acknowledgement did not preserve committed backend events and the unchanged UE spool.'
            }
            Copy-Item -LiteralPath $telemetrySpoolPath `
                -Destination (Join-Path $logRoot 'Telemetry.unacknowledged.jsonl')
            Invoke-JsonRequest -Method Post -Uri "$proxyControlUrl/mode" `
                -Headers $proxyControlHeaders -ExpectedStatus 200 -Body @{
                    mode = 'pass'
                    dungeonSessionId = $sessionId.ToString('D')
                } | Out-Null
            Write-Host (
                'Observed a committed batch with its HTTP acknowledgement ' +
                'disconnected, an unchanged durable spool, and 8s backoff; ' +
                'restored the HTTP path for the UE retry.')
        }
    }
    if ($TestTelemetryRestartRecovery) {
        # Stop before the two-second telemetry flush. The event append is
        # synchronous, so this models a server process loss after evidence is
        # durable but before the backend acknowledges it.
        Stop-OwnedProcess $serverProcess
        $serverProcess = $null
        Stop-OwnedProcess $clientProcess
        $clientProcess = $null

        if (-not (Test-Path `
                -LiteralPath $telemetrySpoolPath `
                -PathType Leaf)) {
            throw (
                'The security telemetry spool was not durable before the ' +
                'simulated server restart.')
        }

        # Exercise backward compatibility with spools written before the
        # JSONL encoder was changed to condensed, single-line JSON.
        $spoolRecords = @(Get-Content `
            -LiteralPath $telemetrySpoolPath |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        if ($spoolRecords.Count -ne 3) {
            throw (
                'Expected three durable telemetry JSONL records before ' +
                "restart, got $($spoolRecords.Count).")
        }
        $legacyPrettyRecords = @($spoolRecords | ForEach-Object {
            ($_ | ConvertFrom-Json) |
                ConvertTo-Json -Depth 20
        })
        $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::WriteAllText(
            $telemetrySpoolPath,
            ($legacyPrettyRecords -join [Environment]::NewLine) +
                [Environment]::NewLine,
            $utf8WithoutBom)

        $preRecoverySummary = Invoke-JsonRequest `
            -Method Get `
            -Uri (
                "$normalizedBackendUrl/api/security/sessions/" +
                "$($sessionId.ToString('D'))/summary") `
            -Headers $adminHeaders `
            -Body $null `
            -ExpectedStatus 200
        if ($preRecoverySummary.totalEvents -ne 0) {
            throw (
                'Security telemetry reached the backend before the ' +
                'restart-recovery checkpoint could be established.')
        }

        $recoveryArguments = @($serverArguments | Where-Object {
            $_ -notlike '-abslog=*'
        })
        $recoveryArguments +=
            "-abslog=$recoveryServerRequestedLog"
        $previousRecoveryEnvironment = @{}
        foreach ($name in $serverEnvironmentNames) {
            $previousRecoveryEnvironment[$name] =
                [Environment]::GetEnvironmentVariable($name, 'Process')
        }
        try {
            [Environment]::SetEnvironmentVariable(
                'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
                [string] $session.gameServerAccessToken,
                'Process')
            [Environment]::SetEnvironmentVariable(
                'PROJECT_RPG_GAME_SERVER_ID',
                $serverId,
                'Process')
            [Environment]::SetEnvironmentVariable(
                'PROJECT_RPG_DUNGEON_SESSION_ID',
                $sessionId.ToString('D'),
                'Process')
            $serverProcess = Start-Process `
                -FilePath $gamePath `
                -ArgumentList $recoveryArguments `
                -WorkingDirectory $projectRoot `
                -WindowStyle Hidden `
                -PassThru
        }
        finally {
            foreach ($name in $serverEnvironmentNames) {
                [Environment]::SetEnvironmentVariable(
                    $name,
                    $previousRecoveryEnvironment[$name],
                    'Process')
            }
        }
        Write-Host (
            'Restarted the UE server to recover durable security ' +
            'telemetry.')
    }

    $telemetryPassed = $false
    $telemetryPollAttempts = 60
    if ($TestTelemetryRestartRecovery -or $testUsesPostgres) {
        $telemetryPollAttempts = 150
    }
    foreach ($attempt in 1..$telemetryPollAttempts) {
        if ($null -ne $serverProcess) {
            $serverProcess.Refresh()
            if ($serverProcess.HasExited) {
                $activeServerLog = $serverRuntimeLog
                if ($TestTelemetryRestartRecovery) {
                    $activeServerLog = $recoveryServerRuntimeLog
                }
                throw (
                    'The UE server exited before telemetry delivery.' +
                    [Environment]::NewLine +
                    (Get-LogTail $activeServerLog))
            }
        }
        $securitySummary = Invoke-JsonRequest `
            -Method Get `
            -Uri (
                "$normalizedBackendUrl/api/security/sessions/" +
                "$($sessionId.ToString('D'))/summary") `
            -Headers $adminHeaders `
            -Body $null `
            -ExpectedStatus 200
        $invalidDamageEvents = @($securitySummary.byType |
            Where-Object { $_.key -eq 'InvalidDamage' })
        $stateChangeEvents = @($securitySummary.byType |
            Where-Object { $_.key -eq 'EnforcementStateChanged' })
        $restrictedAttemptEvents = @($securitySummary.byType |
            Where-Object { $_.key -eq 'RestrictedActionAttempt' })
        if ($invalidDamageEvents.Count -eq 1 -and
            $invalidDamageEvents[0].eventCount -ge 1 -and
            $stateChangeEvents.Count -eq 1 -and
            $stateChangeEvents[0].eventCount -ge 1 -and
            $restrictedAttemptEvents.Count -eq 1 -and
            $restrictedAttemptEvents[0].eventCount -ge 1) {
            $telemetryPassed = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $telemetryPassed) {
        throw (
            'The live server did not deliver all Restricted-state security ' +
            'telemetry events before the timeout.')
    }
    if ($TestTelemetryRestartRecovery -or $testUsesPostgres) {
        if ($securitySummary.totalEvents -ne 3) {
            throw (
                'Recovery did not preserve exactly-once backend ' +
                "effects. Expected 3 events, got " +
                "$($securitySummary.totalEvents).")
        }
        $spoolPollAttempts = 30
        if ($testUsesPostgres) {
            $spoolPollAttempts = 150
        }
        foreach ($attempt in 1..$spoolPollAttempts) {
            if (-not (Test-Path -LiteralPath $telemetrySpoolPath)) {
                break
            }
            Start-Sleep -Milliseconds 100
        }
        if (Test-Path -LiteralPath $telemetrySpoolPath) {
            throw (
                'The acknowledged security telemetry spool was not ' +
                'removed after recovery.')
        }
    }
    if ($testUsesPostgres) {
        if (Test-Path -LiteralPath ($telemetrySpoolPath -replace
                '\.jsonl$', '.deadletter.jsonl')) {
            throw 'Transient backend failures incorrectly produced a deadletter.'
        }
        if ($TestTelemetryHttpRecovery) {
            $proxyState = Invoke-JsonRequest -Method Get `
                -Uri "$proxyControlUrl/state" -Headers $proxyControlHeaders `
                -Body $null -ExpectedStatus 200
            if ($proxyState.droppedAcknowledgements -ne 1 -or
                $proxyState.deliveredBatch.acceptedCount -ne 0 -or
                $proxyState.deliveredBatch.duplicateCount -ne 3 -or
                @(Compare-Object -ReferenceObject @($outageEvents.eventId) `
                    -DifferenceObject @($proxyState.deliveredBatch.eventIds)).Count -ne 0) {
                throw 'The UE retry did not receive a duplicate-only acknowledgement for its original events.'
            }
            [IO.File]::WriteAllText(
                (Join-Path $logRoot 'Telemetry.http-proof.json'),
                ($proxyState | ConvertTo-Json -Depth 10),
                (New-Object System.Text.UTF8Encoding($false)))
        }
        $replay = Invoke-JsonRequest -Method Post `
            -Uri "$normalizedBackendUrl/api/security/events/batch" `
            -Headers $serverHeaders -Body $outageBatch -ExpectedStatus 200
        if ($replay.acceptedCount -ne 0 -or $replay.duplicateCount -ne 3) {
            throw 'Replaying the durable outage batch did not return three duplicates.'
        }
        $replaySummary = Invoke-JsonRequest -Method Get `
            -Uri ("$normalizedBackendUrl/api/security/sessions/" +
                "$($sessionId.ToString('D'))/summary") `
            -Headers $adminHeaders -Body $null -ExpectedStatus 200
        if ($replaySummary.totalEvents -ne 3) {
            throw 'Replaying the outage batch changed the backend event count.'
        }
        $serverProcess.Refresh()
        if ($serverProcess.HasExited) {
            throw 'The UE server did not remain alive through outage recovery.'
        }
        if ($TestTelemetryHttpRecovery) {
            Write-Host (
                'PASS: HTTP disconnect and lost post-commit acknowledgement ' +
                'retained three events, retried with 2s/4s/8s backoff, ' +
                'received three duplicates on the UE retry, and removed ' +
                'the acknowledged spool with exactly three backend events.')
        }
        else {
            Write-Host (
                'PASS: PostgreSQL outage recovery retained three events, ' +
                'retried with 2s/4s backoff, removed the acknowledged spool, ' +
                'and replayed as three duplicates with no extra backend effects.')
        }
    }

    $persistedItem = Invoke-JsonRequest `
        -Method Get `
        -Uri "$normalizedBackendUrl/api/items/$($itemId.ToString('D'))" `
        -Headers $playerHeaders `
        -Body $null `
        -ExpectedStatus 200
    if ($persistedItem.state.quantity -ne 2 -or
        $persistedItem.revision -ne 2 -or
        $persistedItem.lifecycleState -ne 'Active') {
        throw (
            'The backend did not persist the expected quantity 2, ' +
            'revision 2 active item.')
    }

    $persistedEquipment = Invoke-JsonRequest `
        -Method Get `
        -Uri (
            "$normalizedBackendUrl/api/items/" +
            "$($equipmentItemId.ToString('D'))") `
        -Headers $playerHeaders `
        -Body $null `
        -ExpectedStatus 200
    if ($persistedEquipment.definitionName -ne
            'GameItem.Equipment.Helm.Default' -or
        $persistedEquipment.state.quantity -ne 1 -or
        $persistedEquipment.revision -ne 3 -or
        $persistedEquipment.lifecycleState -ne 'Active' -or
        $persistedEquipment.location.containerType -ne 'Inventory' -or
        $persistedEquipment.location.slotIndex -ne 1) {
        throw (
            'The backend did not persist the expected unequipped ' +
            'helm at inventory slot 1, revision 3.')
    }

    $settlement = Invoke-JsonRequest `
        -Method Post `
        -Uri (
            "$normalizedBackendUrl/api/dungeon-sessions/" +
            "$($sessionId.ToString('D'))/settle-rewards") `
        -Headers $serverHeaders `
        -Body @{
            serverId = $serverId
            rewardVersion = "item_e2e_$runHex"
            changes = @()
        } `
        -ExpectedStatus @(200, 202)
    if ($settlement.rewardVersion -ne "item_e2e_$runHex") {
        throw 'The E2E session settlement was not accepted.'
    }

    foreach ($attempt in 1..50) {
        $finishedSession = Invoke-JsonRequest `
            -Method Get `
            -Uri (
                "$normalizedBackendUrl/api/dungeon-sessions/" +
                $sessionId.ToString('D')) `
            -Headers $adminHeaders `
            -Body $null `
            -ExpectedStatus 200
        if ($finishedSession.state -eq 'Cleared') {
            $settled = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $settled) {
        throw 'The E2E dungeon session did not settle cleanly.'
    }

    Write-Host (
        'PASS: backend join-ticket admission, owner-only projection, ' +
        'live consume/equip/unequip RPCs, replicated container deltas, ' +
        'Restricted-state rejection and telemetry, and backend persistence.')
    if ($TestTelemetryRestartRecovery) {
        Write-Host (
            'PASS: durable telemetry survived a pre-ack UE server restart ' +
            'and produced exactly three backend events.')
    }
    Write-Host "Potion: $($itemId.ToString('D')) quantity 3 -> 2, revision 1 -> 2"
    Write-Host "Helm: $($equipmentItemId.ToString('D')) inventory 1 -> equipment Head -> inventory 1, revision 1 -> 3"
    Write-Host "Logs: $logRoot"
}
finally {
    if ($postgresPaused) {
        & docker unpause $postgresContainerName | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Warning 'Could not unpause the owned test container during cleanup.'
        }
    }
    if (-not $settled -and
        $null -ne $sessionId -and
        $null -ne $serverHeaders) {
        try {
            Invoke-WebRequest `
                -Method Post `
                -Uri (
                    "$backendHostUrl/api/dungeon-sessions/" +
                    "$($sessionId.ToString('D'))/finish") `
                -Headers $serverHeaders `
                -UseBasicParsing `
                -TimeoutSec 5 `
                -ContentType 'application/json' `
                -Body (@{
                    serverId = $serverId
                    outcome = 'Failed'
                } | ConvertTo-Json -Compress) | Out-Null
        }
        catch {
            Write-Warning (
                'Could not compensate the unfinished E2E session: ' +
                $_.Exception.Message)
        }
    }

    Stop-OwnedProcess $clientProcess
    Stop-OwnedProcess $serverProcess
    Stop-OwnedProcess $proxyProcess
    Stop-OwnedProcess $backendProcess
    if ($ownsPostgresContainer) {
        & docker stop --time 5 $postgresContainerName | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Warning 'Could not stop the owned PostgreSQL test container.'
        }
    }

    $logCopies = @(
        [pscustomobject] @{
            Source = $serverRuntimeLog
            Destination = $serverRequestedLog
        },
        [pscustomobject] @{
            Source = $clientRuntimeLog
            Destination = $clientRequestedLog
        },
        [pscustomobject] @{
            Source = $recoveryServerRuntimeLog
            Destination = $recoveryServerRequestedLog
        })
    foreach ($logCopy in $logCopies) {
        if (Test-Path -LiteralPath $logCopy.Source -PathType Leaf) {
            Copy-Item `
                -LiteralPath $logCopy.Source `
                -Destination $logCopy.Destination `
                -Force `
                -ErrorAction SilentlyContinue
        }
    }
}
