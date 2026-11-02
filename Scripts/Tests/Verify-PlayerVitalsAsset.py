"""Reload the saved HUD/Vitals assets and verify native bindings without saving."""
import hashlib
import copy
import json
import pathlib
import runpy

import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


args = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
parameters = {str(k).lower(): str(v) for k, v in args[2].items()}
output = pathlib.Path(parameters['rpgvitalsassetproof'])
before = json.loads(pathlib.Path(parameters['rpgvitalsbefore']).read_text(encoding='utf-8-sig'))
workspace = pathlib.Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='vitals_reload_helpers')
def normalized(value):
    # ToolsetLibrary preserves requested PascalCase property names; MCP returns camelCase.
    if isinstance(value, dict):
        return {k[0].lower()+k[1:]: normalized(v) for k, v in value.items()}
    if isinstance(value, list):
        return [normalized(v) for v in value]
    return value


def snapshot(api, blueprint):
    return {name: normalized(entry) for name, entry in helpers['snapshot_widgets'](api, blueprint).items()}
hud_path = '/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
module_path = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals'
paths = [workspace / 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset',
         workspace / 'Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset']
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
hud, module = [unreal.load_asset(p) for p in (hud_path, module_path)]
require(isinstance(hud, unreal.WidgetBlueprint) and isinstance(module, unreal.WidgetBlueprint), 'Missing WBP')
for bp in (hud, module):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    require(bp.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Compile failed: '+bp.get_name())
widgets, skin = snapshot(unreal, hud), snapshot(unreal, module)
has_shield = 'PlayerShield' in skin
expected_hud = (set(before['hud']) - {'WBP_PlayerHealthBar', 'WBP_PlayerManaBar'}) | {'PlayerVitals'}
for name, replaced in (('PlayerIdentity', {'WBP_IdentityGaugeBar', 'WBP_AbilityIconSlot_RageSkill'}),
                       ('PlayerNotice', {'NoticeText'})):
    if name in widgets:
        expected_hud = (expected_hud - replaced) | {name}
        expected_class = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_' + name + '.WBP_' + name + '_C'
        require(widgets[name]['class'] == expected_class and not replaced.intersection(widgets), 'Unexpected HUD module: '+name)
require(set(widgets) == expected_hud, 'Unexpected parent widgets: missing='+str(expected_hud-set(widgets))+' added='+str(set(widgets)-expected_hud))
require(len(skin) == 14 + int(has_shield), 'Unexpected Vitals widget count: '+str(len(skin)))
require(skin.get('PlayerDebuffs', {}).get('class') == '/Script/Project_RPG.RPGPlayerDebuffBarWidget', 'Native debuff module missing')
if has_shield:
    shield = skin['PlayerShield']
    require(shield['class'] == '/Script/Project_RPG.RPGPlayerShieldWidget', 'Unexpected shield module')
    expected_shield = copy.deepcopy(skin['Health_RPGSizeBox_Base']['slot_properties']['layoutData'])
    expected_shield['offsets']['top'] -= 34
    expected_shield['offsets'].update(right=390, bottom=28)
    require(shield['slot_properties']['layoutData'] == expected_shield and shield['slot_properties']['bAutoSize'] is False,
            'Shield amount/countdown must sit directly above the health bar')
debuff_slot = skin['PlayerDebuffs']['slot_properties']
expected_debuff = copy.deepcopy(skin['Mana_RPGSizeBox_Base']['slot_properties']['layoutData'])
expected_debuff['offsets']['top'] -= 62
expected_debuff['offsets'].update(right=390, bottom=58)
require(debuff_slot['layoutData'] == expected_debuff and debuff_slot['bAutoSize'] is False,
        'Debuff icons/timers must sit directly above the right-hand mana bar')
require('PlayerVitals' in widgets and not {'WBP_PlayerHealthBar', 'WBP_PlayerManaBar'} & widgets.keys(), 'Old parent bars remain')
require(widgets['PlayerVitals']['class'] == module_path+'.WBP_PlayerVitals_C', 'Wrong module class')
layout = widgets['PlayerVitals']['slot_properties']['layoutData']
require(layout == {'offsets': {'left': 0, 'top': 0, 'right': 0, 'bottom': 0},
                   'anchors': {'minimum': {'x': 0, 'y': 0}, 'maximum': {'x': 1, 'y': 1}},
                   'alignment': {'x': 0, 'y': 0}}, 'Module must fill the original HUD canvas')
for kind in ('Health', 'Mana'):
    source = before['hud']['WBP_Player'+kind+'Bar']
    root = skin[kind+'_RPGSizeBox_Base']
    expected_slot = copy.deepcopy(source['slot'])
    expected_slot['layoutData']['offsets']['left'] -= 960
    expected_slot['layoutData']['offsets']['top'] -= 1080
    expected_slot['layoutData']['anchors'] = {'minimum': {'x': 0.5, 'y': 1}, 'maximum': {'x': 0.5, 'y': 1}}
    require(root['slot_properties'] == expected_slot, 'Bottom-relative canvas placement differs: '+kind)
    require(skin[kind+'_ProgressTimeText']['properties']['visibility'] == 'Collapsed', 'Unused template timer must stay hidden: '+kind)
    require(root['properties']['widthOverride'] == source['properties']['in Width Override'] and
            root['properties']['heightOverride'] == source['properties']['in Height Override'], 'Original size differs: '+kind)
    bar = skin[kind+'Bar']['properties']
    require(bar['widgetStyle'] == source['properties']['statusBarStyle'], 'Authored brush/material differs: '+kind)
    require(bar['fillColorAndOpacity'] == source['properties']['statusBarPreviewFillColor'], 'Authored fill tint differs: '+kind)
    label = skin[kind+'Text']['properties']
    for key in ('font', 'colorAndOpacity', 'shadowOffset', 'shadowColorAndOpacity', 'justification', 'visibility'):
        require(label[key] == before['status']['ProgressText']['properties'][key], 'Label appearance differs: '+key)

# Create the actual saved generated classes, so BindWidgetOptional resolution is exercised.
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
module_class = unreal.load_class(None, module_path+'.WBP_PlayerVitals_C')
require(unreal.SystemLibrary.is_valid_class(module_class), 'Generated Vitals class missing')
widget_library = unreal.get_default_object(unreal.WidgetLibrary)
view = widget_library.call_method('Create', (world, module_class, None))
require(isinstance(view, unreal.RPGPlayerVitalsWidget), 'Saved class does not derive from native Vitals')
bindings = {}
for field, name, cls in (('health_bar', 'HealthBar', unreal.ProgressBar), ('mana_bar', 'ManaBar', unreal.ProgressBar),
                         ('health_text', 'HealthText', unreal.TextBlock), ('mana_text', 'ManaText', unreal.TextBlock)):
    value = view.get_editor_property(field)
    require(isinstance(value, cls) and value.get_name() == name, 'Native control did not bind: '+field)
    bindings[field] = value.get_class().get_path_name()
parent_class = unreal.load_class(None, hud_path+'.WBP_PlayerHUD_C')
parent = widget_library.call_method('Create', (world, parent_class, None))
require(parent is not None, 'Saved parent could not initialize')
def is_nested_in(widget, owner):
    outer = widget.get_outer()
    while outer is not None:
        if outer == owner:
            return True
        outer = outer.get_outer()
    return False


nested = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, module_class, False)
          if is_nested_in(w, parent)]
