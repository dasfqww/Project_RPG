"""Explicit rage classification for the two authored prototype skills.

Inspect by default. -RPGConfigureGreatSwordPrototype permits the two saves.
Existing montage, policy, damage and cooldown data are retained.
"""
import json
import pathlib
import unreal

args = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
apply = 'RPGConfigureGreatSwordPrototype' in args[1]
output = pathlib.Path(args[2]['RPGGreatSwordProof'])
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
records = []
for name, rage in [('AssultBlade', False), ('JumpSmash', False), ('ChargeSlash', True), ('WhirlWind', True)]:
    package = '/Game/Blueprints/GameData/Skill/DA_Skill_' + name
    if package in dirty:
        raise RuntimeError('Preserving dirty asset: ' + package)
    asset = assets.load_asset(package)
    fields = ['bGreatSwordRageSkill', 'GreatSwordRageCost', 'GreatSwordRageDamageMultiplier',
              'SkillMontage', 'DefaultExecutionPolicyClass', 'DefaultExecutionConfig', 'BaseCooldown', 'SkillDataHandle']
    before = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, fields))
    if len(before) != len(fields):
        raise RuntimeError('Rebuild/load prototype module before configuring content')
    if before['GreatSwordRageCost'] != 50 or abs(before['GreatSwordRageDamageMultiplier'] - 1.15) > 0.00001:
        raise RuntimeError('Preserving customized identity parameters')
    changed = before['bGreatSwordRageSkill'] != rage
    if changed and apply:
        if not unreal.ToolsetLibrary.set_object_properties(asset, json.dumps({'bGreatSwordRageSkill': rage})):
            raise RuntimeError('Could not classify skill')
        expected = dict(before, bGreatSwordRageSkill=rage)
        actual = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, fields))
        if actual != expected:
            raise RuntimeError('Unexpected content change; refusing save')
        if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError('Save failed: ' + package)
    after = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, fields))
    records.append({'asset': package, 'rage': after['bGreatSwordRageSkill'], 'cost': after['GreatSwordRageCost'],
                    'multiplier': after['GreatSwordRageDamageMultiplier'], 'saved': changed and apply,
                    'matches': after['bGreatSwordRageSkill'] == rage})
output.write_text(json.dumps({'passed': all(r['matches'] for r in records), 'apply': apply, 'skills': records}, indent=2), encoding='utf-8')
