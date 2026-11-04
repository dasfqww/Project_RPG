# Freeze local backend source/config/schema before compiling the owned E2E host.
# This is input provenance, not a sandbox or a claim of a hermetic SDK/package cache.
function New-NPCPickupBackendSnapshot {
    param([string]$ProjectRoot,[string]$SnapshotRoot,[switch]$ReuseRestoreCache)
    $projectPath=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\','/')
    $destination=[IO.Path]::GetFullPath($SnapshotRoot).TrimEnd('\','/')
    $allowed=[IO.Path]::GetFullPath((Join-Path $projectPath 'Saved\Tests\NPCPickupBackend')).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if (-not $destination.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or
        (Test-Path -LiteralPath $destination)) { throw 'Backend snapshot requires a new owned NPC pickup test directory.' }
    for ($parent=Split-Path $destination -Parent; $parent; $parent=Split-Path $parent -Parent) {
        if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Reparse point in backend snapshot destination.'
        }
        foreach ($name in @('Directory.Build.props','Directory.Build.targets','Directory.Packages.props')) {
            if (Test-Path -LiteralPath (Join-Path $parent $name)) { throw 'Uncaptured snapshot-parent MSBuild input.' }
        }
    }
    $source=Join-Path $projectPath 'Backend\ProjectRpg.Backend'
    $projectFile=Join-Path $source 'ProjectRpg.Backend.csproj'
    [xml]$projectXml=Get-Content -Raw -LiteralPath $projectFile
    $allowedContent=@('../Database/schema.sql','../Database/Migrations/0002_item_equipment_progression.sql',
        '../Database/Migrations/0003_character_narrative.sql','../Database/Migrations/0004_operations_sessions.sql','../Database/Migrations/0005_operations_evidence.sql','../Database/Migrations/0006_operations_compensation.sql')
    $references=@($projectXml.SelectNodes('//ProjectReference'))
    $core=Join-Path $projectPath 'Backend/ProjectRpg.Operations.Core'
    $coreProject=Join-Path $core 'ProjectRpg.Operations.Core.csproj'
    $invalidReferences=@($references | Where-Object {[IO.Path]::GetFullPath((Join-Path $source $_.Include)) -cne $coreProject})
    if($references.Count){
        [xml]$coreXml=Get-Content -Raw -LiteralPath $coreProject
        if($coreXml.SelectNodes('//ProjectReference|//Import|//Compile|//Content[@Include]').Count){throw 'Uncaptured operations core dependencies.'}
    }
    $contentInputs=@($projectXml.SelectNodes('//Content[@Include]'))
    if ($invalidReferences.Count -or $projectXml.SelectNodes('//Import|//Compile').Count -or
        @($contentInputs | Where-Object {$_.Include.Replace('\','/') -cnotin $allowedContent}).Count) {
        throw 'Backend build dependencies changed; extend explicit snapshot capture before running E2E.'
    }
    # Parent MSBuild imports are not part of the currently supported project.
    # Fail rather than silently compiling against an uncaptured shared import.
    for ($ancestor=$source; $ancestor; $ancestor=Split-Path $ancestor -Parent) {
        foreach ($name in @('Directory.Build.props','Directory.Build.targets','Directory.Packages.props')) {
            if (Test-Path -LiteralPath (Join-Path $ancestor $name)) { throw 'Uncaptured parent MSBuild input; extend backend snapshot capture.' }
        }
    }
    function Get-BackendInputs {
        $trees=@($source);if($references.Count){$trees+=$core}
        @((Get-ChildItem -LiteralPath $trees -Recurse -File | Where-Object {$_.FullName -notmatch '\\(?:bin|obj|artifacts|\.git|\.vs)\\'}).FullName)+
            @($contentInputs | ForEach-Object {[IO.Path]::GetFullPath((Join-Path $source $_.Include))})
    }
    $paths=@(Get-BackendInputs | Sort-Object -CaseSensitive)
    $inputs=@(foreach ($path in $paths) {
        # Do not follow a file/directory junction out of the declared input tree.
        for ($node=Get-Item -LiteralPath $path; $node -and $node.FullName -ne $projectPath; $node=$node.Parent) {
            if ($node.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in backend build inputs.' }
            if ($node -is [IO.FileInfo]) { $node=$node.Directory; if ($node.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in backend build inputs.' } }
        }
        $relative=$path.Substring((Join-Path $projectPath 'Backend').Length).TrimStart('\','/')
        [ordered]@{originalPath=$path;path=(Join-Path $destination $relative);SHA256=(Get-FileHash -LiteralPath $path).Hash}
    })
    $cache=@()
    if ($ReuseRestoreCache) {
        $restoreProjects=@('ProjectRpg.Backend');if($references.Count){$restoreProjects+='ProjectRpg.Operations.Core'}
        foreach ($restoreProject in $restoreProjects) {
        foreach ($name in @('project.assets.json',($restoreProject+'.csproj.nuget.g.props'),($restoreProject+'.csproj.nuget.g.targets'))) {
            $original=Join-Path $projectPath "Backend\$restoreProject\obj\$name"
            if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { throw 'NoBackendRestore requires existing local restore metadata.' }
            $cache+= [ordered]@{originalPath=$original;path=(Join-Path $destination "$restoreProject\obj\$name");SHA256=(Get-FileHash -LiteralPath $original).Hash}
        }
        }
    }
    foreach ($inputFile in @($inputs)+@($cache)) {
        New-Item -ItemType Directory -Path (Split-Path $inputFile.path -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $inputFile.originalPath -Destination $inputFile.path
        if ((Get-FileHash -LiteralPath $inputFile.path).Hash -cne $inputFile.SHA256 -or
            (Get-FileHash -LiteralPath $inputFile.originalPath).Hash -cne $inputFile.SHA256) { throw 'Backend input changed during snapshot; no host was started.' }
    }
    if ((@(Get-BackendInputs | Sort-Object -CaseSensitive) -join "`n") -cne ($paths -join "`n")) { throw 'Backend input inventory changed during snapshot.' }
    foreach ($inputFile in $inputs) {
        if ((Get-FileHash -LiteralPath $inputFile.originalPath).Hash -cne $inputFile.SHA256) { throw 'Backend input changed during snapshot; no host was started.' }
    }
    [pscustomobject]@{schemaVersion=1;capturedAtUtc=[DateTime]::UtcNow.ToString('o');snapshotRoot=$destination;
        projectFile=(Join-Path $destination 'ProjectRpg.Backend\ProjectRpg.Backend.csproj');inputs=$inputs;
        restoreMetadata=$cache;reuseRestoreCache=[bool]$ReuseRestoreCache;sharedSourcesStableDuringCapture=$true}
}
