[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int] $Port = 17815,

    [ValidateRange(60, 300)]
    [int] $TimeoutSeconds = 150,

    [ValidateRange(1, 4)]
    [int] $ClientCount = 1,

    [string] $BackendUrl = 'http://127.0.0.1:3015',

    [string] $ConnectionString =
        'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only',

    [switch] $SkipPostgresStart,

    [switch] $NoBackendRestore,

    [switch] $ClaimMail,

    [switch] $ConcurrentClaims,

    [switch] $InventoryOverflow,

    [switch] $PartialStackOverflow
)

$ErrorActionPreference = 'Stop'
if ($ConcurrentClaims -and -not $ClaimMail) {
    throw '-ConcurrentClaims requires -ClaimMail.'
}
if ($InventoryOverflow -and $ClaimMail) { throw '-InventoryOverflow uses production stack limits and cannot use the legacy -ClaimMail fixture.' }
if ($PartialStackOverflow -and -not $InventoryOverflow) { throw '-PartialStackOverflow requires -InventoryOverflow.' }
$runMailClaim = $ClaimMail -or $InventoryOverflow

function Repair-DuplicatePathEnvironment {
    $environment = [Environment]::GetEnvironmentVariables('Process')
    $pathKeys = @($environment.Keys | Where-Object {
        $_.ToString() -ieq 'Path'
    })
    if ($pathKeys.Count -le 1) {
        return
    }

    $pathValue = [string] $environment['Path']
    if ([string]::IsNullOrWhiteSpace($pathValue)) {
        $pathValue = [string] $environment[$pathKeys[0]]
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

function Invoke-JsonRequest {
    param(
        [ValidateSet('Get', 'Post')]
        [string] $Method,

        [string] $Uri,

        [hashtable] $Headers = @{},

        [object] $Body
    )

    $parameters = @{
        Method = $Method
        Uri = $Uri
        Headers = $Headers
        UseBasicParsing = $true
        TimeoutSec = 10
    }
    if ($null -ne $Body) {
        $parameters.ContentType = 'application/json'
        $parameters.Body = $Body | ConvertTo-Json -Depth 20 -Compress
    }

    try {
        $response = Invoke-WebRequest @parameters
    }
    catch {
        $safeUri = ([Uri] $Uri).GetLeftPart([UriPartial]::Path)
        $_.ErrorDetails = [System.Management.Automation.ErrorDetails]::new(
            "E2E HTTP $Method $safeUri failed: $($_.Exception.Message)")
        throw
    }
    if ([string]::IsNullOrWhiteSpace([string] $response.Content)) {
        return $null
    }
    return $response.Content | ConvertFrom-Json
}

function Test-LogMarker {
    param([string] $Path, [string] $Marker)

    return (Test-Path -LiteralPath $Path -PathType Leaf) -and
        [bool] (Select-String `
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
    $tail = (Get-Content `
        -LiteralPath $Path `
        -Tail 80 `
        -ErrorAction SilentlyContinue) -join [Environment]::NewLine
    return $tail -replace '(?i)(JoinTicket=)[^\s"?]+', '$1<redacted>'
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
            $Process.WaitForExit(10000) | Out-Null
        }
    }
    catch {
        Write-Warning (
            "Could not stop owned process $($Process.Id): " +
            $_.Exception.Message)
    }
}

function Wait-BackendReady {
    param([object] $Handle)

    $deadline = [DateTimeOffset]::UtcNow.AddSeconds(30)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $Handle.Process.Refresh()
        if ($Handle.Process.HasExited) {
            throw (
                'Backend exited before readiness.' +
                [Environment]::NewLine +
                (Get-LogTail $Handle.Stdout) +
                [Environment]::NewLine +
                (Get-LogTail $Handle.Stderr))
        }
        try {
            $health = Invoke-RestMethod `
                -Method Get `
                -Uri "$script:normalizedBackendUrl/health/ready" `
                -TimeoutSec 2
            if ($health.status -eq 'ready' -and
                $health.storageProvider -eq 'Postgres') {
                return
            }
        }
        catch {
            Start-Sleep -Milliseconds 250
        }
    }
    throw (
        'PostgreSQL backend did not become ready.' +
        [Environment]::NewLine +
        (Get-LogTail $Handle.Stdout) +
        [Environment]::NewLine +
        (Get-LogTail $Handle.Stderr))
}

function Start-Backend {
    $script:backendGeneration++
    $stdout = Join-Path `
        $script:logRoot `
        "backend-$($script:backendGeneration).stdout.log"
    $stderr = Join-Path `
        $script:logRoot `
        "backend-$($script:backendGeneration).stderr.log"
    $process = Start-Process `
        -FilePath $script:dotnet `
        -ArgumentList @("`"$script:backendDll`"",
            '--DungeonContent:Entries:1:DungeonId=Dungeon.RewardE2E',
            '--DungeonContent:Entries:1:MapPackage=/Engine/Maps/Entry',
            '--DungeonContent:Entries:1:Difficulties:0=Hard') `
        -WorkingDirectory $script:backendBuildRoot `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -WindowStyle Hidden `
        -PassThru
    $handle = [pscustomobject]@{
        Process = $process
        Stdout = $stdout
        Stderr = $stderr
    }
    try {
        Wait-BackendReady -Handle $handle
    }
    catch {
        Stop-OwnedProcess -Process $process
        throw
    }
    return $handle
}

function New-PlayerAuthentication {
    param([string] $SteamId)

    return Invoke-JsonRequest `
        -Method Post `
        -Uri "$script:normalizedBackendUrl/api/auth/steam-ticket" `
        -Body @{ ticket = "dev:$SteamId" }
}

