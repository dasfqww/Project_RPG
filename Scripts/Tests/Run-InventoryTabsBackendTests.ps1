[CmdletBinding()]
param([switch]$MemoryOnly)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id = [Guid]::NewGuid().ToString('N')
$tempRoot = Join-Path $workspace 'Saved/IT'
$temp = Join-Path $tempRoot ('be'+$id.Substring(0,8))
$proofRoot = Join-Path $workspace ('Saved/BuildChecks/InventoryTabs-20261108/Backend/'+$id)
$container = 'rpg-inventory-tabs-'+$id.Substring(0,8)
$createdContainer = $false
$proof = [ordered]@{Passed=$false; Inputs=@(); Failure=$null; Cleanup=$null; Postgres=(!$MemoryOnly)}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
$null = [IO.Directory]::CreateDirectory($proofRoot)
try {
    if (@(Get-Content (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match '^\|[^|]*/ inventory-tabs-20261108 \|'}).Count -ne 1) { throw 'Reservation missing' }
    if (Test-Path -LiteralPath $temp) { throw 'Temporary path already exists' }
    $null = [IO.Directory]::CreateDirectory($temp)
    $null = [IO.Directory]::CreateDirectory($proofRoot)
    $proof.Head = (git -C $workspace rev-parse HEAD).Trim()
    & git -C $workspace archive --format=tar ('-o'+(Join-Path $temp 'Backend.tar')) $proof.Head -- Backend
    if ($LASTEXITCODE -ne 0) { throw 'Source archive failed' }
    & tar -xf (Join-Path $temp 'Backend.tar') -C $temp
    if ($LASTEXITCODE -ne 0) { throw 'Source extraction failed' }
    $inputs = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'InventoryTabsInputs.json') -Raw -Encoding UTF8 | ConvertFrom-Json).Backend
    foreach ($relative in $inputs) {
        if (!$relative.StartsWith('Backend/') -or $relative.Contains('..')) { throw 'Invalid input path' }
        $source = Join-Path $workspace $relative
        $destination = Join-Path $temp $relative
        $hash = (Get-FileHash -LiteralPath $source).Hash
        $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $source).Hash -ne $hash -or (Get-FileHash -LiteralPath $destination).Hash -ne $hash) { throw 'Input changed during capture' }
        $proof.Inputs += @{Path=$relative; SHA256=$hash}
    }
    $project = Join-Path $temp 'Backend/ProjectRpg.Backend.InventoryTabsTests/ProjectRpg.Backend.InventoryTabsTests.csproj'
    $artifacts = Join-Path $temp 'Artifacts'
    & dotnet build $project -c Release --artifacts-path $artifacts --nologo *> (Join-Path $proofRoot 'Build.log')
    $proof.BuildExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Backend build failed' }
    if (!$MemoryOnly) {
        & docker run -d --rm --name $container -e POSTGRES_PASSWORD=inventory-test-only -e POSTGRES_DB=inventory_tabs -p '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=134217728' postgres:17-alpine *> (Join-Path $proofRoot 'PostgresStart.log')
        if ($LASTEXITCODE -ne 0) { throw 'Private PostgreSQL start failed' }
        $createdContainer = $true
        $ready = $false
        for ($attempt = 0; $attempt -lt 100; ++$attempt) {
            & docker exec $container pg_isready -U postgres -d inventory_tabs *> $null
            if ($LASTEXITCODE -eq 0) { $ready=$true; break }
            Start-Sleep -Milliseconds 200
        }
        if (!$ready) { throw 'Private PostgreSQL readiness failed' }
        $portText = (& docker port $container '5432/tcp').Trim()
        if ($portText -notmatch '^127\.0\.0\.1:(\d+)$') { throw 'Unexpected private port binding' }
        $env:PROJECTRPG_INVENTORY_TEST_DB = 'Host=127.0.0.1;Port='+$Matches[1]+';Database=inventory_tabs;Username=postgres;Password=inventory-test-only;Pooling=false'
    }
    $testDll = Join-Path $artifacts 'bin/ProjectRpg.Backend.InventoryTabsTests/release/ProjectRpg.Backend.InventoryTabsTests.dll'
    & dotnet $testDll *> (Join-Path $proofRoot 'Tests.log')
    $proof.TestExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Backend inventory tab checks failed' }
    foreach ($entry in $proof.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash -ne $entry.SHA256) { throw 'Source changed during validation' }
    }
    $proof.Passed = $true
} catch { $proof.Failure=$_.Exception.Message }
finally {
    Remove-Item Env:PROJECTRPG_INVENTORY_TEST_DB -ErrorAction SilentlyContinue
    if ($createdContainer) {
        & docker stop $container *> (Join-Path $proofRoot 'PostgresStop.log')
        if ($LASTEXITCODE -ne 0) { $proof.Passed=$false; $proof.Failure='Private PostgreSQL cleanup failed' }
    }
    if (Test-Path -LiteralPath $temp) {
        try { $proof.Cleanup = Remove-GuardianProbeArtifact $temp $tempRoot }
        catch { $proof.Passed=$false; $proof.Failure=$_.Exception.Message }
    }
    $proof.CleanupComplete = !(Test-Path -LiteralPath $temp)
    $proof.Passed = $proof.Passed -and $proof.CleanupComplete
    [IO.File]::WriteAllText((Join-Path $proofRoot 'Result.json'), ($proof | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    Write-Output ('Passed='+$proof.Passed+'; Proof='+$proofRoot)
}
if (!$proof.Passed) { throw $proof.Failure }