require(len(nested) == 1, 'Parent does not contain exactly one initialized Vitals module')
for field in bindings:
    require(nested[0].get_editor_property(field) is not None, 'Nested native binding missing: '+field)

graphs = BlueprintTools().list_graphs(hud)
for graph in graphs:
    for title in ('WBP_PlayerHealthBar', 'WBP_PlayerManaBar', 'OnCurrentHealthChanged', 'OnCurrentManaChanged',
                  'OnHealthTextChangedDelegate', 'OnManaTextChanged'):
        require(not BlueprintTools().find_nodes(graph, title), 'Old vitals graph remains: '+title)
dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
require(hud_path not in dirty and module_path not in dirty, 'Read-only compile dirtied assets')
require(all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == value for p, value in hashes.items()), 'Asset changed on disk')
output.write_text(json.dumps({'passed': True, 'compiled': True, 'saved_assets': 0, 'dirty': False,
                              'hud_widgets': len(widgets), 'module_widgets': len(skin), 'native_bindings': bindings,
                              'nested_native_bindings': True, 'preserved_canvas_slots_and_skin': True,
                              'asset_hashes': hashes, 'pie': False, 'physical_input': False}, indent=2), encoding='utf-8')
unreal.log('PLAYER_VITALS_ASSET_RELOAD PASS')