function New-ActivatedSession {
    param(
        [object[]] $Members,
        [string] $ServerId,
        [int] $ServerPort
    )

    $session = Invoke-JsonRequest `
        -Method Post `
        -Uri "$script:normalizedBackendUrl/api/dungeon-sessions" `
        -Headers $Members[0].Headers `
        -Body @{
            characterId = $Members[0].CharacterId
            dungeonId = 'Dungeon.RewardE2E'
            difficulty = 'Hard'
        }
    foreach ($member in @($Members | Select-Object -Skip 1)) {
        $session = Invoke-JsonRequest -Method Post `
            -Uri "$script:normalizedBackendUrl/api/dungeon-sessions/$($session.dungeonSessionId)/members" `
            -Headers $member.Headers -Body @{ characterId = $member.CharacterId }
    }
    if (@($session.members).Count -ne $Members.Count) {
        throw 'Backend dungeon membership does not match the expected party.'
    }
    foreach ($member in $Members) {
        if (-not @($session.members | Where-Object {
            [Guid] $_.characterId -eq $member.CharacterId
        })) { throw 'An expected character is missing from the backend party.' }
    }
    . (Join-Path $PSScriptRoot '../../Backend/Tools/Request-DungeonDeparture.ps1')
    $null = Request-RPGDungeonDeparture $script:normalizedBackendUrl $session.dungeonSessionId $Members[0].CharacterId $Members[0].Headers
    return Invoke-JsonRequest `
        -Method Post `
        -Uri (
            "$script:normalizedBackendUrl/api/dungeon-sessions/" +
            "$($session.dungeonSessionId)/activate") `
        -Headers $script:adminHeaders `
        -Body @{
            serverId = $ServerId
            serverAddress = "127.0.0.1:$ServerPort"
        }
}

