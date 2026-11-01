"""Connect the first slice's fourth skill through existing data-driven input/UI.

Read-only by default; -RPGConfigureGreatSwordFourthSkillSlot permits saving only
IA_QuickSkill4, DA_InputConfig, IMC_Default and the PlayerHUD skill row. Existing
bindings, item cells, identity controls, widget classes and C++ are preserved.
"""
import copy
import hashlib
import importlib.util
import json
import pathlib
import shutil

spec = importlib.util.spec_from_file_location(
    'fourth_skill_helpers', pathlib.Path(__file__).with_name('ConfigureGreatSwordCombatBaseline.py'))
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
require, equivalent = helpers.require, helpers.equivalent
BASE = '/Game/Blueprints/Character/Player/Input/'
CONFIG = BASE + 'DA_InputConfig'
CONTEXT = BASE + 'IMC_Default'
ACTION = BASE + 'Actions/IA_QuickSkill4'
TEMPLATE = BASE + 'Actions/IA_QuickSkill3'
HUD = '/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
SKILL_BAR = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerSkillBar'
CELL = '/Game/Blueprints/UI/QuickSlot/WBP_SkillQuickSlot'
TAG = 'InputTag.QuickSkill.4'
KEY = 'R'
ROW = 'HorizontalBox_SkillQuickSlots'
WIDGET = 'SkillQuickSlot_3'
ROW_WIDTH = 445.0  # Four existing 100px cells, with three 15px gaps.


def object_properties(unreal, obj):
    schema_names = list(json.loads(unreal.ToolsetLibrary.list_struct_properties(obj.get_class())))
    names = [name if name.startswith('b') and len(name) > 1 and name[1].isupper()
             else name[0].upper() + name[1:] for name in schema_names]
    require(names, 'Cannot inspect authored object properties')
    return json.loads(unreal.ToolsetLibrary.get_object_properties(obj, names))


def widget_infos(unreal, blueprint):
    info = unreal.get_default_object(unreal.UMGToolSet).call_method('GetWidgets', (blueprint,))
    return {item.get_editor_property('widget').get_name(): item for item in info.get_editor_property('widgets')
            if item.get_editor_property('widget') is not None}


def snapshot_widgets(unreal, blueprint):
    result = {}
    for name, info in widget_infos(unreal, blueprint).items():
        widget = info.get_editor_property('widget')
        slot, parent = info.get_editor_property('slot'), info.get_editor_property('parent')
        result[name] = {'class': widget.get_class().get_path_name(),
                        'parent': parent.get_name() if parent else None,
                        'properties': object_properties(unreal, widget),
                        'slot_properties': object_properties(unreal, slot) if slot else None}
    return result


