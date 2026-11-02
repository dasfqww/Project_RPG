"""Move the existing identity visuals, event bindings and animations together.

Read-only by default. -RPGExtractHUDIdentity saves the module and parent after
validating that all animations target only the two extracted identity widgets.
No gameplay/identity rules change. Native HUD possession owns module lifetime.
"""
import copy
import hashlib
import json
import pathlib
import runpy
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP
from toolset_registry.helpers import require_editable

workspace = pathlib.Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='identity_helpers')
graph = runpy.run_path(str(workspace / 'Scripts/Editor/ExtractPlayerHUDModules.py'), run_name='identity_graph_helpers')['graph']
HUD = '/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
MODULE = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerIdentity'
widgets = {'WBP_IdentityGaugeBar', 'WBP_AbilityIconSlot_RageSkill'}
keep = {'K2Node_Event_3', 'K2Node_VariableSet_0', 'K2Node_VariableGet_12', 'K2Node_CustomEvent_1',
        'K2Node_AssignDelegate_1', 'K2Node_VariableGet_2', 'K2Node_CallFunction_2', 'K2Node_VariableGet_29',
        'K2Node_VariableGet_28', 'K2Node_CustomEvent_12', 'K2Node_AssignDelegate_11', 'K2Node_VariableGet_33',
        'K2Node_VariableGet_34', 'K2Node_CallFunction_21', 'K2Node_CallFunction_24', 'K2Node_CustomEvent_13',
        'K2Node_AssignDelegate_12', 'K2Node_CallFunction_17', 'K2Node_CallFunction_6'}
mail = {'K2Node_ComponentBoundEvent_0', 'K2Node_CallFunction_30', 'K2Node_DynamicCast_0', 'K2Node_CallFunction_31'}

def require(value, message):
    if not value:
        raise RuntimeError(message)

def canonical(value, source, target):
    return json.loads(json.dumps(value).replace(source, target))