function Start-DedicatedFixture {
    param(
        [object] $Session,
        [string] $ServerId,
        [string] $LogPath,
        [switch] $Recover,
        [switch] $Claim,
        [switch] $Claimed,
        [object[]] $Members
    )

    $serverEnvironmentNames = @(
        'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
        'PROJECT_RPG_GAME_SERVER_ID',
        'PROJECT_RPG_DUNGEON_SESSION_ID')
    $previous = @{}
    foreach ($name in $serverEnvironmentNames) {
        $previous[$name] = [Environment]::GetEnvironmentVariable(
            $name,
            'Process')
    }
    try {
        [Environment]::SetEnvironmentVariable(
            'PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN',
            [string] $Session.gameServerAccessToken,
            'Process')
        [Environment]::SetEnvironmentVariable(
            'PROJECT_RPG_GAME_SERVER_ID',
            $ServerId,
            'Process')
        [Environment]::SetEnvironmentVariable(
            'PROJECT_RPG_DUNGEON_SESSION_ID',
            [string] $Session.dungeonSessionId,
            'Process')

        $arguments = @(
            "`"$script:projectPath`"",
            '/Engine/Maps/Entry?game=/Script/Project_RPG.RPGDungeonRewardE2EGameMode',
            '-server',
            "-port=$Port",
            '-unattended',
            '-nop4',
            '-nosplash',
            '-NoSound',
            '-NullRHI',
            '-multiprocess',
            '-nosteam',
            '-NoLiveCoding',
            '-UseIrisReplication=1',
            '-RPGDungeonRewardE2E',
            "-RPGRewardE2EPlayers=$ClientCount",
            '-DisablePython',
            '-ExecCmds="t.MaxFPS 30"',
            '-DDC-ForceMemoryCache',
            "-abslog=`"$LogPath`"",
            "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$script:apiUrl",
            "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$script:apiUrl",
            "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$script:apiUrl")
        if ($Recover) {
            $expectedRewards = @($Members | ForEach-Object {
                '{0},{1},{2}' -f $_.CharacterId.ToString('D'), $_.PotionId.ToString('D'), $_.HelmId.ToString('D')
            }) -join ';'
            $arguments += @(
                '-RPGRewardE2ERecover',
                "-RPGRewardE2ERewards=$expectedRewards")
        }
        if ($ClaimMail) { $arguments += '-RPGRewardE2ELegacyMail' }
        if ($InventoryOverflow) { $arguments += '-RPGRewardE2EOverflow' }
        if ($PartialStackOverflow) { $arguments += '-RPGRewardE2EPartialStackOverflow' }
        if ($Claim) { $arguments += '-RPGRewardE2EClaimMail' }
        if ($Claimed) { $arguments += '-RPGRewardE2EClaimed' }
        return Start-Process `
            -FilePath $script:unrealEditorCmd `
            -ArgumentList $arguments `
            -WorkingDirectory $script:projectRoot `
            -WindowStyle Hidden `
            -PassThru
    }
    finally {
        foreach ($name in $serverEnvironmentNames) {
            [Environment]::SetEnvironmentVariable(
                $name,
                $previous[$name],
                'Process')
        }
    }
}

function Start-HeadlessClient {
    param(
        [string] $JoinTicket,
        [string] $LogPath,
        [switch] $Recover,
        [switch] $Claim,
        [switch] $ConcurrentClaims,
        [switch] $Claimed,
        [Guid] $PotionId,
        [Guid] $HelmId,
        [object] $Member
    )

    $ticket = [Uri]::EscapeDataString($JoinTicket)
    $arguments = @(
        "`"$script:projectPath`"",
        "127.0.0.1:$Port`?JoinTicket=$ticket",
        '-game',
        '-unattended',
        '-nop4',
        '-nosplash',
        '-NoSound',
        '-NullRHI',
        '-multiprocess',
        '-nosteam',
        '-NoLiveCoding',
        '-UseIrisReplication=1',
        '-RPGDungeonRewardE2E',
        '-DisablePython',
        '-ExecCmds="t.MaxFPS 30"',
        '-DDC-ForceMemoryCache',
        "-abslog=`"$LogPath`"",
        "-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=$script:apiUrl",
        "-ini:Game:[/Script/Project_RPG.RPGItemBackendSubsystem]:ApiUrl=$script:apiUrl",
        "-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=$script:apiUrl")
    if ($Recover) {
        $arguments += @(
            '-RPGRewardE2ERecover',
            "-RPGRewardE2EPotion=$($PotionId.ToString('D'))",
            "-RPGRewardE2EHelm=$($HelmId.ToString('D'))")
    }
    if ($ClaimMail) { $arguments += '-RPGRewardE2ELegacyMail' }
    if ($InventoryOverflow) {
        $arguments += @('-RPGRewardE2EOverflow',
            "-RPGRewardE2EConsumeA=$($Member.ConsumeA.ToString('D'))",
            "-RPGRewardE2EConsumeB=$($Member.ConsumeB.ToString('D'))",
            "-RPGRewardE2EStack=$($Member.SeedIds[0].ToString('D'))")
    }
    if ($PartialStackOverflow) { $arguments += '-RPGRewardE2EPartialStackOverflow' }
    if ($Claim) { $arguments += '-RPGRewardE2EClaimMail' }
    if ($ConcurrentClaims) { $arguments += '-RPGRewardE2EConcurrentClaims' }
    if ($Claimed) { $arguments += '-RPGRewardE2EClaimed' }
    return Start-Process `
        -FilePath $script:unrealEditorCmd `
        -ArgumentList $arguments `
        -WorkingDirectory $script:projectRoot `
        -WindowStyle Hidden `
        -PassThru
}

function Wait-SessionInProgress {
    param(
        [object] $ServerProcess,
        [Guid] $SessionId,
        [string] $ServerLog
    )

    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $ServerProcess.Refresh()
        if ($ServerProcess.HasExited) {
            throw (
                'Dedicated fixture exited before session start.' +
                [Environment]::NewLine +
                (Get-LogTail $ServerLog))
        }
        if (Test-LogMarker $ServerLog 'RPG_REWARD_E2E FAIL') {
            throw (
                'Dedicated fixture reported failure.' +
                [Environment]::NewLine +
                (Get-LogTail $ServerLog))
        }
        try {
            $session = Invoke-JsonRequest `
                -Method Get `
                -Uri (
                    "$script:normalizedBackendUrl/api/dungeon-sessions/" +
                    $SessionId.ToString('D')) `
                -Headers $script:adminHeaders
            if ($session.state -eq 'InProgress' -and
                (Test-LogMarker `
                    $ServerLog `
                    'RPG_REWARD_E2E DEDICATED_READY') -and
                (Test-LogMarker $ServerLog 'RPG_REWARD_E2E SERVER_TICK_READY Iris=true')) {
                return
            }
        }
        catch {
            # Backend state and the log marker can become visible on different ticks.
        }
        Start-Sleep -Milliseconds 250
    }
    throw (
        'Dedicated fixture did not enter InProgress.' +
        [Environment]::NewLine +
        (Get-LogTail $ServerLog))
}

function New-JoinTicket {
    param(
        [hashtable] $PlayerHeaders,
        [Guid] $CharacterId,
        [Guid] $SessionId
    )

    return Invoke-JsonRequest `
        -Method Post `
        -Uri "$script:normalizedBackendUrl/api/join-tickets" `
        -Headers $PlayerHeaders `
        -Body @{
            characterId = $CharacterId
            dungeonSessionId = $SessionId
        }
}

function Wait-PhaseMarkers {
    param(
        [object] $ServerProcess,
        [string] $ServerLog,
        [string] $ServerPassMarker,
        [object[]] $Clients
    )

    if ($Clients.Count -ne $ClientCount) {
        throw 'The client process count does not match the expected party.'
    }

    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $ServerProcess.Refresh()
        if (Test-LogMarker $ServerLog 'RPG_REWARD_E2E FAIL') {
            throw (
                'Server reward probe failed.' +
                [Environment]::NewLine +
                (Get-LogTail $ServerLog))
        }
        if ($ServerProcess.HasExited) {
            throw (
                'The UE server exited before the reward probe passed.' +
                [Environment]::NewLine +
                (Get-LogTail $ServerLog))
        }
        $allClientsPassed = $true
        foreach ($clientHandle in $Clients) {
            $clientHandle.Process.Refresh()
            if (Test-LogMarker $clientHandle.Log 'RPG_REWARD_E2E FAIL') {
                throw ('Client reward probe failed.' + [Environment]::NewLine + (Get-LogTail $clientHandle.Log))
            }
            if ($clientHandle.Process.HasExited) {
                throw ('A UE client exited before the reward probe passed.' + [Environment]::NewLine + (Get-LogTail $clientHandle.Log))
            }
            if (-not (Test-LogMarker $clientHandle.Log $clientHandle.PassMarker)) {
                $allClientsPassed = $false
            }
        }
        if ($allClientsPassed -and (Test-LogMarker $ServerLog $ServerPassMarker)) { return }
        Start-Sleep -Milliseconds 250
    }
    throw (
        'Timed out waiting for the reward probe.' +
        [Environment]::NewLine +
        (Get-LogTail $ServerLog) +
        [Environment]::NewLine +
        (@($Clients | ForEach-Object { Get-LogTail $_.Log }) -join [Environment]::NewLine))
}

function Start-PartyClients {
    param(
        [object[]] $Members,
        [Guid] $SessionId,
        [string] $Phase,
        [switch] $Recover,
        [switch] $Claim,
        [switch] $Claimed
    )
    foreach ($member in $Members) {
        $ticket = New-JoinTicket -PlayerHeaders $member.Headers `
            -CharacterId $member.CharacterId -SessionId $SessionId
        $suffix = if ($Members.Count -eq 1) { '' } else { "-$($member.Index)" }
        $clientLog = Join-Path $script:logRoot "client-$Phase$suffix.log"
        $process = Start-HeadlessClient -JoinTicket ([string] $ticket.joinTicket) `
            -LogPath $clientLog -Recover:$Recover -Claim:$Claim -Claimed:$Claimed `
            -ConcurrentClaims:($Claim -and $ConcurrentClaims) `
            -PotionId $member.PotionId -HelmId $member.HelmId -Member $member
        $script:ownedClients.Add($process)
        $marker = 'RPG_REWARD_E2E CLIENT_PASS Gold=0'
        if ($Recover) { $marker = 'RPG_REWARD_E2E CLIENT_PASS Gold=500' }
        if ($Claim) { $marker = 'RPG_REWARD_E2E CLAIM_PASS Mail=0 Inventory=2 Gold=500' }
        if ($Claim -and $ConcurrentClaims) { $marker = 'RPG_REWARD_E2E CONCURRENT_CLAIM_PASS Mail=0 Inventory=2 Gold=500' }
        if ($InventoryOverflow -and $Claim) { $marker = 'RPG_REWARD_E2E OVERFLOW_CLAIM_PASS Mail=0 Inventory=80 Gold=500' }
        if ($Claimed) { $marker = 'RPG_REWARD_E2E CLAIM_RESTORE_CLIENT_PASS Gold=500' }
        [pscustomobject]@{
            Process = $process
            Log = $clientLog
            PassMarker = "$marker Character=$($member.CharacterId.ToString('D'))"
        }
    }
}

function Stop-PartyClients {
    foreach ($process in $script:ownedClients) { Stop-OwnedProcess -Process $process }
    $script:ownedClients.Clear()
}

function Wait-SessionCleared {
    param([Guid] $SessionId)

    $deadline = [DateTimeOffset]::UtcNow.AddSeconds(30)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $session = Invoke-JsonRequest `
            -Method Get `
            -Uri (
                "$script:normalizedBackendUrl/api/dungeon-sessions/" +
                $SessionId.ToString('D')) `
            -Headers $script:adminHeaders
        if ($session.state -eq 'Cleared') {
            return $session
        }
        if ($session.state -eq 'Failed') {
            throw 'Dungeon reward settlement finished as Failed.'
        }
        Start-Sleep -Milliseconds 250
    }
    throw 'Dungeon reward settlement did not reach Cleared.'
}

function Get-RewardState {
    param(
        [hashtable] $PlayerHeaders,
        [Guid] $CharacterId,
        [Guid] $RewardSessionId,
        [switch] $Claimed,
        [object] $Member
    )

    if ($InventoryOverflow) { return Get-OverflowRewardState -Member $Member -RewardSessionId $RewardSessionId -Claimed:$Claimed }

    $wallet = Invoke-JsonRequest `
        -Method Get `
        -Uri (
            "$script:normalizedBackendUrl/api/economy/wallets/" +
            $CharacterId.ToString('D')) `
        -Headers $PlayerHeaders
    $gold = @($wallet.balances | Where-Object {
        $_.currencyCode -eq 'RosterGold'
    })
    $items = Invoke-JsonRequest `
        -Method Get `
        -Uri (
            "$script:normalizedBackendUrl/api/items?ownerType=Character" +
            "&ownerId=$($CharacterId.ToString('D'))&includeTerminal=false&limit=500") `
        -Headers $PlayerHeaders
    $potions = @($items.items | Where-Object {
        $_.definitionType -eq 'RPGItemDefinition' -and
        $_.definitionName -eq 'GameItem.Consume.Potion.Red.Large'
    })
    $helms = @($items.items | Where-Object {
        $_.definitionType -eq 'RPGItemDefinition' -and
        $_.definitionName -eq 'GameItem.Equipment.Helm.Default'
    })
    $expectedContainer = "DungeonReward.$($RewardSessionId.ToString('N'))"
    $expectedType = 'Mail'
    $expectedRevision = 1
    if (-not $ClaimMail) {
        $expectedContainer = $CharacterId.ToString('D')
        $expectedType = 'Inventory'
    }
    if ($Claimed) {
        $expectedContainer = $CharacterId.ToString('D')
        $expectedType = 'Inventory'
        $expectedRevision = 2
    }
    if ($gold.Count -ne 1 -or $gold[0].balance -ne 500 -or
        $potions.Count -ne 1 -or $potions[0].definitionVersion -ne 1 -or
        $potions[0].state.quantity -ne 3 -or
        $potions[0].revision -ne $expectedRevision -or
        $potions[0].lifecycleState -ne 'Active' -or
        $potions[0].owner.type -ne 'Character' -or
        [Guid] $potions[0].owner.ownerId -ne $CharacterId -or
        $potions[0].location.containerType -ne $expectedType -or
        $potions[0].location.containerId -ne $expectedContainer -or
        $potions[0].location.slotIndex -ne 0 -or
        $helms.Count -ne 1 -or $helms[0].definitionVersion -ne 1 -or
        $helms[0].state.quantity -ne 1 -or
        $helms[0].revision -ne $expectedRevision -or
        $helms[0].lifecycleState -ne 'Active' -or
        $helms[0].owner.type -ne 'Character' -or
        [Guid] $helms[0].owner.ownerId -ne $CharacterId -or
        $helms[0].location.containerType -ne $expectedType -or
        $helms[0].location.containerId -ne $expectedContainer -or
        $helms[0].location.slotIndex -ne 1 -or
        @($items.items).Count -ne 2) {
        throw "Backend reward state does not match one Hard reward for $CharacterId."
    }
    return [pscustomobject]@{
        Gold = [long] $gold[0].balance
        Potion = $potions[0]
        Helm = $helms[0]
    }
}

function Assert-PartyRewardState {
    param([object[]] $Members, [Guid] $RewardSessionId, [switch] $Claimed)

    $identities = [System.Collections.Generic.HashSet[Guid]]::new()
    foreach ($member in $Members) {
        $state = Get-RewardState -PlayerHeaders $member.Headers `
            -CharacterId $member.CharacterId -RewardSessionId $RewardSessionId -Claimed:$Claimed -Member $member
        $potionId = [Guid] $state.Potion.state.instanceId
        $helmId = [Guid] $state.Helm.state.instanceId
        if ($member.PotionId -eq [Guid]::Empty -and $member.HelmId -eq [Guid]::Empty) {
            $member.PotionId = $potionId
            $member.HelmId = $helmId
        }
        if ($member.PotionId -ne $potionId -or $member.HelmId -ne $helmId) {
            throw "Reward identity changed for $($member.CharacterId)."
        }
        if (-not $identities.Add($potionId) -or -not $identities.Add($helmId)) {
            throw 'Reward item identities were reused across party members.'
        }
    }
}

function Initialize-OverflowInventory {
    param([object[]] $Members, [object] $Session)

    $serverHeaders = @{ Authorization = "Bearer $($Session.gameServerAccessToken)" }

    foreach ($member in $Members) {
        $member.SeedIds = @()
        for ($chunk = 0; $chunk -lt 5; $chunk++) {
            $mutations = @(for ($offset = 0; $offset -lt 16; $offset++) {
                $slot = $chunk * 16 + $offset
                $itemId = [Guid]::NewGuid()
                $member.SeedIds += $itemId
                $isPartialStack = $PartialStackOverflow -and $slot -eq 0
                @{
                    expectedRevision = 0
                    newRecord = @{
                        definitionType = 'RPGItemDefinition'
                        definitionName = 'GameItem.Consume.Potion.Red.Large'
                        definitionVersion = 1
                        owner = @{ type = 'Character'; ownerId = $member.CharacterId.ToString('D') }
                        location = @{ containerType = 'Inventory'; containerId = $member.CharacterId.ToString('D'); slotIndex = $slot }
                        state = @{ instanceId = $itemId; generationSeed = 42 + $slot; quantity = $(if ($isPartialStack) { 8 } else { 1 }); instanceTags = @(); statValues = @() }
                        revision = 0
                        lifecycleState = 'Active'
                        metadata = @{
                            bindState = $(if ($isPartialStack) { 'Unbound' } else { 'CharacterBound' })
                            durability = @{ current = 0; maximum = 0 }
                            expiresAtUtc = $null
                            creationSource = 'RewardOverflowE2E.Seed'
                            isLocked = $false
                        }
                    }
                }
            })
            $receipt = Invoke-JsonRequest -Method Post -Uri "$script:normalizedBackendUrl/api/item-transactions/commit" `
                -Headers $serverHeaders -Body @{
                    requestId = [Guid]::NewGuid()
                    operation = 'SeedOverflow'
                    commandFingerprint = "overflow-seed-$($member.CharacterId)-$chunk"
                    actor = @{ type = 'Character'; ownerId = $member.CharacterId.ToString('D') }
                    affectedQuantity = [int](($mutations | ForEach-Object { $_.newRecord.state.quantity } | Measure-Object -Sum).Sum)
                    mutations = $mutations
                }
            if ($receipt.status -ne 'Committed' -or @($receipt.records).Count -ne 16) { throw 'Full inventory seed did not commit.' }
        }
        $start = if ($PartialStackOverflow) { 1 } else { 0 }
        $member.ConsumeA = $member.SeedIds[$start]
        $member.ConsumeB = $member.SeedIds[$start + 1]
    }
    Write-Host "Seeded $($Members.Count) full 80-slot inventories; partial-stack mode=$PartialStackOverflow."
}