def main(output_root=None):
    import unreal
    from toolset_registry.helpers import require_editable

    apply = '-RPGConfigureGreatSwordFourthSkillSlot' in unreal.SystemLibrary.get_command_line()
    require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Finish PIE first')
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    config, context, hud = (assets.load_asset(p) for p in [CONFIG, CONTEXT, HUD])
    template, cell = assets.load_asset(TEMPLATE), assets.load_asset(CELL)
    require(isinstance(config, unreal.DataAsset_InputConfig) and isinstance(context, unreal.InputMappingContext)
            and isinstance(hud, unreal.WidgetBlueprint) and isinstance(template, unreal.InputAction)
            and isinstance(cell, unreal.WidgetBlueprint), 'Missing current skill input/UI content')
    before_config, before_context = object_properties(unreal, config), object_properties(unreal, context)
    before_widgets = snapshot_widgets(unreal, hud)
    # The extracted bar owns its cells. The HUD only owns the module's placement.
    modular = 'PlayerSkillBar' in before_widgets
    skill_owner = assets.load_asset(SKILL_BAR) if modular else hud
    require(isinstance(skill_owner, unreal.WidgetBlueprint), 'Missing skill-bar module')
    before_skill_widgets = snapshot_widgets(unreal, skill_owner) if modular else before_widgets
    layout_widget = 'PlayerSkillBar' if modular else ROW
    infos = widget_infos(unreal, skill_owner)
    require(ROW in infos and 'SkillQuickSlot_2' in infos, 'Missing authored skill row')
    row = infos[ROW].get_editor_property('widget')
    row_slot = widget_infos(unreal, hud)[layout_widget].get_editor_property('slot')
    require(isinstance(row, unreal.HorizontalBox) and isinstance(row_slot, unreal.CanvasPanelSlot), 'Review changed skill layout')
    for index in range(3):
        name = 'SkillQuickSlot_' + str(index)
        require(name in before_skill_widgets, 'Missing existing skill cell')
        props = before_skill_widgets[name]['properties']
        require(props['SlotIndex'] == index and props['bIsSkillSlot'] and not props['bCanDrag']
                and before_skill_widgets[name]['class'] == cell.generated_class().get_path_name(), 'Preserving changed skill cell settings')
    existing_fourth = WIDGET in infos
    if existing_fourth:
        props = before_skill_widgets[WIDGET]['properties']
        require(props['SlotIndex'] == 3 and props['bIsSkillSlot'] and not props['bCanDrag']
                and before_skill_widgets[WIDGET]['class'] == cell.generated_class().get_path_name()
                and before_skill_widgets[WIDGET]['parent'] == ROW, 'Preserving a customized fourth cell')
    require(not modular or existing_fourth, 'Author new module cells in the skill-bar module, not this legacy migration')
    require(row.get_children_count() == (4 if existing_fourth else 3), 'Preserving extra skill-row children')
    layout = copy.deepcopy(before_widgets[layout_widget]['slot_properties']['LayoutData'])
    require(layout['offsets']['right'] >= 400.0 and layout['offsets']['right'] <= ROW_WIDTH,
            'Review a customized skill-row width')
    # Keep the new row clear of the existing identity icon; no other controls move.
    identity = before_widgets['WBP_AbilityIconSlot_RageSkill']['slot_properties']['LayoutData']
    require(layout['offsets']['left'] + ROW_WIDTH + 15 <= identity['offsets']['left'], 'Fourth cell would overlap identity controls')
    layout['offsets']['right'] = ROW_WIDTH
    config_rows = copy.deepcopy(before_config['NativeInputActions'])
    expected_entry = {'inputTag': {'tagName': TAG}, 'inputAction': {'refPath': ACTION + '.IA_QuickSkill4'},
                      'inputActionName': 'QuickSkill.4', 'bShowInInputOptionMenu': True}
    matching = [entry for entry in config_rows if entry['inputTag']['tagName'] == TAG]
    require(len(matching) <= 1, 'Duplicate fourth skill binding')
    if matching:
        require(equivalent(matching[0], expected_entry), 'Preserving a customized fourth binding')
    else:
        require(not any(entry['inputAction'] == expected_entry['inputAction'] for entry in config_rows), 'Action is already owned by another input tag')
        config_rows.append(expected_entry)
    maps = before_context['DefaultKeyMappings']['mappings']
    action_maps = [mapping for mapping in maps if mapping['action'] == expected_entry['inputAction']]
    require(len(action_maps) <= 1, 'Preserving multiple fourth-skill keys')
    if action_maps:
        require(action_maps[0]['key'] == KEY and not action_maps[0]['triggers'] and not action_maps[0]['modifiers'],
                'Preserving a customized fourth-skill mapping')
    for name in ['IMC_Default', 'IMC_GreatSword', 'IMC_TargetLock']:
        other = assets.load_asset(BASE + name)
        data = json.loads(unreal.ToolsetLibrary.get_object_properties(other, ['DefaultKeyMappings']))
        require(not any(m['key'] == KEY and m['action'] != expected_entry['inputAction']
                        for m in data['DefaultKeyMappings']['mappings']), 'R is already assigned in ' + name)
    action = assets.load_asset(ACTION) if assets.does_asset_exist(ACTION) else None
    template_props = object_properties(unreal, template)
    if action:
        require(isinstance(action, unreal.InputAction) and equivalent(object_properties(unreal, action),
                json.loads(json.dumps(template_props).replace(TEMPLATE, ACTION))), 'Preserving a customized fourth action')
    require(before_config['DefaultMappingContext'] == {'refPath': CONTEXT + '.IMC_Default'}, 'Different default mapping context')
    needs_change = not matching or not action_maps or not existing_fourth or not action or not equivalent(
        before_widgets[layout_widget]['slot_properties']['LayoutData'], layout)
    root = pathlib.Path(output_root) if output_root is not None else (
        pathlib.Path(unreal.Paths.project_saved_dir()) / 'ContentChecks/GreatSwordFourthSkillSlot')
    root.mkdir(parents=True, exist_ok=True)
    plan = {'apply': apply, 'needs_change': bool(needs_change), 'key': KEY, 'slot_index': 3,
            'new_widget': WIDGET, 'row_width': ROW_WIDTH, 'actual_saved': []}
    (root / 'Prepared.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
    (root / ('Before.json' if apply else 'ReadOnly.json')).write_text(json.dumps({
        'config': before_config, 'context': before_context, 'widgets': before_widgets}, ensure_ascii=False, indent=2), encoding='utf-8')
    if not apply:
        unreal.log('GREATSWORD_FOURTH_SKILL INSPECT NeedsChange=' + str(bool(needs_change)) + ' NoAssetsSaved=1')
        return
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    disks = []
    for asset, package in [(config, CONFIG), (context, CONTEXT), (hud, HUD), (action, ACTION)]:
        require(package not in dirty, 'Preserving unsaved content: ' + package)
        if asset:
            require_editable(asset)
        disk = pathlib.Path(unreal.Paths.project_content_dir()) / (package[len('/Game/'):] + '.uasset')
        digest = hashlib.sha256(disk.read_bytes()).hexdigest() if disk.exists() else None
        if digest:
            backup = root / (disk.stem + '.' + digest[:12] + '.uasset.backup')
            if not backup.exists():
                shutil.copy2(disk, backup)
            require(hashlib.sha256(backup.read_bytes()).hexdigest() == digest, 'Backup differs')
        disks.append((disk, digest))
    saved = []
    if not action:
        action = assets.duplicate_asset(TEMPLATE, ACTION)
        require(isinstance(action, unreal.InputAction), 'Could not duplicate the skill input action')
        require(equivalent(object_properties(unreal, action), json.loads(json.dumps(template_props).replace(TEMPLATE, ACTION))),
                'Cloned action settings differ')
        saved.append(action)
    if not matching:
        require(unreal.ToolsetLibrary.set_object_properties(config, json.dumps({'NativeInputActions': config_rows})), 'Could not add native input binding')
        saved.append(config)
    if not action_maps:
        mapping_data = copy.deepcopy(before_context['DefaultKeyMappings'])
        mapping_data['mappings'].append({'triggers': [], 'modifiers': [], 'action': expected_entry['inputAction'],
            'key': KEY, 'settingBehavior': 'InheritSettingsFromAction', 'playerMappableKeySettings': 'None'})
        require(unreal.ToolsetLibrary.set_object_properties(context, json.dumps({'DefaultKeyMappings': mapping_data})),
                'Could not add fourth-skill key mapping')
        saved.append(context)
    if not existing_fourth:
        added = unreal.get_default_object(unreal.UMGToolSet).call_method('AddWidget',
                    (hud, cell.generated_class(), WIDGET, row, -1))
        widget = added.get_editor_property('widget')
        slot = added.get_editor_property('slot')
        require(isinstance(widget, unreal.RPGQuickSlotWidget) and isinstance(slot, unreal.HorizontalBoxSlot), 'Could not add the skill cell')
        require(unreal.ToolsetLibrary.set_object_properties(widget, json.dumps({
            'SlotIndex': 3, 'bIsSkillSlot': True, 'InputText': '', 'bCanDrag': False, 'Visibility': 'Visible'})), 'Could not configure skill cell')
        slot_properties = copy.deepcopy(before_widgets['SkillQuickSlot_2']['slot_properties'])
        slot_properties['Padding'] = {'left': 15.0, 'top': 0.0, 'right': 0.0, 'bottom': 0.0}
        require(unreal.ToolsetLibrary.set_object_properties(slot, json.dumps(slot_properties)), 'Could not configure skill-cell spacing')
        saved.append(hud)
    if not equivalent(before_widgets[layout_widget]['slot_properties']['LayoutData'], layout):
        require(unreal.ToolsetLibrary.set_object_properties(row_slot, json.dumps({'LayoutData': layout})), 'Could not size the skill row')
        if hud not in saved:
            saved.append(hud)
    if hud in saved:
        unreal.BlueprintEditorLibrary.compile_blueprint(hud)
        require(hud.get_editor_property('status') in (unreal.BlueprintStatus.BS_UP_TO_DATE,
                unreal.BlueprintStatus.BS_UP_TO_DATE_WITH_WARNINGS), 'HUD compilation failed')
    current_config, current_context = object_properties(unreal, config), object_properties(unreal, context)
    expected_config = copy.deepcopy(before_config)
    expected_config['NativeInputActions'] = config_rows
    require(equivalent(current_config, expected_config), 'Unrelated input configuration changed; not saved')
    expected_maps = copy.deepcopy(maps)
    if not action_maps:
        expected_maps.append({'triggers': [], 'modifiers': [], 'action': expected_entry['inputAction'], 'key': KEY,
                              'settingBehavior': 'InheritSettingsFromAction', 'playerMappableKeySettings': 'None'})
    expected_context = copy.deepcopy(before_context)
    expected_context['DefaultKeyMappings']['mappings'] = expected_maps
    require(equivalent(current_context, expected_context), 'Unrelated input mappings changed; not saved')
    current_widgets = snapshot_widgets(unreal, hud)
    expected_widgets = copy.deepcopy(before_widgets)
    expected_widgets[layout_widget]['slot_properties']['LayoutData'] = layout
    for name, before in expected_widgets.items():
        require(name in current_widgets and equivalent(current_widgets[name], before), 'Existing HUD widget changed: ' + name)
    require(set(current_widgets) == set(before_widgets) | (set() if modular else {WIDGET}), 'Unexpected HUD widgets changed')
    current_skill_widgets = snapshot_widgets(unreal, skill_owner) if modular else current_widgets
    if modular:
        require(equivalent(current_skill_widgets, before_skill_widgets), 'Skill-bar module changed during input migration')
    require(current_skill_widgets[WIDGET]['properties']['SlotIndex'] == 3
            and current_skill_widgets[WIDGET]['properties']['bIsSkillSlot'], 'Fourth cell lost its binding')
    for disk, digest in disks:
        require((hashlib.sha256(disk.read_bytes()).hexdigest() if disk.exists() else None) == digest, 'Asset changed during authoring')
    for asset in saved:
        require(assets.save_loaded_asset(asset, only_if_is_dirty=False), 'Could not save ' + asset.get_path_name())
    packages = [CONFIG, CONTEXT, HUD, ACTION]
    require(not any(str(p.get_name()) in packages for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Target remains dirty')
    plan.update({'actual_saved': [a.get_path_name() for a in saved], 'existing_widget_count_preserved': len(before_widgets),
                 'existing_actions_keys_and_other_hud_preserved': True, 'hud_compile_status': str(hud.get_editor_property('status')),
                 'assets': [{'path': str(disk), 'before_sha256': digest,
                            'after_sha256': hashlib.sha256(disk.read_bytes()).hexdigest()} for disk, digest in disks]})
    (root / 'Applied.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('GREATSWORD_FOURTH_SKILL PASS Saved=' + str(len(saved)) + ' Key=R SlotIndex=3')


if __name__ == '__main__':
    main()