def main():
    _, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    params = {str(k).lower(): str(v) for k, v in params.items()}
    apply = any(str(s).lower() == 'rpgextracthudidentity' for s in switches)
    output = pathlib.Path(params['rpghudownerproof'])
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    hud = assets.load_asset(HUD)
    before = helpers['snapshot_widgets'](unreal, hud)
    native = unreal.RPGHUDModuleTools
    tool = unreal.get_default_object(unreal.UMGToolSet)
    result = {'passed': False, 'changed': 'PlayerIdentity' not in before, 'saved_assets': []}
    if 'PlayerIdentity' in before:
        require(not widgets.intersection(before), 'Old identity widgets remain')
        module = assets.load_asset(MODULE)
    else:
        require(widgets.issubset(before) and 'PlayerNotice' in before, 'Review changed parent or extract notice first')
        before_graph = graph(hud)
        require(keep.issubset(before_graph) and mail.issubset(before_graph), 'Review changed identity graph')
        animation_info = list(native.describe_animations(hud))
        require(animation_info[0] == '__PROPERTY_BINDINGS__|0', 'Review Designer property bindings')
        for row in animation_info[1:]:
            parts = row.split('|')
            require(len(parts) == 4 and parts[1] in widgets and parts[3] == '0', 'Animation extends beyond identity: '+row)
        result['animations'] = animation_info
        # Check direct referring assets for actual calls before retiring orphan functions.
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.search_all_assets(True)
        refs = registry.get_referencers(HUD, unreal.AssetRegistryDependencyOptions(True, True, True, True, True)) or []
        for ref in refs:
            asset = assets.load_asset(str(ref))
            if isinstance(asset, unreal.Blueprint):
                retired = {'UpdateValue', 'OnCurrentRageChanged_Event_0', 'OnCurrentRageChanged_Event_1', 'OnCurrentRageChanged_Event_2',
                           'OnActivateIdentity_Event_0', 'OnActivateIdentity_Event_1', 'OnActivateIdentity_Event_2',
                           'OnDeactivateIdentity_Event_0', 'OnDeactivateIdentity_Event_1', 'OnDeactivateIdentity_Event_2'}
                require(not any(not n['type'].startswith('AddEvent|Custom|') and n['type'].rsplit('|', 1)[-1] in retired
                                for n in graph(asset).values()),
                        'Review callers in '+str(ref))
        result['referencers_checked'] = [str(r) for r in refs]
        if not apply:
            result['passed'] = True
            output.write_text(json.dumps(result, indent=2), encoding='utf-8')
            return
        require(any(' / hud-remaining-20261102 |' in row and '진행 중' in row for row in
                    (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()), 'Missing reservation')
        recovery = None
        if assets.does_asset_exist(MODULE):
            recovery_path = workspace / 'Saved/BuildChecks/HudRemaining-20261102/IdentityRecovery.json'
            require(recovery_path.exists(), 'Preserve existing module')
            recovery = json.loads(recovery_path.read_text(encoding='utf-8-sig'))
            disk = workspace / 'Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerIdentity.uasset'
            parent_disk = workspace / 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset'
            require(recovery['owner'] == 'hud-remaining-20261102' and recovery['module'] == MODULE
                    and hashlib.sha256(disk.read_bytes()).hexdigest().upper() == recovery['moduleSHA256']
                    and hashlib.sha256(parent_disk.read_bytes()).hexdigest().upper() == recovery['parentSHA256'], 'Partial asset recovery inputs changed')
        require(not {HUD, MODULE}.intersection(p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Preserve unsaved assets')
        require_editable(hud)
        module = assets.load_asset(MODULE) if recovery else unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset('WBP_PlayerIdentity', '/Game/Blueprints/UI/PlayerHUD/Modules', hud)
        require(isinstance(module, unreal.WidgetBlueprint), 'Cannot duplicate authored identity')
        infos = helpers['widget_infos'](unreal, module)
        infos['CanvasPanel_30'].get_editor_property('widget').set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
        for name, info in list(infos.items()):
            if name not in widgets and info.get_editor_property('parent') is not None and info.get_editor_property('parent').get_name() == 'CanvasPanel_30':
                tool.call_method('RemoveWidget', (module, info.get_editor_property('widget')))
        for g in BP.list_graphs(module):
            if g.get_name() == 'UpdateValue':
                unreal.BlueprintEditorLibrary.remove_graph(module, g)
            else:
                for n in BP.find_nodes(g):
                    if n.get_name() not in keep:
                        BP.delete_node(n)
        unreal.BlueprintEditorLibrary.compile_blueprint(module)
        require(module.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Identity module compile failed')
        defaults = unreal.get_default_object(module.generated_class())
        if hasattr(defaults, 'set_is_focusable'):
            defaults.set_is_focusable(False)
        require(set(helpers['snapshot_widgets'](unreal, module)) == widgets | {'CanvasPanel_30'}, 'Unexpected module tree')
        expected_graph = {k: copy.deepcopy(v) for k, v in before_graph.items() if k in keep}
        for n in expected_graph.values():
            for group in ('inputs', 'outputs'):
                for pin in n[group].values():
                    pin['links'] = [link for link in pin['links'] if link[0] in keep]
        require(graph(module) == expected_graph, 'Identity event behavior changed')
        info = helpers['widget_infos'](unreal, hud)
        parent = info['WBP_IdentityGaugeBar'].get_editor_property('parent')
        instance = tool.call_method('AddWidget', (hud, module.generated_class(), 'PlayerIdentity', parent, -1))
        instance.get_editor_property('widget').set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
        layout = {'LayoutData': {'offsets': {'left': 0, 'top': 0, 'right': 0, 'bottom': 0},
                  'anchors': {'minimum': {'x': 0, 'y': 0}, 'maximum': {'x': 1, 'y': 1}}, 'alignment': {'x': 0, 'y': 0}},
                  'bAutoSize': False, 'ZOrder': 0}
        require(unreal.ToolsetLibrary.set_object_properties(instance.get_editor_property('slot'), json.dumps(layout)), 'Cannot position identity module')
        for g in BP.list_graphs(hud):
            if g.get_name() == 'UpdateValue':
                unreal.BlueprintEditorLibrary.remove_graph(hud, g)
            else:
                for n in BP.find_nodes(g):
                    if n.get_name() not in mail:
                        BP.delete_node(n)
        require(native.finalize_identity_animation_extraction(hud, module), 'Animation copies/bindings differ; parent not saved')
        for name in widgets:
            tool.call_method('RemoveWidget', (hud, info[name].get_editor_property('widget')))
        unreal.BlueprintEditorLibrary.remove_unused_variables(hud)
        unreal.BlueprintEditorLibrary.remove_unused_variables(module)
        unreal.BlueprintEditorLibrary.compile_blueprint(hud)
        require(hud.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Parent compile failed')
        after = helpers['snapshot_widgets'](unreal, hud)
        require(set(after) == (set(before) - widgets) | {'PlayerIdentity'}, 'Unexpected parent tree')
        for name, old in before.items():
            if name in widgets:
                continue
            old, current = copy.deepcopy(old), copy.deepcopy(after[name])
            if name == parent.get_name():
                old['properties'].pop('Slots', None); current['properties'].pop('Slots', None)
            require(old == current, 'Unrelated parent widget changed: '+name)
        for name in widgets:
            current = helpers['snapshot_widgets'](unreal, module)[name]
            require(current == canonical(before[name], HUD+'.WBP_PlayerHUD', MODULE+'.WBP_PlayerIdentity'), 'Identity visual changed: '+name)
        require(set(graph(hud)) == mail, 'Unexpected remaining parent logic')
        for asset, path in ((module, MODULE), (hud, HUD)):
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), 'Cannot save '+path)
            result['saved_assets'].append(path)
    for asset in (hud, module):
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        require(asset.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Saved Blueprint compile failed')
    result['parent_nodes'] = graph(hud)
    result['module_nodes'] = graph(module)
    result['module_widgets'] = helpers['snapshot_widgets'](unreal, module)
    result['module_animations'] = list(native.describe_animations(module))
    result['parent_animations'] = list(native.describe_animations(hud))
    result['passed'] = True
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
