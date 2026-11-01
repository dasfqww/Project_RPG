[CmdletBinding()]
param([switch]$Postgres, [switch]$PostgresOnly, [switch]$IsolatedPostgres)

$ErrorActionPreference = 'Stop'
$rpgRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$taskRoot = Join-Path $rpgRoot 'Saved\BuildChecks\PostgresVersionedStartup-20261030'
$runRoot = Join-Path $taskRoot ([Guid]::NewGuid().ToString('N'))
$runId = Split-Path -Leaf $runRoot
$tempRoot = Join-Path $runRoot 'Temp'
$inputRoot = Join-Path $tempRoot 'Inputs'
$artifacts = Join-Path $tempRoot 'artifacts'
$dotnetPath = (Get-Command dotnet -ErrorAction Stop).Source
if ($Postgres -and ($PostgresOnly -or $IsolatedPostgres)) { throw 'Choose combined -Postgres OR PG-only mode.' }
if ($IsolatedPostgres) { $PostgresOnly = $true }
$rpgResult = [ordered]@{ Passed = $false; PostgresRequested = [bool]($Postgres -or $PostgresOnly); PostgresOnly = [bool]$PostgresOnly; IsolatedPostgres = [bool]$IsolatedPostgres; StartedUtc = [DateTime]::UtcNow.ToString('o'); Commands = @(); Inputs = @(); Modules = @(); DeletedBytes = 0L; CleanupComplete = $false; ContainerRemoved = $null; Failure = $null }
$containerName = 'rpg-migration-' + $runId
$containerId = $null
$priorTestConnection = $env:PROJECT_RPG_SCHEMA_TEST_POSTGRES
$priorIsolatedRun = $env:PROJECT_RPG_MIGRATION_TEST_ISOLATED_RUN

function Assert-Reservation {
    $row = Get-Content -LiteralPath (Join-Path $rpgRoot 'Docs\Online_RPG_Development_Roadmap.md') -Encoding UTF8 |
        Where-Object { $_ -match '^\|.*postgres-versioned-startup-20261030' }
    $activeText = [string][char]0xC9C4 + [char]0xD589 + ' ' + [char]0xC911
    if (@($row).Count -ne 1 -or -not $row.Contains($activeText)) { throw 'Active task reservation is missing.' }
}

function Get-OwnContainer {
    # `docker inspect missing` writes stderr, which is terminating under PS5's
    # ErrorActionPreference=Stop even when redirected. A missing owned container
    # is a normal pre-creation/already-cleaned state, not a cleanup failure.
    $found = @(& docker container ls --all --filter ('name=^/' + $containerName + '$') --format '{{.ID}}')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot query own container; cleanup state is unknown.' }
    if ($found.Count -eq 0) { return $null }
    if ($found.Count -ne 1) { throw 'Ambiguous owned container identity.' }
    $raw = & docker inspect $found[0]
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect own container.' }
    $info = ($raw | ConvertFrom-Json)[0]
    if ($info.Name -cne ('/' + $containerName) -or $info.Config.Labels.'project-rpg.run' -cne $runId -or
        $info.Config.Labels.'project-rpg.task' -cne 'postgres-versioned-startup-20261030') {
        throw 'Container identity/ownership mismatch; do not stop or remove it.'
    }
    return $info
}

