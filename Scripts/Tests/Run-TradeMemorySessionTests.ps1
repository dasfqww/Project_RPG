[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskId = 'trading-memory-20261106'
$runId = [Guid]::NewGuid().ToString('N')
$proofRoot = Join-Path $workspace 'Saved/BuildChecks/TradeMemorySession-20261106'
$proof = Join-Path $proofRoot $runId
$tempRoot = Join-Path $proofRoot 'Temp'
$temp = Join-Path $tempRoot $runId
$inputs = Join-Path $temp 'Inputs'
$containerName = 'rpg-trade-memory-' + $runId
$containerId = $null
$failure = $null
$overlay = @(
    'Backend/ProjectRpg.Backend/Data/InMemoryTradeSessionRepository.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.Trading.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryTransactionGate.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryItemRepository.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryEconomyRepository.cs',
    'Backend/ProjectRpg.Backend/Data/PostgresTradeSessionRepository.cs',
    'Backend/ProjectRpg.Backend/Domain/Trading/TradeSessionRules.cs',
    'Backend/ProjectRpg.Backend/Program.cs',
    'Backend/ProjectRpg.Backend.TradingTests/Program.cs',
    'Backend/ProjectRpg.Backend.TradingTests/SessionCases.cs',
    'Backend/ProjectRpg.Backend.TradingTests/MemorySessionCases.cs'
)
$environmentNames = @('PROJECT_RPG_TRADE_TEST_RUN','PROJECT_RPG_TRADE_TEST_POSTGRES',
    'PROJECT_RPG_EQUIPMENT_TEST_RUN_ID','PROJECT_RPG_EQUIPMENT_TEST_POSTGRES')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$result = [ordered]@{ Passed=$false; Head=''; Hashes=@{}; Checks=@(); Cleanup=$null; CleanupComplete=$false }
function Remove-TradeMemoryTemp([string]$Target, [string]$AllowedRoot, [string]$ExpectedRun) {
    # Node's Windows filesystem APIs handle long paths. Reject links before deleting any file.
    $summary = @'
const fs=require('fs'), path=require('path');
const [targetArg,rootArg,run]=process.argv.slice(2);
const target=path.resolve(targetArg), root=path.resolve(rootArg);
if(!/^[a-f0-9]{32}$/.test(run)||path.basename(target)!==run||path.dirname(target).toLowerCase()!==root.toLowerCase()) throw new Error('Not an owned run path');
for(let p=root;;){const st=fs.lstatSync(p);if(!st.isDirectory()||st.isSymbolicLink())throw new Error('Redirected cleanup ancestor');const next=path.dirname(p);if(next===p)break;p=next;}
const files=[],dirs=[];let bytes=0;
function check(p){if(!path.resolve(p).toLowerCase().startsWith((root+path.sep).toLowerCase()))throw new Error('Cleanup escaped root');const st=fs.lstatSync(p);if(st.isSymbolicLink())throw new Error('Unexpected link; preserve target');return st;}
function scan(p){const st=check(p);if(st.isDirectory()){dirs.push(p);for(const n of fs.readdirSync(p))scan(path.join(p,n));}else{bytes+=st.size;files.push(p);}}
if(fs.existsSync(target))scan(target);
for(const p of files){check(p);fs.unlinkSync(p);}for(const p of dirs.reverse()){check(p);fs.rmdirSync(p);}
process.stdout.write(JSON.stringify({deletedBytes:bytes,deletedFiles:files.length,deletedDirectories:dirs.length,unlinkedEntries:0}));
'@ | & node - $Target $AllowedRoot $ExpectedRun
    if ($LASTEXITCODE -ne 0) { throw 'Owned long-path cleanup failed' }
    return ($summary | ConvertFrom-Json)
}
function Assert-Reservation {
    if (!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 -Pattern '^\|[^|]* / trading-memory-20261106 \|')) { throw 'Reservation missing' }
}
function Invoke-Dotnet([string[]]$Arguments, [string]$Label) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & dotnet @Arguments *> (Join-Path $proof ($Label+'.log'))
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    Get-Content -LiteralPath (Join-Path $proof ($Label+'.log')) -Tail 12
    if ($code -ne 0) { throw ('dotnet failed: '+$Label) }
    $result.Checks += $Label
}
function Test-Project([string]$Project, [string[]]$Modes) {
    $file = Join-Path $inputs ('Backend/'+$Project+'/'+$Project+'.csproj')
    Invoke-Dotnet @('build',$file,'-c','Release','--nologo','--disable-build-servers','-p:UseSharedCompilation=false') ($Project+'.build')
    $dll = Join-Path $inputs ('Backend/'+$Project+'/bin/Release/net9.0/'+$Project+'.dll')
    foreach ($mode in $Modes) {
        $arguments = @($dll)
        if ($mode) { $arguments += $mode }
        Invoke-Dotnet $arguments ($Project+'.'+$mode.TrimStart('-'))
    }
}
try {
    Assert-Reservation
    Get-Command dotnet -ErrorAction Stop | Out-Null
    Get-Command node -ErrorAction Stop | Out-Null
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
    $result.Hashes['Scripts/Tests/Run-TradeMemorySessionTests.ps1'] = (Get-FileHash -LiteralPath $PSCommandPath).Hash
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
    Test-Project 'ProjectRpg.Backend.TradingTests' @('','--exchange-plan','--reservations-memory','--reservations-postgres','--sessions-memory','--sessions-postgres','--sessions-restart')
    Test-Project 'ProjectRpg.Backend.StarterEquipmentTests' @('--equipment-crafting-memory','--equipment-rewards-memory')
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
        if (Test-Path -LiteralPath $temp) { $result.Cleanup = Remove-TradeMemoryTemp $temp $tempRoot $runId }
    } catch { $failure=([string]$failure+'; cleanup: '+$_.Exception.Message) }
    $result.CleanupComplete = !(Test-Path -LiteralPath $temp)
    $result.Failure = $failure
    $result.Passed = $result.Passed -and !$failure -and $result.CleanupComplete
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $proof 'Result.json') -Encoding UTF8
    Write-Output ('Passed='+$result.Passed+'; Proof='+ (Join-Path $proof 'Result.json'))
}
if (!$result.Passed) { throw $failure }
