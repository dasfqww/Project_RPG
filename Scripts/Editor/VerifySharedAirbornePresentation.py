"""Read-only reload verification for the shared airborne presentation profiles."""
import unreal

ROOT = '/Game/Blueprints/Combat/Reactions/'
for kind, bp_path in [('Player', '/Game/Blueprints/Character/Player/BP_Player'),
                      ('Guardian', '/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian')]:
    bp = unreal.load_asset(bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    control = cdo.get_editor_property('combat_control_component')
    profile = control.get_editor_property('airborne_presentation_profile')
    assert profile and profile.get_path_name().startswith(ROOT + 'DA_Airborne_' + kind)
    skeleton = cdo.get_editor_property('mesh').get_skeletal_mesh_asset().get_editor_property('skeleton')
    for phase in ['airborne', 'downed', 'getting_up']:
        montage = profile.get_editor_property(phase).get_editor_property('montage')
        assert montage and montage.get_editor_property('skeleton') == skeleton
        assert not unreal.AnimationLibrary.get_animation_notify_events(montage)
        assert 0 < montage.get_play_length() <= 10
        for track in montage.get_editor_property('slot_anim_tracks'):
            for segment in track.get_editor_property('anim_track').get_editor_property('anim_segments'):
                sequence = segment.get_editor_property('anim_reference')
                assert sequence.get_editor_property('skeleton') == skeleton
                assert not sequence.get_editor_property('enable_root_motion')
                assert not unreal.AnimationLibrary.get_animation_notify_events(sequence)
unreal.log('SHARED_AIRBORNE_CONTENT_RELOADED SharedProfiles=2 SkeletonsValid=1 SafeCosmeticClips=6')