function Start-OwnPostgres {
    Assert-Reservation
    $image = & docker image inspect postgres:17-alpine --format '{{.Id}}' 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'Cached postgres:17-alpine is required; this runner never pulls or starts shared DBs.' }
    $rpgResult.PostgresImage = [string]$image
    $password = [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
    $created = & docker run --pull=never --detach --name $containerName --label ('project-rpg.run=' + $runId) --label 'project-rpg.task=postgres-versioned-startup-20261030' --memory 512m --cpus 1 --pids-limit 128 --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env 'POSTGRES_DB=migration_test' --env 'POSTGRES_USER=migration_test' --env ('POSTGRES_PASSWORD=' + $password) postgres:17-alpine -c shared_buffers=16MB -c max_connections=32
    if ($LASTEXITCODE -ne 0) { throw 'Could not create own temporary PostgreSQL container.' }
    $script:containerId = ([string]$created).Trim()
    $info = Get-OwnContainer
    if ($info.Id -cne $containerId -or @($info.Mounts | Where-Object Type -ne 'tmpfs').Count -ne 0) {
        throw 'Unexpected container identity or persistent mounts.'
    }
    $rpgResult.ContainerId = $containerId
    $rpgResult.ContainerName = $containerName
    $rpgResult.Commands += 'docker run --pull=never --memory 512m --cpus 1 --pids-limit 128 --publish 127.0.0.1::5432 --tmpfs /var/lib/postgresql/data:rw,size=201326592 [owned labels; temporary credential redacted] postgres:17-alpine -c shared_buffers=16MB -c max_connections=32'
    $ready = $false
    for ($attempt = 0; $attempt -lt 45; $attempt++) {
        & docker exec $containerId pg_isready -h 127.0.0.1 -U migration_test -d migration_test *> $null
        if ($LASTEXITCODE -eq 0) { $ready = $true; break }
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw 'Owned temporary PostgreSQL did not become ready.' }
    $binding = [string](& docker port $containerId 5432/tcp)
    if ($LASTEXITCODE -ne 0 -or $binding.Trim() -notmatch '^127\.0\.0\.1:(\d+)$') { throw 'Unexpected DB binding.' }
    $port = [int]$Matches[1]
    $env:PROJECT_RPG_SCHEMA_TEST_POSTGRES = 'Host=127.0.0.1;Port=' + $port + ';Database=migration_test;Username=migration_test;Password=' + $password
    $env:PROJECT_RPG_MIGRATION_TEST_ISOLATED_RUN = $runId
    $rpgResult.PostgresPort = $port
    Write-Output ('Owned PostgreSQL ready on loopback port ' + $port + ' (tmpfs; no existing DB/volume).')
}

function Remove-OwnPostgres {
    if (-not $IsolatedPostgres) { return }
    Assert-Reservation
    $info = Get-OwnContainer
    if ($null -eq $info) { $rpgResult.ContainerRemoved = $true; return }
    if ($containerId -and $info.Id -cne $containerId) { throw 'Container ID changed; cleanup refused.' }
    try {
        $dbBytes = & docker exec $info.Id psql -U migration_test -d migration_test -At -c 'SELECT pg_database_size(current_database());' 2>$null
        if ($LASTEXITCODE -eq 0 -and [string]$dbBytes -match '^\d+$') { $rpgResult.TmpfsDatabaseBytesAtCleanup = [long]$dbBytes }
    }
    catch { $rpgResult.TmpfsSizeUnavailable = $true } # Metrics must not prevent removal after DB startup failure.
    & docker rm --force $info.Id | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Own PostgreSQL cleanup failed.' }
    $rpgResult.ContainerRemoved = $true
}

function Invoke-RecordedProcess([string]$Arguments, [string]$LogName, [int]$TimeoutSeconds = 180) {
    $rpgResult.Commands += ('dotnet ' + $Arguments)
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $dotnetPath
    $start.Arguments = $Arguments
    $start.WorkingDirectory = $inputRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = 'Hidden'
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $child = New-Object Diagnostics.Process
    $child.StartInfo = $start
    $started = $false
    try {
        if (-not $child.Start()) { throw 'Could not start own verification child.' }
        $started = $true
        $outRead = $child.StandardOutput.ReadToEndAsync()
        $errRead = $child.StandardError.ReadToEndAsync()
        if (-not $child.WaitForExit($TimeoutSeconds * 1000)) { throw 'Own verification child exceeded deadline.' }
        $child.WaitForExit()
        $output = $outRead.GetAwaiter().GetResult() + $errRead.GetAwaiter().GetResult()
        $output | Set-Content -LiteralPath (Join-Path $runRoot $LogName) -Encoding UTF8
        if ($child.ExitCode -ne 0) { throw ('Verification command failed: ' + $LogName + ', exit ' + $child.ExitCode) }
        return $output
    }
    finally {
        if ($started -and -not $child.HasExited) {
            # Only this function's directly created child; never terminate shared tools.
            & taskkill.exe /PID $child.Id /T /F | Out-Null
            if (-not $child.WaitForExit(10000)) { throw 'Owned process has not exited; do not delete its inputs.' }
        }
        $child.Dispose()
    }
}

