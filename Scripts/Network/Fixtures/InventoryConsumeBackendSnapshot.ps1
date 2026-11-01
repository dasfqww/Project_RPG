# Application-source snapshots only; never builds, migrates or deletes the source project.
function Assert-InventoryConsumeSnapshotBoundary {
    param([string]$ProjectRoot,[string]$Destination)
    $root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
    $target=[IO.Path]::GetFullPath($Destination)
    $prefix=Join-Path $root 'Saved\Tests\DungeonBossReturn'
    if(-not $target.StartsWith($prefix+'\',[StringComparison]::OrdinalIgnoreCase) -or
        $target.Substring($prefix.Length) -cnotmatch '^\\[0-9a-f]{32}\\backend-build\\inputs$') {
        throw 'Backend snapshot destination is outside the allocated run inputs.'
    }
    foreach($path in @($root,$target,(Join-Path $root 'Backend'))){
        $ancestor=$path
        while($ancestor){
            $item=Get-Item -LiteralPath $ancestor -Force -ErrorAction SilentlyContinue
            if($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Backend snapshot refused a reparse ancestor.'}
            $ancestor=[IO.Path]::GetDirectoryName($ancestor)
        }
    }
    $target
}

function Get-InventoryConsumeBackendInputs {
    param([string]$ProjectRoot)
    $root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
    $files=[Collections.Generic.List[string]]::new()
    foreach($part in @('ProjectRpg.Backend','Database')){
        $source=Join-Path $root ('Backend/'+$part)
        $stack=[Collections.Generic.Stack[string]]::new();$stack.Push($source)
        while($stack.Count){
            $directory=Get-Item -LiteralPath $stack.Pop() -Force -ErrorAction Stop
            if(-not $directory.PSIsContainer -or ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Backend snapshot source directory is missing or linked.'}
            foreach($child in Get-ChildItem -LiteralPath $directory.FullName -Force){
                if($child.PSIsContainer -and $child.Name -in @('bin','obj','artifacts','.git','.vs')){continue}
                if($child.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Backend snapshot refused a linked source input.'}
                if($child.PSIsContainer){$stack.Push($child.FullName)}else{$files.Add($child.FullName)}
            }
        }
    }
    foreach($parent in @($root,(Join-Path $root 'Backend'))){
        foreach($name in @('global.json','NuGet.config','Directory.Build.props','Directory.Build.targets','Directory.Packages.props')){
            $path=Join-Path $parent $name
            $item=Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
            if($item){
                if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Backend build control is not a regular file.'}
                $files.Add($item.FullName)
            }
        }
    }
    @($files | Sort-Object -Unique)
}

function Assert-InventoryConsumeBackendSnapshot {
    param([string]$ProjectRoot,[Parameter(Mandatory)]$Snapshot)
    $target=Assert-InventoryConsumeSnapshotBoundary $ProjectRoot $Snapshot.SnapshotRoot
    $expected=[Collections.Generic.List[string]]::new()
    foreach($entry in $Snapshot.Files){
        $path=[IO.Path]::GetFullPath((Join-Path $target $entry.RelativePath))
        if(-not $path.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backend snapshot manifest escaped its inputs.'}
        $expected.Add($path)
    }
    $actual=@(Get-InventoryConsumeBackendInputs $target)
    if(@(Compare-Object @($expected | Sort-Object -Unique) $actual).Count){throw 'Backend snapshot input set changed.'}
    foreach($entry in $Snapshot.Files){
        if((Get-FileHash -LiteralPath (Join-Path $target $entry.RelativePath)).Hash -cne $entry.SHA256){throw 'Backend snapshot input bytes changed.'}
    }
}

function New-InventoryConsumeBackendSnapshot {
    param([string]$ProjectRoot,[string]$Destination)
    $root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
    $target=Assert-InventoryConsumeSnapshotBoundary $root $Destination
    if(Get-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue){throw 'Backend snapshot destination already exists.'}
    $project=Join-Path $root 'Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj'
    if(-not (Test-Path -LiteralPath $project -PathType Leaf)){throw 'Backend snapshot project is missing.'}
    $paths=@(Get-InventoryConsumeBackendInputs $root)
    $hashes=@{};foreach($path in $paths){$hashes[$path]=(Get-FileHash -LiteralPath $path).Hash}
    foreach($path in $paths | Where-Object {[IO.Path]::GetExtension($_) -in '.csproj','.props','.targets'}){
        $document=[Xml.XmlDocument]::new();$document.XmlResolver=$null;$document.Load($path)
        if($document.SelectNodes('//*[local-name()="ProjectReference" or local-name()="Import"]').Count){
            throw 'Backend snapshot requires an explicitly self-contained application project.'
        }
    }
    New-Item -ItemType Directory -Path $target -ErrorAction Stop | Out-Null
    $entries=[Collections.Generic.List[object]]::new()
    foreach($path in $paths){
        if(-not $path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backend source escaped its project root.'}
        $relative=$path.Substring($root.Length+1)
        $destinationPath=[IO.Path]::GetFullPath((Join-Path $target $relative))
        if(-not $destinationPath.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backend copy escaped allocated inputs.'}
        New-Item -ItemType Directory -Path (Split-Path $destinationPath) -Force | Out-Null
        Copy-Item -LiteralPath $path -Destination $destinationPath -ErrorAction Stop
        if((Get-FileHash -LiteralPath $destinationPath).Hash -cne $hashes[$path]){throw 'Backend source changed while copying.'}
        $entries.Add([pscustomobject]@{RelativePath=$relative;SHA256=$hashes[$path]})
    }
    $after=@(Get-InventoryConsumeBackendInputs $root)
    if(@(Compare-Object $paths $after).Count){throw 'Backend source cohort changed while copying.'}
    foreach($path in $paths){if((Get-FileHash -LiteralPath $path).Hash -cne $hashes[$path]){throw 'Backend source cohort bytes changed while copying.'}}
    $snapshot=[pscustomobject]@{SnapshotRoot=$target;ProjectPath=(Join-Path $target 'Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj');Files=$entries.ToArray();SourceCohortVerified=$true}
    Assert-InventoryConsumeBackendSnapshot $root $snapshot
    $snapshot
}
