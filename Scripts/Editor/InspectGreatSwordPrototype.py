"""Read authored skills/montages without saving. Used by the prototype runner."""
import json
import pathlib
import unreal


def main():
    records = []
    for name in ("AssultBlade", "JumpSmash", "ChargeSlash", "WhirlWind"):
        asset = unreal.load_asset('/Game/Blueprints/GameData/Skill/DA_Skill_' + name)
        if not asset:
            # Locate by stable asset name, rather than guessing a different skill.
            matches = [a for a in unreal.AssetRegistryHelpers.get_asset_registry().get_all_assets()
                       if str(a.asset_name) == 'DA_Skill_' + name]
            if len(matches) != 1:
                raise RuntimeError('Missing/ambiguous skill: ' + name)
            asset = matches[0].get_asset()
        fields = ['SkillTag', 'SkillMontage', 'SkillDataHandle', 'DefaultExecutionPolicyClass',
                  'DefaultExecutionConfig', 'BaseCooldown', 'DefaultTargetingConfig']
        record = json.loads(unreal.ToolsetLibrary.get_object_properties(asset, fields))
        record['asset'] = asset.get_path_name()
        montage = asset.get_editor_property('skill_montage')
        if montage:
            record['montage'] = montage.get_path_name()
            record['animation'] = json.loads(unreal.ToolsetLibrary.get_object_properties(
                montage, ['Skeleton', 'CompositeSections', 'SlotAnimTracks', 'Notifies']))
        records.append(record)
    bp = unreal.load_class(None, '/Game/Blueprints/Character/Player/BP_Player.BP_Player_C')
    cdo = unreal.get_default_object(bp)
    mesh = cdo.get_editor_property('mesh')
    result = {'skills': records, 'mesh': mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
              'anim_class': mesh.get_editor_property('anim_class').get_path_name(),
              'mesh_methods': [x for x in dir(mesh) if any(y in x for y in ('tick_animation', 'refresh_bone', 'animation'))]}
    path = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())[2]['RPGGreatSwordProof']
    pathlib.Path(path).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('RPG_GREATSWORD_INSPECTION_COMPLETE')


main()
