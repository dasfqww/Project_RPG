[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$proofRoot = Join-Path $repoRoot 'Saved/BuildChecks/TradingFoundation-20261106'
$runId = [Guid]::NewGuid().ToString('N')
$tempParent = Join-Path $proofRoot 'Temp'
$tempRoot = Join-Path $tempParent $runId
$resultRoot = Join-Path $proofRoot $runId
$roadmapPath = Join-Path $repoRoot 'Docs/Online_RPG_Development_Roadmap.md'
$sourcePaths = @(
    'Backend/ProjectRpg.Backend/Domain/Trading/TradeOffer.cs',
    'Backend/ProjectRpg.Backend/Domain/Trading/TradeItemPolicy.cs',
    'Backend/ProjectRpg.Backend/Domain/Trading/TradeNegotiation.cs',
    'Backend/ProjectRpg.Backend.TradingTests/ProjectRpg.Backend.TradingTests.csproj',
    'Backend/ProjectRpg.Backend.TradingTests/Program.cs',
    'Scripts/Tests/Run-TradingTests.ps1'
)
$result = [ordered]@{ RunId = $runId; Status = 'Failed'; BaseCommit = ''; SourceHashes = @{};
    BuildExit = $null; TestExit = $null; DeletedBytes = 0; TempRemoved = $false;
    Scope = 'Backend build and trading domain scenarios only. No PostgreSQL, HTTP, UE or deployed trading.' }

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
    $project = Join-Path $tempRoot 'Backend/ProjectRpg.Backend.TradingTests/ProjectRpg.Backend.TradingTests.csproj'
    $result['BuildCommand'] = 'dotnet build <snapshot>/Backend/ProjectRpg.Backend.TradingTests -c Release --disable-build-servers -p:UseSharedCompilation=false'
    $result.BuildExit = Invoke-DotnetLogged -Arguments @('build', $project, '-c', 'Release', '--disable-build-servers', '-p:UseSharedCompilation=false') -LogPath (Join-Path $resultRoot 'build.log')
    if ($result.BuildExit -ne 0) {
        Get-Content -LiteralPath (Join-Path $resultRoot 'build.log') -Tail 35
        throw 'Trading build failed.'
    }
    $dll = Join-Path $tempRoot 'Backend/ProjectRpg.Backend.TradingTests/bin/Release/net9.0/ProjectRpg.Backend.TradingTests.dll'
    $result['ModuleHash'] = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    $result['TestCommand'] = 'dotnet <snapshot>/Backend/ProjectRpg.Backend.TradingTests/bin/Release/net9.0/ProjectRpg.Backend.TradingTests.dll'
    $result.TestExit = Invoke-DotnetLogged -Arguments @($dll) -LogPath (Join-Path $resultRoot 'tests.log')
    Get-Content -LiteralPath (Join-Path $resultRoot 'tests.log')
    if ($result.TestExit -ne 0) { throw 'Trading scenarios failed.' }
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
        if ($reservations | Where-Object { $_ -match '^\|' -and $_ -notmatch 'trading-foundation-20261106' -and $_.Contains($runId) }) {
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
        Write-Output ("Trading proof: " + (Join-Path $resultRoot 'Result.json'))
        Write-Output ("Owned temporary bytes deleted: " + $result.DeletedBytes)
    }
}