function Get-OverflowRewardState {
    param([object] $Member, [Guid] $RewardSessionId, [switch] $Claimed)

    $ownerId = $Member.CharacterId.ToString('D')
    $wallet = Invoke-JsonRequest -Method Get -Uri "$script:normalizedBackendUrl/api/economy/wallets/$ownerId" -Headers $Member.Headers
    $gold = @($wallet.balances | Where-Object { $_.currencyCode -eq 'RosterGold' })
    $response = Invoke-JsonRequest -Method Get -Uri "$script:normalizedBackendUrl/api/items?ownerType=Character&ownerId=$ownerId&includeTerminal=false&limit=500" -Headers $Member.Headers
    $all = @($response.items)
    # Player bulk reads intentionally exclude terminal records. Verify each known consumed ID
    # through its existing owner-authorized single-item endpoint without widening that permission.
    if ($Claimed) {
        foreach ($consumedId in @($Member.ConsumeA, $Member.ConsumeB)) {
            $all += Invoke-JsonRequest -Method Get -Uri "$script:normalizedBackendUrl/api/items/$consumedId" -Headers $Member.Headers
        }
    }
    $active = @($all | Where-Object { $_.lifecycleState -eq 'Active' })
    $seed = @($all | Where-Object { $_.metadata.creationSource -eq 'RewardOverflowE2E.Seed' })
    $reward = @($all | Where-Object { $_.metadata.creationSource -eq 'DungeonClear.pve_hard_v1' })
    $potions = @($reward | Where-Object { $_.definitionName -eq 'GameItem.Consume.Potion.Red.Large' })
    $helms = @($reward | Where-Object { $_.definitionName -eq 'GameItem.Equipment.Helm.Default' })
    $expectedQuantity = if ($PartialStackOverflow) { 1 } else { 3 }
    $expectedRevision = if ($Claimed) { 2 } else { 1 }
    $expectedType = if ($Claimed) { 'Inventory' } else { 'Mail' }
    $expectedContainer = if ($Claimed) { $ownerId } else { "DungeonReward.$($RewardSessionId.ToString('N'))" }
    $slotA = if ($PartialStackOverflow) { 1 } else { 0 }
    $potionSlot = if ($Claimed) { $slotA } else { 0 }
    $helmSlot = if ($Claimed) { $slotA + 1 } else { 1 }
    if ($gold.Count -ne 1 -or $gold[0].balance -ne 500 -or $all.Count -ne 82 -or $seed.Count -ne 80 -or
        $reward.Count -ne 2 -or $potions.Count -ne 1 -or $helms.Count -ne 1 -or
        @($active | Where-Object { $_.location.containerType -eq 'Inventory' }).Count -ne 80 -or
        @($active | Where-Object { $_.location.containerType -eq 'Mail' }).Count -ne $(if ($Claimed) { 0 } else { 2 })) {
        throw "Overflow reward counts/gold do not match for $ownerId."
    }
    foreach ($item in $all) {
        if ($item.owner.type -ne 'Character' -or [Guid]$item.owner.ownerId -ne $Member.CharacterId -or
            $item.definitionType -ne 'RPGItemDefinition' -or $item.definitionVersion -ne 1) { throw 'Overflow ownership or definition changed.' }
    }
    for ($slot = 0; $slot -lt 80; $slot++) {
        $id = $Member.SeedIds[$slot]
        $record = @($seed | Where-Object { [Guid]$_.state.instanceId -eq $id })
        if ($record.Count -ne 1) { throw 'Seeded item identity changed.' }
        $record = $record[0]
        $consumed = $Claimed -and ($id -eq $Member.ConsumeA -or $id -eq $Member.ConsumeB)
        $partial = $PartialStackOverflow -and $slot -eq 0
        if ($record.definitionName -ne 'GameItem.Consume.Potion.Red.Large' -or
            $record.metadata.bindState -ne $(if ($partial) { 'Unbound' } else { 'CharacterBound' }) -or
            $record.state.generationSeed -ne (42 + $slot) -or
            $record.state.quantity -ne $(if ($consumed) { 0 } elseif ($partial) { 10 } else { 1 }) -or
            $record.revision -ne $(if ($consumed -or $partial) { 2 } else { 1 }) -or
            $record.lifecycleState -ne $(if ($consumed) { 'Consumed' } else { 'Active' }) -or
            $record.location.containerType -ne $(if ($consumed) { 'Terminal' } else { 'Inventory' }) -or
            $record.location.containerId -ne $(if ($consumed) { '' } else { $ownerId }) -or
            $record.location.slotIndex -ne $(if ($consumed) { -1 } else { $slot })) {
            throw "Seed record was incorrectly merged, removed or consumed: $id."
        }
    }
    foreach ($item in $reward) {
        $potion = $item.definitionName -eq 'GameItem.Consume.Potion.Red.Large'
        if ($item.lifecycleState -ne 'Active' -or $item.revision -ne $expectedRevision -or
            $item.state.quantity -ne $(if ($potion) { $expectedQuantity } else { 1 }) -or
            $item.location.containerType -ne $expectedType -or $item.location.containerId -ne $expectedContainer -or
            $item.location.slotIndex -ne $(if ($potion) { $potionSlot } else { $helmSlot })) {
            throw 'Overflow reward quantity, revision or delivery location changed.'
        }
    }
    $total = [int](($all | ForEach-Object { $_.state.quantity } | Measure-Object -Sum).Sum)
    if ($total -ne ((80 + $(if ($PartialStackOverflow) { 7 } else { 0 })) + 4 - $(if ($Claimed) { 2 } else { 0 }))) {
        throw 'Overflow item quantities were not conserved across grant, consumption and claim.'
    }
    return [pscustomobject]@{ Gold = [long]$gold[0].balance; Potion = $potions[0]; Helm = $helms[0] }
}

