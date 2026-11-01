<#
Extract the existing four self-binding skill cells into a reusable UserWidget.
Read-only by default. -Apply edits and saves only the module and PlayerHUD.
Requires the editor MCP endpoint, no PIE, clean targets and the coordination row.
Does not change gameplay/input, compile C++, or stop/restart the editor.
#>
[CmdletBinding()]
param([switch]$Apply, [int]$McpPort = 8000)
$ErrorActionPreference = 'Stop'
$project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $project 'Scripts/Network/DungeonPartyHudObservation.ps1')
$output = Join-Path $project 'Saved/BuildChecks/HudSkillBarModule-20261029'
$hudPath = '/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
$barPath = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerSkillBar'
$hud = @{refPath=($hudPath+'.WBP_PlayerHUD')}
$bar = @{refPath=($barPath+'.WBP_PlayerSkillBar')}
$umg = 'UMGToolSet.UMGToolSet'
$objects = 'editor_toolset.toolsets.object.ObjectTools'
$assets = 'editor_toolset.toolsets.asset.AssetTools'
$blueprints = 'editor_toolset.toolsets.blueprint.BlueprintTools'
$rowName = 'HorizontalBox_SkillQuickSlots'
$moduleName = 'PlayerSkillBar'
$cellNames = @(0..3 | ForEach-Object { 'SkillQuickSlot_' + $_ })
$extracted = @($rowName) + $cellNames

function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Call([string]$Set, [string]$Tool, [hashtable]$Arguments = @{}) {
    $r = Invoke-PartyHudMcpRpc $McpPort 'tools/call' @{
        name='call_tool';arguments=@{toolset_name=$Set;tool_name=$Tool;arguments=$Arguments}}
    Require (-not $r.isError) (($r.content.text) -join ' ')
    $data = $r.content[0].text | ConvertFrom-Json
    return $data.returnValue
}
function Json($Value) { ConvertTo-Json -InputObject $Value -Depth 60 -Compress }
function Equal($A, $B) { (Json $A) -ceq (Json $B) }
function WriteResult([string]$Name, $Value) {
    [IO.File]::WriteAllText((Join-Path $output $Name), (ConvertTo-Json -InputObject $Value -Depth 60),
        [Text.UTF8Encoding]::new($false))
}
function Props($Ref) {
    $schema = (Call $objects 'list_properties' @{instance=$Ref}) | ConvertFrom-Json
    $names = @($schema.PSObject.Properties.Name)
    Require ($names.Count -gt 0) ('No property schema: ' + $Ref.refPath)
    return ((Call $objects 'get_properties' @{instance=$Ref;properties=$names}) | ConvertFrom-Json)
}
function CopyProps($Ref, $Expected, [string[]]$Exclude = @()) {
    $current = Props $Ref
    $delta = [ordered]@{}
    foreach ($p in $Expected.PSObject.Properties) {
        if ($p.Name -in $Exclude) { continue }
        Require ($null -ne $current.PSObject.Properties[$p.Name]) ('Missing property ' + $p.Name)
        if (-not (Equal $current.($p.Name) $p.Value)) { $delta[$p.Name] = $p.Value }
    }
    if ($delta.Count) { $null = Call $objects 'set_properties' @{instance=$Ref;values=(Json $delta)} }
    $after = Props $Ref
    foreach ($p in $Expected.PSObject.Properties) {
        if ($p.Name -notin $Exclude) {
            Require (Equal $after.($p.Name) $p.Value) ('Property differs: ' + $Ref.refPath + ' ' + $p.Name)
        }
    }
}
function Snapshot($BP) {
    $tree = Call $umg 'GetWidgets' @{widgetBlueprint=$BP}
    $entries = [ordered]@{}
    foreach ($w in $tree.widgets) {
        if (-not $w.widget.refPath) { continue }
        $entries[$w.widgetName] = [ordered]@{
            info=$w;properties=(Props $w.widget)
            slotProperties=$(if ($w.slot.refPath) { Props $w.slot } else { $null })}
    }
    return $entries
}
function GraphSnapshot {
    $result = @()
    foreach ($graph in @(Call $blueprints 'list_graphs' @{blueprint=$hud})) {
        $nodes = @(Call $blueprints 'find_nodes' @{graph=$graph;title=''})
        $infos = @(Call $blueprints 'get_node_infos' @{nodes=$nodes})
        $result += [ordered]@{graph=$graph;nodes=$infos}
    }
    return ,$result
}
function AssertIdle {
    Require (-not (Call 'EditorToolset.EditorAppToolset' 'IsPIERunning')) 'Preserving active PIE'
    Require (-not (Call $assets 'is_dirty' @{asset_path=$hudPath})) 'Preserving unsaved HUD changes'
}

