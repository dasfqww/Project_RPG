<#
Preflight the six files in the FirstSlice material package. Read-only by default.
-Apply copies missing files only; identical targets are preserved and conflicts
are rejected before any content write. Coordinate Content/Config readers first.
Does not launch Unreal, publish content, grant rewards, or delete any files.
#>
[CmdletBinding()]
param(
    [switch]$Apply,
    [string]$TargetProjectRoot,
    [string]$PackageRoot,
    [string]$ReportPath
)
$ErrorActionPreference='Stop'
$repository=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(!$TargetProjectRoot){$TargetProjectRoot=$repository}
if(!$PackageRoot){$PackageRoot=Join-Path $repository 'ContentAuthoring/ProgressionMaterials'}
$TargetProjectRoot=[IO.Path]::GetFullPath($TargetProjectRoot).TrimEnd('\','/')
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot).TrimEnd('\','/')
if(!$ReportPath){$ReportPath=Join-Path $TargetProjectRoot 'Saved/ContentChecks/ProgressionMaterialInstall/Result.json'}
$ReportPath=[IO.Path]::GetFullPath($ReportPath)
$result=[ordered]@{mode=$(if($Apply){'Apply'}else{'Plan'});targetProjectRoot=$TargetProjectRoot;packageRoot=$PackageRoot;passed=$false;plannedCopyCount=0;copiedCount=0;preservedCount=0;files=@();failure=$null}
$expected=@(
    'Config/Tags/ProgressionMaterials.ini',
    'Content/Blueprints/GameData/Item/Progression/DA_Item_ProgressionOre.uasset',
    'Content/Blueprints/GameData/Item/Progression/DA_Item_ProgressionToken.uasset',
    'Content/Blueprints/GameData/DungeonReward/Progression/DA_DungeonReward_Intro1.uasset',
    'Content/Blueprints/GameData/DungeonReward/Progression/DA_DungeonReward_Intro2.uasset',
    'Content/Blueprints/GameData/DungeonReward/Progression/DA_DungeonReward_Intro3.uasset'
)
function Hash([string]$path){return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Safe-Child([string]$root,[string]$relative){
    $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(!$path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw ('Path escapes its root: '+$relative)}
    $cursor=$path
    while($cursor.Length -ge $root.Length){
        if(Test-Path -LiteralPath $cursor){
            $item=Get-Item -LiteralPath $cursor -Force
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('Preserving reparse point: '+$cursor)}
        }
        if([string]::Equals($cursor,$root,[StringComparison]::OrdinalIgnoreCase)){break}
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $path
}
try {
    if(!(Test-Path -LiteralPath (Safe-Child $TargetProjectRoot 'Project_RPG.uproject') -PathType Leaf)){throw 'Target must contain Project_RPG.uproject'}
    $manifestPath=Safe-Child $PackageRoot 'Manifest.json'
    $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if($manifest.version -cne 'FirstSlice.Materials.v1'){throw 'Unsupported package version'}
    $entries=@($manifest.assets)+@($manifest.tagConfig)
    $paths=@($entries|ForEach-Object {[string]$_.path})
    if($entries.Count -ne 6 -or @($paths|Sort-Object -Unique).Count -ne 6 -or @($paths|Where-Object {$_ -cnotin $expected}).Count){throw 'Manifest must contain exactly the six allowed relative paths'}
    $reportAllowed=$ReportPath.StartsWith($TargetProjectRoot+'\Saved\',[StringComparison]::OrdinalIgnoreCase) -or $ReportPath.StartsWith($repository+'\Saved\',[StringComparison]::OrdinalIgnoreCase)
    if(!$reportAllowed){throw 'Report must stay under the target or repository Saved directory'}
    $reportRoot=if($ReportPath.StartsWith($repository+'\Saved\',[StringComparison]::OrdinalIgnoreCase)){$repository}else{$TargetProjectRoot}
    $null=Safe-Child $reportRoot $ReportPath.Substring($reportRoot.Length+1)
    $records=@()
    foreach($relative in $expected){
        $entry=@($entries|Where-Object {$_.path -ceq $relative})
        if($entry.Count -ne 1 -or $entry[0].sha256 -notmatch '^[0-9a-fA-F]{64}$'){throw ('Invalid manifest hash: '+$relative)}
        $source=Safe-Child $PackageRoot $relative
        $destination=Safe-Child $TargetProjectRoot $relative
        if(!(Test-Path -LiteralPath $source -PathType Leaf) -or (Hash $source) -cne $entry[0].sha256.ToLowerInvariant()){throw ('Package bytes differ from manifest: '+$relative)}
        $exists=Test-Path -LiteralPath $destination
        if($exists -and (!(Test-Path -LiteralPath $destination -PathType Leaf) -or (Hash $destination) -cne $entry[0].sha256.ToLowerInvariant())){throw ('Preserving different existing target: '+$relative)}
        $records += [pscustomobject]@{path=$relative;source=$source;destination=$destination;sha256=$entry[0].sha256.ToLowerInvariant();existed=$exists;copied=$false}
    }
    $result.files=$records
    $result.manifestSha256=Hash $manifestPath
    $result.plannedCopyCount=@($records|Where-Object {!$_.existed}).Count
    $result.preservedCount=@($records|Where-Object {$_.existed}).Count
    if($Apply){
        foreach($record in $records){
            $null=Safe-Child $PackageRoot $record.path
            $null=Safe-Child $TargetProjectRoot $record.path
            if((Hash $record.source) -cne $record.sha256){throw ('Package changed during install: '+$record.path)}
            if(Test-Path -LiteralPath $record.destination){
                if((Hash $record.destination) -cne $record.sha256){throw ('Target changed during install: '+$record.path)}
                continue
            }
            $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($record.destination))
            $null=Safe-Child $TargetProjectRoot $record.path
            # overwrite=false also rejects a target created after the last check.
            [IO.File]::Copy($record.source,$record.destination,$false)
            $record.copied=$true;$result.copiedCount++
            if((Hash $record.destination) -cne $record.sha256){throw ('Installed hash mismatch: '+$record.path)}
        }
    }
    $result.passed=$true
}
catch {$result.failure=$_.Exception.Message}
finally {
    # Reports are small evidence; never redirect a report into Content/Config or a linked directory.
    $savedRoots=@((Join-Path $repository 'Saved'),(Join-Path $TargetProjectRoot 'Saved'))
    $allowed=@($savedRoots|Where-Object {$ReportPath.StartsWith([IO.Path]::GetFullPath($_)+'\',[StringComparison]::OrdinalIgnoreCase)})
    if($allowed.Count){
        $reportBoundary=[IO.Path]::GetFullPath($allowed[0])
        $null=Safe-Child $reportBoundary $ReportPath.Substring($reportBoundary.Length+1)
        $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ReportPath))
        $null=Safe-Child $reportBoundary $ReportPath.Substring($reportBoundary.Length+1)
        [IO.File]::WriteAllText($ReportPath,($result|ConvertTo-Json -Depth 7),(New-Object Text.UTF8Encoding($false)))
    }
    $result|ConvertTo-Json -Depth 7
}
if(!$result.passed){exit 1}
exit 0
