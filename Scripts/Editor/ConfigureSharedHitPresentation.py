"""Configure reusable presentation profiles; attack policy is owned by grant/skill data.

Run in an editor commandlet with the current runtime and editor modules built.
Only the audited NPC success branch and two receiver BP defaults are upgraded.
"""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = "/Game/Blueprints/Combat/Reactions"
PLAYER = "/Game/Blueprints/Character/Player/BP_Player"
GUARDIAN = "/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian"
NPC_BASE = "/Game/Blueprints/Character/Shared/GameplayAbility/GA_NPC_MeleeAttack_Base"
PLAYER_ATTACK = "/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack"
NPC_ATTACK = "/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee1"
PLAYER_MONTAGE = "/Game/Blueprints/Character/Player/Anim/Montage/AM_Player_HitReaction"
NPC_MONTAGE = "/Game/Blueprints/Character/NPC/Gruntling/Anim/Montage/AM_Guardian_HitReact"


def configure():
    editor = unreal.EditorAssetLibrary
    project = Path(unreal.Paths.project_dir()).resolve()
    backup = project / "Saved/Backups/ReactionPolicyData"
    backup.mkdir(parents=True, exist_ok=True)
    originals = {}
    for asset_path in [PLAYER, GUARDIAN, NPC_BASE, PLAYER_ATTACK, NPC_ATTACK, PLAYER_MONTAGE, NPC_MONTAGE]:
        file = project / "Content" / (asset_path.removeprefix('/Game/') + '.uasset')
        data = file.read_bytes()
        originals[asset_path] = hashlib.sha256(data).hexdigest()
        target = backup / file.relative_to(project)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(data)

    saved = []

    def duplicate(source, name):
        path = ROOT + '/' + name
        existing = editor.load_asset(path) if editor.does_asset_exist(path) else None
        return existing or editor.duplicate_asset(source, path)

    def save(asset):
        assert editor.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()
        saved.append(asset.get_path_name())

    def clear_notifies(asset):
        for track in unreal.AnimationLibrary.get_animation_notify_track_names(asset):
            unreal.AnimationLibrary.remove_animation_notify_events_by_track(asset, track)
        assert not unreal.AnimationLibrary.get_animation_notify_events(asset)

    player_montage = duplicate(PLAYER_MONTAGE, 'AM_Player_LightHit')
    clear_notifies(player_montage)
    save(player_montage)
    npc_montage = duplicate(NPC_MONTAGE, 'AM_Guardian_LightHit')
    clear_notifies(npc_montage)
    tracks = list(npc_montage.get_editor_property('slot_anim_tracks'))
    for track in tracks:
        anim_track = track.get_editor_property('anim_track')
        segments = list(anim_track.get_editor_property('anim_segments'))
        for i, segment in enumerate(segments):
            old = segment.get_editor_property('anim_reference')
            name = 'A_Guardian_LightHit_Front' + ('Alt' if i else '')
            sequence = duplicate(old.get_path_name(), name)
            sequence.set_editor_property('enable_root_motion', False)
            unreal.AnimationLibrary.set_is_root_motion_lock_forced(sequence, True)
            unreal.AnimationLibrary.set_root_motion_lock_type(sequence, unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
            clear_notifies(sequence)
            save(sequence)
            segment.set_editor_property('anim_reference', sequence)
        anim_track.set_editor_property('anim_segments', segments)
        track.set_editor_property('anim_track', anim_track)
    npc_montage.set_editor_property('slot_anim_tracks', tracks)
    save(npc_montage)

    for path, montage, profile_name in [(PLAYER, player_montage, 'DA_HitReaction_Player'),
                                        (GUARDIAN, npc_montage, 'DA_HitReaction_Guardian')]:
        profile_path = ROOT + '/' + profile_name
        if editor.does_asset_exist(profile_path):
            profile = editor.load_asset(profile_path)
        else:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.RPGHitReactionProfile)
            profile = unreal.AssetToolsHelpers.get_asset_tools().create_asset(profile_name, ROOT, unreal.RPGHitReactionProfile, factory)
        bp = editor.load_asset(path)
        receiver = unreal.get_default_object(bp.generated_class()).get_editor_property('combat_reaction_component')
        assert receiver.get_outer().get_class() == bp.generated_class(), 'Refuse to edit a parent component template'
        if path == PLAYER:
            profile.set_editor_property('direction_montages', {
                getattr(unreal.RPGHitReactionDirection, section.upper()): unreal.RPGHitReactionMontage(montage=montage, section=section)
                for section in ['Front', 'Back', 'Left', 'Right']})
        else:
            profile.set_editor_property('fallback_montage', unreal.RPGHitReactionMontage(montage=montage, section='React1'))
        save(profile)
        receiver.set_editor_property('presentation_profile', profile)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert 'ERROR' not in str(bp.get_editor_property('status')).upper()
        save(bp)

    npc_bp = editor.load_asset(NPC_BASE)
    assert unreal.Project_RPGEditorUtilities.configure_npc_melee_hit_reaction_branch(npc_bp)
    save(npc_bp)
    for path in [PLAYER_ATTACK, NPC_ATTACK, PLAYER_MONTAGE, NPC_MONTAGE]:
        file = project / 'Content' / (path.removeprefix('/Game/') + '.uasset')
        assert hashlib.sha256(file.read_bytes()).hexdigest() == originals[path], 'Legacy attack/animation changed'
    retired = []
    for name in ['GA_Player_LightHitAttack', 'GA_Guardian_LightHitAttack']:
        path = ROOT + '/' + name
        if not editor.does_asset_exist(path):
            continue
        references = [r for r in editor.find_package_referencers_for_asset(path, load_assets_to_confirm=True) if r != path]
        assert not references, 'Migrate existing references before retiring the temporary GA: ' + str(references)
        file = project / 'Content' / (path.removeprefix('/Game/') + '.uasset')
        target = backup / file.relative_to(project)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(file.read_bytes())
        assert editor.delete_asset(path)
        # In a commandlet the native delete may unload the package without
        # removing its file. The audited, unreferenced package is backed up above.
        if file.exists():
            assert (project / 'Content').resolve() in file.resolve().parents
            assert hashlib.sha256(file.read_bytes()).digest() == hashlib.sha256(target.read_bytes()).digest()
            file.unlink()
        assert not file.exists(), 'Retired GA package was not removed: ' + str(file)
        retired.append(path)
    report = {'saved': saved, 'retired': retired, 'original_sha256': originals, 'legacy_attacks_retained': True,
              'startup_or_default_weapon_assignment_changed': False}
    (backup / 'Manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.log('SHARED_HIT_CONTENT_CONFIGURED Assets=%d SharedProfiles=2 PerMonsterGARequired=0 LegacyAttacksRetained=1 StartupChanged=0' % len(saved))


if __name__ == '__main__':
    configure()
