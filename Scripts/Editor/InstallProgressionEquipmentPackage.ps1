<#
Preflight the seven equipment definitions and their tag INI. Read-only by default.
-Apply copies missing files only; identical targets keep their bytes/timestamps.
Coordinate Content/Config readers before applying to the original project.
Does not start Unreal, grant items, initialize growth, or change existing equipment.
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
if(!$PackageRoot){$PackageRoot=Join-Path $repository 'ContentAuthoring/ProgressionEquipment'}
$TargetProjectRoot=[IO.Path]::GetFullPath($TargetProjectRoot).TrimEnd('\','/')
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot).TrimEnd('\','/')
if(!$ReportPath){$ReportPath=Join-Path $TargetProjectRoot 'Saved/ContentChecks/ProgressionEquipmentInstall/Result.json'}
$ReportPath=[IO.Path]::GetFullPath($ReportPath)
$result=[ordered]@{mode=$(if($Apply){'Apply'}else{'Plan'});targetProjectRoot=$TargetProjectRoot;packageRoot=$PackageRoot;passed=$false;plannedCopyCount=0;copiedCount=0;preservedCount=0;files=@();failure=$null}
$models=[ordered]@{
    StarterGreatSword=@('Starter.GreatSword',9)
    StarterChest=@('Starter.Chest',1)
    StarterGloves=@('Starter.Gloves',4)
    EnduranceGreatSword=@('Target.EnduranceGreatSword',9)
    CirculationGreatSword=@('Target.CirculationGreatSword',9)
    TargetChest=@('Target.Chest',1)
    TargetGloves=@('Target.Gloves',4)
}
$tag='Config/Tags/ProgressionEquipment.ini'
$template='Content/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.uasset'
$expected=@($models.Keys|ForEach-Object {'Content/Blueprints/GameData/Item/Progression/DA_Item_Progression_'+$_+'.uasset'})+@($tag)
function Hash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Safe-Child([string]$root,[string]$relative){
    $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(!$path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw ('Path escapes its root: '+$relative)}
    # Include ancestors above the root too: a real project directory inside a
    # junction is still a redirected write. Do not follow links during install.
    $cursor=$path
    while($cursor){
        if(Test-Path -LiteralPath $cursor){
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('Preserving reparse point: '+$cursor)}
        }
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $path
}
function Report-Allowed {
    return !$ReportPath.StartsWith($PackageRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and (
        $ReportPath.StartsWith($TargetProjectRoot+'\Saved\',[StringComparison]::OrdinalIgnoreCase) -or
        $ReportPath.StartsWith($repository+'\Saved\',[StringComparison]::OrdinalIgnoreCase))
}
try {
    if($TargetProjectRoot -match '^[A-Za-z]:$' -or $PackageRoot -match '^[A-Za-z]:$'){
        throw 'Project/package root must be a directory below the volume root'
    }
    if(!(Report-Allowed)){throw 'Report must stay under Saved and outside the source package'}
    $reportRoot=if($ReportPath.StartsWith($repository+'\Saved\',[StringComparison]::OrdinalIgnoreCase)){$repository}else{$TargetProjectRoot}
    $null=Safe-Child $reportRoot $ReportPath.Substring($reportRoot.Length+1)
    if(Test-Path -LiteralPath $ReportPath -PathType Container){throw 'Report path must name a file'}
    if(!(Test-Path -LiteralPath (Safe-Child $TargetProjectRoot 'Project_RPG.uproject') -PathType Leaf)){throw 'Target must contain Project_RPG.uproject'}
    $manifestPath=Safe-Child $PackageRoot 'Manifest.json'
    $manifestHash=Hash $manifestPath
    $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
    if($manifest.version -cne 'FirstSlice.Equipment.v1'){throw 'Unsupported equipment package version'}
    $entries=@($manifest.assets)+@($manifest.tagConfig)
    $paths=@($entries|ForEach-Object {[string]$_.path})
    if(@($manifest.assets).Count -ne 7 -or $manifest.tagConfig.path -cne $tag -or $entries.Count -ne 8 -or
        @($paths|Sort-Object -Unique).Count -ne 8 -or @($paths|Where-Object {$_ -cnotin $expected}).Count){
        throw 'Manifest must contain exactly the eight allowed relative paths'
    }
    foreach($model in $models.Keys){
        $relative='Content/Blueprints/GameData/Item/Progression/DA_Item_Progression_'+$model+'.uasset'
        $entry=@($manifest.assets|Where-Object {$_.path -ceq $relative})
        $id='RPGItemDefinition:GameItem.Equipment.Progression.'+$models[$model][0]
        if($entry.Count -ne 1 -or $entry[0].model -cne $model -or
            $entry[0].asset -cne ('/Game/'+$relative.Substring('Content/'.Length).Replace('.uasset','')) -or
            $entry[0].properties.expected_primary_asset_id -cne $id -or $manifest.registryIds.$model -cne $id -or
            $entry[0].properties.definition_version -ne 1 -or $entry[0].properties.slot_index -ne $models[$model][1]){
            throw ('Equipment identity/version/slot differs: '+$model)
        }
    }
    if($manifest.sourceTemplate -cne '/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword' -or
        $manifest.sourceTemplateSha256 -notmatch '^[0-9a-fA-F]{64}$') {throw 'Unexpected source template'}
    $templatePath=Safe-Child $TargetProjectRoot $template
    if(!(Test-Path -LiteralPath $templatePath -PathType Leaf) -or
        (Hash $templatePath) -cne $manifest.sourceTemplateSha256.ToLowerInvariant()){
        throw 'Target GreatSword template differs; recheck the authored package before applying'
    }
    $records=@()
    foreach($relative in $expected){
        $entry=@($entries|Where-Object {$_.path -ceq $relative})
        if($entry.Count -ne 1 -or $entry[0].sha256 -notmatch '^[0-9a-fA-F]{64}$'){throw ('Invalid file hash: '+$relative)}
        $source=Safe-Child $PackageRoot $relative
        $destination=Safe-Child $TargetProjectRoot $relative
        $hash=$entry[0].sha256.ToLowerInvariant()
        if(!(Test-Path -LiteralPath $source -PathType Leaf) -or (Hash $source) -cne $hash){throw ('Package bytes differ from manifest: '+$relative)}
        $exists=Test-Path -LiteralPath $destination
        if($exists -and (!(Test-Path -LiteralPath $destination -PathType Leaf) -or (Hash $destination) -cne $hash)){
            throw ('Preserving different existing target: '+$relative)
        }
        $records+=[pscustomobject]@{path=$relative;source=$source;destination=$destination;sha256=$hash;existed=$exists;copied=$false}
    }
    $result.files=$records;$result.manifestSha256=$manifestHash;$result.templateSha256=Hash $templatePath
    $result.plannedCopyCount=@($records|Where-Object {!$_.existed}).Count
    $result.preservedCount=@($records|Where-Object {$_.existed}).Count
    if($Apply){
        foreach($record in $records){
            $null=Safe-Child $PackageRoot $record.path
            $null=Safe-Child $TargetProjectRoot $record.path
            if((Hash $manifestPath) -cne $manifestHash -or (Hash $templatePath) -cne $result.templateSha256){throw 'Manifest/template changed during install'}
            if((Hash $record.source) -cne $record.sha256){throw ('Package changed during install: '+$record.path)}
            if(Test-Path -LiteralPath $record.destination){
                if((Hash $record.destination) -cne $record.sha256){throw ('Target changed during install: '+$record.path)}
                if(!$record.existed){$result.preservedCount++;$record.existed=$true}
                continue
            }
            $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($record.destination))
            $null=Safe-Child $TargetProjectRoot $record.path
            [IO.File]::Copy($record.source,$record.destination,$false)
            $record.copied=$true;$result.copiedCount++
            if((Hash $record.destination) -cne $record.sha256){throw ('Installed hash mismatch: '+$record.path)}
        }
    }
    $result.passed=$true
}
catch {$result.failure=$_.Exception.Message}
finally {
    try {
        if(Report-Allowed){
            $reportRoot=if($ReportPath.StartsWith($repository+'\Saved\',[StringComparison]::OrdinalIgnoreCase)){$repository}else{$TargetProjectRoot}
            $null=Safe-Child $reportRoot $ReportPath.Substring($reportRoot.Length+1)
            $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ReportPath))
            $null=Safe-Child $reportRoot $ReportPath.Substring($reportRoot.Length+1)
            [IO.File]::WriteAllText($ReportPath,($result|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
        }
    }catch{$result.passed=$false;$result.reportFailure=$_.Exception.Message}
    $result|ConvertTo-Json -Depth 8
}
if(!$result.passed){exit 1}
exit 0
