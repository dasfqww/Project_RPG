[CmdletBinding()]
param([switch]$Apply, [int]$McpPort=8000)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $project 'Scripts/Network/DungeonPartyHudObservation.ps1')
$root=Join-Path $project 'Saved/BuildChecks/ActionProgressLegacy-20261030'
$null=New-Item -ItemType Directory -Path $root -Force
$hudPath='/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
$hud=@{refPath=$hudPath+'.WBP_PlayerHUD'}
$bp='editor_toolset.toolsets.blueprint.BlueprintTools'
$umg='UMGToolSet.UMGToolSet'
$assets='editor_toolset.toolsets.asset.AssetTools'
$objects='editor_toolset.toolsets.object.ObjectTools'
$pattern='OnCurrentProgressChanged|OnProgressBarShow|OnProgressBarHidden|OnProgressBarTextChanged|OnProgressTimeChanged|OnChargeCompleted'
function Call($set,$tool,$a=@{}) {
    $r=Invoke-PartyHudMcpRpc $McpPort 'tools/call' @{name='call_tool';arguments=@{toolset_name=$set;tool_name=$tool;arguments=$a}}
    if($r.isError){throw ($r.content.text -join ' ')}
    return ($r.content[0].text | ConvertFrom-Json).returnValue
}
function Save($name,$value){[IO.File]::WriteAllText((Join-Path $root $name),(ConvertTo-Json -InputObject $value -Depth 70),[Text.UTF8Encoding]::new($false))}
function Json($v){ConvertTo-Json -InputObject $v -Depth 60 -Compress}
function ExecPin($p){$p.type_id -in @((-join @([char]0xC2E4,[char]0xD589)),'exec','Execution')}
function Properties($ref) {
    $schema=(Call $objects 'list_properties' @{instance=$ref}) | ConvertFrom-Json
    return ((Call $objects 'get_properties' @{instance=$ref;properties=@($schema.PSObject.Properties.Name)}) | ConvertFrom-Json)
}
function Widgets {
    $result=@()
    foreach($w in (Call $umg 'GetWidgets' @{widgetBlueprint=$hud}).widgets) {
        if($w.widget.refPath){$result+=@{name=$w.widgetName;info=$w;properties=(Properties $w.widget);slot=$(if($w.slot.refPath){Properties $w.slot}else{$null})}}
    }
    return ,$result
}
function Graphs {
    $result=@()
    foreach($graph in @(Call $bp 'list_graphs' @{blueprint=$hud})) {
        $nodes=@(Call $bp 'find_nodes' @{graph=$graph;title=''})
        $result+=@{graph=$graph;nodes=@(Call $bp 'get_node_infos' @{nodes=$nodes})}
    }
    return ,$result
}
if(Call 'EditorToolset.EditorAppToolset' 'IsPIERunning'){throw 'Preserving active PIE'}
if(Call $assets 'is_dirty' @{asset_path=$hudPath}){throw 'Preserving unsaved HUD edits'}
$widgets=Widgets
$graphs=Graphs
$progress=@($widgets | Where-Object name -eq 'WBP_ProgressBar')
if(!$progress.Count) {
    if(@($graphs.nodes | Where-Object {$_.type_id -match $pattern -or $_.type_id -match 'GetWBP_ProgressBar|BrightProgressBar'}).Count){throw 'Progress widget absent but graph still refers to it'}
    Save 'HUDRepeat.json' @{changed=$false;widgetCount=$widgets.Count}
    Write-Output 'Already retired; no save required'
    return
}
if($progress.Count -ne 1){throw 'Ambiguous progress widget'}
$event=@($graphs | Where-Object {$_.graph.refPath.EndsWith(':EventGraph')})
$bright=@($graphs | Where-Object {$_.graph.refPath.EndsWith(':BrightProgressBar')})
if($event.Count -ne 1 -or $bright.Count -ne 1){throw 'Unexpected HUD graphs'}
foreach($path in @(Call $assets 'get_referencers' @{asset_path=$hudPath})) {
    $ref=@{refPath=$path+'.'+($path.Split('/')[-1])}
    foreach($g in @(Call $bp 'list_graphs' @{blueprint=$ref})) {
        if(@(Call $bp 'find_nodes' @{graph=$g;title='BrightProgressBar'}).Count){throw ('External BrightProgressBar caller: '+$path)}
    }
}
$map=@{}; foreach($n in $event[0].nodes){$map[$n.node.refPath]=$n}
$remove=@{}; $queue=[Collections.Generic.Queue[string]]::new()
foreach($n in $event[0].nodes) {
    if($n.type_id -match $pattern -or $n.type_id -match 'GetWBP_ProgressBar|\|BrightProgressBar$|Custom\|Brighten$') {
        $remove[$n.node.refPath]=$true
        if($n.type_id -notmatch '\|Assign'){$queue.Enqueue($n.node.refPath)}
    }
}
while($queue.Count) {
    $n=$map[$queue.Dequeue()]
    foreach($p in $n.output_pins) { foreach($c in $p.connected_pins) {
        $key=$c.node.refPath; if($remove.ContainsKey($key)){continue}
        $target=$map[$key]; if(!$target){throw 'Cross-graph edge'}
        # An exec chain can continue into an unrelated initialization binding.
        if($target.type_id -match '\|Assign' -and $target.type_id -notmatch $pattern){continue}
        $remove[$key]=$true; $queue.Enqueue($key)
    }}
}
# Include pure inputs that are used exclusively by the retired branches.
do {
    $changed=$false
    foreach($n in $event[0].nodes) {
        if($remove.ContainsKey($n.node.refPath)){continue}
        if(@(@($n.input_pins)+@($n.output_pins) | Where-Object {ExecPin $_}).Count){continue}
        $targets=@($n.output_pins.connected_pins | Where-Object {$_.node.refPath})
        if($targets.Count -and !@($targets | Where-Object {!$remove.ContainsKey($_.node.refPath)}).Count) {
            $remove[$n.node.refPath]=$true; $changed=$true
        }
    }
} while($changed)
$bridges=@()
Save 'HUDCandidate.json' @($remove.Keys | Sort-Object | ForEach-Object {$map[$_]})
function FindExit($key,$visited) {
    if(!$remove.ContainsKey($key)){throw 'Expected removed node'}
    if($visited.ContainsKey($key)){throw 'Cycle in retired exec chain'}
    $visited[$key]=$true
    foreach($p in $map[$key].output_pins | Where-Object {ExecPin $_}) {
        foreach($c in $p.connected_pins) {
            if($remove.ContainsKey($c.node.refPath)){FindExit $c.node.refPath $visited.Clone()}else{$c}
        }
    }
}
foreach($n in $event[0].nodes | Where-Object {!$remove.ContainsKey($_.node.refPath)}) {
    foreach($p in $n.output_pins) { foreach($c in $p.connected_pins) {
        if(!$remove.ContainsKey($c.node.refPath)){continue}
        if(!(ExecPin $p)){continue}
        foreach($exit in @(FindExit $c.node.refPath @{})) {$bridges+=@{output_pin=$p.pin_id;input_pin=$exit}}
    }}
}
$forbidden=@($remove.Keys | ForEach-Object {$map[$_]} | Where-Object {$_.type_id -match 'Health|Mana|Identity|Rage|Notice|QuickSlot|RewardMail' -and $_.type_id -ne '|GetNoticeText'})
if($forbidden.Count){throw ('Retirement crosses another feature: '+($forbidden.type_id -join ','))}
$plan=@{removedNodes=@($remove.Keys | Sort-Object | ForEach-Object {@{node=$_;type=$map[$_].type_id}});bridges=$bridges;removeFunction='BrightProgressBar';removeWidget='WBP_ProgressBar'}
Save 'HUDPlan.json' $plan
if(!$Apply){Write-Output ('Planned nodes='+$remove.Count+'; bridges='+$bridges.Count); return}
Save 'HUDBefore.json' @{widgets=$widgets;graphs=$graphs}
$disk=Join-Path $project 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset'
$backup=Join-Path $root 'HUD-before.uasset'
Copy-Item -LiteralPath $disk -Destination $backup
$beforeHash=(Get-FileHash -LiteralPath $disk).Hash
foreach($bridge in $bridges){$null=Call $bp 'connect_pins' $bridge}
foreach($key in @($remove.Keys)){$null=Call $bp 'delete_node' @{node=@{refPath=$key}}}
$null=Call $bp 'remove_function_graph' @{blueprint=$hud;graph_name='BrightProgressBar'}
$null=Call $umg 'RemoveWidget' @{widgetBlueprint=$hud;widget=$progress[0].info.widget}
$compile=Call $bp 'compile_blueprint' @{blueprint=$hud}
$afterWidgets=Widgets
$afterGraphs=Graphs
foreach($w in $widgets | Where-Object name -ne 'WBP_ProgressBar') {
    $a=@($afterWidgets | Where-Object name -eq $w.name)
    if($a.Count -ne 1 -or (Json $a[0]) -cne (Json $w)){throw ('Other widget changed: '+$w.name)}
}
if(@($afterGraphs.nodes | Where-Object {$_.type_id -match $pattern -or $_.type_id -match 'GetWBP_ProgressBar|BrightProgressBar'}).Count){throw 'Legacy references remain'}
# Preserve the complete unrelated UpdateValue function.
$beforeUpdate=@($graphs | Where-Object {$_.graph.refPath.EndsWith(':UpdateValue')})
$afterUpdate=@($afterGraphs | Where-Object {$_.graph.refPath.EndsWith(':UpdateValue')})
if((Json $beforeUpdate) -cne (Json $afterUpdate)){throw 'UpdateValue changed'}
function CanonicalNode($n) {
    foreach($pin in @($n.input_pins)+@($n.output_pins)){$pin.connected_pins=@($pin.connected_pins | Sort-Object {Json $_})}
    return Json $n
}
$afterMap=@{}; foreach($n in ($afterGraphs | Where-Object {$_.graph.refPath.EndsWith(':EventGraph')} ).nodes){$afterMap[$n.node.refPath]=$n}
foreach($n in $event[0].nodes | Where-Object {!$remove.ContainsKey($_.node.refPath)}) {
    $expected=(Json $n) | ConvertFrom-Json
    foreach($pin in @($expected.input_pins)+@($expected.output_pins)) {
        $pin.connected_pins=@($pin.connected_pins | Where-Object {!$remove.ContainsKey($_.node.refPath)})
        foreach($bridge in $bridges) {
            if((Json $pin.pin_id) -eq (Json $bridge.output_pin)){$pin.connected_pins+= $bridge.input_pin}
            if((Json $pin.pin_id) -eq (Json $bridge.input_pin)){$pin.connected_pins+= $bridge.output_pin}
        }
    }
    if(!$afterMap.ContainsKey($n.node.refPath) -or (CanonicalNode $expected) -cne (CanonicalNode $afterMap[$n.node.refPath])){throw ('Unrelated node changed: '+$n.type_id)}
}
$null=Call $assets 'save_assets' @{asset_paths=@($hudPath)}
if(Call $assets 'is_dirty' @{asset_path=$hudPath}){throw 'Saved HUD is dirty'}
Save 'HUDApplied.json' @{changed=$true;compile=$compile;removedNodes=$remove.Count;bridges=$bridges.Count;widgetsBefore=$widgets.Count;widgetsAfter=$afterWidgets.Count;beforeHash=$beforeHash;afterHash=(Get-FileHash $disk).Hash;graphs=$afterGraphs}
Write-Output ('Retired progress: nodes='+$remove.Count+'; widgets='+$widgets.Count+' -> '+$afterWidgets.Count)
