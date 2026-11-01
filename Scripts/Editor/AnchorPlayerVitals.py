"""Anchor the two authored resource groups to the bottom of their host canvas.

Read-only by default. -RPGAnchorPlayerVitals saves only the existing Vitals WBP.
The 1920x1080 reference coordinates, dimensions and skin remain unchanged there.
"""
import copy
import hashlib
import json
import pathlib
import runpy
import shutil

import unreal
from toolset_registry.helpers import require_editable


def require(value, message):
    if not value:
        raise RuntimeError(message)


workspace = pathlib.Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'),
                        run_name='vitals_anchor_helpers')
_, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
params = {str(k).lower(): str(v) for k, v in params.items()}
apply = any(str(s).lower() == 'rpganchorplayervitals' for s in switches)
output = pathlib.Path(params['rpgvitalsanchorproof'])
require(any(' / vitals-pie-20261101 |' in line for line in
            (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()),
        'Missing owner reservation')
require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
module_path = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals'
hud_path = '/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD'
module_disk = workspace / 'Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset'
hud_disk = workspace / 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset'
require((pathlib.Path(unreal.Paths.project_content_dir()) / 'Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset').resolve()
        == module_disk.resolve(), 'Authoring requires the reserved original module mount')
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (module_disk, hud_disk)}
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
module, hud = assets.load_asset(module_path), assets.load_asset(hud_path)
require(isinstance(module, unreal.WidgetBlueprint) and isinstance(hud, unreal.WidgetBlueprint), 'Missing authored widgets')
require(module_path not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()},
        'Preserve dirty Vitals')
snapshot = helpers['snapshot_widgets']
before, parent_before = snapshot(unreal, module), snapshot(unreal, hud)
require(len(before) == 13 and 'PlayerVitals' in parent_before, 'Review changed module structure')
expected = copy.deepcopy(before)
targets, changed = {}, False
for name, old_x, new_x in (('Health_RPGSizeBox_Base', 500, -460), ('Mana_RPGSizeBox_Base', 1204, 244)):
    entry = before[name]
    slot = copy.deepcopy(entry['slot_properties'])
    original = {'offsets': {'left': old_x, 'top': 832, 'right': 550, 'bottom': 65},
                'anchors': {'minimum': {'x': 0, 'y': 0}, 'maximum': {'x': 0, 'y': 0}},
                'alignment': {'x': 0, 'y': 0}}
    target = copy.deepcopy(original)
    target['offsets'].update(left=new_x, top=-248)
    target['anchors'] = {'minimum': {'x': 0.5, 'y': 1}, 'maximum': {'x': 0.5, 'y': 1}}
    require(slot['LayoutData'] in (original, target) and slot['bAutoSize'] is True, 'Preserve customized resource slot: '+name)
    targets[name] = target
    changed |= slot['LayoutData'] != target
    expected[name]['slot_properties']['LayoutData'] = target
proof = {'apply': apply, 'changed': changed, 'saved_assets': [], 'before_hashes': hashes,
         'reference_size': [1920, 1080], 'before': before, 'target_slots': targets}
if apply and changed:
    require_editable(module)
    shutil.copy2(module_disk, output.parent / 'Vitals-before.uasset.backup')
    infos = helpers['widget_infos'](unreal, module)
    for name, target in targets.items():
        slot = infos[name].get_editor_property('slot')
        require(isinstance(slot, unreal.CanvasPanelSlot), 'Expected Canvas slot')
        require(unreal.ToolsetLibrary.set_object_properties(slot, json.dumps({'LayoutData': target})), 'Could not anchor: '+name)
    unreal.BlueprintEditorLibrary.compile_blueprint(module)
    require(module.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Vitals compile failed')
    require(snapshot(unreal, module) == expected, 'Unexpected skin, hierarchy or property change')
    require(snapshot(unreal, hud) == parent_before, 'Parent HUD changed')
    require(all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == digest for p, digest in hashes.items()),
            'Original disk changed during authoring')
    require(assets.save_loaded_asset(module, only_if_is_dirty=False), 'Could not save Vitals')
    proof['saved_assets'] = [module_path]
require(hashlib.sha256(hud_disk.read_bytes()).hexdigest() == hashes[str(hud_disk)], 'Parent disk changed')
proof['module_sha256'] = hashlib.sha256(module_disk.read_bytes()).hexdigest()
proof['passed'] = True
output.write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('PLAYER_VITALS_ANCHOR PASS Changed='+str(changed)+' Saved='+str(len(proof['saved_assets'])))
