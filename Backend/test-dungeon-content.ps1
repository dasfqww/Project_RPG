[CmdletBinding()]
param(
    [string] $PostgresConnectionString = $env:PROJECT_RPG_ABANDON_TEST_POSTGRES,
    [switch] $RequirePostgres,
    [string] $BuiltArtifactsDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$runRoot = Join-Path $projectRoot ('Saved/Tests/DungeonContentCatalog/' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Force -Path $runRoot
$dotnet = (Get-Command dotnet).Source
$artifacts = if ($BuiltArtifactsDirectory) { (Resolve-Path -LiteralPath $BuiltArtifactsDirectory).Path } else { Join-Path $runRoot 'build' }
$selectedPaths = @(
    'Backend/ProjectRpg.Backend/Configuration/DungeonContentOptions.cs',
    'Backend/ProjectRpg.Backend/Application/DungeonContentCatalog.cs',
    'Backend/ProjectRpg.Backend/Domain/DungeonContentSelection.cs',
    'Backend/ProjectRpg.Backend/Program.cs',
    'Backend/ProjectRpg.Backend/Contracts/ApiContracts.cs',
    'Backend/ProjectRpg.Backend/Data/IGameRepository.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.cs',
    'Backend/ProjectRpg.Backend/appsettings.json',
    'Backend/ProjectRpg.Backend/appsettings.Development.json',
    'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/Program.cs',
    'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/DungeonContentCatalogTests.cs',
    'Backend/Tools/Start-LocalDungeonServer.ps1',
    'Backend/Tools/Run-LocalDungeonAllocator.ps1',
    'Backend/Tools/LocalDungeonServerReadiness.ps1',
    'Backend/Tools/Request-DungeonDeparture.ps1',
    'Backend/Tools/Test-DungeonContentAllocator.ps1',
    'Backend/smoke-test.ps1', 'Backend/economy-smoke-test.ps1', 'Backend/item-smoke-test.ps1',
    'Backend/security-telemetry-smoke-test.ps1', 'Backend/dungeon-reward-smoke-test.ps1',
    'Scripts/Network/Run-DungeonRewardPersistenceE2ETest.ps1',
    'Scripts/Network/Run-ItemBackendE2ETest.ps1', 'Scripts/Network/Run-NPCPickupBackendE2ETest.ps1',
    'Scripts/Network/Run-DungeonMembershipBackendE2ETest.ps1',
    'Backend/test-dungeon-content.ps1'
)
function Get-Inputs {
    @($selectedPaths | ForEach-Object {
        [pscustomobject]@{ Path = $_; Sha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash }
    })
}
$inputsBefore = Get-Inputs
$backend = $null
$passed = $false
$proof = [ordered]@{ Passed = $false; Scope = 'Catalog policy, Memory/optional isolated PG claim, actual Program HTTP Memory, mocked allocator; no UE/Steam/Shipping'; Inputs = $inputsBefore }
$envNames = @('ASPNETCORE_ENVIRONMENT', 'DOTNET_ENVIRONMENT', 'PROJECT_RPG_BACKEND_ADMIN_TOKEN',
    'POSTGRES_CONNECTION_STRING', 'PROJECT_RPG_ABANDON_TEST_POSTGRES')
$savedEnvironment = @{}
foreach ($name in $envNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Request([string]$Method, [string]$Route, $Body, [hashtable]$Headers, [int]$Expected) {
    $requestArguments = @{ Method = $Method; Uri = "$baseUrl$Route"; UseBasicParsing = $true; TimeoutSec = 10 }
    if ($Headers) { $requestArguments.Headers = $Headers }
    if ($null -ne $Body) { $requestArguments.Body = ($Body | ConvertTo-Json -Depth 10 -Compress); $requestArguments.ContentType = 'application/json' }
    try { $response = Invoke-WebRequest @requestArguments; $status = [int]$response.StatusCode }
    catch { if ($null -eq $_.Exception.Response) { throw }; $status = [int]$_.Exception.Response.StatusCode; $response = $null }
    Require ($status -eq $Expected) "HTTP $Method $Route expected $Expected, received $status."
    if ($null -ne $response -and $response.Content) { return $response.Content | ConvertFrom-Json }
}
try {
    foreach ($relative in $selectedPaths | Where-Object { $_.EndsWith('.ps1') }) {
        $tokens = $null; $errors = $null
        $null = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot $relative), [ref]$tokens, [ref]$errors)
        Require (@($errors).Count -eq 0) "Script parser failure: $relative"
    }
    if (-not $BuiltArtifactsDirectory) {
        & $dotnet build (Join-Path $PSScriptRoot 'ProjectRpg.Backend.DungeonAbandonmentTests/ProjectRpg.Backend.DungeonAbandonmentTests.csproj') -c Release --artifacts-path $artifacts --nologo -v minimal *> (Join-Path $runRoot 'build.log')
        Require ($LASTEXITCODE -eq 0) 'Backend content test build failed; inspect build.log.'
    }
    $testDll = Join-Path $artifacts 'bin/ProjectRpg.Backend.DungeonAbandonmentTests/release/ProjectRpg.Backend.DungeonAbandonmentTests.dll'
    $backendDll = Join-Path $artifacts 'bin/ProjectRpg.Backend/release/ProjectRpg.Backend.dll'
    Require ((Test-Path -LiteralPath $testDll) -and (Test-Path -LiteralPath $backendDll)) 'Required isolated build artifacts are missing.'
    $proof.BackendSha256 = (Get-FileHash -LiteralPath $backendDll).Hash
    $proof.TestSha256 = (Get-FileHash -LiteralPath $testDll).Hash
    $proof.ExternallyBuiltArtifacts = [bool]$BuiltArtifactsDirectory
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_ABANDON_TEST_POSTGRES', $PostgresConnectionString, 'Process')
    $testArgs = @($testDll, '--content-catalog-only')
    if ($RequirePostgres) { $testArgs += '--require-postgres' }
    & $dotnet @testArgs *> (Join-Path $runRoot 'policy-repository.log')
    Require ($LASTEXITCODE -eq 0) 'Catalog/repository tests failed; inspect policy-repository.log.'
    $proof.PostgresRequested = -not [string]::IsNullOrWhiteSpace($PostgresConnectionString)
    & (Join-Path $PSScriptRoot 'Tools/Test-DungeonContentAllocator.ps1') -OutputDirectory (Join-Path $runRoot 'allocator') *> (Join-Path $runRoot 'allocator.log')

    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start(); $port = $listener.LocalEndpoint.Port; $listener.Stop()
    $baseUrl = "http://127.0.0.1:$port"
    $adminToken = [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
    $adminHeaders = @{ Authorization = "Bearer $adminToken" }
    [Environment]::SetEnvironmentVariable('ASPNETCORE_ENVIRONMENT', 'Development', 'Process')
    [Environment]::SetEnvironmentVariable('DOTNET_ENVIRONMENT', 'Development', 'Process')
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN', $adminToken, 'Process')
    [Environment]::SetEnvironmentVariable('POSTGRES_CONNECTION_STRING', $null, 'Process')
    $arguments = @(('"' + $backendDll + '"'), '--Storage:Provider=Memory', '--Steam:AllowDevelopmentAuthentication=true',
        '--RateLimits:Enabled=false', "--Kestrel:Endpoints:Http:Url=$baseUrl",
        '--DungeonContent:Entries:1:DungeonId=Dungeon.Second', '--DungeonContent:Entries:1:MapPackage=/Game/Maps/testmap',
        '--DungeonContent:Entries:1:Difficulties:0=Hard')
    $backend = Start-Process -FilePath $dotnet -ArgumentList $arguments -WorkingDirectory (Split-Path $backendDll -Parent) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runRoot 'backend.stdout.log') -RedirectStandardError (Join-Path $runRoot 'backend.stderr.log')
    $ready = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        if ($backend.HasExited) { throw 'Isolated backend exited before readiness.' }
        try { $null = Invoke-WebRequest "$baseUrl/health/ready" -UseBasicParsing -TimeoutSec 1; $ready = $true; break } catch { Start-Sleep -Milliseconds 250 }
    }
    Require $ready 'Isolated backend readiness timed out.'
    $login = Request Post '/api/auth/steam-ticket' @{ ticket = 'dev:76561198000999881' } @{} 200
    $playerHeaders = @{ Authorization = "Bearer $($login.accessToken)" }
    $characters = @()
    for ($i = 0; $i -lt 5; $i++) { $characters += Request Post '/api/characters' @{ name = "ContentCase$i" } $playerHeaders 201 }
    foreach ($selection in @(
        @{ dungeonId = 'Dungeon.Unknown'; difficulty = 'Normal' },
        @{ dungeonId = 'dungeon.RaidSwordMan'; difficulty = 'Normal' },
        @{ dungeonId = ' Dungeon.RaidSwordMan'; difficulty = 'Normal' },
        @{ dungeonId = 'Dungeon.Second'; difficulty = 'Normal' },
        @{ dungeonId = 'Dungeon.RaidSwordMan'; difficulty = 'hard' })) {
        $selection.characterId = $characters[0].characterId
        $null = Request Post '/api/dungeon-sessions' $selection $playerHeaders 400
    }
    $null = Request Get "/api/characters/$($characters[0].characterId)/active-dungeon-session" $null $playerHeaders 204
    $null = Request Post '/api/dungeon-sessions/claim' @{ serverId = 'forbidden'; serverAddress = '127.0.0.1:7881' } $playerHeaders 403
    $sessions = @()
    foreach ($case in @(@('Dungeon.RaidSwordMan', 'Normal'), @('Dungeon.Second', 'Hard'))) {
        $index = $sessions.Count
        $sessions += Request Post '/api/dungeon-sessions' @{ characterId = $characters[$index].characterId; dungeonId = $case[0]; difficulty = $case[1] } $playerHeaders 201
    }
    . (Join-Path $PSScriptRoot 'Tools/Request-DungeonDeparture.ps1')
    for ($i = 0; $i -lt $sessions.Count; $i++) {
        $null = Request-RPGDungeonDeparture $baseUrl $sessions[$i].dungeonSessionId $characters[$i].characterId $playerHeaders
    }
    $first = Request Post '/api/dungeon-sessions/claim' @{ serverId = 'content-claim'; serverAddress = '127.0.0.1:7881' } $adminHeaders 200
    Require ($first.dungeonSessionId -ceq $sessions[0].dungeonSessionId -and $first.serverMapPackage -ceq '/Game/Maps/bossmap' -and $first.gameServerAccessToken) 'Claim did not return exact catalog map/scoped credential.'
    $second = Request Post "/api/dungeon-sessions/$($sessions[1].dungeonSessionId)/activate" @{ serverId = 'content-activate'; serverAddress = '127.0.0.1:7882' } $adminHeaders 200
    Require ($second.serverMapPackage -ceq '/Game/Maps/testmap' -and $second.gameServerAccessToken) 'Activate did not resolve the second catalog entry.'
    $read = Request Get "/api/dungeon-sessions/$($first.dungeonSessionId)" $null $playerHeaders 200
    Require (-not $read.serverMapPackage -and -not $read.gameServerAccessToken) 'Allocator-only metadata/credential leaked into player session read.'
    $serverHeaders = @{ Authorization = "Bearer $($first.gameServerAccessToken)" }
    $null = Request Post '/api/dungeon-sessions/claim' @{ serverId = 'forbidden-server'; serverAddress = '127.0.0.1:7883' } $serverHeaders 403
    $started = Request Post "/api/dungeon-sessions/$($first.dungeonSessionId)/start" @{ serverId = 'content-claim' } $serverHeaders 200
    Require ($started.state -ceq 'InProgress') 'Existing assigned-server start contract changed.'
    $active = Request Get "/api/characters/$($characters[0].characterId)/active-dungeon-session" $null $playerHeaders 200
    Require ($active.dungeonSessionId -ceq $first.dungeonSessionId -and $active.state -ceq 'InProgress') 'Existing active session recovery was lost.'
    $null = Request Post '/api/dungeon-sessions/claim' @{ serverId = 'empty'; serverAddress = '127.0.0.1:7883' } $adminHeaders 204
    $proof.HttpCases = 'unknown/case/whitespace/unsupported difficulty rejected without membership, player/server cannot allocate, two IDs resolve distinct maps through claim/activate, player read has no launch map/token, assigned start and active recovery, empty queue'
    $proof.InputsUnchanged = (($inputsBefore | ConvertTo-Json -Compress) -ceq ((Get-Inputs) | ConvertTo-Json -Compress))
    Require $proof.InputsUnchanged 'Selected inputs changed during verification.'
    Require ($proof.BackendSha256 -ceq (Get-FileHash -LiteralPath $backendDll).Hash) 'Isolated backend module changed during verification.'
    $passed = $true
}
finally {
    if ($null -ne $backend -and -not $backend.HasExited) { Stop-Process -Id $backend.Id -Force; $backend.WaitForExit() }
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    $proof.Passed = $passed
    $proof.ClosedLogs = @(Get-ChildItem -LiteralPath $runRoot -Filter '*.log' | ForEach-Object { [pscustomobject]@{ Path = $_.Name; Sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash } })
    $proof | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $runRoot 'Verification.proof.json') -Encoding UTF8
    Write-Output "Dungeon content verification: Passed=$passed Root=$runRoot"
}
