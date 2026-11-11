[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskId = 'auction-escrow-pg-20261110'
$runId = [Guid]::NewGuid().ToString('N')
$proofRoot = Join-Path $workspace 'Saved/BuildChecks/AuctionEscrow-20261110'
$proof = Join-Path $proofRoot $runId
$tempRoot = Join-Path $workspace 'Saved/AE'
$temp = Join-Path $tempRoot $runId.Substring(0,8)
$inputs = Join-Path $temp 'Inputs'
$containerName = 'rpg-auction-escrow-' + $runId
$containerId = $null
$failure = $null
$overlay = @(
    'Backend/ProjectRpg.Backend/Domain/Trading/AuctionClaim.cs',
    'Backend/ProjectRpg.Backend/Data/IAuctionClaimRepository.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresAuctionClaimRepository.cs',
    'Backend/Database/Migrations/0014_auction_claims.sql',
    'Backend/ProjectRpg.Backend.TradingTests/AuctionClaimCases.cs',
    'Backend/ProjectRpg.Backend/Domain/Trading/AuctionEscrow.cs',
    'Backend/ProjectRpg.Backend/Domain/Trading/AuctionPurchase.cs',
    'Backend/ProjectRpg.Backend/Domain/Trading/AuctionListing.cs',
    'Backend/ProjectRpg.Backend/Domain/Trading/TradeItemPolicy.cs',
    'Backend/ProjectRpg.Backend/Domain/ItemModels.cs',
    'Backend/ProjectRpg.Backend/Domain/DungeonRewardDeliveryPlanner.cs',
    'Backend/ProjectRpg.Backend/Data/IAuctionEscrowRepository.cs',
    'Backend/ProjectRpg.Backend/Data/IAuctionPurchaseRepository.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresAuctionEscrowRepository.Purchase.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresAuctionEscrowRepository.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresTradeReservationGuards.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresSchemaMigrations.cs',
    'Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj',
    'Backend/Database/Migrations/0012_auction_escrow.sql',
    'Backend/Database/Migrations/0013_auction_sales.sql',
    'Backend/ProjectRpg.Backend.TradingTests/AuctionEscrowCases.cs',
    'Backend/ProjectRpg.Backend.TradingTests/AuctionPurchaseCases.cs',
    'Backend/ProjectRpg.Backend.TradingTests/Program.cs',
    'Backend/ProjectRpg.Backend.MigrationTests/Program.cs'
)
$environmentNames = @('PROJECT_RPG_TRADE_TEST_RUN','PROJECT_RPG_TRADE_TEST_POSTGRES',
    'PROJECT_RPG_EQUIPMENT_TEST_RUN_ID','PROJECT_RPG_EQUIPMENT_TEST_POSTGRES')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$result = [ordered]@{ Passed=$false; Scope='PG auction registration/return/receipt; existing direct-trade HTTP regressions and migration command/pure checks. No auction purchase/API/UE or original DB migration'; Head=''; Hashes=@{}; Modules=@{}; Commands=@(); Checks=@(); Cleanup=$null; CleanupComplete=$false }
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Assert-Reservation {
    if (!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 -Pattern '^\|[^|]* / auction-escrow-pg-20261110 \|')) { throw 'Reservation missing' }
}
function Invoke-Dotnet([string[]]$Arguments, [string]$Label) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & dotnet @Arguments *> (Join-Path $proof ($Label+'.log'))
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    $result.Commands += @{Label=$Label; Arguments=@('dotnet')+$Arguments; Exit=$code}
    Get-Content -LiteralPath (Join-Path $proof ($Label+'.log')) -Tail 12
    if ($code -ne 0) { throw ('dotnet failed: '+$Label) }
    $result.Checks += $Label
}
function Test-Project([string]$Project, [string[]]$Modes) {
    $file = Join-Path $inputs ('Backend/'+$Project+'/'+$Project+'.csproj')
    Invoke-Dotnet @('build',$file,'-c','Release','--nologo','--disable-build-servers','-p:UseSharedCompilation=false') ($Project+'.build')
    $dll = Join-Path $inputs ('Backend/'+$Project+'/bin/Release/net9.0/'+$Project+'.dll')
    $result.Modules[$Project] = (Get-FileHash -LiteralPath $dll).Hash
    foreach ($mode in $Modes) {
        $arguments = @($dll)
        if ($mode) { $arguments += $mode }
        Invoke-Dotnet $arguments ($Project+'.'+$(if($mode){$mode.TrimStart('-')}else{'domain'}))
    }
}
try {
    Assert-Reservation
    Get-Command dotnet -ErrorAction Stop | Out-Null
    $null = New-Item -ItemType Directory -Path $inputs,$proof -Force
    $result.Head = (git -C $workspace rev-parse HEAD).Trim()
    & git -C $workspace archive --format=tar -o (Join-Path $temp 'Backend.tar') $result.Head Backend
    if ($LASTEXITCODE -ne 0) { throw 'Archive failed' }
    & tar -xf (Join-Path $temp 'Backend.tar') -C $inputs
    if ($LASTEXITCODE -ne 0) { throw 'Archive extraction failed' }
    foreach ($relative in $overlay) {
        $source = Join-Path $workspace $relative
        $destination = Join-Path $inputs $relative
        $hash = (Get-FileHash -LiteralPath $source).Hash
        $null = New-Item -ItemType Directory -Path (Split-Path $destination) -Force
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $source).Hash -ne $hash -or (Get-FileHash -LiteralPath $destination).Hash -ne $hash) { throw 'Source changed during snapshot' }
        $result.Hashes[$relative] = $hash
    }
    $result.Hashes['Scripts/Tests/Run-AuctionEscrowTests.ps1'] = (Get-FileHash -LiteralPath $PSCommandPath).Hash
    $password = [Guid]::NewGuid().ToString('N')
    $containerId = & docker run --pull=never --detach --name $containerName --label ('project-rpg.task='+$taskId) --label ('project-rpg.run='+$runId) --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env 'POSTGRES_DB=trade_test' --env 'POSTGRES_USER=trade_test' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine
    if ($LASTEXITCODE -ne 0) { $containerId=$null; throw 'Private PostgreSQL failed to start' }
    $containerId = $containerId.Trim()
    $ready = $false
    for ($attempt=0; $attempt -lt 40; $attempt++) {
        # The image's initialization server listens only on a Unix socket and then restarts.
        & docker exec $containerId pg_isready -h 127.0.0.1 -U trade_test -d trade_test *> $null
        if ($LASTEXITCODE -eq 0) { $ready=$true; break }
        Start-Sleep -Milliseconds 500
    }
    if (!$ready) { throw 'Private PostgreSQL not ready' }
    & docker exec --env ('PGPASSWORD='+$password) $containerId createdb -h 127.0.0.1 -U trade_test equipment_test
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create private regression database' }
    $port = (& docker port $containerId 5432/tcp).Trim().Split(':')[-1]
    $connection = 'Host=127.0.0.1;Port='+$port+';Username=trade_test;Password='+$password
    $env:PROJECT_RPG_TRADE_TEST_RUN = $runId
    $env:PROJECT_RPG_TRADE_TEST_POSTGRES = $connection+';Database=trade_test'
    $env:PROJECT_RPG_EQUIPMENT_TEST_RUN_ID = $runId
    $env:PROJECT_RPG_EQUIPMENT_TEST_POSTGRES = $connection+';Database=equipment_test'
    $modes = @('--auction-escrow','--auction-restart','--auction-domain','','--api-memory','--api-postgres')
    Test-Project 'ProjectRpg.Backend.TradingTests' $modes
    Test-Project 'ProjectRpg.Backend.MigrationTests' @('')
    foreach ($relative in $result.Hashes.Keys) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $relative)).Hash -ne $result.Hashes[$relative]) { throw ('Input changed: '+$relative) }
    }
    $result.Passed = $true
} catch { $failure=$_.Exception.Message }
finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
    $owned = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'dotnet.exe' -and $_.CommandLine -and $_.CommandLine.Replace('/','\').Contains($temp.Replace('/','\'))
    })
    foreach ($entry in $owned) {
        try { Stop-Process -Id $entry.ProcessId -ErrorAction Stop; Wait-Process -Id $entry.ProcessId -Timeout 10 -ErrorAction SilentlyContinue }
        catch { $failure=([string]$failure+'; owned process cleanup: '+$_.Exception.Message) }
    }
    try {
        Assert-Reservation
        if ($containerId) {
            $info = (& docker inspect $containerId | ConvertFrom-Json)[0]
            if ($info.Id -ne $containerId -or $info.Name -ne ('/'+$containerName) -or
                $info.Config.Labels.'project-rpg.task' -ne $taskId -or $info.Config.Labels.'project-rpg.run' -ne $runId) { throw 'Container ownership mismatch' }
            & docker rm --force $containerId | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Private PostgreSQL cleanup failed' }
        }
        $remaining = @(Get-CimInstance Win32_Process | Where-Object {
            $_.Name -eq 'dotnet.exe' -and $_.CommandLine -and $_.CommandLine.Replace('/','\').Contains($temp.Replace('/','\'))
        })
        if ($remaining.Count) { throw 'Private process still using temporary inputs' }
        if (Test-Path -LiteralPath $temp) { $result.Cleanup = Remove-GuardianProbeArtifact -Path $temp -AllowedRoot $tempRoot }
    } catch { $failure=([string]$failure+'; cleanup: '+$_.Exception.Message) }
    $result.CleanupComplete = !(Test-Path -LiteralPath $temp)
    $result.Failure = $failure
    $result.Passed = $result.Passed -and !$failure -and $result.CleanupComplete
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $proof 'Result.json') -Encoding UTF8
    Write-Output ('Passed='+$result.Passed+'; Proof='+ (Join-Path $proof 'Result.json'))
}
if (!$result.Passed) { throw $failure }
