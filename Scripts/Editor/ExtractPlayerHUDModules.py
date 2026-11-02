"""Extract the authored notice into an event-driven native module.

Read-only by default. -RPGExtractHUDNotice saves only the parent and new notice
module. Existing visuals and non-notice graph nodes must remain identical.
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
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='hud_helpers')
HUD = '/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
MODULE = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerNotice'

def require(value, message):
    if not value:
        raise RuntimeError(message)

def graph(bp):
    def pins(items):
        return {p.name: {'value': p.value, 'links': sorted([[c.node.get_name(), str(c.direction), c.index_id] for c in p.connected_pins])} for p in items}
    return {n.get_name(): {'type': i.type_id, 'inputs': pins(i.input_pins), 'outputs': pins(i.output_pins)}
            for g in BP.list_graphs(bp) for n in BP.find_nodes(g) for i in BP.get_node_infos([n])}

def main():
    _, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    params = {str(k).lower(): str(v) for k, v in params.items()}
    apply = any(str(s).lower() == 'rpgextracthudnotice' for s in switches)
    output = pathlib.Path(params['rpghudownerproof'])
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    hud = assets.load_asset(HUD)
    before = helpers['snapshot_widgets'](unreal, hud)
    before_graph = graph(hud)
    tool = unreal.get_default_object(unreal.UMGToolSet)
    result = {'passed': False, 'changed': 'NoticeText' in before, 'saved_assets': []}
    if 'PlayerNotice' in before:
        require('NoticeText' not in before, 'Both old and modular notice exist')
        module = assets.load_asset(MODULE)
        require(module is not None, 'Missing notice module')
    else:
        require('NoticeText' in before and not assets.does_asset_exist(MODULE), 'Review existing notice/module')
        require(before['NoticeText']['class'] == '/Script/UMG.TextBlock', 'Review changed notice control')
        removed = {'K2Node_AssignDelegate_13', 'K2Node_CustomEvent_14', 'K2Node_VariableGet_1',
                   'K2Node_CallFunction_19', 'K2Node_CallFunction_27', 'K2Node_CallFunction_28', 'K2Node_CallFunction_29'}
        removed.update(k for k, n in before_graph.items() if 'OnNoticeTextChanged_Event_' in n['type'])
        require(all(k in before_graph for k in removed), 'Review changed notice graph')
        require(before_graph['K2Node_CallFunction_27']['inputs']['Duration']['value'] == '3.000000', 'Review changed authored duration')
        result['removed_nodes'] = sorted(removed)
        if not apply:
            result['passed'] = True
            output.write_text(json.dumps(result, indent=2), encoding='utf-8')
            return
        require(any(' / hud-remaining-20261102 |' in line and '진행 중' in line for line in
                    (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()), 'Missing reservation')
        require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
        require(not {HUD, MODULE}.intersection(p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Preserve unsaved content')
        require_editable(hud)
        kind = unreal.load_class(None, '/Script/Project_RPG.RPGPlayerNoticeWidget')
        require(kind is not None, 'Build native notice widget first')
        module = tool.call_method('CreateWidgetBlueprint', ('/Game/Blueprints/UI/PlayerHUD/Modules', 'WBP_PlayerNotice', kind))
        require(isinstance(module, unreal.WidgetBlueprint), 'Cannot create notice Blueprint')
        added = tool.call_method('AddWidget', (module, unreal.TextBlock.static_class(), 'NoticeText', None, -1))
        text = added.get_editor_property('widget')
        require(isinstance(text, unreal.TextBlock), 'Cannot create native notice text control')
        style = copy.deepcopy(before['NoticeText']['properties'])
        for key in ('Slot', 'Navigation', 'OnAllFontFacesFinishLoading'):
            style.pop(key, None)
        style['Text'] = ''
        current = helpers['object_properties'](unreal, text)
        for key, value in style.items():
            if current.get(key) != value:
                require(unreal.ToolsetLibrary.set_object_properties(text, json.dumps({key: value})), 'Cannot copy notice style: ' + key)
        unreal.BlueprintEditorLibrary.compile_blueprint(module)
        require(module.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Module compile failed')
        infos = helpers['widget_infos'](unreal, hud)
        original = infos['NoticeText'].get_editor_property('widget')
        parent = infos['NoticeText'].get_editor_property('parent')
        index = list(parent.get_all_children()).index(original)
        added = tool.call_method('AddWidget', (hud, module.generated_class(), 'PlayerNotice', parent, index))
        require(unreal.ToolsetLibrary.set_object_properties(added.get_editor_property('slot'), json.dumps(before['NoticeText']['slot_properties'])), 'Cannot preserve notice placement')
        for g in BP.list_graphs(hud):
            for n in BP.find_nodes(g):
                if n.get_name() in removed:
                    BP.delete_node(n)
        tool.call_method('RemoveWidget', (hud, original))
        unreal.BlueprintEditorLibrary.compile_blueprint(hud)
        require(hud.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Parent compile failed')
        after = helpers['snapshot_widgets'](unreal, hud)
        require(set(after) == (set(before) - {'NoticeText'}) | {'PlayerNotice'}, 'Unrelated widget additions/removals')
        for name, old in before.items():
            if name == 'NoticeText':
                continue
            old, current = copy.deepcopy(old), copy.deepcopy(after[name])
            if name == parent.get_name():
                old['properties'].pop('Slots', None); current['properties'].pop('Slots', None)
            require(old == current, 'Unrelated widget changed: ' + name)
        expected = {k: v for k, v in before_graph.items() if k not in removed}
        for node in expected.values():
            for group in ('inputs', 'outputs'):
                for pin in node[group].values():
                    pin['links'] = [link for link in pin['links'] if link[0] not in removed]
        require(graph(hud) == expected, 'Unrelated graph changed')
        for asset, path in ((module, MODULE), (hud, HUD)):
            require(assets.save_loaded_asset(asset, only_if_is_dirty=False), 'Cannot save ' + path)
            result['saved_assets'].append(path)
    for asset in (module, hud):
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        require(asset.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Reload compile failed')
    skin = helpers['snapshot_widgets'](unreal, module)
    require(set(skin) == {'NoticeText'} and skin['NoticeText']['properties']['Text'] == '', 'Review notice skin or placeholder')
    require(not any('OnNoticeTextChanged' in n['type'] or n['type'].endswith('|Delay') for n in graph(hud).values()), 'Legacy notice logic remains')
    result['parent_widgets'] = helpers['snapshot_widgets'](unreal, hud)
    result['module_widgets'] = skin
    result['parent_nodes'] = graph(hud)
    result['passed'] = True
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
