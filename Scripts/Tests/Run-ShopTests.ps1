[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$proofRoot = Join-Path $repoRoot 'Saved/BuildChecks/Shop-20261117'
$runId = [Guid]::NewGuid().ToString('N')
$tempParent = Join-Path $proofRoot 'Temp'
$tempRoot = Join-Path $tempParent $runId
$resultRoot = Join-Path $proofRoot $runId
$roadmapPath = Join-Path $repoRoot 'Docs/Online_RPG_Development_Roadmap.md'
$sourcePaths = @(
    'Backend/ProjectRpg.Backend/Program.cs',
    'Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj',
    'Backend/ProjectRpg.Backend/appsettings.json',
    'Backend/ProjectRpg.Backend/appsettings.Development.json',
    'Backend/ProjectRpg.Backend/Data/PostgresSchemaMigrations.cs',
    'Backend/Database/Migrations/0017_shop_transactions.sql',
    'Backend/ProjectRpg.Backend.MigrationTests/Program.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryTransactionGate.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.Shop.cs',
    'Scripts/Tests/Run-ShopTests.ps1'
)
foreach ($relativeRoot in @('Backend/ProjectRpg.Backend/Shop', 'Backend/ProjectRpg.Backend.ShopTests')) {
    $sourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $repoRoot $relativeRoot) -File | Where-Object { $_.Extension -in @('.cs', '.csproj', '.json') } | ForEach-Object { $relativeRoot + '/' + $_.Name })
}
$result = [ordered]@{ RunId = $runId; Status = 'Failed'; BaseCommit = ''; SourceHashes = @{};
    BuildExit = $null; TestExit = $null; DeletedBytes = 0; TempRemoved = $false;
    Scope = 'Isolated backend shop domain, memory and HTTP contract tests. PostgreSQL and UE require separate evidence.' }

function Invoke-DotnetLogged([string[]]$Arguments, [string]$LogPath) {
    # Windows PowerShell represents native stderr as ErrorRecord. Preserve the entire log and exit code.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & dotnet @Arguments *> $LogPath
        return $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
}

