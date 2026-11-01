"""Play existing montages on the player mesh in a disposable editor world.

No package saves or synthetic damage events. Samples evaluated hand/foot poses,
montage time, charge release and spin loop/end transitions before content edits.
"""
import json
import pathlib
import time
import unreal

PATH = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())[2]['RPGGreatSwordProof']
PLAYER = unreal.get_default_object(unreal.load_class(None, '/Game/Blueprints/Character/Player/BP_Player.BP_Player_C'))
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ACTOR = MESH = None
CASES = [('AssultBlade', 'AM_PlayerSkill', 'AssultBlade', None),
         ('JumpSmash', 'AM_PlayerSkill', 'JumpSmash', None),
         ('ChargeSlash', 'AM_PlayerChargeSkill', 'Prepare', 'Attack'),
         ('WhirlWind', 'AM_PlayerToggleSkill', 'Default', 'End'),
         ('Identity', 'AM_ActivateRage', None, None)]
STATE = {'index': -1, 'records': [], 'start': time.monotonic(), 'stage_start': 0, 'callback': None}


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(STATE['callback'])
    if ACTOR:
        ACTOR.destroy_actor()
    result = {'passed': error is None, 'error': error, 'cases': STATE['records'],
              'assets_saved': 0, 'scope': 'Single-node montage playback in disposable PIE; no GAS damage/network assertions'}
    pathlib.Path(PATH).write_text(json.dumps(result, indent=2), encoding='utf-8')
    unreal.SystemLibrary.quit_editor()


def next_case():
    STATE['index'] += 1
    if STATE['index'] == len(CASES):
        finish()
        return False
    name, montage_name, section, release = CASES[STATE['index']]
    montage = unreal.load_asset('/Game/Blueprints/Character/Player/Anim/Montage/' + montage_name)
    if not montage:
        raise RuntimeError('Missing montage: ' + montage_name)
    MESH.play_animation(montage, False)
    MESH.set_update_animation_in_editor(True)
    instance = MESH.get_anim_instance()
    if section:
        instance.montage_jump_to_section(section, montage)
    record = {'skill': name, 'montage': montage.get_path_name(), 'section': section, 'release': release,
              'samples': [], 'released': False}
    STATE.update(montage=montage, instance=instance, record=record, stage_start=time.monotonic())
    STATE['records'].append(record)
    return True


def tick(delta):
    global ACTOR, MESH
    try:
        if time.monotonic() - STATE['start'] > 120:
            raise RuntimeError('Playback timed out')
        if STATE['index'] < 0:
            if not STATE.get('pie_requested'):
                if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
                    return
                if LEVEL.is_in_play_in_editor():
                    raise RuntimeError('Existing PIE must not be taken over')
                world = EDITOR.get_editor_world()
                if world.get_name() != 'Entry':
                    raise RuntimeError('Disposable Entry map required')
                world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
                LEVEL.editor_request_begin_play()
                STATE['pie_requested'] = True
                return
            world = EDITOR.get_game_world()
            if not world:
                return
            library = unreal.get_default_object(unreal.GameplayStatics)
            ACTOR = library.call_method('BeginDeferredActorSpawnFromClass', args=(world, unreal.SkeletalMeshActor, unreal.Transform(), unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN))
            ACTOR = library.call_method('FinishSpawningActor', args=(ACTOR, unreal.Transform()))
            MESH = ACTOR.skeletal_mesh_component
            MESH.set_skeletal_mesh_asset(PLAYER.mesh.get_editor_property('skeletal_mesh_asset'))
            MESH.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            if not next_case():
                return
        elapsed = time.monotonic() - STATE['stage_start']
        record = STATE['record']
        if record['release'] and elapsed >= 1.2 and not record['released']:
            STATE['instance'].montage_jump_to_section(record['release'], STATE['montage'])
            record['released'] = True
        position = STATE['instance'].montage_get_position(STATE['montage'])
        pose = []
        for bone in ('hand_r', 'hand_l', 'foot_r'):
            transform = MESH.get_socket_transform(bone, unreal.RelativeTransformSpace.RTS_COMPONENT)
            v, q = transform.translation, transform.rotation
            pose.append([v.x, v.y, v.z, q.x, q.y, q.z, q.w])
        record['samples'].append({'time': round(elapsed, 3), 'position': position,
                                  'section': str(STATE['instance'].montage_get_current_section(STATE['montage'])),
                                  'pose': pose})
        if elapsed >= 3:
            samples = record['samples']
            if len({round(s['position'], 4) for s in samples}) < 3:
                raise RuntimeError('Montage did not advance: ' + record['skill'])
            if len({str(s['pose']) for s in samples}) < 3:
                raise RuntimeError('Evaluated pose did not change: ' + record['skill'])
            record['sample_count'] = len(samples)
            record['samples'] = samples[::max(1, len(samples)//12)]
            next_case()
    except Exception as error:
        finish(str(error))


STATE['callback'] = unreal.register_slate_post_tick_callback(tick)
