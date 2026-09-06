param(
    [string]$ConnectionString =
        'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only',
    [string]$BaseUrl = 'http://127.0.0.1:3010',
    [string]$AdminToken = $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN,
    [string]$RunId = [Guid]::NewGuid().ToString('N').Substring(0, 12),
    [switch]$NoRestore
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ConnectionString)) {
    throw 'A PostgreSQL connection string is required.'
}
if ($BaseUrl -notmatch '^http://(127\.0\.0\.1|localhost):\d{1,5}/?$') {
    throw 'BaseUrl must be an unused localhost HTTP endpoint.'
}
if ($RunId -notmatch '^[a-fA-F0-9]{8,24}$') {
    throw 'RunId must contain 8 to 24 hexadecimal characters.'
}
if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    $AdminToken = "postgres-integration-$([Guid]::NewGuid().ToString('N'))"
}
if ($AdminToken.Trim().Length -lt 32) {
    throw 'AdminToken must contain at least 32 characters.'
}

$BaseUrl = $BaseUrl.TrimEnd('/')
$RunId = $RunId.ToLowerInvariant()
$backendProject = Join-Path `
    $PSScriptRoot `
    'ProjectRpg.Backend\ProjectRpg.Backend.csproj'
$smokeTest = Join-Path $PSScriptRoot 'smoke-test.ps1'
$economySmokeTest = Join-Path $PSScriptRoot 'economy-smoke-test.ps1'
$securityTelemetrySmokeTest = Join-Path `
    $PSScriptRoot `
    'security-telemetry-smoke-test.ps1'
$itemSmokeTest = Join-Path $PSScriptRoot 'item-smoke-test.ps1'
$dungeonRewardSmokeTest = Join-Path `
    $PSScriptRoot `
    'dungeon-reward-smoke-test.ps1'
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
$temporaryRoot = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    "project-rpg-postgres-$([Guid]::NewGuid().ToString('N'))"
$buildDirectory = Join-Path $temporaryRoot 'build'
$logDirectory = Join-Path $temporaryRoot 'logs'
$backendHandle = $null
$backendGeneration = 0
$succeeded = $false
$configuredRewardResults = @()
$securityTelemetryResult = $null

$environmentNames = @(
    'ASPNETCORE_ENVIRONMENT',
    'PROJECT_RPG_BACKEND_ADMIN_TOKEN',
    'POSTGRES_CONNECTION_STRING',
    'Kestrel__Endpoints__Http__Url'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable(
        $name,
        [EnvironmentVariableTarget]::Process)
}

function Set-BackendEnvironment {
    [Environment]::SetEnvironmentVariable(
        'ASPNETCORE_ENVIRONMENT',
        'Development',
        [EnvironmentVariableTarget]::Process)
    [Environment]::SetEnvironmentVariable(
        'PROJECT_RPG_BACKEND_ADMIN_TOKEN',
        $AdminToken,
        [EnvironmentVariableTarget]::Process)
    [Environment]::SetEnvironmentVariable(
        'POSTGRES_CONNECTION_STRING',
        $ConnectionString,
        [EnvironmentVariableTarget]::Process)
    [Environment]::SetEnvironmentVariable(
        'Kestrel__Endpoints__Http__Url',
        $BaseUrl,
        [EnvironmentVariableTarget]::Process)
}