# Do not traverse a redirected output root, even before creating the run directory.
$ancestor = $tempRoot
while ($ancestor -and $ancestor -ne $repoRoot) {
    if ((Test-Path -LiteralPath $ancestor) -and
        ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Reparse output ancestor rejected: $ancestor"
    }
    $ancestor = Split-Path -Parent $ancestor
}
New-Item -ItemType Directory -Path $tempRoot, $resultRoot -Force | Out-Null
try {
    Get-Command dotnet -ErrorAction Stop | Out-Null
    $result.BaseCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read snapshot commit.' }
    $zipPath = Join-Path $tempRoot 'backend.zip'
    & git -C $repoRoot archive --format=zip --output=$zipPath $result.BaseCommit Backend
    if ($LASTEXITCODE -ne 0) { throw 'Backend archive failed.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $tempRoot)
    foreach ($relative in $sourcePaths) {
        $source = Join-Path $repoRoot $relative
        $result.SourceHashes[$relative] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $destination = Join-Path $tempRoot $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $result.SourceHashes[$relative]) {
            throw "Source changed while taking snapshot: $relative"
        }
    }
    $project = Join-Path $tempRoot 'Backend/ProjectRpg.Backend.ShopTests/ProjectRpg.Backend.ShopTests.csproj'
    $result['BuildCommand'] = 'dotnet build <snapshot>/Backend/ProjectRpg.Backend.ShopTests -c Release --disable-build-servers -p:UseSharedCompilation=false'
    $result.BuildExit = Invoke-DotnetLogged -Arguments @('build', $project, '-c', 'Release', '--disable-build-servers', '-p:UseSharedCompilation=false') -LogPath (Join-Path $resultRoot 'build.log')
    if ($result.BuildExit -ne 0) {
        Get-Content -LiteralPath (Join-Path $resultRoot 'build.log') -Tail 35
        throw 'Shop build failed.'
    }
    $dll = Join-Path $tempRoot 'Backend/ProjectRpg.Backend.ShopTests/bin/Release/net9.0/ProjectRpg.Backend.ShopTests.dll'
    $result['ModuleHash'] = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    $result['TestCommand'] = 'dotnet <snapshot>/Backend/ProjectRpg.Backend.ShopTests/bin/Release/net9.0/ProjectRpg.Backend.ShopTests.dll'
    $result.TestExit = Invoke-DotnetLogged -Arguments @($dll) -LogPath (Join-Path $resultRoot 'tests.log')
    Get-Content -LiteralPath (Join-Path $resultRoot 'tests.log')
    if ($result.TestExit -ne 0) { throw 'Shop scenarios failed.' }
    $migrationProject = Join-Path $tempRoot 'Backend/ProjectRpg.Backend.MigrationTests/ProjectRpg.Backend.MigrationTests.csproj'
    $result['MigrationBuildExit'] = Invoke-DotnetLogged -Arguments @('build', $migrationProject, '-c', 'Release', '--disable-build-servers', '-p:UseSharedCompilation=false') -LogPath (Join-Path $resultRoot 'migration-build.log')
    if ($result.MigrationBuildExit -ne 0) { throw 'Packaged migration build failed.' }
    $migrationDll = Join-Path $tempRoot 'Backend/ProjectRpg.Backend.MigrationTests/bin/Release/net9.0/ProjectRpg.Backend.MigrationTests.dll'
    $result['MigrationTestExit'] = Invoke-DotnetLogged -Arguments @($migrationDll) -LogPath (Join-Path $resultRoot 'migration-tests.log')
    Get-Content -LiteralPath (Join-Path $resultRoot 'migration-tests.log')
    if ($result.MigrationTestExit -ne 0) { throw 'Packaged migration checks failed.' }
    foreach ($relative in $sourcePaths) {
        if ((Get-FileHash -LiteralPath (Join-Path $repoRoot $relative) -Algorithm SHA256).Hash -ne $result.SourceHashes[$relative]) {
            throw "Verified source changed: $relative"
        }
    }
    $result.Status = 'Passed'
}
catch {
    $result['Error'] = $_.Exception.Message
    throw
}
finally {
    try {
        # All native commands above are synchronous; build servers are disabled. Never touch shared outputs.
        $resolved = [IO.Path]::GetFullPath($tempRoot)
        $allowed = [IO.Path]::GetFullPath($tempParent).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path -Leaf $resolved) -ne $runId) { throw 'Cleanup path is outside the owned run.' }
        $reservations = Get-Content -LiteralPath $roadmapPath -Encoding UTF8
        if ($reservations | Where-Object { $_ -match '^\|' -and $_ -notmatch 'shop-mvp-20261117' -and $_.Contains($runId) }) {
            throw 'Another owner references this run; cleanup requires handoff.'
        }
        if (Test-Path -LiteralPath $resolved) {
            # Enumerate without following links and refuse deletion if an unexpected link was introduced.
            $pending = [Collections.Generic.Stack[string]]::new()
            $pending.Push($resolved)
            [long]$bytes = 0
            while ($pending.Count -gt 0) {
                $directory = $pending.Pop()
                if ((Get-Item -LiteralPath $directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw 'Cleanup refused an unexpected reparse point.'
                }
                foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
                    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cleanup refused an unexpected reparse point.' }
                    if ($entry.PSIsContainer) { $pending.Push($entry.FullName) } else { $bytes += $entry.Length }
                }
            }
            Remove-Item -LiteralPath $resolved -Recurse -Force
            $result.DeletedBytes = $bytes
        }
        $result.TempRemoved = -not (Test-Path -LiteralPath $resolved)
    }
    catch {
        $result.Status = 'Failed'
        $result['CleanupError'] = $_.Exception.Message
        throw
    }
    finally {
        $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resultRoot 'Result.json') -Encoding UTF8
        Write-Output ("Shop proof: " + (Join-Path $resultRoot 'Result.json'))
        Write-Output ("Owned temporary bytes deleted: " + $result.DeletedBytes)
    }
}
