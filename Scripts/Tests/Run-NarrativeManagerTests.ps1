[CmdletBinding()]
param([switch]$Postgres)

$ErrorActionPreference = 'Stop'
$rpgRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskRoot = Join-Path $rpgRoot 'Saved/BuildChecks/NarrativeManagers-20261104'
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $taskRoot $runId
$tempRoot = Join-Path $runRoot 'Temp'
$inputs = Join-Path $tempRoot 'Inputs'
$artifacts = Join-Path $tempRoot 'artifacts'
$containerName = 'rpg-narrative-' + $runId
$containerId = $null
$priorConnection = $env:PROJECT_RPG_NARRATIVE_TEST_POSTGRES
$priorRun = $env:PROJECT_RPG_NARRATIVE_TEST_RUN
$result = [ordered]@{ Passed=$false; PostgresRequested=[bool]$Postgres; Run=$runId; Commands=@(); Inputs=@(); DeletedBytes=0L; CleanupComplete=$false; ContainerRemoved=$null; Failure=$null }
New-Item -ItemType Directory -Path $inputs -Force | Out-Null

function Assert-Reservation {
    $rows = @(Get-Content -LiteralPath (Join-Path $rpgRoot 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 |
        Where-Object { $_ -match '^\|.*narrative-managers-20261104' })
    if ($rows.Count -ne 1) { throw 'Narrative task reservation is missing.' }
}
function Invoke-Dotnet([string]$Arguments, [string]$LogName) {
    $result.Commands += ('dotnet ' + $Arguments.Replace($tempRoot, '[own Temp]'))
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = (Get-Command dotnet -ErrorAction Stop).Source
    $start.Arguments = $Arguments
    $start.WorkingDirectory = $inputs
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = 'Hidden'
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $child = New-Object Diagnostics.Process
    $child.StartInfo = $start
    try {
        if (-not $child.Start()) { throw 'dotnet did not start.' }
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        if (-not $child.WaitForExit(180000)) { $child.Kill(); $child.WaitForExit(); throw 'dotnet timed out.' }
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $runRoot $LogName), $output)
        if ($child.ExitCode -ne 0) { throw ('dotnet failed; inspect ' + $LogName) }
        Write-Output ('PASS ' + $LogName)
    }
    finally {
        if ($child.Id -and -not $child.HasExited) { $child.Kill(); $child.WaitForExit() }
        $child.Dispose()
    }
}
function Get-OwnedContainer {
    $found = @(& docker container ls --all --filter ('name=^/' + $containerName + '$') --format '{{.ID}}')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect own container.' }
    if ($found.Count -eq 0) { return $null }
    if ($found.Count -ne 1) { throw 'Ambiguous container identity.' }
    $info = ((& docker inspect $found[0]) | ConvertFrom-Json)[0]
    if ($LASTEXITCODE -ne 0 -or $info.Name -cne ('/' + $containerName) -or
        $info.Config.Labels.'project-rpg.run' -cne $runId -or
        $info.Config.Labels.'project-rpg.task' -cne 'narrative-managers-20261104' -or
        ($containerId -and $info.Id -cne $containerId)) { throw 'Container ownership mismatch.' }
    return $info
}
try {
    Assert-Reservation
    # Only backend build inputs. No UE outputs, user settings or shared runtime files.
    $directories = @('ProjectRpg.Backend','ProjectRpg.Operations.Core','ProjectRpg.Backend.NarrativeTests',
        'ProjectRpg.Backend.MigrationTests','ProjectRpg.Backend.StarterEquipmentTests','Database')
    foreach ($directory in $directories) {
        $source = Join-Path $rpgRoot ('Backend/' + $directory)
        $files = @(Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object {
            $_.FullName -notmatch '[\\/](bin|obj)[\\/]' -and $_.Extension -in @('.cs','.cshtml','.csproj','.sql','.json')
        })
        foreach ($file in $files) {
            if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing linked input.' }
            $relative = $file.FullName.Substring($rpgRoot.Length + 1)
            $destination = Join-Path $inputs $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination
            $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            $result.Inputs += @{ Path=$relative; SHA256=$hash }
        }
    }
    if ($Postgres) {
        & docker image inspect postgres:17-alpine --format '{{.Id}}' *> $null
        if ($LASTEXITCODE -ne 0) { throw 'Cached postgres:17-alpine is required.' }
        $password = [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
        $created = & docker run --pull=never --detach --name $containerName --label ('project-rpg.run=' + $runId) --label 'project-rpg.task=narrative-managers-20261104' --memory 512m --cpus 1 --pids-limit 128 --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env 'POSTGRES_DB=narrative_test' --env 'POSTGRES_USER=narrative_test' --env ('POSTGRES_PASSWORD=' + $password) postgres:17-alpine -c shared_buffers=16MB -c max_connections=32
        if ($LASTEXITCODE -ne 0) { throw 'Owned PostgreSQL startup failed.' }
        $containerId = ([string]$created).Trim()
        $info = Get-OwnedContainer
        if (@($info.Mounts | Where-Object Type -ne 'tmpfs').Count -ne 0) { throw 'Unexpected persistent container mounts.' }
        $ready = $false
        for ($i=0; $i -lt 40; $i++) {
            & docker exec $containerId pg_isready -U narrative_test -d narrative_test *> $null
            if ($LASTEXITCODE -eq 0) { $ready=$true; break }
            Start-Sleep -Seconds 1
        }
        if (-not $ready) { throw 'Owned PostgreSQL did not become ready.' }
        $binding = [string](& docker port $containerId 5432/tcp)
        if ($LASTEXITCODE -ne 0 -or $binding.Trim() -notmatch '^127\.0\.0\.1:(\d+)$') { throw 'Unexpected DB binding.' }
        $env:PROJECT_RPG_NARRATIVE_TEST_POSTGRES = 'Host=127.0.0.1;Port=' + $Matches[1] + ';Database=narrative_test;Username=narrative_test;Password=' + $password
        $env:PROJECT_RPG_NARRATIVE_TEST_RUN = $runId
    }
    Invoke-Dotnet ('build "Backend/ProjectRpg.Backend.NarrativeTests/ProjectRpg.Backend.NarrativeTests.csproj" --artifacts-path "' + $artifacts + '" -v minimal') 'Build.log'
    $testDll = Join-Path $artifacts 'bin/ProjectRpg.Backend.NarrativeTests/debug/ProjectRpg.Backend.NarrativeTests.dll'
    $testArgs = '"' + $testDll + '"'
    if ($Postgres) { $testArgs += ' --postgres' }
    Invoke-Dotnet $testArgs 'Narrative.log'
    Invoke-Dotnet ('build "Backend/ProjectRpg.Backend.MigrationTests/ProjectRpg.Backend.MigrationTests.csproj" --artifacts-path "' + $artifacts + '" -v minimal') 'MigrationBuild.log'
    Invoke-Dotnet ('"' + (Join-Path $artifacts 'bin/ProjectRpg.Backend.MigrationTests/debug/ProjectRpg.Backend.MigrationTests.dll') + '"') 'Migration.log'
    Invoke-Dotnet ('build "Backend/ProjectRpg.Backend.StarterEquipmentTests/ProjectRpg.Backend.StarterEquipmentTests.csproj" --artifacts-path "' + $artifacts + '" -v minimal') 'EquipmentBuild.log'
    Invoke-Dotnet ('"' + (Join-Path $artifacts 'bin/ProjectRpg.Backend.StarterEquipmentTests/debug/ProjectRpg.Backend.StarterEquipmentTests.dll') + '" --equipment-persistence-only') 'Equipment.log'
    foreach ($input in $result.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $rpgRoot $input.Path) -Algorithm SHA256).Hash -cne $input.SHA256) {
            throw ('Source changed during verification: ' + $input.Path)
        }
    }
    $result.Passed = $true
}
catch { $result.Failure = $_.Exception.Message; throw }
finally {
    try {
        if ($Postgres -and $containerId) {
            $info = Get-OwnedContainer
            if ($null -ne $info) {
                & docker rm --force $info.Id | Out-Null
                if ($LASTEXITCODE -ne 0) { throw 'Own container cleanup failed.' }
            }
            $result.ContainerRemoved = $true
        }
        $env:PROJECT_RPG_NARRATIVE_TEST_POSTGRES = $priorConnection
        $env:PROJECT_RPG_NARRATIVE_TEST_RUN = $priorRun
        Assert-Reservation
        $resolved = [IO.Path]::GetFullPath($tempRoot)
        $boundary = [IO.Path]::GetFullPath($runRoot).TrimEnd('\') + '\'
        if (-not $resolved.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -cne 'Temp') { throw 'Unsafe cleanup target.' }
        if (Test-Path -LiteralPath $resolved) {
            $entries = @(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
            if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count -ne 0) { throw 'Cleanup contains a reparse point.' }
            $result.DeletedBytes = [long](($entries | Where-Object { -not $_.PSIsContainer } | Measure-Object Length -Sum).Sum)
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
        $result.CleanupComplete = -not (Test-Path -LiteralPath $resolved)
    }
    finally { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'Result.json') -Encoding UTF8 }
    Write-Output ('Result: ' + (Join-Path $runRoot 'Result.json'))
}
