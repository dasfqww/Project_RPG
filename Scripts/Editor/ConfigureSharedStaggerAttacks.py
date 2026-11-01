"""Inspect/reload the first authored stagger attacks; opt in to change two assets.

Run outside PIE. -RPGConfigureSharedStaggerAttacks changes only unconfigured or
already-matching values; custom values and dirty assets are preserved.
"""
import json
from pathlib import Path
import unreal

WEAPON = '/Game/Blueprints/Weapon/BP_PlayerWeapon'
NPC_DATA = '/Game/Blueprints/Character/NPC/Gruntling/Guardian/DA_Guardian'
NPC_ATTACK = '/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee1.GA_Guardian_Melee1_C'
PLAYER_ATTACK = '/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack.GA_Player_DefaultAttack_C'
PLAYER_SECONDS = .25
NPC_SECONDS = .35


def inspect(apply=False):
    assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None, 'Preserve active PIE'
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection({WEAPON, NPC_DATA}), 'Preserve unsaved attack data'
    weapon_bp = unreal.load_asset(WEAPON)
    npc = unreal.load_asset(NPC_DATA)
    assert weapon_bp and isinstance(npc, unreal.DataAsset_NPCStartUpData)
    cdo = unreal.get_default_object(weapon_bp.generated_class())
    data = cdo.get_editor_property('player_weapon_data')
    entries = list(data.get_editor_property('default_weapon_abilities'))
    matches = [i for i, e in enumerate(entries) if e.get_editor_property('ability_to_grant')
               and e.get_editor_property('ability_to_grant').get_path_name() == PLAYER_ATTACK]
    assert len(matches) == 1, 'Expected one existing default attack grant'
    index = matches[0]
    assert entries[index].get_editor_property('input_tag').export_text() == '(TagName="InputTag.LightAttack.GreatSword")'
    npc_class = unreal.load_class(None, NPC_ATTACK)
    assert npc_class
    other_npc_class = unreal.load_class(None, NPC_ATTACK.replace('Melee1', 'Melee2'))
    assert other_npc_class
    assert not unreal.MathLibrary.class_is_child_of(other_npc_class, npc_class), 'Preserve the other attack with an explicit zero override before changing this family'
    npc_values = dict(npc.get_editor_property('attack_staggers'))
    player_old = entries[index].get_editor_property('stagger').get_editor_property('duration')
    npc_old = npc_values[npc_class].get_editor_property('duration') if npc_class in npc_values else 0.
    assert player_old == 0 or abs(player_old - PLAYER_SECONDS) < .00001, 'Preserve custom player duration'
    assert npc_old == 0 or abs(npc_old - NPC_SECONDS) < .00001, 'Preserve custom NPC duration'
    root = Path(unreal.Paths.project_dir()).resolve()
    backup = root / 'Saved/Backups/SharedStaggerAttacks'
    changed = apply and (abs(player_old-PLAYER_SECONDS) > .00001 or abs(npc_old-NPC_SECONDS) > .00001)
    if changed:
        backup.mkdir(parents=True, exist_ok=True)
        for asset in [WEAPON, NPC_DATA]:
            file = root / 'Content' / (asset.removeprefix('/Game/') + '.uasset')
            target = backup / file.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                target.write_bytes(file.read_bytes())
        entries[index].set_editor_property('stagger', unreal.RPGStaggerSettings(duration=PLAYER_SECONDS))
        data.set_editor_property('default_weapon_abilities', entries)
        cdo.set_editor_property('player_weapon_data', data)
        npc_values[npc_class] = unreal.RPGStaggerSettings(duration=NPC_SECONDS)
        npc.set_editor_property('attack_staggers', npc_values)
        unreal.BlueprintEditorLibrary.compile_blueprint(weapon_bp)
        assert 'ERROR' not in str(weapon_bp.get_editor_property('status')).upper()
        for obj in [weapon_bp, npc]:
            assert unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False)
        player_old, npc_old = PLAYER_SECONDS, NPC_SECONDS
    assert abs(player_old-PLAYER_SECONDS)<.00001 and abs(npc_old-NPC_SECONDS)<.00001, 'Authored stagger data not configured'
    for name in ['Player', 'Guardian']:
        profile = unreal.load_asset('/Game/Blueprints/Combat/Reactions/DA_HitReaction_'+name)
        assert isinstance(profile, unreal.RPGHitReactionProfile)
    result = dict(playerSeconds=player_old, npcSeconds=npc_old, originalGAClasses=True,
                  assets=[WEAPON, NPC_DATA], applied=changed, otherNPCAttackUnaffected=True)
    unreal.log('SHARED_STAGGER_ATTACKS_VERIFIED '+json.dumps(result))
    return result


if __name__ == '__main__':
    inspect('-RPGConfigureSharedStaggerAttacks' in unreal.SystemLibrary.get_command_line())
