"""Add the native debuff row directly above the right-hand mana bar.

Read-only by default; -RPGConfigureDebuffHUD saves only WBP_PlayerVitals.
Works in an owned closed runtime/Content overlay before installing the asset.
"""
import copy
import hashlib
import json
import pathlib
import runpy
import unreal
from toolset_registry.helpers import require_editable


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


workspace = pathlib.Path(__file__).resolve().parents[2]
_, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
params = {str(k).lower(): str(v) for k, v in params.items()}
apply = any(str(s).lower() == 'rpgconfiguredebuffhud' for s in switches)
output = pathlib.Path(params['rpgdebuffhudproof'])
require(any(' / player-debuff-hud-20261102 |' in row for row in
            (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()),
        'Missing owner reservation')
require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='debuff_layout_helpers')
path = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals'
disk = pathlib.Path(unreal.Paths.project_content_dir()) / 'Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset'
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
module = assets.load_asset(path)
require(isinstance(module, unreal.WidgetBlueprint), 'Missing Vitals module')
require(path not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Preserve dirty Vitals')
before = helpers['snapshot_widgets'](unreal, module)
infos = helpers['widget_infos'](unreal, module)
require(all(n in before for n in ('Mana_RPGSizeBox_Base', 'HealthBar', 'ManaBar', 'HealthText', 'ManaText')), 'Review changed Vitals bindings')
mana = infos['Mana_RPGSizeBox_Base']
parent = mana.get_editor_property('parent')
require(isinstance(parent, unreal.CanvasPanel), 'Mana must be on the authored Vitals canvas')
layout = copy.deepcopy(before['Mana_RPGSizeBox_Base']['slot_properties']['LayoutData'])
require(layout['anchors']['minimum'] == layout['anchors']['maximum'], 'Review stretched mana anchor')
layout['offsets']['top'] -= 62
layout['offsets']['right'] = 390
layout['offsets']['bottom'] = 58
target = {'LayoutData': layout, 'bAutoSize': False, 'ZOrder': 3}
kind = unreal.load_class(None, '/Script/Project_RPG.RPGPlayerDebuffBarWidget')
require(kind is not None, 'The debuff module must be compiled before asset authoring')
changed = 'PlayerDebuffs' not in before
proof = {'passed': False, 'apply': apply, 'changed': changed, 'saved_assets': [],
         'placement': 'above_right_mana', 'target_slot': target, 'before_sha256': hashlib.sha256(disk.read_bytes()).hexdigest()}
if not changed:
    entry = before['PlayerDebuffs']
    require(entry['class'] == '/Script/Project_RPG.RPGPlayerDebuffBarWidget', 'Preserve customized debuff child')
    require(all(entry['slot_properties'][key] == value for key, value in target.items()), 'Preserve customized debuff layout')
elif apply:
    require_editable(module)
    tool = unreal.get_default_object(unreal.UMGToolSet)
    added = tool.call_method('AddWidget', (module, kind, 'PlayerDebuffs', parent, -1))
    slot = added.get_editor_property('slot')
    require(isinstance(slot, unreal.CanvasPanelSlot), 'Could not add native debuff module')
    require(unreal.ToolsetLibrary.set_object_properties(slot, json.dumps(target)), 'Could not position debuffs above mana')
    unreal.BlueprintEditorLibrary.compile_blueprint(module)
    require(module.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Vitals compile failed')
    after = helpers['snapshot_widgets'](unreal, module)
    require(set(after) == set(before) | {'PlayerDebuffs'}, 'Unexpected widget additions/removals')
    for name, old in before.items():
        old, current = copy.deepcopy(old), copy.deepcopy(after[name])
        if name == parent.get_name():
            old['properties'].pop('Slots', None)
            current['properties'].pop('Slots', None)
        require(old == current, 'Existing Vitals widget changed: '+name)
    require(hashlib.sha256(disk.read_bytes()).hexdigest() == proof['before_sha256'], 'Disk changed during authoring')
    require(assets.save_loaded_asset(module, only_if_is_dirty=False), 'Could not save Vitals')
    proof['saved_assets'] = [path]
proof['module_sha256'] = hashlib.sha256(disk.read_bytes()).hexdigest()
proof['widget_count'] = len(helpers['snapshot_widgets'](unreal, module))
proof['passed'] = True
output.write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('PLAYER_DEBUFF_HUD_LAYOUT PASS Changed='+str(changed)+' Saved='+str(len(proof['saved_assets'])))
