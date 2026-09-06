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
        '/Game/Blueprints/GameMode/BP_GameModeBase.BP_GameModeBase_C'
)

$ErrorActionPreference = 'Stop'

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

$runStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runHex = [Guid]::NewGuid().ToString('N').Substring(0, 12)
$logRoot = Join-Path `
    $projectRoot `
    "Saved\Logs\ItemBackendE2E\$runStamp"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

$serverLogName = "ItemE2E-$runStamp-Server.log"
$clientLogName = "ItemE2E-$runStamp-Client.log"
$serverRequestedLog = Join-Path $logRoot $serverLogName
$clientRequestedLog = Join-Path $logRoot $clientLogName
$runtimeLogRoot = Join-Path `
    $cookedRoot `
    "Project_RPG\Saved\Logs\ItemBackendE2E\$runStamp"
$serverRuntimeLog = Join-Path $runtimeLogRoot $serverLogName
$clientRuntimeLog = Join-Path $runtimeLogRoot $clientLogName
$backendOutputLog = Join-Path $logRoot 'Backend.stdout.log'
$backendErrorLog = Join-Path $logRoot 'Backend.stderr.log'

$backendProcess = $null
$serverProcess = $null
$clientProcess = $null
$serverHeaders = $null
$sessionId = $null
$serverId = "item-e2e-$runHex"
$settled = $false

try {
    $backendWasRunning = Test-BackendHealth $normalizedBackendUrl
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
            'PROJECT_RPG_BACKEND_ADMIN_TOKEN')
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
            $backendProcess = Start-Process `
                -FilePath 'dotnet' `
                -ArgumentList @(
                    'run',
                    '--no-restore',
                    '--no-launch-profile',
                    '--project',
                    $backendProject) `
                -WorkingDirectory (Split-Path $backendProject) `
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
            if (Test-BackendHealth $normalizedBackendUrl) {
                break
            }
            Start-Sleep -Milliseconds 200
        }
        if (-not (Test-BackendHealth $normalizedBackendUrl)) {
            throw (
                'The local backend did not become healthy.' +
                [Environment]::NewLine +
                (Get-LogTail $backendOutputLog))
        }
        Write-Host 'Started a Development/Memory backend.'
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
            '-RPGItemE2EAllowUnverifiedIdentity')
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
        "Started the UE dedicated-server runtime on " +
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
        "-RPGItemE2EConsume=$($itemId.ToString('D'))",
        "-RPGItemE2EEquip=$($equipmentItemId.ToString('D'))")
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
        'and backend persistence.')
    Write-Host "Potion: $($itemId.ToString('D')) quantity 3 -> 2, revision 1 -> 2"
    Write-Host "Helm: $($equipmentItemId.ToString('D')) inventory 1 -> equipment Head -> inventory 1, revision 1 -> 3"
    Write-Host "Logs: $logRoot"
}
finally {
    if (-not $settled -and
        $null -ne $sessionId -and
        $null -ne $serverHeaders) {
        try {
            Invoke-WebRequest `
                -Method Post `
                -Uri (
                    "$normalizedBackendUrl/api/dungeon-sessions/" +
                    "$($sessionId.ToString('D'))/finish") `
                -Headers $serverHeaders `
                -UseBasicParsing `
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
    Stop-OwnedProcess $backendProcess

    $logCopies = @(
        [pscustomobject] @{
            Source = $serverRuntimeLog
            Destination = $serverRequestedLog
        },
        [pscustomobject] @{
            Source = $clientRuntimeLog
            Destination = $clientRequestedLog
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