function Start-Backend {
    # Some launchers inject both `Path` and `PATH`. Start-Process treats them as
    # duplicate keys on Windows even though the process environment itself does not.
    $processEnvironment = [Environment]::GetEnvironmentVariables(
        [EnvironmentVariableTarget]::Process)
    $pathKeys = @($processEnvironment.Keys | Where-Object {
        [string]::Equals(
            [string]$_,
            'Path',
            [StringComparison]::OrdinalIgnoreCase)
    })
    if ($pathKeys.Count -gt 1) {
        $pathValue = [Environment]::GetEnvironmentVariable(
            'Path',
            [EnvironmentVariableTarget]::Process)
        foreach ($pathKey in $pathKeys) {
            if (-not [string]::Equals(
                    [string]$pathKey,
                    'Path',
                    [StringComparison]::Ordinal)) {
                [Environment]::SetEnvironmentVariable(
                    [string]$pathKey,
                    $null,
                    [EnvironmentVariableTarget]::Process)
            }
        }
        [Environment]::SetEnvironmentVariable(
            'Path',
            $pathValue,
            [EnvironmentVariableTarget]::Process)
    }

    $script:backendGeneration++
    $stdoutPath = Join-Path `
        $logDirectory `
        "backend-$($script:backendGeneration).stdout.log"
    $stderrPath = Join-Path `
        $logDirectory `
        "backend-$($script:backendGeneration).stderr.log"
    $backendDll = Join-Path $buildDirectory 'ProjectRpg.Backend.dll'
    $process = Start-Process `
        -FilePath $dotnet `
        -ArgumentList @("`"$backendDll`"") `
        -WorkingDirectory $buildDirectory `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru
    return [pscustomobject]@{
        Process = $process
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
    }
}

function Stop-Backend {
    param([object]$Handle)

    if ($null -eq $Handle -or $null -eq $Handle.Process) {
        return
    }
    if (-not $Handle.Process.HasExited) {
        Stop-Process -Id $Handle.Process.Id -Force
        $Handle.Process.WaitForExit(10000) | Out-Null
    }
}

function Get-BackendFailureLog {
    param([object]$Handle)

    $parts = @()
    foreach ($path in @($Handle.StdoutPath, $Handle.StderrPath)) {
        if (Test-Path -LiteralPath $path) {
            $parts += Get-Content -LiteralPath $path -Tail 80
        }
    }
    return $parts -join [Environment]::NewLine
}

function Wait-BackendReady {
    param(
        [object]$Handle,
        [int]$TimeoutSeconds = 30
    )

    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        if ($Handle.Process.HasExited) {
            $log = Get-BackendFailureLog -Handle $Handle
            throw "Backend exited before becoming ready.`n$log"
        }
        try {
            $health = Invoke-RestMethod `
                -Method Get `
                -Uri "$BaseUrl/health/ready" `
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

    $failureLog = Get-BackendFailureLog -Handle $Handle
    throw "Backend did not become ready in $TimeoutSeconds seconds.`n$failureLog"
}

function Restore-Environment {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $previousEnvironment[$name],
            [EnvironmentVariableTarget]::Process)
    }
}