function Restore-PartyAuthentication {
    param([object[]] $Members)

    foreach ($member in $Members) {
        $auth = New-PlayerAuthentication -SteamId $member.SteamId
        $member.Headers = @{ Authorization = "Bearer $($auth.accessToken)" }
        $characters = @(Invoke-JsonRequest -Method Get `
            -Uri "$script:normalizedBackendUrl/api/characters" -Headers $member.Headers)
        if (-not @($characters | Where-Object { [Guid] $_.characterId -eq $member.CharacterId })) {
            throw "Character $($member.CharacterId) was not restored after backend restart."
        }
    }
}

function Assert-ForbiddenRead {
    param([string] $Uri, [hashtable] $Headers)

    $status = 200
    try { Invoke-JsonRequest -Method Get -Uri $Uri -Headers $Headers | Out-Null }
    catch {
        if ($null -eq $_.Exception.Response) { throw }
        $status = [int] $_.Exception.Response.StatusCode
    }
    if ($status -ne 403) { throw "Another party member's reward read returned $status instead of 403." }
}

function Assert-PartyOwnerIsolation {
    param([object[]] $Members)

    if ($Members.Count -lt 2) { return }
    for ($index = 0; $index -lt $Members.Count; $index++) {
        $otherId = $Members[($index + 1) % $Members.Count].CharacterId.ToString('D')
        Assert-ForbiddenRead -Headers $Members[$index].Headers `
            -Uri "$script:normalizedBackendUrl/api/economy/wallets/$otherId"
        Assert-ForbiddenRead -Headers $Members[$index].Headers `
            -Uri "$script:normalizedBackendUrl/api/items?ownerType=Character&ownerId=$otherId&includeTerminal=false&limit=500"
    }
}

function Close-SessionAsFailed {
    param(
        [Guid] $SessionId,
        [string] $ServerId
    )

    try {
        $current = Invoke-JsonRequest `
            -Method Get `
            -Uri (
                "$script:normalizedBackendUrl/api/dungeon-sessions/" +
                $SessionId.ToString('D')) `
            -Headers $script:adminHeaders
        if ($current.state -notin @('Cleared', 'Failed')) {
            Invoke-JsonRequest `
                -Method Post `
                -Uri (
                    "$script:normalizedBackendUrl/api/dungeon-sessions/" +
                    "$($SessionId.ToString('D'))/finish") `
                -Headers $script:adminHeaders `
                -Body @{
                    serverId = $ServerId
                    outcome = 'Failed'
                } | Out-Null
        }
    }
    catch {
        Write-Warning "Could not close E2E session $SessionId as Failed."
    }
}

Repair-DuplicatePathEnvironment

if ($BackendUrl -notmatch '^http://(127\.0\.0\.1|localhost):\d{1,5}/?$') {
    throw 'BackendUrl must be an unused localhost HTTP endpoint.'
}
$script:normalizedBackendUrl = $BackendUrl.TrimEnd('/')
$backendUri = [Uri] $script:normalizedBackendUrl
if ($backendUri.Port -eq $Port) {
    throw 'The backend and UE server must use different ports.'
}
$networkProperties = [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
if (@($networkProperties.GetActiveTcpListeners() | Where-Object {
    $_.Port -eq $backendUri.Port
}).Count -gt 0) {
    throw "Backend port $($backendUri.Port) is already in use."
}
if (@($networkProperties.GetActiveUdpListeners() | Where-Object {
    $_.Port -eq $Port
}).Count -gt 0) {
    throw "UE server port $Port is already in use."
}
$script:apiUrl = "$script:normalizedBackendUrl/api"
$script:projectRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
$script:projectPath = Join-Path $script:projectRoot 'Project_RPG.uproject'
$script:unrealEditorCmd =
    'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$backendProject = Join-Path `
    $script:projectRoot `
    'Backend\ProjectRpg.Backend\ProjectRpg.Backend.csproj'
$composeFile = Join-Path `
    $script:projectRoot `
    'Backend\compose.postgres.yml'
$seedScript = Join-Path `
    $script:projectRoot `
    'Backend\seed-game-economy.ps1'
$runHex = [Guid]::NewGuid().ToString('N').Substring(0, 12)
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:logRoot = Join-Path `
    $script:projectRoot `
    "Saved\Logs\DungeonRewardE2E\$timestamp-$runHex"
$script:backendBuildRoot = Join-Path $script:logRoot 'backend-build'
$script:backendDll = Join-Path `
    $script:backendBuildRoot `
    'ProjectRpg.Backend.dll'
$script:dotnet = (Get-Command dotnet -ErrorAction Stop).Source
$adminToken = "dungeon-reward-e2e-$([Guid]::NewGuid().ToString('N'))"
$script:adminHeaders = @{ Authorization = "Bearer $adminToken" }
$script:backendGeneration = 0

foreach ($requiredPath in @(
    $script:projectPath,
    $script:unrealEditorCmd,
    $backendProject,
    $composeFile,
    $seedScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required path was not found: $requiredPath"
    }
}

$environmentNames = @(
    'ASPNETCORE_ENVIRONMENT',
    'PROJECT_RPG_BACKEND_ADMIN_TOKEN',
    'POSTGRES_CONNECTION_STRING',
    'Kestrel__Endpoints__Http__Url')
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable(
        $name,
        'Process')
}

$backend = $null
$server = $null
$script:ownedClients = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$members = @()
$firstSessionId = [Guid]::Empty
$recoverySessionId = [Guid]::Empty
$claimedSessionId = [Guid]::Empty
$firstServerId = "reward-e2e-a-$runHex"
$recoveryServerId = "reward-e2e-b-$runHex"
$claimedServerId = "reward-e2e-c-$runHex"
$succeeded = $false

try {
    New-Item `
        -ItemType Directory `
        -Path $script:backendBuildRoot `
        -Force | Out-Null

    if (-not $SkipPostgresStart) {
        $docker = (Get-Command docker -ErrorAction Stop).Source
        & $docker compose -f $composeFile up -d --wait
        if ($LASTEXITCODE -ne 0) {
            throw "PostgreSQL compose startup failed with $LASTEXITCODE."
        }
    }

    $buildArguments = @(
        'build',
        $backendProject,
        '--output',
        $script:backendBuildRoot)
    if ($NoBackendRestore) {
        $buildArguments += '--no-restore'
    }
    & $script:dotnet @buildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Backend build failed with $LASTEXITCODE."
    }

    [Environment]::SetEnvironmentVariable(
        'ASPNETCORE_ENVIRONMENT', 'Development', 'Process')
    [Environment]::SetEnvironmentVariable(
        'PROJECT_RPG_BACKEND_ADMIN_TOKEN', $adminToken, 'Process')
    [Environment]::SetEnvironmentVariable(
        'POSTGRES_CONNECTION_STRING', $ConnectionString, 'Process')
    [Environment]::SetEnvironmentVariable(
        'Kestrel__Endpoints__Http__Url',
        $script:normalizedBackendUrl,
        'Process')

    $backend = Start-Backend
    Write-Host 'PostgreSQL backend is ready; starting the dedicated settlement phase.'
    & $seedScript `
        -BaseUrl $script:normalizedBackendUrl `
        -AdminToken $adminToken | Out-Null

    $runNumber = [Convert]::ToUInt32($runHex.Substring(0, 8), 16)
    $members = @(for ($index = 1; $index -le $ClientCount; $index++) {
        $steamId = ([uint64] 76561300000000000 + [uint64] $runNumber * 4 + [uint64] $index).ToString()
        $auth = New-PlayerAuthentication -SteamId $steamId
        $headers = @{ Authorization = "Bearer $($auth.accessToken)" }
        $character = Invoke-JsonRequest -Method Post `
            -Uri "$script:normalizedBackendUrl/api/characters" -Headers $headers `
            -Body @{ name = "RewardE2E$($runHex.Substring(0, 8))$index" }
        [pscustomobject]@{
            Index = $index
            SteamId = $steamId
            Headers = $headers
            CharacterId = [Guid] $character.characterId
            PotionId = [Guid]::Empty
            HelmId = [Guid]::Empty
            SeedIds = @()
            ConsumeA = [Guid]::Empty
            ConsumeB = [Guid]::Empty
        }
    })
    $firstSession = New-ActivatedSession `
        -Members $members `
        -ServerId $firstServerId `
        -ServerPort $Port
    $firstSessionId = [Guid] $firstSession.dungeonSessionId
    if ($InventoryOverflow) { Initialize-OverflowInventory -Members $members -Session $firstSession }
    $firstServerLog = Join-Path $script:logRoot 'server-settle.log'
    $server = Start-DedicatedFixture `
        -Session $firstSession `
        -ServerId $firstServerId `
        -LogPath $firstServerLog `
        -Members $members
    Wait-SessionInProgress `
        -ServerProcess $server `
        -SessionId $firstSessionId `
        -ServerLog $firstServerLog
    $clients = @(Start-PartyClients -Members $members -SessionId $firstSessionId -Phase 'settle')
    Wait-PhaseMarkers `
        -ServerProcess $server `
        -ServerLog $firstServerLog `
        -ServerPassMarker "RPG_REWARD_E2E CLEAR_SUBMITTED Twice=true Iris=true Players=$ClientCount" `
        -Clients $clients
    Wait-SessionCleared -SessionId $firstSessionId | Out-Null
    Assert-PartyRewardState -Members $members -RewardSessionId $firstSessionId
    Assert-PartyOwnerIsolation -Members $members
    Write-Host "Dedicated settlement passed: one Hard reward per member ($ClientCount players) after two clear calls."

    for ($index = 0; $index -lt $clients.Count; $index++) {
        $clients[$index].PassMarker = 'RPG_REWARD_E2E LIVE_REFRESH_CLIENT_PASS Gold=500 Character=' +
            $members[$index].CharacterId.ToString('D')
    }
    Wait-PhaseMarkers -ServerProcess $server -ServerLog $firstServerLog `
        -ServerPassMarker 'RPG_REWARD_REFRESH_APPLIED' -Clients $clients
    Write-Host 'Live reward refresh passed: already-open owner UI and wallet updated without reconnecting.'

    Stop-PartyClients
    Stop-OwnedProcess -Process $server
    $server = $null
    Stop-OwnedProcess -Process $backend.Process
    $backend = $null

    $backend = Start-Backend
    Write-Host 'Backend restarted; checking persisted rewards and a fresh UE connection.'
    Restore-PartyAuthentication -Members $members
    Assert-PartyRewardState -Members $members -RewardSessionId $firstSessionId
    Assert-PartyOwnerIsolation -Members $members
    $persistedSession = Invoke-JsonRequest `
        -Method Get `
        -Uri (
            "$script:normalizedBackendUrl/api/dungeon-sessions/" +
            $firstSessionId.ToString('D')) `
        -Headers $script:adminHeaders
    if ($persistedSession.state -ne 'Cleared' -or @($persistedSession.members).Count -ne $ClientCount) {
        throw 'Cleared session state was not restored after backend restart.'
    }
    foreach ($member in $members) {
        if (-not @($persistedSession.members | Where-Object {
            [Guid] $_.characterId -eq $member.CharacterId
        })) { throw 'Persisted dungeon membership changed after backend restart.' }
    }

    $recoverySession = New-ActivatedSession `
        -Members $members `
        -ServerId $recoveryServerId `
        -ServerPort $Port
    $recoverySessionId = [Guid] $recoverySession.dungeonSessionId
    $recoveryServerLog = Join-Path $script:logRoot 'server-recover.log'
    $server = Start-DedicatedFixture `
        -Session $recoverySession `
        -ServerId $recoveryServerId `
        -LogPath $recoveryServerLog `
        -Recover `
        -Claim:$runMailClaim `
        -Members $members
    Wait-SessionInProgress `
        -ServerProcess $server `
        -SessionId $recoverySessionId `
        -ServerLog $recoveryServerLog
    $clients = @(Start-PartyClients -Members $members -SessionId $recoverySessionId `
        -Phase 'recover' -Recover -Claim:$runMailClaim)
    Wait-PhaseMarkers `
        -ServerProcess $server `
        -ServerLog $recoveryServerLog `
        -ServerPassMarker "RPG_REWARD_E2E PARTY_RESTORE_PASS Players=$ClientCount Claimed=false" `
        -Clients $clients
    Assert-PartyRewardState -Members $members -RewardSessionId $firstSessionId `
        -Claimed:$runMailClaim

    Close-SessionAsFailed `
        -SessionId $recoverySessionId `
        -ServerId $recoveryServerId
    if ($runMailClaim) {
        Write-Host 'Mailbox UI claim checks passed; restarting all processes again.'
        Stop-PartyClients
        Stop-OwnedProcess -Process $server
        $server = $null
        Stop-OwnedProcess -Process $backend.Process
        $backend = $null
        $backend = Start-Backend
        Restore-PartyAuthentication -Members $members
        Assert-PartyRewardState -Members $members -RewardSessionId $firstSessionId -Claimed
        Assert-PartyOwnerIsolation -Members $members
        $claimedSession = New-ActivatedSession -Members $members `
            -ServerId $claimedServerId -ServerPort $Port
        $claimedSessionId = [Guid] $claimedSession.dungeonSessionId
        $claimedServerLog = Join-Path $script:logRoot 'server-claimed-restore.log'
        $server = Start-DedicatedFixture -Session $claimedSession `
            -ServerId $claimedServerId -LogPath $claimedServerLog `
            -Recover -Claimed -Members $members
        Wait-SessionInProgress -ServerProcess $server `
            -SessionId $claimedSessionId -ServerLog $claimedServerLog
        $clients = @(Start-PartyClients -Members $members -SessionId $claimedSessionId `
            -Phase 'claimed-restore' -Recover -Claimed)
        Wait-PhaseMarkers -ServerProcess $server -ServerLog $claimedServerLog `
            -ServerPassMarker "RPG_REWARD_E2E PARTY_RESTORE_PASS Players=$ClientCount Claimed=true" `
            -Clients $clients
        Close-SessionAsFailed -SessionId $claimedSessionId -ServerId $claimedServerId
    }
    $firstMember = $members[0]
    $result = [pscustomobject]@{
        Result = 'PASS'
        ClientCount = $ClientCount
        CharacterId = $firstMember.CharacterId.ToString('D')
        SettledSessionId = $firstSessionId.ToString('D')
        RecoverySessionId = $recoverySessionId.ToString('D')
        ClaimedRestoreSessionId = $claimedSessionId.ToString('D')
        MailClaimAndReplayVerified = [bool] ($runMailClaim -and -not $ConcurrentClaims)
        ConcurrentMailClaimsVerified = [bool] $ConcurrentClaims
        ClaimedItemsRestartRestored = [bool] $runMailClaim
        InventoryFirstDeliveryVerified = [bool] (-not $ClaimMail)
        LiveRewardRefreshVerified = $true
        MailClaimUsesLegacyCompatibilityFixture = [bool] $ClaimMail
        InventoryOverflowVerified = [bool] $InventoryOverflow
        PartialStackOverflowVerified = [bool] $PartialStackOverflow
        FullInventoryClaimBlocked = [bool] $InventoryOverflow
        SeedItemsConsumedToFreeSlots = $(if ($InventoryOverflow) { 2 } else { 0 })
        ItemRevision = $(if ($runMailClaim) { 2 } else { 1 })
        RosterGold = 500
        TotalRosterGold = 500 * $ClientCount
        PotionItemId = $firstMember.PotionId.ToString('D')
        PotionQuantity = $(if ($PartialStackOverflow) { 1 } else { 3 })
        PotionQuantityMergedIntoExistingStack = $(if ($PartialStackOverflow) { 2 } else { 0 })
        HelmItemId = $firstMember.HelmId.ToString('D')
        HelmQuantity = 1
        DuplicateClearGrantedOnce = $true
        BackendRestartRestored = $true
        DedicatedServer = $true
        Iris = $true
        ClientOwnerProjectionVerified = $true
        HttpOwnerIsolationVerified = ($ClientCount -gt 1)
        Members = @($members | ForEach-Object {
            [pscustomobject]@{
                CharacterId = $_.CharacterId.ToString('D')
                RosterGold = 500
                PotionItemId = $_.PotionId.ToString('D')
                PotionQuantity = $(if ($PartialStackOverflow) { 1 } else { 3 })
                HelmItemId = $_.HelmId.ToString('D')
                HelmQuantity = 1
                ItemRevision = $(if ($runMailClaim) { 2 } else { 1 })
                SeedStackItemId = $(if ($InventoryOverflow) { $_.SeedIds[0].ToString('D') } else { $null })
                ConsumedSeedItemIds = $(if ($InventoryOverflow) { @($_.ConsumeA.ToString('D'), $_.ConsumeB.ToString('D')) } else { @() })
            }
        })
        Logs = $script:logRoot
    }
    $result | ConvertTo-Json -Depth 5 | Set-Content `
        -LiteralPath (Join-Path $script:logRoot 'result.json') `
        -Encoding UTF8
    $succeeded = $true
    $result
}
finally {
    Stop-PartyClients
    Stop-OwnedProcess -Process $server
    if (-not $succeeded -and $null -ne $backend) {
        if ($claimedSessionId -ne [Guid]::Empty) {
            Close-SessionAsFailed -SessionId $claimedSessionId -ServerId $claimedServerId
        }
        if ($recoverySessionId -ne [Guid]::Empty) {
            Close-SessionAsFailed `
                -SessionId $recoverySessionId `
                -ServerId $recoveryServerId
        }
        if ($firstSessionId -ne [Guid]::Empty) {
            Close-SessionAsFailed `
                -SessionId $firstSessionId `
                -ServerId $firstServerId
        }
    }
    if ($null -ne $backend) {
        Stop-OwnedProcess -Process $backend.Process
    }
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $previousEnvironment[$name],
            'Process')
    }
}
