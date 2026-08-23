param(
    [string]$ConnectionString =
        'Host=127.0.0.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=project_rpg_dev_only',
    [string]$BaseUrl = 'http://127.0.0.1:3010',
    [string]$AdminToken = $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN,
    [string]$RunId = [Guid]::NewGuid().ToString('N').Substring(0, 12)
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
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
$temporaryRoot = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    "project-rpg-postgres-$([Guid]::NewGuid().ToString('N'))"
$buildDirectory = Join-Path $temporaryRoot 'build'
$logDirectory = Join-Path $temporaryRoot 'logs'
$backendHandle = $null
$backendGeneration = 0
$succeeded = $false

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
                -Uri "$BaseUrl/health" `
                -TimeoutSec 2
            if ($health.status -eq 'ok') {
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

    & $dotnet build $backendProject --output $buildDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "Backend build failed with exit code $LASTEXITCODE."
    }

    Set-BackendEnvironment
    $backendHandle = Start-Backend
    Wait-BackendReady -Handle $backendHandle

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
    $session = Invoke-RestMethod `
        -Method Get `
        -Uri "$BaseUrl/api/dungeon-sessions/$($smokeResult.DungeonSessionId)" `
        -Headers $adminHeaders
    if ($session.state -ne 'Cleared') {
        throw 'The completed dungeon state was not preserved after restart.'
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
