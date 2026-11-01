"""Build safe, reusable airborne/down/get-up profiles without editing source animations.

Guardian uses crouched stun recovery as temporary art; no prone get-up clip is shipped.
Run against the isolated, freshly compiled project before installing reviewed assets.
"""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = '/Game/Blueprints/Combat/Reactions'
PLAYER = '/Game/Blueprints/Character/Player/BP_Player'
GUARDIAN = '/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian'
NPC_ANIMS = '/Game/Assets/NPC/Gruntling/Enemy_Gruntling_Guardian/Enemy_Gruntling_Guardian_Animation/'
SOURCES = {
    'Player': ['/Game/Assets/PlayerAnims/GreatSword/' + n for n in
               ['MM_Fall_Loop', 'Frank_RPG_GreatSword_Hit_Knockdown_Loop', 'Frank_RPG_GreatSword_Getup01']],
    'Guardian': [NPC_ANIMS + n for n in ['ExoGame_Gruntling_Falling', 'ExoGame_Gruntling_React_Stunned_Idle', 'ExoGame_Gruntling_React_Stunned_Exit']],
}


def configure():
    editor = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    project = Path(unreal.Paths.project_dir()).resolve()
    backup = project / 'Saved/Backups/SharedAirborne'
    backup.mkdir(parents=True, exist_ok=True)
    source_hashes = {}
    saved = []

    def file(path):
        return project / 'Content' / (path.removeprefix('/Game/') + '.uasset')

    def protect(path):
        source = file(path)
        if source.exists():
            target = backup / source.relative_to(project)
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                target.write_bytes(source.read_bytes())

    def save(asset):
        assert editor.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()
        saved.append(asset.get_path_name().split('.')[0])

    for kind, bp_path in [('Player', PLAYER), ('Guardian', GUARDIAN)]:
        bp = editor.load_asset(bp_path)
        cdo = unreal.get_default_object(bp.generated_class())
        receiver = cdo.get_editor_property('combat_control_component')
        assert receiver.get_outer().get_class() == bp.generated_class(), 'Parent template must remain untouched'
        skeleton = cdo.get_editor_property('mesh').get_skeletal_mesh_asset().get_editor_property('skeleton')
        template = editor.load_asset(ROOT + '/AM_' + kind + '_LightHit')
        slot_name = template.get_editor_property('slot_anim_tracks')[0].get_editor_property('slot_name')
        stages = {}
        for phase, source in zip(['Airborne', 'Downed', 'GettingUp'], SOURCES[kind]):
            source_hashes[source] = hashlib.sha256(file(source).read_bytes()).hexdigest()
            seq_path = ROOT + '/A_' + kind + '_' + phase
            seq = editor.load_asset(seq_path) if editor.does_asset_exist(seq_path) else editor.duplicate_asset(source, seq_path)
            assert seq and seq.get_editor_property('skeleton') == skeleton, source
            protect(seq_path)
            seq.set_editor_property('enable_root_motion', False)
            unreal.AnimationLibrary.set_is_root_motion_lock_forced(seq, True)
            unreal.AnimationLibrary.set_root_motion_lock_type(seq, unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
            for track in unreal.AnimationLibrary.get_animation_notify_track_names(seq):
                unreal.AnimationLibrary.remove_animation_notify_events_by_track(seq, track)
            assert not unreal.AnimationLibrary.get_animation_notify_events(seq)
            save(seq)
            name = 'AM_' + kind + '_' + phase
            path = ROOT + '/' + name
            protect(path)
            montage = editor.load_asset(path) if editor.does_asset_exist(path) else None
            if not montage:
                factory = unreal.AnimMontageFactory()
                factory.set_editor_property('source_animation', seq)
                factory.set_editor_property('target_skeleton', skeleton)
                montage = tools.create_asset(name, ROOT, unreal.AnimMontage, factory)
            tracks = list(montage.get_editor_property('slot_anim_tracks'))
            assert len(tracks) == 1
            tracks[0].set_editor_property('slot_name', slot_name)
            montage.set_editor_property('slot_anim_tracks', tracks)
            assert not unreal.AnimationLibrary.get_animation_notify_events(montage)
            save(montage)
            stages[phase] = unreal.RPGHitReactionMontage(montage=montage)
        name = 'DA_Airborne_' + kind
        path = ROOT + '/' + name
        protect(path)
        profile = editor.load_asset(path) if editor.does_asset_exist(path) else None
        if not profile:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.RPGAirbornePresentationProfile)
            profile = tools.create_asset(name, ROOT, unreal.RPGAirbornePresentationProfile, factory)
        for phase, property_name in [('Airborne', 'airborne'), ('Downed', 'downed'), ('GettingUp', 'getting_up')]:
            profile.set_editor_property(property_name, stages[phase])
        save(profile)
        protect(bp_path)
        receiver.set_editor_property('airborne_presentation_profile', profile)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert 'ERROR' not in str(bp.get_editor_property('status')).upper()
        save(bp)
    for source, expected in source_hashes.items():
        assert hashlib.sha256(file(source).read_bytes()).hexdigest() == expected, 'Source animation changed'
    report = {'saved': saved, 'sourceSHA256': source_hashes, 'guardianArt': 'temporary crouched down/recovery, not prone get-up'}
    (backup / 'Manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.log('SHARED_AIRBORNE_CONTENT_CONFIGURED SharedProfiles=2 SourcesUnchanged=1 NewAbilityClasses=0')


if __name__ == '__main__':
    configure()
