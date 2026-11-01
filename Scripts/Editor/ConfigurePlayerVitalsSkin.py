"""Hide the status-bar template's unused timer labels in the Vitals module.

Read-only by default. -RPGConfigurePlayerVitalsSkin saves only WBP_PlayerVitals;
resource bars/text, authored layout, materials and other HUD assets are preserved.
"""
import copy
import hashlib
import json
import pathlib
import runpy
import shutil

import unreal
from toolset_registry.helpers import require_editable


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


workspace = pathlib.Path(__file__).resolve().parents[2]
_, switches, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
params = {str(k).lower(): str(v) for k, v in params.items()}
apply = any(str(s).lower() == 'rpgconfigureplayervitalsskin' for s in switches)
output = pathlib.Path(params['rpgvitalsanchorproof'])
require(any(' / vitals-pie-20261101 |' in row for row in
            (workspace / 'Docs/Online_RPG_Development_Roadmap.md').read_text(encoding='utf-8-sig').splitlines()), 'Missing owner reservation')
require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve PIE')
module_path = '/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals'
module_disk = workspace / 'Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset'
require((pathlib.Path(unreal.Paths.project_content_dir()) / 'Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.uasset').resolve()
        == module_disk.resolve(), 'Reserved original module mount required')
helpers = runpy.run_path(str(workspace / 'Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py'), run_name='vitals_skin_helpers')
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
module = assets.load_asset(module_path)
require(isinstance(module, unreal.WidgetBlueprint), 'Missing Vitals WBP')
require(module_path not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Preserve dirty Vitals')
before = helpers['snapshot_widgets'](unreal, module)
require(len(before) == 13 and all(n in before for n in ('HealthBar', 'ManaBar', 'HealthText', 'ManaText')), 'Review changed Vitals structure')
expected = copy.deepcopy(before)
names = ('Health_ProgressTimeText', 'Mana_ProgressTimeText')
for name in names:
    require(before[name]['class'] == '/Script/UMG.TextBlock', 'Unexpected template timer type')
    require(before[name]['properties']['Visibility'] in ('Visible', 'Collapsed'), 'Preserve customized timer visibility')
    expected[name]['properties']['Visibility'] = 'Collapsed'
changed = before != expected
protected = [workspace / 'Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset',
             workspace / 'Content/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD.uasset']
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected + [module_disk]}
proof = {'passed': False, 'apply': apply, 'changed': changed, 'saved_assets': [], 'before_hashes': hashes,
         'hidden_template_timers': list(names)}
if apply and changed:
    require_editable(module)
    shutil.copy2(module_disk, output.parent / 'Vitals-skin-before.uasset.backup')
    infos = helpers['widget_infos'](unreal, module)
    for name in names:
        widget = infos[name].get_editor_property('widget')
        require(unreal.ToolsetLibrary.set_object_properties(widget, json.dumps({'Visibility': 'Collapsed'})), 'Could not hide unused timer')
    unreal.BlueprintEditorLibrary.compile_blueprint(module)
    require(module.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, 'Vitals compile failed')
    require(helpers['snapshot_widgets'](unreal, module) == expected, 'Unexpected layout, skin, binding or hierarchy change')
    require(all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == h for p, h in hashes.items()), 'Original content changed before save')
    require(assets.save_loaded_asset(module, only_if_is_dirty=False), 'Could not save Vitals')
    proof['saved_assets'] = [module_path]
require(all(hashlib.sha256(p.read_bytes()).hexdigest() == hashes[str(p)] for p in protected), 'Another HUD asset changed')
proof['module_sha256'] = hashlib.sha256(module_disk.read_bytes()).hexdigest()
proof['passed'] = True
output.write_text(json.dumps(proof, indent=2), encoding='utf-8')
unreal.log('PLAYER_VITALS_SKIN PASS Changed='+str(changed)+' Saved='+str(len(proof['saved_assets'])))
