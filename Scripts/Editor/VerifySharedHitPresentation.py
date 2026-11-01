"""Read-only reload checks for shared presentation profiles and retired GA variants."""
import hashlib
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
base = '/Game/Blueprints/Combat/Reactions/'
report = json.loads((root / 'Saved/Backups/ReactionPolicyData/Manifest.json').read_text(encoding='utf-8'))
for saved in report['saved']:
    asset = unreal.load_asset(saved)
    assert asset, saved
    if isinstance(asset, unreal.Blueprint):
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        assert 'ERROR' not in str(asset.get_editor_property('status')).upper()
for name in ['AM_Player_LightHit', 'AM_Guardian_LightHit']:
    montage = unreal.load_asset(base + name)
    assert not unreal.AnimationLibrary.get_animation_notify_events(montage)
    for track in montage.get_editor_property('slot_anim_tracks'):
        for segment in track.get_editor_property('anim_track').get_editor_property('anim_segments'):
            sequence = segment.get_editor_property('anim_reference')
            assert not unreal.AnimationLibrary.is_root_motion_enabled(sequence)
            assert not unreal.AnimationLibrary.get_animation_notify_events(sequence)
for name in ['GA_Player_LightHitAttack', 'GA_Guardian_LightHitAttack']:
    assert not unreal.EditorAssetLibrary.does_asset_exist(base + name), 'Retired GA still exists'
for path, profile_name in [
    ('/Game/Blueprints/Character/Player/BP_Player', 'DA_HitReaction_Player'),
    ('/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian', 'DA_HitReaction_Guardian'),
]:
    bp = unreal.load_asset(path)
    receiver = unreal.get_default_object(bp.generated_class()).get_editor_property('combat_reaction_component')
    profile = receiver.get_editor_property('presentation_profile')
    assert isinstance(profile, unreal.RPGHitReactionProfile)
    assert profile == unreal.load_asset(base + profile_name)
    if profile_name.endswith('Player'):
        assert len(profile.get_editor_property('direction_montages')) == 4
    else:
        assert profile.get_editor_property('fallback_montage').get_editor_property('montage').get_name() == 'AM_Guardian_LightHit'
for path, old_hash in report['original_sha256'].items():
    if path.endswith(('BP_Player', 'BP_Gruntling_Guardian', 'GA_NPC_MeleeAttack_Base')):
        continue
    file = root / 'Content' / (path.removeprefix('/Game/') + '.uasset')
    assert hashlib.sha256(file.read_bytes()).hexdigest() == old_hash
unreal.log('SHARED_HIT_CONTENT_RELOADED SharedProfiles=2 PerMonsterGARequired=0 NotifyFree=1 RootMotionFree=1 LegacyAssetsUnchanged=1')
