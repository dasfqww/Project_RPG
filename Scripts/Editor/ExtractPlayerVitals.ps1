<# Preserve the authored HP/MP skin while moving presentation into RPGPlayerVitalsWidget.
Read-only by default. Requires the owned editor Session.json and clean source assets.
#>
[CmdletBinding()]
param([switch]$Apply,[string]$OutputRoot='C:/UE5/Project_RPG/Saved/BuildChecks/HudVitalsModule-20261101',[string]$SessionFile='')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$SessionFile){$SessionFile=Join-Path $OutputRoot 'Session.json'}
$session=Get-Content -Raw -LiteralPath $SessionFile | ConvertFrom-Json
$command=(Get-CimInstance Win32_Process -Filter ('ProcessId='+$session.pid)).CommandLine
if(!$command -or !$command.Contains($session.runtime)){throw 'Owned editor identity changed'}
. (Join-Path $workspace 'Scripts/Network/DungeonPartyHudObservation.ps1')
$script:PartyHudMcpSessions=@{}
$script:PartyHudClients=@{vitals=@{Port=[int]$session.port;Process=(Get-Process -Id $session.pid)}}
$umg='UMGToolSet.UMGToolSet';$objects='editor_toolset.toolsets.object.ObjectTools';$blueprints='editor_toolset.toolsets.blueprint.BlueprintTools';$assets='editor_toolset.toolsets.asset.AssetTools'
$hudPath='/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD';$modulePath='/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals'
$hud=@{refPath=$hudPath+'.WBP_PlayerHUD'};$module=@{refPath=$modulePath+'.WBP_PlayerVitals'}
$status=@{refPath='/Game/Blueprints/UI/TemplateWidget/TPWBP_StatusBar.TPWBP_StatusBar'}
$oldNames=@('WBP_PlayerHealthBar','WBP_PlayerManaBar')
$pattern='OnCurrentHealthChanged|OnHealthTextChangedDelegate|OnCurrentManaChanged|OnManaTextChanged|GetWBP_Player(?:Health|Mana)Bar'
function Require($Condition,[string]$Message){if(!$Condition){throw $Message}}
function Call($Set,$Tool,$Arguments=@{}){Invoke-PartyHudEditorTool $session.port $Set $Tool $Arguments -RequestTimeoutSeconds 45}
function OrderedValue($Value){
    if($null -eq $Value){return $null}
    if($Value -is [Collections.IDictionary]){$r=[ordered]@{};foreach($k in @($Value.Keys|Sort-Object)){$r[$k]=OrderedValue $Value[$k]};return $r}
    if($Value -is [pscustomobject]){$r=[ordered]@{};foreach($p in @($Value.PSObject.Properties|Sort-Object Name)){$r[$p.Name]=OrderedValue $p.Value};return $r}
    if($Value -is [Collections.IEnumerable] -and $Value -isnot [string]){$items=@(foreach($v in $Value){OrderedValue $v});return ,$items}
    return $Value
}
function Json($Value){ConvertTo-Json -InputObject (OrderedValue $Value) -Depth 70 -Compress}
function Save($Name,$Value){[IO.File]::WriteAllText((Join-Path $OutputRoot $Name),(ConvertTo-Json -InputObject $Value -Depth 80),[Text.UTF8Encoding]::new($false))}
function Props($Ref){$schema=(Call $objects 'list_properties' @{instance=$Ref})|ConvertFrom-Json;(Call $objects 'get_properties' @{instance=$Ref;properties=@($schema.PSObject.Properties.Name)})|ConvertFrom-Json}
function SetProps($Ref,$Values){
    $current=Props $Ref
    foreach($p in $Values.Keys){Require ($null -ne $current.PSObject.Properties[$p]) ('Unknown exact property: '+$p)}
    $null=Call $objects 'set_properties' @{instance=$Ref;values=(Json $Values)}
    $after=Props $Ref
    foreach($p in $Values.Keys){Require ((Json $after.$p) -ceq (Json $Values[$p])) ('Property did not apply: '+$p)}
}
function CopyProps($Ref,$Expected,[string[]]$Exclude=@()){
    $current=Props $Ref;$values=[ordered]@{}
    foreach($p in $Expected.PSObject.Properties){
        if($p.Name -in (@('slot','slots','content','contentSlot','widgetTree','namedSlotBindings')+$Exclude)){continue}
        # Never transplant a pointer into another blueprint's widget tree.
        if((Json $p.Value) -match 'TPWBP_StatusBar[^"\r\n]*:WidgetTree'){continue}
        Require ($null -ne $current.PSObject.Properties[$p.Name]) ('Class property differs: '+$p.Name)
        if((Json $current.($p.Name)) -cne (Json $p.Value)){$values[$p.Name]=$p.Value}
    }
    if($values.Count){SetProps $Ref $values}
}
function Widgets($BP){$result=[ordered]@{};foreach($w in (Call $umg 'GetWidgets' @{widgetBlueprint=$BP}).widgets){if($w.widget.refPath){$result[$w.widgetName]=@{info=$w;properties=(Props $w.widget);slot=$(if($w.slot.refPath){Props $w.slot}else{$null})}}};return ,$result}
function Graphs($BP){$r=@();foreach($g in @(Call $blueprints 'list_graphs' @{blueprint=$BP})){$nodes=@(Call $blueprints 'find_nodes' @{graph=$g;title=''});$r+=@{graph=$g;nodes=@(Call $blueprints 'get_node_infos' @{nodes=$nodes})}};return ,$r}
function ExecPin($Pin){$Pin.type_id -in @((-join @([char]0xC2E4,[char]0xD589)),'exec','Execution')}
function AssertIdle([switch]$AllowDirty){
    Require (!(Call 'EditorToolset.EditorAppToolset' 'IsPIERunning')) 'Preserving PIE'
    if(!$AllowDirty){Require (!(Call $assets 'is_dirty' @{asset_path=$hudPath})) 'Preserving dirty HUD'}
    $foreign=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^UnrealEditor(-Cmd)?\.exe$' -and $_.ProcessId -ne $session.pid -and $_.CommandLine.Contains((Join-Path $workspace 'Project_RPG.uproject'))})
    Require (!$foreign.Count) 'Original-project editor appeared; preserving possible unsaved assets'
}
Require (@(Get-Content -Encoding UTF8 -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') | Where-Object {$_ -match '^\|[^|]+ / hud-vitals-asset-20261101 \|'}).Count -eq 1) 'Ownership row missing'
AssertIdle
$before=Widgets $hud;$graphs=Graphs $hud
if($before.Contains('PlayerVitals')){
    foreach($name in $oldNames){Require (!$before.Contains($name)) 'Both old and new vitals exist'}
    Require (!@($graphs.nodes | Where-Object {$_.type_id -match $pattern}).Count) 'Old health/mana graph remains'
    $skin=Widgets $module
    foreach($name in @('HealthBar','ManaBar','HealthText','ManaText')){Require ($skin.Contains($name)) ('Bound control missing: '+$name)}
    Save 'VitalsRepeat.json' @{changed=$false;hudWidgets=$before.Count;moduleWidgets=$skin.Count}
    Write-Output 'Already extracted; no assets saved'
    return
}
foreach($name in $oldNames){Require ($before.Contains($name)) ('Source child missing: '+$name)}
Require (!(Test-Path -LiteralPath (Join-Path $workspace 'Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset'))) 'Existing module requires review'
$template=Widgets $status
Require ($template.Count -eq 6 -and $template.Contains('RPGSizeBox_Base') -and $template.Contains('ProgressBar_Main') -and $template.Contains('ProgressText')) 'Status template changed'
$event=@($graphs | Where-Object {$_.graph.refPath.EndsWith(':EventGraph')});Require ($event.Count -eq 1) 'Unexpected graphs'
$map=@{};foreach($n in $event[0].nodes){$map[$n.node.refPath]=$n}
$remove=@{};$queue=[Collections.Generic.Queue[string]]::new()
foreach($n in $event[0].nodes){if($n.type_id -match $pattern){$remove[$n.node.refPath]=$true;if($n.type_id -notmatch '\|Assign'){$queue.Enqueue($n.node.refPath)}}}
while($queue.Count){
    $n=$map[$queue.Dequeue()]
    foreach($p in $n.output_pins){foreach($c in $p.connected_pins){
        $key=$c.node.refPath;if($remove.ContainsKey($key)){continue};$target=$map[$key];Require ($target) 'Cross-graph edge'
        if($target.type_id -match '\|Assign' -and $target.type_id -notmatch $pattern){continue}
        $remove[$key]=$true;$queue.Enqueue($key)
    }}
}
do{
    $changed=$false
    foreach($n in $event[0].nodes){
        if($remove.ContainsKey($n.node.refPath) -or @(@($n.input_pins)+@($n.output_pins) | Where-Object {ExecPin $_}).Count){continue}
        $targets=@($n.output_pins.connected_pins.node.refPath | Where-Object {$_})
        if($targets.Count -and !@($targets | Where-Object {!$remove.ContainsKey($_)}).Count){$remove[$n.node.refPath]=$true;$changed=$true}
    }
}while($changed)
function FindExit($Key,$Visited){Require (!$Visited.ContainsKey($Key)) 'Cycle in old exec chain';$Visited[$Key]=$true;foreach($p in $map[$Key].output_pins | Where-Object {ExecPin $_}){foreach($c in $p.connected_pins){if($remove.ContainsKey($c.node.refPath)){FindExit $c.node.refPath $Visited.Clone()}else{$c}}}}
$bridges=@()
foreach($n in $event[0].nodes | Where-Object {!$remove.ContainsKey($_.node.refPath)}){foreach($p in $n.output_pins){foreach($c in $p.connected_pins){if($remove.ContainsKey($c.node.refPath) -and (ExecPin $p)){foreach($exit in @(FindExit $c.node.refPath @{})){$bridges+=@{output_pin=$p.pin_id;input_pin=$exit}}}}}}
$forbidden=@($remove.Keys | ForEach-Object {$map[$_]} | Where-Object {$_.type_id -match 'Identity|Rage|Notice|QuickSlot|RewardMail|GetWBP_ProgressBar|BrightProgressBar'})
Require (!$forbidden.Count) ('Removal crosses other feature: '+($forbidden.type_id -join ','))
$siblings=@($before.Values | Where-Object {$_.info.parent.refPath -eq $before[$oldNames[0]].info.parent.refPath})
$names=@($siblings | ForEach-Object {$_.info.widgetName})
$at=[Array]::IndexOf($names,$oldNames[0]);Require ($at -ge 0 -and [Array]::IndexOf($names,$oldNames[1]) -eq $at+1) 'Review non-adjacent HP/MP canvas order'
foreach($name in $oldNames){Require ((Json $before[$name].properties.colorAndOpacity) -ceq (Json @{r=1;g=1;b=1;a=1})) 'Review non-neutral widget tint'}
$plan=@{removedNodes=@($remove.Keys|Sort-Object|ForEach-Object {@{node=$_;type=$map[$_].type_id}});bridges=$bridges;insertionIndex=$at;oldSlots=@($oldNames|ForEach-Object {$before[$_].slot});moduleClass='/Script/Project_RPG.RPGPlayerVitalsWidget';modulePath=$modulePath}
Save 'VitalsPlan.json' $plan
if(!$Apply){Write-Output ('Planned nodes='+$remove.Count+'; bridges='+$bridges.Count+'; skinWidgets=13');return}
AssertIdle
$disk=Join-Path $workspace 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset';$hash=(Get-FileHash -LiteralPath $disk).Hash
$backup=Join-Path $session.root 'Temp/HUD-before.uasset';Copy-Item -LiteralPath $disk -Destination $backup
Save 'VitalsBefore.json' @{hud=$before;status=$template;graphs=$graphs;hudHash=$hash}
$created=Call $umg 'CreateWidgetBlueprint' @{folderPath='/Game/Blueprints/UI/PlayerHUD/Modules';assetName='WBP_PlayerVitals';parentClass=@{refPath='/Script/Project_RPG.RPGPlayerVitalsWidget'}}
Require ($created.refPath) 'Module creation failed'
$root=Call $umg 'AddWidget' @{widgetBlueprint=$module;widgetClass=@{refPath='/Script/UMG.CanvasPanel'};widgetDisplayName='VitalsCanvas'}
$skinMap=@{}
foreach($kind in @('Health','Mana')){
    $source=$before[$('WBP_Player'+$kind+'Bar')]
    foreach($entry in $template.GetEnumerator()){
        $old=$entry.Value;$name=$kind+'_'+$entry.Key
        if($entry.Key -eq 'ProgressBar_Main'){$name=$kind+'Bar'}
        if($entry.Key -eq 'ProgressText'){$name=$kind+'Text'}
        $class=$old.info.widgetClassPath.refPath
        # The existing primitive Blueprint presets are not supported by AddWidget.
        # Copy their complete reflected appearance onto the same native primitive.
        if($class -eq '/Game/Blueprints/UI/TemplateWidget/RPGSizeBox.RPGSizeBox'){$class='/Script/UMG.SizeBox'}
        elseif($class -eq '/Game/Blueprints/UI/TemplateWidget/RPGTextBlock.RPGTextBlock'){$class='/Script/UMG.TextBlock'}
        elseif($class.StartsWith('/Game/') -and !$class.EndsWith('_C')){$class+='_C'}
        $parent=if($entry.Key -eq 'RPGSizeBox_Base'){$root.widget}else{$skinMap[$kind+'|'+$old.info.parent.refPath].widget}
        Require ($parent.refPath) 'Template parent order changed'
        $added=Call $umg 'AddWidget' @{widgetBlueprint=$module;widgetClass=@{refPath=$class};widgetDisplayName=$name;parentWidget=$parent;childIndex=-1}
        $skinMap[$kind+'|'+$old.info.widget.refPath]=$added
        CopyProps $added.widget $old.properties
        if($entry.Key -eq 'RPGSizeBox_Base'){
            CopyProps $added.slot $source.slot
            SetProps $added.widget @{widthOverride=$source.properties.'in Width Override';heightOverride=$source.properties.'in Height Override';bOverride_WidthOverride=$true;bOverride_HeightOverride=$true}
            $base=[ordered]@{};foreach($p in @('renderTransform','renderTransformPivot','visibility','renderOpacity','bIsEnabled','clipping','flowDirectionPreference')){$base[$p]=$source.properties.$p};SetProps $added.widget $base
        }else{CopyProps $added.slot $old.slot}
        if($entry.Key -eq 'ProgressBar_Main'){SetProps $added.widget @{widgetStyle=$source.properties.statusBarStyle;fillColorAndOpacity=$source.properties.statusBarPreviewFillColor}}
    }
}
Require (Call $umg 'CompileWidgetBlueprint' @{widgetBlueprint=$module}) 'Module compile failed; parent unedited'
$instance=Call $umg 'AddWidget' @{widgetBlueprint=$hud;widgetClass=@{refPath=$modulePath+'.WBP_PlayerVitals_C'};widgetDisplayName='PlayerVitals';parentWidget=$before[$oldNames[0]].info.parent;childIndex=$at}
SetProps $instance.slot @{layoutData=@{offsets=@{left=0;top=0;right=0;bottom=0};anchors=@{minimum=@{x=0;y=0};maximum=@{x=1;y=1}};alignment=@{x=0;y=0}};bAutoSize=$false;zOrder=$before[$oldNames[0]].slot.zOrder}
foreach($bridge in $bridges){$null=Call $blueprints 'connect_pins' $bridge}
foreach($key in @($remove.Keys)){$null=Call $blueprints 'delete_node' @{node=@{refPath=$key}}}
foreach($name in $oldNames){$null=Call $umg 'RemoveWidget' @{widgetBlueprint=$hud;widget=$before[$name].info.widget}}
Require (Call $umg 'CompileWidgetBlueprint' @{widgetBlueprint=$hud}) 'Parent compile failed; changes unsaved'
$after=Widgets $hud;$afterGraphs=Graphs $hud;$skin=Widgets $module
Require ($after.Count -eq $before.Count-1 -and $skin.Count -eq 13) 'Unexpected widget count'
foreach($name in $before.Keys | Where-Object {$_ -notin $oldNames}){Require ($after.Contains($name) -and (Json $before[$name]) -ceq (Json $after[$name])) ('Unrelated widget changed: '+$name)}
Require (!@($afterGraphs.nodes | Where-Object {$_.type_id -match $pattern}).Count) 'Old vitals references remain'
foreach($g in $graphs | Where-Object {!$_.graph.refPath.EndsWith(':EventGraph')}){Require ((Json $g) -ceq (Json @($afterGraphs|Where-Object {$_.graph.refPath -eq $g.graph.refPath})[0])) 'Unrelated function changed'}
function Canonical($n){foreach($p in @($n.input_pins)+@($n.output_pins)){$p.connected_pins=@($p.connected_pins|Sort-Object {Json $_})};Json $n}
$afterMap=@{};foreach($n in ($afterGraphs|Where-Object {$_.graph.refPath.EndsWith(':EventGraph')}).nodes){$afterMap[$n.node.refPath]=$n}
foreach($n in $event[0].nodes|Where-Object {!$remove.ContainsKey($_.node.refPath)}){
    $expected=(Json $n)|ConvertFrom-Json
    foreach($pin in @($expected.input_pins)+@($expected.output_pins)){
        $pin.connected_pins=@($pin.connected_pins|Where-Object {!$remove.ContainsKey($_.node.refPath)})
        foreach($b in $bridges){if((Json $pin.pin_id) -eq (Json $b.output_pin)){$pin.connected_pins+=$b.input_pin};if((Json $pin.pin_id) -eq (Json $b.input_pin)){$pin.connected_pins+=$b.output_pin}}
    }
    Require ($afterMap.ContainsKey($n.node.refPath) -and (Canonical $expected) -ceq (Canonical $afterMap[$n.node.refPath])) ('Unrelated node changed: '+$n.type_id)
}
AssertIdle -AllowDirty
Require ((Get-FileHash -LiteralPath $disk).Hash -ceq $hash) 'Original HUD changed before save'
$null=Call $assets 'save_assets' @{asset_paths=@($modulePath,$hudPath)}
foreach($path in @($hudPath,$modulePath)){Require (!(Call $assets 'is_dirty' @{asset_path=$path})) ('Asset remains dirty: '+$path)}
Save 'VitalsApplied.json' @{changed=$true;removedNodes=$remove.Count;bridges=$bridges.Count;hud=$after;module=$skin;graphs=$afterGraphs;hudHash=(Get-FileHash -LiteralPath $disk).Hash;moduleHash=(Get-FileHash -LiteralPath (Join-Path $workspace 'Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset')).Hash}
Write-Output ('PASS: native Vitals module saved; HUD '+$before.Count+' -> '+$after.Count+' widgets; nodes retired='+$remove.Count)