function Remove-OwnTemp {
    Assert-Reservation
    $resolved = [IO.Path]::GetFullPath($tempRoot)
    $expected = [IO.Path]::GetFullPath((Join-Path $runRoot 'Temp'))
    if ($resolved -cne $expected -or -not $resolved.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unsafe temporary cleanup boundary.'
    }
    if (-not (Test-Path -LiteralPath $resolved)) { $rpgResult.CleanupComplete = $true; return }
    # Inspect each directory without following any junction/symlink. These build
    # outputs should contain none; refuse rather than recurse into a new link.
    $pending = New-Object 'Collections.Generic.Stack[string]'
    $pending.Push($resolved)
    $bytes = 0L
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        if ((Get-Item -LiteralPath $directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Unexpected reparse point; automatic cleanup refused.'
        }
        foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected link in own build outputs.' }
            if ($entry.PSIsContainer) { $pending.Push($entry.FullName) }
            else {
                # Ensure no live process is holding the artifact before deleting.
                $probe = [IO.File]::Open($entry.FullName, 'Open', 'Read', 'None')
                $probe.Dispose()
                $bytes += $entry.Length
            }
        }
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
    $rpgResult.DeletedBytes = $bytes
    $rpgResult.CleanupComplete = -not (Test-Path -LiteralPath $resolved)
}

New-Item -ItemType Directory -Path $inputRoot -Force | Out-Null
try {
    Assert-Reservation
    Push-Location $rpgRoot
    try {
        $sources = @(& rg --files Backend/ProjectRpg.Backend Backend/ProjectRpg.Backend.MigrationTests -g '*.cs' -g '*.csproj' -g '*.json')
        if ($LASTEXITCODE -ne 0) { throw 'Source inventory failed.' }
    }
    finally { Pop-Location }
    $sources += 'Backend/Database/schema.sql'
    foreach ($relative in ($sources | Sort-Object -Unique)) {
        $original = Join-Path $rpgRoot $relative
        $copy = Join-Path $inputRoot $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
        $before = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $original -Destination $copy
        if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $before -or
            (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ne $before) { throw 'Input changed during snapshot.' }
        $rpgResult.Inputs += [ordered]@{ Path = $relative; Sha256 = $before }
    }
    $project = Join-Path $inputRoot 'Backend\ProjectRpg.Backend.MigrationTests\ProjectRpg.Backend.MigrationTests.csproj'
    Write-Output ('Building isolated snapshot: ' + $runRoot)
    Invoke-RecordedProcess ('build "' + $project + '" --configuration Release --artifacts-path "' + $artifacts + '" --nologo -p:UseSharedCompilation=false') 'Build.log' | Out-Null
    $assembly = Join-Path $artifacts 'bin\ProjectRpg.Backend.MigrationTests\release\ProjectRpg.Backend.MigrationTests.dll'
    $backend = Join-Path $artifacts 'bin\ProjectRpg.Backend\release\ProjectRpg.Backend.dll'
    foreach ($module in @($assembly, $backend)) {
        $rpgResult.Modules += [ordered]@{ Name = [IO.Path]::GetFileName($module); Sha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash }
    }
    Assert-Reservation
    if ($IsolatedPostgres) { Start-OwnPostgres }
    $arguments = '"' + $assembly + '"'
    if ($Postgres) { $arguments += ' --postgres' }
    if ($PostgresOnly) { $arguments += ' --postgres-only' }
    $testOutput = Invoke-RecordedProcess $arguments 'Tests.log'
    Write-Output $testOutput
    foreach ($entry in $rpgResult.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $rpgRoot $entry.Path) -Algorithm SHA256).Hash -ne $entry.Sha256) {
            throw 'Original inputs changed after the frozen build; do not claim current-tree verification.'
        }
    }
    $rpgResult.Passed = $true
}
catch {
    $rpgResult.Failure = $_.Exception.Message
    throw
}
finally {
    try { Remove-OwnPostgres }
    catch { $rpgResult.Passed = $false; $rpgResult.CleanupFailure = $_.Exception.Message; throw }
    finally {
        $env:PROJECT_RPG_SCHEMA_TEST_POSTGRES = $priorTestConnection
        $env:PROJECT_RPG_MIGRATION_TEST_ISOLATED_RUN = $priorIsolatedRun
        try { Remove-OwnTemp }
        catch { $rpgResult.Passed = $false; $rpgResult.CleanupFailure = $_.Exception.Message; throw }
        finally {
            $rpgResult.CompletedUtc = [DateTime]::UtcNow.ToString('o')
            $rpgResult | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $runRoot 'Result.json') -Encoding UTF8
            Write-Output ('Result: ' + (Join-Path $runRoot 'Result.json'))
            Write-Output ('Temporary bytes removed: ' + $rpgResult.DeletedBytes)
        }
    }
}