Require ((Get-Content -LiteralPath (Join-Path $project 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 |
    Where-Object { $_ -match '^\|[^|]+ / hud-skillbar-module-20261029 \|' }).Count -eq 1) 'Missing ownership row'
AssertIdle
$before = Snapshot $hud
$graphs = GraphSnapshot
$diskHud = Join-Path $project 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset'
$diskBar = Join-Path $project 'Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerSkillBar.uasset'
$hudHash = (Get-FileHash -LiteralPath $diskHud -Algorithm SHA256).Hash
$null = New-Item -ItemType Directory -Path $output -Force

if ($before.Contains($moduleName)) {
    Require (-not $before.Contains($rowName)) 'Both legacy row and module are present'
    Require (Test-Path -LiteralPath $diskBar) 'Module was not saved'
    $module = Snapshot $bar
    Require ($module.Count -eq 5) 'Unexpected module contents'
    foreach ($index in 0..3) {
        $cell = $module[$cellNames[$index]]
        Require ($cell -and $cell.properties.slotIndex -eq $index -and $cell.properties.bIsSkillSlot) 'Slot identity differs'
    }
    WriteResult 'Repeat.json' @{needsChange=$false;hud=$before;module=$module;hudHash=$hudHash}
    Write-Output 'Skill bar module already extracted; no assets edited or saved.'
    return
}

Require (-not (Test-Path -LiteralPath $diskBar)) 'Preserving existing module asset'
Require ($before.Contains($rowName)) 'Missing original skill row'
$row = $before[$rowName]
Require ($row.info.widgetClassPath.refPath -eq '/Script/UMG.HorizontalBox') 'Different skill row class'
$children = @($before.Values | Where-Object { $_.info.parent.refPath -eq $row.info.widget.refPath })
Require ($children.Count -eq 4) 'Preserving custom skill row children'
foreach ($index in 0..3) {
    $cell = $before[$cellNames[$index]]
    Require ($cell -and $cell.properties.slotIndex -eq $index -and $cell.properties.bIsSkillSlot -and
        -not $cell.properties.bCanDrag -and $cell.info.parent.refPath -eq $row.info.widget.refPath) 'Different skill-cell configuration'
    Require ($cell.info.widgetClassPath.refPath -like '*/WBP_SkillQuickSlot.WBP_SkillQuickSlot*') 'Different skill-cell class'
}
foreach ($name in $extracted) { Require (-not (Json $graphs).Contains($name)) ('Parent graph references ' + $name) }
WriteResult 'Before.json' @{hud=$before;graphs=$graphs;hudHash=$hudHash}
if (-not $Apply) { Write-Output 'Ready: four self-binding cells, no parent graph references. No assets changed.'; return }

AssertIdle
Require ((Get-FileHash -LiteralPath $diskHud -Algorithm SHA256).Hash -eq $hudHash) 'HUD changed during inspection'
Copy-Item -LiteralPath $diskHud -Destination (Join-Path $output ('WBP_PlayerHUD.'+$hudHash+'.before.uasset'))
$created = Call $umg 'CreateWidgetBlueprint' @{
    folderPath='/Game/Blueprints/UI/PlayerHUD/Modules';assetName='WBP_PlayerSkillBar';parentClass=@{refPath='/Script/UMG.UserWidget'}}
Require ($created.refPath) 'Could not create skill bar module'
$root = Call $umg 'AddWidget' @{widgetBlueprint=$bar;widgetClass=@{refPath='/Script/UMG.HorizontalBox'};widgetDisplayName=$rowName}
# Panel appearance belongs to the new module instance; the internal panel stays neutral.
foreach ($index in 0..3) {
    $source = $before[$cellNames[$index]]
    $classPath = $source.info.widgetClassPath.refPath
    if (-not $classPath.EndsWith('_C')) { $classPath += '_C' }
    $added = Call $umg 'AddWidget' @{widgetBlueprint=$bar;widgetClass=@{refPath=$classPath};widgetDisplayName=$cellNames[$index];parentWidget=$root.widget;childIndex=$index}
    CopyProps $added.widget $source.properties @('slot')
    CopyProps $added.slot $source.slotProperties
}
Require (Call $umg 'CompileWidgetBlueprint' @{widgetBlueprint=$bar}) 'Module compile failed; parent not edited'

# Insert at the original canvas position, then remove only the extracted row.
$siblings = @($before.Values | Where-Object { $_.info.parent.refPath -eq $row.info.parent.refPath })
$siblingNames = @($siblings | ForEach-Object { $_.info.widgetName })
$at = [Array]::IndexOf($siblingNames, $rowName)
Require ($at -ge 0) 'Missing original canvas child order'
$instance = Call $umg 'AddWidget' @{widgetBlueprint=$hud;widgetClass=@{refPath=($barPath+'.WBP_PlayerSkillBar_C')};widgetDisplayName=$moduleName;parentWidget=$row.info.parent;childIndex=$at}
CopyProps $instance.slot $row.slotProperties
CopyProps $instance.widget $row.properties @('slot')
$null = Call $umg 'RemoveWidget' @{widgetBlueprint=$hud;widget=$row.info.widget}
Require (Call $umg 'CompileWidgetBlueprint' @{widgetBlueprint=$hud}) 'HUD compile failed; changes remain unsaved'
$after = Snapshot $hud
$module = Snapshot $bar
Require ($after.Count -eq $before.Count - 4) 'Unexpected HUD widget count'
Require ($module.Count -eq 5) 'Unexpected module widget count'
foreach ($name in $before.Keys) {
    if ($name -in $extracted) { Require (-not $after.Contains($name)) ('Old child remains ' + $name); continue }
    Require ($after.Contains($name) -and (Equal $before[$name] $after[$name])) ('Unrelated HUD widget changed ' + $name)
}
foreach ($index in 0..3) {
    $source = $before[$cellNames[$index]]
    CopyProps $module[$cellNames[$index]].info.widget $source.properties @('slot')
    Require (Equal $module[$cellNames[$index]].slotProperties $source.slotProperties) 'Cell spacing changed'
}
$afterGraphs = GraphSnapshot
Require (Equal $graphs $afterGraphs) 'HUD event/function graph changed'
Require ((Get-FileHash -LiteralPath $diskHud -Algorithm SHA256).Hash -eq $hudHash) 'HUD disk changed externally; not saving'
$null = Call $assets 'save_assets' @{asset_paths=@($barPath,$hudPath)}
Require (Test-Path -LiteralPath $diskBar) 'Module did not save'
Require (-not (Call $assets 'is_dirty' @{asset_path=$hudPath})) 'HUD remains dirty'
Require (-not (Call $assets 'is_dirty' @{asset_path=$barPath})) 'Module remains dirty'
WriteResult 'Applied.json' @{hud=$after;module=$module;graphsUnchanged=$true;preservedHudWidgets=($before.Count-5);
    originalHudHash=$hudHash;hudHash=(Get-FileHash -LiteralPath $diskHud).Hash;moduleHash=(Get-FileHash -LiteralPath $diskBar).Hash;
    cppChanged=$false;gameplayExecuted=$false;compileSucceeded=$true}
Write-Output 'PASS: independent four-cell skill bar saved; other HUD widgets and graphs preserved.'