try {
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

    $buildArguments = @('build', $backendProject, '--output', $buildDirectory)
    if ($NoRestore) {
        $buildArguments += '--no-restore'
    }
    & $dotnet @buildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Backend build failed with exit code $LASTEXITCODE."
    }

    Set-BackendEnvironment
    $backendHandle = Start-Backend
    Wait-BackendReady -Handle $backendHandle

    $liveness = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/health/live" `
        -TimeoutSec 2
    if ($liveness.status -ne 'ok') {
        throw 'Backend liveness endpoint did not report ok.'
    }

    $smokeOutput = @(& $smokeTest `
        -BaseUrl $BaseUrl `
        -AdminToken $AdminToken `
        -RunId $RunId)
    $smokeResult = $smokeOutput |
        Where-Object {
            $null -ne $_ -and
            $null -ne $_.PSObject.Properties['CharacterId']
        } |
        Select-Object -Last 1
    if ($null -eq $smokeResult) {
        throw 'The smoke test did not return its persistence checkpoint.'
    }

    & $economySmokeTest `
        -BaseUrl $BaseUrl `
        -AdminToken $AdminToken `
        -RunId $RunId
    $securityTelemetryResults = @(& $securityTelemetrySmokeTest `
        -BaseUrl $BaseUrl `
        -AdminToken $AdminToken `
        -RunId $RunId)
    $securityTelemetryResult = $securityTelemetryResults |
        Where-Object {
            $null -ne $_ -and
            $null -ne $_.PSObject.Properties['DungeonSessionId']
        } |
        Select-Object -Last 1
    if ($null -eq $securityTelemetryResult) {
        throw 'The security telemetry smoke test did not return its persistence checkpoint.'
    }
    & $itemSmokeTest `
        -BaseUrl $BaseUrl `
        -AdminToken $AdminToken `
        -RunId $RunId
    $configuredRewardResults = @(& $dungeonRewardSmokeTest `
        -BaseUrl $BaseUrl `
        -AdminToken $AdminToken `
        -RunId $RunId)
    if ($configuredRewardResults.Count -ne 4) {
        throw 'The configured dungeon reward smoke test did not return all four difficulties.'
    }

    $authBeforeRestart = Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/api/auth/steam-ticket" `
        -ContentType 'application/json' `
        -Body (@{
            ticket = "dev:$($smokeResult.SteamId)"
        } | ConvertTo-Json -Compress)
    $persistedPlayerHeaders = @{
        Authorization = "Bearer $($authBeforeRestart.accessToken)"
    }

    Stop-Backend -Handle $backendHandle
    $backendHandle = $null

    $backendHandle = Start-Backend
    Wait-BackendReady -Handle $backendHandle

    $characters = @(Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/characters" `
        -Headers $persistedPlayerHeaders)
    $persistedCharacter = $characters | Where-Object {
        $_.characterId -eq $smokeResult.CharacterId
    }
    if ($null -eq $persistedCharacter) {
        throw 'The character disappeared after the backend restart.'
    }

    $wallet = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/economy/wallets/$($smokeResult.CharacterId)" `
        -Headers $persistedPlayerHeaders
    $persistedCurrency = @($wallet.balances | Where-Object {
        $_.currencyCode -eq $smokeResult.CurrencyDefinition
    })
    if ($persistedCurrency.Count -ne 1 -or
        $persistedCurrency[0].balance -ne 125) {
        throw 'The settled currency balance was not preserved after restart.'
    }

    $items = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/items?ownerType=Character&ownerId=$($smokeResult.CharacterId)&includeTerminal=false" `
        -Headers $persistedPlayerHeaders
    $persistedRewards = @($items.items | Where-Object {
        $_.definitionName -eq 'Potion.SmokeReward'
    })
    if ($persistedRewards.Count -ne 1 -or
        $persistedRewards[0].state.quantity -ne 2) {
        throw 'The settled item reward was not preserved after restart.'
    }

    $adminHeaders = @{ Authorization = "Bearer $AdminToken" }
    $securitySummary = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/security/sessions/$($securityTelemetryResult.DungeonSessionId)/summary" `
        -Headers $adminHeaders
    if ($securitySummary.totalEvents -ne 4 -or
        $securitySummary.totalScore -ne 23) {
        throw 'Security telemetry was not preserved after the backend restart.'
    }
    $session = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/dungeon-sessions/$($smokeResult.DungeonSessionId)" `
        -Headers $adminHeaders
    if ($session.state -ne 'Cleared') {
        throw 'The completed dungeon state was not preserved after restart.'
    }

    $definitions = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/economy/currency-definitions" `
        -Headers $adminHeaders
    $rosterGoldDefinition = @($definitions.definitions | Where-Object {
        $_.currencyCode -eq 'RosterGold'
    })
    if ($rosterGoldDefinition.Count -ne 1 -or
        $rosterGoldDefinition[0].scope -ne 'Roster' -or
        $rosterGoldDefinition[0].maxBalance -ne 1000000000 -or
        -not $rosterGoldDefinition[0].enabled) {
        throw 'The production RosterGold definition was not preserved after restart.'
    }

    foreach ($rewardResult in $configuredRewardResults) {
        $rewardAuth = Invoke-RestMethod `
            -Method Post `
            -Uri "$BaseUrl/api/auth/steam-ticket" `
            -ContentType 'application/json' `
            -Body (@{
                ticket = "dev:$($rewardResult.SteamId)"
            } | ConvertTo-Json -Compress)
        $rewardPlayerHeaders = @{
            Authorization = "Bearer $($rewardAuth.accessToken)"
        }

        $rewardWallet = Invoke-RestMethod `
            -Method Get `
            -Uri "$BaseUrl/api/economy/wallets/$($rewardResult.CharacterId)" `
            -Headers $rewardPlayerHeaders
        $rewardGold = @($rewardWallet.balances | Where-Object {
            $_.currencyCode -eq 'RosterGold'
        })
        if ($rewardGold.Count -ne 1 -or
            $rewardGold[0].balance -ne $rewardResult.GoldBalance) {
            throw "The '$($rewardResult.Difficulty)' RosterGold reward was not preserved after restart."
        }

        $rewardItems = Invoke-RestMethod `
            -Method Get `
            -Uri "$BaseUrl/api/items?ownerType=Character&ownerId=$($rewardResult.CharacterId)&includeTerminal=false" `
            -Headers $rewardPlayerHeaders
        $rewardPotions = @($rewardItems.items | Where-Object {
            $_.definitionName -eq 'GameItem.Consume.Potion.Red.Large'
        })
        if ($rewardPotions.Count -ne 1 -or
            $rewardPotions[0].state.quantity -ne
                $rewardResult.PotionQuantity -or
            $rewardPotions[0].location.containerType -ne 'Mail') {
            throw "The '$($rewardResult.Difficulty)' potion reward was not preserved after restart."
        }
        $rewardHelms = @($rewardItems.items | Where-Object {
            $_.definitionName -eq 'GameItem.Equipment.Helm.Default'
        })
        $expectedHelmCount = if ($rewardResult.HelmQuantity -gt 0) {
            1
        }
        else {
            0
        }
        if ($rewardHelms.Count -ne $expectedHelmCount -or
            ($expectedHelmCount -eq 1 -and
                ($rewardHelms[0].state.quantity -ne
                    $rewardResult.HelmQuantity -or
                 $rewardHelms[0].location.containerType -ne 'Mail'))) {
            throw "The '$($rewardResult.Difficulty)' helm reward was not preserved after restart."
        }

        $rewardSession = Invoke-RestMethod `
            -Method Get `
            -Uri "$BaseUrl/api/dungeon-sessions/$($rewardResult.DungeonSessionId)" `
            -Headers $adminHeaders
        if ($rewardSession.state -ne 'Cleared') {
            throw "The '$($rewardResult.Difficulty)' dungeon state was not preserved after restart."
        }
    }

    $succeeded = $true
    [pscustomobject]@{
        RunId = $RunId
        CharacterId = $smokeResult.CharacterId
        CurrencyCode = $smokeResult.CurrencyDefinition
        CurrencyBalance = $persistedCurrency[0].balance
        RewardItemCount = $persistedRewards.Count
        DungeonState = $session.state
        AuthSessionSurvivedRestart = $true
        SchemaReapplySurvivedRestart = $true
        StorageReadinessPassed = $true
        EconomySmokePassed = $true
        ItemV2SmokePassed = $true
        SecurityTelemetryEvents = $securitySummary.totalEvents
        SecurityTelemetrySurvivedRestart = $true
        ConfiguredDungeonRewardCases = $configuredRewardResults.Count
        RosterGoldDefinitionSurvivedRestart = $true
    }
}
finally {
    Stop-Backend -Handle $backendHandle
    Restore-Environment
    if ($succeeded -and (Test-Path -LiteralPath $temporaryRoot)) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
    elseif (Test-Path -LiteralPath $temporaryRoot) {
        Write-Warning "Integration artifacts were retained at '$temporaryRoot'."
    }
}
